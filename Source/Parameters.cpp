#include "Parameters.h"

namespace bsparams
{

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
    /** Exactly logarithmic frequency mapping.  JUCE's `skew` is a power law,
        which is close but not identical; an HP cutoff that is genuinely linear
        in log f is the whole basis of the perceptual scheduling, so it is worth
        spelling out. */
    juce::NormalisableRange<float> logFrequencyRange (float low, float high)
    {
        return { low, high,
                 [] (float s, float e, float v) { return s * std::pow (e / s, v); },
                 [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
                 [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
    }

    juce::String frequencyText (float value, int)
    {
        // juce::String (x, 0) selects the shortest round-tripping form rather
        // than zero decimal places, so integers are rounded explicitly.
        if (value >= 1000.0f) return juce::String (value / 1000.0f, 2) + " kHz";
        if (value >= 100.0f)  return juce::String (juce::roundToInt (value)) + " Hz";
        return juce::String (value, 1) + " Hz";
    }

    juce::String percentText (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; }
    juce::String decibelText (float value, int) { return juce::String (value, 1) + " dB"; }
    juce::String rateText (float value, int)
    {
        return value < 1.0f ? juce::String (value, 3) + " Hz" : juce::String (value, 2) + " Hz";
    }
    juce::String degreesText (float value, int)
    {
        return juce::String (juce::roundToInt (value)) + juce::String::fromUTF8 ("\xc2\xb0");
    }
}

juce::StringArray slopeChoices()
{
    juce::StringArray choices;
    for (int i = 0; i < bsweep::kNumSlopes; ++i)
        choices.add (juce::String (bsweep::kSlopeConfigs[i].dbPerOctave) + " dB/oct");
    return choices;
}

juce::StringArray filterModeChoices() { return { "Clean", "Ladder" }; }

juce::StringArray oversamplingChoices() { return { "Off", "2x", "4x" }; }

juce::StringArray lfoShapeChoices()
{
    return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Smooth Random" };
}

juce::StringArray lfoDestinationChoices()
{
    return { "Off", "Cutoff", "Resonance", "Character", "Saturation",
             "Treble", "Mid Level", "Bass Level", "Amplitude", "Mix" };
}

juce::StringArray syncDivisionChoices()
{
    juce::StringArray choices;
    for (int i = 0; i < bsweep::kNumSyncDivisions; ++i)
        choices.add (bsweep::kSyncDivisions[i].name);
    return choices;
}

APVTS::ParameterLayout createParameterLayout()
{
    APVTS::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::cutoff, 1 }, "Cutoff",
        logFrequencyRange (bsweep::kMinCutoffHz, bsweep::kMaxCutoffHz), bsweep::kMinCutoffHz,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (frequencyText)
                                             .withLabel ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::character, 1 }, "Character",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 65.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText).withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::analog, 1 }, "Analog",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText).withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::resonance, 1 }, "Resonance",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText).withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::mix, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText).withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::output, 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (decibelText).withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::slope, 1 }, "Slope", slopeChoices(), 2));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::filterMode, 1 }, "Filter Mode", filterModeChoices(),
        static_cast<int> (bsweep::FilterMode::ladder)));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::autoGain, 1 }, "Auto Gain", true));

    // Oversampling changes the reported latency, so it is a setup control
    // rather than something to draw automation curves on.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::oversampling, 1 }, "Oversampling", oversamplingChoices(), 1,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { id::bypass, 1 }, "Bypass", false));

    for (int n = 0; n < 2; ++n)
    {
        const juce::String label = "LFO " + juce::String (n + 1) + " ";

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { lfoId (n, id::lfoDest), 1 }, label + "Destination",
            lfoDestinationChoices(), 0));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { lfoId (n, id::lfoShape), 1 }, label + "Shape",
            lfoShapeChoices(), 0));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { lfoId (n, id::lfoRate), 1 }, label + "Rate",
            logFrequencyRange (0.01f, 20.0f), n == 0 ? 0.5f : 0.25f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (rateText).withLabel ("Hz")));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { lfoId (n, id::lfoSync), 1 }, label + "Sync", false));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { lfoId (n, id::lfoDiv), 1 }, label + "Division",
            syncDivisionChoices(), bsweep::kDefaultSyncDivision));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { lfoId (n, id::lfoDepth), 1 }, label + "Depth",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText).withLabel ("%")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { lfoId (n, id::lfoPhase), 1 }, label + "Phase",
            juce::NormalisableRange<float> (0.0f, 360.0f, 1.0f), n == 0 ? 0.0f : 90.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (degreesText)));
    }

    return layout;
}

//==============================================================================
void ParameterHandles::attach (APVTS& state)
{
    cutoff       = state.getRawParameterValue (id::cutoff);
    character    = state.getRawParameterValue (id::character);
    analog       = state.getRawParameterValue (id::analog);
    resonance    = state.getRawParameterValue (id::resonance);
    mix          = state.getRawParameterValue (id::mix);
    output       = state.getRawParameterValue (id::output);
    slope        = state.getRawParameterValue (id::slope);
    filterMode   = state.getRawParameterValue (id::filterMode);
    autoGain     = state.getRawParameterValue (id::autoGain);
    oversampling = state.getRawParameterValue (id::oversampling);

    for (int n = 0; n < 2; ++n)
    {
        lfo[n].destination = state.getRawParameterValue (lfoId (n, id::lfoDest));
        lfo[n].shape       = state.getRawParameterValue (lfoId (n, id::lfoShape));
        lfo[n].rate        = state.getRawParameterValue (lfoId (n, id::lfoRate));
        lfo[n].sync        = state.getRawParameterValue (lfoId (n, id::lfoSync));
        lfo[n].division    = state.getRawParameterValue (lfoId (n, id::lfoDiv));
        lfo[n].depth       = state.getRawParameterValue (lfoId (n, id::lfoDepth));
        lfo[n].phase       = state.getRawParameterValue (lfoId (n, id::lfoPhase));
    }
}

bsweep::EngineParameters ParameterHandles::read (double bpm) const
{
    bsweep::EngineParameters p;

    p.cutoffHz  = cutoff->load();
    p.character = character->load() * 0.01f;
    p.analog    = analog->load()    * 0.01f;
    p.resonance = resonance->load() * 0.01f;
    p.mix       = mix->load()       * 0.01f;
    p.outputDb  = output->load();
    p.slopeIndex = static_cast<int> (slope->load());
    p.filterMode = static_cast<int> (filterMode->load());
    p.autoGain   = autoGain->load() > 0.5f;
    p.bpm        = bpm;

    switch (static_cast<int> (oversampling->load()))
    {
        case 2:  p.oversamplingFactor = 4; break;
        case 1:  p.oversamplingFactor = 2; break;
        default: p.oversamplingFactor = 1; break;
    }

    for (int n = 0; n < 2; ++n)
    {
        p.lfo[n].destination = static_cast<int> (lfo[n].destination->load());
        p.lfo[n].shape       = static_cast<int> (lfo[n].shape->load());
        p.lfo[n].rateHz      = lfo[n].rate->load();
        p.lfo[n].sync        = lfo[n].sync->load() > 0.5f;
        p.lfo[n].division    = static_cast<int> (lfo[n].division->load());
        p.lfo[n].depth       = lfo[n].depth->load() * 0.01f;
        p.lfo[n].phaseOffset = lfo[n].phase->load() / 360.0f;
    }

    return p;
}

} // namespace bsparams
