/*
    BrownSweep - unit tests for the individual DSP building blocks.
*/

#include "TestFramework.h"
#include "TestUtilities.h"

#include "../Source/dsp/AnalogStage.h"
#include "../Source/dsp/BrownTilt.h"
#include "../Source/dsp/Filters.h"
#include "../Source/dsp/LadderStage.h"
#include "../Source/dsp/Lfo.h"
#include "../Source/dsp/LoudnessSchedule.h"
#include "../Source/dsp/Oversampler.h"
#include "../Source/dsp/ResponseModel.h"

using namespace bsweep;
using namespace bstest;

//==============================================================================
// Filters
//==============================================================================

BS_TEST (onePoleShelfWithUnityGainIsAnExactBypass)
{
    // This property is what lets the tilt cascade fade sections in and out
    // without clicking, so it has to hold bit-exactly, whatever the state.
    OnePoleTpt p;
    Xorshift32 rng (99u);

    for (int i = 0; i < 200; ++i)        // warm the state up with real signal
        p.processHighShelfShared (rng.nextBipolar(), 0.4f, 0.3f);

    for (int i = 0; i < 500; ++i)
    {
        const float x = rng.nextBipolar();
        CHECK (p.processHighShelfShared (x, 0.4f, 1.0f) == x);
    }
}

BS_TEST (svfHighpassIsStableAtExtremeSettings)
{
    for (float freq : { 0.05f, 20.0f, 1000.0f, 23990.0f })
    {
        for (float q : { 0.05f, 0.7071f, 40.0f })
        {
            SvfTpt f;
            SvfCoefficients c;
            c.set (tptGain (freq, 48000.0f), 1.0f / q);

            float p = 0.0f;
            Xorshift32 rng (7u);
            for (int i = 0; i < 200000; ++i)
                p = std::max (p, std::fabs (f.processHighpassShared (rng.nextBipolar(), c)));

            CHECK (f.isFinite());
            CHECK_LE (p, 200.0f);
        }
    }
}

BS_TEST (highPassPassesHighFrequenciesAndRemovesLowOnes)
{
    constexpr float fs = 48000.0f;

    for (int slope = 0; slope < kNumSlopes; ++slope)
    {
        const auto& cfg = kSlopeConfigs[slope];

        // Two octaves above the corner: essentially unity.
        const float above = std::abs (responsemodel::highPassResponse (4000.0f, 1000.0f, slope, 0.0f, fs));
        CHECK_NEAR (gainToDb (above), 0.0f, 1.0f);

        // Two octaves below: close to -2 * dbPerOctave.
        const float below = gainToDb (std::abs (responsemodel::highPassResponse (250.0f, 1000.0f, slope, 0.0f, fs)));
        CHECK_NEAR (below, -2.0f * cfg.dbPerOctave, 2.0f);
    }
}

//==============================================================================
// Brown tilt
//==============================================================================

BS_TEST (tiltCascadeAchievesTheRequestedSlope)
{
    constexpr float fc = 200.0f;

    for (float target : { 1.0f, 2.0f, 3.0f, 4.5f, 6.0f })
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int n = 0;
        std::vector<std::pair<double, double>> pts;

        for (int i = 0; i <= 120; ++i)
        {
            const double oct = 2.0 + 6.0 * i / 120.0;
            const double y = gainToDb (std::abs (responsemodel::tiltResponse (
                fc * static_cast<float> (std::exp2 (oct)), fc, target, 0.0f)));
            pts.push_back ({ oct, y });
            sx += oct; sy += y; sxx += oct * oct; sxy += oct * y; ++n;
        }

        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        const double icept = (sy - slope * sx) / n;

        CHECK_NEAR (slope, -target, 0.2);

        double lo = 1e9, hi = -1e9;
        for (auto& p : pts) { const double e = p.second - (slope * p.first + icept); lo = std::min (lo, e); hi = std::max (hi, e); }
        CHECK_MSG (hi - lo < 0.9, "tilt ripple " + testing::describe (hi - lo) + " dB p-p");
    }
}

