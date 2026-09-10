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
    bool  active    = false;

    void setTapOrder (int order) noexcept
    {
        static constexpr float kBinomial[5][5] =
        {
            {  1.0f,  0.0f,  0.0f,  0.0f, 0.0f },
            {  1.0f, -1.0f,  0.0f,  0.0f, 0.0f },
            {  1.0f, -2.0f,  1.0f,  0.0f, 0.0f },
            {  1.0f, -3.0f,  3.0f, -1.0f, 0.0f },
            {  1.0f, -4.0f,  6.0f, -4.0f, 1.0f }
        };

        const int n = clampValue (order, 0, 4);
        for (int i = 0; i < 5; ++i) taps[i] = kBinomial[n][i];
    }

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

/** Coefficients for the whole ladder stage (one or two cascaded ladders). */
struct LadderCoefficients
{
    SingleLadderCoefficients ladder[2];
    AnalogCoefficients feedbackShaper;   // identity when ANALOG is 0
    float poleHz = 20.0f;

    void update (float cutoffHz, int slopeIndex, float resonance01, float analog01, float sampleRate) noexcept
    {
        const auto& cfg = kLadderConfigs[clampValue (slopeIndex, 0, kNumSlopes - 1)];

        poleHz = clampValue (cutoffHz / cfg.minus3dbScale, 1.0f, 0.49f * sampleRate);

        const int orders[2] = { cfg.tapA, cfg.tapB };

        for (int i = 0; i < 2; ++i)
        {
            auto& l = ladder[i];
            l.active = (orders[i] > 0);

            if (! l.active)
            {
                l.setTapOrder (0);
                l.setPole (poleHz, sampleRate);
                l.setFeedback (0.0f);
                continue;
            }

            l.setTapOrder (orders[i]);
            l.setPole (poleHz, sampleRate);
            l.setFeedback (ladderFeedback (cfg, i, resonance01));
        }

        // The feedback clipper reuses the analogue stage's transfer curve, but
        // driven harder and with more offset - it is inside a loop, so its
        // artefacts are shaped by the filter rather than sprayed across the
        // output.
        feedbackShaper.updateCustom (analog01, kLadderFeedbackDrive, kLadderFeedbackBias);
    }
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
        for (int i = 0; i < 2; ++i)
        {
            if (! c.ladder[i].active) continue;
            x = processOne (ladders[i], c.ladder[i], c.feedbackShaper, x);
        }
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
