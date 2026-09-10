#include "PluginProcessor.h"

#include "PluginEditor.h"

BrownSweepAudioProcessor::BrownSweepAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BROWNSWEEP", bsparams::createParameterLayout())
{
    handles.attach (apvts);
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (bsparams::id::bypass));
}

//==============================================================================
bool BrownSweepAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out.isDisabled() || out != in)
        return false;

    // Mono, stereo, and anything up to the engine's channel limit (so that
    // surround or multi-channel buses still work; every channel is filtered
    // with identical coefficients).
    return out.size() >= 1 && out.size() <= bsweep::BrownSweepEngine::kMaxChannels;
}

void BrownSweepAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int channels = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels(), 1);

    engine.setParameters (handles.read (lastKnownBpm));
    engine.prepare (sampleRate, samplesPerBlock, juce::jmin (channels, bsweep::BrownSweepEngine::kMaxChannels));

    conversionBuffer.setSize (juce::jmin (channels, bsweep::BrownSweepEngine::kMaxChannels),
                              juce::jmax (1, samplesPerBlock));

    reportedLatency = engine.getLatencySamples();
    pendingLatency.store (reportedLatency, std::memory_order_relaxed);
    setLatencySamples (juce::roundToInt (reportedLatency));
}

void BrownSweepAudioProcessor::handleAsyncUpdate()
{
    const float latency = pendingLatency.load (std::memory_order_relaxed);
    if (! juce::approximatelyEqual (latency, reportedLatency))
    {
        reportedLatency = latency;
        setLatencySamples (juce::roundToInt (latency));
    }
}

//==============================================================================
void BrownSweepAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < juce::jmin (totalOut, buffer.getNumChannels()); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    double bpm = lastKnownBpm;
    if (auto* transport = getPlayHead())
        if (auto position = transport->getPosition())
            if (auto hostBpm = position->getBpm())
                if (*hostBpm > 1.0 && *hostBpm < 1000.0)
                    bpm = *hostBpm;
    lastKnownBpm = bpm;

    auto parameters = handles.read (bpm);
    parameters.bypassed = (bypassParameter != nullptr && bypassParameter->get());
    engine.setParameters (parameters);

    const int channels = juce::jmin (totalOut, buffer.getNumChannels(),
                                     bsweep::BrownSweepEngine::kMaxChannels);
    engine.process (buffer.getArrayOfWritePointers(), channels, buffer.getNumSamples());

    const float latency = engine.getLatencySamples();
    if (! juce::approximatelyEqual (latency, pendingLatency.load (std::memory_order_relaxed)))
    {
        pendingLatency.store (latency, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
}

void BrownSweepAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    // The engine is single precision throughout (a filter this simple gains
    // nothing measurable from doubles, and single precision keeps the
    // oversampled inner loop cache friendly).  Hosts running in double
    // precision are served through a conversion buffer.
    const int channels = juce::jmin (buffer.getNumChannels(), bsweep::BrownSweepEngine::kMaxChannels);
    const int numSamples = buffer.getNumSamples();

    if (conversionBuffer.getNumChannels() < channels || conversionBuffer.getNumSamples() < numSamples)
        conversionBuffer.setSize (juce::jmax (channels, 1), juce::jmax (numSamples, 1), false, false, true);

    for (int ch = 0; ch < channels; ++ch)
    {
        const double* src = buffer.getReadPointer (ch);
        float* dst = conversionBuffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i) dst[i] = static_cast<float> (src[i]);
    }

    juce::AudioBuffer<float> view (conversionBuffer.getArrayOfWritePointers(), channels, numSamples);
    processBlock (view, midi);

    for (int ch = 0; ch < channels; ++ch)
    {
        const float* src = conversionBuffer.getReadPointer (ch);
        double* dst = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i) dst[i] = static_cast<double> (src[i]);
    }
}

//==============================================================================
juce::AudioProcessorEditor* BrownSweepAudioProcessor::createEditor()
{
    return new BrownSweepAudioProcessorEditor (*this);
}

void BrownSweepAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BrownSweepAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BrownSweepAudioProcessor();
}
