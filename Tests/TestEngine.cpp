/*
    BrownSweep - end-to-end tests of the complete engine.
*/

#include "TestFramework.h"
#include "TestUtilities.h"

#include <memory>

using namespace bsweep;
using namespace bstest;

namespace
{

constexpr double kFs = 48000.0;

EngineParameters defaultParams()
{
    EngineParameters p;
    p.cutoffHz  = 200.0f;
    p.character = 0.65f;
    p.analog    = 0.0f;
    p.resonance = 0.0f;
    p.mix       = 1.0f;
    p.outputDb  = 0.0f;
    p.slopeIndex = 2;
    p.filterMode = static_cast<int> (FilterMode::clean);
    p.autoGain  = true;
    p.oversamplingFactor = 1;
    p.bpm = 138.0;
    return p;
}

std::unique_ptr<BrownSweepEngine> makeEngine (const EngineParameters& p, int channels = 2, double fs = kFs)
{
    auto engine = std::make_unique<BrownSweepEngine>();
    engine->setParameters (p);
    engine->prepare (fs, 512, channels);
    engine->setParameters (p);
    return engine;
}

} // namespace

//==============================================================================
// Basic well-formedness
//==============================================================================

BS_TEST (silenceInGivesSilenceOut)
{
    for (int factor : { 1, 2, 4 })
    {
        auto p = defaultParams();
        p.oversamplingFactor = factor;
        p.analog = 1.0f;
        p.resonance = 1.0f;
        BrownSweepEngine engine;
        engine.setParameters (p);
        engine.prepare (kFs, 512, 2);
        engine.setParameters (p);

        auto out = runEngine (engine, duplicate (silence (16384), 2));

        CHECK (allFinite (out[0]));
        CHECK_LE (peak (out[0]), 1.0e-6f);
        CHECK_LE (peak (out[1]), 1.0e-6f);
        CHECK (engine.getSanitiseCount() == 0);
    }
}

BS_TEST (impulseResponseIsFiniteAndDecays)
{
    auto p = defaultParams();
    p.resonance = 1.0f;
    p.cutoffHz  = 100.0f;
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    auto out = runEngine (engine, duplicate (impulse (32768), 2));

    CHECK (allFinite (out[0]));
    CHECK_LE (peak (out[0]), 8.0f);

    const double early = rms (out[0], 0, 4096);
    const double late  = rms (out[0], 24576, 32768);
    CHECK_MSG (late < early * 1.0e-3, "impulse tail is not decaying: early " + testing::describe (early)
                                       + " late " + testing::describe (late));
    CHECK (engine.getSanitiseCount() == 0);
}

BS_TEST (dcIsRemoved)
{
    auto p = defaultParams();
    p.cutoffHz = 20.0f;         // the mildest possible setting
    p.character = 0.0f;
    p.analog = 1.0f;            // asymmetric saturation would otherwise add DC
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    auto out = runEngine (engine, duplicate (dc (48000, 0.7f), 2));

    CHECK (allFinite (out[0]));
    const double tail = std::fabs (static_cast<double> (out[0][47999]));
    CHECK_MSG (tail < 1.0e-3, "DC residue " + testing::describe (tail));
}

BS_TEST (noiseSourcesStayFiniteAndBounded)
{
    struct Source { const char* name; std::vector<float> data; };
    std::vector<Source> sources {
        { "white", whiteNoise (65536) },
        { "pink",  pinkNoise (65536) },
        { "brown", brownNoise (65536) }
    };

    for (auto& src : sources)
        for (int mode = 0; mode < kNumFilterModes; ++mode)
        for (int factor : { 1, 2, 4 })
        {
            auto p = defaultParams();
            p.filterMode = mode;
            p.oversamplingFactor = factor;
            p.cutoffHz  = 800.0f;
            p.resonance = 0.8f;
            p.analog    = 0.8f;
            p.character = 1.0f;

            BrownSweepEngine engine;
            engine.setParameters (p);
            engine.prepare (kFs, 256, 2);
            engine.setParameters (p);

            auto out = runEngine (engine, duplicate (src.data, 2), 256);

            CHECK_MSG (allFinite (out[0]), std::string (src.name) + " produced a non-finite sample");
            CHECK_MSG (peak (out[0]) < 4.0f, std::string (src.name) + " peaked at "
                                              + testing::describe (peak (out[0])));
            CHECK (engine.getSanitiseCount() == 0);
        }
}

