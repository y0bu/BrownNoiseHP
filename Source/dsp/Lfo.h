/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    Lfo.h - two identical modulators, free-running or tempo-synced.

    Both LFOs are mono: left and right receive exactly the same modulation.
    That is a deliberate restriction.  Independent per-channel modulation is the
    fastest way to destroy the stereo relationship of a pad or a supersaw, which
    the brief explicitly rules out.

    The LFO is evaluated once per control block (~0.35 ms) rather than per
    sample.  Everything it drives is either a filter coefficient (updated at
    control rate anyway) or a gain, and gains are linearly ramped across the
    block, so nothing steps.
*/

#pragma once

#include "DesignConstants.h"
#include "DspMath.h"

namespace bsweep
{

enum class LfoShape
{
    sine = 0, triangle, sawUp, sawDown, square, sampleHold, smoothRandom, numShapes
};

enum class LfoDestination
{
    off = 0, cutoff, resonance, character, saturation,
    treble, midLevel, bassLevel, amplitude, mix, numDestinations
};

constexpr int kNumLfoShapes       = static_cast<int> (LfoShape::numShapes);
constexpr int kNumLfoDestinations = static_cast<int> (LfoDestination::numDestinations);

/** Tempo-sync divisions, expressed as a period in quarter notes. */
struct SyncDivision { const char* name; double beats; };

constexpr SyncDivision kSyncDivisions[] =
{
    { "8 bars", 32.0 },      { "4 bars", 16.0 },      { "2 bars", 8.0 },
    { "1 bar",  4.0  },      { "1/2",    2.0  },      { "1/2T",   4.0 / 3.0 },
    { "1/4.",   1.5  },      { "1/4",    1.0  },      { "1/4T",   2.0 / 3.0 },
    { "1/8.",   0.75 },      { "1/8",    0.5  },      { "1/8T",   1.0 / 3.0 },
    { "1/16",   0.25 },      { "1/16T",  1.0 / 6.0 }, { "1/32",   0.125 }
};

constexpr int kNumSyncDivisions = static_cast<int> (sizeof (kSyncDivisions) / sizeof (SyncDivision));
constexpr int kDefaultSyncDivision = 3;   // 1 bar

struct LfoParameters
{
    int   destination = static_cast<int> (LfoDestination::off);
    int   shape       = static_cast<int> (LfoShape::sine);
    float rateHz      = 0.5f;
    bool  sync        = false;
    int   division    = kDefaultSyncDivision;
    float depth       = 0.0f;      // 0..1
    float phaseOffset = 0.0f;      // 0..1
};

class Lfo
{
public:
    void prepare (double hostSampleRate, uint32_t seed) noexcept
    {
        sampleRate = hostSampleRate > 0.0 ? hostSampleRate : 48000.0;
        rng.seed (seed);
        reset();
    }

    void reset() noexcept
    {
        phase = 0.0;
        heldValue = rng.nextBipolar();
        nextValue = rng.nextBipolar();
        value = 0.0f;
    }

    /** Advance by `numSamples` host-rate samples and return the new bipolar
        value.  Returns 0 when the LFO is switched off, so callers never need to
        branch. */
    float advance (const LfoParameters& p, double bpm, int numSamples) noexcept
    {
        if (p.destination == static_cast<int> (LfoDestination::off) || p.depth <= 0.0f)
        {
            value = 0.0f;
            return 0.0f;
        }

        double hz = p.rateHz;
        if (p.sync)
        {
            const double beats = kSyncDivisions[clampValue (p.division, 0, kNumSyncDivisions - 1)].beats;
            const double safeBpm = (bpm > 1.0 && bpm < 1000.0) ? bpm : 120.0;
            hz = safeBpm / (60.0 * beats);
        }

        hz = clampValue (hz, 0.001, 40.0);

        const double increment = hz * static_cast<double> (numSamples) / sampleRate;
        phase += increment;
        while (phase >= 1.0)
        {
            phase -= 1.0;
            heldValue = nextValue;
            nextValue = rng.nextBipolar();
        }

        const double p01 = phase + static_cast<double> (p.phaseOffset);
        const float  ph  = static_cast<float> (p01 - std::floor (p01));

        value = evaluate (static_cast<LfoShape> (clampValue (p.shape, 0, kNumLfoShapes - 1)), ph);
        return value;
    }

    float currentValue() const noexcept { return value; }

    /** Shape evaluation, exposed for the unit tests. */
    float evaluate (LfoShape shape, float ph) const noexcept
    {
        switch (shape)
        {
            case LfoShape::sine:     return std::sin (2.0f * static_cast<float> (kPi) * ph);
            case LfoShape::triangle: return ph < 0.25f ? 4.0f * ph
                                          : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f);
            case LfoShape::sawUp:    return 2.0f * ph - 1.0f;
            case LfoShape::sawDown:  return 1.0f - 2.0f * ph;
            // Soft square: the edges are band-limited enough not to click when
            // they hit a filter coefficient.
            case LfoShape::square:   return fastTanh (6.0f * std::sin (2.0f * static_cast<float> (kPi) * ph));
            case LfoShape::sampleHold:   return heldValue;
            case LfoShape::smoothRandom: return lerp (heldValue, nextValue, smootherStep (ph));
            case LfoShape::numShapes:    break;
        }
        return 0.0f;
    }

    /** Signed modulation offset, already scaled by depth, for a destination. */
    static float modulationAmount (LfoDestination destination, float lfoValue, float depth) noexcept
    {
        const float d = clampValue (depth, 0.0f, 1.0f);
        switch (destination)
        {
            case LfoDestination::cutoff:     return lfoValue * d * kModCutoffOctaves;
            case LfoDestination::resonance:  return lfoValue * d * kModResonance;
            case LfoDestination::character:  return lfoValue * d * kModCharacter;
            case LfoDestination::saturation: return lfoValue * d * kModSaturation;
            case LfoDestination::treble:     return lfoValue * d * kModTrebleDb;
            case LfoDestination::midLevel:   return lfoValue * d * kModMidDb;
            case LfoDestination::bassLevel:  return lfoValue * d * kModBassDb;
            case LfoDestination::mix:        return lfoValue * d * kModMix;
            // Tremolo is unipolar-downwards: 0 dB at the top of the cycle so it
            // can never make the plugin louder than it was.
            case LfoDestination::amplitude:  return -0.5f * (1.0f - lfoValue) * d * kModAmplitudeDb;
            case LfoDestination::off:
            case LfoDestination::numDestinations: break;
        }
        return 0.0f;
    }

private:
    double sampleRate = 48000.0;
    double phase = 0.0;
    float  value = 0.0f;
    float  heldValue = 0.0f, nextValue = 0.0f;
    Xorshift32 rng;
};

} // namespace bsweep
