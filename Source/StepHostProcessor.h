/*
  ==============================================================================

    StepHostProcessor.h

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "StepSequencerEngine.h"
#include "HostedInstrumentRack.h"
#include "MonomeConnection.h"
#include "StepMonomeController.h"

class StepHostAudioProcessor : public juce::AudioProcessor,
                               public juce::Timer
{
public:
    StepHostAudioProcessor();
    ~StepHostAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override { return "Step Sequencer Host"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    StepSequencerEngine& getStepEngine() { return stepEngine; }
    MonomeConnection& getMonomeConnection() { return monomeConnection; }
    HostedInstrumentRack& getHostRack() { return hostRack; }
    StepMonomeController& getMonomeStepController() { return monomeStepController; }

    struct MidiMapping
    {
        std::array<int, StepSequencerEngine::NumStrips> midiChannel{};
        std::array<int, StepSequencerEngine::NumStrips> triggerNote{};
        int laneToCCPitch = 74;
        int laneToCCPan = 10;
        int laneToCCDecay = 72;
        int laneToCCVolume = 7;
        bool pitchUsesPitchBend = false;
    };

    MidiMapping& getMidiMapping() { return midiMapping; }

private:
    void timerCallback() override;
    void emitLaneCCs(const StepSequencerEngine::TriggerEvent& event, juce::MidiBuffer& midi) const;

    StepSequencerEngine stepEngine;
    HostedInstrumentRack hostRack;

    MonomeConnection monomeConnection;
    StepMonomeController monomeStepController;

    MidiMapping midiMapping;
};