BS_TEST (sineInputsAtManyFrequenciesStayClean)
{
    for (double f : { 20.0, 50.0, 110.0, 220.0, 440.0, 1000.0, 2500.0, 5000.0, 10000.0, 18000.0 })
    {
        auto p = defaultParams();
        p.cutoffHz  = 400.0f;
        p.resonance = 0.5f;
        p.analog    = 0.5f;
        p.oversamplingFactor = 2;
        auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

        auto out = runEngine (engine, duplicate (sine (16384, f, kFs, 0.7f), 2));

        CHECK_MSG (allFinite (out[0]), "non-finite output at " + testing::describe (f) + " Hz");
        CHECK_MSG (peak (out[0]) < 3.0f, "peak " + testing::describe (peak (out[0]))
                                          + " at " + testing::describe (f) + " Hz");
    }
}

//==============================================================================
// Channel behaviour
//==============================================================================

BS_TEST (monoProcessingWorks)
{
    auto p = defaultParams();
    p.oversamplingFactor = 2;
    BrownSweepEngine engine;
    engine.setParameters (p);
    engine.prepare (kFs, 512, 1);
    engine.setParameters (p);

    auto out = runEngine (engine, duplicate (pinkNoise (16384), 1));

    CHECK (allFinite (out[0]));
    CHECK (rms (out[0], 4096) > 1.0e-4);
}

BS_TEST (stereoChannelsAreProcessedIdentically)
{
    // Same input on both channels must give bit-identical output: any drift
    // means the two channels are not sharing coefficients.
    auto p = defaultParams();
    p.resonance = 0.7f;
    p.analog    = 0.6f;
    p.oversamplingFactor = 4;
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    auto out = runEngine (engine, duplicate (pinkNoise (32768), 2));

    for (size_t i = 0; i < out[0].size(); ++i)
        if (out[0][i] != out[1][i]) { CHECK_MSG (false, "channel drift at sample " + testing::describe (double (i))); break; }
    CHECK (out[0] == out[1]);
}

BS_TEST (thereIsNoCrosstalkBetweenChannels)
{
    auto p = defaultParams();
    p.oversamplingFactor = 2;
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    std::vector<std::vector<float>> in { impulse (8192), silence (8192) };
    auto out = runEngine (engine, in);

    CHECK (peak (out[0]) > 1.0e-4f);
    // Only the anti-denormal dither (1e-20) may appear on the silent channel.
    CHECK_LE (peak (out[1]), 1.0e-12f);
}

BS_TEST (leftAndRightHaveMatchedTransferFunctions)
{
    auto p = defaultParams();
    p.resonance = 0.6f;
    auto enginePtr1 = makeEngine (p);
    auto enginePtr2 = makeEngine (p);
    auto& engine1 = *enginePtr1;
    auto& engine2 = *enginePtr2;

    std::vector<std::vector<float>> inL { impulse (8192), silence (8192) };
    std::vector<std::vector<float>> inR { silence (8192), impulse (8192) };

    auto outL = runEngine (engine1, inL);
    auto outR = runEngine (engine2, inR);

    CHECK (outL[0] == outR[1]);
}

BS_TEST (multiChannelBusesAreSupported)
{
    for (int channels : { 1, 2, 4, 8 })
    {
        auto p = defaultParams();
        p.oversamplingFactor = 2;
        BrownSweepEngine engine;
        engine.setParameters (p);
        engine.prepare (kFs, 128, channels);
        engine.setParameters (p);

        auto out = runEngine (engine, duplicate (whiteNoise (8192), channels), 128);
        for (int ch = 0; ch < channels; ++ch)
            CHECK (allFinite (out[static_cast<size_t> (ch)]));
        CHECK (engine.getSanitiseCount() == 0);
    }
}

//==============================================================================
// Parameter behaviour
//==============================================================================

