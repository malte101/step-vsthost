#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

class ModernAudioEngine;

namespace PresetStore
{
juce::File getPresetDirectory();
bool savePreset(int presetIndex,
                int maxStrips,
                ModernAudioEngine* audioEngine,
                juce::AudioProcessorValueTreeState& parameters,
                const juce::File* currentStripFiles,
                const std::function<void(juce::XmlElement&)>& onBeforeWrite = {});
bool loadPreset(int presetIndex,
                int maxStrips,
                ModernAudioEngine* audioEngine,
                juce::AudioProcessorValueTreeState& parameters,
                const std::function<bool(int, const juce::File&)>& loadSampleToStrip,
                double hostPpqSnapshot,
                double hostTempoSnapshot,
                bool preservePlaybackState = false,
                const std::function<void(const juce::XmlElement&)>& onAfterLoad = {});
juce::String getPresetName(int presetIndex);
bool setPresetName(int presetIndex, const juce::String& presetName);
bool presetExists(int presetIndex);
bool deletePreset(int presetIndex);
} // namespace PresetStore