BS_TEST (tiltIsFlatAtAndBelowTheCutoff)
{
    // The whole point of anchoring the tilt above the cutoff: the cutoff region
    // itself must survive, otherwise the filter is just a band-pass.
    for (float target : { 1.0f, 3.0f, 6.0f })
    {
        const float atCutoff = gainToDb (std::abs (responsemodel::tiltResponse (500.0f, 500.0f, target, 0.0f)));
        CHECK_MSG (atCutoff > -1.2f, "tilt at the cutoff is " + testing::describe (atCutoff) + " dB");

        const float belowCutoff = gainToDb (std::abs (responsemodel::tiltResponse (125.0f, 500.0f, target, 0.0f)));
        CHECK_MSG (belowCutoff > -0.35f, "tilt two octaves below the cutoff is "
                                          + testing::describe (belowCutoff) + " dB");
    }
}

BS_TEST (tiltNeverBoosts)
{
    for (float target : { 0.5f, 3.0f, 6.0f })
        for (int i = 0; i <= 400; ++i)
        {
            const float f = 10.0f * std::pow (2200.0f, static_cast<float> (i) / 400.0f);
            CHECK_LE (std::abs (responsemodel::tiltResponse (f, 300.0f, target, 96000.0f)), 1.0001f);
        }
}

//==============================================================================
// Analogue stage
//==============================================================================

BS_TEST (analogStageIsExactlyTransparentAtZero)
{
    AnalogCoefficients c;
    c.update (0.0f, 96000.0f);
    CHECK (c.linear);
    CHECK (analogTransfer (0.0f, c) == 0.0f);

    Xorshift32 rng (5u);
    for (int i = 0; i < 1000; ++i)
    {
        const float x = 2.0f * rng.nextBipolar();
        CHECK (analogTransfer (x, c) == x);
    }
    CHECK (c.hfGain == 1.0f);
}

BS_TEST (analogTransferIsMonotonicBoundedAndUnityAtSmallSignal)
{
    for (float amount : { 0.1f, 0.5f, 1.0f })
    {
        AnalogCoefficients c;
        c.update (amount, 96000.0f);

        // Exactly zero, not "nearly": otherwise the stage emits a DC step.
        CHECK (analogTransfer (0.0f, c) == 0.0f);

        // unity small-signal gain
        const float slope = (analogTransfer (1.0e-4f, c) - analogTransfer (-1.0e-4f, c)) / 2.0e-4f;
        CHECK_NEAR (slope, 1.0f, 0.01f);

        // Asymptote of the shaper: norm * (1 + |tanh(bias)|) / drive.
        const float bound = c.norm * (1.0f + std::fabs (c.biasOffset)) / c.drive + 1.0e-4f;

        float previous = analogTransfer (-8.0f, c);
        for (int i = -799; i <= 800; ++i)
        {
            const float x = static_cast<float> (i) * 0.01f;
            const float y = analogTransfer (x, c);
            CHECK_GE (y, previous - 1.0e-6f);                       // monotonic
            CHECK_LE (std::fabs (y), bound);                        // bounded
            // The bias makes the negative half slightly steeper than unity for
            // small inputs - that asymmetry *is* the second harmonic - but the
            // stage must never behave like a gain stage.
            CHECK_LE (std::fabs (y), std::fabs (x) * 1.05f + 1.0e-6f);
            CHECK (std::isfinite (y));
            previous = y;
        }
    }
}

