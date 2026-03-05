/*
  ==============================================================================

    HostedInstrumentRack.cpp

  ==============================================================================
*/

#include "HostedInstrumentRack.h"
#include <limits>

HostedInstrumentRack::HostedInstrumentRack()
{
#if JUCE_PLUGINHOST_VST3
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
#endif
#if JUCE_PLUGINHOST_AU
    formatManager.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
#endif
}

int HostedInstrumentRack::getEnabledBusChannelSum(const juce::AudioPluginInstance& plugin, bool isInput)
{
    const int busCount = plugin.getBusCount(isInput);
    int sum = 0;
    for (int bus = 0; bus < busCount; ++bus)
    {
        auto* busPtr = plugin.getBus(isInput, bus);
        if (busPtr == nullptr || !busPtr->isEnabled())
            continue;
        sum += juce::jmax(0, plugin.getChannelCountOfBus(isInput, bus));
    }
    return juce::jmax(0, sum);
}

int HostedInstrumentRack::getConfiguredOutputChannelCount(const juce::AudioPluginInstance& plugin)
{
    const int outputChannels = juce::jmax(
        juce::jmax(0, plugin.getTotalNumOutputChannels()),
        getEnabledBusChannelSum(plugin, false));
    return juce::jmax(0, outputChannels);
}

int HostedInstrumentRack::getRequiredProcessChannelCount(const juce::AudioPluginInstance& plugin)
{
    const int inputChannels = juce::jmax(
        juce::jmax(0, plugin.getTotalNumInputChannels()),
        getEnabledBusChannelSum(plugin, true));
    const int outputChannels = getConfiguredOutputChannelCount(plugin);
    return juce::jmax(2, juce::jmax(inputChannels, outputChannels));
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

    auto scoreDescription = [](const juce::PluginDescription& desc) -> int
    {
        const juce::String name = desc.name.toLowerCase();
        const bool mentionsMicrotonic = name.contains("microtonic");
        const bool mentionsMultiOut = name.contains("multi")
            || name.contains("multiout")
            || name.contains("multi-out");
        int score = 0;
        score += juce::jmax(0, desc.numOutputChannels) * 8;
        score += juce::jmax(0, desc.numInputChannels) * -1;
        score += desc.isInstrument ? 32 : 0;
        score += (mentionsMicrotonic && mentionsMultiOut) ? 48 : 0;
        score += mentionsMultiOut ? 8 : 0;
        return score;
    };

    const juce::PluginDescription* bestDescription = descriptions.getFirst();
    int bestScore = std::numeric_limits<int>::lowest();
    for (const auto* candidate : descriptions)
    {
        if (candidate == nullptr)
            continue;
        const int score = scoreDescription(*candidate);
        if (score > bestScore)
        {
            bestScore = score;
            bestDescription = candidate;
        }
    }

    if (bestDescription == nullptr)
    {
        error = "No suitable plugin description found";
        return false;
    }

    auto description = *bestDescription;
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
    processScratchBuffer.setSize(0, 0);
    return true;
}

void HostedInstrumentRack::unloadPlugin()
{
    if (instance != nullptr)
        instance->releaseResources();
    instance.reset();
    processScratchBuffer.setSize(0, 0);
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

    auto* activeInstance = instance.get();
    const int numSamples = audio.getNumSamples();
    if (numSamples <= 0)
    {
        midi.clear();
        audio.clear();
        return;
    }

    const int requiredChannels = getRequiredProcessChannelCount(*activeInstance);
    juce::AudioBuffer<float>* processBuffer = &audio;

    if (audio.getNumChannels() < requiredChannels)
    {
        if (processScratchBuffer.getNumChannels() < requiredChannels
            || processScratchBuffer.getNumSamples() < numSamples)
        {
            processScratchBuffer.setSize(requiredChannels, numSamples, false, false, true);
        }

        processScratchBuffer.clear();

        const int copyChannels = juce::jmin(audio.getNumChannels(), processScratchBuffer.getNumChannels());
        for (int ch = 0; ch < copyChannels; ++ch)
            processScratchBuffer.copyFrom(ch, 0, audio, ch, 0, numSamples);

        processBuffer = &processScratchBuffer;
    }

    activeInstance->processBlock(*processBuffer, midi);

    if (processBuffer != &audio)
    {
        const int copyChannels = juce::jmin(audio.getNumChannels(), processBuffer->getNumChannels());
        for (int ch = 0; ch < copyChannels; ++ch)
            audio.copyFrom(ch, 0, *processBuffer, ch, 0, numSamples);

        for (int ch = copyChannels; ch < audio.getNumChannels(); ++ch)
            audio.clear(ch, 0, numSamples);
    }
}

int HostedInstrumentRack::getOutputChannelCount() const
{
    if (instance == nullptr)
        return 0;
    return getConfiguredOutputChannelCount(*instance);
}
