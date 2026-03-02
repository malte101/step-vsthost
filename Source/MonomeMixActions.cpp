#include "MonomeMixActions.h"
#include "PluginProcessor.h"
#include "AudioEngine.h"
#include "PlayheadSpeedQuantizer.h"
#include <array>
#include <cmath>

namespace MonomeMixActions
{
namespace
{
constexpr int speedMode = static_cast<int>(StepVstHostAudioProcessor::ControlMode::Speed);
constexpr int pitchMode = static_cast<int>(StepVstHostAudioProcessor::ControlMode::Pitch);
constexpr int panMode = static_cast<int>(StepVstHostAudioProcessor::ControlMode::Pan);
constexpr int volumeMode = static_cast<int>(StepVstHostAudioProcessor::ControlMode::Volume);
constexpr int lengthMode = static_cast<int>(StepVstHostAudioProcessor::ControlMode::Length);
constexpr int swingMode = static_cast<int>(StepVstHostAudioProcessor::ControlMode::Swing);
constexpr int gateMode = static_cast<int>(StepVstHostAudioProcessor::ControlMode::Gate);
constexpr float minSliceLength = 0.02f;
constexpr float maxSliceLength = 1.0f;
constexpr float minStepDecayMs = 1.0f;
constexpr float maxStepDecayMs = 4000.0f;
constexpr float stepDecayMidpointMs = 700.0f;
constexpr std::array<int, 16> kStepLengthStepsByColumn {
    2, 3, 4, 5, 6, 8, 10, 12,
    16, 20, 24, 32, 40, 48, 56, 64
};

const std::array<int, 16> musicalPitchSemitones = {
    -24, -21, -18, -15, -12, -9, -6, -3,
      0,   3,   6,   9,  12, 15, 18, 24
};

float unitFromColumn(int x)
{
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(x) / 15.0f);
}

const juce::NormalisableRange<float>& sliceLengthRange()
{
    static const juce::NormalisableRange<float> range(minSliceLength, maxSliceLength, 0.001f, 0.5f);
    return range;
}

const juce::NormalisableRange<float>& stepDecayRange()
{
    static const juce::NormalisableRange<float> range = []
    {
        juce::NormalisableRange<float> r(minStepDecayMs, maxStepDecayMs);
        r.setSkewForCentre(stepDecayMidpointMs);
        return r;
    }();
    return range;
}

float sliceLengthFromColumn(int x)
{
    return sliceLengthRange().convertFrom0to1(unitFromColumn(juce::jlimit(0, 15, x)));
}

float stepDecayMsFromColumn(int x)
{
    return stepDecayRange().convertFrom0to1(unitFromColumn(juce::jlimit(0, 15, x)));
}

int findNearestColumn(float value, float minValue, float maxValue)
{
    const float range = juce::jmax(1.0e-6f, maxValue - minValue);
    const float t = juce::jlimit(0.0f, 1.0f, (value - minValue) / range);
    return juce::jlimit(0, 15, static_cast<int>(std::round(t * 15.0f)));
}

int findNearestSliceLengthColumn(float value)
{
    const float t = juce::jlimit(0.0f, 1.0f,
                                 sliceLengthRange().convertTo0to1(juce::jlimit(minSliceLength, maxSliceLength, value)));
    return juce::jlimit(0, 15, static_cast<int>(std::round(t * 15.0f)));
}

int findNearestStepDecayColumn(float valueMs)
{
    const float t = juce::jlimit(0.0f, 1.0f,
                                 stepDecayRange().convertTo0to1(juce::jlimit(minStepDecayMs, maxStepDecayMs, valueMs)));
    return juce::jlimit(0, 15, static_cast<int>(std::round(t * 15.0f)));
}

int findNearestSpeedColumn(float speed)
{
    return PlayheadSpeedQuantizer::nearestSpeedIndex(speed);
}

int findNearestPitchColumn(int semitones)
{
    int best = 0;
    int bestDiff = std::abs(semitones - musicalPitchSemitones[0]);

    for (int i = 1; i < 16; ++i)
    {
        const int diff = std::abs(semitones - musicalPitchSemitones[static_cast<size_t>(i)]);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            best = i;
        }
    }

