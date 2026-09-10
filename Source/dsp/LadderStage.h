/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    LadderStage.h - the SH-101-flavoured filter topology.

    Four TPT one-pole stages in series with one global feedback loop, solved
    zero-delay, and tapped with binomial coefficients to produce a high-pass:

        u  = x - k * y4                       (feedback from the low-pass tap)
        yn = LP(y[n-1]),  y0 = u
        HP = sum_j C(N,j) (-1)^j y_j  =  u * (s/(1+s))^N

    which gives the closed form

        H(s) = s^N (1+s)^(4-N) / ( (1+s)^4 + k )

    exactly, because a zero-delay-feedback structure with prewarped integrators
    *is* the bilinear transform of its analogue prototype.  ResponseModel relies
    on that, and the test suite checks it against an FFT of the real impulse
    response.

    The zero-delay solve
    --------------------
    A TPT one-pole is lp = G*in + (1-G)*s, so the four stages give

        y4 = G^4 * u + Sacc,   Sacc = G^3 S1 + G^2 S2 + G S3 + S4

    with S_i = (1-G) * state_i, and therefore

        u = (x - k * Sacc) / (1 + k * G^4)

    - one multiply-add chain and one pre-computed reciprocal per sample.

    The non-linearity
    -----------------
    A real ladder clips inside the feedback loop, and in the IR3109 it clips
    *asymmetrically*.  Solving that exactly would need an iteration per sample,
    so it is applied as a residual instead:

        x' = x + k * (yPrev - saturate(yPrev))

    then the ordinary linear solve runs on x'.  When `saturate` is the identity
    the residual is exactly zero, so ANALOG = 0 leaves the filter perfectly
    linear and the analytic model exact.  When it is not, the correction is the
    saturator's *residual* - a small, bounded quantity - carried one sample, so
    the structure stays unconditionally stable while still producing the
    level-dependent, lopsided resonance that the symmetric alternative cannot.

    In a high-pass ladder the feedback tap is the low-pass output, i.e. the part
    of the spectrum the filter is throwing away.  Sweeping the cutoff upward
    therefore feeds the disappearing bass into the clipper, which is why the
    resonance growls and settles as the sweep moves instead of sitting still.
*/

#pragma once

#include "AnalogStage.h"
#include "DesignConstants.h"
#include "Filters.h"

namespace bsweep
{

/** One ladder's shared coefficients. */
struct SingleLadderCoefficients
{
    float bigG      = 0.5f;
    float feedback  = 0.0f;      // k
    float solveGain = 1.0f;      // 1 / (1 + k * G^4)
    float taps[5]   { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    void setPole (float poleHz, float sampleRate) noexcept
    {
        const float g = tptGain (poleHz, sampleRate);
        bigG = g / (1.0f + g);
    }

    void setFeedback (float k) noexcept
    {
        feedback = k;
        const float g2 = bigG * bigG;
        solveGain = 1.0f / (1.0f + k * g2 * g2);
    }
};

/** Binomial tap sets.  Order 0 - {1,0,0,0,0} with zero feedback - is an exact
    algebraic passthrough: u = (x - 0) * 1.0 and the output is 1.0 * u.  That is
    what lets the second ladder run permanently and be faded in and out rather
    than switched, so changing SLOPE cannot click. */
inline const float* ladderTaps (int order) noexcept
{
    static constexpr float kBinomial[5][5] =
    {
        {  1.0f,  0.0f,  0.0f,  0.0f, 0.0f },
        {  1.0f, -1.0f,  0.0f,  0.0f, 0.0f },
        {  1.0f, -2.0f,  1.0f,  0.0f, 0.0f },
        {  1.0f, -3.0f,  3.0f, -1.0f, 0.0f },
        {  1.0f, -4.0f,  6.0f, -4.0f, 1.0f }
    };
    return kBinomial[clampValue (order, 0, 4)];
}

/** Coefficients for the whole ladder stage (two cascaded ladders, the second of
    which is a passthrough for slopes of 24 dB/oct and below).

    Everything that differs between slope settings - the tap coefficients, the
    feedback amounts and the pole placement, which moves by up to 1.1 octaves
    between 12 and 48 dB/oct - is smoothed.  Without that, changing SLOPE
    produced an output step 2.7x larger than the signal's own maximum slew; the
    Butterworth cascade next door has always cross-faded its sections for the
    same reason. */
struct LadderCoefficients
{
    SingleLadderCoefficients ladder[2];
    AnalogCoefficients feedbackShaper;   // identity when ANALOG is 0
    float poleHz = 20.0f;

