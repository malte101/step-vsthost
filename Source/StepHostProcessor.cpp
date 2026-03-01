/*
  ==============================================================================

    StepHostProcessor.cpp

  ==============================================================================
*/

#include "StepHostProcessor.h"
#include "StepHostEditor.h"

StepHostAudioProcessor::StepHostAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      monomeStepController(stepEngine, monomeConnection)
{
    for (int i = 0; i < StepSequencerEngine::NumStrips; ++i)
    {
        midiMapping.midiChannel[static_cast<size_t>(i)] = i + 1;
        midiMapping.triggerNote[static_cast<size_t>(i)] = 60;
    }

    monomeConnection.onKeyPress = [this](int x, int y, int state)
    {
        monomeStepController.handleGridKey(x, y, state);
    };

    monomeConnection.connect(8000);
    startTimer(100);
}

StepHostAudioProcessor::~StepHostAudioProcessor()
{
    stopTimer();
    monomeConnection.disconnect();
}

void StepHostAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostRack.prepareToPlay(sampleRate, samplesPerBlock);
}

void StepHostAudioProcessor::releaseResources()
{
    hostRack.releaseResources();
}

bool StepHostAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::stereo();
}

void StepHostAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    juce::MidiBuffer midiToHost;
    juce::AudioPlayHead::PositionInfo posInfo;
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
            posInfo = *position;
        else
            posInfo.setIsPlaying(true);
    }
    else
    {
        posInfo.setIsPlaying(true);
    }

    stepEngine.renderMidi(midiToHost, posInfo, buffer.getNumSamples(), getSampleRate());

    for (const auto& event : stepEngine.getTriggeredEvents())
        emitLaneCCs(event, midiToHost);

    hostRack.processBlock(buffer, midiToHost);
}

void StepHostAudioProcessor::emitLaneCCs(const StepSequencerEngine::TriggerEvent& event,
                                         juce::MidiBuffer& midi) const
{
    const int channel = midiMapping.midiChannel[static_cast<size_t>(event.strip)];
    const int sampleOffset = event.sampleOffset;

    const auto pitchValue = stepEngine.getLaneValue(event.strip, event.step, StepSequencerEngine::Lane::Pitch);
    if (midiMapping.pitchUsesPitchBend)
    {
        const int bend = juce::jlimit(0, 16383, static_cast<int>(pitchValue * 16383.0f));
        midi.addEvent(juce::MidiMessage::pitchWheel(channel, bend), sampleOffset);
    }
    else
    {
        const int cc = juce::jlimit(0, 127, static_cast<int>(pitchValue * 127.0f));
        midi.addEvent(juce::MidiMessage::controllerEvent(channel, midiMapping.laneToCCPitch, cc), sampleOffset);
    }

    const auto panValue = stepEngine.getLaneValue(event.strip, event.step, StepSequencerEngine::Lane::Pan);
    const int panCc = juce::jlimit(0, 127, static_cast<int>(panValue * 127.0f));
    midi.addEvent(juce::MidiMessage::controllerEvent(channel, midiMapping.laneToCCPan, panCc), sampleOffset);

    const auto decayValue = stepEngine.getLaneValue(event.strip, event.step, StepSequencerEngine::Lane::Decay);
    const int decayCc = juce::jlimit(0, 127, static_cast<int>(decayValue * 127.0f));
    midi.addEvent(juce::MidiMessage::controllerEvent(channel, midiMapping.laneToCCDecay, decayCc), sampleOffset);
}

void StepHostAudioProcessor::timerCallback()
{
    monomeStepController.renderLeds();
}

void StepHostAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream(destData, true);
    stream.writeInt(1);
}

void StepHostAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
    juce::ignoreUnused(stream);
}

juce::AudioProcessorEditor* StepHostAudioProcessor::createEditor()
{
    return new StepHostAudioProcessorEditor(*this);
}

bool StepHostAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StepHostAudioProcessor();
}
