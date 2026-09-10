/*
    BrownSweep - Perceptual High-Pass
    Copyright (c) Yoav Audio
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/BrownSweepEngine.h"

class BrownSweepAudioProcessor : public juce::AudioProcessor,
                                 private juce::AsyncUpdater
{
public:
    BrownSweepAudioProcessor();
    ~BrownSweepAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* getBypassParameter() const override { return bypassParameter; }

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    bsweep::ResponseState getResponseState() const noexcept { return engine.getResponseState(); }

private:
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;
    bsparams::ParameterHandles handles;
    juce::AudioParameterBool* bypassParameter = nullptr;

    bsweep::BrownSweepEngine engine;
    juce::AudioBuffer<float> conversionBuffer;

    // setLatencySamples() notifies the host, which is not something to do from
    // the audio thread; the change is handed to the message thread instead.
    std::atomic<float> pendingLatency { -1.0f };
    float reportedLatency = -1.0f;
    double lastKnownBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrownSweepAudioProcessor)
};
