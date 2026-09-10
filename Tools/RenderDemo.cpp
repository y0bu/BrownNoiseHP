/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    Tools/RenderDemo.cpp - renders the audio examples.

    A band-limited detuned saw stack (the psytrance staple) with the cutoff
    swept from the bottom of the range to the top, rendered through several
    settings so they can be compared directly.

    Nothing is normalised and every render uses the same source at the same
    level, because the loudness trajectory is the thing being demonstrated.
    Normalising these files would hide the entire point.

    Usage:  brownsweep_demo [output directory]
*/

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../Source/dsp/BrownSweepEngine.h"

using namespace bsweep;

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 256;

//==============================================================================
// A very small 16-bit WAV writer
//==============================================================================

void writeLittleEndian (std::FILE* f, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i) { const uint8_t b = static_cast<uint8_t> (value >> (8 * i)); std::fwrite (&b, 1, 1, f); }
}

bool writeWav (const std::string& path, const std::vector<std::vector<float>>& channels, double sampleRate)
{
    const int numChannels = static_cast<int> (channels.size());
    const int numFrames   = numChannels > 0 ? static_cast<int> (channels[0].size()) : 0;
    const int bytesPerSample = 2;
    const uint32_t dataBytes = static_cast<uint32_t> (numFrames * numChannels * bytesPerSample);

    std::FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;

    std::fwrite ("RIFF", 1, 4, f);
    writeLittleEndian (f, 36 + dataBytes, 4);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    writeLittleEndian (f, 16, 4);                                        // fmt chunk size
    writeLittleEndian (f, 1, 2);                                         // PCM
    writeLittleEndian (f, static_cast<uint32_t> (numChannels), 2);
    writeLittleEndian (f, static_cast<uint32_t> (sampleRate), 4);
    writeLittleEndian (f, static_cast<uint32_t> (sampleRate) * static_cast<uint32_t> (numChannels * bytesPerSample), 4);
    writeLittleEndian (f, static_cast<uint32_t> (numChannels * bytesPerSample), 2);
    writeLittleEndian (f, 16, 2);                                        // bits per sample
    std::fwrite ("data", 1, 4, f);
    writeLittleEndian (f, dataBytes, 4);

    for (int i = 0; i < numFrames; ++i)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float v = clampValue (channels[static_cast<size_t> (ch)][static_cast<size_t> (i)], -1.0f, 1.0f);
            writeLittleEndian (f, static_cast<uint32_t> (static_cast<int16_t> (std::lround (v * 32767.0f))), 2);
        }

    std::fclose (f);
    return true;
}

//==============================================================================
// Source: a band-limited detuned saw stack
//==============================================================================

std::vector<float> makeSawTable (int size, int numHarmonics)
{
    std::vector<float> table (static_cast<size_t> (size), 0.0f);

    for (int n = 1; n <= numHarmonics; ++n)
        for (int i = 0; i < size; ++i)
            table[static_cast<size_t> (i)] +=
                static_cast<float> (std::sin (2.0 * kPi * n * i / size) / n);

    float peak = 0.0f;
    for (float v : table) peak = std::max (peak, std::fabs (v));
    for (float& v : table) v /= peak;

    return table;
}