    return best;
}

int stepLengthStepsFromColumn(int x)
{
    return kStepLengthStepsByColumn[static_cast<size_t>(juce::jlimit(0, 15, x))];
}

int findNearestStepLengthColumn(int steps)
{
    int bestCol = 0;
    int bestDistance = std::abs(steps - kStepLengthStepsByColumn[0]);
    for (int i = 1; i < 16; ++i)
    {
        const int dist = std::abs(steps - kStepLengthStepsByColumn[static_cast<size_t>(i)]);
        if (dist < bestDistance)
        {
            bestDistance = dist;
            bestCol = i;
        }
    }
    return bestCol;
}

float getStripParameterValue(const StepVstHostAudioProcessor& processor,
                             int stripIndex,
                             const juce::String& prefix,
                             float fallback)
{
    if (stripIndex < 0 || stripIndex >= StepVstHostAudioProcessor::MaxStrips)
        return fallback;
    if (auto* raw = processor.parameters.getRawParameterValue(prefix + juce::String(stripIndex)))
        return raw->load();
    return fallback;
}
}

void handleButtonPress(StepVstHostAudioProcessor& processor,
                       EnhancedAudioStrip& strip,
                       int stripIndex,
                       int x,
                       int mode)
{
    if (mode == speedMode)
    {
        const float speedRatio = PlayheadSpeedQuantizer::ratioFromColumn(juce::jlimit(0, 15, x));
        strip.setPlayheadSpeedRatio(speedRatio);

        if (auto* param = processor.parameters.getParameter("stripSpeed" + juce::String(stripIndex)))
            param->setValueNotifyingHost(param->convertTo0to1(speedRatio));
    }
    else if (mode == pitchMode)
    {
        const int semitones = musicalPitchSemitones[static_cast<size_t>(juce::jlimit(0, 15, x))];
        processor.applyPitchControlToStrip(strip, static_cast<float>(semitones));

        if (auto* param = processor.parameters.getParameter("stripPitch" + juce::String(stripIndex)))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(semitones)));
    }
    else if (mode == panMode)
    {
        float pan = (x - 8) / 8.0f;
        pan = juce::jlimit(-1.0f, 1.0f, pan);

        if (strip.playMode == EnhancedAudioStrip::PlayMode::Step)
        {
            if (auto* stepSampler = strip.getStepSampler())
                stepSampler->setPan(pan);
        }

        strip.setPan(pan);

        if (auto* param = processor.parameters.getParameter("stripPan" + juce::String(stripIndex)))
            param->setValueNotifyingHost((pan + 1.0f) / 2.0f);
    }
    else if (mode == volumeMode)
    {
        float vol = x / 15.0f;

        if (strip.playMode == EnhancedAudioStrip::PlayMode::Step)
        {
            if (auto* stepSampler = strip.getStepSampler())
                stepSampler->setVolume(vol);
        }

        strip.setVolume(vol);

        if (auto* param = processor.parameters.getParameter("stripVolume" + juce::String(stripIndex)))
            param->setValueNotifyingHost(vol);
    }
    else if (mode == lengthMode)
    {
        const int steps = stepLengthStepsFromColumn(x);
        strip.setStepPatternLengthSteps(steps);
    }
    else if (mode == swingMode)
    {
        strip.setSwingAmount(unitFromColumn(juce::jlimit(0, 15, x)));
    }
    else if (mode == gateMode)
    {
        const int clampedColumn = juce::jlimit(0, 15, x);
        if (strip.playMode == EnhancedAudioStrip::PlayMode::Step)
        {
            strip.setStepEnvelopeDecayMs(stepDecayMsFromColumn(clampedColumn));
        }
        else
        {
            const float sliceLength = sliceLengthFromColumn(clampedColumn);
            strip.setLoopSliceLength(sliceLength);
            if (auto* param = processor.parameters.getParameter("stripSliceLength" + juce::String(stripIndex)))
                param->setValueNotifyingHost(param->convertTo0to1(sliceLength));
        }
    }
}

