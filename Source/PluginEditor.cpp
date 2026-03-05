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
constexpr int kTopTabBeatSpaceIndex = 3;
constexpr int kTopTabMixIndex = 4;
constexpr int kTopTabMacroIndex = 5;
constexpr int kStepOscPatchControlCount = 9;
constexpr int kStepNoisePatchControlCount = 8;
constexpr int kStepOutPatchControlCount = 4;
constexpr int kBeatPatternComboBaseId = 7000;

constexpr std::array<int, kStepOscPatchControlCount> kStepOscPatchParamIndices {
    0, 1, 2, 3, 4, 5, 6, 22, 24
};

constexpr std::array<const char*, kStepOscPatchControlCount> kStepOscPatchParamLabels {
    "WAVE", "FREQ", "ATK", "DCY", "MODM", "MODR", "MODA", "OVEL", "MVEL"
};

constexpr std::array<int, kStepNoisePatchControlCount> kStepNoisePatchParamIndices {
    7, 8, 9, 10, 11, 12, 13, 23
};

constexpr std::array<const char*, kStepNoisePatchControlCount> kStepNoisePatchParamLabels {
    "FMOD", "FREQ", "RES", "STEO", "ENVM", "ATK", "DCY", "VEL"
};

constexpr std::array<int, kStepOutPatchControlCount> kStepOutPatchParamIndices {
    14, 15, 16, 17
};

constexpr std::array<const char*, kStepOutPatchControlCount> kStepOutPatchParamLabels {
    "MIX", "DIST", "EQFR", "EQGN"
};

constexpr std::array<const char*, StepVstHostAudioProcessor::BeatSpaceMacroKnobCount> kBeatSpaceMacroKnobLabels {
    "Deep", "Tight", "Noise", "Density", "Pitch", "Pitch Mod", "Length", "Filter"
};

constexpr std::array<double, StepVstHostAudioProcessor::BeatSpaceMacroKnobCount> kBeatSpaceMacroKnobDefaults {
    0.0, 0.0, 0.0, 0.0, 0.5, 0.5, 0.5, 0.5
};

juce::Rectangle<int> getBeatSpaceMapInnerBounds(const juce::Rectangle<int>& previewBounds)
{
    auto inner = previewBounds.reduced(1);
    if (inner.isEmpty())
        return inner;

    const int side = juce::jmax(1, juce::jmin(inner.getWidth(), inner.getHeight()));
    const int x = inner.getX() + ((inner.getWidth() - side) / 2);
    const int y = inner.getY() + ((inner.getHeight() - side) / 2);
    return { x, y, side, side };
}

juce::Rectangle<int> getBeatSpaceMapProjectionBounds(const juce::Rectangle<int>& visibleBounds)
{
    return visibleBounds;
}

juce::Point<float> getBeatSpaceLinkHandleDisplayPixel(
    juce::Point<float> handlePixel,
    const juce::Point<float>& masterPixel,
    const juce::Rectangle<float>& mapBounds,
    int selectedChannel)
{
    const float dx = handlePixel.x - masterPixel.x;
    const float dy = handlePixel.y - masterPixel.y;
    constexpr float kMinSeparation = 11.0f;
    if ((dx * dx) + (dy * dy) >= (kMinSeparation * kMinSeparation))
        return handlePixel;

    juce::ignoreUnused(selectedChannel);
    const float angle = -0.95f;
    constexpr float kOffsetRadius = 15.0f;
    constexpr float kEdgePadding = 8.0f;

    handlePixel.x = juce::jlimit(
        mapBounds.getX() + kEdgePadding,
        mapBounds.getRight() - kEdgePadding,
        masterPixel.x + (std::cos(angle) * kOffsetRadius));
    handlePixel.y = juce::jlimit(
        mapBounds.getY() + kEdgePadding,
        mapBounds.getBottom() - kEdgePadding,
        masterPixel.y + (std::sin(angle) * kOffsetRadius));
    return handlePixel;
}

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

int beatSpacePathPlayModeToComboId(StepVstHostAudioProcessor::BeatSpacePathPlayMode mode)
{
    switch (mode)
    {
        case StepVstHostAudioProcessor::BeatSpacePathPlayMode::Normal: return 1;
        case StepVstHostAudioProcessor::BeatSpacePathPlayMode::Reverse: return 2;
        case StepVstHostAudioProcessor::BeatSpacePathPlayMode::PingPong: return 3;
        case StepVstHostAudioProcessor::BeatSpacePathPlayMode::Random: return 4;
        case StepVstHostAudioProcessor::BeatSpacePathPlayMode::RandomWalk: return 5;
        case StepVstHostAudioProcessor::BeatSpacePathPlayMode::RandomSlice: return 6;
        default: return 1;
    }
}

StepVstHostAudioProcessor::BeatSpacePathPlayMode beatSpacePathPlayModeFromComboId(int comboId)
{
    switch (comboId)
    {
        case 1: return StepVstHostAudioProcessor::BeatSpacePathPlayMode::Normal;
        case 2: return StepVstHostAudioProcessor::BeatSpacePathPlayMode::Reverse;
        case 3: return StepVstHostAudioProcessor::BeatSpacePathPlayMode::PingPong;
        case 4: return StepVstHostAudioProcessor::BeatSpacePathPlayMode::Random;
        case 5: return StepVstHostAudioProcessor::BeatSpacePathPlayMode::RandomWalk;
        case 6: return StepVstHostAudioProcessor::BeatSpacePathPlayMode::RandomSlice;
        default: return StepVstHostAudioProcessor::BeatSpacePathPlayMode::Normal;
    }
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
        case StepVstHostAudioProcessor::ControlMode::GroupAssign: return "Mode";
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
        case StepVstHostAudioProcessor::ControlMode::GroupAssign: return "MODE";
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
        case ModernAudioEngine::ModTarget::Retrigger: return 8;
        case ModernAudioEngine::ModTarget::BeatSpaceX: return 9;
        case ModernAudioEngine::ModTarget::BeatSpaceY: return 10;
        case ModernAudioEngine::ModTarget::Favorites: return 11;
        case ModernAudioEngine::ModTarget::GrainSize:
        case ModernAudioEngine::ModTarget::GrainDensity:
        case ModernAudioEngine::ModTarget::GrainPitch:
        case ModernAudioEngine::ModTarget::GrainPitchJitter:
        case ModernAudioEngine::ModTarget::GrainSpread:
        case ModernAudioEngine::ModTarget::GrainJitter:
        case ModernAudioEngine::ModTarget::GrainRandom:
        case ModernAudioEngine::ModTarget::GrainArp:
        case ModernAudioEngine::ModTarget::GrainCloud:
        case ModernAudioEngine::ModTarget::GrainEmitter:
        case ModernAudioEngine::ModTarget::GrainEnvelope:
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
        case 8:
        case 19: return ModernAudioEngine::ModTarget::Retrigger;
        case 9: return ModernAudioEngine::ModTarget::BeatSpaceX;
        case 10: return ModernAudioEngine::ModTarget::BeatSpaceY;
        case 11: return ModernAudioEngine::ModTarget::Favorites;
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
    // Keep strip UI state (including step playmarker) updating continuously.
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
            {
                const int totalSteps = strip->getStepTotalSteps();
                if (stepIndex < 0 || stepIndex >= totalSteps)
                    return;

                const int clampedSubs = juce::jmax(1, subdivisions);
                strip->setStepSubdivisionAtIndex(stepIndex, clampedSubs);

                if (clampedSubs > 1)
                {
                    // Preserve the subdivision/ramp shape just written above.
                    strip->setStepEnabledAtIndex(stepIndex, true, false);
                    if (strip->getStepProbabilityAtIndex(stepIndex) <= 0.001f)
                        strip->setStepProbabilityAtIndex(stepIndex, 1.0f);
                }
            }
        }
    };
    stepDisplay.onStepVelocityRangeSet = [this](int stepIndex, float startVelocity, float endVelocity)
    {
        if (auto* engine = processor.getAudioEngine())
        {
            if (auto* strip = engine->getStrip(stripIndex))
            {
                const int totalSteps = strip->getStepTotalSteps();
                if (stepIndex < 0 || stepIndex >= totalSteps)
                    return;

                const float clampedStart = juce::jlimit(0.0f, 1.0f, startVelocity);
                const float clampedEnd = juce::jlimit(0.0f, 1.0f, endVelocity);
                strip->setStepSubdivisionVelocityRangeAtIndex(stepIndex, clampedStart, clampedEnd);

                const float maxVel = juce::jmax(clampedStart, clampedEnd);
                if (maxVel > 0.001f)
                {
                    // Preserve the velocity ramp shape just written above.
                    strip->setStepEnabledAtIndex(stepIndex, true, false);
                    if (strip->getStepProbabilityAtIndex(stepIndex) <= 0.001f)
                        strip->setStepProbabilityAtIndex(stepIndex, 1.0f);

                    const bool rampShape = std::abs(clampedStart - clampedEnd) > 0.001f;
                    if (rampShape && strip->getStepSubdivisionAtIndex(stepIndex) <= 1)
                        strip->setStepSubdivisionAtIndex(stepIndex, 2);
                }
            }
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
    stepDisplay.setToolbarVisible(false);
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
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(1.0);
    enableAltClickReset(volumeSlider, 1.0);
    volumeSlider.setPopupDisplayEnabled(true, false, this);
    addAndMakeVisible(volumeSlider);
    
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripVolume" + juce::String(stripIndex), volumeSlider);
    volumeSlider.onDragStart = [this]()
    {
        if (auto* param = processor.parameters.getParameter("stripVolume" + juce::String(stripIndex)))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(volumeSlider.getValue())));
    };
    
    panSlider.setLookAndFeel(&knobLookAndFeel);
    panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setRange(-1.0, 1.0, 0.01);
    panSlider.setValue(0.0);
    enableAltClickReset(panSlider, 0.0);
    panSlider.setPopupDisplayEnabled(true, false, this);
    addAndMakeVisible(panSlider);
    
    panAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "stripPan" + juce::String(stripIndex), panSlider);
    panSlider.onDragStart = [this]()
    {
        if (auto* param = processor.parameters.getParameter("stripPan" + juce::String(stripIndex)))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(panSlider.getValue())));
    };

    pitchSlider.setLookAndFeel(&knobLookAndFeel);
    pitchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
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
    pitchSlider.onDragStart = [this]()
    {
        if (auto* param = processor.parameters.getParameter("stripPitch" + juce::String(stripIndex)))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(pitchSlider.getValue())));
    };

    speedSlider.setLookAndFeel(&knobLookAndFeel);
    speedSlider.setSliderStyle(juce::Slider::LinearHorizontal);
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
    speedSlider.onDragStart = [this]()
    {
        if (auto* param = processor.parameters.getParameter("stripSpeed" + juce::String(stripIndex)))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(speedSlider.getValue())));
    };
    
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

    const auto stepTabLabelFont = juce::Font(juce::FontOptions(9.0f, juce::Font::bold));
    const auto stepTabLabelColour = stripColor.interpolatedWith(kTextPrimary, 0.72f).darker(0.12f);
    const auto stepTabTrackColour = stripColor.withMultipliedSaturation(1.08f).withMultipliedBrightness(0.96f).withAlpha(0.94f);
    const auto stepTabThumbColour = stripColor.brighter(0.38f).interpolatedWith(juce::Colours::white, 0.2f);
    const auto stepTabBackgroundColour = juce::Colour(0xffd8e3ee);

    auto styleStepTabSlider = [this, stepTabTrackColour, stepTabThumbColour, stepTabBackgroundColour](juce::Slider& slider)
    {
        slider.setLookAndFeel(&knobLookAndFeel);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setColour(juce::Slider::backgroundColourId, stepTabBackgroundColour);
        slider.setColour(juce::Slider::trackColourId, stepTabTrackColour);
        slider.setColour(juce::Slider::thumbColourId, stepTabThumbColour);
    };

    auto styleStepTabLabel = [stepTabLabelFont, stepTabLabelColour](juce::Label& label)
    {
        label.setFont(stepTabLabelFont);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, stepTabLabelColour);
    };

    auto setupStepEnvelopeSlider = [this, &styleStepTabSlider, &styleStepTabLabel](juce::Slider& slider, juce::Label& label,
                                          const char* text, double min, double max, double def, double skewMid)
    {
        styleStepTabSlider(slider);
        slider.setRange(min, max, 0.1);
        slider.setSkewFactorFromMidPoint(skewMid);
        slider.setValue(def, juce::dontSendNotification);
        slider.setPopupDisplayEnabled(true, false, this);
        slider.setTextValueSuffix(" ms");
        enableAltClickReset(slider, def);
        addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        styleStepTabLabel(label);
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
    stepAttackSlider.onDragStart = [this]()
    {
        if (auto* param = processor.parameters.getParameter("stripStepAttack" + juce::String(stripIndex)))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(stepAttackSlider.getValue())));
    };
    stepDecaySlider.onDragStart = [this]()
    {
        if (auto* param = processor.parameters.getParameter("stripStepDecay" + juce::String(stripIndex)))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(stepDecaySlider.getValue())));
    };
    stepReleaseSlider.onDragStart = [this]()
    {
        if (auto* param = processor.parameters.getParameter("stripStepRelease" + juce::String(stripIndex)))
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(stepReleaseSlider.getValue())));
    };

    auto setupStepPatchTab = [this](juce::TextButton& button, const juce::String& text, StepPatchTab tab)
    {
        button.setButtonText(text);
        button.setClickingTogglesState(false);
        button.setTooltip("Step patch page: " + text);
        styleUiButton(button, false);
        button.onClick = [this, tab]()
        {
            setStepPatchTabIndex(static_cast<int>(tab));
        };
        addAndMakeVisible(button);
    };
    setupStepPatchTab(stepPatchTabButtons[0], "MIX", StepPatchTab::Mix);
    setupStepPatchTab(stepPatchTabButtons[1], "SHAPE", StepPatchTab::Shape);
    setupStepPatchTab(stepPatchTabButtons[2], "OSC", StepPatchTab::Osc);
    setupStepPatchTab(stepPatchTabButtons[3], "NOISE", StepPatchTab::Noise);
    updateStepPatchTabButtons();

    auto setupPatchSlider = [this, &styleStepTabSlider, &styleStepTabLabel](juce::Slider& slider,
                                   juce::Label& label,
                                   const juce::String& text,
                                   int patchParamIndex)
    {
        auto* sliderPtr = &slider;
        styleStepTabSlider(slider);
        slider.setRange(0.0, 1.0, 0.001);
        slider.setValue(0.5, juce::dontSendNotification);
        slider.setPopupDisplayEnabled(true, false, this);
        slider.setTooltip("Microtonic patch param " + text + " (normalized).");
        slider.onDragStart = [this, patchParamIndex, sliderPtr]()
        {
            if (stripIndex < 0 || stripIndex >= StepVstHostAudioProcessor::BeatSpaceChannels)
                return;
            // Latch the current moving value so edits start from the live modulated position.
            processor.setBeatSpacePatchParamNormalized(
                stripIndex,
                patchParamIndex,
                static_cast<float>(sliderPtr->getValue()));
        };
        slider.onValueChange = [this, patchParamIndex, sliderPtr]()
        {
            if (stripIndex < 0 || stripIndex >= StepVstHostAudioProcessor::BeatSpaceChannels)
                return;
            processor.setBeatSpacePatchParamNormalized(
                stripIndex,
                patchParamIndex,
                static_cast<float>(sliderPtr->getValue()));
        };
        addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        styleStepTabLabel(label);
        addAndMakeVisible(label);
    };

    for (int i = 0; i < static_cast<int>(oscPatchSliders.size()); ++i)
    {
        setupPatchSlider(
            oscPatchSliders[static_cast<size_t>(i)],
            oscPatchLabels[static_cast<size_t>(i)],
            kStepOscPatchParamLabels[static_cast<size_t>(i)],
            kStepOscPatchParamIndices[static_cast<size_t>(i)]);
    }
    for (int i = 0; i < static_cast<int>(noisePatchSliders.size()); ++i)
    {
        setupPatchSlider(
            noisePatchSliders[static_cast<size_t>(i)],
            noisePatchLabels[static_cast<size_t>(i)],
            kStepNoisePatchParamLabels[static_cast<size_t>(i)],
            kStepNoisePatchParamIndices[static_cast<size_t>(i)]);
    }
    for (int i = 0; i < static_cast<int>(outPatchSliders.size()); ++i)
    {
        setupPatchSlider(
            outPatchSliders[static_cast<size_t>(i)],
            outPatchLabels[static_cast<size_t>(i)],
            kStepOutPatchParamLabels[static_cast<size_t>(i)],
            kStepOutPatchParamIndices[static_cast<size_t>(i)]);
    }
    
    // Labels below knobs
    volumeLabel.setText("VOL", juce::dontSendNotification);
    styleStepTabLabel(volumeLabel);
    addAndMakeVisible(volumeLabel);
    
    panLabel.setText("PAN", juce::dontSendNotification);
    styleStepTabLabel(panLabel);
    addAndMakeVisible(panLabel);

    pitchLabel.setText("PITCH", juce::dontSendNotification);
    styleStepTabLabel(pitchLabel);
    addAndMakeVisible(pitchLabel);

    speedLabel.setText("SPEED", juce::dontSendNotification);
    styleStepTabLabel(speedLabel);
    addAndMakeVisible(speedLabel);
    
    scratchLabel.setText("SCR", juce::dontSendNotification);
    styleStepTabLabel(scratchLabel);
    addAndMakeVisible(scratchLabel);

    sliceLengthLabel.setText("SLICE", juce::dontSendNotification);
    styleStepTabLabel(sliceLengthLabel);
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
    modTargetBox.addItem("Retrig", 8);
    modTargetBox.addItem("BSP X", 9);
    modTargetBox.addItem("BSP Y", 10);
    modTargetBox.addItem("Kits", 11);
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

    const bool hasBeatSpacePatchControls =
        stripIndex >= 0 && stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels;
    const bool showPatchTabs = isStepMode && !isGrainMode && hasBeatSpacePatchControls;
    const bool showMixPatch = isStepMode && !isGrainMode
        && (!showPatchTabs || stepPatchTab == StepPatchTab::Mix);
    const bool showShapePatch = isStepMode && !isGrainMode
        && (!showPatchTabs || stepPatchTab == StepPatchTab::Shape);
    const bool showOscPatch = showPatchTabs && stepPatchTab == StepPatchTab::Osc;
    const bool showNoisePatch = showPatchTabs && stepPatchTab == StepPatchTab::Noise;

    const bool showMainMixKnobs = !isGrainMode && (!isStepMode || showMixPatch);
    const bool showTopActionControls = !isGrainMode && (!isStepMode || showMixPatch);
    loadButton.setVisible(showTopActionControls);
    muteButton.setVisible(showTopActionControls);
    soloButton.setVisible(showTopActionControls);
    directionModeBox.setVisible(showTopActionControls);
    volumeSlider.setVisible(showMainMixKnobs);
    panSlider.setVisible(showMainMixKnobs);
    volumeLabel.setVisible(showMainMixKnobs);
    panLabel.setVisible(showMainMixKnobs);

    const bool showOffsetKnobs = showMainMixKnobs;
    const bool showLoopOnlyKnobs = !showingStepDisplay && !isGrainMode;
    bool showSliceLength = false;
    if (showLoopOnlyKnobs && processor.getAudioEngine())
        showSliceLength = false;

    pitchSlider.setVisible(showOffsetKnobs);
    speedSlider.setVisible(showOffsetKnobs);
    scratchSlider.setVisible(showLoopOnlyKnobs);
    sliceLengthSlider.setVisible(showSliceLength);
    pitchLabel.setVisible(showOffsetKnobs);
    speedLabel.setVisible(showOffsetKnobs);
    scratchLabel.setVisible(showLoopOnlyKnobs);
    sliceLengthLabel.setVisible(showSliceLength);
    patternLengthBox.setVisible(isStepMode && !isGrainMode);
    stepLengthReadoutBox.setVisible(isStepMode && !isGrainMode);
    laneMidiChannelBox.setVisible(isStepMode && !isGrainMode);
    laneMidiNoteBox.setVisible(isStepMode && !isGrainMode);
    laneMidiChannelLabel.setVisible(isStepMode && !isGrainMode);
    laneMidiNoteLabel.setVisible(isStepMode && !isGrainMode);

    // Step patch tabs are global (shared above strip 1), so strip-local tabs stay hidden.
    for (auto& tabButton : stepPatchTabButtons)
        tabButton.setVisible(false);

    stepAttackSlider.setVisible(showShapePatch);
    stepDecaySlider.setVisible(showShapePatch);
    stepReleaseSlider.setVisible(showShapePatch);
    stepAttackLabel.setVisible(showShapePatch);
    stepDecayLabel.setVisible(showShapePatch);
    stepReleaseLabel.setVisible(showShapePatch);

    for (size_t i = 0; i < oscPatchSliders.size(); ++i)
    {
        oscPatchSliders[i].setVisible(showOscPatch);
        oscPatchLabels[i].setVisible(showOscPatch);
    }
    for (size_t i = 0; i < noisePatchSliders.size(); ++i)
    {
        noisePatchSliders[i].setVisible(showNoisePatch);
        noisePatchLabels[i].setVisible(showNoisePatch);
    }
    for (size_t i = 0; i < outPatchSliders.size(); ++i)
    {
        outPatchSliders[i].setVisible(showShapePatch);
        outPatchLabels[i].setVisible(showShapePatch);
    }

    beatSpaceCategoryLabel.setVisible(false);
    beatSpaceCategoryBox.setVisible(
        showTopActionControls && stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels);
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
    updateStepPatchTabButtons();
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

void StripControl::updateStepPatchTabButtons()
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

    tintTab(stepPatchTabButtons[0], stepPatchTab == StepPatchTab::Mix);
    tintTab(stepPatchTabButtons[1], stepPatchTab == StepPatchTab::Shape);
    tintTab(stepPatchTabButtons[2], stepPatchTab == StepPatchTab::Osc);
    tintTab(stepPatchTabButtons[3], stepPatchTab == StepPatchTab::Noise);
}

void StripControl::setStepPatchTabIndex(int tabIndex)
{
    const int clamped = juce::jlimit(0, 3, tabIndex);
    const auto nextTab = static_cast<StepPatchTab>(clamped);
    if (stepPatchTab == nextTab)
        return;

    stepPatchTab = nextTab;
    oscPatchWheelAccum = 0.0f;
    noisePatchWheelAccum = 0.0f;
    updateStepPatchTabButtons();
    updateGrainOverlayVisibility();
    resized();
    repaint();
}

