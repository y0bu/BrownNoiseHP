/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio

    Tools/GuiSnapshot.cpp - renders the editor to a PNG without opening a
    window.  Useful for reviewing layout changes, and for CI to prove the editor
    still constructs and paints.

    Build with -DBROWNSWEEP_BUILD_GUI_SNAPSHOT=ON, then:
        ./build/brownsweep_snapshot out.png [cutoffHz] [character] [resonance]
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

namespace
{
    void setParameter (juce::AudioProcessorValueTreeState& state, const char* id, float value)
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String outputPath = argc > 1 ? argv[1] : "brownsweep.png";
    const float cutoff    = argc > 2 ? static_cast<float> (std::atof (argv[2])) : 420.0f;
    const float character = argc > 3 ? static_cast<float> (std::atof (argv[3])) : 78.0f;
    const float resonance = argc > 4 ? static_cast<float> (std::atof (argv[4])) : 34.0f;

    BrownSweepAudioProcessor processor;
    auto& state = processor.getValueTreeState();

    setParameter (state, "cutoff", cutoff);
    setParameter (state, "character", character);
    setParameter (state, "resonance", resonance);
    setParameter (state, "analog", 42.0f);
    setParameter (state, "lfo1dest", 1.0f);      // Cutoff
    setParameter (state, "lfo1depth", 25.0f);
    setParameter (state, "lfo2dest", 5.0f);      // Treble
    setParameter (state, "lfo2depth", 40.0f);

    // Run a little audio so the engine publishes a response snapshot.
    processor.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random random;

    for (int block = 0; block < 40; ++block)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                d[i] = 0.25f * (random.nextFloat() * 2.0f - 1.0f);
        }
        processor.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::fprintf (stderr, "failed to create the editor\n");
        return 1;
    }

    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (outputPath));
    file.deleteFile();
    juce::FileOutputStream stream (file);

    if (! stream.openedOk())
    {
        std::fprintf (stderr, "could not open %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, stream))
    {
        std::fprintf (stderr, "could not encode the image\n");
        return 1;
    }

    std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(),
                 image.getWidth(), image.getHeight());
    return 0;
}
