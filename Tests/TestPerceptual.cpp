/*
    BrownSweep - tests for the perceptual claims the plugin actually makes.

    These are the tests that matter: anything can be "stable and free of NaNs".
    The design is only correct if an upward cutoff sweep reads as a loss of
    energy rather than as an increasingly bright removal of low frequencies, and
    that is a measurable property.
*/

#include "TestFramework.h"
#include "TestUtilities.h"

#include "../Source/dsp/LoudnessSchedule.h"
#include "../Source/dsp/ResponseModel.h"

using namespace bsweep;
using namespace bstest;

namespace
{

constexpr int   kBins  = 400;
constexpr float kLowHz = 15.0f;
constexpr float kHighHz = 21000.0f;

struct Reference
{
    std::vector<float> freq, kWeight;

    Reference()
    {
        freq.resize (kBins);
        kWeight.resize (kBins);
        for (int i = 0; i < kBins; ++i)
        {
            const float t = static_cast<float> (i) / (kBins - 1);
            freq[static_cast<size_t> (i)]    = kLowHz * std::pow (kHighHz / kLowHz, t);
            kWeight[static_cast<size_t> (i)] = kWeightingMagnitudeSquared (freq[static_cast<size_t> (i)]);
        }
    }
};

const Reference& reference() { static Reference r; return r; }

ResponseState stateFor (float u, float character, float resonance, int slope)
{
    ResponseState s;
    s.cutoffHz        = positionToCutoff (u);
    s.slopeIndex      = slope;
    s.resonance01     = resonance;
    s.tiltDbPerOctave = tiltSlopeDbPerOctave (u, character);
    s.wetGainDb       = LoudnessSchedule::instance().gainDb (u, character, resonance, slope);
    s.analog01        = 0.0f;
    s.mix             = 1.0f;
    s.outputDb        = 0.0f;
    s.sampleRate      = 0.0f;      // analogue prototype - sample-rate independent
    return s;
}

struct SweepPoint
{
    double loudnessDb;      // K-weighted, pink reference, relative to unfiltered
    double highBandDb;      // absolute level of everything above 4 kHz
    double centroidOctaves; // log-frequency centroid, in octaves above 20 Hz
    double peakGainDb;      // largest magnitude anywhere in the response
};

SweepPoint measure (const std::function<Complex (float)>& response)
{
    const auto& r = reference();

    double energy = 0.0, kEnergy = 0.0, highK = 0.0, logSum = 0.0, refK = 0.0, peakDb = -1000.0;

    for (int i = 0; i < kBins; ++i)
    {
        const float f = r.freq[static_cast<size_t> (i)];
        const double w = r.kWeight[static_cast<size_t> (i)];
        const double m2 = std::norm (response (f));          // pink -> unity weight per log bin

        energy  += m2;
        kEnergy += m2 * w;
        logSum  += m2 * std::log2 (static_cast<double> (f) / 20.0);
        if (f > 4000.0f) highK += m2 * w;
        refK += w;
        peakDb = std::max (peakDb, 10.0 * std::log10 (std::max (m2, 1.0e-30)));
    }

    SweepPoint p {};
    p.loudnessDb      = 10.0 * std::log10 (std::max (kEnergy / refK, 1.0e-14));
    p.highBandDb      = 10.0 * std::log10 (std::max (highK   / refK, 1.0e-14));
    p.centroidOctaves = energy > 0.0 ? logSum / energy : 0.0;
    p.peakGainDb      = peakDb;
    return p;
}

} // namespace

//==============================================================================

BS_TEST (risingCutoffNeverIncreasesHighFrequencyEnergy)
{
    // The core claim.  As the cutoff goes up, the absolute amount of energy
    // above 4 kHz must go DOWN, at every character setting above zero.  A
    // conventional high-pass fails this badly (see the next test).
    for (int slope = 0; slope < kNumSlopes; ++slope)
        for (float character : { 0.35f, 0.65f, 1.0f })
        {
            double previous = 1000.0;
            for (int i = 0; i <= 120; ++i)
            {
                const float u = static_cast<float> (i) / 120.0f;
                const auto s = stateFor (u, character, 0.0f, slope);
                const auto m = measure ([&] (float f) { return responsemodel::wetResponse (s, f); });

                CHECK_MSG (m.highBandDb <= previous + 0.05,
                           "high-band energy rose at u = " + testing::describe (u)
                           + " (character " + testing::describe (character) + ")");
                previous = m.highBandDb;
            }
        }
}

