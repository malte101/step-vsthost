/*
  ==============================================================================

    PluginEditor.h
    Modern Comprehensive UI for mlrVST

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include "PluginProcessor.h"
#include "StepSequencerDisplay.h"

//==============================================================================
/**
 * WaveformDisplay - Shows waveform with playback position
 */
class WaveformDisplay : public juce::Component
{
public:
    WaveformDisplay();
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void setAudioBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate);
    void setPlaybackPosition(double normalizedPosition);
    void setGrainWindowOverlay(bool enabled, double windowNorm);
    void setGrainMarkerPositions(const std::array<float, 8>& positions,
                                 const std::array<float, 8>& pitchNorms);
    void setGrainHudOverlay(bool enabled,
                            const juce::String& lineA,
                            const juce::String& lineB,
                            float density,
                            float spread,
                            float emitter,
                            float pitchSemitones,
                            float arpDepth,
                            float pitchJitterSemitones);
    void setLoopPoints(int startCol, int endCol, int maxCols);
    void setSliceMarkers(const std::array<int, 16>& normalSlices,
                         const std::array<int, 16>& transientSlices,
                         int totalSamples,
                         bool transientModeActive);
    void setWaveformColor(juce::Colour color);
    bool hasLoadedAudio() const noexcept { return hasAudio; }
    void clear();
    
private:
    std::vector<float> thumbnail;
    double playbackPosition = 0.0;
    int loopStart = 0;
    int loopEnd = 16;
    int maxColumns = 16;
    bool hasAudio = false;
    juce::Colour waveformColor = juce::Colour(0xff8cb8ff);  // Brighter default blue
    std::array<int, 16> normalSliceSamples{};
    std::array<int, 16> transientSliceSamples{};
    int waveformTotalSamples = 0;
    bool transientSlicesActive = false;
    bool grainWindowOverlayEnabled = false;
    double grainWindowNorm = 0.0;
    std::array<float, 8> grainMarkerPositions {};
    std::array<float, 8> grainMarkerPitchNorms {};
    bool grainHudOverlayEnabled = false;
    juce::String grainHudLineA;
    juce::String grainHudLineB;
    float grainHudDensity = 0.0f;
    float grainHudSpread = 0.0f;
    float grainHudEmitter = 0.0f;
    float grainHudPitchSemitones = 0.0f;
    float grainHudArpDepth = 0.0f;
    float grainHudPitchJitterSemitones = 0.0f;
    
    void generateThumbnail(const juce::AudioBuffer<float>& buffer);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};

//==============================================================================
/**
 * FXStripControl - Filter controls for each strip (FX tab)
 */
class FXStripControl : public juce::Component,
                       public juce::Timer
{
public:
    FXStripControl(int idx, MlrVSTAudioProcessor& p);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void updateFromEngine();
    
private:
    int stripIndex;
    MlrVSTAudioProcessor& processor;
    juce::Colour stripColor;
    
    juce::Label stripLabel;
    juce::ToggleButton filterEnableButton;
    juce::Slider filterFreqSlider;
    juce::Slider filterResSlider;
    juce::Slider filterMorphSlider;
    juce::ComboBox filterAlgoBox;
    juce::Label filterFreqLabel;
    juce::Label filterResLabel;
    juce::Label filterMorphLabel;
    juce::Label filterAlgoLabel;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FXStripControl)
};

//==============================================================================
/**
 * StripControl - Compact horizontal strip with overlaid LED grid
 */
