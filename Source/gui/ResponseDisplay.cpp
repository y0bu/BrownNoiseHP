#include "ResponseDisplay.h"

namespace bsgui
{

using namespace theme;

namespace
{
    constexpr int kCurvePoints = 320;

    const float kGridFrequencies[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                       2000.0f, 5000.0f, 10000.0f, 20000.0f };
    const char* kGridLabels[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    const float kGridDecibels[] = { 12.0f, 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f };
    const char* kGridDecibelLabels[] = { "+12", "0", "-12", "-24", "-36", "-48", "-60" };

    juce::String formatFrequency (float hz)
    {
        // Note: juce::String (x, 0) does *not* mean "no decimal places" - zero
        // selects the shortest round-tripping representation, which for 573.5
        // prints "573.531".  Round explicitly instead.
        if (hz >= 1000.0f) return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
        if (hz < 100.0f)   return juce::String (hz, 1) + " Hz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }
}

ResponseDisplay::ResponseDisplay()
{
    setInterceptsMouseClicks (false, false);
}

float ResponseDisplay::frequencyToX (float hz) const noexcept
{
    const float t = std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
    return plot.getX() + t * plot.getWidth();
}

float ResponseDisplay::decibelToY (float db) const noexcept
{
    const float t = (kTopDb - juce::jlimit (kBottomDb, kTopDb, db)) / (kTopDb - kBottomDb);
    return plot.getY() + t * plot.getHeight();
}

void ResponseDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (14.0f);
    buildPaths();
}

bool ResponseDisplay::stateChangedEnoughToRedraw (const bsweep::ResponseState& a,
                                                  const bsweep::ResponseState& b) const noexcept
{
    const auto differs = [] (float x, float y, float tolerance) { return std::abs (x - y) > tolerance; };

    return differs (a.cutoffHz, b.cutoffHz, std::max (0.25f, b.cutoffHz * 0.002f))
        || differs (a.tiltDbPerOctave, b.tiltDbPerOctave, 0.01f)
        || differs (a.resonance01, b.resonance01, 0.002f)
        || differs (a.wetGainDb, b.wetGainDb, 0.05f)
        || differs (a.mix, b.mix, 0.002f)
        || differs (a.outputDb, b.outputDb, 0.02f)
        || differs (a.bassDb, b.bassDb, 0.05f)
        || differs (a.midDb, b.midDb, 0.05f)
        || differs (a.trebleDb, b.trebleDb, 0.05f)
        || differs (a.analog01, b.analog01, 0.005f)
        || a.slopeIndex != b.slopeIndex
        || differs (a.sampleRate, b.sampleRate, 1.0f);
}

void ResponseDisplay::setState (const bsweep::ResponseState& newState)
{
    if (! stateChangedEnoughToRedraw (state, newState))
        return;

    state = newState;
    buildPaths();
    repaint();
}

void ResponseDisplay::buildPaths()
{
    brownPath.clear();
    brownFill.clear();
    conventionalPath.clear();

    if (plot.getWidth() < 4.0f || plot.getHeight() < 4.0f)
        return;

    for (int i = 0; i < kCurvePoints; ++i)
    {
        const float t  = static_cast<float> (i) / (kCurvePoints - 1);
        const float hz = kMinHz * std::pow (kMaxHz / kMinHz, t);
        const float x  = plot.getX() + t * plot.getWidth();

        const float brownDb = bsweep::responsemodel::magnitudeDb (
            bsweep::responsemodel::fullResponse (state, hz));
        const float plainDb = bsweep::responsemodel::magnitudeDb (
            bsweep::responsemodel::conventionalResponse (state, hz));

        const float yBrown = decibelToY (brownDb);
        const float yPlain = decibelToY (plainDb);

        if (i == 0)
        {
            brownPath.startNewSubPath (x, yBrown);
            conventionalPath.startNewSubPath (x, yPlain);
            brownFill.startNewSubPath (x, plot.getBottom());
            brownFill.lineTo (x, yBrown);
        }
        else
        {
            brownPath.lineTo (x, yBrown);
            conventionalPath.lineTo (x, yPlain);
            brownFill.lineTo (x, yBrown);
        }
    }

    brownFill.lineTo (plot.getRight(), plot.getBottom());
    brownFill.closeSubPath();
}

void ResponseDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (panelDeep);
    g.fillRoundedRectangle (bounds, 4.0f);

