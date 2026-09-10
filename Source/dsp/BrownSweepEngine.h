/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    BrownSweepEngine.h - the complete signal chain, free of any JUCE
    dependency so that it can be built and tested standalone.

    Signal flow (per channel, all coefficients shared between channels):

        IN ---+--------------------------------------------------[dry delay]--+
              |                                                               |
              +-> [up 2x/4x] -> HP cascade -> brown tilt -> tone -> analogue --+--> MIX -> OUT
                                                                    -> [down] -> x AUTO gain

    Every coefficient is computed once per ~0.35 ms control block and shared by
    all channels, which is what guarantees that a stereo signal comes out with
    its image intact.
*/

#pragma once

#include <atomic>
#include <vector>

#include "AnalogStage.h"
#include "BrownTilt.h"
#include "DesignConstants.h"
#include "FractionalDelay.h"
#include "HighPassStage.h"
#include "Lfo.h"
#include "LoudnessSchedule.h"
#include "Oversampler.h"
#include "ResponseModel.h"
#include "ToneStage.h"

namespace bsweep
{

struct EngineParameters
{
    float cutoffHz   = 20.0f;
    float character  = 0.60f;
    float analog     = 0.30f;
    float resonance  = 0.0f;
    float mix        = 1.0f;
    float outputDb   = 0.0f;
    int   slopeIndex = 2;        // 24 dB/oct
    int   filterMode = static_cast<int> (FilterMode::ladder);
    bool  autoGain   = true;
    int   oversamplingFactor = 2;

    /** Soft bypass.  The wet chain keeps running (so un-bypassing cannot click)
        but the output becomes the *delayed* dry signal, which is what keeps the
        plugin aligned with the latency it reports to the host. */
    bool  bypassed   = false;

    LfoParameters lfo[2];
    double bpm = 120.0;
};

class BrownSweepEngine
{
public:
    static constexpr int kMaxChannels = 8;

    BrownSweepEngine();

    void prepare (double hostSampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Called once per audio block, before process(). */
    void setParameters (const EngineParameters& p) noexcept;

    /** In-place processing.  `numSamples` is at the host sample rate. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Host-rate latency of the current oversampling setting. */
    float getLatencySamples() const noexcept { return oversampler.getLatencySamples(); }

    /** Lock-free snapshot for the editor. */
    ResponseState getResponseState() const noexcept;

    /** Number of times the output had to be muted because a non-finite sample
        appeared.  Should stay at zero; the tests assert that it does. */
    int getSanitiseCount() const noexcept { return sanitiseCount.load (std::memory_order_relaxed); }

    double getOversampledRate() const noexcept { return sampleRate * oversampler.getFactor(); }

private:
    void processChunk (float* const* channels, int numChannels, int startSample, int numSamples) noexcept;
    void applyOversamplingChange();
    void publishResponseState (float cutoff, float tilt, float resonance, float bassDb,
                               float midDb, float trebleDb, float analogAmt,
                               float wetGainDb, float mixValue) noexcept;

    double sampleRate = 48000.0;
    int    maxBlock   = 512;
    int    channels   = 2;

    EngineParameters params;

    // Shared coefficients -----------------------------------------------------
    HighPassStage         hpStage;
    BrownTiltCoefficients tiltCoefficients;
    ToneCoefficients      toneCoefficients;
    AnalogCoefficients    analogCoefficients;

    // Per-channel state -------------------------------------------------------
    HighPassState    hpState[kMaxChannels];
    BrownTiltState   tiltState[kMaxChannels];
    ToneStageState   toneState[kMaxChannels];
    AnalogStageState analogState[kMaxChannels];
    FractionalDelay  dryDelay[kMaxChannels];

    Oversampler oversampler;
    Lfo         lfos[2];

    // Smoothed control values -------------------------------------------------
    float smLog2Cutoff = 0.0f;
    float smCharacter  = 0.0f;
    float smResonance  = 0.0f;
    float smAnalog     = 0.0f;
    float smBassDb     = 0.0f;
    float smMidDb      = 0.0f;
    float smTrebleDb   = 0.0f;
    float smWetGain    = 1.0f;
    float smMix        = 1.0f;
    float smOutGain    = 1.0f;
    bool  needsSnap    = true;

    // Oversampling reconfiguration fade ---------------------------------------
    int   activeOsFactor  = 2;
    int   pendingOsFactor = 2;
    float configFade      = 1.0f;
    float configFadeStep  = 1.0f;
    bool  fadingOut       = false;

    float antiDenormal = 1.0e-20f;

    std::vector<float> scratchIn, scratchDry, scratchWet;

    std::atomic<int> sanitiseCount { 0 };

    // Seqlock-published response snapshot for the editor.
    mutable std::atomic<uint32_t> responseSeq { 0 };
    ResponseState responseState;
};

} // namespace bsweep