std::vector<std::vector<float>> renderSupersaw (double baseHz, double seconds, float peakLevel)
{
    const int numFrames = static_cast<int> (seconds * kSampleRate);

    // Harmonics up to 18 kHz, so the source itself is clean and any brightness
    // you hear belongs to the filter rather than to aliasing.
    const int numHarmonics = std::max (1, static_cast<int> (18000.0 / baseHz));
    const auto table = makeSawTable (4096, numHarmonics);
    const double tableSize = static_cast<double> (table.size());

    constexpr int kVoices = 7;
    const double detuneCents[kVoices] = { -14.0, -9.0, -4.0, 0.0, 4.0, 9.0, 14.0 };
    const double pan[kVoices]         = { -0.7, -0.45, -0.2, 0.0, 0.2, 0.45, 0.7 };

    Xorshift32 rng (0xC0FFEEu);
    double phase[kVoices];
    for (int v = 0; v < kVoices; ++v) phase[v] = rng.nextUnipolar();

    std::vector<std::vector<float>> out (2, std::vector<float> (static_cast<size_t> (numFrames), 0.0f));

    for (int v = 0; v < kVoices; ++v)
    {
        const double hz = baseHz * std::pow (2.0, detuneCents[v] / 1200.0);
        const double increment = hz / kSampleRate;
        const float left  = static_cast<float> (std::sqrt (0.5 * (1.0 - pan[v])));
        const float right = static_cast<float> (std::sqrt (0.5 * (1.0 + pan[v])));

        for (int i = 0; i < numFrames; ++i)
        {
            const double pos = phase[v] * tableSize;
            const int i0 = static_cast<int> (pos) % table.size();
            const int i1 = (i0 + 1) % static_cast<int> (table.size());
            const float frac = static_cast<float> (pos - std::floor (pos));
            const float s = lerp (table[static_cast<size_t> (i0)], table[static_cast<size_t> (i1)], frac);

            out[0][static_cast<size_t> (i)] += s * left;
            out[1][static_cast<size_t> (i)] += s * right;

            phase[v] += increment;
            if (phase[v] >= 1.0) phase[v] -= 1.0;
        }
    }

    // Short fades so nothing clicks at the file boundaries.
    const int fade = static_cast<int> (0.01 * kSampleRate);
    for (int i = 0; i < fade; ++i)
    {
        const float g = static_cast<float> (i) / static_cast<float> (fade);
        for (auto& ch : out)
        {
            ch[static_cast<size_t> (i)] *= g;
            ch[static_cast<size_t> (numFrames - 1 - i)] *= g;
        }
    }

    float peak = 0.0f;
    for (const auto& ch : out) for (float v : ch) peak = std::max (peak, std::fabs (v));
    const float gain = peakLevel / std::max (peak, 1.0e-9f);
    for (auto& ch : out) for (float& v : ch) v *= gain;

    return out;
}

//==============================================================================
// A traditional Goa kick and offbeat bass, for putting the filter in context
//==============================================================================

/** Psy/Goa kick: a fast downward pitch sweep into a short body, plus a click.
    Short and punchy - it has to get out of the way of the offbeat bass. */
void addKick (std::vector<std::vector<float>>& out, int at, float level, uint32_t seed)
{
    const int numFrames = static_cast<int> (out[0].size());
    const int length    = static_cast<int> (0.40 * kSampleRate);

    Xorshift32 rng (seed);
    double phase = 0.0;

    for (int i = 0; i < length; ++i)
    {
        const int index = at + i;
        if (index < 0 || index >= numFrames) continue;

        const double t = static_cast<double> (i) / kSampleRate;

        // 200 Hz down to 48 Hz in about 15 ms: the "thump".
        const double hz = 48.0 + 152.0 * std::exp (-t / 0.015);
        phase += hz / kSampleRate;

        const double body  = std::sin (2.0 * kPi * phase) * std::exp (-t / 0.105);
        const double click = rng.nextBipolar() * std::exp (-t / 0.0035) * 0.30;

        const float s = static_cast<float> (std::tanh ((body + click) * 1.5) / 1.5) * level;
        out[0][static_cast<size_t> (index)] += s;
        out[1][static_cast<size_t> (index)] += s;
    }
}

/** The offbeat bass note: short, saturated, always on the root.  The saturation
    is what makes it audible on a small speaker - a pure 55 Hz sine mostly is
    not. */
void addBass (std::vector<std::vector<float>>& out, int at, double hz, float level)
{
    const int numFrames = static_cast<int> (out[0].size());
    const int length    = static_cast<int> (0.17 * kSampleRate);

    for (int i = 0; i < length; ++i)
    {
        const int index = at + i;
        if (index < 0 || index >= numFrames) continue;

        const double t = static_cast<double> (i) / kSampleRate;
        const double attack  = 1.0 - std::exp (-t / 0.0025);
        const double decay   = std::exp (-t / 0.055);
        const double release = std::min (1.0, (length - i) / (0.012 * kSampleRate));
        const double env = attack * decay * release;

        const double tone = std::sin (2.0 * kPi * hz * t) + 0.25 * std::sin (4.0 * kPi * hz * t);
        const float s = static_cast<float> (std::tanh (tone * env * 2.0) / 2.0) * level;

        out[0][static_cast<size_t> (index)] += s;
        out[1][static_cast<size_t> (index)] += s;
    }
}