BS_TEST (parameterJumpsDoNotProduceDiscontinuities)
{
    // Slam every control from one extreme to the other mid-signal and check
    // that the output never steps by more than the input can.
    auto p = defaultParams();
    p.cutoffHz  = 20.0f;
    p.character = 0.0f;
    p.mix       = 1.0f;
    p.outputDb  = 0.0f;
    p.oversamplingFactor = 1;
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    const int n = 48000;
    auto input = duplicate (sine (n, 220.0, kFs, 0.6f), 2);
    auto output = input;

    const double inputMaxStep = 0.6 * 2.0 * kPi * 220.0 / kFs;

    std::vector<float*> ptrs (2);
    double worstStep = 0.0;

    for (int pos = 0; pos < n; pos += 64)
    {
        if (pos == 12800) { p.cutoffHz = 8000.0f; p.character = 1.0f; p.resonance = 1.0f; engine.setParameters (p); }
        if (pos == 25600) { p.cutoffHz = 20.0f;   p.outputDb = 12.0f; p.analog = 1.0f;    engine.setParameters (p); }
        if (pos == 38400) { p.mix = 0.0f;         p.outputDb = -24.0f;                    engine.setParameters (p); }

        for (int ch = 0; ch < 2; ++ch) ptrs[static_cast<size_t> (ch)] = output[static_cast<size_t> (ch)].data() + pos;
        engine.process (ptrs.data(), 2, 64);
    }

    for (size_t i = 1; i < output[0].size(); ++i)
        worstStep = std::max (worstStep, std::fabs (static_cast<double> (output[0][i]) - output[0][i - 1]));

    CHECK (allFinite (output[0]));
    CHECK_MSG (worstStep < inputMaxStep * 6.0,
               "worst output step " + testing::describe (worstStep)
               + " vs input step " + testing::describe (inputMaxStep));
}

BS_TEST (slopeChangesDoNotClick)
{
    auto p = defaultParams();
    p.cutoffHz = 300.0f;
    p.oversamplingFactor = 1;
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    const int n = 48000;
    auto output = duplicate (sine (n, 500.0, kFs, 0.6f), 2);
    std::vector<float*> ptrs (2);

    const double inputMaxStep = 0.6 * 2.0 * kPi * 500.0 / kFs;
    double worstStep = 0.0;

    for (int pos = 0; pos < n; pos += 64)
    {
        if (pos % 6400 == 0) { p.slopeIndex = (p.slopeIndex + 1) % kNumSlopes; engine.setParameters (p); }
        for (int ch = 0; ch < 2; ++ch) ptrs[static_cast<size_t> (ch)] = output[static_cast<size_t> (ch)].data() + pos;
        engine.process (ptrs.data(), 2, 64);
    }

    for (size_t i = 1; i < output[0].size(); ++i)
        worstStep = std::max (worstStep, std::fabs (static_cast<double> (output[0][i]) - output[0][i - 1]));

    CHECK (allFinite (output[0]));
    CHECK_MSG (worstStep < inputMaxStep * 4.0, "worst step on slope change " + testing::describe (worstStep));
}

BS_TEST (oversamplingChangesAreFadedNotSwitched)
{
    auto p = defaultParams();
    p.cutoffHz = 300.0f;
    p.analog = 0.7f;
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    const int n = 96000;
    auto output = duplicate (sine (n, 500.0, kFs, 0.6f), 2);
    std::vector<float*> ptrs (2);
    const int factors[3] = { 1, 2, 4 };
    int next = 0;

    const double inputMaxStep = 0.6 * 2.0 * kPi * 500.0 / kFs;
    double worstStep = 0.0;

    for (int pos = 0; pos < n; pos += 64)
    {
        if (pos % 16000 == 0) { p.oversamplingFactor = factors[next++ % 3]; engine.setParameters (p); }
        for (int ch = 0; ch < 2; ++ch) ptrs[static_cast<size_t> (ch)] = output[static_cast<size_t> (ch)].data() + pos;
        engine.process (ptrs.data(), 2, 64);
    }

    for (size_t i = 1; i < output[0].size(); ++i)
        worstStep = std::max (worstStep, std::fabs (static_cast<double> (output[0][i]) - output[0][i - 1]));

    CHECK (allFinite (output[0]));
    CHECK_MSG (worstStep < inputMaxStep * 4.0, "worst step on oversampling change " + testing::describe (worstStep));
    CHECK (engine.getSanitiseCount() == 0);
}