class StripControl : public juce::Component,
                     public juce::Timer,
                     public juce::FileDragAndDropTarget
{
public:
    StripControl(int stripIndex, MlrVSTAudioProcessor& p);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    
    void updateFromEngine();
    void setModulationLaneView(bool shouldShow);
    
    class ColoredKnobLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void setKnobColor(juce::Colour color) { knobColor = color; }
        
        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                            juce::Slider& slider) override
        {
            auto radius = juce::jmin(width / 2, height / 2) - 4.0f;
            auto centreX = x + width * 0.5f;
            auto centreY = y + height * 0.5f;
            auto rx = centreX - radius;
            auto ry = centreY - radius;
            auto rw = radius * 2.0f;
            auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            
            // Flat light body with subtle ring and a single bright pointer.
            g.setColour(juce::Colour(0xffeef3f8));
            g.fillEllipse(rx, ry, rw, rw);
            
            g.setColour(juce::Colour(0xff9eafc1));
            g.drawEllipse(rx, ry, rw, rw, 1.0f);
            
            const auto ringColour = slider.findColour(juce::Slider::rotarySliderFillColourId)
                .interpolatedWith(knobColor, 0.25f);
            g.setColour(ringColour.withAlpha(0.9f));
            g.drawEllipse(rx + 1.5f, ry + 1.5f, rw - 3.0f, rw - 3.0f, 1.0f);

            // Optional bipolar base indicator (used by pitch-like knobs).
            const auto& props = slider.getProperties();
            const bool bipolarBase = static_cast<bool>(props.getWithDefault("bipolarBase", false));
            if (bipolarBase)
            {
                const float midAngle = rotaryStartAngle + (0.5f * (rotaryEndAngle - rotaryStartAngle));
                const float rOuter = radius - 0.8f;
                const float rInner = radius - 4.2f;
                const float mx1 = centreX + (std::cos(midAngle - juce::MathConstants<float>::halfPi) * rInner);
                const float my1 = centreY + (std::sin(midAngle - juce::MathConstants<float>::halfPi) * rInner);
                const float mx2 = centreX + (std::cos(midAngle - juce::MathConstants<float>::halfPi) * rOuter);
                const float my2 = centreY + (std::sin(midAngle - juce::MathConstants<float>::halfPi) * rOuter);
                g.setColour(juce::Colour(0xff32465e).withAlpha(0.9f));
                g.drawLine(mx1, my1, mx2, my2, 1.4f);

                juce::Path bipolarArc;
                bipolarArc.addArc(rx + 2.2f, ry + 2.2f, rw - 4.4f, rw - 4.4f,
                                  juce::jmin(midAngle, angle) - juce::MathConstants<float>::halfPi,
                                  juce::jmax(midAngle, angle) - juce::MathConstants<float>::halfPi,
                                  true);
                g.setColour(ringColour.withAlpha(0.95f));
                g.strokePath(bipolarArc, juce::PathStrokeType(1.8f));
            }

            // Modulation indicator (synth-style): bipolar range around current knob position.
            const bool modActive = static_cast<bool>(props.getWithDefault("modActive", false));
            if (modActive)
            {
                const float depth = juce::jlimit(0.0f, 1.0f, static_cast<float>(props.getWithDefault("modDepth", 0.0)));
                const float signedPos = juce::jlimit(-1.0f, 1.0f, static_cast<float>(props.getWithDefault("modSigned", 0.0)));
                const auto modColour = juce::Colour(static_cast<juce::uint32>(
                    static_cast<int>(props.getWithDefault("modColour", static_cast<int>(0xffffd24a)))));

                if (depth > 0.0001f)
                {
                    const float spanNorm = depth * 0.5f; // bipolar span: +/- half range at full depth
                    const float baseNorm = juce::jlimit(0.0f, 1.0f, sliderPos);
                    const float startNorm = juce::jlimit(0.0f, 1.0f, baseNorm - spanNorm);
                    const float endNorm = juce::jlimit(0.0f, 1.0f, baseNorm + spanNorm);

                    const float startA = rotaryStartAngle + (startNorm * (rotaryEndAngle - rotaryStartAngle));
                    const float endA = rotaryStartAngle + (endNorm * (rotaryEndAngle - rotaryStartAngle));
                    const float markerNorm = juce::jlimit(0.0f, 1.0f, baseNorm + (signedPos * spanNorm));
                    const float markerA = rotaryStartAngle + (markerNorm * (rotaryEndAngle - rotaryStartAngle));

                    // Draw a dark underlay first so modulation remains visible over bright/yellow fills.
                    juce::Path arc;
                    arc.addArc(rx + 2.0f, ry + 2.0f, rw - 4.0f, rw - 4.0f,
                               startA - juce::MathConstants<float>::halfPi,
                               endA - juce::MathConstants<float>::halfPi,
                               true);
                    g.setColour(juce::Colour(0xff8a9eb7).withAlpha(0.55f));
                    g.strokePath(arc, juce::PathStrokeType(3.2f));
                    g.setColour(modColour.withAlpha(0.95f));
                    g.strokePath(arc, juce::PathStrokeType(2.1f));

                    const float markerRadius = radius - 1.5f;
                    const float mx = centreX + (std::cos(markerA - juce::MathConstants<float>::halfPi) * markerRadius);
                    const float my = centreY + (std::sin(markerA - juce::MathConstants<float>::halfPi) * markerRadius);
                    g.setColour(juce::Colour(0xff32465e).withAlpha(0.8f));
                    g.fillEllipse(mx - 3.2f, my - 3.2f, 6.4f, 6.4f);
                    g.setColour(modColour.brighter(0.45f));
                    g.fillEllipse(mx - 2.2f, my - 2.2f, 4.4f, 4.4f);
                }
            }

            // Pointer line
            juce::Path p;
            auto pointerLength = radius * 0.6f;
            auto pointerThickness = 1.5f;
            p.addRectangle(-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
            p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
            
            g.setColour(juce::Colour(0xfff2f2f2));
            g.fillPath(p);
        }

        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float minSliderPos, float maxSliderPos,
                              const juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            const auto& props = slider.getProperties();
            const bool bipolarBase = static_cast<bool>(props.getWithDefault("bipolarBase", false));

            // True bipolar rendering: center is neutral, fill extends left/right from center.
            const bool customBipolarHorizontal = bipolarBase && !(style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical);
            if (customBipolarHorizontal)
            {
                const float cx = static_cast<float>(x) + (0.5f * static_cast<float>(width));
                const float cy = static_cast<float>(y) + (0.5f * static_cast<float>(height));
                const float trackH = juce::jlimit(2.0f, 6.0f, static_cast<float>(height) * 0.26f);
                const float trackY = cy - (trackH * 0.5f);
                const float left = static_cast<float>(x);
                const float right = static_cast<float>(x + width);
                const float pos = juce::jlimit(left, right, sliderPos);

                g.setColour(slider.findColour(juce::Slider::backgroundColourId).withAlpha(0.75f));
                g.fillRoundedRectangle(left, trackY, static_cast<float>(width), trackH, trackH * 0.5f);

                const float fillX = juce::jmin(cx, pos);
                const float fillW = std::abs(pos - cx);
                if (fillW > 0.5f)
                {
                    g.setColour(slider.findColour(juce::Slider::trackColourId).withAlpha(0.95f));
                    g.fillRoundedRectangle(fillX, trackY, fillW, trackH, trackH * 0.5f);
                }

                g.setColour(juce::Colour(0xff32465e).withAlpha(0.92f));
                g.drawLine(cx, static_cast<float>(y + 2), cx, static_cast<float>(y + height - 2), 1.2f);

                g.setColour(slider.findColour(juce::Slider::thumbColourId));
                g.fillEllipse(pos - 4.0f, cy - 4.0f, 8.0f, 8.0f);
                g.setColour(juce::Colour(0xff4f6278).withAlpha(0.8f));
                g.drawEllipse(pos - 4.0f, cy - 4.0f, 8.0f, 8.0f, 1.0f);
            }
            else
            {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                                   sliderPos, minSliderPos, maxSliderPos, style, slider);
            }

            if (bipolarBase && !customBipolarHorizontal)
            {
                const float cy = static_cast<float>(y + (height / 2));
                const float centerX = static_cast<float>(x) + (0.5f * static_cast<float>(width));
                const auto baseColour = slider.findColour(juce::Slider::trackColourId).withAlpha(0.9f);
                g.setColour(baseColour);
                g.drawLine(centerX, cy, sliderPos, cy, 2.0f); // bipolar value segment from center
                g.setColour(juce::Colour(0xff32465e).withAlpha(0.9f));
                g.drawLine(centerX, static_cast<float>(y + 2), centerX, static_cast<float>(y + height - 2), 1.2f);
            }

            const bool modActive = static_cast<bool>(props.getWithDefault("modActive", false));
            if (!modActive)
                return;

            const float depth = juce::jlimit(0.0f, 1.0f, static_cast<float>(props.getWithDefault("modDepth", 0.0)));
            if (depth <= 0.0001f)
                return;

            const float signedPos = juce::jlimit(-1.0f, 1.0f, static_cast<float>(props.getWithDefault("modSigned", 0.0)));
            const auto modColour = juce::Colour(static_cast<juce::uint32>(
                static_cast<int>(props.getWithDefault("modColour", static_cast<int>(0xffffd24a)))));

            g.setColour(juce::Colour(0xff8a9eb7).withAlpha(0.6f));

            if (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical)
            {
                const float spanPx = depth * 0.5f * static_cast<float>(height);
                const float start = juce::jlimit(static_cast<float>(y), static_cast<float>(y + height), sliderPos - spanPx);
                const float end = juce::jlimit(static_cast<float>(y), static_cast<float>(y + height), sliderPos + spanPx);
                const float marker = juce::jlimit(static_cast<float>(y), static_cast<float>(y + height),
                                                  sliderPos + (signedPos * spanPx));
                const float cx = static_cast<float>(x + (width / 2));
                g.drawLine(cx, start, cx, end, 3.0f);
                g.setColour(modColour.withAlpha(0.95f));
                g.drawLine(cx, start, cx, end, 2.0f);
                g.setColour(juce::Colour(0xff32465e).withAlpha(0.82f));
                g.fillEllipse(cx - 3.0f, marker - 3.0f, 6.0f, 6.0f);
                g.setColour(modColour.brighter(0.45f));
                g.fillEllipse(cx - 2.0f, marker - 2.0f, 4.0f, 4.0f);
            }
            else
            {
                const float spanPx = depth * 0.5f * static_cast<float>(width);
                const float start = juce::jlimit(static_cast<float>(x), static_cast<float>(x + width), sliderPos - spanPx);
                const float end = juce::jlimit(static_cast<float>(x), static_cast<float>(x + width), sliderPos + spanPx);
                const float marker = juce::jlimit(static_cast<float>(x), static_cast<float>(x + width),
                                                  sliderPos + (signedPos * spanPx));
                const float cy = static_cast<float>(y + (height / 2));
                g.drawLine(start, cy, end, cy, 3.0f);
                g.setColour(modColour.withAlpha(0.95f));
                g.drawLine(start, cy, end, cy, 2.0f);
                g.setColour(juce::Colour(0xff32465e).withAlpha(0.82f));
                g.fillEllipse(marker - 3.0f, cy - 3.0f, 6.0f, 6.0f);
                g.setColour(modColour.brighter(0.45f));
                g.fillEllipse(marker - 2.0f, cy - 2.0f, 4.0f, 4.0f);
            }

        }
        
    private:
        juce::Colour knobColor{juce::Colour(0xff6b94c4)};
    };

    class DraggableNumberBox : public juce::Label
    {
    public:
        void setRange(int minimum, int maximum)
        {
            minValue = juce::jmin(minimum, maximum);
            maxValue = juce::jmax(minimum, maximum);
            setValue(currentValue, juce::dontSendNotification);
        }

        void setValue(int value, juce::NotificationType notify)
        {
            const int clamped = juce::jlimit(minValue, maxValue, value);
            currentValue = clamped;
            setText(juce::String(clamped), juce::dontSendNotification);
            if (notify != juce::dontSendNotification && onValueChange != nullptr)
                onValueChange(clamped);
        }

        int getValue() const { return currentValue; }
        bool isInteracting() const { return dragActive || isBeingEdited(); }
        std::function<void(int)> onValueChange;

        void mouseDown(const juce::MouseEvent& e) override
        {
            juce::Label::mouseDown(e);
            if (!e.mods.isLeftButtonDown() || isBeingEdited())
                return;
            dragActive = true;
            dragStartPos = e.getPosition();
            dragStartValue = currentValue;
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (!dragActive || isBeingEdited())
            {
                juce::Label::mouseDrag(e);
                return;
            }

            const int dragDelta = dragStartPos.y - e.getPosition().y;
            const int pixelsPerStep = e.mods.isShiftDown() ? 8 : 3;
            const int valueDelta = dragDelta / juce::jmax(1, pixelsPerStep);
            setValue(dragStartValue + valueDelta, juce::sendNotification);
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            dragActive = false;
            juce::Label::mouseUp(e);
        }

        void textWasEdited() override
        {
            setValue(getText().trim().getIntValue(), juce::sendNotification);
        }

    private:
        int minValue = 1;
        int maxValue = 64;
        int currentValue = 16;
        bool dragActive = false;
        juce::Point<int> dragStartPos;
        int dragStartValue = 16;
    };
    