    void update (float cutoffHz, int slopeIndex, float resonance01, float analog01,
                 float sampleRate, float alpha) noexcept
    {
        const auto& cfg = kLadderConfigs[clampValue (slopeIndex, 0, kNumSlopes - 1)];
        const int orders[2] = { cfg.tapA, cfg.tapB };

        const float targetLogScale = std::log2 (cfg.minus3dbScale);
        const float targetFeedback[2] = { ladderFeedback (cfg, 0, resonance01),
                                          cfg.tapB > 0 ? ladderFeedback (cfg, 1, resonance01) : 0.0f };

        if (! initialised)
        {
            smoothedLogScale = targetLogScale;
            for (int i = 0; i < 2; ++i)
            {
                smoothedFeedback[i] = targetFeedback[i];
                for (int t = 0; t < 5; ++t) smoothedTaps[i][t] = ladderTaps (orders[i])[t];
            }
            initialised = true;
        }
        else
        {
            smoothedLogScale += (targetLogScale - smoothedLogScale) * alpha;
            for (int i = 0; i < 2; ++i)
            {
                smoothedFeedback[i] += (targetFeedback[i] - smoothedFeedback[i]) * alpha;
                const float* target = ladderTaps (orders[i]);
                for (int t = 0; t < 5; ++t)
                    smoothedTaps[i][t] += (target[t] - smoothedTaps[i][t]) * alpha;
            }
        }

        poleHz = clampValue (cutoffHz / std::exp2 (smoothedLogScale), 1.0f, 0.49f * sampleRate);

        for (int i = 0; i < 2; ++i)
        {
            ladder[i].setPole (poleHz, sampleRate);
            ladder[i].setFeedback (smoothedFeedback[i]);
            for (int t = 0; t < 5; ++t) ladder[i].taps[t] = smoothedTaps[i][t];
        }

        // The feedback clipper reuses the analogue stage's transfer curve, but
        // driven harder and with more offset - it is inside a loop, so its
        // artefacts are shaped by the filter rather than sprayed across the
        // output.
        feedbackShaper.updateCustom (analog01, kLadderFeedbackDrive, kLadderFeedbackBias);
    }

    void reset() noexcept { initialised = false; }

private:
    float smoothedLogScale = 0.0f;
    float smoothedFeedback[2] {};
    float smoothedTaps[2][5] {};
    bool  initialised = false;
};

/** Per-channel ladder state. */
class LadderState
{
public:
    void reset() noexcept
    {
        for (auto& l : ladders)
        {
            for (auto& s : l.stage) s.reset();
            l.previousFeedbackTap = 0.0f;
        }
    }

    inline float process (const LadderCoefficients& c, float x) noexcept
    {
        // Both ladders always run.  An unused one carries passthrough taps and
        // zero feedback, which is bit-exact, so this costs colour nothing and
        // keeps its state warm for the moment SLOPE brings it in.
        x = processOne (ladders[0], c.ladder[0], c.feedbackShaper, x);
        x = processOne (ladders[1], c.ladder[1], c.feedbackShaper, x);
        return x;
    }

    bool isFinite() const noexcept
    {
        for (const auto& l : ladders)
        {
            for (const auto& s : l.stage) if (! s.isFinite()) return false;
            if (! std::isfinite (l.previousFeedbackTap)) return false;
        }
        return true;
    }

private:
    struct One
    {
        OnePoleTpt stage[4];
        float previousFeedbackTap = 0.0f;
    };

    static inline float processOne (One& state, const SingleLadderCoefficients& c,
                                    const AnalogCoefficients& shaper, float x) noexcept
    {
        const float g = c.bigG;
        const float oneMinusG = 1.0f - g;

        const float s1 = oneMinusG * state.stage[0].getState();
        const float s2 = oneMinusG * state.stage[1].getState();
        const float s3 = oneMinusG * state.stage[2].getState();
        const float s4 = oneMinusG * state.stage[3].getState();
        const float accumulated = ((s1 * g + s2) * g + s3) * g + s4;

        float input = x;
        if (! shaper.linear)
        {
            const float previous = state.previousFeedbackTap;
            input += c.feedback * (previous - analogTransfer (previous, shaper));
        }

        const float u = (input - c.feedback * accumulated) * c.solveGain;

        const float y1 = state.stage[0].processLowpassShared (u,  g);
        const float y2 = state.stage[1].processLowpassShared (y1, g);
        const float y3 = state.stage[2].processLowpassShared (y2, g);
        const float y4 = state.stage[3].processLowpassShared (y3, g);

        state.previousFeedbackTap = y4;

        return c.taps[0] * u + c.taps[1] * y1 + c.taps[2] * y2 + c.taps[3] * y3 + c.taps[4] * y4;
    }

    One ladders[2];
};

} // namespace bsweep
