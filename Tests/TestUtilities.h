/*
    BrownSweep - shared helpers for the test suite: signal generators, a small
    radix-2 FFT, and measurement utilities.
*/

#pragma once

#include <algorithm>
#include <complex>
#include <vector>

#include "../Source/dsp/BrownSweepEngine.h"
#include "TestFramework.h"

namespace bstest
{

using bsweep::Xorshift32;

//==============================================================================
// Signal generation
//==============================================================================

inline std::vector<float> silence (int n) { return std::vector<float> (static_cast<size_t> (n), 0.0f); }

inline std::vector<float> impulse (int n, float amplitude = 1.0f, int position = 0)
{
    std::vector<float> v (static_cast<size_t> (n), 0.0f);
    if (position >= 0 && position < n) v[static_cast<size_t> (position)] = amplitude;
    return v;
}

inline std::vector<float> dc (int n, float level = 1.0f)
{
    return std::vector<float> (static_cast<size_t> (n), level);
}

inline std::vector<float> sine (int n, double freqHz, double sampleRate, float amplitude = 0.5f)
{
    std::vector<float> v (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t> (i)] =
            amplitude * static_cast<float> (std::sin (2.0 * bsweep::kPi * freqHz * i / sampleRate));
    return v;
}

inline std::vector<float> whiteNoise (int n, uint32_t seed = 12345u, float amplitude = 0.35f)
{
    Xorshift32 rng (seed);
    std::vector<float> v (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t> (i)] = amplitude * rng.nextBipolar();
    return v;
}

/** Voss-McCartney style pink noise (-3 dB/octave). */
inline std::vector<float> pinkNoise (int n, uint32_t seed = 424242u, float amplitude = 0.3f)
{
    Xorshift32 rng (seed);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    std::vector<float> v (static_cast<size_t> (n));

    for (int i = 0; i < n; ++i)
    {
        const double w = rng.nextBipolar();
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const double out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        v[static_cast<size_t> (i)] = static_cast<float> (out * 0.11) * (amplitude / 0.3f);
    }
    return v;
}

/** Brown / red noise (-6 dB/octave): integrated white noise with a leak so it
    cannot wander off to DC. */
inline std::vector<float> brownNoise (int n, uint32_t seed = 777u, float amplitude = 0.3f)
{
    Xorshift32 rng (seed);
    double state = 0.0;
    std::vector<float> v (static_cast<size_t> (n));
    double peak = 1.0e-9;

    for (int i = 0; i < n; ++i)
    {
        state = 0.998 * state + 0.02 * rng.nextBipolar();
        v[static_cast<size_t> (i)] = static_cast<float> (state);
        peak = std::max (peak, std::fabs (state));
    }
    for (auto& s : v) s = static_cast<float> (s / peak) * amplitude;
    return v;
}

//==============================================================================
// FFT (iterative radix-2, in place)
//==============================================================================

inline void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    if (n < 2) return;

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * bsweep::kPi / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));

        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

//==============================================================================
// Measurement
//==============================================================================

inline bool allFinite (const std::vector<float>& v)
{
    for (float s : v) if (! std::isfinite (s)) return false;
    return true;
}

inline float peak (const std::vector<float>& v)
{
    float p = 0.0f;
    for (float s : v) p = std::max (p, std::fabs (s));
    return p;
}

inline double rms (const std::vector<float>& v, int from = 0, int to = -1)
{
    if (to < 0) to = static_cast<int> (v.size());
    double sum = 0.0;
    int count = 0;
    for (int i = from; i < to; ++i) { sum += static_cast<double> (v[static_cast<size_t> (i)]) * v[static_cast<size_t> (i)]; ++count; }
    return count > 0 ? std::sqrt (sum / count) : 0.0;
}

inline double rmsDb (const std::vector<float>& v, int from = 0, int to = -1)
{
    return 20.0 * std::log10 (std::max (rms (v, from, to), 1.0e-30));
}

/** Energy of `v` between lo and hi Hz, via a Hann-windowed FFT. */
inline double bandEnergy (const std::vector<float>& v, double sampleRate, double loHz, double hiHz)
{
    size_t n = 1;
    while (n * 2 <= v.size()) n *= 2;
    if (n < 64) return 0.0;

    std::vector<std::complex<double>> spec (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * bsweep::kPi * static_cast<double> (i) / static_cast<double> (n));
        spec[i] = std::complex<double> (static_cast<double> (v[i]) * w, 0.0);
    }
    fft (spec);

    double e = 0.0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double f = static_cast<double> (k) * sampleRate / static_cast<double> (n);
        if (f >= loHz && f < hiHz) e += std::norm (spec[k]);
    }
    return e;
}

//==============================================================================
// Engine driving helpers
//==============================================================================

/** Runs the engine over a signal in blocks, returning the output.  `numChannels`
    copies of the input are used unless `right` is supplied. */
inline std::vector<std::vector<float>> runEngine (bsweep::BrownSweepEngine& engine,
                                                  const std::vector<std::vector<float>>& input,
                                                  int blockSize = 64)
{
    auto output = input;
    const int numChannels = static_cast<int> (output.size());
    const int numSamples  = numChannels > 0 ? static_cast<int> (output[0].size()) : 0;

    std::vector<float*> ptrs (static_cast<size_t> (numChannels));

    for (int pos = 0; pos < numSamples; pos += blockSize)
    {
        const int n = std::min (blockSize, numSamples - pos);
        for (int ch = 0; ch < numChannels; ++ch)
            ptrs[static_cast<size_t> (ch)] = output[static_cast<size_t> (ch)].data() + pos;

        engine.process (ptrs.data(), numChannels, n);
    }

    return output;
}

inline std::vector<std::vector<float>> duplicate (const std::vector<float>& mono, int channels)
{
    return std::vector<std::vector<float>> (static_cast<size_t> (channels), mono);
}

/** Steady-state impulse response of the engine, after letting the smoothers
    settle on silence. */
inline std::vector<float> impulseResponse (bsweep::BrownSweepEngine& engine,
                                           const bsweep::EngineParameters& params,
                                           int length, int settleSamples = 16384)
{
    engine.setParameters (params);

    auto settle = duplicate (silence (settleSamples), 1);
    runEngine (engine, settle);

    auto in = duplicate (impulse (length), 1);
    auto out = runEngine (engine, in);
    return out[0];
}

} // namespace bstest