/** Sustained minor-chord saw pad: three notes, three detuned voices each. */
std::vector<std::vector<float>> renderPad (double seconds, float peakLevel)
{
    const int numFrames = static_cast<int> (seconds * kSampleRate);
    const auto table = makeSawTable (4096, 160);
    const double tableSize = static_cast<double> (table.size());

    const double notes[3] = { 110.00, 130.81, 164.81 };          // A minor
    const double detune[3] = { -8.0, 0.0, 8.0 };                 // cents
    const double pan[3]    = { -0.6, 0.0, 0.6 };

    Xorshift32 rng (0xBEEFu);
    std::vector<std::vector<float>> out (2, std::vector<float> (static_cast<size_t> (numFrames), 0.0f));

    for (int n = 0; n < 3; ++n)
        for (int v = 0; v < 3; ++v)
        {
            const double hz = notes[n] * std::pow (2.0, detune[v] / 1200.0);
            const double increment = hz / kSampleRate;
            const float left  = static_cast<float> (std::sqrt (0.5 * (1.0 - pan[v])));
            const float right = static_cast<float> (std::sqrt (0.5 * (1.0 + pan[v])));

            double phase = rng.nextUnipolar();

            for (int i = 0; i < numFrames; ++i)
            {
                const double pos = phase * tableSize;
                const int i0 = static_cast<int> (pos) % static_cast<int> (table.size());
                const int i1 = (i0 + 1) % static_cast<int> (table.size());
                const float frac = static_cast<float> (pos - std::floor (pos));
                const float s = lerp (table[static_cast<size_t> (i0)], table[static_cast<size_t> (i1)], frac);

                out[0][static_cast<size_t> (i)] += s * left;
                out[1][static_cast<size_t> (i)] += s * right;

                phase += increment;
                if (phase >= 1.0) phase -= 1.0;
            }
        }

    float peak = 0.0f;
    for (const auto& ch : out) for (float v : ch) peak = std::max (peak, std::fabs (v));
    const float gain = peakLevel / std::max (peak, 1.0e-9f);
    for (auto& ch : out) for (float& v : ch) v *= gain;

    return out;
}

//==============================================================================

struct Render
{
    const char* filename;
    const char* description;
    EngineParameters params;
    bool reverse = false;      // true = closed -> open, the "drop" move
};

/** Cutoff automation.

    Forward is the build: open at the bottom of the range, sweeping up until
    almost nothing is left.  Reverse is the drop: the high-pass starts closed
    near the top of its range and opens downward, with a longer tail at the end
    so the full sound has time to land. */
float cutoffAt (double t, double seconds, bool reverse)
{
    const double holdStart = reverse ? 1.0 : 1.5;
    const double holdEnd   = reverse ? 2.5 : 1.0;
    const double sweepLength = seconds - holdStart - holdEnd;

    double u = clampValue ((t - holdStart) / sweepLength, 0.0, 1.0);
    if (reverse) u = 1.0 - u;

    return positionToCutoff (static_cast<float> (u) * cutoffPosition (14000.0f));
}

void render (const Render& r, const std::vector<std::vector<float>>& source,
             double seconds, const std::string& directory)
{
    auto audio = source;

    BrownSweepEngine engine;
    engine.setParameters (r.params);
    engine.prepare (kSampleRate, kBlockSize, 2);
    engine.setParameters (r.params);

    const int numFrames = static_cast<int> (audio[0].size());
    std::vector<float*> ptrs (2);
    auto params = r.params;

    for (int pos = 0; pos < numFrames; pos += kBlockSize)
    {
        const int n = std::min (kBlockSize, numFrames - pos);

        params.cutoffHz = cutoffAt (static_cast<double> (pos) / kSampleRate, seconds, r.reverse);
        engine.setParameters (params);

        for (int ch = 0; ch < 2; ++ch) ptrs[static_cast<size_t> (ch)] = audio[static_cast<size_t> (ch)].data() + pos;
        engine.process (ptrs.data(), 2, n);
    }

    const std::string path = directory + "/" + r.filename;
    writeWav (path, audio, kSampleRate);

    // A whole-file RMS averages the loud start with the quiet end and tells you
    // nothing.  The level *trajectory* is the thing being demonstrated, so print
    // one figure per second.
    std::printf ("%-34s |", r.filename);

    const int window = static_cast<int> (kSampleRate);
    for (int start = 0; start + window <= numFrames; start += window)
    {
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = start; i < start + window; ++i)
                sum += static_cast<double> (audio[static_cast<size_t> (ch)][static_cast<size_t> (i)])
                     * audio[static_cast<size_t> (ch)][static_cast<size_t> (i)];

        std::printf ("%6.1f", gainToDb (static_cast<float> (std::sqrt (sum / (window * 2)))));
    }

    std::printf ("   %s\n", r.description);
}

