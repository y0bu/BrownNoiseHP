#include "PluginEditor.h"

#include "Parameters.h"

using namespace bsgui::theme;

namespace
{
    constexpr int kWidth  = 780;
    constexpr int kHeight = 640;

    void styleSectionLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (bsgui::theme::labelFont (9.5f, true));
        label.setColour (juce::Label::textColourId, bsgui::theme::text);
        label.setJustificationType (juce::Justification::centredLeft);
    }
}

BrownSweepAudioProcessorEditor::BrownSweepAudioProcessorEditor (BrownSweepAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (display);

    for (auto* knob : { &cutoffKnob, &characterKnob, &analogKnob, &resonanceKnob, &mixKnob, &outputKnob })
        addAndMakeVisible (*knob);

    auto& state = processor.getValueTreeState();

    cutoffAttachment    = std::make_unique<SliderAttachment> (state, bsparams::id::cutoff,    cutoffKnob.getSlider());
    characterAttachment = std::make_unique<SliderAttachment> (state, bsparams::id::character, characterKnob.getSlider());
    analogAttachment    = std::make_unique<SliderAttachment> (state, bsparams::id::analog,    analogKnob.getSlider());
    resonanceAttachment = std::make_unique<SliderAttachment> (state, bsparams::id::resonance, resonanceKnob.getSlider());
    mixAttachment       = std::make_unique<SliderAttachment> (state, bsparams::id::mix,       mixKnob.getSlider());
    outputAttachment    = std::make_unique<SliderAttachment> (state, bsparams::id::output,    outputKnob.getSlider());

    slopeBox.addItemList (bsparams::slopeChoices(), 1);
    modeBox.addItemList (bsparams::filterModeChoices(), 1);
    oversamplingBox.addItemList (bsparams::oversamplingChoices(), 1);
    addAndMakeVisible (slopeBox);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (oversamplingBox);
    addAndMakeVisible (autoGainButton);
    addAndMakeVisible (slopeLabel);
    addAndMakeVisible (modeLabel);
    addAndMakeVisible (oversamplingLabel);
    styleSectionLabel (slopeLabel, "SLOPE");
    styleSectionLabel (modeLabel, "MODE");
    styleSectionLabel (oversamplingLabel, "OVER");

    modeBox.setTooltip ("CLEAN: Butterworth cascade - neutral, maximally flat, resonance on the corner.\n"
                        "LADDER: four-pole OTA ladder with one global feedback loop and an asymmetric "
                        "clipper in it, after the SH-101's IR3109. Wider knee, resonance that blooms "
                        "below the corner, and even harmonics when you drive it.");

    slopeAttachment        = std::make_unique<ComboBoxAttachment> (state, bsparams::id::slope, slopeBox);
    modeAttachment         = std::make_unique<ComboBoxAttachment> (state, bsparams::id::filterMode, modeBox);
    oversamplingAttachment = std::make_unique<ComboBoxAttachment> (state, bsparams::id::oversampling, oversamplingBox);
    autoGainAttachment     = std::make_unique<ButtonAttachment>   (state, bsparams::id::autoGain, autoGainButton);

    autoGainButton.setTooltip ("Applies the perceptual loudness contour that makes an upward sweep "
                               "fade rather than thin out.  Switch it off to hear the raw filter.");
    cutoffKnob.getSlider().setTooltip ("High-pass cutoff.  Everything else in the plugin is scheduled "
                                       "against this control.");
    characterKnob.getSlider().setTooltip ("How strongly the brown-noise-inspired spectral balancing acts. "
                                          "0% is an ordinary high-pass.");

    for (int i = 0; i < 2; ++i)
        setUpLfoStrip (i);

    display.setState (processor.getResponseState());

    setSize (kWidth, kHeight);
    setResizable (false, false);
    startTimerHz (30);
}

BrownSweepAudioProcessorEditor::~BrownSweepAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void BrownSweepAudioProcessorEditor::setUpLfoStrip (int index)
{
    auto& strip = lfo[static_cast<size_t> (index)];
    auto& state = processor.getValueTreeState();

    styleSectionLabel (strip.caption, "LFO " + juce::String (index + 1));
    strip.caption.setFont (labelFont (11.0f, true));
    strip.caption.setColour (juce::Label::textColourId, accent);

    strip.destination.addItemList (bsparams::lfoDestinationChoices(), 1);
    strip.shape.addItemList (bsparams::lfoShapeChoices(), 1);
    strip.division.addItemList (bsparams::syncDivisionChoices(), 1);

    addAndMakeVisible (strip.caption);
    addAndMakeVisible (strip.destination);
    addAndMakeVisible (strip.shape);
    addAndMakeVisible (strip.division);
    addAndMakeVisible (strip.sync);
    addAndMakeVisible (strip.rate);
    addAndMakeVisible (strip.depth);
    addAndMakeVisible (strip.phase);

    strip.destinationAttachment = std::make_unique<ComboBoxAttachment> (state, bsparams::lfoId (index, bsparams::id::lfoDest),  strip.destination);
    strip.shapeAttachment       = std::make_unique<ComboBoxAttachment> (state, bsparams::lfoId (index, bsparams::id::lfoShape), strip.shape);
    strip.divisionAttachment    = std::make_unique<ComboBoxAttachment> (state, bsparams::lfoId (index, bsparams::id::lfoDiv),   strip.division);
    strip.syncAttachment        = std::make_unique<ButtonAttachment>   (state, bsparams::lfoId (index, bsparams::id::lfoSync),  strip.sync);
    strip.rateAttachment        = std::make_unique<SliderAttachment>   (state, bsparams::lfoId (index, bsparams::id::lfoRate),  strip.rate.getSlider());
    strip.depthAttachment       = std::make_unique<SliderAttachment>   (state, bsparams::lfoId (index, bsparams::id::lfoDepth), strip.depth.getSlider());
    strip.phaseAttachment       = std::make_unique<SliderAttachment>   (state, bsparams::lfoId (index, bsparams::id::lfoPhase), strip.phase.getSlider());

    strip.sync.onClick = [this, index]
    {
        auto& s = lfo[static_cast<size_t> (index)];
        const bool synced = s.sync.getToggleState();
        s.rate.getSlider().setEnabled (! synced);
        s.division.setEnabled (synced);
    };
    strip.sync.onClick();
}