int StripControl::getStepPatchTabIndex() const noexcept
{
    return static_cast<int>(stepPatchTab);
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

    if (beatSpaceBubbleSelected)
    {
        const auto labelBounds = stripLabel.getBounds().toFloat();
        const float cx = labelBounds.getRight() + 4.0f;
        const float cy = labelBounds.getCentreY();
        const auto indicatorColour = stripColor.brighter(0.45f);

        g.setColour(indicatorColour.withAlpha(0.26f));
        g.fillEllipse(cx - 4.6f, cy - 4.6f, 9.2f, 9.2f);
        g.setColour(juce::Colours::white.withAlpha(0.94f));
        g.fillEllipse(cx - 2.45f, cy - 2.45f, 4.9f, 4.9f);
        g.setColour(indicatorColour.withAlpha(0.98f));
        g.fillEllipse(cx - 1.55f, cy - 1.55f, 3.1f, 3.1f);
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

void StripControl::setStepToolbarVisible(bool shouldShow)
{
    stepDisplay.setToolbarVisible(shouldShow);
}

void StripControl::setStepEditTool(StepSequencerDisplay::EditTool tool)
{
    stepDisplay.setActiveTool(tool);
}

StepSequencerDisplay::EditTool StripControl::getStepEditTool() const
{
    return stepDisplay.getActiveTool();
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

void StripControl::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const bool patchTabActive = showingStepDisplay
        && !grainOverlayVisible
        && !modulationLaneView
        && (stepPatchTab == StepPatchTab::Osc
            || stepPatchTab == StepPatchTab::Noise);
    if (!patchTabActive || stepPatchWheelBounds.isEmpty())
    {
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }

    const auto local = e.getEventRelativeTo(this).getPosition();
    if (!stepPatchWheelBounds.contains(local))
    {
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }

    const bool oscTab = (stepPatchTab == StepPatchTab::Osc);
    auto& scrollRow = oscTab ? oscPatchScrollRow : noisePatchScrollRow;
    auto& wheelAccum = oscTab ? oscPatchWheelAccum : noisePatchWheelAccum;
    const int totalControls = oscTab
        ? static_cast<int>(oscPatchSliders.size())
        : static_cast<int>(noisePatchSliders.size());
    const int totalRows = juce::jmax(1, (totalControls + 1) / 2);
    constexpr int kMaxVisibleRows = 4;
    const int visibleRows = juce::jmin(kMaxVisibleRows, totalRows);
    const int maxScrollRow = juce::jmax(0, totalRows - visibleRows);
    if (maxScrollRow <= 0)
    {
        wheelAccum = 0.0f;
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }

    const float primaryDelta = (std::abs(wheel.deltaY) >= std::abs(wheel.deltaX))
        ? wheel.deltaY
        : wheel.deltaX;
    if (std::abs(primaryDelta) <= 1.0e-5f)
    {
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }

    constexpr float kScrollThreshold = 0.28f;
    wheelAccum += primaryDelta;
    const int previousRow = scrollRow;
    while (wheelAccum <= -kScrollThreshold)
    {
        scrollRow = juce::jmin(maxScrollRow, scrollRow + 1);
        wheelAccum += kScrollThreshold;
        if (scrollRow >= maxScrollRow)
        {
            wheelAccum = juce::jmax(0.0f, wheelAccum);
            break;
        }
    }
    while (wheelAccum >= kScrollThreshold)
    {
        scrollRow = juce::jmax(0, scrollRow - 1);
        wheelAccum -= kScrollThreshold;
        if (scrollRow <= 0)
        {
            wheelAccum = juce::jmin(0.0f, wheelAccum);
            break;
        }
    }

    if (scrollRow != previousRow)
    {
        resized();
        repaint();
        return;
    }

    juce::Component::mouseWheelMove(e, wheel);
}

void StripControl::hideAllPrimaryControls()
{
    auto hide = [](juce::Component& c){ c.setVisible(false); };
    hide(loadButton); hide(muteButton); hide(soloButton); hide(directionModeBox);
    hide(beatSpaceCategoryLabel); hide(beatSpaceCategoryBox);
    hide(volumeSlider); hide(panSlider); hide(pitchSlider); hide(speedSlider); hide(scratchSlider); hide(sliceLengthSlider); hide(patternLengthBox); hide(stepLengthReadoutBox);
    hide(laneMidiChannelBox); hide(laneMidiNoteBox);
    for (auto& tab : stepPatchTabButtons)
        hide(tab);
    hide(stepAttackSlider); hide(stepDecaySlider); hide(stepReleaseSlider);
    for (auto& slider : oscPatchSliders)
        hide(slider);
    for (auto& label : oscPatchLabels)
        hide(label);
    for (auto& slider : noisePatchSliders)
        hide(slider);
    for (auto& label : noisePatchLabels)
        hide(label);
    for (auto& slider : outPatchSliders)
        hide(slider);
    for (auto& label : outPatchLabels)
        hide(label);
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
    stepPatchWheelBounds = {};
    
    // Safety check for minimum size
    if (bounds.getWidth() < 50 || bounds.getHeight() < 50)
        return;
    
    // Label at very top
    auto labelArea = bounds.removeFromTop(14);
    stripLabel.setBounds(labelArea.removeFromLeft(30));
    
    // Main area splits: waveform left, controls right
    const bool isGrainMode = grainOverlayVisible;
    const bool isStepMode = showingStepDisplay;
    const int preferredControlsWidth = juce::roundToInt(
        bounds.getWidth() * (isStepMode ? 0.31f : 0.27f));
    const int controlsWidth = juce::jlimit(isStepMode ? 250 : 236,
                                           isStepMode ? 320 : 286,
                                           preferredControlsWidth);
    auto controlsArea = bounds.removeFromRight(controlsWidth);
    
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

    const bool hasBeatSpacePatchControls =
        stripIndex >= 0 && stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels;
    const bool showPatchTabs = isStepMode && !isGrainMode && hasBeatSpacePatchControls;
    const bool showMixPatch = !isStepMode || !showPatchTabs || stepPatchTab == StepPatchTab::Mix;
    const bool showTopActionRow = !isGrainMode && showMixPatch;

    loadButton.setVisible(showTopActionRow);
    muteButton.setVisible(showTopActionRow);
    soloButton.setVisible(showTopActionRow);
    directionModeBox.setVisible(showTopActionRow);
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
    
    const bool isGrainSpacePage = isGrainMode && grainSubPage == GrainSubPage::Space;
    patternLengthBox.setBounds({});
    stepLengthReadoutBox.setBounds({});
    laneMidiChannelLabel.setBounds({});
    laneMidiChannelBox.setBounds({});
    laneMidiNoteLabel.setBounds({});
    laneMidiNoteBox.setBounds({});

    const int rowGap = isGrainMode ? 0 : (isStepMode ? 0 : 1);
    const bool placeTopActionUnderStepTabs = isStepMode && showTopActionRow && showPatchTabs;

    auto layoutTopActionRow = [&](juce::Rectangle<int>& area)
    {
        const int topRowHeight = isGrainMode ? (isGrainSpacePage ? 12 : 14) : (isStepMode ? 16 : 17);
        auto topRow = area.removeFromTop(topRowHeight);
        const int loadWidth = juce::jlimit(24, isGrainMode ? 34 : 42, topRow.getWidth() / 7);
        const int toggleWidth = juce::jlimit(isGrainMode ? 22 : 30, isGrainMode ? 32 : 44, topRow.getWidth() / 5);
        muteButton.setBounds(topRow.removeFromLeft(toggleWidth));
        topRow.removeFromLeft(1);
        soloButton.setBounds(topRow.removeFromLeft(toggleWidth));
        topRow.removeFromLeft(2);
        if (stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels)
        {
            const int catBoxW = juce::jlimit(70, 108, topRow.getWidth() / 3);
            beatSpaceCategoryBox.setBounds(topRow.removeFromLeft(catBoxW));
            topRow.removeFromLeft(2);
            loadButton.setBounds(topRow.removeFromLeft(loadWidth));
            topRow.removeFromLeft(2);
        }
        else
        {
            beatSpaceCategoryLabel.setBounds({});
            beatSpaceCategoryBox.setBounds({});
            loadButton.setBounds(topRow.removeFromLeft(loadWidth));
            topRow.removeFromLeft(2);
        }
        directionModeBox.setBounds(topRow.reduced(1, 0));
        area.removeFromTop(rowGap);
    };

    // Keep top action controls on the regular row, except Step/Mix where they belong under the tab bar.
    if (showTopActionRow && !placeTopActionUnderStepTabs)
    {
        layoutTopActionRow(controlsArea);
    }
    else if (!showTopActionRow)
    {
        beatSpaceCategoryLabel.setBounds({});
        beatSpaceCategoryBox.setBounds({});
        loadButton.setBounds({});
        muteButton.setBounds({});
        soloButton.setBounds({});
        directionModeBox.setBounds({});
    }
    
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
        const bool showStepAuxControls = controlsArea.getHeight() >= 132;
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

    if (!isGrainMode)
        controlsArea.removeFromTop(isStepMode ? 1 : 2);
    
    if (isStepMode)
    {
        // Dedicated step layout with patch tabs: MIX, SHAPE, OSC, NOISE.
        auto stepArea = controlsArea;
        const int totalStepHeight = juce::jmax(1, stepArea.getHeight());
        const int stepGap = totalStepHeight >= 58 ? 2 : 1;

        auto layoutStepTabCell = [](juce::Rectangle<int> cell, juce::Slider& slider, juce::Label& label)
        {
            const int labelHeight = juce::jlimit(9, 12, juce::jmax(9, cell.getHeight() / 4));
            label.setBounds(cell.removeFromTop(labelHeight));
            if (cell.getHeight() > 0)
                cell.removeFromTop(1);

            const int sliderHeight = juce::jlimit(10, 16, juce::jmax(10, cell.getHeight()));
            auto sliderArea = cell.removeFromTop(sliderHeight);
            const int padY = juce::jmax(0, (sliderArea.getHeight() - 12) / 2);
            slider.setBounds(sliderArea.reduced(1, padY));
        };

        if (stepPatchTabButtons[0].isVisible())
        {
            auto tabRow = stepArea.removeFromTop(juce::jlimit(12, 16, totalStepHeight / 8));
            const int tabGap = 2;
            const int tabWidth = juce::jmax(1, (tabRow.getWidth() - (3 * tabGap)) / 4);
            stepPatchTabButtons[0].setBounds(tabRow.removeFromLeft(tabWidth));
            tabRow.removeFromLeft(tabGap);
            stepPatchTabButtons[1].setBounds(tabRow.removeFromLeft(tabWidth));
            tabRow.removeFromLeft(tabGap);
            stepPatchTabButtons[2].setBounds(tabRow.removeFromLeft(tabWidth));
            tabRow.removeFromLeft(tabGap);
            stepPatchTabButtons[3].setBounds(tabRow);
            if (stepArea.getHeight() > stepGap)
                stepArea.removeFromTop(stepGap);
        }
        else
        {
            for (auto& tab : stepPatchTabButtons)
                tab.setBounds({});
        }

        if (placeTopActionUnderStepTabs)
            layoutTopActionRow(stepArea);

        auto layoutMixPage = [&](juce::Rectangle<int> area)
        {
            std::array<juce::Slider*, 4> pageSliders {
                &volumeSlider, &panSlider, &pitchSlider, &speedSlider
            };
            std::array<juce::Label*, 4> pageLabels {
                &volumeLabel, &panLabel, &pitchLabel, &speedLabel
            };

            constexpr int columns = 4;
            const int colGap = area.getWidth() >= 170 ? 4 : 3;
            auto rowArea = area.removeFromTop(juce::jmax(14, area.getHeight()));
            const int cellWidth = juce::jmax(10, (rowArea.getWidth() - ((columns - 1) * colGap)) / columns);

            for (int col = 0; col < columns; ++col)
            {
                auto cell = rowArea.removeFromLeft(cellWidth);
                if (col < (columns - 1))
                    rowArea.removeFromLeft(colGap);

                layoutStepTabCell(cell,
                                  *pageSliders[static_cast<size_t>(col)],
                                  *pageLabels[static_cast<size_t>(col)]);
            }
        };

        auto layoutShapePage = [&](juce::Rectangle<int> area)
        {
            const int shapeRowGap = area.getHeight() >= 32 ? 2 : 1;
            const int envRowHeight = juce::jmax(10, (area.getHeight() - shapeRowGap) / 2);
            auto envRow = area.removeFromTop(envRowHeight);
            const int envGap = area.getWidth() >= 150 ? 4 : 3;
            const int envWidth = juce::jmax(10, (envRow.getWidth() - (2 * envGap)) / 3);

            layoutStepTabCell(envRow.removeFromLeft(envWidth), stepAttackSlider, stepAttackLabel);
            envRow.removeFromLeft(envGap);
            layoutStepTabCell(envRow.removeFromLeft(envWidth), stepDecaySlider, stepDecayLabel);
            envRow.removeFromLeft(envGap);
            layoutStepTabCell(envRow.removeFromLeft(envWidth), stepReleaseSlider, stepReleaseLabel);

            if (area.getHeight() > shapeRowGap)
                area.removeFromTop(shapeRowGap);

            const int outGap = area.getWidth() >= 170 ? 4 : 3;
            const int outWidth = juce::jmax(10, (area.getWidth() - (3 * outGap)) / 4);
            for (int i = 0; i < 4; ++i)
            {
                layoutStepTabCell(area.removeFromLeft(outWidth),
                                  outPatchSliders[static_cast<size_t>(i)],
                                  outPatchLabels[static_cast<size_t>(i)]);
                if (i < 3)
                    area.removeFromLeft(outGap);
            }
        };

        auto layoutPatchPage = [&](juce::Rectangle<int> area)
        {
            const bool showOscPage = oscPatchSliders[0].isVisible();
            auto& scrollRow = showOscPage ? oscPatchScrollRow : noisePatchScrollRow;
            juce::Slider* const pageSliders = showOscPage
                ? oscPatchSliders.data()
                : noisePatchSliders.data();
            juce::Label* const pageLabels = showOscPage
                ? oscPatchLabels.data()
                : noisePatchLabels.data();
            constexpr int kMaxVisibleRows = 3;
            const int patchRowGap = 1;
            const int colGap = 5;

            const int totalControls = showOscPage
                ? static_cast<int>(oscPatchSliders.size())
                : static_cast<int>(noisePatchSliders.size());
            const int totalRows = juce::jmax(1, (totalControls + 1) / 2);
            const int visibleRows = juce::jmax(1, juce::jmin(kMaxVisibleRows, totalRows));
            const int maxScrollRow = juce::jmax(0, totalRows - visibleRows);
            scrollRow = juce::jlimit(0, maxScrollRow, scrollRow);

            const int totalGaps = patchRowGap * juce::jmax(0, visibleRows - 1);
            const int rowHeight = juce::jmax(1, (juce::jmax(1, area.getHeight()) - totalGaps) / visibleRows);

            for (int i = 0; i < totalControls; ++i)
            {
                pageLabels[static_cast<size_t>(i)].setBounds({});
                pageSliders[static_cast<size_t>(i)].setBounds({});
            }

            for (int row = 0; row < visibleRows; ++row)
            {
                const int logicalRow = scrollRow + row;
                auto rowArea = area.removeFromTop(rowHeight);
                const int halfWidth = juce::jmax(8, (rowArea.getWidth() - colGap) / 2);
                auto left = rowArea.removeFromLeft(halfWidth);
                rowArea.removeFromLeft(colGap);
                auto right = rowArea;

                const int leftIndex = logicalRow * 2;
                const int rightIndex = leftIndex + 1;

                if (leftIndex >= 0 && leftIndex < totalControls)
                {
                    layoutStepTabCell(left,
                                      pageSliders[static_cast<size_t>(leftIndex)],
                                      pageLabels[static_cast<size_t>(leftIndex)]);
                }
                if (rightIndex >= 0 && rightIndex < totalControls)
                {
                    layoutStepTabCell(right,
                                      pageSliders[static_cast<size_t>(rightIndex)],
                                      pageLabels[static_cast<size_t>(rightIndex)]);
                }

                if (row < (visibleRows - 1) && area.getHeight() > patchRowGap)
                    area.removeFromTop(patchRowGap);
            }
        };

        const bool mixVisible = volumeSlider.isVisible();
        const bool shapeVisible = stepAttackSlider.isVisible();
        const bool patchVisible = oscPatchSliders[0].isVisible()
            || noisePatchSliders[0].isVisible();

        if (mixVisible && shapeVisible)
        {
            const int splitHeight = juce::jlimit(20, juce::jmax(20, stepArea.getHeight() - 24), stepArea.getHeight() / 2);
            auto mixArea = stepArea.removeFromTop(splitHeight);
            layoutMixPage(mixArea);
            if (stepArea.getHeight() > stepGap)
                stepArea.removeFromTop(stepGap);
            layoutShapePage(stepArea);
        }
        else if (mixVisible)
        {
            layoutMixPage(stepArea);
        }
        else if (shapeVisible)
        {
            layoutShapePage(stepArea);
        }
        else if (patchVisible)
        {
            stepPatchWheelBounds = stepArea;
            layoutPatchPage(stepArea);
        }
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

    bool showBeatSpaceBubbleIndicator = false;
    if (stripIndex >= 0 && stripIndex < StepVstHostAudioProcessor::BeatSpaceChannels)
    {
        const auto beatState = processor.getBeatSpaceVisualState();
        showBeatSpaceBubbleIndicator = (juce::jlimit(
            0,
            StepVstHostAudioProcessor::BeatSpaceChannels - 1,
            beatState.selectedChannel) == stripIndex);
    }
    if (beatSpaceBubbleSelected != showBeatSpaceBubbleIndicator)
    {
        beatSpaceBubbleSelected = showBeatSpaceBubbleIndicator;
        repaint(stripLabel.getBounds().expanded(10, 2));
    }

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
        for (int i = 0; i < static_cast<int>(oscPatchSliders.size()); ++i)
        {
            auto& slider = oscPatchSliders[static_cast<size_t>(i)];
            if (!slider.isMouseButtonDown())
            {
                const float normalized = processor.getBeatSpacePatchParamNormalized(
                    stripIndex,
                    kStepOscPatchParamIndices[static_cast<size_t>(i)]);
                slider.setValue(normalized, juce::dontSendNotification);
            }
        }
        for (int i = 0; i < static_cast<int>(noisePatchSliders.size()); ++i)
        {
            auto& slider = noisePatchSliders[static_cast<size_t>(i)];
            if (!slider.isMouseButtonDown())
            {
                const float normalized = processor.getBeatSpacePatchParamNormalized(
                    stripIndex,
                    kStepNoisePatchParamIndices[static_cast<size_t>(i)]);
                slider.setValue(normalized, juce::dontSendNotification);
            }
        }
        for (int i = 0; i < static_cast<int>(outPatchSliders.size()); ++i)
        {
            auto& slider = outPatchSliders[static_cast<size_t>(i)];
            if (!slider.isMouseButtonDown())
            {
                const float normalized = processor.getBeatSpacePatchParamNormalized(
                    stripIndex,
                    kStepOutPatchParamIndices[static_cast<size_t>(i)]);
                slider.setValue(normalized, juce::dontSendNotification);
            }
        }
    }
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
    const float paramSpeed = getParamValue(
        "stripSpeed" + juce::String(stripIndex),
        getPlayheadSpeedRatioForStrip(*strip));
    const float paramPitch = getParamValue(
        "stripPitch" + juce::String(stripIndex),
        processor.getPitchSemitonesForDisplay(*strip));
    const float paramAttack = getParamValue("stripStepAttack" + juce::String(stripIndex), strip->getStepEnvelopeAttackMs());
    const float paramDecay = getParamValue("stripStepDecay" + juce::String(stripIndex), strip->getStepEnvelopeDecayMs());
    const float paramRelease = getParamValue("stripStepRelease" + juce::String(stripIndex), strip->getStepEnvelopeReleaseMs());

    if (!modulates(ModernAudioEngine::ModTarget::Volume))
        volumeSlider.setValue(paramVolume, juce::dontSendNotification);
    if (!modulates(ModernAudioEngine::ModTarget::Pan))
        panSlider.setValue(paramPan, juce::dontSendNotification);
    if (!pitchSlider.isMouseButtonDown() && !modulates(ModernAudioEngine::ModTarget::Pitch))
        pitchSlider.setValue(paramPitch, juce::dontSendNotification);

    const auto toNorm = [](float value, float minValue, float maxValue)
    {
        const float denom = juce::jmax(1.0e-6f, maxValue - minValue);
        return juce::jlimit(0.0f, 1.0f, (value - minValue) / denom);
    };
    const auto setOffsetIndicator = [](juce::Slider& slider,
                                       float neutralNorm,
                                       float valueNorm,
                                       float epsilon,
                                       juce::Colour colour,
                                       juce::Colour baseColour)
    {
        const float neutral = juce::jlimit(0.0f, 1.0f, neutralNorm);
        const float value = juce::jlimit(0.0f, 1.0f, valueNorm);
        const bool active = std::abs(value - neutral) > juce::jmax(1.0e-6f, epsilon);
        auto& props = slider.getProperties();
        props.set("offsetActive", active);
        props.set("offsetNeutralNorm", neutral);
        props.set("offsetValueNorm", value);
        props.set("offsetColour", static_cast<int>(colour.getARGB()));
        props.set("offsetBaseColour", static_cast<int>(baseColour.getARGB()));
    };

    const auto offsetRingColour = stripColor
        .interpolatedWith(juce::Colours::white, 0.33f)
        .withMultipliedSaturation(1.24f)
        .withMultipliedBrightness(1.12f);
    const auto offsetRingBaseColour = stripColor
        .interpolatedWith(juce::Colours::white, 0.24f)
        .withMultipliedSaturation(0.62f)
        .withMultipliedBrightness(0.78f);
    setOffsetIndicator(volumeSlider, 1.0f, paramVolume, 1.0e-4f, offsetRingColour, offsetRingBaseColour);
    setOffsetIndicator(panSlider, 0.5f, toNorm(paramPan, -1.0f, 1.0f), 1.0e-4f, offsetRingColour, offsetRingBaseColour);
    setOffsetIndicator(speedSlider, toNorm(1.0f, 0.0f, 8.0f), toNorm(paramSpeed, 0.0f, 8.0f), 1.0e-4f, offsetRingColour, offsetRingBaseColour);
    setOffsetIndicator(pitchSlider, 0.5f, toNorm(paramPitch, -24.0f, 24.0f), 1.0e-4f, offsetRingColour, offsetRingBaseColour);
    setOffsetIndicator(stepAttackSlider, 0.0f, toNorm(paramAttack, 0.0f, 400.0f), 1.0e-4f, offsetRingColour, offsetRingBaseColour);
    setOffsetIndicator(stepDecaySlider, 1.0f, toNorm(paramDecay, 1.0f, 4000.0f), 1.0e-4f, offsetRingColour, offsetRingBaseColour);
    setOffsetIndicator(stepReleaseSlider, toNorm(110.0f, 1.0f, 4000.0f), toNorm(paramRelease, 1.0f, 4000.0f), 1.0e-4f, offsetRingColour, offsetRingBaseColour);

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
    tintSlider(stepAttackSlider, baseControl, 0.0f);
    tintSlider(stepDecaySlider, baseControl, 0.0f);
    tintSlider(stepReleaseSlider, baseControl, 0.0f);
    for (auto& slider : oscPatchSliders)
        tintSlider(slider, baseControl, 0.0f);
    for (auto& slider : noisePatchSliders)
        tintSlider(slider, baseControl, 0.0f);
    for (auto& slider : outPatchSliders)
        tintSlider(slider, baseControl, 0.0f);
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
    setModIndicator(stepAttackSlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(stepDecaySlider, false, 0.0f, 0.0f, kAccent);
    setModIndicator(stepReleaseSlider, false, 0.0f, 0.0f, kAccent);
    for (auto& slider : oscPatchSliders)
        setModIndicator(slider, false, 0.0f, 0.0f, kAccent);
    for (auto& slider : noisePatchSliders)
        setModIndicator(slider, false, 0.0f, 0.0f, kAccent);
    for (auto& slider : outPatchSliders)
        setModIndicator(slider, false, 0.0f, 0.0f, kAccent);

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
                    case ModernAudioEngine::ModTarget::BeatSpaceX: return nullptr;
                    case ModernAudioEngine::ModTarget::BeatSpaceY: return nullptr;
                    case ModernAudioEngine::ModTarget::Favorites: return nullptr;
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
    if (!isShowing())
        return;

    updateFromEngine();
}

void FXStripControl::visibilityChanged()
{
    if (isShowing())
    {
        if (!isTimerRunning())
            startTimer(50); // 20 Hz
        updateFromEngine();
    }
    else
    {
        stopTimer();
    }
}

void StripControl::timerCallback()
{
    if (!processor.getAudioEngine())
        return;
    
    updateFromEngine();
}

void StripControl::visibilityChanged()
{
    if (isShowing())
        updateFromEngine();
}

MonomeGridDisplay::MonomeGridDisplay(StepVstHostAudioProcessor& p)
    : processor(p)
{
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
    if (!isShowing())
        return;

    if (!processor.getAudioEngine())
        return;
    
    updateFromEngine();
}

void MonomeGridDisplay::visibilityChanged()
{
    if (isShowing())
    {
        if (!isTimerRunning())
            startTimer(50); // 20 Hz
        updateFromEngine();
    }
    else
    {
        stopTimer();
    }
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
    if (!isShowing())
        return;

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

    quantizeLabel.setText("Quantize", juce::dontSendNotification);
    quantizeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(quantizeLabel);

    quantizeSelector.addItem("1", 1);
    quantizeSelector.addItem("1/2", 2);
    quantizeSelector.addItem("1/2T", 3);
    quantizeSelector.addItem("1/4", 4);
    quantizeSelector.addItem("1/4T", 5);
    quantizeSelector.addItem("1/8", 6);
    quantizeSelector.addItem("1/8T", 7);
    quantizeSelector.addItem("1/16", 8);
    quantizeSelector.addItem("1/16T", 9);
    quantizeSelector.addItem("1/32", 10);
    quantizeSelector.setSelectedId(6); // Default to 1/8
    addAndMakeVisible(quantizeSelector);
    styleUiCombo(quantizeSelector);
    quantizeSelector.setTooltip("Global PPQ quantization grid for triggering, sub-preset switching, and stutter in/out.");
    quantizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, "quantize", quantizeSelector);
    quantizeSelector.onChange = [this]()
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

    auto applyKitScaleSettingsImmediately = [this]()
    {
        processor.applyGlobalKitScaleSettings();
    };
    auto ensureKitScaleEnabled = [this]()
    {
        if (auto* enabledParam = processor.parameters.getParameter("kitScaleEnabled"))
        {
            if (enabledParam->getValue() < 0.5f)
                enabledParam->setValueNotifyingHost(1.0f);
        }
    };

    kitScaleToggle.setButtonText("Kit Key");
    kitScaleToggle.setClickingTogglesState(true);
    kitScaleToggle.setTooltip("Enable global kit tuning so all strip pitch values follow one root and scale.");
    addAndMakeVisible(kitScaleToggle);
    styleUiButton(kitScaleToggle);
    kitScaleEnabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, "kitScaleEnabled", kitScaleToggle);
    kitScaleToggle.onClick = [this, applyKitScaleSettingsImmediately]()
    {
        if (globalUiReady)
        {
            applyKitScaleSettingsImmediately();
            processor.markPersistentGlobalUserChange();
        }
    };

    kitScaleRootLabel.setText("Root", juce::dontSendNotification);
    kitScaleRootLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(kitScaleRootLabel);

    kitScaleRootBox.addItem("C", 1);
    kitScaleRootBox.addItem("C#", 2);
    kitScaleRootBox.addItem("D", 3);
    kitScaleRootBox.addItem("D#", 4);
    kitScaleRootBox.addItem("E", 5);
    kitScaleRootBox.addItem("F", 6);
    kitScaleRootBox.addItem("F#", 7);
    kitScaleRootBox.addItem("G", 8);
    kitScaleRootBox.addItem("G#", 9);
    kitScaleRootBox.addItem("A", 10);
    kitScaleRootBox.addItem("A#", 11);
    kitScaleRootBox.addItem("B", 12);
    kitScaleRootBox.setTooltip("Global root note for kit tuning.");
    styleUiCombo(kitScaleRootBox);
    addAndMakeVisible(kitScaleRootBox);
    kitScaleRootAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, "kitScaleRoot", kitScaleRootBox);
    kitScaleRootBox.onChange = [this, applyKitScaleSettingsImmediately, ensureKitScaleEnabled]()
    {
        if (globalUiReady)
        {
            ensureKitScaleEnabled();
            applyKitScaleSettingsImmediately();
            processor.markPersistentGlobalUserChange();
        }
    };

    kitScaleModeLabel.setText("Scale", juce::dontSendNotification);
    kitScaleModeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(kitScaleModeLabel);

    kitScaleModeBox.addItem("Chromatic", 1);
    kitScaleModeBox.addItem("Major", 2);
    kitScaleModeBox.addItem("Minor", 3);
    kitScaleModeBox.addItem("Dorian", 4);
    kitScaleModeBox.addItem("Pentatonic", 5);
    kitScaleModeBox.setTooltip("Global scale used when kit key mode is enabled.");
    styleUiCombo(kitScaleModeBox);
    addAndMakeVisible(kitScaleModeBox);
    kitScaleModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, "kitScaleMode", kitScaleModeBox);
    kitScaleModeBox.onChange = [this, applyKitScaleSettingsImmediately, ensureKitScaleEnabled]()
    {
        if (globalUiReady)
        {
            ensureKitScaleEnabled();
            applyKitScaleSettingsImmediately();
            processor.markPersistentGlobalUserChange();
        }
    };

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

    microtonicPresetStripLabel.setVisible(false);
    microtonicPresetStripBox.setVisible(false);

    microtonicPresetListLabel.setText("Kits", juce::dontSendNotification);
    microtonicPresetListLabel.setJustificationType(juce::Justification::centredLeft);
    microtonicPresetListLabel.setTooltip("Stored full-kit snapshots (all 6 lanes, pattern excluded).");
    addAndMakeVisible(microtonicPresetListLabel);

    microtonicPresetListBox.setTooltip("Select to recall a kit, or choose a new slot to store.");
    styleUiCombo(microtonicPresetListBox);
    microtonicPresetListBox.onChange = [this]()
    {
        constexpr int kitScopeChannel = 0;
        const int selectedId = microtonicPresetListBox.getSelectedId();
        if (selectedId < 1 || selectedId > StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
            return;

        juce::String error;
        if (!processor.recallMicrotonicStripPreset(kitScopeChannel, selectedId - 1, &error))
        {
            hostedStatusLabel.setText(
                "Patch recall failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
                juce::dontSendNotification);
            return;
        }

        hostedStatusLabel.setText(
            "Recalled patch combo slot " + juce::String(selectedId).paddedLeft('0', 2),
            juce::dontSendNotification);
    };
    addAndMakeVisible(microtonicPresetListBox);

    microtonicPresetStoreButton.setButtonText("Store Patch");
    microtonicPresetStoreButton.setTooltip("Store current sound combination for all lanes.");
    microtonicPresetStoreButton.onClick = [this]()
    {
        constexpr int kitScopeChannel = 0;
        const int selectedId = microtonicPresetListBox.getSelectedId();
        int preferredSlot = -1;
        if (selectedId >= 1000
                 && selectedId < (1000 + StepVstHostAudioProcessor::MicrotonicStripPresetSlots))
            preferredSlot = selectedId - 1000;
        else if (selectedId >= 1 && selectedId <= StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
        {
            // Default behavior: keep adding new kits while free slots exist.
            // If all slots are full, overwrite the selected slot instead.
            const auto usedSlots = processor.getMicrotonicStripPresetSlots(kitScopeChannel);
            const bool hasFreeSlot = usedSlots.size() < static_cast<size_t>(StepVstHostAudioProcessor::MicrotonicStripPresetSlots);
            if (!hasFreeSlot)
                preferredSlot = selectedId - 1;
        }

        int storedSlot = -1;
        juce::String error;
        if (!processor.storeCurrentMicrotonicStripPreset(kitScopeChannel, preferredSlot, {}, &storedSlot, &error))
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
            "Stored patch combo slot " + juce::String(storedSlot + 1).paddedLeft('0', 2),
            juce::dontSendNotification);
        if (globalUiReady)
            processor.markPersistentGlobalUserChange();
    };
    styleUiButton(microtonicPresetStoreButton, true);
    addAndMakeVisible(microtonicPresetStoreButton);

    microtonicPresetRecallButton.setButtonText("Recall Patch");
    microtonicPresetRecallButton.setTooltip("Recall stored sound combination for all lanes.");
    microtonicPresetRecallButton.onClick = [this]()
    {
        constexpr int kitScopeChannel = 0;
        const int selectedId = microtonicPresetListBox.getSelectedId();
        if (selectedId < 1 || selectedId > StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
            return;

        juce::String error;
        if (!processor.recallMicrotonicStripPreset(kitScopeChannel, selectedId - 1, &error))
        {
            hostedStatusLabel.setText(
                "Patch recall failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
                juce::dontSendNotification);
            return;
        }

        hostedStatusLabel.setText(
            "Recalled patch combo slot " + juce::String(selectedId).paddedLeft('0', 2),
            juce::dontSendNotification);
    };
    styleUiButton(microtonicPresetRecallButton);
    addAndMakeVisible(microtonicPresetRecallButton);

    microtonicPresetDeleteButton.setButtonText("Delete");
    microtonicPresetDeleteButton.setTooltip("Delete selected full-kit patch preset.");
    microtonicPresetDeleteButton.onClick = [this]()
    {
        constexpr int kitScopeChannel = 0;
        const int selectedId = microtonicPresetListBox.getSelectedId();
        if (selectedId < 1 || selectedId > StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
            return;

        if (!processor.deleteMicrotonicStripPreset(kitScopeChannel, selectedId - 1))
            return;

        rebuildMicrotonicPresetList();
        hostedStatusLabel.setText(
            "Deleted patch combo slot " + juce::String(selectedId).paddedLeft('0', 2),
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

    // Patch preset controls were moved to the dedicated BeatSpace tab.
    microtonicPresetStripLabel.setVisible(false);
    microtonicPresetStripBox.setVisible(false);
    microtonicPresetListLabel.setVisible(false);
    microtonicPresetListBox.setVisible(false);
    microtonicPresetStoreButton.setVisible(false);
    microtonicPresetRecallButton.setVisible(false);
    microtonicPresetDeleteButton.setVisible(false);

    updateHostedPluginStatus();
    rebuildMicrotonicPresetList();
    refreshFromProcessor();
    globalUiReady = true;
}

GlobalControlPanel::~GlobalControlPanel()
{
    closeHostedPluginEditor();
}

BeatSpaceControlPanel::BeatSpaceControlPanel(StepVstHostAudioProcessor& p)
    : processor(p)
{
    titleLabel.setText("BEATSPACE CONTROLS", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, kTextPrimary);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);
    titleLabel.setTooltip("BeatSpace morph timing, path recording, and XY table preview.");

    beatSpaceViewModeLabel.setText("Size", juce::dontSendNotification);
    beatSpaceViewModeLabel.setJustificationType(juce::Justification::centredRight);
    beatSpaceViewModeLabel.setColour(juce::Label::textColourId, kTextMuted);
    beatSpaceViewModeLabel.setTooltip("BeatSpace panel footprint: Compact keeps strip steps visible, Focus gives the map more room.");
    addAndMakeVisible(beatSpaceViewModeLabel);

    styleUiCombo(beatSpaceViewModeBox);
    beatSpaceViewModeBox.addItem("Compact", 1);
    beatSpaceViewModeBox.addItem("Standard", 2);
    beatSpaceViewModeBox.addItem("Focus", 3);
    beatSpaceViewModeBox.setTooltip("BeatSpace panel footprint mode.");
    beatSpaceViewModeBox.onChange = [this]()
    {
        const int selectedId = beatSpaceViewModeBox.getSelectedId();
        switch (selectedId)
        {
            case 1: beatSpaceViewSizeMode = ViewSizeMode::Compact; break;
            case 3: beatSpaceViewSizeMode = ViewSizeMode::Focus; break;
            case 2:
            default: beatSpaceViewSizeMode = ViewSizeMode::Standard; break;
        }

        if (auto* editor = findParentComponentOfClass<StepVstHostAudioProcessorEditor>(); editor != nullptr)
            editor->resized();
        else
            resized();
        repaint();
    };
    beatSpaceViewModeBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(beatSpaceViewModeBox);

    beatSpaceMorphLabel.setText("Morph", juce::dontSendNotification);
    beatSpaceMorphLabel.setJustificationType(juce::Justification::centredLeft);
    beatSpaceMorphLabel.setTooltip("Smoothing time for monome BeatSpace XY moves.");
    addAndMakeVisible(beatSpaceMorphLabel);

    beatSpaceMorphSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    beatSpaceMorphSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
    beatSpaceMorphSlider.setRange(40.0, 1200.0, 10.0);
    beatSpaceMorphSlider.setValue(processor.getBeatSpaceMorphDurationMs(), juce::dontSendNotification);
    beatSpaceMorphSlider.setNumDecimalPlacesToDisplay(0);
    beatSpaceMorphSlider.setPopupDisplayEnabled(true, false, this);
    enableAltClickReset(beatSpaceMorphSlider, 160.0);
    beatSpaceMorphSlider.setTooltip("Morph speed knob. 0 ms is immediate; higher values smooth BeatSpace moves.");
    beatSpaceMorphSlider.onValueChange = [this]()
    {
        processor.setBeatSpaceMorphDurationMs(beatSpaceMorphSlider.getValue());
        if (beatSpaceUiReady)
            processor.markPersistentGlobalUserChange();
    };
    addAndMakeVisible(beatSpaceMorphSlider);

    microtonicPresetStripLabel.setVisible(false);
    microtonicPresetStripBox.setVisible(false);

    microtonicPresetListLabel.setText("Kits", juce::dontSendNotification);
    microtonicPresetListLabel.setJustificationType(juce::Justification::centredLeft);
    microtonicPresetListLabel.setTooltip("Stored full-kit snapshots (all 6 lanes, pattern excluded).");
    addAndMakeVisible(microtonicPresetListLabel);

    microtonicPresetListBox.setTooltip("Select to recall a kit, or choose a new slot to store.");
    styleUiCombo(microtonicPresetListBox);
    microtonicPresetListBox.onChange = [this]()
    {
        constexpr int kitScopeChannel = 0;
        const int selectedId = microtonicPresetListBox.getSelectedId();
        if (selectedId < 1 || selectedId > StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
            return;

        juce::String error;
        if (!processor.recallMicrotonicStripPreset(kitScopeChannel, selectedId - 1, &error))
        {
            microtonicPresetStatusLabel.setText(
                "Kit recall failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
                juce::dontSendNotification);
            return;
        }

        microtonicPresetStatusLabel.setText(
            "Recalled kit slot "
                + juce::String(selectedId).paddedLeft('0', 2),
            juce::dontSendNotification);
    };
    addAndMakeVisible(microtonicPresetListBox);

    beatPatternListLabel.setText("Beat Pattern", juce::dontSendNotification);
    beatPatternListLabel.setJustificationType(juce::Justification::centredLeft);
    beatPatternListLabel.setTooltip("Load a source groove into the currently active sub preset.");
    addAndMakeVisible(beatPatternListLabel);

    beatPatternListBox.setTooltip(
        "Choose a pattern to write into the active sub preset. "
        "32-step patterns auto-expand strip step length.");
    beatPatternListBox.setTextWhenNothingSelected("Choose pattern");
    beatPatternListBox.setTextWhenNoChoicesAvailable("No patterns available");
    styleUiCombo(beatPatternListBox);
    rebuildBeatPatternList();
    beatPatternListBox.onChange = [this]()
    {
        const int selectedId = beatPatternListBox.getSelectedId();
        if (selectedId < kBeatPatternComboBaseId)
            return;

        const int comboIndex = selectedId - kBeatPatternComboBaseId;
        if (comboIndex < 0
            || comboIndex >= static_cast<int>(beatPatternComboToPresetIndex.size()))
        {
            return;
        }
        const int presetIndex = beatPatternComboToPresetIndex[static_cast<size_t>(comboIndex)];

        juce::String error;
        if (!processor.applySiteDrumPatternPresetToCurrentSubPreset(presetIndex, &error))
        {
            microtonicPresetStatusLabel.setText(
                "Pattern load failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
                juce::dontSendNotification);
            return;
        }

        const auto& info = beatPatternPresetInfos[static_cast<size_t>(presetIndex)];
        microtonicPresetStatusLabel.setText(
            "Loaded pattern to current sub preset: " + info.name
                + " (" + juce::String(info.bpm) + " BPM)",
            juce::dontSendNotification);
        if (beatSpaceUiReady)
            processor.markPersistentGlobalUserChange();
    };
    addAndMakeVisible(beatPatternListBox);

    beatPatternStoreButton.setButtonText("Store Pattern");
    beatPatternStoreButton.setTooltip("Store the current edited pattern into the active sub preset.");
    beatPatternStoreButton.onClick = [this]()
    {
        juce::String error;
        if (!processor.storeCurrentPatternToCurrentSubPreset(&error))
        {
            microtonicPresetStatusLabel.setText(
                "Pattern store failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
                juce::dontSendNotification);
            return;
        }

        microtonicPresetStatusLabel.setText(
            "Stored current pattern to active sub preset",
            juce::dontSendNotification);
        if (beatSpaceUiReady)
            processor.markPersistentGlobalUserChange();
    };
    styleUiButton(beatPatternStoreButton, true);
    addAndMakeVisible(beatPatternStoreButton);

    beatPatternDeleteButton.setButtonText("Del Pattern");
    beatPatternDeleteButton.setTooltip("Clear the current pattern in the active sub preset.");
    beatPatternDeleteButton.onClick = [this]()
    {
        juce::String error;
        if (!processor.clearCurrentPatternInCurrentSubPreset(&error))
        {
            microtonicPresetStatusLabel.setText(
                "Pattern clear failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
                juce::dontSendNotification);
            return;
        }

        microtonicPresetStatusLabel.setText(
            "Cleared pattern in active sub preset",
            juce::dontSendNotification);
        if (beatSpaceUiReady)
            processor.markPersistentGlobalUserChange();
    };
    styleUiButton(beatPatternDeleteButton);
    addAndMakeVisible(beatPatternDeleteButton);

    microtonicPresetStoreButton.setButtonText("Store");
    microtonicPresetStoreButton.setTooltip("Store current sound combination as a kit for all lanes.");
    microtonicPresetStoreButton.onClick = [this]()
    {
        constexpr int kitScopeChannel = 0;
        const int selectedId = microtonicPresetListBox.getSelectedId();
        int preferredSlot = -1;
        if (selectedId >= 1000
                 && selectedId < (1000 + StepVstHostAudioProcessor::MicrotonicStripPresetSlots))
            preferredSlot = selectedId - 1000;
        else if (selectedId >= 1 && selectedId <= StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
        {
            // Default behavior: keep adding new kits while free slots exist.
            // If all slots are full, overwrite the selected slot instead.
            const auto usedSlots = processor.getMicrotonicStripPresetSlots(kitScopeChannel);
            const bool hasFreeSlot = usedSlots.size() < static_cast<size_t>(StepVstHostAudioProcessor::MicrotonicStripPresetSlots);
            if (!hasFreeSlot)
                preferredSlot = selectedId - 1;
        }

        int storedSlot = -1;
        juce::String error;
        if (!processor.storeCurrentMicrotonicStripPreset(kitScopeChannel, preferredSlot, {}, &storedSlot, &error))
        {
            microtonicPresetStatusLabel.setText(
                "Kit store failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
                juce::dontSendNotification);
            return;
        }

        rebuildMicrotonicPresetList();
        if (storedSlot >= 0)
            microtonicPresetListBox.setSelectedId(storedSlot + 1, juce::dontSendNotification);
        microtonicPresetStatusLabel.setText(
            "Stored kit slot "
                + juce::String(storedSlot + 1).paddedLeft('0', 2),
            juce::dontSendNotification);
        if (beatSpaceUiReady)
            processor.markPersistentGlobalUserChange();
    };
    styleUiButton(microtonicPresetStoreButton, true);
    addAndMakeVisible(microtonicPresetStoreButton);

    microtonicPresetDeleteButton.setButtonText("Del");
    microtonicPresetDeleteButton.setTooltip("Delete selected kit.");
    microtonicPresetDeleteButton.onClick = [this]()
    {
        constexpr int kitScopeChannel = 0;
        const int selectedId = microtonicPresetListBox.getSelectedId();
        if (selectedId < 1 || selectedId > StepVstHostAudioProcessor::MicrotonicStripPresetSlots)
            return;

        if (!processor.deleteMicrotonicStripPreset(kitScopeChannel, selectedId - 1))
            return;

        rebuildMicrotonicPresetList();
        microtonicPresetStatusLabel.setText(
            "Deleted kit slot "
                + juce::String(selectedId).paddedLeft('0', 2),
            juce::dontSendNotification);
        if (beatSpaceUiReady)
            processor.markPersistentGlobalUserChange();
    };
    styleUiButton(microtonicPresetDeleteButton);
    addAndMakeVisible(microtonicPresetDeleteButton);

    microtonicPresetStatusLabel.setText("Kits store and recall all lane sounds together", juce::dontSendNotification);
    microtonicPresetStatusLabel.setFont(juce::Font(juce::FontOptions(9.0f)));
    microtonicPresetStatusLabel.setJustificationType(juce::Justification::centredLeft);
    microtonicPresetStatusLabel.setColour(juce::Label::textColourId, kTextMuted);
    microtonicPresetStatusLabel.setTooltip("Status of kit actions.");
    addAndMakeVisible(microtonicPresetStatusLabel);

    beatSpaceLinkButton.setButtonText("Link");
    beatSpaceLinkButton.setClickingTogglesState(true);
    beatSpaceLinkButton.setTooltip("Link channel moves so dragging one channel keeps the BeatSpace offsets for all channels.");
    beatSpaceLinkButton.onClick = [this]()
    {
        processor.beatSpaceSetLinkAllChannels(beatSpaceLinkButton.getToggleState());
        beatSpaceLinkHandlePointValid = false;
        refreshFromProcessor();
        if (beatSpaceUiReady)
            processor.markPersistentGlobalUserChange();
    };
    styleUiButton(beatSpaceLinkButton);
    addAndMakeVisible(beatSpaceLinkButton);

    beatSpacePathPlayModeLabel.setText("Path Mode", juce::dontSendNotification);
    beatSpacePathPlayModeLabel.setJustificationType(juce::Justification::centredLeft);
    beatSpacePathPlayModeLabel.setTooltip("Global playmode for BeatSpace path playback (all channels).");
    addAndMakeVisible(beatSpacePathPlayModeLabel);

    styleUiCombo(beatSpacePathPlayModeBox);
    beatSpacePathPlayModeBox.addItem("Normal", 1);
    beatSpacePathPlayModeBox.addItem("Reverse", 2);
    beatSpacePathPlayModeBox.addItem("PingPong", 3);
    beatSpacePathPlayModeBox.addItem("Random", 4);
    beatSpacePathPlayModeBox.addItem("RandomWalk", 5);
    beatSpacePathPlayModeBox.addItem("RandomSlice", 6);
    beatSpacePathPlayModeBox.setTextWhenNothingSelected("Mixed");
    beatSpacePathPlayModeBox.setTooltip("Set one path playmode for all BeatSpace channels.");
    beatSpacePathPlayModeBox.onChange = [this]()
    {
        const int selectedId = beatSpacePathPlayModeBox.getSelectedId();
        if (selectedId <= 0)
            return;

        const auto mode = beatSpacePathPlayModeFromComboId(selectedId);
        for (int channel = 0; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
            processor.beatSpacePathSetPlayMode(channel, mode);

        refreshFromProcessor();
        if (!beatSpacePreviewBounds.isEmpty())
            repaint(beatSpacePreviewBounds.expanded(2));
        if (beatSpaceUiReady)
            processor.markPersistentGlobalUserChange();
    };
    addAndMakeVisible(beatSpacePathPlayModeBox);

    beatSpacePreviewLabel.setText("BeatSpace", juce::dontSendNotification);
    beatSpacePreviewLabel.setJustificationType(juce::Justification::centredLeft);
    beatSpacePreviewLabel.setColour(juce::Label::textColourId, kTextSecondary);
    beatSpacePreviewLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    beatSpacePreviewLabel.setTooltip("BeatSpace cursor preview from monome page.");
    addAndMakeVisible(beatSpacePreviewLabel);
    beatSpacePreviewLabel.setVisible(false);

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

    refreshFromProcessor();
    beatSpaceUiReady = true;
    startTimerHz(30);
}

void BeatSpaceControlPanel::timerCallback()
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
    const bool anyPathDrawArmed = state.pathRecordArmedChannel >= 0;

    // Keep BeatSpace playback/morph visuals live even when no mouse interaction occurs.
    if (state.anyMorphActive || anyPathActive || anyPathDrawArmed || beatSpacePathRecordingActive)
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
            + (label.isNotEmpty() ? label : ("Kit " + juce::String(clampedSlot + 1).paddedLeft('0', 2)));
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
            "New Kit " + juce::String(firstFreeSlot + 1).paddedLeft('0', 2),
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
    if (!isShowing())
        return;

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
}

void BeatSpaceControlPanel::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), kAccent, 8.0f);

    if (beatSpacePreviewBounds.isEmpty())
        return;

    const auto state = processor.getBeatSpaceVisualState();
    const bool showTextOverlay = beatSpaceTextOverlayEnabled;

    auto innerBounds = getBeatSpaceMapInnerBounds(beatSpacePreviewBounds);
    auto inner = innerBounds.toFloat();
    const auto mapProjectionBounds = getBeatSpaceMapProjectionBounds(innerBounds);
    const auto mapProjection = mapProjectionBounds.toFloat();
    const auto previewImage = processor.getBeatSpaceTablePreviewImage();
    const int tableSize = juce::jmax(1, state.tableSize);
    const int viewX = juce::jlimit(0, tableSize - 1, state.viewX);
    const int viewY = juce::jlimit(0, tableSize - 1, state.viewY);
    const int viewW = juce::jmax(1, juce::jmin(tableSize - viewX, state.viewWidth));
    const int viewH = juce::jmax(1, juce::jmin(tableSize - viewY, state.viewHeight));
    const float scaleX = mapProjection.getWidth() / static_cast<float>(viewW);
    const float scaleY = mapProjection.getHeight() / static_cast<float>(viewH);

    if (previewImage.isValid())
    {
        g.saveState();
        g.reduceClipRegion(innerBounds);
        const bool drawPerCell = (viewW <= 18 || viewH <= 18);
        if (drawPerCell)
        {
            const float cellW = mapProjection.getWidth() / static_cast<float>(viewW);
            const float cellH = mapProjection.getHeight() / static_cast<float>(viewH);
            for (int yy = 0; yy < viewH; ++yy)
            {
                for (int xx = 0; xx < viewW; ++xx)
                {
                    const auto c = previewImage.getPixelAt(viewX + xx, viewY + yy);
                    const float px = mapProjection.getX() + (static_cast<float>(xx) * cellW);
                    const float py = mapProjection.getY() + (static_cast<float>(yy) * cellH);
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
                mapProjectionBounds.getX(),
                mapProjectionBounds.getY(),
                mapProjectionBounds.getWidth(),
                mapProjectionBounds.getHeight(),
                viewX,
                viewY,
                viewW,
                viewH);
        }

        if (showTextOverlay && state.colorClustersReady)
        {
            const auto clusterOverlay = processor.getBeatSpaceClusterOverlayImage();
            if (clusterOverlay.isValid())
            {
                if (drawPerCell)
                {
                    const float cellW = mapProjection.getWidth() / static_cast<float>(viewW);
                    const float cellH = mapProjection.getHeight() / static_cast<float>(viewH);
                    for (int yy = 0; yy < viewH; ++yy)
                    {
                        for (int xx = 0; xx < viewW; ++xx)
                        {
                            const auto c = clusterOverlay.getPixelAt(viewX + xx, viewY + yy);
                            if (c.getAlpha() <= 0)
                                continue;
                            const float px = mapProjection.getX() + (static_cast<float>(xx) * cellW);
                            const float py = mapProjection.getY() + (static_cast<float>(yy) * cellH);
                            g.setColour(c);
                            g.fillRect(px, py, juce::jmax(1.0f, cellW + 0.45f), juce::jmax(1.0f, cellH + 0.45f));
                        }
                    }
                }
                else
                {
                    g.setOpacity(0.72f);
                    g.drawImage(
                        clusterOverlay,
                        mapProjectionBounds.getX(),
                        mapProjectionBounds.getY(),
                        mapProjectionBounds.getWidth(),
                        mapProjectionBounds.getHeight(),
                        viewX,
                        viewY,
                        viewW,
                        viewH);
                    g.setOpacity(1.0f);
                }
            }
        }

        if (state.confidenceOverlayEnabled)
        {
            const auto confidenceImage = processor.getBeatSpaceConfidencePreviewImage();
            if (confidenceImage.isValid())
            {
                if (drawPerCell)
                {
                    const float cellW = mapProjection.getWidth() / static_cast<float>(viewW);
                    const float cellH = mapProjection.getHeight() / static_cast<float>(viewH);
                    for (int yy = 0; yy < viewH; ++yy)
                    {
                        for (int xx = 0; xx < viewW; ++xx)
                        {
                            const auto c = confidenceImage.getPixelAt(viewX + xx, viewY + yy);
                            const float alpha = 0.10f * static_cast<float>(c.getRed()) / 255.0f;
                            if (alpha <= 0.001f)
                                continue;
                            const float px = mapProjection.getX() + (static_cast<float>(xx) * cellW);
                            const float py = mapProjection.getY() + (static_cast<float>(yy) * cellH);
                            g.setColour(juce::Colour(0xff8aa4bf).withAlpha(alpha));
                            g.fillRect(px, py, juce::jmax(1.0f, cellW + 0.45f), juce::jmax(1.0f, cellH + 0.45f));
                        }
                    }
                }
                else
                {
                    g.setOpacity(0.12f);
                    g.drawImage(
                        confidenceImage,
                        mapProjectionBounds.getX(),
                        mapProjectionBounds.getY(),
                        mapProjectionBounds.getWidth(),
                        mapProjectionBounds.getHeight(),
                        viewX,
                        viewY,
                        viewW,
                        viewH);
                    g.setOpacity(1.0f);
                }
            }
        }

        g.restoreState();
    }
    else
    {
        g.setColour(juce::Colour(0xfff6f9fc));
        g.fillRect(inner);
    }
    g.setColour(juce::Colour(0xffc2d2e2).withAlpha(0.74f));
    g.drawRect(inner, 1.0f);

    if (!state.decoderReady)
    {
        g.setColour(kTextMuted);
        g.drawFittedText("decoder missing", beatSpacePreviewBounds, juce::Justification::centred, 1);
        return;
    }

    if (!previewImage.isValid())
    {
        auto loadingBounds = inner.reduced(10.0f, 10.0f);
        if (loadingBounds.getWidth() < 110.0f || loadingBounds.getHeight() < 42.0f)
            loadingBounds = inner;

        g.setColour(juce::Colour(0xff0e2335).withAlpha(0.82f));
        g.fillRoundedRectangle(loadingBounds, 5.0f);
        g.setColour(kAccent.withAlpha(0.86f));
        g.drawRoundedRectangle(loadingBounds, 5.0f, 1.2f);

        auto textArea = loadingBounds.toNearestInt().reduced(8, 6);
        auto titleArea = textArea.removeFromTop(16);
        g.setColour(juce::Colour(0xfff4fbff));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText("Loading BeatSpace table...", titleArea, juce::Justification::centred);

        const juce::String detail = state.statusMessage.isNotEmpty()
            ? state.statusMessage
            : juce::String("Preparing preview from local table cache...");
        g.setColour(juce::Colour(0xffd5e5f6).withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        g.drawFittedText(detail, textArea.removeFromTop(20), juce::Justification::centred, 2);

        auto barArea = textArea.removeFromTop(8).toFloat();
        g.setColour(juce::Colour(0xff243b53).withAlpha(0.9f));
        g.fillRoundedRectangle(barArea, 3.0f);
        g.setColour(juce::Colour(0xff5a7490).withAlpha(0.95f));
        g.drawRoundedRectangle(barArea, 3.0f, 0.9f);

        const float phase = static_cast<float>(
            std::fmod(juce::Time::getMillisecondCounterHiRes() * 0.00115, 1.0));
        const float segmentW = juce::jmax(16.0f, barArea.getWidth() * 0.30f);
        const float startX = barArea.getX() + ((barArea.getWidth() + segmentW) * phase) - segmentW;
        auto segment = juce::Rectangle<float>(startX, barArea.getY(), segmentW, barArea.getHeight())
            .getIntersection(barArea);
        juce::ColourGradient grad(
            kAccent.withAlpha(0.15f), segment.getX(), segment.getY(),
            kAccent.withAlpha(0.98f), segment.getRight(), segment.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(segment, 2.5f);
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
    constexpr std::array<const char*, StepVstHostAudioProcessor::BeatSpaceChannels> kCategoryLongNames {
        "Kick", "Snare", "Closed Hat", "Open Hat", "Percussion", "Misc"
    };
    g.saveState();
    g.reduceClipRegion(innerBounds);

    // Keep the default view close to the original BeatSpace map; optional text
    // overlay re-enables analytical guides.
    const int coarseStep = (viewW <= 16 || viewH <= 16) ? 2 : ((viewW <= 32 || viewH <= 32) ? 4 : 8);
    if (showTextOverlay && coarseStep > 0)
    {
        for (int gx = 0; gx <= viewW; gx += coarseStep)
        {
            const float px = mapProjection.getX() + (static_cast<float>(gx) * scaleX);
            g.setColour(juce::Colour(0xfff5fbff).withAlpha((gx % (coarseStep * 2) == 0) ? 0.16f : 0.08f));
            g.drawVerticalLine(static_cast<int>(std::round(px)), inner.getY(), inner.getBottom());
        }
        for (int gy = 0; gy <= viewH; gy += coarseStep)
        {
            const float py = mapProjection.getY() + (static_cast<float>(gy) * scaleY);
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
        const float centerX = mapProjection.getX() + ((static_cast<float>(relAnchorX) + 0.5f) * scaleX);
        const float centerY = mapProjection.getY() + ((static_cast<float>(relAnchorY) + 0.5f) * scaleY);
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

    if (showTextOverlay)
    {
        for (int category = 0; category < StepVstHostAudioProcessor::BeatSpaceChannels; ++category)
            drawCategoryRegion(category);
    }

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
        return getStripColor(clampedChannel);
    };

    auto pointToPixel = [&](const juce::Point<int>& p, bool clampToView = true) -> juce::Point<float>
    {
        float relX = static_cast<float>(p.x - viewX);
        float relY = static_cast<float>(p.y - viewY);
        if (clampToView)
        {
            relX = juce::jlimit(0.0f, static_cast<float>(viewW - 1), relX);
            relY = juce::jlimit(0.0f, static_cast<float>(viewH - 1), relY);
        }
        return {
            mapProjection.getX() + ((relX + 0.5f) * scaleX),
            mapProjection.getY() + ((relY + 0.5f) * scaleY)
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
                const auto headPoint = state.channelPoints[idx];
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
    auto linkHandlePoint = beatSpaceLinkHandlePointValid
        ? beatSpaceLinkHandlePoint
        : visualPointForChannel(selectedChannel);

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

    const float dotRadiusUnselected = state.linkAllChannels ? 2.25f : 3.4f;
    const float dotRadiusSelected = state.linkAllChannels ? 3.2f : 4.8f;

    for (int c = 0; c < StepVstHostAudioProcessor::BeatSpaceChannels; ++c)
    {
        if (c == selectedChannel)
            continue;
        drawPoint(visualPointForChannel(c), channelColour(c).withAlpha(0.90f), dotRadiusUnselected, false);
    }
    const auto selectedPoint = visualPointForChannel(selectedChannel);
    const auto selectedColour = channelColour(selectedChannel);
    drawPoint(selectedPoint, selectedColour, dotRadiusSelected, true);

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
                + (beatSpaceBubbleCoordinateOverlayEnabled
                    ? ("  X" + juce::String(selectedPoint.x)
                        + " Y" + juce::String(selectedPoint.y))
                    : juce::String())
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
        const auto masterPoint = visualPointForChannel(selectedChannel);
        const auto master = pointToPixel(masterPoint, true);
        const auto linkPoint = linkHandlePoint;
        const auto linkPixelRaw = pointToPixel(linkPoint, true);
        const auto linkPixel = getBeatSpaceLinkHandleDisplayPixel(
            linkPixelRaw,
            master,
            inner,
            selectedChannel);

        const float linkDisplayDx = linkPixel.x - linkPixelRaw.x;
        const float linkDisplayDy = linkPixel.y - linkPixelRaw.y;
        if ((linkDisplayDx * linkDisplayDx) + (linkDisplayDy * linkDisplayDy) > 0.25f)
        {
            g.setColour(selectedColour.withAlpha(0.52f));
            g.drawLine(master.x, master.y, linkPixel.x, linkPixel.y, 0.8f);
        }

        g.setColour(selectedColour.withAlpha(0.92f));
        g.fillEllipse(linkPixel.x - 10.4f, linkPixel.y - 10.4f, 20.8f, 20.8f);
        g.setColour(selectedColour.withAlpha(0.98f));
        g.drawEllipse(linkPixel.x - 9.0f, linkPixel.y - 9.0f, 18.0f, 18.0f, 2.2f);
        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.drawEllipse(linkPixel.x - 7.2f, linkPixel.y - 7.2f, 14.4f, 14.4f, 1.2f);
        g.setColour(juce::Colour(0xffeef7ff).withAlpha(0.96f));
        g.setFont(juce::Font(juce::FontOptions(8.4f, juce::Font::bold)));
        g.drawFittedText("L",
                         juce::Rectangle<int>(static_cast<int>(std::lround(linkPixel.x - 7.0f)),
                                              static_cast<int>(std::lround(linkPixel.y - 7.0f)),
                                              14,
                                              14),
                         juce::Justification::centred,
                         1);

        g.setColour(selectedColour.withAlpha(0.24f));
        g.fillEllipse(master.x - 10.0f, master.y - 10.0f, 20.0f, 20.0f);
        g.setColour(juce::Colour(0xff0d2438).withAlpha(0.88f));
        g.drawEllipse(master.x - 7.4f, master.y - 7.4f, 14.8f, 14.8f, 2.2f);
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.drawEllipse(master.x - 6.0f, master.y - 6.0f, 12.0f, 12.0f, 1.3f);

        for (int i = 0; i < StepVstHostAudioProcessor::BeatSpaceChannels; ++i)
        {
            const bool isSelectedConnection = (i == selectedChannel);
            const auto point = visualPointForChannel(i);
            auto p = pointToPixel(point, true);
            const auto c = channelColour(i);

            const float dxToHandle = p.x - linkPixel.x;
            const float dyToHandle = p.y - linkPixel.y;
            if ((dxToHandle * dxToHandle) + (dyToHandle * dyToHandle) < 9.0f)
            {
                const float spokeLength = isSelectedConnection ? 12.0f : 9.0f;
                const float angle = juce::MathConstants<float>::twoPi
                    * (static_cast<float>(i) / static_cast<float>(StepVstHostAudioProcessor::BeatSpaceChannels));
                p.x = linkPixel.x + (std::cos(angle) * spokeLength);
                p.y = linkPixel.y + (std::sin(angle) * spokeLength);
            }

            g.setColour(juce::Colour(0xff091f31).withAlpha(isSelectedConnection ? 0.95f : 0.91f));
            g.drawLine(linkPixel.x, linkPixel.y, p.x, p.y, isSelectedConnection ? 3.4f : 2.9f);
            g.setColour((isSelectedConnection ? selectedColour : c).withAlpha(0.995f));
            g.drawLine(linkPixel.x, linkPixel.y, p.x, p.y, isSelectedConnection ? 2.2f : 1.8f);

            if (isSelectedConnection)
                continue;

            g.setColour(juce::Colour(0xff08273d).withAlpha(0.85f));
            g.drawEllipse(p.x - 7.6f, p.y - 7.6f, 15.2f, 15.2f, 2.4f);
            g.setColour(c.withAlpha(0.99f));
            g.drawEllipse(p.x - 6.4f, p.y - 6.4f, 12.8f, 12.8f, 1.6f);

            const int dx = point.x - masterPoint.x;
            const int dy = point.y - masterPoint.y;
            if (beatSpaceBubbleCoordinateOverlayEnabled && (showTextOverlay || dx != 0 || dy != 0))
            {
                const juce::String offsetText = (dx >= 0 ? "+" : "") + juce::String(dx)
                    + "," + (dy >= 0 ? "+" : "") + juce::String(dy);
                auto offsetTag = juce::Rectangle<float>(
                    p.x + 5.0f,
                    p.y - 11.0f,
                    juce::jmax(26.0f, 12.0f + (5.6f * static_cast<float>(offsetText.length()))),
                    12.0f).constrainedWithin(inner.reduced(1.0f));
                g.setColour(juce::Colour(0xff0a2439).withAlpha(0.86f));
                g.fillRoundedRectangle(offsetTag, 2.1f);
                g.setColour(c.withAlpha(0.98f));
                g.drawRoundedRectangle(offsetTag, 2.1f, 1.3f);
                g.setColour(juce::Colour(0xfff2f9ff).withAlpha(0.99f));
                g.setFont(juce::Font(juce::FontOptions(7.6f, juce::Font::bold)));
                g.drawFittedText(offsetText, offsetTag.toNearestInt(), juce::Justification::centred, 1);
            }
        }

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
    g.restoreState();

    // Active viewport is now the main preview; keep a subtle frame to make this explicit.
    g.setColour(juce::Colour(0xfff5a65b).withAlpha(0.9f));
    g.drawRect(inner, 1.0f);
}

void BeatSpaceControlPanel::mouseDown(const juce::MouseEvent& e)
{
    beatSpaceDragChannel = -1;
    beatSpaceDragSingleChannel = false;
    beatSpaceDragLinkHandle = false;
    beatSpaceDragLinkAnchorChannel = -1;
    beatSpaceDragLinkAnchorStartPoint = { 0, 0 };
    beatSpaceDragLinkStartPoint = { 0, 0 };

    if (beatSpacePreviewBounds.isEmpty())
        return;

    const auto state = processor.getBeatSpaceVisualState();
    if (!state.decoderReady)
        return;

    auto innerBounds = getBeatSpaceMapInnerBounds(beatSpacePreviewBounds);
    if (!innerBounds.contains(e.getPosition()))
        return;
    const auto mapProjectionBounds = getBeatSpaceMapProjectionBounds(innerBounds);

    auto pointFromLocal = [&](juce::Point<int> localPos) -> juce::Point<int>
    {
        const int tableSize = juce::jmax(1, state.tableSize);
        const int viewX = juce::jlimit(0, tableSize - 1, state.viewX);
        const int viewY = juce::jlimit(0, tableSize - 1, state.viewY);
        const int viewW = juce::jmax(1, juce::jmin(tableSize - viewX, state.viewWidth));
        const int viewH = juce::jmax(1, juce::jmin(tableSize - viewY, state.viewHeight));
        const int relX = juce::jlimit(
            0, mapProjectionBounds.getWidth() - 1, localPos.x - mapProjectionBounds.getX());
        const int relY = juce::jlimit(
            0, mapProjectionBounds.getHeight() - 1, localPos.y - mapProjectionBounds.getY());
        const float tx = (mapProjectionBounds.getWidth() > 1)
            ? static_cast<float>(relX) / static_cast<float>(mapProjectionBounds.getWidth() - 1)
            : 0.0f;
        const float ty = (mapProjectionBounds.getHeight() > 1)
            ? static_cast<float>(relY) / static_cast<float>(mapProjectionBounds.getHeight() - 1)
            : 0.0f;
        return {
            viewX + juce::jlimit(0, viewW - 1, static_cast<int>(std::round(tx * static_cast<float>(juce::jmax(0, viewW - 1))))),
            viewY + juce::jlimit(0, viewH - 1, static_cast<int>(std::round(ty * static_cast<float>(juce::jmax(0, viewH - 1)))))
        };
    };

    auto applyPointFromLocal = [&](juce::Point<int> localPos)
    {
        const int relX = juce::jlimit(
            0, mapProjectionBounds.getWidth() - 1, localPos.x - mapProjectionBounds.getX());
        const int relY = juce::jlimit(
            0, mapProjectionBounds.getHeight() - 1, localPos.y - mapProjectionBounds.getY());
        if (state.linkAllChannels)
        {
            const int selectedChannel = juce::jlimit(
                0, StepVstHostAudioProcessor::BeatSpaceChannels - 1, state.selectedChannel);
            processor.beatSpaceSetChannelPoint(
                selectedChannel,
                pointFromLocal(localPos),
                false,
                true);
        }
        else
        {
            processor.beatSpaceSetPointFromGridCell(
                relX,
                relY,
                mapProjectionBounds.getWidth(),
                mapProjectionBounds.getHeight());
        }
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
        const float scaleX = static_cast<float>(mapProjectionBounds.getWidth()) / static_cast<float>(viewW);
        const float scaleY = static_cast<float>(mapProjectionBounds.getHeight()) / static_cast<float>(viewH);
        return {
            static_cast<float>(mapProjectionBounds.getX()) + ((static_cast<float>(relX) + 0.5f) * scaleX),
            static_cast<float>(mapProjectionBounds.getY()) + ((static_cast<float>(relY) + 0.5f) * scaleY)
        };
    };

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
    auto clampToTable = [&](juce::Point<int> p) -> juce::Point<int>
    {
        const int tableSize = juce::jmax(1, state.tableSize);
        p.x = juce::jlimit(0, tableSize - 1, p.x);
        p.y = juce::jlimit(0, tableSize - 1, p.y);
        return p;
    };

    if (e.mods.isLeftButtonDown() && !e.mods.isPopupMenu())
    {
        if (e.mods.isShiftDown() && state.zoomLevel > 0)
        {
            beatSpaceShiftPanActive = true;
            beatSpaceShiftPanLastMouse = e.getPosition();
            beatSpaceShiftPanAccumX = 0.0f;
            beatSpaceShiftPanAccumY = 0.0f;
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

        int selectedChannelForHit = -1;
        bool linkPixelForHitValid = false;
        juce::Point<float> linkPixelForHit;
        if (state.linkAllChannels)
        {
            const int selectedChannel = juce::jlimit(
                0,
                StepVstHostAudioProcessor::BeatSpaceChannels - 1,
                state.selectedChannel);
            selectedChannelForHit = selectedChannel;
            auto handlePoint = beatSpaceLinkHandlePointValid
                ? beatSpaceLinkHandlePoint
                : visualPointForChannel(selectedChannel);
            handlePoint = clampToTable(handlePoint);
            const auto masterPoint = visualPointForChannel(selectedChannel);
            const auto handlePixel = getBeatSpaceLinkHandleDisplayPixel(
                pointToPreviewPixel(handlePoint),
                pointToPreviewPixel(masterPoint),
                mapProjectionBounds.toFloat(),
                selectedChannel);
            linkPixelForHit = handlePixel;
            linkPixelForHitValid = true;
            const float hx = handlePixel.x - e.position.x;
            const float hy = handlePixel.y - e.position.y;
            const float d2 = (hx * hx) + (hy * hy);
            if (d2 <= (12.0f * 12.0f))
            {
                beatSpaceDragLinkHandle = true;
                beatSpaceDragChannel = -1;
                beatSpaceDragSingleChannel = false;
                beatSpaceDragLinkAnchorChannel = selectedChannel;
                processor.beatSpaceSelectChannel(selectedChannel);
                beatSpaceDragLinkAnchorStartPoint =
                    state.channelPoints[static_cast<size_t>(beatSpaceDragLinkAnchorChannel)];
                const auto dragStartPoint = pointFromLocal(e.getPosition());
                beatSpaceDragLinkStartPoint = dragStartPoint;
                beatSpaceLinkHandlePoint = beatSpaceDragLinkAnchorStartPoint;
                beatSpaceLinkHandlePointValid = true;
                refreshFromProcessor();
                repaint(beatSpacePreviewBounds.expanded(2));
                return;
            }
        }

        int nearestChannel = -1;
        float nearestDistanceSq = std::numeric_limits<float>::max();
        for (int c = 0; c < StepVstHostAudioProcessor::BeatSpaceChannels; ++c)
        {
            const auto point = visualPointForChannel(c);
            auto pixel = pointToPreviewPixel(point);
            if (linkPixelForHitValid && c != selectedChannelForHit)
            {
                const float dxToHandle = pixel.x - linkPixelForHit.x;
                const float dyToHandle = pixel.y - linkPixelForHit.y;
                if ((dxToHandle * dxToHandle) + (dyToHandle * dyToHandle) < 9.0f)
                {
                    const float spokeLength = 9.0f;
                    const float angle = juce::MathConstants<float>::twoPi
                        * (static_cast<float>(c)
                           / static_cast<float>(StepVstHostAudioProcessor::BeatSpaceChannels));
                    pixel.x = linkPixelForHit.x + (std::cos(angle) * spokeLength);
                    pixel.y = linkPixelForHit.y + (std::sin(angle) * spokeLength);
                }
            }
            const float dx = pixel.x - e.position.x;
            const float dy = pixel.y - e.position.y;
            const float d2 = (dx * dx) + (dy * dy);
            if (d2 < nearestDistanceSq)
            {
                nearestDistanceSq = d2;
                nearestChannel = c;
            }
        }
        if (nearestChannel >= 0 && nearestDistanceSq <= (11.0f * 11.0f))
        {
            beatSpaceDragChannel = nearestChannel;
            beatSpaceDragSingleChannel = state.linkAllChannels;
            beatSpaceDragLinkHandle = false;
            beatSpaceDragLinkAnchorChannel = -1;
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
    menu.addItem(9003, "Bubble Coordinates", true, beatSpaceBubbleCoordinateOverlayEnabled);
    menu.addItem(9004, "Constrain To Category Fields", true, processor.isBeatSpaceCategoryConstrainEnabled());
    menu.addItem(9005,
                 "Drawn Paths Respect Category Fields",
                 true,
                 processor.isBeatSpacePathCategoryConstrainEnabled());
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
    else if (result == 9003)
    {
        beatSpaceBubbleCoordinateOverlayEnabled = !beatSpaceBubbleCoordinateOverlayEnabled;
    }
    else if (result == 9004)
    {
        processor.setBeatSpaceCategoryConstrainEnabled(!processor.isBeatSpaceCategoryConstrainEnabled());
    }
    else if (result == 9005)
    {
        processor.setBeatSpacePathCategoryConstrainEnabled(!processor.isBeatSpacePathCategoryConstrainEnabled());
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

void BeatSpaceControlPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (!e.mods.isLeftButtonDown())
    {
        beatSpaceDragChannel = -1;
        beatSpaceDragSingleChannel = false;
        beatSpaceDragLinkHandle = false;
        beatSpaceDragLinkAnchorChannel = -1;
        beatSpaceDragLinkAnchorStartPoint = { 0, 0 };
        beatSpaceDragLinkStartPoint = { 0, 0 };
        beatSpaceShiftPanActive = false;
        beatSpaceShiftPanAccumX = 0.0f;
        beatSpaceShiftPanAccumY = 0.0f;
        return;
    }
    if (beatSpacePreviewBounds.isEmpty())
        return;

    const auto state = processor.getBeatSpaceVisualState();
    if (!state.decoderReady)
        return;

    auto innerBounds = getBeatSpaceMapInnerBounds(beatSpacePreviewBounds);
    const bool insidePreview = innerBounds.contains(e.getPosition());
    const auto mapProjectionBounds = getBeatSpaceMapProjectionBounds(innerBounds);
    const bool exceededDragThreshold = e.getDistanceFromDragStart() > 2;

    const auto pointFromLocal = [&](juce::Point<int> localPos, bool allowOverflow) -> juce::Point<int>
    {
        const int tableSize = juce::jmax(1, state.tableSize);
        const int viewX = juce::jlimit(0, tableSize - 1, state.viewX);
        const int viewY = juce::jlimit(0, tableSize - 1, state.viewY);
        const int viewW = juce::jmax(1, juce::jmin(tableSize - viewX, state.viewWidth));
        const int viewH = juce::jmax(1, juce::jmin(tableSize - viewY, state.viewHeight));
        const int relXRaw = localPos.x - mapProjectionBounds.getX();
        const int relYRaw = localPos.y - mapProjectionBounds.getY();
        const int relX = allowOverflow
            ? relXRaw
            : juce::jlimit(0, mapProjectionBounds.getWidth() - 1, relXRaw);
        const int relY = allowOverflow
            ? relYRaw
            : juce::jlimit(0, mapProjectionBounds.getHeight() - 1, relYRaw);
        const float tx = (mapProjectionBounds.getWidth() > 1)
            ? static_cast<float>(relX) / static_cast<float>(mapProjectionBounds.getWidth() - 1)
            : 0.0f;
        const float ty = (mapProjectionBounds.getHeight() > 1)
            ? static_cast<float>(relY) / static_cast<float>(mapProjectionBounds.getHeight() - 1)
            : 0.0f;
        return {
            viewX + static_cast<int>(std::round(tx * static_cast<float>(juce::jmax(0, viewW - 1)))),
            viewY + static_cast<int>(std::round(ty * static_cast<float>(juce::jmax(0, viewH - 1))))
        };
    };

    if (beatSpacePathRecordingActive && beatSpacePathRecordingChannel >= 0)
    {
        processor.beatSpacePathRecordAppendPoint(
            beatSpacePathRecordingChannel,
            pointFromLocal(e.getPosition(), false));
        refreshFromProcessor();
        repaint(beatSpacePreviewBounds.expanded(2));
        return;
    }

    if (beatSpaceShiftPanActive || (e.mods.isShiftDown() && state.zoomLevel > 0))
    {
        beatSpaceShiftPanActive = true;
        const auto mousePos = e.getPosition();
        const int dx = mousePos.x - beatSpaceShiftPanLastMouse.x;
        const int dy = mousePos.y - beatSpaceShiftPanLastMouse.y;
        beatSpaceShiftPanLastMouse = mousePos;

        beatSpaceShiftPanAccumX += static_cast<float>(dx);
        beatSpaceShiftPanAccumY += static_cast<float>(dy);

        constexpr float kDragPanThreshold = 18.0f;
        int panX = 0;
        int panY = 0;
        while (beatSpaceShiftPanAccumX >= kDragPanThreshold)
        {
            --panX;
            beatSpaceShiftPanAccumX -= kDragPanThreshold;
        }
        while (beatSpaceShiftPanAccumX <= -kDragPanThreshold)
        {
            ++panX;
            beatSpaceShiftPanAccumX += kDragPanThreshold;
        }
        while (beatSpaceShiftPanAccumY >= kDragPanThreshold)
        {
            --panY;
            beatSpaceShiftPanAccumY -= kDragPanThreshold;
        }
        while (beatSpaceShiftPanAccumY <= -kDragPanThreshold)
        {
            ++panY;
            beatSpaceShiftPanAccumY += kDragPanThreshold;
        }

        if (panX != 0 || panY != 0)
        {
            processor.beatSpacePan(panX, panY);
            refreshFromProcessor();
            repaint(beatSpacePreviewBounds.expanded(2));
        }
        return;
    }

    if (beatSpaceDragLinkHandle && beatSpaceDragLinkAnchorChannel >= 0)
    {
        const auto mousePoint = pointFromLocal(e.getPosition(), false);
        const juce::Point<int> desiredDelta {
            mousePoint.x - beatSpaceDragLinkStartPoint.x,
            mousePoint.y - beatSpaceDragLinkStartPoint.y
        };
        auto targetAnchor = beatSpaceDragLinkAnchorStartPoint;
        targetAnchor.x += desiredDelta.x;
        targetAnchor.y += desiredDelta.y;
        processor.beatSpaceSetChannelPoint(
            beatSpaceDragLinkAnchorChannel,
            targetAnchor,
            true,
            !exceededDragThreshold);
        beatSpaceLinkHandlePoint = mousePoint;
        beatSpaceLinkHandlePointValid = true;
    }
    else
    if (beatSpaceDragChannel >= 0)
    {
        processor.beatSpaceSetChannelPoint(
            beatSpaceDragChannel,
            pointFromLocal(e.getPosition(), true),
            !beatSpaceDragSingleChannel,
            !exceededDragThreshold);
    }
    else
    {
        if (!insidePreview)
            return;

        if (state.linkAllChannels)
        {
            const int selectedChannel = juce::jlimit(
                0, StepVstHostAudioProcessor::BeatSpaceChannels - 1, state.selectedChannel);
            processor.beatSpaceSetChannelPoint(
                selectedChannel,
                pointFromLocal(e.getPosition(), false),
                false,
                !exceededDragThreshold);
        }
        else
        {
            const int relX = juce::jlimit(
                0, mapProjectionBounds.getWidth() - 1, e.x - mapProjectionBounds.getX());
            const int relY = juce::jlimit(
                0, mapProjectionBounds.getHeight() - 1, e.y - mapProjectionBounds.getY());
            processor.beatSpaceSetPointFromGridCell(
                relX,
                relY,
                mapProjectionBounds.getWidth(),
                mapProjectionBounds.getHeight());
        }
    }
    repaint(beatSpacePreviewBounds.expanded(2));
}

void BeatSpaceControlPanel::mouseUp(const juce::MouseEvent& e)
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
    beatSpaceDragLinkHandle = false;
    beatSpaceDragLinkAnchorChannel = -1;
    beatSpaceDragLinkAnchorStartPoint = { 0, 0 };
    beatSpaceDragLinkStartPoint = { 0, 0 };
    beatSpaceShiftPanActive = false;
    beatSpaceShiftPanAccumX = 0.0f;
    beatSpaceShiftPanAccumY = 0.0f;

    if (!beatSpacePathRecordingActive || beatSpacePathRecordingChannel < 0)
        return;

    processor.beatSpacePathRecordFinishAndPlay(beatSpacePathRecordingChannel);
    beatSpacePathRecordingActive = false;
    beatSpacePathRecordingChannel = -1;
    refreshFromProcessor();
    if (!beatSpacePreviewBounds.isEmpty())
        repaint(beatSpacePreviewBounds.expanded(2));
}

void BeatSpaceControlPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (beatSpacePreviewBounds.isEmpty())
    {
        beatSpaceWheelZoomAccumulator = 0.0f;
        beatSpaceWheelPanAccumulatorX = 0.0f;
        beatSpaceWheelPanAccumulatorY = 0.0f;
        return;
    }

    if (!beatSpacePreviewBounds.contains(e.getPosition()))
    {
        beatSpaceWheelZoomAccumulator = 0.0f;
        beatSpaceWheelPanAccumulatorX = 0.0f;
        beatSpaceWheelPanAccumulatorY = 0.0f;
        return;
    }

    const auto state = processor.getBeatSpaceVisualState();
    const bool zoomModifierDown = e.mods.isCommandDown() || e.mods.isCtrlDown();
    const float absX = std::abs(wheel.deltaX);
    const float absY = std::abs(wheel.deltaY);
    const bool horizontalPanGesture = state.zoomLevel > 0
        && wheel.isSmooth
        && !zoomModifierDown
        && (absX > (absY * 1.15f));
    const bool verticalPanGesture = state.zoomLevel > 0
        && wheel.isSmooth
        && !zoomModifierDown
        && e.mods.isShiftDown()
        && (absY > (absX * 1.15f));
    if (horizontalPanGesture || verticalPanGesture)
    {
        constexpr float kSwipePanThreshold = 0.16f;
        beatSpaceWheelPanAccumulatorX += horizontalPanGesture ? wheel.deltaX : 0.0f;
        beatSpaceWheelPanAccumulatorY += verticalPanGesture ? wheel.deltaY : 0.0f;
        beatSpaceWheelZoomAccumulator = 0.0f;

        int panX = 0;
        int panY = 0;
        while (beatSpaceWheelPanAccumulatorX >= kSwipePanThreshold)
        {
            ++panX;
            beatSpaceWheelPanAccumulatorX -= kSwipePanThreshold;
        }
        while (beatSpaceWheelPanAccumulatorX <= -kSwipePanThreshold)
        {
            --panX;
            beatSpaceWheelPanAccumulatorX += kSwipePanThreshold;
        }
        while (beatSpaceWheelPanAccumulatorY >= kSwipePanThreshold)
        {
            ++panY;
            beatSpaceWheelPanAccumulatorY -= kSwipePanThreshold;
        }
        while (beatSpaceWheelPanAccumulatorY <= -kSwipePanThreshold)
        {
            --panY;
            beatSpaceWheelPanAccumulatorY += kSwipePanThreshold;
        }

        if (panX == 0 && panY == 0)
            return;

        processor.beatSpacePan(panX, panY);
        refreshFromProcessor();
        repaint(beatSpacePreviewBounds.expanded(2));
        return;
    }

    beatSpaceWheelPanAccumulatorX = 0.0f;
    beatSpaceWheelPanAccumulatorY = 0.0f;
    const float wheelDelta = (std::abs(wheel.deltaY) >= std::abs(wheel.deltaX))
        ? wheel.deltaY
        : wheel.deltaX;
    if (std::abs(wheelDelta) < 0.001f)
        return;

    constexpr float kWheelZoomThreshold = 0.12f;
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
    constexpr int spacing = 6;

    auto masterArea = controlsArea.removeFromLeft(56);
    masterVolumeLabel.setBounds(masterArea.removeFromTop(16));
    masterArea.removeFromTop(2);
    masterVolumeSlider.setBounds(masterArea);
    controlsArea.removeFromLeft(spacing);

    auto controlRows = controlsArea;
    auto selectRow = controlRows.removeFromTop(52);
    auto swingArea = selectRow.removeFromLeft(108);
    swingDivisionLabel.setBounds(swingArea.removeFromTop(16));
    swingArea.removeFromTop(2);
    swingDivisionBox.setBounds(swingArea.removeFromTop(30));
    selectRow.removeFromLeft(spacing);

    auto quantizeArea = selectRow.removeFromLeft(108);
    quantizeLabel.setBounds(quantizeArea.removeFromTop(16));
    quantizeArea.removeFromTop(2);
    quantizeSelector.setBounds(quantizeArea.removeFromTop(30));
    selectRow.removeFromLeft(spacing);

    auto momentaryArea = selectRow.removeFromLeft(110);
    momentaryArea.removeFromTop(16);
    momentaryToggle.setBounds(momentaryArea.removeFromTop(26));

    controlRows.removeFromTop(4);
    auto keyRow = controlRows.removeFromTop(52);
    auto keyToggleArea = keyRow.removeFromLeft(98);
    keyToggleArea.removeFromTop(16);
    kitScaleToggle.setBounds(keyToggleArea.removeFromTop(26));
    keyRow.removeFromLeft(spacing);

    auto rootArea = keyRow.removeFromLeft(88);
    kitScaleRootLabel.setBounds(rootArea.removeFromTop(16));
    rootArea.removeFromTop(2);
    kitScaleRootBox.setBounds(rootArea.removeFromTop(30));
    keyRow.removeFromLeft(spacing);

    auto modeArea = keyRow.removeFromLeft(130);
    kitScaleModeLabel.setBounds(modeArea.removeFromTop(16));
    modeArea.removeFromTop(2);
    kitScaleModeBox.setBounds(modeArea.removeFromTop(30));

}

void GlobalControlPanel::refreshFromProcessor()
{
    swingDivisionBox.setSelectedId(processor.getSwingDivisionSelection() + 1, juce::dontSendNotification);
    momentaryToggle.setToggleState(processor.isControlPageMomentary(), juce::dontSendNotification);

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
}

void BeatSpaceControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    auto titleRow = bounds.removeFromTop(20);
    auto headerControls = titleRow.removeFromRight(168);
    beatSpaceViewModeBox.setBounds(headerControls.removeFromRight(122));
    headerControls.removeFromRight(4);
    beatSpaceViewModeLabel.setBounds(headerControls);
    titleLabel.setBounds(titleRow);
    bounds.removeFromTop(4);

    auto contentArea = bounds;
    constexpr int spacing = 6;
    beatSpacePreviewBounds = {};

    double previewRatio = 0.76;
    switch (beatSpaceViewSizeMode)
    {
        case ViewSizeMode::Compact: previewRatio = 0.56; break;
        case ViewSizeMode::Focus: previewRatio = 0.88; break;
        case ViewSizeMode::Standard:
        default: previewRatio = 0.76; break;
    }

    beatSpaceMorphLabel.setVisible(true);
    beatSpaceMorphSlider.setVisible(true);
    microtonicPresetListLabel.setVisible(true);
    microtonicPresetListBox.setVisible(true);
    microtonicPresetStoreButton.setVisible(true);
    microtonicPresetDeleteButton.setVisible(true);
    beatPatternListLabel.setVisible(true);
    beatPatternListBox.setVisible(true);
    beatPatternStoreButton.setVisible(true);
    beatPatternDeleteButton.setVisible(true);
    microtonicPresetStatusLabel.setVisible(true);
    beatSpacePreviewLabel.setVisible(false);

    auto layoutPathButtonsInRow = [&](juce::Rectangle<int> row)
    {
        const int pathGap = 3;
        const int totalGap = pathGap * (StepVstHostAudioProcessor::BeatSpaceChannels - 1);
        const int buttonWidth = juce::jmax(
            22,
            (row.getWidth() - totalGap) / StepVstHostAudioProcessor::BeatSpaceChannels);
        for (int channel = 0; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
        {
            auto& button = beatSpacePathButtons[static_cast<size_t>(channel)];
            button.setBounds(row.removeFromLeft(buttonWidth));
            if (channel < (StepVstHostAudioProcessor::BeatSpaceChannels - 1))
                row.removeFromLeft(pathGap);
        }
    };

    if (contentArea.getWidth() >= 220 && contentArea.getHeight() >= 120)
    {
        auto mapArea = contentArea;
        auto pathModeRow = mapArea.removeFromTop(20);
        mapArea.removeFromTop(2);
        auto pathButtonsRow = mapArea.removeFromTop(14);
        mapArea.removeFromTop(2);

        const int minControlWidth = 136;
        int minMapWidth = 220;
        if (beatSpaceViewSizeMode == ViewSizeMode::Compact)
            minMapWidth = 180;
        else if (beatSpaceViewSizeMode == ViewSizeMode::Focus)
            minMapWidth = 260;
        const int targetMapWidth = static_cast<int>(std::round(static_cast<double>(mapArea.getWidth()) * previewRatio));
        const int maxMapWidthForControls = juce::jmax(
            minMapWidth,
            mapArea.getWidth() - ((minControlWidth + spacing) * 2));
        int mapWidth = juce::jlimit(
            minMapWidth,
            juce::jmax(minMapWidth, maxMapWidthForControls),
            targetMapWidth);
        mapWidth = juce::jmin(mapWidth, mapArea.getWidth());

        auto centeredMapArea = juce::Rectangle<int>(mapWidth, mapArea.getHeight());
        centeredMapArea.setCentre(mapArea.getCentre());
        centeredMapArea = centeredMapArea.getIntersection(mapArea);
        beatSpacePreviewBounds = centeredMapArea.reduced(1);

        const auto mapBounds = getBeatSpaceMapInnerBounds(beatSpacePreviewBounds);
        auto controlsArea = juce::Rectangle<int>(
            contentArea.getX(),
            contentArea.getY(),
            juce::jmax(0, mapBounds.getX() - contentArea.getX() - spacing),
            contentArea.getHeight());
        if (controlsArea.getWidth() > 172)
            controlsArea = controlsArea.withWidth(172);

        pathModeRow = pathModeRow.withX(mapBounds.getX()).withWidth(mapBounds.getWidth());
        pathButtonsRow = pathButtonsRow.withX(mapBounds.getX()).withWidth(mapBounds.getWidth());

        if (pathModeRow.getWidth() >= 220)
        {
            auto linkArea = pathModeRow.removeFromRight(56);
            beatSpaceLinkButton.setBounds(linkArea);
            pathModeRow.removeFromRight(6);

            beatSpacePathPlayModeLabel.setBounds(pathModeRow.removeFromLeft(64));
            pathModeRow.removeFromLeft(4);
            beatSpacePathPlayModeBox.setBounds(pathModeRow.removeFromLeft(124));
        }
        else
        {
            beatSpaceLinkButton.setBounds({});
            beatSpacePathPlayModeLabel.setBounds({});
            beatSpacePathPlayModeBox.setBounds({});
        }
        beatSpacePreviewLabel.setBounds({});
        layoutPathButtonsInRow(pathButtonsRow);

        if (controlsArea.getWidth() > 0)
        {
            const int inset = juce::jmin(8, juce::jmax(0, controlsArea.getWidth() / 8));
            controlsArea = controlsArea.reduced(inset, 0);

            auto topControls = controlsArea;
            topControls.removeFromBottom(92);

            microtonicPresetListLabel.setBounds(topControls.removeFromTop(14));
            topControls.removeFromTop(2);
            microtonicPresetListBox.setBounds(topControls.removeFromTop(22));
            topControls.removeFromTop(3);

            auto kitButtons = topControls.removeFromTop(20);
            auto kitDeleteArea = kitButtons.removeFromRight(42);
            microtonicPresetDeleteButton.setBounds(kitDeleteArea);
            kitButtons.removeFromRight(3);
            microtonicPresetStoreButton.setBounds(kitButtons.removeFromRight(56));

            topControls.removeFromTop(6);
            beatPatternListLabel.setBounds(topControls.removeFromTop(14));
            topControls.removeFromTop(2);
            beatPatternListBox.setBounds(topControls.removeFromTop(22));
            topControls.removeFromTop(3);

            auto patternButtonRow = topControls.removeFromTop(20);
            auto patternDeleteArea = patternButtonRow.removeFromRight(66);
            beatPatternDeleteButton.setBounds(patternDeleteArea);
            patternButtonRow.removeFromRight(3);
            beatPatternStoreButton.setBounds(patternButtonRow.removeFromRight(juce::jmin(108, patternButtonRow.getWidth())));

            auto morphArea = controlsArea.removeFromBottom(88);
            beatSpaceMorphLabel.setBounds(morphArea.removeFromTop(14));
            auto morphKnobArea = morphArea.removeFromTop(72);
            const int morphKnobWidth = juce::jmin(90, morphKnobArea.getWidth());
            beatSpaceMorphSlider.setBounds(morphKnobArea.withWidth(morphKnobWidth));

            microtonicPresetStatusLabel.setBounds(topControls.reduced(0, 2));
        }
        else
        {
            beatSpaceMorphLabel.setBounds({});
            beatSpaceMorphSlider.setBounds({});
            microtonicPresetListLabel.setBounds({});
            microtonicPresetListBox.setBounds({});
            microtonicPresetStoreButton.setBounds({});
            microtonicPresetDeleteButton.setBounds({});
            beatPatternListLabel.setBounds({});
            beatPatternListBox.setBounds({});
            beatPatternStoreButton.setBounds({});
            beatPatternDeleteButton.setBounds({});
            microtonicPresetStatusLabel.setBounds({});
        }
    }
    else
    {
        beatSpaceMorphLabel.setBounds({});
        beatSpaceMorphSlider.setBounds({});
        microtonicPresetListLabel.setBounds({});
        microtonicPresetListBox.setBounds({});
        microtonicPresetStoreButton.setBounds({});
        microtonicPresetDeleteButton.setBounds({});
        beatPatternListLabel.setBounds({});
        beatPatternListBox.setBounds({});
        beatPatternStoreButton.setBounds({});
        beatPatternDeleteButton.setBounds({});
        microtonicPresetStatusLabel.setBounds({});
        beatSpacePreviewLabel.setBounds({});
        beatSpaceLinkButton.setBounds({});
        beatSpacePathPlayModeLabel.setBounds({});
        beatSpacePathPlayModeBox.setBounds({});
        for (auto& button : beatSpacePathButtons)
            button.setBounds({});
        beatSpacePreviewBounds = {};
    }
}

int BeatSpaceControlPanel::getPreferredTopSectionHeight(int availableHeight) const
{
    const int safeHeight = juce::jmax(0, availableHeight);
    double ratio = 0.50;
    int minHeight = 240;

    switch (beatSpaceViewSizeMode)
    {
        case ViewSizeMode::Compact:
            ratio = 0.40;
            minHeight = 200;
            break;
        case ViewSizeMode::Focus:
            ratio = 0.60;
            minHeight = 300;
            break;
        case ViewSizeMode::Standard:
        default:
            ratio = 0.50;
            minHeight = 240;
            break;
    }

    const int maxHeight = juce::jmax(minHeight, safeHeight - 420);
    return juce::jlimit(
        minHeight,
        maxHeight,
        static_cast<int>(std::round(static_cast<double>(safeHeight) * ratio)));
}

void BeatSpaceControlPanel::refreshFromProcessor()
{
    if (isShowing() && !processor.getBeatSpaceTablePreviewImage().isValid())
        processor.ensureBeatSpaceVisualAssetsReady();

    const auto presetSignature = buildMicrotonicPresetListSignature();
    if (presetSignature != lastMicrotonicPresetListSignature)
        rebuildMicrotonicPresetList();
    const auto beatPatternSignature = buildBeatPatternListSignature();
    if (beatPatternSignature != lastBeatPatternListSignature)
        rebuildBeatPatternList();

    const auto beatState = processor.getBeatSpaceVisualState();
    beatSpaceMorphSlider.setValue(beatState.morphDurationMs, juce::dontSendNotification);
    beatSpaceLinkButton.setToggleState(beatState.linkAllChannels, juce::dontSendNotification);

    if (beatState.linkAllChannels)
    {
        const int selectedChannel = juce::jlimit(
            0,
            StepVstHostAudioProcessor::BeatSpaceChannels - 1,
            beatState.selectedChannel);

        const int tableSize = juce::jmax(1, beatState.tableSize);
        if (!beatSpaceLastLinkEnabled || !beatSpaceLinkHandlePointValid)
        {
            beatSpaceLinkHandlePoint = beatState.channelPoints[static_cast<size_t>(selectedChannel)];
            beatSpaceLinkHandlePointValid = true;
        }

        beatSpaceLinkHandlePoint.x = juce::jlimit(0, tableSize - 1, beatSpaceLinkHandlePoint.x);
        beatSpaceLinkHandlePoint.y = juce::jlimit(0, tableSize - 1, beatSpaceLinkHandlePoint.y);
    }
    else
    {
        beatSpaceLinkHandlePointValid = false;
    }
    beatSpaceLastLinkEnabled = beatState.linkAllChannels;

    bool mixedPathPlayMode = false;
    auto sharedPathPlayMode = static_cast<StepVstHostAudioProcessor::BeatSpacePathPlayMode>(juce::jlimit(
        static_cast<int>(StepVstHostAudioProcessor::BeatSpacePathPlayMode::Normal),
        static_cast<int>(StepVstHostAudioProcessor::BeatSpacePathPlayMode::RandomSlice),
        beatState.pathPlayModes[0]));
    for (int channel = 1; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
    {
        const auto mode = static_cast<StepVstHostAudioProcessor::BeatSpacePathPlayMode>(juce::jlimit(
            static_cast<int>(StepVstHostAudioProcessor::BeatSpacePathPlayMode::Normal),
            static_cast<int>(StepVstHostAudioProcessor::BeatSpacePathPlayMode::RandomSlice),
            beatState.pathPlayModes[static_cast<size_t>(channel)]));
        if (mode != sharedPathPlayMode)
        {
            mixedPathPlayMode = true;
            break;
        }
    }
    if (mixedPathPlayMode)
    {
        beatSpacePathPlayModeBox.setTextWhenNothingSelected("Mixed");
        beatSpacePathPlayModeBox.setSelectedId(0, juce::dontSendNotification);
    }
    else
    {
        beatSpacePathPlayModeBox.setTextWhenNothingSelected("Path mode");
        beatSpacePathPlayModeBox.setSelectedId(
            beatSpacePathPlayModeToComboId(sharedPathPlayMode),
            juce::dontSendNotification);
    }

    for (int channel = 0; channel < StepVstHostAudioProcessor::BeatSpaceChannels; ++channel)
    {
        const auto idx = static_cast<size_t>(channel);
        const bool pathActive = beatState.pathActive[idx];
        const bool armed = (beatState.pathRecordArmedChannel == channel);
        const bool drawModeActive = armed
            || (beatSpacePathRecordingActive && beatSpacePathRecordingChannel == channel);
        const bool blinkOn = ((juce::Time::getMillisecondCounter() / 180u) % 2u) == 0u;
        auto& button = beatSpacePathButtons[idx];
        button.setToggleState(armed, juce::dontSendNotification);
        button.setButtonText(
            "P" + juce::String(channel + 1)
            + (pathActive ? "*" : "")
            + (drawModeActive ? (blinkOn ? ">" : " ") : ""));
        styleUiButton(button, false);
        const juce::Colour base = getStripColor(channel);
        const float alpha = drawModeActive
            ? (blinkOn ? 0.96f : 0.35f)
            : ((pathActive || armed) ? 0.90f : 0.62f);
        button.setColour(juce::TextButton::buttonColourId, base.withAlpha(alpha));
        button.setColour(
            juce::TextButton::buttonOnColourId,
            base.brighter(drawModeActive && blinkOn ? 0.34f : 0.22f)
                .withAlpha(drawModeActive ? (blinkOn ? 0.98f : 0.58f) : 0.95f));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff8fbff));
        button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfff8fbff));
    }
    juce::String beatTooltip = beatState.statusMessage.isNotEmpty()
        ? beatState.statusMessage
        : juce::String("BeatSpace");
    beatTooltip += "\nClick 1..6 chips (or dots) to select a channel before dragging.";
    beatTooltip += "\nUse mouse wheel over XY to zoom in/out.";
    beatTooltip += "\nUse P1..P6 buttons, then click-drag on XY to record PPQ-synced BeatSpace paths.";
    beatTooltip += "\nRight-click on XY for text/confidence/path overlays and category tagging.";
    if (beatState.categoryConstrainEnabled)
        beatTooltip += "\nCategory Constrain: ON (bubbles stay in assigned Kick/Snare/etc regions).";
    beatTooltip += "\nPreview follows current BeatSpace zoom and pan window.";
    if (beatState.colorClustersReady)
        beatTooltip += "\nCluster overlay uses the BeatSpace script map colors plus in-plugin ML grouping.";
    titleLabel.setTooltip(beatTooltip);

    if (!beatSpacePreviewBounds.isEmpty())
        repaint(beatSpacePreviewBounds.expanded(2));
}

juce::String BeatSpaceControlPanel::buildMicrotonicPresetListSignature() const
{
    constexpr int kitScopeChannel = 0;
    juce::String signature;
    const auto slots = processor.getMicrotonicStripPresetSlots(kitScopeChannel);
    signature.preallocateBytes((slots.size() * 24u) + 16u);
    for (const int slot : slots)
    {
        const int clampedSlot = juce::jlimit(0, StepVstHostAudioProcessor::MicrotonicStripPresetSlots - 1, slot);
        signature << clampedSlot << ":"
                  << processor.getMicrotonicStripPresetName(kitScopeChannel, clampedSlot).trim()
                  << ";";
    }
    return signature;
}

juce::String BeatSpaceControlPanel::buildBeatPatternListSignature() const
{
    juce::String signature;
    const auto infos = processor.getSiteDrumPatternPresetInfos();
    signature.preallocateBytes((infos.size() * static_cast<size_t>(56)) + static_cast<size_t>(16));
    for (const auto& info : infos)
    {
        signature << info.source.trim() << "|"
                  << info.genre.trim() << "|"
                  << info.name.trim() << "|"
                  << info.bpm << ";";
    }
    return signature;
}

void BeatSpaceControlPanel::rebuildBeatPatternList()
{
    const int selectedBefore = beatPatternListBox.getSelectedId();
    beatPatternListBox.clear(juce::dontSendNotification);
    beatPatternPresetInfos = processor.getSiteDrumPatternPresetInfos();
    beatPatternComboToPresetIndex.clear();

    for (int i = 0; i < static_cast<int>(beatPatternPresetInfos.size()); ++i)
    {
        const auto& info = beatPatternPresetInfos[static_cast<size_t>(i)];
        const auto trimmedName = info.name.trim();
        if (trimmedName.isEmpty())
            continue;

        const auto trimmedSource = info.source.trim();
        const auto trimmedGenre = info.genre.trim();
        juce::String itemText = trimmedName;
        if (trimmedSource.isNotEmpty() && trimmedGenre.isNotEmpty()
            && !trimmedSource.equalsIgnoreCase(trimmedGenre))
        {
            itemText = trimmedSource + " / " + trimmedGenre + " - " + itemText;
        }
        else if (trimmedGenre.isNotEmpty())
        {
            itemText = trimmedGenre + " - " + itemText;
        }
        else if (trimmedSource.isNotEmpty())
        {
            itemText = trimmedSource + " - " + itemText;
        }
        if (info.bpm > 0)
            itemText += " (" + juce::String(info.bpm) + " BPM)";

        const int comboIndex = static_cast<int>(beatPatternComboToPresetIndex.size());
        beatPatternListBox.addItem(itemText, kBeatPatternComboBaseId + comboIndex);
        beatPatternComboToPresetIndex.push_back(i);
    }

    if (beatPatternComboToPresetIndex.empty())
        beatPatternListBox.addItem("No patterns available", kBeatPatternComboBaseId);

    if (selectedBefore >= kBeatPatternComboBaseId
        && beatPatternListBox.indexOfItemId(selectedBefore) >= 0)
    {
        beatPatternListBox.setSelectedId(selectedBefore, juce::dontSendNotification);
    }
    else
    {
        beatPatternListBox.setSelectedId(0, juce::dontSendNotification);
    }

    lastBeatPatternListSignature = buildBeatPatternListSignature();
}

void BeatSpaceControlPanel::rebuildMicrotonicPresetList()
{
    constexpr int kitScopeChannel = 0;

    const int selectedBefore = microtonicPresetListBox.getSelectedId();
    microtonicPresetListBox.clear(juce::dontSendNotification);

    std::array<bool, StepVstHostAudioProcessor::MicrotonicStripPresetSlots> used{};
    used.fill(false);
    const auto slots = processor.getMicrotonicStripPresetSlots(kitScopeChannel);
    for (const int slot : slots)
    {
        const int clampedSlot = juce::jlimit(0, StepVstHostAudioProcessor::MicrotonicStripPresetSlots - 1, slot);
        used[static_cast<size_t>(clampedSlot)] = true;
        const auto label = processor.getMicrotonicStripPresetName(kitScopeChannel, clampedSlot).trim();
        const auto itemText = juce::String(clampedSlot + 1).paddedLeft('0', 2) + "  "
            + (label.isNotEmpty() ? label : ("Kit " + juce::String(clampedSlot + 1).paddedLeft('0', 2)));
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
            "New Kit " + juce::String(firstFreeSlot + 1).paddedLeft('0', 2),
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
    microtonicPresetDeleteButton.setEnabled(hasStoredSelection);
    microtonicPresetStoreButton.setEnabled(true);
    lastMicrotonicPresetListSignature = buildMicrotonicPresetListSignature();
}

MacroControlPanel::MacroControlPanel(StepVstHostAudioProcessor& p)
    : processor(p)
{
    titleLabel.setText("BEATSPACE MACROS", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, kTextPrimary);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setTooltip("Global macros for BeatSpace bubble offsets and patch shaping.");
    addAndMakeVisible(titleLabel);

    resetButton.setButtonText("Reset");
    resetButton.setTooltip("Reset all macro knobs to defaults.");
    styleUiButton(resetButton, false);
    resetButton.onClick = [this]()
    {
        processor.resetBeatSpaceMacroKnobs();
        refreshFromProcessor();
        if (macroUiReady)
            processor.markPersistentGlobalUserChange();
    };
    addAndMakeVisible(resetButton);

    for (int i = 0; i < StepVstHostAudioProcessor::BeatSpaceMacroKnobCount; ++i)
    {
        auto& slider = macroKnobs[static_cast<size_t>(i)];
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 14);
        if (i == 3)
            slider.setRange(-1.0, 1.0, 0.001);
        else
            slider.setRange(0.0, 1.0, 0.001);
        slider.setValue(kBeatSpaceMacroKnobDefaults[static_cast<size_t>(i)], juce::dontSendNotification);
        slider.setNumDecimalPlacesToDisplay(2);
        slider.setPopupDisplayEnabled(true, false, this);
        enableAltClickReset(slider, kBeatSpaceMacroKnobDefaults[static_cast<size_t>(i)]);
        slider.textFromValueFunction = [i](double value)
        {
            if (i < 3)
            {
                const int percent = static_cast<int>(std::round(juce::jlimit(0.0, 1.0, value) * 100.0));
                return juce::String(percent) + "%";
            }
            if (i == 3)
            {
                const int percent = static_cast<int>(std::round(juce::jlimit(-1.0, 1.0, value) * 100.0));
                const juce::String sign = (percent > 0) ? "+" : "";
                return sign + juce::String(percent) + "%";
            }

            const int percent = static_cast<int>(std::round((juce::jlimit(0.0, 1.0, value) - 0.5) * 200.0));
            const juce::String sign = (percent > 0) ? "+" : "";
            return sign + juce::String(percent) + "%";
        };
        slider.onValueChange = [this, i]()
        {
            processor.setBeatSpaceMacroKnobValue(i, static_cast<float>(macroKnobs[static_cast<size_t>(i)].getValue()));
        };
        slider.onDragEnd = [this]()
        {
            if (macroUiReady)
                processor.markPersistentGlobalUserChange();
        };
        addAndMakeVisible(slider);

        auto& label = macroLabels[static_cast<size_t>(i)];
        label.setText(kBeatSpaceMacroKnobLabels[static_cast<size_t>(i)], juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, kTextSecondary);
        juce::String macroTooltip;
        switch (i)
        {
            case 0: macroTooltip = "Deep: biases bubbles toward lower-pitched presets."; break;
            case 1: macroTooltip = "Tight: biases bubbles toward shorter attack/decay envelopes."; break;
            case 2: macroTooltip = "Noise: biases bubbles toward noisier preset character."; break;
            case 3: macroTooltip = "Density (bipolar): 0 keeps original pattern, negative thins/softens, positive adds musical notes."; break;
            case 4: macroTooltip = "Pitch offset macro for BeatSpace-applied patch vectors."; break;
            case 5: macroTooltip = "Pitch Mod offset macro for BeatSpace-applied patch vectors."; break;
            case 6: macroTooltip = "Length offset macro for BeatSpace-applied patch vectors."; break;
            case 7:
            default: macroTooltip = "Filter offset macro for BeatSpace-applied patch vectors."; break;
        }
        label.setTooltip(macroTooltip);
        slider.setTooltip(macroTooltip);
        addAndMakeVisible(label);
    }

    refreshFromProcessor();
    macroUiReady = true;
}

void MacroControlPanel::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), kAccent, 8.0f);
}

void MacroControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    auto titleRow = bounds.removeFromTop(20);
    resetButton.setBounds(titleRow.removeFromRight(70));
    titleLabel.setBounds(titleRow);
    bounds.removeFromTop(6);

    constexpr int rows = 2;
    constexpr int columns = 4;
    const int rowGap = 8;
    const int colGap = 8;
    const int totalRowGap = rowGap * (rows - 1);
    const int rowHeight = juce::jmax(24, (bounds.getHeight() - totalRowGap) / rows);

    for (int row = 0; row < rows; ++row)
    {
        auto rowBounds = bounds.removeFromTop(rowHeight);
        if (row < (rows - 1))
            bounds.removeFromTop(rowGap);

        const int totalColGap = colGap * (columns - 1);
        const int cellWidth = juce::jmax(26, (rowBounds.getWidth() - totalColGap) / columns);
        for (int col = 0; col < columns; ++col)
        {
            const int idx = (row * columns) + col;
            auto cell = rowBounds.removeFromLeft(cellWidth);
            if (col < (columns - 1))
                rowBounds.removeFromLeft(colGap);

            auto labelArea = cell.removeFromTop(14);
            macroLabels[static_cast<size_t>(idx)].setBounds(labelArea);
            cell.removeFromTop(1);
            macroKnobs[static_cast<size_t>(idx)].setBounds(cell);
        }
    }
}

void MacroControlPanel::refreshFromProcessor()
{
    for (int i = 0; i < StepVstHostAudioProcessor::BeatSpaceMacroKnobCount; ++i)
    {
        auto& slider = macroKnobs[static_cast<size_t>(i)];
        if (!slider.isMouseButtonDown())
        {
            slider.setValue(
                processor.getBeatSpaceMacroKnobValue(i),
                juce::dontSendNotification);
        }
    }
}

int MacroControlPanel::getPreferredTopSectionHeight(int availableHeight) const
{
    const int safeHeight = juce::jmax(0, availableHeight);
    const int minHeight = 190;
    const int maxHeight = juce::jmin(320, juce::jmax(minHeight, safeHeight - 430));
    const int targetHeight = static_cast<int>(std::round(static_cast<double>(safeHeight) * 0.34));
    return juce::jlimit(minHeight, maxHeight, targetHeight);
}

MixControlPanel::MixControlPanel(StepVstHostAudioProcessor& p)
    : processor(p)
{
    titleLabel.setText("HOSTED MIX", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, kTextPrimary);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setTooltip("Strip-colored lane mixer with per-lane fader, pan, mute, solo, and meters.");
    addAndMakeVisible(titleLabel);

    statusLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    statusLabel.setColour(juce::Label::textColourId, kTextMuted);
    statusLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel);

    outputRoutingLabel.setText("Outputs", juce::dontSendNotification);
    outputRoutingLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    outputRoutingLabel.setJustificationType(juce::Justification::centredLeft);
    outputRoutingLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(outputRoutingLabel);

    outputRoutingBox.addItem("Stereo Mix", 1);
    outputRoutingBox.addItem("Separate Strip Outs", 2);
    outputRoutingBox.setSelectedId(1, juce::dontSendNotification);
    styleUiCombo(outputRoutingBox);
    outputRoutingBox.setTooltip("Route plugin output as stereo mix or per-strip stereo buses.");
    outputRoutingBox.onChange = [this]()
    {
        if (mixUiReady)
            processor.markPersistentGlobalUserChange();
    };
    addAndMakeVisible(outputRoutingBox);
    outputRoutingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters,
        "outputRouting",
        outputRoutingBox);

    hostSlotsLabel.setText("Insert Slots", juce::dontSendNotification);
    hostSlotsLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    hostSlotsLabel.setJustificationType(juce::Justification::centredLeft);
    hostSlotsLabel.setColour(juce::Label::textColourId, kTextSecondary);
    addAndMakeVisible(hostSlotsLabel);

    for (int slot = 0; slot < 2; ++slot)
    {
        const auto idx = static_cast<size_t>(slot);
        auto& slotLabel = hostSlotLabels[idx];
        slotLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        slotLabel.setJustificationType(juce::Justification::centredLeft);
        slotLabel.setColour(juce::Label::textColourId, kTextMuted);
        addAndMakeVisible(slotLabel);

        auto& loadButton = hostSlotLoadButtons[idx];
        loadButton.setButtonText("Load " + juce::String::charToString(slot == 0 ? 'A' : 'B'));
        loadButton.setTooltip("Choose and load insert plugin for slot "
                              + juce::String::charToString(slot == 0 ? 'A' : 'B'));
        loadButton.onClick = [this, slot]() { chooseHostedSlotFile(slot); };
        styleUiButton(loadButton);
        addAndMakeVisible(loadButton);

        auto& openButton = hostSlotOpenButtons[idx];
        openButton.setButtonText("Open GUI");
        openButton.setTooltip("Open insert plugin GUI for slot "
                              + juce::String::charToString(slot == 0 ? 'A' : 'B'));
        openButton.onClick = [this, slot]() { openHostedSlotGui(slot); };
        styleUiButton(openButton, true);
        addAndMakeVisible(openButton);

        auto& removeButton = hostSlotRemoveButtons[idx];
        removeButton.setButtonText("Remove");
        removeButton.setTooltip("Unload insert plugin from slot "
                                + juce::String::charToString(slot == 0 ? 'A' : 'B'));
        removeButton.onClick = [this, slot]() { removeHostedSlotPlugin(slot); };
        styleUiButton(removeButton);
        removeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe9d7d7));
        removeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd9c2c2));
        addAndMakeVisible(removeButton);
    }

    hostSaveDefaultsButton.setButtonText("Save As Default");
    hostSaveDefaultsButton.setTooltip("Persist both insert slot paths (A/B) for startup fallback loading.");
    hostSaveDefaultsButton.onClick = [this]() { saveHostedSlotsAsDefault(); };
    styleUiButton(hostSaveDefaultsButton, true);
    addAndMakeVisible(hostSaveDefaultsButton);

    hostStatusLabel.setFont(juce::Font(juce::FontOptions(9.0f)));
    hostStatusLabel.setJustificationType(juce::Justification::centredLeft);
    hostStatusLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(hostStatusLabel);

    masterVolumeLabel.setText("Master", juce::dontSendNotification);
    masterVolumeLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    masterVolumeLabel.setJustificationType(juce::Justification::centred);
    masterVolumeLabel.setColour(juce::Label::textColourId, kTextMuted);
    addAndMakeVisible(masterVolumeLabel);

    masterVolumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    masterVolumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterVolumeSlider.setRange(0.0, 1.0, 0.001);
    masterVolumeSlider.setTooltip("Global master output level.");
    masterVolumeSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff253240));
    masterVolumeSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff8cc7ff));
    masterVolumeSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffd9f0ff));
    enableAltClickReset(masterVolumeSlider, 1.0);
    masterVolumeSlider.onDragEnd = [this]()
    {
        if (mixUiReady)
            processor.markPersistentGlobalUserChange();
    };
    addAndMakeVisible(masterVolumeSlider);
    masterVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters,
        "masterVolume",
        masterVolumeSlider);

    masterMeterSlider.setSliderStyle(juce::Slider::LinearBarVertical);
    masterMeterSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterMeterSlider.setRange(0.0, 1.0, 0.0001);
    masterMeterSlider.setValue(0.0, juce::dontSendNotification);
    masterMeterSlider.setEnabled(false);
    masterMeterSlider.setInterceptsMouseClicks(false, false);
    masterMeterSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff263648));
    masterMeterSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff95d8ff));
    masterMeterSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffd4eeff));
    masterMeterSlider.setTooltip("Master meter (post lane mix).");
    addAndMakeVisible(masterMeterSlider);

    constexpr std::array<const char*, 6> kMeterDbLabels { "0", "12", "24", "36", "48", "60" };
    for (int i = 0; i < static_cast<int>(meterScaleLabels.size()); ++i)
    {
        auto& label = meterScaleLabels[static_cast<size_t>(i)];
        label.setText(kMeterDbLabels[static_cast<size_t>(i)], juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
        label.setJustificationType(juce::Justification::centredRight);
        label.setColour(juce::Label::textColourId, kTextMuted.withAlpha(0.86f));
        label.setTooltip("Meter scale in dBFS.");
        addAndMakeVisible(label);
    }

    for (int i = 0; i < StepVstHostAudioProcessor::BeatSpaceChannels; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        const auto laneColour = getStripColor(i);
        laneColours[idx] = laneColour;

        auto& laneLabel = laneLabels[idx];
        laneLabel.setText("S" + juce::String(i + 1), juce::dontSendNotification);
        laneLabel.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        laneLabel.setJustificationType(juce::Justification::centred);
        laneLabel.setColour(juce::Label::textColourId, laneColour.brighter(0.18f));
        addAndMakeVisible(laneLabel);

        auto& outLabel = outPairLabels[idx];
        outLabel.setText(
            juce::String((i * 2) + 1) + "/" + juce::String((i * 2) + 2),
            juce::dontSendNotification);
        outLabel.setFont(juce::Font(juce::FontOptions(9.5f)));
        outLabel.setJustificationType(juce::Justification::centred);
        outLabel.setColour(juce::Label::textColourId, laneColour.withMultipliedBrightness(0.92f));
        addAndMakeVisible(outLabel);

        auto& mute = muteButtons[idx];
        mute.setButtonText("M");
        mute.setClickingTogglesState(true);
        mute.setTooltip("Mute lane " + juce::String(i + 1));
        mute.onClick = [this, i]()
        {
            if (suppressMixToggleCallbacks)
                return;
            if (auto* engine = processor.getAudioEngine())
            {
                if (auto* strip = engine->getStrip(i))
                    strip->setMuted(muteButtons[static_cast<size_t>(i)].getToggleState());
            }
            if (mixUiReady)
                processor.markPersistentGlobalUserChange();
        };
        styleUiButton(mute);
        mute.setColour(juce::TextButton::buttonColourId, laneColour.withAlpha(0.14f));
        mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffcf4e43));
        mute.setColour(juce::TextButton::textColourOffId, laneColour.darker(0.48f));
        mute.setColour(juce::TextButton::textColourOnId, juce::Colour(0xfffcffff));
        addAndMakeVisible(mute);

        auto& solo = soloButtons[idx];
        solo.setButtonText("S");
        solo.setClickingTogglesState(true);
        solo.setTooltip("Solo lane " + juce::String(i + 1));
        solo.onClick = [this, i]()
        {
            if (suppressMixToggleCallbacks)
                return;
            if (auto* engine = processor.getAudioEngine())
            {
                if (auto* strip = engine->getStrip(i))
                    strip->setSolo(soloButtons[static_cast<size_t>(i)].getToggleState());
            }
            if (mixUiReady)
                processor.markPersistentGlobalUserChange();
        };
        styleUiButton(solo);
        solo.setColour(juce::TextButton::buttonColourId, laneColour.withAlpha(0.14f));
        solo.setColour(juce::TextButton::buttonOnColourId, laneColour.brighter(0.20f));
        solo.setColour(juce::TextButton::textColourOffId, laneColour.darker(0.52f));
        solo.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff0a1015));
        addAndMakeVisible(solo);

        auto& meter = meterSliders[idx];
        meter.setSliderStyle(juce::Slider::LinearBarVertical);
        meter.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        meter.setRange(0.0, 1.0, 0.0001);
        meter.setValue(0.0, juce::dontSendNotification);
        meter.setEnabled(false);
        meter.setInterceptsMouseClicks(false, false);
        meter.setColour(juce::Slider::backgroundColourId, laneColour.withAlpha(0.18f));
        meter.setColour(juce::Slider::trackColourId, laneColour.brighter(0.48f));
        meter.setColour(juce::Slider::thumbColourId, laneColour.brighter(0.62f));
        meter.setTooltip("Post-mix lane output level.");
        addAndMakeVisible(meter);

        auto& volume = volumeSliders[idx];
        volume.setSliderStyle(juce::Slider::LinearVertical);
        volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange(0.0, 1.0, 0.001);
        volume.setTooltip("Mirrors Strip " + juce::String(i + 1) + " volume.");
        volume.setColour(juce::Slider::backgroundColourId, laneColour.withAlpha(0.24f));
        volume.setColour(juce::Slider::trackColourId, laneColour.withMultipliedSaturation(1.10f).brighter(0.12f));
        volume.setColour(juce::Slider::thumbColourId, laneColour.brighter(0.36f));
        enableAltClickReset(volume, 1.0);
        volume.onDragEnd = [this]()
        {
            if (mixUiReady)
                processor.markPersistentGlobalUserChange();
        };
        addAndMakeVisible(volume);
        volumeAttachments[idx] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.parameters,
                "stripVolume" + juce::String(i),
                volume);

        auto& pan = panSliders[idx];
        panLookAndFeels[idx].setKnobColor(laneColour);
        pan.setLookAndFeel(&panLookAndFeels[idx]);
        pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange(-1.0, 1.0, 0.001);
        pan.setTooltip("Mirrors Strip " + juce::String(i + 1) + " pan.");
        enableAltClickReset(pan, 0.0);
        pan.onDragEnd = [this]()
        {
            if (mixUiReady)
                processor.markPersistentGlobalUserChange();
        };
        addAndMakeVisible(pan);
        panAttachments[idx] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.parameters,
                "stripPan" + juce::String(i),
                pan);

        meterDisplayValues[idx] = 0.0f;
    }

    hostSlotFiles = processor.getDefaultMixInsertFiles();
    refreshHostedSlotLabels();
    refreshFromProcessor();
    mixUiReady = true;
}

