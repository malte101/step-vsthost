/*
  ==============================================================================

    HostedInstrumentRack.cpp

  ==============================================================================
*/

#include "HostedInstrumentRack.h"

HostedInstrumentRack::HostedInstrumentRack()
{
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
#endif
#if JUCE_PLUGINHOST_AU
    formatManager.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
#endif
}

bool HostedInstrumentRack::loadPlugin(const juce::File& file, double sampleRate, int blockSize, juce::String& error)
{
    error.clear();
    if (!file.exists())
    {
        error = "Plugin file not found: " + file.getFullPathName();
        return false;
    }

    juce::OwnedArray<juce::PluginDescription> descriptions;
    for (auto* format : formatManager.getFormats())
        format->findAllTypesForFile(descriptions, file.getFullPathName());

    if (descriptions.isEmpty())
    {
        error = "No plugin types found in file";
        return false;
    }

    auto description = *descriptions.getFirst();
    std::unique_ptr<juce::AudioPluginInstance> newInstance;
    auto result = formatManager.createPluginInstance(description, sampleRate, blockSize, error);
    if (error.isNotEmpty() || result == nullptr)
        return false;

    newInstance = std::move(result);
    instance = std::move(newInstance);
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    instance->enableAllBuses();
    instance->prepareToPlay(sampleRate, blockSize);
    return true;
}

void HostedInstrumentRack::prepareToPlay(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    if (instance)
        instance->prepareToPlay(sampleRate, blockSize);
}

void HostedInstrumentRack::releaseResources()
{
    if (instance)
        instance->releaseResources();
}

void HostedInstrumentRack::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    if (!instance)
    {
        audio.clear();
        return;
    }
    instance->processBlock(audio, midi);
}

int HostedInstrumentRack::getOutputChannelCount() const
{
    if (instance == nullptr)
        return 0;
    return juce::jmax(0, instance->getTotalNumOutputChannels());
}
