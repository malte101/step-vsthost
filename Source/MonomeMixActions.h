#pragma once

class EnhancedAudioStrip;
class StepVstHostAudioProcessor;

namespace MonomeMixActions
{
void handleButtonPress(StepVstHostAudioProcessor& processor,
                       EnhancedAudioStrip& strip,
                       int stripIndex,
                       int x,
                       int mode);

void renderRow(const EnhancedAudioStrip& strip,
               const StepVstHostAudioProcessor& processor,
               int stripIndex,
               int y,
               int newLedState[16][16],
               int mode);
} // namespace MonomeMixActions
