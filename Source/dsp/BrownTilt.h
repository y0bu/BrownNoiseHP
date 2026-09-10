/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    BrownTilt.h - the brown-noise-inspired spectral tilt.

    WHAT IT IS
    ----------
    A cascade of first-order high-shelf sections whose corner frequencies are
    staggered one octave apart, each cutting the same number of dB.  N shelves
    spaced one octave apart, each dropping D dB, sum to a near-constant -D
    dB/octave downward slope across the region they span - a cheap, stable,
    minimum-phase approximation of a fractional-order 1/f^(D/6) filter.  At
    D = 6 dB/octave the amplitude slope is exactly the brown-noise weighting
    (1/f^2 in power).

    WHY IT IS ANCHORED TO THE CUTOFF
    --------------------------------
    A fixed-pivot tilt would be the naive reading of "make it brown", and it is
    wrong for this problem.  Once the high-pass has removed the low end, that
    energy is gone; no amount of tilting brings it back.  What actually causes
    an HP sweep to sound aggressive is that the *surviving* spectrum keeps its
    full top end, so the spectral centre of gravity races upward while the level
    barely moves.

    So the tilt is anchored kTiltAnchorOctaves above the current cutoff and
    slides with it.  The octave just above the cutoff - the part that carries
    the "note" of the sweep, plus any resonant peak - is left intact, and
    everything above it is progressively pulled down.  The centroid therefore
    tracks the cutoff instead of running away, the surviving band gets narrower
    as the sweep rises, and the whole thing reads as "the sound is running out
    of energy" rather than "the bass is being deleted".

    WHY A SHELF CASCADE RATHER THAN AN FFT
    --------------------------------------
    An FFT tilt would be exact, but it costs a window of latency, smears
    transients, and (with a moving anchor) needs its curve rebuilt every block.
    The shelf cascade is ~10 one-pole sections per channel, zero latency,
    minimum phase, and modulates continuously - which matters far more here,
    because the anchor moves whenever the cutoff moves.

    Deviation from the ideal constant slope, measured over the 2-8 octave window
    above the anchor (Tools/SweepAnalysis --tilt, asserted in Tests/):

        1 dB/oct  0.06 dB p-p      3 dB/oct  0.24 dB p-p
        4.5       0.43             6         0.70

    i.e. inaudible everywhere except at the very top of the CHARACTER range,
    where 0.7 dB spread over an octave is still far below a shelf you could
    point at.
*/

#pragma once

#include "DesignConstants.h"
#include "Filters.h"

namespace bsweep
{

/** Coefficients for the tilt cascade.  Shared by every channel: the stereo
    image cannot be disturbed if both channels run identical filters. */
struct BrownTiltCoefficients
{
    float bigG[kTiltSections] {};
    float m[kTiltSections] {};
    float achievedSlopeDbPerOctave = 0.0f;

    void update (float cutoffHz, float slopeDbPerOctave, float sampleRate) noexcept
    {
        achievedSlopeDbPerOctave = slopeDbPerOctave;

        const float anchorHz  = cutoffHz * std::exp2 (kTiltAnchorOctaves);
        const float shelfGain = dbToGain (-slopeDbPerOctave);

        for (int k = 0; k < kTiltSections; ++k)
        {
            const float fk = anchorHz * std::exp2 (static_cast<float> (k));

            // Fade the section to an exact bypass as its corner approaches
            // Nyquist.  m == 1 makes processHighShelfShared() return the input
            // verbatim whatever the state holds, so this can never click.
            const float fn    = fk / sampleRate;
            const float fade  = 1.0f - smoothStep ((fn - kTiltFadeStartNormalisedFreq)
                                                   / (kTiltFadeEndNormalisedFreq - kTiltFadeStartNormalisedFreq));

            const float fClamped = std::min (fk, 0.47f * sampleRate * 0.999f);
            const float g = tptGain (fClamped, sampleRate);

            bigG[k] = g / (1.0f + g);
            m[k]    = 1.0f + (shelfGain - 1.0f) * fade;
        }
    }

    void setBypass() noexcept
    {
        achievedSlopeDbPerOctave = 0.0f;
        for (int k = 0; k < kTiltSections; ++k) { bigG[k] = 0.5f; m[k] = 1.0f; }
    }
};

/** Per-channel state for the tilt cascade. */
class BrownTiltState
{
public:
    void reset() noexcept
    {
        for (auto& s : sections) s.reset();
    }

    inline float process (const BrownTiltCoefficients& c, float x) noexcept
    {
        for (int k = 0; k < kTiltSections; ++k)
            x = sections[k].processHighShelfShared (x, c.bigG[k], c.m[k]);
        return x;
    }

    bool isFinite() const noexcept
    {
        for (const auto& s : sections) if (! s.isFinite()) return false;
        return true;
    }

private:
    OnePoleTpt sections[kTiltSections];
};

} // namespace bsweep
