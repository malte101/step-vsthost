#include "MonomeGroupAssignActions.h"
#include "AudioEngine.h"

namespace MonomeGroupAssignActions
{
bool handleButtonPress(ModernAudioEngine& audioEngine, int stripIndex, int x)
{
    auto* strip = audioEngine.getStrip(stripIndex);
    if (!strip)
        return false;

    // Mode page: playback direction mode selection only.
    // 6=Normal, 7=Reverse, 8=PingPong, 9=Random, 10=RandomWalk, 11=RandomSlice
    if (x >= 6 && x <= 11)
    {
        switch (x - 6)
        {
            case 0: strip->setDirectionMode(EnhancedAudioStrip::DirectionMode::Normal); break;
            case 1: strip->setDirectionMode(EnhancedAudioStrip::DirectionMode::Reverse); break;
            case 2: strip->setDirectionMode(EnhancedAudioStrip::DirectionMode::PingPong); break;
            case 3: strip->setDirectionMode(EnhancedAudioStrip::DirectionMode::Random); break;
            case 4: strip->setDirectionMode(EnhancedAudioStrip::DirectionMode::RandomWalk); break;
            case 5: strip->setDirectionMode(EnhancedAudioStrip::DirectionMode::RandomSlice); break;
            default: return false;
        }

        return true;
    }

    return false;
}

void renderRow(const EnhancedAudioStrip& strip, int y, int newLedState[16][16])
{
    for (int x = 0; x < 16; ++x)
        newLedState[x][y] = 0;

    const auto directionMode = strip.getDirectionMode();
    newLedState[6][y] = (directionMode == EnhancedAudioStrip::DirectionMode::Normal) ? 15 : 4;
    newLedState[7][y] = (directionMode == EnhancedAudioStrip::DirectionMode::Reverse) ? 15 : 4;
    newLedState[8][y] = (directionMode == EnhancedAudioStrip::DirectionMode::PingPong) ? 15 : 4;
    newLedState[9][y] = (directionMode == EnhancedAudioStrip::DirectionMode::Random) ? 15 : 4;
    newLedState[10][y] = (directionMode == EnhancedAudioStrip::DirectionMode::RandomWalk) ? 15 : 4;
    newLedState[11][y] = (directionMode == EnhancedAudioStrip::DirectionMode::RandomSlice) ? 15 : 4;
}
} // namespace MonomeGroupAssignActions