private:
    int stripIndex;
    MlrVSTAudioProcessor& processor;
    juce::Colour stripColor;  // Track strip color for controls
    ColoredKnobLookAndFeel knobLookAndFeel;
    
    // Main display area combines waveform + LED overlay
    WaveformDisplay waveform;
    StepSequencerDisplay stepDisplay;  // Step sequencer grid display
    bool showingStepDisplay = false;   // Toggle between waveform and step display
    bool modulationLaneView = false;
    bool preModulationShowingStepDisplay = false;
    bool preModulationWaveformVisible = true;
    bool preModulationStepVisible = false;
    juce::Rectangle<int> modulationLaneBounds;
    int modulationLastDrawStep = -1;
    float modulationLastDrawValue = 0.0f;
    
    // Compact controls on the right
    juce::Slider volumeSlider;      // Compact rotary
    juce::Slider panSlider;         // Compact rotary
    juce::Slider pitchSlider;       // Compact rotary - pitch semitones
    juce::Slider speedSlider;       // Compact rotary - playhead speed
    juce::Slider scratchSlider;     // Compact rotary - scratch amount
    juce::Slider sliceLengthSlider; // Compact rotary - loop slice length
    juce::ComboBox patternLengthBox; // Step mode pattern length (16..64)
    DraggableNumberBox stepLengthReadoutBox; // Step mode draggable numeric length (1..64)
    DraggableNumberBox laneMidiChannelBox; // Step lane MIDI channel (1..16)
    DraggableNumberBox laneMidiNoteBox;    // Step lane MIDI note (0..127)
    juce::Slider stepAttackSlider;  // Step mode attack (ms)
    juce::Slider stepDecaySlider;   // Step mode decay (ms)
    juce::Slider stepReleaseSlider; // Step mode release (ms)
    juce::Label tempoLabel;         // Shows current beats setting
    juce::ComboBox recordBarsBox;   // Selects loop bars for this strip
    juce::Label volumeLabel;        // Label below knob
    juce::Label panLabel;           // Label below knob
    juce::Label pitchLabel;         // Label below knob
    juce::Label speedLabel;         // Label below knob
    juce::Label scratchLabel;       // Label below knob
    juce::Label sliceLengthLabel;   // Label below knob
    juce::Label stepAttackLabel;    // Step envelope labels
    juce::Label stepDecayLabel;
    juce::Label stepReleaseLabel;
    juce::Label laneMidiChannelLabel;
    juce::Label laneMidiNoteLabel;
    juce::ComboBox playModeBox;     // Play mode selector (step-only)
    juce::ComboBox directionModeBox; // Direction mode selector (Normal/Reverse/PingPong/Random)
    juce::Slider grainSizeSlider;
    juce::Slider grainDensitySlider;
    juce::Slider grainPitchSlider;
    juce::Slider grainPitchJitterSlider;
    juce::Slider grainSpreadSlider;
    juce::Slider grainJitterSlider;
    juce::Slider grainPositionJitterSlider;
    juce::Slider grainRandomSlider;
    juce::Slider grainArpSlider;
    juce::Slider grainCloudSlider;
    juce::Slider grainEmitterSlider;
    juce::Slider grainEnvelopeSlider;
    juce::Slider grainShapeSlider;
    juce::TextButton grainTabPitchButton;
    juce::TextButton grainTabSpaceButton;
    juce::TextButton grainTabShapeButton;
    juce::ToggleButton grainSizeSyncToggle;
    juce::Label grainSizeDivLabel;
    juce::Label grainSizeLabel;
    juce::Label grainDensityLabel;
    juce::Label grainPitchLabel;
    juce::Label grainPitchJitterLabel;
    juce::Label grainSpreadLabel;
    juce::Label grainJitterLabel;
    juce::Label grainPositionJitterLabel;
    juce::Label grainRandomLabel;
    juce::Label grainArpLabel;
    juce::Label grainCloudLabel;
    juce::Label grainEmitterLabel;
    juce::Label grainEnvelopeLabel;
    juce::Label grainShapeLabel;
    juce::Label modTargetLabel;
    juce::ComboBox modTargetBox;
    juce::ToggleButton modBipolarToggle;
    juce::Label modDepthLabel;
    juce::Slider modDepthSlider;
    juce::Label modOffsetLabel;
    juce::Slider modOffsetSlider;
    juce::Label modCurveBendLabel;
    juce::Slider modCurveBendSlider;
    juce::Label modLengthLabel;
    juce::ComboBox modLengthBox;
    std::array<juce::TextButton, ModernAudioEngine::NumModSequencers> modSequencerTabs;
    juce::ToggleButton modPitchQuantToggle;
    juce::ComboBox modPitchScaleBox;
    juce::Label modShapeLabel;
    juce::ComboBox modShapeBox;
    juce::Label modCurveTypeLabel;
    juce::ComboBox modCurveTypeBox;
    bool grainOverlayVisible = false;
    enum class GrainSubPage
    {
        Pitch = 0,
        Space,
        Shape
    };
    GrainSubPage grainSubPage = GrainSubPage::Pitch;
    juce::TextButton loadButton;    // Small
    juce::Label stripLabel;         // Small
    
    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliceLengthAttachment;
    
    void setupComponents();
    void loadSample();
    void loadSampleFromFile(const juce::File& file);
    static bool isSupportedAudioFile(const juce::File& file);
    void paintLEDOverlay(juce::Graphics& g);  // Draw LED blocks over waveform
    void paintModulationLane(juce::Graphics& g);
    juce::Rectangle<int> getModulationLaneBounds() const;
    void applyModulationPoint(juce::Point<int> p);
    int getModulationStepFromPoint(juce::Point<int> p) const;
    void applyModulationCellDuplicateFromDrag(int deltaY);
    void applyModulationCellCurveFromDrag(int deltaY, bool rampUpMode);
    void hideAllPrimaryControls();
    void hideAllGrainControls();
    void updateGrainOverlayVisibility();
    void updateGrainTabButtons();
    void updateModSequencerTabButtons();

    enum class ModTransformMode
    {
        None = 0,
        DuplicateCell,
        ShapeUpCell,
        ShapeDownCell
    };
    ModTransformMode modTransformMode = ModTransformMode::None;
    int modTransformStartY = 0;
    int modTransformStep = -1;
    int modTransformStartSubdivision = 1;
    float modTransformStartValue = 0.0f;
    float modTransformStartEndValue = 0.0f;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StripControl)
};

