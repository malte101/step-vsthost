/*
  ==============================================================================

    StepMonomeController.cpp

  ==============================================================================
*/

#include "StepMonomeController.h"

namespace
{
constexpr int kGridWidth = 16;
constexpr int kGridHeight = 8;

float valueFromRow(int y)
{
    const int clamped = juce::jlimit(1, 6, y);
    return 1.0f - (static_cast<float>(clamped - 1) / 5.0f);
}
}

StepMonomeController::StepMonomeController(StepSequencerEngine& engineIn, MonomeConnection& monomeIn)
    : engine(engineIn), monome(monomeIn)
{
}

void StepMonomeController::handleGridKey(int x, int y, int state)
{
    if (state == 0)
        return;

    if (y == 0)
    {
        if (x >= 0 && x <= 7)
            tool = static_cast<Tool>(x);
        else if (x >= 8 && x <= 15)
            selectedStrip = juce::jlimit(0, StepSequencerEngine::NumStrips - 1, x - 8);
        return;
    }

    if (y >= 1 && y <= 6)
    {
        const int step = juce::jlimit(0, engine.getPatternLength() - 1, x);
        switch (tool)
        {
            case Tool::Velocity:
                engine.setStepEnabled(selectedStrip, step, true);
                engine.setLaneValue(selectedStrip, step, StepSequencerEngine::Lane::Velocity, valueFromRow(y));
                break;
            case Tool::Probability:
                engine.setStepEnabled(selectedStrip, step, true);
                engine.setLaneValue(selectedStrip, step, StepSequencerEngine::Lane::Probability, valueFromRow(y));
                break;
            case Tool::Pitch:
                engine.setStepEnabled(selectedStrip, step, true);
                engine.setLaneValue(selectedStrip, step, StepSequencerEngine::Lane::Pitch, valueFromRow(y));
                break;
            case Tool::Decay:
                engine.setStepEnabled(selectedStrip, step, true);
                engine.setLaneValue(selectedStrip, step, StepSequencerEngine::Lane::Decay, valueFromRow(y));
                break;
            default:
                engine.toggleStep(selectedStrip, step);
                break;
        }
    }
}

void StepMonomeController::renderLeds()
{
    if (!monome.isConnected() || !monome.supportsGrid())
        return;

    for (int y = 0; y < kGridHeight; ++y)
    {
        for (int x = 0; x < kGridWidth; ++x)
        {
            int level = 0;
            if (y == 0)
            {
                if (x == static_cast<int>(tool))
                    level = 12;
                if (x >= 8 && x <= 15 && (x - 8) == selectedStrip)
                    level = 10;
            }
            else if (y >= 1 && y <= 6)
            {
                const int step = juce::jlimit(0, engine.getPatternLength() - 1, x);
                level = engine.isStepEnabled(selectedStrip, step) ? 8 : 0;
            }
            monome.setLEDLevel(x, y, level);
        }
    }
}