BS_TEST (analogStageAddsHarmonicsButStaysSubtle)
{
    // At ANALOG = 100% and a realistic -6 dBFS input, total harmonic distortion
    // should be present but modest: "warm", not "fuzz".
    constexpr double fs = 192000.0;
    constexpr int n = 16384;

    AnalogCoefficients c;
    c.update (1.0f, static_cast<float> (fs));

    std::vector<std::complex<double>> spec (n);
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n);
        const double x = 0.5 * std::sin (2.0 * kPi * 1000.0 * i / fs);
        spec[static_cast<size_t> (i)] = { static_cast<double> (analogTransfer (static_cast<float> (x), c)) * w, 0.0 };
    }
    fft (spec);

    const auto binEnergy = [&] (double f)
    {
        const int k = static_cast<int> (std::round (f * n / fs));
        double e = 0.0;
        for (int j = k - 3; j <= k + 3; ++j) e += std::norm (spec[static_cast<size_t> (j)]);
        return e;
    };

    const double fundamental = binEnergy (1000.0);
    double harmonics = 0.0;
    for (int h = 2; h <= 8; ++h) harmonics += binEnergy (1000.0 * h);

    const double thd = std::sqrt (harmonics / fundamental);
    CHECK_MSG (thd > 0.005, "THD " + testing::describe (thd * 100.0) + "% - stage is doing nothing");
    CHECK_MSG (thd < 0.10,  "THD " + testing::describe (thd * 100.0) + "% - stage is too dirty");

    // Asymmetry must actually produce a second harmonic, not only odd ones.
    CHECK_GE (binEnergy (2000.0) / fundamental, 1.0e-7);
}

//==============================================================================
// Oversampler
//==============================================================================

BS_TEST (oversamplerRoundTripIsUnityWithIntegerLatencyAt2x)
{
    constexpr int n = 4096;
    Oversampler os;
    os.prepare (1, kControlBlockSamples, 2);

    CHECK_NEAR (os.getLatencySamples(), 23.0f, 1.0e-6f);

    auto in  = sine (n, 1000.0, 48000.0, 0.5f);
    std::vector<float> out (static_cast<size_t> (n), 0.0f);

    for (int pos = 0; pos < n; pos += kControlBlockSamples)
    {
        const float* inPtr[1]  = { in.data() + pos };
        float*       outPtr[1] = { out.data() + pos };
        os.upsample (inPtr, 1, kControlBlockSamples);
        os.downsample (outPtr, 1, kControlBlockSamples);
    }

    const int latency = 23;
    double worst = 0.0;
    for (int i = 512; i < n - latency; ++i)
        worst = std::max (worst, std::fabs (static_cast<double> (out[static_cast<size_t> (i + latency)])
                                            - in[static_cast<size_t> (i)]));

    CHECK_MSG (worst < 2.0e-3, "2x round-trip error " + testing::describe (worst));
    CHECK (allFinite (out));
}

BS_TEST (oversamplerRoundTripPreservesLevelAt4x)
{
    constexpr int n = 8192;
    Oversampler os;
    os.prepare (1, kControlBlockSamples, 4);

    CHECK_NEAR (os.getLatencySamples(), 30.5f, 1.0e-6f);

    auto in  = sine (n, 1000.0, 48000.0, 0.5f);
    std::vector<float> out (static_cast<size_t> (n), 0.0f);

    for (int pos = 0; pos < n; pos += kControlBlockSamples)
    {
        const float* inPtr[1]  = { in.data() + pos };
        float*       outPtr[1] = { out.data() + pos };
        os.upsample (inPtr, 1, kControlBlockSamples);
        os.downsample (outPtr, 1, kControlBlockSamples);
    }

    CHECK (allFinite (out));
    CHECK_NEAR (rmsDb (out, 1024, n), rmsDb (in, 1024, n), 0.05);
}

