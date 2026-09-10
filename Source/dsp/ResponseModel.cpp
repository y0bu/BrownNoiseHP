#include "ResponseModel.h"

namespace bsweep::responsemodel
{

Complex highPassResponse (float freqHz, float cutoffHz, int slopeIndex,
                          float resonance01, float sampleRate) noexcept
{
    const auto& cfg = kSlopeConfigs[clampValue (slopeIndex, 0, kNumSlopes - 1)];
    const float w   = omega (freqHz, cutoffHz, sampleRate);
    const float resFactor = 1.0f + kResonanceQFactor * clampValue (resonance01, 0.0f, 1.0f);
    const int   resIdx    = cfg.numSvfStages - 1;

    Complex h (1.0f, 0.0f);

    for (int i = 0; i < cfg.numSvfStages; ++i)
    {
        const float q = cfg.q[i] * (i == resIdx ? resFactor : 1.0f);
        h *= svfHighpass (w, q);
    }

    if (cfg.usesOnePole)
        h *= onePoleHighpass (w);

    return h;
}

Complex tiltResponse (float freqHz, float cutoffHz, float tiltDbPerOctave, float sampleRate) noexcept
{
    if (tiltDbPerOctave <= 1.0e-5f)
        return { 1.0f, 0.0f };

    const float anchorHz  = cutoffHz * std::exp2 (kTiltAnchorOctaves);
    const float shelfGain = dbToGain (-tiltDbPerOctave);

    Complex h (1.0f, 0.0f);

    for (int k = 0; k < kTiltSections; ++k)
    {
        const float fk = anchorHz * std::exp2 (static_cast<float> (k));

        float m = shelfGain;
        float corner = fk;

        if (sampleRate > 0.0f)
        {
            const float fn   = fk / sampleRate;
            const float fade = 1.0f - smoothStep ((fn - kTiltFadeStartNormalisedFreq)
                                                  / (kTiltFadeEndNormalisedFreq - kTiltFadeStartNormalisedFreq));
            m      = 1.0f + (shelfGain - 1.0f) * fade;
            corner = std::min (fk, 0.47f * sampleRate * 0.999f);
        }

        if (std::abs (m - 1.0f) > 1.0e-6f)
            h *= onePoleHighShelf (omega (freqHz, corner, sampleRate), m);
    }

    return h;
}

Complex toneResponse (float freqHz, float bassDb, float midDb, float trebleDb, float sampleRate) noexcept
{
    Complex h (1.0f, 0.0f);

    if (std::abs (bassDb) > 1.0e-4f)
        h *= onePoleLowShelf (omega (freqHz, kBassShelfHz, sampleRate), dbToGain (bassDb));

    if (std::abs (midDb) > 1.0e-4f)
        h *= svfBell (omega (freqHz, kMidBellHz, sampleRate), kMidBellQ, dbToGain (midDb));

    if (std::abs (trebleDb) > 1.0e-4f)
        h *= onePoleHighShelf (omega (freqHz, kTrebleShelfHz, sampleRate), dbToGain (trebleDb));

    return h;
}

Complex analogShelfResponse (float freqHz, float analog01, float sampleRate) noexcept
{
    const float a = clampValue (analog01, 0.0f, 1.0f);
    if (a <= 1.0e-5f)
        return { 1.0f, 0.0f };

    return onePoleHighShelf (omega (freqHz, kAnalogHfShelfHz, sampleRate),
                             dbToGain (kAnalogHfShelfMaxDb * a));
}

Complex dcBlockerResponse (float freqHz, float sampleRate) noexcept
{
    return onePoleHighpass (omega (freqHz, kDcBlockerHz, sampleRate));
}

Complex wetResponse (const ResponseState& s, float freqHz) noexcept
{
    Complex h = highPassResponse (freqHz, s.cutoffHz, s.slopeIndex, s.resonance01, s.sampleRate);
    h *= tiltResponse (freqHz, s.cutoffHz, s.tiltDbPerOctave, s.sampleRate);
    h *= toneResponse (freqHz, s.bassDb, s.midDb, s.trebleDb, s.sampleRate);
    h *= analogShelfResponse (freqHz, s.analog01, s.sampleRate);
    h *= dcBlockerResponse (freqHz, s.sampleRate);
    return h * dbToGain (s.wetGainDb);
}

Complex fullResponse (const ResponseState& s, float freqHz) noexcept
{
    const float mix = clampValue (s.mix, 0.0f, 1.0f);
    const Complex wet = wetResponse (s, freqHz);
    const Complex dry (1.0f - mix, 0.0f);
    return (dry + mix * wet) * dbToGain (s.outputDb);
}

Complex conventionalResponse (const ResponseState& s, float freqHz) noexcept
{
    const float mix = clampValue (s.mix, 0.0f, 1.0f);
    const Complex wet = highPassResponse (freqHz, s.cutoffHz, s.slopeIndex, s.resonance01, s.sampleRate);
    const Complex dry (1.0f - mix, 0.0f);
    return (dry + mix * wet) * dbToGain (s.outputDb);
}

} // namespace bsweep::responsemodel
