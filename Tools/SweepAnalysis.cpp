/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    Tools/SweepAnalysis.cpp - the measurement rig that the design constants were
    tuned against.

    For a range of CUTOFF positions it evaluates the *exact* linear response of
    the plugin (ResponseModel is not an approximation of the running filters -
    see the header) against several reference spectra, and reports:

        L    K-weighted loudness, dB relative to the unfiltered source
        C    spectral centroid on a log-frequency axis, in Hz
        C/fc how far the centroid sits above the cutoff, in octaves
        HF   fraction of the surviving energy above 2 kHz

    and does the same for a conventional high-pass of the same slope, so the two
    can be compared directly.

    Run with:  brownsweep_analysis [--csv] [--slope N] [--character X]
*/

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../Source/dsp/BrownSweepEngine.h"
#include "../Source/dsp/LoudnessSchedule.h"
#include "../Source/dsp/ResponseModel.h"

using namespace bsweep;

namespace
{

constexpr int   kNumBins = 512;
constexpr float kLowHz   = 15.0f;
constexpr float kHighHz  = 21000.0f;

struct Grid
{
    std::vector<float> freq, kWeight;

    Grid()
    {
        freq.resize (kNumBins);
        kWeight.resize (kNumBins);
        for (int i = 0; i < kNumBins; ++i)
        {
            const float t = static_cast<float> (i) / (kNumBins - 1);
            freq[static_cast<size_t> (i)]    = kLowHz * std::pow (kHighHz / kLowHz, t);
            kWeight[static_cast<size_t> (i)] = kWeightingMagnitudeSquared (freq[static_cast<size_t> (i)]);
        }
    }
};

const Grid& grid() { static Grid g; return g; }

/** Per-log-bin energy of a reference source. */
std::vector<float> sourceSpectrum (const std::string& name)
{
    const auto& g = grid();
    std::vector<float> s (kNumBins, 0.0f);

    for (int i = 0; i < kNumBins; ++i)
    {
        const float f = g.freq[static_cast<size_t> (i)];

        if (name == "pink")        s[static_cast<size_t> (i)] = 1.0f;                       // -3 dB/oct
        else if (name == "brown")  s[static_cast<size_t> (i)] = 1000.0f / f;                // -6 dB/oct
        else if (name == "white")  s[static_cast<size_t> (i)] = f / 1000.0f;                //  0 dB/oct
        else if (name == "lead")
        {
            // Saw-ish lead at 110 Hz: 1/n harmonic amplitudes (-6 dB/oct), with
            // a -12 dB/oct extra roll-off above 6 kHz, i.e. a typical
            // supersaw-through-a-lowpass psytrance voice.
            if (f < 105.0f) s[static_cast<size_t> (i)] = 0.0f;
            else
            {
                float e = 110.0f / f;
                if (f > 6000.0f) e *= std::pow (6000.0f / f, 2.0f);
                s[static_cast<size_t> (i)] = e;
            }
        }
        else s[static_cast<size_t> (i)] = 1.0f;
    }

    return s;
}

struct Metrics { float loudnessDb, centroidHz, hfFraction, hfAbsoluteDb; };

Metrics analyse (const std::vector<float>& source, const std::vector<float>& mag2,
                 double referenceEnergy)
{
    const auto& g = grid();

    double energy = 0.0, kEnergy = 0.0, logSum = 0.0, hfEnergy = 0.0, hfK = 0.0;

    for (int i = 0; i < kNumBins; ++i)
    {
        const double e = static_cast<double> (source[static_cast<size_t> (i)])
                       * static_cast<double> (mag2[static_cast<size_t> (i)]);
        energy  += e;
        kEnergy += e * static_cast<double> (g.kWeight[static_cast<size_t> (i)]);
        logSum  += e * std::log2 (static_cast<double> (g.freq[static_cast<size_t> (i)]));
        if (g.freq[static_cast<size_t> (i)] > 2000.0f) hfEnergy += e;
        if (g.freq[static_cast<size_t> (i)] > 4000.0f)
            hfK += e * static_cast<double> (g.kWeight[static_cast<size_t> (i)]);
    }

    Metrics m {};
    m.loudnessDb = static_cast<float> (10.0 * std::log10 (std::max (kEnergy / referenceEnergy, 1.0e-12)));
    m.centroidHz = energy > 0.0 ? static_cast<float> (std::exp2 (logSum / energy)) : 0.0f;
    m.hfFraction = energy > 0.0 ? static_cast<float> (hfEnergy / energy) : 0.0f;
    // Absolute level of everything above 4 kHz, relative to the *unfiltered*
    // source.  This is the number that explains why a conventional HP sweep
    // sounds aggressive: it barely moves.
    m.hfAbsoluteDb = static_cast<float> (10.0 * std::log10 (std::max (hfK / referenceEnergy, 1.0e-12)));
    return m;
}

double referenceKEnergy (const std::vector<float>& source)
{
    const auto& g = grid();
    double e = 0.0;
    for (int i = 0; i < kNumBins; ++i)
        e += static_cast<double> (source[static_cast<size_t> (i)])
           * static_cast<double> (g.kWeight[static_cast<size_t> (i)]);
    return std::max (e, 1.0e-30);
}

std::vector<float> brownSweepMag2 (float u, float character, float resonance, int slope, bool autoGain)
{
    const auto& g = grid();

    ResponseState st;
    st.cutoffHz        = positionToCutoff (u);
    st.slopeIndex      = slope;
    st.resonance01     = resonance;
    st.tiltDbPerOctave = tiltSlopeDbPerOctave (u, character);
    st.wetGainDb       = autoGain ? LoudnessSchedule::instance().gainDb (u, character, resonance, slope) : 0.0f;
    st.analog01        = 0.0f;
    st.mix             = 1.0f;
    st.outputDb        = 0.0f;
    st.sampleRate      = 0.0f;   // analogue prototype

    std::vector<float> m (kNumBins);
    for (int i = 0; i < kNumBins; ++i)
        m[static_cast<size_t> (i)] = std::norm (responsemodel::wetResponse (st, g.freq[static_cast<size_t> (i)]));
    return m;
}

std::vector<float> conventionalMag2 (float u, float resonance, int slope)
{
    const auto& g = grid();
    const float fc = positionToCutoff (u);

    std::vector<float> m (kNumBins);
    for (int i = 0; i < kNumBins; ++i)
        m[static_cast<size_t> (i)] =
            std::norm (responsemodel::highPassResponse (g.freq[static_cast<size_t> (i)], fc, slope, resonance, 0.0f));
    return m;
}

void reportTiltAccuracy()
{
    std::printf ("\n== Tilt cascade accuracy (anchor = cutoff * 2^%.2f, %d sections) ==\n",
                 kTiltAnchorOctaves, kTiltSections);
    std::printf ("%8s | %-12s %-11s %-12s %-10s\n",
                 "dB/oct", "fit 2..8oct", "ripple pp", "-1dB point", "-3dB point");

    const float fc = 100.0f;

    for (float target : { 1.0f, 2.0f, 3.0f, 4.5f, 6.0f })
    {
        auto db = [&] (float f) {
            return gainToDb (std::abs (responsemodel::tiltResponse (f, fc, target, 0.0f)));
        };

        // Least-squares slope over the interior of the tilt region (2 to 8
        // octaves above the cutoff).  Below 2 octaves the response is still
        // curving into the tilt at the anchor, which is by design.
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int cnt = 0;
        float minErr = 1e9f, maxErr = -1e9f;
        for (int i = 0; i <= 140; ++i)
        {
            const float oct = 2.0f + 6.0f * i / 140.0f;
            const float x = oct, y = db (fc * std::exp2 (oct));
            sx += x; sy += y; sxx += x * x; sxy += x * y; ++cnt;
        }
        const double slope = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);
        const double icept = (sy - slope * sx) / cnt;

        for (int i = 0; i <= 140; ++i)
        {
            const float oct = 2.0f + 6.0f * i / 140.0f;
            const float err = static_cast<float> (db (fc * std::exp2 (oct)) - (slope * oct + icept));
            minErr = std::min (minErr, err);
            maxErr = std::max (maxErr, err);
        }

        // Where does the tilt first reach -1 dB / -3 dB, in octaves above fc?
        float oct1 = 0.0f, oct3 = 0.0f;
        for (int i = 0; i <= 1200; ++i)
        {
            const float oct = 10.0f * i / 1200.0f;
            const float d = db (fc * std::exp2 (oct));
            if (oct1 == 0.0f && d <= -1.0f) oct1 = oct;
            if (oct3 == 0.0f && d <= -3.0f) oct3 = oct;
        }

        std::printf ("%6.1f   | %10.2f   %10.2f   %8.2f oct %8.2f oct\n",
                     target, slope, maxErr - minErr, oct1, oct3);
    }
}

