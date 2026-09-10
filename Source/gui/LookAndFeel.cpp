#include "LookAndFeel.h"

namespace bsgui
{

using namespace theme;

BrownSweepLookAndFeel::BrownSweepLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Slider::textBoxTextColourId,         textBright);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,                 text);
    setColour (juce::ComboBox::backgroundColourId,        panelDeep);
    setColour (juce::ComboBox::outlineColourId,           outline);
    setColour (juce::ComboBox::textColourId,              textBright);
    setColour (juce::ComboBox::arrowColourId,             accent);
    setColour (juce::PopupMenu::backgroundColourId,       panel);
    setColour (juce::PopupMenu::textColourId,             textBright);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId,  juce::Colours::white);
    setColour (juce::ToggleButton::textColourId,          text);
    setColour (juce::TooltipWindow::backgroundColourId,   panel);
    setColour (juce::TooltipWindow::textColourId,         textBright);
}

juce::Font BrownSweepLookAndFeel::getLabelFont (juce::Label&)      { return labelFont (11.0f); }
juce::Font BrownSweepLookAndFeel::getComboBoxFont (juce::ComboBox&) { return labelFont (11.5f); }
juce::Font BrownSweepLookAndFeel::getPopupMenuFont()                { return labelFont (12.5f); }

void BrownSweepLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                              juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const float trackRadius = radius - 1.0f;
    const float bodyRadius  = radius * 0.74f;
    const float trackWidth  = juce::jmax (2.0f, radius * 0.12f);

    // Tick marks around the track.
    g.setColour (outline);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * (i / 10.0f);
        const float inner = trackRadius + 1.5f;
        const float outer = trackRadius + (i % 5 == 0 ? 5.0f : 3.0f);
        g.drawLine (centre.x + inner * std::sin (a), centre.y - inner * std::cos (a),
                    centre.x + outer * std::sin (a), centre.y - outer * std::cos (a),
                    i % 5 == 0 ? 1.4f : 1.0f);
    }

    // Value track.
    juce::Path back;
    back.addCentredArc (centre.x, centre.y, trackRadius - trackWidth * 0.5f, trackRadius - trackWidth * 0.5f,
                        0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (panelDeep);
    g.strokePath (back, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, trackRadius - trackWidth * 0.5f, trackRadius - trackWidth * 0.5f,
                             0.0f, rotaryStartAngle, angle, true);
        g.setColour (slider.isEnabled() ? accent : outlineBright);
        g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body: a shallow vertical gradient plus a rim, which reads as a machined
    // aluminium cap without needing an image.
    juce::ColourGradient body (knobBody, centre.x, centre.y - bodyRadius,
                               knobBodyDark, centre.x, centre.y + bodyRadius, false);
    g.setGradientFill (body);
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);

    g.setColour (outlineBright);
    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (centre.x - bodyRadius + 1.5f, centre.y - bodyRadius + 1.5f,
                   bodyRadius * 2.0f - 3.0f, bodyRadius * 2.0f - 3.0f, 1.0f);

    // Pointer.
    const float pointerInner = bodyRadius * 0.30f;
    const float pointerOuter = bodyRadius * 0.88f;
    g.setColour (slider.isEnabled() ? accent : outlineBright);
    g.drawLine (centre.x + pointerInner * std::sin (angle), centre.y - pointerInner * std::cos (angle),
                centre.x + pointerOuter * std::sin (angle), centre.y - pointerOuter * std::cos (angle),
                juce::jmax (1.8f, radius * 0.075f));
}

void BrownSweepLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                          int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height))
                            .reduced (0.5f);

    g.setColour (panelDeep);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (box.hasKeyboardFocus (false) ? accentDim : outline);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    juce::Path arrow;
    const float cx = bounds.getRight() - 11.0f;
    const float cy = bounds.getCentreY();
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (accent.withAlpha (box.isEnabled() ? 0.9f : 0.4f));
    g.fillPath (arrow);
}

void BrownSweepLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (7, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void BrownSweepLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                              bool isHighlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const float size = juce::jmin (14.0f, bounds.getHeight() - 2.0f);
    const auto box = juce::Rectangle<float> (bounds.getX() + 0.5f,
                                             bounds.getCentreY() - size * 0.5f, size, size);

    g.setColour (panelDeep);
    g.fillRoundedRectangle (box, 2.5f);
    g.setColour (isHighlighted ? outlineBright : outline);
    g.drawRoundedRectangle (box, 2.5f, 1.0f);

    if (button.getToggleState())
    {
        g.setColour (accent);
        g.fillRoundedRectangle (box.reduced (3.5f), 1.5f);
    }

    g.setColour (button.getToggleState() ? textBright : text);
    g.setFont (labelFont (11.0f));
    g.drawText (button.getButtonText(),
                bounds.withTrimmedLeft (size + 8.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
LabelledKnob::LabelledKnob (juce::String captionText, float knobFontSize)
    : caption (std::move (captionText)), fontSize (knobFontSize)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 16);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::textBoxTextColourId, theme::textBright);
    addAndMakeVisible (slider);
}

void LabelledKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (static_cast<int> (fontSize) + 5);
    slider.setBounds (area);
}

void LabelledKnob::paint (juce::Graphics& g)
{
    g.setColour (theme::text);
    g.setFont (theme::labelFont (fontSize, true));
    g.drawText (caption, getLocalBounds().removeFromTop (static_cast<int> (fontSize) + 5),
                juce::Justification::centredTop, false);
}

} // namespace bsgui
