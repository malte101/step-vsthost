/*
  ==============================================================================

    StepMonomeController.h

  ==============================================================================
*/

#pragma once

#include "StepSequencerEngine.h"
#include "MonomeConnection.h"

class StepMonomeController
{
public:
    enum class Tool
    {
        Velocity = 0,
        Divide,
        RampUp,
        RampDown,
        Probability,
        Attack,
        Decay,
        Pitch
    };

    explicit StepMonomeController(StepSequencerEngine& engineIn, MonomeConnection& monomeIn);

    void handleGridKey(int x, int y, int state);
    void renderLeds();

    int getSelectedStrip() const { return selectedStrip; }
    Tool getCurrentTool() const { return tool; }

private:
    StepSequencerEngine& engine;
    MonomeConnection& monome;

    int selectedStrip = 0;
    Tool tool = Tool::Velocity;
};
