#pragma once

class StepVstHostAudioProcessor;
class ModernAudioEngine;
class EnhancedAudioStrip;

namespace MonomeFileBrowserActions
{
void handleButtonPress(StepVstHostAudioProcessor& processor, EnhancedAudioStrip& strip, int stripIndex, int x);
void handleButtonRelease(StepVstHostAudioProcessor& processor, EnhancedAudioStrip& strip, int stripIndex, int x);
void renderRow(const StepVstHostAudioProcessor& processor, const ModernAudioEngine& engine, const EnhancedAudioStrip& strip, int stripIndex, int y, int newLedState[16][16]);
} // namespace MonomeFileBrowserActions
