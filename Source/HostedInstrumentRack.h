/*
  ==============================================================================

    HostedInstrumentRack.h

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class HostedInstrumentRack
{
public:
    HostedInstrumentRack();

    bool loadPlugin(const juce::File& file, double sampleRate, int blockSize, juce::String& error);
    void prepareToPlay(double sampleRate, int blockSize);
    void releaseResources();

    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi);
    int getOutputChannelCount() const;

    juce::AudioPluginInstance* getInstance() const { return instance.get(); }

private:
    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
};