BS_TEST (oversamplerRejectsImages)
{
    // Upsample a 12 kHz tone from 48 kHz to 96 kHz.  Zero stuffing would put an
    // image at 36 kHz; the half-band filter has to bury it.
    constexpr int n = 8192;
    Oversampler os;
    os.prepare (1, n, 2);

    auto in = sine (n, 12000.0, 48000.0, 0.5f);
    const float* inPtr[1] = { in.data() };
    os.upsample (inPtr, 1, n);

    std::vector<float> upsampled (os.channelData (0), os.channelData (0) + 2 * n);

    const double wanted = bandEnergy (upsampled, 96000.0, 11000.0, 13000.0);
    const double image  = bandEnergy (upsampled, 96000.0, 35000.0, 37000.0);
    const double rejectionDb = 10.0 * std::log10 (image / std::max (wanted, 1.0e-30));

    CHECK_MSG (rejectionDb < -70.0, "image rejection only " + testing::describe (rejectionDb) + " dB");
}

BS_TEST (oversamplerPassesDcAtUnity)
{
    for (int factor : { 1, 2, 4 })
    {
        constexpr int n = 4096;
        Oversampler os;
        os.prepare (1, kControlBlockSamples, factor);

        auto in = dc (n, 0.5f);
        std::vector<float> out (static_cast<size_t> (n), 0.0f);

        for (int pos = 0; pos < n; pos += kControlBlockSamples)
        {
            const float* inPtr[1]  = { in.data() + pos };
            float*       outPtr[1] = { out.data() + pos };
            os.upsample (inPtr, 1, kControlBlockSamples);
            os.downsample (outPtr, 1, kControlBlockSamples);
        }

        CHECK_NEAR (out[static_cast<size_t> (n - 1)], 0.5f, 1.0e-4f);
    }
}

//==============================================================================
// LFO
//==============================================================================

BS_TEST (lfoShapesAreBoundedAndContinuousWhereExpected)
{
    Lfo lfo;
    lfo.prepare (48000.0, 1u);

    for (int shape = 0; shape < kNumLfoShapes; ++shape)
    {
        float previous = lfo.evaluate (static_cast<LfoShape> (shape), 0.0f);
        for (int i = 0; i <= 2000; ++i)
        {
            const float v = lfo.evaluate (static_cast<LfoShape> (shape), static_cast<float> (i) / 2000.0f);
            CHECK (std::isfinite (v));
            CHECK_LE (std::fabs (v), 1.0001f);

            if (shape != static_cast<int> (LfoShape::sawUp)
                && shape != static_cast<int> (LfoShape::sawDown)
                && shape != static_cast<int> (LfoShape::sampleHold))
                CHECK_LE (std::fabs (v - previous), 0.05f);

            previous = v;
        }
    }
}

BS_TEST (lfoRunsAtTheRequestedRate)
{
    Lfo lfo;
    lfo.prepare (48000.0, 3u);

    LfoParameters p;
    p.destination = static_cast<int> (LfoDestination::cutoff);
    p.shape = static_cast<int> (LfoShape::sine);
    p.depth = 1.0f;
    p.rateHz = 2.0f;

    // Count zero crossings over exactly one second: a 2 Hz sine has four.
    int crossings = 0;
    float previous = lfo.advance (p, 120.0, 1);
    for (int i = 0; i < 48000 / 16; ++i)
    {
        const float v = lfo.advance (p, 120.0, 16);
        if ((v >= 0.0f) != (previous >= 0.0f)) ++crossings;
        previous = v;
    }
    CHECK_NEAR (crossings, 4, 1);
}

BS_TEST (lfoSyncFollowsTempo)
{
    Lfo lfo;
    lfo.prepare (48000.0, 9u);

    LfoParameters p;
    p.destination = static_cast<int> (LfoDestination::cutoff);
    p.depth = 1.0f;
    p.sync = true;
    p.division = 7;                    // 1/4 note
    const double bpm = 120.0;          // -> 2 Hz

    int crossings = 0;
    float previous = lfo.advance (p, bpm, 1);
    for (int i = 0; i < 48000 / 16; ++i)
    {
        const float v = lfo.advance (p, bpm, 16);
        if ((v >= 0.0f) != (previous >= 0.0f)) ++crossings;
        previous = v;
    }
    CHECK_NEAR (crossings, 4, 1);
}

