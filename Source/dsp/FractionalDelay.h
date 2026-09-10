/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    FractionalDelay.h - 3rd-order Lagrange delay for the dry path.

    The oversampler's round-trip latency is 23 samples at 2x but 30.5 samples at
    4x.  Rounding that half sample away would leave the dry and wet paths
    misaligned, and at MIX settings between 1% and 99% with a low cutoff (where
    wet and dry are nearly identical above the corner) the result is audible
    comb filtering in the top octave.  So the dry path is delayed by the exact
    fractional amount instead.
*/

#pragma once

#include <vector>

#include "DspMath.h"

namespace bsweep
{

class FractionalDelay
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 4;
        while (size < maxDelaySamples + 8) size <<= 1;
        buffer.assign (static_cast<size_t> (size), 0.0f);
        mask = size - 1;
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    void setDelay (float delaySamples) noexcept
    {
        delay = std::max (0.0f, delaySamples);
        bypass = (delay < 1.0e-4f);

        if (bypass) return;

        // Lagrange needs one sample either side, so anchor at floor(delay) and
        // interpolate at t = 1 + frac across taps [d-1, d, d+1, d+2].
        intDelay = std::max (1, static_cast<int> (std::floor (delay)));
        const float frac = delay - static_cast<float> (intDelay);
        const float t = 1.0f + frac;

        for (int i = 0; i < 4; ++i)
        {
            float h = 1.0f;
            for (int j = 0; j < 4; ++j)
                if (j != i)
                    h *= (t - static_cast<float> (j)) / static_cast<float> (i - j);
            coeff[i] = h;
        }
    }

    inline float process (float x) noexcept
    {
        buffer[static_cast<size_t> (writePos)] = x;

        float y = x;
        if (! bypass)
        {
            y = 0.0f;
            for (int i = 0; i < 4; ++i)
            {
                const int idx = (writePos - (intDelay - 1 + i)) & mask;
                y += coeff[i] * buffer[static_cast<size_t> (idx)];
            }
        }

        writePos = (writePos + 1) & mask;
        return y;
    }

    float getDelay() const noexcept { return delay; }

private:
    std::vector<float> buffer;
    int   mask = 0, writePos = 0, intDelay = 1;
    float delay = 0.0f;
    float coeff[4] { 0.0f, 1.0f, 0.0f, 0.0f };
    bool  bypass = true;
};

} // namespace bsweep
