/*
  ==============================================================================

    PluginEditor.cpp
    Modern Comprehensive UI Implementation

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PlayheadSpeedQuantizer.h"
#include <cmath>
#include <limits>
#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
const auto kBgTop = juce::Colour(0xfff5f8fc);
const auto kBgBottom = juce::Colour(0xffe9eff7);
const auto kPanelTop = juce::Colour(0xfffdfefe);
const auto kPanelBottom = juce::Colour(0xfff0f4f9);
const auto kPanelStroke = juce::Colour(0xffb5c3d2);
const auto kPanelInnerStroke = juce::Colour(0xffd7e0ea);
const auto kAccent = juce::Colour(0xff4f84c4);
const auto kTextPrimary = juce::Colour(0xff233347);
const auto kTextSecondary = juce::Colour(0xff4f6278);
const auto kTextMuted = juce::Colour(0xff78879a);
const auto kSurfaceDark = juce::Colour(0xffd7e0ea);

constexpr int kBeatSpaceCategoryCount = StepVstHostAudioProcessor::BeatSpaceChannels;
constexpr int kBeatSpaceActionNavCoarse = 101;
constexpr int kBeatSpaceActionNavFine = 102;
constexpr int kBeatSpaceActionZoneLock0 = 120;
constexpr int kBeatSpaceActionZoneLock25 = 121;
constexpr int kBeatSpaceActionZoneLock50 = 122;
constexpr int kBeatSpaceActionZoneLock75 = 123;
constexpr int kBeatSpaceActionZoneLock100 = 124;
constexpr int kBeatSpaceActionRandomZone = 140;
constexpr int kBeatSpaceActionRandomNear = 141;
constexpr int kBeatSpaceActionRandomCharacter = 142;
constexpr int kBeatSpaceActionRandomFull = 143;
constexpr int kBeatSpaceActionNearest8 = 150;
constexpr int kBeatSpaceActionPathAdd = 160;
constexpr int kBeatSpaceActionPathStartQuarter = 161;
constexpr int kBeatSpaceActionPathStartBar = 162;
constexpr int kBeatSpaceActionPathStop = 163;
constexpr int kBeatSpaceActionPathClear = 164;
constexpr int kBeatSpaceActionBookmarkAdd = 180;
constexpr int kBeatSpaceActionBookmarkClear = 181;
constexpr int kBeatSpaceActionBookmarkRecallBase = 200;
constexpr int kBeatSpaceBookmarkSlots = 8;
constexpr int kBeatSpaceMenuSlotBase = 1000;
constexpr int kBeatSpaceMenuNearestBase = 2000;
constexpr int kBeatSpaceMenuManageRenameBase = 3000;
constexpr int kBeatSpaceMenuManageMoveUpBase = 4000;
constexpr int kBeatSpaceMenuManageMoveDownBase = 5000;
constexpr int kBeatSpaceMenuManageDeleteBase = 6000;
constexpr int kBeatSpaceMenuManageReset = 7001;

void drawPanel(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour accent, float radius = 8.0f)
{
    g.setColour(juce::Colours::black.withAlpha(0.08f));
    g.fillRoundedRectangle(bounds.translated(0.0f, 1.5f), radius);

    juce::ColourGradient fill(kPanelTop, bounds.getX(), bounds.getY(),
                              kPanelBottom, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(bounds, radius);

    juce::ColourGradient topSheen(juce::Colours::white.withAlpha(0.18f), bounds.getX(), bounds.getY(),
                                  juce::Colours::transparentWhite, bounds.getX(), bounds.getY() + (bounds.getHeight() * 0.33f), false);
    g.setGradientFill(topSheen);
    g.fillRoundedRectangle(bounds.reduced(1.0f), juce::jmax(2.0f, radius - 1.0f));

    g.setColour(kPanelStroke);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);

    g.setColour(accent.withAlpha(0.22f));
    g.drawRoundedRectangle(bounds.reduced(1.5f), juce::jmax(2.0f, radius - 1.5f), 1.0f);

    g.setColour(kPanelInnerStroke);
    g.drawRoundedRectangle(bounds.reduced(2.0f), juce::jmax(2.0f, radius - 2.0f), 1.0f);
}

void enableAltClickReset(juce::Slider& slider, double defaultValue)
{
    // JUCE supports modifier-click reset when a double-click return value is set.
    slider.setDoubleClickReturnValue(true, defaultValue);
}

void styleUiButton(juce::Button& button, bool primary = false)
{
    button.setColour(juce::TextButton::buttonColourId,
                     primary ? kAccent.withAlpha(0.93f) : juce::Colour(0xffedf3fa));
    button.setColour(juce::TextButton::buttonOnColourId,
                     primary ? kAccent.brighter(0.15f) : juce::Colour(0xffdee9f4));
    button.setColour(juce::TextButton::textColourOffId,
                     primary ? juce::Colour(0xfff7fbff) : kTextPrimary);
    button.setColour(juce::TextButton::textColourOnId,
                     primary ? juce::Colour(0xfff7fbff) : kTextPrimary);
}

void styleUiCombo(juce::ComboBox& combo)
{
    combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xfff3f7fc));
    combo.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xffb7c5d5));
    combo.setColour(juce::ComboBox::textColourId, kTextPrimary);
    combo.setColour(juce::ComboBox::arrowColourId, kTextSecondary);
}

class HostedPluginEditorWindow final : public juce::DocumentWindow
{
public:
    explicit HostedPluginEditorWindow(juce::String title, std::function<void()> onCloseFn)
        : juce::DocumentWindow(std::move(title),
                               juce::Colour(0xffe7edf4),
                               juce::DocumentWindow::closeButton),
          onClose(std::move(onCloseFn))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
    }

    void closeButtonPressed() override
    {
        if (onClose != nullptr)
            onClose();
    }

private:
    std::function<void()> onClose;
};

juce::String getGrainArpModeName(int mode)
{
    switch (juce::jlimit(0, 5, mode))
    {
        case 0: return "Octave";
        case 1: return "Power";
        case 2: return "Zigzag";
        case 3: return "Major";
        case 4: return "Minor";
        case 5: return "Penta";
        default: break;
    }
    return "Octave";
}

juce::String getPlayheadSpeedLabel(float ratio)
{
    return juce::String(PlayheadSpeedQuantizer::labelForRatio(ratio));
}

float getPlayheadSpeedRatioForStrip(const EnhancedAudioStrip& strip)
{
    return PlayheadSpeedQuantizer::quantizeRatio(strip.getPlayheadSpeedRatio());
}

juce::String getMonomePageDisplayName(StepVstHostAudioProcessor::ControlMode mode)
{
    switch (mode)
    {
        case StepVstHostAudioProcessor::ControlMode::Normal: return "Normal";
        case StepVstHostAudioProcessor::ControlMode::Speed: return "Speed";
        case StepVstHostAudioProcessor::ControlMode::Pitch: return "Pitch";
        case StepVstHostAudioProcessor::ControlMode::Pan: return "Pan";
        case StepVstHostAudioProcessor::ControlMode::Volume: return "Volume";
        case StepVstHostAudioProcessor::ControlMode::Length: return "Length";
        case StepVstHostAudioProcessor::ControlMode::Filter: return "Filter";
        case StepVstHostAudioProcessor::ControlMode::Swing: return "Swing";
        case StepVstHostAudioProcessor::ControlMode::Gate: return "Gate";
        case StepVstHostAudioProcessor::ControlMode::Modulation: return "Modulation";
        case StepVstHostAudioProcessor::ControlMode::BeatSpace: return "BeatSpace";
        case StepVstHostAudioProcessor::ControlMode::Preset: return "Preset Loader";
        case StepVstHostAudioProcessor::ControlMode::StepEdit: return "Step Edit";
        case StepVstHostAudioProcessor::ControlMode::GroupAssign: return "Group Assign";
        case StepVstHostAudioProcessor::ControlMode::FileBrowser: return "File Browser";
    }
    return "Normal";
}

juce::String getMonomePageShortName(StepVstHostAudioProcessor::ControlMode mode)
{
    switch (mode)
    {
        case StepVstHostAudioProcessor::ControlMode::Speed: return "SPD";
        case StepVstHostAudioProcessor::ControlMode::Pitch: return "PIT";
        case StepVstHostAudioProcessor::ControlMode::Pan: return "PAN";
        case StepVstHostAudioProcessor::ControlMode::Volume: return "VOL";
        case StepVstHostAudioProcessor::ControlMode::Length: return "LEN";
        case StepVstHostAudioProcessor::ControlMode::Filter: return "FLT";
        case StepVstHostAudioProcessor::ControlMode::Swing: return "SWG";
        case StepVstHostAudioProcessor::ControlMode::Gate: return "GATE";
        case StepVstHostAudioProcessor::ControlMode::FileBrowser: return "BRW";
        case StepVstHostAudioProcessor::ControlMode::GroupAssign: return "GRP";
        case StepVstHostAudioProcessor::ControlMode::Modulation: return "MOD";
        case StepVstHostAudioProcessor::ControlMode::BeatSpace: return "BSP";
        case StepVstHostAudioProcessor::ControlMode::Preset: return "PST";
        case StepVstHostAudioProcessor::ControlMode::StepEdit: return "STEP";
        case StepVstHostAudioProcessor::ControlMode::Normal:
        default: return "NORM";
    }
}

int modTargetToComboId(ModernAudioEngine::ModTarget target)
{
    switch (target)
    {
        case ModernAudioEngine::ModTarget::Volume: return 2;
        case ModernAudioEngine::ModTarget::Pan: return 3;
        case ModernAudioEngine::ModTarget::Pitch: return 4;
        case ModernAudioEngine::ModTarget::Speed: return 5;
        case ModernAudioEngine::ModTarget::Cutoff: return 6;
        case ModernAudioEngine::ModTarget::Resonance: return 7;
        case ModernAudioEngine::ModTarget::GrainSize: return 8;
        case ModernAudioEngine::ModTarget::GrainDensity: return 9;
        case ModernAudioEngine::ModTarget::GrainPitch: return 10;
        case ModernAudioEngine::ModTarget::GrainPitchJitter: return 11;
        case ModernAudioEngine::ModTarget::GrainSpread: return 12;
        case ModernAudioEngine::ModTarget::GrainJitter: return 13;
        case ModernAudioEngine::ModTarget::GrainRandom: return 14;
        case ModernAudioEngine::ModTarget::GrainArp: return 15;
        case ModernAudioEngine::ModTarget::GrainCloud: return 16;
        case ModernAudioEngine::ModTarget::GrainEmitter: return 17;
        case ModernAudioEngine::ModTarget::GrainEnvelope: return 18;
        case ModernAudioEngine::ModTarget::Retrigger: return 19;
        case ModernAudioEngine::ModTarget::None:
        default: return 1;
    }
}

ModernAudioEngine::ModTarget comboIdToModTarget(int id)
{
    switch (id)
    {
        case 2: return ModernAudioEngine::ModTarget::Volume;
        case 3: return ModernAudioEngine::ModTarget::Pan;
        case 4: return ModernAudioEngine::ModTarget::Pitch;
        case 5: return ModernAudioEngine::ModTarget::Speed;
        case 6: return ModernAudioEngine::ModTarget::Cutoff;
        case 7: return ModernAudioEngine::ModTarget::Resonance;
        case 8: return ModernAudioEngine::ModTarget::GrainSize;
        case 9: return ModernAudioEngine::ModTarget::GrainDensity;
        case 10: return ModernAudioEngine::ModTarget::GrainPitch;
        case 11: return ModernAudioEngine::ModTarget::GrainPitchJitter;
        case 12: return ModernAudioEngine::ModTarget::GrainSpread;
        case 13: return ModernAudioEngine::ModTarget::GrainJitter;
        case 14: return ModernAudioEngine::ModTarget::GrainRandom;
        case 15: return ModernAudioEngine::ModTarget::GrainArp;
        case 16: return ModernAudioEngine::ModTarget::GrainCloud;
        case 17: return ModernAudioEngine::ModTarget::GrainEmitter;
        case 18: return ModernAudioEngine::ModTarget::GrainEnvelope;
        case 19: return ModernAudioEngine::ModTarget::Retrigger;
        case 1:
        default: return ModernAudioEngine::ModTarget::None;
    }
}

bool modTargetAllowsBipolar(ModernAudioEngine::ModTarget target)
{
    return ModernAudioEngine::modTargetSupportsBipolar(target);
}

enum class StepCellModifierGesture
{
    None = 0,
    Divide,
    RampUp,
    RampDown
};

StepCellModifierGesture getStepCellModifierGesture(const juce::ModifierKeys& mods)
{
    // Keep modifier priority aligned with StepSequencerDisplay cell editing.
    if (mods.isCommandDown())
        return StepCellModifierGesture::Divide;
    if (mods.isCtrlDown())
        return StepCellModifierGesture::RampUp;
    if (mods.isAltDown())
        return StepCellModifierGesture::RampDown;
    return StepCellModifierGesture::None;
}

float shapeCurvePhaseUi(float phase01, float bend, ModernAudioEngine::ModCurveShape shape);

float sampleModSubdivisionValueUi(float startValue,
                                  float endValue,
                                  int subdivisions,
                                  float phase01)
{
    const float start = juce::jlimit(0.0f, 1.0f, startValue);
    const float end = juce::jlimit(0.0f, 1.0f, endValue);
    const int subdiv = juce::jlimit(1, ModernAudioEngine::ModMaxStepSubdivisions, subdivisions);
    const float phase = juce::jlimit(0.0f, 0.999999f, phase01);

    if (subdiv <= 1)
        return start;

    const float subdivPos = phase * static_cast<float>(subdiv);
    const int subdivIndex = juce::jlimit(0, subdiv - 1, static_cast<int>(std::floor(subdivPos)));
    const float t = static_cast<float>(subdivIndex) / static_cast<float>(juce::jmax(1, subdiv - 1));
    return juce::jlimit(0.0f, 1.0f, start + ((end - start) * t));
}

void computeSingleModCellRamp(float sourceStart,
                              float sourceEnd,
                              int deltaY,
                              bool rampUpMode,
                              float& outStart,
                              float& outEnd)
{
    const float clampedStart = juce::jlimit(0.0f, 1.0f, sourceStart);
    const float clampedEnd = juce::jlimit(0.0f, 1.0f, sourceEnd);
    float peak = juce::jlimit(0.0f, 1.0f, juce::jmax(clampedStart, clampedEnd));
    if (peak < 0.001f)
        peak = 1.0f;

    const float depth = juce::jlimit(0.0f, 1.0f, 0.5f + (static_cast<float>(-deltaY) / 160.0f));
    const float low = juce::jlimit(0.0f, 1.0f, peak * (1.0f - depth));

    if (rampUpMode)
    {
        outStart = low;
        outEnd = peak;
    }
    else
    {
        outStart = peak;
        outEnd = low;
    }
}

int pitchScaleToComboId(ModernAudioEngine::PitchScale scale)
{
    switch (scale)
    {
        case ModernAudioEngine::PitchScale::Chromatic: return 1;
        case ModernAudioEngine::PitchScale::Major: return 2;
        case ModernAudioEngine::PitchScale::Minor: return 3;
        case ModernAudioEngine::PitchScale::Dorian: return 4;
        case ModernAudioEngine::PitchScale::PentatonicMinor: return 5;
        default: return 1;
    }
}

ModernAudioEngine::PitchScale comboIdToPitchScale(int id)
{
    switch (id)
    {
        case 2: return ModernAudioEngine::PitchScale::Major;
        case 3: return ModernAudioEngine::PitchScale::Minor;
        case 4: return ModernAudioEngine::PitchScale::Dorian;
        case 5: return ModernAudioEngine::PitchScale::PentatonicMinor;
        case 1:
        default: return ModernAudioEngine::PitchScale::Chromatic;
    }
}

int curveShapeToComboId(ModernAudioEngine::ModCurveShape shape)
{
    switch (shape)
    {
        case ModernAudioEngine::ModCurveShape::Linear: return 1;
        case ModernAudioEngine::ModCurveShape::ExponentialUp: return 2;
        case ModernAudioEngine::ModCurveShape::ExponentialDown: return 3;
        case ModernAudioEngine::ModCurveShape::Sine: return 4;
        case ModernAudioEngine::ModCurveShape::Square: return 5;
        default: return 1;
    }
}

ModernAudioEngine::ModCurveShape comboIdToCurveShape(int id)
{
    switch (id)
    {
        case 2: return ModernAudioEngine::ModCurveShape::ExponentialUp;
        case 3: return ModernAudioEngine::ModCurveShape::ExponentialDown;
        case 4: return ModernAudioEngine::ModCurveShape::Sine;
        case 5: return ModernAudioEngine::ModCurveShape::Square;
        case 1:
        default: return ModernAudioEngine::ModCurveShape::Linear;
    }
}

float shapeCurvePhaseUi(float phase01, float bend, ModernAudioEngine::ModCurveShape shape)
{
    const float t = juce::jlimit(0.0f, 1.0f, phase01);
    const float b = juce::jlimit(-1.0f, 1.0f, bend);
    const float amount = std::abs(b);

    switch (shape)
    {
        case ModernAudioEngine::ModCurveShape::Linear:
            return t;
        case ModernAudioEngine::ModCurveShape::ExponentialUp:
        {
            const float exp = 1.0f + (15.0f * amount);
            return std::pow(t, exp);
        }
        case ModernAudioEngine::ModCurveShape::ExponentialDown:
        {
            const float exp = 1.0f + (15.0f * amount);
            return 1.0f - std::pow(1.0f - t, exp);
        }
        case ModernAudioEngine::ModCurveShape::Sine:
        {
            const float phase = juce::jlimit(0.0f, 1.0f, t + (b * 0.45f));
            return 0.5f - (0.5f * std::cos(phase * juce::MathConstants<float>::pi));
        }
        case ModernAudioEngine::ModCurveShape::Square:
        {
            const float duty = juce::jlimit(0.02f, 0.98f, 0.5f + (b * 0.45f));
            return (t >= duty) ? 1.0f : 0.0f;
        }
        default:
            return t;
    }
}

float shapeSubdivisionBendPhaseUi(float phase01, float bend)
{
    const float t = juce::jlimit(0.0f, 1.0f, phase01);
    const float b = juce::jlimit(-1.0f, 1.0f, bend);
    const float amount = std::abs(b);
    const float exp = 1.0f + (18.0f * amount);
    return b >= 0.0f ? std::pow(t, exp) : (1.0f - std::pow(1.0f - t, exp));
}

}

//==============================================================================
// WaveformDisplay Implementation
//==============================================================================

WaveformDisplay::WaveformDisplay()
{
    setOpaque(false);
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    
    // Safety check for invalid bounds
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0 || 
        !std::isfinite(bounds.getWidth()) || !std::isfinite(bounds.getHeight()))
        return;
    
    // Background with depth so grain overlays read clearly.
    juce::ColourGradient bgGrad(kSurfaceDark.brighter(0.12f), bounds.getX(), bounds.getY(),
                                kSurfaceDark.darker(0.22f), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(kPanelStroke.withAlpha(0.85f));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    
    if (!hasAudio)
    {
        // Keep the gradient look, but tint it with the strip color so empty strips
        // feel connected to their lane identity.
        const auto tint = waveformColor.withAlpha(0.18f);
        juce::ColourGradient emptyGrad(
            kSurfaceDark.brighter(0.16f).interpolatedWith(tint.brighter(0.45f), 0.26f),
            bounds.getX(), bounds.getY(),
            kSurfaceDark.darker(0.24f).interpolatedWith(tint.darker(0.35f), 0.22f),
            bounds.getRight(), bounds.getBottom(),
            false);
        g.setGradientFill(emptyGrad);
        g.fillRoundedRectangle(bounds.reduced(0.5f), 4.0f);

        // "No Sample" text (like reference image)
        g.setColour(kTextMuted);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText("No Sample", bounds, juce::Justification::centred);
        return;
    }

    const juce::Colour grainAccent = waveformColor.interpolatedWith(kAccent, 0.35f)
                                       .withMultipliedSaturation(1.1f)
                                       .withMultipliedBrightness(1.08f);
    
    // Draw waveform
    if (!thumbnail.empty())
    {
        juce::Path waveformPath;
        auto width = bounds.getWidth();
        auto height = bounds.getHeight();
        auto centerY = height * 0.5f;
        
        waveformPath.startNewSubPath(0, centerY);
        
        for (size_t i = 0; i < thumbnail.size(); ++i)
        {
            auto x = (i / static_cast<float>(thumbnail.size())) * width;
            auto y = centerY - (thumbnail[i] * centerY * 0.9f);
            
            // Safety check for valid coordinates
            if (std::isfinite(x) && std::isfinite(y))
                waveformPath.lineTo(x, y);
        }
        
        // Mirror bottom half
        for (int i = static_cast<int>(thumbnail.size()) - 1; i >= 0; --i)
        {
            auto x = (i / static_cast<float>(thumbnail.size())) * width;
            auto y = centerY + (thumbnail[static_cast<size_t>(i)] * centerY * 0.9f);
            
            // Safety check for valid coordinates
            if (std::isfinite(x) && std::isfinite(y))
                waveformPath.lineTo(x, y);
        }
        
        waveformPath.closeSubPath();
        
        // Fill waveform with custom color
        g.setColour(waveformColor.withAlpha(0.5f));
        g.fillPath(waveformPath);
        
        // Outline
        g.setColour(waveformColor.brighter(0.2f));
        g.strokePath(waveformPath, juce::PathStrokeType(1.35f));
    }
    
    // Draw loop points with matching waveform color
    if (maxColumns > 0)
    {
        auto loopStartX = (loopStart / static_cast<float>(maxColumns)) * bounds.getWidth();
        auto loopEndX = (loopEnd / static_cast<float>(maxColumns)) * bounds.getWidth();
        auto rectWidth = loopEndX - loopStartX;
        auto rectHeight = bounds.getHeight();
        
        // Strict safety check - JUCE requires positive, finite dimensions
        if (std::isfinite(loopStartX) && std::isfinite(loopEndX) && 
            std::isfinite(rectWidth) && std::isfinite(rectHeight) &&
            rectWidth > 0.0f && rectHeight > 0.0f &&
            loopStartX >= 0.0f && loopStartX < bounds.getWidth())
        {
            // Fill with transparent waveform color
            g.setColour(waveformColor.withAlpha(0.25f));
            g.fillRect(loopStartX, 0.0f, rectWidth, rectHeight);
            
            // Draw loop markers with semi-transparent waveform color
            g.setColour(waveformColor.withAlpha(0.95f));
            g.drawLine(loopStartX, 0.0f, loopStartX, rectHeight, 2.0f);
            g.drawLine(loopEndX, 0.0f, loopEndX, rectHeight, 2.0f);
        }
    }
    
    // Draw playback position with matching waveform color (darker)
    if (std::isfinite(playbackPosition) && playbackPosition >= 0.0 && playbackPosition <= 1.0)
    {
        auto playX = playbackPosition * bounds.getWidth();
        if (std::isfinite(playX))
        {
            if (grainWindowOverlayEnabled && grainWindowNorm > 0.0)
            {
                const auto winW = juce::jlimit(1.0f,
                                               bounds.getWidth(),
                                               static_cast<float>(grainWindowNorm * bounds.getWidth()));
                auto x0 = static_cast<float>(playX) - (winW * 0.5f);
                x0 = juce::jlimit(0.0f, bounds.getWidth() - winW, x0);
                auto windowRect = juce::Rectangle<float>(x0, 0.0f, winW, bounds.getHeight()).reduced(0.0f, 1.0f);
                juce::ColourGradient winGrad(grainAccent.withAlpha(0.08f), windowRect.getX(), windowRect.getY(),
                                             grainAccent.withAlpha(0.24f), windowRect.getCentreX(), windowRect.getCentreY(), true);
                g.setGradientFill(winGrad);
                g.fillRoundedRectangle(windowRect, 2.5f);
                g.setColour(grainAccent.withAlpha(0.42f));
                g.drawRoundedRectangle(windowRect, 2.5f, 1.0f);
            }

            g.setColour(grainAccent.withAlpha(0.2f));
            g.drawLine(static_cast<float>(playX), 0.0f, static_cast<float>(playX),
                       static_cast<float>(bounds.getHeight()), 7.0f);
            g.setColour(grainAccent.withAlpha(0.98f));
            g.drawLine(static_cast<float>(playX), 0.0f, static_cast<float>(playX),
                       static_cast<float>(bounds.getHeight()), 2.0f);
            g.fillEllipse(static_cast<float>(playX) - 2.6f, 1.0f, 5.2f, 5.2f);
        }
    }

    // Draw slice markers overlay for active mode only.
    if (waveformTotalSamples > 0)
    {
        const auto drawSliceSet = [&](const std::array<int, 16>& slices, juce::Colour colour, float thickness)
        {
            g.setColour(colour);
            for (int i = 0; i < 16; ++i)
            {
                const float norm = juce::jlimit(0.0f, 1.0f,
                                                static_cast<float>(slices[static_cast<size_t>(i)])
                                                / static_cast<float>(juce::jmax(1, waveformTotalSamples - 1)));
                const float x = norm * bounds.getWidth();
                if (std::isfinite(x))
                    g.drawLine(x, 0.0f, x, bounds.getHeight(), thickness);
            }
        };

        const auto markerColor = waveformColor.withAlpha(transientSlicesActive ? 0.95f : 0.7f);
        if (transientSlicesActive)
            drawSliceSet(transientSliceSamples, markerColor, 1.7f);
        else
            drawSliceSet(normalSliceSamples, markerColor, 1.2f);
    }

    // Draw column dividers
    g.setColour(juce::Colour(0xff4a4a4a).withAlpha(grainWindowOverlayEnabled ? 0.55f : 1.0f));
    for (int i = 1; i < maxColumns; ++i)
    {
        auto x = (i / static_cast<float>(maxColumns)) * bounds.getWidth();
        if (std::isfinite(x))
            g.drawLine(x, 0, x, bounds.getHeight(), 0.5f);
    }

    if (grainWindowOverlayEnabled)
    {
        g.setColour(grainAccent.withAlpha(0.22f));
        int markerIdx = 0;
        const float markerHalfHeight = 6.0f;
        const float markerRadius = 3.2f;
        const float markerGlowRadius = 6.4f;
        const float edgePad = juce::jmax(markerHalfHeight, markerGlowRadius) + 1.0f;
        const float maxPitchTravel = juce::jmax(1.0f, (bounds.getHeight() * 0.5f) - edgePad);
        for (const float marker : grainMarkerPositions)
        {
            if (marker < 0.0f || marker > 1.0f || !std::isfinite(marker))
            {
                ++markerIdx;
                continue;
            }
            const float x = marker * bounds.getWidth();
            float pitchNorm = juce::jlimit(-1.0f, 1.0f, grainHudPitchSemitones / 48.0f);
            if (markerIdx >= 0 && markerIdx < static_cast<int>(grainMarkerPitchNorms.size()))
            {
                const float markerPitchNorm = grainMarkerPitchNorms[static_cast<size_t>(markerIdx)];
                if (std::isfinite(markerPitchNorm))
                    pitchNorm = juce::jlimit(-1.0f, 1.0f, markerPitchNorm);
            }
            const float jitterNorm = juce::jlimit(0.0f, 1.0f, grainHudPitchJitterSemitones / 48.0f);
            const float phase = static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.0025);
            const float yBase = (bounds.getHeight() * 0.5f) - (pitchNorm * maxPitchTravel);
            const float yJitter = std::sin((static_cast<float>(markerIdx) * 1.3f) + phase)
                * (grainHudArpDepth * 0.08f + jitterNorm * 0.12f) * bounds.getHeight();
            const float yCenter = juce::jlimit(edgePad, bounds.getHeight() - edgePad, yBase + yJitter);
            g.drawLine(x, yCenter - markerHalfHeight, x, yCenter + markerHalfHeight, 2.4f);
            g.setColour(grainAccent.withAlpha(0.84f));
            g.fillEllipse(x - markerRadius, yCenter - markerRadius, markerRadius * 2.0f, markerRadius * 2.0f);
            g.setColour(grainAccent.withAlpha(0.26f));
            g.fillEllipse(x - markerGlowRadius, yCenter - markerGlowRadius, markerGlowRadius * 2.0f, markerGlowRadius * 2.0f);
            g.setColour(grainAccent.withAlpha(0.22f));
            ++markerIdx;
        }
    }

    if (grainHudOverlayEnabled)
    {
        auto hud = bounds.reduced(6.0f);
        auto hudW = juce::jlimit(150.0f, bounds.getWidth() - 8.0f, bounds.getWidth() * 0.56f);
        auto hudH = juce::jlimit(22.0f, bounds.getHeight() - 8.0f, bounds.getHeight() * 0.45f);
        auto hudRect = juce::Rectangle<float>(hud.getRight() - hudW, hud.getY() + 2.0f, hudW, hudH);
        g.setColour(juce::Colour(0xff121212).withAlpha(0.72f));
        g.fillRoundedRectangle(hudRect, 3.0f);
        g.setColour(grainAccent.withAlpha(0.4f));
        g.drawRoundedRectangle(hudRect, 3.0f, 0.9f);

        auto textRect = hudRect.reduced(5.0f, 2.5f);
        g.setColour(kTextSecondary.withAlpha(0.95f));
        g.setFont(juce::Font(juce::FontOptions(8.4f, juce::Font::bold)));
        g.drawText(grainHudLineA, textRect.removeFromTop(8.8f), juce::Justification::left, false);
        g.setColour(kTextMuted.withAlpha(0.98f));
        g.setFont(juce::Font(juce::FontOptions(7.8f)));
        g.drawText(grainHudLineB, textRect.removeFromTop(8.5f), juce::Justification::left, false);

        auto bars = hudRect.removeFromBottom(5.0f).reduced(5.0f, 0.0f);
        auto drawHudBar = [&](float value, juce::Colour c)
        {
            const float clamped = juce::jlimit(0.0f, 1.0f, value);
            auto slot = bars.removeFromLeft((bars.getWidth() / 3.0f) - 1.0f);
            g.setColour(juce::Colours::black.withAlpha(0.3f));
            g.fillRoundedRectangle(slot, 1.4f);
            g.setColour(c.withAlpha(0.85f));
            g.fillRoundedRectangle(slot.withWidth(slot.getWidth() * clamped), 1.4f);
            bars.removeFromLeft(1.0f);
        };
        drawHudBar(grainHudDensity, waveformColor.withMultipliedBrightness(1.1f));
        drawHudBar(grainHudSpread, grainAccent.withMultipliedBrightness(1.05f));
        drawHudBar(grainHudEmitter, grainAccent.brighter(0.22f));
    }
}

void WaveformDisplay::resized()
{
}

void WaveformDisplay::setAudioBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    (void) sampleRate;
    hasAudio = buffer.getNumSamples() > 0;
    if (!hasAudio)
    {
        clear();
        return;
    }

    generateThumbnail(buffer);
    repaint();
}

void WaveformDisplay::generateThumbnail(const juce::AudioBuffer<float>& buffer)
{
    const int thumbnailSize = 512;
    thumbnail.clear();
    thumbnail.resize(static_cast<size_t>(thumbnailSize), 0.0f);
    
    auto numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;
    
    auto samplesPerPixel = numSamples / thumbnailSize;
    
    for (int i = 0; i < thumbnailSize; ++i)
    {
        float maxVal = 0.0f;
        auto startSample = i * samplesPerPixel;
        auto endSample = juce::jmin((i + 1) * samplesPerPixel, numSamples);
        
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* channelData = buffer.getReadPointer(ch);
            for (int s = startSample; s < endSample; ++s)
            {
                maxVal = juce::jmax(maxVal, std::abs(channelData[s]));
            }
        }
        
        thumbnail[static_cast<size_t>(i)] = maxVal;
    }
}

void WaveformDisplay::setPlaybackPosition(double normalizedPosition)
{
    // Validate input to prevent NaN/Inf
    if (std::isfinite(normalizedPosition))
        playbackPosition = juce::jlimit(0.0, 1.0, normalizedPosition);
    else
        playbackPosition = 0.0;
    
    repaint();
}

void WaveformDisplay::setGrainWindowOverlay(bool enabled, double windowNorm)
{
    grainWindowOverlayEnabled = enabled;
    grainWindowNorm = juce::jlimit(0.0, 1.0, std::isfinite(windowNorm) ? windowNorm : 0.0);
    repaint();
}

void WaveformDisplay::setGrainMarkerPositions(const std::array<float, 8>& positions,
                                              const std::array<float, 8>& pitchNorms)
{
    grainMarkerPositions = positions;
    grainMarkerPitchNorms = pitchNorms;
    repaint();
}

void WaveformDisplay::setGrainHudOverlay(bool enabled,
                                         const juce::String& lineA,
                                         const juce::String& lineB,
                                         float density,
                                         float spread,
                                         float emitter,
                                         float pitchSemitones,
                                         float arpDepth,
                                         float pitchJitterSemitones)
{
    grainHudOverlayEnabled = enabled;
    grainHudLineA = lineA;
    grainHudLineB = lineB;
    grainHudDensity = juce::jlimit(0.0f, 1.0f, density);
    grainHudSpread = juce::jlimit(0.0f, 1.0f, spread);
    grainHudEmitter = juce::jlimit(0.0f, 1.0f, emitter);
    grainHudPitchSemitones = juce::jlimit(-48.0f, 48.0f, pitchSemitones);
    grainHudArpDepth = juce::jlimit(0.0f, 1.0f, arpDepth);
    grainHudPitchJitterSemitones = juce::jlimit(0.0f, 48.0f, pitchJitterSemitones);
    repaint();
}

void WaveformDisplay::setLoopPoints(int startCol, int endCol, int cols)
{
    loopStart = startCol;
    loopEnd = endCol;
    maxColumns = cols;
    repaint();
}

void WaveformDisplay::setSliceMarkers(const std::array<int, 16>& normalSlices,
                                      const std::array<int, 16>& transientSlices,
                                      int totalSamples,
                                      bool transientModeActive)
{
    normalSliceSamples = normalSlices;
    transientSliceSamples = transientSlices;
    waveformTotalSamples = juce::jmax(0, totalSamples);
    transientSlicesActive = transientModeActive;
    repaint();
}

void WaveformDisplay::clear()
{
    hasAudio = false;
    thumbnail.clear();
    playbackPosition = 0.0;
    waveformTotalSamples = 0;
    normalSliceSamples.fill(0);
    transientSliceSamples.fill(0);
    grainWindowOverlayEnabled = false;
    grainWindowNorm = 0.0;
    grainMarkerPositions.fill(-1.0f);
    grainMarkerPitchNorms.fill(0.0f);
    grainHudOverlayEnabled = false;
    grainHudLineA.clear();
    grainHudLineB.clear();
    grainHudDensity = 0.0f;
    grainHudSpread = 0.0f;
    grainHudEmitter = 0.0f;
    grainHudPitchSemitones = 0.0f;
    grainHudArpDepth = 0.0f;
    grainHudPitchJitterSemitones = 0.0f;
    repaint();
}

void WaveformDisplay::setWaveformColor(juce::Colour color)
{
    waveformColor = color;
    repaint();
}

//==============================================================================
// LevelMeter Implementation
//==============================================================================

LevelMeter::LevelMeter()
{
    setOpaque(false);
}

void LevelMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    
    // Background
    g.setColour(kSurfaceDark);
    g.fillRoundedRectangle(bounds, 2.0f);
    
    // Border
    g.setColour(kPanelStroke);
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
    
    // Level bar
    if (currentLevel > 0.0f)
    {
        float barHeight = bounds.getHeight() * currentLevel;
        auto barBounds = bounds.removeFromBottom(barHeight).reduced(2.0f);
        
        // Color based on level (green -> yellow -> red)
        juce::Colour barColor;
        if (currentLevel < 0.7f)
            barColor = juce::Colour(0xff6eb676);
        else if (currentLevel < 0.9f)
            barColor = juce::Colour(0xffd3b35c);
        else
            barColor = juce::Colour(0xffd46b62);
        
        g.setColour(barColor);
        g.fillRoundedRectangle(barBounds, 1.0f);
    }
    
    // Peak indicator (small line at peak level)
    if (peakLevel > 0.0f)
    {
        float peakY = bounds.getBottom() - (bounds.getHeight() * peakLevel);
        g.setColour(kTextPrimary);
        g.drawLine(bounds.getX() + 2, peakY, bounds.getRight() - 2, peakY, 1.0f);
    }
}

void LevelMeter::setLevel(float level)
{
    currentLevel = juce::jlimit(0.0f, 1.0f, level);
    
    // Update peak with decay
    if (currentLevel > peakLevel)
        peakLevel = currentLevel;
    else
        peakLevel *= 0.95f;  // Slow decay
    
    repaint();
}

void LevelMeter::setPeak(float peak)
{
    peakLevel = juce::jlimit(0.0f, 1.0f, peak);
    repaint();
}

//==============================================================================
// StripControl Implementation
//==============================================================================

//==============================================================================
// StripControl - Compact horizontal layout with LED overlay
//==============================================================================

StripControl::StripControl(int idx, StepVstHostAudioProcessor& p)
    : stripIndex(idx), processor(p), waveform()
{
    setupComponents();
    startTimer(30);
}

void StripControl::setupComponents()
{
    // Track palette uses muted tones closer to Ableton's default session colors.
    const juce::Colour trackColors[] = {
        juce::Colour(0xffd36f63),
        juce::Colour(0xffd18f4f),
        juce::Colour(0xffbda659),
        juce::Colour(0xff6faa6f),
        juce::Colour(0xff5ea5a8),
        juce::Colour(0xff6f93c8),
        juce::Colour(0xff9a82bc)
    };

    stripColor = trackColors[juce::jmax(0, stripIndex) % 7];
    
    // Setup colored knob look and feel
    knobLookAndFeel.setKnobColor(stripColor);
    
    // Strip label with colored text
    stripLabel.setText("S" + juce::String(stripIndex + 1), juce::dontSendNotification);
    stripLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    stripLabel.setJustificationType(juce::Justification::centredLeft);
    stripLabel.setColour(juce::Label::textColourId, stripColor);
    addAndMakeVisible(stripLabel);
    
    // Waveform display with rainbow color
    waveform.setWaveformColor(stripColor.withMultipliedSaturation(1.35f).withMultipliedBrightness(1.25f));
    addAndMakeVisible(waveform);
    
    // Step sequencer display with matching rainbow color
    stepDisplay.setStripColor(stripColor);
    stepDisplay.onStepSet = [this](int stepIndex, bool enabled)
    {
        if (auto* engine = processor.getAudioEngine())
        {
            if (auto* strip = engine->getStrip(stripIndex))
            {
                const int totalSteps = strip->getStepTotalSteps();
                if (stepIndex >= 0 && stepIndex < totalSteps)
                    strip->setStepEnabledAtIndex(stepIndex, enabled, true);
            }
        }
    };
    stepDisplay.onStepSubdivisionSet = [this](int stepIndex, int subdivisions)
    {
        if (auto* engine = processor.getAudioEngine())
        {
            if (auto* strip = engine->getStrip(stripIndex))
                strip->setStepSubdivisionAtIndex(stepIndex, subdivisions);
        }
    };
    stepDisplay.onStepVelocityRangeSet = [this](int stepIndex, float startVelocity, float endVelocity)
    {
        if (auto* engine = processor.getAudioEngine())
        {
            if (auto* strip = engine->getStrip(stripIndex))
                strip->setStepSubdivisionVelocityRangeAtIndex(stepIndex, startVelocity, endVelocity);
        }
    };
    stepDisplay.onStepProbabilitySet = [this](int stepIndex, float probability)
    {
        if (auto* engine = processor.getAudioEngine())
        {
            if (auto* strip = engine->getStrip(stripIndex))
                strip->setStepProbabilityAtIndex(stepIndex, probability);
        }
    };
    addChildComponent(stepDisplay);  // Hidden initially
    
    // Load button - compact
    loadButton.setButtonText("Load");
    loadButton.onClick = [this]() { loadSample(); };
    loadButton.setTooltip("Open BeatSpace preset picker for this lane category (lanes 1-6).");
    styleUiButton(loadButton);
    addAndMakeVisible(loadButton);

    muteButton.setButtonText("Mute");
    muteButton.setClickingTogglesState(true);
    muteButton.setTooltip("Mute this strip.");
    muteButton.onClick = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            if (auto* strip = engine->getStrip(stripIndex))
                strip->setMuted(muteButton.getToggleState());
    };
    styleUiButton(muteButton);
    muteButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xfff7e2e0));
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd75749));
    muteButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff7d2d26));
    muteButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfff9fdff));
    addAndMakeVisible(muteButton);

    soloButton.setButtonText("Solo");
    soloButton.setClickingTogglesState(true);
    soloButton.setTooltip("Solo this strip.");
    soloButton.onClick = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            if (auto* strip = engine->getStrip(stripIndex))
                strip->setSolo(soloButton.getToggleState());
    };
    styleUiButton(soloButton);
    soloButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xfff6ecd6));
    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffc28f2d));
    soloButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff664b19));
    soloButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfff9fdff));
    addAndMakeVisible(soloButton);

    beatSpaceCategoryLabel.setText("CAT", juce::dontSendNotification);
    beatSpaceCategoryLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    beatSpaceCategoryLabel.setJustificationType(juce::Justification::centredLeft);
    beatSpaceCategoryLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(beatSpaceCategoryLabel);

    rebuildBeatSpaceCategoryMenu();
    beatSpaceCategoryBox.setTooltip("BeatSpace category + workflow actions for this strip.");
    beatSpaceCategoryBox.onChange = [this]()
    {
        if (suppressBeatSpaceCategoryAction)
            return;
        if (stripIndex < 0 || stripIndex >= StepVstHostAudioProcessor::BeatSpaceChannels)
            return;
        const int selected = beatSpaceCategoryBox.getSelectedId();
        if (selected <= 0)
            return;
        if (selected <= kBeatSpaceCategoryCount)
        {
            processor.setBeatSpaceChannelSpaceAssignment(stripIndex, selected - 1);
            return;
        }

        handleBeatSpaceCategoryAction(selected);
        suppressBeatSpaceCategoryAction = true;
        beatSpaceCategoryBox.setSelectedId(
            processor.getBeatSpaceChannelSpaceAssignment(stripIndex) + 1,
            juce::dontSendNotification);
        suppressBeatSpaceCategoryAction = false;
    };
    styleUiCombo(beatSpaceCategoryBox);
    addAndMakeVisible(beatSpaceCategoryBox);

    // Direction mode selector - NEW!
    directionModeBox.addItem("Normal", 1);
    directionModeBox.addItem("Reverse", 2);
    directionModeBox.addItem("Ping-Pong", 3);
    directionModeBox.addItem("Random", 4);
    directionModeBox.addItem("Rnd Walk", 5);
    directionModeBox.addItem("Rnd Slice", 6);
    directionModeBox.setJustificationType(juce::Justification::centredLeft);
    directionModeBox.setSelectedId(1);  // Default Normal
    directionModeBox.setTooltip("Playback direction behavior.");
    directionModeBox.onChange = [this]()
    {
        if (!processor.getAudioEngine()) return;
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
        {
            int modeId = directionModeBox.getSelectedId() - 1;
            strip->setDirectionMode(static_cast<EnhancedAudioStrip::DirectionMode>(modeId));
            
            DBG("Strip " << stripIndex << " direction changed to " << modeId);
        }
    };
    addAndMakeVisible(directionModeBox);
    
    // Compact rotary controls with colored look
    volumeSlider.setLookAndFeel(&knobLookAndFeel);
    volumeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(1.0);
    enableAltClickReset(volumeSlider, 1.0);
    volumeSlider.setPopupDisplayEnabled(true, false, this);
    addAndMakeVisible(volumeSlider);
    
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripVolume" + juce::String(stripIndex), volumeSlider);
    
    panSlider.setLookAndFeel(&knobLookAndFeel);
    panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setValue(0.0);
    enableAltClickReset(panSlider, 0.0);
    panSlider.setPopupDisplayEnabled(true, false, this);
    addAndMakeVisible(panSlider);
    
    panAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripPan" + juce::String(stripIndex), panSlider);

    pitchSlider.setLookAndFeel(&knobLookAndFeel);
    pitchSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pitchSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pitchSlider.setRange(-24.0, 24.0, 0.01);
    pitchSlider.setValue(0.0);
    enableAltClickReset(pitchSlider, 0.0);
    pitchSlider.setPopupDisplayEnabled(true, false, this);
    pitchSlider.setTextValueSuffix(" st");
    pitchSlider.setTooltip("Pitch offset in semitones.");
    addAndMakeVisible(pitchSlider);

    pitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripPitch" + juce::String(stripIndex), pitchSlider);

    speedSlider.setLookAndFeel(&knobLookAndFeel);
    speedSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    speedSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    speedSlider.setRange(0.0, 8.0, 0.001);
    speedSlider.setValue(1.0);
    enableAltClickReset(speedSlider, 1.0);
    speedSlider.textFromValueFunction = [](double value)
    {
        return getPlayheadSpeedLabel(PlayheadSpeedQuantizer::quantizeRatio(static_cast<float>(value)));
    };
    speedSlider.setPopupDisplayEnabled(true, false, this);
    speedSlider.setTooltip("Playhead speed (slice traversal only).");
    speedSlider.onValueChange = [this]()
    {
        const float quantizedRatio = PlayheadSpeedQuantizer::quantizeRatio(static_cast<float>(speedSlider.getValue()));
        if (std::abs(quantizedRatio - static_cast<float>(speedSlider.getValue())) > 1.0e-4f)
        {
            speedSlider.setValue(quantizedRatio, juce::sendNotificationSync);
            return;
        }

        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setPlayheadSpeedRatio(quantizedRatio);
    };
    addAndMakeVisible(speedSlider);
    
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripSpeed" + juce::String(stripIndex), speedSlider);
    
    // Scratch amount
    scratchSlider.setLookAndFeel(&knobLookAndFeel);
    scratchSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    scratchSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    scratchSlider.setRange(0.0, 100.0, 1.0);
    scratchSlider.setValue(0.0);
    enableAltClickReset(scratchSlider, 0.0);
    scratchSlider.textFromValueFunction = [this](double value)
    {
        const double clamped = juce::jlimit(0.0, 100.0, value);
        if (clamped <= 0.0001)
            return juce::String("0.00 s");

        double beats = 0.25;
        if (clamped <= 10.0)
        {
            const double t = clamped / 10.0;
            beats = 0.02 + (std::pow(t, 1.6) * 0.08);
        }
        else
        {
            const double t = (clamped - 10.0) / 90.0;
            beats = 0.10 + (std::pow(t, 1.8) * 7.90);
        }

        double tempo = 120.0;
        if (auto* engine = processor.getAudioEngine())
            tempo = juce::jmax(1.0, engine->getCurrentTempo());
        const double seconds = beats * (60.0 / tempo);

        return juce::String(seconds, 2) + " s";
    };
    scratchSlider.setPopupDisplayEnabled(true, false, this);
    scratchSlider.setTooltip("Scratch amount. Higher values increase scratch gesture duration.");
    scratchSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setScratchAmount(static_cast<float>(scratchSlider.getValue()));
    };
    addAndMakeVisible(scratchSlider);

    sliceLengthSlider.setLookAndFeel(&knobLookAndFeel);
    sliceLengthSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    sliceLengthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sliceLengthSlider.setRange(0.0, 1.0, 0.001);
    sliceLengthSlider.setValue(1.0);
    enableAltClickReset(sliceLengthSlider, 1.0);
    sliceLengthSlider.textFromValueFunction = [](double value)
    {
        const int percent = static_cast<int>(std::round(juce::jlimit(0.0, 1.0, value) * 100.0));
        return juce::String(percent) + "%";
    };
    sliceLengthSlider.setPopupDisplayEnabled(true, false, this);
    sliceLengthSlider.setTooltip("Loop segment length. 100% = full segment, lower values add click-free gaps.");
    addAndMakeVisible(sliceLengthSlider);
    sliceLengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripSliceLength" + juce::String(stripIndex), sliceLengthSlider);

    auto setupGrainKnob = [this](juce::Slider& slider, juce::Label& label, const char* text,
                                 double min, double max, double step)
    {
        slider.setLookAndFeel(&knobLookAndFeel);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(min, max, step);
        enableAltClickReset(slider, juce::jlimit(min, max, 0.5 * (min + max)));
        slider.setPopupDisplayEnabled(true, false, this);
        addAndMakeVisible(slider);
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(9.2f, juce::Font::bold)));
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, stripColor.brighter(0.35f));
        addAndMakeVisible(label);
    };

    setupGrainKnob(grainSizeSlider, grainSizeLabel, "SIZE", 5.0, 2400.0, 1.0);
    setupGrainKnob(grainDensitySlider, grainDensityLabel, "DENS", 0.05, 0.9, 0.01);
    setupGrainKnob(grainPitchSlider, grainPitchLabel, "PITCH", -48.0, 48.0, 0.1);
    grainPitchSlider.getProperties().set("bipolarBase", true);
    grainPitchLabel.setJustificationType(juce::Justification::centredLeft);
    setupGrainKnob(grainPitchJitterSlider, grainPitchJitterLabel, "PJIT", 0.0, 48.0, 0.1);
    setupGrainKnob(grainSpreadSlider, grainSpreadLabel, "SPRD", 0.0, 1.0, 0.01);
    setupGrainKnob(grainJitterSlider, grainJitterLabel, "SJTR", 0.0, 1.0, 0.01);
    setupGrainKnob(grainPositionJitterSlider, grainPositionJitterLabel, "POSJ", 0.0, 1.0, 0.01);
    setupGrainKnob(grainRandomSlider, grainRandomLabel, "RAND", 0.0, 1.0, 0.01);
    setupGrainKnob(grainArpSlider, grainArpLabel, "ARP", 0.0, 1.0, 0.01);
    setupGrainKnob(grainCloudSlider, grainCloudLabel, "CLOUD", 0.0, 1.0, 0.01);
    setupGrainKnob(grainEmitterSlider, grainEmitterLabel, "EMIT", 0.0, 1.0, 0.01);
    setupGrainKnob(grainEnvelopeSlider, grainEnvelopeLabel, "ENV", 0.0, 1.0, 0.01);
    setupGrainKnob(grainShapeSlider, grainShapeLabel, "SHAPE", -1.0, 1.0, 0.01);
    enableAltClickReset(grainSizeSlider, 1240.0);
    enableAltClickReset(grainDensitySlider, 0.05);
    enableAltClickReset(grainPitchSlider, 0.0);
    enableAltClickReset(grainPitchJitterSlider, 0.0);
    enableAltClickReset(grainSpreadSlider, 0.0);
    enableAltClickReset(grainJitterSlider, 0.0);
    enableAltClickReset(grainPositionJitterSlider, 0.0);
    enableAltClickReset(grainRandomSlider, 0.0);
    enableAltClickReset(grainArpSlider, 0.0);
    enableAltClickReset(grainCloudSlider, 0.0);
    enableAltClickReset(grainEmitterSlider, 0.0);
    enableAltClickReset(grainEnvelopeSlider, 0.0);
    enableAltClickReset(grainShapeSlider, 0.0);
    auto setupMini = [](juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    };
    setupMini(grainPitchSlider);
    setupMini(grainPitchJitterSlider);
    setupMini(grainSpreadSlider);
    setupMini(grainJitterSlider);
    setupMini(grainPositionJitterSlider);
    setupMini(grainRandomSlider);
    setupMini(grainArpSlider);
    setupMini(grainCloudSlider);
    setupMini(grainEmitterSlider);
    setupMini(grainEnvelopeSlider);
    setupMini(grainShapeSlider);
    grainPitchSlider.textFromValueFunction = [this](double value)
    {
        const bool arpActive = grainArpSlider.getValue() > 0.001;
        const juce::String prefix = arpActive ? "Range " : "Pitch ";
        return prefix + juce::String(value, 1) + " st";
    };
    grainSizeSlider.textFromValueFunction = [this](double value)
    {
        static constexpr std::array<const char*, 13> sizeDivisionLabels {
            "1/64", "1/48", "1/32", "1/24", "1/16", "1/12", "1/8", "1/6", "1/4", "1/3", "1/2", "1", "2"
        };
        const bool syncEnabled = [this]()
        {
            if (auto* engine = processor.getAudioEngine())
                if (auto* strip = engine->getStrip(stripIndex))
                    return strip->isGrainTempoSyncEnabled();
            return grainSizeSyncToggle.getToggleState();
        }();

        if (!syncEnabled)
            return juce::String(value, 1) + " ms (FREE)";

        const double t = juce::jlimit(0.0, 1.0, (value - 5.0) / (2400.0 - 5.0));
        const int idx = juce::jlimit(0, static_cast<int>(sizeDivisionLabels.size()) - 1,
                                     static_cast<int>(std::round(t * static_cast<double>(sizeDivisionLabels.size() - 1))));
        return juce::String(sizeDivisionLabels[static_cast<size_t>(idx)]);
    };
    grainArpSlider.textFromValueFunction = [](double value)
    {
        if (value <= 0.001)
            return juce::String("Off");
        const int mode = juce::jlimit(0, 5, static_cast<int>(std::floor(juce::jlimit(0.0, 0.999999, value) * 6.0)));
        return getGrainArpModeName(mode);
    };
    grainJitterSlider.textFromValueFunction = [](double value)
    {
        const int percent = static_cast<int>(std::round(juce::jlimit(0.0, 1.0, value) * 100.0));
        return juce::String(percent) + "% size jitter";
    };
    grainPositionJitterSlider.textFromValueFunction = [this](double value)
    {
        const int percent = static_cast<int>(std::round(juce::jlimit(0.0, 1.0, value) * 100.0));
        bool syncEnabled = grainSizeSyncToggle.getToggleState();
        if (auto* engine = processor.getAudioEngine())
            if (auto* strip = engine->getStrip(stripIndex))
                syncEnabled = strip->isGrainTempoSyncEnabled();
        return juce::String(percent) + (syncEnabled ? "% pos jitter (sync)" : "% pos jitter");
    };
    grainRandomSlider.textFromValueFunction = [](double value)
    {
        const int percent = static_cast<int>(std::round(juce::jlimit(0.0, 1.0, value) * 100.0));
        return juce::String(percent) + "% macro rand";
    };
    grainEnvelopeSlider.textFromValueFunction = [](double value)
    {
        const int percent = static_cast<int>(std::round(juce::jlimit(0.0, 1.0, value) * 100.0));
        return juce::String(percent) + "% Fade";
    };
    grainShapeSlider.textFromValueFunction = [](double value)
    {
        const int percent = static_cast<int>(std::round(juce::jlimit(-1.0, 1.0, value) * 100.0));
        return juce::String(percent) + "% Shape";
    };
    grainPositionJitterSlider.setTooltip("POSJ: grain center position jitter. Quantized to sync grid when Grain SYNC is enabled.");
    grainRandomSlider.setTooltip("RAND: macro random depth (pitch, size, reverse, spray), independent of POSJ.");

    grainSizeSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainSizeMs(static_cast<float>(grainSizeSlider.getValue()));
    };
    grainDensitySlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainDensity(static_cast<float>(grainDensitySlider.getValue()));
    };
    grainPitchSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
        {
            float value = static_cast<float>(grainPitchSlider.getValue());
            if (strip->getGrainArpDepth() > 0.001f)
            {
                value = std::abs(value);
                if (std::abs(static_cast<float>(grainPitchSlider.getValue()) - value) > 1.0e-4f)
                    grainPitchSlider.setValue(value, juce::dontSendNotification);
            }
            strip->setGrainPitch(value);
        }
    };
    grainPitchJitterSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainPitchJitter(static_cast<float>(grainPitchJitterSlider.getValue()));
    };
    grainSpreadSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainSpread(static_cast<float>(grainSpreadSlider.getValue()));
    };
    grainJitterSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainJitter(static_cast<float>(grainJitterSlider.getValue()));
    };
    grainPositionJitterSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainPositionJitter(static_cast<float>(grainPositionJitterSlider.getValue()));
    };
    grainRandomSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainRandomDepth(static_cast<float>(grainRandomSlider.getValue()));
    };
    grainArpSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
        {
            strip->setGrainArpDepth(static_cast<float>(grainArpSlider.getValue()));
            if (grainArpSlider.getValue() > 0.001)
            {
                const int mode = juce::jlimit(0, 5, static_cast<int>(std::floor(juce::jlimit(0.0, 0.999999, grainArpSlider.getValue()) * 6.0)));
                strip->setGrainArpMode(mode);
            }
        }
    };
    grainCloudSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainCloudDepth(static_cast<float>(grainCloudSlider.getValue()));
    };
    grainEmitterSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainEmitterDepth(static_cast<float>(grainEmitterSlider.getValue()));
    };
    grainEnvelopeSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainEnvelope(static_cast<float>(grainEnvelopeSlider.getValue()));
    };
    grainShapeSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainShape(static_cast<float>(grainShapeSlider.getValue()));
    };

    grainSizeSyncToggle.setButtonText("");
    grainSizeSyncToggle.setClickingTogglesState(true);
    grainSizeSyncToggle.setToggleState(true, juce::dontSendNotification);
    grainSizeSyncToggle.setColour(juce::ToggleButton::textColourId, stripColor.withAlpha(0.72f));
    grainSizeSyncToggle.setColour(juce::ToggleButton::tickColourId, stripColor.withAlpha(0.72f));
    grainSizeSyncToggle.setColour(juce::ToggleButton::tickDisabledColourId, stripColor.withAlpha(0.28f));
    grainSizeSyncToggle.setTooltip("Tempo-sync grain size.");
    grainSizeSyncToggle.onClick = [this]()
    {
        const bool enabled = grainSizeSyncToggle.getToggleState();
        grainSizeDivLabel.setText(enabled ? "SYNC" : "FREE", juce::dontSendNotification);
        grainSizeSyncToggle.setColour(juce::ToggleButton::textColourId, enabled ? stripColor.brighter(0.35f) : stripColor.withAlpha(0.72f));
        grainSizeSyncToggle.setColour(juce::ToggleButton::tickColourId, enabled ? stripColor.brighter(0.35f) : stripColor.withAlpha(0.72f));
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setGrainTempoSyncEnabled(enabled);
    };
    addAndMakeVisible(grainSizeSyncToggle);

    grainSizeDivLabel.setText("SYNC", juce::dontSendNotification);
    grainSizeDivLabel.setJustificationType(juce::Justification::centredRight);
    grainSizeDivLabel.setColour(juce::Label::textColourId, stripColor.withAlpha(0.78f));
    grainSizeDivLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    addAndMakeVisible(grainSizeDivLabel);
    grainSizeLabel.setJustificationType(juce::Justification::centredLeft);

    auto setupGrainTab = [this](juce::TextButton& button, const juce::String& text, GrainSubPage page)
    {
        button.setButtonText(text);
        button.setClickingTogglesState(false);
        button.setTooltip("Grain page: " + text);
        styleUiButton(button, false);
        button.onClick = [this, page]()
        {
            grainSubPage = page;
            updateGrainTabButtons();
            updateGrainOverlayVisibility();
            resized();
            repaint();
        };
        addAndMakeVisible(button);
    };
    setupGrainTab(grainTabPitchButton, "PITCH", GrainSubPage::Pitch);
    setupGrainTab(grainTabSpaceButton, "SPACE", GrainSubPage::Space);
    setupGrainTab(grainTabShapeButton, "SHAPE", GrainSubPage::Shape);
    updateGrainTabButtons();

    patternLengthBox.addItem("16", 1);
    patternLengthBox.addItem("32", 2);
    patternLengthBox.addItem("48", 3);
    patternLengthBox.addItem("64", 4);
    patternLengthBox.setJustificationType(juce::Justification::centred);
    patternLengthBox.setTextWhenNothingSelected("");
    patternLengthBox.setTextWhenNoChoicesAvailable("");
    patternLengthBox.setColour(juce::ComboBox::textColourId, juce::Colours::transparentWhite);
    patternLengthBox.setSelectedId(1, juce::dontSendNotification);
    patternLengthBox.setTooltip("Step pattern length");
    patternLengthBox.onChange = [this]()
    {
        const int bars = juce::jmax(1, patternLengthBox.getSelectedId());
        const int steps = juce::jlimit(2, 64, bars * 16);
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setStepPatternBars(bars);
        stepLengthReadoutBox.setValue(steps, juce::dontSendNotification);
    };
    addAndMakeVisible(patternLengthBox);

    stepLengthReadoutBox.setRange(2, 64);
    stepLengthReadoutBox.setEditable(false, true, false);
    stepLengthReadoutBox.setJustificationType(juce::Justification::centred);
    stepLengthReadoutBox.setInterceptsMouseClicks(true, false);
    stepLengthReadoutBox.setColour(juce::Label::backgroundColourId, juce::Colour(0xfff3f7fc));
    stepLengthReadoutBox.setColour(juce::Label::textColourId, kTextPrimary);
    stepLengthReadoutBox.setColour(juce::Label::outlineColourId, juce::Colour(0xffb7c5d5));
    stepLengthReadoutBox.setColour(juce::TextEditor::focusedOutlineColourId, stripColor.withAlpha(0.9f));
    stepLengthReadoutBox.setTooltip("Step pattern length (2..64). Drag or double-click to type.");
    stepLengthReadoutBox.setValue(16, juce::dontSendNotification);
    stepLengthReadoutBox.onValueChange = [this](int steps)
    {
        const int clampedSteps = juce::jlimit(2, 64, steps);
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
            strip->setStepPatternLengthSteps(clampedSteps);

        if (clampedSteps % 16 == 0)
            patternLengthBox.setSelectedId(juce::jlimit(1, 4, clampedSteps / 16), juce::dontSendNotification);
        else
        {
            patternLengthBox.setSelectedId(0, juce::dontSendNotification);
            patternLengthBox.setText(juce::String(clampedSteps), juce::dontSendNotification);
        }
    };
    addAndMakeVisible(stepLengthReadoutBox);

    auto setupLaneMidiNumberBox = [this](DraggableNumberBox& box, int minValue, int maxValue, const juce::String& tooltip)
    {
        box.setRange(minValue, maxValue);
        box.setEditable(false, true, false);
        box.setJustificationType(juce::Justification::centred);
        box.setInterceptsMouseClicks(true, false);
        box.setColour(juce::Label::backgroundColourId, juce::Colour(0xfff3f7fc));
        box.setColour(juce::Label::textColourId, kTextPrimary);
        box.setColour(juce::Label::outlineColourId, juce::Colour(0xffb7c5d5));
        box.setColour(juce::TextEditor::focusedOutlineColourId, stripColor.withAlpha(0.9f));
        box.setTooltip(tooltip);
        addAndMakeVisible(box);
    };

    laneMidiChannelLabel.setText("CH", juce::dontSendNotification);
    laneMidiChannelLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    laneMidiChannelLabel.setJustificationType(juce::Justification::centredLeft);
    laneMidiChannelLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(laneMidiChannelLabel);

    laneMidiNoteLabel.setText("NOTE", juce::dontSendNotification);
    laneMidiNoteLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    laneMidiNoteLabel.setJustificationType(juce::Justification::centredLeft);
    laneMidiNoteLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(laneMidiNoteLabel);

    setupLaneMidiNumberBox(laneMidiChannelBox, 1, 16,
                           "Hosted MIDI channel for this step lane (1..16). Drag vertically or double-click to type.");
    setupLaneMidiNumberBox(laneMidiNoteBox, 0, 127,
                           "Hosted MIDI note for this step lane (0..127). Drag vertically or double-click to type.");

    laneMidiChannelBox.setValue(processor.getLaneMidiChannel(stripIndex), juce::dontSendNotification);
    laneMidiNoteBox.setValue(processor.getLaneMidiNote(stripIndex), juce::dontSendNotification);
    laneMidiChannelBox.onValueChange = [this](int channel)
    {
        processor.setLaneMidiChannel(stripIndex, channel);
    };
    laneMidiNoteBox.onValueChange = [this](int note)
    {
        processor.setLaneMidiNote(stripIndex, note);
    };

    auto setupStepEnvelopeSlider = [this](juce::Slider& slider, juce::Label& label,
                                          const char* text, double min, double max, double def, double skewMid)
    {
        slider.setLookAndFeel(&knobLookAndFeel);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(min, max, 0.1);
        slider.setSkewFactorFromMidPoint(skewMid);
        slider.setValue(def, juce::dontSendNotification);
        slider.setPopupDisplayEnabled(true, false, this);
        slider.setTextValueSuffix(" ms");
        slider.setColour(juce::Slider::trackColourId, stripColor.withAlpha(0.9f));
        slider.setColour(juce::Slider::thumbColourId, stripColor.brighter(0.35f));
        enableAltClickReset(slider, def);
        addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, stripColor.brighter(0.35f));
        addAndMakeVisible(label);
    };

    setupStepEnvelopeSlider(stepAttackSlider, stepAttackLabel, "A", 0.0, 400.0, 0.0, 12.0);
    setupStepEnvelopeSlider(stepDecaySlider, stepDecayLabel, "D", 1.0, 4000.0, 4000.0, 700.0);
    setupStepEnvelopeSlider(stepReleaseSlider, stepReleaseLabel, "R", 1.0, 4000.0, 110.0, 180.0);
    stepAttackSlider.setTooltip("Step envelope attack");
    stepDecaySlider.setTooltip("Step envelope decay");
    stepReleaseSlider.setTooltip("Step envelope release");
    stepAttackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripStepAttack" + juce::String(stripIndex), stepAttackSlider);
    stepDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripStepDecay" + juce::String(stripIndex), stepDecaySlider);
    stepReleaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripStepRelease" + juce::String(stripIndex), stepReleaseSlider);
    
    // Labels below knobs
    volumeLabel.setText("VOL", juce::dontSendNotification);
    volumeLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));  // Increased from 9
    volumeLabel.setJustificationType(juce::Justification::centred);
    volumeLabel.setColour(juce::Label::textColourId, stripColor.brighter(0.3f));
    addAndMakeVisible(volumeLabel);
    
    panLabel.setText("PAN", juce::dontSendNotification);
    panLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    panLabel.setJustificationType(juce::Justification::centred);
    panLabel.setColour(juce::Label::textColourId, stripColor.brighter(0.3f));
    addAndMakeVisible(panLabel);

    pitchLabel.setText("PITCH", juce::dontSendNotification);
    pitchLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    pitchLabel.setJustificationType(juce::Justification::centred);
    pitchLabel.setColour(juce::Label::textColourId, stripColor.brighter(0.3f));
    addAndMakeVisible(pitchLabel);

    speedLabel.setText("SPEED", juce::dontSendNotification);
    speedLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    speedLabel.setJustificationType(juce::Justification::centred);
    speedLabel.setColour(juce::Label::textColourId, stripColor.brighter(0.3f));
    addAndMakeVisible(speedLabel);
    
    scratchLabel.setText("SCR", juce::dontSendNotification);
    scratchLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    scratchLabel.setJustificationType(juce::Justification::centred);
    scratchLabel.setColour(juce::Label::textColourId, stripColor.brighter(0.3f));
    addAndMakeVisible(scratchLabel);

    sliceLengthLabel.setText("SLICE", juce::dontSendNotification);
    sliceLengthLabel.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
    sliceLengthLabel.setJustificationType(juce::Justification::centred);
    sliceLengthLabel.setColour(juce::Label::textColourId, stripColor.brighter(0.3f));
    addAndMakeVisible(sliceLengthLabel);

    // Label showing current beats setting
    tempoLabel.setText("AUTO", juce::dontSendNotification);
    tempoLabel.setFont(juce::Font(juce::FontOptions(9.0f)));
    tempoLabel.setJustificationType(juce::Justification::centred);
    tempoLabel.setColour(juce::Label::textColourId, stripColor);
    addAndMakeVisible(tempoLabel);
    tempoLabel.setTooltip("Beats per loop (auto or manual).");

    modTargetLabel.setText("TARGET", juce::dontSendNotification);
    modTargetLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    modTargetLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(modTargetLabel);

    modTargetBox.addItem("None", 1);
    modTargetBox.addItem("Vol", 2);
    modTargetBox.addItem("Pan", 3);
    modTargetBox.addItem("Pitch", 4);
    modTargetBox.addItem("Speed", 5);
    modTargetBox.addItem("Cutoff", 6);
    modTargetBox.addItem("Reso", 7);
    modTargetBox.addItem("G.Size", 8);
    modTargetBox.addItem("G.Dens", 9);
    modTargetBox.addItem("G.Pitch", 10);
    modTargetBox.addItem("G.PJit", 11);
    modTargetBox.addItem("G.Spread", 12);
    modTargetBox.addItem("G.Jitter", 13);
    modTargetBox.addItem("G.Random", 14);
    modTargetBox.addItem("G.Arp", 15);
    modTargetBox.addItem("G.Cloud", 16);
    modTargetBox.addItem("G.Emit", 17);
    modTargetBox.addItem("G.Env", 18);
    modTargetBox.addItem("Retrig", 19);
    modTargetBox.setSelectedId(1, juce::dontSendNotification);
    modTargetBox.onChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
        {
            engine->setModTarget(stripIndex, comboIdToModTarget(modTargetBox.getSelectedId()));
            modBipolarToggle.setToggleState(engine->isModBipolar(stripIndex), juce::dontSendNotification);
        }
    };
    addAndMakeVisible(modTargetBox);

    modBipolarToggle.setButtonText("BIP");
    modBipolarToggle.onClick = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModBipolar(stripIndex, modBipolarToggle.getToggleState());
    };
    addAndMakeVisible(modBipolarToggle);

    modDepthLabel.setText("DEPTH", juce::dontSendNotification);
    modDepthLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    modDepthLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(modDepthLabel);

    modDepthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    modDepthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    modDepthSlider.setRange(0.0, 1.0, 0.01);
    modDepthSlider.onValueChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModDepth(stripIndex, static_cast<float>(modDepthSlider.getValue()));
    };
    addAndMakeVisible(modDepthSlider);

    modOffsetLabel.setText("SMTH", juce::dontSendNotification);
    modOffsetLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    modOffsetLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(modOffsetLabel);

    modOffsetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    modOffsetSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    modOffsetSlider.setRange(0.0, 250.0, 1.0);
    modOffsetSlider.setSkewFactorFromMidPoint(40.0);
    modOffsetSlider.onValueChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModSmoothingMs(stripIndex, static_cast<float>(modOffsetSlider.getValue()));
    };
    addAndMakeVisible(modOffsetSlider);

    modCurveBendLabel.setText("BEND", juce::dontSendNotification);
    modCurveBendLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    modCurveBendLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(modCurveBendLabel);

    modCurveBendSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    modCurveBendSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    modCurveBendSlider.setRange(-1.0, 1.0, 0.01);
    modCurveBendSlider.setValue(0.0, juce::dontSendNotification);
    modCurveBendSlider.onValueChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModCurveBend(stripIndex, static_cast<float>(modCurveBendSlider.getValue()));
    };
    addAndMakeVisible(modCurveBendSlider);

    modLengthLabel.setText("LEN", juce::dontSendNotification);
    modLengthLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    modLengthLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(modLengthLabel);

    modLengthBox.addItem("1", 1);
    modLengthBox.addItem("2", 2);
    modLengthBox.addItem("4", 4);
    modLengthBox.addItem("8", 8);
    modLengthBox.setSelectedId(1, juce::dontSendNotification);
    modLengthBox.onChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModLengthBars(stripIndex, modLengthBox.getSelectedId());
    };
    addAndMakeVisible(modLengthBox);

    for (int slot = 0; slot < ModernAudioEngine::NumModSequencers; ++slot)
    {
        auto& tab = modSequencerTabs[static_cast<size_t>(slot)];
        tab.setButtonText("S" + juce::String(slot + 1));
        tab.setTooltip("Switch to modulation sequencer " + juce::String(slot + 1) + ".");
        tab.onClick = [this, slot]()
        {
            if (auto* engine = processor.getAudioEngine())
                engine->setModSequencerSlot(stripIndex, slot);
            updateModSequencerTabButtons();
            repaint();
        };
        addAndMakeVisible(tab);
    }
    updateModSequencerTabButtons();

    modPitchQuantToggle.setButtonText("P.Quant");
    modPitchQuantToggle.onClick = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModPitchScaleQuantize(stripIndex, modPitchQuantToggle.getToggleState());
    };
    addAndMakeVisible(modPitchQuantToggle);

    modPitchScaleBox.addItem("Chrom", 1);
    modPitchScaleBox.addItem("Maj", 2);
    modPitchScaleBox.addItem("Min", 3);
    modPitchScaleBox.addItem("Dor", 4);
    modPitchScaleBox.addItem("Pent", 5);
    modPitchScaleBox.setSelectedId(1, juce::dontSendNotification);
    modPitchScaleBox.onChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModPitchScale(stripIndex, comboIdToPitchScale(modPitchScaleBox.getSelectedId()));
    };
    addAndMakeVisible(modPitchScaleBox);

    modShapeLabel.setText("SHAPE", juce::dontSendNotification);
    modShapeLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    modShapeLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(modShapeLabel);

    modShapeBox.addItem("Curve", 1);
    modShapeBox.addItem("Steps", 2);
    modShapeBox.setSelectedId(1, juce::dontSendNotification);
    modShapeBox.onChange = [this]()
    {
        const bool curveMode = (modShapeBox.getSelectedId() == 1);
        modCurveBendSlider.setEnabled(curveMode);
        modCurveTypeBox.setEnabled(curveMode);
        if (auto* engine = processor.getAudioEngine())
            engine->setModCurveMode(stripIndex, curveMode);
    };
    addAndMakeVisible(modShapeBox);

    modCurveTypeLabel.setText("CTYPE", juce::dontSendNotification);
    modCurveTypeLabel.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    modCurveTypeLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(modCurveTypeLabel);

    modCurveTypeBox.addItem("Normal", 1);
    modCurveTypeBox.addItem("Exp+", 2);
    modCurveTypeBox.addItem("Exp-", 3);
    modCurveTypeBox.addItem("Sine", 4);
    modCurveTypeBox.addItem("Square", 5);
    modCurveTypeBox.setSelectedId(1, juce::dontSendNotification);
    modCurveTypeBox.setTooltip("Curve draw type in Curve mode: Normal, Exp+/-, Sine, Square.");
    modCurveTypeBox.onChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModCurveShape(stripIndex, comboIdToCurveShape(modCurveTypeBox.getSelectedId()));
    };
    addAndMakeVisible(modCurveTypeBox);
    
    showingStepDisplay = true;
    waveform.setVisible(false);
    stepDisplay.setVisible(true);
    updateGrainOverlayVisibility();
}

void StripControl::updateGrainOverlayVisibility()
{
    const bool isGrainMode = false;
    const bool isStepMode = showingStepDisplay;
    grainOverlayVisible = isGrainMode;
    const bool showPitchPage = isGrainMode && grainSubPage == GrainSubPage::Pitch;
    const bool showSpacePage = isGrainMode && grainSubPage == GrainSubPage::Space;
    const bool showShapePage = isGrainMode && grainSubPage == GrainSubPage::Shape;

    volumeSlider.setVisible(!isGrainMode);
    panSlider.setVisible(!isGrainMode);
    volumeLabel.setVisible(!isGrainMode);
    panLabel.setVisible(!isGrainMode);

    const bool showLoopKnobs = !showingStepDisplay && !isGrainMode;
    bool showSliceLength = false;
    if (showLoopKnobs && processor.getAudioEngine())
        showSliceLength = false;

    pitchSlider.setVisible(showLoopKnobs);
    speedSlider.setVisible(showLoopKnobs);
    scratchSlider.setVisible(showLoopKnobs);
    sliceLengthSlider.setVisible(showSliceLength);
    pitchLabel.setVisible(showLoopKnobs);
    speedLabel.setVisible(showLoopKnobs);
    scratchLabel.setVisible(showLoopKnobs);
    sliceLengthLabel.setVisible(showSliceLength);
    patternLengthBox.setVisible(isStepMode && !isGrainMode);
    stepLengthReadoutBox.setVisible(isStepMode && !isGrainMode);
    laneMidiChannelBox.setVisible(isStepMode && !isGrainMode);
    laneMidiNoteBox.setVisible(isStepMode && !isGrainMode);
    laneMidiChannelLabel.setVisible(isStepMode && !isGrainMode);
    laneMidiNoteLabel.setVisible(isStepMode && !isGrainMode);
    stepAttackSlider.setVisible(isStepMode && !isGrainMode);
    stepDecaySlider.setVisible(isStepMode && !isGrainMode);
    stepReleaseSlider.setVisible(isStepMode && !isGrainMode);
    stepAttackLabel.setVisible(isStepMode && !isGrainMode);
    stepDecayLabel.setVisible(isStepMode && !isGrainMode);
    stepReleaseLabel.setVisible(isStepMode && !isGrainMode);
    beatSpaceCategoryLabel.setVisible(!isGrainMode && stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels);
    beatSpaceCategoryBox.setVisible(!isGrainMode && stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels);
    grainSizeSlider.setVisible(isGrainMode);
    grainDensitySlider.setVisible(isGrainMode);
    grainPitchSlider.setVisible(showPitchPage);
    grainPitchJitterSlider.setVisible(showPitchPage);
    grainSpreadSlider.setVisible(showSpacePage);
    grainJitterSlider.setVisible(showSpacePage);
    grainPositionJitterSlider.setVisible(showSpacePage);
    grainRandomSlider.setVisible(showPitchPage);
    grainArpSlider.setVisible(showPitchPage);
    grainCloudSlider.setVisible(showSpacePage);
    grainEmitterSlider.setVisible(showSpacePage);
    grainEnvelopeSlider.setVisible(showShapePage);
    grainShapeSlider.setVisible(showShapePage);
    grainTabPitchButton.setVisible(isGrainMode);
    grainTabSpaceButton.setVisible(isGrainMode);
    grainTabShapeButton.setVisible(isGrainMode);
    grainSizeSyncToggle.setVisible(isGrainMode);
    grainSizeDivLabel.setVisible(isGrainMode);
    grainSizeLabel.setVisible(isGrainMode);
    grainDensityLabel.setVisible(isGrainMode);
    grainPitchLabel.setVisible(showPitchPage);
    grainPitchJitterLabel.setVisible(showPitchPage);
    grainSpreadLabel.setVisible(showSpacePage);
    grainJitterLabel.setVisible(showSpacePage);
    grainPositionJitterLabel.setVisible(showSpacePage);
    grainRandomLabel.setVisible(showPitchPage);
    grainArpLabel.setVisible(showPitchPage);
    grainCloudLabel.setVisible(showSpacePage);
    grainEmitterLabel.setVisible(showSpacePage);
    grainEnvelopeLabel.setVisible(showShapePage);
    grainShapeLabel.setVisible(showShapePage);
    updateGrainTabButtons();
}

void StripControl::updateGrainTabButtons()
{
    auto tintTab = [](juce::TextButton& button, bool active)
    {
        button.setColour(juce::TextButton::buttonColourId,
                         active ? kAccent.withAlpha(0.95f) : juce::Colour(0xffebf2fa));
        button.setColour(juce::TextButton::buttonOnColourId,
                         active ? kAccent.brighter(0.12f) : juce::Colour(0xffdbe7f3));
        button.setColour(juce::TextButton::textColourOffId,
                         active ? juce::Colour(0xfff7fbff) : kTextPrimary);
        button.setColour(juce::TextButton::textColourOnId,
                         active ? juce::Colour(0xfff7fbff) : kTextPrimary);
    };
    tintTab(grainTabPitchButton, grainSubPage == GrainSubPage::Pitch);
    tintTab(grainTabSpaceButton, grainSubPage == GrainSubPage::Space);
    tintTab(grainTabShapeButton, grainSubPage == GrainSubPage::Shape);
}

void StripControl::updateModSequencerTabButtons()
{
    const int activeSlot = processor.getAudioEngine()
        ? processor.getAudioEngine()->getModSequencerSlot(stripIndex)
        : 0;

    for (int slot = 0; slot < ModernAudioEngine::NumModSequencers; ++slot)
    {
        auto& tab = modSequencerTabs[static_cast<size_t>(slot)];
        const bool active = (slot == activeSlot);
        tab.setColour(juce::TextButton::buttonColourId,
                      active ? kAccent.withAlpha(0.95f) : juce::Colour(0xffebf2fa));
        tab.setColour(juce::TextButton::buttonOnColourId,
                      active ? kAccent.brighter(0.12f) : juce::Colour(0xffdbe7f3));
        tab.setColour(juce::TextButton::textColourOffId,
                      active ? juce::Colour(0xfff7fbff) : kTextPrimary);
        tab.setColour(juce::TextButton::textColourOnId,
                      active ? juce::Colour(0xfff7fbff) : kTextPrimary);
    }
}


void StripControl::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    drawPanel(g, bounds, stripColor, 10.0f);

    if (modulationLaneView)
    {
        paintModulationLane(g);
    }
    else
    {
        // Paint LED overlay on top of waveform area
        paintLEDOverlay(g);
    }

}

void StripControl::setModulationLaneView(bool shouldShow)
{
    if (modulationLaneView == shouldShow)
        return;
    if (shouldShow)
    {
        preModulationShowingStepDisplay = showingStepDisplay;
        preModulationWaveformVisible = waveform.isVisible();
        preModulationStepVisible = stepDisplay.isVisible();
    }
    modulationLaneView = shouldShow;
    if (!shouldShow)
    {
        showingStepDisplay = preModulationShowingStepDisplay;
        waveform.setVisible(preModulationWaveformVisible);
        stepDisplay.setVisible(preModulationStepVisible);
        modulationLastDrawStep = -1;
        updateGrainOverlayVisibility();
        updateFromEngine();
    }
    resized();
    repaint();
}

juce::Rectangle<int> StripControl::getModulationLaneBounds() const
{
    return modulationLaneBounds;
}

void StripControl::paintModulationLane(juce::Graphics& g)
{
    auto* engine = processor.getAudioEngine();
    if (!engine)
        return;

    auto lane = getModulationLaneBounds();
    if (lane.isEmpty())
        return;

    const auto seq = engine->getModSequencerState(stripIndex);
    const int lengthBars = juce::jlimit(1, ModernAudioEngine::MaxModBars, engine->getModLengthBars(stripIndex));
    const int totalSteps = juce::jmax(ModernAudioEngine::ModSteps, lengthBars * ModernAudioEngine::ModSteps);
    const int activeStep = juce::jlimit(0, totalSteps - 1, engine->getModCurrentGlobalStep(stripIndex));
    if (totalSteps <= 0)
        return;

    g.setColour(juce::Colour(0xff1f1f1f));
    g.fillRoundedRectangle(lane.toFloat(), 6.0f);
    g.setColour(stripColor.withAlpha(0.35f));
    g.drawRoundedRectangle(lane.toFloat().reduced(0.5f), 6.0f, 1.0f);

    const int activeSlot = juce::jlimit(0, ModernAudioEngine::NumModSequencers - 1, engine->getModSequencerSlot(stripIndex));
    const juce::String laneInfo = "SEQ " + juce::String(activeSlot + 1)
        + "  PAGE " + juce::String(seq.editPage + 1) + "/" + juce::String(lengthBars);
    auto infoBadge = juce::Rectangle<float>(lane.getX() + 8.0f, lane.getY() + 4.0f, 118.0f, 14.0f);
    g.setColour(juce::Colour(0xff111111).withAlpha(0.72f));
    g.fillRoundedRectangle(infoBadge, 3.0f);
    g.setColour(stripColor.withAlpha(0.22f));
    g.drawRoundedRectangle(infoBadge, 3.0f, 1.0f);
    g.setColour(kTextPrimary.withAlpha(0.88f));
    g.setFont(8.5f);
    g.drawText(laneInfo, infoBadge.toNearestInt(), juce::Justification::centred, false);

    const auto drawLane = lane.reduced(12, 2);
    const float dotSize = (totalSteps > 32) ? 4.0f : 6.0f;
    const float dotPad = dotSize * 0.6f;
    const float left = static_cast<float>(drawLane.getX()) + dotPad;
    const float right = juce::jmax(left, static_cast<float>(drawLane.getRight() - 1) - dotPad);
    const float top = static_cast<float>(drawLane.getY()) + 2.0f;
    const float bottom = static_cast<float>(drawLane.getBottom()) - 2.0f;
    const float width = juce::jmax(1.0f, right - left);
    const float height = bottom - top;
    const float stepWidth = juce::jmax(0.25f, width / static_cast<float>(juce::jmax(1, totalSteps)));
    const float centerY = top + (height * 0.5f);

    if (seq.bipolar)
    {
        g.setColour(juce::Colour(0xff454545));
        g.drawLine(left, centerY, right, centerY, 1.0f);
    }

    auto valueToY = [&](float v) -> float
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, v);
        const float n = seq.bipolar ? ((clamped * 2.0f) - 1.0f) : clamped;
        return seq.bipolar
            ? (centerY - (n * (height * 0.48f)))
            : (bottom - (n * height));
    };

    std::vector<float> startValues(static_cast<size_t>(totalSteps));
    std::vector<float> endValues(static_cast<size_t>(totalSteps));
    std::vector<int> subdivisions(static_cast<size_t>(totalSteps));
    std::vector<ModernAudioEngine::ModCurveShape> stepCurveShapes(static_cast<size_t>(totalSteps),
                                                                   ModernAudioEngine::ModCurveShape::Linear);
    for (int i = 0; i < totalSteps; ++i)
    {
        const float startValue = juce::jlimit(0.0f, 1.0f, engine->getModStepValueAbsolute(stripIndex, i));
        const int subdiv = juce::jlimit(
            1, ModernAudioEngine::ModMaxStepSubdivisions, engine->getModStepSubdivisionAbsolute(stripIndex, i));
        float endValue = juce::jlimit(0.0f, 1.0f, engine->getModStepEndValueAbsolute(stripIndex, i));
        const auto stepCurveShape = engine->getModStepCurveShapeAbsolute(stripIndex, i);
        if (subdiv <= 1)
            endValue = startValue;
        startValues[static_cast<size_t>(i)] = startValue;
        endValues[static_cast<size_t>(i)] = endValue;
        subdivisions[static_cast<size_t>(i)] = subdiv;
        stepCurveShapes[static_cast<size_t>(i)] = stepCurveShape;
    }

    const float activeStepX = left + (stepWidth * static_cast<float>(activeStep));
    g.setColour(kAccent.withAlpha(0.10f));
    g.fillRect(activeStepX, top, juce::jmax(1.0f, stepWidth), juce::jmax(1.0f, height));

    const float bend = juce::jlimit(-1.0f, 1.0f, seq.curveBend);
    std::vector<juce::Point<float>> stepMarkerPoints(static_cast<size_t>(totalSteps));
    for (int i = 0; i < totalSteps; ++i)
    {
        const float markerPhase = seq.curveMode ? shapeSubdivisionBendPhaseUi(0.5f, bend) : 0.5f;
        const float markerValue = (subdivisions[static_cast<size_t>(i)] > 1)
            ? sampleModSubdivisionValueUi(
                startValues[static_cast<size_t>(i)],
                endValues[static_cast<size_t>(i)],
                subdivisions[static_cast<size_t>(i)],
                markerPhase)
            : startValues[static_cast<size_t>(i)];
        const float x = left + (stepWidth * (static_cast<float>(i) + 0.5f));
        stepMarkerPoints[static_cast<size_t>(i)] = { x, valueToY(markerValue) };
    }

    if (seq.curveMode)
    {
        juce::Path rawPath;
        std::vector<float> sampledX;
        std::vector<float> sampledValues;
        sampledX.reserve(static_cast<size_t>(totalSteps * 10));
        sampledValues.reserve(static_cast<size_t>(totalSteps * 10));
        bool started = false;
        for (int i = 0; i < totalSteps; ++i)
        {
            const int subdiv = subdivisions[static_cast<size_t>(i)];
            const bool hasLocalRamp = (subdiv > 1);
            const int segmentCount = hasLocalRamp ? juce::jlimit(2, 64, subdiv * 4) : 8;
            const float startValue = startValues[static_cast<size_t>(i)];
            const float endValue = endValues[static_cast<size_t>(i)];
            const float nextStart = startValues[static_cast<size_t>((i + 1) % totalSteps)];

            for (int s = 0; s <= segmentCount; ++s)
            {
                if (i > 0 && s == 0)
                    continue;

                const float t = static_cast<float>(s) / static_cast<float>(segmentCount);
                const float shapedT = shapeCurvePhaseUi(
                    t,
                    bend,
                    stepCurveShapes[static_cast<size_t>(i)]);
                const float bendT = shapeSubdivisionBendPhaseUi(t, bend);
                const float value = hasLocalRamp
                    ? sampleModSubdivisionValueUi(startValue,
                                                  endValue,
                                                  subdiv,
                                                  bendT)
                    : juce::jlimit(0.0f, 1.0f,
                                   startValue + ((nextStart - startValue) * shapedT));
                const float x = juce::jlimit(left, right, left + (stepWidth * (static_cast<float>(i) + t)));
                const float y = valueToY(value);

                if (!started)
                {
                    rawPath.startNewSubPath(x, y);
                    started = true;
                }
                else
                {
                    rawPath.lineTo(x, y);
                }
                sampledX.push_back(x);
                sampledValues.push_back(value);
            }
        }

        const float smoothingMs = juce::jlimit(0.0f, 250.0f, seq.smoothingMs);
        const bool showSmoothedOverlay = (smoothingMs > 0.05f && sampledValues.size() > 2);

        g.setColour(stripColor.withAlpha(showSmoothedOverlay ? 0.58f : 0.9f));
        g.strokePath(rawPath, juce::PathStrokeType(showSmoothedOverlay ? 1.6f : 2.0f));

        if (showSmoothedOverlay)
        {
            // Approximate post-curve smoothing for visual feedback in curve mode.
            const float refStepMs = 125.0f;
            const float totalMs = refStepMs * static_cast<float>(totalSteps);
            const int sampleCount = static_cast<int>(sampledValues.size());
            const float dtMs = totalMs / static_cast<float>(juce::jmax(1, sampleCount - 1));
            const float alpha = 1.0f - std::exp(-dtMs / juce::jmax(1.0f, smoothingMs));

            float smoothed = sampledValues.front();
            juce::Path smoothPath;
            smoothPath.startNewSubPath(sampledX.front(), valueToY(smoothed));
            for (size_t idx = 1; idx < sampledValues.size(); ++idx)
            {
                smoothed += (sampledValues[idx] - smoothed) * juce::jlimit(0.0f, 1.0f, alpha);
                smoothPath.lineTo(sampledX[idx], valueToY(smoothed));
            }

            g.setColour(juce::Colour(0xff101010).withAlpha(0.68f));
            g.strokePath(smoothPath, juce::PathStrokeType(3.4f));
            g.setColour(kAccent.brighter(0.35f).withAlpha(0.92f));
            g.strokePath(smoothPath, juce::PathStrokeType(2.2f));

            auto badge = juce::Rectangle<float>(right - 76.0f, top + 1.0f, 74.0f, 13.0f);
            g.setColour(juce::Colour(0xff111111).withAlpha(0.74f));
            g.fillRoundedRectangle(badge, 3.0f);
            g.setColour(kAccent.withAlpha(0.26f));
            g.drawRoundedRectangle(badge, 3.0f, 1.0f);
            g.setColour(juce::Colour(0xfff8e7c2).withAlpha(0.92f));
            g.setFont(8.0f);
            g.drawText("Smth " + juce::String(static_cast<int>(std::round(smoothingMs))) + "ms",
                       badge.toNearestInt(), juce::Justification::centred, false);
        }
    }
    else
    {
        for (int i = 0; i < totalSteps; ++i)
        {
            const float stepX = left + (stepWidth * static_cast<float>(i));
            const int subdiv = subdivisions[static_cast<size_t>(i)];
            const float startValue = startValues[static_cast<size_t>(i)];
            const float endValue = endValues[static_cast<size_t>(i)];
            const float slotWidth = stepWidth / static_cast<float>(juce::jmax(1, subdiv));
            const float barWidth = juce::jmax(1.0f, slotWidth * 0.72f);

            for (int s = 0; s < subdiv; ++s)
            {
                const float t = (subdiv <= 1)
                    ? 1.0f
                    : (static_cast<float>(s + 1) / static_cast<float>(subdiv));
                const float value = (subdiv <= 1)
                    ? startValue
                    : juce::jlimit(0.0f, 1.0f, startValue + ((endValue - startValue) * t));

                float y0 = seq.bipolar ? centerY : bottom;
                const float y1 = valueToY(value);
                const float x = stepX + (slotWidth * (static_cast<float>(s) + 0.5f)) - (barWidth * 0.5f);
                const float yTop = juce::jmin(y0, y1);
                const float h = juce::jmax(1.0f, std::abs(y1 - y0));
                const float shade = (subdiv <= 1)
                    ? 0.55f
                    : juce::jmap(static_cast<float>(s) / static_cast<float>(juce::jmax(1, subdiv - 1)), 0.72f, 0.44f);
                g.setColour(stripColor.withAlpha(shade));
                g.fillRoundedRectangle(x, yTop, barWidth, h, 1.5f);
            }
        }
    }

    for (int i = 0; i < totalSteps; ++i)
    {
        const auto point = stepMarkerPoints[static_cast<size_t>(i)];
        const bool isActive = (i == activeStep);
        g.setColour(isActive ? kAccent : stripColor.withMultipliedBrightness(0.8f));
        g.fillEllipse(point.x - (dotSize * 0.5f), point.y - (dotSize * 0.5f), dotSize, dotSize);

        const int subdiv = subdivisions[static_cast<size_t>(i)];
        if (subdiv > 1 && stepWidth > 14.0f)
        {
            auto label = juce::Rectangle<float>(
                left + (stepWidth * static_cast<float>(i)),
                top + 1.0f,
                juce::jmax(8.0f, stepWidth),
                juce::jmax(8.0f, juce::jmin(12.0f, height * 0.25f)));
            g.setColour(juce::Colour(0xfff0f0f0).withAlpha(0.72f));
            g.setFont(juce::jmax(7.0f, juce::jmin(10.0f, stepWidth * 0.34f)));
            g.drawText("x" + juce::String(subdiv), label.toNearestInt(), juce::Justification::centred, false);
        }
    }
}

void StripControl::applyModulationPoint(juce::Point<int> p)
{
    auto* engine = processor.getAudioEngine();
    if (!engine || stripIndex >= StepVstHostAudioProcessor::MaxStrips)
        return;

    auto lane = getModulationLaneBounds().reduced(12, 2);
    auto hitLane = lane.expanded(1, 0);
    if (!hitLane.contains(p))
        return;

    const int lengthBars = juce::jlimit(1, ModernAudioEngine::MaxModBars, engine->getModLengthBars(stripIndex));
    const int totalSteps = juce::jmax(ModernAudioEngine::ModSteps, lengthBars * ModernAudioEngine::ModSteps);
    if (modulationLastDrawStep >= totalSteps)
        modulationLastDrawStep = -1;
    const float x = juce::jlimit(static_cast<float>(lane.getX()),
                                 static_cast<float>(lane.getRight() - 1),
                                 static_cast<float>(p.x));
    const float nx = juce::jlimit(0.0f, 1.0f, (x - static_cast<float>(lane.getX())) / juce::jmax(1.0f, static_cast<float>(lane.getWidth() - 1)));
    const float ny = juce::jlimit(0.0f, 1.0f, (static_cast<float>(p.y - lane.getY())) / juce::jmax(1.0f, static_cast<float>(lane.getHeight())));
    const int step = juce::jlimit(0, totalSteps - 1,
                                  static_cast<int>(std::round(nx * static_cast<float>(juce::jmax(1, totalSteps - 1)))));
    const float value = juce::jlimit(0.0f, 1.0f, 1.0f - ny);
    if (modulationLastDrawStep < 0)
    {
        engine->setModStepValueAbsolute(stripIndex, step, value);
        modulationLastDrawStep = step;
        modulationLastDrawValue = value;
        return;
    }

    const int from = juce::jmin(modulationLastDrawStep, step);
    const int to = juce::jmax(modulationLastDrawStep, step);
    for (int s = from; s <= to; ++s)
    {
        const float t = (to == from) ? 1.0f : (static_cast<float>(s - from) / static_cast<float>(to - from));
        const float v = modulationLastDrawValue + ((value - modulationLastDrawValue) * t);
        engine->setModStepValueAbsolute(stripIndex, s, v);
    }
    modulationLastDrawStep = step;
    modulationLastDrawValue = value;
}

int StripControl::getModulationStepFromPoint(juce::Point<int> p) const
{
    auto* engine = processor.getAudioEngine();
    if (!engine)
        return -1;
    auto lane = getModulationLaneBounds().reduced(12, 2);
    if (lane.isEmpty())
        return -1;
    if (!lane.expanded(1, 0).contains(p))
        return -1;

    const int lengthBars = juce::jlimit(1, ModernAudioEngine::MaxModBars, engine->getModLengthBars(stripIndex));
    const int totalSteps = juce::jmax(ModernAudioEngine::ModSteps, lengthBars * ModernAudioEngine::ModSteps);
    const float x = juce::jlimit(static_cast<float>(lane.getX()),
                                 static_cast<float>(lane.getRight() - 1),
                                 static_cast<float>(p.x));
    const float nx = juce::jlimit(0.0f, 1.0f,
                                  (x - static_cast<float>(lane.getX()))
                                  / juce::jmax(1.0f, static_cast<float>(lane.getWidth() - 1)));
    return juce::jlimit(0, totalSteps - 1,
                        static_cast<int>(std::round(nx * static_cast<float>(juce::jmax(1, totalSteps - 1)))));
}

void StripControl::applyModulationCellDuplicateFromDrag(int deltaY)
{
    auto* engine = processor.getAudioEngine();
    if (!engine || stripIndex >= StepVstHostAudioProcessor::MaxStrips || modTransformStep < 0)
        return;

    const int lengthBars = juce::jlimit(1, ModernAudioEngine::MaxModBars, engine->getModLengthBars(stripIndex));
    const int totalSteps = juce::jmax(ModernAudioEngine::ModSteps, lengthBars * ModernAudioEngine::ModSteps);
    if (modTransformStep >= totalSteps)
        return;

    const int nextSubdivision = juce::jlimit(
        1,
        ModernAudioEngine::ModMaxStepSubdivisions,
        modTransformStartSubdivision + ((-deltaY) / 14));
    const float endValue = (nextSubdivision > 1) ? modTransformStartEndValue : modTransformStartValue;
    engine->setModStepShapeAbsolute(stripIndex, modTransformStep, nextSubdivision, endValue);
}

void StripControl::applyModulationCellCurveFromDrag(int deltaY, bool rampUpMode)
{
    auto* engine = processor.getAudioEngine();
    if (!engine || stripIndex >= StepVstHostAudioProcessor::MaxStrips || modTransformStep < 0)
        return;

    const int lengthBars = juce::jlimit(1, ModernAudioEngine::MaxModBars, engine->getModLengthBars(stripIndex));
    const int totalSteps = juce::jmax(ModernAudioEngine::ModSteps, lengthBars * ModernAudioEngine::ModSteps);
    if (modTransformStep >= totalSteps)
        return;

    int subdivisions = modTransformStartSubdivision;
    if (subdivisions <= 1)
    {
        subdivisions = juce::jlimit(
            2,
            ModernAudioEngine::ModMaxStepSubdivisions,
            2 + (std::abs(deltaY) / 14));
    }

    float startValue = modTransformStartValue;
    float endValue = modTransformStartEndValue;
    computeSingleModCellRamp(modTransformStartValue, modTransformStartEndValue, deltaY, rampUpMode, startValue, endValue);
    engine->setModStepValueAbsolute(stripIndex, modTransformStep, startValue);
    engine->setModStepShapeAbsolute(stripIndex, modTransformStep, subdivisions, endValue);
}

void StripControl::mouseDown(const juce::MouseEvent& e)
{
    if (modulationLaneView)
    {
        auto* engine = processor.getAudioEngine();
        if (!engine || stripIndex >= StepVstHostAudioProcessor::MaxStrips)
            return;

        const auto state = engine->getModSequencerState(stripIndex);
        const bool neutralBipolar = modTargetAllowsBipolar(state.target) && state.bipolar;
        const float neutralValue = neutralBipolar ? 0.5f : 0.0f;
        const int lengthBars = juce::jlimit(1, ModernAudioEngine::MaxModBars, engine->getModLengthBars(stripIndex));
        const int totalSteps = juce::jmax(ModernAudioEngine::ModSteps, lengthBars * ModernAudioEngine::ModSteps);

        const auto mods = e.mods;
        const auto modifierGesture = getStepCellModifierGesture(mods);
        const int clickedStep = getModulationStepFromPoint(e.getPosition());
        if (clickedStep >= 0 && modifierGesture != StepCellModifierGesture::None)
        {
            modTransformStartY = e.y;
            modTransformStep = clickedStep;
            modTransformStartValue = juce::jlimit(0.0f, 1.0f, engine->getModStepValueAbsolute(stripIndex, clickedStep));
            modTransformStartSubdivision = juce::jlimit(
                1,
                ModernAudioEngine::ModMaxStepSubdivisions,
                engine->getModStepSubdivisionAbsolute(stripIndex, clickedStep));
            modTransformStartEndValue = juce::jlimit(
                0.0f, 1.0f, engine->getModStepEndValueAbsolute(stripIndex, clickedStep));
            switch (modifierGesture)
            {
                case StepCellModifierGesture::Divide:
                    modTransformMode = ModTransformMode::DuplicateCell;
                    break;
                case StepCellModifierGesture::RampUp:
                    modTransformMode = ModTransformMode::ShapeUpCell;
                    break;
                case StepCellModifierGesture::RampDown:
                    modTransformMode = ModTransformMode::ShapeDownCell;
                    break;
                case StepCellModifierGesture::None:
                default:
                    modTransformMode = ModTransformMode::None;
                    break;
            }

            if (modTransformMode == ModTransformMode::ShapeUpCell)
                applyModulationCellCurveFromDrag(0, true);
            else if (modTransformMode == ModTransformMode::ShapeDownCell)
                applyModulationCellCurveFromDrag(0, false);
            return;
        }

        if (mods.isRightButtonDown() && modifierGesture == StepCellModifierGesture::None)
        {
            for (int i = 0; i < totalSteps; ++i)
                engine->setModStepValueAbsolute(stripIndex, i, neutralValue);
            modulationLastDrawStep = -1;
            return;
        }

        modTransformMode = ModTransformMode::None;
        modTransformStep = -1;
        modulationLastDrawStep = -1;
        applyModulationPoint(e.getPosition());
    }
}

void StripControl::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!modulationLaneView)
        return;

    auto* engine = processor.getAudioEngine();
    if (!engine || stripIndex >= StepVstHostAudioProcessor::MaxStrips)
        return;

    const int step = getModulationStepFromPoint(e.getPosition());
    if (step < 0)
        return;

    const auto state = engine->getModSequencerState(stripIndex);
    const bool neutralBipolar = modTargetAllowsBipolar(state.target) && state.bipolar;
    const float neutralValue = neutralBipolar ? 0.5f : 0.0f;
    engine->setModStepValueAbsolute(stripIndex, step, neutralValue);
    modulationLastDrawStep = -1;
}

void StripControl::mouseDrag(const juce::MouseEvent& e)
{
    if (modulationLaneView)
    {
        if (modTransformMode != ModTransformMode::None)
        {
            const int deltaY = e.y - modTransformStartY;
            if (modTransformMode == ModTransformMode::DuplicateCell)
                applyModulationCellDuplicateFromDrag(deltaY);
            else if (modTransformMode == ModTransformMode::ShapeUpCell)
                applyModulationCellCurveFromDrag(deltaY, true);
            else if (modTransformMode == ModTransformMode::ShapeDownCell)
                applyModulationCellCurveFromDrag(deltaY, false);
            return;
        }

        applyModulationPoint(e.getPosition());
    }
}

void StripControl::mouseUp(const juce::MouseEvent&)
{
    modTransformMode = ModTransformMode::None;
    modTransformStep = -1;
}

void StripControl::hideAllPrimaryControls()
{
    auto hide = [](juce::Component& c){ c.setVisible(false); };
    hide(loadButton); hide(muteButton); hide(soloButton); hide(directionModeBox);
    hide(beatSpaceCategoryLabel); hide(beatSpaceCategoryBox);
    hide(volumeSlider); hide(panSlider); hide(pitchSlider); hide(speedSlider); hide(scratchSlider); hide(sliceLengthSlider); hide(patternLengthBox); hide(stepLengthReadoutBox);
    hide(laneMidiChannelBox); hide(laneMidiNoteBox);
    hide(stepAttackSlider); hide(stepDecaySlider); hide(stepReleaseSlider);
    hide(tempoLabel);
    hide(volumeLabel); hide(panLabel); hide(pitchLabel); hide(speedLabel); hide(scratchLabel); hide(sliceLengthLabel);
    hide(stepAttackLabel); hide(stepDecayLabel); hide(stepReleaseLabel);
    hide(laneMidiChannelLabel); hide(laneMidiNoteLabel);
}

void StripControl::hideAllGrainControls()
{
    auto hide = [](juce::Component& c){ c.setVisible(false); };
    hide(grainSizeSlider); hide(grainDensitySlider); hide(grainPitchSlider); hide(grainPitchJitterSlider);
    hide(grainSpreadSlider); hide(grainJitterSlider); hide(grainPositionJitterSlider); hide(grainRandomSlider); hide(grainArpSlider);
    hide(grainCloudSlider); hide(grainEmitterSlider); hide(grainEnvelopeSlider); hide(grainShapeSlider);
    hide(grainTabPitchButton); hide(grainTabSpaceButton); hide(grainTabShapeButton);
    hide(grainSizeSyncToggle); hide(grainSizeDivLabel); hide(grainSizeLabel);
    hide(grainDensityLabel); hide(grainPitchLabel); hide(grainPitchJitterLabel); hide(grainSpreadLabel);
    hide(grainJitterLabel); hide(grainPositionJitterLabel); hide(grainRandomLabel); hide(grainArpLabel); hide(grainCloudLabel);
    hide(grainEmitterLabel); hide(grainEnvelopeLabel); hide(grainShapeLabel);
}

void StripControl::paintLEDOverlay(juce::Graphics& g)
{
    if (!processor.getAudioEngine()) return;
    
    auto* strip = processor.getAudioEngine()->getStrip(stripIndex);
    if (!strip || !strip->hasAudio()) return;
    
    // Get waveform bounds
    auto wfBounds = waveform.getBounds();
    if (wfBounds.isEmpty() || wfBounds.getWidth() <= 0 || wfBounds.getHeight() <= 0) 
        return;
    
    float colWidth = wfBounds.getWidth() / 16.0f;
    float ledHeight = 10.0f;
    
    // Safety check for valid dimensions
    if (!std::isfinite(colWidth) || colWidth <= 0.0f || ledHeight <= 0.0f)
        return;
    
    int currentCol = strip->getCurrentColumn();
    int loopStart = strip->getLoopStart();
    int loopEnd = strip->getLoopEnd();
    bool isPlaying = strip->isPlaying();
    
    // Draw LED blocks at top of waveform
    for (int x = 0; x < 16; ++x)
    {
        float xPos = wfBounds.getX() + x * colWidth;
        float rectWidth = colWidth - 2.0f;
        
        // Validate rectangle dimensions
        if (!std::isfinite(xPos) || !std::isfinite(rectWidth) || rectWidth <= 0.0f)
            continue;
        
        juce::Rectangle<float> ledRect(xPos + 1.0f, wfBounds.getY() + 1.0f, rectWidth, ledHeight);
        
        // Double-check the rectangle is valid
        if (ledRect.isEmpty() || !ledRect.isFinite())
            continue;
        
        // Determine LED brightness
        juce::Colour ledColor;
        
        if (isPlaying && x == currentCol)
        {
            ledColor = kAccent;
        }
        else if (x >= loopStart && x < loopEnd)
        {
            ledColor = juce::Colour(0xffc2cfde);
        }
        else
        {
            ledColor = juce::Colour(0xffe4ebf3);
        }
        
        g.setColour(ledColor);
        g.fillRoundedRectangle(ledRect, 1.0f);
        
        // Subtle border
        g.setColour(juce::Colour(0xffa9b7c8));
        g.drawRoundedRectangle(ledRect, 1.0f, 0.5f);
    }
}

void StripControl::resized()
{
    auto bounds = getLocalBounds().reduced(2);
    
    // Safety check for minimum size
    if (bounds.getWidth() < 50 || bounds.getHeight() < 50)
        return;
    
    // Label at very top
    auto labelArea = bounds.removeFromTop(14);
    stripLabel.setBounds(labelArea.removeFromLeft(30));
    
    // Main area splits: waveform left, controls right
    auto controlsArea = bounds.removeFromRight(228);
    
    // Waveform OR step display gets all remaining space
    waveform.setBounds(bounds);
    stepDisplay.setBounds(bounds);  // Same position, visibility toggled
    modulationLaneBounds = bounds;  // Match waveform/step display exactly
    
    if (modulationLaneView)
    {
        waveform.setVisible(false);
        stepDisplay.setVisible(false);
        hideAllPrimaryControls();
        hideAllGrainControls();

        modTargetLabel.setVisible(true);
        modTargetBox.setVisible(true);
        modBipolarToggle.setVisible(true);
        modDepthLabel.setVisible(true);
        modDepthSlider.setVisible(true);
        modOffsetLabel.setVisible(true);
        modOffsetSlider.setVisible(true);
        modCurveBendLabel.setVisible(true);
        modCurveBendSlider.setVisible(true);
        modLengthLabel.setVisible(true);
        modLengthBox.setVisible(true);
        for (auto& tab : modSequencerTabs)
            tab.setVisible(true);
        modPitchQuantToggle.setVisible(true);
        modPitchScaleBox.setVisible(true);
        modShapeLabel.setVisible(true);
        modShapeBox.setVisible(true);
        modCurveTypeLabel.setVisible(true);
        modCurveTypeBox.setVisible(true);

        controlsArea.reduce(4, 0);
        const int gap = 4;
        const int compactGap = 1;
        const int tabRowHeight = 14;
        const int compactRowHeight = 16;
        const int columnWidth = juce::jmax(80, (controlsArea.getWidth() - gap) / 2);
        auto splitRow = [columnWidth](juce::Rectangle<int> row, const int rowGap)
        {
            auto left = row.removeFromLeft(columnWidth);
            row.removeFromLeft(rowGap);
            return std::pair<juce::Rectangle<int>, juce::Rectangle<int>>(left, row);
        };

        auto tabRow = controlsArea.removeFromTop(tabRowHeight);
        const int tabGap = 2;
        const int tabWidth = juce::jmax(1, (tabRow.getWidth() - ((ModernAudioEngine::NumModSequencers - 1) * tabGap))
                                             / ModernAudioEngine::NumModSequencers);
        for (int slot = 0; slot < ModernAudioEngine::NumModSequencers; ++slot)
        {
            modSequencerTabs[static_cast<size_t>(slot)].setBounds(tabRow.removeFromLeft(tabWidth));
            if (slot < (ModernAudioEngine::NumModSequencers - 1))
                tabRow.removeFromLeft(tabGap);
        }
        controlsArea.removeFromTop(compactGap);

        auto row0 = controlsArea.removeFromTop(compactRowHeight);
        auto cols0 = splitRow(row0, gap);
        auto row0Left = cols0.first;
        modTargetLabel.setBounds(row0Left.removeFromLeft(36));
        modTargetBox.setBounds(row0Left);
        modLengthLabel.setBounds(cols0.second.removeFromLeft(22));
        modLengthBox.setBounds(cols0.second.removeFromLeft(60));

        controlsArea.removeFromTop(compactGap);
        auto row1 = controlsArea.removeFromTop(compactRowHeight);
        auto cols1 = splitRow(row1, gap);
        auto row1Left = cols1.first;
        modDepthLabel.setBounds(row1Left.removeFromLeft(36));
        modDepthSlider.setBounds(row1Left);
        modBipolarToggle.setBounds(cols1.second);

        controlsArea.removeFromTop(compactGap);
        auto row2 = controlsArea.removeFromTop(compactRowHeight);
        auto cols2 = splitRow(row2, gap);
        auto row2Left = cols2.first;
        modOffsetLabel.setBounds(row2Left.removeFromLeft(36));
        modOffsetSlider.setBounds(row2Left);
        modCurveBendLabel.setBounds(cols2.second.removeFromLeft(30));
        modCurveBendSlider.setBounds(cols2.second);

        controlsArea.removeFromTop(compactGap);
        auto row3 = controlsArea.removeFromTop(compactRowHeight);
        auto cols3 = splitRow(row3, gap);
        modPitchQuantToggle.setBounds(cols3.first);
        modPitchScaleBox.setBounds(cols3.second);

        controlsArea.removeFromTop(compactGap);
        auto row4 = controlsArea.removeFromTop(compactRowHeight);
        auto cols4 = splitRow(row4, gap);
        modCurveTypeLabel.setBounds(cols4.first.removeFromLeft(30));
        modCurveTypeBox.setBounds(cols4.first);
        modShapeLabel.setBounds(cols4.second.removeFromLeft(30));
        modShapeBox.setBounds(cols4.second);
        return;
    }

    loadButton.setVisible(true);
    muteButton.setVisible(true);
    soloButton.setVisible(true);
    directionModeBox.setVisible(true);
    modTargetLabel.setVisible(false);
    modTargetBox.setVisible(false);
    modBipolarToggle.setVisible(false);
    modDepthLabel.setVisible(false);
    modDepthSlider.setVisible(false);
    modOffsetLabel.setVisible(false);
    modOffsetSlider.setVisible(false);
    modCurveBendLabel.setVisible(false);
    modCurveBendSlider.setVisible(false);
    modLengthLabel.setVisible(false);
    modLengthBox.setVisible(false);
    for (auto& tab : modSequencerTabs)
        tab.setVisible(false);
    modPitchQuantToggle.setVisible(false);
    modPitchScaleBox.setVisible(false);
    modShapeLabel.setVisible(false);
    modShapeBox.setVisible(false);
    modCurveTypeLabel.setVisible(false);
    modCurveTypeBox.setVisible(false);
    
    // Controls column on right
    controlsArea.reduce(4, 0);
    
    const bool isGrainMode = grainOverlayVisible;
    const bool isGrainSpacePage = isGrainMode && grainSubPage == GrainSubPage::Space;
    const bool isStepMode = showingStepDisplay;
    patternLengthBox.setBounds({});
    stepLengthReadoutBox.setBounds({});
    laneMidiChannelLabel.setBounds({});
    laneMidiChannelBox.setBounds({});
    laneMidiNoteLabel.setBounds({});
    laneMidiNoteBox.setBounds({});

    const int rowGap = isGrainMode ? 0 : 1;

    // Top row: compact load + direction mode to keep all strip controls visible.
    const int topRowHeight = isGrainMode ? (isGrainSpacePage ? 12 : 14) : 17;
    auto topRow = controlsArea.removeFromTop(topRowHeight);
    const int loadWidth = juce::jlimit(24, isGrainMode ? 34 : 42, topRow.getWidth() / 7);
    const int toggleWidth = juce::jlimit(isGrainMode ? 22 : 30, isGrainMode ? 32 : 44, topRow.getWidth() / 5);
    loadButton.setBounds(topRow.removeFromLeft(loadWidth));
    topRow.removeFromLeft(2);
    muteButton.setBounds(topRow.removeFromLeft(toggleWidth));
    topRow.removeFromLeft(1);
    soloButton.setBounds(topRow.removeFromLeft(toggleWidth));
    topRow.removeFromLeft(2);
    if (stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels)
    {
        const int catLabelW = 22;
        const int catBoxW = juce::jlimit(58, 88, topRow.getWidth() / 3);
        beatSpaceCategoryLabel.setBounds(topRow.removeFromLeft(catLabelW));
        beatSpaceCategoryBox.setBounds(topRow.removeFromLeft(catBoxW));
        topRow.removeFromLeft(2);
    }
    else
    {
        beatSpaceCategoryLabel.setBounds({});
        beatSpaceCategoryBox.setBounds({});
    }
    directionModeBox.setBounds(topRow.reduced(1, 0));
    controlsArea.removeFromTop(rowGap);
    
    // Check if we have enough height for compact tempo controls.
    const int requiredTopControlsHeight = 34;
    bool showTempoControls = (!isGrainMode && !isStepMode) && (controlsArea.getHeight() >= requiredTopControlsHeight);

    tempoLabel.setVisible(showTempoControls);

    // Tempo controls row - only if we have space
    if (showTempoControls)
    {
        auto tempoRow = controlsArea.removeFromTop(22);
        tempoLabel.setBounds(tempoRow.removeFromLeft(44));
        controlsArea.removeFromTop(2);

        auto recBarsRow = controlsArea.removeFromTop(18);
        if (isStepMode)
        {
            const int lenWidth = juce::jlimit(18, 26, recBarsRow.getWidth());
            patternLengthBox.setBounds(recBarsRow.removeFromLeft(lenWidth));
            if (recBarsRow.getWidth() > 0)
            {
                recBarsRow.removeFromLeft(3);
                const int readoutWidth = juce::jlimit(34, 62, recBarsRow.getWidth());
                stepLengthReadoutBox.setBounds(recBarsRow.removeFromLeft(readoutWidth));
            }
        }
        controlsArea.removeFromTop(2);
    }
    else if (isStepMode)
    {
        // Keep step strip controls legible first; auxiliary rows appear only on taller strips.
        const bool showStepAuxControls = controlsArea.getHeight() >= 86;
        if (showStepAuxControls)
        {
            auto lenRow = controlsArea.removeFromTop(14);
            const int lenWidth = juce::jlimit(18, 24, lenRow.getWidth());
            patternLengthBox.setBounds(lenRow.removeFromLeft(lenWidth));
            if (lenRow.getWidth() > 0)
            {
                lenRow.removeFromLeft(3);
                const int readoutWidth = juce::jlimit(28, 56, lenRow.getWidth());
                stepLengthReadoutBox.setBounds(lenRow.removeFromLeft(readoutWidth));
            }
            controlsArea.removeFromTop(1);

            auto midiRow = controlsArea.removeFromTop(14);
            laneMidiChannelLabel.setBounds(midiRow.removeFromLeft(16));
            laneMidiChannelBox.setBounds(midiRow.removeFromLeft(30));
            midiRow.removeFromLeft(4);
            laneMidiNoteLabel.setBounds(midiRow.removeFromLeft(26));
            laneMidiNoteBox.setBounds(midiRow.removeFromLeft(38));
            controlsArea.removeFromTop(1);
        }
    }
    
    if (isStepMode)
    {
        // Dedicated step layout keeps VOL/PAN and A/D/R visible on small strip heights.
        const int stepGap = 2;
        auto stepArea = controlsArea;

        const int totalStepHeight = juce::jmax(1, stepArea.getHeight());
        const int labelHeight = juce::jlimit(8, 10, juce::jmax(8, totalStepHeight / 8));
        const int knobBudget = juce::jmax(24, totalStepHeight - (2 * labelHeight) - (3 * stepGap));
        const int volKnobHeight = juce::jmax(12, knobBudget / 2);
        const int envKnobHeight = juce::jmax(12, knobBudget - volKnobHeight);

        auto volRow = stepArea.removeFromTop(volKnobHeight);
        const int pairWidth = juce::jmax(8, (volRow.getWidth() - stepGap) / 2);
        auto volArea = volRow.removeFromLeft(pairWidth);
        volRow.removeFromLeft(stepGap);
        auto panArea = volRow;
        volumeSlider.setBounds(volArea.reduced(1));
        panSlider.setBounds(panArea.reduced(1));

        stepArea.removeFromTop(2);
        auto volLabelsRow = stepArea.removeFromTop(labelHeight);
        const int pairLabelWidth = juce::jmax(8, (volLabelsRow.getWidth() - stepGap) / 2);
        volumeLabel.setBounds(volLabelsRow.removeFromLeft(pairLabelWidth));
        volLabelsRow.removeFromLeft(stepGap);
        panLabel.setBounds(volLabelsRow);

        stepArea.removeFromTop(stepGap);
        auto envRow = stepArea.removeFromTop(envKnobHeight);
        const int envGap = 3;
        const int envWidth = juce::jmax(8, (envRow.getWidth() - (2 * envGap)) / 3);
        stepAttackSlider.setBounds(envRow.removeFromLeft(envWidth).reduced(0));
        envRow.removeFromLeft(envGap);
        stepDecaySlider.setBounds(envRow.removeFromLeft(envWidth).reduced(0));
        envRow.removeFromLeft(envGap);
        stepReleaseSlider.setBounds(envRow.reduced(0));

        stepArea.removeFromTop(stepGap);
        auto envLabelsRow = stepArea.removeFromTop(labelHeight);
        const int envLabelWidth = juce::jmax(8, (envLabelsRow.getWidth() - (2 * envGap)) / 3);
        stepAttackLabel.setBounds(envLabelsRow.removeFromLeft(envLabelWidth));
        envLabelsRow.removeFromLeft(envGap);
        stepDecayLabel.setBounds(envLabelsRow.removeFromLeft(envLabelWidth));
        envLabelsRow.removeFromLeft(envGap);
        stepReleaseLabel.setBounds(envLabelsRow);
        return;
    }

    // Rotary knobs row.
    const int knobsRowHeight = isGrainMode ? (isGrainSpacePage ? 20 : 22) : 30;
    auto knobsRow = controlsArea.removeFromTop(knobsRowHeight);
    int totalWidth = knobsRow.getWidth();
    int mainKnobsWidth = (totalWidth * 7) / 10;
    int mainKnobWidth = mainKnobsWidth / 3;

    if (isGrainMode)
    {
        grainSizeSlider.setBounds(knobsRow.removeFromLeft(mainKnobWidth).reduced(1));
        grainDensitySlider.setBounds(knobsRow.removeFromLeft(mainKnobWidth).reduced(1));
        knobsRow.removeFromLeft(mainKnobWidth);
    }
    else
    {
        std::array<juce::Slider*, 6> visibleKnobs{};
        int visibleCount = 0;
        auto addVisibleKnob = [&](juce::Slider& knob)
        {
            if (knob.isVisible() && visibleCount < static_cast<int>(visibleKnobs.size()))
                visibleKnobs[static_cast<size_t>(visibleCount++)] = &knob;
        };
        addVisibleKnob(volumeSlider);
        addVisibleKnob(panSlider);
        addVisibleKnob(pitchSlider);
        addVisibleKnob(speedSlider);
        addVisibleKnob(scratchSlider);
        addVisibleKnob(sliceLengthSlider);

        const bool denseLayout = visibleCount >= 6;
        const int knobGap = denseLayout ? 1 : 2;
        const int totalGap = knobGap * juce::jmax(0, visibleCount - 1);
        const int knobWidth = juce::jmax(8, (knobsRow.getWidth() - totalGap) / juce::jmax(1, visibleCount));
        for (int i = 0; i < visibleCount; ++i)
        {
            if (auto* knob = visibleKnobs[static_cast<size_t>(i)])
                knob->setBounds(knobsRow.removeFromLeft(knobWidth).reduced(denseLayout ? 2 : 1));
            if (i < (visibleCount - 1))
                knobsRow.removeFromLeft(knobGap);
        }
    }

    const int labelsRowHeight = isGrainMode ? (isGrainSpacePage ? 9 : 10) : 9;
    auto labelsRow = controlsArea.removeFromTop(labelsRowHeight);
    if (isGrainMode)
    {
        auto sizeLabelArea = labelsRow.removeFromLeft(mainKnobWidth);
        const int syncToggleW = 14;
        const int syncModeW = 30;
        const int labelW = juce::jmax(0, sizeLabelArea.getWidth() - syncToggleW - syncModeW - 4);
        grainSizeLabel.setBounds(sizeLabelArea.removeFromLeft(labelW));
        sizeLabelArea.removeFromLeft(2);
        grainSizeSyncToggle.setBounds(sizeLabelArea.removeFromLeft(syncToggleW));
        sizeLabelArea.removeFromLeft(2);
        grainSizeDivLabel.setBounds(sizeLabelArea.removeFromLeft(syncModeW));
        grainDensityLabel.setBounds(labelsRow.removeFromLeft(mainKnobWidth));
        labelsRow.removeFromLeft(mainKnobWidth);
    }
    else
    {
        std::array<juce::Label*, 6> visibleLabels{};
        int visibleCount = 0;
        auto addVisibleLabel = [&](juce::Label& label)
        {
            if (label.isVisible() && visibleCount < static_cast<int>(visibleLabels.size()))
                visibleLabels[static_cast<size_t>(visibleCount++)] = &label;
        };
        addVisibleLabel(volumeLabel);
        addVisibleLabel(panLabel);
        addVisibleLabel(pitchLabel);
        addVisibleLabel(speedLabel);
        addVisibleLabel(scratchLabel);
        addVisibleLabel(sliceLengthLabel);

        const int labelGap = 2;
        const int totalGap = labelGap * juce::jmax(0, visibleCount - 1);
        const int labelWidth = juce::jmax(8, (labelsRow.getWidth() - totalGap) / juce::jmax(1, visibleCount));
        for (int i = 0; i < visibleCount; ++i)
        {
            if (auto* label = visibleLabels[static_cast<size_t>(i)])
                label->setBounds(labelsRow.removeFromLeft(labelWidth));
            if (i < (visibleCount - 1))
                labelsRow.removeFromLeft(labelGap);
        }
    }
    if (!isGrainMode)
        return;

    controlsArea.removeFromTop(isGrainSpacePage ? 0 : 1);
    auto tabRow = controlsArea.removeFromTop(isGrainSpacePage ? 11 : 13);
    const int tabGap = 2;
    const int tabW = juce::jmax(1, (tabRow.getWidth() - (2 * tabGap)) / 3);
    grainTabPitchButton.setBounds(tabRow.removeFromLeft(tabW));
    tabRow.removeFromLeft(tabGap);
    grainTabSpaceButton.setBounds(tabRow.removeFromLeft(tabW));
    tabRow.removeFromLeft(tabGap);
    grainTabShapeButton.setBounds(tabRow);

    controlsArea.removeFromTop(isGrainSpacePage ? 1 : 2);
    const int rowGapMini = isGrainSpacePage ? 1 : 2;
    const int totalMiniRows = (grainSubPage == GrainSubPage::Space) ? 3 : 2;
    const int minMiniRowH = isGrainSpacePage ? 8 : 12;
    const int maxMiniRowH = isGrainSpacePage ? 20 : 22;
    const int rowH = juce::jlimit(minMiniRowH,
                                  maxMiniRowH,
                                  (controlsArea.getHeight() - ((totalMiniRows - 1) * rowGapMini)) / totalMiniRows);
    const int miniLabelW = isGrainSpacePage ? 30 : 34;
    auto layoutGrainMiniRow = [&](juce::Label& labelA, juce::Slider& sliderA,
                                  juce::Label& labelB, juce::Slider& sliderB)
    {
        if (controlsArea.getHeight() <= 0)
            return;
        auto row = controlsArea.removeFromTop(rowH);
        auto left = row.removeFromLeft(row.getWidth() / 2);
        labelA.setBounds(left.removeFromLeft(miniLabelW));
        sliderA.setBounds(left);
        row.removeFromLeft(2);
        labelB.setBounds(row.removeFromLeft(miniLabelW));
        sliderB.setBounds(row);
        if (controlsArea.getHeight() > rowGapMini)
            controlsArea.removeFromTop(rowGapMini);
    };
    auto layoutGrainSingleRow = [&](juce::Label& label, juce::Slider& slider)
    {
        if (controlsArea.getHeight() <= 0)
            return;
        auto row = controlsArea.removeFromTop(rowH);
        label.setBounds(row.removeFromLeft(miniLabelW));
        slider.setBounds(row);
        if (controlsArea.getHeight() > rowGapMini)
            controlsArea.removeFromTop(rowGapMini);
    };

    if (grainSubPage == GrainSubPage::Pitch)
    {
        if (controlsArea.getHeight() > 0)
        {
            layoutGrainMiniRow(grainPitchLabel, grainPitchSlider, grainPitchJitterLabel, grainPitchJitterSlider);
        }
        layoutGrainMiniRow(grainArpLabel, grainArpSlider, grainRandomLabel, grainRandomSlider);
    }
    else if (grainSubPage == GrainSubPage::Space)
    {
        layoutGrainMiniRow(grainSpreadLabel, grainSpreadSlider, grainJitterLabel, grainJitterSlider);
        layoutGrainMiniRow(grainCloudLabel, grainCloudSlider, grainEmitterLabel, grainEmitterSlider);
        layoutGrainSingleRow(grainPositionJitterLabel, grainPositionJitterSlider);
    }
    else
    {
        layoutGrainMiniRow(grainEnvelopeLabel, grainEnvelopeSlider, grainShapeLabel, grainShapeSlider);
    }
}


void StripControl::rebuildBeatSpaceCategoryMenu()
{
    if (stripIndex < 0 || stripIndex >= StepVstHostAudioProcessor::BeatSpaceChannels)
        return;

    const int selectedBefore = beatSpaceCategoryBox.getSelectedId();
    const int assignedSpace = processor.getBeatSpaceChannelSpaceAssignment(stripIndex);
    const int bookmarkCount = processor.getBeatSpaceBookmarkCount(stripIndex);

    suppressBeatSpaceCategoryAction = true;
    beatSpaceCategoryBox.clear(juce::dontSendNotification);
    for (int space = 0; space < StepVstHostAudioProcessor::BeatSpaceChannels; ++space)
        beatSpaceCategoryBox.addItem(StepVstHostAudioProcessor::getBeatSpaceSpaceName(space), space + 1);

    beatSpaceCategoryBox.addSeparator();
    beatSpaceCategoryBox.addItem("Nav Coarse", kBeatSpaceActionNavCoarse);
    beatSpaceCategoryBox.addItem("Nav Fine", kBeatSpaceActionNavFine);

    beatSpaceCategoryBox.addSeparator();
    beatSpaceCategoryBox.addItem("Lock 0%", kBeatSpaceActionZoneLock0);
    beatSpaceCategoryBox.addItem("Lock 25%", kBeatSpaceActionZoneLock25);
    beatSpaceCategoryBox.addItem("Lock 50%", kBeatSpaceActionZoneLock50);
    beatSpaceCategoryBox.addItem("Lock 75%", kBeatSpaceActionZoneLock75);
    beatSpaceCategoryBox.addItem("Lock 100%", kBeatSpaceActionZoneLock100);

    beatSpaceCategoryBox.addSeparator();
    beatSpaceCategoryBox.addItem("Random Zone", kBeatSpaceActionRandomZone);
    beatSpaceCategoryBox.addItem("Random Near", kBeatSpaceActionRandomNear);
    beatSpaceCategoryBox.addItem("Random Character", kBeatSpaceActionRandomCharacter);
    beatSpaceCategoryBox.addItem("Random Full", kBeatSpaceActionRandomFull);
    beatSpaceCategoryBox.addItem("Nearest 8", kBeatSpaceActionNearest8);

    beatSpaceCategoryBox.addSeparator();
    beatSpaceCategoryBox.addItem("Path Add Point", kBeatSpaceActionPathAdd);
    beatSpaceCategoryBox.addItem("Path Start 1/4", kBeatSpaceActionPathStartQuarter);
    beatSpaceCategoryBox.addItem("Path Start 1 Bar", kBeatSpaceActionPathStartBar);
    beatSpaceCategoryBox.addItem("Path Stop", kBeatSpaceActionPathStop);
    beatSpaceCategoryBox.addItem("Path Clear", kBeatSpaceActionPathClear);

    beatSpaceCategoryBox.addSeparator();
    beatSpaceCategoryBox.addItem("Bookmark Save Current", kBeatSpaceActionBookmarkAdd);
    for (int slot = 0; slot < kBeatSpaceBookmarkSlots; ++slot)
    {
        const auto tag = processor.getBeatSpaceBookmarkTag(stripIndex, slot).trim();
        if (tag.isEmpty())
            continue;
        beatSpaceCategoryBox.addItem(
            "Bookmark " + juce::String(slot + 1) + " " + tag,
            kBeatSpaceActionBookmarkRecallBase + slot);
    }
    if (bookmarkCount > 0)
        beatSpaceCategoryBox.addItem("Bookmark Clear", kBeatSpaceActionBookmarkClear);

    int restoreId = assignedSpace + 1;
    if (selectedBefore >= 1 && selectedBefore <= kBeatSpaceCategoryCount)
        restoreId = selectedBefore;
    beatSpaceCategoryBox.setSelectedId(restoreId, juce::dontSendNotification);
    suppressBeatSpaceCategoryAction = false;
}

bool StripControl::handleBeatSpaceCategoryAction(int selectedId)
{
    if (stripIndex < 0 || stripIndex >= StepVstHostAudioProcessor::BeatSpaceChannels)
        return false;

    auto setZoom = [this](int targetLevel)
    {
        auto state = processor.getBeatSpaceVisualState();
        targetLevel = juce::jlimit(0, 2, targetLevel);
        while (state.zoomLevel < targetLevel)
        {
            processor.beatSpaceAdjustZoom(+1);
            state = processor.getBeatSpaceVisualState();
        }
        while (state.zoomLevel > targetLevel)
        {
            processor.beatSpaceAdjustZoom(-1);
            state = processor.getBeatSpaceVisualState();
        }
    };

    switch (selectedId)
    {
        case kBeatSpaceActionNavCoarse:
            setZoom(0);
            break;
        case kBeatSpaceActionNavFine:
            setZoom(2);
            break;
        case kBeatSpaceActionZoneLock0:
            processor.setBeatSpaceZoneLockStrength(stripIndex, 0.0f);
            break;
        case kBeatSpaceActionZoneLock25:
            processor.setBeatSpaceZoneLockStrength(stripIndex, 0.25f);
            break;
        case kBeatSpaceActionZoneLock50:
            processor.setBeatSpaceZoneLockStrength(stripIndex, 0.50f);
            break;
        case kBeatSpaceActionZoneLock75:
            processor.setBeatSpaceZoneLockStrength(stripIndex, 0.75f);
            break;
        case kBeatSpaceActionZoneLock100:
            processor.setBeatSpaceZoneLockStrength(stripIndex, 1.0f);
            break;
        case kBeatSpaceActionRandomZone:
            processor.setBeatSpaceRandomizeMode(StepVstHostAudioProcessor::BeatSpaceRandomizeMode::WithinCategory);
            processor.beatSpaceRandomizeChannel(stripIndex, StepVstHostAudioProcessor::BeatSpaceRandomizeMode::WithinCategory, true);
            break;
        case kBeatSpaceActionRandomNear:
            processor.setBeatSpaceRandomizeMode(StepVstHostAudioProcessor::BeatSpaceRandomizeMode::NearCurrent);
            processor.beatSpaceRandomizeChannel(stripIndex, StepVstHostAudioProcessor::BeatSpaceRandomizeMode::NearCurrent, true);
            break;
        case kBeatSpaceActionRandomCharacter:
            processor.setBeatSpaceRandomizeMode(StepVstHostAudioProcessor::BeatSpaceRandomizeMode::PreserveCharacter);
            processor.beatSpaceRandomizeChannel(stripIndex, StepVstHostAudioProcessor::BeatSpaceRandomizeMode::PreserveCharacter, true);
            break;
        case kBeatSpaceActionRandomFull:
            processor.setBeatSpaceRandomizeMode(StepVstHostAudioProcessor::BeatSpaceRandomizeMode::FullWild);
            processor.beatSpaceRandomizeChannel(stripIndex, StepVstHostAudioProcessor::BeatSpaceRandomizeMode::FullWild, true);
            break;
        case kBeatSpaceActionNearest8:
        {
            const auto nearest = processor.getBeatSpaceNearestPresetSlots(stripIndex, 8);
            if (!nearest.empty())
            {
                const int assignedSpace = processor.getBeatSpaceChannelSpaceAssignment(stripIndex);
                const auto spaceName = StepVstHostAudioProcessor::getBeatSpaceSpaceName(assignedSpace);
                juce::PopupMenu menu;
                menu.addSectionHeader("Nearest 8 (" + spaceName + ")");
                for (const int slot : nearest)
                {
                    const int safeSlot = juce::jlimit(0, StepVstHostAudioProcessor::BeatSpacePresetSlotsPerSpace - 1, slot);
                    menu.addItem(
                        safeSlot + 1,
                        "#" + juce::String(safeSlot + 1).paddedLeft('0', 2));
                }
                auto safeThis = juce::Component::SafePointer<StripControl>(this);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&beatSpaceCategoryBox),
                    [safeThis](int selected)
                    {
                        if (safeThis == nullptr || selected <= 0)
                            return;
                        safeThis->processor.loadBeatSpacePresetFromAssignedSpace(
                            safeThis->stripIndex,
                            selected - 1);
                    });
            }
            break;
        }
        case kBeatSpaceActionPathAdd:
            processor.beatSpacePathAddCurrentPoint(stripIndex);
            break;
        case kBeatSpaceActionPathStartQuarter:
            processor.beatSpacePathStart(stripIndex, StepVstHostAudioProcessor::BeatSpacePathMode::QuarterNote);
            break;
        case kBeatSpaceActionPathStartBar:
            processor.beatSpacePathStart(stripIndex, StepVstHostAudioProcessor::BeatSpacePathMode::OneBar);
            break;
        case kBeatSpaceActionPathStop:
            processor.beatSpacePathStop(stripIndex);
            break;
        case kBeatSpaceActionPathClear:
            processor.beatSpacePathClear(stripIndex);
            break;
        case kBeatSpaceActionBookmarkAdd:
        {
            const auto state = processor.getBeatSpaceVisualState();
            const auto p = state.channelPoints[static_cast<size_t>(stripIndex)];
            const auto tag = "X" + juce::String(p.x) + " Y" + juce::String(p.y);
            processor.beatSpaceAddBookmark(stripIndex, tag);
            break;
        }
        case kBeatSpaceActionBookmarkClear:
            processor.beatSpaceClearBookmarks(stripIndex);
            break;
        default:
            if (selectedId >= kBeatSpaceActionBookmarkRecallBase
                && selectedId < (kBeatSpaceActionBookmarkRecallBase + kBeatSpaceBookmarkSlots))
            {
                const int slot = selectedId - kBeatSpaceActionBookmarkRecallBase;
                processor.beatSpaceRecallBookmark(stripIndex, slot, true);
            }
            break;
    }

    rebuildBeatSpaceCategoryMenu();
    return true;
}

void StripControl::loadSample()
{
    if (stripIndex >= 0 && stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels)
    {
        const int assignedSpace = processor.getBeatSpaceChannelSpaceAssignment(stripIndex);
        const auto spaceName = StepVstHostAudioProcessor::getBeatSpaceSpaceName(assignedSpace);
        const auto displaySlots = processor.getBeatSpacePresetDisplaySlotsForAssignedSpace(stripIndex);

        juce::PopupMenu menu;
        menu.addSectionHeader("BeatSpace " + spaceName + " (Lane " + juce::String(stripIndex + 1) + ")");

        const auto nearest = processor.getBeatSpaceNearestPresetSlots(stripIndex, 8);
        if (!nearest.empty())
        {
            menu.addSectionHeader("Nearest 8");
            for (const int slot : nearest)
            {
                const int safeSlot = juce::jlimit(0, StepVstHostAudioProcessor::BeatSpacePresetSlotsPerSpace - 1, slot);
                menu.addItem(
                    kBeatSpaceMenuNearestBase + safeSlot,
                    "Near " + juce::String(safeSlot + 1).paddedLeft('0', 2)
                        + "  " + processor.getBeatSpacePresetLabelForSpaceSlot(assignedSpace, safeSlot));
            }
            menu.addSeparator();
        }

        for (int displayIndex = 0; displayIndex < static_cast<int>(displaySlots.size()); ++displayIndex)
        {
            const int slot = displaySlots[static_cast<size_t>(displayIndex)];
            const auto label = processor.getBeatSpacePresetLabelForSpaceSlot(assignedSpace, slot);
            menu.addItem(
                kBeatSpaceMenuSlotBase + displayIndex,
                juce::String("#") + juce::String(slot + 1).paddedLeft('0', 2) + "  " + label);
        }

        juce::PopupMenu manageMenu;
        for (int displayIndex = 0; displayIndex < static_cast<int>(displaySlots.size()); ++displayIndex)
        {
            const int slot = displaySlots[static_cast<size_t>(displayIndex)];
            const auto label = processor.getBeatSpacePresetLabelForSpaceSlot(assignedSpace, slot);
            juce::PopupMenu slotMenu;
            slotMenu.addItem(kBeatSpaceMenuManageRenameBase + displayIndex, "Rename...");
            slotMenu.addItem(kBeatSpaceMenuManageMoveUpBase + displayIndex, "Move Up", displayIndex > 0);
            slotMenu.addItem(kBeatSpaceMenuManageMoveDownBase + displayIndex, "Move Down",
                             displayIndex < (static_cast<int>(displaySlots.size()) - 1));
            slotMenu.addItem(kBeatSpaceMenuManageDeleteBase + displayIndex, "Delete",
                             displaySlots.size() > 1);
            manageMenu.addSubMenu(
                juce::String("#") + juce::String(slot + 1).paddedLeft('0', 2) + "  " + label,
                slotMenu);
        }
        manageMenu.addSeparator();
        manageMenu.addItem(kBeatSpaceMenuManageReset, "Reset Slot List");
        menu.addSeparator();
        menu.addSubMenu("Manage Presets", manageMenu);

        auto safeThis = juce::Component::SafePointer<StripControl>(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadButton),
            [safeThis](int selectedId)
            {
                if (safeThis == nullptr || selectedId <= 0)
                    return;

                if (selectedId >= kBeatSpaceMenuSlotBase
                    && selectedId < (kBeatSpaceMenuSlotBase + StepVstHostAudioProcessor::BeatSpacePresetSlotsPerSpace))
                {
                    const int displayIndex = selectedId - kBeatSpaceMenuSlotBase;
                    safeThis->processor.loadBeatSpacePresetFromAssignedSpaceDisplayIndex(
                        safeThis->stripIndex,
                        displayIndex);
                    return;
                }

                if (selectedId >= kBeatSpaceMenuNearestBase
                    && selectedId < (kBeatSpaceMenuNearestBase + StepVstHostAudioProcessor::BeatSpacePresetSlotsPerSpace))
                {
                    const int slot = selectedId - kBeatSpaceMenuNearestBase;
                    safeThis->processor.loadBeatSpacePresetFromAssignedSpace(
                        safeThis->stripIndex,
                        slot);
                    return;
                }

                if (selectedId >= kBeatSpaceMenuManageMoveUpBase
                    && selectedId < (kBeatSpaceMenuManageMoveUpBase + StepVstHostAudioProcessor::BeatSpacePresetSlotsPerSpace))
                {
                    const int displayIndex = selectedId - kBeatSpaceMenuManageMoveUpBase;
                    safeThis->processor.moveBeatSpacePresetForAssignedSpace(
                        safeThis->stripIndex,
                        displayIndex,
                        displayIndex - 1);
                    return;
                }

                if (selectedId >= kBeatSpaceMenuManageMoveDownBase
                    && selectedId < (kBeatSpaceMenuManageMoveDownBase + StepVstHostAudioProcessor::BeatSpacePresetSlotsPerSpace))
                {
                    const int displayIndex = selectedId - kBeatSpaceMenuManageMoveDownBase;
                    safeThis->processor.moveBeatSpacePresetForAssignedSpace(
                        safeThis->stripIndex,
                        displayIndex,
                        displayIndex + 1);
                    return;
                }

                if (selectedId >= kBeatSpaceMenuManageDeleteBase
                    && selectedId < (kBeatSpaceMenuManageDeleteBase + StepVstHostAudioProcessor::BeatSpacePresetSlotsPerSpace))
                {
                    const int displayIndex = selectedId - kBeatSpaceMenuManageDeleteBase;
                    safeThis->processor.deleteBeatSpacePresetForAssignedSpace(
                        safeThis->stripIndex,
                        displayIndex);
                    return;
                }

                if (selectedId >= kBeatSpaceMenuManageRenameBase
                    && selectedId < (kBeatSpaceMenuManageRenameBase + StepVstHostAudioProcessor::BeatSpacePresetSlotsPerSpace))
                {
                    const int displayIndex = selectedId - kBeatSpaceMenuManageRenameBase;
                    const int spaceForRename = safeThis->processor.getBeatSpaceChannelSpaceAssignment(safeThis->stripIndex);
                    const auto slotsForRename = safeThis->processor.getBeatSpacePresetDisplaySlotsForAssignedSpace(safeThis->stripIndex);
                    if (displayIndex < 0 || displayIndex >= static_cast<int>(slotsForRename.size()))
                        return;

                    const int slot = slotsForRename[static_cast<size_t>(displayIndex)];
                    const auto currentLabel = safeThis->processor.getBeatSpacePresetLabelForSpaceSlot(spaceForRename, slot);

                    auto* renameWindow = new juce::AlertWindow(
                        "Rename BeatSpace Preset",
                        "Enter display name for this preset slot.",
                        juce::AlertWindow::NoIcon);
                    renameWindow->addTextEditor("name", currentLabel, "Name:");
                    renameWindow->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
                    renameWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                    renameWindow->setEscapeKeyCancels(true);
                    auto safeWindow = juce::Component::SafePointer<juce::AlertWindow>(renameWindow);
                    renameWindow->enterModalState(
                        true,
                        juce::ModalCallbackFunction::create(
                            [safeThis, safeWindow, displayIndex](int result)
                            {
                                if (safeThis == nullptr || safeWindow == nullptr || result != 1)
                                    return;
                                safeThis->processor.renameBeatSpacePresetForAssignedSpace(
                                    safeThis->stripIndex,
                                    displayIndex,
                                    safeWindow->getTextEditorContents("name"));
                            }),
                        true);
                    renameWindow->centreAroundComponent(safeThis.getComponent(), 360, 140);
                    return;
                }

                if (selectedId == kBeatSpaceMenuManageReset)
                {
                    safeThis->processor.resetBeatSpacePresetLayoutForAssignedSpace(
                        safeThis->stripIndex);
                }
            });
        return;
    }

    // Get current play mode to determine which path to use
    auto* strip = processor.getAudioEngine()->getStrip(stripIndex);
    bool isStepMode = (strip && strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step);
    auto mode = isStepMode ? StepVstHostAudioProcessor::SamplePathMode::Step
                           : StepVstHostAudioProcessor::SamplePathMode::Loop;
    juce::File startingDirectory = processor.getDefaultSampleDirectory(stripIndex, mode);
    
    // If no last path, use default
    if (!startingDirectory.exists())
        startingDirectory = juce::File();
    
    juce::FileChooser chooser("Load Sample", startingDirectory, "*.wav;*.aif;*.aiff;*.mp3;*.ogg;*.flac");
    
    if (chooser.browseForFileToOpen())
    {
        loadSampleFromFile(chooser.getResult());
    }
}

bool StripControl::isSupportedAudioFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    return file.hasFileExtension(".wav;.aif;.aiff;.mp3;.ogg;.flac");
}

void StripControl::loadSampleFromFile(const juce::File& file)
{
    if (!isSupportedAudioFile(file))
        return;

    processor.loadSampleToStrip(stripIndex, file);

    auto* strip = processor.getAudioEngine() ? processor.getAudioEngine()->getStrip(stripIndex) : nullptr;
    const bool isStepMode = (strip && strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step);
    auto mode = isStepMode ? StepVstHostAudioProcessor::SamplePathMode::Step
                           : StepVstHostAudioProcessor::SamplePathMode::Loop;
    processor.setDefaultSampleDirectory(stripIndex, mode, file.getParentDirectory());
}

bool StripControl::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        if (isSupportedAudioFile(juce::File(path)))
            return true;
    }
    return false;
}

void StripControl::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    for (const auto& path : files)
    {
        juce::File file(path);
        if (isSupportedAudioFile(file))
        {
            loadSampleFromFile(file);
            break;
        }
    }
}

void StripControl::updateFromEngine()
{
    if (!processor.getAudioEngine()) return;
    
    auto* strip = processor.getAudioEngine()->getStrip(stripIndex);
    if (!strip) return;
    const auto modState = processor.getAudioEngine()->getModSequencerState(stripIndex);
    const auto modulates = [&](ModernAudioEngine::ModTarget t)
    {
        return modState.target == t;
    };

    if (modulationLaneView)
    {
        const auto mod = processor.getAudioEngine()->getModSequencerState(stripIndex);
        modTargetBox.setSelectedId(modTargetToComboId(mod.target), juce::dontSendNotification);
        modBipolarToggle.setToggleState(mod.bipolar, juce::dontSendNotification);
        modBipolarToggle.setEnabled(modTargetAllowsBipolar(mod.target));
        modDepthSlider.setValue(mod.depth, juce::dontSendNotification);
        modOffsetSlider.setValue(mod.smoothingMs, juce::dontSendNotification);
        modCurveBendSlider.setValue(mod.curveBend, juce::dontSendNotification);
        modLengthBox.setSelectedId(mod.lengthBars, juce::dontSendNotification);
        modPitchQuantToggle.setToggleState(mod.pitchScaleQuantize, juce::dontSendNotification);
        modPitchScaleBox.setSelectedId(pitchScaleToComboId(static_cast<ModernAudioEngine::PitchScale>(mod.pitchScale)), juce::dontSendNotification);
        modPitchScaleBox.setEnabled(mod.pitchScaleQuantize);
        modShapeBox.setSelectedId(mod.curveMode ? 1 : 2, juce::dontSendNotification);
        modCurveTypeBox.setSelectedId(curveShapeToComboId(static_cast<ModernAudioEngine::ModCurveShape>(mod.curveShape)),
                                      juce::dontSendNotification);
        modCurveBendSlider.setEnabled(mod.curveMode);
        modCurveTypeBox.setEnabled(mod.curveMode);
        updateModSequencerTabButtons();
        repaint();
        return;
    }

    const bool isStepMode = (strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step);
    if (showingStepDisplay != isStepMode)
    {
        showingStepDisplay = isStepMode;
        waveform.setVisible(!isStepMode);
        stepDisplay.setVisible(isStepMode);
        patternLengthBox.setVisible(isStepMode);
        stepLengthReadoutBox.setVisible(isStepMode);
        updateGrainOverlayVisibility();
        resized();
    }
    
    // Update step display if in step mode
    if (showingStepDisplay)
    {
        stepDisplay.setStepPattern(strip->stepPattern, strip->getStepTotalSteps());
        stepDisplay.setStepSubdivisions(strip->stepSubdivisions);
        stepDisplay.setStepSubdivisionVelocityRange(strip->stepSubdivisionStartVelocity,
                                                    strip->stepSubdivisionRepeatVelocity);
        stepDisplay.setStepProbability(strip->stepProbability);
        stepDisplay.setCurrentStep(strip->currentStep);
        stepDisplay.setPlaying(strip->isPlaying());

        if (processor.isStepEditModeActive())
        {
            StepSequencerDisplay::EditTool mappedTool = StepSequencerDisplay::EditTool::Volume;
            switch (processor.getStepEditToolIndex())
            {
                case 1: mappedTool = StepSequencerDisplay::EditTool::Volume; break;
                case 2: mappedTool = StepSequencerDisplay::EditTool::Divide; break;
                case 3: mappedTool = StepSequencerDisplay::EditTool::RampUp; break;
                case 4: mappedTool = StepSequencerDisplay::EditTool::RampDown; break;
                case 5: mappedTool = StepSequencerDisplay::EditTool::Probability; break;
                case 0:
                case 6:
                case 7:
                case 8:
                default: mappedTool = StepSequencerDisplay::EditTool::Volume; break;
            }

            if (stepDisplay.getActiveTool() != mappedTool)
                stepDisplay.setActiveTool(mappedTool);
        }

        // No playback position indicator in step mode - just show steps
    }
    
    // Update waveform display (only if visible - i.e., not in step mode)
    if (!showingStepDisplay && strip->hasAudio())
    {
        auto* buffer = strip->getAudioBuffer();
        if (buffer && buffer->getNumSamples() > 0)
        {
            waveform.setAudioBuffer(*buffer, strip->getSourceSampleRate());
            waveform.setLoopPoints(strip->getLoopStart(), strip->getLoopEnd(), 16);
            waveform.setSliceMarkers(strip->getSliceStartSamples(false),
                                     strip->getSliceStartSamples(true),
                                     buffer->getNumSamples(),
                                     false);
            
            if (strip->isPlaying())
            {
                double playbackPos = strip->getPlaybackPosition();
                double numSamples = static_cast<double>(buffer->getNumSamples());
                
                // Safety check to prevent division by zero or NaN
                if (numSamples > 0 && std::isfinite(playbackPos))
                {
                    double wrappedPos = std::fmod(playbackPos, numSamples);
                    if (wrappedPos < 0.0)
                        wrappedPos += numSamples;
                    double normalized = wrappedPos / numSamples;
                    waveform.setPlaybackPosition(normalized);
                }
            }

            const bool isGrainMode = false;
            double grainWindowNorm = 0.0;
            if (isGrainMode && buffer->getNumSamples() > 0 && strip->getSourceSampleRate() > 0.0)
            {
                double sizeMsForDisplay = static_cast<double>(strip->getGrainSizeMs());
                const double hostTempo = juce::jmax(1.0, processor.getAudioEngine()->getCurrentTempo());
                static constexpr std::array<double, 13> sizeDivisionsBeats {
                    1.0 / 64.0, 1.0 / 48.0, 1.0 / 32.0, 1.0 / 24.0, 1.0 / 16.0,
                    1.0 / 12.0, 1.0 / 8.0, 1.0 / 6.0, 1.0 / 4.0, 1.0 / 3.0,
                    1.0 / 2.0, 1.0, 2.0
                };
                const double t = juce::jlimit(0.0, 1.0, (sizeMsForDisplay - 5.0) / (2400.0 - 5.0));
                const int idx = juce::jlimit(0, static_cast<int>(sizeDivisionsBeats.size()) - 1,
                                             static_cast<int>(std::round(t * static_cast<double>(sizeDivisionsBeats.size() - 1))));
                if (strip->isGrainTempoSyncEnabled())
                    sizeMsForDisplay = sizeDivisionsBeats[static_cast<size_t>(idx)] * (60.0 / hostTempo) * 1000.0;
                const double sizeSamples = (sizeMsForDisplay * 0.001) * strip->getSourceSampleRate();
                grainWindowNorm = sizeSamples / static_cast<double>(buffer->getNumSamples());
            }
            waveform.setGrainWindowOverlay(isGrainMode, grainWindowNorm);
            waveform.setGrainMarkerPositions(strip->getGrainPreviewPositions(),
                                             strip->getGrainPreviewPitchNorms());
            waveform.setGrainHudOverlay(false, {}, {}, 0.0f, 0.0f, 0.0f,
                                        strip->getGrainPitch(), strip->getGrainArpDepth(), strip->getGrainPitchJitter());
        }
    }
    else if (!showingStepDisplay)
    {
        if (waveform.hasLoadedAudio())
            waveform.clear();
    }
    
    // Update tempo label - only if visible
    if (tempoLabel.isVisible())
    {
        float beats = strip->getBeatsPerLoop();
        
        // Simple, safe validation
        if (beats >= 0.25f && beats <= 64.0f && std::isfinite(beats))
        {
            // Valid range - format it
            tempoLabel.setText(juce::String(beats, 1) + "b", juce::dontSendNotification);
        }
        else
        {
            // Invalid or auto - show AUTO
            tempoLabel.setText("AUTO", juce::dontSendNotification);
        }
    }
    
    if (!speedSlider.isMouseButtonDown() && !modulates(ModernAudioEngine::ModTarget::Speed))
        speedSlider.setValue(getPlayheadSpeedRatioForStrip(*strip), juce::dontSendNotification);
    if (!scratchSlider.isMouseButtonDown())
        scratchSlider.setValue(strip->getScratchAmount(), juce::dontSendNotification);
    if (!sliceLengthSlider.isMouseButtonDown())
        sliceLengthSlider.setValue(strip->getLoopSliceLength(), juce::dontSendNotification);
    const int stepLength = strip->getStepPatternLengthSteps();
    if ((stepLength % 16) == 0)
        patternLengthBox.setSelectedId(juce::jlimit(1, 4, stepLength / 16), juce::dontSendNotification);
    else
    {
        patternLengthBox.setSelectedId(0, juce::dontSendNotification);
        patternLengthBox.setText(juce::String(stepLength), juce::dontSendNotification);
    }
    if (!stepLengthReadoutBox.isInteracting())
        stepLengthReadoutBox.setValue(stepLength, juce::dontSendNotification);
    if (!laneMidiChannelBox.isInteracting())
        laneMidiChannelBox.setValue(processor.getLaneMidiChannel(stripIndex), juce::dontSendNotification);
    if (!laneMidiNoteBox.isInteracting())
        laneMidiNoteBox.setValue(processor.getLaneMidiNote(stripIndex), juce::dontSendNotification);
    if (!stepAttackSlider.isMouseButtonDown())
        stepAttackSlider.setValue(strip->getStepEnvelopeAttackMs(), juce::dontSendNotification);
    if (!stepDecaySlider.isMouseButtonDown())
        stepDecaySlider.setValue(strip->getStepEnvelopeDecayMs(), juce::dontSendNotification);
    if (!stepReleaseSlider.isMouseButtonDown())
        stepReleaseSlider.setValue(strip->getStepEnvelopeReleaseMs(), juce::dontSendNotification);
    if (stripIndex >= 0 && stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels)
    {
        beatSpaceCategoryBox.setSelectedId(
            processor.getBeatSpaceChannelSpaceAssignment(stripIndex) + 1,
            juce::dontSendNotification);
    }
    muteButton.setToggleState(strip->isMuted(), juce::dontSendNotification);
    soloButton.setToggleState(strip->isSolo(), juce::dontSendNotification);
    // Sync volume/pan/pitch from parameters so preset recall always overrides UI state.
    const auto getParamValue = [this](const juce::String& paramId, float fallback)
    {
        if (auto* raw = processor.parameters.getRawParameterValue(paramId))
            return raw->load();
        return fallback;
    };

    const float paramVolume = getParamValue("stripVolume" + juce::String(stripIndex), strip->getVolume());
    const float paramPan = getParamValue("stripPan" + juce::String(stripIndex), strip->getPan());
    const float paramPitch = getParamValue(
        "stripPitch" + juce::String(stripIndex),
        processor.getPitchSemitonesForDisplay(*strip));

    if (!modulates(ModernAudioEngine::ModTarget::Volume))
        volumeSlider.setValue(paramVolume, juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::Pan))
        panSlider.setValue(paramPan, juce::dontSendNotification);
    if (!pitchSlider.isMouseButtonDown() && !modulates(ModernAudioEngine::ModTarget::Pitch))
        pitchSlider.setValue(paramPitch, juce::dontSendNotification);
    // Sync direction mode dropdown with strip state
    int dirModeId = static_cast<int>(strip->getDirectionMode()) + 1;
    if (directionModeBox.getSelectedId() != dirModeId)
        directionModeBox.setSelectedId(dirModeId, juce::dontSendNotification);

    updateGrainOverlayVisibility();
    if (!modulates(ModernAudioEngine::ModTarget::GrainSize))
        grainSizeSlider.setValue(strip->getGrainSizeMs(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainDensity))
        grainDensitySlider.setValue(strip->getGrainDensity(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainPitch))
        grainPitchSlider.setValue(strip->getGrainPitch(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainPitchJitter))
        grainPitchJitterSlider.setValue(strip->getGrainPitchJitter(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainSpread))
        grainSpreadSlider.setValue(strip->getGrainSpread(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainJitter))
        grainJitterSlider.setValue(strip->getGrainJitter(), juce::dontSendNotification);
    if (!grainPositionJitterSlider.isMouseButtonDown())
        grainPositionJitterSlider.setValue(strip->getGrainPositionJitter(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainRandom))
        grainRandomSlider.setValue(strip->getGrainRandomDepth(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainArp))
        grainArpSlider.setValue(strip->getGrainArpDepth(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainCloud))
        grainCloudSlider.setValue(strip->getGrainCloudDepth(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainEmitter))
        grainEmitterSlider.setValue(strip->getGrainEmitterDepth(), juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::GrainEnvelope))
        grainEnvelopeSlider.setValue(strip->getGrainEnvelope(), juce::dontSendNotification);
    grainShapeSlider.setValue(strip->getGrainShape(), juce::dontSendNotification);
    const bool grainSyncEnabled = strip->isGrainTempoSyncEnabled();
    grainSizeSyncToggle.setToggleState(grainSyncEnabled, juce::dontSendNotification);
    grainSizeDivLabel.setText(grainSyncEnabled ? "SYNC" : "FREE", juce::dontSendNotification);
    grainSizeSyncToggle.setColour(juce::ToggleButton::textColourId, grainSyncEnabled ? stripColor.brighter(0.35f) : stripColor.withAlpha(0.72f));
    grainSizeSyncToggle.setColour(juce::ToggleButton::tickColourId, grainSyncEnabled ? stripColor.brighter(0.35f) : stripColor.withAlpha(0.72f));
    {
        const bool arpActive = strip->getGrainArpDepth() > 0.001f;
        grainPitchLabel.setText(arpActive ? "RANGE" : "PITCH", juce::dontSendNotification);
        if (!grainPitchSlider.isMouseButtonDown())
        {
            if (arpActive)
            {
                grainPitchSlider.setRange(0.0, 48.0, 0.1);
                grainPitchSlider.setValue(std::abs(strip->getGrainPitch()), juce::dontSendNotification);
            }
            else
            {
                grainPitchSlider.setRange(-48.0, 48.0, 0.1);
                grainPitchSlider.setValue(strip->getGrainPitch(), juce::dontSendNotification);
            }
        }
    }

    // Mod target pulse indication on actual control colours (not label text).
    auto tintSlider = [](juce::Slider& s, juce::Colour c, float pulseAmount)
    {
        const float pulse = juce::jlimit(0.0f, 1.0f, pulseAmount);
        const auto fill = c.interpolatedWith(kAccent.brighter(0.5f), 0.25f * pulse);
        s.setColour(juce::Slider::rotarySliderFillColourId, fill);
        s.setColour(juce::Slider::trackColourId, fill.withAlpha(0.78f + (0.2f * pulse)));
        s.setColour(juce::Slider::thumbColourId, fill.brighter(0.18f + (0.42f * pulse)));
        s.setColour(juce::Slider::rotarySliderOutlineColourId,
                    juce::Colour(0xff4a4a4a).interpolatedWith(fill.brighter(0.55f), 0.7f * pulse));
    };
    auto setModIndicator = [](juce::Slider& s, bool active, float depth, float signedPos, juce::Colour colour)
    {
        auto& props = s.getProperties();
        props.set("modActive", active);
        props.set("modDepth", juce::jlimit(0.0f, 1.0f, depth));
        props.set("modSigned", juce::jlimit(-1.0f, 1.0f, signedPos));
        props.set("modColour", static_cast<int>(colour.getARGB()));
    };
    auto pickVisibleModColour = [](const juce::Slider& s)
    {
        const auto base = s.findColour(juce::Slider::rotarySliderFillColourId);
        const float hue = base.getHue();
        const bool nearYellowHue = (hue > 0.10f && hue < 0.18f) && base.getSaturation() > 0.25f;
        const auto ref = juce::Colour(0xffffd24a);
        const float dr = base.getFloatRed() - ref.getFloatRed();
        const float dg = base.getFloatGreen() - ref.getFloatGreen();
        const float db = base.getFloatBlue() - ref.getFloatBlue();
        const float rgbDist = std::sqrt((dr * dr) + (dg * dg) + (db * db));
        const bool nearAccent = base.getPerceivedBrightness() > 0.45f
                             && rgbDist < 0.34f;
        if (nearYellowHue || nearAccent)
            return juce::Colour(0xff3bd5ff); // cyan contrast for yellow/orange controls
        return juce::Colour(0xffffd24a);     // default warm modulation color
    };
    const auto baseControl = stripColor.withAlpha(0.72f);
    tintSlider(volumeSlider, baseControl, 0.0f);
    tintSlider(panSlider, baseControl, 0.0f);
    tintSlider(pitchSlider, baseControl, 0.0f);
    tintSlider(speedSlider, baseControl, 0.0f);
    tintSlider(scratchSlider, baseControl, 0.0f);
    tintSlider(sliceLengthSlider, baseControl, 0.0f);
    tintSlider(grainSizeSlider, baseControl, 0.0f);
    tintSlider(grainDensitySlider, baseControl, 0.0f);
    tintSlider(grainPitchSlider, baseControl, 0.0f);
    tintSlider(grainPitchJitterSlider, baseControl, 0.0f);
    tintSlider(grainSpreadSlider, baseControl, 0.0f);
    tintSlider(grainJitterSlider, baseControl, 0.0f);
    tintSlider(grainPositionJitterSlider, baseControl, 0.0f);
    tintSlider(grainRandomSlider, baseControl, 0.0f);
    tintSlider(grainArpSlider, baseControl, 0.0f);
    tintSlider(grainCloudSlider, baseControl, 0.0f);
    tintSlider(grainEmitterSlider, baseControl, 0.0f);
    tintSlider(grainEnvelopeSlider, baseControl, 0.0f);
    tintSlider(grainShapeSlider, baseControl, 0.0f);
    setModIndicator(volumeSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(panSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(pitchSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(speedSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(scratchSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(sliceLengthSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainSizeSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainDensitySlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainPitchSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainPitchJitterSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainSpreadSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainJitterSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainPositionJitterSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainRandomSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainArpSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainCloudSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainEmitterSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainEnvelopeSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(grainShapeSlider, false, 0.0f, 0.0f, kAccent);

    if (auto* engine = processor.getAudioEngine())
    {
        const auto mod = engine->getModSequencerState(stripIndex);
        if (mod.target != ModernAudioEngine::ModTarget::None)
        {
            const int lengthBars = juce::jlimit(1, ModernAudioEngine::MaxModBars, engine->getModLengthBars(stripIndex));
            const int totalSteps = juce::jmax(ModernAudioEngine::ModSteps, lengthBars * ModernAudioEngine::ModSteps);
            const int activeStep = juce::jlimit(0, totalSteps - 1, engine->getModCurrentGlobalStep(stripIndex));
            const float raw = juce::jlimit(0.0f, 1.0f, engine->getModStepValueAbsolute(stripIndex, activeStep));
            const bool bipolar = mod.bipolar && modTargetAllowsBipolar(mod.target);
            const float depth = juce::jlimit(0.0f, 1.0f, mod.depth);
            const float modNorm = juce::jlimit(0.0f, 1.0f, raw * depth);
            const float modBi = juce::jlimit(-1.0f, 1.0f, ((raw * 2.0f) - 1.0f) * depth);
            const float intensity = bipolar ? std::abs(modBi) : modNorm;
            const float signedPos = juce::jlimit(-1.0f, 1.0f, (raw * 2.0f) - 1.0f);

            const float stepPulse = ((activeStep & 1) == 0) ? 1.0f : 0.65f;
            const float pulseAmount = juce::jlimit(0.0f, 1.0f,
                                                   (0.35f + (0.65f * juce::jmax(0.2f, intensity))) * stepPulse);

            auto* targetSlider = [&]() -> juce::Slider*
            {
                switch (mod.target)
                {
                    case ModernAudioEngine::ModTarget::None: return nullptr;
                    case ModernAudioEngine::ModTarget::Volume: return &volumeSlider;
                    case ModernAudioEngine::ModTarget::Pan: return &panSlider;
                    case ModernAudioEngine::ModTarget::Pitch: return &pitchSlider;
                    case ModernAudioEngine::ModTarget::Speed: return &speedSlider;
                    case ModernAudioEngine::ModTarget::Cutoff: return nullptr;
                    case ModernAudioEngine::ModTarget::Resonance: return nullptr;
                    case ModernAudioEngine::ModTarget::GrainSize: return &grainSizeSlider;
                    case ModernAudioEngine::ModTarget::GrainDensity: return &grainDensitySlider;
                    case ModernAudioEngine::ModTarget::GrainPitch: return &grainPitchSlider;
                    case ModernAudioEngine::ModTarget::GrainPitchJitter: return &grainPitchJitterSlider;
                    case ModernAudioEngine::ModTarget::GrainSpread: return &grainSpreadSlider;
                    case ModernAudioEngine::ModTarget::GrainJitter: return &grainJitterSlider;
                    case ModernAudioEngine::ModTarget::GrainRandom: return &grainRandomSlider;
                    case ModernAudioEngine::ModTarget::GrainArp: return &grainArpSlider;
                    case ModernAudioEngine::ModTarget::GrainCloud: return &grainCloudSlider;
                    case ModernAudioEngine::ModTarget::GrainEmitter: return &grainEmitterSlider;
                    case ModernAudioEngine::ModTarget::GrainEnvelope: return &grainEnvelopeSlider;
                    case ModernAudioEngine::ModTarget::Retrigger: return nullptr;
                    default: return nullptr;
                }
            }();
            if (targetSlider != nullptr)
            {
                const auto targetColour = pickVisibleModColour(*targetSlider);
                const auto pulseColour = targetColour.withAlpha(0.82f + (0.18f * pulseAmount));
                tintSlider(*targetSlider, pulseColour, pulseAmount);
                setModIndicator(*targetSlider, true, depth, signedPos, targetColour);
            }
        }
    }
    
    repaint();  // For LED overlay
}

//==============================================================================
// FXStripControl Implementation
//==============================================================================

FXStripControl::FXStripControl(int idx, StepVstHostAudioProcessor& p)
    : stripIndex(idx), processor(p)
{
    // Get strip color
    stripColor = getStripColor(idx);
    
    // Strip label exists but not visible (used internally if needed)
    stripLabel.setText("Strip " + juce::String(idx + 1), juce::dontSendNotification);
    stripLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    stripLabel.setColour(juce::Label::textColourId, stripColor);
    // DON'T add to view - no label shown
    
    // Filter Enable (button only, no text label)
    filterEnableButton.setButtonText("Filter");
    filterEnableButton.setClickingTogglesState(true);
    filterEnableButton.onClick = [this]() {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
        {
            const bool enabled = filterEnableButton.getToggleState();
            strip->setFilterEnabled(enabled);
            if (strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step)
            {
                if (auto* stepSampler = strip->getStepSampler())
                    stepSampler->setFilterEnabled(enabled);
            }
        }
    };
    addAndMakeVisible(filterEnableButton);
    
    // Filter Frequency
    filterFreqLabel.setText("Freq", juce::dontSendNotification);
    filterFreqLabel.setJustificationType(juce::Justification::centred);
    filterFreqLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    filterFreqLabel.setColour(juce::Label::textColourId, stripColor);
    addAndMakeVisible(filterFreqLabel);
    
    filterFreqSlider.setSliderStyle(juce::Slider::Rotary);
    filterFreqSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 38, 12);
    filterFreqSlider.setRange(20.0, 20000.0, 1.0);
    filterFreqSlider.setSkewFactorFromMidPoint(1000.0);
    filterFreqSlider.setValue(20000.0);  // Default fully open (20kHz)
    enableAltClickReset(filterFreqSlider, 20000.0);
    filterFreqSlider.setTextValueSuffix(" Hz");
    filterFreqSlider.onValueChange = [this]() {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
        {
            strip->setFilterFrequency(static_cast<float>(filterFreqSlider.getValue()));
            if (strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step)
            {
                if (auto* stepSampler = strip->getStepSampler())
                    stepSampler->setFilterFrequency(static_cast<float>(filterFreqSlider.getValue()));
            }
        }
    };
    addAndMakeVisible(filterFreqSlider);
    
    // Filter Resonance
    filterResLabel.setText("Res", juce::dontSendNotification);
    filterResLabel.setJustificationType(juce::Justification::centred);
    filterResLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    filterResLabel.setColour(juce::Label::textColourId, stripColor);
    addAndMakeVisible(filterResLabel);
    
    filterResSlider.setSliderStyle(juce::Slider::Rotary);
    filterResSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 35, 12);
    filterResSlider.setRange(0.1, 10.0, 0.01);
    filterResSlider.setValue(0.707);
    enableAltClickReset(filterResSlider, 0.707);
    filterResSlider.setTextValueSuffix(" Q");
    filterResSlider.onValueChange = [this]() {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
        {
            strip->setFilterResonance(static_cast<float>(filterResSlider.getValue()));
            if (strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step)
            {
                if (auto* stepSampler = strip->getStepSampler())
                    stepSampler->setFilterResonance(static_cast<float>(filterResSlider.getValue()));
            }
        }
    };
    addAndMakeVisible(filterResSlider);
    
    // Filter Morph
    filterMorphLabel.setText("Morph", juce::dontSendNotification);
    filterMorphLabel.setJustificationType(juce::Justification::centred);
    filterMorphLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    filterMorphLabel.setColour(juce::Label::textColourId, stripColor);
    addAndMakeVisible(filterMorphLabel);

    filterMorphSlider.setSliderStyle(juce::Slider::Rotary);
    filterMorphSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 38, 12);
    filterMorphSlider.setRange(0.0, 1.0, 0.001);
    filterMorphSlider.setValue(0.0);
    filterMorphSlider.setDoubleClickReturnValue(true, 0.0);
    filterMorphSlider.textFromValueFunction = [](double value)
    {
        const double v = juce::jlimit(0.0, 1.0, value);
        if (v < 0.25) return juce::String("LP");
        if (v < 0.75) return juce::String("BP");
        return juce::String("HP");
    };
    filterMorphSlider.valueFromTextFunction = [](const juce::String& text)
    {
        const auto t = text.trim().toUpperCase();
        if (t.contains("LP")) return 0.0;
        if (t.contains("BP")) return 0.5;
        if (t.contains("HP")) return 1.0;
        return 0.0;
    };
    filterMorphSlider.onValueChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
        {
            const float morphValue = static_cast<float>(filterMorphSlider.getValue());
            strip->setFilterMorph(morphValue);
            if (strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step)
            {
                EnhancedAudioStrip::FilterType stripType = EnhancedAudioStrip::FilterType::LowPass;
                FilterType stepType = FilterType::LowPass;
                if (morphValue >= 0.75f)
                {
                    stripType = EnhancedAudioStrip::FilterType::HighPass;
                    stepType = FilterType::HighPass;
                }
                else if (morphValue >= 0.25f)
                {
                    stripType = EnhancedAudioStrip::FilterType::BandPass;
                    stepType = FilterType::BandPass;
                }

                strip->setFilterType(stripType);
                if (auto* stepSampler = strip->getStepSampler())
                    stepSampler->setFilterType(stepType);
            }
        }
    };
    addAndMakeVisible(filterMorphSlider);

    // Filter Algorithm selector
    filterAlgoLabel.setText("Alg", juce::dontSendNotification);
    filterAlgoLabel.setJustificationType(juce::Justification::centred);
    filterAlgoLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    filterAlgoLabel.setColour(juce::Label::textColourId, stripColor);
    addAndMakeVisible(filterAlgoLabel);

    filterAlgoBox.addItem("SVF12", 1);
    filterAlgoBox.addItem("SVF24", 2);
    filterAlgoBox.addItem("LAD12", 3);
    filterAlgoBox.addItem("LAD24", 4);
    filterAlgoBox.addItem("MOOG S", 5);
#if STEPVSTHOST_ENABLE_HUOVILAINEN
    filterAlgoBox.addItem("MOOG H", 6);
#else
    filterAlgoBox.addItem("MOOG H*", 6);
#endif
    filterAlgoBox.setSelectedId(1);
    styleUiCombo(filterAlgoBox);
    filterAlgoBox.setJustificationType(juce::Justification::centred);
#if STEPVSTHOST_ENABLE_HUOVILAINEN
    filterAlgoBox.setTooltip("Filter algorithm: SVF12, SVF24, Ladder12, Ladder24, Moog Stilson LP, Moog Huovilainen LP");
#else
    filterAlgoBox.setTooltip("Filter algorithm: SVF12, SVF24, Ladder12, Ladder24, Moog Stilson LP, Moog H* (Stilson fallback; Huovilainen disabled in this build)");
#endif
    filterAlgoBox.onChange = [this]()
    {
        if (auto* strip = processor.getAudioEngine()->getStrip(stripIndex))
        {
            const int id = filterAlgoBox.getSelectedId();
            auto algo = EnhancedAudioStrip::FilterAlgorithm::Tpt12;
            if (id == 2) algo = EnhancedAudioStrip::FilterAlgorithm::Tpt24;
            else if (id == 3) algo = EnhancedAudioStrip::FilterAlgorithm::Ladder12;
            else if (id == 4) algo = EnhancedAudioStrip::FilterAlgorithm::Ladder24;
            else if (id == 5) algo = EnhancedAudioStrip::FilterAlgorithm::MoogStilson;
            else if (id == 6) algo = EnhancedAudioStrip::FilterAlgorithm::MoogHuov;
            strip->setFilterAlgorithm(algo);
        }
    };
    addAndMakeVisible(filterAlgoBox);

    // Start timer for updating from engine
    startTimer(50);  // Update at 20Hz
}

void FXStripControl::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    drawPanel(g, bounds, stripColor, 10.0f);

    // TWO vertical dividers creating 3 equal fields
    float thirdWidth = bounds.getWidth() / 3.0f;
    g.setColour(kPanelStroke.withAlpha(0.7f));

    // First divider (1/3 from left)
    float divider1X = bounds.getX() + thirdWidth;
    g.fillRect(divider1X - 1.0f, bounds.getY() + 20.0f, 2.0f, bounds.getHeight() - 40.0f);

    // Second divider (2/3 from left)
    float divider2X = bounds.getX() + (thirdWidth * 2.0f);
    g.fillRect(divider2X - 1.0f, bounds.getY() + 20.0f, 2.0f, bounds.getHeight() - 40.0f);
}

void FXStripControl::resized()
{
    auto bounds = getLocalBounds();
    
    // Match Play strip padding
    bounds.reduce(8, 8);
    
    // SPLIT INTO THREE FIELDS
    int fieldWidth = bounds.getWidth() / 3;

    // Field 1: Filter controls (left third)
    auto field1 = bounds.removeFromLeft(fieldWidth).reduced(6, 0);

    // Field 2/3: reserved for future controls
    auto field2 = bounds.removeFromLeft(fieldWidth).reduced(6, 0);
    auto field3 = bounds.reduced(6, 0);
    
    // Top row: Filter enable + compact algorithm selector
    auto topRow = field1.removeFromTop(22);
    filterEnableButton.setBounds(topRow.removeFromLeft(56));
    topRow.removeFromLeft(4);
    filterAlgoLabel.setBounds(topRow.removeFromLeft(24));
    topRow.removeFromLeft(3);
    filterAlgoBox.setBounds(topRow.removeFromLeft(92));
    field1.removeFromTop(4);
    
    // Three rotary controls: Freq | Res | Morph
    auto controlsRow = field1.removeFromTop(64);

    int controlWidth = controlsRow.getWidth() / 3;
    auto freqCol = controlsRow.removeFromLeft(controlWidth).reduced(2, 0);
    filterFreqLabel.setBounds(freqCol.removeFromTop(12));
    filterFreqSlider.setBounds(freqCol);

    auto resCol = controlsRow.removeFromLeft(controlWidth).reduced(2, 0);
    filterResLabel.setBounds(resCol.removeFromTop(12));
    filterResSlider.setBounds(resCol);

    auto morphCol = controlsRow.reduced(2, 0);
    filterMorphLabel.setBounds(morphCol.removeFromTop(12));
    filterMorphSlider.setBounds(morphCol);

    (void) field2;
    (void) field3;
}

void FXStripControl::updateFromEngine()
{
    if (!processor.getAudioEngine()) return;
    
    auto* strip = processor.getAudioEngine()->getStrip(stripIndex);
    if (!strip) return;

    const bool isStepMode = (strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step);
    auto* stepSampler = isStepMode ? strip->getStepSampler() : nullptr;
    
    // Update from engine state
    const bool filterEnabled = (isStepMode && stepSampler) ? stepSampler->isFilterEnabled() : strip->isFilterEnabled();
    const float filterFreq = (isStepMode && stepSampler) ? stepSampler->getFilterFrequency() : strip->getFilterFrequency();
    const float filterRes = (isStepMode && stepSampler) ? stepSampler->getFilterResonance() : strip->getFilterResonance();
    float filterMorph = strip->getFilterMorph();
    if (isStepMode && stepSampler)
    {
        switch (stepSampler->getFilterType())
        {
            case FilterType::LowPass:  filterMorph = 0.0f; break;
            case FilterType::BandPass: filterMorph = 0.5f; break;
            case FilterType::HighPass: filterMorph = 1.0f; break;
            default: break;
        }
    }

    filterEnableButton.setToggleState(filterEnabled, juce::dontSendNotification);
    filterFreqSlider.setValue(filterFreq, juce::dontSendNotification);
    filterResSlider.setValue(filterRes, juce::dontSendNotification);
    filterMorphSlider.setValue(filterMorph, juce::dontSendNotification);

    const auto base = stripColor.withAlpha(0.72f);
    auto setBaseSliderTint = [](juce::Slider& s, juce::Colour c)
    {
        s.setColour(juce::Slider::rotarySliderFillColourId, c);
        s.setColour(juce::Slider::trackColourId, c.withAlpha(0.78f));
        s.setColour(juce::Slider::thumbColourId, c.brighter(0.18f));
        s.setColour(juce::Slider::rotarySliderOutlineColourId, c.darker(0.72f).withAlpha(0.82f));
    };
    auto pickVisibleModColour = [](juce::Colour baseColour)
    {
        const auto baseRgb = juce::Colour::fromRGB(baseColour.getRed(), baseColour.getGreen(), baseColour.getBlue());
        const auto accentRgb = juce::Colour::fromRGB(0xff, 0xd2, 0x4a);
        const auto dR = static_cast<float>(baseRgb.getFloatRed() - accentRgb.getFloatRed());
        const auto dG = static_cast<float>(baseRgb.getFloatGreen() - accentRgb.getFloatGreen());
        const auto dB = static_cast<float>(baseRgb.getFloatBlue() - accentRgb.getFloatBlue());
        const float rgbDist = std::sqrt((dR * dR) + (dG * dG) + (dB * dB));
        const bool nearYellowHue = baseColour.getHue() > 0.10f && baseColour.getHue() < 0.18f;
        const bool nearAccent = baseColour.getPerceivedBrightness() > 0.45f && rgbDist < 0.34f;
        return (nearYellowHue || nearAccent) ? juce::Colour(0xff3bd5ff) : juce::Colour(0xffffd24a);
    };

    setBaseSliderTint(filterFreqSlider, base);
    setBaseSliderTint(filterResSlider, base);

    const auto algo = strip->getFilterAlgorithm();
    int algoId = 1;
    if (algo == EnhancedAudioStrip::FilterAlgorithm::Tpt24) algoId = 2;
    else if (algo == EnhancedAudioStrip::FilterAlgorithm::Ladder12) algoId = 3;
    else if (algo == EnhancedAudioStrip::FilterAlgorithm::Ladder24) algoId = 4;
    else if (algo == EnhancedAudioStrip::FilterAlgorithm::MoogStilson) algoId = 5;
    else if (algo == EnhancedAudioStrip::FilterAlgorithm::MoogHuov) algoId = 6;
    filterAlgoBox.setSelectedId(algoId, juce::dontSendNotification);

    if (auto* engine = processor.getAudioEngine())
    {
        const auto mod = engine->getModSequencerState(stripIndex);
        const bool active = (mod.target == ModernAudioEngine::ModTarget::Cutoff
                          || mod.target == ModernAudioEngine::ModTarget::Resonance);
        if (active)
        {
            const float depth = juce::jlimit(0.0f, 1.0f, mod.depth);
            const int lengthBars = juce::jlimit(1, ModernAudioEngine::MaxModBars, engine->getModLengthBars(stripIndex));
            const int totalSteps = juce::jmax(ModernAudioEngine::ModSteps, lengthBars * ModernAudioEngine::ModSteps);
            const int step = juce::jlimit(0, totalSteps - 1, engine->getModCurrentGlobalStep(stripIndex));
            const float raw = juce::jlimit(0.0f, 1.0f, engine->getModStepValueAbsolute(stripIndex, step));
            const bool bipolar = mod.bipolar && modTargetAllowsBipolar(mod.target);
            const float modNorm = juce::jlimit(0.0f, 1.0f, raw * depth);
            const float modBi = juce::jlimit(-1.0f, 1.0f, ((raw * 2.0f) - 1.0f) * depth);
            const float intensity = bipolar ? std::abs(modBi) : modNorm;
            const float stepPulse = ((step & 1) == 0) ? 1.0f : 0.65f;
            const float pulse = juce::jlimit(0.0f, 1.0f,
                                             (0.35f + (0.65f * juce::jmax(0.2f, intensity))) * stepPulse);
            const auto modColour = pickVisibleModColour(base).withAlpha(0.82f + (0.18f * pulse));
            if (mod.target == ModernAudioEngine::ModTarget::Cutoff)
                setBaseSliderTint(filterFreqSlider, modColour);
            else
                setBaseSliderTint(filterResSlider, modColour);
        }
    }
}

void FXStripControl::timerCallback()
{
    updateFromEngine();
}

void StripControl::timerCallback()
{
    if (!processor.getAudioEngine())
        return;
    
    updateFromEngine();
}

MonomeGridDisplay::MonomeGridDisplay(StepVstHostAudioProcessor& p)
    : processor(p)
{
    startTimer(50); // 20fps updates
}

void MonomeGridDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    
    // Background
    g.setColour(kSurfaceDark);
    g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    
    // Title
    g.setColour(kTextPrimary);
    g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    auto titleArea = bounds.removeFromTop(30);
    g.drawText("Monome Grid", titleArea, juce::Justification::centred);
    
    bounds.removeFromTop(4);
    
    // Draw grid
    for (int y = 0; y < gridHeight; ++y)
    {
        for (int x = 0; x < gridWidth; ++x)
        {
            auto buttonBounds = getButtonBounds(x, y);
            
            // Button background
            g.setColour(juce::Colour(0xff2a2a2a));
            g.fillRoundedRectangle(buttonBounds.toFloat(), 2.0f);
            
            // LED state
            int brightness = ledState[x][y];
            if (brightness > 0)
            {
                float alpha = brightness / 15.0f;
                g.setColour(kAccent.withAlpha(alpha));
                g.fillRoundedRectangle(buttonBounds.toFloat().reduced(2), 2.0f);
            }
            
            // Pressed state
            if (buttonPressed[x][y])
            {
                g.setColour(kTextPrimary.withAlpha(0.25f));
                g.fillRoundedRectangle(buttonBounds.toFloat(), 2.0f);
            }
            
            // Border
            g.setColour(kPanelStroke);
            g.drawRoundedRectangle(buttonBounds.toFloat(), 2.0f, 1.0f);
        }
    }
}

void MonomeGridDisplay::resized()
{
    repaint();
}

juce::Rectangle<int> MonomeGridDisplay::getButtonBounds(int x, int y) const
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(34); // Title area
    
    auto buttonSize = juce::jmin(
        bounds.getWidth() / gridWidth - 4,
        bounds.getHeight() / gridHeight - 4
    );
    
    auto gridStartX = (bounds.getWidth() - (buttonSize + 4) * gridWidth) / 2;
    auto gridStartY = bounds.getY() + (bounds.getHeight() - (buttonSize + 4) * gridHeight) / 2;
    
    return juce::Rectangle<int>(
        gridStartX + x * (buttonSize + 4),
        gridStartY + y * (buttonSize + 4),
        buttonSize,
        buttonSize
    );
}

void MonomeGridDisplay::mouseDown(const juce::MouseEvent& e)
{
    for (int y = 0; y < gridHeight; ++y)
    {
        for (int x = 0; x < gridWidth; ++x)
        {
            if (getButtonBounds(x, y).contains(e.getPosition()))
            {
                handleButtonPress(x, y, true);
                return;
            }
        }
    }
}

void MonomeGridDisplay::mouseUp(const juce::MouseEvent& e)
{
    (void) e;
    for (int y = 0; y < gridHeight; ++y)
    {
        for (int x = 0; x < gridWidth; ++x)
        {
            if (buttonPressed[x][y])
            {
                handleButtonPress(x, y, false);
            }
        }
    }
}

void MonomeGridDisplay::mouseDrag(const juce::MouseEvent& e)
{
    for (int y = 0; y < gridHeight; ++y)
    {
        for (int x = 0; x < gridWidth; ++x)
        {
            bool shouldBePressed = getButtonBounds(x, y).contains(e.getPosition());
            if (shouldBePressed != buttonPressed[x][y])
            {
                handleButtonPress(x, y, shouldBePressed);
            }
        }
    }
}

void MonomeGridDisplay::handleButtonPress(int x, int y, bool down)
{
    buttonPressed[x][y] = down;
    
    if (down)
    {
        DBG("Button pressed: x=" << x << ", y=" << y);
        
        // Rows 0-5: Strip triggering (row 0 = strip 0, row 1 = strip 1, etc.)
        if (y >= 0 && y < processor.MaxStrips && x < processor.MaxColumns)
        {
            int stripIndex = y;  // Row 0 → strip 0, Row 1 → strip 1, etc.
            
            // Trigger the strip
            processor.triggerStrip(stripIndex, x);
        }
    }
    
    // Don't send LEDs to Monome from here - PluginProcessor handles all LED updates
    // This updateFromEngine() is only for updating the GUI visualization
    
    repaint();
}

void MonomeGridDisplay::timerCallback()
{
    if (!processor.getAudioEngine())
        return;
    
    updateFromEngine();
}

void MonomeGridDisplay::updateFromEngine()
{
    // Update LED states from strips
    // Row 1 = Strip 0
    // Row 2 = Strip 1, etc.
    for (int stripIndex = 0; stripIndex < processor.MaxStrips; ++stripIndex)
    {
        int monomeRow = stripIndex + 1;  // Strip 0 → row 1, Strip 1 → row 2, etc.
        
        if (monomeRow >= gridHeight)
            break;  // Don't exceed grid height
        
        auto* strip = processor.getAudioEngine()->getStrip(stripIndex);
        if (strip)
        {
            // Check if this strip is in Step mode AND control mode is not active
            // When control mode is active (level, pan, sample select, etc.), hide step display
            bool controlModeActive = (processor.getCurrentControlMode() != StepVstHostAudioProcessor::ControlMode::Normal);
            
            if (strip->playMode == EnhancedAudioStrip::PlayMode::Step && !controlModeActive)
            {
                DBG("Strip " << stripIndex << " in Step mode - updating row " << monomeRow);
                
                // Show step pattern on Monome
                const auto visiblePattern = strip->getVisibleStepPattern();
                const int visibleCurrentStep = strip->getVisibleCurrentStep();
                for (int x = 0; x < gridWidth && x < 16; ++x)
                {
                    bool isCurrentStep = (x == visibleCurrentStep);
                    bool isActiveStep = visiblePattern[static_cast<size_t>(x)];
                    
                    int brightness = 0;
                    if (isCurrentStep && isActiveStep)
                    {
                        // Current step AND active - brightest
                        brightness = 15;
                    }
                    else if (isCurrentStep)
                    {
                        // Current step but inactive - medium
                        brightness = 6;
                    }
                    else if (isActiveStep)
                    {
                        // Active step (not current) - medium bright
                        brightness = 10;
                    }
                    else
                    {
                        // Inactive step - dim
                        brightness = 2;
                    }
                    
                    ledState[x][monomeRow] = brightness;
                }
                
                // Debug first few LEDs
                DBG("Step LEDs [0-3]: " << ledState[0][monomeRow] << " " 
                    << ledState[1][monomeRow] << " " 
                    << ledState[2][monomeRow] << " " 
                    << ledState[3][monomeRow]);
            }
            else if (strip->playMode != EnhancedAudioStrip::PlayMode::Step && !controlModeActive)
            {
                // Normal playback mode (Loop/OneShot) - show LED states from strip
                // When control mode is active, PluginProcessor handles ALL LED display
                auto ledStates = strip->getLEDStates();
                for (int x = 0; x < gridWidth && x < processor.MaxColumns; ++x)
                {
                    ledState[x][monomeRow] = ledStates[static_cast<size_t>(x)] ? 12 : 0; // Variable brightness
                }
            }
            // If control mode is active, don't touch LEDs - PluginProcessor handles it
        }
    }
    
    // Hardware LED writes are centralized in StepVstHostAudioProcessor::updateMonomeLEDs().
    // The editor grid is visualization-only.
    repaint();
}


//==============================================================================
// MonomeControlPanel Implementation
//==============================================================================

MonomeControlPanel::MonomeControlPanel(StepVstHostAudioProcessor& p)
    : processor(p)
{
    // Title - compact
    titleLabel.setText("MONOME DEVICE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));  // Smaller
    titleLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(titleLabel);
    
    // Device selector
    deviceSelector.setTextWhenNoChoicesAvailable("No devices found");
    deviceSelector.setTextWhenNothingSelected("Select device...");
    addAndMakeVisible(deviceSelector);
    
    // Refresh button
    refreshButton.setButtonText("Refresh");
    refreshButton.onClick = [this]() { updateDeviceList(); };
    addAndMakeVisible(refreshButton);
    
    // Connect button
    connectButton.setButtonText("Connect");
    connectButton.onClick = [this]() { connectToDevice(); };
    addAndMakeVisible(connectButton);
    
    // Status label
    statusLabel.setText("Not connected", juce::dontSendNotification);
    statusLabel.setFont(juce::Font(juce::FontOptions(11.0f)));  // Slightly smaller
    statusLabel.setColour(juce::Label::textColourId, kAccent);
    addAndMakeVisible(statusLabel);
    
    // Rotation selector
    rotationLabel.setText("Rotation", juce::dontSendNotification);  // Shorter text
    rotationLabel.setFont(juce::Font(juce::FontOptions(11.0f)));  // Slightly smaller, consistent
    rotationLabel.setColour(juce::Label::textColourId, kTextPrimary);
    addAndMakeVisible(rotationLabel);
    
    rotationSelector.addItem("0°", 1);
    rotationSelector.addItem("90°", 2);
    rotationSelector.addItem("180°", 3);
    rotationSelector.addItem("270°", 4);
    rotationSelector.setSelectedId(1);
    rotationSelector.onChange = [this]()
    {
        int rotation = (rotationSelector.getSelectedId() - 1) * 90;
        processor.getMonomeConnection().setRotation(rotation);
    };
    addAndMakeVisible(rotationSelector);
    
    updateDeviceList();
    startTimer(1000); // Update status every second
}

void MonomeControlPanel::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), kAccent, 8.0f);
}

void MonomeControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    
    // Compact title
    auto titleRow = bounds.removeFromTop(20);  // Smaller (was 24)
    titleLabel.setBounds(titleRow);
    
    bounds.removeFromTop(6);  // Less gap (was 8)
    
    // Device selector row
    auto deviceRow = bounds.removeFromTop(22);  // Smaller (was 24)
    deviceSelector.setBounds(deviceRow.removeFromLeft(200));
    deviceRow.removeFromLeft(4);
    refreshButton.setBounds(deviceRow.removeFromLeft(70));
    deviceRow.removeFromLeft(4);
    connectButton.setBounds(deviceRow.removeFromLeft(70));
    
    bounds.removeFromTop(6);  // Less gap (was 8)
    
    // Status row
    auto statusRow = bounds.removeFromTop(18);  // Smaller (was 20)
    statusLabel.setBounds(statusRow);
    
    bounds.removeFromTop(6);  // Less gap (was 8)
    
    // Rotation row - ensure it's visible!
    auto rotationRow = bounds.removeFromTop(22);  // Match other controls
    rotationLabel.setBounds(rotationRow.removeFromLeft(70));
    rotationRow.removeFromLeft(4);
    rotationSelector.setBounds(rotationRow.removeFromLeft(100));  // Wider (was 80)
}

void MonomeControlPanel::timerCallback()
{
    if (!processor.getAudioEngine())
        return;
    
    updateStatus();
}

void MonomeControlPanel::updateDeviceList()
{
    deviceSelector.clear();
    processor.getMonomeConnection().refreshDeviceList();
    
    auto devices = processor.getMonomeConnection().getDiscoveredDevices();
    for (size_t i = 0; i < devices.size(); ++i)
    {
        auto& device = devices[i];
        juce::String itemText = device.id + " (" + device.type + ") - " +
                                juce::String(device.sizeX) + "x" + juce::String(device.sizeY);
        deviceSelector.addItem(itemText, static_cast<int>(i + 1));
    }
    
    if (devices.size() > 0)
        deviceSelector.setSelectedId(1);
}

void MonomeControlPanel::connectToDevice()
{
    int selectedIndex = deviceSelector.getSelectedId() - 1;
    if (selectedIndex >= 0)
    {
        processor.getMonomeConnection().selectDevice(selectedIndex);
    }
}

void MonomeControlPanel::updateStatus()
{
    auto status = processor.getMonomeConnection().getConnectionStatus();
    statusLabel.setText(status, juce::dontSendNotification);
    
    bool connected = processor.getMonomeConnection().isConnected();
    statusLabel.setColour(juce::Label::textColourId,
                          connected ? juce::Colour(0xff76be7e) : kAccent);
}


//==============================================================================
// GlobalControlPanel Implementation
//==============================================================================

GlobalControlPanel::GlobalControlPanel(StepVstHostAudioProcessor& p)
    : processor(p)
{
    // Title - compact
    titleLabel.setText("GLOBAL CONTROLS", juce::dontSendNotification);  // Uppercase, compact
    titleLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, kTextPrimary);
    addAndMakeVisible(titleLabel);
    titleLabel.setTooltip("Master host, routing, swing, and UI help settings.");

    versionLabel.setText("v" + juce::String(JucePlugin_VersionString), juce::dontSendNotification);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    versionLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    versionLabel.setColour(juce::Label::textColourId, kTextMuted);
    versionLabel.setTooltip("Plugin version.");
    addAndMakeVisible(versionLabel);
    
    // Master volume
    masterVolumeLabel.setText("Master", juce::dontSendNotification);
    masterVolumeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(masterVolumeLabel);
    
    masterVolumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    masterVolumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);  // Clean, no text
    masterVolumeSlider.setRange(0.0, 1.0, 0.01);
    masterVolumeSlider.setValue(1.0);
    enableAltClickReset(masterVolumeSlider, 1.0);
    masterVolumeSlider.setPopupDisplayEnabled(true, false, this);  // Show value on hover
    addAndMakeVisible(masterVolumeSlider);
    
    masterVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "masterVolume", masterVolumeSlider);
    masterVolumeSlider.onDragEnd = [this]()
    {
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };

    limiterToggle.setButtonText("Limiter");
    limiterToggle.setClickingTogglesState(true);
    limiterToggle.setTooltip("Enable JUCE limiter on plugin outputs.");
    addAndMakeVisible(limiterToggle);
    styleUiButton(limiterToggle);
    limiterEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, "limiterEnabled", limiterToggle);
    limiterToggle.onClick = [this]()
    {
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };
    
    swingDivisionLabel.setText("Swing grid", juce::dontSendNotification);
    swingDivisionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(swingDivisionLabel);

    swingDivisionBox.addItem("1/4", 1);
    swingDivisionBox.addItem("1/8", 2);
    swingDivisionBox.addItem("1/16", 3);
    swingDivisionBox.addItem("1/8T", 4);
    swingDivisionBox.addItem("1/2", 5);
    swingDivisionBox.addItem("1/32", 6);
    swingDivisionBox.addItem("1/16T", 7);
    swingDivisionBox.onChange = [this]()
    {
        processor.setSwingDivisionSelection(swingDivisionBox.getSelectedId() - 1);
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };
    addAndMakeVisible(swingDivisionBox);
    styleUiCombo(swingDivisionBox);
    swingDivisionBox.setTooltip("Swing subdivision grid. 1/8T is triplet swing (3 subdivisions per beat), 1/16T is 6 subdivisions per beat.");

    outputRoutingLabel.setText("Outputs", juce::dontSendNotification);
    outputRoutingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(outputRoutingLabel);

    outputRoutingBox.addItem("Stereo Mix", 1);
    outputRoutingBox.addItem("Separate Strip Outs", 2);
    outputRoutingBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(outputRoutingBox);
    styleUiCombo(outputRoutingBox);
    outputRoutingBox.setTooltip("Route strip audio to separate DAW outputs (requires multi-output plugin instance).");
    outputRoutingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, "outputRouting", outputRoutingBox);
    outputRoutingBox.onChange = [this]()
    {
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };

    beatSpaceMorphLabel.setText("Beat Morph", juce::dontSendNotification);
    beatSpaceMorphLabel.setJustificationType(juce::Justification::centredLeft);
    beatSpaceMorphLabel.setTooltip("Smoothing time for monome BeatSpace XY moves.");
    addAndMakeVisible(beatSpaceMorphLabel);

    beatSpaceMorphSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    beatSpaceMorphSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 48, 16);
    beatSpaceMorphSlider.setRange(40.0, 1200.0, 10.0);
    beatSpaceMorphSlider.setValue(processor.getBeatSpaceMorphDurationMs(), juce::dontSendNotification);
    beatSpaceMorphSlider.setNumDecimalPlacesToDisplay(0);
    beatSpaceMorphSlider.setPopupDisplayEnabled(true, false, this);
    enableAltClickReset(beatSpaceMorphSlider, 160.0);
    beatSpaceMorphSlider.setTooltip("0 ms is immediate; higher values morph between BeatSpace table points.");
    beatSpaceMorphSlider.onValueChange = [this]()
    {
        processor.setBeatSpaceMorphDurationMs(beatSpaceMorphSlider.getValue());
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };
    addAndMakeVisible(beatSpaceMorphSlider);

    tooltipsToggle.setButtonText("Tooltips");
    tooltipsToggle.setClickingTogglesState(true);
    tooltipsToggle.setToggleState(false, juce::dontSendNotification);
    tooltipsToggle.setTooltip("Show or hide control descriptions on mouse hover.");
    tooltipsToggle.onClick = [this]()
    {
        if (onTooltipsToggled)
            onTooltipsToggled(tooltipsToggle.getToggleState());
    };
    addAndMakeVisible(tooltipsToggle);
    styleUiButton(tooltipsToggle);

    momentaryToggle.setButtonText("Momentary");
    momentaryToggle.setClickingTogglesState(true);
    momentaryToggle.onClick = [this]()
    {
        processor.setControlPageMomentary(momentaryToggle.getToggleState());
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };
    momentaryToggle.setTooltip("Monome page buttons are hold-to-temporary when enabled.");
    addAndMakeVisible(momentaryToggle);
    styleUiButton(momentaryToggle);
    momentaryToggle.setColour(juce::ToggleButton::textColourId, kAccent.brighter(0.05f));
    momentaryToggle.setColour(juce::ToggleButton::tickColourId, kAccent);
    momentaryToggle.setColour(juce::ToggleButton::tickDisabledColourId, kAccent.withAlpha(0.45f));

    soundTouchToggle.setButtonText("SoundTouch");
    soundTouchToggle.setClickingTogglesState(true);
    soundTouchToggle.setTooltip("Enable SoundTouch swing warp in Loop/Gate playback.");
    addAndMakeVisible(soundTouchToggle);
    styleUiButton(soundTouchToggle);
    soundTouchEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, "soundTouchEnabled", soundTouchToggle);
    soundTouchToggle.onClick = [this]()
    {
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };

    hostedLoadButton.setButtonText("Load Host");
    hostedLoadButton.setTooltip("Load a hosted instrument plugin (.vst3 or .component).");
    hostedLoadButton.onClick = [this]() { loadHostedPlugin(); };
    addAndMakeVisible(hostedLoadButton);
    styleUiButton(hostedLoadButton);

    hostedShowGuiButton.setButtonText("Open GUI");
    hostedShowGuiButton.setTooltip("Open the hosted instrument editor.");
    hostedShowGuiButton.onClick = [this]() { openHostedPluginEditor(); };
    addAndMakeVisible(hostedShowGuiButton);
    styleUiButton(hostedShowGuiButton);

    hostedMakeDefaultButton.setButtonText("Make Plugin Default");
    hostedMakeDefaultButton.setTooltip("Store the currently loaded hosted plugin and auto-load it next time.");
    hostedMakeDefaultButton.onClick = [this]() { makeHostedPluginDefault(); };
    addAndMakeVisible(hostedMakeDefaultButton);
    styleUiButton(hostedMakeDefaultButton, true);

    microtonicPresetStripLabel.setText("Strip", juce::dontSendNotification);
    microtonicPresetStripLabel.setJustificationType(juce::Justification::centredLeft);
    microtonicPresetStripLabel.setTooltip("Target lane for Microtonic patch-only presets.");
    addAndMakeVisible(microtonicPresetStripLabel);

    for (int channel = 0; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
        microtonicPresetStripBox.addItem("Lane " + juce::String(channel + 1), channel + 1);
    microtonicPresetStripBox.setSelectedId(1, juce::dontSendNotification);
    microtonicPresetStripBox.onChange = [this]() { rebuildMicrotonicPresetList(); };
    microtonicPresetStripBox.setTooltip("Select the strip whose Microtonic patch preset list is shown.");
    styleUiCombo(microtonicPresetStripBox);
    addAndMakeVisible(microtonicPresetStripBox);

    microtonicPresetListLabel.setText("Patch Presets", juce::dontSendNotification);
    microtonicPresetListLabel.setJustificationType(juce::Justification::centredLeft);
    microtonicPresetListLabel.setTooltip("Stored Microtonic patch snapshots (pattern is excluded).");
    addAndMakeVisible(microtonicPresetListLabel);

    microtonicPresetListBox.setTooltip("Recall or overwrite patch-only snapshots for this strip.");
    styleUiCombo(microtonicPresetListBox);
    addAndMakeVisible(microtonicPresetListBox);

    microtonicPresetStoreButton.setButtonText("Store Patch");
    microtonicPresetStoreButton.setTooltip("Store current Microtonic patch state for the selected strip.");
    microtonicPresetStoreButton.onClick = [this]()
    {
        const int strip = juce::jlimit(
            0,
            StepVstHostAudioProcessor::BeatSpaceChannels - 1,
            microtonicPresetStripBox.getSelectedId() - 1);
        const int selectedId = microtonicPresetListBox.getSelectedId();
        int preferredSlot = -1;
        if (selectedId >= 1 && selectedId <= StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
            preferredSlot = selectedId - 1;
        else if (selectedId >= 1000
                 && selectedId < (1000 + StepVstHostAudioProcessor::MicrotonicStripPresetSlots))
            preferredSlot = selectedId - 1000;

        int storedSlot = -1;
        juce::String error;
        if (!processor.storeCurrentMicrotonicStripPreset(strip, preferredSlot, {}, &storedSlot, &error))
        {
            hostedStatusLabel.setText(
                "Patch store failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
                juce::dontSendNotification);
            return;
        }

        rebuildMicrotonicPresetList();
        if (storedSlot >= 0)
            microtonicPresetListBox.setSelectedId(storedSlot + 1, juce::dontSendNotification);
        hostedStatusLabel.setText(
            "Stored patch for lane " + juce::String(strip + 1) + " slot "
                + juce::String(storedSlot + 1).paddedLeft('0', 2),
            juce::dontSendNotification);
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };
    styleUiButton(microtonicPresetStoreButton, true);
    addAndMakeVisible(microtonicPresetStoreButton);

    microtonicPresetRecallButton.setButtonText("Recall Patch");
    microtonicPresetRecallButton.setTooltip("Recall stored Microtonic patch state for this strip.");
    microtonicPresetRecallButton.onClick = [this]()
    {
        const int strip = juce::jlimit(
            0,
            StepVstHostAudioProcessor::BeatSpaceChannels - 1,
            microtonicPresetStripBox.getSelectedId() - 1);
        const int selectedId = microtonicPresetListBox.getSelectedId();
        if (selectedId < 1 || selectedId > StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
            return;

        juce::String error;
        if (!processor.recallMicrotonicStripPreset(strip, selectedId - 1, &error))
        {
            hostedStatusLabel.setText(
                "Patch recall failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
                juce::dontSendNotification);
            return;
        }

        hostedStatusLabel.setText(
            "Recalled patch for lane " + juce::String(strip + 1) + " slot "
                + juce::String(selectedId).paddedLeft('0', 2),
            juce::dontSendNotification);
    };
    styleUiButton(microtonicPresetRecallButton);
    addAndMakeVisible(microtonicPresetRecallButton);

    microtonicPresetDeleteButton.setButtonText("Delete");
    microtonicPresetDeleteButton.setTooltip("Delete selected patch preset from this strip.");
    microtonicPresetDeleteButton.onClick = [this]()
    {
        const int strip = juce::jlimit(
            0,
            StepVstHostAudioProcessor::BeatSpaceChannels - 1,
            microtonicPresetStripBox.getSelectedId() - 1);
        const int selectedId = microtonicPresetListBox.getSelectedId();
        if (selectedId < 1 || selectedId > StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
            return;

        if (!processor.deleteMicrotonicStripPreset(strip, selectedId - 1))
            return;

        rebuildMicrotonicPresetList();
        hostedStatusLabel.setText(
            "Deleted patch in lane " + juce::String(strip + 1) + " slot "
                + juce::String(selectedId).paddedLeft('0', 2),
            juce::dontSendNotification);
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };
    styleUiButton(microtonicPresetDeleteButton);
    addAndMakeVisible(microtonicPresetDeleteButton);

    hostedStatusLabel.setText("Host: no instrument loaded", juce::dontSendNotification);
    hostedStatusLabel.setFont(juce::Font(juce::FontOptions(9.0f)));
    hostedStatusLabel.setJustificationType(juce::Justification::centredLeft);
    hostedStatusLabel.setColour(juce::Label::textColourId, kTextMuted);
    hostedStatusLabel.setTooltip("Status of the hosted instrument plugin.");
    addAndMakeVisible(hostedStatusLabel);

    beatSpacePreviewLabel.setText("BeatSpace", juce::dontSendNotification);
    beatSpacePreviewLabel.setJustificationType(juce::Justification::centredLeft);
    beatSpacePreviewLabel.setColour(juce::Label::textColourId, kTextSecondary);
    beatSpacePreviewLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    beatSpacePreviewLabel.setTooltip("BeatSpace cursor preview from monome page.");
    addAndMakeVisible(beatSpacePreviewLabel);

    for (int channel = 0; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
    {
        auto& button = beatSpacePathButtons[static_cast<size_t>(channel)];
        button.setButtonText("P" + juce::String(channel + 1));
        button.setClickingTogglesState(true);
        button.setTooltip(
            "Arm BeatSpace path record for channel " + juce::String(channel + 1)
            + ". Then click-drag on the BeatSpace display to draw.\n"
            + "Right-click clears this channel path.");
        button.onClick = [this, channel]()
        {
            const bool armed = beatSpacePathButtons[static_cast<size_t>(channel)].getToggleState();
            processor.setBeatSpacePathRecordArmedChannel(armed ? channel : -1);
            refreshFromProcessor();
            if (!beatSpacePreviewBounds.isEmpty())
                repaint(beatSpacePreviewBounds.expanded(2));
        };
        button.addMouseListener(this, false);
        styleUiButton(button);
        addAndMakeVisible(button);
    }
    
    updateHostedPluginStatus();
    rebuildMicrotonicPresetList();
    refreshFromProcessor();
    globalUiReady = true;
    startTimerHz(30);
}

GlobalControlPanel::~GlobalControlPanel()
{
    stopTimer();
    closeHostedPluginEditor();
}

void GlobalControlPanel::timerCallback()
{
    if (beatSpacePreviewBounds.isEmpty() || !isShowing())
        return;

    const auto state = processor.getBeatSpaceVisualState();
    bool anyPathActive = false;
    for (const bool active : state.pathActive)
    {
        if (active)
        {
            anyPathActive = true;
            break;
        }
    }

    // Keep BeatSpace playback/morph visuals live even when no mouse interaction occurs.
    if (state.anyMorphActive || anyPathActive || beatSpacePathRecordingActive)
        repaint(beatSpacePreviewBounds.expanded(2));
}

void GlobalControlPanel::loadHostedPlugin()
{
    auto startDir = hostedLastPluginFile;
    if (startDir.exists())
    {
        if (!startDir.isDirectory())
            startDir = startDir.getParentDirectory();
    }
    else
    {
        const auto defaultHostedFile = processor.getDefaultHostedInstrumentFile();
        if (defaultHostedFile != juce::File())
            startDir = defaultHostedFile;

        const juce::File systemVst3("/Library/Audio/Plug-Ins/VST3");
        const juce::File systemAu("/Library/Audio/Plug-Ins/Components");
        if (startDir == juce::File() && systemVst3.exists() && systemVst3.isDirectory())
            startDir = systemVst3;
        else if (startDir == juce::File() && systemAu.exists() && systemAu.isDirectory())
            startDir = systemAu;
        else if (startDir == juce::File())
            startDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    }

    if (!startDir.isDirectory())
        startDir = startDir.getParentDirectory();

    juce::FileChooser chooser("Load Hosted Instrument Plugin", startDir, "*.vst3;*.component");
    if (!chooser.browseForFileToOpen())
        return;

    const auto file = chooser.getResult();
    hostedLastPluginFile = file;

    closeHostedPluginEditor();

    juce::String error;
    if (!processor.loadHostedInstrument(file, error))
    {
        if (error.isEmpty())
            error = "Unknown error";
        hostedStatusLabel.setText("Host load failed: " + error, juce::dontSendNotification);
        hostedShowGuiButton.setEnabled(false);
        return;
    }

    updateHostedPluginStatus();
    openHostedPluginEditor();
}

void GlobalControlPanel::openHostedPluginEditor()
{
    if (hostedEditorWindow != nullptr)
    {
        hostedEditorWindow->toFront(true);
        return;
    }

    auto* instance = processor.getHostRack().getInstance();
    if (instance == nullptr)
    {
        hostedStatusLabel.setText("Host: no instrument loaded", juce::dontSendNotification);
        hostedShowGuiButton.setEnabled(false);
        return;
    }

    if (!instance->hasEditor())
    {
        hostedStatusLabel.setText("Host loaded (no GUI): " + instance->getName(), juce::dontSendNotification);
        hostedShowGuiButton.setEnabled(false);
        return;
    }

    if (auto* editor = instance->createEditorIfNeeded())
    {
        auto safeThis = juce::Component::SafePointer<GlobalControlPanel>(this);
        auto window = std::make_unique<HostedPluginEditorWindow>("Hosted: " + instance->getName(),
            [safeThis]()
            {
                if (safeThis != nullptr)
                    safeThis->closeHostedPluginEditor();
            });

        const int width = juce::jmax(420, editor->getWidth());
        const int height = juce::jmax(260, editor->getHeight());
        window->setResizeLimits(320, 220, 1920, 1400);
        window->setContentOwned(editor, true);
        window->centreWithSize(width, height);
        window->setVisible(true);
        hostedEditorWindow = std::move(window);
        hostedShowGuiButton.setEnabled(true);
        hostedStatusLabel.setText("Host loaded: " + instance->getName(), juce::dontSendNotification);
        return;
    }

    hostedStatusLabel.setText("Host GUI unavailable: " + instance->getName(), juce::dontSendNotification);
}

void GlobalControlPanel::makeHostedPluginDefault()
{
    juce::String error;
    if (!processor.setLoadedHostedInstrumentAsDefault(error))
    {
        if (error.isEmpty())
            error = "No hosted plugin loaded";
        hostedStatusLabel.setText("Host default failed: " + error, juce::dontSendNotification);
        return;
    }

    const auto file = processor.getDefaultHostedInstrumentFile();
    if (file != juce::File())
        hostedLastPluginFile = file;

    auto* instance = processor.getHostRack().getInstance();
    const juce::String pluginName = (instance != nullptr) ? instance->getName() : file.getFileName();
    hostedStatusLabel.setText("Host default set: " + pluginName, juce::dontSendNotification);
}

void GlobalControlPanel::closeHostedPluginEditor()
{
    if (hostedEditorWindow == nullptr)
        return;

    hostedEditorWindow->setVisible(false);
    hostedEditorWindow.reset();
}

void GlobalControlPanel::updateHostedPluginStatus()
{
    auto* instance = processor.getHostRack().getInstance();
    if (instance == nullptr)
    {
        hostedShowGuiButton.setEnabled(false);
        hostedMakeDefaultButton.setEnabled(false);
        hostedStatusLabel.setText("Host: no instrument loaded", juce::dontSendNotification);
        return;
    }

    const auto loadedFile = processor.getLoadedHostedInstrumentFile();
    if (loadedFile != juce::File())
        hostedLastPluginFile = loadedFile;

    hostedShowGuiButton.setEnabled(instance->hasEditor());
    hostedMakeDefaultButton.setEnabled(true);
    if (instance->hasEditor())
        hostedStatusLabel.setText("Host loaded: " + instance->getName(), juce::dontSendNotification);
    else
        hostedStatusLabel.setText("Host loaded (no GUI): " + instance->getName(), juce::dontSendNotification);
}

void GlobalControlPanel::rebuildMicrotonicPresetList()
{
    const int strip = juce::jlimit(
        0,
        StepVstHostAudioProcessor::BeatSpaceChannels - 1,
        microtonicPresetStripBox.getSelectedId() - 1);

    const int selectedBefore = microtonicPresetListBox.getSelectedId();
    microtonicPresetListBox.clear(juce::dontSendNotification);

    std::array<bool, StepVstHostAudioProcessor::MicrotonicStripPresetSlots> used{};
    used.fill(false);
    const auto slots = processor.getMicrotonicStripPresetSlots(strip);
    for (const int slot : slots)
    {
        const int clampedSlot = juce::jlimit(0, StepVstHostAudioProcessor::MicrotonicStripPresetSlots - 1, slot);
        used[static_cast<size_t>(clampedSlot)] = true;
        const auto label = processor.getMicrotonicStripPresetName(strip, clampedSlot).trim();
        const auto itemText = juce::String(clampedSlot + 1).paddedLeft('0', 2) + "  "
            + (label.isNotEmpty() ? label : ("Patch " + juce::String(clampedSlot + 1).paddedLeft('0', 2)));
        microtonicPresetListBox.addItem(itemText, clampedSlot + 1);
    }

    int firstFreeSlot = -1;
    for (int slot = 0; slot < StepVstHostAudioProcessor::MicrotonicStripPresetSlots; ++slot)
    {
        if (!used[static_cast<size_t>(slot)])
        {
            firstFreeSlot = slot;
            break;
        }
    }
    if (firstFreeSlot >= 0)
    {
        microtonicPresetListBox.addItem(
            "New Slot " + juce::String(firstFreeSlot + 1).paddedLeft('0', 2),
            1000 + firstFreeSlot);
    }

    int nextSelected = selectedBefore;
    if (microtonicPresetListBox.indexOfItemId(nextSelected) < 0)
    {
        nextSelected = (slots.empty() && firstFreeSlot >= 0)
            ? (1000 + firstFreeSlot)
            : (slots.empty() ? 0 : (slots.front() + 1));
    }
    if (nextSelected != 0 && microtonicPresetListBox.indexOfItemId(nextSelected) >= 0)
        microtonicPresetListBox.setSelectedId(nextSelected, juce::dontSendNotification);

    const int selectedId = microtonicPresetListBox.getSelectedId();
    const bool hasStoredSelection = (selectedId >= 1
        && selectedId <= StepVstHostAudioProcessor::MicrotonicStripPresetSlots);
    microtonicPresetRecallButton.setEnabled(hasStoredSelection);
    microtonicPresetDeleteButton.setEnabled(hasStoredSelection);
    microtonicPresetStoreButton.setEnabled(true);
}

//==============================================================================
// PresetControlPanel Implementation
//==============================================================================

PresetControlPanel::PresetControlPanel(StepVstHostAudioProcessor& p)
    : processor(p)
{
    instructionsLabel.setText("Click=load default/preset  Shift+Click=save  Right-click=delete", juce::dontSendNotification);
    instructionsLabel.setJustificationType(juce::Justification::centredLeft);
    instructionsLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(instructionsLabel);

    presetNameEditor.setTextToShowWhenEmpty("Preset name", kTextMuted);
    presetNameEditor.setMultiLine(false);
    presetNameEditor.setReturnKeyStartsNewLine(false);
    presetNameEditor.setSelectAllWhenFocused(true);
    presetNameEditor.setMouseClickGrabsKeyboardFocus(true);
    presetNameEditor.onTextChange = [this]() { presetNameDraft = presetNameEditor.getText(); };
    presetNameEditor.onFocusLost = [this]()
    {
        presetNameDraft = presetNameEditor.getText();
    };
    presetNameEditor.onReturnKey = [this]()
    {
        savePresetClicked(selectedPresetIndex, presetNameEditor.getText());
    };
    addAndMakeVisible(presetNameEditor);

    saveButton.setButtonText("Save");
    saveButton.onClick = [this]()
    {
        auto safePanel = juce::Component::SafePointer<PresetControlPanel>(this);
        // Defer one message tick so in-flight text edits are committed first.
        juce::MessageManager::callAsync([safe = safePanel]()
        {
            if (safe == nullptr)
                return;
            safe->savePresetClicked(safe->selectedPresetIndex, safe->presetNameEditor.getText());
        });
    };
    addAndMakeVisible(saveButton);
    styleUiButton(saveButton, true);

    deleteButton.setButtonText("Delete");
    deleteButton.onClick = [this]()
    {
        if (processor.deletePreset(selectedPresetIndex))
            updatePresetButtons();
    };
    addAndMakeVisible(deleteButton);
    styleUiButton(deleteButton);

    exportWavButton.setButtonText("Export");
    exportWavButton.onClick = [this]() { exportRecordingsAsWav(); };
    exportWavButton.setTooltip("Export current strip recordings to WAV files.");
    addAndMakeVisible(exportWavButton);
    styleUiButton(exportWavButton);

    presetViewport.setViewedComponent(&presetGridContent, false);
    presetViewport.setScrollBarsShown(true, true, true, true);
    presetViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible(presetViewport);

    // 16x7 preset grid, origin 0x0
    for (int i = 0; i < StepVstHostAudioProcessor::MaxPresetSlots; ++i)
    {
        const int x = i % StepVstHostAudioProcessor::PresetColumns;
        const int y = i / StepVstHostAudioProcessor::PresetColumns;
        auto& button = presetButtons[static_cast<size_t>(i)];
        button.setButtonText(juce::String(x) + "," + juce::String(y));
        button.setClickingTogglesState(false);
        styleUiButton(button);
        button.addMouseListener(this, false);

        button.onClick = [this, i]()
        {
            if (juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
            {
                auto safePanel = juce::Component::SafePointer<PresetControlPanel>(this);
                juce::MessageManager::callAsync([safe = safePanel, i]()
                {
                    if (safe == nullptr)
                        return;
                    safe->savePresetClicked(i, safe->presetNameEditor.getText());
                });
            }
            else
                loadPresetClicked(i);
        };
        button.setTooltip("Preset " + juce::String(i + 1) + " (" + juce::String(x) + "," + juce::String(y) + ")");
        presetGridContent.addAndMakeVisible(button);
    }
    
    selectedPresetIndex = juce::jmax(0, processor.getLoadedPresetIndex());
    presetNameDraft = processor.getPresetName(selectedPresetIndex);
    presetNameEditor.setText(presetNameDraft, juce::dontSendNotification);
    layoutPresetButtons();
    updatePresetButtons();
}

void PresetControlPanel::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), kAccent, 8.0f);
}

void PresetControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);

    auto editorArea = bounds.removeFromTop(26);
    const int saveDeleteButtonW = 60;
    const int exportButtonW = 78;
    deleteButton.setBounds(editorArea.removeFromRight(saveDeleteButtonW));
    editorArea.removeFromRight(4);
    exportWavButton.setBounds(editorArea.removeFromRight(exportButtonW));
    editorArea.removeFromRight(4);
    saveButton.setBounds(editorArea.removeFromRight(saveDeleteButtonW));
    editorArea.removeFromRight(6);

    // Keep name input compact to protect button layout on narrow widths.
    constexpr int kNameFieldMaxW = 180;
    const int nameW = juce::jmin(kNameFieldMaxW, editorArea.getWidth());
    presetNameEditor.setBounds(editorArea.removeFromLeft(nameW));
    editorArea.removeFromLeft(6);
    instructionsLabel.setBounds(editorArea);
    bounds.removeFromTop(2);

    presetViewport.setBounds(bounds);
    layoutPresetButtons();
}

void PresetControlPanel::mouseUp(const juce::MouseEvent& e)
{
    if (!e.mods.isRightButtonDown())
        return;

    for (int i = 0; i < StepVstHostAudioProcessor::MaxPresetSlots; ++i)
    {
        auto& button = presetButtons[static_cast<size_t>(i)];
        if (e.originalComponent == &button || e.eventComponent == &button)
        {
            selectedPresetIndex = i;
            if (processor.deletePreset(i))
            {
                presetNameDraft = processor.getPresetName(i);
                presetNameEditor.setText(presetNameDraft, juce::dontSendNotification);
                updatePresetButtons();
            }
            break;
        }
    }
}

void PresetControlPanel::savePresetClicked(int index, juce::String typedName)
{
    processor.savePreset(index);
    const auto trimmed = (typedName.isNotEmpty() ? typedName : presetNameEditor.getText()).trim();
    if (trimmed.isNotEmpty())
    {
        processor.setPresetName(index, trimmed);
        presetNameDraft = trimmed;
        presetNameEditor.setText(trimmed, juce::dontSendNotification);
    }
    selectedPresetIndex = index;
    updatePresetButtons();
}

void PresetControlPanel::loadPresetClicked(int index)
{
    processor.loadPreset(index);
    selectedPresetIndex = index;
    const auto name = processor.getPresetName(index);
    presetNameDraft = name;
    presetNameEditor.setText(name, juce::dontSendNotification);
}

void PresetControlPanel::exportRecordingsAsWav()
{
    auto startDir = lastExportDirectory;
    if (!startDir.exists() || !startDir.isDirectory())
        startDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    juce::FileChooser chooser("Export strip recordings to folder", startDir, "*");
    if (!chooser.browseForDirectory())
        return;

    auto targetDir = chooser.getResult();
    if (!targetDir.exists())
        targetDir.createDirectory();
    lastExportDirectory = targetDir;

    auto* engine = processor.getAudioEngine();
    if (engine == nullptr)
        return;

    int exportedCount = 0;
    int failedCount = 0;
    juce::WavAudioFormat wavFormat;

    for (int i = 0; i < StepVstHostAudioProcessor::MaxStrips; ++i)
    {
        auto* strip = engine->getStrip(i);
        if (strip == nullptr || !strip->hasAudio())
            continue;

        const auto* audioBuffer = strip->getAudioBuffer();
        const double sampleRate = strip->getSourceSampleRate();
        if (audioBuffer == nullptr || audioBuffer->getNumSamples() <= 0 || sampleRate <= 1000.0)
            continue;

        auto outFile = targetDir.getChildFile("Strip_" + juce::String(i + 1) + ".wav");
        std::unique_ptr<juce::FileOutputStream> outStream(outFile.createOutputStream());
        if (outStream == nullptr)
        {
            ++failedCount;
            continue;
        }

        auto writerStream = std::unique_ptr<juce::OutputStream>(outStream.release());
        const auto writerOptions = juce::AudioFormatWriter::Options{}
            .withSampleRate(sampleRate)
            .withNumChannels(audioBuffer->getNumChannels())
            .withBitsPerSample(24)
            .withQualityOptionIndex(0);
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(writerStream, writerOptions));

        if (writer == nullptr || !writer->writeFromAudioSampleBuffer(*audioBuffer, 0, audioBuffer->getNumSamples()))
        {
            ++failedCount;
            continue;
        }

        writer->flush();
        ++exportedCount;
    }

    const juce::String message = "Exported " + juce::String(exportedCount)
        + " strip recording(s) to:\n" + targetDir.getFullPathName()
        + (failedCount > 0 ? "\nFailed: " + juce::String(failedCount) : "");
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Export WAV", message);
}

void PresetControlPanel::updatePresetButtons()
{
    const int loadedPreset = processor.getLoadedPresetIndex();
    deleteButton.setEnabled(processor.presetExists(selectedPresetIndex));
    auto shortPresetLabel = [](const juce::String& name, int fallbackIndex) -> juce::String
    {
        auto n = name.trim();
        if (n.isEmpty())
            return juce::String(fallbackIndex + 1);
        juce::String compact;
        for (auto c : n)
        {
            if (!juce::CharacterFunctions::isWhitespace(c))
                compact << juce::String::charToString(c);
            if (compact.length() >= 4)
                break;
        }
        if (compact.isEmpty())
            compact = juce::String(fallbackIndex + 1);
        return compact.toUpperCase();
    };

    for (int i = 0; i < StepVstHostAudioProcessor::MaxPresetSlots; ++i)
    {
        bool exists = processor.presetExists(i);
        auto& button = presetButtons[static_cast<size_t>(i)];
        const juce::String presetName = exists ? processor.getPresetName(i) : juce::String();
        button.setButtonText(shortPresetLabel(presetName, i));
        juce::String tip = "Preset " + juce::String(i + 1);
        if (exists)
            tip << " - " << presetName;
        button.setTooltip(tip);
        if (i == loadedPreset && exists)
        {
            button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffb8d478));
            button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff111111));
        }
        else
        {
            const bool isSelected = (i == selectedPresetIndex);
            button.setColour(juce::TextButton::buttonColourId,
                             exists
                                 ? (isSelected ? kAccent.withMultipliedBrightness(1.1f) : kAccent.withMultipliedBrightness(0.9f))
                                 : (isSelected ? juce::Colour(0xffdce7f3) : juce::Colour(0xffedf3fa)));
            button.setColour(juce::TextButton::textColourOffId,
                             exists ? juce::Colour(0xfff7fbff) : kTextMuted);
        }
    }
}

void PresetControlPanel::layoutPresetButtons()
{
    const int gap = 4;
    const int buttonHeight = 16;
    const int minButtonWidth = 26;

    const int viewportWidth = juce::jmax(0, presetViewport.getWidth() - presetViewport.getScrollBarThickness());
    const int buttonWidth = juce::jmax(minButtonWidth,
                                       (viewportWidth - ((StepVstHostAudioProcessor::PresetColumns - 1) * gap))
                                       / StepVstHostAudioProcessor::PresetColumns);
    const int contentWidth = (StepVstHostAudioProcessor::PresetColumns * buttonWidth)
                             + ((StepVstHostAudioProcessor::PresetColumns - 1) * gap);
    const int contentHeight = (StepVstHostAudioProcessor::PresetRows * buttonHeight)
                              + ((StepVstHostAudioProcessor::PresetRows - 1) * gap);

    presetGridContent.setSize(contentWidth, contentHeight);

    for (int i = 0; i < StepVstHostAudioProcessor::MaxPresetSlots; ++i)
    {
        const int x = i % StepVstHostAudioProcessor::PresetColumns;
        const int y = i / StepVstHostAudioProcessor::PresetColumns;
        presetButtons[static_cast<size_t>(i)].setBounds(x * (buttonWidth + gap),
                                                        y * (buttonHeight + gap),
                                                        buttonWidth,
                                                        buttonHeight);
    }
}

void PresetControlPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int deltaY = static_cast<int>(-wheel.deltaY * 96.0f);
    if (deltaY != 0)
        presetViewport.setViewPosition(presetViewport.getViewPositionX(),
                                       juce::jmax(0, presetViewport.getViewPositionY() + deltaY));
}

void PresetControlPanel::refreshVisualState()
{
    updatePresetButtons();
}

//==============================================================================
// PathsControlPanel Implementation
//==============================================================================

PathsControlPanel::PathsControlPanel(StepVstHostAudioProcessor& p)
    : processor(p)
{
    titleLabel.setText("DEFAULT LOAD PATHS", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, kTextPrimary);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    scrollViewport.setViewedComponent(&scrollContent, false);
    scrollViewport.setScrollBarsShown(true, false, true, true);
    addAndMakeVisible(scrollViewport);

    headerStripLabel.setText("Strip", juce::dontSendNotification);
    headerStripLabel.setColour(juce::Label::textColourId, kTextMuted);
    headerStripLabel.setJustificationType(juce::Justification::centredLeft);
    scrollContent.addAndMakeVisible(headerStripLabel);

    headerLoopLabel.setText("Loop Mode Path", juce::dontSendNotification);
    headerLoopLabel.setColour(juce::Label::textColourId, kTextMuted);
    headerLoopLabel.setJustificationType(juce::Justification::centredLeft);
    scrollContent.addAndMakeVisible(headerLoopLabel);

    headerStepLabel.setText("Step Mode Path", juce::dontSendNotification);
    headerStepLabel.setColour(juce::Label::textColourId, kTextMuted);
    headerStepLabel.setJustificationType(juce::Justification::centredLeft);
    scrollContent.addAndMakeVisible(headerStepLabel);

    for (int i = 0; i < StepVstHostAudioProcessor::MaxStrips; ++i)
    {
        auto& row = rows[static_cast<size_t>(i)];

        row.stripLabel.setText("S" + juce::String(i + 1), juce::dontSendNotification);
        row.stripLabel.setColour(juce::Label::textColourId, getStripColor(i));
        row.stripLabel.setJustificationType(juce::Justification::centredLeft);
        scrollContent.addAndMakeVisible(row.stripLabel);

        row.loopPathLabel.setColour(juce::Label::textColourId, kTextPrimary);
        row.loopPathLabel.setJustificationType(juce::Justification::centredLeft);
        scrollContent.addAndMakeVisible(row.loopPathLabel);

        row.loopSetButton.setButtonText("Set");
        row.loopSetButton.setTooltip("Set default loop-mode sample folder.");
        row.loopSetButton.onClick = [this, i]() { chooseDirectory(i, StepVstHostAudioProcessor::SamplePathMode::Loop); };
        scrollContent.addAndMakeVisible(row.loopSetButton);

        row.loopClearButton.setButtonText("Clear");
        row.loopClearButton.setTooltip("Clear default loop-mode folder.");
        row.loopClearButton.onClick = [this, i]() { clearDirectory(i, StepVstHostAudioProcessor::SamplePathMode::Loop); };
        scrollContent.addAndMakeVisible(row.loopClearButton);

        row.stepPathLabel.setColour(juce::Label::textColourId, kTextPrimary);
        row.stepPathLabel.setJustificationType(juce::Justification::centredLeft);
        scrollContent.addAndMakeVisible(row.stepPathLabel);

        row.stepSetButton.setButtonText("Set");
        row.stepSetButton.setTooltip("Set default step-mode sample folder.");
        row.stepSetButton.onClick = [this, i]() { chooseDirectory(i, StepVstHostAudioProcessor::SamplePathMode::Step); };
        scrollContent.addAndMakeVisible(row.stepSetButton);

        row.stepClearButton.setButtonText("Clear");
        row.stepClearButton.setTooltip("Clear default step-mode folder.");
        row.stepClearButton.onClick = [this, i]() { clearDirectory(i, StepVstHostAudioProcessor::SamplePathMode::Step); };
        scrollContent.addAndMakeVisible(row.stepClearButton);
    }

    refreshLabels();
    startTimer(500);
}

void PathsControlPanel::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), kAccent, 8.0f);
}

void PathsControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);

    titleLabel.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(6);
    scrollViewport.setBounds(bounds);

    const int rowHeight = 24;
    const int contentHeight = 18 + 4 + (rowHeight * StepVstHostAudioProcessor::MaxStrips);
    const int contentWidth = juce::jmax(200, scrollViewport.getWidth() - scrollViewport.getScrollBarThickness());
    scrollContent.setSize(contentWidth, contentHeight);

    auto layout = scrollContent.getLocalBounds();

    auto header = layout.removeFromTop(18);
    const int stripWidth = 42;
    const int buttonWidth = 48;
    const int gap = 4;
    const int pathAreaWidth = (header.getWidth() - stripWidth - (4 * buttonWidth) - (6 * gap)) / 2;

    headerStripLabel.setBounds(header.removeFromLeft(stripWidth));
    header.removeFromLeft(gap);
    headerLoopLabel.setBounds(header.removeFromLeft(pathAreaWidth + (2 * buttonWidth) + (2 * gap)));
    header.removeFromLeft(gap);
    headerStepLabel.setBounds(header);

    layout.removeFromTop(4);

    for (int i = 0; i < StepVstHostAudioProcessor::MaxStrips; ++i)
    {
        auto& row = rows[static_cast<size_t>(i)];
        auto rowArea = layout.removeFromTop(rowHeight);
        rowArea.removeFromBottom(2);

        row.stripLabel.setBounds(rowArea.removeFromLeft(stripWidth));
        rowArea.removeFromLeft(gap);

        row.loopPathLabel.setBounds(rowArea.removeFromLeft(pathAreaWidth));
        rowArea.removeFromLeft(gap);
        row.loopSetButton.setBounds(rowArea.removeFromLeft(buttonWidth));
        rowArea.removeFromLeft(gap);
        row.loopClearButton.setBounds(rowArea.removeFromLeft(buttonWidth));
        rowArea.removeFromLeft(gap * 2);

        row.stepPathLabel.setBounds(rowArea.removeFromLeft(pathAreaWidth));
        rowArea.removeFromLeft(gap);
        row.stepSetButton.setBounds(rowArea.removeFromLeft(buttonWidth));
        rowArea.removeFromLeft(gap);
        row.stepClearButton.setBounds(rowArea.removeFromLeft(buttonWidth));
    }
}

void PathsControlPanel::timerCallback()
{
    refreshLabels();
}

void PathsControlPanel::refreshLabels()
{
    for (int i = 0; i < StepVstHostAudioProcessor::MaxStrips; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        const auto loopDir = processor.getDefaultSampleDirectory(i, StepVstHostAudioProcessor::SamplePathMode::Loop);
        const auto stepDir = processor.getDefaultSampleDirectory(i, StepVstHostAudioProcessor::SamplePathMode::Step);

        rows[idx].loopPathLabel.setText(pathToDisplay(loopDir), juce::dontSendNotification);
        rows[idx].loopPathLabel.setTooltip(loopDir.getFullPathName());
        rows[idx].stepPathLabel.setText(pathToDisplay(stepDir), juce::dontSendNotification);
        rows[idx].stepPathLabel.setTooltip(stepDir.getFullPathName());
    }
}

void PathsControlPanel::chooseDirectory(int stripIndex, StepVstHostAudioProcessor::SamplePathMode mode)
{
    auto startDir = processor.getDefaultSampleDirectory(stripIndex, mode);
    if (!startDir.exists() || !startDir.isDirectory())
        startDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    const juce::String modeName = (mode == StepVstHostAudioProcessor::SamplePathMode::Step) ? "Step" : "Loop";
    juce::FileChooser chooser("Select " + modeName + " Default Path for Strip " + juce::String(stripIndex + 1),
                              startDir,
                              "*");

    if (chooser.browseForDirectory())
    {
        processor.setDefaultSampleDirectory(stripIndex, mode, chooser.getResult());
        refreshLabels();
    }
}

void PathsControlPanel::clearDirectory(int stripIndex, StepVstHostAudioProcessor::SamplePathMode mode)
{
    processor.setDefaultSampleDirectory(stripIndex, mode, {});
    refreshLabels();
}

juce::String PathsControlPanel::pathToDisplay(const juce::File& file)
{
    if (!file.exists() || !file.isDirectory())
        return "(not set)";
    return file.getFullPathName();
}

void GlobalControlPanel::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), kAccent, 8.0f);

    if (beatSpacePreviewBounds.isEmpty())
        return;

    const auto state = processor.getBeatSpaceVisualState();
    auto area = beatSpacePreviewBounds.toFloat();
    g.setColour(juce::Colour(0xffd6e4f2));
    g.fillRoundedRectangle(area, 4.5f);
    g.setColour(juce::Colour(0xff8ca3bc));
    g.drawRoundedRectangle(area, 4.5f, 1.2f);

    auto innerBounds = beatSpacePreviewBounds.reduced(3);
    auto inner = innerBounds.toFloat();
    const auto previewImage = processor.getBeatSpaceTablePreviewImage();
    const int tableSize = juce::jmax(1, state.tableSize);
    const int viewX = juce::jlimit(0, tableSize - 1, state.viewX);
    const int viewY = juce::jlimit(0, tableSize - 1, state.viewY);
    const int viewW = juce::jmax(1, juce::jmin(tableSize - viewX, state.viewWidth));
    const int viewH = juce::jmax(1, juce::jmin(tableSize - viewY, state.viewHeight));
    const float scaleX = inner.getWidth() / static_cast<float>(viewW);
    const float scaleY = inner.getHeight() / static_cast<float>(viewH);

    if (previewImage.isValid())
    {
        const bool drawPerCell = (viewW <= 18 || viewH <= 18);
        if (drawPerCell)
        {
            const float cellW = inner.getWidth() / static_cast<float>(viewW);
            const float cellH = inner.getHeight() / static_cast<float>(viewH);
            for (int yy = 0; yy < viewH; ++yy)
            {
                for (int xx = 0; xx < viewW; ++xx)
                {
                    const auto c = previewImage.getPixelAt(viewX + xx, viewY + yy);
                    const float px = inner.getX() + (static_cast<float>(xx) * cellW);
                    const float py = inner.getY() + (static_cast<float>(yy) * cellH);
                    g.setColour(c);
                    g.fillRect(px, py, juce::jmax(1.0f, cellW + 0.45f), juce::jmax(1.0f, cellH + 0.45f));
                }
            }
        }
        else
        {
            g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.drawImage(
                previewImage,
                innerBounds.getX(),
                innerBounds.getY(),
                innerBounds.getWidth(),
                innerBounds.getHeight(),
                viewX,
                viewY,
                viewW,
                viewH);
        }

        if (state.confidenceOverlayEnabled)
        {
            const auto confidenceImage = processor.getBeatSpaceConfidencePreviewImage();
            if (confidenceImage.isValid())
            {
                if (drawPerCell)
                {
                    const float cellW = inner.getWidth() / static_cast<float>(viewW);
                    const float cellH = inner.getHeight() / static_cast<float>(viewH);
                    for (int yy = 0; yy < viewH; ++yy)
                    {
                        for (int xx = 0; xx < viewW; ++xx)
                        {
                            const auto c = confidenceImage.getPixelAt(viewX + xx, viewY + yy);
                            const float alpha = 0.22f * static_cast<float>(c.getRed()) / 255.0f;
                            if (alpha <= 0.001f)
                                continue;
                            const float px = inner.getX() + (static_cast<float>(xx) * cellW);
                            const float py = inner.getY() + (static_cast<float>(yy) * cellH);
                            g.setColour(juce::Colours::white.withAlpha(alpha));
                            g.fillRect(px, py, juce::jmax(1.0f, cellW + 0.45f), juce::jmax(1.0f, cellH + 0.45f));
                        }
                    }
                }
                else
                {
                    g.setOpacity(0.28f);
                    g.drawImage(
                        confidenceImage,
                        innerBounds.getX(),
                        innerBounds.getY(),
                        innerBounds.getWidth(),
                        innerBounds.getHeight(),
                        viewX,
                        viewY,
                        viewW,
                        viewH);
                    g.setOpacity(1.0f);
                }
            }
        }

        juce::ColourGradient vignette(
            juce::Colour(0x00000000), inner.getCentreX(), inner.getCentreY(),
            juce::Colour(0x2a0c1a2a), inner.getX(), inner.getY(), true);
        g.setGradientFill(vignette);
        g.fillRect(inner);
    }
    else
    {
        g.setColour(juce::Colour(0xfff6f9fc));
        g.fillRect(inner);
    }
    g.setColour(juce::Colour(0xffc2d2e2).withAlpha(0.92f));
    g.drawRect(inner, 1.0f);

    if (!state.decoderReady)
    {
        g.setColour(kTextMuted);
        g.drawFittedText("decoder missing", beatSpacePreviewBounds, juce::Justification::centred, 1);
        return;
    }

    constexpr std::array<uint32_t, StepVstHostAudioProcessor::BeatSpaceChannels> kCategoryColors {
        0xffea5455u, // Kick
        0xfff07f3au, // Snare
        0xff6bcf80u, // Closed hat
        0xff4db6b8u, // Open hat
        0xff5f8fd3u, // Perc
        0xff9f7ccbu  // Misc
    };
    constexpr std::array<const char*, StepVstHostAudioProcessor::BeatSpaceChannels> kCategoryShortNames {
        "KICK", "SNARE", "C-HAT", "O-HAT", "PERC", "MISC"
    };
    constexpr std::array<const char*, StepVstHostAudioProcessor::BeatSpaceChannels> kCategoryChipNames {
        "K", "S", "CH", "OH", "PC", "MX"
    };
    constexpr std::array<const char*, StepVstHostAudioProcessor::BeatSpaceChannels> kCategoryLongNames {
        "Kick", "Snare", "Closed Hat", "Open Hat", "Percussion", "Misc"
    };
    const bool showTextOverlay = beatSpaceTextOverlayEnabled;

    // Grid guides help read cluster locations and zoom context.
    const int coarseStep = (viewW <= 16 || viewH <= 16) ? 2 : ((viewW <= 32 || viewH <= 32) ? 4 : 8);
    if (coarseStep > 0)
    {
        for (int gx = 0; gx <= viewW; gx += coarseStep)
        {
            const float px = inner.getX() + (static_cast<float>(gx) * scaleX);
            g.setColour(juce::Colour(0xfff5fbff).withAlpha((gx % (coarseStep * 2) == 0) ? 0.16f : 0.08f));
            g.drawVerticalLine(static_cast<int>(std::round(px)), inner.getY(), inner.getBottom());
        }
        for (int gy = 0; gy <= viewH; gy += coarseStep)
        {
            const float py = inner.getY() + (static_cast<float>(gy) * scaleY);
            g.setColour(juce::Colour(0xfff5fbff).withAlpha((gy % (coarseStep * 2) == 0) ? 0.16f : 0.08f));
            g.drawHorizontalLine(static_cast<int>(std::round(py)), inner.getX(), inner.getRight());
        }
    }

    auto drawCategoryRegion = [&](int category)
    {
        const auto anchor = state.categoryAnchors[static_cast<size_t>(category)];
        const int radiusX = juce::jmax(1, state.categoryRadiusX[static_cast<size_t>(category)]);
        const int radiusY = juce::jmax(1, state.categoryRadiusY[static_cast<size_t>(category)]);
        const int relAnchorX = anchor.x - viewX;
        const int relAnchorY = anchor.y - viewY;
        if (relAnchorX < -radiusX || relAnchorX >= (viewW + radiusX)
            || relAnchorY < -radiusY || relAnchorY >= (viewH + radiusY))
            return;

        const bool manual = state.categoryManual[static_cast<size_t>(category)];
        const auto colour = juce::Colour(kCategoryColors[static_cast<size_t>(category)]);
        const float centerX = inner.getX() + ((static_cast<float>(relAnchorX) + 0.5f) * scaleX);
        const float centerY = inner.getY() + ((static_cast<float>(relAnchorY) + 0.5f) * scaleY);
        const float regionRadiusX = juce::jmax(5.0f, (static_cast<float>(radiusX) + 0.6f) * scaleX);
        const float regionRadiusY = juce::jmax(5.0f, (static_cast<float>(radiusY) + 0.6f) * scaleY);
        const auto ellipseBounds = juce::Rectangle<float>(
            centerX - regionRadiusX,
            centerY - regionRadiusY,
            regionRadiusX * 2.0f,
            regionRadiusY * 2.0f);

        if (!ellipseBounds.intersects(inner))
            return;

        juce::ColourGradient regionGradient(
            colour.withAlpha(manual ? 0.30f : 0.18f), centerX, centerY,
            juce::Colour(0x00000000), centerX + (regionRadiusX * 0.75f), centerY + (regionRadiusY * 0.75f), true);
        g.saveState();
        g.reduceClipRegion(innerBounds);
        g.setGradientFill(regionGradient);
        g.fillEllipse(ellipseBounds);
        g.restoreState();
        g.setColour(colour.withAlpha(manual ? 0.90f : 0.56f));
        g.drawEllipse(ellipseBounds, manual ? 1.8f : 1.1f);

        if (relAnchorX < 0 || relAnchorX >= viewW || relAnchorY < 0 || relAnchorY >= viewH)
            return;
        const float ax = centerX;
        const float ay = centerY;
        g.setColour(juce::Colours::white.withAlpha(manual ? 0.95f : 0.78f));
        g.fillEllipse(ax - 2.8f, ay - 2.8f, 5.6f, 5.6f);
        g.setColour(colour.withAlpha(manual ? 0.98f : 0.80f));
        g.fillEllipse(ax - 1.9f, ay - 1.9f, 3.8f, 3.8f);

        if (showTextOverlay)
        {
            const juce::String tagText = kCategoryShortNames[static_cast<size_t>(category)];
            auto tagFont = juce::Font(juce::FontOptions(7.0f, juce::Font::bold));
            const float tagWidth = juce::jmax(
                20.0f,
                8.0f + (5.2f * static_cast<float>(tagText.length())));
            auto tag = juce::Rectangle<float>(ax + 3.0f, ay - 6.0f, tagWidth, 10.0f)
                .constrainedWithin(inner.reduced(1.0f));
            g.setColour(juce::Colour(0xff0f2234).withAlpha(0.62f));
            g.fillRoundedRectangle(tag, 1.6f);
            g.setColour(colour.withAlpha(0.92f));
            g.drawRoundedRectangle(tag, 1.6f, 0.8f);
            g.setColour(juce::Colour(0xffeef5ff).withAlpha(0.95f));
            g.setFont(tagFont);
            g.drawFittedText(tagText,
                             tag.toNearestInt(),
                             juce::Justification::centred,
                             1);
        }
    };

    for (int category = 0; category < StepVstHostAudioProcessor::BeatSpaceChannels; ++category)
        drawCategoryRegion(category);

    if (showTextOverlay)
    {
        auto legendArea = inner.reduced(3.0f).removeFromTop(14.0f);
        legendArea.removeFromLeft(206.0f);
        if (state.linkAllChannels)
            legendArea.removeFromRight(62.0f);
        if (legendArea.getWidth() > 70.0f)
        {
            g.setFont(juce::Font(juce::FontOptions(7.0f, juce::Font::bold)));
            float cursorX = legendArea.getX();
            for (int category = 0; category < StepVstHostAudioProcessor::BeatSpaceChannels; ++category)
            {
                const auto colour = juce::Colour(kCategoryColors[static_cast<size_t>(category)]);
                const juce::String label = kCategoryLongNames[static_cast<size_t>(category)];
                const float pillW = juce::jmax(
                    26.0f,
                    10.0f + (5.0f * static_cast<float>(label.length())));
                if ((cursorX + pillW) > legendArea.getRight())
                    break;
                auto pill = juce::Rectangle<float>(cursorX, legendArea.getY(), pillW, legendArea.getHeight());
                g.setColour(juce::Colour(0xff0f2234).withAlpha(0.63f));
                g.fillRoundedRectangle(pill, 1.8f);
                g.setColour(colour.withAlpha(0.90f));
                g.drawRoundedRectangle(pill, 1.8f, 0.9f);
                g.setColour(juce::Colour(0xffeef5ff).withAlpha(0.96f));
                g.drawFittedText(label, pill.toNearestInt(), juce::Justification::centred, 1);
                cursorX += pillW + 2.0f;
            }
        }
    }

    auto channelColour = [&](int channel) -> juce::Colour
    {
        const int clampedChannel = juce::jlimit(0, StepVstHostAudioProcessor::BeatSpaceChannels - 1, channel);
        const int assignment = juce::jlimit(
            0,
            StepVstHostAudioProcessor::BeatSpaceChannels - 1,
            state.channelCategoryAssignment[static_cast<size_t>(clampedChannel)]);
        return juce::Colour(kCategoryColors[static_cast<size_t>(assignment)]);
    };

    auto pointToPixel = [&](const juce::Point<int>& p) -> juce::Point<float>
    {
        const int relX = juce::jlimit(0, viewW - 1, p.x - viewX);
        const int relY = juce::jlimit(0, viewH - 1, p.y - viewY);
        return {
            inner.getX() + ((static_cast<float>(relX) + 0.5f) * scaleX),
            inner.getY() + ((static_cast<float>(relY) + 0.5f) * scaleY)
        };
    };

    if (state.pathOverlayEnabled)
    {
        const bool pulseOn = ((juce::Time::getMillisecondCounter() / 220u) & 1u) == 0u;
        for (int channel = 0; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
        {
            const auto idx = static_cast<size_t>(channel);
            const int pathCount = juce::jlimit(0, StepVstHostAudioProcessor::BeatSpacePathMaxPoints, state.pathPointCounts[idx]);
            if (pathCount < 2)
                continue;

            juce::Path overlayPath;
            for (int p = 0; p < pathCount; ++p)
            {
                const auto pixel = pointToPixel(state.pathPoints[idx][static_cast<size_t>(p)]);
                if (p == 0)
                    overlayPath.startNewSubPath(pixel);
                else
                    overlayPath.lineTo(pixel);
            }

            const auto colour = channelColour(channel);
            const bool selected = (channel == state.selectedChannel);
            g.setColour(juce::Colours::black.withAlpha(state.pathActive[idx] ? 0.52f : 0.34f));
            g.strokePath(
                overlayPath,
                juce::PathStrokeType(selected ? 7.0f : 5.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(colour.withAlpha(state.pathActive[idx] ? 0.98f : 0.76f));
            g.strokePath(
                overlayPath,
                juce::PathStrokeType(selected ? 4.2f : 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            for (int p = 0; p < pathCount; ++p)
            {
                const auto pixel = pointToPixel(state.pathPoints[idx][static_cast<size_t>(p)]);
                const float radius = (p == 0) ? 2.6f : 1.9f;
                g.setColour(juce::Colours::white.withAlpha(0.92f));
                g.fillEllipse(pixel.x - (radius + 0.9f), pixel.y - (radius + 0.9f),
                              (radius + 0.9f) * 2.0f, (radius + 0.9f) * 2.0f);
                g.setColour(colour.withAlpha(0.95f));
                g.fillEllipse(pixel.x - radius, pixel.y - radius, radius * 2.0f, radius * 2.0f);
            }

            if (state.pathActive[idx])
            {
                const auto headPoint = state.channelMorphActive[idx]
                    ? state.channelMorphCurrent[idx]
                    : state.channelPoints[idx];
                const auto headPixel = pointToPixel(headPoint);
                const float headRadius = selected ? 6.8f : 5.3f;
                g.setColour(colour.withAlpha(pulseOn ? 0.28f : 0.16f));
                g.fillEllipse(
                    headPixel.x - (headRadius + 2.5f),
                    headPixel.y - (headRadius + 2.5f),
                    (headRadius + 2.5f) * 2.0f,
                    (headRadius + 2.5f) * 2.0f);
                g.setColour(juce::Colours::white.withAlpha(0.96f));
                g.drawEllipse(
                    headPixel.x - headRadius,
                    headPixel.y - headRadius,
                    headRadius * 2.0f,
                    headRadius * 2.0f,
                    selected ? 1.8f : 1.3f);
                g.setColour(colour.withAlpha(0.95f));
                g.drawEllipse(
                    headPixel.x - (headRadius - 1.6f),
                    headPixel.y - (headRadius - 1.6f),
                    (headRadius - 1.6f) * 2.0f,
                    (headRadius - 1.6f) * 2.0f,
                    selected ? 1.5f : 1.1f);
            }
        }
    }

    auto drawPoint = [&](const juce::Point<int>& p, juce::Colour colour, float radius, bool forceDraw)
    {
        const int relX = p.x - viewX;
        const int relY = p.y - viewY;
        if (!forceDraw && (relX < 0 || relX >= viewW || relY < 0 || relY >= viewH))
            return;

        const auto pixel = pointToPixel(p);
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.fillEllipse(pixel.x - (radius + 1.15f), pixel.y - (radius + 1.15f), (radius + 1.15f) * 2.0f, (radius + 1.15f) * 2.0f);
        g.setColour(colour);
        g.fillEllipse(pixel.x - radius, pixel.y - radius, radius * 2.0f, radius * 2.0f);
    };

    const int selectedChannel = juce::jlimit(
        0,
        StepVstHostAudioProcessor::BeatSpaceChannels - 1,
        state.selectedChannel);
    const int selectedAssignment = juce::jlimit(
        0,
        StepVstHostAudioProcessor::BeatSpaceChannels - 1,
        state.channelCategoryAssignment[static_cast<size_t>(selectedChannel)]);

    auto visualPointForChannel = [&](int channel) -> juce::Point<int>
    {
        const auto idx = static_cast<size_t>(juce::jlimit(
            0,
            StepVstHostAudioProcessor::BeatSpaceChannels - 1,
            channel));
        return state.channelMorphActive[idx]
            ? state.channelMorphCurrent[idx]
            : state.channelPoints[idx];
    };

    for (int c = 0; c < StepVstHostAudioProcessor::BeatSpaceChannels; ++c)
    {
        const auto idx = static_cast<size_t>(c);
        if (!state.channelMorphActive[idx])
            continue;
        const auto from = pointToPixel(state.channelMorphFrom[idx]);
        const auto to = pointToPixel(state.channelMorphTo[idx]);
        const auto current = pointToPixel(state.channelMorphCurrent[idx]);
        const auto colour = channelColour(c);
        g.setColour(colour.withAlpha(c == selectedChannel ? 0.62f : 0.42f));
        g.drawLine(from.x, from.y, to.x, to.y, c == selectedChannel ? 1.8f : 1.3f);
        g.setColour(colour.withAlpha(0.35f));
        g.fillEllipse(from.x - 1.5f, from.y - 1.5f, 3.0f, 3.0f);
        g.setColour(colour.withAlpha(0.30f));
        g.fillEllipse(to.x - 1.8f, to.y - 1.8f, 3.6f, 3.6f);
        g.setColour(colour.withAlpha(c == selectedChannel ? 0.95f : 0.80f));
        g.fillEllipse(current.x - 2.0f, current.y - 2.0f, 4.0f, 4.0f);
    }

    for (int c = 0; c < StepVstHostAudioProcessor::BeatSpaceChannels; ++c)
    {
        if (c == selectedChannel)
            continue;
        drawPoint(visualPointForChannel(c), channelColour(c).withAlpha(0.86f), 2.15f, false);
    }
    const auto selectedPoint = visualPointForChannel(selectedChannel);
    const auto selectedColour = channelColour(selectedChannel);
    drawPoint(selectedPoint, selectedColour, 3.1f, true);

    const int selectedRelX = selectedPoint.x - viewX;
    const int selectedRelY = selectedPoint.y - viewY;
    if (selectedRelX >= 0 && selectedRelX < viewW && selectedRelY >= 0 && selectedRelY < viewH)
    {
        const auto selectedPixel = pointToPixel(selectedPoint);
        const float sx = selectedPixel.x;
        const float sy = selectedPixel.y;
        g.setColour(selectedColour.withAlpha(0.34f));
        g.drawHorizontalLine(static_cast<int>(std::round(sy)), inner.getX(), inner.getRight());
        g.drawVerticalLine(static_cast<int>(std::round(sx)), inner.getY(), inner.getBottom());
    }

    if (showTextOverlay)
    {
        auto infoBadge = inner.reduced(3.0f).removeFromTop(14.0f).removeFromLeft(202.0f);
        g.setColour(juce::Colour(0xff0f2234).withAlpha(0.74f));
        g.fillRoundedRectangle(infoBadge, 2.2f);
        g.setColour(selectedColour.withAlpha(0.88f));
        g.drawRoundedRectangle(infoBadge, 2.2f, 0.9f);
        g.setColour(juce::Colour(0xffeef5ff).withAlpha(0.95f));
        g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
        g.drawFittedText(
            "CH " + juce::String(selectedChannel + 1)
                + " " + juce::String(kCategoryLongNames[static_cast<size_t>(selectedAssignment)])
                + "  X" + juce::String(selectedPoint.x)
                + " Y" + juce::String(selectedPoint.y)
                + "  Z" + juce::String(state.zoomLevel + 1)
                + "  C" + juce::String(static_cast<int>(std::round(state.selectedConfidence * 100.0f))) + "%"
                + (state.channelMorphActive[static_cast<size_t>(selectedChannel)]
                    ? "  M" + juce::String(static_cast<int>(std::round(
                        state.channelMorphProgress[static_cast<size_t>(selectedChannel)] * 100.0f))) + "%"
                    : ""),
            infoBadge.toNearestInt(),
            juce::Justification::centred,
            1);

        if (state.pathRecordArmedChannel >= 0)
        {
            auto pathBadge = inner.reduced(3.0f).removeFromTop(14.0f).removeFromRight(84.0f);
            g.setColour(juce::Colour(0xff13324f).withAlpha(0.80f));
            g.fillRoundedRectangle(pathBadge, 2.2f);
            g.setColour(kAccent.withAlpha(0.96f));
            g.drawRoundedRectangle(pathBadge, 2.2f, 1.0f);
            g.setColour(juce::Colour(0xffeaf4ff));
            g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
            g.drawFittedText(
                "PATH CH " + juce::String(state.pathRecordArmedChannel + 1),
                pathBadge.toNearestInt(),
                juce::Justification::centred,
                1);
        }
    }

    if (state.linkAllChannels)
    {
        const auto master = pointToPixel(visualPointForChannel(selectedChannel));
        for (int i = 0; i < StepVstHostAudioProcessor::BeatSpaceChannels; ++i)
        {
            if (i == selectedChannel)
                continue;
            const auto p = pointToPixel(visualPointForChannel(i));
            const auto c = channelColour(i).withAlpha(0.48f);
            g.setColour(c);
            g.drawLine(master.x, master.y, p.x, p.y, 1.4f);
        }

        if (showTextOverlay)
        {
            auto badgeArea = inner;
            auto badge = badgeArea.removeFromTop(14.0f).removeFromRight(58.0f);
            g.setColour(juce::Colour(0xff0d314f).withAlpha(0.80f));
            g.fillRoundedRectangle(badge, 2.5f);
            g.setColour(selectedColour.withAlpha(0.95f));
            g.drawRoundedRectangle(badge, 2.5f, 1.0f);
            g.setColour(juce::Colour(0xffeaf4ff));
            g.setFont(juce::Font(juce::FontOptions(8.6f, juce::Font::bold)));
            g.drawFittedText("LINK", badge.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    // Channel selector chips (clickable) + category initials.
    auto selectorArea = inner.reduced(4.0f);
    selectorArea.setY(selectorArea.getBottom() - 16.0f);
    selectorArea.setHeight(15.0f);
    g.setColour(juce::Colour(0xff0f2234).withAlpha(0.56f));
    g.fillRoundedRectangle(selectorArea, 2.5f);
    g.setColour(juce::Colour(0xffd8e5f5).withAlpha(0.74f));
    g.drawRoundedRectangle(selectorArea, 2.5f, 0.8f);

    const float chipW = selectorArea.getWidth() / static_cast<float>(StepVstHostAudioProcessor::BeatSpaceChannels);
    g.setFont(juce::Font(juce::FontOptions(8.4f, juce::Font::bold)));
    for (int i = 0; i < StepVstHostAudioProcessor::BeatSpaceChannels; ++i)
    {
        auto chip = selectorArea.withX(selectorArea.getX() + (chipW * static_cast<float>(i))).withWidth(chipW);
        const bool isSelected = (i == selectedChannel);
        const int assignment = juce::jlimit(
            0,
            StepVstHostAudioProcessor::BeatSpaceChannels - 1,
            state.channelCategoryAssignment[static_cast<size_t>(i)]);
        const auto c = channelColour(i);
        g.setColour(c.withAlpha(isSelected ? 0.42f : 0.23f));
        g.fillRoundedRectangle(chip.reduced(1.0f), 1.8f);
        g.setColour(c.withAlpha(isSelected ? 0.98f : 0.68f));
        g.drawRoundedRectangle(chip.reduced(1.0f), 1.8f, isSelected ? 1.2f : 0.8f);
        if (showTextOverlay)
        {
            g.setColour(juce::Colour(0xffeef5ff).withAlpha(0.93f));
            g.drawFittedText(
                juce::String(i + 1) + ":" + juce::String(kCategoryChipNames[static_cast<size_t>(assignment)]),
                chip.reduced(2.0f).toNearestInt(),
                juce::Justification::centred,
                1);
        }
        else
        {
            g.setColour(juce::Colours::white.withAlpha(0.88f));
            g.fillEllipse(chip.getCentreX() - 2.0f, chip.getCentreY() - 2.0f, 4.0f, 4.0f);
        }
    }

    // Active viewport is now the main preview; keep a subtle frame to make this explicit.
    g.setColour(juce::Colour(0xfff5a65b).withAlpha(0.9f));
    g.drawRect(inner, 1.0f);
}

void GlobalControlPanel::mouseDown(const juce::MouseEvent& e)
{
    beatSpaceDragChannel = -1;
    beatSpaceDragSingleChannel = false;

    if (beatSpacePreviewBounds.isEmpty())
        return;

    const auto state = processor.getBeatSpaceVisualState();
    if (!state.decoderReady)
        return;

    auto innerBounds = beatSpacePreviewBounds.reduced(3);
    if (!innerBounds.contains(e.getPosition()))
        return;

    auto pointFromLocal = [&](juce::Point<int> localPos) -> juce::Point<int>
    {
        const int tableSize = juce::jmax(1, state.tableSize);
        const int viewX = juce::jlimit(0, tableSize - 1, state.viewX);
        const int viewY = juce::jlimit(0, tableSize - 1, state.viewY);
        const int viewW = juce::jmax(1, juce::jmin(tableSize - viewX, state.viewWidth));
        const int viewH = juce::jmax(1, juce::jmin(tableSize - viewY, state.viewHeight));
        const int relX = juce::jlimit(0, innerBounds.getWidth() - 1, localPos.x - innerBounds.getX());
        const int relY = juce::jlimit(0, innerBounds.getHeight() - 1, localPos.y - innerBounds.getY());
        const float tx = (innerBounds.getWidth() > 1)
            ? static_cast<float>(relX) / static_cast<float>(innerBounds.getWidth() - 1)
            : 0.0f;
        const float ty = (innerBounds.getHeight() > 1)
            ? static_cast<float>(relY) / static_cast<float>(innerBounds.getHeight() - 1)
            : 0.0f;
        return {
            viewX + juce::jlimit(0, viewW - 1, static_cast<int>(std::round(tx * static_cast<float>(juce::jmax(0, viewW - 1))))),
            viewY + juce::jlimit(0, viewH - 1, static_cast<int>(std::round(ty * static_cast<float>(juce::jmax(0, viewH - 1)))))
        };
    };

    auto applyPointFromLocal = [&](juce::Point<int> localPos)
    {
        const int relX = juce::jlimit(0, innerBounds.getWidth() - 1, localPos.x - innerBounds.getX());
        const int relY = juce::jlimit(0, innerBounds.getHeight() - 1, localPos.y - innerBounds.getY());
        processor.beatSpaceSetPointFromGridCell(relX, relY, innerBounds.getWidth(), innerBounds.getHeight());
        refreshFromProcessor();
        repaint(beatSpacePreviewBounds.expanded(2));
    };

    auto pointToPreviewPixel = [&](const juce::Point<int>& p) -> juce::Point<float>
    {
        const int tableSize = juce::jmax(1, state.tableSize);
        const int viewX = juce::jlimit(0, tableSize - 1, state.viewX);
        const int viewY = juce::jlimit(0, tableSize - 1, state.viewY);
        const int viewW = juce::jmax(1, juce::jmin(tableSize - viewX, state.viewWidth));
        const int viewH = juce::jmax(1, juce::jmin(tableSize - viewY, state.viewHeight));
        const int relX = juce::jlimit(0, viewW - 1, p.x - viewX);
        const int relY = juce::jlimit(0, viewH - 1, p.y - viewY);
        const float scaleX = static_cast<float>(innerBounds.getWidth()) / static_cast<float>(viewW);
        const float scaleY = static_cast<float>(innerBounds.getHeight()) / static_cast<float>(viewH);
        return {
            static_cast<float>(innerBounds.getX()) + (static_cast<float>(relX) * scaleX),
            static_cast<float>(innerBounds.getY()) + (static_cast<float>(relY) * scaleY)
        };
    };

    auto selectorArea = [&]() -> juce::Rectangle<float>
    {
        auto area = innerBounds.toFloat().reduced(4.0f);
        area.setY(area.getBottom() - 16.0f);
        area.setHeight(15.0f);
        return area;
    };

    if (e.mods.isLeftButtonDown() && !e.mods.isPopupMenu())
    {
        const auto selector = selectorArea();
        if (selector.contains(e.position))
        {
            const float chipW = selector.getWidth() / static_cast<float>(StepVstHostAudioProcessor::BeatSpaceChannels);
            const int channel = juce::jlimit(
                0,
                StepVstHostAudioProcessor::BeatSpaceChannels - 1,
                static_cast<int>(std::floor((e.position.x - selector.getX()) / juce::jmax(1.0f, chipW))));
            processor.beatSpaceSelectChannel(channel);
            refreshFromProcessor();
            repaint(beatSpacePreviewBounds.expanded(2));
            return;
        }

        if (state.pathRecordArmedChannel >= 0)
        {
            const auto startPoint = pointFromLocal(e.getPosition());
            if (processor.beatSpacePathRecordStart(state.pathRecordArmedChannel, startPoint))
            {
                beatSpacePathRecordingActive = true;
                beatSpacePathRecordingChannel = state.pathRecordArmedChannel;
                refreshFromProcessor();
                repaint(beatSpacePreviewBounds.expanded(2));
                return;
            }
        }

        int nearestChannel = -1;
        float nearestDistanceSq = std::numeric_limits<float>::max();
        for (int c = 0; c < StepVstHostAudioProcessor::BeatSpaceChannels; ++c)
        {
            const auto idx = static_cast<size_t>(c);
            const auto point = state.channelMorphActive[idx]
                ? state.channelMorphCurrent[idx]
                : state.channelPoints[idx];
            const auto pixel = pointToPreviewPixel(point);
            const float dx = pixel.x - e.position.x;
            const float dy = pixel.y - e.position.y;
            const float d2 = (dx * dx) + (dy * dy);
            if (d2 < nearestDistanceSq)
            {
                nearestDistanceSq = d2;
                nearestChannel = c;
            }
        }
        if (nearestChannel >= 0 && nearestDistanceSq <= (9.0f * 9.0f))
        {
            beatSpaceDragChannel = nearestChannel;
            beatSpaceDragSingleChannel = state.linkAllChannels && (nearestChannel != state.selectedChannel);
            if (!beatSpaceDragSingleChannel)
                processor.beatSpaceSelectChannel(nearestChannel);

            const auto point = pointFromLocal(e.getPosition());
            processor.beatSpaceSetChannelPoint(
                beatSpaceDragChannel,
                point,
                !beatSpaceDragSingleChannel,
                true);
            refreshFromProcessor();
            repaint(beatSpacePreviewBounds.expanded(2));
            return;
        }

        applyPointFromLocal(e.getPosition());
        return;
    }

    if (!e.mods.isPopupMenu())
        return;

    const juce::Point<int> tablePoint = pointFromLocal(e.getPosition());

    juce::PopupMenu menu;
    menu.addSectionHeader("BeatSpace View");
    menu.addItem(9000, "Text Overlay", true, beatSpaceTextOverlayEnabled);
    menu.addItem(9001, "Confidence Overlay", true, processor.isBeatSpaceConfidenceOverlayEnabled());
    menu.addItem(9002, "Path Overlay", true, processor.isBeatSpacePathOverlayEnabled());
    menu.addSeparator();

    menu.addSectionHeader("Tag BeatSpace Region");
    constexpr int kTagBase = 1000;
    for (int category = 0; category < StepVstHostAudioProcessor::BeatSpaceChannels; ++category)
    {
        menu.addItem(
            kTagBase + category,
            "Add point to " + StepVstHostAudioProcessor::getBeatSpaceSpaceName(category),
            true,
            state.categoryManual[static_cast<size_t>(category)]);
    }
    menu.addSeparator();
    menu.addItem(2000, "Clear nearest tag");
    menu.addItem(2001, "Clear all tags");
    menu.addSeparator();
    menu.addItem(3000, "Zoom In");
    menu.addItem(3001, "Zoom Out");

    const int result = menu.showAt(this);
    if (result == 9000)
    {
        beatSpaceTextOverlayEnabled = !beatSpaceTextOverlayEnabled;
    }
    else if (result == 9001)
    {
        processor.setBeatSpaceConfidenceOverlayEnabled(!processor.isBeatSpaceConfidenceOverlayEnabled());
    }
    else if (result == 9002)
    {
        processor.setBeatSpacePathOverlayEnabled(!processor.isBeatSpacePathOverlayEnabled());
    }
    else if (result >= kTagBase && result < (kTagBase + StepVstHostAudioProcessor::BeatSpaceChannels))
    {
        processor.beatSpaceSetManualCategoryAnchor(result - kTagBase, tablePoint);
    }
    else if (result == 2000)
    {
        processor.beatSpaceClearNearestManualCategoryAnchor(tablePoint, 7);
    }
    else if (result == 2001)
    {
        processor.beatSpaceClearAllManualCategoryAnchors();
    }
    else if (result == 3000)
    {
        processor.beatSpaceAdjustZoom(+1);
    }
    else if (result == 3001)
    {
        processor.beatSpaceAdjustZoom(-1);
    }

    refreshFromProcessor();
    repaint(beatSpacePreviewBounds.expanded(2));
}

void GlobalControlPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (!e.mods.isLeftButtonDown())
    {
        beatSpaceDragChannel = -1;
        beatSpaceDragSingleChannel = false;
        return;
    }
    if (beatSpacePreviewBounds.isEmpty())
        return;

    const auto state = processor.getBeatSpaceVisualState();
    if (!state.decoderReady)
        return;

    auto innerBounds = beatSpacePreviewBounds.reduced(3);
    if (!innerBounds.contains(e.getPosition()))
        return;

    const auto pointFromLocal = [&](juce::Point<int> localPos) -> juce::Point<int>
    {
        const int tableSize = juce::jmax(1, state.tableSize);
        const int viewX = juce::jlimit(0, tableSize - 1, state.viewX);
        const int viewY = juce::jlimit(0, tableSize - 1, state.viewY);
        const int viewW = juce::jmax(1, juce::jmin(tableSize - viewX, state.viewWidth));
        const int viewH = juce::jmax(1, juce::jmin(tableSize - viewY, state.viewHeight));
        const int relX = juce::jlimit(0, innerBounds.getWidth() - 1, localPos.x - innerBounds.getX());
        const int relY = juce::jlimit(0, innerBounds.getHeight() - 1, localPos.y - innerBounds.getY());
        const float tx = (innerBounds.getWidth() > 1)
            ? static_cast<float>(relX) / static_cast<float>(innerBounds.getWidth() - 1)
            : 0.0f;
        const float ty = (innerBounds.getHeight() > 1)
            ? static_cast<float>(relY) / static_cast<float>(innerBounds.getHeight() - 1)
            : 0.0f;
        return {
            viewX + juce::jlimit(0, viewW - 1, static_cast<int>(std::round(tx * static_cast<float>(juce::jmax(0, viewW - 1))))),
            viewY + juce::jlimit(0, viewH - 1, static_cast<int>(std::round(ty * static_cast<float>(juce::jmax(0, viewH - 1)))))
        };
    };

    if (beatSpacePathRecordingActive && beatSpacePathRecordingChannel >= 0)
    {
        processor.beatSpacePathRecordAppendPoint(
            beatSpacePathRecordingChannel,
            pointFromLocal(e.getPosition()));
        refreshFromProcessor();
        repaint(beatSpacePreviewBounds.expanded(2));
        return;
    }

    if (beatSpaceDragChannel >= 0)
    {
        processor.beatSpaceSetChannelPoint(
            beatSpaceDragChannel,
            pointFromLocal(e.getPosition()),
            !beatSpaceDragSingleChannel,
            true);
    }
    else
    {
        const int relX = juce::jlimit(0, innerBounds.getWidth() - 1, e.x - innerBounds.getX());
        const int relY = juce::jlimit(0, innerBounds.getHeight() - 1, e.y - innerBounds.getY());
        processor.beatSpaceSetPointFromGridCell(relX, relY, innerBounds.getWidth(), innerBounds.getHeight());
    }
    refreshFromProcessor();
    repaint(beatSpacePreviewBounds.expanded(2));
}

void GlobalControlPanel::mouseUp(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        auto* source = (e.originalComponent != nullptr) ? e.originalComponent : e.eventComponent;
        for (int channel = 0; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
        {
            auto& button = beatSpacePathButtons[static_cast<size_t>(channel)];
            if (source == &button || (source != nullptr && button.isParentOf(source)))
            {
                processor.beatSpacePathClear(channel);
                if (processor.getBeatSpacePathRecordArmedChannel() == channel)
                    processor.setBeatSpacePathRecordArmedChannel(-1);
                if (beatSpacePathRecordingActive && beatSpacePathRecordingChannel == channel)
                {
                    beatSpacePathRecordingActive = false;
                    beatSpacePathRecordingChannel = -1;
                }

                refreshFromProcessor();
                if (!beatSpacePreviewBounds.isEmpty())
                    repaint(beatSpacePreviewBounds.expanded(2));
                return;
            }
        }
    }

    juce::ignoreUnused(e);
    beatSpaceDragChannel = -1;
    beatSpaceDragSingleChannel = false;

    if (!beatSpacePathRecordingActive || beatSpacePathRecordingChannel < 0)
        return;

    processor.beatSpacePathRecordFinishAndPlay(beatSpacePathRecordingChannel);
    beatSpacePathRecordingActive = false;
    beatSpacePathRecordingChannel = -1;
    refreshFromProcessor();
    if (!beatSpacePreviewBounds.isEmpty())
        repaint(beatSpacePreviewBounds.expanded(2));
}

void GlobalControlPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (beatSpacePreviewBounds.isEmpty())
    {
        beatSpaceWheelZoomAccumulator = 0.0f;
        return;
    }

    const auto innerBounds = beatSpacePreviewBounds.reduced(3);
    if (!innerBounds.contains(e.getPosition()))
    {
        beatSpaceWheelZoomAccumulator = 0.0f;
        return;
    }

    const float wheelDelta = (std::abs(wheel.deltaY) >= std::abs(wheel.deltaX))
        ? wheel.deltaY
        : wheel.deltaX;
    if (std::abs(wheelDelta) < 0.001f)
        return;

    constexpr float kWheelZoomThreshold = 0.22f;
    beatSpaceWheelZoomAccumulator += wheelDelta;

    int zoomDelta = 0;
    while (beatSpaceWheelZoomAccumulator >= kWheelZoomThreshold)
    {
        ++zoomDelta;
        beatSpaceWheelZoomAccumulator -= kWheelZoomThreshold;
    }
    while (beatSpaceWheelZoomAccumulator <= -kWheelZoomThreshold)
    {
        --zoomDelta;
        beatSpaceWheelZoomAccumulator += kWheelZoomThreshold;
    }
    if (zoomDelta == 0)
        return;

    processor.beatSpaceAdjustZoom(zoomDelta);
    refreshFromProcessor();
    repaint(beatSpacePreviewBounds.expanded(2));
}

void GlobalControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    auto titleRow = bounds.removeFromTop(20);
    versionLabel.setBounds(titleRow.removeFromRight(62));
    titleRow.removeFromRight(8);
    titleLabel.setBounds(titleRow);

    bounds.removeFromTop(4);
    auto hostRow = bounds.removeFromTop(24);
    hostedLoadButton.setBounds(hostRow.removeFromLeft(92));
    hostRow.removeFromLeft(6);
    hostedShowGuiButton.setBounds(hostRow.removeFromLeft(92));
    hostRow.removeFromLeft(6);
    hostedMakeDefaultButton.setBounds(hostRow.removeFromLeft(138));
    hostRow.removeFromLeft(8);
    tooltipsToggle.setBounds(hostRow.removeFromLeft(92));

    bounds.removeFromTop(3);
    hostedStatusLabel.setBounds(bounds.removeFromTop(14));
    bounds.removeFromTop(4);

    auto toggleRow = bounds.removeFromTop(24);
    limiterToggle.setBounds(toggleRow.removeFromLeft(84));
    toggleRow.removeFromLeft(6);
    soundTouchToggle.setBounds(toggleRow.removeFromLeft(108));
    bounds.removeFromTop(4);

    auto controlsArea = bounds;

    const int spacing = 6;
    beatSpacePreviewBounds = {};

    const int previewWidth = juce::jlimit(
        300,
        juce::jmax(340, controlsArea.getWidth() - 305),
        static_cast<int>(std::round(static_cast<double>(controlsArea.getWidth()) * 0.64)));
    auto previewArea = controlsArea.removeFromRight(previewWidth);
    if (!previewArea.isEmpty())
        controlsArea.removeFromRight(spacing);

    auto masterArea = controlsArea.removeFromLeft(56);
    masterVolumeLabel.setBounds(masterArea.removeFromTop(16));
    masterArea.removeFromTop(2);
    masterVolumeSlider.setBounds(masterArea);
    controlsArea.removeFromLeft(spacing);

    auto controlRows = controlsArea;
    auto selectRow = controlRows.removeFromTop(52);
    auto swingArea = selectRow.removeFromLeft(116);
    swingDivisionLabel.setBounds(swingArea.removeFromTop(16));
    swingArea.removeFromTop(2);
    swingDivisionBox.setBounds(swingArea.removeFromTop(30));
    selectRow.removeFromLeft(spacing);

    auto momentaryArea = selectRow.removeFromRight(118);
    if (!selectRow.isEmpty())
        selectRow.removeFromRight(spacing);

    auto outputRoutingArea = selectRow.removeFromLeft(156);
    outputRoutingLabel.setBounds(outputRoutingArea.removeFromTop(16));
    outputRoutingArea.removeFromTop(2);
    outputRoutingBox.setBounds(outputRoutingArea.removeFromTop(30));
    momentaryArea.removeFromTop(16);
    momentaryToggle.setBounds(momentaryArea.removeFromTop(26));

    controlRows.removeFromTop(2);
    auto presetLabelRow = controlRows.removeFromTop(16);
    auto stripPresetLabelArea = presetLabelRow.removeFromLeft(96);
    microtonicPresetStripLabel.setBounds(stripPresetLabelArea);
    presetLabelRow.removeFromLeft(4);
    microtonicPresetListLabel.setBounds(presetLabelRow);

    controlRows.removeFromTop(2);
    auto presetSelectRow = controlRows.removeFromTop(24);
    auto stripPresetSelectArea = presetSelectRow.removeFromLeft(96);
    microtonicPresetStripBox.setBounds(stripPresetSelectArea);
    presetSelectRow.removeFromLeft(4);
    microtonicPresetListBox.setBounds(presetSelectRow);

    controlRows.removeFromTop(4);
    auto presetButtonRow = controlRows.removeFromTop(24);
    auto deleteArea = presetButtonRow.removeFromRight(76);
    microtonicPresetDeleteButton.setBounds(deleteArea);
    presetButtonRow.removeFromRight(4);
    auto recallArea = presetButtonRow.removeFromRight(110);
    microtonicPresetRecallButton.setBounds(recallArea);
    presetButtonRow.removeFromRight(4);
    microtonicPresetStoreButton.setBounds(presetButtonRow);

    controlRows.removeFromTop(4);
    auto morphRow = controlRows.removeFromTop(24);
    beatSpaceMorphLabel.setBounds(morphRow.removeFromLeft(72));
    morphRow.removeFromLeft(4);
    beatSpaceMorphSlider.setBounds(morphRow.removeFromLeft(juce::jmax(120, morphRow.getWidth())));

    auto layoutPathButtonsInRow = [&](juce::Rectangle<int> row)
    {
        const int pathGap = 4;
        const int totalGap = pathGap * (StepVstHostAudioProcessor::BeatSpaceChannels - 1);
        const int buttonWidth = juce::jmax(
            28,
            (row.getWidth() - totalGap) / StepVstHostAudioProcessor::BeatSpaceChannels);
        for (int channel = 0; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
        {
            auto& button = beatSpacePathButtons[static_cast<size_t>(channel)];
            button.setBounds(row.removeFromLeft(buttonWidth));
            if (channel < (StepVstHostAudioProcessor::BeatSpaceChannels - 1))
                row.removeFromLeft(pathGap);
        }
    };

    if (previewArea.getWidth() >= 180 && previewArea.getHeight() >= 112)
    {
        auto pathRow = previewArea.removeFromTop(22);
        layoutPathButtonsInRow(pathRow);
        previewArea.removeFromTop(3);
        beatSpacePreviewLabel.setBounds(previewArea.removeFromTop(17));
        previewArea.removeFromTop(4);
        beatSpacePreviewBounds = previewArea.reduced(2);
    }
    else
    {
        beatSpacePreviewLabel.setBounds({});
        for (auto& button : beatSpacePathButtons)
            button.setBounds({});
    }

}

void GlobalControlPanel::refreshFromProcessor()
{
    swingDivisionBox.setSelectedId(processor.getSwingDivisionSelection() + 1, juce::dontSendNotification);
    momentaryToggle.setToggleState(processor.isControlPageMomentary(), juce::dontSendNotification);
    rebuildMicrotonicPresetList();
    const auto beatState = processor.getBeatSpaceVisualState();
    beatSpaceMorphSlider.setValue(beatState.morphDurationMs, juce::dontSendNotification);
    for (int channel = 0; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
    {
        const auto idx = static_cast<size_t>(channel);
        const bool pathActive = beatState.pathActive[idx];
        const bool armed = (beatState.pathRecordArmedChannel == channel);
        auto& button = beatSpacePathButtons[idx];
        button.setToggleState(armed, juce::dontSendNotification);
        button.setButtonText(
            "P" + juce::String(channel + 1)
            + (pathActive ? "*" : ""));
        styleUiButton(button, false);
        constexpr std::array<uint32_t, StepVstHostAudioProcessor::BeatSpaceChannels> kCategoryColors {
            0xffea5455u, 0xfff07f3au, 0xff6bcf80u, 0xff4db6b8u, 0xff5f8fd3u, 0xff9f7ccbu
        };
        const int assignment = juce::jlimit(
            0,
            StepVstHostAudioProcessor::BeatSpaceChannels - 1,
            beatState.channelCategoryAssignment[idx]);
        const juce::Colour base = juce::Colour(kCategoryColors[static_cast<size_t>(assignment)]);
        const float alpha = (pathActive || armed) ? 0.90f : 0.62f;
        button.setColour(juce::TextButton::buttonColourId, base.withAlpha(alpha));
        button.setColour(juce::TextButton::buttonOnColourId, base.brighter(0.22f).withAlpha(0.95f));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff8fbff));
        button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfff8fbff));
    }
    juce::String beatLabel = "BeatSpace";
    if (!beatState.decoderReady)
        beatLabel += " (decoder missing)";
    else if (beatState.mappingReady && beatState.microtonicExactMapping)
        beatLabel += " (exact " + juce::String(beatState.mappedChannels) + "/" + juce::String(StepVstHostAudioProcessor::BeatSpaceChannels) + ")";
    else if (beatState.mappingReady)
        beatLabel += " (mapped " + juce::String(beatState.mappedChannels) + "/" + juce::String(StepVstHostAudioProcessor::BeatSpaceChannels) + ")";
    else if (beatState.mappedChannels <= 0)
        beatLabel += " (mapping unavailable)";
    else
        beatLabel += " (" + juce::String(beatState.mappedChannels) + "/" + juce::String(StepVstHostAudioProcessor::BeatSpaceChannels) + ")";
    if (beatState.maxRecallError > 0.001f)
        beatLabel += " err " + juce::String(beatState.maxRecallError, 3);
    if (beatState.colorClustersReady)
        beatLabel += " clustered";
    int manualTagCount = 0;
    for (const auto isManual : beatState.categoryManual)
    {
        if (isManual)
            ++manualTagCount;
    }
    if (manualTagCount > 0)
        beatLabel += " tags " + juce::String(manualTagCount);
    beatSpacePreviewLabel.setText(beatLabel, juce::dontSendNotification);
    juce::String beatTooltip = beatState.statusMessage.isNotEmpty() ? beatState.statusMessage : beatLabel;
    beatTooltip += "\nClick 1..6 chips (or dots) to select a channel before dragging.";
    beatTooltip += "\nUse mouse wheel over XY to zoom in/out.";
    beatTooltip += "\nUse P1..P6 buttons, then click-drag on XY to record PPQ-synced BeatSpace paths.";
    beatTooltip += "\nRight-click on XY for text/confidence/path overlays and category tagging.";
    beatTooltip += "\nPreview follows current BeatSpace zoom and pan window.";
    beatSpacePreviewLabel.setTooltip(beatTooltip);

    auto loadedFile = processor.getLoadedHostedInstrumentFile();
    if (loadedFile != juce::File())
        hostedLastPluginFile = loadedFile;
    else
    {
        auto defaultFile = processor.getDefaultHostedInstrumentFile();
        if (defaultFile != juce::File())
            hostedLastPluginFile = defaultFile;
    }
    updateHostedPluginStatus();
    if (!beatSpacePreviewBounds.isEmpty())
        repaint(beatSpacePreviewBounds.expanded(2));
}

//==============================================================================
// MonomePagesPanel Implementation
//==============================================================================

MonomePagesPanel::MonomePagesPanel(StepVstHostAudioProcessor& p)
    : processor(p)
{
    for (int i = 0; i < StepVstHostAudioProcessor::NumControlRowPages; ++i)
    {
        auto& row = rows[static_cast<size_t>(i)];
        row.positionLabel.setJustificationType(juce::Justification::centred);
        row.positionLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        row.positionLabel.setColour(juce::Label::textColourId, kTextMuted);
        addAndMakeVisible(row.positionLabel);

        row.modeButton.setClickingTogglesState(false);
        row.modeButton.setTriggeredOnMouseDown(true);
        styleUiButton(row.modeButton);
        row.modeButton.setTooltip("Click to activate this page");
        row.modeButton.onStateChange = [this, i]()
        {
            if (!processor.isControlPageMomentary())
                return;
            const auto modeAtButton = processor.getControlModeForControlButton(i);
            const bool isDown = rows[static_cast<size_t>(i)].modeButton.isDown();
            processor.setControlModeFromGui(isDown ? modeAtButton : StepVstHostAudioProcessor::ControlMode::Normal,
                                            isDown);
            refreshFromProcessor();
        };
        row.modeButton.onClick = [this, i]()
        {
            if (processor.isControlPageMomentary())
                return; // handled by onStateChange while pressed
            const auto modeAtButton = processor.getControlModeForControlButton(i);
            const bool active = processor.isControlModeActive()
                                && processor.getCurrentControlMode() == modeAtButton;
            processor.setControlModeFromGui(active ? StepVstHostAudioProcessor::ControlMode::Normal
                                                   : modeAtButton,
                                            !active);
            refreshFromProcessor();
        };
        addAndMakeVisible(row.modeButton);

        row.upButton.setButtonText("^");
        row.upButton.setTooltip("Move page left");
        row.upButton.onClick = [this, i]()
        {
            processor.moveControlPage(i, i - 1);
            refreshFromProcessor();
        };
        addAndMakeVisible(row.upButton);
        styleUiButton(row.upButton);

        row.downButton.setButtonText("v");
        row.downButton.setTooltip("Move page right");
        row.downButton.onClick = [this, i]()
        {
            processor.moveControlPage(i, i + 1);
            refreshFromProcessor();
        };
        addAndMakeVisible(row.downButton);
        styleUiButton(row.downButton);
    }

    refreshFromProcessor();
    startTimer(200);
}

void MonomePagesPanel::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), kAccent, 8.0f);

    auto bounds = getLocalBounds().reduced(8);
    bounds.removeFromTop(4);

    auto pageOrderArea = bounds.removeFromTop(58);
    const int numSlots = StepVstHostAudioProcessor::NumControlRowPages;
    const int gapX = 4;
    const int slotWidth = juce::jmax(52, (pageOrderArea.getWidth() - ((numSlots - 1) * gapX)) / juce::jmax(1, numSlots));
    const int slotHeight = pageOrderArea.getHeight();

    g.setColour(juce::Colour(0xff2a2a2a).withAlpha(0.9f));
    for (int i = 0; i < numSlots; ++i)
    {
        const int x = pageOrderArea.getX() + i * (slotWidth + gapX);
        const int y = pageOrderArea.getY();
        g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(x),
                                                      static_cast<float>(y),
                                                      static_cast<float>(slotWidth),
                                                      static_cast<float>(slotHeight)),
                               5.0f);
    }

}

void MonomePagesPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    bounds.removeFromTop(4);

    auto pageOrderArea = bounds.removeFromTop(58);
    const int numSlots = StepVstHostAudioProcessor::NumControlRowPages;
    const int gapX = 4;
    const int slotWidth = juce::jmax(52, (pageOrderArea.getWidth() - ((numSlots - 1) * gapX)) / juce::jmax(1, numSlots));
    const int slotHeight = pageOrderArea.getHeight();

    for (int i = 0; i < StepVstHostAudioProcessor::NumControlRowPages; ++i)
    {
        auto& row = rows[static_cast<size_t>(i)];
        juce::Rectangle<int> slotBounds(pageOrderArea.getX() + i * (slotWidth + gapX),
                                        pageOrderArea.getY(),
                                        slotWidth, slotHeight);

        auto header = slotBounds.removeFromTop(11);
        row.positionLabel.setBounds(header.removeFromLeft(18));
        slotBounds.removeFromTop(1);

        auto arrows = slotBounds.removeFromRight(16);
        row.modeButton.setBounds(slotBounds.reduced(0, 2));

        const int arrowW = 13;
        const int arrowH = 9;
        row.upButton.setBounds(arrows.getCentreX() - (arrowW / 2), arrows.getY() + 1, arrowW, arrowH);
        row.downButton.setBounds(arrows.getCentreX() - (arrowW / 2), arrows.getBottom() - arrowH - 1, arrowW, arrowH);
    }

}

void MonomePagesPanel::timerCallback()
{
    refreshFromProcessor();
}

void MonomePagesPanel::refreshFromProcessor()
{
    const auto order = processor.getControlPageOrder();
    const auto activeMode = processor.getCurrentControlMode();

    for (int i = 0; i < StepVstHostAudioProcessor::NumControlRowPages; ++i)
    {
        auto& row = rows[static_cast<size_t>(i)];
        const auto modeAtButton = order[static_cast<size_t>(i)];
        const bool isActive = (activeMode == modeAtButton) && (activeMode != StepVstHostAudioProcessor::ControlMode::Normal);

        row.positionLabel.setText("#" + juce::String(i + 1), juce::dontSendNotification);
        row.modeButton.setButtonText(getMonomePageShortName(modeAtButton));
        row.modeButton.setTooltip(getMonomePageDisplayName(modeAtButton));
        row.positionLabel.setColour(juce::Label::textColourId, isActive ? kAccent.brighter(0.15f) : kTextSecondary);
        row.modeButton.setColour(juce::TextButton::buttonColourId,
                                 isActive ? kAccent.withAlpha(0.78f) : juce::Colour(0xffebf2fa));
        row.modeButton.setColour(juce::TextButton::textColourOffId,
                                 isActive ? juce::Colour(0xfff7fbff) : kTextPrimary);
        row.upButton.setEnabled(i > 0);
        row.downButton.setEnabled(i < (StepVstHostAudioProcessor::NumControlRowPages - 1));
        row.upButton.setColour(juce::TextButton::buttonColourId, isActive ? kAccent.withAlpha(0.6f) : juce::Colour(0xffd7e3ef));
        row.downButton.setColour(juce::TextButton::buttonColourId, isActive ? kAccent.withAlpha(0.6f) : juce::Colour(0xffd7e3ef));
    }

}

void MonomePagesPanel::updatePresetButtons()
{
    const int loadedPreset = processor.getLoadedPresetIndex();
    for (int i = 0; i < StepVstHostAudioProcessor::MaxPresetSlots; ++i)
    {
        const bool exists = processor.presetExists(i);
        auto& button = presetButtons[static_cast<size_t>(i)];
        if (i == loadedPreset && exists)
        {
            button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffb8d478));
            button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff111111));
        }
        else
        {
            button.setColour(juce::TextButton::buttonColourId,
                             exists ? kAccent.withMultipliedBrightness(0.9f) : juce::Colour(0xffedf3fa));
            button.setColour(juce::TextButton::textColourOffId,
                             exists ? juce::Colour(0xfff7fbff) : kTextMuted);
        }
    }
}

void MonomePagesPanel::layoutPresetButtons()
{
    const int gap = 4;
    const int buttonHeight = 16;
    const int minButtonWidth = 26;

    const int viewportWidth = juce::jmax(0, presetViewport.getWidth() - presetViewport.getScrollBarThickness());
    const int buttonWidth = juce::jmax(minButtonWidth,
                                       (viewportWidth - ((StepVstHostAudioProcessor::PresetColumns - 1) * gap))
                                       / StepVstHostAudioProcessor::PresetColumns);
    const int contentWidth = (StepVstHostAudioProcessor::PresetColumns * buttonWidth)
                             + ((StepVstHostAudioProcessor::PresetColumns - 1) * gap);
    const int contentHeight = (StepVstHostAudioProcessor::PresetRows * buttonHeight)
                              + ((StepVstHostAudioProcessor::PresetRows - 1) * gap);

    presetGridContent.setSize(contentWidth, contentHeight);

    for (int i = 0; i < StepVstHostAudioProcessor::MaxPresetSlots; ++i)
    {
        const int x = i % StepVstHostAudioProcessor::PresetColumns;
        const int y = i / StepVstHostAudioProcessor::PresetColumns;
        presetButtons[static_cast<size_t>(i)].setBounds(x * (buttonWidth + gap),
                                                        y * (buttonHeight + gap),
                                                        buttonWidth,
                                                        buttonHeight);
    }
}

void MonomePagesPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int deltaY = static_cast<int>(-wheel.deltaY * 96.0f);
    if (deltaY != 0)
        presetViewport.setViewPosition(presetViewport.getViewPositionX(),
                                       juce::jmax(0, presetViewport.getViewPositionY() + deltaY));
}

void MonomePagesPanel::onPresetButtonClicked(int presetIndex)
{
    if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
        processor.savePreset(presetIndex);
    else
        processor.loadPreset(presetIndex);

    updatePresetButtons();
}

//==============================================================================
// ModulationControlPanel Implementation
//==============================================================================

ModulationControlPanel::ModulationControlPanel(StepVstHostAudioProcessor& p)
    : processor(p)
{
    titleLabel.setText("Per-Row Modulation Sequencer", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, kTextPrimary);
    titleLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);

    stripLabel.setColour(juce::Label::textColourId, kAccent);
    stripLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    addAndMakeVisible(stripLabel);

    targetLabel.setText("Target", juce::dontSendNotification);
    targetLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(targetLabel);

    targetBox.addItem("None", 1);
    targetBox.addItem("Volume", 2);
    targetBox.addItem("Pan", 3);
    targetBox.addItem("Pitch", 4);
    targetBox.addItem("Speed", 5);
    targetBox.addItem("Cutoff", 6);
    targetBox.addItem("Resonance", 7);
    targetBox.addItem("Grain Size", 8);
    targetBox.addItem("Grain Density", 9);
    targetBox.addItem("Grain Pitch", 10);
    targetBox.addItem("Grain Pitch Jitter", 11);
    targetBox.addItem("Grain Spread", 12);
    targetBox.addItem("Grain Jitter", 13);
    targetBox.addItem("Grain Random", 14);
    targetBox.addItem("Grain Arp", 15);
    targetBox.addItem("Grain Cloud", 16);
    targetBox.addItem("Grain Emitter", 17);
    targetBox.addItem("Grain Envelope", 18);
    targetBox.addItem("Retrigger", 19);
    targetBox.onChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
        {
            engine->setModTarget(selectedStrip, comboIdToModTarget(targetBox.getSelectedId()));
            bipolarToggle.setToggleState(engine->isModBipolar(selectedStrip), juce::dontSendNotification);
        }
    };
    addAndMakeVisible(targetBox);

    bipolarToggle.setButtonText("Bipolar");
    bipolarToggle.onClick = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModBipolar(selectedStrip, bipolarToggle.getToggleState());
    };
    addAndMakeVisible(bipolarToggle);

    depthLabel.setText("Depth", juce::dontSendNotification);
    depthLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(depthLabel);

    depthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    depthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 16);
    depthSlider.setRange(0.0, 1.0, 0.01);
    depthSlider.onValueChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModDepth(selectedStrip, static_cast<float>(depthSlider.getValue()));
    };
    addAndMakeVisible(depthSlider);

    offsetLabel.setVisible(false);
    offsetSlider.setVisible(false);

    lengthLabel.setText("Length", juce::dontSendNotification);
    lengthLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(lengthLabel);

    lengthBox.addItem("1", 1);
    lengthBox.addItem("2", 2);
    lengthBox.addItem("4", 4);
    lengthBox.addItem("8", 8);
    lengthBox.onChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
        {
            const int bars = lengthBox.getSelectedId();
            const int currentPage = engine->getModEditPage(selectedStrip);
            engine->setModLengthBars(selectedStrip, bars);
            engine->setModEditPage(selectedStrip, juce::jlimit(0, juce::jmax(0, bars - 1), currentPage));
        }
    };
    addAndMakeVisible(lengthBox);

    pageLabel.setText("Page", juce::dontSendNotification);
    pageLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(pageLabel);

    pageBox.addItem("1", 1);
    pageBox.addItem("2", 2);
    pageBox.addItem("3", 3);
    pageBox.addItem("4", 4);
    pageBox.addItem("5", 5);
    pageBox.addItem("6", 6);
    pageBox.addItem("7", 7);
    pageBox.addItem("8", 8);
    pageBox.onChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModEditPage(selectedStrip, juce::jlimit(0, 7, pageBox.getSelectedId() - 1));
    };
    addAndMakeVisible(pageBox);

    smoothLabel.setText("Smooth", juce::dontSendNotification);
    smoothLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(smoothLabel);

    smoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    smoothSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 16);
    smoothSlider.setRange(0.0, 250.0, 1.0);
    smoothSlider.setSkewFactorFromMidPoint(40.0);
    smoothSlider.onValueChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModSmoothingMs(selectedStrip, static_cast<float>(smoothSlider.getValue()));
    };
    addAndMakeVisible(smoothSlider);

    pitchScaleToggle.setButtonText("Pitch Quantize");
    pitchScaleToggle.onClick = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModPitchScaleQuantize(selectedStrip, pitchScaleToggle.getToggleState());
    };
    addAndMakeVisible(pitchScaleToggle);

    pitchScaleLabel.setText("Scale", juce::dontSendNotification);
    pitchScaleLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(pitchScaleLabel);

    pitchScaleBox.addItem("Chromatic", 1);
    pitchScaleBox.addItem("Major", 2);
    pitchScaleBox.addItem("Minor", 3);
    pitchScaleBox.addItem("Dorian", 4);
    pitchScaleBox.addItem("Pentatonic", 5);
    pitchScaleBox.onChange = [this]()
    {
        if (auto* engine = processor.getAudioEngine())
            engine->setModPitchScale(selectedStrip, comboIdToPitchScale(pitchScaleBox.getSelectedId()));
    };
    addAndMakeVisible(pitchScaleBox);

    gestureHintLabel.setText("Cell mods (same as Step): Cmd=Divide  Ctrl=Ramp+  Opt=Ramp-", juce::dontSendNotification);
    gestureHintLabel.setColour(juce::Label::textColourId, kTextMuted);
    gestureHintLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    gestureHintLabel.setJustificationType(juce::Justification::centredLeft);
    gestureHintLabel.setTooltip("Modifier drags on mod cells mirror step-sequencer cells.");
    addAndMakeVisible(gestureHintLabel);

    for (int i = 0; i < ModernAudioEngine::ModSteps; ++i)
    {
        auto& b = stepButtons[static_cast<size_t>(i)];
        b.setButtonText(juce::String(i + 1));
        b.setTooltip("Click: toggle step. Cmd+drag: divide. Ctrl+drag: ramp up. Opt+drag: ramp down.");
        b.onClick = [this, i]()
        {
            if (suppressNextStepClick)
            {
                suppressNextStepClick = false;
                return;
            }
            if (getStepCellModifierGesture(juce::ModifierKeys::getCurrentModifiersRealtime()) != StepCellModifierGesture::None)
                return;
            if (auto* engine = processor.getAudioEngine())
                engine->toggleModStep(selectedStrip, i);
            refreshFromEngine();
        };
        b.addMouseListener(this, true);
        addAndMakeVisible(b);
    }

    startTimer(80);
    refreshFromEngine();
}

void ModulationControlPanel::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), kAccent, 8.0f);
}

void ModulationControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    titleLabel.setBounds(bounds.removeFromTop(22));
    stripLabel.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(4);

    auto top = bounds.removeFromTop(22);
    targetLabel.setBounds(top.removeFromLeft(40));
    targetBox.setBounds(top.removeFromLeft(98));
    top.removeFromLeft(4);
    lengthLabel.setBounds(top.removeFromLeft(38));
    lengthBox.setBounds(top.removeFromLeft(56));
    top.removeFromLeft(4);
    pageLabel.setBounds(top.removeFromLeft(28));
    pageBox.setBounds(top.removeFromLeft(46));

    bounds.removeFromTop(3);
    auto depthRow = bounds.removeFromTop(22);
    depthLabel.setBounds(depthRow.removeFromLeft(44));
    depthSlider.setBounds(depthRow.removeFromLeft(120));
    depthRow.removeFromLeft(4);
    bipolarToggle.setBounds(depthRow.removeFromLeft(70));
    depthRow.removeFromLeft(4);
    pitchScaleToggle.setBounds(depthRow);

    bounds.removeFromTop(3);
    auto smoothRow = bounds.removeFromTop(22);
    smoothLabel.setBounds(smoothRow.removeFromLeft(44));
    smoothSlider.setBounds(smoothRow.removeFromLeft(120));

    bounds.removeFromTop(3);
    auto scaleRow = bounds.removeFromTop(22);
    pitchScaleLabel.setBounds(scaleRow.removeFromLeft(44));
    pitchScaleBox.setBounds(scaleRow.removeFromLeft(112));
    scaleRow.removeFromLeft(4);

    bounds.removeFromTop(4);
    gestureHintLabel.setBounds(bounds.removeFromTop(16));
    bounds.removeFromTop(6);
    const int gap = 4;
    const int w = juce::jmax(20, (bounds.getWidth() - (gap * (ModernAudioEngine::ModSteps - 1))) / ModernAudioEngine::ModSteps);
    const int h = juce::jmax(24, bounds.getHeight());
    for (int i = 0; i < ModernAudioEngine::ModSteps; ++i)
        stepButtons[static_cast<size_t>(i)].setBounds(bounds.getX() + i * (w + gap), bounds.getY(), w, h);
}

void ModulationControlPanel::timerCallback()
{
    refreshFromEngine();
}

void ModulationControlPanel::mouseDown(const juce::MouseEvent& e)
{
    auto* engine = processor.getAudioEngine();
    if (!engine)
        return;

    const int step = stepIndexForComponent(e.eventComponent);
    if (step < 0)
        return;

    const auto modifierGesture = getStepCellModifierGesture(e.mods);
    if (modifierGesture != StepCellModifierGesture::None)
    {
        const auto state = engine->getModSequencerState(selectedStrip);
        gestureSourceValue = juce::jlimit(0.0f, 1.0f, state.steps[static_cast<size_t>(step)]);
        gestureSourceSubdivision = juce::jlimit(
            1,
            ModernAudioEngine::ModMaxStepSubdivisions,
            state.stepSubdivisions[static_cast<size_t>(step)]);
        gestureSourceEndValue = juce::jlimit(0.0f, 1.0f, state.stepEndValues[static_cast<size_t>(step)]);
        switch (modifierGesture)
        {
            case StepCellModifierGesture::Divide:
                gestureMode = EditGestureMode::DuplicateCell;
                break;
            case StepCellModifierGesture::RampUp:
                gestureMode = EditGestureMode::ShapeUpCell;
                break;
            case StepCellModifierGesture::RampDown:
                gestureMode = EditGestureMode::ShapeDownCell;
                break;
            case StepCellModifierGesture::None:
            default:
                gestureMode = EditGestureMode::None;
                break;
        }
        gestureActive = true;
        gestureStartY = e.getScreenPosition().y;
        gestureStep = step;
        suppressNextStepClick = true;

        if (gestureMode == EditGestureMode::ShapeUpCell)
            applyShapeGesture(0, true);
        else if (gestureMode == EditGestureMode::ShapeDownCell)
            applyShapeGesture(0, false);

        refreshFromEngine();
    }
}

void ModulationControlPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (!gestureActive || !processor.getAudioEngine())
        return;

    const int deltaY = e.getScreenPosition().y - gestureStartY;
    if (gestureMode == EditGestureMode::DuplicateCell)
        applyDuplicateGesture(deltaY);
    else if (gestureMode == EditGestureMode::ShapeUpCell)
        applyShapeGesture(deltaY, true);
    else if (gestureMode == EditGestureMode::ShapeDownCell)
        applyShapeGesture(deltaY, false);

    refreshFromEngine();
}

void ModulationControlPanel::mouseUp(const juce::MouseEvent&)
{
    gestureActive = false;
    gestureMode = EditGestureMode::None;
    gestureStep = -1;
}

int ModulationControlPanel::stepIndexForComponent(juce::Component* c) const
{
    for (int i = 0; i < ModernAudioEngine::ModSteps; ++i)
    {
        if (c == &stepButtons[static_cast<size_t>(i)])
            return i;
    }
    return -1;
}

void ModulationControlPanel::applyDuplicateGesture(int deltaY)
{
    auto* engine = processor.getAudioEngine();
    if (!engine || gestureStep < 0 || gestureStep >= ModernAudioEngine::ModSteps)
        return;

    const int nextSubdivision = juce::jlimit(
        1,
        ModernAudioEngine::ModMaxStepSubdivisions,
        gestureSourceSubdivision + ((-deltaY) / 14));
    const float endValue = (nextSubdivision > 1) ? gestureSourceEndValue : gestureSourceValue;
    engine->setModStepShape(selectedStrip, gestureStep, nextSubdivision, endValue);
}

void ModulationControlPanel::applyShapeGesture(int deltaY, bool rampUpMode)
{
    auto* engine = processor.getAudioEngine();
    if (!engine || gestureStep < 0 || gestureStep >= ModernAudioEngine::ModSteps)
        return;

    int subdivisions = gestureSourceSubdivision;
    if (subdivisions <= 1)
    {
        subdivisions = juce::jlimit(
            2,
            ModernAudioEngine::ModMaxStepSubdivisions,
            2 + (std::abs(deltaY) / 14));
    }

    float startValue = gestureSourceValue;
    float endValue = gestureSourceEndValue;
    computeSingleModCellRamp(gestureSourceValue, gestureSourceEndValue, deltaY, rampUpMode, startValue, endValue);
    engine->setModStepValue(selectedStrip, gestureStep, startValue);
    engine->setModStepShape(selectedStrip, gestureStep, subdivisions, endValue);
}

void ModulationControlPanel::refreshFromEngine()
{
    auto* engine = processor.getAudioEngine();
    if (!engine)
        return;

    selectedStrip = juce::jlimit(0, StepVstHostAudioProcessor::MaxStrips - 1, processor.getLastMonomePressedStripRow());
    stripLabel.setText("Selected Row: " + juce::String(selectedStrip + 1) + " (last pressed)", juce::dontSendNotification);

    const auto state = engine->getModSequencerState(selectedStrip);
    targetBox.setSelectedId(modTargetToComboId(state.target), juce::dontSendNotification);
    bipolarToggle.setToggleState(state.bipolar, juce::dontSendNotification);
    bipolarToggle.setEnabled(modTargetAllowsBipolar(state.target));
    depthSlider.setValue(state.depth, juce::dontSendNotification);
    lengthBox.setSelectedId(state.lengthBars, juce::dontSendNotification);
    pageBox.setSelectedId(juce::jlimit(1, 8, state.editPage + 1), juce::dontSendNotification);
    pageBox.setEnabled(state.lengthBars > 1);
    smoothSlider.setValue(state.smoothingMs, juce::dontSendNotification);
    pitchScaleToggle.setToggleState(state.pitchScaleQuantize, juce::dontSendNotification);
    pitchScaleBox.setSelectedId(pitchScaleToComboId(static_cast<ModernAudioEngine::PitchScale>(state.pitchScale)), juce::dontSendNotification);
    pitchScaleLabel.setEnabled(state.pitchScaleQuantize);
    pitchScaleBox.setEnabled(state.pitchScaleQuantize);

    const int activeGlobalStep = engine->getModCurrentGlobalStep(selectedStrip);
    const int playbackPage = juce::jlimit(
        0,
        ModernAudioEngine::MaxModBars - 1,
        activeGlobalStep / ModernAudioEngine::ModSteps);
    const int activeStep = (playbackPage == state.editPage)
        ? (activeGlobalStep % ModernAudioEngine::ModSteps)
        : -1;
    for (int i = 0; i < ModernAudioEngine::ModSteps; ++i)
    {
        auto& b = stepButtons[static_cast<size_t>(i)];
        const float value = juce::jlimit(0.0f, 1.0f, state.steps[static_cast<size_t>(i)]);
        const int subdivisions = juce::jlimit(
            1,
            ModernAudioEngine::ModMaxStepSubdivisions,
            state.stepSubdivisions[static_cast<size_t>(i)]);
        const float endValue = juce::jlimit(0.0f, 1.0f, state.stepEndValues[static_cast<size_t>(i)]);
        const auto offColour = juce::Colour(0xffd9e4f0);
        const auto onColour = kAccent.withMultipliedBrightness(0.9f);
        juce::Colour c = offColour.interpolatedWith(onColour, value);
        if (subdivisions > 1)
            c = c.interpolatedWith(juce::Colour(0xfff0f6ff), 0.16f);
        if (i == activeStep)
            c = c.interpolatedWith(juce::Colour(0xffffcf75), 0.55f);
        b.setColour(juce::TextButton::buttonColourId, c);
        b.setTooltip("Step " + juce::String(i + 1)
                     + ": " + juce::String(static_cast<int>(std::round(value * 100.0f))) + "%\n"
                     + "Shape: x" + juce::String(subdivisions)
                     + "  end " + juce::String(static_cast<int>(std::round(endValue * 100.0f))) + "%\n"
                     + "Click: toggle step. Cmd+drag: divide. Ctrl+drag: ramp up. Opt+drag: ramp down.");
    }
}


//==============================================================================
// StepVstHostAudioProcessorEditor Implementation
//==============================================================================

StepVstHostAudioProcessorEditor::StepVstHostAudioProcessorEditor(StepVstHostAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setupLookAndFeel();
    setTooltipsEnabled(false);
    activeGuiStripCount = getDetectedGuiStripCount();
    
    // Enable keyboard input for spacebar transport control
    setWantsKeyboardFocus(true);
    
    // Set window size FIRST. Clamp initial height to the current display so hosts
    // that do not expose plugin resizing (e.g. Logic for some formats) still show
    // the full UI without clipping the bottom.
    int initialHeight = windowHeight;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const int safeVisibleHeight = juce::jmax(620, display->userArea.getHeight() - 80);
        initialHeight = juce::jmin(windowHeight, safeVisibleHeight);
    }
    setSize(windowWidth, initialHeight);
    setResizable(true, true);
    setResizeLimits(1000, 620, 1920, 1400);
    
    // Create all UI components
    createUIComponents();
    setActiveGuiStripCount(activeGuiStripCount, true);
    
    // Force initial layout
    resized();
    
    // Start UI update timer
    startTimer(50);
    lastPresetRefreshToken = audioProcessor.getPresetRefreshToken();
}

int StepVstHostAudioProcessorEditor::getDetectedGuiStripCount() const
{
    const int reportedStripCount = audioProcessor.getMonomeActiveStripCount();
    if (reportedStripCount <= 0)
        return 6;

    return juce::jlimit(1, StepVstHostAudioProcessor::MaxStrips, reportedStripCount);
}

void StepVstHostAudioProcessorEditor::setActiveGuiStripCount(int stripCount, bool forceRelayout)
{
    const int clampedStripCount = juce::jlimit(1, StepVstHostAudioProcessor::MaxStrips, stripCount);
    if (!forceRelayout && clampedStripCount == activeGuiStripCount)
        return;

    activeGuiStripCount = clampedStripCount;

    for (int i = 0; i < stripControls.size(); ++i)
        if (auto* strip = stripControls[i])
            strip->setVisible(i < activeGuiStripCount);

    for (int i = 0; i < fxStripControls.size(); ++i)
        if (auto* fxStrip = fxStripControls[i])
            fxStrip->setVisible(i < activeGuiStripCount);

    if (mainTabs)
    {
        if (auto* playPanel = mainTabs->getTabContentComponent(0))
            playPanel->resized();
        if (auto* fxPanel = mainTabs->getTabContentComponent(1))
            fxPanel->resized();
    }

    repaint();
}

void StepVstHostAudioProcessorEditor::createUIComponents()
{
    constexpr int kTotalSampleStrips = StepVstHostAudioProcessor::MaxStrips;
    // Monome grid hidden to save space - use physical monome instead
    monomeGrid = std::make_unique<MonomeGridDisplay>(audioProcessor);
    // Don't add to view - saves space
    
    // Create control panels
    monomeControl = std::make_unique<MonomeControlPanel>(audioProcessor);
    globalControl = std::make_unique<GlobalControlPanel>(audioProcessor);
    globalControl->onTooltipsToggled = [this](bool enabled)
    {
        setTooltipsEnabled(enabled);
    };
    monomePagesControl = std::make_unique<MonomePagesPanel>(audioProcessor);
    presetControl = std::make_unique<PresetControlPanel>(audioProcessor);
    
    // Create TABBED top controls to save space
    topTabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    topTabs->addTab("Global Controls", juce::Colour(0xffedf3fa), globalControl.get(), false);
    topTabs->addTab("Presets", juce::Colour(0xffedf3fa), presetControl.get(), false);
    topTabs->addTab("Monome Device", juce::Colour(0xffedf3fa), monomeControl.get(), false);
    topTabs->setTabBarDepth(28);
    topTabs->setCurrentTabIndex(0);  // Global Controls visible by default
    addAndMakeVisible(*topTabs);
    addAndMakeVisible(*monomePagesControl);
    
    // Helper panel classes for main tabs
    struct PlayPanel : public juce::Component
    {
        juce::OwnedArray<StripControl>& strips;
        int& visibleStripCount;
        
        PlayPanel(juce::OwnedArray<StripControl>& s, int& visibleCount)
            : strips(s), visibleStripCount(visibleCount) {}
        
        void resized() override
        {
            auto bounds = getLocalBounds();
            const int gap = 1;
            const int totalStrips = strips.size();
            if (totalStrips <= 0)
                return;

            const int stripCount = juce::jlimit(1, totalStrips, visibleStripCount);
            const int totalGap = gap * juce::jmax(0, stripCount - 1);
            const int stripHeight = juce::jmax(1, (bounds.getHeight() - totalGap) / stripCount);
            
            for (int i = 0; i < stripCount; ++i)
            {
                if (auto* strip = strips[i])
                {
                    const int y = i * (stripHeight + gap);
                    strip->setBounds(0, y, bounds.getWidth(), stripHeight);
                }
            }
        }
    };
    
    struct FXPanel : public juce::Component
    {
        juce::OwnedArray<FXStripControl>& strips;
        int& visibleStripCount;
        
        FXPanel(juce::OwnedArray<FXStripControl>& s, int& visibleCount)
            : strips(s), visibleStripCount(visibleCount) {}
        
        void resized() override
        {
            auto bounds = getLocalBounds();
            const int gap = 1;
            const int totalStrips = strips.size();
            if (totalStrips <= 0)
                return;

            const int stripCount = juce::jlimit(1, totalStrips, visibleStripCount);
            const int totalGap = gap * juce::jmax(0, stripCount - 1);
            const int stripHeight = juce::jmax(1, (bounds.getHeight() - totalGap) / stripCount);
            
            for (int i = 0; i < stripCount; ++i)
            {
                if (auto* strip = strips[i])
                {
                    const int y = i * (stripHeight + gap);
                    strip->setBounds(0, y, bounds.getWidth(), stripHeight);
                }
            }
        }
    };
    
    // Create MAIN UNIFIED TABS: Play / FX
    mainTabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    
    // PLAY TAB - regular strip controls
    auto* playPanel = new PlayPanel(stripControls, activeGuiStripCount);
    for (int i = 0; i < kTotalSampleStrips; ++i)
    {
        auto* strip = new StripControl(i, audioProcessor);
        stripControls.add(strip);
        playPanel->addAndMakeVisible(strip);
    }
    
    // FX TAB - filter controls for each strip
    auto* fxPanel = new FXPanel(fxStripControls, activeGuiStripCount);
    for (int i = 0; i < kTotalSampleStrips; ++i)
    {
        auto* fxStrip = new FXStripControl(i, audioProcessor);
        fxStripControls.add(fxStrip);
        fxPanel->addAndMakeVisible(fxStrip);
    }
    
    // Add main tabs to container
    mainTabs->addTab("Play", juce::Colour(0xffedf3fa), playPanel, true);
    mainTabs->addTab("FX", juce::Colour(0xffedf3fa), fxPanel, true);
    mainTabs->setTabBarDepth(28);
    mainTabs->setCurrentTabIndex(0);  // Start on Play tab
    addAndMakeVisible(*mainTabs);
}

StepVstHostAudioProcessorEditor::~StepVstHostAudioProcessorEditor()
{
    stopTimer();
}

void StepVstHostAudioProcessorEditor::setupLookAndFeel()
{
    darkLookAndFeel.setDefaultSansSerifTypefaceName("Helvetica Neue");

    darkLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, kBgBottom);

    darkLookAndFeel.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe8f0f8));
    darkLookAndFeel.setColour(juce::TextButton::buttonOnColourId, kAccent);
    darkLookAndFeel.setColour(juce::TextButton::textColourOffId, kTextPrimary);
    darkLookAndFeel.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfff7fbff));

    darkLookAndFeel.setColour(juce::Slider::thumbColourId, kAccent);
    darkLookAndFeel.setColour(juce::Slider::trackColourId, juce::Colour(0xff86a7cc));
    darkLookAndFeel.setColour(juce::Slider::backgroundColourId, juce::Colour(0xffe5ecf5));
    darkLookAndFeel.setColour(juce::Slider::rotarySliderFillColourId, kAccent.withAlpha(0.9f));
    darkLookAndFeel.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff9fb0c4));

    darkLookAndFeel.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xfff3f7fc));
    darkLookAndFeel.setColour(juce::ComboBox::textColourId, kTextPrimary);
    darkLookAndFeel.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xffb7c5d5));
    darkLookAndFeel.setColour(juce::ComboBox::arrowColourId, kTextSecondary);
    darkLookAndFeel.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xfff7faff));
    darkLookAndFeel.setColour(juce::PopupMenu::textColourId, kTextPrimary);
    darkLookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.25f));
    darkLookAndFeel.setColour(juce::PopupMenu::highlightedTextColourId, kTextPrimary);

    darkLookAndFeel.setColour(juce::Label::textColourId, kTextPrimary);

    darkLookAndFeel.setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(0xffedf3fa));
    darkLookAndFeel.setColour(juce::TabbedComponent::outlineColourId, juce::Colour(0xffb5c3d2));
    darkLookAndFeel.setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colour(0xffb5c3d2));
    darkLookAndFeel.setColour(juce::TabbedButtonBar::tabTextColourId, kTextSecondary);
    darkLookAndFeel.setColour(juce::TabbedButtonBar::frontTextColourId, kTextPrimary);
    
    setLookAndFeel(&darkLookAndFeel);
}

void StepVstHostAudioProcessorEditor::setTooltipsEnabled(bool enabled)
{
    tooltipsEnabled = enabled;
    if (tooltipsEnabled)
    {
        if (!tooltipWindow)
            tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 350);
    }
    else
    {
        tooltipWindow.reset();
    }
}

void StepVstHostAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    juce::ColourGradient bg(kBgTop, 0.0f, 0.0f, kBgBottom, 0.0f, area.getBottom(), false);
    g.setGradientFill(bg);
    g.fillAll();

    auto titleBar = getLocalBounds().removeFromTop(40).toFloat();
    juce::ColourGradient titleFill(juce::Colour(0xffedf3fa), 0.0f, titleBar.getY(),
                                   juce::Colour(0xffdde8f4), 0.0f, titleBar.getBottom(), false);
    g.setGradientFill(titleFill);
    g.fillRect(titleBar);
    g.setColour(juce::Colour(0xffb7c4d2));
    g.drawLine(titleBar.getX(), titleBar.getBottom(), titleBar.getRight(), titleBar.getBottom(), 1.0f);

    g.setColour(kTextPrimary);
    g.setFont(juce::Font(juce::FontOptions(23.0f, juce::Font::bold)));
    g.drawText(JucePlugin_Name, 16, 7, 260, 30, juce::Justification::centredLeft);

    g.setColour(kTextSecondary.brighter(0.1f));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText("Performance Slicer", 152, 10, 170, 20, juce::Justification::centredLeft);

    g.setColour(kTextMuted);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    const juce::String buildInfo = "v" + juce::String(JucePlugin_VersionString)
        + " | build " + juce::String(__DATE__) + " " + juce::String(__TIME__);
    g.drawText(buildInfo, getWidth() - 440, 11, 424, 18, juce::Justification::centredRight);
}

bool StepVstHostAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    (void) key;
    // Spacebar does nothing in plugin mode - DAW controls transport
    return false;  // Let other keys pass through
}

void StepVstHostAudioProcessorEditor::resized()
{
    // Safety check
    if (!topTabs || !mainTabs)
        return;
    
    auto bounds = getLocalBounds();
    
    // Title area
    bounds.removeFromTop(40);
    
    auto margin = 6;
    bounds.reduce(margin, margin);
    
    // Top section: TABBED controls (Global/Presets/Monome).
    // Keep enough vertical space so all Global Controls rows stay visible.
    const int topBarHeight = juce::jlimit(
        220,
        310,
        static_cast<int>(std::round(static_cast<double>(bounds.getHeight()) * 0.56)));
    auto topBar = bounds.removeFromTop(topBarHeight);
    topTabs->setBounds(topBar);
    
    bounds.removeFromTop(margin);
    
    // MAIN AREA: Unified tabs (Play/FX)
    auto monomePagesArea = bounds.removeFromBottom(50);
    monomePagesControl->setBounds(monomePagesArea);
    bounds.removeFromBottom(margin);
    mainTabs->setBounds(bounds);
}

//==============================================================================

void StepVstHostAudioProcessorEditor::timerCallback()
{
    if (!audioProcessor.getAudioEngine())
        return;
    
    if (globalControl)
    {
        globalControl->refreshFromProcessor();
    }

    if (presetControl)
        presetControl->refreshVisualState();

    const bool modulationActive = audioProcessor.isControlModeActive()
        && audioProcessor.getCurrentControlMode() == StepVstHostAudioProcessor::ControlMode::Modulation;
    setActiveGuiStripCount(getDetectedGuiStripCount(), false);
    for (int i = 0; i < stripControls.size(); ++i)
    {
        if (auto* strip = stripControls[i])
        {
            const bool isVisibleStrip = i < activeGuiStripCount;
            const bool showLane = modulationActive && isVisibleStrip;
            strip->setModulationLaneView(showLane);
            strip->setVisible(isVisibleStrip);
        }
    }

    const uint32_t refreshToken = audioProcessor.getPresetRefreshToken();
    if (refreshToken != lastPresetRefreshToken)
    {
        lastPresetRefreshToken = refreshToken;
        for (auto* strip : stripControls)
            if (strip) strip->repaint();
        for (auto* fxStrip : fxStripControls)
            if (fxStrip) fxStrip->repaint();
        repaint();
    }
    
    // Update grid from monome connection
    if (auto& monome = audioProcessor.getMonomeConnection(); monome.isConnected())
    {
        if (monomeGrid)
            monomeGrid->updateFromEngine();
    }
}