// Strip color utility - rainbow progression
inline juce::Colour getStripColor(int stripIndex)
{
    const juce::Colour colors[] = {
        juce::Colour(0xffd36f63), // Muted red
        juce::Colour(0xffd18f4f), // Burnt orange
        juce::Colour(0xffbda659), // Olive/yellow
        juce::Colour(0xff6faa6f), // Muted green
        juce::Colour(0xff5ea5a8), // Teal
        juce::Colour(0xff6f93c8), // Muted blue
        juce::Colour(0xff9a82bc)  // Soft violet
    };
    return colors[stripIndex % 7];
}

//==============================================================================
/**
 * MonomeGridDisplay - Interactive monome grid visualization
 */
class MonomeGridDisplay : public juce::Component,
                          public juce::Timer
{
public:
    MonomeGridDisplay(MlrVSTAudioProcessor& p);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void timerCallback() override;
    
    void updateFromEngine();
    void sendGridStateToMonome();  // Send LED state to actual hardware
    
private:
    MlrVSTAudioProcessor& processor;
    
    static constexpr int gridWidth = 16;
    static constexpr int gridHeight = 8;
    
    int ledState[gridWidth][gridHeight] = {{0}};
    bool buttonPressed[gridWidth][gridHeight] = {{false}};
    
    juce::Rectangle<int> getButtonBounds(int x, int y) const;
    void handleButtonPress(int x, int y, bool down);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonomeGridDisplay)
};

