/*
  ==============================================================================

    StepSequencerEngine.h

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <random>

class StepSequencerEngine
{
public:
    static constexpr int NumStrips = 8;
    static constexpr int MaxSteps = 64;

    enum class Lane
    {
        Velocity = 0,
        Probability,
        Pitch,
        Pan,
        Decay,
        Count
    };

    StepSequencerEngine();

    void setTempo(double bpm) { tempoBpm = juce::jmax(1.0, bpm); }
    void setSwing(float amount) { swingAmount = juce::jlimit(0.0f, 1.0f, amount); }
    void setPatternLength(int steps);

    int getPatternLength() const { return patternLengthSteps; }
    int getCurrentStep() const { return currentStep; }

    void toggleStep(int strip, int step);
    void setStepEnabled(int strip, int step, bool enabled);
    bool isStepEnabled(int strip, int step) const;

    void setLaneValue(int strip, int step, Lane lane, float value);
    float getLaneValue(int strip, int step, Lane lane) const;

    void renderMidi(juce::MidiBuffer& midi,
                    const juce::AudioPlayHead::PositionInfo& pos,
                    int numSamples,
                    double sampleRate);

    struct TriggerEvent
    {
        int strip = 0;
        int step = 0;
        int sampleOffset = 0;
        float velocity = 1.0f;
    };

    const std::vector<TriggerEvent>& getTriggeredEvents() const { return triggeredEvents; }
    void clearTriggeredEvents() { triggeredEvents.clear(); }

private:
    int patternLengthSteps = 16;
    int currentStep = 0;
    double tempoBpm = 120.0;
    float swingAmount = 0.0f;

    std::array<std::array<bool, MaxSteps>, NumStrips> stepEnabled{};
    std::array<std::array<float, MaxSteps>, NumStrips> velocity{};
    std::array<std::array<float, MaxSteps>, NumStrips> probability{};
    std::array<std::array<float, MaxSteps>, NumStrips> pitch{};
    std::array<std::array<float, MaxSteps>, NumStrips> pan{};
    std::array<std::array<float, MaxSteps>, NumStrips> decay{};

    std::array<int, NumStrips> lastStepFired{};
    std::mt19937 rng{ std::random_device{}() };
    std::vector<TriggerEvent> triggeredEvents;
};
