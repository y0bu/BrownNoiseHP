/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    DesignConstants.h - every tuned number in the DSP lives here.

    These values were not guessed.  Tools/SweepAnalysis.cpp measures the K-
    weighted loudness and the log-frequency spectral centroid of the full signal
    chain across the entire CUTOFF range, for pink / brown / white noise and for
    a synthetic saw-with-rolloff "psytrance lead" reference, and prints the
    trajectories.  The constants below are the ones that produced a monotonic,
    close-to-linear-in-dB loudness ramp with a bounded centroid rise (see
    README, "Measured behaviour").
*/

#pragma once

#include "DspMath.h"

namespace bsweep
{

//==============================================================================
// Control range
//==============================================================================

constexpr float kMinCutoffHz = 20.0f;
constexpr float kMaxCutoffHz = 20000.0f;

/** log2(20000/20) - the number of octaves the CUTOFF control spans. */
constexpr float kCutoffOctaves = 9.965784284662087f;

/** Normalised cutoff position u in [0,1].  Everything in the design is
    scheduled against u, not against Hz: a control that is linear in log
    frequency is the only kind that can be perceptually linear. */
inline float cutoffPosition (float cutoffHz) noexcept
{
    return clampValue (std::log2 (clampValue (cutoffHz, kMinCutoffHz, kMaxCutoffHz) / kMinCutoffHz)
                           / kCutoffOctaves,
                       0.0f, 1.0f);
}

inline float positionToCutoff (float u) noexcept
{
    return kMinCutoffHz * std::exp2 (clampValue (u, 0.0f, 1.0f) * kCutoffOctaves);
}

//==============================================================================
// High-pass stage
//==============================================================================

enum class Slope { db12 = 0, db18, db24, db36, db48, numSlopes };

constexpr int kNumSlopes    = 5;
constexpr int kMaxSvfStages = 4;

/** Butterworth section Q values, ordered low-Q first so that the *last* active
    section is the resonant one.  Putting resonance at the end of the cascade
    means the non-linear damping term sees an already high-passed signal, which
    is how a real cascaded analogue filter behaves. */
struct SlopeConfig
{
    int   numSvfStages;
    bool  usesOnePole;      // adds the odd 6 dB/oct for the 18 dB/oct setting
    float q[kMaxSvfStages];
    int   dbPerOctave;
};

constexpr SlopeConfig kSlopeConfigs[kNumSlopes] =
{
    { 1, false, { 0.70710678f, 1.0f, 1.0f, 1.0f },              12 },
    { 1, true,  { 1.00000000f, 1.0f, 1.0f, 1.0f },              18 },
    { 2, false, { 0.54119610f, 1.30656296f, 1.0f, 1.0f },       24 },
    { 3, false, { 0.51763809f, 0.70710678f, 1.93185165f, 1.0f },36 },
    { 4, false, { 0.50979558f, 0.60134489f, 0.89997622f, 2.56291545f }, 48 }
};

/** RESONANCE = 100% multiplies the resonant section's Q by (1 + this), i.e. a
    peak roughly 12 dB above the Butterworth response.  Deliberately modest:
    self-oscillation would fight the perceptual-balance concept and is not
    what a "smooth energy loss" sweep wants. */
constexpr float kResonanceQFactor = 2.98f;

//==============================================================================
// Brown-noise-inspired spectral tilt
//==============================================================================

/** Number of staggered first-order high-shelf sections used to synthesise a
    near-constant dB/octave downward tilt.  One per octave; 10 sections cover
    the whole audio band when the anchor sits at the bottom of the range. */
constexpr int kTiltSections = 10;

/** The tilt is anchored this many octaves ABOVE the current cutoff.

    This is the single most important decision in the plugin.  The tilt is not a
    fixed-pivot "1/f^2 filter": it tracks the cutoff, so the octave immediately
    above the cutoff (including any resonant peak) is left alone and everything
    further up is progressively attenuated.  The result is that the spectral
    centre of gravity stays pinned near the cutoff instead of running away
    toward Nyquist. */
constexpr float kTiltAnchorOctaves = 1.0f;   // see Tools/SweepAnalysis --tilt

/** Slope in dB/octave applied when CHARACTER = 100% at the top of the cutoff
    range.  -6 dB/oct in amplitude is exactly the brown-noise (1/f^2 power)
    weighting; that is the ceiling, reached only at maximum cutoff. */
constexpr float kTiltMaxDbPerOctave = 6.0f;

/** Below this normalised cutoff position the tilt is (essentially) zero, so
    low cutoff settings stay uncoloured. */
constexpr float kTiltKneeStart = 0.12f;

/** Shape of the cutoff -> tilt-slope relationship.  Smoothstep between the knee
    and the top of the range: no tilt at the bottom, gentle onset through the
    low mids, strongest at the top. */
inline float tiltSlopeShape (float u) noexcept
{
    return smoothStep ((u - kTiltKneeStart) / (1.0f - kTiltKneeStart));
}

/** dB/octave of downward tilt for a given cutoff position and CHARACTER. */
inline float tiltSlopeDbPerOctave (float u, float character) noexcept
{
    return kTiltMaxDbPerOctave * clampValue (character, 0.0f, 1.0f) * tiltSlopeShape (u);
}

/** Tilt sections whose corner frequency approaches Nyquist are faded out to an
    exact bypass (shelf gain 1.0) rather than being switched off, so they can
    never click and never park energy at Nyquist. */
constexpr float kTiltFadeStartNormalisedFreq = 0.30f;   // x sample rate
constexpr float kTiltFadeEndNormalisedFreq   = 0.45f;

//==============================================================================
// Perceptual loudness contour (the AUTO gain schedule)
//==============================================================================

/** Total designed loudness drop, in dB, from the bottom to the top of the
    cutoff range at CHARACTER = 100%. */
constexpr float kLoudnessTargetDropDb = 45.0f;

/** The contour stays flat below this cutoff position ("full body" region). */
constexpr float kLoudnessKneeStart = 0.05f;

/** Width of the smooth onset of the fade, in units of the remaining range.

    The contour has to start with zero slope (otherwise the fade "switches on"
    as soon as you leave the bottom of the range) but must become linear in dB
    quickly, because a constant dLoudness/du is what makes a sweep feel even.
    s(w) = (1+k) w^2 / (w+k) does exactly that: s(0) = s'(0) = 0, s(1) = 1, and
    s(w) ~ w - k for w >> k.  A power law (w^1.15, the first thing tried) has a
    near-infinite second derivative at the origin and audibly "snaps" in. */
constexpr float kLoudnessOnsetKnee = 0.15f;

/** Softness of the "never boost" clamp, in dB. */
constexpr float kLoudnessSoftKneeDb = 5.0f;

/** Normalised shape of the loudness contour: 0 at the bottom of the cutoff
    range, 1 at the top, smooth at both ends, near-linear in between. */
inline float loudnessContourShape (float u) noexcept
{
    const float w = clampValue ((u - kLoudnessKneeStart) / (1.0f - kLoudnessKneeStart), 0.0f, 1.0f);
    return (1.0f + kLoudnessOnsetKnee) * w * w / (w + kLoudnessOnsetKnee);
}

inline float loudnessTargetDb (float u, float character) noexcept
{
    return -kLoudnessTargetDropDb * clampValue (character, 0.0f, 1.0f) * loudnessContourShape (u);
}

//==============================================================================
// Tone stage (modulation destinations only - flat by default)
//==============================================================================

constexpr float kBassShelfHz   = 180.0f;
constexpr float kMidBellHz     = 900.0f;
constexpr float kMidBellQ      = 0.9f;
constexpr float kTrebleShelfHz = 3200.0f;

//==============================================================================
// Analogue stage
//==============================================================================

/** Pre-gain into the waveshaper at ANALOG = 100%. */
constexpr float kAnalogMaxDrive = 1.8f;

/** Asymmetry offset at ANALOG = 100%.  Produces the second harmonic that makes
    the stage read as "valve-ish" rather than "fuzz". */
constexpr float kAnalogMaxBias = 0.14f;

/** Level-dependent damping added to the resonant SVF section at ANALOG = 100%. */
constexpr float kAnalogMaxFilterDamping = 1.5f;

/** Gentle top-end softening: a first-order high shelf that goes from 0 dB
    (ANALOG = 0, an exact bypass) to this much cut above kAnalogHfShelfHz. */
constexpr float kDcBlockerHz = 12.0f;

constexpr float kAnalogHfShelfHz    = 9000.0f;
constexpr float kAnalogHfShelfMaxDb = -4.5f;

//==============================================================================
// Smoothing
//==============================================================================

/** Cutoff is smoothed in log2(Hz), so the ramp is perceptually uniform.  60 ms
    is long enough to make even a stepped host automation curve sound like a
    continuous glide, short enough to still feel like a performance control. */
constexpr float kCutoffSmoothingSeconds = 0.060f;
constexpr float kSlowSmoothingSeconds   = 0.030f;
constexpr float kFastSmoothingSeconds   = 0.012f;

/** Control-rate block, in samples at the *host* sample rate.  ~0.35 ms at
    44.1 kHz, i.e. a ~2.8 kHz coefficient update rate. */
constexpr int kControlBlockSamples = 16;

//==============================================================================
// Modulation depths (per LFO destination, at DEPTH = 100%)
//==============================================================================

constexpr float kModCutoffOctaves = 2.0f;
constexpr float kModResonance     = 0.5f;
constexpr float kModCharacter     = 0.5f;
constexpr float kModSaturation    = 0.5f;
constexpr float kModTrebleDb      = 10.0f;
constexpr float kModMidDb         = 10.0f;
constexpr float kModBassDb        = 12.0f;
constexpr float kModAmplitudeDb   = 36.0f;   // unipolar, downwards only
constexpr float kModMix           = 0.5f;

} // namespace bsweep
