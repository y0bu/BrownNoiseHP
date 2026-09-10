#include "BrownSweepEngine.h"

#include <cstring>

namespace bsweep
{

namespace
{
    int validFactor (int f) noexcept { return f == 4 ? 4 : (f == 2 ? 2 : 1); }
    constexpr float kConfigFadeSeconds = 0.008f;
    constexpr int   kCB = kControlBlockSamples;
}

BrownSweepEngine::BrownSweepEngine()
{
    // Force the loudness table to build here, on the construction thread,
    // rather than on the first prepareToPlay call.
    LoudnessSchedule::instance();
}

//==============================================================================
void BrownSweepEngine::prepare (double hostSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = (hostSampleRate > 0.0) ? hostSampleRate : 48000.0;
    maxBlock   = std::max (1, maxBlockSize);
    channels   = clampValue (numChannels, 1, kMaxChannels);

    activeOsFactor  = validFactor (params.oversamplingFactor);
    pendingOsFactor = activeOsFactor;

    oversampler.prepare (channels, kCB, activeOsFactor);

    const float osRate = static_cast<float> (sampleRate * activeOsFactor);
    hpStage.prepare (osRate);

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        analogState[ch].prepare (osRate);
        dryDelay[ch].prepare (64);
        dryDelay[ch].setDelay (oversampler.getLatencySamples());
    }

    lfos[0].prepare (sampleRate, 0x1F123BB5u);
    lfos[1].prepare (sampleRate, 0x6C078965u);

    scratchIn .assign (static_cast<size_t> (kMaxChannels * kCB), 0.0f);
    scratchDry.assign (static_cast<size_t> (kMaxChannels * kCB), 0.0f);
    scratchWet.assign (static_cast<size_t> (kMaxChannels * kCB), 0.0f);

    configFadeStep = 1.0f / std::max (1.0f, kConfigFadeSeconds * static_cast<float> (sampleRate));
    configFade     = 1.0f;
    fadingOut      = false;

    reset();
}

void BrownSweepEngine::reset()
{
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        hpState[ch].reset();
        tiltState[ch].reset();
        toneState[ch].reset();
        analogState[ch].reset();
        dryDelay[ch].reset();
    }

    oversampler.reset();
    lfos[0].reset();
    lfos[1].reset();

    needsSnap = true;
    sanitiseCount.store (0, std::memory_order_relaxed);
}

void BrownSweepEngine::setParameters (const EngineParameters& p) noexcept
{
    params = p;
    pendingOsFactor = validFactor (p.oversamplingFactor);
}

//==============================================================================
void BrownSweepEngine::applyOversamplingChange()
{
    activeOsFactor = pendingOsFactor;
    oversampler.setFactor (activeOsFactor);

    const float osRate = static_cast<float> (sampleRate * activeOsFactor);
    hpStage.prepare (osRate);

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        hpState[ch].reset();
        tiltState[ch].reset();
        toneState[ch].reset();
        analogState[ch].prepare (osRate);
        dryDelay[ch].setDelay (oversampler.getLatencySamples());
        dryDelay[ch].reset();
    }

    needsSnap = true;
}

//==============================================================================
void BrownSweepEngine::process (float* const* channelData, int numChannels, int numSamples) noexcept
{
    const int nch = clampValue (numChannels, 0, channels);
    if (nch <= 0 || numSamples <= 0) return;

    for (int offset = 0; offset < numSamples; offset += kCB)
    {
        const int n = std::min (kCB, numSamples - offset);
        processChunk (channelData, nch, offset, n);
    }
}

