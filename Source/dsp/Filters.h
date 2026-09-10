/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    Filters.h - topology-preserving-transform (TPT / zero-delay-feedback)
    building blocks.

    Why TPT and not Direct Form biquads?

      * Coefficients are recomputed on every control block while the CUTOFF
        parameter is being automated.  Direct Form II biquads misbehave badly
        under fast coefficient modulation (the state no longer means what the
        new coefficients think it means), which is exactly the "digital zipper"
        artefact this plugin is trying to avoid.  TPT structures keep their
        state in physically meaningful integrator variables, so sweeping the
        cutoff sounds continuous.
      * They stay well conditioned at very low cutoffs (20 Hz at 192 kHz is a
        pathological case for DF1/DF2 in single precision).
      * The analogue prototype is preserved exactly under prewarping, which lets
        ResponseModel compute the magnitude response of the *running* filter
        analytically - the GUI curve and the loudness scheduler are therefore
        guaranteed to agree with what you hear.

    References: Vadim Zavalishin, "The Art of VA Filter Design"; Andrew Simper
    (Cytomic), "Solving the continuous SVF equations using trapezoidal
    integration".
*/

#pragma once

#include "DesignConstants.h"
#include "DspMath.h"

namespace bsweep
{

//==============================================================================
/** TPT one-pole.  Produces low-pass and high-pass outputs simultaneously; the
    shelving filters below are built by recombining them.

    Pole location is (1 - 2G) with G = g/(1+g), which is strictly inside the
    unit circle for any finite g, so the structure cannot be driven unstable by
    modulation.
*/
class OnePoleTpt
{
public:
    void reset() noexcept { s = 0.0f; }

    /** g = tan(pi * fc / fs). */
    void setG (float g) noexcept { bigG = g / (1.0f + g); }

    float getG() const noexcept { return bigG; }

    struct Outputs { float lp, hp; };

    inline Outputs process (float x) noexcept
    {
        const float v  = (x - s) * bigG;
        const float lp = v + s;
        s = lp + v;
        return { lp, x - lp };
    }

    inline float processLowpass (float x) noexcept
    {
        const float v  = (x - s) * bigG;
        const float lp = v + s;
        s = lp + v;
        return lp;
    }

    inline float processHighpass (float x) noexcept { return x - processLowpass (x); }

    inline float processHighpass (float x, float bigGIn) noexcept
    {
        const float v  = (x - s) * bigGIn;
        const float lp = v + s;
        s = lp + v;
        return x - lp;
    }

    /** High shelf: unity below the corner, gain `m` above it.

        m == 1 returns the input bit-exactly (the state is still advanced, so
        the section stays warm).  `lp + 1 * (x - lp)` is only algebraically
        equal to x - in float it rounds - and the tilt cascade relies on a
        faded-out section being a *perfect* bypass, so the guard is not
        cosmetic. */
    inline float processHighShelf (float x, float m) noexcept
    {
        const auto o = process (x);
        return isExactlyUnity (m) ? x : o.lp + m * o.hp;
    }

    /** Low shelf: gain `g0` below the corner, unity above it. */
    inline float processLowShelf (float x, float g0) noexcept
    {
        const auto o = process (x);
        return isExactlyUnity (g0) ? x : g0 * o.lp + o.hp;
    }

    /** High shelf using an externally supplied integrator gain.  Lets a whole
        cascade share one set of coefficients across channels while each channel
        keeps its own state - which is how matched stereo is guaranteed. */
    inline float processHighShelfShared (float x, float bigGIn, float m) noexcept
    {
        const float v  = (x - s) * bigGIn;
        const float lp = v + s;
        s = lp + v;
        return isExactlyUnity (m) ? x : lp + m * (x - lp);
    }

    inline float processLowShelfShared (float x, float bigGIn, float g0) noexcept
    {
        const float v  = (x - s) * bigGIn;
        const float lp = v + s;
        s = lp + v;
        return isExactlyUnity (g0) ? x : g0 * lp + (x - lp);
    }