BS_TEST (conventionalHighPassLeavesTheTopEndUntouched)
{
    // Documents the problem this plugin exists to solve.  A 24 dB/oct high-pass
    // swept from 20 Hz to 2 kHz barely touches the energy above 4 kHz, and its
    // loudness hardly moves either - so all the listener hears is the low end
    // being deleted.
    const auto at = [] (float u)
    {
        const float fc = positionToCutoff (u);
        return measure ([&] (float f) { return responsemodel::highPassResponse (f, fc, 2, 0.0f, 0.0f); });
    };

    const auto bottom = at (0.0f);
    const auto middle = at (cutoffPosition (2000.0f));

    CHECK_MSG (std::fabs (middle.highBandDb - bottom.highBandDb) < 0.2,
               "conventional HP high band moved by "
               + testing::describe (middle.highBandDb - bottom.highBandDb) + " dB");
    CHECK_MSG (bottom.loudnessDb - middle.loudnessDb < 4.0,
               "conventional HP loudness fell by "
               + testing::describe (bottom.loudnessDb - middle.loudnessDb) + " dB");

    // ...whereas BrownSweep at the same cutoff has done something substantial.
    const auto brown = measure ([&] (float f)
        { return responsemodel::wetResponse (stateFor (cutoffPosition (2000.0f), 1.0f, 0.0f, 2), f); });

    CHECK_MSG (bottom.highBandDb - brown.highBandDb > 15.0,
               "BrownSweep only pulled the high band down by "
               + testing::describe (bottom.highBandDb - brown.highBandDb) + " dB");
}

BS_TEST (perceivedLoudnessFallsSteadilyAndReachesNearSilence)
{
    for (int slope = 0; slope < kNumSlopes; ++slope)
    {
        std::vector<double> loudness;
        for (int i = 0; i <= 200; ++i)
        {
            const float u = static_cast<float> (i) / 200.0f;
            loudness.push_back (measure ([&] (float f)
                { return responsemodel::wetResponse (stateFor (u, 1.0f, 0.0f, slope), f); }).loudnessDb);
        }

        for (size_t i = 1; i < loudness.size(); ++i)
            CHECK_MSG (loudness[i] <= loudness[i - 1] + 0.05,
                       "loudness rose at step " + testing::describe (double (i)));

        CHECK_MSG (loudness.front() > -0.5, "not transparent at the bottom of the range: "
                                             + testing::describe (loudness.front()) + " dB");
        CHECK_MSG (loudness.back() < -38.0, "not quiet enough at the top of the range: "
                                             + testing::describe (loudness.back()) + " dB");

        // No single 0.5% step of the control may drop more than 2 dB: that is
        // what "the transition should feel gradual" means numerically.
        for (size_t i = 1; i < loudness.size(); ++i)
            CHECK_MSG (loudness[i - 1] - loudness[i] < 2.0,
                       "loudness cliff of " + testing::describe (loudness[i - 1] - loudness[i])
                       + " dB at step " + testing::describe (double (i)));
    }
}

BS_TEST (spectralCentroidStaysBelowTheConventionalHighPass)
{
    // The centroid cannot be prevented from rising - the low end really is
    // gone - but it must rise substantially less than a plain high-pass, over
    // the part of the range where sweeps actually live.
    double worstAdvantage = 1000.0;
    double bestAdvantage = -1000.0;
    double worstInSweepRange = 1000.0;

    for (int i = 5; i <= 95; ++i)
    {
        const float u  = static_cast<float> (i) / 100.0f;
        const float fc = positionToCutoff (u);

        const auto brown = measure ([&] (float f)
            { return responsemodel::wetResponse (stateFor (u, 1.0f, 0.0f, 2), f); });
        const auto plain = measure ([&] (float f)
        {
            // Include the always-on 12 Hz DC blocker in the reference so the
            // two curves differ only by the tilt.
            return responsemodel::highPassResponse (f, fc, 2, 0.0f, 0.0f)
                 * responsemodel::dcBlockerResponse (f, 0.0f);
        });

        const double advantage = plain.centroidOctaves - brown.centroidOctaves;
        worstAdvantage = std::min (worstAdvantage, advantage);
        bestAdvantage  = std::max (bestAdvantage, advantage);
        if (i >= 35 && i <= 65) worstInSweepRange = std::min (worstInSweepRange, advantage);

        CHECK_MSG (advantage >= -1.0e-6,
                   "centroid is higher than a plain HP at u = " + testing::describe (u));
    }

    // The advantage is necessarily small at the very bottom (nothing has been
    // removed yet) and at the very top (only a narrow band survives, so both
    // filters have to put their centroid in the same place).  In the middle of
    // the range - where sweeps actually live - it has to be substantial.
    CHECK_MSG (worstInSweepRange > 0.5,
               "mid-range centroid advantage only " + testing::describe (worstInSweepRange) + " octaves");
    CHECK_MSG (bestAdvantage > 0.8,
               "peak centroid advantage only " + testing::describe (bestAdvantage) + " octaves");
}

