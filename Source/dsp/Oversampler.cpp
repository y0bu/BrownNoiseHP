#include "Oversampler.h"

#include <cassert>

namespace bsweep
{

namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double halfX = 0.5 * x;
        for (int k = 1; k < 60; ++k)
        {
            term *= (halfX / k) * (halfX / k);
            sum  += term;
            if (term < 1.0e-16 * sum) break;
        }
        return sum;
    }

    int nextPowerOfTwo (int n)
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }
}

//==============================================================================
void HalfBandKernel::design (int N, double beta)
{
    assert (N % 4 == 3 && "half-band length must satisfy N % 4 == 3");

    length = N;
    const int c = (N - 1) / 2;

    std::vector<double> h (static_cast<size_t> (N), 0.0);
    const double i0beta = besselI0 (beta);

    for (int n = 0; n < N; ++n)
    {
        const double m = static_cast<double> (n - c);

        // Ideal half-band low-pass: h[n] = 0.5 * sinc(0.5 * m).
        double ideal;
        if (m == 0.0)
            ideal = 0.5;
        else
            ideal = 0.5 * std::sin (kPi * 0.5 * m) / (kPi * 0.5 * m);

        const double r = (2.0 * n) / (N - 1) - 1.0;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0beta;

        h[static_cast<size_t> (n)] = ideal * w;
    }

    // Force the exact half-band zeros (they are ~1e-17 already) and normalise
    // the DC gain to 1.
    for (int n = 0; n < N; ++n)
        if (n != c && ((n - c) % 2) == 0)
            h[static_cast<size_t> (n)] = 0.0;

    double dc = 0.0;
    for (double v : h) dc += v;
    for (double& v : h) v /= dc;

    numEvenTaps = (N + 1) / 2;                  // taps at even indices 0,2,...,N-1
    upDelay     = (N - 3) / 4;
    downDelay   = (N + 1) / 4;

    // Fold: g[k] == g[K-1-k].
    halfTaps.assign (static_cast<size_t> (numEvenTaps / 2), 0.0f);
    for (int k = 0; k < numEvenTaps / 2; ++k)
        halfTaps[static_cast<size_t> (k)] = static_cast<float> (h[static_cast<size_t> (2 * k)]);
}

//==============================================================================
void HalfBandUpsampler::prepare (const HalfBandKernel* k)
{
    kernel = k;
    const int histSize = nextPowerOfTwo (k->numEvenTaps + 4);
    hist.assign (static_cast<size_t> (histSize), 0.0f);
    histMask = histSize - 1;

    const int delaySize = nextPowerOfTwo (k->upDelay + 4);
    delayLine.assign (static_cast<size_t> (delaySize), 0.0f);
    delayMask = delaySize - 1;

    reset();
}

void HalfBandUpsampler::reset()
{
    std::fill (hist.begin(), hist.end(), 0.0f);
    std::fill (delayLine.begin(), delayLine.end(), 0.0f);
    histPos = delayPos = 0;
}

void HalfBandUpsampler::process (const float* in, float* out, int numSamples) noexcept
{
    const int   K     = kernel->numEvenTaps;
    const int   half  = K / 2;
    const float* g    = kernel->halfTaps.data();
    const int   dly   = kernel->upDelay;

    for (int n = 0; n < numSamples; ++n)
    {
        const float x = in[n];

        hist[static_cast<size_t> (histPos)] = x;

        float acc = 0.0f;
        for (int k = 0; k < half; ++k)
        {
            const float a = hist[static_cast<size_t> ((histPos - k) & histMask)];
            const float b = hist[static_cast<size_t> ((histPos - (K - 1 - k)) & histMask)];
            acc += g[k] * (a + b);
        }

        delayLine[static_cast<size_t> (delayPos)] = x;
        const float delayed = delayLine[static_cast<size_t> ((delayPos - dly) & delayMask)];

        out[2 * n]     = 2.0f * acc;
        out[2 * n + 1] = delayed;

        histPos  = (histPos + 1) & histMask;
        delayPos = (delayPos + 1) & delayMask;
    }
}

//==============================================================================
void HalfBandDownsampler::prepare (const HalfBandKernel* k)
{
    kernel = k;
    const int evenSize = nextPowerOfTwo (k->numEvenTaps + 4);
    evenHist.assign (static_cast<size_t> (evenSize), 0.0f);
    evenMask = evenSize - 1;

    const int oddSize = nextPowerOfTwo (k->downDelay + 4);
    oddHist.assign (static_cast<size_t> (oddSize), 0.0f);
    oddMask = oddSize - 1;

    reset();
}

void HalfBandDownsampler::reset()
{
    std::fill (evenHist.begin(), evenHist.end(), 0.0f);
    std::fill (oddHist.begin(), oddHist.end(), 0.0f);
    evenPos = oddPos = 0;
}