//==============================================================================
/**
 * MonomeControlPanel - Device selection and status
 */
class MonomeControlPanel : public juce::Component,
                           public juce::Timer
{
public:
    MonomeControlPanel(MlrVSTAudioProcessor& p);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    
private:
    MlrVSTAudioProcessor& processor;
    
    juce::Label titleLabel;
    juce::ComboBox deviceSelector;
    juce::TextButton refreshButton;
    juce::TextButton connectButton;
    juce::Label statusLabel;
    juce::ComboBox rotationSelector;
    juce::Label rotationLabel;
    
    void updateDeviceList();
    void connectToDevice();
    void updateStatus();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonomeControlPanel)
};

//==============================================================================
/**
 * LevelMeter - Vertical level meter display
 */
class LevelMeter : public juce::Component
{
public:
    LevelMeter();
    
    void paint(juce::Graphics& g) override;
    void setLevel(float level);  // 0.0 to 1.0
    void setPeak(float peak);    // 0.0 to 1.0
    
private:
    float currentLevel = 0.0f;
    float peakLevel = 0.0f;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

//==============================================================================
/**
 * GlobalControlPanel - Master controls
 */
class GlobalControlPanel : public juce::Component
{
public:
    GlobalControlPanel(MlrVSTAudioProcessor& p);
    ~GlobalControlPanel() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void refreshFromProcessor();
    std::function<void(bool)> onTooltipsToggled;
    
private:
    MlrVSTAudioProcessor& processor;
    
    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Slider masterVolumeSlider;
    juce::Label masterVolumeLabel;
    juce::ToggleButton limiterToggle;
    juce::ComboBox swingDivisionBox;
    juce::Label swingDivisionLabel;
    juce::ComboBox outputRoutingBox;
    juce::Label outputRoutingLabel;
    juce::ToggleButton momentaryToggle;
    juce::ToggleButton soundTouchToggle;
    juce::ToggleButton tooltipsToggle;
    juce::TextButton hostedLoadButton;
    juce::TextButton hostedShowGuiButton;
    juce::Label hostedStatusLabel;
    juce::File hostedLastPluginFile;
    std::unique_ptr<juce::DocumentWindow> hostedEditorWindow;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limiterEnabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outputRoutingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soundTouchEnabledAttachment;
    bool globalUiReady = false;

