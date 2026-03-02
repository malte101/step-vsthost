#include "MonomeFileBrowserActions.h"
#include "PluginProcessor.h"

namespace MonomeFileBrowserActions
{
namespace
{
constexpr int kPrevButton = 0;
constexpr int kNextButton = 1;
constexpr int kFavoriteFirstButton = 3; // x2 intentionally left empty as visual divider
constexpr int kFavoriteButtonCount = StepVstHostAudioProcessor::BrowserFavoriteSlots;
constexpr int kBarsFirstButton = 11;
constexpr int kBarsLastButton = 14;
}

void handleButtonPress(StepVstHostAudioProcessor& processor, EnhancedAudioStrip& strip, int stripIndex, int x)
{
    juce::ignoreUnused(strip);
    if (x == kPrevButton)
    {
        if (!processor.loadAdjacentBeatSpacePresetForAssignedSpace(stripIndex, -1))
        {
            processor.loadAdjacentFile(stripIndex, -1);  // Prev
            processor.queueHostedProgramChangeForStrip(stripIndex, -1);
        }
    }
    else if (x == kNextButton)
    {
        if (!processor.loadAdjacentBeatSpacePresetForAssignedSpace(stripIndex, 1))
        {
            processor.loadAdjacentFile(stripIndex, 1);   // Next
            processor.queueHostedProgramChangeForStrip(stripIndex, 1);
        }
    }
    else if (x >= kFavoriteFirstButton && x < (kFavoriteFirstButton + kFavoriteButtonCount))
        processor.beginBrowserFavoritePadHold(stripIndex, x - kFavoriteFirstButton);
    else if (x >= kBarsFirstButton && x <= kBarsLastButton)
    {
        // Button 11=1, 12=2, 13=4, 14=8 bars
        int bars = 1;
        if (x == 12) bars = 2;
        else if (x == 13) bars = 4;
        else if (x == 14) bars = 8;

        processor.requestBarLengthChange(stripIndex, bars);
    }
}

void handleButtonRelease(StepVstHostAudioProcessor& processor, EnhancedAudioStrip& strip, int stripIndex, int x)
{
    juce::ignoreUnused(strip);
    if (x >= kFavoriteFirstButton && x < (kFavoriteFirstButton + kFavoriteButtonCount))
        processor.endBrowserFavoritePadHold(stripIndex, x - kFavoriteFirstButton);
}

void renderRow(const StepVstHostAudioProcessor& processor, const ModernAudioEngine& engine, const EnhancedAudioStrip& strip, int stripIndex, int y, int newLedState[16][16])
{
    // File browser controls (always visible)
    newLedState[kPrevButton][y] = 8;  // Prev
    newLedState[kNextButton][y] = 8;  // Next

    const auto nowMs = juce::Time::getMillisecondCounter();
    const auto activeDirectory = processor.getCurrentBrowserDirectoryForStrip(stripIndex);
    const bool hasActiveDirectory = activeDirectory.exists() && activeDirectory.isDirectory();

    for (int slot = 0; slot < kFavoriteButtonCount; ++slot)
    {
        const int buttonX = kFavoriteFirstButton + slot;
        const auto favoriteDirectory = processor.getBrowserFavoriteDirectory(slot);
        const bool saveBurstActive = processor.isBrowserFavoriteSaveBurstActive(slot, nowMs);
        const bool missingBurstActive = processor.isBrowserFavoriteMissingBurstActive(slot, nowMs);

        int level = 2;
        if (saveBurstActive)
        {
            const bool burstOn = ((nowMs / 45u) & 1u) == 0u;
            level = burstOn ? 15 : 0;
        }
        else if (missingBurstActive)
        {
            const bool burstOn = ((nowMs / 90u) & 1u) == 0u;
            level = burstOn ? 11 : 0;
        }
        else if (favoriteDirectory.exists() && favoriteDirectory.isDirectory())
        {
            const bool matchesActiveDirectory = hasActiveDirectory && (favoriteDirectory == activeDirectory);
            const bool held = processor.isBrowserFavoritePadHeld(stripIndex, slot);
            if (held)
                level = 13;
            else if (matchesActiveDirectory)
                level = 14;
            else
                level = 8;
        }

        newLedState[buttonX][y] = level;
    }

    double beatPos = engine.getCurrentBeat();
    if (!std::isfinite(beatPos) || beatPos < 0.0)
        beatPos = 0.0;

    const double beatFraction = beatPos - std::floor(beatPos);

    // Buttons 11-14: Loop length selector (1, 2, 4, 8 bars)
    const int selectedBars = strip.getRecordingBars();

    for (int buttonX = kBarsFirstButton; buttonX <= kBarsLastButton; ++buttonX)
    {
        int bars = 1;
        if (buttonX == 12) bars = 2;
        else if (buttonX == 13) bars = 4;
        else if (buttonX == 14) bars = 8;

        if (bars == selectedBars)
        {
            double blinkPhase = 0.0;
            if (bars == 1)
                blinkPhase = beatFraction;
            else if (bars == 2)
                blinkPhase = beatFraction;        // 1x speed
            else if (bars == 4)
                blinkPhase = (beatPos / 4.0) - std::floor(beatPos / 4.0);
            else
                blinkPhase = (beatPos / 8.0) - std::floor(beatPos / 8.0);

            double lengthPulse = (std::sin((blinkPhase - 0.5) * 2.0 * 3.14159) + 1.0) / 2.0;
            lengthPulse = juce::jlimit(0.0, 1.0, lengthPulse);

            newLedState[buttonX][y] = 6 + static_cast<int>(lengthPulse * 6.0);
        }
        else
        {
            newLedState[buttonX][y] = 3;
        }
    }
    newLedState[15][y] = 0;
}
} // namespace MonomeFileBrowserActions
