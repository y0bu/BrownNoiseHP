/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    Oversampler.h - 2x / 4x polyphase half-band oversampling.

    Why hand-rolled instead of juce::dsp::Oversampling?  The whole DSP core is
    deliberately JUCE-free so it can be unit tested (and reasoned about) on its
    own, and a half-band FIR is about eighty lines.  It also lets us state the
    latency exactly and design the kernels at run time with a known Kaiser
    specification, which Tests/ then verifies (pass-band ripple, stop-band
    rejection, round-trip fidelity).

    Structure: a length-N half-band low-pass has every second tap equal to zero
    apart from the centre tap, which is exactly 0.5.  Splitting it into its two
    polyphase branches therefore gives

        interpolation:  out[2n]   = sum_k 2*g[k]*x[n-k]        (g[k] = h[2k])
                        out[2n+1] = x[n - (N-3)/4]             (pure delay)

        decimation:     y[n]      = sum_k g[k]*v[2(n-k)] + 0.5*v[2(n-(N+1)/4)+1]

    so the cost is K/2 multiplies per sample (K = (N+1)/2) plus a delay line -
    about 12 multiplies per sample per channel for the 47-tap stage.

    Latency: 23 samples at 2x, 30.5 samples at 4x (host sample rate).  The dry
    path inside the engine is delayed by exactly that amount - including the
    half sample - so the dry/wet MIX never comb-filters.
*/

#pragma once

#include <vector>

#include "DspMath.h"

namespace bsweep
{

/** Kaiser-windowed half-band FIR, stored folded (symmetric) for the even
    polyphase branch. */
struct HalfBandKernel
{
    std::vector<float> halfTaps;   // folded even-branch taps, K/2 entries
    int    numEvenTaps  = 0;       // K = (N+1)/2
    int    upDelay      = 0;       // (N-3)/4, in input samples
    int    downDelay    = 0;       // (N+1)/4, in output samples
    int    length       = 0;       // N

    /** N must satisfy N % 4 == 3 so that (N-1)/2 is odd (true half-band). */
    void design (int N, double beta);
};

/** Single 2x interpolation stage. */
class HalfBandUpsampler
{
public:
    void prepare (const HalfBandKernel* k);
    void reset();
    /** in: numSamples, out: 2*numSamples. */
    void process (const float* in, float* out, int numSamples) noexcept;

private:
    const HalfBandKernel* kernel = nullptr;
    std::vector<float> hist;      // circular, power-of-two
    std::vector<float> delayLine; // circular, power-of-two
    int histMask = 0, histPos = 0;
    int delayMask = 0, delayPos = 0;
};

/** Single 2x decimation stage. */
class HalfBandDownsampler
{
public:
    void prepare (const HalfBandKernel* k);
    void reset();
    /** in: 2*numSamples, out: numSamples. */
    void process (const float* in, float* out, int numSamples) noexcept;

private:
    const HalfBandKernel* kernel = nullptr;
    std::vector<float> evenHist;
    std::vector<float> oddHist;
    int evenMask = 0, evenPos = 0;
    int oddMask = 0, oddPos = 0;
};

/** 1x / 2x / 4x oversampler for an arbitrary number of channels. */
class Oversampler
{
public:
    static constexpr int kStage1Length = 47;
    static constexpr int kStage2Length = 31;
    static constexpr double kStage1Beta = 7.86;   // ~ -80 dB stop band
    static constexpr double kStage2Beta = 7.00;   // ~ -70 dB stop band

    /** Allocates for the maximum (4x) factor and then selects `factor`, so
        that switching factor later never allocates on the audio thread. */
    void prepare (int numChannels, int maxBlockSamples, int factor);
    /** Allocation-free factor change (1, 2 or 4).  Clears the filter states. */
    void setFactor (int factor) noexcept;
    void reset();

    int   getFactor() const noexcept { return oversamplingFactor; }
    /** Round-trip latency in host-rate samples (may be fractional at 4x). */
    float getLatencySamples() const noexcept { return latency; }

    /** Upsample `numSamples` host-rate frames; returns the working buffer for
        each channel, containing numSamples * factor samples. */
    float* upsample (const float* const* input, int numChannels, int numSamples) noexcept;

    /** Pointer to channel `ch` of the internal oversampled working area. */
    float* channelData (int ch) noexcept { return work[static_cast<size_t> (ch)].data(); }

    void downsample (float* const* output, int numChannels, int numSamples) noexcept;

private:
    int oversamplingFactor = 1;
    int maxBlock = 0;
    int channels = 0;
    float latency = 0.0f;

    HalfBandKernel kernel1, kernel2;
    std::vector<HalfBandUpsampler>   up1, up2;
    std::vector<HalfBandDownsampler> down1, down2;
    std::vector<std::vector<float>>  work;      // factor * maxBlock
    std::vector<std::vector<float>>  scratch;   // 2 * maxBlock (intermediate at 2x)
};

} // namespace bsweep