MixControlPanel::~MixControlPanel()
{
    for (int slot = 0; slot < 2; ++slot)
        closeHostedSlotGui(slot);
    for (auto& pan : panSliders)
        pan.setLookAndFeel(nullptr);
}

void MixControlPanel::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat(), kAccent, 8.0f);

    for (int i = 0; i < StepVstHostAudioProcessor::BeatSpaceChannels; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        auto laneArea = volumeSliders[idx].getBounds().toFloat()
            .expanded(4.0f, 3.0f)
            .withY(static_cast<float>(laneLabels[idx].getY()) - 2.0f);
        laneArea = laneArea.getUnion(panSliders[idx].getBounds().toFloat().expanded(3.0f));
        g.setColour(laneColours[idx].withAlpha(0.08f));
        g.fillRoundedRectangle(laneArea, 6.0f);
        g.setColour(laneColours[idx].withAlpha(0.23f));
        g.drawRoundedRectangle(laneArea, 6.0f, 1.0f);
    }
}

void MixControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    auto titleRow = bounds.removeFromTop(20);
    statusLabel.setBounds(titleRow.removeFromRight(300));
    titleLabel.setBounds(titleRow);
    bounds.removeFromTop(4);

    auto topRow = bounds.removeFromTop(34);
    auto outputArea = topRow.removeFromLeft(110);
    outputRoutingLabel.setBounds(outputArea.removeFromTop(14));
    outputArea.removeFromTop(2);
    outputRoutingBox.setBounds(outputArea.removeFromTop(18));
    bounds.removeFromTop(6);

    constexpr int hostColumnWidth = 296;
    auto hostColumn = bounds.removeFromRight(hostColumnWidth);
    bounds.removeFromRight(8);

    hostSlotsLabel.setBounds(hostColumn.removeFromTop(12));
    hostColumn.removeFromTop(2);
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto idx = static_cast<size_t>(slot);
        auto slotRow = hostColumn.removeFromTop(20);
        hostSlotLoadButtons[idx].setBounds(slotRow.removeFromLeft(58));
        slotRow.removeFromLeft(3);
        hostSlotOpenButtons[idx].setBounds(slotRow.removeFromLeft(72));
        slotRow.removeFromLeft(3);
        hostSlotRemoveButtons[idx].setBounds(slotRow.removeFromLeft(60));
        slotRow.removeFromLeft(3);
        hostSlotLabels[idx].setBounds(slotRow);
        hostColumn.removeFromTop(4);
    }
    hostSaveDefaultsButton.setBounds(hostColumn.removeFromTop(20));
    hostColumn.removeFromTop(4);
    hostStatusLabel.setBounds(hostColumn.removeFromTop(16));
    hostColumn.removeFromTop(6);
    masterVolumeLabel.setBounds(hostColumn.removeFromTop(12));
    const int masterMeterWidth = 10;
    auto masterFaderArea = hostColumn;
    masterMeterSlider.setBounds(masterFaderArea.removeFromRight(masterMeterWidth));
    masterFaderArea.removeFromRight(4);
    masterVolumeSlider.setBounds(masterFaderArea);

    auto meterScaleArea = bounds.removeFromLeft(26);
    meterScaleArea.removeFromTop(46);
    const int scaleStep = juce::jmax(9, meterScaleArea.getHeight() / 5);
    for (int i = 0; i < static_cast<int>(meterScaleLabels.size()); ++i)
    {
        auto& label = meterScaleLabels[static_cast<size_t>(i)];
        label.setBounds(meterScaleArea.removeFromTop(10));
        if (i < static_cast<int>(meterScaleLabels.size()) - 1 && meterScaleArea.getHeight() > scaleStep - 10)
            meterScaleArea.removeFromTop(scaleStep - 10);
    }
    bounds.removeFromLeft(4);

    const int lanes = StepVstHostAudioProcessor::BeatSpaceChannels;
    const int laneGap = 6;
    const int laneWidth = juce::jmax(
        86,
        (bounds.getWidth() - ((lanes - 1) * laneGap)) / lanes);

    for (int i = 0; i < lanes; ++i)
    {
        auto lane = bounds.removeFromLeft(laneWidth);
        if (i < (lanes - 1))
            bounds.removeFromLeft(laneGap);

        laneLabels[static_cast<size_t>(i)].setBounds(lane.removeFromTop(14));
        outPairLabels[static_cast<size_t>(i)].setBounds(lane.removeFromTop(12));
        lane.removeFromTop(2);

        auto toggleRow = lane.removeFromTop(20);
        auto& mute = muteButtons[static_cast<size_t>(i)];
        auto& solo = soloButtons[static_cast<size_t>(i)];
        const int buttonGap = 3;
        const int buttonWidth = juce::jmax(22, (toggleRow.getWidth() - buttonGap) / 2);
        mute.setBounds(toggleRow.removeFromLeft(buttonWidth));
        toggleRow.removeFromLeft(buttonGap);
        solo.setBounds(toggleRow.removeFromLeft(buttonWidth));

        lane.removeFromTop(4);
        auto panArea = lane.removeFromBottom(50);
        panSliders[static_cast<size_t>(i)].setBounds(panArea.reduced(2, 0));
        lane.removeFromTop(2);

        auto faderArea = lane.reduced(6, 0);
        const int meterWidth = juce::jlimit(11, 18, faderArea.getWidth() / 5);
        const int faderWidth = juce::jmax(22, faderArea.getWidth() - meterWidth - 6);
        volumeSliders[static_cast<size_t>(i)].setBounds(faderArea.removeFromLeft(faderWidth));
        faderArea.removeFromLeft(6);
        meterSliders[static_cast<size_t>(i)].setBounds(faderArea.removeFromLeft(meterWidth));
    }

    if (!meterSliders.empty())
    {
        const auto refMeter = meterSliders[0].getBounds();
        const int labelX = juce::jmax(0, refMeter.getX() - 26);
        const int labelW = 24;
        for (int i = 0; i < static_cast<int>(meterScaleLabels.size()); ++i)
        {
            const float norm = static_cast<float>(i) / 5.0f; // 0 -> top (0 dB), 1 -> bottom (-60 dB)
            const int y = refMeter.getY() + static_cast<int>(std::round(norm * static_cast<float>(refMeter.getHeight() - 1)));
            meterScaleLabels[static_cast<size_t>(i)].setBounds(labelX, y - 5, labelW, 10);
        }
    }
}

