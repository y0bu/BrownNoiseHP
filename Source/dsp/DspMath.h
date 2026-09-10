/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    DspMath.h - small, dependency-free math helpers shared by the DSP core.

    The whole of Source/dsp is deliberately free of any JUCE dependency so that
    the signal processing can be compiled and unit-tested on its own (see
    Tests/).  Only the plugin wrapper (PluginProcessor / PluginEditor) knows
    about JUCE.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bsweep
{

constexpr double kPi = 3.14159265358979323846;

template <typename T>
inline T clampValue (T v, T lo, T hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float dbToGain (float db) noexcept          { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  noexcept          { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }
inline double dbToGain (double db) noexcept        { return std::pow (10.0, db * 0.05); }
inline double gainToDb (double g)  noexcept        { return 20.0 * std::log10 (std::max (g, 1.0e-30)); }

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

/** Exact "is this gain exactly 1.0?" test.

    Several stages rely on a *bit-exact* bypass when their gain is unity - a
    faded-out tilt section must return its input unchanged, not
    input-plus-rounding-error - so the comparison really is meant to be exact.
    Written as two inequalities so that it does not trip -Wfloat-equal, which is
    otherwise a warning worth keeping on. */
inline bool isExactlyUnity (float gain) noexcept
{
    return ! (gain < 1.0f) && ! (gain > 1.0f);
}

/** Catmull-Rom interpolation between p1 and p2 (t in [0,1]).

    Used for the cutoff axis of the loudness table.  Plain linear interpolation
    leaves a slope discontinuity at every table node, which during a slow sweep
    is audible as the fade rate visibly changing gear; Catmull-Rom is C1, so it
    does not. */
inline float catmullRom (float p0, float p1, float p2, float p3, float t) noexcept
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1)
                   + (-p0 + p2) * t
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                   + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

/** Classic Hermite smoothstep on [0,1]. */
inline float smoothStep (float x) noexcept
{
    x = clampValue (x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

/** 5th-order smootherstep - zero 1st and 2nd derivative at both ends. */
inline float smootherStep (float x) noexcept
{
    x = clampValue (x, 0.0f, 1.0f);
    return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

/** Maps x from [a,b] to [0,1] with clamping. */
inline float normaliseRange (float x, float a, float b) noexcept
{
    return clampValue ((x - a) / (b - a), 0.0f, 1.0f);
}

/** C1-continuous one-sided hinge: exactly max(x, 0) away from the origin, with
    a quadratic blend of width `knee` on the positive side.

    Used by the loudness scheduler for the "never boost" clamp.  A plain
    max(0, x) would put a slope discontinuity in the gain trajectory exactly
    where the natural response crosses the target contour, which is audible as a
    kink while sweeping.  A softplus would smooth that but is *strictly
    positive* at x = 0 (softplus(0) = knee*ln2), which would silently attenuate
    the plugin even at unity settings.  This hinge is 0 for x <= 0, so unity
    stays unity.

        f(x) = 0                for x <= 0
             = x^2 / (2*knee)   for 0 < x < knee
             = x - knee/2       for x >= knee
*/
inline float smoothHinge (float x, float knee) noexcept
{
    if (x <= 0.0f)   return 0.0f;
    if (knee <= 0.0f) return x;
    if (x >= knee)   return x - 0.5f * knee;
    return x * x / (2.0f * knee);
}

/** tanh approximation (Pade 3/2) clamped to its region of validity.

    Accurate to < 0.3% for |x| <= 3, exactly saturating at +/-1 outside.  Roughly
    6x cheaper than std::tanh, which matters because the saturator runs inside
    the oversampled region.
*/
inline float fastTanh (float x) noexcept
{
    if (x > 3.0f)  return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Prewarped TPT integrator gain: g = tan(pi * f / fs). */
inline float tptGain (float freqHz, float sampleRate) noexcept
{
    const float maxF = 0.4995f * sampleRate;
    const float f    = clampValue (freqHz, 0.05f, maxF);
    return std::tan (static_cast<float> (kPi) * f / sampleRate);
}

/** One-pole smoother coefficient for a given time constant, advanced n samples. */
inline float smootherCoefficient (float timeConstantSeconds, float sampleRate, int numSamples) noexcept
{
    if (timeConstantSeconds <= 0.0f) return 1.0f;
    const float k = static_cast<float> (numSamples) / (timeConstantSeconds * sampleRate);
    return 1.0f - std::exp (-k);
}

/** Deterministic 32-bit PRNG - used by the sample & hold / random LFO shapes and
    by the test-signal generators, so results are reproducible across platforms. */
class Xorshift32
{
public:
    explicit Xorshift32 (uint32_t seedValue = 0x9E3779B9u) : state (seedValue ? seedValue : 1u) {}

    uint32_t nextUint() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept
    {
        return static_cast<float> (static_cast<int32_t> (nextUint())) * 4.656612873e-10f;
    }

    /** Uniform in [0, 1). */
    float nextUnipolar() noexcept
    {
        return static_cast<float> (nextUint() >> 8) * (1.0f / 16777216.0f);
    }

    void seed (uint32_t s) noexcept { state = s ? s : 1u; }

private:
    uint32_t state;
};

} // namespace bsweep
