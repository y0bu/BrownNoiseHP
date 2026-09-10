/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    ResponseDisplay.h - draws what the plugin is actually doing.

    Two curves: the response a conventional high-pass of the same slope, cutoff
    and resonance would produce (dim), and BrownSweep's own response (bright).
    Both come from ResponseModel, which is exact for the TPT filters the engine
    runs, so the picture is not an illustration - it is the transfer function.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../dsp/ResponseModel.h"
#include "Theme.h"

namespace bsgui
{

class ResponseDisplay : public juce::Component
{
public:
    ResponseDisplay();

    /** Called from the editor's timer. */
    void setState (const bsweep::ResponseState& newState);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static constexpr float kTopDb = 18.0f;
    static constexpr float kBottomDb = -66.0f;

    float frequencyToX (float hz) const noexcept;
    float decibelToY (float db) const noexcept;

    void buildPaths();
    bool stateChangedEnoughToRedraw (const bsweep::ResponseState& a, const bsweep::ResponseState& b) const noexcept;

    bsweep::ResponseState state;
    juce::Rectangle<float> plot;
    juce::Path brownPath, brownFill, conventionalPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseDisplay)
};

} // namespace bsgui