BS_TEST (extremeSettingsRemainStable)
{
    const float cutoffs[] = { 20.0f, 20.0001f, 19999.0f, 20000.0f };
    for (float cutoff : cutoffs)
        for (int slope = 0; slope < kNumSlopes; ++slope)
            for (float resonance : { 0.0f, 1.0f })
            {
                auto p = defaultParams();
                p.cutoffHz  = cutoff;
                p.slopeIndex = slope;
                p.resonance = resonance;
                p.character = 1.0f;
                p.analog    = 1.0f;
                p.oversamplingFactor = 2;

                BrownSweepEngine engine;
                engine.setParameters (p);
                engine.prepare (kFs, 128, 2);
                engine.setParameters (p);

                auto out = runEngine (engine, duplicate (whiteNoise (24000, 31u), 2), 128);

                CHECK_MSG (allFinite (out[0]), "non-finite at cutoff " + testing::describe (cutoff)
                                                + " slope " + testing::describe (slope));
                CHECK_MSG (peak (out[0]) < 8.0f, "peak " + testing::describe (peak (out[0]))
                                                  + " at cutoff " + testing::describe (cutoff));
                CHECK (engine.getSanitiseCount() == 0);
            }
}

BS_TEST (sampleRatesFrom44kTo192kAllWork)
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        auto p = defaultParams();
        p.cutoffHz  = 19000.0f;      // close to Nyquist at 44.1 kHz
        p.resonance = 1.0f;
        p.analog    = 1.0f;
        p.character = 1.0f;
        p.oversamplingFactor = 2;

        BrownSweepEngine engine;
        engine.setParameters (p);
        engine.prepare (fs, 256, 2);
        engine.setParameters (p);

        auto out = runEngine (engine, duplicate (whiteNoise (32768, 77u), 2), 256);

        CHECK_MSG (allFinite (out[0]), "non-finite at " + testing::describe (fs) + " Hz");
        CHECK_MSG (peak (out[0]) < 4.0f, "peak " + testing::describe (peak (out[0]))
                                          + " at " + testing::describe (fs) + " Hz");
        CHECK (engine.getSanitiseCount() == 0);
    }
}

BS_TEST (longRunsStayStable)
{
    auto p = defaultParams();
    p.resonance = 1.0f;
    p.analog = 1.0f;
    p.character = 1.0f;
    p.oversamplingFactor = 2;
    p.lfo[0].destination = static_cast<int> (LfoDestination::cutoff);
    p.lfo[0].depth = 1.0f;
    p.lfo[0].rateHz = 3.0f;
    p.lfo[1].destination = static_cast<int> (LfoDestination::resonance);
    p.lfo[1].depth = 1.0f;
    p.lfo[1].rateHz = 0.7f;
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    // Five minutes of audio.
    for (int block = 0; block < 300; ++block)
    {
        auto out = runEngine (engine, duplicate (pinkNoise (48000, static_cast<uint32_t> (block + 1)), 2), 512);
        if (! allFinite (out[0])) { CHECK_MSG (false, "non-finite after " + testing::describe (block) + " s"); break; }
        if (peak (out[0]) > 8.0f)  { CHECK_MSG (false, "runaway after " + testing::describe (block) + " s"); break; }
    }
    CHECK (engine.getSanitiseCount() == 0);
}

//==============================================================================
// Mix / latency
//==============================================================================

BS_TEST (fullyDryOutputIsTheInputDelayedByTheReportedLatency)
{
    for (int factor : { 1, 2 })
    {
        auto p = defaultParams();
        p.mix = 0.0f;
        p.oversamplingFactor = factor;
        auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

        const int latency = static_cast<int> (std::lround (engine.getLatencySamples()));

        auto in  = duplicate (pinkNoise (16384, 5u), 2);
        auto out = runEngine (engine, in);

        double worst = 0.0;
        for (int i = 2048; i < 16384 - latency; ++i)
            worst = std::max (worst, std::fabs (static_cast<double> (out[0][static_cast<size_t> (i + latency)])
                                                - in[0][static_cast<size_t> (i)]));

        CHECK_MSG (worst < 1.0e-5, "dry path error " + testing::describe (worst)
                                    + " at factor " + testing::describe (factor));
    }
}