    void loadHostedPlugin();
    void openHostedPluginEditor();
    void closeHostedPluginEditor();
    void updateHostedPluginStatus();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalControlPanel)
};

//==============================================================================
/**
 * MonomePagesPanel - Control row page order and behavior
 */
class MonomePagesPanel : public juce::Component,
                         public juce::Timer
{
public:
    MonomePagesPanel(MlrVSTAudioProcessor& p);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

private:
    MlrVSTAudioProcessor& processor;

    juce::Label titleLabel;
    juce::Label modeLabel;

    struct PageRow
    {
        juce::Label positionLabel;
        juce::TextButton modeButton;
        juce::TextButton upButton;
        juce::TextButton downButton;
    };
    std::array<PageRow, MlrVSTAudioProcessor::NumControlRowPages> rows;
    juce::Label presetGridLabel;
    juce::Label presetInstructionsLabel;
    juce::Viewport presetViewport;
    juce::Component presetGridContent;
    std::array<juce::TextButton, MlrVSTAudioProcessor::MaxPresetSlots> presetButtons;

    void refreshFromProcessor();
    void updatePresetButtons();
    void layoutPresetButtons();
    void onPresetButtonClicked(int presetIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonomePagesPanel)
};

//==============================================================================
/**
 * ModulationControlPanel - Per-row modulation sequencer editor
 */
