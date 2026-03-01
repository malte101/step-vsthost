/*
  ==============================================================================

    StepHostEditor.cpp

  ==============================================================================
*/

#include "StepHostEditor.h"

StepHostAudioProcessorEditor::StepHostAudioProcessorEditor(StepHostAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this]() { handleLoadClicked(); };

    statusLabel.setText("No instrument loaded", juce::dontSendNotification);
    addAndMakeVisible(statusLabel);

    setSize(480, 220);
}

StepHostAudioProcessorEditor::~StepHostAudioProcessorEditor() = default;

void StepHostAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.setFont(18.0f);
    g.drawText("Step Sequencer Instrument Host", getLocalBounds().removeFromTop(40), juce::Justification::centred);
}

void StepHostAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    loadButton.setBounds(bounds.removeFromTop(40));
    bounds.removeFromTop(10);
    statusLabel.setBounds(bounds.removeFromTop(30));
}

void StepHostAudioProcessorEditor::handleLoadClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select instrument plugin",
                                                      juce::File(),
                                                      "*.vst3;*.component");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser)
                             {
                                 const auto file = chooser.getResult();
                                 if (!file.existsAsFile())
                                     return;

                                 juce::String error;
                                 const bool loaded = processor.getHostRack().loadPlugin(
                                     file, processor.getSampleRate(), processor.getBlockSize(), error);
                                 if (!loaded)
                                     statusLabel.setText("Load failed: " + error, juce::dontSendNotification);
                                 else
                                     statusLabel.setText("Loaded: " + file.getFileName(), juce::dontSendNotification);
                             });
}