void renderRow(const EnhancedAudioStrip& strip,
               const StepVstHostAudioProcessor& processor,
               int stripIndex,
               int y,
               int newLedState[16][16],
               int mode)
{
    const bool isStepMode = (strip.playMode == EnhancedAudioStrip::PlayMode::Step);
    auto* stepSampler = isStepMode ? const_cast<EnhancedAudioStrip&>(strip).getStepSampler() : nullptr;

    if (mode == speedMode)
    {
        const float speed = PlayheadSpeedQuantizer::quantizeRatio(
            strip.getPlayheadSpeedRatio());
        const int activeCol = findNearestSpeedColumn(speed);

        for (int x = 0; x < 16; ++x)
        {
            newLedState[x][y] = 4;
            if (x == 8)
                newLedState[x][y] = 6;
            if (x == activeCol)
                newLedState[x][y] = 15;
        }
    }
    else if (mode == pitchMode)
    {
        const float semitonesValue = getStripParameterValue(
            processor, stripIndex, "stripPitch", processor.getPitchSemitonesForDisplay(strip));
        const int semitones = static_cast<int>(std::round(semitonesValue));

        const int activeCol = findNearestPitchColumn(semitones);

        for (int x = 0; x < 16; ++x)
        {
            newLedState[x][y] = 4;
            if (x == 8)
                newLedState[x][y] = 6;
            if (x == activeCol)
                newLedState[x][y] = 15;
        }
    }
    else if (mode == panMode)
    {
        const float fallbackPan = (isStepMode && stepSampler) ? stepSampler->getPan() : strip.getPan();
        const float pan = getStripParameterValue(processor, stripIndex, "stripPan", fallbackPan);

        int panX = 8 + static_cast<int>(pan * 8.0f);
        panX = juce::jlimit(0, 15, panX);

        for (int x = 0; x < 16; ++x)
        {
            if (x == panX)
                newLedState[x][y] = 15;
            else if (x == 8)
                newLedState[x][y] = 6;
            else
                newLedState[x][y] = 2;
        }
    }
    else if (mode == volumeMode)
    {
        const float fallbackVol = (isStepMode && stepSampler) ? stepSampler->getVolume() : strip.getVolume();
        const float vol = getStripParameterValue(processor, stripIndex, "stripVolume", fallbackVol);

        int numLit = static_cast<int>(vol * 16.0f);
        for (int x = 0; x < 16; ++x)
            newLedState[x][y] = (x < numLit) ? 12 : 2;
    }
    else if (mode == lengthMode)
    {
        if (isStepMode)
        {
            const int activeCol = findNearestStepLengthColumn(strip.getStepPatternLengthSteps());
            for (int x = 0; x < 16; ++x)
            {
                if (x == activeCol)
                    newLedState[x][y] = 15;
                else if (x <= activeCol)
                    newLedState[x][y] = 8;
                else
                    newLedState[x][y] = 2;
            }
        }
        else
        {
            const int loopStart = juce::jlimit(0, 15, strip.getLoopStart());
            const int loopEnd = juce::jlimit(loopStart + 1, 16, strip.getLoopEnd());
            for (int x = 0; x < 16; ++x)
            {
                if (x >= loopStart && x < loopEnd)
                    newLedState[x][y] = 10;
                else
                    newLedState[x][y] = 2;
            }
            newLedState[loopStart][y] = 15;
            newLedState[juce::jmax(loopStart, loopEnd - 1)][y] = 13;
        }
    }
    else if (mode == swingMode)
    {
        const int activeCol = findNearestColumn(strip.getSwingAmount(), 0.0f, 1.0f);
        for (int x = 0; x < 16; ++x)
            newLedState[x][y] = (x <= activeCol) ? (x == activeCol ? 15 : 8) : 2;
    }
    else if (mode == gateMode)
    {
        const int activeCol = isStepMode
            ? findNearestStepDecayColumn(strip.getStepEnvelopeDecayMs())
            : findNearestSliceLengthColumn(strip.getLoopSliceLength());
        for (int x = 0; x < 16; ++x)
            newLedState[x][y] = (x <= activeCol) ? (x == activeCol ? 15 : 8) : 2;
    }
}

} // namespace MonomeMixActions