class ModulationControlPanel : public juce::Component,
                               public juce::Timer
{
public:
    ModulationControlPanel(MlrVSTAudioProcessor& p);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    enum class EditGestureMode
    {
        None = 0,
        DuplicateCell,
        ShapeUpCell,
        ShapeDownCell
    };

    MlrVSTAudioProcessor& processor;
    int selectedStrip = 0;

    juce::Label titleLabel;
    juce::Label stripLabel;
    juce::Label targetLabel;
    juce::ComboBox targetBox;
    juce::ToggleButton bipolarToggle;
    juce::Label depthLabel;
    juce::Slider depthSlider;
    juce::Label offsetLabel;
    juce::Slider offsetSlider;
    juce::Label lengthLabel;
    juce::ComboBox lengthBox;
    juce::Label pageLabel;
    juce::ComboBox pageBox;
    juce::Label smoothLabel;
    juce::Slider smoothSlider;
    juce::Label gestureHintLabel;
    juce::ToggleButton pitchScaleToggle;
    juce::Label pitchScaleLabel;
    juce::ComboBox pitchScaleBox;
    std::array<juce::TextButton, ModernAudioEngine::ModSteps> stepButtons;
    EditGestureMode gestureMode = EditGestureMode::None;
    bool gestureActive = false;
    bool suppressNextStepClick = false;
    int gestureStartY = 0;
    int gestureStep = -1;
    int gestureSourceSubdivision = 1;
    float gestureSourceValue = 0.0f;
    float gestureSourceEndValue = 0.0f;

    void refreshFromEngine();
    int stepIndexForComponent(juce::Component* c) const;
    void applyDuplicateGesture(int deltaY);
    void applyShapeGesture(int deltaY, bool rampUpMode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationControlPanel)
};

//==============================================================================
/**
 * Preset Management Panel - 16x7 preset grid
 */