//==============================================================================
void BrownSweepAudioProcessorEditor::timerCallback()
{
    display.setState (processor.getResponseState());
}

//==============================================================================
void BrownSweepAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // A very faint vertical sheen so the panel does not read as flat black.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.022f), 0.0f, 0.0f,
                                             juce::Colours::transparentBlack, 0.0f, static_cast<float> (getHeight()) * 0.6f,
                                             false));
    g.fillRect (getLocalBounds());

    const auto panelRect = [&g] (juce::Rectangle<int> r)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (outline);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
    };

    panelRect (controlArea);
    panelRect (settingsArea);
    panelRect (lfoArea);

    // ---- header ------------------------------------------------------------
    auto header = headerArea;

    g.setColour (textBright);
    g.setFont (labelFont (24.0f, true));
    g.drawText ("BROWNSWEEP", header.removeFromLeft (200), juce::Justification::centredLeft, false);

    g.setColour (accent);
    g.setFont (labelFont (11.0f, true));
    g.drawText ("PERCEPTUAL HIGH-PASS", header.removeFromLeft (200).withTrimmedTop (8),
                juce::Justification::centredLeft, false);

    g.setColour (text.withAlpha (0.7f));
    g.setFont (labelFont (10.0f));
    g.drawText ("YOAV AUDIO", headerArea, juce::Justification::centredRight, false);

    g.setColour (outline);
    g.drawHorizontalLine (headerArea.getBottom() + 2, static_cast<float> (headerArea.getX()),
                          static_cast<float> (headerArea.getRight()));

    // ---- divider between the big control and the small ones ----------------
    const int dividerX = controlArea.getX() + 196;
    g.setColour (outline);
    g.drawVerticalLine (dividerX, static_cast<float> (controlArea.getY() + 14),
                        static_cast<float> (controlArea.getBottom() - 14));
}

void BrownSweepAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    headerArea = area.removeFromTop (38);
    area.removeFromTop (10);

    display.setBounds (area.removeFromTop (186));
    area.removeFromTop (10);

    controlArea = area.removeFromTop (178);
    {
        auto inner = controlArea.reduced (10, 8);
        cutoffKnob.setBounds (inner.removeFromLeft (176));
        inner.removeFromLeft (20);

        const int each = inner.getWidth() / 5;
        for (auto* knob : { &characterKnob, &analogKnob, &resonanceKnob, &mixKnob, &outputKnob })
            knob->setBounds (inner.removeFromLeft (each).reduced (4, 14));
    }

    area.removeFromTop (10);

    settingsArea = area.removeFromTop (34);
    {
        auto inner = settingsArea.reduced (10, 6);
        modeLabel.setBounds (inner.removeFromLeft (36));
        modeBox.setBounds (inner.removeFromLeft (84).reduced (0, 1));
        inner.removeFromLeft (18);
        slopeLabel.setBounds (inner.removeFromLeft (38));
        slopeBox.setBounds (inner.removeFromLeft (92).reduced (0, 1));
        inner.removeFromLeft (18);
        oversamplingLabel.setBounds (inner.removeFromLeft (38));
        oversamplingBox.setBounds (inner.removeFromLeft (64).reduced (0, 1));
        inner.removeFromLeft (18);
        autoGainButton.setBounds (inner.removeFromLeft (110));
    }

    area.removeFromTop (10);

    lfoArea = area.removeFromTop (146);
    {
        auto inner = lfoArea.reduced (10, 6);
        layOutLfoStrip (lfo[0], inner.removeFromTop (66));
        inner.removeFromTop (2);
        layOutLfoStrip (lfo[1], inner.removeFromTop (66));
    }
}

void BrownSweepAudioProcessorEditor::layOutLfoStrip (LfoStrip& strip, juce::Rectangle<int> row)
{
    strip.caption.setBounds (row.removeFromLeft (46).withSizeKeepingCentre (46, 20));
    row.removeFromLeft (4);

    auto combo = [&row] (int width)
    {
        auto r = row.removeFromLeft (width).withSizeKeepingCentre (width, 22);
        row.removeFromLeft (8);
        return r;
    };

    strip.destination.setBounds (combo (110));
    strip.shape.setBounds (combo (118));
    strip.sync.setBounds (row.removeFromLeft (58).withSizeKeepingCentre (58, 20));
    row.removeFromLeft (6);
    strip.division.setBounds (combo (76));

    const int knobWidth = juce::jmax (52, row.getWidth() / 3);
    strip.rate.setBounds (row.removeFromLeft (knobWidth).reduced (2, 0));
    strip.depth.setBounds (row.removeFromLeft (knobWidth).reduced (2, 0));
    strip.phase.setBounds (row.removeFromLeft (knobWidth).reduced (2, 0));
}
