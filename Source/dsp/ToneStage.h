/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    ToneStage.h - three broad, flat-by-default trims.

    These exist purely as LFO destinations ("Bass Level", "Mid Level",
    "Treble").  They are deliberately gentle - first-order shelves and one very
    wide bell - so that modulating them sounds like a filter breathing rather
    than an EQ being yanked.  At 0 dB each section is an exact algebraic bypass,
    so leaving them unused costs nothing in colour.
*/

#pragma once

#include "DesignConstants.h"
#include "Filters.h"

namespace bsweep
{

struct ToneCoefficients
{
    float bassG = 0.5f,   bassGain   = 1.0f;
    SvfCoefficients mid;  float midGain = 1.0f;
    float trebG = 0.5f,   trebleGain = 1.0f;
    bool  flat  = true;

    void update (float bassDb, float midDb, float trebleDb, float sampleRate) noexcept
    {
        bassGain   = dbToGain (bassDb);
        midGain    = dbToGain (midDb);
        trebleGain = dbToGain (trebleDb);
        flat = (std::abs (bassDb) < 1.0e-4f && std::abs (midDb) < 1.0e-4f && std::abs (trebleDb) < 1.0e-4f);

        const float gb = tptGain (kBassShelfHz, sampleRate);
        bassG = gb / (1.0f + gb);

        mid.set (tptGain (kMidBellHz, sampleRate), 1.0f / kMidBellQ);

        const float gt = tptGain (kTrebleShelfHz, sampleRate);
        trebG = gt / (1.0f + gt);
    }
};

class ToneStageState
{
public:
    void reset() noexcept { bass.reset(); mid.reset(); treble.reset(); }

    inline float process (const ToneCoefficients& c, float x) noexcept
    {
        x = bass.processLowShelfShared (x, c.bassG, c.bassGain);
        x = mid.processBellShared (x, c.mid, c.midGain);
        x = treble.processHighShelfShared (x, c.trebG, c.trebleGain);
        return x;
    }

    bool isFinite() const noexcept { return bass.isFinite() && mid.isFinite() && treble.isFinite(); }

private:
    OnePoleTpt bass, treble;
    SvfTpt     mid;
};

} // namespace bsweep