void MixControlPanel::timerCallback()
{
    refreshFromProcessor();
}

void MixControlPanel::chooseHostedSlotFile(int slot)
{
    const int clampedSlot = juce::jlimit(0, 1, slot);
    const auto idx = static_cast<size_t>(clampedSlot);
    auto startDir = hostSlotFiles[idx];
    if (startDir.getFullPathName().trim().isEmpty())
        startDir = processor.getLoadedMixInsertFile(clampedSlot);
    if (startDir.getFullPathName().trim().isEmpty())
        startDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    if (!startDir.isDirectory())
        startDir = startDir.getParentDirectory();

    juce::FileChooser chooser("Choose hosted plugin for slot "
                                  + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B'),
                              startDir,
                              "*.vst3;*.component");
    if (!chooser.browseForFileToOpen())
        return;

    const auto selectedFile = chooser.getResult();
    hostSlotFiles[idx] = selectedFile;
    closeHostedSlotGui(clampedSlot);
    juce::String error;
    if (!processor.loadMixInsertPlugin(clampedSlot, selectedFile, error))
    {
        hostStatusLabel.setText(
            "Insert load failed (" + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B')
                + "): " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
            juce::dontSendNotification);
        refreshHostedSlotLabels();
        return;
    }

    refreshHostedSlotLabels();
    if (auto* instance = processor.getMixInsertInstance(clampedSlot))
    {
        hostStatusLabel.setText(
            "Insert " + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B')
                + " loaded: " + instance->getName(),
            juce::dontSendNotification);
    }
    else
    {
        hostStatusLabel.setText(
            "Insert " + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B')
                + " loaded",
            juce::dontSendNotification);
    }
}