/** A traditional Goa arrangement: kick on every beat, bass on every offbeat -
    K B K B K B K B - with a sustained saw pad running through the plug-in.

    The pad's cutoff never goes below 350 Hz, so the filter is only ever working
    on the low mids upward and the kick and bass are left completely alone.  The
    motion is asymmetric because it comes from the plug-in's own saw-up LFO
    synced to two bars: a slow climb as the pad thins and recedes, then a snap
    back to full at the top of every second bar. */
void renderGoaTrack (const std::string& directory)
{
    constexpr double bpm = 145.0;
    constexpr int    bars = 8;

    const double samplesPerBeat = 60.0 / bpm * kSampleRate;
    const int numFrames = static_cast<int> (bars * 4 * samplesPerBeat + 0.6 * kSampleRate);
    const double seconds = numFrames / kSampleRate;

    // --- the pad, through the plug-in ------------------------------------
    auto pad = renderPad (seconds, 0.30f);

    EngineParameters p;
    p.cutoffHz   = 1107.0f;                 // geometric centre of 350 Hz .. 3.5 kHz
    p.character  = 0.60f;
    p.analog     = 0.45f;
    p.resonance  = 0.30f;
    p.mix        = 1.0f;
    p.outputDb   = 0.0f;
    p.slopeIndex = 2;                       // 24 dB/oct
    p.filterMode = static_cast<int> (FilterMode::ladder);
    p.autoGain   = true;
    p.oversamplingFactor = 2;
    p.bpm        = bpm;

    // Saw-up, two bars: the asymmetric part.  Depth 0.83 of +/-2 octaves gives
    // +/-1.66 octaves around 1107 Hz, i.e. exactly 350 Hz to 3.5 kHz.
    p.lfo[0].destination = static_cast<int> (LfoDestination::cutoff);
    p.lfo[0].shape       = static_cast<int> (LfoShape::sawUp);
    p.lfo[0].sync        = true;
    p.lfo[0].division    = 2;               // 2 bars
    p.lfo[0].depth       = 0.83f;

    // A slow four-bar breath on the resonance so the climbs are not identical.
    p.lfo[1].destination = static_cast<int> (LfoDestination::resonance);
    p.lfo[1].shape       = static_cast<int> (LfoShape::sine);
    p.lfo[1].sync        = true;
    p.lfo[1].division    = 1;               // 4 bars
    p.lfo[1].depth       = 0.45f;

    BrownSweepEngine engine;
    engine.setParameters (p);
    engine.prepare (kSampleRate, kBlockSize, 2);
    engine.setParameters (p);

    std::vector<float*> ptrs (2);
    for (int pos = 0; pos < numFrames; pos += kBlockSize)
    {
        const int n = std::min (kBlockSize, numFrames - pos);
        for (int ch = 0; ch < 2; ++ch) ptrs[static_cast<size_t> (ch)] = pad[static_cast<size_t> (ch)].data() + pos;
        engine.process (ptrs.data(), 2, n);
    }

    writeWav (directory + "/08-goa-pad-only.wav", pad, kSampleRate);

    // --- kick and bass, dry -----------------------------------------------
    auto mix = pad;

    for (int beat = 0; beat < bars * 4; ++beat)
    {
        addKick (mix, static_cast<int> (beat * samplesPerBeat), 0.58f,
                 static_cast<uint32_t> (beat * 2654435761u + 1u));
        addBass (mix, static_cast<int> ((beat + 0.5) * samplesPerBeat), 55.0, 0.34f);
    }

    float peak = 0.0f;
    for (const auto& ch : mix) for (float v : ch) peak = std::max (peak, std::fabs (v));

    writeWav (directory + "/09-goa-pad-over-kick-bass.wav", mix, kSampleRate);

    std::printf ("\n%.0f BPM, %d bars, kick on every beat and bass on every offbeat.\n"
                 "Pad cutoff swept 350 Hz - 3.5 kHz by the plug-in's own saw-up LFO synced to two\n"
                 "bars, so it climbs slowly and snaps back.  Kick and bass are dry.\n"
                 "  08-goa-pad-only.wav            the filtered pad on its own\n"
                 "  09-goa-pad-over-kick-bass.wav  the full arrangement (peak %.1f dBFS)\n",
                 bpm, bars, gainToDb (peak));
}