BS_TEST (lfoAmplitudeModulationOnlyEverAttenuates)
{
    for (int i = 0; i <= 100; ++i)
    {
        const float v = -1.0f + 0.02f * static_cast<float> (i);
        CHECK_LE (Lfo::modulationAmount (LfoDestination::amplitude, v, 1.0f), 1.0e-6f);
    }
    CHECK_NEAR (Lfo::modulationAmount (LfoDestination::amplitude, 1.0f, 1.0f), 0.0f, 1.0e-6f);
    CHECK_NEAR (Lfo::modulationAmount (LfoDestination::amplitude, -1.0f, 1.0f), -kModAmplitudeDb, 1.0e-4f);
}

BS_TEST (lfoOffProducesNoModulation)
{
    Lfo lfo;
    lfo.prepare (48000.0, 11u);
    LfoParameters p;                    // destination defaults to off
    p.depth = 1.0f;
    for (int i = 0; i < 100; ++i)
        CHECK (lfo.advance (p, 120.0, 16) == 0.0f);
}

//==============================================================================
// Loudness schedule
//==============================================================================

BS_TEST (autoGainNeverBoosts)
{
    const auto& schedule = LoudnessSchedule::instance();

    for (int mode = 0; mode < kNumFilterModes; ++mode)
    for (int slope = 0; slope < kNumSlopes; ++slope)
        for (int iu = 0; iu <= 100; ++iu)
            for (int ic = 0; ic <= 10; ++ic)
                for (int ir = 0; ir <= 4; ++ir)
                {
                    const float g = schedule.gainDb (iu / 100.0f, ic / 10.0f, ir / 4.0f, slope, mode);
                    CHECK_LE (g, 1.0e-4f);
                    CHECK (std::isfinite (g));
                    CHECK_GE (g, -80.0f);
                }
}

BS_TEST (autoGainIsUnityWhenCharacterIsZero)
{
    const auto& schedule = LoudnessSchedule::instance();
    for (int mode = 0; mode < kNumFilterModes; ++mode)
        for (int slope = 0; slope < kNumSlopes; ++slope)
            for (int iu = 0; iu <= 100; ++iu)
                CHECK_NEAR (schedule.gainDb (iu / 100.0f, 0.0f, 0.0f, slope, mode), 0.0f, 1.0e-4f);
}

BS_TEST (loudnessContourIsMonotonicAndSmooth)
{
    const auto& schedule = LoudnessSchedule::instance();

    for (int mode = 0; mode < kNumFilterModes; ++mode)
    for (int slope = 0; slope < kNumSlopes; ++slope)
        for (float character : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            constexpr int steps = 400;
            std::vector<double> loudness (steps + 1);

            for (int i = 0; i <= steps; ++i)
            {
                const float u = static_cast<float> (i) / steps;
                loudness[static_cast<size_t> (i)] =
                    schedule.measuredDb (u, character, 0.0f, slope, mode)
                        + schedule.gainDb (u, character, 0.0f, slope, mode);
            }

            for (int i = 1; i <= steps; ++i)
                CHECK_MSG (loudness[static_cast<size_t> (i)] <= loudness[static_cast<size_t> (i - 1)] + 0.02,
                           "loudness contour rises at u = " + testing::describe (i / double (steps)));

            // Bounded curvature: no sudden change of fade rate anywhere.
            for (int i = 1; i < steps; ++i)
            {
                const double second = loudness[static_cast<size_t> (i + 1)]
                                    - 2.0 * loudness[static_cast<size_t> (i)]
                                    + loudness[static_cast<size_t> (i - 1)];
                CHECK_MSG (std::fabs (second) < 0.05,
                           "loudness contour kinks at u = " + testing::describe (i / double (steps))
                           + " (2nd difference " + testing::describe (second) + ")");
            }
        }
}