//==============================================================================
void BrownSweepEngine::processChunk (float* const* channelData, int nch,
                                     int startSample, int n) noexcept
{
    // -- oversampling reconfiguration happens on chunk boundaries only --------
    if (fadingOut && configFade <= 0.0f)
    {
        applyOversamplingChange();
        fadingOut = false;
    }
    if (pendingOsFactor != activeOsFactor)
        fadingOut = true;

    const int    factor = activeOsFactor;
    const double osRate = sampleRate * factor;

    // -- LFOs -----------------------------------------------------------------
    float modCutoffOct = 0.0f, modRes = 0.0f, modChar = 0.0f, modSat = 0.0f;
    float modTreble = 0.0f, modMid = 0.0f, modBass = 0.0f, modMix = 0.0f, modAmpDb = 0.0f;

    for (int i = 0; i < 2; ++i)
    {
        const auto& lp  = params.lfo[i];
        const float v   = lfos[i].advance (lp, params.bpm, n);
        const auto  dst = static_cast<LfoDestination> (clampValue (lp.destination, 0, kNumLfoDestinations - 1));
        const float amt = Lfo::modulationAmount (dst, v, lp.depth);

        switch (dst)
        {
            case LfoDestination::cutoff:     modCutoffOct += amt; break;
            case LfoDestination::resonance:  modRes       += amt; break;
            case LfoDestination::character:  modChar      += amt; break;
            case LfoDestination::saturation: modSat       += amt; break;
            case LfoDestination::treble:     modTreble    += amt; break;
            case LfoDestination::midLevel:   modMid       += amt; break;
            case LfoDestination::bassLevel:  modBass      += amt; break;
            case LfoDestination::amplitude:  modAmpDb     += amt; break;
            case LfoDestination::mix:        modMix       += amt; break;
            case LfoDestination::off:
            case LfoDestination::numDestinations: break;
        }
    }

    // -- targets --------------------------------------------------------------
    const float targetCutoff = clampValue (params.cutoffHz * std::exp2 (modCutoffOct),
                                           kMinCutoffHz, kMaxCutoffHz);
    const float targetLog2   = std::log2 (targetCutoff);
    const float targetChar   = clampValue (params.character + modChar, 0.0f, 1.0f);
    const float targetRes    = clampValue (params.resonance + modRes,  0.0f, 1.0f);
    const float targetAnalog = clampValue (params.analog    + modSat,  0.0f, 1.0f);
    const float targetBass   = clampValue (modBass,   -24.0f, 24.0f);
    const float targetMid    = clampValue (modMid,    -24.0f, 24.0f);
    const float targetTreble = clampValue (modTreble, -24.0f, 24.0f);
    // Bypass is applied here rather than by an early return in the host wrapper:
    // routing the output to the delay-compensated dry path keeps the plugin in
    // time with the latency it reports, and the smoothers make the transition
    // click-free in both directions.
    const float targetMix    = params.bypassed ? 0.0f : clampValue (params.mix + modMix, 0.0f, 1.0f);

    const float sr    = static_cast<float> (sampleRate);
    const float aCut  = needsSnap ? 1.0f : smootherCoefficient (kCutoffSmoothingSeconds, sr, n);
    const float aSlow = needsSnap ? 1.0f : smootherCoefficient (kSlowSmoothingSeconds,   sr, n);
    const float aFast = needsSnap ? 1.0f : smootherCoefficient (kFastSmoothingSeconds,   sr, n);

    smLog2Cutoff += (targetLog2   - smLog2Cutoff) * aCut;
    smCharacter  += (targetChar   - smCharacter)  * aSlow;
    smResonance  += (targetRes    - smResonance)  * aSlow;
    smAnalog     += (targetAnalog - smAnalog)     * aSlow;
    smBassDb     += (targetBass   - smBassDb)     * aFast;
    smMidDb      += (targetMid    - smMidDb)      * aFast;
    smTrebleDb   += (targetTreble - smTrebleDb)   * aFast;

    const float cutoff = clampValue (std::exp2 (smLog2Cutoff), kMinCutoffHz, kMaxCutoffHz);
    const float u      = cutoffPosition (cutoff);
    const float tiltDb = tiltSlopeDbPerOctave (u, smCharacter);

    const float autoDb = params.autoGain
        ? LoudnessSchedule::instance().gainDb (u, smCharacter, smResonance,
                                               params.slopeIndex, params.filterMode)
        : 0.0f;

    const float wetTarget = dbToGain (autoDb + modAmpDb);
    const float outTarget = params.bypassed ? 1.0f : dbToGain (params.outputDb);

    const float wetStart = needsSnap ? wetTarget : smWetGain;
    const float mixStart = needsSnap ? targetMix : smMix;
    const float outStart = needsSnap ? outTarget : smOutGain;

    smWetGain += (wetTarget - smWetGain) * aSlow;
    smMix     += (targetMix - smMix)     * aSlow;
    smOutGain += (outTarget - smOutGain) * aSlow;

    // -- coefficients (shared by every channel) -------------------------------
    hpStage.update (cutoff, params.slopeIndex, params.filterMode, smResonance, smAnalog,
                    needsSnap ? 1.0f : aSlow, static_cast<float> (osRate));
    tiltCoefficients.update (cutoff, tiltDb, static_cast<float> (osRate));
    toneCoefficients.update (smBassDb, smMidDb, smTrebleDb, static_cast<float> (osRate));
    analogCoefficients.update (smAnalog, static_cast<float> (osRate));

    publishResponseState (cutoff, tiltDb, smResonance, smBassDb, smMidDb, smTrebleDb,
                          smAnalog, gainToDb (smWetGain), smMix);

    needsSnap = false;

    // -- gather input, feed the dry delay -------------------------------------
    const float* inPtrs[kMaxChannels];
    float*       wetPtrs[kMaxChannels];

    for (int ch = 0; ch < nch; ++ch)
    {
        const float* src = channelData[ch] + startSample;
        float* in  = scratchIn.data()  + static_cast<size_t> (ch * kCB);
        float* dry = scratchDry.data() + static_cast<size_t> (ch * kCB);

        for (int i = 0; i < n; ++i)
        {
            float x = src[i];
            if (! std::isfinite (x)) x = 0.0f;
            dry[i] = dryDelay[ch].process (x);
            in[i]  = x + antiDenormal;
        }

        inPtrs[ch]  = in;
        wetPtrs[ch] = scratchWet.data() + static_cast<size_t> (ch * kCB);
    }

    antiDenormal = -antiDenormal;

    // -- oversampled non-linear chain -----------------------------------------
    oversampler.upsample (inPtrs, nch, n);

    const int osLen = n * factor;

    for (int ch = 0; ch < nch; ++ch)
    {
        float* w = oversampler.channelData (ch);
        auto& hp   = hpState[ch];
        auto& tilt = tiltState[ch];
        auto& tone = toneState[ch];
        auto& ana  = analogState[ch];

        hp.beginBlock (hpStage);

        for (int m = 0; m < osLen; ++m)
        {
            float x = w[m];
            x = hp.process (hpStage, x);
            x = tilt.process (tiltCoefficients, x);
            x = tone.process (toneCoefficients, x);
            x = ana.process (analogCoefficients, x);
            w[m] = x;
        }
    }

    oversampler.downsample (wetPtrs, nch, n);

    // -- mix, gains, output ---------------------------------------------------
    const float fadeStart = configFade;
    float fadeEnd = configFade;
    {
        const float dir = fadingOut ? -configFadeStep : configFadeStep;
        fadeEnd = clampValue (configFade + dir * static_cast<float> (n), 0.0f, 1.0f);
    }
    configFade = fadeEnd;

    double checksum = 0.0;

    for (int ch = 0; ch < nch; ++ch)
    {
        float* dst = channelData[ch] + startSample;
        const float* dry = scratchDry.data() + static_cast<size_t> (ch * kCB);
        const float* wet = scratchWet.data() + static_cast<size_t> (ch * kCB);

        for (int i = 0; i < n; ++i)
        {
            const float t  = static_cast<float> (i + 1) / static_cast<float> (n);
            const float wg = lerp (wetStart,  smWetGain, t);
            const float mx = lerp (mixStart,  smMix,     t);
            const float og = lerp (outStart,  smOutGain, t);
            const float fd = lerp (fadeStart, fadeEnd,   t);

            const float y = (dry[i] * (1.0f - mx) + wet[i] * wg * mx) * og * fd;
            dst[i] = y;
            checksum += static_cast<double> (y);
        }
    }

    // -- safety net -----------------------------------------------------------
    // The structures used here are unconditionally stable, so this should never
    // fire; the tests assert that the counter stays at zero.  It exists because
    // a host that feeds a NaN into a feedback path would otherwise poison the
    // filter states permanently.
    if (! std::isfinite (checksum))
    {
        for (int ch = 0; ch < nch; ++ch)
            std::memset (channelData[ch] + startSample, 0, sizeof (float) * static_cast<size_t> (n));

        reset();
        sanitiseCount.fetch_add (1, std::memory_order_relaxed);
    }
}