BS_TEST (bypassReturnsTheInputDelayedByTheReportedLatency)
{
    // A bypass that does not honour the plugin's own reported latency puts the
    // track out of time with everything the host delay-compensated.
    for (int factor : { 1, 2 })
    {
        auto p = defaultParams();
        p.oversamplingFactor = factor;
        p.outputDb = 9.0f;          // must be ignored while bypassed
        p.character = 1.0f;
        p.cutoffHz = 5000.0f;
        p.bypassed = true;
        auto enginePtr = makeEngine (p);
        auto& engine = *enginePtr;

        const int latency = static_cast<int> (std::lround (engine.getLatencySamples()));

        auto in  = duplicate (pinkNoise (16384, 91u), 2);
        auto out = runEngine (engine, in);

        double worst = 0.0;
        for (int i = 4096; i < 16384 - latency; ++i)
            worst = std::max (worst, std::fabs (static_cast<double> (out[0][static_cast<size_t> (i + latency)])
                                                - in[0][static_cast<size_t> (i)]));

        CHECK_MSG (worst < 1.0e-5, "bypass error " + testing::describe (worst)
                                    + " at factor " + testing::describe (factor));
    }
}

BS_TEST (bypassSwitchingIsSmooth)
{
    auto p = defaultParams();
    p.cutoffHz = 2000.0f;
    p.character = 1.0f;
    p.oversamplingFactor = 1;
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    const int n = 48000;
    auto output = duplicate (sine (n, 700.0, kFs, 0.6f), 2);
    std::vector<float*> ptrs (2);

    const double inputMaxStep = 0.6 * 2.0 * kPi * 700.0 / kFs;
    double worstStep = 0.0;

    for (int pos = 0; pos < n; pos += 64)
    {
        if (pos % 9600 == 0) { p.bypassed = ! p.bypassed; engine.setParameters (p); }
        for (int ch = 0; ch < 2; ++ch) ptrs[static_cast<size_t> (ch)] = output[static_cast<size_t> (ch)].data() + pos;
        engine.process (ptrs.data(), 2, 64);
    }

    for (size_t i = 1; i < output[0].size(); ++i)
        worstStep = std::max (worstStep, std::fabs (static_cast<double> (output[0][i]) - output[0][i - 1]));

    CHECK (allFinite (output[0]));
    CHECK_MSG (worstStep < inputMaxStep * 4.0, "worst step on bypass switch " + testing::describe (worstStep));
}

BS_TEST (filterModeSwitchingIsSmooth)
{
    auto p = defaultParams();
    p.cutoffHz  = 700.0f;
    p.resonance = 0.8f;
    p.analog    = 0.6f;
    p.oversamplingFactor = 1;
    auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;

    const int n = 96000;
    auto output = duplicate (sine (n, 900.0, kFs, 0.6f), 2);
    std::vector<float*> ptrs (2);

    const double inputMaxStep = 0.6 * 2.0 * kPi * 900.0 / kFs;
    double worstStep = 0.0;

    for (int pos = 0; pos < n; pos += 64)
    {
        if (pos % 9600 == 0)
        {
            p.filterMode = 1 - p.filterMode;
            engine.setParameters (p);
        }
        for (int ch = 0; ch < 2; ++ch) ptrs[static_cast<size_t> (ch)] = output[static_cast<size_t> (ch)].data() + pos;
        engine.process (ptrs.data(), 2, 64);
    }

    for (size_t i = 1; i < output[0].size(); ++i)
        worstStep = std::max (worstStep, std::fabs (static_cast<double> (output[0][i]) - output[0][i - 1]));

    CHECK (allFinite (output[0]));
    CHECK_MSG (worstStep < inputMaxStep * 4.0, "worst step on mode change " + testing::describe (worstStep));
    CHECK (engine.getSanitiseCount() == 0);
}

