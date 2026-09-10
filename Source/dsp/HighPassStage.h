/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    HighPassStage.h - the conventional part of the filter: a cascade of
    Butterworth-aligned TPT state-variable high-pass sections, plus one
    first-order section for the odd 18 dB/octave setting.

    Slope morphing
    --------------
    All four SVF sections always run.  Sections that the current slope does not
    need are faded to an algebraic bypass (their output is cross-faded back to
    their input) rather than being switched off, and their Q targets are
    smoothed.  Two consequences:

      * changing SLOPE mid-performance cannot click, because nothing is ever
        switched abruptly and every section's state stays warm;
      * the CPU cost is constant regardless of slope, which makes the plugin
        predictable on a session with twenty instances.

    Running the unused sections costs about four extra one-pole-equivalents per
    channel - far less than the tilt cascade next to it - so the trade is easy.

    Resonance
    ---------
    RESONANCE multiplies the Q of the *last* active section only, up to
    (1 + kResonanceQFactor) which is roughly a 12 dB peak.  Placing it last
    means the level-dependent damping (the "analogue" term) sees an already
    high-passed signal, so the resonance blooms and settles the way a real
    cascaded filter does instead of ringing at a constant amplitude.

    Mode switching
    --------------
    The stage also owns the LADDER topology (see LadderStage.h).  Only the
    selected one runs; a mode change cross-fades between them over ~30 ms, with
    the incoming topology reset to zero state first so it fades in from silence
    rather than from whatever it last held.  Outside a transition the cost is
    exactly one topology, so choosing a mode does not cost CPU.
*/

#pragma once

#include "DesignConstants.h"
#include "Filters.h"
#include "LadderStage.h"

namespace bsweep
{

class HighPassStage
{
public:
    void prepare (float sr) noexcept { sampleRate = sr; }

    void update (float cutoffHz, int slopeIndex, int filterMode, float resonance01,
                 float analog01, float alpha, float sampleRateForLadder) noexcept
    {
        updateBlend (filterMode, alpha);

        // Coming back from CLEAN, the ladder's smoothers hold whatever they had
        // when it was last switched off - possibly a different slope entirely -
        // so they are snapped rather than ramped from stale values.
        if (activatedLadder)
            ladderCoefficients.reset();

        if (needsLadder())
            ladderCoefficients.update (cutoffHz, slopeIndex, resonance01, analog01,
                                       sampleRateForLadder, alpha);

        const auto& cfg = kSlopeConfigs[clampValue (slopeIndex, 0, kNumSlopes - 1)];
        resonantIdx = cfg.numSvfStages - 1;

        const float g = tptGain (cutoffHz, sampleRate);
        const float resFactor = 1.0f + kResonanceQFactor * clampValue (resonance01, 0.0f, 1.0f);

        for (int i = 0; i < kMaxSvfStages; ++i)
        {
            const bool  used    = (i < cfg.numSvfStages);
            const float qTarget = used ? cfg.q[i] * (i == resonantIdx ? resFactor : 1.0f)
                                       : 0.70710678f;
            const float aTarget = used ? 1.0f : 0.0f;

            qSmoothed[i]   += (qTarget - qSmoothed[i]) * alpha;
            activations[i] += (aTarget - activations[i]) * alpha;

            coeffs[i].set (g, 1.0f / std::max (qSmoothed[i], 0.05f));
        }

        const float opTarget = cfg.usesOnePole ? 1.0f : 0.0f;
        onePoleAct += (opTarget - onePoleAct) * alpha;
        onePoleBigG = g / (1.0f + g);

        // Only the resonant section gets the non-linear damping term.
        damping = clampValue (analog01, 0.0f, 1.0f) * kAnalogMaxFilterDamping;
        cutoff  = cutoffHz;
    }

    const SvfCoefficients&   svfCoefficients (int i) const noexcept { return coeffs[i]; }
    const LadderCoefficients& ladder()          const noexcept { return ladderCoefficients; }