BS_TEST (loudnessContourReachesNearSilenceAtMaximumCutoff)
{
    const auto& schedule = LoudnessSchedule::instance();
    for (int mode = 0; mode < kNumFilterModes; ++mode)
    {
        const float atTop = schedule.measuredDb (1.0f, 1.0f, 0.0f, 2, mode)
                          + schedule.gainDb (1.0f, 1.0f, 0.0f, 2, mode);
        CHECK_LE (atTop, -38.0f);
    }
}

//==============================================================================
// Ladder topology (the SH-101 mode)
//==============================================================================

BS_TEST (bothFilterModesPutTheirCornerOnTheCutoffControl)
{
    // CUTOFF drives the tilt anchor and the loudness contour, so the two
    // topologies have to agree about what "cutoff" means.  If they did not,
    // switching MODE would be a large tone and level jump and automation curves
    // would stop being portable between them.
    for (int slope = 0; slope < kNumSlopes; ++slope)
        for (float fc : { 60.0f, 500.0f, 4000.0f })
        {
            const float clean  = gainToDb (std::abs (responsemodel::filterResponse (fc, fc, slope, 0, 0.0f, 0.0f)));
            const float ladder = gainToDb (std::abs (responsemodel::filterResponse (fc, fc, slope, 1, 0.0f, 0.0f)));

            CHECK_NEAR (clean,  -3.01f, 0.1f);
            CHECK_NEAR (ladder, -3.01f, 0.1f);
        }
}

BS_TEST (ladderReachesItsDesignedResonantPeakAndNoMore)
{
    for (int slope = 0; slope < kNumSlopes; ++slope)
    {
        const float fc = 1000.0f;

        const auto peakDb = [&] (float resonance)
        {
            float best = -1000.0f;
            for (int i = 0; i <= 4000; ++i)
            {
                const float f = fc * std::exp2 (-6.0f + 8.0f * static_cast<float> (i) / 4000.0f);
                best = std::max (best, gainToDb (std::abs (
                    responsemodel::ladderResponse (f, fc, slope, resonance, 0.0f))));
            }
            return best;
        };

        // No resonance: a pure attenuator, like every other stage in the plugin.
        CHECK_LE (peakDb (0.0f), 0.02f);

        // Full resonance: exactly the designed ceiling.
        CHECK_NEAR (peakDb (1.0f), kLadderMaxPeakDb, 0.3f);

        // ...and monotonic in between, so the control feels even.
        float previous = -1000.0f;
        for (int i = 0; i <= 20; ++i)
        {
            const float p = peakDb (static_cast<float> (i) / 20.0f);
            CHECK_GE (p, previous - 0.05f);
            previous = p;
        }
    }
}

BS_TEST (ladderResonanceSitsBelowTheCornerWhereItsPolesAre)
{
    // A signature of the topology rather than an accident: all four poles sit
    // at cutoff / minus3dbScale, so the peak lands below the -3 dB point.  That
    // offset is why ladder resonance sounds hollow instead of surgical.
    const float fc = 1000.0f;

    for (int slope = 0; slope < kNumSlopes; ++slope)
    {
        float best = -1000.0f, peakFreq = 0.0f;
        for (int i = 0; i <= 4000; ++i)
        {
            const float f = fc * std::exp2 (-6.0f + 8.0f * static_cast<float> (i) / 4000.0f);
            const float d = gainToDb (std::abs (responsemodel::ladderResponse (f, fc, slope, 1.0f, 0.0f)));
            if (d > best) { best = d; peakFreq = f; }
        }

        const float expected = fc / kLadderConfigs[slope].minus3dbScale;
        CHECK_MSG (std::abs (std::log2 (peakFreq / expected)) < 0.15f,
                   "ladder peak at " + testing::describe (peakFreq)
                   + " Hz, expected near " + testing::describe (expected) + " Hz");
    }
}

