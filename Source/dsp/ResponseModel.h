/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    ResponseModel.h - closed-form magnitude/phase of the linear part of the
    chain.

    Because every filter in the plugin is a TPT structure derived by bilinear
    transform with frequency prewarping, the digital response at frequency f is
    exactly the analogue prototype evaluated at

        Omega = tan(pi*f/fs) / tan(pi*f0/fs)

    so this model is not an approximation of the running DSP - it *is* the
    running DSP, minus the (deliberately gentle) non-linearity.  That single
    fact is what lets us:

      * draw a GUI curve that is guaranteed to match what you hear;
      * build the loudness schedule offline by integrating the real response
        against a pink reference spectrum and a K-weighting curve;
      * unit-test the model against an FFT of the measured impulse response.

    Passing sampleRate <= 0 evaluates the pure analogue prototype instead, which
    is what the sample-rate-independent loudness table uses.
*/

#pragma once

#include <complex>

#include "DesignConstants.h"
#include "DspMath.h"

namespace bsweep
{

using Complex = std::complex<float>;

/** Snapshot of everything the response depends on.  The engine publishes one of
    these for the editor. */
struct ResponseState
{
    float cutoffHz        = 20.0f;
    float tiltDbPerOctave = 0.0f;
    float resonance01     = 0.0f;
    int   slopeIndex      = 2;
    int   filterMode      = 0;      // FilterMode
    float bassDb          = 0.0f;
    float midDb           = 0.0f;
    float trebleDb        = 0.0f;
    float analog01        = 0.0f;
    float wetGainDb       = 0.0f;
    float mix             = 1.0f;
    float outputDb        = 0.0f;
    float sampleRate      = 48000.0f;
};

namespace responsemodel
{

/** Prewarped normalised frequency. */
inline float omega (float freqHz, float cornerHz, float sampleRate) noexcept
{
    if (sampleRate <= 0.0f)
        return freqHz / std::max (cornerHz, 1.0e-6f);

    const float nyq = 0.5f * sampleRate;
    const float wf  = std::tan (static_cast<float> (kPi) * clampValue (freqHz, 0.01f, 0.9999f * nyq) / sampleRate);
    const float w0  = std::tan (static_cast<float> (kPi) * clampValue (cornerHz, 0.01f, 0.9995f * nyq) / sampleRate);
    return wf / std::max (w0, 1.0e-9f);
}

/** Second-order high-pass section: H = s^2 / (s^2 + s/Q + 1). */
inline Complex svfHighpass (float w, float q) noexcept
{
    const Complex s (0.0f, w);
    const Complex s2 = s * s;
    return s2 / (s2 + s / q + Complex (1.0f, 0.0f));
}

/** First-order high-pass: H = s / (s + 1). */
inline Complex onePoleHighpass (float w) noexcept
{
    const Complex s (0.0f, w);
    return s / (s + Complex (1.0f, 0.0f));
}

/** First-order high shelf: unity at DC, gain m at infinity. */
inline Complex onePoleHighShelf (float w, float m) noexcept
{
    const Complex s (0.0f, w);
    return (Complex (1.0f, 0.0f) + m * s) / (Complex (1.0f, 0.0f) + s);
}

/** First-order low shelf: gain g0 at DC, unity at infinity. */
inline Complex onePoleLowShelf (float w, float g0) noexcept
{
    const Complex s (0.0f, w);
    return (Complex (g0, 0.0f) + s) / (Complex (1.0f, 0.0f) + s);
}

/** Bell: H = (s^2 + G*s/Q + 1) / (s^2 + s/Q + 1). */
inline Complex svfBell (float w, float q, float gain) noexcept
{
    const Complex s (0.0f, w);
    const Complex s2 = s * s;
    return (s2 + (gain / q) * s + Complex (1.0f, 0.0f)) / (s2 + s / q + Complex (1.0f, 0.0f));
}

//==============================================================================
/** Butterworth cascade only - i.e. what a "normal" HP filter with the same
    slope, cutoff and resonance would do.  This is the reference curve drawn
    behind the BrownSweep curve in the editor, and it is also the CLEAN mode's
    filter stage. */
Complex highPassResponse (float freqHz, float cutoffHz, int slopeIndex,
                          float resonance01, float sampleRate) noexcept;

/** The SH-101-style ladder topology:

        H(s) = s^N (1+s)^(4-N) / ((1+s)^4 + k)

    cascaded twice for slopes steeper than 24 dB/oct, with the poles placed so
    that the -3 dB point lands on `cutoffHz`. */
Complex ladderResponse (float freqHz, float cutoffHz, int slopeIndex,
                        float resonance01, float sampleRate) noexcept;

/** Whichever topology `filterMode` selects. */
Complex filterResponse (float freqHz, float cutoffHz, int slopeIndex, int filterMode,
                        float resonance01, float sampleRate) noexcept;

/** The brown-noise-inspired tilt cascade. */
Complex tiltResponse (float freqHz, float cutoffHz, float tiltDbPerOctave, float sampleRate) noexcept;

/** Bass / mid / treble trims (flat by default). */
Complex toneResponse (float freqHz, float bassDb, float midDb, float trebleDb, float sampleRate) noexcept;

/** The analogue stage's high-frequency shelf (the only linear part of it). */
Complex analogShelfResponse (float freqHz, float analog01, float sampleRate) noexcept;

/** The 12 Hz DC blocker that always follows the analogue stage. */
Complex dcBlockerResponse (float freqHz, float sampleRate) noexcept;

/** Complete wet path, including the AUTO gain. */
Complex wetResponse (const ResponseState& s, float freqHz) noexcept;

/** Wet/dry mixed output, including OUTPUT trim - what the plugin actually does. */
Complex fullResponse (const ResponseState& s, float freqHz) noexcept;

/** Conventional high-pass reference, including OUTPUT trim so the two curves
    are compared on equal terms. */
Complex conventionalResponse (const ResponseState& s, float freqHz) noexcept;

inline float magnitudeDb (Complex h) noexcept { return gainToDb (std::abs (h)); }

} // namespace responsemodel
} // namespace bsweep