    // ---- grid --------------------------------------------------------------
    g.setFont (labelFont (9.5f));

    for (size_t i = 0; i < sizeof (kGridFrequencies) / sizeof (float); ++i)
    {
        const float x = frequencyToX (kGridFrequencies[i]);
        g.setColour (outline.withAlpha (0.55f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (text.withAlpha (0.55f));
        g.drawText (kGridLabels[i], juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 12.0f),
                    juce::Justification::centred, false);
    }

    for (size_t i = 0; i < sizeof (kGridDecibels) / sizeof (float); ++i)
    {
        const float db = kGridDecibels[i];
        const float y  = decibelToY (db);

        const bool isUnityLine = std::abs (db) < 0.001f;
        g.setColour (isUnityLine ? outlineBright.withAlpha (0.8f) : outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());

        // The curve runs through this area, so the scale labels get a small
        // backing plate to stay readable.  Lines that would sit under the
        // readout row at the top are left unlabelled rather than overlapping it.
        if (y < plot.getY() + 22.0f)
            continue;

        const auto label = juce::Rectangle<float> (plot.getX() + 3.0f, y - 5.0f, 22.0f, 10.0f);
        g.setColour (panelDeep.withAlpha (0.85f));
        g.fillRect (label);
        g.setColour (text.withAlpha (0.5f));
        g.drawText (kGridDecibelLabels[i], label, juce::Justification::centredLeft, false);
    }

    // ---- cutoff marker -----------------------------------------------------
    {
        const float x = frequencyToX (state.cutoffHz);
        g.setColour (accent.withAlpha (0.28f));
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ x, plot.getY(), x, plot.getBottom() }, dashes, 2, 1.0f);
    }

    // ---- curves ------------------------------------------------------------
    g.setColour (reference);
    g.strokePath (conventionalPath, juce::PathStrokeType (1.2f));

    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f), plot.getCentreX(), plot.getY(),
                                             accent.withAlpha (0.0f),  plot.getCentreX(), plot.getBottom(), false));
    g.fillPath (brownFill);

    g.setColour (accent);
    g.strokePath (brownPath, juce::PathStrokeType (1.9f, juce::PathStrokeType::curved));

    // ---- readouts ----------------------------------------------------------
    g.setFont (monoFont (10.5f));
    auto readout = plot.reduced (6.0f, 4.0f).removeFromTop (13.0f).withTrimmedLeft (2.0f);

    g.setColour (textBright);
    g.drawText (formatFrequency (state.cutoffHz), readout.removeFromLeft (78.0f),
                juce::Justification::left, false);

    g.setColour (text);
    g.drawText ("TILT " + juce::String (state.tiltDbPerOctave, 2) + " dB/oct",
                readout.removeFromLeft (120.0f), juce::Justification::left, false);
    g.drawText ("AUTO " + juce::String (state.wetGainDb, 1) + " dB",
                readout.removeFromLeft (110.0f), juce::Justification::left, false);

    // ---- legend ------------------------------------------------------------
    auto legend = plot.reduced (6.0f, 4.0f).removeFromTop (13.0f).removeFromRight (188.0f);

    g.setColour (reference);
    g.fillRect (juce::Rectangle<float> (legend.getX(), legend.getCentreY() - 0.5f, 12.0f, 1.5f));
    g.setColour (text);
    g.drawText ("HP", legend.withTrimmedLeft (16.0f).withWidth (24.0f), juce::Justification::left, false);

    g.setColour (accent);
    g.fillRect (juce::Rectangle<float> (legend.getX() + 48.0f, legend.getCentreY() - 0.5f, 12.0f, 2.0f));
    g.setColour (textBright);
    g.drawText ("BROWNSWEEP", legend.withTrimmedLeft (64.0f), juce::Justification::left, false);

    // ---- frame -------------------------------------------------------------
    g.setColour (outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
}

} // namespace bsgui
