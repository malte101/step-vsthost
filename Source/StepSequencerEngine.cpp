/*
  ==============================================================================

    StepSequencerEngine.cpp

  ==============================================================================
*/

#include "StepSequencerEngine.h"

namespace
{
constexpr double kStepLengthBeats = 0.25; // 16th note
constexpr double kNoteLengthMs = 15.0;
}

StepSequencerEngine::StepSequencerEngine()
{
    for (int s = 0; s < NumStrips; ++s)
    {
        for (int i = 0; i < MaxSteps; ++i)
        {
            stepEnabled[s][i] = false;
            velocity[s][i] = 1.0f;
            probability[s][i] = 1.0f;
            pitch[s][i] = 0.5f;
            pan[s][i] = 0.5f;
            decay[s][i] = 0.5f;
        }
        lastStepFired[s] = -1;
    }
}

void StepSequencerEngine::setPatternLength(int steps)
{
    patternLengthSteps = juce::jlimit(1, MaxSteps, steps);
}

void StepSequencerEngine::toggleStep(int strip, int step)
{
    if (strip < 0 || strip >= NumStrips)
        return;
    const int idx = juce::jlimit(0, patternLengthSteps - 1, step);
    stepEnabled[strip][idx] = !stepEnabled[strip][idx];
}

void StepSequencerEngine::setStepEnabled(int strip, int step, bool enabled)
{
    if (strip < 0 || strip >= NumStrips)
        return;
    const int idx = juce::jlimit(0, patternLengthSteps - 1, step);
    stepEnabled[strip][idx] = enabled;
}

bool StepSequencerEngine::isStepEnabled(int strip, int step) const
{
    if (strip < 0 || strip >= NumStrips)
        return false;
    const int idx = juce::jlimit(0, patternLengthSteps - 1, step);
    return stepEnabled[strip][idx];
}

void StepSequencerEngine::setLaneValue(int strip, int step, Lane lane, float value)
{
    if (strip < 0 || strip >= NumStrips)
        return;
    const int idx = juce::jlimit(0, patternLengthSteps - 1, step);
    const float v = juce::jlimit(0.0f, 1.0f, value);
    switch (lane)
    {
        case Lane::Velocity: velocity[strip][idx] = v; break;
        case Lane::Probability: probability[strip][idx] = v; break;
        case Lane::Pitch: pitch[strip][idx] = v; break;
        case Lane::Pan: pan[strip][idx] = v; break;
        case Lane::Decay: decay[strip][idx] = v; break;
        default: break;
    }
}

float StepSequencerEngine::getLaneValue(int strip, int step, Lane lane) const
{
    if (strip < 0 || strip >= NumStrips)
        return 0.0f;
    const int idx = juce::jlimit(0, patternLengthSteps - 1, step);
    switch (lane)
    {
        case Lane::Velocity: return velocity[strip][idx];
        case Lane::Probability: return probability[strip][idx];
        case Lane::Pitch: return pitch[strip][idx];
        case Lane::Pan: return pan[strip][idx];
        case Lane::Decay: return decay[strip][idx];
        default: break;
    }
    return 0.0f;
}

void StepSequencerEngine::renderMidi(juce::MidiBuffer& midi,
                                     const juce::AudioPlayHead::PositionInfo& pos,
                                     int numSamples,
                                     double sampleRate)
{
    triggeredEvents.clear();
    if (!pos.getIsPlaying())
        return;

    const auto bpm = pos.getBpm().value_or(tempoBpm);
    const double samplesPerBeat = (bpm > 1.0) ? (sampleRate * 60.0 / bpm) : (sampleRate * 60.0 / tempoBpm);
    const double ppqStart = pos.getPpqPosition().value_or(0.0);
    const double ppqEnd = ppqStart + (static_cast<double>(numSamples) / samplesPerBeat);

    const double stepBeats = kStepLengthBeats;
    const int totalSteps = patternLengthSteps;

    const int startStep = static_cast<int>(std::floor(ppqStart / stepBeats));
    const int endStep = static_cast<int>(std::floor(ppqEnd / stepBeats));

    for (int stepIndex = startStep; stepIndex <= endStep; ++stepIndex)
    {
        const int stepWithin = ((stepIndex % totalSteps) + totalSteps) % totalSteps;
        const bool isOffBeat = (stepWithin % 2) == 1;
        const double swingOffset = isOffBeat ? (swingAmount * stepBeats * 0.5) : 0.0;
        const double stepPpq = (stepIndex * stepBeats) + swingOffset;
        if (stepPpq < ppqStart || stepPpq >= ppqEnd)
            continue;

        const int sampleOffset = static_cast<int>(juce::jlimit(0.0,
            static_cast<double>(numSamples - 1),
            (stepPpq - ppqStart) * samplesPerBeat));

        currentStep = stepWithin;

        for (int strip = 0; strip < NumStrips; ++strip)
        {
            if (!stepEnabled[strip][stepWithin])
                continue;
            if (lastStepFired[strip] == stepIndex)
                continue;

            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            if (dist(rng) > probability[strip][stepWithin])
                continue;

            const int velocityVal = juce::jlimit(1, 127, static_cast<int>(velocity[strip][stepWithin] * 127.0f));
            const int note = 60;
            const int channel = strip + 1;
            midi.addEvent(juce::MidiMessage::noteOn(channel, note, (juce::uint8) velocityVal), sampleOffset);

            const int noteLengthSamples = juce::jmax(1, static_cast<int>((kNoteLengthMs / 1000.0) * sampleRate));
            const int noteOffOffset = juce::jmin(numSamples - 1, sampleOffset + noteLengthSamples);
            midi.addEvent(juce::MidiMessage::noteOff(channel, note), noteOffOffset);

            lastStepFired[strip] = stepIndex;
            TriggerEvent event;
            event.strip = strip;
            event.step = stepWithin;
            event.sampleOffset = sampleOffset;
            event.velocity = velocity[strip][stepWithin];
            triggeredEvents.push_back(event);
        }
    }
}