void HalfBandDownsampler::process (const float* in, float* out, int numSamples) noexcept
{
    const int    K    = kernel->numEvenTaps;
    const int    half = K / 2;
    const float* g    = kernel->halfTaps.data();
    const int    dly  = kernel->downDelay;

    for (int n = 0; n < numSamples; ++n)
    {
        evenHist[static_cast<size_t> (evenPos)] = in[2 * n];
        oddHist[static_cast<size_t> (oddPos)]   = in[2 * n + 1];

        float acc = 0.0f;
        for (int k = 0; k < half; ++k)
        {
            const float a = evenHist[static_cast<size_t> ((evenPos - k) & evenMask)];
            const float b = evenHist[static_cast<size_t> ((evenPos - (K - 1 - k)) & evenMask)];
            acc += g[k] * (a + b);
        }

        out[n] = acc + 0.5f * oddHist[static_cast<size_t> ((oddPos - dly) & oddMask)];

        evenPos = (evenPos + 1) & evenMask;
        oddPos  = (oddPos + 1) & oddMask;
    }
}

//==============================================================================
void Oversampler::prepare (int numChannels, int maxBlockSamples, int factor)
{
    channels = std::max (1, numChannels);
    maxBlock = std::max (1, maxBlockSamples);

    kernel1.design (kStage1Length, kStage1Beta);
    kernel2.design (kStage2Length, kStage2Beta);

    up1.resize   (static_cast<size_t> (channels));
    up2.resize   (static_cast<size_t> (channels));
    down1.resize (static_cast<size_t> (channels));
    down2.resize (static_cast<size_t> (channels));
    work.resize  (static_cast<size_t> (channels));
    scratch.resize (static_cast<size_t> (channels));

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<size_t> (ch);
        up1[c].prepare   (&kernel1);
        down1[c].prepare (&kernel1);
        up2[c].prepare   (&kernel2);
        down2[c].prepare (&kernel2);
        work[c].assign   (static_cast<size_t> (maxBlock * 4 + 8), 0.0f);
        scratch[c].assign (static_cast<size_t> (maxBlock * 2 + 8), 0.0f);
    }

    setFactor (factor);
}

void Oversampler::setFactor (int factor) noexcept
{
    oversamplingFactor = (factor == 4 ? 4 : (factor == 2 ? 2 : 1));

    // Half-band FIR of length N delays by (N-1)/2 samples at its own rate.
    // Round trip 2x: 2 * 23 samples at 2x = 23 host samples.
    // Round trip 4x: that, plus 2 * 15 samples at 4x = 7.5 host samples.
    const float d1 = 0.5f * static_cast<float> (kStage1Length - 1);   // at 2x rate
    const float d2 = 0.5f * static_cast<float> (kStage2Length - 1);   // at 4x rate

    if (oversamplingFactor == 1)      latency = 0.0f;
    else if (oversamplingFactor == 2) latency = (2.0f * d1) / 2.0f;
    else                              latency = (2.0f * d1) / 2.0f + (2.0f * d2) / 4.0f;

    reset();
}

void Oversampler::reset()
{
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<size_t> (ch);
        up1[c].reset(); up2[c].reset(); down1[c].reset(); down2[c].reset();
        std::fill (work[c].begin(), work[c].end(), 0.0f);
        std::fill (scratch[c].begin(), scratch[c].end(), 0.0f);
    }
}

float* Oversampler::upsample (const float* const* input, int numChannels, int numSamples) noexcept
{
    const int n = std::min (numChannels, channels);

    for (int ch = 0; ch < n; ++ch)
    {
        const auto c = static_cast<size_t> (ch);

        if (oversamplingFactor == 1)
        {
            std::copy (input[ch], input[ch] + numSamples, work[c].begin());
        }
        else if (oversamplingFactor == 2)
        {
            up1[c].process (input[ch], work[c].data(), numSamples);
        }
        else
        {
            up1[c].process (input[ch], scratch[c].data(), numSamples);
            up2[c].process (scratch[c].data(), work[c].data(), 2 * numSamples);
        }
    }

    return work.empty() ? nullptr : work[0].data();
}

void Oversampler::downsample (float* const* output, int numChannels, int numSamples) noexcept
{
    const int n = std::min (numChannels, channels);

    for (int ch = 0; ch < n; ++ch)
    {
        const auto c = static_cast<size_t> (ch);

        if (oversamplingFactor == 1)
        {
            std::copy (work[c].begin(), work[c].begin() + numSamples, output[ch]);
        }
        else if (oversamplingFactor == 2)
        {
            down1[c].process (work[c].data(), output[ch], numSamples);
        }
        else
        {
            down2[c].process (work[c].data(), scratch[c].data(), 2 * numSamples);
            down1[c].process (scratch[c].data(), output[ch], numSamples);
        }
    }
}

} // namespace bsweep