    float getState() const noexcept { return s; }
    bool  isFinite() const noexcept { return std::isfinite (s); }

private:
    float bigG = 0.5f;
    float s    = 0.0f;
};

//==============================================================================
/** Cytomic-style trapezoidal state variable filter.

    Gives LP / BP / HP from one structure, unconditionally stable for any g > 0
    and k > 0, and - importantly here - tolerates per-control-block changes of
    both g and k without glitching.
*/
struct SvfCoefficients
{
    float g = 0.0f, k = 1.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    /** g = tan(pi * fc / fs), k = 1 / Q. */
    void set (float gIn, float kIn) noexcept
    {
        g  = gIn;
        k  = kIn;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

class SvfTpt
{
public:
    void reset() noexcept { ic1eq = ic2eq = 0.0f; lastBp = 0.0f; }

    /** g = tan(pi * fc / fs), k = 1 / Q. */
    void setCoefficients (float g, float k) noexcept
    {
        gCoeff = g;
        kCoeff = k;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    struct Outputs { float lp, bp, hp; };

    inline Outputs process (float x) noexcept
    {
        const float v3 = x - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return { v2, v1, x - kCoeff * v1 - v2 };
    }

    inline float processHighpass (float x) noexcept { return process (x).hp; }

    /** High-pass with a level-dependent damping term.

        Physical resonant filters lose Q as the resonant path is driven hard -
        the reason a real ladder "blooms" and then settles instead of ringing
        forever.  We emulate that by increasing the damping k with the squared
        band-pass state, which is cheap (one divide per sample) and can only
        ever make the filter *more* damped, so it is unconditionally safe.

        `amount` is 0 for a perfectly linear filter. */
    inline float processHighpassSaturating (float x, float amount) noexcept
    {
        if (amount > 0.0f)
        {
            const float bp2 = clampValue (lastBp * lastBp, 0.0f, 4.0f);
            const float kEff = kCoeff * (1.0f + amount * bp2);
            const float d  = 1.0f / (1.0f + gCoeff * (gCoeff + kEff));
            const float b2 = gCoeff * d;
            const float b3 = gCoeff * b2;

            const float v3 = x - ic2eq;
            const float v1 = d * ic1eq + b2 * v3;
            const float v2 = ic2eq + b2 * ic1eq + b3 * v3;
            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;
            lastBp = v1;
            return x - kEff * v1 - v2;
        }

        const auto o = process (x);
        lastBp = o.bp;
        return o.hp;
    }

    /** Bell / peaking response with linear gain `gain` at the corner. */
    inline float processBell (float x, float gain) noexcept
    {
        const auto o = process (x);
        return isExactlyUnity (gain) ? x : o.hp + (gain * kCoeff) * o.bp + o.lp;
    }

    //== Shared-coefficient variants ===========================================
    // One coefficient set, many channel states: identical filtering on left and
    // right is what keeps the stereo image intact.

    inline Outputs processShared (float x, const SvfCoefficients& c) noexcept
    {
        const float v3 = x - ic2eq;
        const float v1 = c.a1 * ic1eq + c.a2 * v3;
        const float v2 = ic2eq + c.a2 * ic1eq + c.a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return { v2, v1, x - c.k * v1 - v2 };
    }

    inline float processHighpassShared (float x, const SvfCoefficients& c) noexcept
    {
        const auto o = processShared (x, c);
        lastBp = o.bp;
        return o.hp;
    }

    /** High-pass with level-dependent damping - see processHighpassSaturating. */
    inline float processHighpassSaturatingShared (float x, const SvfCoefficients& c, float amount) noexcept
    {
        if (amount <= 0.0f)
            return processHighpassShared (x, c);

        const float bp2  = clampValue (lastBp * lastBp, 0.0f, 4.0f);
        const float kEff = c.k * (1.0f + amount * bp2);
        const float d    = 1.0f / (1.0f + c.g * (c.g + kEff));
        const float b2   = c.g * d;
        const float b3   = c.g * b2;

        const float v3 = x - ic2eq;
        const float v1 = d * ic1eq + b2 * v3;
        const float v2 = ic2eq + b2 * ic1eq + b3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        lastBp = v1;
        return x - kEff * v1 - v2;
    }

    inline float processBellShared (float x, const SvfCoefficients& c, float gain) noexcept
    {
        const auto o = processShared (x, c);
        return isExactlyUnity (gain) ? x : o.hp + (gain * c.k) * o.bp + o.lp;
    }

    bool isFinite() const noexcept { return std::isfinite (ic1eq) && std::isfinite (ic2eq); }

private:
    float gCoeff = 0.0f, kCoeff = 1.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1eq = 0.0f, ic2eq = 0.0f;
    float lastBp = 0.0f;
};

//==============================================================================
/** First-order DC blocker (TPT high-pass).  The asymmetric saturator adds a
    small DC offset that must not reach the output or the host meters. */
class DcBlocker
{
public:
    void prepare (float sampleRate) noexcept { pole.setG (tptGain (kDcBlockerHz, sampleRate)); reset(); }
    void reset() noexcept { pole.reset(); }
    inline float process (float x) noexcept { return pole.processHighpass (x); }
    bool isFinite() const noexcept { return pole.isFinite(); }

private:
    OnePoleTpt pole;
};

} // namespace bsweep