    /** 0 = CLEAN only, 1 = LADDER only, in between = cross-fading. */
    float modeBlend() const noexcept { return blend; }
    bool  needsClean()  const noexcept { return blend < 1.0f; }
    bool  needsLadder() const noexcept { return blend > 0.0f; }

    /** True for the one block on which a topology becomes active again. */
    bool  cleanJustActivated()  const noexcept { return activatedClean; }
    bool  ladderJustActivated() const noexcept { return activatedLadder; }
    float activation (int i)   const noexcept { return activations[i]; }
    float qOf (int i)          const noexcept { return qSmoothed[i]; }
    float onePoleG()           const noexcept { return onePoleBigG; }
    float onePoleActivation()  const noexcept { return onePoleAct; }
    float nonlinearDamping()   const noexcept { return damping; }
    int   resonantIndex()      const noexcept { return resonantIdx; }
    float cutoffHz()           const noexcept { return cutoff; }

private:
    void updateBlend (int filterMode, float alpha) noexcept
    {
        const bool wantsClean  = blend < 1.0f;
        const bool wantsLadder = blend > 0.0f;

        const float target = (filterMode == static_cast<int> (FilterMode::ladder)) ? 1.0f : 0.0f;
        blend += (target - blend) * alpha;

        // Snap to the endpoints so that outside a transition exactly one
        // topology runs and the other can stay switched off entirely.
        if (std::abs (blend - target) < 1.0e-4f) blend = target;

        activatedClean  = (blend < 1.0f) && ! wantsClean;
        activatedLadder = (blend > 0.0f) && ! wantsLadder;
    }

    float sampleRate = 48000.0f;
    SvfCoefficients coeffs[kMaxSvfStages];
    LadderCoefficients ladderCoefficients;
    float blend = 0.0f;
    bool  activatedClean = false;
    bool  activatedLadder = false;
    float qSmoothed[kMaxSvfStages]   { 0.70710678f, 0.70710678f, 0.70710678f, 0.70710678f };
    float activations[kMaxSvfStages] { 1.0f, 0.0f, 0.0f, 0.0f };
    float onePoleBigG = 0.5f;
    float onePoleAct  = 0.0f;
    float damping     = 0.0f;
    float cutoff      = 20.0f;
    int   resonantIdx = 0;
};

/** Per-channel state for the high-pass cascade. */
class HighPassState
{
public:
    void reset() noexcept
    {
        for (auto& s : svf) s.reset();
        onePole.reset();
        ladder.reset();
    }

    /** Called once per control block, before the sample loop. */
    void beginBlock (const HighPassStage& st) noexcept
    {
        if (st.cleanJustActivated())
        {
            for (auto& s : svf) s.reset();
            onePole.reset();
        }
        if (st.ladderJustActivated())
            ladder.reset();
    }

    inline float process (const HighPassStage& st, float x) noexcept
    {
        const float blend = st.modeBlend();

        if (blend <= 0.0f) return processClean (st, x);
        if (blend >= 1.0f) return ladder.process (st.ladder(), x);

        const float clean = processClean (st, x);
        const float lad   = ladder.process (st.ladder(), x);
        return lerp (clean, lad, blend);
    }

    bool isFinite() const noexcept
    {
        for (const auto& s : svf) if (! s.isFinite()) return false;
        return onePole.isFinite() && ladder.isFinite();
    }

private:
    inline float processClean (const HighPassStage& st, float x) noexcept
    {
        const int   resIdx  = st.resonantIndex();
        const float damping = st.nonlinearDamping();

        for (int i = 0; i < kMaxSvfStages; ++i)
        {
            const float hp = (i == resIdx)
                ? svf[i].processHighpassSaturatingShared (x, st.svfCoefficients (i), damping)
                : svf[i].processHighpassShared (x, st.svfCoefficients (i));

            x += st.activation (i) * (hp - x);
        }

        const float hp1 = onePole.processHighpass (x, st.onePoleG());
        x += st.onePoleActivation() * (hp1 - x);
        return x;
    }

    SvfTpt      svf[kMaxSvfStages];
    OnePoleTpt  onePole;
    LadderState ladder;
};

} // namespace bsweep