void reportSweep (const std::string& sourceName, float character, float resonance, int slope, bool csv)
{
    const auto source = sourceSpectrum (sourceName);
    const double ref  = referenceKEnergy (source);

    if (! csv)
    {
        std::printf ("\n== %s | CHARACTER %.0f%% | RESONANCE %.0f%% | %d dB/oct ==\n",
                     sourceName.c_str(), character * 100.0f, resonance * 100.0f,
                     kSlopeConfigs[slope].dbPerOctave);
        std::printf ("%7s %6s | %8s %7s %7s | %8s %7s %7s | %7s\n",
                     "cutoff", "tilt",
                     "BS L", "BS cen", "BS >4k", "HP L", "HP cen", "HP >4k", "dL/du");
    }
    else
    {
        std::printf ("source,cutoffHz,tiltDbOct,bs_loudnessDb,bs_centroidHz,bs_centroidOct,bs_hf,"
                     "hp_loudnessDb,hp_centroidHz,hp_centroidOct,hp_hf4kDb\n");
    }

    float prevBs = 0.0f;

    for (int i = 0; i <= 40; ++i)
    {
        const float u  = static_cast<float> (i) / 40.0f;
        const float fc = positionToCutoff (u);

        const auto bs = analyse (source, brownSweepMag2 (u, character, resonance, slope, true), ref);
        const auto hp = analyse (source, conventionalMag2 (u, resonance, slope), ref);

        const float bsOct = std::log2 (std::max (bs.centroidHz, 1.0f) / fc);
        const float hpOct = std::log2 (std::max (hp.centroidHz, 1.0f) / fc);

        if (csv)
        {
            std::printf ("%s,%.1f,%.2f,%.2f,%.1f,%.2f,%.3f,%.2f,%.1f,%.2f,%.3f\n",
                         sourceName.c_str(), fc, tiltSlopeDbPerOctave (u, character),
                         bs.loudnessDb, bs.centroidHz, bsOct, bs.hfAbsoluteDb,
                         hp.loudnessDb, hp.centroidHz, hpOct, hp.hfAbsoluteDb);
        }
        else
        {
            std::printf ("%7.0f %6.2f | %8.2f %7.0f %7.1f | %8.2f %7.0f %7.1f | %7.1f\n",
                         fc, tiltSlopeDbPerOctave (u, character),
                         bs.loudnessDb, bs.centroidHz, bs.hfAbsoluteDb,
                         hp.loudnessDb, hp.centroidHz, hp.hfAbsoluteDb,
                         i == 0 ? 0.0f : (bs.loudnessDb - prevBs) * 40.0f);
        }

        prevBs = bs.loudnessDb;
    }
}

} // namespace

int main (int argc, char** argv)
{
    bool  csv       = false;
    int   slope     = 2;
    float character = 1.0f;
    float resonance = 0.0f;
    bool  tiltOnly  = false;
    std::string only;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--csv") csv = true;
        else if (a == "--tilt") tiltOnly = true;
        else if (a == "--slope" && i + 1 < argc) slope = std::atoi (argv[++i]);
        else if (a == "--character" && i + 1 < argc) character = static_cast<float> (std::atof (argv[++i]));
        else if (a == "--resonance" && i + 1 < argc) resonance = static_cast<float> (std::atof (argv[++i]));
        else if (a == "--source" && i + 1 < argc) only = argv[++i];
    }

    slope = clampValue (slope, 0, kNumSlopes - 1);

    if (tiltOnly) { reportTiltAccuracy(); return 0; }

    if (! csv) reportTiltAccuracy();

    if (! only.empty())
    {
        reportSweep (only, character, resonance, slope, csv);
    }
    else
    {
        for (const char* s : { "pink", "brown", "white", "lead" })
            reportSweep (s, character, resonance, slope, csv);
    }

    return 0;
}
