/*
  ==============================================================================

    StepHostEditor.h

  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "StepHostProcessor.h"

class StepHostAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit StepHostAudioProcessorEditor(StepHostAudioProcessor& processor);
    ~StepHostAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void handleLoadClicked();

    StepHostAudioProcessor& processor;
    juce::TextButton loadButton{ "Load Instrument" };
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
};
