/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    LoudnessSchedule.h - the AUTO gain, and the reason the sweep fades instead
    of thinning.

    The problem
    -----------
    A high-pass sweep does not change loudness the way it changes timbre.  Take
    pink noise and sweep a 24 dB/oct HP from 20 Hz to 1 kHz: you have removed
    more than half the spectrum, the sound is unrecognisably thin, and the
    K-weighted loudness has fallen by about 3 dB.  Keep going and the loudness
    stays almost flat until the cutoff passes 3-4 kHz, at which point it falls
    off a cliff.  That mismatch - huge timbral change, tiny loudness change,
    then a cliff - is exactly what makes automated HP sweeps sound like an
    effect being applied rather than a sound running out of energy.

    The fix
    -------
    Decide what the loudness trajectory *should* be, measure what the filter
    actually does, and make up the difference.

      target(u, character) = -45 dB * character * ((u - 0.08)/0.92)^1.15

    is a smooth, monotonic, near-linear-in-dB ramp (u is the normalised cutoff
    position, so it is linear in log frequency - the only place a perceptually
    linear control can live).

      measured(u, character, resonance, slope)

    is the K-weighted level of the real filter chain on a pink reference,
    obtained by integrating ResponseModel against the pink spectrum and the
    BS.1770 weighting curve.  Because ResponseModel is exact for the TPT filters
    the plugin actually runs, this is a real measurement, not a guess.

      gain = -softplus(measured - target)

    softplus rather than min(0, .) so the curve has no slope discontinuity where
    the natural response crosses the target.  The gain is never positive: AUTO
    only ever takes level away, so it cannot amplify hiss at extreme cutoff
    settings, and CHARACTER = 0 leaves the plugin at unity (target = 0 dB, and
    the measured response is always <= 0 dB), which is what "0% = normal-ish HP
    behaviour" means.

    The table is built once, in the analogue domain, so it is sample-rate
    independent and costs nothing at prepareToPlay time after the first call.
*/

#pragma once

#include "DesignConstants.h"
#include "ResponseModel.h"

namespace bsweep
{

/** Squared magnitude of an analogue approximation of the ITU-R BS.1770
    K-weighting curve (a +4 dB high shelf at 1681.97 Hz over a 38.13 Hz,
    Q = 0.5 high-pass). */
float kWeightingMagnitudeSquared (float freqHz) noexcept;

class LoudnessSchedule
{
public:
    static constexpr int kNumU = 49;   // normalised cutoff positions
    static constexpr int kNumC = 7;    // CHARACTER
    static constexpr int kNumR = 4;    // RESONANCE
    static constexpr int kNumFreq = 192;

    /** Built on first use (a few tens of milliseconds), then shared. */
    static const LoudnessSchedule& instance();

    /** K-weighted level of the linear chain on a pink reference, in dB
        relative to unfiltered pink. */
    float measuredDb (float u, float character01, float resonance01, int slopeIndex) const noexcept;

    /** Wet-path gain, in dB, that puts the measured loudness on the designed
        contour.  Always <= 0. */
    float gainDb (float u, float character01, float resonance01, int slopeIndex) const noexcept;

private:
    LoudnessSchedule();

    float table[kNumSlopes][kNumU][kNumC][kNumR] {};
};

} // namespace bsweep