void MixControlPanel::openHostedSlotGui(int slot)
{
    const int clampedSlot = juce::jlimit(0, 1, slot);
    const auto idx = static_cast<size_t>(clampedSlot);

    if (hostSlotEditorWindows[idx] != nullptr)
    {
        hostSlotEditorWindows[idx]->toFront(true);
        return;
    }

    auto* instance = processor.getMixInsertInstance(clampedSlot);
    if (instance == nullptr)
    {
        hostStatusLabel.setText(
            "Insert " + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B') + ": no plugin loaded",
            juce::dontSendNotification);
        return;
    }

    if (!instance->hasEditor())
    {
        hostStatusLabel.setText(
            "Insert " + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B')
                + " has no GUI: " + instance->getName(),
            juce::dontSendNotification);
        return;
    }

    if (auto* editor = instance->createEditorIfNeeded())
    {
        auto safeThis = juce::Component::SafePointer<MixControlPanel>(this);
        auto window = std::make_unique<HostedPluginEditorWindow>(
            "Insert " + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B') + ": " + instance->getName(),
            [safeThis, clampedSlot]()
            {
                if (safeThis != nullptr)
                    safeThis->closeHostedSlotGui(clampedSlot);
            });

        const int width = juce::jmax(420, editor->getWidth());
        const int height = juce::jmax(260, editor->getHeight());
        window->setResizeLimits(320, 220, 1920, 1400);
        window->setContentOwned(editor, true);
        window->centreWithSize(width, height);
        window->setVisible(true);
        hostSlotEditorWindows[idx] = std::move(window);
        hostStatusLabel.setText(
            "Insert " + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B')
                + " GUI opened",
            juce::dontSendNotification);
        return;
    }

    hostStatusLabel.setText(
        "Insert " + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B')
            + " GUI unavailable",
        juce::dontSendNotification);
}

