/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"
#include "gui/ResponseDisplay.h"

class BrownSweepAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit BrownSweepAudioProcessorEditor (BrownSweepAudioProcessor&);
    ~BrownSweepAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct LfoStrip
    {
        juce::Label      caption;
        juce::ComboBox   destination, shape, division;
        juce::ToggleButton sync { "SYNC" };
        bsgui::LabelledKnob rate  { "RATE",  9.5f };
        bsgui::LabelledKnob depth { "DEPTH", 9.5f };
        bsgui::LabelledKnob phase { "PHASE", 9.5f };

        std::unique_ptr<ComboBoxAttachment> destinationAttachment, shapeAttachment, divisionAttachment;
        std::unique_ptr<ButtonAttachment>   syncAttachment;
        std::unique_ptr<SliderAttachment>   rateAttachment, depthAttachment, phaseAttachment;
    };

    void timerCallback() override;
    void setUpLfoStrip (int index);
    void layOutLfoStrip (LfoStrip&, juce::Rectangle<int> row);

    BrownSweepAudioProcessor& processor;
    bsgui::BrownSweepLookAndFeel lookAndFeel;

    bsgui::ResponseDisplay display;

    bsgui::LabelledKnob cutoffKnob    { "CUTOFF", 13.0f };
    bsgui::LabelledKnob characterKnob { "CHARACTER" };
    bsgui::LabelledKnob analogKnob    { "ANALOG" };
    bsgui::LabelledKnob resonanceKnob { "RESONANCE" };
    bsgui::LabelledKnob mixKnob       { "MIX" };
    bsgui::LabelledKnob outputKnob    { "OUTPUT" };

    juce::ComboBox slopeBox, oversamplingBox;
    juce::ToggleButton autoGainButton { "AUTO GAIN" };
    juce::Label slopeLabel, oversamplingLabel;

    LfoStrip lfo[2];

    std::unique_ptr<SliderAttachment>   cutoffAttachment, characterAttachment, analogAttachment,
                                        resonanceAttachment, mixAttachment, outputAttachment;
    std::unique_ptr<ComboBoxAttachment> slopeAttachment, oversamplingAttachment;
    std::unique_ptr<ButtonAttachment>   autoGainAttachment;

    juce::Rectangle<int> headerArea, controlArea, settingsArea, lfoArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrownSweepAudioProcessorEditor)
};