//==============================================================================
void BrownSweepEngine::publishResponseState (float cutoff, float tilt, float resonance,
                                             float bassDb, float midDb, float trebleDb,
                                             float analogAmt, float wetGainDb, float mixValue) noexcept
{
    const uint32_t seq = responseSeq.load (std::memory_order_relaxed);
    responseSeq.store (seq + 1, std::memory_order_release);

    responseState.cutoffHz        = cutoff;
    responseState.tiltDbPerOctave = tilt;
    responseState.resonance01     = resonance;
    responseState.slopeIndex      = params.slopeIndex;
    responseState.filterMode      = params.filterMode;
    responseState.bassDb          = bassDb;
    responseState.midDb           = midDb;
    responseState.trebleDb        = trebleDb;
    responseState.analog01        = analogAmt;
    responseState.wetGainDb       = wetGainDb;
    responseState.mix             = mixValue;
    responseState.outputDb        = params.outputDb;
    responseState.sampleRate      = static_cast<float> (sampleRate * activeOsFactor);

    responseSeq.store (seq + 2, std::memory_order_release);
}

ResponseState BrownSweepEngine::getResponseState() const noexcept
{
    ResponseState copy;

    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const uint32_t before = responseSeq.load (std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;

        copy = responseState;

        const uint32_t after = responseSeq.load (std::memory_order_acquire);
        if (before == after) break;
    }

    return copy;
}

} // namespace bsweep