void MixControlPanel::removeHostedSlotPlugin(int slot)
{
    const int clampedSlot = juce::jlimit(0, 1, slot);
    const auto idx = static_cast<size_t>(clampedSlot);
    closeHostedSlotGui(clampedSlot);
    processor.unloadMixInsertPlugin(clampedSlot);
    hostSlotFiles[idx] = juce::File();
    refreshHostedSlotLabels();
    hostStatusLabel.setText(
        "Insert " + juce::String::charToString(clampedSlot == 0 ? 'A' : 'B') + " removed",
        juce::dontSendNotification);
    if (mixUiReady)
        processor.markPersistentGlobalUserChange();
}

void MixControlPanel::closeHostedSlotGui(int slot)
{
    const int clampedSlot = juce::jlimit(0, 1, slot);
    const auto idx = static_cast<size_t>(clampedSlot);
    if (hostSlotEditorWindows[idx] == nullptr)
        return;

    hostSlotEditorWindows[idx]->setVisible(false);
    hostSlotEditorWindows[idx].reset();
}

void MixControlPanel::saveHostedSlotsAsDefault()
{
    juce::String error;
    if (!processor.setDefaultMixInsertFiles(hostSlotFiles, error))
    {
        hostStatusLabel.setText(
            "Default save failed: " + (error.isNotEmpty() ? error : juce::String("Unknown error")),
            juce::dontSendNotification);
        return;
    }

    if (mixUiReady)
        processor.markPersistentGlobalUserChange();
    hostStatusLabel.setText("Saved slot A/B as startup defaults", juce::dontSendNotification);
}

