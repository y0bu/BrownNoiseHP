/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    AnalogStage.h - the "not sterile" stage.

    Three small things, none of which is allowed to sound like distortion:

      1. An asymmetric soft saturator.  The transfer curve is
             y = norm * (tanh(d*x + b) - tanh(b)) / d
         which degenerates to y = x *exactly* as d -> 0, so ANALOG = 0% is a
         true bypass rather than "a little bit of tanh".  The bias b generates
         second-harmonic content (the "valve" flavour); dividing by d and by
         (1 - tanh^2 b) keeps the small-signal gain at unity, so the stage only
         does something when the signal is actually loud.

      2. A level-dependent damping term inside the resonant filter section
         (implemented in HighPassStage/SvfTpt).  This is what makes the
         resonance breathe.

      3. A first-order high shelf that removes up to 4.5 dB above 9 kHz.  This
         is the "carefully controlled high-frequency attenuation": it stops the
         residue at high cutoff settings from sounding clinical, and it also
         suppresses whatever aliasing survives the oversampler.  At ANALOG = 0
         the shelf gain is exactly 1.0, i.e. an algebraic bypass.

    A DC blocker follows, because the asymmetry deliberately introduces an
    offset.
*/

#pragma once

#include "DesignConstants.h"
#include "Filters.h"

namespace bsweep
{

struct AnalogCoefficients
{
    float drive      = 0.0f;    // 0 means "perfectly linear"
    float bias       = 0.0f;
    float biasOffset = 0.0f;    // tanh(bias)
    float norm       = 1.0f;    // restores unity small-signal gain
    float hfBigG     = 0.5f;
    float hfGain     = 1.0f;
    bool  linear     = true;

    void update (float analog01, float sampleRate) noexcept
    {
        const float a = clampValue (analog01, 0.0f, 1.0f);

        drive  = a * kAnalogMaxDrive;
        bias   = a * kAnalogMaxBias;
        linear = (drive < 1.0e-5f);

        // The offset has to be computed with the *same* function the shaper
        // uses, otherwise analogTransfer(0) is a few tens of microvolts instead
        // of zero and the stage emits a DC step every time ANALOG moves.
        biasOffset = fastTanh (bias);
        const float tb = std::tanh (bias);
        norm       = 1.0f / std::max (1.0f - tb * tb, 1.0e-6f);

        const float g = tptGain (kAnalogHfShelfHz, sampleRate);
        hfBigG = g / (1.0f + g);
        hfGain = dbToGain (kAnalogHfShelfMaxDb * a);
    }
};

/** The static transfer curve, exposed so tests can check monotonicity,
    boundedness and the y(0) = 0 / y'(0) = 1 conditions directly. */
inline float analogTransfer (float x, const AnalogCoefficients& c) noexcept
{
    if (c.linear) return x;
    return c.norm * (fastTanh (c.drive * x + c.bias) - c.biasOffset) / c.drive;
}

class AnalogStageState
{
public:
    void prepare (float sampleRate) noexcept { dc.prepare (sampleRate); reset(); }

    void reset() noexcept { hfShelf.reset(); dc.reset(); }

    inline float process (const AnalogCoefficients& c, float x) noexcept
    {
        x = analogTransfer (x, c);
        x = hfShelf.processHighShelfShared (x, c.hfBigG, c.hfGain);
        return dc.process (x);
    }

    bool isFinite() const noexcept { return hfShelf.isFinite() && dc.isFinite(); }

private:
    OnePoleTpt hfShelf;
    DcBlocker  dc;
};

} // namespace bsweep