class PresetControlPanel : public juce::Component
{
public:
    PresetControlPanel(MlrVSTAudioProcessor& p);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void refreshVisualState();
    
private:
    MlrVSTAudioProcessor& processor;
    
    juce::Label titleLabel;
    juce::Label instructionsLabel;
    juce::TextEditor presetNameEditor;
    juce::TextButton saveButton;
    juce::TextButton deleteButton;
    juce::TextButton exportWavButton;
    juce::Viewport presetViewport;
    juce::Component presetGridContent;
    std::array<juce::TextButton, MlrVSTAudioProcessor::MaxPresetSlots> presetButtons;
    int selectedPresetIndex = 0;
    juce::String presetNameDraft;
    juce::File lastExportDirectory;
    
    void savePresetClicked(int index, juce::String typedName = {});
    void loadPresetClicked(int index);
    void exportRecordingsAsWav();
    void updatePresetButtons();
    void layoutPresetButtons();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetControlPanel)
};

//==============================================================================
/**
 * PathsControlPanel - Per-strip default load directories for Loop/Step modes
 */
class PathsControlPanel : public juce::Component,
                          public juce::Timer
{
public:
    PathsControlPanel(MlrVSTAudioProcessor& p);
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    
private:
    MlrVSTAudioProcessor& processor;
    
    juce::Label titleLabel;
    juce::Viewport scrollViewport;
    juce::Component scrollContent;
    juce::Label headerStripLabel;
    juce::Label headerLoopLabel;
    juce::Label headerStepLabel;
    
    struct PathRow
    {
        juce::Label stripLabel;
        juce::Label loopPathLabel;
        juce::TextButton loopSetButton;
        juce::TextButton loopClearButton;
        juce::Label stepPathLabel;
        juce::TextButton stepSetButton;
        juce::TextButton stepClearButton;
    };
    std::array<PathRow, MlrVSTAudioProcessor::MaxStrips> rows;
    
    void refreshLabels();
    void chooseDirectory(int stripIndex, MlrVSTAudioProcessor::SamplePathMode mode);
    void clearDirectory(int stripIndex, MlrVSTAudioProcessor::SamplePathMode mode);
    static juce::String pathToDisplay(const juce::File& file);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PathsControlPanel)
};

//==============================================================================
/**
 * Modern mlrVST Editor - Main window
 */
class MlrVSTAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::Timer
{
public:
    MlrVSTAudioProcessorEditor(MlrVSTAudioProcessor&);
    ~MlrVSTAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class EditorLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        juce::Font getComboBoxFont(juce::ComboBox&) override
        {
            return juce::Font(juce::FontOptions(11.0f, juce::Font::bold));
        }

        juce::Font getPopupMenuFont() override
        {
            return juce::Font(juce::FontOptions(14.0f, juce::Font::bold));
        }
    };

    MlrVSTAudioProcessor& audioProcessor;
    
    // Main sections
    std::unique_ptr<MonomeGridDisplay> monomeGrid;
    std::unique_ptr<MonomeControlPanel> monomeControl;
    std::unique_ptr<GlobalControlPanel> globalControl;
    std::unique_ptr<MonomePagesPanel> monomePagesControl;
    std::unique_ptr<PresetControlPanel> presetControl;
    std::unique_ptr<PathsControlPanel> pathsControl;
    
    // Top controls in tabs (to save space)
    std::unique_ptr<juce::TabbedComponent> topTabs;
    
    // Main unified tabs: Play / FX
    std::unique_ptr<juce::TabbedComponent> mainTabs;
    
    // Strip controls (8 rows) - Play tab
    juce::OwnedArray<StripControl> stripControls;
    
    // FX strips (8 rows) - FX tab
    juce::OwnedArray<FXStripControl> fxStripControls;
    
    // Layout components
    juce::Viewport stripsViewport;
    juce::Component stripsContainer;
    
    // Look and Feel
    EditorLookAndFeel darkLookAndFeel;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    bool tooltipsEnabled = true;
    uint32_t lastPresetRefreshToken = 0;
    int activeGuiStripCount = 6;

    int getDetectedGuiStripCount() const;
    void setActiveGuiStripCount(int stripCount, bool forceRelayout);
    void createUIComponents();
    void setupLookAndFeel();
    void layoutComponents();
    void setTooltipsEnabled(bool enabled);
    
    static constexpr int windowWidth = 1000;
    static constexpr int windowHeight = 1020;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MlrVSTAudioProcessorEditor)
};