void MixControlPanel::refreshHostedSlotLabels()
{
    for (int slot = 0; slot < 2; ++slot)
    {
        const auto idx = static_cast<size_t>(slot);
        const auto slotName = juce::String::charToString(slot == 0 ? 'A' : 'B');
        const auto& file = hostSlotFiles[idx];
        const bool hasFile = file.getFullPathName().trim().isNotEmpty();
        hostSlotLabels[idx].setText(
            "Slot " + slotName + ": " + (hasFile ? file.getFileName() : juce::String("(empty)")),
            juce::dontSendNotification);
        hostSlotLabels[idx].setTooltip(hasFile ? file.getFullPathName() : juce::String("No file selected"));
    }
}

void MixControlPanel::refreshFromProcessor()
{
    // Keep meter updates fast, but throttle expensive non-meter UI updates.
    const bool slowRefresh = ((refreshFrameCounter++ % 5) == 0);

    if (slowRefresh)
    {
        const int hostedOuts = processor.getHostRack().getOutputChannelCount();
        const bool multiOutActive = hostedOuts >= (StepVstHostAudioProcessor::BeatSpaceChannels * 2);
        const bool separateRouting = outputRoutingBox.getSelectedId() == 2;
        statusLabel.setText(
            multiOutActive
                ? (separateRouting ? "Multi-out active  |  Separate strip outs"
                                   : "Multi-out active  |  Stereo mix out")
                : (separateRouting ? "Stereo fallback  |  Separate strip outs selected"
                                   : "Stereo fallback  |  Stereo mix out"),
            juce::dontSendNotification);

        juce::StringArray insertStatusTokens;
        for (int slot = 0; slot < 2; ++slot)
        {
            const auto idx = static_cast<size_t>(slot);
            const auto loadedFile = processor.getLoadedMixInsertFile(slot);
            const bool active = loadedFile.getFullPathName().trim().isNotEmpty();
            if (active)
                hostSlotFiles[idx] = loadedFile;

            auto* instance = processor.getMixInsertInstance(slot);
            hostSlotOpenButtons[idx].setColour(
                juce::TextButton::buttonColourId,
                active ? juce::Colour(0xff79b4ff) : juce::Colour(0xff5f6f83));
            hostSlotOpenButtons[idx].setColour(
                juce::TextButton::buttonOnColourId,
                active ? juce::Colour(0xff7bb5ff) : juce::Colour(0xff4f6f93));
            hostSlotOpenButtons[idx].setEnabled(instance != nullptr && instance->hasEditor());
            hostSlotRemoveButtons[idx].setEnabled(active);

            if (instance == nullptr)
                closeHostedSlotGui(slot);

            const juce::String slotName = juce::String::charToString(slot == 0 ? 'A' : 'B');
            insertStatusTokens.add(
                "Insert " + slotName + ": "
                    + (instance != nullptr ? instance->getName() : juce::String("empty")));
        }
        refreshHostedSlotLabels();
        hostStatusLabel.setText(insertStatusTokens.joinIntoString("  |  "), juce::dontSendNotification);

        suppressMixToggleCallbacks = true;
        if (auto* engine = processor.getAudioEngine())
        {
            for (int i = 0; i < StepVstHostAudioProcessor::BeatSpaceChannels; ++i)
            {
                const auto idx = static_cast<size_t>(i);
                if (auto* strip = engine->getStrip(i))
                {
                    muteButtons[idx].setToggleState(strip->isMuted(), juce::dontSendNotification);
                    soloButtons[idx].setToggleState(strip->isSolo(), juce::dontSendNotification);
                    muteButtons[idx].setEnabled(true);
                    soloButtons[idx].setEnabled(true);
                }
                else
                {
                    muteButtons[idx].setToggleState(false, juce::dontSendNotification);
                    soloButtons[idx].setToggleState(false, juce::dontSendNotification);
                    muteButtons[idx].setEnabled(false);
                    soloButtons[idx].setEnabled(false);
                }
            }
        }
        suppressMixToggleCallbacks = false;
    }

    float masterMeterTarget = 0.0f;
    for (int i = 0; i < StepVstHostAudioProcessor::BeatSpaceChannels; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        const float raw = processor.getHostedMixMeterLevel(i);
        constexpr float meterFloorDb = -60.0f;
        const float rawDb = juce::Decibels::gainToDecibels(juce::jmax(raw, 1.0e-6f), meterFloorDb);
        const float norm = juce::jlimit(0.0f, 1.0f, juce::jmap(rawDb, meterFloorDb, 0.0f, 0.0f, 1.0f));
        const float target = std::pow(norm, 0.72f);
        const float previous = meterDisplayValues[idx];
        const float smoothed = juce::jmax(target, previous * 0.87f);
        meterDisplayValues[idx] = smoothed;
        const float meterCurrent = static_cast<float>(meterSliders[idx].getValue());
        if (std::abs(meterCurrent - smoothed) > 0.002f)
            meterSliders[idx].setValue(smoothed, juce::dontSendNotification);
        masterMeterTarget = juce::jmax(masterMeterTarget, smoothed);
    }

    const float masterPrev = static_cast<float>(masterMeterSlider.getValue());
    const float masterSmoothed = juce::jmax(masterMeterTarget, masterPrev * 0.89f);
    if (std::abs(masterPrev - masterSmoothed) > 0.002f)
        masterMeterSlider.setValue(masterSmoothed, juce::dontSendNotification);
}