BS_TEST (ladderModeSurvivesEverythingTheCleanModeDoes)
{
    for (int slope = 0; slope < kNumSlopes; ++slope)
        for (float cutoff : { 20.0f, 250.0f, 3000.0f, 20000.0f })
        {
            auto p = defaultParams();
            p.filterMode = static_cast<int> (FilterMode::ladder);
            p.slopeIndex = slope;
            p.cutoffHz   = cutoff;
            p.resonance  = 1.0f;
            p.analog     = 1.0f;
            p.character  = 1.0f;
            p.oversamplingFactor = 2;

            BrownSweepEngine engine;
            engine.setParameters (p);
            engine.prepare (kFs, 256, 2);
            engine.setParameters (p);

            auto out = runEngine (engine, duplicate (whiteNoise (32768, 5150u), 2), 256);

            CHECK_MSG (allFinite (out[0]), "ladder non-finite at cutoff " + testing::describe (cutoff)
                                            + " slope " + testing::describe (slope));
            CHECK_MSG (peak (out[0]) < 8.0f, "ladder peaked at " + testing::describe (peak (out[0]))
                                              + " at cutoff " + testing::describe (cutoff));
            CHECK (engine.getSanitiseCount() == 0);
        }
}

BS_TEST (latencyMatchesTheOversamplingSetting)
{
    for (auto pair : { std::pair<int, float> { 1, 0.0f },
                       std::pair<int, float> { 2, 23.0f },
                       std::pair<int, float> { 4, 30.5f } })
    {
        auto p = defaultParams();
        p.oversamplingFactor = pair.first;
        auto enginePtr = makeEngine (p);
    auto& engine = *enginePtr;
        CHECK_NEAR (engine.getLatencySamples(), pair.second, 1.0e-6f);
    }
}

//==============================================================================
// The model and the running filter must agree
//==============================================================================

BS_TEST (measuredResponseMatchesTheAnalyticModel)
{
    // Both topologies: the zero-delay ladder is exactly the bilinear transform
    // of its analogue prototype too, so the model has to hold there as well.
    for (int mode = 0; mode < kNumFilterModes; ++mode)
    for (float cutoff : { 200.0f, 1000.0f, 5000.0f })
        for (float resonance : { 0.0f, 0.8f })
        {
            auto p = defaultParams();
            p.filterMode = mode;
            p.cutoffHz  = cutoff;
            p.resonance = resonance;
            p.character = 1.0f;
            p.analog    = 0.0f;      // keep the chain linear so a transfer function exists
            p.oversamplingFactor = 1;
            p.slopeIndex = 2;

            BrownSweepEngine engine;
            engine.setParameters (p);
            engine.prepare (kFs, 512, 1);

            constexpr int n = 32768;
            auto ir = impulseResponse (engine, p, n);
            CHECK (allFinite (ir));

            std::vector<std::complex<double>> spec (n);
            for (int i = 0; i < n; ++i) spec[static_cast<size_t> (i)] = { static_cast<double> (ir[static_cast<size_t> (i)]), 0.0 };
            fft (spec);

            const auto state = engine.getResponseState();

            double worst = 0.0;
            double worstAt = 0.0;
            for (int k = 1; k < n / 2; ++k)
            {
                const double f = k * kFs / n;
                if (f < 60.0 || f > 16000.0) continue;

                const double measured = 20.0 * std::log10 (std::max (std::abs (spec[static_cast<size_t> (k)]), 1.0e-30));
                const double model    = responsemodel::magnitudeDb (
                    responsemodel::fullResponse (state, static_cast<float> (f)));

                // Below about -80 dB a 32-bit impulse response is measuring its
                // own numerical noise floor, not the filter.
                if (model < -80.0) continue;

                if (std::fabs (measured - model) > worst) { worst = std::fabs (measured - model); worstAt = f; }
            }

            CHECK_MSG (worst < 0.6, "model/measurement mismatch " + testing::describe (worst)
                                     + " dB at " + testing::describe (worstAt) + " Hz"
                                     + " (mode " + testing::describe (mode)
                                     + ", cutoff " + testing::describe (cutoff)
                                     + ", resonance " + testing::describe (resonance) + ")");
        }
}
