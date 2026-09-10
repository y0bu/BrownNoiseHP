/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    Parameters.h - parameter identifiers, the APVTS layout, and the bridge from
    plugin parameters to the JUCE-free DSP engine.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/BrownSweepEngine.h"

namespace bsparams
{

namespace id
{
    inline constexpr const char* cutoff       = "cutoff";
    inline constexpr const char* character    = "character";
    inline constexpr const char* analog       = "analog";
    inline constexpr const char* resonance    = "resonance";
    inline constexpr const char* mix          = "mix";
    inline constexpr const char* output       = "output";
    inline constexpr const char* slope        = "slope";
    inline constexpr const char* autoGain     = "autogain";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* bypass       = "bypass";

    // LFO n (n = 1, 2) - built with lfoId() below.
    inline constexpr const char* lfoDest  = "dest";
    inline constexpr const char* lfoShape = "shape";
    inline constexpr const char* lfoRate  = "rate";
    inline constexpr const char* lfoSync  = "sync";
    inline constexpr const char* lfoDiv   = "div";
    inline constexpr const char* lfoDepth = "depth";
    inline constexpr const char* lfoPhase = "phase";
}

inline juce::String lfoId (int lfoIndex, const char* suffix)
{
    return "lfo" + juce::String (lfoIndex + 1) + suffix;
}

juce::StringArray slopeChoices();
juce::StringArray oversamplingChoices();
juce::StringArray lfoShapeChoices();
juce::StringArray lfoDestinationChoices();
juce::StringArray syncDivisionChoices();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Caches the raw parameter pointers once so that processBlock() never has to
    look anything up by string. */
struct ParameterHandles
{
    void attach (juce::AudioProcessorValueTreeState& state);

    /** Builds the engine parameter block for the current control values. */
    bsweep::EngineParameters read (double bpm) const;

    std::atomic<float>* cutoff       = nullptr;
    std::atomic<float>* character    = nullptr;
    std::atomic<float>* analog       = nullptr;
    std::atomic<float>* resonance    = nullptr;
    std::atomic<float>* mix          = nullptr;
    std::atomic<float>* output       = nullptr;
    std::atomic<float>* slope        = nullptr;
    std::atomic<float>* autoGain     = nullptr;
    std::atomic<float>* oversampling = nullptr;

    struct LfoHandles
    {
        std::atomic<float>* destination = nullptr;
        std::atomic<float>* shape       = nullptr;
        std::atomic<float>* rate        = nullptr;
        std::atomic<float>* sync        = nullptr;
        std::atomic<float>* division    = nullptr;
        std::atomic<float>* depth       = nullptr;
        std::atomic<float>* phase       = nullptr;
    };

    LfoHandles lfo[2];
};

} // namespace bsparams