int MixControlPanel::getPreferredTopSectionHeight(int availableHeight) const
{
    const int safeHeight = juce::jmax(0, availableHeight);
    const int minHeight = 220;
    const int maxHeight = juce::jmin(360, juce::jmax(minHeight, safeHeight - 410));
    const int targetHeight = static_cast<int>(std::round(static_cast<double>(safeHeight) * 0.40));
    return juce::jlimit(minHeight, maxHeight, targetHeight);
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

    for (int i = 0; i < numSlots; ++i)
    {
        const int x = pageOrderArea.getX() + i * (slotWidth + gapX);
        const int y = pageOrderArea.getY();
        auto slot = juce::Rectangle<float>(
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(slotWidth),
            static_cast<float>(slotHeight));

        juce::ColourGradient slotFill(
            juce::Colour(0xffdfe8f2).withAlpha(0.88f), slot.getX(), slot.getY(),
            juce::Colour(0xffbdcddd).withAlpha(0.86f), slot.getX(), slot.getBottom(), false);
        g.setGradientFill(slotFill);
        g.fillRoundedRectangle(slot, 5.0f);

        g.setColour(juce::Colours::white.withAlpha(0.30f));
        g.drawRoundedRectangle(slot.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(juce::Colour(0xff7f94ab).withAlpha(0.74f));
        g.drawRoundedRectangle(slot.reduced(1.3f), 4.0f, 0.9f);
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
    if (!isShowing())
        return;

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
    targetBox.addItem("Retrigger", 8);
    targetBox.addItem("BeatSpace X", 9);
    targetBox.addItem("BeatSpace Y", 10);
    targetBox.addItem("Kits", 11);
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
    if (!isShowing())
        return;

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

StepSequencerDisplay::EditTool StepVstHostAudioProcessorEditor::stepToolFromComboId(int comboId)
{
    switch (comboId)
    {
        case 2: return StepSequencerDisplay::EditTool::Divide;
        case 3: return StepSequencerDisplay::EditTool::RampUp;
        case 4: return StepSequencerDisplay::EditTool::RampDown;
        case 5: return StepSequencerDisplay::EditTool::Probability;
        case 6: return StepSequencerDisplay::EditTool::Select;
        case 1:
        default:
            return StepSequencerDisplay::EditTool::Volume;
    }
}

int StepVstHostAudioProcessorEditor::comboIdFromStepTool(StepSequencerDisplay::EditTool tool)
{
    switch (tool)
    {
        case StepSequencerDisplay::EditTool::Divide: return 2;
        case StepSequencerDisplay::EditTool::RampUp: return 3;
        case StepSequencerDisplay::EditTool::RampDown: return 4;
        case StepSequencerDisplay::EditTool::Probability: return 5;
        case StepSequencerDisplay::EditTool::Select: return 6;
        case StepSequencerDisplay::EditTool::Draw:
        case StepSequencerDisplay::EditTool::Volume:
        default:
            return 1;
    }
}

StepSequencerDisplay::EditTool StepVstHostAudioProcessorEditor::stepToolFromProcessorIndex(int toolIndex)
{
    switch (toolIndex)
    {
        case 2: return StepSequencerDisplay::EditTool::Divide;
        case 3: return StepSequencerDisplay::EditTool::RampUp;
        case 4: return StepSequencerDisplay::EditTool::RampDown;
        case 5: return StepSequencerDisplay::EditTool::Probability;
        case 1:
        case 0:
        case 6:
        case 7:
        case 8:
        default:
            return StepSequencerDisplay::EditTool::Volume;
    }
}

void StepVstHostAudioProcessorEditor::setGlobalStepTool(
    StepSequencerDisplay::EditTool tool, bool updateComboBox, bool applyToStrips)
{
    if (tool == StepSequencerDisplay::EditTool::Draw)
        tool = StepSequencerDisplay::EditTool::Volume;

    const bool toolChanged = (globalStepTool != tool);
    if (toolChanged)
        globalStepTool = tool;

    if (updateComboBox)
    {
        const int selectedId = comboIdFromStepTool(globalStepTool);
        suppressGlobalStepToolChange = true;
        for (int i = 0; i < GlobalStepToolButtonCount; ++i)
        {
            auto& button = globalStepToolButtons[static_cast<size_t>(i)];
            const bool isSelected = ((i + 1) == selectedId);
            button.setToggleState(isSelected, juce::dontSendNotification);
            styleUiButton(button, isSelected);
        }
        suppressGlobalStepToolChange = false;
    }

    if (applyToStrips && toolChanged)
        applyGlobalStepToolToAllStrips();
}

void StepVstHostAudioProcessorEditor::applyGlobalStepToolToAllStrips()
{
    for (auto* strip : stripControls)
    {
        if (strip != nullptr)
            strip->setStepEditTool(globalStepTool);
    }
}

void StepVstHostAudioProcessorEditor::setGlobalStepPatchTab(
    int tabIndex, bool updateButtons, bool applyToStrips)
{
    const int clampedTab = juce::jlimit(0, GlobalStepPatchTabCount - 1, tabIndex);
    const bool tabChanged = (globalStepPatchTabIndex != clampedTab);
    if (tabChanged)
        globalStepPatchTabIndex = clampedTab;

    if (updateButtons)
    {
        suppressGlobalStepPatchTabChange = true;
        for (int i = 0; i < GlobalStepPatchTabCount; ++i)
        {
            auto& button = globalStepPatchTabButtons[static_cast<size_t>(i)];
            const bool isSelected = (i == globalStepPatchTabIndex);
            button.setToggleState(isSelected, juce::dontSendNotification);
            styleUiButton(button, isSelected);
        }
        suppressGlobalStepPatchTabChange = false;
    }

    if (applyToStrips && tabChanged)
        applyGlobalStepPatchTabToAllStrips();
}

void StepVstHostAudioProcessorEditor::applyGlobalStepPatchTabToAllStrips()
{
    for (auto* strip : stripControls)
    {
        if (strip != nullptr)
            strip->setStepPatchTabIndex(globalStepPatchTabIndex);
    }
}

void StepVstHostAudioProcessorEditor::setupGlobalStepToolControls(juce::Component& parent)
{
    globalStepToolLabel.setText("Step Tool", juce::dontSendNotification);
    globalStepToolLabel.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    globalStepToolLabel.setColour(juce::Label::textColourId, kTextMuted);
    globalStepToolLabel.setJustificationType(juce::Justification::centredLeft);
    parent.addAndMakeVisible(globalStepToolLabel);

    static constexpr int kToolButtonRadioGroup = 0x53544550; // "STEP"
    static constexpr std::array<const char*, GlobalStepToolButtonCount> kToolLabels {
        "Vol", "Divide", "Ramp+", "Ramp-", "Prob", "Select"
    };
    static constexpr std::array<const char*, GlobalStepToolButtonCount> kToolTooltips {
        "Global step-edit tool: Volume draw",
        "Global step-edit tool: Divide",
        "Global step-edit tool: Ramp up",
        "Global step-edit tool: Ramp down",
        "Global step-edit tool: Probability",
        "Global step-edit tool: Select"
    };

    for (int i = 0; i < GlobalStepToolButtonCount; ++i)
    {
        auto& button = globalStepToolButtons[static_cast<size_t>(i)];
        button.setButtonText(kToolLabels[static_cast<size_t>(i)]);
        button.setTooltip(kToolTooltips[static_cast<size_t>(i)]);
        button.setClickingTogglesState(true);
        button.setRadioGroupId(kToolButtonRadioGroup);
        styleUiButton(button, false);
        const int toolId = i + 1;
        button.onClick = [this, toolId]()
        {
            if (suppressGlobalStepToolChange)
                return;
            setGlobalStepTool(stepToolFromComboId(toolId), true, true);
        };
        parent.addAndMakeVisible(button);
    }

    globalStepPatchTabLabel.setText("Patch Tab", juce::dontSendNotification);
    globalStepPatchTabLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    globalStepPatchTabLabel.setColour(juce::Label::textColourId, kTextMuted);
    globalStepPatchTabLabel.setJustificationType(juce::Justification::centredLeft);
    parent.addAndMakeVisible(globalStepPatchTabLabel);

    static constexpr std::array<const char*, GlobalStepPatchTabCount> kPatchTabLabels {
        "MIX", "SHAPE", "OSC", "NOISE"
    };
    static constexpr std::array<const char*, GlobalStepPatchTabCount> kPatchTabTooltips {
        "Global step patch tab: MIX",
        "Global step patch tab: SHAPE",
        "Global step patch tab: OSC",
        "Global step patch tab: NOISE"
    };

    for (int i = 0; i < GlobalStepPatchTabCount; ++i)
    {
        auto& button = globalStepPatchTabButtons[static_cast<size_t>(i)];
        button.setButtonText(kPatchTabLabels[static_cast<size_t>(i)]);
        button.setTooltip(kPatchTabTooltips[static_cast<size_t>(i)]);
        button.setClickingTogglesState(true);
        styleUiButton(button, false);
        button.onClick = [this, i]()
        {
            if (suppressGlobalStepPatchTabChange)
                return;
            setGlobalStepPatchTab(i, true, true);
        };
        parent.addAndMakeVisible(button);
    }

    setGlobalStepTool(globalStepTool, true, true);
    setGlobalStepPatchTab(globalStepPatchTabIndex, true, true);
    layoutGlobalStepToolControls();
}

void StepVstHostAudioProcessorEditor::layoutGlobalStepToolControls()
{
    auto bounds = globalStepToolBar.getLocalBounds().reduced(8, 3);
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    const int rowGap = 2;
    auto toolRow = bounds.removeFromTop(22);
    if (bounds.getHeight() > rowGap)
        bounds.removeFromTop(rowGap);
    auto patchRow = bounds.removeFromTop(22);

    globalStepToolLabel.setBounds(toolRow.removeFromLeft(76));
    toolRow.removeFromLeft(6);

    constexpr int kButtonGap = 4;
    const int totalGap = kButtonGap * juce::jmax(0, GlobalStepToolButtonCount - 1);
    const int buttonWidth = juce::jmax(1, (toolRow.getWidth() - totalGap) / GlobalStepToolButtonCount);
    for (int i = 0; i < GlobalStepToolButtonCount; ++i)
    {
        auto& button = globalStepToolButtons[static_cast<size_t>(i)];
        button.setBounds(toolRow.removeFromLeft(buttonWidth));
        if (i < (GlobalStepToolButtonCount - 1))
            toolRow.removeFromLeft(kButtonGap);
    }

    // Match the right-side strip control column so MIX aligns above per-strip mute.
    const int stripLayoutWidth = juce::jmax(1, globalStepToolBar.getWidth() - 4);
    const int preferredControlsWidth = juce::roundToInt(static_cast<float>(stripLayoutWidth) * 0.31f);
    const int controlsWidth = juce::jlimit(250, 320, preferredControlsWidth);
    const int patchButtonsWidth = juce::jlimit(
        1,
        patchRow.getWidth(),
        juce::jmax(120, controlsWidth - 10));
    auto patchButtonsRow = patchRow.removeFromRight(patchButtonsWidth);

    // Keep the toolbar clean: tabs are self-explanatory once right-anchored.
    globalStepPatchTabLabel.setBounds({});

    const int patchTotalGap = kButtonGap * juce::jmax(0, GlobalStepPatchTabCount - 1);
    const int patchButtonWidth = juce::jmax(
        1,
        (patchButtonsRow.getWidth() - patchTotalGap) / GlobalStepPatchTabCount);
    for (int i = 0; i < GlobalStepPatchTabCount; ++i)
    {
        auto& button = globalStepPatchTabButtons[static_cast<size_t>(i)];
        button.setBounds(patchButtonsRow.removeFromLeft(patchButtonWidth));
        if (i < (GlobalStepPatchTabCount - 1))
            patchButtonsRow.removeFromLeft(kButtonGap);
    }
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
    beatSpaceControl = std::make_unique<BeatSpaceControlPanel>(audioProcessor);
    mixControl = std::make_unique<MixControlPanel>(audioProcessor);
    macroControl = std::make_unique<MacroControlPanel>(audioProcessor);
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
    topTabs->addTab("BeatSpace", juce::Colour(0xffedf3fa), beatSpaceControl.get(), false);
    topTabs->addTab("Mix", juce::Colour(0xffedf3fa), mixControl.get(), false);
    topTabs->addTab("Macro", juce::Colour(0xffedf3fa), macroControl.get(), false);
    topTabs->setTabBarDepth(28);
    topTabs->setCurrentTabIndex(kTopTabBeatSpaceIndex);  // BeatSpace visible by default
    lastTopTabIndex = topTabs->getCurrentTabIndex();
    addAndMakeVisible(*topTabs);
    addAndMakeVisible(*monomePagesControl);
    
    // Helper panel classes for main tabs
    struct PlayPanel : public juce::Component
    {
        juce::OwnedArray<StripControl>& strips;
        int& visibleStripCount;
        juce::Component* globalTools = nullptr;

        PlayPanel(juce::OwnedArray<StripControl>& s, int& visibleCount, juce::Component* globalToolsComponent)
            : strips(s), visibleStripCount(visibleCount), globalTools(globalToolsComponent) {}
        
        void resized() override
        {
            auto bounds = getLocalBounds();
            const int gap = 0;
            if (globalTools != nullptr)
            {
                const auto toolsArea = bounds.removeFromTop(52);
                globalTools->setBounds(toolsArea.reduced(0, 1));
                bounds.removeFromTop(gap);
            }

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
                    const int y = bounds.getY() + (i * (stripHeight + gap));
                    strip->setBounds(bounds.getX(), y, bounds.getWidth(), stripHeight);
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
    auto* playPanel = new PlayPanel(stripControls, activeGuiStripCount, &globalStepToolBar);
    setupGlobalStepToolControls(globalStepToolBar);
    playPanel->addAndMakeVisible(globalStepToolBar);
    for (int i = 0; i < kTotalSampleStrips; ++i)
    {
        auto* strip = new StripControl(i, audioProcessor);
        strip->setStepToolbarVisible(false);
        strip->setStepEditTool(globalStepTool);
        strip->setStepPatchTabIndex(globalStepPatchTabIndex);
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
    const auto binaryTimestamp = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getLastModificationTime();
    const juce::String buildInfo = "v" + juce::String(JucePlugin_VersionString)
        + " | bin " + binaryTimestamp.toString(true, true, true, true);
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
    
    // Top section: tabbed control panels.
    const int currentTopTab = topTabs->getCurrentTabIndex();
    const bool beatSpaceTabActive = (currentTopTab == kTopTabBeatSpaceIndex);
    const bool mixTabActive = (currentTopTab == kTopTabMixIndex);
    const bool macroTabActive = (currentTopTab == kTopTabMacroIndex);
    const int topBarHeight = (beatSpaceTabActive && beatSpaceControl != nullptr)
        ? beatSpaceControl->getPreferredTopSectionHeight(bounds.getHeight())
        : (mixTabActive && mixControl != nullptr)
            ? mixControl->getPreferredTopSectionHeight(bounds.getHeight())
        : (macroTabActive && macroControl != nullptr)
            ? macroControl->getPreferredTopSectionHeight(bounds.getHeight())
            : juce::jlimit(
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
    layoutGlobalStepToolControls();
}

//==============================================================================

void StepVstHostAudioProcessorEditor::timerCallback()
{
    int currentTopTabIndex = -1;
    if (topTabs != nullptr)
    {
        currentTopTabIndex = topTabs->getCurrentTabIndex();
        if (currentTopTabIndex != lastTopTabIndex)
        {
            lastTopTabIndex = currentTopTabIndex;
            resized();
        }
    }

    // Startup guard: ensure global patch tabs are laid out even before a tab switch.
    if (mainTabs != nullptr
        && mainTabs->getCurrentTabIndex() == 0
        && globalStepToolBar.getWidth() > 0
        && globalStepPatchTabButtons[0].getWidth() <= 1)
    {
        layoutGlobalStepToolControls();
    }

    if (!audioProcessor.getAudioEngine())
        return;
    
    if (globalControl && currentTopTabIndex == 0)
    {
        globalControl->refreshFromProcessor();
    }
    if (beatSpaceControl && currentTopTabIndex == kTopTabBeatSpaceIndex)
        beatSpaceControl->refreshFromProcessor();
    if (mixControl && currentTopTabIndex == kTopTabMixIndex)
        mixControl->refreshFromProcessor();
    if (macroControl && currentTopTabIndex == kTopTabMacroIndex)
        macroControl->refreshFromProcessor();

    if (presetControl && currentTopTabIndex == 1)
        presetControl->refreshVisualState();

    const bool modulationActive = audioProcessor.isControlModeActive()
        && audioProcessor.getCurrentControlMode() == StepVstHostAudioProcessor::ControlMode::Modulation;
    const int detectedStripCount = getDetectedGuiStripCount();
    const bool stripCountChanged = (detectedStripCount != activeGuiStripCount);
    setActiveGuiStripCount(detectedStripCount, false);

    // Undo strip-refresh throttling: keep strip UIs in sync every editor tick.
    for (int i = 0; i < stripControls.size(); ++i)
    {
        if (auto* strip = stripControls[i])
        {
            if (i < activeGuiStripCount)
                strip->updateFromEngine();
        }
    }

    if (modulationActive != lastModulationLaneViewActive || stripCountChanged)
    {
        for (int i = 0; i < stripControls.size(); ++i)
        {
            if (auto* strip = stripControls[i])
            {
                const bool isVisibleStrip = i < activeGuiStripCount;
                strip->setModulationLaneView(modulationActive && isVisibleStrip);
            }
        }
        lastModulationLaneViewActive = modulationActive;
    }

    if (audioProcessor.isStepEditModeActive())
    {
        setGlobalStepTool(
            stepToolFromProcessorIndex(audioProcessor.getStepEditToolIndex()),
            true,
            true);
    }

    const uint32_t refreshToken = audioProcessor.getPresetRefreshToken();
    if (refreshToken != lastPresetRefreshToken)
    {
        lastPresetRefreshToken = refreshToken;
        for (auto* strip : stripControls)
        {
            if (strip == nullptr)
                continue;
            strip->updateFromEngine();
            strip->repaint();
        }
        for (auto* fxStrip : fxStripControls)
            if (fxStrip) fxStrip->repaint();
        repaint();
    }
    
    // Hidden Monome GUI grid should not consume refresh time.
    if (auto& monome = audioProcessor.getMonomeConnection();
        monome.isConnected() && monomeGrid && monomeGrid->isShowing())
    {
        monomeGrid->updateFromEngine();
    }
}