BS_TEST (ladderNeverBoostsWithoutResonance)
{
    for (int slope = 0; slope < kNumSlopes; ++slope)
        for (int i = 0; i <= 600; ++i)
        {
            const float f = 10.0f * std::pow (2200.0f, static_cast<float> (i) / 600.0f);
            CHECK_LE (std::abs (responsemodel::ladderResponse (f, 400.0f, slope, 0.0f, 96000.0f)), 1.0002f);
        }
}

BS_TEST (ladderIsExactlyLinearWhenAnalogIsZero)
{
    LadderCoefficients c;
    c.update (800.0f, 2, 1.0f, 0.0f, 96000.0f);
    CHECK (c.feedbackShaper.linear);

    // With a linear shaper the feedback residual is identically zero, which is
    // what keeps the zero-delay solve - and therefore the analytic model -
    // exact.
    Xorshift32 rng (17u);
    LadderState a, b;
    a.reset();
    b.reset();

    for (int i = 0; i < 4096; ++i)
    {
        const float x = rng.nextBipolar();
        CHECK (a.process (c, x) == b.process (c, x));
    }
    CHECK (a.isFinite());
}

BS_TEST (ladderFeedbackClipperGeneratesEvenHarmonics)
{
    // The asymmetric clipper in the feedback loop is the whole point of the
    // LADDER mode: a symmetric one would give odd harmonics only, and the
    // resonance would compress rather than lean.
    constexpr double fs = 96000.0;
    constexpr int n = 32768;

    const int slope = 2;                         // 24 dB/oct
    const float cutoff = 1000.0f;
    const double poleHz = cutoff / kLadderConfigs[slope].minus3dbScale;

    const auto harmonics = [&] (float analog)
    {
        LadderCoefficients c;
        c.update (cutoff, slope, 1.0f, analog, static_cast<float> (fs));

        LadderState state;
        state.reset();

        std::vector<std::complex<double>> spec (n);
        for (int i = 0; i < n; ++i)
        {
            const double x = 0.5 * std::sin (2.0 * kPi * poleHz * i / fs);
            const double y = static_cast<double> (state.process (c, static_cast<float> (x)));
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n);
            spec[static_cast<size_t> (i)] = { y * w, 0.0 };
        }
        fft (spec);

        const auto binEnergy = [&] (double f)
        {
            const int k = static_cast<int> (std::round (f * n / fs));
            double e = 0.0;
            for (int j = k - 3; j <= k + 3; ++j) e += std::norm (spec[static_cast<size_t> (j)]);
            return e;
        };

        return std::pair<double, double> { binEnergy (2.0 * poleHz) / binEnergy (poleHz),
                                           binEnergy (3.0 * poleHz) / binEnergy (poleHz) };
    };

    const auto clean  = harmonics (0.0f);
    const auto driven = harmonics (1.0f);

    CHECK_MSG (clean.first < 1.0e-10, "linear ladder produced a second harmonic: "
                                       + testing::describe (clean.first));
    CHECK_MSG (driven.first > 1.0e-5, "driven ladder produced no second harmonic: "
                                       + testing::describe (driven.first));
    CHECK_MSG (driven.second > 1.0e-5, "driven ladder produced no third harmonic: "
                                        + testing::describe (driven.second));
}

BS_TEST (ladderStaysStableWhenDrivenHard)
{
    for (int slope = 0; slope < kNumSlopes; ++slope)
    {
        LadderCoefficients c;
        c.update (300.0f, slope, 1.0f, 1.0f, 96000.0f);

        LadderState state;
        state.reset();

        Xorshift32 rng (999u);
        float peak = 0.0f;

        for (int i = 0; i < 400000; ++i)
        {
            // Deliberately far too hot: +12 dBFS of noise plus a DC-ish offset.
            const float x = 4.0f * rng.nextBipolar() + 0.5f;
            peak = std::max (peak, std::fabs (state.process (c, x)));
        }

        CHECK (state.isFinite());
        CHECK_MSG (peak < 200.0f, "ladder peaked at " + testing::describe (peak)
                                   + " at slope " + testing::describe (slope));
    }
}