BS_TEST (theResponseNeverGainsMoreThanTheResonanceAllowance)
{
    // No hidden boost anywhere: with RESONANCE at zero the wet path is a pure
    // attenuator, and at full resonance the peak is bounded by design.
    for (int slope = 0; slope < kNumSlopes; ++slope)
        for (int i = 0; i <= 60; ++i)
        {
            const float u = static_cast<float> (i) / 60.0f;

            const auto flat = measure ([&] (float f)
                { return responsemodel::wetResponse (stateFor (u, 1.0f, 0.0f, slope), f); });
            CHECK_MSG (flat.peakGainDb < 0.05, "peak gain " + testing::describe (flat.peakGainDb)
                                                + " dB with no resonance at u = " + testing::describe (u));

            const auto resonant = measure ([&] (float f)
                { return responsemodel::wetResponse (stateFor (u, 1.0f, 1.0f, slope), f); });
            CHECK_MSG (resonant.peakGainDb < 13.0, "resonant peak " + testing::describe (resonant.peakGainDb)
                                                    + " dB at u = " + testing::describe (u)
                                                    + " slope " + testing::describe (slope));
        }
}

BS_TEST (characterZeroBehavesLikeAPlainHighPass)
{
    for (int i = 0; i <= 40; ++i)
    {
        const float u  = static_cast<float> (i) / 40.0f;
        const float fc = positionToCutoff (u);
        const auto s = stateFor (u, 0.0f, 0.0f, 2);

        for (float f : { 30.0f, 100.0f, 500.0f, 2000.0f, 9000.0f })
        {
            const float a = gainToDb (std::abs (responsemodel::wetResponse (s, f)));
            const float b = gainToDb (std::abs (responsemodel::highPassResponse (f, fc, 2, 0.0f, 0.0f)
                                                * responsemodel::dcBlockerResponse (f, 0.0f)));
            if (b < -80.0f) continue;      // nothing meaningful left to compare
            CHECK_NEAR (a, b, 0.05f);
        }
    }
}

//==============================================================================
// Same claim, measured on the running engine rather than the model
//==============================================================================

BS_TEST (runningEngineAlsoPullsDownTheTopEndAsTheCutoffRises)
{
    constexpr double fs = 48000.0;
    constexpr int n = 65536;

    const auto source = pinkNoise (n, 20240521u, 0.3f);
    const double sourceHigh = bandEnergy (source, fs, 4000.0, 20000.0);

    double previous = 1000.0;

    for (float cutoff : { 20.0f, 60.0f, 150.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 14000.0f, 20000.0f })
    {
        EngineParameters p;
        p.cutoffHz  = cutoff;
        p.character = 1.0f;
        p.analog    = 0.0f;
        p.resonance = 0.0f;
        p.mix       = 1.0f;
        p.slopeIndex = 2;
        p.autoGain  = true;
        p.oversamplingFactor = 1;

        BrownSweepEngine engine;
        engine.setParameters (p);
        engine.prepare (fs, 512, 1);
        engine.setParameters (p);

        runEngine (engine, duplicate (silence (8192), 1));      // let the smoothers settle
        auto out = runEngine (engine, duplicate (source, 1));

        CHECK (allFinite (out[0]));

        const double high = 10.0 * std::log10 (std::max (bandEnergy (out[0], fs, 4000.0, 20000.0), 1.0e-30)
                                               / std::max (sourceHigh, 1.0e-30));

        CHECK_MSG (high <= previous + 0.25,
                   "measured high band rose from " + testing::describe (previous)
                   + " to " + testing::describe (high) + " dB at cutoff " + testing::describe (cutoff));
        previous = high;
    }

    CHECK_MSG (previous < -30.0, "top of the sweep still has " + testing::describe (previous)
                                  + " dB of high band");
}