EngineParameters baseParams()
{
    EngineParameters p;
    p.character = 0.75f;
    p.analog    = 0.35f;
    p.resonance = 0.0f;
    p.mix       = 1.0f;
    p.outputDb  = 0.0f;
    p.slopeIndex = 2;                                        // 24 dB/oct
    p.filterMode = static_cast<int> (FilterMode::ladder);
    p.autoGain  = true;
    p.oversamplingFactor = 2;
    return p;
}

} // namespace

int main (int argc, char** argv)
{
    const std::string directory = (argc > 1) ? argv[1] : ".";
    constexpr double seconds = 11.0;

    const auto source = renderSupersaw (110.0, seconds, 0.25f);   // A2, about -12 dBFS

    std::vector<Render> renders;

    {
        Render r { "00-saw-dry.wav", "the source: 7 detuned saws at 110 Hz, no filter", baseParams() };
        r.params.mix = 0.0f;
        renders.push_back (r);
    }
    {
        Render r { "01-conventional-highpass.wav",
                   "an ordinary 24 dB/oct HP sweep - CHARACTER 0, AUTO off", baseParams() };
        r.params.character = 0.0f;
        r.params.analog    = 0.0f;
        r.params.autoGain  = false;
        r.params.filterMode = static_cast<int> (FilterMode::clean);
        renders.push_back (r);
    }
    {
        Render r { "02-brownsweep-clean.wav",
                   "BrownSweep, CLEAN mode, CHARACTER 75%", baseParams() };
        r.params.filterMode = static_cast<int> (FilterMode::clean);
        renders.push_back (r);
    }
    {
        Render r { "03-brownsweep-ladder.wav",
                   "BrownSweep, LADDER mode (SH-101 topology)", baseParams() };
        renders.push_back (r);
    }
    {
        Render r { "04-brownsweep-ladder-resonance.wav",
                   "LADDER + RESONANCE 65% + ANALOG 65% - the acid one", baseParams() };
        r.params.resonance = 0.65f;
        r.params.analog    = 0.65f;
        renders.push_back (r);
    }

    // --- the same thing backwards: closed -> open, i.e. the drop ------------
    {
        Render r { "05-reverse-conventional-highpass.wav",
                   "REVERSE: ordinary HP opening downward", baseParams(), true };
        r.params.character = 0.0f;
        r.params.analog    = 0.0f;
        r.params.autoGain  = false;
        r.params.filterMode = static_cast<int> (FilterMode::clean);
        renders.push_back (r);
    }
    {
        Render r { "06-reverse-brownsweep-ladder.wav",
                   "REVERSE: BrownSweep LADDER opening downward", baseParams(), true };
        renders.push_back (r);
    }
    {
        Render r { "07-reverse-brownsweep-ladder-resonance.wav",
                   "REVERSE: LADDER + RESONANCE 65% + ANALOG 65%", baseParams(), true };
        r.params.resonance = 0.65f;
        r.params.analog    = 0.65f;
        renders.push_back (r);
    }

    std::printf ("Source: 7 detuned saws at 110 Hz, %.0f s, cutoff swept between 20 Hz and 14 kHz.\n"
                 "Nothing is normalised - the level differences are the point.\n"
                 "Figures are RMS in dBFS, one per second of the render.\n\n", seconds);

    std::printf ("%-34s |", "second");
    for (int i = 1; i <= static_cast<int> (seconds); ++i) std::printf ("%6d", i);
    std::printf ("\n");

    for (const auto& r : renders)
        render (r, source, seconds, directory);

    renderGoaTrack (directory);

    return 0;
}
