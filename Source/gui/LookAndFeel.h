/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    LookAndFeel.h - hardware-style rotary controls, drawn rather than
    bitmapped so the plugin has no binary assets and scales cleanly.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace bsgui
{

class BrownSweepLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BrownSweepLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

/** A rotary slider with its caption and value drawn underneath, so the editor
    layout only ever has to place one rectangle per control. */
class LabelledKnob : public juce::Component
{
public:
    LabelledKnob (juce::String captionText, float knobFontSize = 11.0f);

    juce::Slider& getSlider() noexcept { return slider; }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::String caption;
    float fontSize;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabelledKnob)
};

} // namespace bsgui
