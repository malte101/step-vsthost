/*
  ==============================================================================

    PluginProcessor.cpp
    step-vsthost - Modern Edition Implementation

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PlayheadSpeedQuantizer.h"
#include "PresetStore.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>

namespace
{
constexpr bool kEnableTriggerDebugLogging = false;
constexpr bool kEnableGlobalSettingsDiagnostics = false;

struct BarSelection
{
    int recordingBars = 1;
    float beatsPerLoop = 4.0f;
};

BarSelection decodeBarSelection(int value)
{
    switch (value)
    {
        case 25:  return { 1, 1.0f };   // 1/4 bar
        case 50:  return { 1, 2.0f };   // 1/2 bar
        case 100: return { 1, 4.0f };   // 1 bar
        case 200: return { 2, 8.0f };   // 2 bars
        case 400: return { 4, 16.0f };  // 4 bars
        case 800: return { 8, 32.0f };  // 8 bars
        // Backward compatibility (monome and legacy callers)
        case 1:   return { 1, 4.0f };
        case 2:   return { 2, 8.0f };
        case 4:   return { 4, 16.0f };
        case 8:   return { 8, 32.0f };
        default:  return { 1, 4.0f };
    }
}

juce::String controlModeToKey(StepVstHostAudioProcessor::ControlMode mode)
{
    switch (mode)
    {
        case StepVstHostAudioProcessor::ControlMode::Speed: return "speed";
        case StepVstHostAudioProcessor::ControlMode::Pitch: return "pitch";
        case StepVstHostAudioProcessor::ControlMode::Pan: return "pan";
        case StepVstHostAudioProcessor::ControlMode::Volume: return "volume";
        case StepVstHostAudioProcessor::ControlMode::Length: return "length";
        case StepVstHostAudioProcessor::ControlMode::Filter: return "filter";
        case StepVstHostAudioProcessor::ControlMode::Swing: return "swing";
        case StepVstHostAudioProcessor::ControlMode::Gate: return "gate";
        case StepVstHostAudioProcessor::ControlMode::FileBrowser: return "browser";
        case StepVstHostAudioProcessor::ControlMode::GroupAssign: return "group";
        case StepVstHostAudioProcessor::ControlMode::Modulation: return "modulation";
        case StepVstHostAudioProcessor::ControlMode::BeatSpace: return "beatspace";
        case StepVstHostAudioProcessor::ControlMode::Preset: return "preset";
        case StepVstHostAudioProcessor::ControlMode::StepEdit: return "stepedit";
        case StepVstHostAudioProcessor::ControlMode::Normal:
        default: return "normal";
    }
}

bool controlModeFromKey(const juce::String& key, StepVstHostAudioProcessor::ControlMode& mode)
{
    const auto normalized = key.trim().toLowerCase();
    if (normalized == "speed") { mode = StepVstHostAudioProcessor::ControlMode::Speed; return true; }
    if (normalized == "pitch") { mode = StepVstHostAudioProcessor::ControlMode::Pitch; return true; }
    if (normalized == "pan") { mode = StepVstHostAudioProcessor::ControlMode::Pan; return true; }
    if (normalized == "volume") { mode = StepVstHostAudioProcessor::ControlMode::Volume; return true; }
    if (normalized == "length" || normalized == "len") { mode = StepVstHostAudioProcessor::ControlMode::Length; return true; }
    if (normalized == "grainsize" || normalized == "grain_size" || normalized == "grain")
    {
        mode = StepVstHostAudioProcessor::ControlMode::Length; // legacy mapping
        return true;
    }
    if (normalized == "filter") { mode = StepVstHostAudioProcessor::ControlMode::Filter; return true; }
    if (normalized == "swing") { mode = StepVstHostAudioProcessor::ControlMode::Swing; return true; }
    if (normalized == "gate") { mode = StepVstHostAudioProcessor::ControlMode::Gate; return true; }
    if (normalized == "browser") { mode = StepVstHostAudioProcessor::ControlMode::FileBrowser; return true; }
    if (normalized == "group") { mode = StepVstHostAudioProcessor::ControlMode::GroupAssign; return true; }
    if (normalized == "mod" || normalized == "modulation") { mode = StepVstHostAudioProcessor::ControlMode::Modulation; return true; }
    if (normalized == "beatspace" || normalized == "beat_space" || normalized == "beat") { mode = StepVstHostAudioProcessor::ControlMode::BeatSpace; return true; }
    if (normalized == "preset") { mode = StepVstHostAudioProcessor::ControlMode::Preset; return true; }
    if (normalized == "stepedit" || normalized == "step_edit" || normalized == "step") { mode = StepVstHostAudioProcessor::ControlMode::StepEdit; return true; }
    return false;
}

juce::File getBeatSpaceScriptDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library")
        .getChildFile("Application Support")
        .getChildFile("Sonic Charge")
        .getChildFile("Microtonic Scripts")
        .getChildFile("BeatSpace.mtscript");
}

juce::File resolveBeatSpaceMapFile(const juce::File& scriptDir, const juce::String& decoderName)
{
    juce::StringArray mapCandidates;
    if (decoderName.isNotEmpty())
    {
        mapCandidates.addIfNotAlreadyThere(decoderName + "_map_x2.png");
        mapCandidates.addIfNotAlreadyThere(decoderName + "_map.png");
    }
    mapCandidates.addIfNotAlreadyThere("marigoldG_map_x2.png");
    mapCandidates.addIfNotAlreadyThere("marigoldG_map.png");

    for (const auto& candidate : mapCandidates)
    {
        auto file = scriptDir.getChildFile(candidate);
        if (file.existsAsFile())
            return file;
    }
    return {};
}

juce::Image loadBeatSpaceMapImage(const juce::File& scriptDir, const juce::String& decoderName)
{
    const auto mapFile = resolveBeatSpaceMapFile(scriptDir, decoderName);
    if (mapFile == juce::File())
        return {};

    auto mapImage = juce::ImageFileFormat::loadFrom(mapFile);
    if (!mapImage.isValid() || mapImage.getWidth() < 2 || mapImage.getHeight() < 2)
        return {};

    return mapImage;
}

constexpr int kSitePatternDefaultSteps = 16;
constexpr int kSitePatternCategoryCount = StepVstHostAudioProcessor::BeatSpaceChannels;

struct SiteDrumPatternPresetData
{
    const char* source = "";
    const char* genre = "";
    const char* name = "";
    int bpm = 120;
    int steps = kSitePatternDefaultSteps;
    std::array<const char*, kSitePatternCategoryCount> categoryMasks{};
    std::array<const char*, kSitePatternCategoryCount> categoryVelocityMasks{};
};

constexpr std::array<SiteDrumPatternPresetData, 21> kSiteDrumPatternPresets {{
    { "Drumpatterns", "Breakbeat", "Ashleys Roachclip", 136, 16, { "X-X---X--XX-----", "----X-------X---", "X-X-X-X-X---X-X-", "----------X-----", "X-X-X-X-X-X-X-X-", "----------------" } },
    { "Drumpatterns", "Electro", "Da Funk", 111, 16, { "X---X---X---X---", "----X-------X---", "X-X-X-X-X-X-X-XX", "----------------", "----------------", "----------------" } },
    { "Drumpatterns", "Electro Funk", "Dont Stop The Rock", 112, 16, { "X-----X---------", "----X-------X---", "X-XXX-XXX-XX-XXX", "----------------", "X--XX--X-XX-X--X", "----------------" } },
    { "Drumpatterns", "Electro Pop", "Freeez IOU", 125, 16, { "X---X---X---X---", "----X-------X---", "X-XXX-XXX-XXXXXX", "----------------", "XXXXXXXXXXXXXXXX", "----------------" } },
    { "Drumpatterns", "Hip Hop", "Boogie Down Bronx", 103, 16, { "------------XXXX", "----X-------X---", "----------------", "----------------", "XXXXXXXXXXXXXXXX", "----------------" } },
    { "Drumpatterns", "House", "House Pattern 10a", 125, 16, { "X---X---X--X--X-", "X---X---X---X---", "----------------", "----------------", "----------------", "----------------" } },
    { "Drumpatterns", "Miami Bass", "Miami Bass P1", 130, 16, { "X-----X---------", "----X-------X---", "X-X-X-X---X-X-X-", "--------X-------", "----------------", "----------------" } },
    { "Drumpatterns", "Pop", "Billie Jean", 117, 16, { "X-------X-------", "----X-------X---", "X-X-X-X-X-X-X-X-", "----------------", "X-X-X-X-X-X-X-X-", "----------------" } },
    { "Drumpatterns", "R&B", "Let The Music Play", 117, 16, { "X---------X-----", "----X-------X---", "XXXXXXXXXXXXXXXX", "----------------", "XX------XX-XX--X", "----------------" } },
    { "Drumpatterns", "Reggae", "Reggaeton", 94, 16, { "X---X---X---X---", "--X----X--X----X", "X-X-X-X-X-X-X-X-", "----------------", "----X-------X---", "----------------" } },
    { "Drumpatterns", "Rock", "Another One Bites The Dust", 110, 16, { "X---X---X---X---", "----X-------X---", "--X-X-X-X-X-X-X-", "----------------", "----------------", "X---------------" } },
    { "Drumpatterns", "Techno", "Techno Minimal", 128, 16, { "X---X---X---X---", "----------------", "-XXX-XXX-XXX-XXX", "X---X---X---X---", "XXXXXXXXXXXXXXXX", "X--X-X-X--X---X-" } },
    { "Funklet", "Funklet", "Bobby Byrd - I Got The Feelin", 130, 32,
      { "X-X-------X-------X-----X---X-X-", "------X--X----X--X--XX-X-XXX-XXX", "X---X-X-X---X-X-X-X-X-X---X-X-X-", "--X-------X-------------X-------", "--------------------------------", "--------------------------------" },
      { "30400000004000000040000030003030", "00000040020000400200420302120121", "20002020200020202020202000202020", "00200000002000000000000020000000", "00000000000000000000000000000000", "00000000000000000000000000000000" } },
    { "Funklet", "Funklet", "Clyde Stubblefield - Mother Popcorn", 117, 32,
      { "X-X-------X-------X---X---X---X-", "----X--X-X----X--X-XXX-X-X-XXX-X", "X---X---X---X---X---X---X---X---", "--------------------------------", "--------------------------------", "--------------------------------" },
      { "30300000003000000030003000300030", "00004002010000400201320104013202", "40004000400040004000400040004000", "00000000000000000000000000000000", "00000000000000000000000000000000", "00000000000000000000000000000000" } },
    { "Funklet", "Funklet", "Zigaboo Modeliste - Cissy Strut", 89, 32,
      { "X-------X--X----X--X-X--XX-X-X--", "----X-------X-X-----X-------X-X-", "X---X--X--X-X-X--XX-X--X-XX-X-X-", "--------------------------------", "--------------------------------", "--------------------------------" },
      { "40000000400400004004040034040400", "00004000000040400000400000004040", "40003004004040400430400404304040", "00000000000000000000000000000000", "00000000000000000000000000000000", "00000000000000000000000000000000" } },
    { "Funklet", "Funklet", "James Gadson - Use Me", 78, 32,
      { "X-X--X-XX-XX-X-XX-X--X--XX-X-X-X", "----X--X-X--X--X----X--X-------X", "XXXXXXXXXXXXXXXXXXXXXXXXX------X", "-------------------------X-X-X--", "--------------------------------", "--------------------------------" },
      { "30300302303303023030030023030302", "00004004040040040000400400000004", "21212121212121212121212120000003", "00000000000000000000000002030300", "00000000000000000000000000000000", "00000000000000000000000000000000" } },
    { "Funklet", "Funklet", "Max Roach - Come Dancing", 96, 32,
      { "X------XX------XX-X--X-XX------X", "-XX-XXX--XX-XXX--X--XX---X--XXX-", "X-X-X-X-X-X-X-X-X-X-X-X-X---X-X-", "--------------------------X-----", "--------------------------------", "--------------------------------" },
      { "40000003400000034040040340000003", "01204210012042100200420004004210", "30203020302030203020302020003020", "00000000000000000000000000400000", "00000000000000000000000000000000", "00000000000000000000000000000000" } },
    { "Funklet", "Funklet", "Jabo Starks - Hot Sweat", 115, 32,
      { "X---------X-------XX------XX--X-", "----X--XXX--X-XX-X-XXX-X-X-XXX--", "X-X-X-X-X-X-X-X-X-XXX-X-X-XXX-X-", "--------------------------------", "--------------------------------", "--------------------------------" },
      { "40000000004000000034000000340040", "00004002110020410101420101014100", "30404010402020202021204040212020", "00000000000000000000000000000000", "00000000000000000000000000000000", "00000000000000000000000000000000" } },
    { "Funklet", "Funklet", "The Unknown Drummer - The Unknown Drummer", 110, 32,
      { "X--X--X---X-----X--X--X---X-----", "-X--X--X----X--X-X--X--X----X--X", "-XX-XX-X--X--XXX-XX-XX-X--X--XXX", "--------X---------------X-------", "--------------------------------", "--------------------------------" },
      { "40040040004000004004004000400000", "01004001000040010100400100004001", "02402402000002240240240200000224", "00000000400000000000000040000000", "00000000000000000000000000000000", "00000000000000000000000000000000" } },
    { "Funklet", "Funklet", "Dennis Davis - Expensive Shit", 125, 32,
      { "---X--X-------XX---X--X---------", "XX---X--XX---X--XX---X--XX--XX--", "X-XXX-XXX-X-X-XXX-X-X-XXX-XXX-XX", "-----------X-------X------------", "--------------------------------", "--------------------------------" },
      { "00020010000000230002002000000000", "32000200310004003200010032002100", "10122021302030122010301130224021", "00000000000100000001000000000000", "00000000000000000000000000000000", "00000000000000000000000000000000" } },
    { "Funklet", "Funklet", "David Garibaldi - Soul Vaccination", 106, 32,
      { "X--X-----X--X---X--X-----X--X---", "-XX-XX-X--XX-X-X-XX-XX-X--XX-X-X", "X-XX-XX-X-XX--X-X-XX-XX-X-XX--X-", "---------X--X------------X--X---", "--------------------------------", "--------------------------------" },
      { "40040000040040004004000004004000", "01401404002101010140140400210101", "40240240200200304024024020020030", "00000000030030000000000003003000", "00000000000000000000000000000000", "00000000000000000000000000000000" } }
}};

std::vector<bool> parseStepMask(const char* mask, int stepCount)
{
    const int clampedStepCount = juce::jlimit(2, 64, stepCount);
    std::vector<bool> steps(static_cast<size_t>(clampedStepCount), false);
    if (mask == nullptr)
        return steps;

    int cursor = 0;
    for (const char* p = mask; (*p != '\0') && (cursor < clampedStepCount); ++p)
    {
        const char c = *p;
        if (c == 'X' || c == 'x' || c == '*' || c == '1' || c == 'O' || c == 'o')
        {
            steps[static_cast<size_t>(cursor++)] = true;
            continue;
        }
        if (c == '-' || c == '.' || c == '_' || c == '0')
        {
            steps[static_cast<size_t>(cursor++)] = false;
            continue;
        }
    }

    return steps;
}

std::vector<float> parseStepVelocityMask(const char* mask, int stepCount)
{
    const int clampedStepCount = juce::jlimit(2, 64, stepCount);
    std::vector<float> steps(static_cast<size_t>(clampedStepCount), 0.0f);
    if (mask == nullptr)
        return steps;

    int cursor = 0;
    for (const char* p = mask; (*p != '\0') && (cursor < clampedStepCount); ++p)
    {
        const char c = *p;
        if (c >= '0' && c <= '9')
        {
            const int digit = juce::jlimit(0, 4, static_cast<int>(c - '0'));
            steps[static_cast<size_t>(cursor++)] = static_cast<float>(digit) / 4.0f;
            continue;
        }
        if (c == 'X' || c == 'x' || c == '*' || c == 'O' || c == 'o')
        {
            steps[static_cast<size_t>(cursor++)] = 1.0f;
            continue;
        }
        if (c == '-' || c == '.' || c == '_')
        {
            steps[static_cast<size_t>(cursor++)] = 0.0f;
            continue;
        }
    }

    return steps;
}

bool isLikelyMicrotonicPlugin(const juce::String& pluginName)
{
    const auto normalized = pluginName.toLowerCase();
    return normalized.contains("microtonic")
        || normalized.contains("sonic charge");
}

constexpr const char* kGlobalSettingsKey = "GlobalSettingsXml";

juce::File getGlobalSettingsDirectory()
{
    // Keep global settings in a dedicated app-specific folder, separate from
    // preset files and independent from other plugin project paths.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("step-vsthost")
        .getChildFile("Settings");
}

juce::File getGlobalSettingsFile()
{
    return getGlobalSettingsDirectory().getChildFile("GlobalSettings.xml");
}

juce::File getBeatSpaceTableCacheFile()
{
    return getGlobalSettingsDirectory().getChildFile("BeatSpaceTableCache.bin");
}

juce::File getLegacyPresetGlobalSettingsFile()
{
    auto presetsRoot = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library")
        .getChildFile("Audio")
        .getChildFile("Presets")
        .getChildFile("step-vsthost")
        .getChildFile("step-vsthost");
    return presetsRoot.getChildFile("GlobalSettings.xml");
}

juce::File getLegacyAppSupportGlobalSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("step-vsthost")
        .getChildFile("GlobalSettings.xml");
}

juce::PropertiesFile::Options getLegacySettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "step-vsthost";
    options.filenameSuffix = "settings";
    options.folderName = "";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

std::unique_ptr<juce::XmlElement> loadGlobalSettingsXml()
{
    const auto settingsFile = getGlobalSettingsFile();
    if (settingsFile.existsAsFile())
    {
        if (auto xml = juce::XmlDocument::parse(settingsFile))
            return xml;
    }

    // Migrate from legacy locations when needed.
    const std::array<juce::File, 2> legacyFiles {
        getLegacyPresetGlobalSettingsFile(),
        getLegacyAppSupportGlobalSettingsFile()
    };
    for (const auto& legacySettingsFile : legacyFiles)
    {
        if (!legacySettingsFile.existsAsFile())
            continue;
        if (auto xml = juce::XmlDocument::parse(legacySettingsFile))
        {
            auto settingsDir = settingsFile.getParentDirectory();
            if (!settingsDir.exists())
                settingsDir.createDirectory();
            xml->writeTo(settingsFile);
            return xml;
        }
    }

    juce::PropertiesFile legacyProps(getLegacySettingsOptions());
    if (legacyProps.isValidFile())
    {
        if (auto xml = legacyProps.getXmlValue(kGlobalSettingsKey))
        {
            auto settingsDir = settingsFile.getParentDirectory();
            if (!settingsDir.exists())
                settingsDir.createDirectory();
            xml->writeTo(settingsFile);
            return xml;
        }
    }

    return nullptr;
}

void saveGlobalSettingsXml(const juce::XmlElement& xml)
{
    auto settingsFile = getGlobalSettingsFile();
    auto settingsDir = settingsFile.getParentDirectory();
    if (!settingsDir.exists())
        settingsDir.createDirectory();
    xml.writeTo(settingsFile);

    const std::array<juce::File, 2> compatibilityFiles {
        getLegacyPresetGlobalSettingsFile(),
        getLegacyAppSupportGlobalSettingsFile()
    };
    for (const auto& compatibilityFile : compatibilityFiles)
    {
        if (compatibilityFile == settingsFile)
            continue;
        auto compatibilityDir = compatibilityFile.getParentDirectory();
        if (!compatibilityDir.exists())
            compatibilityDir.createDirectory();
        xml.writeTo(compatibilityFile);
    }

    // Keep the JUCE properties mirror for backward compatibility only.
    juce::PropertiesFile legacyProps(getLegacySettingsOptions());
    if (legacyProps.isValidFile())
    {
        legacyProps.setValue(kGlobalSettingsKey, &xml);
        legacyProps.saveIfNeeded();
    }
}

void appendGlobalSettingsDiagnostic(const juce::String& tag, const juce::XmlElement* xml)
{
    if (!kEnableGlobalSettingsDiagnostics)
        return;

    auto diagFile = getGlobalSettingsFile().getParentDirectory().getChildFile("GlobalSettings_diag.txt");
    juce::FileOutputStream stream(diagFile, 1024);
    if (!stream.openedOk())
        return;
    stream.setPosition(diagFile.getSize());
    stream << "=== " << tag << " @ " << juce::Time::getCurrentTime().toString(true, true) << " ===\n";
    stream << "Path: " << getGlobalSettingsFile().getFullPathName() << "\n";
    stream << "Exists: " << (getGlobalSettingsFile().existsAsFile() ? "yes" : "no") << "\n";
    if (xml != nullptr)
        stream << "XML: " << xml->toString() << "\n";
    stream << "\n";
    stream.flush();
}

constexpr int kBeatSpaceTableCacheMagic = 0x42535443; // BSTC
constexpr int kBeatSpaceTableCacheVersion = 2;

juce::String hashMemoryBlockFNV1a64(const juce::MemoryBlock& block)
{
    constexpr uint64_t kOffset = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;

    uint64_t hash = kOffset;
    const auto* bytes = static_cast<const uint8_t*>(block.getData());
    for (size_t i = 0; i < block.getSize(); ++i)
    {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= kPrime;
    }

    return juce::String::formatted("%016llx", static_cast<unsigned long long>(hash));
}

template <typename BeatSpaceCellType, int TableSize, int VectorSize>
bool loadBeatSpaceTableCache(
    const juce::File& cacheFile,
    const juce::String& expectedDecoderHash,
    juce::int64 expectedDecoderSourceSize,
    juce::int64 expectedDecoderSourceModifiedMs,
    std::vector<BeatSpaceCellType>& outTable,
    juce::String& outDecoderName)
{
    if (!cacheFile.existsAsFile())
        return false;

    juce::FileInputStream stream(cacheFile);
    if (!stream.openedOk())
        return false;

    const int magic = stream.readInt();
    const int version = stream.readInt();
    if (magic != kBeatSpaceTableCacheMagic || version != kBeatSpaceTableCacheVersion)
        return false;

    const int tableSize = stream.readInt();
    const int vectorSize = stream.readInt();
    if (tableSize != TableSize
        || vectorSize != VectorSize)
    {
        return false;
    }

    const juce::int64 cachedDecoderSourceSize = stream.readInt64();
    const juce::int64 cachedDecoderSourceModifiedMs = stream.readInt64();
    if (expectedDecoderSourceSize >= 0 && cachedDecoderSourceSize != expectedDecoderSourceSize)
        return false;
    if (expectedDecoderSourceModifiedMs >= 0 && cachedDecoderSourceModifiedMs != expectedDecoderSourceModifiedMs)
        return false;

    const auto cachedDecoderHash = stream.readString();
    if (expectedDecoderHash.isNotEmpty() && cachedDecoderHash != expectedDecoderHash)
        return false;

    outDecoderName = stream.readString();

    const int expectedCellCount = TableSize * TableSize;
    const int cachedCellCount = stream.readInt();
    if (cachedCellCount != expectedCellCount)
        return false;

    outTable.clear();
    outTable.resize(static_cast<size_t>(expectedCellCount));

    // Hot path startup optimization: read the whole table in one block instead of
    // issuing one stream read per cell.
    const int totalBytes = static_cast<int>(sizeof(BeatSpaceCellType) * static_cast<size_t>(expectedCellCount));
    if (stream.read(outTable.data(), totalBytes) != totalBytes)
        return false;

    return true;
}

template <typename BeatSpaceCellType, int TableSize, int VectorSize>
bool saveBeatSpaceTableCache(
    const juce::File& cacheFile,
    const juce::String& decoderHash,
    const juce::String& decoderName,
    juce::int64 decoderSourceSize,
    juce::int64 decoderSourceModifiedMs,
    const std::vector<BeatSpaceCellType>& table)
{
    const int expectedCellCount = TableSize * TableSize;
    if (static_cast<int>(table.size()) != expectedCellCount)
        return false;

    auto cacheDir = cacheFile.getParentDirectory();
    if (!cacheDir.exists() && !cacheDir.createDirectory())
        return false;

    juce::TemporaryFile tempFile(cacheFile);
    juce::FileOutputStream stream(tempFile.getFile());
    if (!stream.openedOk())
        return false;

    stream.writeInt(kBeatSpaceTableCacheMagic);
    stream.writeInt(kBeatSpaceTableCacheVersion);
    stream.writeInt(TableSize);
    stream.writeInt(VectorSize);
    stream.writeInt64(decoderSourceSize);
    stream.writeInt64(decoderSourceModifiedMs);
    stream.writeString(decoderHash);
    stream.writeString(decoderName);
    stream.writeInt(expectedCellCount);

    // Hot path startup optimization (cache regeneration path): write the whole
    // table in one block to avoid per-cell stream overhead.
    const int totalBytes = static_cast<int>(sizeof(BeatSpaceCellType) * static_cast<size_t>(expectedCellCount));
    if (!stream.write(table.data(), totalBytes))
        return false;

    stream.flush();
    return tempFile.overwriteTargetFileWithTemporary();
}

constexpr juce::int64 kPersistentGlobalControlsSaveDebounceMs = 350;

constexpr std::array<const char*, 10> kPersistentGlobalControlParameterIds {
    "masterVolume",
    "limiterThreshold",
    "limiterEnabled",
    "quantize",
    "pitchSmoothing",
    "outputRouting",
    "soundTouchEnabled",
    "kitScaleEnabled",
    "kitScaleMode",
    "kitScaleRoot"
};

constexpr int kHostedCcVolume = 7;   // Channel Volume (MSB)
constexpr int kHostedCcPan = 10;     // Pan (MSB)
constexpr int kHostedCcPitch = 74;   // Brightness/timbre; mapped as pitch macro in hosted synth setups
constexpr int kMicrotonicPatchOscAtkIndex = 2;
constexpr int kMicrotonicPatchOscDcyIndex = 3;
constexpr int kMicrotonicPatchNEnvAtkIndex = 12;
constexpr int kMicrotonicPatchNEnvDcyIndex = 13;
constexpr int kMicrotonicPatchOscFreqIndex = 1;
constexpr int kMicrotonicPatchModAmtIndex = 6;
constexpr int kMicrotonicPatchNFilFrqIndex = 8;
constexpr int kMicrotonicPatchLevelIndex = 18;
constexpr int kMicrotonicPatchPanIndex = 19;

constexpr std::array<const char*, 25> kMicrotonicBeatSpacePatchParamOrder {
    "OscWave", "OscFreq", "OscAtk", "OscDcy", "ModMode",
    "ModRate", "ModAmt", "NFilMod", "NFilFrq", "NFilQ",
    "NStereo", "NEnvMod", "NEnvAtk", "NEnvDcy", "Mix",
    "DistAmt", "EQFreq", "EQGain", "Level", "Pan",
    "Output", "Choke", "OscVel", "NVel", "ModVel"
};

constexpr std::array<const char*, StepVstHostAudioProcessor::BeatSpaceChannels> kBeatSpaceSpaceNames {
    "Kick",
    "Snare",
    "Closed Hat",
    "Open Hat",
    "Percussion",
    "Misc"
};

std::array<bool, 12> buildPitchClassMaskForScale(ModernAudioEngine::PitchScale scale, int rootSemitone)
{
    std::array<bool, 12> mask{};
    mask.fill(false);
    const int root = juce::jlimit(0, 11, rootSemitone);
    auto setDegree = [&mask, root](int degree)
    {
        mask[static_cast<size_t>((root + degree) % 12)] = true;
    };

    switch (scale)
    {
        case ModernAudioEngine::PitchScale::Major:
            for (const int degree : std::array<int, 7>{ 0, 2, 4, 5, 7, 9, 11 }) setDegree(degree);
            break;
        case ModernAudioEngine::PitchScale::Minor:
            for (const int degree : std::array<int, 7>{ 0, 2, 3, 5, 7, 8, 10 }) setDegree(degree);
            break;
        case ModernAudioEngine::PitchScale::Dorian:
            for (const int degree : std::array<int, 7>{ 0, 2, 3, 5, 7, 9, 10 }) setDegree(degree);
            break;
        case ModernAudioEngine::PitchScale::PentatonicMinor:
            for (const int degree : std::array<int, 5>{ 0, 3, 5, 7, 10 }) setDegree(degree);
            break;
        case ModernAudioEngine::PitchScale::Chromatic:
        default:
            mask.fill(true);
            break;
    }
    return mask;
}

float quantizeBeatSpaceOscFreqPreserveOctave(float normalized,
                                             int parameterSteps,
                                             ModernAudioEngine::PitchScale scale,
                                             int rootSemitone)
{
    const float clamped = juce::jlimit(0.0f, 1.0f, normalized);
    if (parameterSteps <= 1 || scale == ModernAudioEngine::PitchScale::Chromatic)
        return clamped;

    const int maxStep = parameterSteps - 1;
    if (maxStep < 12)
        return clamped;

    const int rawStep = juce::jlimit(0, maxStep, static_cast<int>(std::lround(clamped * static_cast<float>(maxStep))));
    const int octave = rawStep / 12;
    const auto allowed = buildPitchClassMaskForScale(scale, rootSemitone);

    int bestStep = rawStep;
    int bestDistance = std::numeric_limits<int>::max();
    bool foundCandidate = false;

    for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
    {
        if (!allowed[static_cast<size_t>(pitchClass)])
            continue;
        const int candidateStep = (octave * 12) + pitchClass;
        if (candidateStep < 0 || candidateStep > maxStep)
            continue;
        const int distance = std::abs(candidateStep - rawStep);
        if (!foundCandidate || distance < bestDistance || (distance == bestDistance && candidateStep < bestStep))
        {
            bestStep = candidateStep;
            bestDistance = distance;
            foundCandidate = true;
        }
    }

    if (!foundCandidate)
        return clamped;
    return static_cast<float>(bestStep) / static_cast<float>(maxStep);
}

juce::String normalizeParameterToken(const juce::String& text)
{
    juce::String normalized;
    normalized.preallocateBytes(text.getNumBytesAsUTF8() + 1);
    for (int i = 0; i < text.length(); ++i)
    {
        const juce::juce_wchar c = text[i];
        if (juce::CharacterFunctions::isLetterOrDigit(c))
            normalized += juce::CharacterFunctions::toLowerCase(c);
    }
    return normalized;
}

struct NuXnnLayer
{
    using EvalFn = std::function<std::vector<float>(const std::vector<float>&)>;
    EvalFn eval;
    int outputSize = 0;
    bool valid = false;
};

struct NuXnnModel
{
    juce::String name;
    int inputSize = 0;
    int outputSize = 0;
    NuXnnLayer::EvalFn rootEval;
    bool valid = false;
};

class NuXnnReader
{
public:
    explicit NuXnnReader(const std::vector<uint8_t>& bytesIn) : bytes(bytesIn) {}

    bool canRead(size_t count) const
    {
        return (offset + count) <= bytes.size();
    }

    uint8_t readByte()
    {
        if (!canRead(1))
            return 0;
        return bytes[offset++];
    }

    uint32_t readUInt32()
    {
        if (!canRead(4))
            return 0;
        const uint32_t v = static_cast<uint32_t>(bytes[offset])
            | (static_cast<uint32_t>(bytes[offset + 1]) << 8u)
            | (static_cast<uint32_t>(bytes[offset + 2]) << 16u)
            | (static_cast<uint32_t>(bytes[offset + 3]) << 24u);
        offset += 4;
        return v;
    }

    static float decodeFloat16(uint16_t bits)
    {
        const int exponent = (bits >> 10) & 31;
        const float sign = ((bits & 0x8000u) != 0u) ? -1.0f : 1.0f;
        if (exponent != 0)
        {
            const float mantissa = static_cast<float>((bits & 0x03ffu) + 0x0400u);
            return sign * mantissa * 2.9802322387695312e-8f
                * static_cast<float>(1 << exponent);
        }
        return sign * static_cast<float>(bits & 0x03ffu) * 5.960464477539063e-8f;
    }

    float readFloat16()
    {
        if (!canRead(2))
            return 0.0f;
        const uint16_t bits = static_cast<uint16_t>(bytes[offset])
            | static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8u);
        offset += 2;
        return decodeFloat16(bits);
    }

    float readFloat32()
    {
        if (!canRead(4))
            return 0.0f;
        union
        {
            uint32_t u = 0u;
            float f;
        } cvt;
        cvt.u = readUInt32();
        return cvt.f;
    }

    std::vector<float> readFloatArray(int count, bool half)
    {
        std::vector<float> values;
        if (count <= 0)
            return values;
        values.resize(static_cast<size_t>(count), 0.0f);
        for (int i = 0; i < count; ++i)
            values[static_cast<size_t>(i)] = half ? readFloat16() : readFloat32();
        return values;
    }

private:
    const std::vector<uint8_t>& bytes;
    size_t offset = 0;
};

NuXnnLayer parseNuXnnLayer(NuXnnReader& reader, int inputSize);

NuXnnLayer parseNuXnnDenseLayer(NuXnnReader& reader, int inputSize, bool halfPrecision)
{
    NuXnnLayer layer;
    const int outputSize = static_cast<int>(reader.readUInt32());
    if (outputSize <= 0 || inputSize <= 0)
        return layer;

    std::vector<std::vector<float>> kernel;
    kernel.resize(static_cast<size_t>(outputSize));
    for (int i = 0; i < outputSize; ++i)
        kernel[static_cast<size_t>(i)] = reader.readFloatArray(inputSize, halfPrecision);
    const auto bias = reader.readFloatArray(outputSize, halfPrecision);

    layer.outputSize = outputSize;
    layer.valid = true;
    layer.eval = [kernelData = std::move(kernel), bias, outputSize](const std::vector<float>& input)
    {
        std::vector<float> out(static_cast<size_t>(outputSize), 0.0f);
        for (int row = 0; row < outputSize; ++row)
        {
            float sum = bias[static_cast<size_t>(row)];
            const auto& k = kernelData[static_cast<size_t>(row)];
            const size_t n = juce::jmin(k.size(), input.size());
            for (size_t i = 0; i < n; ++i)
                sum += k[i] * input[i];
            out[static_cast<size_t>(row)] = sum;
        }
        return out;
    };
    return layer;
}

NuXnnLayer parseNuXnnLayer(NuXnnReader& reader, int inputSize)
{
    NuXnnLayer layer;
    if (inputSize <= 0)
        return layer;

    const uint32_t tag = reader.readUInt32();
    if (tag == 0u)
        return layer;

    // Tanh
    if (tag == 0xd9fd8e7bu)
    {
        layer.outputSize = inputSize;
        layer.valid = true;
        layer.eval = [](const std::vector<float>& input)
        {
            std::vector<float> out = input;
            for (auto& v : out)
                v = std::tanh(v);
            return out;
        };
        return layer;
    }

    // Sigmoid
    if (tag == 0xd5b8e08eu)
    {
        layer.outputSize = inputSize;
        layer.valid = true;
        layer.eval = [](const std::vector<float>& input)
        {
            std::vector<float> out = input;
            for (auto& v : out)
                v = 1.0f / (1.0f + std::exp(-v));
            return out;
        };
        return layer;
    }

    // LeakyReLU + alpha
    if (tag == 0xf36cdc69u)
    {
        const float alpha = reader.readFloat32();
        layer.outputSize = inputSize;
        layer.valid = true;
        layer.eval = [alpha](const std::vector<float>& input)
        {
            std::vector<float> out = input;
            for (auto& v : out)
                v = juce::jmax(v * alpha, v);
            return out;
        };
        return layer;
    }

    // 16-bit Dense
    if (tag == 0x9cb138bcu)
        return parseNuXnnDenseLayer(reader, inputSize, true);
    // 32-bit Dense
    if (tag == 0x5a5591ebu)
        return parseNuXnnDenseLayer(reader, inputSize, false);

    // Sequential
    if (tag == 0xa7fb7d64u)
    {
        std::vector<NuXnnLayer> layers;
        int currentSize = inputSize;
        for (;;)
        {
            auto next = parseNuXnnLayer(reader, currentSize);
            if (!next.valid)
                break;
            currentSize = next.outputSize;
            layers.push_back(std::move(next));
        }
        if (layers.empty() || currentSize <= 0)
            return {};
        layer.outputSize = currentSize;
        layer.valid = true;
        layer.eval = [sequenceLayers = std::move(layers)](const std::vector<float>& input)
        {
            std::vector<float> v = input;
            for (const auto& l : sequenceLayers)
                v = l.eval(v);
            return v;
        };
        return layer;
    }

    return {};
}

NuXnnModel parseNuXnnModel(const std::vector<uint8_t>& bytes)
{
    NuXnnModel model;
    if (bytes.empty())
        return model;

    NuXnnReader reader(bytes);
    const uint32_t magic = reader.readUInt32();
    if (magic != 0x8d77306fu && magic != 0x8d773070u)
        return model;

    if (magic == 0x8d773070u)
    {
        const int nameLen = static_cast<int>(reader.readByte());
        juce::String name;
        for (int i = 0; i < nameLen; ++i)
            name << juce::String::charToString(static_cast<juce::juce_wchar>(reader.readByte()));
        model.name = name;
        (void) reader.readUInt32(); // unix timestamp, not needed for runtime mapping
    }

    const int inputSize = static_cast<int>(reader.readUInt32());
    auto root = parseNuXnnLayer(reader, inputSize);
    if (!root.valid || inputSize <= 0)
        return model;

    model.inputSize = inputSize;
    model.outputSize = root.outputSize;
    model.rootEval = std::move(root.eval);
    model.valid = (model.outputSize > 0);
    return model;
}

std::array<float, 73> postProcessBeatSpaceVector(const std::vector<float>& raw)
{
    constexpr int kBeatSpaceVectorSize = 73;
    constexpr int kBeatSpacePatchParamCount = 25;
    std::array<float, kBeatSpaceVectorSize> out{};
    out.fill(0.0f);
    if (raw.empty())
        return out;

    // First 25 parameters are continuous 0..1 patch controls.
    const int patchCount = juce::jmin<int>(
        kBeatSpacePatchParamCount,
        juce::jmin<int>(kBeatSpaceVectorSize, static_cast<int>(raw.size())));
    for (int i = 0; i < patchCount; ++i)
        out[static_cast<size_t>(i)] = juce::jlimit(0.0f, 1.0f, raw[static_cast<size_t>(i)]);

    // Pattern section: 16 * (trigger, accent, fill), with same trigger normalization
    // strategy as the BeatSpace script.
    const int firstPatternIndex = kBeatSpacePatchParamCount;
    if (static_cast<int>(raw.size()) <= firstPatternIndex)
        return out;

    float maxWeight = 0.001f;
    for (int step = 0; step < 16; ++step)
    {
        const int idx = firstPatternIndex + (step * 3);
        if (idx >= static_cast<int>(raw.size()))
            break;
        maxWeight = juce::jmax(maxWeight, raw[static_cast<size_t>(idx)]);
    }
    const float norm = (maxWeight < 0.5001f) ? (0.5001f / juce::jmax(0.0001f, maxWeight)) : 1.0f;

    for (int step = 0; step < 16; ++step)
    {
        const int idx = firstPatternIndex + (step * 3);
        if ((idx + 2) >= static_cast<int>(raw.size()) || (idx + 2) >= kBeatSpaceVectorSize)
            break;
        const bool trig = (raw[static_cast<size_t>(idx)] * norm) >= 0.5f;
        const bool accent = trig && (raw[static_cast<size_t>(idx + 1)] >= 0.5f);
        const bool fill = trig && (raw[static_cast<size_t>(idx + 2)] >= 0.5f);
        out[static_cast<size_t>(idx)] = trig ? 1.0f : 0.0f;
        out[static_cast<size_t>(idx + 1)] = accent ? 1.0f : 0.0f;
        out[static_cast<size_t>(idx + 2)] = fill ? 1.0f : 0.0f;
    }
    return out;
}

float scoreBeatSpaceCategoryPoint(int category, const std::array<float, 73>& values)
{
    const float oscFreq = juce::jlimit(0.0f, 1.0f, values[1]);
    const float oscAtk = juce::jlimit(0.0f, 1.0f, values[2]);
    const float oscDcy = juce::jlimit(0.0f, 1.0f, values[3]);
    const float modAmt = juce::jlimit(0.0f, 1.0f, values[6]);
    const float nFilFrq = juce::jlimit(0.0f, 1.0f, values[8]);
    const float nFilQ = juce::jlimit(0.0f, 1.0f, values[9]);
    const float nStereo = juce::jlimit(0.0f, 1.0f, values[10]);
    const float nEnvAtk = juce::jlimit(0.0f, 1.0f, values[12]);
    const float nEnvDcy = juce::jlimit(0.0f, 1.0f, values[13]);
    const float mix = juce::jlimit(0.0f, 1.0f, values[14]);
    const float dist = juce::jlimit(0.0f, 1.0f, values[15]);
    const float eqFreq = juce::jlimit(0.0f, 1.0f, values[16]);
    const float eqGain = juce::jlimit(0.0f, 1.0f, values[17]);

    const float lowBody = juce::jlimit(0.0f, 1.0f, ((1.0f - oscFreq) * 0.58f) + ((1.0f - nFilFrq) * 0.42f));
    const float highBody = juce::jlimit(0.0f, 1.0f, (oscFreq * 0.52f) + (nFilFrq * 0.48f));
    const float transient = juce::jlimit(0.0f, 1.0f, ((1.0f - oscAtk) * 0.62f) + ((1.0f - nEnvAtk) * 0.38f));
    const float tail = juce::jlimit(0.0f, 1.0f, (oscDcy * 0.55f) + (nEnvDcy * 0.45f));
    const float noisiness = juce::jlimit(0.0f, 1.0f, (mix * 0.62f) + (modAmt * 0.26f) + (nFilQ * 0.12f));
    const float tonalness = juce::jlimit(0.0f, 1.0f, ((1.0f - mix) * 0.78f) + ((1.0f - modAmt) * 0.22f));

    auto centered = [](float value, float center, float width)
    {
        if (width <= 0.0f)
            return 0.0f;
        const float t = 1.0f - (std::abs(value - center) / width);
        return juce::jlimit(0.0f, 1.0f, t);
    };

    switch (category)
    {
        case 0: // Kick
            return 0.39f * lowBody
                + 0.20f * transient
                + 0.16f * centered(tail, 0.34f, 0.38f)
                + 0.15f * tonalness
                + 0.10f * dist
                ;
        case 1: // Snare
            return 0.24f * centered(oscFreq, 0.44f, 0.30f)
                + 0.22f * noisiness
                + 0.16f * centered(eqFreq, 0.56f, 0.42f)
                + 0.16f * centered(tail, 0.46f, 0.34f)
                + 0.12f * transient
                + 0.10f * dist
                ;
        case 2: // Closed hat
            return 0.38f * highBody
                + 0.20f * noisiness
                + 0.20f * transient
                + 0.16f * (1.0f - tail)
                + 0.06f * centered(nFilQ, 0.58f, 0.30f);
        case 3: // Open hat
            return 0.32f * highBody
                + 0.24f * noisiness
                + 0.28f * centered(tail, 0.70f, 0.28f)
                + 0.10f * nStereo
                + 0.06f * centered(nFilQ, 0.62f, 0.30f);
        case 4: // Percussion
            return 0.23f * centered(oscFreq, 0.56f, 0.38f)
                + 0.15f * centered(nFilFrq, 0.54f, 0.36f)
                + 0.14f * centered(tail, 0.52f, 0.34f)
                + 0.13f * modAmt
                + 0.13f * nStereo
                + 0.11f * dist
                + 0.11f * centered(nFilQ, 0.62f, 0.30f);
        case 5: // Misc
        default:
            return 0.21f * modAmt
                + 0.21f * nStereo
                + 0.19f * juce::jlimit(0.0f, 1.0f, std::abs((eqGain * 2.0f) - 1.0f))
                + 0.15f * dist
                + 0.13f * centered(oscFreq, 0.50f, 0.46f)
                + 0.11f * centered(nFilQ, 0.72f, 0.32f);
    }
}

juce::String describeBeatSpaceCategoryPresetPoint(
    int category,
    const std::array<float, 73>& values)
{
    constexpr int kBeatSpacePatchParamCount = 25;
    const float oscFreq = juce::jlimit(0.0f, 1.0f, values[1]);
    const float oscAtk = juce::jlimit(0.0f, 1.0f, values[2]);
    const float oscDcy = juce::jlimit(0.0f, 1.0f, values[3]);
    const float modAmt = juce::jlimit(0.0f, 1.0f, values[6]);
    const float nFilFrq = juce::jlimit(0.0f, 1.0f, values[8]);
    const float nFilQ = juce::jlimit(0.0f, 1.0f, values[9]);
    const float nEnvAtk = juce::jlimit(0.0f, 1.0f, values[12]);
    const float nEnvDcy = juce::jlimit(0.0f, 1.0f, values[13]);
    const float mix = juce::jlimit(0.0f, 1.0f, values[14]);

    float triggerDensity = 0.0f;
    for (int step = 0; step < 16; ++step)
    {
        const int idx = kBeatSpacePatchParamCount + (step * 3);
        if (values[static_cast<size_t>(idx)] >= 0.5f)
            triggerDensity += 1.0f;
    }
    triggerDensity /= 16.0f;

    const float lowBody = juce::jlimit(0.0f, 1.0f, ((1.0f - oscFreq) * 0.58f) + ((1.0f - nFilFrq) * 0.42f));
    const float highBody = juce::jlimit(0.0f, 1.0f, (oscFreq * 0.52f) + (nFilFrq * 0.48f));
    const float transient = juce::jlimit(0.0f, 1.0f, ((1.0f - oscAtk) * 0.62f) + ((1.0f - nEnvAtk) * 0.38f));
    const float tail = juce::jlimit(0.0f, 1.0f, (oscDcy * 0.55f) + (nEnvDcy * 0.45f));
    const float noisiness = juce::jlimit(0.0f, 1.0f, (mix * 0.62f) + (modAmt * 0.26f) + (nFilQ * 0.12f));
    const float tonalness = juce::jlimit(0.0f, 1.0f, ((1.0f - mix) * 0.78f) + ((1.0f - modAmt) * 0.22f));

    juce::String toneTag = "Mid";
    if (lowBody > 0.66f)
        toneTag = "Deep";
    else if (highBody > 0.66f)
        toneTag = "Bright";

    juce::String envTag = "Punch";
    if (tail < 0.34f && transient > 0.62f)
        envTag = "Tight";
    else if (tail > 0.66f)
        envTag = "Long";

    juce::String textureTag = "Hybrid";
    if (noisiness > (tonalness + 0.12f))
        textureTag = "Noise";
    else if (tonalness > (noisiness + 0.12f))
        textureTag = "Tone";

    juce::String densityTag = "Steady";
    if (triggerDensity < 0.30f)
        densityTag = "Sparse";
    else if (triggerDensity > 0.62f)
        densityTag = "Dense";

    juce::String categoryTag;
    switch (juce::jlimit(0, StepVstHostAudioProcessor::BeatSpaceChannels - 1, category))
    {
        case 0: categoryTag = "Kick"; break;
        case 1: categoryTag = "Snare"; break;
        case 2: categoryTag = "CHat"; break;
        case 3: categoryTag = "OHat"; break;
        case 4: categoryTag = "Perc"; break;
        default: categoryTag = "Misc"; break;
    }

    return categoryTag + " " + toneTag + " " + envTag + " " + textureTag + " " + densityTag;
}

float beatSpaceTraitDeepScore(const std::array<float, 73>& values)
{
    const float oscFreq = juce::jlimit(0.0f, 1.0f, values[1]);
    const float modRate = juce::jlimit(0.0f, 1.0f, values[5]);
    const float nFilFrq = juce::jlimit(0.0f, 1.0f, values[8]);
    return juce::jlimit(
        0.0f,
        1.0f,
        ((1.0f - oscFreq) * 0.82f)
            + ((1.0f - modRate) * 0.08f)
            + ((1.0f - nFilFrq) * 0.10f));
}

float beatSpaceTraitTightScore(const std::array<float, 73>& values)
{
    const float oscAtk = juce::jlimit(0.0f, 1.0f, values[2]);
    const float oscDcy = juce::jlimit(0.0f, 1.0f, values[3]);
    const float nEnvAtk = juce::jlimit(0.0f, 1.0f, values[12]);
    const float nEnvDcy = juce::jlimit(0.0f, 1.0f, values[13]);
    return juce::jlimit(
        0.0f,
        1.0f,
        ((1.0f - oscAtk) * 0.15f)
            + ((1.0f - oscDcy) * 0.35f)
            + ((1.0f - nEnvAtk) * 0.15f)
            + ((1.0f - nEnvDcy) * 0.35f));
}

float beatSpaceTraitNoiseScore(const std::array<float, 73>& values)
{
    const float mix = juce::jlimit(0.0f, 1.0f, values[14]);
    const float nFilMod = juce::jlimit(0.0f, 1.0f, values[7]);
    const float nEnvMod = juce::jlimit(0.0f, 1.0f, values[11]);
    const float nVel = juce::jlimit(0.0f, 1.0f, values[23]);
    return juce::jlimit(
        0.0f,
        1.0f,
        (mix * 0.70f)
            + (nFilMod * 0.10f)
            + (nEnvMod * 0.12f)
            + (nVel * 0.08f));
}

float beatSpaceTraitDenseScore(const std::array<float, 73>& values)
{
    constexpr int kBeatSpacePatchParamCount = 25;
    float triggerDensity = 0.0f;
    for (int step = 0; step < 16; ++step)
    {
        const int idx = kBeatSpacePatchParamCount + (step * 3);
        if (values[static_cast<size_t>(idx)] >= 0.5f)
            triggerDensity += 1.0f;
    }
    const float triggerScore = juce::jlimit(0.0f, 1.0f, triggerDensity / 16.0f);
    const float oscDcy = juce::jlimit(0.0f, 1.0f, values[3]);
    const float nEnvDcy = juce::jlimit(0.0f, 1.0f, values[13]);
    const float overlapScore = juce::jlimit(0.0f, 1.0f, (oscDcy * 0.52f) + (nEnvDcy * 0.48f));
    return juce::jlimit(0.0f, 1.0f, (triggerScore * 0.68f) + (overlapScore * 0.32f));
}

float beatSpaceTraitScoreForIndex(int traitIndex, const std::array<float, 73>& values)
{
    switch (juce::jlimit(0, 3, traitIndex))
    {
        case 0: return beatSpaceTraitDeepScore(values);
        case 1: return beatSpaceTraitTightScore(values);
        case 2: return beatSpaceTraitNoiseScore(values);
        case 3:
        default: return beatSpaceTraitDenseScore(values);
    }
}

bool isPersistentGlobalControlParameterId(const juce::String& parameterID)
{
    for (const auto* id : kPersistentGlobalControlParameterIds)
    {
        if (parameterID == id)
            return true;
    }
    return false;
}

constexpr int kStutterButtonFirstColumn = 9;
constexpr int kStutterButtonCount = 6;

uint8_t stutterButtonBitFromColumn(int column)
{
    if (column < kStutterButtonFirstColumn || column >= (kStutterButtonFirstColumn + kStutterButtonCount))
        return 0;
    return static_cast<uint8_t>(1u << static_cast<unsigned int>(column - kStutterButtonFirstColumn));
}

int countStutterBits(uint8_t mask)
{
    int count = 0;
    for (int i = 0; i < kStutterButtonCount; ++i)
    {
        if ((mask & static_cast<uint8_t>(1u << static_cast<unsigned int>(i))) != 0)
            ++count;
    }
    return count;
}

int highestStutterBit(uint8_t mask)
{
    for (int i = kStutterButtonCount - 1; i >= 0; --i)
    {
        if ((mask & static_cast<uint8_t>(1u << static_cast<unsigned int>(i))) != 0)
            return i;
    }
    return 0;
}

int lowestStutterBit(uint8_t mask)
{
    for (int i = 0; i < kStutterButtonCount; ++i)
    {
        if ((mask & static_cast<uint8_t>(1u << static_cast<unsigned int>(i))) != 0)
            return i;
    }
    return 0;
}

double stutterDivisionBeatsFromBit(int bit)
{
    static constexpr std::array<double, kStutterButtonCount> kDivisionBeats{
        2.0,            // bit 0 (col 9)  -> 1/2
        1.0,            // bit 1 (col 10) -> 1/4
        0.5,            // bit 2 (col 11) -> 1/8
        0.25,           // bit 3 (col 12) -> 1/16
        0.125,          // bit 4 (col 13) -> 1/32
        0.0625          // bit 5 (col 14) -> 1/64
    };
    const int idx = juce::jlimit(0, kStutterButtonCount - 1, bit);
    return kDivisionBeats[static_cast<size_t>(idx)];
}

double stutterDivisionBeatsFromBitForMacro(int bit, bool preferStraight)
{
    const double base = stutterDivisionBeatsFromBit(bit);
    if (!preferStraight)
        return base;

    switch (juce::jlimit(0, kStutterButtonCount - 1, bit))
    {
        // Keep macro path mostly in the core straight-musical range.
        case 0: return 1.0;   // clamp 1/2 to 1/4 for multi-button macro motion
        case 5: return 0.125; // clamp 1/64 to 1/32
        default: return base;
    }
}

template <size_t N>
double snapDivisionToGrid(double divisionBeats, const std::array<double, N>& grid)
{
    if (!std::isfinite(divisionBeats))
        return grid[0];

    double best = grid[0];
    double bestDist = std::abs(divisionBeats - best);
    for (size_t i = 1; i < N; ++i)
    {
        const double cand = grid[i];
        const double dist = std::abs(divisionBeats - cand);
        if (dist < bestDist)
        {
            best = cand;
            bestDist = dist;
        }
    }
    return best;
}

double wrapUnitPhase(double phase)
{
    if (!std::isfinite(phase))
        return 0.0;
    phase = std::fmod(phase, 1.0);
    if (phase < 0.0)
        phase += 1.0;
    return phase;
}

float cutoffFromNormalized(float normalized)
{
    normalized = juce::jlimit(0.0f, 1.0f, normalized);
    return 20.0f * std::pow(1000.0f, normalized);
}

EnhancedAudioStrip::FilterAlgorithm filterAlgorithmFromIndex(int index)
{
    switch (juce::jlimit(0, 5, index))
    {
        case 0: return EnhancedAudioStrip::FilterAlgorithm::Tpt12;
        case 1: return EnhancedAudioStrip::FilterAlgorithm::Tpt24;
        case 2: return EnhancedAudioStrip::FilterAlgorithm::Ladder12;
        case 3: return EnhancedAudioStrip::FilterAlgorithm::Ladder24;
        case 4: return EnhancedAudioStrip::FilterAlgorithm::MoogStilson;
        case 5:
        default: return EnhancedAudioStrip::FilterAlgorithm::MoogHuov;
    }
}
}

//==============================================================================
// MonomeConnection Implementation
//==============================================================================

MonomeConnection::MonomeConnection()
{
    // Start heartbeat timer for connection monitoring
    startTimer(1000); // Check every second
}

MonomeConnection::~MonomeConnection()
{
    stopTimer();
    disconnect();
}

void MonomeConnection::connect(int appPort)
{
    // Disconnect if already connected
    oscReceiver.removeListener(this);
    oscReceiver.disconnect();

    // Bind to application port for receiving messages from device.
    // After restart, preferred port can be temporarily unavailable, so fall back.
    int boundPort = -1;
    for (int offset = 0; offset < 32; ++offset)
    {
        const int candidate = appPort + offset;
        if (oscReceiver.connect(candidate))
        {
            boundPort = candidate;
            break;
        }
    }

    if (boundPort < 0)
        return;

    applicationPort = boundPort;
    
    oscReceiver.addListener(this);
    
    // Connect to serialosc for device discovery
    (void) serialoscSender.connect("127.0.0.1", 12002);

    reconnectAttempts = 0;
    lastMessageTime = juce::Time::currentTimeMillis();
    lastConnectAttemptTime = lastMessageTime;
    lastPingTime = 0;
    lastDiscoveryTime = 0;
    lastReconnectAttemptTime = 0;
    awaitingDeviceResponse = false;
    
    // Start device discovery
    discoverDevices();
}

void MonomeConnection::refreshDeviceList()
{
    devices.clear();
    discoverDevices();
}

void MonomeConnection::disconnect()
{
    oscReceiver.removeListener(this);
    oscReceiver.disconnect();
    oscSender.disconnect();
    serialoscSender.disconnect();
    connected = false;
    reconnectAttempts = 0;
    lastMessageTime = 0;
    lastConnectAttemptTime = 0;
    lastPingTime = 0;
    lastDiscoveryTime = 0;
    lastReconnectAttemptTime = 0;
    awaitingDeviceResponse = false;
}

void MonomeConnection::discoverDevices()
{
    if (!serialoscSender.connect("127.0.0.1", 12002))
        return;
    
    // Query for device list
    const bool sentList = serialoscSender.send(
        juce::OSCMessage("/serialosc/list", juce::String("127.0.0.1"), applicationPort));
    
    // Subscribe to device notifications
    const bool sentNotify = serialoscSender.send(
        juce::OSCMessage("/serialosc/notify", juce::String("127.0.0.1"), applicationPort));

    if (sentList || sentNotify)
        lastDiscoveryTime = juce::Time::currentTimeMillis();
}

void MonomeConnection::selectDevice(int deviceIndex)
{
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(devices.size()))
        return;
    
    const bool hadActiveConnection = connected;
    currentDevice = devices[static_cast<size_t>(deviceIndex)];
    
    // Hard switch sender endpoint/state before attaching to new device.
    oscSender.disconnect();
    connected = false;
    awaitingDeviceResponse = false;
    lastConnectAttemptTime = 0;
    lastPingTime = 0;
    if (hadActiveConnection && onDeviceDisconnected)
        onDeviceDisconnected();
    
    // Connect to the device's port
    if (oscSender.connect(currentDevice.host, currentDevice.port))
    {
        configureCurrentDevice();
        sendPing();
        
        // Clear all LEDs on connection
        if (supportsGrid())
            setAllLEDs(0);
        
        connected = true;
        reconnectAttempts = 0;
        lastMessageTime = 0;
        lastConnectAttemptTime = juce::Time::currentTimeMillis();
        lastPingTime = 0;
        awaitingDeviceResponse = true;
        
        if (onDeviceConnected)
            onDeviceConnected();

        // Some serialosc/device combinations can ignore initial sys routing
        // commands during rapid endpoint switching. Reassert once shortly after.
        const auto selectedId = currentDevice.id;
        juce::Timer::callAfterDelay(120, [this, selectedId]()
        {
            if (!connected || currentDevice.id != selectedId)
                return;
            configureCurrentDevice();
            sendPing();
        });
        
    }
    else
    {
        connected = false;
    }
}

void MonomeConnection::setLED(int x, int y, int state)
{
    if (!connected) return;
    oscSender.send(juce::OSCMessage(oscPrefix + "/grid/led/set", x, y, state));
}

void MonomeConnection::setAllLEDs(int state)
{
    if (!connected) return;
    oscSender.send(juce::OSCMessage(oscPrefix + "/grid/led/all", state));
}

void MonomeConnection::setLEDRow(int xOffset, int y, const std::array<int, 8>& data)
{
    if (!connected) return;
    
    juce::OSCMessage msg(oscPrefix + "/grid/led/row");
    msg.addInt32(xOffset);
    msg.addInt32(y);
    for (int val : data)
        msg.addInt32(val);
    oscSender.send(msg);
}

void MonomeConnection::setLEDColumn(int x, int yOffset, const std::array<int, 8>& data)
{
    if (!connected) return;
    
    juce::OSCMessage msg(oscPrefix + "/grid/led/col");
    msg.addInt32(x);
    msg.addInt32(yOffset);
    for (int val : data)
        msg.addInt32(val);
    oscSender.send(msg);
}

void MonomeConnection::setLEDMap(int xOffset, int yOffset, const std::array<int, 8>& data)
{
    if (!connected) return;
    
    juce::OSCMessage msg(oscPrefix + "/grid/led/map");
    msg.addInt32(xOffset);
    msg.addInt32(yOffset);
    for (int val : data)
        msg.addInt32(val);
    oscSender.send(msg);
}

void MonomeConnection::setRotation(int degrees)
{
    if (!connected) return;
    // Only 0, 90, 180, 270 are valid
    int validRotation = ((degrees / 90) * 90) % 360;
    oscSender.send(juce::OSCMessage("/sys/rotation", validRotation));
}

void MonomeConnection::setPrefix(const juce::String& newPrefix)
{
    oscPrefix = newPrefix;
    if (connected)
        oscSender.send(juce::OSCMessage("/sys/prefix", oscPrefix));
}

void MonomeConnection::requestInfo()
{
    if (!connected) return;
    oscSender.send(juce::OSCMessage("/sys/info", juce::String(currentDevice.host), applicationPort));
}

void MonomeConnection::requestSize()
{
    if (!connected) return;
    oscSender.send(juce::OSCMessage("/sys/size"));
}

// Variable brightness LED control (0-15 levels)
void MonomeConnection::setLEDLevel(int x, int y, int level)
{
    if (!connected) return;
    int clampedLevel = juce::jlimit(0, 15, level);
    oscSender.send(juce::OSCMessage(oscPrefix + "/grid/led/level/set", x, y, clampedLevel));
}

void MonomeConnection::setAllLEDLevels(int level)
{
    if (!connected) return;
    int clampedLevel = juce::jlimit(0, 15, level);
    oscSender.send(juce::OSCMessage(oscPrefix + "/grid/led/level/all", clampedLevel));
}

void MonomeConnection::setLEDLevelRow(int xOffset, int y, const std::array<int, 8>& levels)
{
    if (!connected) return;
    
    juce::OSCMessage msg(oscPrefix + "/grid/led/level/row");
    msg.addInt32(xOffset);
    msg.addInt32(y);
    for (int level : levels)
    {
        int clampedLevel = juce::jlimit(0, 15, level);
        msg.addInt32(clampedLevel);
    }
    oscSender.send(msg);
}

void MonomeConnection::setLEDLevelColumn(int x, int yOffset, const std::array<int, 8>& levels)
{
    if (!connected) return;
    
    juce::OSCMessage msg(oscPrefix + "/grid/led/level/col");
    msg.addInt32(x);
    msg.addInt32(yOffset);
    for (int level : levels)
    {
        int clampedLevel = juce::jlimit(0, 15, level);
        msg.addInt32(clampedLevel);
    }
    oscSender.send(msg);
}

void MonomeConnection::setLEDLevelMap(int xOffset, int yOffset, const std::array<int, 64>& levels)
{
    if (!connected) return;
    
    juce::OSCMessage msg(oscPrefix + "/grid/led/level/map");
    msg.addInt32(xOffset);
    msg.addInt32(yOffset);
    for (int level : levels)
    {
        int clampedLevel = juce::jlimit(0, 15, level);
        msg.addInt32(clampedLevel);
    }
    oscSender.send(msg);
}

void MonomeConnection::setArcRingMap(int encoder, const std::array<int, 64>& levels)
{
    if (!connected || !supportsArc())
        return;

    const int maxEncoders = juce::jmax(1, getArcEncoderCount());
    const int clampedEncoder = juce::jlimit(0, maxEncoders - 1, encoder);

    juce::OSCMessage msg(oscPrefix + "/ring/map");
    msg.addInt32(clampedEncoder);
    for (int level : levels)
        msg.addInt32(juce::jlimit(0, 15, level));
    oscSender.send(msg);
}

void MonomeConnection::setArcRingLevel(int encoder, int ledIndex, int level)
{
    if (!connected || !supportsArc())
        return;

    const int maxEncoders = juce::jmax(1, getArcEncoderCount());
    const int clampedEncoder = juce::jlimit(0, maxEncoders - 1, encoder);
    const int clampedLed = juce::jlimit(0, 63, ledIndex);
    const int clampedLevel = juce::jlimit(0, 15, level);
    oscSender.send(juce::OSCMessage(oscPrefix + "/ring/set", clampedEncoder, clampedLed, clampedLevel));
}

void MonomeConnection::setArcRingRange(int encoder, int start, int end, int level)
{
    if (!connected || !supportsArc())
        return;

    const int maxEncoders = juce::jmax(1, getArcEncoderCount());
    const int clampedEncoder = juce::jlimit(0, maxEncoders - 1, encoder);
    const int clampedStart = juce::jlimit(0, 63, start);
    const int clampedEnd = juce::jlimit(0, 63, end);
    const int clampedLevel = juce::jlimit(0, 15, level);
    oscSender.send(juce::OSCMessage(oscPrefix + "/ring/range", clampedEncoder, clampedStart, clampedEnd, clampedLevel));
}

bool MonomeConnection::supportsGrid() const
{
    return !supportsArc();
}

bool MonomeConnection::supportsArc() const
{
    return currentDevice.type.containsIgnoreCase("arc");
}

int MonomeConnection::getArcEncoderCount() const
{
    if (!supportsArc())
        return 0;
    if (currentDevice.type.contains("2"))
        return 2;
    if (currentDevice.type.contains("4"))
        return 4;
    return 4;
}

// Tilt support
void MonomeConnection::enableTilt(int sensor, bool enable)
{
    if (!connected) return;
    oscSender.send(juce::OSCMessage(oscPrefix + "/tilt/set", sensor, enable ? 1 : 0));
}

// Connection status
juce::String MonomeConnection::getConnectionStatus() const
{
    if (!connected)
        return "Not connected";
    
    return "Connected to " + currentDevice.id + " (" + currentDevice.type + ") - " +
           juce::String(currentDevice.sizeX) + "x" + juce::String(currentDevice.sizeY);
}

void MonomeConnection::oscMessageReceived(const juce::OSCMessage& message)
{
    auto address = message.getAddressPattern().toString();

    // Only treat actual device/system traffic as successful handshake activity.
    // serialosc discovery traffic can be present even if the selected device is
    // not correctly routed to this app yet.
    const bool isDeviceTraffic = address.startsWith("/sys")
        || address.startsWith(oscPrefix + "/grid")
        || address.startsWith(oscPrefix + "/tilt")
        || address.startsWith(oscPrefix + "/enc");
    if (isDeviceTraffic)
    {
        lastMessageTime = juce::Time::currentTimeMillis();
        awaitingDeviceResponse = false;
    }

    if (address.startsWith("/serialosc"))
        handleSerialOSCMessage(message);
    else if (address.startsWith(oscPrefix + "/grid"))
        handleGridMessage(message);
    else if (address.startsWith(oscPrefix + "/tilt"))
        handleTiltMessage(message);
    else if (address.startsWith(oscPrefix + "/enc"))
        handleArcMessage(message);
    else if (address.startsWith("/sys"))
        handleSystemMessage(message);
}

void MonomeConnection::timerCallback()
{
    const auto currentTime = juce::Time::currentTimeMillis();

    if (!connected)
    {
        if (!autoReconnect)
            return;

        if (currentTime - lastDiscoveryTime >= discoveryIntervalMs)
            discoverDevices();

        // Attempt direct reconnection while we still have a candidate endpoint.
        if (!currentDevice.id.isEmpty()
            && currentDevice.port > 0
            && reconnectAttempts < maxReconnectAttempts
            && (currentTime - lastReconnectAttemptTime) >= reconnectIntervalMs)
        {
            lastReconnectAttemptTime = currentTime;
            attemptReconnection();
        }

        return;
    }

    // A successful UDP "connect" does not guarantee the device is reachable.
    // Require a real response shortly after claiming an endpoint.
    if (awaitingDeviceResponse
        && lastConnectAttemptTime > 0
        && (currentTime - lastConnectAttemptTime) > handshakeTimeoutMs)
    {
        markDisconnected();
        discoverDevices();
        return;
    }

    // Treat long silence as dead connection, then fall back to discovery/reconnect.
    if (lastMessageTime > 0 && (currentTime - lastMessageTime) > connectionTimeoutMs)
    {
        markDisconnected();
        discoverDevices();
        return;
    }

    // Send periodic ping to keep connection alive and refresh sys state.
    if (lastPingTime == 0 || (currentTime - lastPingTime) >= pingIntervalMs)
    {
        sendPing();
        lastPingTime = currentTime;
    }
}

void MonomeConnection::attemptReconnection()
{
    reconnectAttempts++;
    
    // Try to reconnect to current device
    if (oscSender.connect(currentDevice.host, currentDevice.port))
    {
        configureCurrentDevice();
        sendPing();
        
        connected = true;
        reconnectAttempts = 0;
        lastMessageTime = 0;
        lastConnectAttemptTime = juce::Time::currentTimeMillis();
        lastPingTime = 0;
        awaitingDeviceResponse = true;
        
        if (onDeviceConnected)
            onDeviceConnected();
        
    }
    else if (autoReconnect)
    {
        discoverDevices();
    }
}

void MonomeConnection::sendPing()
{
    if (!connected) return;
    
    // Request device info as a "ping"
    oscSender.send(juce::OSCMessage("/sys/info", juce::String(currentDevice.host), applicationPort));
}

void MonomeConnection::handleSerialOSCMessage(const juce::OSCMessage& message)
{
    auto address = message.getAddressPattern().toString();
    auto renewNotify = [this]()
    {
        if (!serialoscSender.connect("127.0.0.1", 12002))
            return;
        serialoscSender.send(juce::OSCMessage("/serialosc/notify",
                                              juce::String("127.0.0.1"),
                                              applicationPort));
    };
    
    if (address == "/serialosc/device" && message.size() >= 3)
    {
        DeviceInfo info;
        info.id = message[0].getString();
        info.type = message[1].getString();
        info.port = message[2].getInt32();
        info.host = "127.0.0.1"; // Default to localhost
        
        // Check if device already exists in list
        bool deviceExists = false;
        bool endpointChanged = false;
        for (auto& existing : devices)
        {
            if (existing.id == info.id)
            {
                deviceExists = true;
                if (existing.port != info.port || existing.type != info.type || existing.host != info.host)
                {
                    existing.type = info.type;
                    existing.port = info.port;
                    existing.host = info.host;
                    endpointChanged = true;
                }
                break;
            }
        }
        
        if (!deviceExists)
        {
            devices.push_back(info);
        }

        // If this is our selected device and serialosc changed its endpoint,
        // switch to the new endpoint immediately.
        if (currentDevice.id == info.id
            && (currentDevice.port != info.port || currentDevice.host != info.host))
        {
            currentDevice.port = info.port;
            currentDevice.host = info.host;

            if (connected)
            {
                oscSender.disconnect();
                markDisconnected();
            }
        }

        if (!deviceExists || endpointChanged)
        {
            if (onDeviceListUpdated)
                onDeviceListUpdated(devices);
        }

        if (!connected)
        {
            int bestIndex = -1;
            if (!currentDevice.id.isEmpty())
            {
                for (int i = 0; i < static_cast<int>(devices.size()); ++i)
                {
                    if (devices[static_cast<size_t>(i)].id == currentDevice.id)
                    {
                        bestIndex = i;
                        break;
                    }
                }
            }

            if (bestIndex < 0 && !devices.empty())
                bestIndex = 0;

            if (bestIndex >= 0)
                selectDevice(bestIndex);
        }
    }
    else if (address == "/serialosc/add" && message.size() >= 1)
    {
        // serialosc notify is one-shot; re-register each time we get add/remove.
        renewNotify();

        // Device was plugged in
        juce::Timer::callAfterDelay(250, [this]()
        {
            discoverDevices(); // Refresh device list
        });
    }
    else if (address == "/serialosc/remove" && message.size() >= 1)
    {
        // serialosc notify is one-shot; re-register each time we get add/remove.
        renewNotify();

        // Device was unplugged
        auto removedId = message[0].getString();
        
        // Remove from device list
        devices.erase(std::remove_if(devices.begin(), devices.end(),
            [&removedId](const DeviceInfo& info) { return info.id == removedId; }),
            devices.end());
        
        // Check if it was our connected device
        if (removedId == currentDevice.id)
        {
            markDisconnected();
            
            // Try to auto-connect to another device if available
            if (!devices.empty() && autoReconnect)
                selectDevice(0);
        }
        
        if (onDeviceListUpdated)
            onDeviceListUpdated(devices);
    }
}

void MonomeConnection::markDisconnected()
{
    if (!connected)
        return;

    connected = false;
    oscSender.disconnect();
    awaitingDeviceResponse = false;
    lastConnectAttemptTime = 0;
    lastPingTime = 0;

    if (onDeviceDisconnected)
        onDeviceDisconnected();
}

void MonomeConnection::configureCurrentDevice()
{
    // Configure device to send messages to our application port.
    oscSender.send(juce::OSCMessage("/sys/port", applicationPort));
    oscSender.send(juce::OSCMessage("/sys/host", juce::String("127.0.0.1")));
    oscSender.send(juce::OSCMessage("/sys/prefix", oscPrefix));
    
    // Request device information and refresh prefix/size state.
    oscSender.send(juce::OSCMessage("/sys/info", juce::String("127.0.0.1"), applicationPort));
    oscSender.send(juce::OSCMessage("/sys/size"));
}

void MonomeConnection::handleGridMessage(const juce::OSCMessage& message)
{
    auto address = message.getAddressPattern().toString();
    
    if (address == oscPrefix + "/grid/key" && message.size() >= 3)
    {
        int x = message[0].getInt32();
        int y = message[1].getInt32();
        int state = message[2].getInt32();
        
        if (onKeyPress)
            onKeyPress(x, y, state);
    }
}

void MonomeConnection::handleSystemMessage(const juce::OSCMessage& message)
{
    auto address = message.getAddressPattern().toString();
    
    if (address == "/sys/size" && message.size() >= 2)
    {
        currentDevice.sizeX = message[0].getInt32();
        currentDevice.sizeY = message[1].getInt32();
    }
    else if (address == "/sys/id" && message.size() >= 1)
    {
        currentDevice.id = message[0].getString();
    }
    else if (address == "/sys/rotation" && message.size() >= 1)
    {
        (void)message[0].getInt32();
    }
    else if (address == "/sys/host" && message.size() >= 1)
    {
        currentDevice.host = message[0].getString();
    }
    else if (address == "/sys/port" && message.size() >= 1)
    {
        // Response to our port configuration
    }
    else if (address == "/sys/prefix" && message.size() >= 1)
    {
        // Response to our prefix configuration
    }
}

void MonomeConnection::handleTiltMessage(const juce::OSCMessage& message)
{
    auto address = message.getAddressPattern().toString();
    
    if (address == oscPrefix + "/tilt" && message.size() >= 4)
    {
        int sensor = message[0].getInt32();
        int x = message[1].getInt32();
        int y = message[2].getInt32();
        int z = message[3].getInt32();
        
        if (onTilt)
            onTilt(sensor, x, y, z);
    }
}

void MonomeConnection::handleArcMessage(const juce::OSCMessage& message)
{
    auto address = message.getAddressPattern().toString();

    if (address == oscPrefix + "/enc/delta" && message.size() >= 2)
    {
        const int encoder = message[0].getInt32();
        const int delta = message[1].getInt32();
        if (onArcDelta)
            onArcDelta(encoder, delta);
    }
    else if (address == oscPrefix + "/enc/key" && message.size() >= 2)
    {
        const int encoder = message[0].getInt32();
        const int state = message[1].getInt32();
        if (onArcKey)
            onArcKey(encoder, state);
    }
}

//==============================================================================
// StepVstHostAudioProcessor Implementation
//==============================================================================

class StepVstHostAudioProcessor::PresetSaveJob final : public juce::ThreadPoolJob
{
public:
    PresetSaveJob(StepVstHostAudioProcessor& ownerIn, PresetSaveRequest requestIn)
        : juce::ThreadPoolJob("stepVstHostPresetSave_" + juce::String(requestIn.presetIndex + 1)),
          owner(ownerIn),
          request(std::move(requestIn))
    {
    }

    JobStatus runJob() override
    {
        if (shouldExit())
        {
            owner.pushPresetSaveResult({ request.presetIndex, false });
            return jobHasFinished;
        }

        const bool success = owner.runPresetSaveRequest(request);
        owner.pushPresetSaveResult({ request.presetIndex, success });
        return jobHasFinished;
    }

private:
    StepVstHostAudioProcessor& owner;
    PresetSaveRequest request;
};

StepVstHostAudioProcessor::StepVstHostAudioProcessor()
     : AudioProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::stereo(), true)
                      .withOutput("Strip 1", juce::AudioChannelSet::stereo(), true)
                      .withOutput("Strip 2", juce::AudioChannelSet::stereo(), false)
                      .withOutput("Strip 3", juce::AudioChannelSet::stereo(), false)
                      .withOutput("Strip 4", juce::AudioChannelSet::stereo(), false)
                      .withOutput("Strip 5", juce::AudioChannelSet::stereo(), false)
                      .withOutput("Strip 6", juce::AudioChannelSet::stereo(), false)),
       parameters(*this, nullptr, juce::Identifier("StepVstHost"), createParameterLayout())
{
    // Initialize audio engine
    audioEngine = std::make_unique<ModernAudioEngine>();
    cacheParameterPointers();
    loadPersistentDefaultPaths();
    loadPersistentGlobalControls();
    persistentGlobalControlsReady.store(1, std::memory_order_release);
    pendingPersistentGlobalControlsRestore.store(1, std::memory_order_release);
    pendingPersistentGlobalControlsRestoreMs = juce::Time::currentTimeMillis() + 250;
    pendingPersistentGlobalControlsRestoreRemaining = 5;
    for (const auto* id : kPersistentGlobalControlParameterIds)
        parameters.addParameterListener(id, this);
    setSwingDivisionSelection(swingDivisionSelection.load(std::memory_order_acquire));
    resetStepEditVelocityGestures();
    for (int i = 0; i < MaxStrips; ++i)
    {
        laneMidiChannel[static_cast<size_t>(i)].store(juce::jlimit(1, 16, i + 1), std::memory_order_relaxed);
        laneMidiNote[static_cast<size_t>(i)].store(juce::jlimit(0, 127, 36 + i), std::memory_order_relaxed);
        hostedLastCcVolume[static_cast<size_t>(i)] = -1;
        hostedLastCcPan[static_cast<size_t>(i)] = -1;
        hostedLastCcPitch[static_cast<size_t>(i)] = -1;
        hostedDirectParamVolume[static_cast<size_t>(i)] = -1;
        hostedDirectParamPan[static_cast<size_t>(i)] = -1;
        hostedDirectParamPitch[static_cast<size_t>(i)] = -1;
        hostedDirectParamAttack[static_cast<size_t>(i)] = -1;
        hostedDirectParamDecay[static_cast<size_t>(i)] = -1;
        hostedDirectParamRelease[static_cast<size_t>(i)] = -1;
        hostedDirectParamAttackAux[static_cast<size_t>(i)] = -1;
        hostedLastDirectParamVolume[static_cast<size_t>(i)] = -1.0f;
        hostedLastDirectParamPan[static_cast<size_t>(i)] = -1.0f;
        hostedLastDirectParamPitch[static_cast<size_t>(i)] = -1.0f;
        hostedLastDirectParamAttack[static_cast<size_t>(i)] = -1.0f;
        hostedLastDirectParamDecay[static_cast<size_t>(i)] = -1.0f;
        hostedLastDirectParamRelease[static_cast<size_t>(i)] = -1.0f;
        hostedLastDirectParamAttackAux[static_cast<size_t>(i)] = -1.0f;
        hostedStripOutputMutedLast[static_cast<size_t>(i)] = false;
        hostedProgramNumber[static_cast<size_t>(i)] = 0;
        hostedPendingProgramDelta[static_cast<size_t>(i)].store(0, std::memory_order_relaxed);
        hostedTraversalRatioAtLastTick[static_cast<size_t>(i)] = -1.0;
        hostedTraversalPhaseOffsetTicks[static_cast<size_t>(i)] = 0.0;
        beatSpaceParamMap[static_cast<size_t>(i)].fill(-1);
        beatSpaceCurrentVectors[static_cast<size_t>(i)].fill(0.0f);
        beatSpaceMorphStartVectors[static_cast<size_t>(i)].fill(0.0f);
        beatSpaceMorphTargetVectors[static_cast<size_t>(i)].fill(0.0f);
        beatSpaceChannelPoints[static_cast<size_t>(i)] = { BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 };
        beatSpaceMorphStartPoints[static_cast<size_t>(i)] = beatSpaceChannelPoints[static_cast<size_t>(i)];
        beatSpaceMorphTargetPoints[static_cast<size_t>(i)] = beatSpaceChannelPoints[static_cast<size_t>(i)];
        beatSpaceMorphCurrentPoints[static_cast<size_t>(i)] = beatSpaceChannelPoints[static_cast<size_t>(i)];
        beatSpaceMorphProgress[static_cast<size_t>(i)] = 1.0f;
        beatSpaceLastAppliedTableIndex[static_cast<size_t>(i)] = -1;
        beatSpaceLastRecallMaxError[static_cast<size_t>(i)] = 0.0f;
        beatSpaceChannelMappingReady[static_cast<size_t>(i)] = false;
        beatSpaceMorphActive[static_cast<size_t>(i)] = false;
    }
    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        beatSpaceLinkedOffsets[static_cast<size_t>(i)] = { 0.0f, 0.0f };
        beatSpaceChannelCategoryAssignment[static_cast<size_t>(i)] = i;
        beatSpaceZoneLockStrength[static_cast<size_t>(i)] = 0.0f;
        beatSpacePaths[static_cast<size_t>(i)] = BeatSpacePathState{};
        for (int b = 0; b < BeatSpaceBookmarkSlots; ++b)
            beatSpaceBookmarks[static_cast<size_t>(i)][static_cast<size_t>(b)] = BeatSpaceBookmark{};
        beatSpaceCategoryColorCluster[static_cast<size_t>(i)] = -1;
        beatSpaceCategoryAnchorManual[static_cast<size_t>(i)] = false;
        beatSpaceCategoryManualTagCounts[static_cast<size_t>(i)] = 0;
        beatSpaceCategoryPresetPointsReady[static_cast<size_t>(i)] = false;
        beatSpaceCategoryAnchors[static_cast<size_t>(i)] = {
            BeatSpaceTableSize / 2,
            BeatSpaceTableSize / 2
        };
        beatSpaceCategoryManualAnchors[static_cast<size_t>(i)] = beatSpaceCategoryAnchors[static_cast<size_t>(i)];
        beatSpaceCategoryManualTagPoints[static_cast<size_t>(i)].fill(beatSpaceCategoryAnchors[static_cast<size_t>(i)]);
        beatSpaceCategoryPresetPoints[static_cast<size_t>(i)].fill(beatSpaceCategoryAnchors[static_cast<size_t>(i)]);
        beatSpaceCategoryPresetOrder[static_cast<size_t>(i)].fill(0);
        beatSpaceCategoryPresetHidden[static_cast<size_t>(i)].fill(false);
        beatSpaceCategoryPresetLabels[static_cast<size_t>(i)].fill({});
        for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
            beatSpaceCategoryPresetOrder[static_cast<size_t>(i)][static_cast<size_t>(slot)] = slot;
        // Kick, snare, closed hat, open hat, percussion, misc
        static constexpr std::array<int, BeatSpaceChannels> kRegionRx { 10, 10, 9, 9, 12, 12 };
        static constexpr std::array<int, BeatSpaceChannels> kRegionRy { 10, 10, 9, 9, 11, 11 };
        beatSpaceCategoryRegionRadiusX[static_cast<size_t>(i)] = kRegionRx[static_cast<size_t>(i)];
        beatSpaceCategoryRegionRadiusY[static_cast<size_t>(i)] = kRegionRy[static_cast<size_t>(i)];
    }
    beatSpaceLinkedOffsetsReady = false;
    beatSpaceMacroKnobValues.fill(0.0f);
    for (int i = 4; i < BeatSpaceMacroKnobCount; ++i)
        beatSpaceMacroKnobValues[static_cast<size_t>(i)] = 0.5f;
    for (int trait = 0; trait < 4; ++trait)
        beatSpaceMacroTraitAnchors[static_cast<size_t>(trait)].fill({ BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 });
    beatSpaceMacroTraitAnchorsReady = false;
    beatSpaceMacroBasePoints.fill({ BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 });
    beatSpaceMacroBasePointsValid = false;
    beatSpaceApplyingMacroMove = false;
    beatSpaceCategoryAnchorsReady = false;
    beatSpaceColorClustersReady = false;
    hostedDefaultAutoLoadAttempted = false;
    beatSpaceSelectedChannel = 0;
    beatSpaceLinkAllChannels = true;
    beatSpaceZoomLevel = 0;
    beatSpaceViewX = 0;
    beatSpaceViewY = 0;
    beatSpaceMorphStartTimeMs = 0.0;
    beatSpaceMorphDurationMs = 160.0;
    beatSpaceStatusMessage = "BeatSpace table not loaded";
    initializeBeatSpaceTable();
    // Load control pages after BeatSpace defaults/table init so persisted BeatSpace
    // assignments/tags are not overwritten by constructor initialization.
    loadPersistentControlPages();

    for (auto& held : arcKeyHeld)
        held = 0;
    for (auto& ring : arcRingCache)
        ring.fill(-1);
    arcControlMode = ArcControlMode::SelectedStrip;
    lastGridLedUpdateTimeMs = 0;
    
    // Setup monome callbacks
    monomeConnection.onKeyPress = [this](int x, int y, int state)
    {
        handleMonomeKeyPress(x, y, state);
    };
    monomeConnection.onArcDelta = [this](int encoder, int delta)
    {
        handleMonomeArcDelta(encoder, delta);
    };
    monomeConnection.onArcKey = [this](int encoder, int state)
    {
        handleMonomeArcKey(encoder, state);
    };
    
    monomeConnection.onDeviceConnected = [this]()
    {
        if (isTimerRunning())
            startTimer(monomeConnection.supportsArc() ? kArcRefreshMs : kGridRefreshMs);

        if (monomeConnection.supportsGrid())
        {
            // Force full LED resend after any reconnect to avoid stale cache mismatch.
            for (int y = 0; y < MaxGridHeight; ++y)
                for (int x = 0; x < MaxGridWidth; ++x)
                    ledCache[x][y] = -1;
        }

        for (auto& held : arcKeyHeld)
            held = 0;
        for (auto& ring : arcRingCache)
            ring.fill(-1);
        arcControlMode = ArcControlMode::SelectedStrip;
        arcSelectedModStep = 0;
        lastGridLedUpdateTimeMs = 0;

        // Defer LED update slightly to ensure everything is ready
        juce::MessageManager::callAsync([this]()
        {
            if (monomeConnection.supportsGrid())
                updateMonomeLEDs();
            if (monomeConnection.supportsArc())
                updateMonomeArcRings();
        });
    };

    monomeConnection.onDeviceDisconnected = [this]()
    {
        if (isTimerRunning())
            startTimer(kGridRefreshMs);
    };
    
    // Don't connect yet - wait for prepareToPlay
}

void StepVstHostAudioProcessor::cacheParameterPointers()
{
    masterVolumeParam = parameters.getRawParameterValue("masterVolume");
    limiterThresholdParam = parameters.getRawParameterValue("limiterThreshold");
    limiterEnabledParam = parameters.getRawParameterValue("limiterEnabled");
    quantizeParam = parameters.getRawParameterValue("quantize");
    pitchSmoothingParam = parameters.getRawParameterValue("pitchSmoothing");
    outputRoutingParam = parameters.getRawParameterValue("outputRouting");
    soundTouchEnabledParam = parameters.getRawParameterValue("soundTouchEnabled");
    kitScaleEnabledParam = parameters.getRawParameterValue("kitScaleEnabled");
    kitScaleModeParam = parameters.getRawParameterValue("kitScaleMode");
    kitScaleRootParam = parameters.getRawParameterValue("kitScaleRoot");

    for (int i = 0; i < MaxStrips; ++i)
    {
        stripVolumeParams[static_cast<size_t>(i)] = parameters.getRawParameterValue("stripVolume" + juce::String(i));
        stripPanParams[static_cast<size_t>(i)] = parameters.getRawParameterValue("stripPan" + juce::String(i));
        stripSpeedParams[static_cast<size_t>(i)] = parameters.getRawParameterValue("stripSpeed" + juce::String(i));
        stripPitchParams[static_cast<size_t>(i)] = parameters.getRawParameterValue("stripPitch" + juce::String(i));
        stripStepAttackParams[static_cast<size_t>(i)] = parameters.getRawParameterValue("stripStepAttack" + juce::String(i));
        stripStepDecayParams[static_cast<size_t>(i)] = parameters.getRawParameterValue("stripStepDecay" + juce::String(i));
        stripStepReleaseParams[static_cast<size_t>(i)] = parameters.getRawParameterValue("stripStepRelease" + juce::String(i));
        stripSliceLengthParams[static_cast<size_t>(i)] = parameters.getRawParameterValue("stripSliceLength" + juce::String(i));
    }
}

void StepVstHostAudioProcessor::syncRuntimeStateFromParameters()
{
    if (!audioEngine)
        return;

    if (masterVolumeParam)
        audioEngine->setMasterVolume(*masterVolumeParam);

    if (limiterThresholdParam)
        audioEngine->setLimiterThresholdDb(limiterThresholdParam->load(std::memory_order_acquire));

    if (limiterEnabledParam)
        audioEngine->setLimiterEnabled(limiterEnabledParam->load(std::memory_order_acquire) > 0.5f);

    audioEngine->setQuantization(getQuantizeDivision());

    if (pitchSmoothingParam)
        audioEngine->setPitchSmoothingTime(*pitchSmoothingParam);

    if (soundTouchEnabledParam)
    {
        const int enabledInt = (soundTouchEnabledParam->load(std::memory_order_acquire) > 0.5f) ? 1 : 0;
        if (enabledInt != lastAppliedSoundTouchEnabled)
        {
            audioEngine->setGlobalSoundTouchEnabled(enabledInt != 0);
            lastAppliedSoundTouchEnabled = enabledInt;
        }
    }

    audioEngine->setGlobalPitchScaleQuantizeEnabled(isGlobalKitScaleQuantizeEnabled());
    audioEngine->setGlobalPitchScale(getGlobalKitPitchScale());
    audioEngine->setGlobalPitchRootSemitone(getGlobalKitPitchRootSemitone());

    for (int i = 0; i < MaxStrips; ++i)
    {
        auto* strip = audioEngine->getStrip(i);
        if (strip == nullptr)
            continue;

        auto* volumeParam = stripVolumeParams[static_cast<size_t>(i)];
        if (volumeParam)
            strip->setVolume(volumeParam->load(std::memory_order_acquire));

        auto* panParam = stripPanParams[static_cast<size_t>(i)];
        if (panParam)
            strip->setPan(panParam->load(std::memory_order_acquire));

        auto* speedParam = stripSpeedParams[static_cast<size_t>(i)];
        if (speedParam)
        {
            const float speedRatio = PlayheadSpeedQuantizer::quantizeRatio(
                juce::jlimit(0.0f, 8.0f, speedParam->load(std::memory_order_acquire)));
            strip->setPlayheadSpeedRatio(speedRatio);
        }

        auto* pitchParam = stripPitchParams[static_cast<size_t>(i)];
        if (pitchParam)
            applyPitchControlToStrip(*strip, pitchParam->load(std::memory_order_acquire));

        auto* stepAttackParam = stripStepAttackParams[static_cast<size_t>(i)];
        if (stepAttackParam)
            strip->setStepEnvelopeAttackMs(stepAttackParam->load(std::memory_order_acquire));

        auto* stepDecayParam = stripStepDecayParams[static_cast<size_t>(i)];
        if (stepDecayParam)
            strip->setStepEnvelopeDecayMs(stepDecayParam->load(std::memory_order_acquire));

        auto* stepReleaseParam = stripStepReleaseParams[static_cast<size_t>(i)];
        if (stepReleaseParam)
            strip->setStepEnvelopeReleaseMs(stepReleaseParam->load(std::memory_order_acquire));

        auto* sliceLengthParam = stripSliceLengthParams[static_cast<size_t>(i)];
        if (sliceLengthParam)
            strip->setLoopSliceLength(sliceLengthParam->load(std::memory_order_acquire));
    }
}

void StepVstHostAudioProcessor::requestImmediateUiAndMonomeRefresh()
{
    presetRefreshToken.fetch_add(1, std::memory_order_acq_rel);

    auto refreshNow = [](StepVstHostAudioProcessor& processor)
    {
        if (processor.monomeConnection.isConnected())
        {
            if (processor.monomeConnection.supportsGrid())
                processor.updateMonomeLEDs();
            if (processor.monomeConnection.supportsArc())
                processor.updateMonomeArcRings();
        }
    };

    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        mm != nullptr && mm->isThisTheMessageThread())
    {
        refreshNow(*this);
        return;
    }

    juce::WeakReference<StepVstHostAudioProcessor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]()
    {
        if (safeThis == nullptr)
            return;
        if (safeThis->monomeConnection.isConnected())
        {
            if (safeThis->monomeConnection.supportsGrid())
                safeThis->updateMonomeLEDs();
            if (safeThis->monomeConnection.supportsArc())
                safeThis->updateMonomeArcRings();
        }
    });
}

void StepVstHostAudioProcessor::parameterChanged(const juce::String& parameterID, float /*newValue*/)
{
    const bool isKitScaleParam = (parameterID == "kitScaleEnabled")
        || (parameterID == "kitScaleMode")
        || (parameterID == "kitScaleRoot");
    if (isKitScaleParam)
        pendingKitScaleSettingsApply.store(1, std::memory_order_release);

    if (!isPersistentGlobalControlParameterId(parameterID))
        return;
    persistentGlobalUserTouched.store(1, std::memory_order_release);
    queuePersistentGlobalControlsSave();
}

void StepVstHostAudioProcessor::markPersistentGlobalUserChange()
{
    cancelPendingBeatSpaceStartupApply();
    persistentGlobalUserTouched.store(1, std::memory_order_release);
    persistentGlobalControlsReady.store(1, std::memory_order_release);
    queuePersistentGlobalControlsSave();
}

void StepVstHostAudioProcessor::cancelPendingBeatSpaceStartupApply()
{
    pendingBeatSpaceStartupApply.store(0, std::memory_order_release);
    pendingBeatSpaceStartupFinalize.store(0, std::memory_order_release);
    pendingBeatSpaceStartupFinalizeRemaining = 0;
    pendingBeatSpaceStartupFinalizeMs = 0;
}

void StepVstHostAudioProcessor::queuePersistentGlobalControlsSave()
{
    if (suppressPersistentGlobalControlsSave.load(std::memory_order_acquire) != 0)
        return;
    if (pendingPersistentGlobalControlsRestore.load(std::memory_order_acquire) != 0)
    {
        if (persistentGlobalUserTouched.load(std::memory_order_acquire) == 0)
            return;
        pendingPersistentGlobalControlsRestore.store(0, std::memory_order_release);
        pendingPersistentGlobalControlsRestoreRemaining = 0;
    }
    if (persistentGlobalControlsReady.load(std::memory_order_acquire) == 0)
        return;

    persistentGlobalControlsDirty.store(1, std::memory_order_release);
    flushPersistentGlobalControlsSaveNowIfMessageThread();
}

void StepVstHostAudioProcessor::flushPersistentGlobalControlsSaveNowIfMessageThread()
{
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    if (messageManager == nullptr || !messageManager->isThisTheMessageThread())
        return;
    if (suppressPersistentGlobalControlsSave.load(std::memory_order_acquire) != 0)
        return;
    if (persistentGlobalControlsReady.load(std::memory_order_acquire) == 0)
        return;

    savePersistentControlPages();
    persistentGlobalControlsDirty.store(0, std::memory_order_release);
    lastPersistentGlobalControlsSaveMs = juce::Time::currentTimeMillis();
}

StepVstHostAudioProcessor::~StepVstHostAudioProcessor()
{
    for (const auto* id : kPersistentGlobalControlParameterIds)
        parameters.removeParameterListener(id, this);
    if (persistentGlobalControlsDirty.load(std::memory_order_acquire) != 0)
        savePersistentControlPages();
    presetSaveThreadPool.removeAllJobs(true, 4000);
    stopTimer();
    monomeConnection.disconnect();
}

juce::String StepVstHostAudioProcessor::getControlModeName(ControlMode mode)
{
    switch (mode)
    {
        case ControlMode::Speed: return "Speed";
        case ControlMode::Pitch: return "Pitch";
        case ControlMode::Pan: return "Pan";
        case ControlMode::Volume: return "Volume";
        case ControlMode::Length: return "Length";
        case ControlMode::Filter: return "Filter";
        case ControlMode::Swing: return "Swing";
        case ControlMode::Gate: return "Gate";
        case ControlMode::FileBrowser: return "Browser";
        case ControlMode::GroupAssign: return "Mode";
        case ControlMode::Modulation: return "Modulation";
        case ControlMode::BeatSpace: return "BeatSpace";
        case ControlMode::Preset: return "Preset";
        case ControlMode::StepEdit: return "Step Edit";
        case ControlMode::Normal:
        default: return "Normal";
    }
}

int StepVstHostAudioProcessor::getMonomeGridWidth() const
{
    if (!monomeConnection.supportsGrid())
        return MaxGridWidth;

    const auto device = monomeConnection.getCurrentDevice();
    const int reportedWidth = (device.sizeX > 0) ? device.sizeX : MaxGridWidth;
    return juce::jlimit(1, MaxGridWidth, reportedWidth);
}

int StepVstHostAudioProcessor::getMonomeGridHeight() const
{
    if (!monomeConnection.supportsGrid())
        return 8;

    const auto device = monomeConnection.getCurrentDevice();
    const int reportedHeight = (device.sizeY > 0) ? device.sizeY : 8;
    return juce::jlimit(2, MaxGridHeight, reportedHeight);
}

int StepVstHostAudioProcessor::getMonomeControlRow() const
{
    return juce::jmax(1, getMonomeGridHeight() - 1);
}

int StepVstHostAudioProcessor::getMonomeActiveStripCount() const
{
    const int stripRows = juce::jmax(0, getMonomeControlRow() - 1);
    return juce::jlimit(0, MaxStrips, stripRows);
}

StepVstHostAudioProcessor::PitchControlMode StepVstHostAudioProcessor::getPitchControlMode() const
{
    return PitchControlMode::PitchShift;
}

bool StepVstHostAudioProcessor::isGlobalKitScaleQuantizeEnabled() const
{
    return kitScaleEnabledParam != nullptr
        && kitScaleEnabledParam->load(std::memory_order_acquire) > 0.5f;
}

ModernAudioEngine::PitchScale StepVstHostAudioProcessor::getGlobalKitPitchScale() const
{
    const int scaleIndex = juce::jlimit(
        0,
        static_cast<int>(ModernAudioEngine::PitchScale::PentatonicMinor),
        (kitScaleModeParam != nullptr)
            ? static_cast<int>(std::round(kitScaleModeParam->load(std::memory_order_acquire)))
            : 0);
    return static_cast<ModernAudioEngine::PitchScale>(scaleIndex);
}

int StepVstHostAudioProcessor::getGlobalKitPitchRootSemitone() const
{
    return juce::jlimit(
        0,
        11,
        (kitScaleRootParam != nullptr)
            ? static_cast<int>(std::round(kitScaleRootParam->load(std::memory_order_acquire)))
            : 0);
}

float StepVstHostAudioProcessor::quantizePitchSemitonesToGlobalKit(float semitones) const
{
    const float clamped = juce::jlimit(-24.0f, 24.0f, semitones);
    if (!isGlobalKitScaleQuantizeEnabled())
        return clamped;
    return juce::jlimit(
        -24.0f,
        24.0f,
        ModernAudioEngine::quantizeSemitonesToScale(
            clamped,
            getGlobalKitPitchScale(),
            getGlobalKitPitchRootSemitone()));
}

void StepVstHostAudioProcessor::applyPitchControlToStrip(EnhancedAudioStrip& strip, float semitones)
{
    const float clampedSemitones = quantizePitchSemitonesToGlobalKit(semitones);
    const float ratio = juce::jlimit(0.125f, 4.0f, std::pow(2.0f, clampedSemitones / 12.0f));
    const bool stripIsStepMode = (strip.getPlayMode() == EnhancedAudioStrip::PlayMode::Step);

    if (stripIsStepMode)
    {
        // Step mode: keep control-domain semitone range unchanged, but expand
        // the resulting playback-speed range to +/-3 octaves (0.125x..8x).
        const float stepSpeedSemitones = clampedSemitones * 1.5f;
        const float stepRatio = juce::jlimit(0.125f, 8.0f, std::pow(2.0f, stepSpeedSemitones / 12.0f));
        strip.setResamplePitchEnabled(false);
        strip.setResamplePitchRatio(1.0f);
        strip.setPitchShift(clampedSemitones);
        if (auto* stepSampler = strip.getStepSampler())
            stepSampler->setSpeed(stepRatio);
        return;
    }

    if (getPitchControlMode() == PitchControlMode::Resample)
    {
        strip.setResamplePitchEnabled(true);
        strip.setResamplePitchRatio(ratio);
        strip.setPitchShift(0.0f);
        // Keep traversal/playmarker speed independent from resample pitch ratio.
        strip.setPlaybackSpeed(1.0f);
        return;
    }

    strip.setResamplePitchEnabled(false);
    strip.setResamplePitchRatio(1.0f);
    strip.setPlaybackSpeed(1.0f);
    strip.setPitchShift(clampedSemitones);
}

void StepVstHostAudioProcessor::applyGlobalKitScaleSettings()
{
    if (!audioEngine)
        return;

    audioEngine->setGlobalPitchScaleQuantizeEnabled(isGlobalKitScaleQuantizeEnabled());
    audioEngine->setGlobalPitchScale(getGlobalKitPitchScale());
    audioEngine->setGlobalPitchRootSemitone(getGlobalKitPitchRootSemitone());

    for (int i = 0; i < MaxStrips; ++i)
    {
        auto* strip = audioEngine->getStrip(i);
        auto* pitchParam = stripPitchParams[static_cast<size_t>(i)];
        if (strip == nullptr || pitchParam == nullptr)
            continue;
        applyPitchControlToStrip(*strip, pitchParam->load(std::memory_order_acquire));
    }

    if (beatSpaceDecoderReady && !beatSpaceTable.empty())
    {
        if (!beatSpaceMappingReady)
            refreshBeatSpaceParameterMap(false);
        if (beatSpaceMappingReady)
            applyBeatSpacePointToChannels(true, false);
    }

    requestImmediateUiAndMonomeRefresh();
}

float StepVstHostAudioProcessor::getPitchSemitonesForDisplay(const EnhancedAudioStrip& strip) const
{
    if (strip.getPlayMode() == EnhancedAudioStrip::PlayMode::Step)
    {
        if (const auto* stepSampler = strip.getStepSampler())
            return juce::jlimit(-24.0f, 24.0f, static_cast<float>(stepSampler->getPitchOffset()) / 1.5f);
    }

    if (getPitchControlMode() == PitchControlMode::Resample)
    {
        const float ratio = strip.isResamplePitchEnabled()
            ? strip.getResamplePitchRatio()
            : 1.0f;
        const float semitones = 12.0f * std::log2(ratio);
        return juce::jlimit(-24.0f, 24.0f, semitones);
    }

    return strip.getPitchShift();
}

StepVstHostAudioProcessor::ControlPageOrder StepVstHostAudioProcessor::getControlPageOrder() const
{
    const juce::ScopedLock lock(controlPageOrderLock);
    return controlPageOrder;
}

StepVstHostAudioProcessor::ControlMode StepVstHostAudioProcessor::getControlModeForControlButton(int buttonIndex) const
{
    const int clamped = juce::jlimit(0, NumControlRowPages - 1, buttonIndex);
    const juce::ScopedLock lock(controlPageOrderLock);
    return controlPageOrder[static_cast<size_t>(clamped)];
}

int StepVstHostAudioProcessor::getControlButtonForMode(ControlMode mode) const
{
    const juce::ScopedLock lock(controlPageOrderLock);
    for (int i = 0; i < NumControlRowPages; ++i)
    {
        if (controlPageOrder[static_cast<size_t>(i)] == mode)
            return i;
    }
    return -1;
}

void StepVstHostAudioProcessor::moveControlPage(int fromIndex, int toIndex)
{
    if (fromIndex == toIndex)
        return;

    fromIndex = juce::jlimit(0, NumControlRowPages - 1, fromIndex);
    toIndex = juce::jlimit(0, NumControlRowPages - 1, toIndex);
    if (fromIndex == toIndex)
        return;

    {
        const juce::ScopedLock lock(controlPageOrderLock);
        std::swap(controlPageOrder[static_cast<size_t>(fromIndex)],
                  controlPageOrder[static_cast<size_t>(toIndex)]);
    }

    savePersistentControlPages();
}

void StepVstHostAudioProcessor::setControlPageMomentary(bool shouldBeMomentary)
{
    controlPageMomentary.store(shouldBeMomentary, std::memory_order_release);
    savePersistentControlPages();
}

void StepVstHostAudioProcessor::setSwingDivisionSelection(int mode)
{
    const int maxDivision = static_cast<int>(EnhancedAudioStrip::SwingDivision::SixteenthTriplet);
    const int clamped = juce::jlimit(0, maxDivision, mode);
    const int previous = swingDivisionSelection.load(std::memory_order_acquire);
    if (previous == clamped)
    {
        if (audioEngine)
            audioEngine->setGlobalSwingDivision(static_cast<EnhancedAudioStrip::SwingDivision>(clamped));
        return;
    }

    swingDivisionSelection.store(clamped, std::memory_order_release);
    if (audioEngine)
        audioEngine->setGlobalSwingDivision(static_cast<EnhancedAudioStrip::SwingDivision>(clamped));
    savePersistentControlPages();
}

void StepVstHostAudioProcessor::setControlModeFromGui(ControlMode mode, bool shouldBeActive)
{
    if (!shouldBeActive || mode == ControlMode::Normal)
    {
        currentControlMode = ControlMode::Normal;
        controlModeActive = false;
    }
    else
    {
        currentControlMode = mode;
        controlModeActive = true;
    }

    updateMonomeLEDs();
}

juce::AudioProcessorValueTreeState::ParameterLayout StepVstHostAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto globalFloatAttrs = juce::AudioParameterFloatAttributes().withAutomatable(false);
    const auto globalChoiceAttrs = juce::AudioParameterChoiceAttributes().withAutomatable(false);
    const auto globalBoolAttrs = juce::AudioParameterBoolAttributes().withAutomatable(false);
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "masterVolume",
        "Master Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f),
        1.0f,
        globalFloatAttrs));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "limiterThreshold",
        "Limiter Threshold (dB)",
        juce::NormalisableRange<float>(-24.0f, 0.0f, 0.1f),
        0.0f,
        globalFloatAttrs));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "limiterEnabled",
        "Limiter Enabled",
        false,
        globalBoolAttrs));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "quantize",
        "Quantize",
        juce::StringArray{"1", "1/2", "1/2T", "1/4", "1/4T",
                          "1/8", "1/8T", "1/16", "1/16T", "1/32"},
        5,
        globalChoiceAttrs));
    
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "pitchSmoothing",
        "Pitch Smoothing",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.05f,
        globalFloatAttrs));  // Default 50ms

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "outputRouting",
        "Output Routing",
        juce::StringArray{"Stereo Mix", "Separate Strip Outs"},
        0,
        globalChoiceAttrs));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "soundTouchEnabled",
        "SoundTouch Enabled",
        true,
        globalBoolAttrs));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "kitScaleEnabled",
        "Kit Scale Enabled",
        false,
        globalBoolAttrs));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "kitScaleMode",
        "Kit Scale Mode",
        juce::StringArray{"Chromatic", "Major", "Minor", "Dorian", "Pentatonic"},
        0,
        globalChoiceAttrs));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "kitScaleRoot",
        "Kit Scale Root",
        juce::StringArray{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
        0,
        globalChoiceAttrs));
    
    for (int i = 0; i < MaxStrips; ++i)
    {
        juce::NormalisableRange<float> attackRange(0.0f, 400.0f, 0.1f);
        attackRange.setSkewForCentre(12.0f);
        juce::NormalisableRange<float> decayRange(1.0f, 4000.0f, 0.1f);
        decayRange.setSkewForCentre(700.0f);
        juce::NormalisableRange<float> releaseRange(1.0f, 4000.0f, 0.1f);
        releaseRange.setSkewForCentre(180.0f);

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "stripVolume" + juce::String(i),
            "Strip " + juce::String(i + 1) + " Volume",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            1.0f));
            
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "stripPan" + juce::String(i),
            "Strip " + juce::String(i + 1) + " Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f),
            0.0f));
        
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "stripSpeed" + juce::String(i),
            "Strip " + juce::String(i + 1) + " Playhead Speed",
            juce::NormalisableRange<float>(0.0f, 8.0f, 0.01f, 0.5f),
            1.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "stripPitch" + juce::String(i),
            "Strip " + juce::String(i + 1) + " Pitch",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f),
            0.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "stripStepAttack" + juce::String(i),
            "Strip " + juce::String(i + 1) + " Step Attack",
            attackRange,
            0.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "stripStepDecay" + juce::String(i),
            "Strip " + juce::String(i + 1) + " Step Decay",
            decayRange,
            4000.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "stripStepRelease" + juce::String(i),
            "Strip " + juce::String(i + 1) + " Step Release",
            releaseRange,
            110.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "stripSliceLength" + juce::String(i),
            "Strip " + juce::String(i + 1) + " Slice Length",
            juce::NormalisableRange<float>(0.02f, 1.0f, 0.001f, 0.5f),
            1.0f));
    }
    
    return layout;
}

//==============================================================================
void StepVstHostAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    audioEngine->prepareToPlay(sampleRate, samplesPerBlock);
    hostRack.prepareToPlay(sampleRate, samplesPerBlock);
    juce::String hostedLoadError;
    if (!loadDefaultHostedInstrumentIfNeeded(hostedLoadError) && hostedLoadError.isNotEmpty())
        DBG("Default hosted plugin auto-load failed: " << hostedLoadError);
    refreshBeatSpaceParameterMap();
    lastAppliedSoundTouchEnabled = -1;
    lastGridLedUpdateTimeMs = 0;

    // Now safe to connect to monome
    if (!monomeConnection.isConnected())
        monomeConnection.connect(8000);

    // Clear all LEDs on startup
    juce::MessageManager::callAsync([this]()
    {
        if (monomeConnection.isConnected())
        {
            if (monomeConnection.supportsGrid())
            {
                monomeConnection.setAllLEDs(0);
                // Initialize LED cache
                for (int y = 0; y < MaxGridHeight; ++y)
                    for (int x = 0; x < MaxGridWidth; ++x)
                        ledCache[x][y] = -1;
            }
            if (monomeConnection.supportsArc())
            {
                for (auto& ring : arcRingCache)
                    ring.fill(-1);
                updateMonomeArcRings();
            }
        }
    });

    // Start LED update timer at 10fps (monome recommended refresh rate)
    if (!isTimerRunning())
        startTimer(kGridRefreshMs);

    if (!persistentGlobalControlsApplied)
    {
        suppressPersistentGlobalControlsSave.store(1, std::memory_order_release);
        loadPersistentGlobalControls();
        suppressPersistentGlobalControlsSave.store(0, std::memory_order_release);
        persistentGlobalControlsApplied = true;
    }

    pendingPersistentGlobalControlsRestore.store(1, std::memory_order_release);
    pendingPersistentGlobalControlsRestoreMs = juce::Time::currentTimeMillis() + 250;
    pendingPersistentGlobalControlsRestoreRemaining = 5;
}

void StepVstHostAudioProcessor::releaseResources()
{
    stopTimer();
    monomeConnection.disconnect();
    hostRack.releaseResources();
}

bool StepVstHostAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Main output is fixed stereo; strip outputs are stereo buses.
    auto mainOutput = layouts.getMainOutputChannelSet();
    if (mainOutput != juce::AudioChannelSet::stereo())
        return false;

    // Aux outputs are either disabled or match main output channel set.
    const int outputBusCount = layouts.outputBuses.size();
    for (int bus = 1; bus < outputBusCount; ++bus)
    {
        const auto busSet = layouts.getChannelSet(false, bus);
        if (busSet != juce::AudioChannelSet::disabled() && busSet != mainOutput)
            return false;
    }

    // Check input (we accept mono or stereo input, or disabled)
    auto inputChannels = layouts.getMainInputChannelSet();
    if (inputChannels != juce::AudioChannelSet::disabled()
     && inputChannels != juce::AudioChannelSet::mono()
     && inputChannels != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void StepVstHostAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    // CRITICAL: Handle separate input/output buffers for AU/VST3 compatibility
    // Some hosts (especially AU) provide separate input and output buffers
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    // Clear any output channels that don't have corresponding input
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    
    // Get position info from host
    juce::AudioPlayHead::PositionInfo posInfo;
    
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            posInfo = *position;
        }
        else
        {
            // Host didn't provide position - assume playing
            posInfo.setIsPlaying(true);
        }
    }
    else
    {
        // No playhead - assume playing  
        posInfo.setIsPlaying(true);
    }
    
    // Set tempo FIRST: use host tempo if available, otherwise fallback default.
    if (!posInfo.getBpm().hasValue() || *posInfo.getBpm() <= 0.0)
    {
        posInfo.setBpm(120.0);  // Fallback default
    }
    syncRuntimeStateFromParameters();

    // Apply any pending loop enter/exit actions that were quantized to timeline.
    applyPendingLoopChanges(posInfo);
    applyPendingBarChanges(posInfo);
    applyPendingStutterRelease(posInfo);
    applyPendingStutterStart(posInfo);
    updateSubPresetQuantizedRecall(posInfo, buffer.getNumSamples());

    updateBeatSpacePathMorph(posInfo);
    applyMomentaryStutterMacro(posInfo);
    
    const bool separateStripRouting = (outputRoutingParam != nullptr && *outputRoutingParam > 0.5f);
    if (separateStripRouting && getBusCount(false) > 1)
    {
        std::array<std::array<float*, 2>, MaxStrips> stripBusChannels{};
        std::array<juce::AudioBuffer<float>, MaxStrips> stripBusViews;
        std::array<juce::AudioBuffer<float>*, MaxStrips> stripBusTargets{};
        stripBusTargets.fill(nullptr);

        for (int stripIndex = 0; stripIndex < MaxStrips; ++stripIndex)
        {
            const int busIndex = stripIndex; // Strip 1 => main bus, others => aux buses.
            if (busIndex >= getBusCount(false))
                continue;

            auto busBuffer = getBusBuffer(buffer, false, busIndex);
            if (busBuffer.getNumChannels() <= 0 || busBuffer.getNumSamples() <= 0)
                continue;

            auto& channelPtrs = stripBusChannels[static_cast<size_t>(stripIndex)];
            channelPtrs.fill(nullptr);
            channelPtrs[0] = busBuffer.getWritePointer(0);
            channelPtrs[1] = (busBuffer.getNumChannels() > 1)
                                 ? busBuffer.getWritePointer(1)
                                 : busBuffer.getWritePointer(0);

            stripBusViews[static_cast<size_t>(stripIndex)].setDataToReferTo(
                channelPtrs.data(), 2, busBuffer.getNumSamples());
            stripBusTargets[static_cast<size_t>(stripIndex)] = &stripBusViews[static_cast<size_t>(stripIndex)];
        }

        // Keep playback robust if some aux buses are disabled in host: fallback to main bus.
        auto* mainTarget = stripBusTargets[0];
        if (mainTarget == nullptr)
            mainTarget = &buffer;
        for (int stripIndex = 0; stripIndex < MaxStrips; ++stripIndex)
        {
            if (stripBusTargets[static_cast<size_t>(stripIndex)] == nullptr)
                stripBusTargets[static_cast<size_t>(stripIndex)] = mainTarget;
        }

        audioEngine->processBlock(buffer, midiMessages, posInfo, &stripBusTargets);
    }
    else
    {
        // Process audio
        audioEngine->processBlock(buffer, midiMessages, posInfo, nullptr);
    }

    applyBeatSpaceModTargetOffsets();
    applyFavoritesModTargetRecall();

    // Render hosted instrument from per-lane step MIDI (each strip uses its own channel).
    juce::MidiBuffer hostedLaneMidi;
    buildHostedLaneMidi(posInfo, buffer.getNumSamples(), hostedLaneMidi);

    if (hostRack.getInstance() != nullptr)
    {
        const int hostedOutChannels = juce::jmax(1, hostRack.getOutputChannelCount());
        const int mixChannels = juce::jmax(buffer.getNumChannels(), hostedOutChannels);
        juce::AudioBuffer<float> hostedBuffer(mixChannels, buffer.getNumSamples());
        hostedBuffer.clear();
        hostRack.processBlock(hostedBuffer, hostedLaneMidi);

        for (int ch = 0; ch < juce::jmin(buffer.getNumChannels(), hostedBuffer.getNumChannels()); ++ch)
            buffer.addFrom(ch, 0, hostedBuffer, ch, 0, buffer.getNumSamples());
    }
}

//==============================================================================
bool StepVstHostAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* StepVstHostAudioProcessor::createEditor()
{
    return new StepVstHostAudioProcessorEditor(*this);
}

//==============================================================================
void StepVstHostAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    try
    {
        auto state = parameters.copyState();
        stripPersistentGlobalControlsFromState(state);
        appendDefaultPathsToState(state);
        appendHostedLaneMidiToState(state);
        appendControlPagesToState(state);
        state.setProperty(
            "activeMainPresetIndex",
            juce::jlimit(0, MaxPresetSlots - 1, getActiveMainPresetIndexForSubPresets()),
            nullptr);
        state.setProperty(
            "activeSubPresetSlot",
            juce::jlimit(0, SubPresetSlots - 1, activeSubPresetSlot),
            nullptr);
        
        if (!state.isValid())
            return;
            
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        
        if (xml != nullptr)
        {
            copyXmlToBinary(*xml, destData);
        }
    }
    catch (...)
    {
        // If anything goes wrong, just return empty state
        destData.reset();
    }
}

void StepVstHostAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
        {
            suppressPersistentGlobalControlsSave.store(1, std::memory_order_release);
            auto state = juce::ValueTree::fromXml(*xmlState);
            stripPersistentGlobalControlsFromState(state);
            parameters.replaceState(state);
            loadDefaultPathsFromState(state);
            loadHostedLaneMidiFromState(state);
            loadControlPagesFromState(state);
            activeMainPresetIndex = juce::jlimit(
                0,
                MaxPresetSlots - 1,
                static_cast<int>(state.getProperty(
                    "activeMainPresetIndex",
                    getActiveMainPresetIndexForSubPresets())));
            activeSubPresetSlot = juce::jlimit(
                0,
                SubPresetSlots - 1,
                static_cast<int>(state.getProperty("activeSubPresetSlot", activeSubPresetSlot)));
            ensureSubPresetsInitializedForMainPreset(activeMainPresetIndex);
            pendingPresetLoadIndex.store(activeMainPresetIndex, std::memory_order_release);
            pendingPresetLoadQueuedAtMs = juce::Time::getMillisecondCounter();
            pendingBeatSpaceStartupApply.store(1, std::memory_order_release);
            loadPersistentGlobalControls();
            persistentGlobalControlsApplied = true;
            pendingPersistentGlobalControlsRestore.store(1, std::memory_order_release);
            pendingPersistentGlobalControlsRestoreMs = juce::Time::currentTimeMillis() + 250;
            pendingPersistentGlobalControlsRestoreRemaining = 5;
            suppressPersistentGlobalControlsSave.store(0, std::memory_order_release);
            persistentGlobalControlsReady.store(1, std::memory_order_release);
            syncRuntimeStateFromParameters();
            requestImmediateUiAndMonomeRefresh();
        }
}

void StepVstHostAudioProcessor::stripPersistentGlobalControlsFromState(juce::ValueTree& state) const
{
    if (!state.isValid())
        return;
    for (const auto* id : kPersistentGlobalControlParameterIds)
        state.removeProperty(id, nullptr);
}

void StepVstHostAudioProcessor::applyBeatSpaceDefaultChannelLayout()
{
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
        return;

    for (int i = 0; i < BeatSpaceChannels; ++i)
        beatSpaceChannelCategoryAssignment[static_cast<size_t>(i)] = i;

    if (!beatSpaceCategoryAnchorsReady)
        rebuildBeatSpaceCategoryAnchors();

    // Kick, snare, closed hat, open hat, percussion, misc.
    // These hand-picked fallbacks bias startup toward clearly separated regions
    // before local scoring/refinement is applied.
    static constexpr std::array<juce::Point<int>, BeatSpaceChannels> kFallbackPoints {{
        { 12, 50 }, { 24, 34 }, { 50, 10 }, { 54, 22 }, { 39, 40 }, { 30, 28 }
    }};

    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        const int assignedSpace = juce::jlimit(
            0,
            BeatSpaceChannels - 1,
            beatSpaceChannelCategoryAssignment[idx]);

        auto scoreCandidate = [&](const juce::Point<int>& candidatePoint) -> float
        {
            const auto p = clampBeatSpacePointToTable(candidatePoint);
            const int tableIndex = (p.y * BeatSpaceTableSize) + p.x;
            if (tableIndex < 0 || tableIndex >= static_cast<int>(beatSpaceTable.size()))
                return -1.0e9f;

            const auto& cellValues = beatSpaceTable[static_cast<size_t>(tableIndex)].values;
            const float ownScore = scoreBeatSpaceCategoryPoint(assignedSpace, cellValues);

            float otherBest = -1.0e9f;
            for (int other = 0; other < BeatSpaceChannels; ++other)
            {
                if (other == assignedSpace)
                    continue;
                otherBest = juce::jmax(otherBest, scoreBeatSpaceCategoryPoint(other, cellValues));
            }

            const float margin = ownScore - otherBest;
            const float hotspot = (tableIndex >= 0 && tableIndex < static_cast<int>(beatSpaceHotspotWeights.size()))
                ? juce::jlimit(0.0f, 1.0f, beatSpaceHotspotWeights[static_cast<size_t>(tableIndex)])
                : 0.35f;

            float anchorPenalty = 0.0f;
            if (beatSpaceCategoryAnchorsReady)
            {
                const auto anchor = beatSpaceCategoryAnchors[static_cast<size_t>(assignedSpace)];
                const float dx = static_cast<float>(p.x - anchor.x);
                const float dy = static_cast<float>(p.y - anchor.y);
                const float distance = std::sqrt((dx * dx) + (dy * dy));
                anchorPenalty = juce::jlimit(0.0f, 0.22f, distance / 90.0f);
            }

            const float confidence = getBeatSpacePointConfidence(p);
            return ownScore
                + (0.60f * margin)
                + (0.08f * hotspot)
                + (0.10f * confidence)
                - anchorPenalty;
        };

        juce::Point<int> sourcePoint = kFallbackPoints[static_cast<size_t>(assignedSpace)];
        float bestScore = scoreCandidate(sourcePoint);

        if (beatSpaceCategoryAnchorsReady)
        {
            const auto anchor = beatSpaceCategoryAnchors[static_cast<size_t>(assignedSpace)];
            const float anchorScore = scoreCandidate(anchor);
            if (anchorScore > bestScore)
            {
                bestScore = anchorScore;
                sourcePoint = anchor;
            }
        }

        const auto visibleSlots = getBeatSpaceVisiblePresetSlotsForSpace(assignedSpace);
        if (!visibleSlots.empty())
        {
            for (const int slot : visibleSlots)
            {
                const int clampedSlot = juce::jlimit(0, BeatSpacePresetSlotsPerSpace - 1, slot);
                const auto candidate = beatSpaceCategoryPresetPoints[static_cast<size_t>(assignedSpace)]
                    [static_cast<size_t>(clampedSlot)];
                const float candidateScore = scoreCandidate(candidate);
                if (candidateScore > bestScore)
                {
                    bestScore = candidateScore;
                    sourcePoint = candidate;
                }
            }
        }

        // Keep startup defaults category-faithful by searching around the category
        // anchor/region first instead of allowing far-away outliers to win globally.
        const auto searchCenter = sourcePoint;
        const int baseRx = beatSpaceCategoryAnchorsReady
            ? juce::jmax(1, beatSpaceCategoryRegionRadiusX[static_cast<size_t>(assignedSpace)])
            : 12;
        const int baseRy = beatSpaceCategoryAnchorsReady
            ? juce::jmax(1, beatSpaceCategoryRegionRadiusY[static_cast<size_t>(assignedSpace)])
            : 12;
        const int searchRx = juce::jlimit(8, BeatSpaceTableSize - 1, baseRx * 3);
        const int searchRy = juce::jlimit(8, BeatSpaceTableSize - 1, baseRy * 3);
        const int minX = juce::jmax(0, searchCenter.x - searchRx);
        const int maxX = juce::jmin(BeatSpaceTableSize - 1, searchCenter.x + searchRx);
        const int minY = juce::jmax(0, searchCenter.y - searchRy);
        const int maxY = juce::jmin(BeatSpaceTableSize - 1, searchCenter.y + searchRy);
        const int preferredCluster = beatSpaceColorClustersReady
            ? beatSpaceCategoryColorCluster[static_cast<size_t>(assignedSpace)]
            : -1;

        auto scanWindow = [&](bool requirePreferredCluster) -> bool
        {
            bool considered = false;
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const int tableIndex = (y * BeatSpaceTableSize) + x;
                    if (tableIndex < 0 || tableIndex >= static_cast<int>(beatSpaceTable.size()))
                        continue;
                    if (requirePreferredCluster
                        && preferredCluster >= 0
                        && (tableIndex < 0
                            || tableIndex >= static_cast<int>(beatSpaceColorClusters.size())
                            || beatSpaceColorClusters[static_cast<size_t>(tableIndex)] != preferredCluster))
                    {
                        continue;
                    }

                    considered = true;
                    const juce::Point<int> candidate { x, y };
                    const float candidateScore = scoreCandidate(candidate);
                    if (candidateScore > bestScore)
                    {
                        bestScore = candidateScore;
                        sourcePoint = candidate;
                    }
                }
            }
            return considered;
        };

        bool consideredLocal = false;
        if (preferredCluster >= 0)
            consideredLocal = scanWindow(true);
        if (!consideredLocal)
            consideredLocal = scanWindow(false);

        if (!consideredLocal)
        {
            for (int y = 0; y < BeatSpaceTableSize; ++y)
            {
                for (int x = 0; x < BeatSpaceTableSize; ++x)
                {
                    const juce::Point<int> candidate { x, y };
                    const float candidateScore = scoreCandidate(candidate);
                    if (candidateScore > bestScore)
                    {
                        bestScore = candidateScore;
                        sourcePoint = candidate;
                    }
                }
            }
        }

        beatSpaceChannelPoints[idx] = constrainBeatSpacePointForChannel(i, sourcePoint);
        beatSpaceMorphStartPoints[idx] = beatSpaceChannelPoints[idx];
        beatSpaceMorphTargetPoints[idx] = beatSpaceChannelPoints[idx];
        beatSpaceMorphCurrentPoints[idx] = beatSpaceChannelPoints[idx];
        beatSpaceMorphProgress[idx] = 1.0f;
        beatSpaceMorphActive[idx] = false;
    }

    beatSpaceSelectedChannel = 0;
    beatSpaceLinkAllChannels = true;
    rebuildBeatSpaceLinkedOffsetsFromCurrent(beatSpaceSelectedChannel);
}

bool StepVstHostAudioProcessor::loadHostedInstrument(const juce::File& file, juce::String& error)
{
    const double sampleRate = (currentSampleRate > 1.0) ? currentSampleRate : 44100.0;
    const int blockSize = juce::jmax(32, getBlockSize() > 0 ? getBlockSize() : 512);
    const bool loaded = hostRack.loadPlugin(file, sampleRate, blockSize, error);
    if (loaded)
    {
        {
            const juce::ScopedLock lock(hostedInstrumentFileLock);
            loadedHostedInstrumentFile = file;
        }
        refreshBeatSpaceParameterMap();

        if (auto* instance = hostRack.getInstance(); instance != nullptr)
        {
            auto dumpDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("step-vsthost");
            dumpDir.createDirectory();
            auto dumpFile = dumpDir.getChildFile("HostedInstrumentParameters.txt");
            juce::StringArray lines;
            lines.add("Plugin: " + instance->getName());
            lines.add("File: " + file.getFullPathName());
            lines.add("Total parameters: " + juce::String(instance->getParameters().size()));
            lines.add("");
            const auto& loadedParams = instance->getParameters();
            for (int paramIndex = 0; paramIndex < static_cast<int>(loadedParams.size()); ++paramIndex)
            {
                const auto name = loadedParams[paramIndex]->getName(256);
                lines.add(juce::String(paramIndex).paddedLeft('0', 4) + " : " + name);
            }
            dumpFile.replaceWithText(lines.joinIntoString("\n"));
        }

        if (auto* instance = hostRack.getInstance(); instance != nullptr
            && isLikelyMicrotonicPlugin(instance->getName()))
        {
            if (!beatSpacePersistentLayoutLoaded)
                applyBeatSpaceDefaultChannelLayout();
        }

        // Startup BeatSpace recall is finalized from timerCallback once hosted mapping
        // is fully ready so channel presets and dot positions are deterministic.
        pendingBeatSpaceStartupApply.store(1, std::memory_order_release);
    }
    else
    {
        clearHostedDirectParameterMap();
        beatSpaceMappingReady = false;
        for (auto& ready : beatSpaceChannelMappingReady)
            ready = false;
    }
    return loaded;
}

juce::File StepVstHostAudioProcessor::getLoadedHostedInstrumentFile() const
{
    const juce::ScopedLock lock(hostedInstrumentFileLock);
    return loadedHostedInstrumentFile;
}

juce::File StepVstHostAudioProcessor::getDefaultHostedInstrumentFile() const
{
    const juce::ScopedLock lock(hostedInstrumentFileLock);
    return defaultHostedInstrumentFile;
}

bool StepVstHostAudioProcessor::setLoadedHostedInstrumentAsDefault(juce::String& error)
{
    error.clear();
    juce::File loadedFile;
    {
        const juce::ScopedLock lock(hostedInstrumentFileLock);
        loadedFile = loadedHostedInstrumentFile;
    }

    if (loadedFile == juce::File())
    {
        error = "No hosted plugin is currently loaded.";
        return false;
    }

    if (!loadedFile.exists())
    {
        error = "Hosted plugin file is missing: " + loadedFile.getFullPathName();
        return false;
    }

    {
        const juce::ScopedLock lock(hostedInstrumentFileLock);
        defaultHostedInstrumentFile = loadedFile;
    }
    savePersistentControlPages();
    return true;
}

bool StepVstHostAudioProcessor::loadDefaultHostedInstrumentIfNeeded(juce::String& error)
{
    error.clear();
    if (hostedDefaultAutoLoadAttempted)
        return hostRack.getInstance() != nullptr;

    hostedDefaultAutoLoadAttempted = true;
    if (hostRack.getInstance() != nullptr)
        return true;

    juce::File targetFile;
    {
        const juce::ScopedLock lock(hostedInstrumentFileLock);
        targetFile = defaultHostedInstrumentFile;
    }

    if (targetFile == juce::File())
        return false;

    if (!targetFile.exists())
    {
        error = "Default hosted plugin missing: " + targetFile.getFullPathName();
        return false;
    }

    return loadHostedInstrument(targetFile, error);
}

void StepVstHostAudioProcessor::rebuildBeatSpaceTablePreviewImage()
{
    beatSpaceTablePreviewImage = {};
    beatSpaceClusterOverlayImage = {};
    beatSpaceConfidencePreviewImage = {};
    if (beatSpaceTable.empty())
        return;

    const int tableCells = BeatSpaceTableSize * BeatSpaceTableSize;
    const bool hasClusterData =
        beatSpaceColorClustersReady
        && static_cast<int>(beatSpaceColorClusters.size()) >= tableCells;
    const auto scriptDir = getBeatSpaceScriptDirectory();
    const auto mapImage = loadBeatSpaceMapImage(scriptDir, beatSpaceDecoderName);
    const bool hasMapImage = mapImage.isValid();

    std::vector<int> mapXLookup;
    std::vector<int> mapYLookup;
    if (hasMapImage)
    {
        mapXLookup.resize(static_cast<size_t>(BeatSpaceTableSize));
        mapYLookup.resize(static_cast<size_t>(BeatSpaceTableSize));
        const int denom = juce::jmax(1, BeatSpaceTableSize - 1);
        for (int x = 0; x < BeatSpaceTableSize; ++x)
        {
            mapXLookup[static_cast<size_t>(x)] = juce::jlimit(
                0,
                mapImage.getWidth() - 1,
                static_cast<int>(std::round((static_cast<double>(x) / static_cast<double>(denom))
                                            * static_cast<double>(mapImage.getWidth() - 1))));
        }
        for (int y = 0; y < BeatSpaceTableSize; ++y)
        {
            mapYLookup[static_cast<size_t>(y)] = juce::jlimit(
                0,
                mapImage.getHeight() - 1,
                static_cast<int>(std::round((static_cast<double>(y) / static_cast<double>(denom))
                                            * static_cast<double>(mapImage.getHeight() - 1))));
        }
    }

    auto mapPixelAtCell = [&](int x, int y) -> juce::Colour
    {
        if (!hasMapImage)
            return {};
        const int imgX = mapXLookup[static_cast<size_t>(juce::jlimit(0, BeatSpaceTableSize - 1, x))];
        const int imgY = mapYLookup[static_cast<size_t>(juce::jlimit(0, BeatSpaceTableSize - 1, y))];
        return mapImage.getPixelAt(imgX, imgY);
    };

    static constexpr std::array<uint32_t, BeatSpaceChannels> kCategoryPalette {
        0xffea5455u, // Kick
        0xfff07f3au, // Snare
        0xff6bcf80u, // Closed hat
        0xff4db6b8u, // Open hat
        0xff5f8fd3u, // Perc
        0xff9f7ccbu  // Misc
    };
    static constexpr std::array<uint32_t, BeatSpaceChannels> kFallbackClusterPalette {
        0xfff06d6du,
        0xfff2a74cu,
        0xff7ed894u,
        0xff5ec8c2u,
        0xff6e9ee7u,
        0xffb091e0u
    };

    auto clusterTintForCell = [&](int cluster) -> juce::Colour
    {
        for (int category = 0; category < BeatSpaceChannels; ++category)
        {
            if (beatSpaceCategoryColorCluster[static_cast<size_t>(category)] == cluster)
                return juce::Colour(kCategoryPalette[static_cast<size_t>(category)]);
        }
        const auto fallback = static_cast<size_t>(
            juce::jlimit(0, static_cast<int>(kFallbackClusterPalette.size()) - 1, cluster));
        return juce::Colour(kFallbackClusterPalette[fallback]);
    };

    auto clusterIndexAt = [&](int x, int y) -> int
    {
        const int cellIndex = (y * BeatSpaceTableSize) + x;
        return hasClusterData
            ? juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceColorClusters[static_cast<size_t>(cellIndex)])
            : 0;
    };

    juce::Image preview(juce::Image::RGB, BeatSpaceTableSize, BeatSpaceTableSize, true);
    preview.clear(preview.getBounds(), juce::Colours::black);

    for (int y = 0; y < BeatSpaceTableSize; ++y)
    {
        for (int x = 0; x < BeatSpaceTableSize; ++x)
        {
            const auto& values = beatSpaceTable[static_cast<size_t>(y * BeatSpaceTableSize + x)].values;

            float patchAverage = 0.0f;
            for (int i = 0; i < BeatSpacePatchParamCount; ++i)
                patchAverage += juce::jlimit(0.0f, 1.0f, values[static_cast<size_t>(i)]);
            patchAverage /= static_cast<float>(BeatSpacePatchParamCount);
            const float toneBalance = juce::jlimit(
                0.0f,
                1.0f,
                (juce::jlimit(0.0f, 1.0f, values[1]) * 0.55f)
                    + (juce::jlimit(0.0f, 1.0f, values[8]) * 0.45f));
            const float textureAmount = juce::jlimit(
                0.0f,
                1.0f,
                (juce::jlimit(0.0f, 1.0f, values[14]) * 0.62f)
                    + (juce::jlimit(0.0f, 1.0f, values[6]) * 0.26f)
                    + (juce::jlimit(0.0f, 1.0f, values[9]) * 0.12f));
            const float transientAmount = juce::jlimit(
                0.0f,
                1.0f,
                ((1.0f - juce::jlimit(0.0f, 1.0f, values[2])) * 0.62f)
                    + ((1.0f - juce::jlimit(0.0f, 1.0f, values[12])) * 0.38f));
            const float tailAmount = juce::jlimit(
                0.0f,
                1.0f,
                (juce::jlimit(0.0f, 1.0f, values[3]) * 0.55f)
                    + (juce::jlimit(0.0f, 1.0f, values[13]) * 0.45f));
            const float driveAmount = juce::jlimit(0.0f, 1.0f, values[15]);

            juce::Colour cellColour;
            if (hasMapImage)
            {
                // Keep original script map appearance intact as the base layer.
                cellColour = mapPixelAtCell(x, y);
            }
            else
            {
                const float hue = juce::jlimit(
                    0.0f,
                    1.0f,
                    0.08f
                        + (0.44f * juce::jlimit(0.0f, 1.0f, values[0]))
                        + (0.26f * juce::jlimit(0.0f, 1.0f, values[16]))
                        + (0.10f * toneBalance));
                const float saturation = juce::jlimit(
                    0.28f,
                    0.98f,
                    0.32f + (0.46f * textureAmount) + (0.14f * driveAmount));
                const float brightness = juce::jlimit(
                    0.15f,
                    0.99f,
                    0.18f + (0.50f * patchAverage) + (0.18f * transientAmount) + (0.14f * tailAmount));
                cellColour = juce::Colour::fromHSV(hue, saturation, brightness, 1.0f);
            }

            if (hasClusterData)
            {
                const int cellIndex = (y * BeatSpaceTableSize) + x;
                const int cluster = clusterIndexAt(x, y);
                const auto clusterTint = clusterTintForCell(cluster);
                const float hotspot = (cellIndex >= 0 && cellIndex < static_cast<int>(beatSpaceHotspotWeights.size()))
                    ? juce::jlimit(0.0f, 1.0f, beatSpaceHotspotWeights[static_cast<size_t>(cellIndex)])
                    : 0.0f;
                const float baseBrightness = hasMapImage
                    ? mapPixelAtCell(x, y).getBrightness()
                    : juce::jlimit(
                        0.0f,
                        1.0f,
                        0.20f + (0.62f * patchAverage) + (0.12f * transientAmount));
                const float clusterBrightness = juce::jlimit(0.18f, 0.97f, 0.18f + (0.74f * baseBrightness));
                const auto clusterTone = juce::Colour::fromHSV(
                    clusterTint.getHue(),
                    juce::jlimit(0.48f, 1.0f, juce::jmax(clusterTint.getSaturation(), 0.58f)),
                    clusterBrightness,
                    1.0f);
                const float tintAmount = hasMapImage
                    ? juce::jlimit(0.10f, 0.30f, 0.14f + (0.14f * hotspot))
                    : juce::jlimit(0.36f, 0.76f, 0.46f + (0.24f * hotspot));
                cellColour = cellColour.interpolatedWith(clusterTone, tintAmount)
                    .brighter((hasMapImage ? 0.03f : 0.10f) * hotspot);
            }
            preview.setPixelAt(x, y, cellColour);
        }
    }

    // Synthetic fallback map benefits from smoothing; the original script
    // raster should keep its native detail.
    if (!hasMapImage)
    {
        juce::Image smoothed(preview.getFormat(), BeatSpaceTableSize, BeatSpaceTableSize, true);
        for (int y = 0; y < BeatSpaceTableSize; ++y)
        {
            for (int x = 0; x < BeatSpaceTableSize; ++x)
            {
                float accumR = 0.0f;
                float accumG = 0.0f;
                float accumB = 0.0f;
                float accumW = 0.0f;
                const int centerCluster = clusterIndexAt(x, y);

                for (int ny = -1; ny <= 1; ++ny)
                {
                    for (int nx = -1; nx <= 1; ++nx)
                    {
                        const int sx = x + nx;
                        const int sy = y + ny;
                        if (sx < 0 || sy < 0 || sx >= BeatSpaceTableSize || sy >= BeatSpaceTableSize)
                            continue;

                        const bool center = (nx == 0 && ny == 0);
                        const float tapWeightBase = center ? 0.40f : ((nx == 0 || ny == 0) ? 0.11f : 0.07f);
                        float clusterWeight = 1.0f;
                        if (hasClusterData && !center)
                        {
                            const int sampleCluster = clusterIndexAt(sx, sy);
                            clusterWeight = (sampleCluster == centerCluster) ? 1.0f : 0.08f;
                        }
                        const float tapWeight = tapWeightBase * clusterWeight;
                        if (tapWeight <= 1.0e-6f)
                            continue;
                        const auto c = preview.getPixelAt(sx, sy);
                        accumR += c.getFloatRed() * tapWeight;
                        accumG += c.getFloatGreen() * tapWeight;
                        accumB += c.getFloatBlue() * tapWeight;
                        accumW += tapWeight;
                    }
                }

                const float invWeight = (accumW > 0.0001f) ? (1.0f / accumW) : 1.0f;
                smoothed.setPixelAt(
                    x,
                    y,
                    juce::Colour::fromFloatRGBA(
                        juce::jlimit(0.0f, 1.0f, accumR * invWeight),
                        juce::jlimit(0.0f, 1.0f, accumG * invWeight),
                        juce::jlimit(0.0f, 1.0f, accumB * invWeight),
                        1.0f));
            }
        }
        for (int y = 0; y < BeatSpaceTableSize; ++y)
        {
            for (int x = 0; x < BeatSpaceTableSize; ++x)
                preview.setPixelAt(x, y, smoothed.getPixelAt(x, y));
        }
    }

    juce::Image clusterOverlay(juce::Image::ARGB, BeatSpaceTableSize, BeatSpaceTableSize, true);
    clusterOverlay.clear(clusterOverlay.getBounds(), juce::Colours::transparentBlack);
    if (hasClusterData)
    {
        std::vector<uint8_t> boundaryStrength(static_cast<size_t>(tableCells), 0u);
        auto edgeStrengthAt = [&](int x, int y) -> uint8_t
        {
            const int cluster = clusterIndexAt(x, y);
            int edgeStrength = 0;
            const auto checkNeighbor = [&](int nx, int ny)
            {
                if (nx < 0 || ny < 0 || nx >= BeatSpaceTableSize || ny >= BeatSpaceTableSize)
                    return;
                if (clusterIndexAt(nx, ny) != cluster)
                    ++edgeStrength;
            };
            checkNeighbor(x - 1, y);
            checkNeighbor(x + 1, y);
            checkNeighbor(x, y - 1);
            checkNeighbor(x, y + 1);
            return static_cast<uint8_t>(juce::jlimit(0, 4, edgeStrength));
        };

        for (int y = 0; y < BeatSpaceTableSize; ++y)
        {
            for (int x = 0; x < BeatSpaceTableSize; ++x)
            {
                const int idx = (y * BeatSpaceTableSize) + x;
                const int cluster = clusterIndexAt(x, y);
                const auto clusterTint = clusterTintForCell(cluster);
                const float hotspot = (idx >= 0 && idx < static_cast<int>(beatSpaceHotspotWeights.size()))
                    ? juce::jlimit(0.0f, 1.0f, beatSpaceHotspotWeights[static_cast<size_t>(idx)])
                    : 0.0f;

                const float regionAlpha = hasMapImage
                    ? juce::jlimit(0.03f, 0.12f, 0.04f + (0.06f * hotspot))
                    : juce::jlimit(0.06f, 0.20f, 0.09f + (0.07f * hotspot));
                clusterOverlay.setPixelAt(x, y, clusterTint.withAlpha(regionAlpha));
                boundaryStrength[static_cast<size_t>(idx)] = edgeStrengthAt(x, y);
            }
        }

        for (int y = 0; y < BeatSpaceTableSize; ++y)
        {
            for (int x = 0; x < BeatSpaceTableSize; ++x)
            {
                const int idx = (y * BeatSpaceTableSize) + x;
                const int strength = static_cast<int>(boundaryStrength[static_cast<size_t>(idx)]);
                if (strength <= 0)
                    continue;

                auto c = preview.getPixelAt(x, y);
                c = hasMapImage
                    ? c.darker(0.18f).withMultipliedSaturation(0.92f)
                    : c.darker(0.52f).withMultipliedSaturation(0.68f);
                preview.setPixelAt(x, y, c);

                const float edgeAlpha = hasMapImage
                    ? juce::jlimit(0.24f, 0.55f, 0.24f + (0.07f * static_cast<float>(strength)))
                    : juce::jlimit(0.44f, 0.92f, 0.44f + (0.10f * static_cast<float>(strength)));
                clusterOverlay.setPixelAt(x, y, juce::Colours::white.withAlpha(edgeAlpha));

                for (int ny = -1; ny <= 1; ++ny)
                {
                    for (int nx = -1; nx <= 1; ++nx)
                    {
                        if (nx == 0 && ny == 0)
                            continue;
                        const int sx = x + nx;
                        const int sy = y + ny;
                        if (sx < 0 || sy < 0 || sx >= BeatSpaceTableSize || sy >= BeatSpaceTableSize)
                            continue;
                        const int nIdx = (sy * BeatSpaceTableSize) + sx;
                        if (boundaryStrength[static_cast<size_t>(nIdx)] > 0u)
                            continue;

                        auto current = clusterOverlay.getPixelAt(sx, sy);
                        const float featherAlpha = hasMapImage
                            ? (((std::abs(nx) + std::abs(ny)) <= 1) ? 0.08f : 0.04f)
                            : (((std::abs(nx) + std::abs(ny)) <= 1) ? 0.18f : 0.10f);
                        current = current.overlaidWith(juce::Colours::white.withAlpha(featherAlpha));
                        clusterOverlay.setPixelAt(sx, sy, current);
                    }
                }
            }
        }
    }

    beatSpaceTablePreviewImage = preview;
    beatSpaceClusterOverlayImage = clusterOverlay;

    juce::Image confidence(juce::Image::ARGB, BeatSpaceTableSize, BeatSpaceTableSize, true);
    confidence.clear(confidence.getBounds(), juce::Colours::transparentBlack);
    const bool hasHotspots = static_cast<int>(beatSpaceHotspotWeights.size()) >= tableCells;

    for (int y = 0; y < BeatSpaceTableSize; ++y)
    {
        for (int x = 0; x < BeatSpaceTableSize; ++x)
        {
            const int idx = (y * BeatSpaceTableSize) + x;
            float hotspot = hasHotspots
                ? juce::jlimit(0.0f, 1.0f, beatSpaceHotspotWeights[static_cast<size_t>(idx)])
                : 0.45f;

            float neighborAgreement = 0.5f;
            if (hasClusterData)
            {
                const int c = juce::jlimit(
                    0, BeatSpaceChannels - 1, beatSpaceColorClusters[static_cast<size_t>(idx)]);
                int same = 0;
                int total = 0;
                const auto checkNeighbor = [&](int nx, int ny)
                {
                    if (nx < 0 || ny < 0 || nx >= BeatSpaceTableSize || ny >= BeatSpaceTableSize)
                        return;
                    ++total;
                    const int nIdx = (ny * BeatSpaceTableSize) + nx;
                    const int nc = juce::jlimit(
                        0, BeatSpaceChannels - 1, beatSpaceColorClusters[static_cast<size_t>(nIdx)]);
                    if (nc == c)
                        ++same;
                };
                checkNeighbor(x - 1, y);
                checkNeighbor(x + 1, y);
                checkNeighbor(x, y - 1);
                checkNeighbor(x, y + 1);
                if (total > 0)
                    neighborAgreement = static_cast<float>(same) / static_cast<float>(total);
            }

            const float confidenceValue = juce::jlimit(
                0.0f,
                1.0f,
                0.12f + (0.68f * hotspot) + (0.20f * neighborAgreement));
            const auto pixel = juce::Colour::fromRGBA(
                static_cast<uint8_t>(std::round(confidenceValue * 255.0f)),
                static_cast<uint8_t>(std::round(confidenceValue * 255.0f)),
                static_cast<uint8_t>(std::round(confidenceValue * 255.0f)),
                255);
            confidence.setPixelAt(x, y, pixel);
        }
    }

    beatSpaceConfidencePreviewImage = confidence;
}

void StepVstHostAudioProcessor::rebuildBeatSpaceColorClusters(
    const juce::File& scriptDir, const juce::String& decoderName)
{
    const int tableCells = BeatSpaceTableSize * BeatSpaceTableSize;
    beatSpaceColorClusters.assign(static_cast<size_t>(tableCells), -1);
    beatSpaceHotspotWeights.assign(static_cast<size_t>(tableCells), 0.0f);
    beatSpaceCategoryColorCluster.fill(-1);
    beatSpaceColorClustersReady = false;

    if (beatSpaceTable.empty())
        return;

    const auto mapImage = loadBeatSpaceMapImage(scriptDir, decoderName);
    const bool hasMapImage = mapImage.isValid();

    std::vector<int> mapXLookup;
    std::vector<int> mapYLookup;
    if (hasMapImage)
    {
        mapXLookup.resize(static_cast<size_t>(BeatSpaceTableSize));
        mapYLookup.resize(static_cast<size_t>(BeatSpaceTableSize));
        const int denom = juce::jmax(1, BeatSpaceTableSize - 1);
        for (int x = 0; x < BeatSpaceTableSize; ++x)
        {
            mapXLookup[static_cast<size_t>(x)] = juce::jlimit(
                0,
                mapImage.getWidth() - 1,
                static_cast<int>(std::round((static_cast<double>(x) / static_cast<double>(denom))
                                            * static_cast<double>(mapImage.getWidth() - 1))));
        }
        for (int y = 0; y < BeatSpaceTableSize; ++y)
        {
            mapYLookup[static_cast<size_t>(y)] = juce::jlimit(
                0,
                mapImage.getHeight() - 1,
                static_cast<int>(std::round((static_cast<double>(y) / static_cast<double>(denom))
                                            * static_cast<double>(mapImage.getHeight() - 1))));
        }
    }

    constexpr int kPatchFeatureDims = BeatSpacePatchParamCount;
    constexpr int kFeatureDimX = kPatchFeatureDims;
    constexpr int kFeatureDimY = kPatchFeatureDims + 1;
    constexpr int kFeatureDims = kPatchFeatureDims + 2;
    struct SoundFeature
    {
        std::array<float, kFeatureDims> values{};
    };

    auto buildSoundFeature = [](const std::array<float, BeatSpaceVectorSize>& values, int tableX, int tableY) -> SoundFeature
    {
        SoundFeature feature{};
        for (int i = 0; i < kPatchFeatureDims; ++i)
        {
            feature.values[static_cast<size_t>(i)] =
                juce::jlimit(0.0f, 1.0f, values[static_cast<size_t>(i)]);
        }

        const float denom = static_cast<float>(juce::jmax(1, BeatSpaceTableSize - 1));
        feature.values[static_cast<size_t>(kFeatureDimX)] =
            juce::jlimit(0.0f, 1.0f, static_cast<float>(tableX) / denom);
        feature.values[static_cast<size_t>(kFeatureDimY)] =
            juce::jlimit(0.0f, 1.0f, static_cast<float>(tableY) / denom);
        return feature;
    };

    std::array<float, kFeatureDims> featureWeight{};
    featureWeight.fill(1.0f);
    // Small XY prior keeps clusters spatially coherent on the latent map.
    featureWeight[static_cast<size_t>(kFeatureDimX)] = 0.70f;
    featureWeight[static_cast<size_t>(kFeatureDimY)] = 0.70f;

    std::array<float, kFeatureDims> featureInvStd{};
    featureInvStd.fill(1.0f);

    auto squaredDistance = [&](const SoundFeature& a, const SoundFeature& b)
    {
        float distance = 0.0f;
        for (int i = 0; i < kFeatureDims; ++i)
        {
            const auto idx = static_cast<size_t>(i);
            const float d = (a.values[idx] - b.values[idx]) * featureInvStd[idx];
            distance += (d * d) * featureWeight[idx];
        }
        return distance;
    };

    auto fallbackHotspotFromFeature = [](const SoundFeature& feature)
    {
        const float oscFreq = feature.values[1];
        const float oscAtk = feature.values[2];
        const float oscDcy = feature.values[3];
        const float modAmt = feature.values[6];
        const float noiseFilter = feature.values[8];
        const float noiseQ = feature.values[9];
        const float noiseAtk = feature.values[12];
        const float noiseDcy = feature.values[13];
        const float noiseMix = feature.values[14];
        const float dist = feature.values[15];

        const float lowTone = juce::jlimit(
            0.0f, 1.0f, ((1.0f - oscFreq) * 0.60f) + ((1.0f - noiseFilter) * 0.40f));
        const float highTone = juce::jlimit(
            0.0f, 1.0f, (oscFreq * 0.60f) + (noiseFilter * 0.40f));
        const float toneSpread = juce::jlimit(0.0f, 1.0f, std::abs(highTone - lowTone));
        const float transient = juce::jlimit(
            0.0f,
            1.0f,
            ((1.0f - oscAtk) * 0.62f) + ((1.0f - noiseAtk) * 0.38f));
        const float tail = juce::jlimit(
            0.0f,
            1.0f,
            (oscDcy * 0.55f) + (noiseDcy * 0.45f));
        const float texture = juce::jlimit(
            0.0f,
            1.0f,
            (noiseMix * 0.58f) + (modAmt * 0.26f) + (noiseQ * 0.16f));
        const float rawHotspot = juce::jlimit(
            0.0f,
            1.0f,
            0.12f
                + (0.26f * transient)
                + (0.18f * tail)
                + (0.18f * texture)
                + (0.12f * toneSpread)
                + (0.14f * dist));
        return rawHotspot * rawHotspot;
    };

    auto mapHotspotAtCell = [&](int x, int y)
    {
        if (!hasMapImage)
            return 0.0f;
        const int imgX = mapXLookup[static_cast<size_t>(juce::jlimit(0, BeatSpaceTableSize - 1, x))];
        const int imgY = mapYLookup[static_cast<size_t>(juce::jlimit(0, BeatSpaceTableSize - 1, y))];
        const auto pixel = mapImage.getPixelAt(imgX, imgY);
        const float hotspot = juce::jlimit(0.0f, 1.0f, (pixel.getBrightness() - 0.20f) / 0.80f);
        return hotspot * hotspot;
    };

    std::vector<int> sampleIndices;
    constexpr int kTargetSampleCells = 24000;
    const int targetSamples = juce::jmin(tableCells, kTargetSampleCells);
    const int sampleSide = juce::jmax(
        1,
        static_cast<int>(std::floor(std::sqrt(static_cast<double>(targetSamples)))));
    const int sampleDenom = juce::jmax(1, sampleSide - 1);
    sampleIndices.reserve(static_cast<size_t>(targetSamples));

    for (int sy = 0; sy < sampleSide; ++sy)
    {
        const int y = juce::jlimit(
            0,
            BeatSpaceTableSize - 1,
            static_cast<int>(std::round((static_cast<double>(sy) / static_cast<double>(sampleDenom))
                                        * static_cast<double>(BeatSpaceTableSize - 1))));
        for (int sx = 0; sx < sampleSide; ++sx)
        {
            const int x = juce::jlimit(
                0,
                BeatSpaceTableSize - 1,
                static_cast<int>(std::round((static_cast<double>(sx) / static_cast<double>(sampleDenom))
                                            * static_cast<double>(BeatSpaceTableSize - 1))));
            const int idx = (y * BeatSpaceTableSize) + x;
            if (sampleIndices.empty() || sampleIndices.back() != idx)
                sampleIndices.push_back(idx);
        }
    }
    if (sampleIndices.empty())
        sampleIndices.push_back(0);

    if (static_cast<int>(sampleIndices.size()) > targetSamples)
    {
        std::vector<int> reduced;
        reduced.reserve(static_cast<size_t>(targetSamples));
        const int denom = juce::jmax(1, targetSamples - 1);
        for (int i = 0; i < targetSamples; ++i)
        {
            const size_t pick = static_cast<size_t>(
                std::round((static_cast<double>(i) / static_cast<double>(denom))
                           * static_cast<double>(sampleIndices.size() - 1)));
            reduced.push_back(sampleIndices[pick]);
        }
        sampleIndices.swap(reduced);
    }

    std::vector<SoundFeature> sampleFeatures;
    sampleFeatures.reserve(sampleIndices.size());
    for (const int sampleIndex : sampleIndices)
    {
        const int clampedIndex = juce::jlimit(0, tableCells - 1, sampleIndex);
        const int sampleY = clampedIndex / BeatSpaceTableSize;
        const int sampleX = clampedIndex - (sampleY * BeatSpaceTableSize);
        sampleFeatures.push_back(buildSoundFeature(
            beatSpaceTable[static_cast<size_t>(clampedIndex)].values,
            sampleX,
            sampleY));
    }
    if (sampleFeatures.empty())
        return;

    std::array<double, kFeatureDims> meanAccum{};
    meanAccum.fill(0.0);
    for (const auto& feature : sampleFeatures)
    {
        for (int d = 0; d < kFeatureDims; ++d)
            meanAccum[static_cast<size_t>(d)] += static_cast<double>(feature.values[static_cast<size_t>(d)]);
    }

    std::array<float, kFeatureDims> featureMean{};
    const double invSampleCount = 1.0 / static_cast<double>(sampleFeatures.size());
    for (int d = 0; d < kFeatureDims; ++d)
        featureMean[static_cast<size_t>(d)] = static_cast<float>(meanAccum[static_cast<size_t>(d)] * invSampleCount);

    std::array<double, kFeatureDims> varAccum{};
    varAccum.fill(0.0);
    for (const auto& feature : sampleFeatures)
    {
        for (int d = 0; d < kFeatureDims; ++d)
        {
            const double delta = static_cast<double>(feature.values[static_cast<size_t>(d)] - featureMean[static_cast<size_t>(d)]);
            varAccum[static_cast<size_t>(d)] += delta * delta;
        }
    }

    for (int d = 0; d < kFeatureDims; ++d)
    {
        const double variance = varAccum[static_cast<size_t>(d)] * invSampleCount;
        const float stddev = static_cast<float>(std::sqrt(juce::jmax(variance, 1.0e-8)));
        featureInvStd[static_cast<size_t>(d)] = (stddev > 1.0e-5f) ? (1.0f / stddev) : 1.0f;
    }

    constexpr int kClusters = BeatSpaceChannels;
    std::array<SoundFeature, kClusters> centers{};
    centers[0] = sampleFeatures.front();

    for (int c = 1; c < kClusters; ++c)
    {
        float bestDistance = -1.0f;
        size_t bestIndex = 0;
        for (size_t i = 0; i < sampleFeatures.size(); ++i)
        {
            float minDistance = std::numeric_limits<float>::max();
            for (int prev = 0; prev < c; ++prev)
            {
                minDistance = juce::jmin(
                    minDistance,
                    squaredDistance(sampleFeatures[i], centers[static_cast<size_t>(prev)]));
            }
            if (minDistance > bestDistance)
            {
                bestDistance = minDistance;
                bestIndex = i;
            }
        }
        centers[static_cast<size_t>(c)] = sampleFeatures[bestIndex];
    }

    std::vector<int> sampleAssignments(sampleFeatures.size(), -1);
    for (int iter = 0; iter < 18; ++iter)
    {
        bool anyChange = false;
        std::array<SoundFeature, kClusters> sums{};
        std::array<int, kClusters> counts{};
        counts.fill(0);

        for (size_t i = 0; i < sampleFeatures.size(); ++i)
        {
            const auto& feature = sampleFeatures[i];
            float bestDistance = std::numeric_limits<float>::max();
            int bestCluster = 0;
            for (int c = 0; c < kClusters; ++c)
            {
                const float d = squaredDistance(feature, centers[static_cast<size_t>(c)]);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    bestCluster = c;
                }
            }

            if (sampleAssignments[i] != bestCluster)
            {
                sampleAssignments[i] = bestCluster;
                anyChange = true;
            }

            auto& sum = sums[static_cast<size_t>(bestCluster)];
            for (int d = 0; d < kFeatureDims; ++d)
                sum.values[static_cast<size_t>(d)] += feature.values[static_cast<size_t>(d)];
            ++counts[static_cast<size_t>(bestCluster)];
        }

        for (int c = 0; c < kClusters; ++c)
        {
            const int count = counts[static_cast<size_t>(c)];
            if (count > 0)
            {
                const float inv = 1.0f / static_cast<float>(count);
                auto& center = centers[static_cast<size_t>(c)];
                for (int d = 0; d < kFeatureDims; ++d)
                    center.values[static_cast<size_t>(d)] = sums[static_cast<size_t>(c)].values[static_cast<size_t>(d)] * inv;
                continue;
            }

            float farthestDistance = -1.0f;
            size_t farthestIndex = 0;
            for (size_t i = 0; i < sampleFeatures.size(); ++i)
            {
                float nearestDistance = std::numeric_limits<float>::max();
                for (int other = 0; other < kClusters; ++other)
                {
                    if (other == c || counts[static_cast<size_t>(other)] <= 0)
                        continue;
                    nearestDistance = juce::jmin(
                        nearestDistance,
                        squaredDistance(sampleFeatures[i], centers[static_cast<size_t>(other)]));
                }
                if (!std::isfinite(nearestDistance))
                    nearestDistance = 0.0f;
                if (nearestDistance > farthestDistance)
                {
                    farthestDistance = nearestDistance;
                    farthestIndex = i;
                }
            }
            centers[static_cast<size_t>(c)] = sampleFeatures[farthestIndex];
        }

        if (!anyChange && iter > 0)
            break;
    }

    std::array<std::array<float, BeatSpaceVectorSize>, kClusters> clusterMeans{};
    std::array<float, kClusters> clusterHotspotSum{};
    std::array<int, kClusters> clusterCounts{};
    clusterHotspotSum.fill(0.0f);
    clusterCounts.fill(0);

    for (int y = 0; y < BeatSpaceTableSize; ++y)
    {
        for (int x = 0; x < BeatSpaceTableSize; ++x)
        {
            const int idx = (y * BeatSpaceTableSize) + x;
            const auto& values = beatSpaceTable[static_cast<size_t>(idx)].values;
            const auto feature = buildSoundFeature(values, x, y);

            float bestDistance = std::numeric_limits<float>::max();
            int bestCluster = 0;
            for (int c = 0; c < kClusters; ++c)
            {
                const float d = squaredDistance(feature, centers[static_cast<size_t>(c)]);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    bestCluster = c;
                }
            }

            const float featureHotspot = fallbackHotspotFromFeature(feature);
            const float hotspot = hasMapImage
                ? juce::jlimit(0.0f, 1.0f, (0.78f * mapHotspotAtCell(x, y)) + (0.22f * featureHotspot))
                : featureHotspot;

            beatSpaceColorClusters[static_cast<size_t>(idx)] = bestCluster;
            beatSpaceHotspotWeights[static_cast<size_t>(idx)] = hotspot;
            ++clusterCounts[static_cast<size_t>(bestCluster)];
            clusterHotspotSum[static_cast<size_t>(bestCluster)] += hotspot;

            auto& mean = clusterMeans[static_cast<size_t>(bestCluster)];
            for (int i = 0; i < BeatSpaceVectorSize; ++i)
                mean[static_cast<size_t>(i)] += values[static_cast<size_t>(i)];
        }
    }

    for (int c = 0; c < kClusters; ++c)
    {
        const int count = clusterCounts[static_cast<size_t>(c)];
        if (count <= 0)
            continue;
        const float inv = 1.0f / static_cast<float>(count);
        auto& mean = clusterMeans[static_cast<size_t>(c)];
        for (auto& v : mean)
            v *= inv;
        clusterHotspotSum[static_cast<size_t>(c)] *= inv;
    }

    std::array<bool, kClusters> clusterUsed{};
    clusterUsed.fill(false);
    for (int category = 0; category < BeatSpaceChannels; ++category)
    {
        float bestScore = -1.0e9f;
        int bestCluster = -1;
        for (int c = 0; c < kClusters; ++c)
        {
            if (clusterUsed[static_cast<size_t>(c)] || clusterCounts[static_cast<size_t>(c)] <= 0)
                continue;

            float score = scoreBeatSpaceCategoryPoint(category, clusterMeans[static_cast<size_t>(c)]);
            // Hotspot should only be a mild tie-breaker; otherwise bright map
            // regions dominate and categories collapse toward the center.
            score += 0.06f * clusterHotspotSum[static_cast<size_t>(c)];
            if (score > bestScore)
            {
                bestScore = score;
                bestCluster = c;
            }
        }

        if (bestCluster < 0)
        {
            for (int c = 0; c < kClusters; ++c)
            {
                if (!clusterUsed[static_cast<size_t>(c)] && clusterCounts[static_cast<size_t>(c)] > 0)
                {
                    bestCluster = c;
                    break;
                }
            }
        }

        if (bestCluster >= 0)
        {
            beatSpaceCategoryColorCluster[static_cast<size_t>(category)] = bestCluster;
            clusterUsed[static_cast<size_t>(bestCluster)] = true;
        }
    }

    beatSpaceColorClustersReady = true;
}

void StepVstHostAudioProcessor::rebuildBeatSpaceCategoryAnchors()
{
    beatSpaceCategoryAnchorsReady = false;
    beatSpaceMacroTraitAnchorsReady = false;
    if (beatSpaceTable.empty())
        return;

    constexpr int kMinSpacing = 8;
    static constexpr std::array<int, BeatSpaceChannels> kDefaultRegionRx { 10, 10, 9, 9, 12, 12 };
    static constexpr std::array<int, BeatSpaceChannels> kDefaultRegionRy { 10, 10, 9, 9, 11, 11 };
    std::array<juce::Point<int>, BeatSpaceChannels> selectedAnchors{};
    std::array<bool, BeatSpaceChannels> selectedValid{};
    selectedValid.fill(false);
    auto clampPoint = [](const juce::Point<int>& p)
    {
        return juce::Point<int> {
            juce::jlimit(0, BeatSpaceTableSize - 1, p.x),
            juce::jlimit(0, BeatSpaceTableSize - 1, p.y)
        };
    };

    for (int category = 0; category < BeatSpaceChannels; ++category)
    {
        if (beatSpaceCategoryAnchorManual[static_cast<size_t>(category)])
        {
            const auto manualPoint = clampPoint(beatSpaceCategoryManualAnchors[static_cast<size_t>(category)]);
            beatSpaceCategoryManualAnchors[static_cast<size_t>(category)] = manualPoint;
            selectedAnchors[static_cast<size_t>(category)] = manualPoint;
            selectedValid[static_cast<size_t>(category)] = true;

            if (beatSpaceColorClustersReady)
            {
                const int tableIndex = (manualPoint.y * BeatSpaceTableSize) + manualPoint.x;
                if (tableIndex >= 0 && tableIndex < static_cast<int>(beatSpaceColorClusters.size()))
                    beatSpaceCategoryColorCluster[static_cast<size_t>(category)] =
                        beatSpaceColorClusters[static_cast<size_t>(tableIndex)];
            }
            continue;
        }

        const int preferredCluster = beatSpaceColorClustersReady
            ? beatSpaceCategoryColorCluster[static_cast<size_t>(category)]
            : -1;

        float bestScore = -1.0e9f;
        juce::Point<int> bestPoint { BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 };
        int consideredPoints = 0;

        auto scanPoints = [&](bool clusterOnly)
        {
            for (int y = 0; y < BeatSpaceTableSize; ++y)
            {
                for (int x = 0; x < BeatSpaceTableSize; ++x)
                {
                    const int tableIndex = (y * BeatSpaceTableSize) + x;
                    if (clusterOnly && preferredCluster >= 0)
                    {
                        if (tableIndex < 0
                            || tableIndex >= static_cast<int>(beatSpaceColorClusters.size())
                            || beatSpaceColorClusters[static_cast<size_t>(tableIndex)] != preferredCluster)
                            continue;
                    }

                    float score = scoreBeatSpaceCategoryPoint(
                        category,
                        beatSpaceTable[static_cast<size_t>(tableIndex)].values);

                    if (tableIndex >= 0 && tableIndex < static_cast<int>(beatSpaceHotspotWeights.size()))
                        score += 0.05f * beatSpaceHotspotWeights[static_cast<size_t>(tableIndex)];

                    // Manual tags guide the analysis: same-category proximity is rewarded,
                    // cross-category proximity is penalized to keep regions separated.
                    for (int manualCategory = 0; manualCategory < BeatSpaceChannels; ++manualCategory)
                    {
                        if (!beatSpaceCategoryAnchorManual[static_cast<size_t>(manualCategory)])
                            continue;

                        const auto tagPoint = clampPoint(
                            beatSpaceCategoryManualAnchors[static_cast<size_t>(manualCategory)]);
                        const float dxTag = static_cast<float>(x - tagPoint.x);
                        const float dyTag = static_cast<float>(y - tagPoint.y);
                        const float distTag = std::sqrt((dxTag * dxTag) + (dyTag * dyTag));
                        const float proximity = juce::jlimit(0.0f, 1.0f, 1.0f - (distTag / 24.0f));
                        if (manualCategory == category)
                            score += 0.22f * proximity;
                        else
                            score -= 0.07f * proximity;
                    }

                    for (int prev = 0; prev < category; ++prev)
                    {
                        if (!selectedValid[static_cast<size_t>(prev)])
                            continue;
                        const auto p = selectedAnchors[static_cast<size_t>(prev)];
                        const float dx = static_cast<float>(x - p.x);
                        const float dy = static_cast<float>(y - p.y);
                        const float spacingDist = std::sqrt((dx * dx) + (dy * dy));
                        if (spacingDist < static_cast<float>(kMinSpacing))
                            score -= (static_cast<float>(kMinSpacing) - spacingDist) * 0.12f;
                    }

                    ++consideredPoints;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestPoint = { x, y };
                    }
                }
            }
        };

        scanPoints(preferredCluster >= 0);
        if (consideredPoints == 0 && preferredCluster >= 0)
            scanPoints(false);

        selectedAnchors[static_cast<size_t>(category)] = bestPoint;
        selectedValid[static_cast<size_t>(category)] = (consideredPoints > 0);
    }

    for (int i = 0; i < BeatSpaceChannels; ++i)
        beatSpaceCategoryAnchors[static_cast<size_t>(i)] = selectedAnchors[static_cast<size_t>(i)];

    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        beatSpaceCategoryRegionRadiusX[static_cast<size_t>(i)] = kDefaultRegionRx[static_cast<size_t>(i)];
        beatSpaceCategoryRegionRadiusY[static_cast<size_t>(i)] = kDefaultRegionRy[static_cast<size_t>(i)];
    }

    if (beatSpaceColorClustersReady)
    {
        for (int category = 0; category < BeatSpaceChannels; ++category)
        {
            int cluster = beatSpaceCategoryColorCluster[static_cast<size_t>(category)];
            if (cluster < 0 && beatSpaceCategoryAnchorManual[static_cast<size_t>(category)])
            {
                const auto p = clampPoint(beatSpaceCategoryManualAnchors[static_cast<size_t>(category)]);
                const int tableIndex = (p.y * BeatSpaceTableSize) + p.x;
                if (tableIndex >= 0 && tableIndex < static_cast<int>(beatSpaceColorClusters.size()))
                    cluster = beatSpaceColorClusters[static_cast<size_t>(tableIndex)];
                beatSpaceCategoryColorCluster[static_cast<size_t>(category)] = cluster;
            }

            if (cluster < 0)
                continue;

            int minX = BeatSpaceTableSize - 1;
            int maxX = 0;
            int minY = BeatSpaceTableSize - 1;
            int maxY = 0;
            int count = 0;

            for (int y = 0; y < BeatSpaceTableSize; ++y)
            {
                for (int x = 0; x < BeatSpaceTableSize; ++x)
                {
                    const int tableIndex = (y * BeatSpaceTableSize) + x;
                    if (tableIndex < 0 || tableIndex >= static_cast<int>(beatSpaceColorClusters.size()))
                        continue;
                    if (beatSpaceColorClusters[static_cast<size_t>(tableIndex)] != cluster)
                        continue;

                    minX = juce::jmin(minX, x);
                    maxX = juce::jmax(maxX, x);
                    minY = juce::jmin(minY, y);
                    maxY = juce::jmax(maxY, y);
                    ++count;
                }
            }

            if (count > 0)
            {
                const int spanX = juce::jmax(1, maxX - minX + 1);
                const int spanY = juce::jmax(1, maxY - minY + 1);
                beatSpaceCategoryRegionRadiusX[static_cast<size_t>(category)] =
                    juce::jlimit(5, 20, static_cast<int>(std::round(static_cast<float>(spanX) * 0.30f)));
                beatSpaceCategoryRegionRadiusY[static_cast<size_t>(category)] =
                    juce::jlimit(5, 20, static_cast<int>(std::round(static_cast<float>(spanY) * 0.30f)));
            }
        }
    }

    beatSpaceCategoryAnchorsReady = true;
    rebuildBeatSpaceCategoryPresetPoints();
}

void StepVstHostAudioProcessor::rebuildBeatSpaceCategoryPresetPoints()
{
    beatSpaceMacroTraitAnchorsReady = false;
    for (auto& ready : beatSpaceCategoryPresetPointsReady)
        ready = false;

    if (beatSpaceTable.empty())
        return;

    auto clampPoint = [](const juce::Point<int>& p)
    {
        return juce::Point<int> {
            juce::jlimit(0, BeatSpaceTableSize - 1, p.x),
            juce::jlimit(0, BeatSpaceTableSize - 1, p.y)
        };
    };

    struct CandidatePoint
    {
        juce::Point<int> point;
        float score = 0.0f;
        bool strict = false;
    };

    constexpr int kTopCandidates = 2048;
    constexpr int kMinStrictCandidates = BeatSpacePresetSlotsPerSpace / 2;

    for (int category = 0; category < BeatSpaceChannels; ++category)
    {
        const auto catIdx = static_cast<size_t>(category);
        const auto anchor = clampPoint(beatSpaceCategoryAnchors[catIdx]);
        const int rx = juce::jmax(1, beatSpaceCategoryRegionRadiusX[catIdx]);
        const int ry = juce::jmax(1, beatSpaceCategoryRegionRadiusY[catIdx]);
        const int preferredCluster = beatSpaceColorClustersReady ? beatSpaceCategoryColorCluster[catIdx] : -1;

        std::vector<CandidatePoint> candidates;
        candidates.reserve(static_cast<size_t>(BeatSpaceTableSize * BeatSpaceTableSize));

        for (int y = 0; y < BeatSpaceTableSize; ++y)
        {
            for (int x = 0; x < BeatSpaceTableSize; ++x)
            {
                const int tableIndex = (y * BeatSpaceTableSize) + x;
                if (tableIndex < 0 || tableIndex >= static_cast<int>(beatSpaceTable.size()))
                    continue;

                const auto& cellValues = beatSpaceTable[static_cast<size_t>(tableIndex)].values;
                const float ownCategoryScore = scoreBeatSpaceCategoryPoint(category, cellValues);
                float bestOtherCategoryScore = -1.0e9f;
                for (int other = 0; other < BeatSpaceChannels; ++other)
                {
                    if (other == category)
                        continue;
                    bestOtherCategoryScore = juce::jmax(
                        bestOtherCategoryScore,
                        scoreBeatSpaceCategoryPoint(other, cellValues));
                }
                const float margin = ownCategoryScore - bestOtherCategoryScore;
                const float confidence = juce::jlimit(0.0f, 1.0f, (margin + 0.22f) / 0.66f);
                const float categoryPurity = juce::jlimit(0.0f, 1.0f, (margin + 0.05f) / 0.35f);
                float score = ownCategoryScore + (0.26f * confidence) + (0.38f * categoryPurity);
                if (margin < -0.06f)
                    score -= juce::jlimit(0.20f, 0.55f, 0.25f + (std::abs(margin) * 0.90f));
                else if (margin > 0.08f)
                    score += juce::jlimit(0.0f, 0.22f, margin * 0.45f);

                if (tableIndex < static_cast<int>(beatSpaceHotspotWeights.size()))
                    score += 0.24f * beatSpaceHotspotWeights[static_cast<size_t>(tableIndex)];

                bool clusterMatch = true;
                if (preferredCluster >= 0
                    && tableIndex < static_cast<int>(beatSpaceColorClusters.size()))
                {
                    if (beatSpaceColorClusters[static_cast<size_t>(tableIndex)] == preferredCluster)
                        score += 0.16f;
                    else
                    {
                        score -= 0.14f;
                        clusterMatch = false;
                    }
                }

                const float dxNorm = std::abs(static_cast<float>(x - anchor.x))
                    / static_cast<float>(juce::jmax(1, rx));
                const float dyNorm = std::abs(static_cast<float>(y - anchor.y))
                    / static_cast<float>(juce::jmax(1, ry));
                const float regionDist = std::sqrt((dxNorm * dxNorm) + (dyNorm * dyNorm));
                score += juce::jlimit(-0.28f, 0.26f, 0.24f - (0.14f * regionDist));

                for (int manualCategory = 0; manualCategory < BeatSpaceChannels; ++manualCategory)
                {
                    const auto manualIdx = static_cast<size_t>(manualCategory);
                    const int manualCount = juce::jlimit(
                        0,
                        BeatSpacePresetSlotsPerSpace,
                        beatSpaceCategoryManualTagCounts[manualIdx]);
                    if (manualCount <= 0)
                        continue;

                    float strongestProximity = 0.0f;
                    for (int tagIndex = 0; tagIndex < manualCount; ++tagIndex)
                    {
                        const auto tagPoint = clampPoint(
                            beatSpaceCategoryManualTagPoints[manualIdx][static_cast<size_t>(tagIndex)]);
                        const float tagDx = static_cast<float>(x - tagPoint.x);
                        const float tagDy = static_cast<float>(y - tagPoint.y);
                        const float tagDist = std::sqrt((tagDx * tagDx) + (tagDy * tagDy));
                        strongestProximity = juce::jmax(
                            strongestProximity,
                            juce::jlimit(0.0f, 1.0f, 1.0f - (tagDist / 20.0f)));
                    }
                    if (manualCategory == category)
                        score += 0.26f * strongestProximity;
                    else
                        score -= 0.10f * strongestProximity;
                }

                const bool insideRegion = (x >= (anchor.x - rx))
                    && (x <= (anchor.x + rx))
                    && (y >= (anchor.y - ry))
                    && (y <= (anchor.y + ry));
                if (!insideRegion)
                    score -= 0.14f;

                const bool strictCandidate = (margin >= 0.04f)
                    && (confidence >= 0.42f)
                    && insideRegion
                    && clusterMatch;

                candidates.push_back({ { x, y }, score, strictCandidate });
            }
        }

        if (candidates.empty())
        {
            beatSpaceCategoryPresetPoints[catIdx].fill(anchor);
            continue;
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const CandidatePoint& a, const CandidatePoint& b)
            {
                if (std::abs(a.score - b.score) > 1.0e-6f)
                    return a.score > b.score;
                if (a.point.y == b.point.y)
                    return a.point.x < b.point.x;
                return a.point.y < b.point.y;
            });

        if (candidates.size() > static_cast<size_t>(kTopCandidates))
            candidates.resize(static_cast<size_t>(kTopCandidates));

        std::vector<CandidatePoint> strictCandidates;
        strictCandidates.reserve(candidates.size());
        for (const auto& candidate : candidates)
        {
            if (candidate.strict)
                strictCandidates.push_back(candidate);
        }

        std::vector<CandidatePoint> selected;
        selected.reserve(BeatSpacePresetSlotsPerSpace);
        const int manualCount = juce::jlimit(
            0,
            BeatSpacePresetSlotsPerSpace,
            beatSpaceCategoryManualTagCounts[catIdx]);
        if (manualCount > 0)
        {
            const auto& manualTags = beatSpaceCategoryManualTagPoints[catIdx];
            for (int i = 0; i < manualCount; ++i)
            {
                const auto p = clampPoint(manualTags[static_cast<size_t>(i)]);
                bool duplicate = false;
                for (const auto& s : selected)
                {
                    if (s.point == p)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate)
                    continue;
                selected.push_back({ p, 2.5f + static_cast<float>(manualCount - i) * 0.05f });
                if (selected.size() >= static_cast<size_t>(BeatSpacePresetSlotsPerSpace))
                    break;
            }
        }

        if (selected.empty())
        {
            if (strictCandidates.size() >= static_cast<size_t>(kMinStrictCandidates))
                selected.push_back(strictCandidates.front());
            else
                selected.push_back(candidates.front());
        }

        auto appendDiverseSelections = [&](const std::vector<CandidatePoint>& pool)
        {
            while (selected.size() < static_cast<size_t>(BeatSpacePresetSlotsPerSpace)
                   && selected.size() < pool.size())
            {
                float bestPickScore = -1.0e9f;
                int bestIndex = -1;

                for (size_t i = 0; i < pool.size(); ++i)
                {
                    const auto& cand = pool[i];
                    bool alreadySelected = false;
                    for (const auto& s : selected)
                    {
                        if (s.point == cand.point)
                        {
                            alreadySelected = true;
                            break;
                        }
                    }
                    if (alreadySelected)
                        continue;

                    float minDistance = std::numeric_limits<float>::max();
                    for (const auto& s : selected)
                    {
                        const float dx = static_cast<float>(cand.point.x - s.point.x);
                        const float dy = static_cast<float>(cand.point.y - s.point.y);
                        minDistance = juce::jmin(minDistance, std::sqrt((dx * dx) + (dy * dy)));
                    }
                    if (!std::isfinite(minDistance))
                        minDistance = 0.0f;

                    const float diversity = juce::jlimit(0.0f, 1.0f, minDistance / 24.0f);
                    const float pickScore = cand.score + (0.28f * diversity);
                    if (pickScore > bestPickScore)
                    {
                        bestPickScore = pickScore;
                        bestIndex = static_cast<int>(i);
                    }
                }

                if (bestIndex < 0)
                    break;
                selected.push_back(pool[static_cast<size_t>(bestIndex)]);
            }
        };

        if (strictCandidates.size() >= static_cast<size_t>(kMinStrictCandidates))
            appendDiverseSelections(strictCandidates);
        appendDiverseSelections(candidates);

        if (selected.empty())
            selected.push_back({ anchor, 0.0f, false });

        auto& presetPoints = beatSpaceCategoryPresetPoints[catIdx];
        auto& presetLabels = beatSpaceCategoryPresetLabels[catIdx];
        for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
        {
            const auto sourceIndex = static_cast<size_t>(
                juce::jlimit(0, static_cast<int>(selected.size()) - 1, slot));
            const auto clampedPoint = clampPoint(selected[sourceIndex].point);
            presetPoints[static_cast<size_t>(slot)] = clampedPoint;

            if (presetLabels[static_cast<size_t>(slot)].trim().isNotEmpty())
                continue;

            const int tableIndex = (clampedPoint.y * BeatSpaceTableSize) + clampedPoint.x;
            if (tableIndex < 0 || tableIndex >= static_cast<int>(beatSpaceTable.size()))
                continue;

            presetLabels[static_cast<size_t>(slot)] = describeBeatSpaceCategoryPresetPoint(
                category,
                beatSpaceTable[static_cast<size_t>(tableIndex)].values);
        }
        beatSpaceCategoryPresetPointsReady[catIdx] = true;
    }
}

bool StepVstHostAudioProcessor::initializeBeatSpaceTable()
{
    beatSpaceTable.clear();
    beatSpaceTablePreviewImage = {};
    beatSpaceCategoryAnchorsReady = false;
    beatSpaceColorClustersReady = false;
    beatSpaceColorClusters.clear();
    beatSpaceHotspotWeights.clear();
    beatSpaceCategoryColorCluster.fill(-1);
    beatSpaceCategoryManualTagCounts.fill(0);
    for (auto& tags : beatSpaceCategoryManualTagPoints)
        tags.fill({ BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 });
    beatSpaceCategoryAnchorManual.fill(false);
    beatSpaceCategoryPresetPointsReady.fill(false);
    beatSpaceMacroTraitAnchorsReady = false;
    beatSpaceLinkedOffsetsReady = false;
    for (auto& offset : beatSpaceLinkedOffsets)
        offset = { 0.0f, 0.0f };
    beatSpaceDecoderReady = false;

    const auto cacheFile = getBeatSpaceTableCacheFile();
    const auto scriptDir = getBeatSpaceScriptDirectory();
    const auto decoderFile = scriptDir.getChildFile("marigoldG_decoder.nuxnn.base64");
    juce::String cachedDecoderName;

    if (!decoderFile.existsAsFile())
    {
        if (loadBeatSpaceTableCache<BeatSpaceCell, BeatSpaceTableSize, BeatSpaceVectorSize>(
                cacheFile,
                {},
                -1,
                -1,
                beatSpaceTable,
                cachedDecoderName))
        {
            beatSpaceDecoderName = cachedDecoderName.isNotEmpty() ? cachedDecoderName : "marigoldG";
            beatSpaceDecoderReady = true;
            const int totalCells = BeatSpaceTableSize * BeatSpaceTableSize;
            beatSpaceStatusMessage = "BeatSpace decoder missing; using local cache ("
                + juce::String(totalCells) + " x 73)";
            return true;
        }

        beatSpaceStatusMessage = "BeatSpace decoder not found (no local cache)";
        return false;
    }

    const auto decoderSourceSize = decoderFile.getSize();
    const auto decoderSourceModifiedMs = decoderFile.getLastModificationTime().toMilliseconds();
    const auto base64Text = decoderFile.loadFileAsString();
    if (base64Text.isEmpty())
    {
        beatSpaceStatusMessage = "BeatSpace decoder empty";
        return false;
    }

    juce::MemoryOutputStream decodedStream;
    if (!juce::Base64::convertFromBase64(decodedStream, base64Text))
    {
        beatSpaceStatusMessage = "BeatSpace decoder base64 parse failed";
        return false;
    }

    const auto decodedBlock = decodedStream.getMemoryBlock();
    if (decodedBlock.getSize() == 0)
    {
        beatSpaceStatusMessage = "BeatSpace decoder payload empty";
        return false;
    }

    const auto decoderHash = hashMemoryBlockFNV1a64(decodedBlock);
    if (loadBeatSpaceTableCache<BeatSpaceCell, BeatSpaceTableSize, BeatSpaceVectorSize>(
            cacheFile,
            decoderHash,
            decoderSourceSize,
            decoderSourceModifiedMs,
            beatSpaceTable,
            cachedDecoderName))
    {
        beatSpaceDecoderName = cachedDecoderName.isNotEmpty() ? cachedDecoderName : "marigoldG";
        beatSpaceDecoderReady = true;
        const int totalCells = BeatSpaceTableSize * BeatSpaceTableSize;
        beatSpaceStatusMessage = "BeatSpace table loaded from validated local cache ("
            + juce::String(totalCells) + " x 73)";
        return true;
    }

    std::vector<uint8_t> bytes(decodedBlock.getSize());
    std::memcpy(bytes.data(), decodedBlock.getData(), decodedBlock.getSize());
    const auto model = parseNuXnnModel(bytes);
    if (!model.valid || model.inputSize < 2 || model.outputSize < BeatSpaceVectorSize)
    {
        beatSpaceStatusMessage = "BeatSpace decoder format unsupported";
        return false;
    }

    beatSpaceTable.resize(static_cast<size_t>(BeatSpaceTableSize * BeatSpaceTableSize));
    for (int y = 0; y < BeatSpaceTableSize; ++y)
    {
        for (int x = 0; x < BeatSpaceTableSize; ++x)
        {
            const float tx = static_cast<float>(x) / static_cast<float>(juce::jmax(1, BeatSpaceTableSize - 1));
            const float ty = static_cast<float>(y) / static_cast<float>(juce::jmax(1, BeatSpaceTableSize - 1));
            const std::vector<float> latentIn {
                (tx * 2.0f) - 1.0f,
                (ty * 2.0f) - 1.0f
            };
            auto raw = model.rootEval(latentIn);
            beatSpaceTable[static_cast<size_t>(y * BeatSpaceTableSize + x)].values =
                postProcessBeatSpaceVector(raw);
        }
    }

    beatSpaceDecoderName = model.name.isNotEmpty() ? model.name : "marigoldG";
    saveBeatSpaceTableCache<BeatSpaceCell, BeatSpaceTableSize, BeatSpaceVectorSize>(
        cacheFile,
        decoderHash,
        beatSpaceDecoderName,
        decoderSourceSize,
        decoderSourceModifiedMs,
        beatSpaceTable);
    beatSpaceDecoderReady = true;
    const int totalCells = BeatSpaceTableSize * BeatSpaceTableSize;
    beatSpaceStatusMessage = "BeatSpace table loaded ("
        + juce::String(totalCells) + " x 73)";
    return true;
}

void StepVstHostAudioProcessor::clearHostedDirectParameterMap()
{
    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        hostedDirectParamVolume[idx] = -1;
        hostedDirectParamPan[idx] = -1;
        hostedDirectParamPitch[idx] = -1;
        hostedDirectParamAttack[idx] = -1;
        hostedDirectParamDecay[idx] = -1;
        hostedDirectParamRelease[idx] = -1;
        hostedDirectParamAttackAux[idx] = -1;
        hostedLastDirectParamVolume[idx] = -1.0f;
        hostedLastDirectParamPan[idx] = -1.0f;
        hostedLastDirectParamPitch[idx] = -1.0f;
        hostedLastDirectParamAttack[idx] = -1.0f;
        hostedLastDirectParamDecay[idx] = -1.0f;
        hostedLastDirectParamRelease[idx] = -1.0f;
        hostedLastDirectParamAttackAux[idx] = -1.0f;
        hostedStripOutputMutedLast[idx] = false;
    }
}

void StepVstHostAudioProcessor::refreshHostedDirectParameterMapFromBeatSpace(bool likelyMicrotonic)
{
    clearHostedDirectParameterMap();
    if (!likelyMicrotonic)
        return;

    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto idx = static_cast<size_t>(channel);
        hostedDirectParamVolume[idx] = beatSpaceParamMap[idx][static_cast<size_t>(kMicrotonicPatchLevelIndex)];
        hostedDirectParamPan[idx] = beatSpaceParamMap[idx][static_cast<size_t>(kMicrotonicPatchPanIndex)];
        hostedDirectParamPitch[idx] = beatSpaceParamMap[idx][static_cast<size_t>(kMicrotonicPatchOscFreqIndex)];
        hostedDirectParamAttack[idx] = beatSpaceParamMap[idx][static_cast<size_t>(kMicrotonicPatchOscAtkIndex)];
        hostedDirectParamDecay[idx] = beatSpaceParamMap[idx][static_cast<size_t>(kMicrotonicPatchOscDcyIndex)];
        hostedDirectParamRelease[idx] = beatSpaceParamMap[idx][static_cast<size_t>(kMicrotonicPatchNEnvDcyIndex)];
        hostedDirectParamAttackAux[idx] = beatSpaceParamMap[idx][static_cast<size_t>(kMicrotonicPatchNEnvAtkIndex)];
    }

    auto* instance = hostRack.getInstance();
    if (instance == nullptr)
        return;

    const auto& params = instance->getParameters();
    const int totalParams = static_cast<int>(params.size());
    if (totalParams <= 0)
        return;

    std::vector<juce::String> normalizedParamNames(static_cast<size_t>(totalParams));
    for (int paramIndex = 0; paramIndex < totalParams; ++paramIndex)
        normalizedParamNames[static_cast<size_t>(paramIndex)] =
            normalizeParameterToken(params[paramIndex]->getName(128));

    auto containsChannelNumber = [](const juce::String& text, int channelOneBased)
    {
        juce::String digits;
        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = text[i];
            if (juce::CharacterFunctions::isDigit(c))
            {
                digits += juce::String::charToString(c);
            }
            else if (digits.isNotEmpty())
            {
                if (digits.getIntValue() == channelOneBased)
                    return true;
                digits.clear();
            }
        }
        return digits.isNotEmpty() && digits.getIntValue() == channelOneBased;
    };

    auto channelMatches = [&containsChannelNumber](const juce::String& normalizedName, int channelOneBased)
    {
        const juce::String suffix = juce::String(channelOneBased);
        if (normalizedName.endsWith(suffix))
            return true;
        if (normalizedName.contains("ch" + suffix)
            || normalizedName.contains("channel" + suffix)
            || normalizedName.contains("drum" + suffix)
            || normalizedName.contains("voice" + suffix)
            || normalizedName.contains("track" + suffix)
            || normalizedName.contains("trk" + suffix)
            || normalizedName.contains("v" + suffix))
        {
            return true;
        }
        return containsChannelNumber(normalizedName, channelOneBased);
    };

    auto findFallbackParam = [&](int channelOneBased,
                                 std::initializer_list<const char*> strongNeedles,
                                 std::initializer_list<const char*> weakNeedles)
    {
        int bestIndex = -1;
        int bestScore = -1;
        for (int paramIndex = 0; paramIndex < totalParams; ++paramIndex)
        {
            const auto& name = normalizedParamNames[static_cast<size_t>(paramIndex)];
            if (name.isEmpty() || !channelMatches(name, channelOneBased))
                continue;

            int score = 0;
            for (auto* needle : strongNeedles)
            {
                if (needle != nullptr && juce::String(needle).isNotEmpty() && name.contains(needle))
                    score += 5;
            }
            for (auto* needle : weakNeedles)
            {
                if (needle != nullptr && juce::String(needle).isNotEmpty() && name.contains(needle))
                    score += 2;
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestIndex = paramIndex;
            }
        }
        return bestScore > 0 ? bestIndex : -1;
    };

    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto idx = static_cast<size_t>(channel);
        const int channelOneBased = channel + 1;

        if (hostedDirectParamVolume[idx] < 0)
            hostedDirectParamVolume[idx] = findFallbackParam(channelOneBased, { "level", "volume" }, { "vol", "amp", "gain" });

        if (hostedDirectParamPan[idx] < 0)
            hostedDirectParamPan[idx] = findFallbackParam(channelOneBased, { "pan" }, { "stereo", "balance" });

        if (hostedDirectParamPitch[idx] < 0)
            hostedDirectParamPitch[idx] = findFallbackParam(channelOneBased, { "oscfreq", "frequency" }, { "pitch", "tune", "freq" });

        if (hostedDirectParamAttack[idx] < 0)
            hostedDirectParamAttack[idx] = findFallbackParam(channelOneBased, { "oscatk", "oscattack" }, { "osc", "atk", "attack" });

        if (hostedDirectParamDecay[idx] < 0)
            hostedDirectParamDecay[idx] = findFallbackParam(channelOneBased, { "oscdcy", "oscdecay" }, { "osc", "dcy", "decay" });

        if (hostedDirectParamRelease[idx] < 0)
            hostedDirectParamRelease[idx] = findFallbackParam(channelOneBased, { "nenvdcy", "nenvdecay", "release" }, { "nenv", "dcy", "decay", "rel" });

        if (hostedDirectParamAttackAux[idx] < 0)
            hostedDirectParamAttackAux[idx] = findFallbackParam(channelOneBased, { "nenvatk", "nenvattack" }, { "nenv", "atk", "attack" });
    }
}

bool StepVstHostAudioProcessor::refreshBeatSpaceParameterMap(bool refreshHostedDirectMap)
{
    auto* instance = hostRack.getInstance();
    if (instance == nullptr)
    {
        clearHostedDirectParameterMap();
        beatSpaceMappingReady = false;
        beatSpaceMicrotonicExactMapping = false;
        for (int i = 0; i < MaxStrips; ++i)
        {
            beatSpaceChannelMappingReady[static_cast<size_t>(i)] = false;
            beatSpaceLastRecallMaxError[static_cast<size_t>(i)] = 0.0f;
        }
        return false;
    }

    const auto& params = instance->getParameters();
    const int totalParams = static_cast<int>(params.size());
    if (totalParams <= 0)
    {
        clearHostedDirectParameterMap();
        beatSpaceMappingReady = false;
        beatSpaceMicrotonicExactMapping = false;
        for (int i = 0; i < MaxStrips; ++i)
        {
            beatSpaceChannelMappingReady[static_cast<size_t>(i)] = false;
            beatSpaceLastRecallMaxError[static_cast<size_t>(i)] = 0.0f;
        }
        return false;
    }

    const bool likelyMicrotonic = isLikelyMicrotonicPlugin(instance->getName());

    if (likelyMicrotonic)
    {
        beatSpaceMicrotonicExactMapping = false;
        std::vector<juce::String> normalizedParamNames(static_cast<size_t>(totalParams));
        for (int paramIndex = 0; paramIndex < totalParams; ++paramIndex)
            normalizedParamNames[static_cast<size_t>(paramIndex)] =
                normalizeParameterToken(params[paramIndex]->getName(128));

        bool allReady = true;
        bool anyReady = false;
        int mappedCount = 0;

        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
        {
            const auto chIdx = static_cast<size_t>(channel);
            beatSpaceParamMap[chIdx].fill(-1);
            beatSpaceLastRecallMaxError[chIdx] = 0.0f;

            int mappedPatchParams = 0;
            const juce::String suffix = juce::String(channel + 1);
            for (int patch = 0; patch < BeatSpacePatchParamCount; ++patch)
            {
                const auto target = normalizeParameterToken(juce::String(kMicrotonicBeatSpacePatchParamOrder[static_cast<size_t>(patch)]) + suffix);
                for (int paramIndex = 0; paramIndex < totalParams; ++paramIndex)
                {
                    if (normalizedParamNames[static_cast<size_t>(paramIndex)] == target)
                    {
                        beatSpaceParamMap[chIdx][static_cast<size_t>(patch)] = paramIndex;
                        ++mappedPatchParams;
                        break;
                    }
                }
            }

            const bool channelReady = (mappedPatchParams == BeatSpacePatchParamCount);
            beatSpaceChannelMappingReady[chIdx] = channelReady;
            if (channelReady)
            {
                anyReady = true;
                ++mappedCount;
            }
            else
            {
                allReady = false;
            }
        }

        for (int channel = BeatSpaceChannels; channel < MaxStrips; ++channel)
        {
            const auto chIdx = static_cast<size_t>(channel);
            beatSpaceParamMap[chIdx].fill(-1);
            beatSpaceChannelMappingReady[chIdx] = false;
            beatSpaceLastRecallMaxError[chIdx] = 0.0f;
        }

        beatSpaceMappingReady = allReady;
        beatSpaceMicrotonicExactMapping = anyReady;
        if (mappedCount > 0 && !beatSpaceChannelMappingReady[static_cast<size_t>(beatSpaceSelectedChannel)])
        {
            for (int channel = 0; channel < BeatSpaceChannels; ++channel)
            {
                if (beatSpaceChannelMappingReady[static_cast<size_t>(channel)])
                {
                    beatSpaceSelectedChannel = channel;
                    break;
                }
            }
        }

        if (beatSpaceDecoderReady && beatSpaceMappingReady)
            beatSpaceStatusMessage = "BeatSpace ready (Microtonic exact patch map 25/25)";
        else if (beatSpaceDecoderReady && anyReady)
            beatSpaceStatusMessage = "BeatSpace: Microtonic mapping partial (" + juce::String(mappedCount) + "/" + juce::String(BeatSpaceChannels) + ")";
        else if (beatSpaceDecoderReady)
            beatSpaceStatusMessage = "BeatSpace: Microtonic mapping unavailable";
        if (refreshHostedDirectMap)
            refreshHostedDirectParameterMapFromBeatSpace(true);
        return anyReady;
    }

    auto isChannelKeyword = [](const juce::String& token)
    {
        return token == "ch"
            || token == "channel"
            || token == "drum"
            || token == "dr"
            || token == "voice"
            || token == "v"
            || token == "track"
            || token == "trk";
    };

    auto parseChannelDigits = [](const juce::String& digits)
    {
        if (digits.isEmpty())
            return 0;
        for (int i = 0; i < digits.length(); ++i)
        {
            if (!juce::CharacterFunctions::isDigit(digits[i]))
                return 0;
        }
        const int value = digits.getIntValue();
        return (value >= 1 && value <= BeatSpaceChannels) ? value : 0;
    };

    auto parseChannelFromToken = [&](const juce::String& token)
    {
        if (token.isEmpty())
            return 0;

        if (const int direct = parseChannelDigits(token); direct != 0)
            return direct;

        static constexpr std::array<const char*, 8> prefixes {{
            "ch", "channel", "drum", "dr", "voice", "v", "track", "trk"
        }};
        for (const auto* prefix : prefixes)
        {
            const juce::String prefixText(prefix);
            if (token.startsWith(prefixText) && token.length() > prefixText.length())
            {
                if (const int parsed = parseChannelDigits(token.substring(prefixText.length())); parsed != 0)
                    return parsed;
            }
        }

        if (token.length() == 2
            && juce::CharacterFunctions::isLetter(token[0])
            && juce::CharacterFunctions::isDigit(token[1]))
        {
            if (const int parsed = parseChannelDigits(token.substring(1)); parsed != 0)
                return parsed;
        }

        return 0;
    };

    auto extractChannelFromName = [&](const juce::String& lowerName)
    {
        juce::String tokenized;
        tokenized.preallocateBytes(lowerName.getNumBytesAsUTF8() + 1);
        for (int i = 0; i < lowerName.length(); ++i)
        {
            const juce::juce_wchar c = lowerName[i];
            if (juce::CharacterFunctions::isLetterOrDigit(c))
                tokenized += juce::String::charToString(c);
            else
                tokenized += " ";
        }

        juce::StringArray tokens;
        tokens.addTokens(tokenized, " ", "");
        tokens.removeEmptyStrings();
        if (tokens.isEmpty())
            return 0;

        int loneChannelToken = 0;
        int loneChannelTokenCount = 0;
        for (int i = 0; i < tokens.size(); ++i)
        {
            const auto token = tokens[i];

            if (isChannelKeyword(token) && i + 1 < tokens.size())
            {
                if (const int parsed = parseChannelFromToken(tokens[i + 1]); parsed != 0)
                    return parsed;
            }

            if (const int parsed = parseChannelFromToken(token); parsed != 0)
            {
                if (i == 0)
                    return parsed;
                if (i > 0 && isChannelKeyword(tokens[i - 1]))
                    return parsed;
                loneChannelToken = parsed;
                ++loneChannelTokenCount;
            }
        }

        if (loneChannelTokenCount == 1)
            return loneChannelToken;
        return 0;
    };

    auto matchAny = [](const juce::String& haystack, const std::array<juce::String, 8>& needles)
    {
        for (const auto& n : needles)
        {
            if (n.isNotEmpty() && haystack.contains(n))
                return true;
        }
        return false;
    };

    std::array<juce::Array<int>, BeatSpaceChannels> parsedCandidates;
    for (int paramIndex = 0; paramIndex < totalParams; ++paramIndex)
    {
        const auto name = params[paramIndex]->getName(128).toLowerCase();
        if (const int parsedChannel = extractChannelFromName(name);
            parsedChannel >= 1 && parsedChannel <= BeatSpaceChannels)
        {
            parsedCandidates[static_cast<size_t>(parsedChannel - 1)].addIfNotAlreadyThere(paramIndex);
        }
    }

    bool allReady = true;
    bool anyReady = false;
    const int estimatedStride = juce::jmax(1, totalParams / BeatSpaceChannels);

    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto chIdx = static_cast<size_t>(channel);
        beatSpaceParamMap[chIdx].fill(-1);
        beatSpaceLastRecallMaxError[chIdx] = 0.0f;

        int mappedParams = 0;
        auto assignContiguous = [&](int start, int availableCount)
        {
            if (availableCount <= 0)
                return;
            const int safeStart = juce::jlimit(0, juce::jmax(0, totalParams - 1), start);
            const int count = juce::jmax(0, juce::jmin(BeatSpaceVectorSize, juce::jmin(availableCount, totalParams - safeStart)));
            for (int i = 0; i < count; ++i)
                beatSpaceParamMap[chIdx][static_cast<size_t>(i)] = safeStart + i;
            mappedParams = juce::jmax(mappedParams, count);
        };

        const juce::String ch = juce::String(channel + 1);
        const std::array<juce::String, 8> channelNeedles {
            "ch " + ch,
            "ch" + ch,
            "channel " + ch,
            "channel" + ch,
            "drum " + ch,
            "voice " + ch,
            "track " + ch,
            "trk" + ch
        };

        juce::Array<int> candidates(parsedCandidates[static_cast<size_t>(channel)]);
        for (int paramIndex = 0; paramIndex < totalParams; ++paramIndex)
        {
            const auto name = params[paramIndex]->getName(128).toLowerCase();
            if (matchAny(name, channelNeedles))
                candidates.addIfNotAlreadyThere(paramIndex);
        }
        candidates.sort();

        if (candidates.size() >= BeatSpacePatchParamCount)
        {
            mappedParams = juce::jmin(BeatSpaceVectorSize, candidates.size());
            for (int i = 0; i < mappedParams; ++i)
                beatSpaceParamMap[chIdx][static_cast<size_t>(i)] = candidates[i];
        }

        // Fallback: split by per-channel parameter block for plugins that expose fewer than 73 params/channel.
        if (mappedParams < BeatSpacePatchParamCount)
        {
            const int blockOffset = channel * estimatedStride;
            const int blockAvailable = juce::jmax(0, juce::jmin(estimatedStride, totalParams - blockOffset));
            if (blockAvailable >= BeatSpacePatchParamCount)
                assignContiguous(blockOffset, blockAvailable);
        }

        // Legacy fallback: strict 73-sized contiguous blocks.
        if (mappedParams < BeatSpacePatchParamCount)
        {
            const int blockOffset = channel * BeatSpaceVectorSize;
            const int blockAvailable = juce::jmax(0, totalParams - blockOffset);
            if (blockAvailable >= BeatSpacePatchParamCount)
                assignContiguous(blockOffset, blockAvailable);
        }

        beatSpaceChannelMappingReady[chIdx] = (mappedParams >= BeatSpacePatchParamCount);
        if (beatSpaceChannelMappingReady[chIdx])
            anyReady = true;
        else
            allReady = false;
    }

    beatSpaceMicrotonicExactMapping = false;
    int mappedCount = 0;
    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        if (beatSpaceChannelMappingReady[static_cast<size_t>(channel)])
            ++mappedCount;
    }

    for (int channel = BeatSpaceChannels; channel < MaxStrips; ++channel)
    {
        const auto chIdx = static_cast<size_t>(channel);
        beatSpaceParamMap[chIdx].fill(-1);
        beatSpaceChannelMappingReady[chIdx] = false;
        beatSpaceLastRecallMaxError[chIdx] = 0.0f;
    }

    beatSpaceMappingReady = allReady;
    if (mappedCount > 0 && !beatSpaceChannelMappingReady[static_cast<size_t>(beatSpaceSelectedChannel)])
    {
        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
        {
            if (beatSpaceChannelMappingReady[static_cast<size_t>(channel)])
            {
                beatSpaceSelectedChannel = channel;
                break;
            }
        }
    }

    if (beatSpaceDecoderReady && beatSpaceMappingReady)
        beatSpaceStatusMessage = "BeatSpace ready (host patch mapping)";
    else if (beatSpaceDecoderReady && anyReady)
        beatSpaceStatusMessage = "BeatSpace: hosted mapping partial (" + juce::String(mappedCount) + "/" + juce::String(BeatSpaceChannels) + ")";
    else if (beatSpaceDecoderReady)
        beatSpaceStatusMessage = "BeatSpace: hosted mapping unavailable";
    if (refreshHostedDirectMap)
        refreshHostedDirectParameterMapFromBeatSpace(false);
    return anyReady;
}

void StepVstHostAudioProcessor::clampBeatSpaceView()
{
    const int viewW = getBeatSpaceViewWidth();
    const int viewH = getBeatSpaceViewHeight();
    beatSpaceViewX = juce::jlimit(0, juce::jmax(0, BeatSpaceTableSize - viewW), beatSpaceViewX);
    beatSpaceViewY = juce::jlimit(0, juce::jmax(0, BeatSpaceTableSize - viewH), beatSpaceViewY);
}

int StepVstHostAudioProcessor::getBeatSpaceViewWidth() const
{
    const int clampedZoom = juce::jlimit(0, BeatSpaceMaxZoom, beatSpaceZoomLevel);
    const float zoomOctaves =
        static_cast<float>(clampedZoom) / static_cast<float>(juce::jmax(1, BeatSpaceZoomStepsPerOctave));
    const float zoomScale = std::pow(0.5f, zoomOctaves);
    const int scaled = static_cast<int>(std::round(static_cast<float>(BeatSpaceTableSize) * zoomScale));
    return juce::jlimit(BeatSpaceMinViewWidth, BeatSpaceTableSize, scaled);
}

int StepVstHostAudioProcessor::getBeatSpaceViewHeight() const
{
    const int clampedZoom = juce::jlimit(0, BeatSpaceMaxZoom, beatSpaceZoomLevel);
    const float zoomOctaves =
        static_cast<float>(clampedZoom) / static_cast<float>(juce::jmax(1, BeatSpaceZoomStepsPerOctave));
    const float zoomScale = std::pow(0.5f, zoomOctaves);
    const int scaled = static_cast<int>(std::round(static_cast<float>(BeatSpaceTableSize) * zoomScale));
    return juce::jlimit(BeatSpaceMinViewHeight, BeatSpaceTableSize, scaled);
}

juce::Point<int> StepVstHostAudioProcessor::gridCellToBeatSpacePoint(
    int gridX, int gridY, int gridWidth, int gridHeight) const
{
    const int w = juce::jmax(1, gridWidth);
    const int h = juce::jmax(1, gridHeight);
    const int viewW = getBeatSpaceViewWidth();
    const int viewH = getBeatSpaceViewHeight();

    const float tx = (w > 1)
        ? static_cast<float>(juce::jlimit(0, w - 1, gridX)) / static_cast<float>(w - 1)
        : 0.5f;
    const float ty = (h > 1)
        ? static_cast<float>(juce::jlimit(0, h - 1, gridY)) / static_cast<float>(h - 1)
        : 0.5f;
    const int x = beatSpaceViewX + juce::jlimit(
        0,
        viewW - 1,
        static_cast<int>(std::round(tx * static_cast<float>(juce::jmax(0, viewW - 1)))));
    const int y = beatSpaceViewY + juce::jlimit(
        0,
        viewH - 1,
        static_cast<int>(std::round(ty * static_cast<float>(juce::jmax(0, viewH - 1)))));
    return { x, y };
}

juce::Point<int> StepVstHostAudioProcessor::beatSpacePointToGridCell(
    const juce::Point<int>& point, int gridWidth, int gridHeight) const
{
    const int w = juce::jmax(1, gridWidth);
    const int h = juce::jmax(1, gridHeight);
    const int viewW = getBeatSpaceViewWidth();
    const int viewH = getBeatSpaceViewHeight();
    const int px = juce::jlimit(0, BeatSpaceTableSize - 1, point.x);
    const int py = juce::jlimit(0, BeatSpaceTableSize - 1, point.y);
    const int relX = juce::jlimit(0, viewW - 1, px - beatSpaceViewX);
    const int relY = juce::jlimit(0, viewH - 1, py - beatSpaceViewY);
    const float tx = (viewW > 1)
        ? static_cast<float>(relX) / static_cast<float>(viewW - 1)
        : 0.0f;
    const float ty = (viewH > 1)
        ? static_cast<float>(relY) / static_cast<float>(viewH - 1)
        : 0.0f;
    const int gx = juce::jlimit(
        0,
        w - 1,
        static_cast<int>(std::round(tx * static_cast<float>(juce::jmax(0, w - 1)))));
    const int gy = juce::jlimit(
        0,
        h - 1,
        static_cast<int>(std::round(ty * static_cast<float>(juce::jmax(0, h - 1)))));
    return { gx, gy };
}

juce::Point<int> StepVstHostAudioProcessor::clampBeatSpacePointToTable(const juce::Point<int>& point) const
{
    return {
        juce::jlimit(0, BeatSpaceTableSize - 1, point.x),
        juce::jlimit(0, BeatSpaceTableSize - 1, point.y)
    };
}

juce::Point<int> StepVstHostAudioProcessor::constrainBeatSpacePointToAssignedCategoryCluster(
    int channel, const juce::Point<int>& point) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int assignedSpace = juce::jlimit(
        0,
        BeatSpaceChannels - 1,
        beatSpaceChannelCategoryAssignment[static_cast<size_t>(clampedChannel)]);
    const auto requested = clampBeatSpacePointToTable(point);

    const int tableCells = BeatSpaceTableSize * BeatSpaceTableSize;
    if (!beatSpaceColorClustersReady
        || static_cast<int>(beatSpaceColorClusters.size()) < tableCells)
    {
        return requested;
    }

    const int targetCluster = beatSpaceCategoryColorCluster[static_cast<size_t>(assignedSpace)];
    if (targetCluster < 0 || targetCluster >= BeatSpaceChannels)
        return requested;

    auto clusterAt = [this](int x, int y) -> int
    {
        if (x < 0 || y < 0 || x >= BeatSpaceTableSize || y >= BeatSpaceTableSize)
            return -1;
        const int idx = (y * BeatSpaceTableSize) + x;
        if (idx < 0 || idx >= static_cast<int>(beatSpaceColorClusters.size()))
            return -1;
        return beatSpaceColorClusters[static_cast<size_t>(idx)];
    };

    if (clusterAt(requested.x, requested.y) == targetCluster)
        return requested;

    juce::Point<int> bestPoint = requested;
    float bestDistanceSquared = std::numeric_limits<float>::max();
    bool found = false;

    auto considerPoint = [&](int x, int y)
    {
        if (clusterAt(x, y) != targetCluster)
            return;
        const float dx = static_cast<float>(x - requested.x);
        const float dy = static_cast<float>(y - requested.y);
        const float d2 = (dx * dx) + (dy * dy);
        if (d2 < bestDistanceSquared)
        {
            bestDistanceSquared = d2;
            bestPoint = { x, y };
            found = true;
        }
    };

    constexpr int kLocalSearchRadius = 192;
    for (int radius = 1; radius <= kLocalSearchRadius; ++radius)
    {
        const int minX = juce::jmax(0, requested.x - radius);
        const int maxX = juce::jmin(BeatSpaceTableSize - 1, requested.x + radius);
        const int minY = juce::jmax(0, requested.y - radius);
        const int maxY = juce::jmin(BeatSpaceTableSize - 1, requested.y + radius);

        for (int x = minX; x <= maxX; ++x)
        {
            considerPoint(x, minY);
            if (maxY != minY)
                considerPoint(x, maxY);
        }
        for (int y = minY + 1; y < maxY; ++y)
        {
            considerPoint(minX, y);
            if (maxX != minX)
                considerPoint(maxX, y);
        }

        if (found)
            return bestPoint;
    }

    bestDistanceSquared = std::numeric_limits<float>::max();
    found = false;
    constexpr int kCoarseStep = 12;
    for (int y = 0; y < BeatSpaceTableSize; y += kCoarseStep)
    {
        for (int x = 0; x < BeatSpaceTableSize; x += kCoarseStep)
            considerPoint(x, y);
        considerPoint(BeatSpaceTableSize - 1, y);
    }
    for (int x = 0; x < BeatSpaceTableSize; x += kCoarseStep)
        considerPoint(x, BeatSpaceTableSize - 1);
    considerPoint(BeatSpaceTableSize - 1, BeatSpaceTableSize - 1);

    if (!found)
    {
        if (beatSpaceCategoryAnchorsReady)
            return clampBeatSpacePointToTable(beatSpaceCategoryAnchors[static_cast<size_t>(assignedSpace)]);
        return requested;
    }

    const auto coarseBest = bestPoint;
    bestDistanceSquared = std::numeric_limits<float>::max();
    found = false;
    constexpr int kRefineRadius = 36;
    const int minRefineX = juce::jmax(0, coarseBest.x - kRefineRadius);
    const int maxRefineX = juce::jmin(BeatSpaceTableSize - 1, coarseBest.x + kRefineRadius);
    const int minRefineY = juce::jmax(0, coarseBest.y - kRefineRadius);
    const int maxRefineY = juce::jmin(BeatSpaceTableSize - 1, coarseBest.y + kRefineRadius);
    for (int y = minRefineY; y <= maxRefineY; ++y)
    {
        for (int x = minRefineX; x <= maxRefineX; ++x)
            considerPoint(x, y);
    }

    return found ? bestPoint : coarseBest;
}

juce::Point<int> StepVstHostAudioProcessor::constrainBeatSpacePointForChannel(
    int channel, const juce::Point<int>& point) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int assignedSpace = juce::jlimit(
        0,
        BeatSpaceChannels - 1,
        beatSpaceChannelCategoryAssignment[static_cast<size_t>(clampedChannel)]);
    const auto freePoint = clampBeatSpacePointToTable(point);
    juce::Point<int> constrained = freePoint;

    const bool clusterConstrainReady = beatSpaceCategoryConstrainEnabled
        && beatSpaceColorClustersReady
        && static_cast<int>(beatSpaceColorClusters.size()) >= (BeatSpaceTableSize * BeatSpaceTableSize)
        && beatSpaceCategoryColorCluster[static_cast<size_t>(assignedSpace)] >= 0;
    if (clusterConstrainReady)
        return constrainBeatSpacePointToAssignedCategoryCluster(clampedChannel, freePoint);

    if (!beatSpaceCategoryAnchorsReady)
        return constrained;

    const auto center = beatSpaceCategoryAnchors[static_cast<size_t>(assignedSpace)];
    const int rx = juce::jmax(1, beatSpaceCategoryRegionRadiusX[static_cast<size_t>(assignedSpace)]);
    const int ry = juce::jmax(1, beatSpaceCategoryRegionRadiusY[static_cast<size_t>(assignedSpace)]);

    constrained.x = juce::jlimit(center.x - rx, center.x + rx, freePoint.x);
    constrained.y = juce::jlimit(center.y - ry, center.y + ry, freePoint.y);
    constrained = clampBeatSpacePointToTable(constrained);

    const float lock = beatSpaceCategoryConstrainEnabled
        ? 1.0f
        : juce::jlimit(
            0.0f,
            1.0f,
            beatSpaceZoneLockStrength[static_cast<size_t>(clampedChannel)]);
    if (lock <= 0.0001f)
        return freePoint;
    if (lock >= 0.9999f)
        return constrained;

    const auto blend = [&](int a, int b)
    {
        return static_cast<int>(std::lround(
            static_cast<double>(a) + (static_cast<double>(b - a) * static_cast<double>(lock))));
    };
    return clampBeatSpacePointToTable({ blend(freePoint.x, constrained.x), blend(freePoint.y, constrained.y) });
}

juce::Point<int> StepVstHostAudioProcessor::randomBeatSpacePointForChannel(
    int channel, BeatSpaceRandomizeMode mode) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    auto randomFull = []()
    {
        return juce::Point<int> {
            juce::Random::getSystemRandom().nextInt(BeatSpaceTableSize),
            juce::Random::getSystemRandom().nextInt(BeatSpaceTableSize)
        };
    };

    const auto currentPoint =
        clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)]);

    auto randomNearPoint = [&](const juce::Point<int>& center, int radius)
    {
        const int safeRadius = juce::jmax(1, radius);
        const int span = (safeRadius * 2) + 1;
        const int dx = juce::Random::getSystemRandom().nextInt(span) - safeRadius;
        const int dy = juce::Random::getSystemRandom().nextInt(span) - safeRadius;
        return juce::Point<int> {
            juce::jlimit(0, BeatSpaceTableSize - 1, center.x + dx),
            juce::jlimit(0, BeatSpaceTableSize - 1, center.y + dy)
        };
    };

    auto randomWithinAssignedZone = [&]() -> juce::Point<int>
    {
        if (!beatSpaceCategoryAnchorsReady)
            return randomFull();
        const int assignedSpace = juce::jlimit(
            0,
            BeatSpaceChannels - 1,
            beatSpaceChannelCategoryAssignment[static_cast<size_t>(clampedChannel)]);
        const auto center = beatSpaceCategoryAnchors[static_cast<size_t>(assignedSpace)];
        const int rx = juce::jmax(1, beatSpaceCategoryRegionRadiusX[static_cast<size_t>(assignedSpace)]);
        const int ry = juce::jmax(1, beatSpaceCategoryRegionRadiusY[static_cast<size_t>(assignedSpace)]);
        const int x = juce::jlimit(
            0, BeatSpaceTableSize - 1,
            center.x + (juce::Random::getSystemRandom().nextInt((rx * 2) + 1) - rx));
        const int y = juce::jlimit(
            0, BeatSpaceTableSize - 1,
            center.y + (juce::Random::getSystemRandom().nextInt((ry * 2) + 1) - ry));
        return { x, y };
    };

    switch (mode)
    {
        case BeatSpaceRandomizeMode::FullWild:
            return constrainBeatSpacePointForChannel(clampedChannel, randomFull());
        case BeatSpaceRandomizeMode::WithinCategory:
            return constrainBeatSpacePointForChannel(clampedChannel, randomWithinAssignedZone());
        case BeatSpaceRandomizeMode::NearCurrent:
        {
            const int radius = juce::jmax(3, juce::jmin(14, getBeatSpaceViewWidth() / 5));
            return constrainBeatSpacePointForChannel(clampedChannel, randomNearPoint(currentPoint, radius));
        }
        case BeatSpaceRandomizeMode::PreserveCharacter:
        default:
            break;
    }

    if (beatSpaceTable.empty())
        return constrainBeatSpacePointForChannel(clampedChannel, randomWithinAssignedZone());

    const int currentIndex = (currentPoint.y * BeatSpaceTableSize) + currentPoint.x;
    const auto& currentVector = beatSpaceTable[static_cast<size_t>(
        juce::jlimit(0, static_cast<int>(beatSpaceTable.size()) - 1, currentIndex))].values;

    juce::Point<int> bestPoint = randomWithinAssignedZone();
    float bestScore = std::numeric_limits<float>::max();
    constexpr int kTries = 96;
    for (int attempt = 0; attempt < kTries; ++attempt)
    {
        const auto candidate = randomWithinAssignedZone();
        const int idx = (candidate.y * BeatSpaceTableSize) + candidate.x;
        if (idx < 0 || idx >= static_cast<int>(beatSpaceTable.size()))
            continue;
        const auto& candVector = beatSpaceTable[static_cast<size_t>(idx)].values;
        float patchDistance = 0.0f;
        for (int i = 0; i < BeatSpacePatchParamCount; ++i)
        {
            const float d =
                candVector[static_cast<size_t>(i)] - currentVector[static_cast<size_t>(i)];
            patchDistance += d * d;
        }
        patchDistance = std::sqrt(patchDistance / static_cast<float>(BeatSpacePatchParamCount));
        const float hotspot = (idx >= 0 && idx < static_cast<int>(beatSpaceHotspotWeights.size()))
            ? juce::jlimit(0.0f, 1.0f, beatSpaceHotspotWeights[static_cast<size_t>(idx)])
            : 0.45f;
        const float score = patchDistance + ((1.0f - hotspot) * 0.16f)
            + (juce::Random::getSystemRandom().nextFloat() * 0.015f);
        if (score < bestScore)
        {
            bestScore = score;
            bestPoint = candidate;
        }
    }
    return constrainBeatSpacePointForChannel(clampedChannel, bestPoint);
}

float StepVstHostAudioProcessor::getBeatSpacePointConfidence(const juce::Point<int>& point) const
{
    if (beatSpaceTable.empty())
        return 0.0f;

    const auto clamped = clampBeatSpacePointToTable(point);
    const int idx = (clamped.y * BeatSpaceTableSize) + clamped.x;
    if (idx < 0 || idx >= static_cast<int>(beatSpaceTable.size()))
        return 0.0f;

    const float hotspot = (idx >= 0 && idx < static_cast<int>(beatSpaceHotspotWeights.size()))
        ? juce::jlimit(0.0f, 1.0f, beatSpaceHotspotWeights[static_cast<size_t>(idx)])
        : 0.45f;

    float clusterAgreement = 0.5f;
    if (beatSpaceColorClustersReady && idx < static_cast<int>(beatSpaceColorClusters.size()))
    {
        const int c = juce::jlimit(
            0, BeatSpaceChannels - 1, beatSpaceColorClusters[static_cast<size_t>(idx)]);
        int same = 0;
        int total = 0;
        const auto checkNeighbor = [&](int nx, int ny)
        {
            if (nx < 0 || ny < 0 || nx >= BeatSpaceTableSize || ny >= BeatSpaceTableSize)
                return;
            ++total;
            const int nIdx = (ny * BeatSpaceTableSize) + nx;
            if (nIdx >= 0 && nIdx < static_cast<int>(beatSpaceColorClusters.size()))
            {
                const int nc = juce::jlimit(
                    0, BeatSpaceChannels - 1, beatSpaceColorClusters[static_cast<size_t>(nIdx)]);
                if (nc == c)
                    ++same;
            }
        };
        checkNeighbor(clamped.x - 1, clamped.y);
        checkNeighbor(clamped.x + 1, clamped.y);
        checkNeighbor(clamped.x, clamped.y - 1);
        checkNeighbor(clamped.x, clamped.y + 1);
        if (total > 0)
            clusterAgreement = static_cast<float>(same) / static_cast<float>(total);
    }

    float tagAffinity = 0.0f;
    for (int category = 0; category < BeatSpaceChannels; ++category)
    {
        const auto catIdx = static_cast<size_t>(category);
        const int tagCount = juce::jlimit(
            0, BeatSpacePresetSlotsPerSpace, beatSpaceCategoryManualTagCounts[catIdx]);
        for (int i = 0; i < tagCount; ++i)
        {
            const auto tagPoint = beatSpaceCategoryManualTagPoints[catIdx][static_cast<size_t>(i)];
            const float dx = static_cast<float>(tagPoint.x - clamped.x);
            const float dy = static_cast<float>(tagPoint.y - clamped.y);
            const float dist = std::sqrt((dx * dx) + (dy * dy));
            tagAffinity = juce::jmax(tagAffinity, juce::jlimit(0.0f, 1.0f, 1.0f - (dist / 14.0f)));
        }
    }

    return juce::jlimit(
        0.0f,
        1.0f,
        0.12f + (0.62f * hotspot) + (0.18f * clusterAgreement) + (0.18f * tagAffinity));
}

void StepVstHostAudioProcessor::updateBeatSpacePathMorph(const juce::AudioPlayHead::PositionInfo& posInfo)
{
    bool anyPathActive = false;
    for (const auto& path : beatSpacePaths)
    {
        if (path.active && path.count > 1)
        {
            anyPathActive = true;
            break;
        }
    }
    if (!anyPathActive || !beatSpaceDecoderReady || beatSpaceTable.empty())
        return;

    const bool hostPlaying = posInfo.getIsPlaying();
    const bool hasPpq = posInfo.getPpqPosition().hasValue();
    const double hostPpq = hasPpq ? *posInfo.getPpqPosition() : 0.0;
    const double hostBpm = (posInfo.getBpm().hasValue() && *posInfo.getBpm() > 0.0)
        ? *posInfo.getBpm()
        : 120.0;
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        auto& path = beatSpacePaths[static_cast<size_t>(channel)];
        if (!path.active || path.count < 2 || path.recording)
            continue;

        const int segments = juce::jmax(1, path.count - 1);
        const double cycleBeats = juce::jlimit(
            0.03125,
            128.0,
            path.cycleBeats > 0.0
                ? path.cycleBeats
                : ((path.mode == BeatSpacePathMode::QuarterNote) ? 0.25 : 4.0));
        const double cycleMs = juce::jmax(1.0, cycleBeats * (60000.0 / juce::jmax(1.0, hostBpm)));

        double phase = 0.0;
        if (hostPlaying && hasPpq)
        {
            if (path.pendingQuantizedStart)
            {
                if (!std::isfinite(path.startPpq) || path.startPpq < 0.0)
                    path.startPpq = (std::floor(hostPpq / 4.0) + 1.0) * 4.0;
                if (hostPpq + 1.0e-9 < path.startPpq)
                {
                    beatSpaceSetChannelPoint(channel, path.points[0], false, false);
                    continue;
                }
                path.pendingQuantizedStart = false;
            }

            if (path.startPpq < 0.0)
                path.startPpq = hostPpq;
            const double elapsed = (hostPpq - path.startPpq) / juce::jmax(1.0e-6, cycleBeats);
            phase = elapsed - std::floor(elapsed);
            if (phase < 0.0)
                phase += 1.0;
        }
        else
        {
            if (path.startMs <= 0.0)
                path.startMs = nowMs;
            const double elapsed = (nowMs - path.startMs) / juce::jmax(1.0, cycleMs);
            phase = elapsed - std::floor(elapsed);
            if (phase < 0.0)
                phase += 1.0;
        }

        auto wrapIndex = [](int value, int modulo) -> int
        {
            if (modulo <= 0)
                return 0;
            int wrapped = value % modulo;
            if (wrapped < 0)
                wrapped += modulo;
            return wrapped;
        };

        auto hashToUnit = [](uint64_t seed) -> double
        {
            seed += 0x9e3779b97f4a7c15ULL;
            seed = (seed ^ (seed >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            seed = (seed ^ (seed >> 27U)) * 0x94d049bb133111ebULL;
            seed ^= (seed >> 31U);
            constexpr double scale = 1.0 / static_cast<double>(std::numeric_limits<uint32_t>::max());
            return static_cast<double>(static_cast<uint32_t>(seed & 0xffffffffULL)) * scale;
        };

        int fromIndex = 0;
        int toIndex = juce::jmin(1, path.count - 1);
        float segT = 0.0f;

        auto assignLinearSegment = [&](double scaledPosition)
        {
            const double maxScaled = static_cast<double>(segments);
            const double clampedScaled = juce::jlimit(0.0, maxScaled, scaledPosition);
            int segIndex = static_cast<int>(std::floor(clampedScaled));
            segT = static_cast<float>(clampedScaled - std::floor(clampedScaled));
            if (segIndex >= segments)
            {
                segIndex = segments - 1;
                segT = 1.0f;
            }
            else if (segIndex < 0)
            {
                segIndex = 0;
                segT = 0.0f;
            }
            else if (segIndex >= (segments - 1) && segT > 0.999f)
            {
                segT = 1.0f;
            }

            fromIndex = juce::jlimit(0, path.count - 1, segIndex);
            toIndex = juce::jlimit(0, path.count - 1, segIndex + 1);
        };

        switch (path.playMode)
        {
            case BeatSpacePathPlayMode::Reverse:
            {
                assignLinearSegment((1.0 - phase) * static_cast<double>(segments));
                break;
            }
            case BeatSpacePathPlayMode::PingPong:
            {
                const double pingTravel = static_cast<double>(segments) * 2.0;
                const double pingScaled = phase * pingTravel;
                if (pingScaled <= static_cast<double>(segments))
                    assignLinearSegment(pingScaled);
                else
                    assignLinearSegment(pingTravel - pingScaled);
                break;
            }
            case BeatSpacePathPlayMode::Random:
            {
                const int stepsPerCycle = juce::jmax(1, segments * 2);
                const double stepPos = phase * static_cast<double>(stepsPerCycle);
                const int stepIndex = juce::jmax(0, static_cast<int>(std::floor(stepPos)));
                segT = static_cast<float>(stepPos - std::floor(stepPos));
                const uint64_t seed = (static_cast<uint64_t>(channel + 1) << 48U)
                    ^ (static_cast<uint64_t>(path.count) << 32U)
                    ^ static_cast<uint64_t>(stepIndex + 1);
                const int segIndex = juce::jlimit(
                    0,
                    segments - 1,
                    static_cast<int>(std::floor(hashToUnit(seed) * static_cast<double>(segments))));
                fromIndex = segIndex;
                toIndex = juce::jmin(path.count - 1, segIndex + 1);
                break;
            }
            case BeatSpacePathPlayMode::RandomWalk:
            {
                const int nodeCount = juce::jmax(1, path.count);
                const int stepsPerCycle = juce::jmax(1, segments * 2);
                const double stepPos = phase * static_cast<double>(stepsPerCycle);
                const int stepIndex = juce::jmax(0, static_cast<int>(std::floor(stepPos)));
                segT = static_cast<float>(stepPos - std::floor(stepPos));
                const int walkPeriod = juce::jlimit(16, 512, stepsPerCycle * 8);

                auto nodeAtStep = [&](int targetStep)
                {
                    if (nodeCount <= 1)
                        return 0;

                    const int boundedStep = juce::jmax(0, targetStep % walkPeriod);
                    int node = wrapIndex((channel * 3) + path.count, nodeCount);
                    for (int s = 0; s < boundedStep; ++s)
                    {
                        const uint64_t seed = (static_cast<uint64_t>(channel + 1) << 40U)
                            ^ (static_cast<uint64_t>(path.count) << 24U)
                            ^ static_cast<uint64_t>(s + 1);
                        const double r = hashToUnit(seed);
                        int delta = 0;
                        if (r < 0.16)
                            delta = -2;
                        else if (r < 0.42)
                            delta = -1;
                        else if (r < 0.58)
                            delta = 0;
                        else if (r < 0.84)
                            delta = 1;
                        else
                            delta = 2;
                        node = wrapIndex(node + delta, nodeCount);
                    }
                    return node;
                };

                fromIndex = nodeAtStep(stepIndex);
                toIndex = nodeAtStep(stepIndex + 1);
                break;
            }
            case BeatSpacePathPlayMode::RandomSlice:
            {
                const int stepsPerCycle = juce::jmax(1, segments * 2);
                const double stepPos = phase * static_cast<double>(stepsPerCycle);
                const int stepIndex = juce::jmax(0, static_cast<int>(std::floor(stepPos)));
                segT = static_cast<float>(stepPos - std::floor(stepPos));
                const int motifLength = juce::jmax(1, juce::jmin(4, segments));
                const int motifGroup = stepIndex / motifLength;
                const int motifStep = stepIndex % motifLength;
                const uint64_t groupSeed = (static_cast<uint64_t>(channel + 1) << 40U)
                    ^ (static_cast<uint64_t>(path.count) << 24U)
                    ^ static_cast<uint64_t>(motifGroup + 1);
                const int baseSeg = wrapIndex(
                    static_cast<int>(std::floor(hashToUnit(groupSeed) * static_cast<double>(segments))),
                    segments);
                const int direction = (hashToUnit(groupSeed ^ 0x91e10da5c79e7b1dULL) < 0.5) ? -1 : 1;
                const int segIndex = wrapIndex(baseSeg + (direction * motifStep), segments);
                fromIndex = segIndex;
                toIndex = juce::jmin(path.count - 1, segIndex + 1);
                break;
            }
            case BeatSpacePathPlayMode::Normal:
            default:
            {
                assignLinearSegment(phase * static_cast<double>(segments));
                break;
            }
        }

        const auto from = path.points[static_cast<size_t>(fromIndex)];
        const auto to = path.points[static_cast<size_t>(toIndex)];
        const juce::Point<int> p {
            static_cast<int>(std::lround(juce::jmap(segT, static_cast<float>(from.x), static_cast<float>(to.x)))),
            static_cast<int>(std::lround(juce::jmap(segT, static_cast<float>(from.y), static_cast<float>(to.y))))
        };
        beatSpaceSetChannelPoint(channel, p, false, false);
    }
}

void StepVstHostAudioProcessor::applyBeatSpaceModTargetOffsets()
{
    auto resetAppliedOffsets = [this]()
    {
        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
        {
            const auto idx = static_cast<size_t>(channel);
            beatSpaceModAppliedOffsetX[idx] = 0;
            beatSpaceModAppliedOffsetY[idx] = 0;
        }
    };

    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
    {
        resetAppliedOffsets();
        return;
    }

    if (audioEngine == nullptr)
    {
        resetAppliedOffsets();
        return;
    }

    const int viewW = getBeatSpaceViewWidth();
    const int viewH = getBeatSpaceViewHeight();
    const int xRange = juce::jmax(1, static_cast<int>(std::lround(static_cast<float>(viewW) * 0.42f)));
    const int yRange = juce::jmax(1, static_cast<int>(std::lround(static_cast<float>(viewH) * 0.42f)));
    bool anyChanged = false;

    if (beatSpaceLinkAllChannels)
    {
        float groupXSigned = 0.0f;
        float groupYSigned = 0.0f;
        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
        {
            const auto target = audioEngine->getModTarget(channel);
            if (target == ModernAudioEngine::ModTarget::BeatSpaceX)
            {
                groupXSigned = juce::jlimit(
                    -1.0f, 1.0f,
                    groupXSigned + audioEngine->getBeatSpaceModXSigned(channel));
            }
            else if (target == ModernAudioEngine::ModTarget::BeatSpaceY)
            {
                groupYSigned = juce::jlimit(
                    -1.0f, 1.0f,
                    groupYSigned + audioEngine->getBeatSpaceModYSigned(channel));
            }
        }

        const int desiredDx = juce::jlimit(-xRange, xRange, static_cast<int>(std::lround(groupXSigned * static_cast<float>(xRange))));
        const int desiredDy = juce::jlimit(-yRange, yRange, static_cast<int>(std::lround(groupYSigned * static_cast<float>(yRange))));

        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
        {
            const auto idx = static_cast<size_t>(channel);
            const int previousDx = beatSpaceModAppliedOffsetX[idx];
            const int previousDy = beatSpaceModAppliedOffsetY[idx];

            auto basePoint = beatSpaceChannelPoints[idx];
            basePoint.x -= previousDx;
            basePoint.y -= previousDy;
            basePoint = clampBeatSpacePointToTable(basePoint);

            auto modPoint = basePoint;
            modPoint.x += desiredDx;
            modPoint.y += desiredDy;
            modPoint = clampBeatSpacePointToTable(modPoint);

            const int appliedDx = modPoint.x - basePoint.x;
            const int appliedDy = modPoint.y - basePoint.y;
            if (appliedDx != previousDx || appliedDy != previousDy
                || modPoint != beatSpaceChannelPoints[idx])
            {
                anyChanged = true;
            }

            beatSpaceChannelPoints[idx] = modPoint;
            beatSpaceModAppliedOffsetX[idx] = appliedDx;
            beatSpaceModAppliedOffsetY[idx] = appliedDy;
        }
    }
    else
    {
        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
        {
            const auto idx = static_cast<size_t>(channel);
            const auto target = audioEngine->getModTarget(channel);
            int desiredDx = 0;
            int desiredDy = 0;

            if (target == ModernAudioEngine::ModTarget::BeatSpaceX)
            {
                const float signedOffset = audioEngine->getBeatSpaceModXSigned(channel);
                desiredDx = juce::jlimit(
                    -xRange, xRange,
                    static_cast<int>(std::lround(signedOffset * static_cast<float>(xRange))));
            }
            else if (target == ModernAudioEngine::ModTarget::BeatSpaceY)
            {
                const float signedOffset = audioEngine->getBeatSpaceModYSigned(channel);
                desiredDy = juce::jlimit(
                    -yRange, yRange,
                    static_cast<int>(std::lround(signedOffset * static_cast<float>(yRange))));
            }

            const int previousDx = beatSpaceModAppliedOffsetX[idx];
            const int previousDy = beatSpaceModAppliedOffsetY[idx];

            auto basePoint = beatSpaceChannelPoints[idx];
            basePoint.x -= previousDx;
            basePoint.y -= previousDy;
            basePoint = clampBeatSpacePointToTable(basePoint);

            auto modPoint = basePoint;
            modPoint.x += desiredDx;
            modPoint.y += desiredDy;
            modPoint = clampBeatSpacePointToTable(modPoint);

            const int appliedDx = modPoint.x - basePoint.x;
            const int appliedDy = modPoint.y - basePoint.y;
            if (appliedDx != previousDx || appliedDy != previousDy
                || modPoint != beatSpaceChannelPoints[idx])
            {
                anyChanged = true;
            }

            beatSpaceChannelPoints[idx] = modPoint;
            beatSpaceModAppliedOffsetX[idx] = appliedDx;
            beatSpaceModAppliedOffsetY[idx] = appliedDy;
        }
    }

    if (anyChanged)
        applyBeatSpacePointToChannels(true, false);
}

void StepVstHostAudioProcessor::applyFavoritesModTargetRecall()
{
    if (audioEngine == nullptr)
    {
        beatSpaceFavoritesModLastSlot = -1;
        return;
    }

    int activeStrip = -1;
    float norm = -1.0f;
    for (int strip = 0; strip < BeatSpaceChannels; ++strip)
    {
        const auto target = audioEngine->getModTarget(strip);
        if (target == ModernAudioEngine::ModTarget::Favorites)
        {
            activeStrip = strip;
            norm = audioEngine->getFavoritesModNorm(strip);
            break;
        }
    }

    if (activeStrip < 0 || norm < 0.0f)
    {
        beatSpaceFavoritesModLastSlot = -1;
        return;
    }

    constexpr int kitScopeChannel = 0;
    const auto usedSlots = getMicrotonicStripPresetSlots(kitScopeChannel);
    if (usedSlots.empty())
    {
        beatSpaceFavoritesModLastSlot = -1;
        return;
    }

    const int mappedIndex = juce::jlimit(
        0,
        static_cast<int>(usedSlots.size()) - 1,
        static_cast<int>(std::lround(
            juce::jlimit(0.0f, 1.0f, norm) * static_cast<float>(juce::jmax(0, static_cast<int>(usedSlots.size()) - 1)))));
    const int slot = juce::jlimit(
        0,
        MicrotonicStripPresetSlots - 1,
        usedSlots[static_cast<size_t>(mappedIndex)]);

    if (slot == beatSpaceFavoritesModLastSlot)
        return;

    juce::String error;
    if (recallMicrotonicStripPreset(kitScopeChannel, slot, &error))
        beatSpaceFavoritesModLastSlot = slot;
}

void StepVstHostAudioProcessor::applyPendingFavoritesBeatSpaceJump()
{
    if (pendingFavoritesBeatSpaceJump.exchange(0, std::memory_order_acq_rel) == 0)
        return;

    const int slot = pendingFavoritesBeatSpaceJumpSlot.exchange(-1, std::memory_order_acq_rel);
    if (slot < 0 || slot >= BeatSpacePresetSlotsPerSpace)
        return;
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
    {
        pendingFavoritesBeatSpaceJumpSlot.store(slot, std::memory_order_release);
        pendingFavoritesBeatSpaceJump.store(1, std::memory_order_release);
        return;
    }

    if (!beatSpaceCategoryAnchorsReady)
        rebuildBeatSpaceCategoryAnchors();

    std::array<juce::Point<int>, BeatSpaceChannels> targetPoints{};
    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const int assignedSpace = getBeatSpaceChannelSpaceAssignment(channel);
        const auto spaceIdx = static_cast<size_t>(assignedSpace);
        if (!beatSpaceCategoryPresetPointsReady[spaceIdx])
            rebuildBeatSpaceCategoryPresetPoints();

        auto targetPoint = beatSpaceCategoryPresetPoints[spaceIdx][static_cast<size_t>(slot)];
        if (!beatSpaceCategoryPresetPointsReady[spaceIdx])
        {
            targetPoint = beatSpaceCategoryAnchorsReady
                ? beatSpaceCategoryAnchors[spaceIdx]
                : juce::Point<int> { BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 };
        }
        targetPoints[static_cast<size_t>(channel)] =
            constrainBeatSpacePointForChannel(channel, targetPoint);
    }

    bool anyMoved = false;
    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto idx = static_cast<size_t>(channel);
        const auto previous = clampBeatSpacePointToTable(beatSpaceChannelPoints[idx]);
        const auto target = clampBeatSpacePointToTable(targetPoints[idx]);
        if (target == previous)
            continue;

        anyMoved = true;
        beatSpaceChannelPoints[idx] = target;

        auto& path = beatSpacePaths[idx];
        if (path.count <= 0)
            continue;

        const int dx = target.x - previous.x;
        const int dy = target.y - previous.y;
        if (dx == 0 && dy == 0)
            continue;

        for (int p = 0; p < path.count && p < BeatSpacePathMaxPoints; ++p)
        {
            auto shifted = path.points[static_cast<size_t>(p)];
            shifted.x += dx;
            shifted.y += dy;
            path.points[static_cast<size_t>(p)] = shifted;
        }
    }

    if (!anyMoved)
        return;

    if (beatSpaceLinkAllChannels)
    {
        const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        rebuildBeatSpaceLinkedOffsetsFromCurrent(selectedChannel);
    }

    applyBeatSpacePointToChannels(true, false);
    requestImmediateUiAndMonomeRefresh();
}

juce::Point<int> StepVstHostAudioProcessor::constrainBeatSpaceLinkedMasterPoint(
    const juce::Point<int>& masterPoint, int masterChannel) const
{
    if (beatSpaceCategoryConstrainEnabled)
        return constrainBeatSpacePointForChannel(masterChannel, masterPoint);

    juce::ignoreUnused(masterChannel);
    // Default mode: keep the link handle draggable across the full table.
    return clampBeatSpacePointToTable(masterPoint);
}

void StepVstHostAudioProcessor::applyBeatSpaceLinkedChannelOffsets(
    const juce::Point<int>& masterPoint, int masterChannel)
{
    const int anchorChannel = juce::jlimit(0, BeatSpaceChannels - 1, masterChannel);
    if (!beatSpaceLinkedOffsetsReady)
        rebuildBeatSpaceLinkedOffsetsFromCurrent(anchorChannel);

    beatSpaceLinkedOffsets[static_cast<size_t>(anchorChannel)] = { 0.0f, 0.0f };
    const auto constrainedMaster = constrainBeatSpaceLinkedMasterPoint(masterPoint, anchorChannel);

    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        auto offset = beatSpaceLinkedOffsets[static_cast<size_t>(channel)];
        if (channel == anchorChannel)
            offset = { 0.0f, 0.0f };
        juce::Point<int> p {
            static_cast<int>(std::lround(static_cast<float>(constrainedMaster.x) + offset.x)),
            static_cast<int>(std::lround(static_cast<float>(constrainedMaster.y) + offset.y))
        };
        p = beatSpaceCategoryConstrainEnabled
            ? constrainBeatSpacePointForChannel(channel, p)
            : clampBeatSpacePointToTable(p);
        beatSpaceChannelPoints[static_cast<size_t>(channel)] = p;
    }
}

void StepVstHostAudioProcessor::rebuildBeatSpaceLinkedOffsetsFromCurrent(int masterChannel)
{
    const int anchorChannel = juce::jlimit(0, BeatSpaceChannels - 1, masterChannel);
    const auto anchorPoint = clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(anchorChannel)]);

    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto p = clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(channel)]);
        beatSpaceLinkedOffsets[static_cast<size_t>(channel)] = {
            static_cast<float>(p.x - anchorPoint.x),
            static_cast<float>(p.y - anchorPoint.y)
        };
    }

    beatSpaceLinkedOffsets[static_cast<size_t>(anchorChannel)] = { 0.0f, 0.0f };
    beatSpaceLinkedOffsetsReady = true;
}

void StepVstHostAudioProcessor::reanchorBeatSpaceLinkedOffsets(int newAnchorChannel)
{
    const int anchorChannel = juce::jlimit(0, BeatSpaceChannels - 1, newAnchorChannel);
    if (!beatSpaceLinkedOffsetsReady)
    {
        rebuildBeatSpaceLinkedOffsetsFromCurrent(anchorChannel);
        return;
    }

    const auto oldAnchorOffset = beatSpaceLinkedOffsets[static_cast<size_t>(anchorChannel)];
    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        auto& offset = beatSpaceLinkedOffsets[static_cast<size_t>(channel)];
        offset.x -= oldAnchorOffset.x;
        offset.y -= oldAnchorOffset.y;
    }
    beatSpaceLinkedOffsets[static_cast<size_t>(anchorChannel)] = { 0.0f, 0.0f };
}

void StepVstHostAudioProcessor::forceBeatSpaceLinkRefreshIfEnabled()
{
    if (!beatSpaceLinkAllChannels)
        return;

    if (!beatSpaceCategoryAnchorsReady)
        rebuildBeatSpaceCategoryAnchors();

    const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
    reanchorBeatSpaceLinkedOffsets(selectedChannel);
    applyBeatSpaceLinkedChannelOffsets(
        beatSpaceChannelPoints[static_cast<size_t>(selectedChannel)],
        selectedChannel);
    applyBeatSpacePointToChannels(true, false);
}

void StepVstHostAudioProcessor::rebuildBeatSpaceMacroTraitAnchors()
{
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
    {
        for (int trait = 0; trait < 4; ++trait)
        {
            for (int channel = 0; channel < BeatSpaceChannels; ++channel)
            {
                beatSpaceMacroTraitAnchors[static_cast<size_t>(trait)][static_cast<size_t>(channel)] =
                    clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(channel)]);
            }
        }
        beatSpaceMacroTraitAnchorsReady = true;
        return;
    }

    if (!beatSpaceCategoryAnchorsReady)
        rebuildBeatSpaceCategoryAnchors();

    for (int space = 0; space < BeatSpaceChannels; ++space)
    {
        if (!beatSpaceCategoryPresetPointsReady[static_cast<size_t>(space)])
        {
            rebuildBeatSpaceCategoryPresetPoints();
            break;
        }
    }

    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const int assignedSpace = getBeatSpaceChannelSpaceAssignment(channel);
        const auto spaceIdx = static_cast<size_t>(assignedSpace);
        const auto fallbackPoint = constrainBeatSpacePointForChannel(
            channel,
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(channel)]));

        std::array<float, 4> bestScores{};
        bestScores.fill(std::numeric_limits<float>::lowest());
        std::array<juce::Point<int>, 4> bestPoints{};
        bestPoints.fill(fallbackPoint);

        auto evaluatePoint = [&](const juce::Point<int>& point)
        {
            const auto clampedPoint = clampBeatSpacePointToTable(point);
            const int tableIndex = (clampedPoint.y * BeatSpaceTableSize) + clampedPoint.x;
            if (tableIndex < 0 || tableIndex >= static_cast<int>(beatSpaceTable.size()))
                return;
            const auto& values = beatSpaceTable[static_cast<size_t>(tableIndex)].values;
            for (int trait = 0; trait < 4; ++trait)
            {
                const float score = beatSpaceTraitScoreForIndex(trait, values);
                if (score > bestScores[static_cast<size_t>(trait)])
                {
                    bestScores[static_cast<size_t>(trait)] = score;
                    bestPoints[static_cast<size_t>(trait)] = clampedPoint;
                }
            }
        };

        bool evaluated = false;
        if (beatSpaceCategoryPresetPointsReady[spaceIdx])
        {
            auto visibleSlots = getBeatSpaceVisiblePresetSlotsForSpace(assignedSpace);
            if (visibleSlots.empty())
            {
                for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
                    visibleSlots.push_back(slot);
            }

            for (const int slot : visibleSlots)
            {
                const int clampedSlot = juce::jlimit(0, BeatSpacePresetSlotsPerSpace - 1, slot);
                evaluatePoint(beatSpaceCategoryPresetPoints[spaceIdx][static_cast<size_t>(clampedSlot)]);
                evaluated = true;
            }
        }

        if (!evaluated && beatSpaceCategoryAnchorsReady)
            evaluatePoint(beatSpaceCategoryAnchors[spaceIdx]);

        if (beatSpaceCategoryAnchorsReady)
        {
            const auto anchor = beatSpaceCategoryAnchors[spaceIdx];
            const int radiusX = juce::jmax(24, beatSpaceCategoryRegionRadiusX[spaceIdx]);
            const int radiusY = juce::jmax(24, beatSpaceCategoryRegionRadiusY[spaceIdx]);
            const int stepX = juce::jmax(12, radiusX / 5);
            const int stepY = juce::jmax(12, radiusY / 5);
            const int minX = juce::jlimit(0, BeatSpaceTableSize - 1, anchor.x - radiusX);
            const int maxX = juce::jlimit(0, BeatSpaceTableSize - 1, anchor.x + radiusX);
            const int minY = juce::jlimit(0, BeatSpaceTableSize - 1, anchor.y - radiusY);
            const int maxY = juce::jlimit(0, BeatSpaceTableSize - 1, anchor.y + radiusY);

            for (int y = minY; y <= maxY; y += stepY)
            {
                for (int x = minX; x <= maxX; x += stepX)
                    evaluatePoint({ x, y });
            }
            evaluatePoint({ minX, minY });
            evaluatePoint({ maxX, minY });
            evaluatePoint({ minX, maxY });
            evaluatePoint({ maxX, maxY });
        }

        // Global coarse scan ensures trait anchors remain audibly distinct
        // even if category-local presets are tightly clustered.
        const int globalStep = juce::jmax(24, BeatSpaceTableSize / 28);
        for (int y = 0; y < BeatSpaceTableSize; y += globalStep)
        {
            for (int x = 0; x < BeatSpaceTableSize; x += globalStep)
                evaluatePoint({ x, y });
        }
        evaluatePoint({ 0, 0 });
        evaluatePoint({ BeatSpaceTableSize - 1, 0 });
        evaluatePoint({ 0, BeatSpaceTableSize - 1 });
        evaluatePoint({ BeatSpaceTableSize - 1, BeatSpaceTableSize - 1 });

        evaluatePoint(fallbackPoint);

        for (int trait = 0; trait < 4; ++trait)
        {
            beatSpaceMacroTraitAnchors[static_cast<size_t>(trait)][static_cast<size_t>(channel)] =
                constrainBeatSpacePointForChannel(channel, bestPoints[static_cast<size_t>(trait)]);
        }
    }

    beatSpaceMacroTraitAnchorsReady = true;
}

void StepVstHostAudioProcessor::applyBeatSpaceMacroBubbleOffsetsFromKnobs()
{
    const float deep = juce::jlimit(0.0f, 1.0f, beatSpaceMacroKnobValues[0]);
    const float tight = juce::jlimit(0.0f, 1.0f, beatSpaceMacroKnobValues[1]);
    const float noise = juce::jlimit(0.0f, 1.0f, beatSpaceMacroKnobValues[2]);
    const float dense = juce::jlimit(0.0f, 1.0f, beatSpaceMacroKnobValues[3]);
    const float totalWeight = deep + tight + noise + dense;

    if (!beatSpaceMacroBasePointsValid)
    {
        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
        {
            beatSpaceMacroBasePoints[static_cast<size_t>(channel)] =
                clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(channel)]);
        }
        beatSpaceMacroBasePointsValid = true;
    }

    if (!beatSpaceMacroTraitAnchorsReady)
        rebuildBeatSpaceMacroTraitAnchors();

    std::array<juce::Point<int>, BeatSpaceChannels> targetPoints{};
    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto basePoint = beatSpaceMacroBasePoints[static_cast<size_t>(channel)];
        juce::Point<float> blended {
            static_cast<float>(basePoint.x),
            static_cast<float>(basePoint.y)
        };

        if (totalWeight > 1.0e-5f)
        {
            juce::Point<float> weightedAnchor { 0.0f, 0.0f };
            const std::array<float, 4> weights { deep, tight, noise, dense };
            for (int trait = 0; trait < 4; ++trait)
            {
                const float weight = weights[static_cast<size_t>(trait)];
                if (weight <= 0.0f)
                    continue;
                const auto anchor = beatSpaceMacroTraitAnchors[static_cast<size_t>(trait)][static_cast<size_t>(channel)];
                weightedAnchor.x += static_cast<float>(anchor.x) * weight;
                weightedAnchor.y += static_cast<float>(anchor.y) * weight;
            }

            weightedAnchor.x /= totalWeight;
            weightedAnchor.y /= totalWeight;
            const float blendAmount = juce::jlimit(0.0f, 1.0f, totalWeight);
            blended.x += (weightedAnchor.x - blended.x) * blendAmount;
            blended.y += (weightedAnchor.y - blended.y) * blendAmount;
        }

        const auto point = juce::Point<int> {
            static_cast<int>(std::lround(blended.x)),
            static_cast<int>(std::lround(blended.y))
        };
        targetPoints[static_cast<size_t>(channel)] = constrainBeatSpacePointForChannel(channel, point);
    }

    bool anyMoved = false;
    beatSpaceApplyingMacroMove = true;
    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto idx = static_cast<size_t>(channel);
        const auto clampedTarget = clampBeatSpacePointToTable(targetPoints[idx]);
        if (clampedTarget != beatSpaceChannelPoints[idx])
            anyMoved = true;
        beatSpaceChannelPoints[idx] = clampedTarget;
    }
    beatSpaceApplyingMacroMove = false;

    if (!anyMoved)
        return;

    if (beatSpaceLinkAllChannels)
    {
        const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        rebuildBeatSpaceLinkedOffsetsFromCurrent(selectedChannel);
    }
    applyBeatSpacePointToChannels(true, false);
    requestImmediateUiAndMonomeRefresh();
}

void StepVstHostAudioProcessor::beginBeatSpaceMorphForChannel(
    int channel,
    const std::array<float, BeatSpaceVectorSize>& targetValues,
    const juce::Point<int>& targetPoint)
{
    if (channel < 0 || channel >= BeatSpaceChannels)
        return;
    const auto idx = static_cast<size_t>(channel);
    const auto clampedTargetPoint = clampBeatSpacePointToTable(targetPoint);
    beatSpaceMorphStartVectors[idx] = beatSpaceCurrentVectors[idx];
    beatSpaceMorphTargetVectors[idx] = targetValues;
    beatSpaceMorphStartPoints[idx] = clampBeatSpacePointToTable(
        beatSpaceMorphActive[idx]
            ? beatSpaceMorphCurrentPoints[idx]
            : beatSpaceMorphTargetPoints[idx]);
    beatSpaceMorphTargetPoints[idx] = clampedTargetPoint;
    beatSpaceMorphCurrentPoints[idx] = beatSpaceMorphStartPoints[idx];
    beatSpaceMorphProgress[idx] = 0.0f;
    beatSpaceMorphActive[idx] = true;
    beatSpaceMorphStartTimeMs = juce::Time::getMillisecondCounterHiRes();
}

void StepVstHostAudioProcessor::applyBeatSpaceVectorToChannel(
    int channel, const std::array<float, BeatSpaceVectorSize>& values)
{
    if (channel < 0 || channel >= BeatSpaceChannels)
        return;
    if (!beatSpaceDecoderReady)
        return;

    auto* instance = hostRack.getInstance();
    if (instance == nullptr)
        return;

    const auto& params = instance->getParameters();
    const auto idx = static_cast<size_t>(channel);
    if (!beatSpaceChannelMappingReady[idx])
        return;
    const bool quantizeBeatSpacePitch =
        isGlobalKitScaleQuantizeEnabled() && beatSpaceMicrotonicExactMapping;
    const auto kitScale = getGlobalKitPitchScale();
    const int kitRoot = getGlobalKitPitchRootSemitone();
    const float macroPitch = (juce::jlimit(0.0f, 1.0f, beatSpaceMacroKnobValues[4]) - 0.5f) * 2.0f;
    const float macroPitchMod = (juce::jlimit(0.0f, 1.0f, beatSpaceMacroKnobValues[5]) - 0.5f) * 2.0f;
    const float macroLength = (juce::jlimit(0.0f, 1.0f, beatSpaceMacroKnobValues[6]) - 0.5f) * 2.0f;
    const float macroFilter = (juce::jlimit(0.0f, 1.0f, beatSpaceMacroKnobValues[7]) - 0.5f) * 2.0f;

    std::array<float, BeatSpaceVectorSize> appliedValues = values;
    float maxError = 0.0f;
    for (int i = 0; i < BeatSpaceVectorSize; ++i)
    {
        const int paramIndex = beatSpaceParamMap[idx][static_cast<size_t>(i)];
        if (paramIndex < 0 || paramIndex >= static_cast<int>(params.size()))
            continue;

        float normalized = juce::jlimit(0.0f, 1.0f, values[static_cast<size_t>(i)]);
        if (i == kMicrotonicPatchOscFreqIndex)
            normalized = juce::jlimit(0.0f, 1.0f, normalized + (macroPitch * 0.22f));
        else if (i == kMicrotonicPatchModAmtIndex)
            normalized = juce::jlimit(0.0f, 1.0f, normalized + (macroPitchMod * 0.45f));
        else if (i == kMicrotonicPatchOscDcyIndex || i == kMicrotonicPatchNEnvDcyIndex)
            normalized = juce::jlimit(0.0f, 1.0f, normalized + (macroLength * 0.35f));
        else if (i == kMicrotonicPatchNFilFrqIndex)
            normalized = juce::jlimit(0.0f, 1.0f, normalized + (macroFilter * 0.45f));

        if (quantizeBeatSpacePitch && i == kMicrotonicPatchOscFreqIndex)
        {
            normalized = quantizeBeatSpaceOscFreqPreserveOctave(
                normalized,
                params[paramIndex]->getNumSteps(),
                kitScale,
                kitRoot);
        }
        const int steps = params[paramIndex]->getNumSteps();
        if (steps > 1 && steps <= 4096)
        {
            const float denom = static_cast<float>(steps - 1);
            if (denom > 0.0f)
                normalized = std::round(normalized * denom) / denom;
        }

        params[paramIndex]->setValueNotifyingHost(normalized);
        const float readBack = params[paramIndex]->getValue();
        maxError = juce::jmax(maxError, std::abs(readBack - normalized));
        appliedValues[static_cast<size_t>(i)] = normalized;
    }

    beatSpaceLastRecallMaxError[idx] = maxError;
    beatSpaceCurrentVectors[idx] = appliedValues;
}

void StepVstHostAudioProcessor::applyBeatSpacePointToChannels(bool applyAllChannels, bool morph)
{
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
        return;
    if (!beatSpaceMappingReady)
    {
        if (!refreshBeatSpaceParameterMap())
            return;
    }
    if (morph)
        updateBeatSpaceMorph();

    if (beatSpaceCategoryConstrainEnabled)
    {
        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
        {
            const auto idx = static_cast<size_t>(channel);
            beatSpaceChannelPoints[idx] = constrainBeatSpacePointForChannel(
                channel,
                beatSpaceChannelPoints[idx]);
        }
    }

    auto applyOne = [this, morph](int channel)
    {
        const auto idx = static_cast<size_t>(channel);
        const auto targetPoint = clampBeatSpacePointToTable(beatSpaceChannelPoints[idx]);
        const int px = targetPoint.x;
        const int py = targetPoint.y;
        const int tableIndex = (py * BeatSpaceTableSize) + px;
        if (tableIndex < 0 || tableIndex >= static_cast<int>(beatSpaceTable.size()))
            return;
        beatSpaceLastAppliedTableIndex[idx] = tableIndex;
        const auto& values = beatSpaceTable[static_cast<size_t>(tableIndex)].values;
        if (morph)
        {
            beginBeatSpaceMorphForChannel(channel, values, targetPoint);
        }
        else
        {
            applyBeatSpaceVectorToChannel(channel, values);
            beatSpaceMorphStartPoints[idx] = targetPoint;
            beatSpaceMorphTargetPoints[idx] = targetPoint;
            beatSpaceMorphCurrentPoints[idx] = targetPoint;
            beatSpaceMorphProgress[idx] = 1.0f;
            beatSpaceMorphActive[idx] = false;
        }
    };

    if (applyAllChannels)
    {
        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
            applyOne(channel);
    }
    else
    {
        applyOne(beatSpaceSelectedChannel);
    }
}

void StepVstHostAudioProcessor::updateBeatSpaceMorph()
{
    if (!beatSpaceDecoderReady)
        return;
    if (!beatSpaceMappingReady)
    {
        if (!refreshBeatSpaceParameterMap())
            return;
    }

    bool anyActive = false;
    for (const auto active : beatSpaceMorphActive)
    {
        if (active)
        {
            anyActive = true;
            break;
        }
    }
    if (!anyActive)
        return;

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double durationMs = juce::jmax(1.0, beatSpaceMorphDurationMs);
    const float t = juce::jlimit(0.0f, 1.0f, static_cast<float>((nowMs - beatSpaceMorphStartTimeMs) / durationMs));
    const float shaped = t * t * (3.0f - (2.0f * t));

    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto idx = static_cast<size_t>(channel);
        if (!beatSpaceMorphActive[idx])
            continue;

        std::array<float, BeatSpaceVectorSize> blended{};
        for (int i = 0; i < BeatSpaceVectorSize; ++i)
        {
            blended[static_cast<size_t>(i)] = juce::jmap(
                shaped,
                beatSpaceMorphStartVectors[idx][static_cast<size_t>(i)],
                beatSpaceMorphTargetVectors[idx][static_cast<size_t>(i)]);
        }
        applyBeatSpaceVectorToChannel(channel, blended);
        beatSpaceMorphProgress[idx] = shaped;
        const auto from = beatSpaceMorphStartPoints[idx];
        const auto to = beatSpaceMorphTargetPoints[idx];
        beatSpaceMorphCurrentPoints[idx] = clampBeatSpacePointToTable({
            static_cast<int>(std::lround(juce::jmap(shaped, static_cast<float>(from.x), static_cast<float>(to.x)))),
            static_cast<int>(std::lround(juce::jmap(shaped, static_cast<float>(from.y), static_cast<float>(to.y))))
        });
        if (t >= 0.9999f)
        {
            beatSpaceMorphProgress[idx] = 1.0f;
            beatSpaceMorphCurrentPoints[idx] = beatSpaceMorphTargetPoints[idx];
            beatSpaceMorphActive[idx] = false;
        }
    }
}

StepVstHostAudioProcessor::BeatSpaceVisualState StepVstHostAudioProcessor::getBeatSpaceVisualState() const
{
    BeatSpaceVisualState state;
    state.decoderReady = beatSpaceDecoderReady;
    state.mappingReady = beatSpaceMappingReady;
    state.microtonicExactMapping = beatSpaceMicrotonicExactMapping;
    state.colorClustersReady = beatSpaceColorClustersReady;
    state.linkAllChannels = beatSpaceLinkAllChannels;
    state.categoryConstrainEnabled = beatSpaceCategoryConstrainEnabled;
    state.confidenceOverlayEnabled = beatSpaceConfidenceOverlayEnabled;
    state.pathOverlayEnabled = beatSpacePathOverlayEnabled;
    state.pathRecordArmedChannel = beatSpacePathRecordArmedChannel;
    state.selectedChannel = beatSpaceSelectedChannel;
    state.tableSize = BeatSpaceTableSize;
    state.zoomLevel = beatSpaceZoomLevel;
    state.viewX = beatSpaceViewX;
    state.viewY = beatSpaceViewY;
    state.viewWidth = getBeatSpaceViewWidth();
    state.viewHeight = getBeatSpaceViewHeight();
    state.morphDurationMs = beatSpaceMorphDurationMs;
    state.statusMessage = beatSpaceStatusMessage;
    state.selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, state.selectedChannel);
    state.channelMapped = beatSpaceChannelMappingReady;
    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto idx = static_cast<size_t>(channel);
        if (state.channelMapped[static_cast<size_t>(channel)])
            ++state.mappedChannels;
        state.maxRecallError = juce::jmax(state.maxRecallError, beatSpaceLastRecallMaxError[idx]);
        state.channelMorphActive[idx] = beatSpaceMorphActive[idx];
        state.channelMorphProgress[idx] = juce::jlimit(0.0f, 1.0f, beatSpaceMorphProgress[idx]);
        state.channelMorphFrom[idx] = beatSpaceMorphStartPoints[idx];
        state.channelMorphTo[idx] = beatSpaceMorphTargetPoints[idx];
        state.channelMorphCurrent[idx] = beatSpaceMorphCurrentPoints[idx];
        if (beatSpaceMorphActive[idx])
            state.anyMorphActive = true;
    }
    state.channelPoints = beatSpaceChannelPoints;
    state.categoryAnchors = beatSpaceCategoryAnchors;
    state.categoryRadiusX = beatSpaceCategoryRegionRadiusX;
    state.categoryRadiusY = beatSpaceCategoryRegionRadiusY;
    state.categoryManual = beatSpaceCategoryAnchorManual;
    state.channelCategoryAssignment = beatSpaceChannelCategoryAssignment;
    state.zoneLockStrength = beatSpaceZoneLockStrength;
    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        int usedCount = 0;
        for (const auto& bookmark : beatSpaceBookmarks[static_cast<size_t>(i)])
        {
            if (bookmark.used)
                ++usedCount;
        }
        state.bookmarkCounts[static_cast<size_t>(i)] = usedCount;

        const auto& path = beatSpacePaths[static_cast<size_t>(i)];
        state.pathPointCounts[static_cast<size_t>(i)] = path.count;
        state.pathLoopBars[static_cast<size_t>(i)] = path.loopBars;
        state.pathPlayModes[static_cast<size_t>(i)] = static_cast<int>(path.playMode);
        state.pathActive[static_cast<size_t>(i)] = path.active;
        state.pathPoints[static_cast<size_t>(i)] = path.points;
    }
    const auto selectedIdx = static_cast<size_t>(state.selectedChannel);
    const auto selectedPointForConfidence = beatSpaceMorphActive[selectedIdx]
        ? beatSpaceMorphCurrentPoints[selectedIdx]
        : beatSpaceChannelPoints[selectedIdx];
    state.selectedConfidence = getBeatSpacePointConfidence(selectedPointForConfidence);
    return state;
}

void StepVstHostAudioProcessor::appendBeatSpaceStateToPresetXml(juce::XmlElement& presetXml,
                                                                 double hostPpqSnapshot,
                                                                 double hostTempoSnapshot,
                                                                 double nowMs) const
{
    auto* beatXml = presetXml.createNewChildElement("BeatSpaceState");
    beatXml->setAttribute(
        "selectedChannel",
        juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel));
    beatXml->setAttribute("linkAllChannels", beatSpaceLinkAllChannels);
    beatXml->setAttribute(
        "pathRecordArmedChannel",
        juce::jlimit(-1, BeatSpaceChannels - 1, beatSpacePathRecordArmedChannel));

    const bool hasHostSync = std::isfinite(hostPpqSnapshot)
        && std::isfinite(hostTempoSnapshot)
        && hostTempoSnapshot > 0.0;
    const double effectiveTempo = hasHostSync
        ? hostTempoSnapshot
        : juce::jmax(1.0, audioEngine ? audioEngine->getCurrentTempo() : 120.0);

    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
    {
        const auto idx = static_cast<size_t>(channel);
        auto* channelXml = beatXml->createNewChildElement("Channel");
        channelXml->setAttribute("index", channel);
        channelXml->setAttribute("pointX", beatSpaceChannelPoints[idx].x);
        channelXml->setAttribute("pointY", beatSpaceChannelPoints[idx].y);

        const auto& path = beatSpacePaths[idx];
        channelXml->setAttribute("pathCount", path.count);
        channelXml->setAttribute("pathActive", path.active);
        channelXml->setAttribute("pathMode", static_cast<int>(path.mode));
        channelXml->setAttribute("pathPlayMode", static_cast<int>(path.playMode));
        channelXml->setAttribute("pathCycleBeats", path.cycleBeats);
        channelXml->setAttribute("pathLoopBars", path.loopBars);
        channelXml->setAttribute("pathPendingQuantizedStart", path.pendingQuantizedStart);

        const double cycleBeats = juce::jlimit(
            0.03125,
            128.0,
            path.cycleBeats > 0.0 ? path.cycleBeats
                                   : ((path.mode == BeatSpacePathMode::QuarterNote) ? 0.25 : 4.0));
        const double cycleMs = juce::jmax(1.0, cycleBeats * (60000.0 / juce::jmax(1.0, effectiveTempo)));
        double phase = 0.0;
        if (path.active && path.count > 1)
        {
            if (hasHostSync && std::isfinite(path.startPpq))
            {
                phase = std::fmod((hostPpqSnapshot - path.startPpq) / juce::jmax(1.0e-6, cycleBeats), 1.0);
            }
            else if (path.startMs > 0.0 && std::isfinite(path.startMs))
            {
                phase = std::fmod((nowMs - path.startMs) / cycleMs, 1.0);
            }
            if (phase < 0.0)
                phase += 1.0;
        }
        channelXml->setAttribute("pathPhase", juce::jlimit(0.0, 1.0, phase));

        for (int p = 0; p < BeatSpacePathMaxPoints; ++p)
        {
            const auto point = path.points[static_cast<size_t>(p)];
            channelXml->setAttribute("pathX_" + juce::String(p), point.x);
            channelXml->setAttribute("pathY_" + juce::String(p), point.y);
        }
    }
}

void StepVstHostAudioProcessor::loadBeatSpaceStateFromPresetXml(const juce::XmlElement& presetXml,
                                                                 double hostPpqSnapshot,
                                                                 double hostTempoSnapshot,
                                                                 double nowMs)
{
    auto* beatXml = presetXml.getChildByName("BeatSpaceState");
    if (beatXml == nullptr)
        return;

    beatSpaceSelectedChannel = juce::jlimit(
        0,
        BeatSpaceChannels - 1,
        beatXml->getIntAttribute("selectedChannel", beatSpaceSelectedChannel));
    beatSpaceLinkAllChannels = beatXml->getBoolAttribute("linkAllChannels", beatSpaceLinkAllChannels);
    beatSpacePathRecordArmedChannel = juce::jlimit(
        -1,
        BeatSpaceChannels - 1,
        beatXml->getIntAttribute("pathRecordArmedChannel", beatSpacePathRecordArmedChannel));

    const bool hasHostSync = std::isfinite(hostPpqSnapshot)
        && std::isfinite(hostTempoSnapshot)
        && hostTempoSnapshot > 0.0;
    const double effectiveTempo = hasHostSync
        ? hostTempoSnapshot
        : juce::jmax(1.0, audioEngine ? audioEngine->getCurrentTempo() : 120.0);

    for (auto* channelXml : beatXml->getChildIterator())
    {
        if (channelXml->getTagName() != "Channel")
            continue;

        const int channel = juce::jlimit(
            0,
            BeatSpaceChannels - 1,
            channelXml->getIntAttribute("index", 0));
        const auto idx = static_cast<size_t>(channel);

        beatSpaceChannelPoints[idx] = clampBeatSpacePointToTable({
            channelXml->getIntAttribute("pointX", beatSpaceChannelPoints[idx].x),
            channelXml->getIntAttribute("pointY", beatSpaceChannelPoints[idx].y)
        });

        auto& path = beatSpacePaths[idx];
        path.count = juce::jlimit(
            0,
            BeatSpacePathMaxPoints,
            channelXml->getIntAttribute("pathCount", path.count));
        path.active = channelXml->getBoolAttribute("pathActive", path.active);
        path.mode = static_cast<BeatSpacePathMode>(juce::jlimit(
            0,
            static_cast<int>(BeatSpacePathMode::OneBar),
            channelXml->getIntAttribute("pathMode", static_cast<int>(path.mode))));
        path.playMode = static_cast<BeatSpacePathPlayMode>(juce::jlimit(
            0,
            static_cast<int>(BeatSpacePathPlayMode::RandomSlice),
            channelXml->getIntAttribute(
                "pathPlayMode",
                static_cast<int>(BeatSpacePathPlayMode::Normal))));
        path.cycleBeats = juce::jlimit(
            0.03125,
            128.0,
            channelXml->getDoubleAttribute("pathCycleBeats", path.cycleBeats));
        path.loopBars = juce::jlimit(0, 32, channelXml->getIntAttribute("pathLoopBars", path.loopBars));
        path.pendingQuantizedStart = channelXml->getBoolAttribute("pathPendingQuantizedStart", false);
        path.recording = false;
        path.recordStartPpq = -1.0;

        for (int p = 0; p < BeatSpacePathMaxPoints; ++p)
        {
            path.points[static_cast<size_t>(p)] = clampBeatSpacePointToTable({
                channelXml->getIntAttribute(
                    "pathX_" + juce::String(p),
                    path.points[static_cast<size_t>(p)].x),
                channelXml->getIntAttribute(
                    "pathY_" + juce::String(p),
                    path.points[static_cast<size_t>(p)].y)
            });
        }

        if (path.count < 2)
            path.active = false;

        const double phase = juce::jlimit(
            0.0,
            1.0,
            channelXml->getDoubleAttribute("pathPhase", 0.0));
        const double cycleBeats = juce::jmax(0.03125, path.cycleBeats);
        const double cycleMs = juce::jmax(1.0, cycleBeats * (60000.0 / juce::jmax(1.0, effectiveTempo)));

        if (path.active && hasHostSync)
            path.startPpq = hostPpqSnapshot - (phase * cycleBeats);
        else
            path.startPpq = -1.0;
        path.startMs = nowMs - (phase * cycleMs);
    }

    for (int i = 0; i < BeatSpaceChannels; ++i)
        beatSpaceChannelPoints[static_cast<size_t>(i)] =
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(i)]);

    if (beatSpaceLinkAllChannels)
    {
        rebuildBeatSpaceLinkedOffsetsFromCurrent(beatSpaceSelectedChannel);
        applyBeatSpaceLinkedChannelOffsets(
            beatSpaceChannelPoints[static_cast<size_t>(beatSpaceSelectedChannel)],
            beatSpaceSelectedChannel);
    }

    applyBeatSpacePointToChannels(true, false);
    requestImmediateUiAndMonomeRefresh();
}

void StepVstHostAudioProcessor::ensureBeatSpaceVisualAssetsReady()
{
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
        return;

    const bool previewReady = beatSpaceTablePreviewImage.isValid()
        && beatSpaceConfidencePreviewImage.isValid()
        && (!beatSpaceColorClustersReady || beatSpaceClusterOverlayImage.isValid());
    if (previewReady && beatSpaceCategoryAnchorsReady)
        return;

    const auto scriptDir = getBeatSpaceScriptDirectory();

    if (!beatSpaceColorClustersReady)
        rebuildBeatSpaceColorClusters(scriptDir, beatSpaceDecoderName);
    if (!beatSpaceCategoryAnchorsReady)
        rebuildBeatSpaceCategoryAnchors();

    const bool needsPreview = !beatSpaceTablePreviewImage.isValid()
        || !beatSpaceConfidencePreviewImage.isValid()
        || (beatSpaceColorClustersReady && !beatSpaceClusterOverlayImage.isValid());
    if (needsPreview)
        rebuildBeatSpaceTablePreviewImage();
}

juce::Image StepVstHostAudioProcessor::getBeatSpaceTablePreviewImage() const
{
    return beatSpaceTablePreviewImage;
}

juce::Image StepVstHostAudioProcessor::getBeatSpaceClusterOverlayImage() const
{
    return beatSpaceClusterOverlayImage;
}

juce::Image StepVstHostAudioProcessor::getBeatSpaceConfidencePreviewImage() const
{
    return beatSpaceConfidencePreviewImage;
}

bool StepVstHostAudioProcessor::isBeatSpaceReady() const
{
    return beatSpaceDecoderReady && beatSpaceMappingReady;
}

void StepVstHostAudioProcessor::normalizeBeatSpacePresetLayoutForSpace(int space)
{
    const int clampedSpace = juce::jlimit(0, BeatSpaceChannels - 1, space);
    const auto idx = static_cast<size_t>(clampedSpace);
    auto& order = beatSpaceCategoryPresetOrder[idx];
    auto& hidden = beatSpaceCategoryPresetHidden[idx];

    std::array<int, BeatSpacePresetSlotsPerSpace> normalized{};
    std::array<bool, BeatSpacePresetSlotsPerSpace> used{};
    used.fill(false);

    int write = 0;
    for (int pos = 0; pos < BeatSpacePresetSlotsPerSpace; ++pos)
    {
        const int slot = order[static_cast<size_t>(pos)];
        if (slot < 0 || slot >= BeatSpacePresetSlotsPerSpace)
            continue;
        if (used[static_cast<size_t>(slot)])
            continue;
        normalized[static_cast<size_t>(write++)] = slot;
        used[static_cast<size_t>(slot)] = true;
    }

    for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
    {
        if (used[static_cast<size_t>(slot)])
            continue;
        normalized[static_cast<size_t>(write++)] = slot;
    }

    for (int i = 0; i < BeatSpacePresetSlotsPerSpace; ++i)
        order[static_cast<size_t>(i)] = normalized[static_cast<size_t>(i)];

    int visibleCount = 0;
    for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
    {
        const bool isHidden = hidden[static_cast<size_t>(slot)];
        if (!isHidden)
            ++visibleCount;
    }

    if (visibleCount <= 0)
        hidden[static_cast<size_t>(order.front())] = false;
}

std::vector<int> StepVstHostAudioProcessor::getBeatSpaceVisiblePresetSlotsForSpace(int space) const
{
    const int clampedSpace = juce::jlimit(0, BeatSpaceChannels - 1, space);
    const auto idx = static_cast<size_t>(clampedSpace);
    const auto& order = beatSpaceCategoryPresetOrder[idx];
    const auto& hidden = beatSpaceCategoryPresetHidden[idx];

    std::vector<int> visible;
    visible.reserve(BeatSpacePresetSlotsPerSpace);
    std::array<bool, BeatSpacePresetSlotsPerSpace> seen{};
    seen.fill(false);

    for (int pos = 0; pos < BeatSpacePresetSlotsPerSpace; ++pos)
    {
        const int slot = order[static_cast<size_t>(pos)];
        if (slot < 0 || slot >= BeatSpacePresetSlotsPerSpace)
            continue;
        if (seen[static_cast<size_t>(slot)])
            continue;
        seen[static_cast<size_t>(slot)] = true;
        if (!hidden[static_cast<size_t>(slot)])
            visible.push_back(slot);
    }

    for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
    {
        if (seen[static_cast<size_t>(slot)])
            continue;
        if (!hidden[static_cast<size_t>(slot)])
            visible.push_back(slot);
    }

    return visible;
}

bool StepVstHostAudioProcessor::loadBeatSpacePresetFromAssignedSpace(int channel, int presetSlot)
{
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
        return false;

    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int clampedSlot = juce::jlimit(0, BeatSpacePresetSlotsPerSpace - 1, presetSlot);

    if (!beatSpaceCategoryAnchorsReady)
        rebuildBeatSpaceCategoryAnchors();

    const int assignedSpace = getBeatSpaceChannelSpaceAssignment(clampedChannel);
    if (!beatSpaceCategoryPresetPointsReady[static_cast<size_t>(assignedSpace)])
        rebuildBeatSpaceCategoryPresetPoints();

    auto targetPoint = beatSpaceCategoryPresetPoints[static_cast<size_t>(assignedSpace)]
        [static_cast<size_t>(clampedSlot)];
    if (!beatSpaceCategoryPresetPointsReady[static_cast<size_t>(assignedSpace)])
    {
        // Fallback to anchor if analysis table was not available for this category.
        targetPoint = beatSpaceCategoryAnchorsReady
            ? beatSpaceCategoryAnchors[static_cast<size_t>(assignedSpace)]
            : juce::Point<int> { BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 };
    }
    targetPoint = constrainBeatSpacePointForChannel(clampedChannel, targetPoint);

    // Strip preset picker always targets only the selected channel, even when Link-All is enabled.
    beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)] = targetPoint;
    const int previousSelected = beatSpaceSelectedChannel;
    beatSpaceSelectedChannel = clampedChannel;
    applyBeatSpacePointToChannels(false, true);
    beatSpaceSelectedChannel = previousSelected;
    requestImmediateUiAndMonomeRefresh();

    return true;
}

bool StepVstHostAudioProcessor::loadBeatSpacePresetFromAssignedSpaceDisplayIndex(int channel, int displayIndex)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int assignedSpace = getBeatSpaceChannelSpaceAssignment(clampedChannel);
    const auto visible = getBeatSpaceVisiblePresetSlotsForSpace(assignedSpace);
    if (visible.empty())
        return false;

    const int clampedDisplay = juce::jlimit(0, static_cast<int>(visible.size()) - 1, displayIndex);
    const bool loaded = loadBeatSpacePresetFromAssignedSpace(
        clampedChannel,
        visible[static_cast<size_t>(clampedDisplay)]);
    if (loaded)
        beatSpaceLastLoadedDisplayPresetIndex[static_cast<size_t>(clampedChannel)] = clampedDisplay;
    return loaded;
}

bool StepVstHostAudioProcessor::loadAdjacentBeatSpacePresetForAssignedSpace(int channel, int direction)
{
    if (channel < 0 || channel >= BeatSpaceChannels)
        return false;

    const int clampedChannel = channel;
    const auto displaySlots = getBeatSpacePresetDisplaySlotsForAssignedSpace(clampedChannel);
    if (displaySlots.empty())
        return false;

    const int count = static_cast<int>(displaySlots.size());
    const auto idx = static_cast<size_t>(clampedChannel);
    int currentDisplay = beatSpaceLastLoadedDisplayPresetIndex[idx];

    if (currentDisplay < 0 || currentDisplay >= count)
    {
        const int assignedSpace = getBeatSpaceChannelSpaceAssignment(clampedChannel);
        if (!beatSpaceCategoryPresetPointsReady[static_cast<size_t>(assignedSpace)])
            rebuildBeatSpaceCategoryPresetPoints();

        const auto currentPoint =
            constrainBeatSpacePointForChannel(clampedChannel, beatSpaceChannelPoints[idx]);
        float bestDistance = std::numeric_limits<float>::max();
        int bestDisplay = 0;
        for (int displayIndex = 0; displayIndex < count; ++displayIndex)
        {
            const int slot = displaySlots[static_cast<size_t>(displayIndex)];
            const auto point = beatSpaceCategoryPresetPoints[static_cast<size_t>(assignedSpace)]
                [static_cast<size_t>(juce::jlimit(0, BeatSpacePresetSlotsPerSpace - 1, slot))];
            const float dx = static_cast<float>(point.x - currentPoint.x);
            const float dy = static_cast<float>(point.y - currentPoint.y);
            const float distance = (dx * dx) + (dy * dy);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestDisplay = displayIndex;
            }
        }
        currentDisplay = bestDisplay;
    }

    const int step = (direction >= 0) ? 1 : -1;
    int targetDisplay = currentDisplay + step;
    while (targetDisplay < 0)
        targetDisplay += count;
    while (targetDisplay >= count)
        targetDisplay -= count;

    return loadBeatSpacePresetFromAssignedSpaceDisplayIndex(clampedChannel, targetDisplay);
}

juce::String StepVstHostAudioProcessor::getBeatSpaceSpaceName(int space)
{
    const int clampedSpace = juce::jlimit(0, BeatSpaceChannels - 1, space);
    return kBeatSpaceSpaceNames[static_cast<size_t>(clampedSpace)];
}

int StepVstHostAudioProcessor::getBeatSpaceChannelSpaceAssignment(int channel) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    return juce::jlimit(
        0,
        BeatSpaceChannels - 1,
        beatSpaceChannelCategoryAssignment[static_cast<size_t>(clampedChannel)]);
}

void StepVstHostAudioProcessor::setBeatSpaceChannelSpaceAssignment(int channel, int space)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int clampedSpace = juce::jlimit(0, BeatSpaceChannels - 1, space);
    auto& assignment = beatSpaceChannelCategoryAssignment[static_cast<size_t>(clampedChannel)];
    if (assignment == clampedSpace)
        return;

    assignment = clampedSpace;
    beatSpaceLastLoadedDisplayPresetIndex[static_cast<size_t>(clampedChannel)] = -1;

    if (beatSpaceLinkAllChannels)
    {
        const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        if (!beatSpaceLinkedOffsetsReady)
            rebuildBeatSpaceLinkedOffsetsFromCurrent(selectedChannel);

        if (beatSpaceCategoryAnchorsReady)
        {
            const int masterSpace = juce::jlimit(
                0,
                BeatSpaceChannels - 1,
                beatSpaceChannelCategoryAssignment[static_cast<size_t>(selectedChannel)]);
            const auto masterAnchor = beatSpaceCategoryAnchors[static_cast<size_t>(masterSpace)];
            const auto channelAnchor = beatSpaceCategoryAnchors[static_cast<size_t>(clampedSpace)];
            beatSpaceLinkedOffsets[static_cast<size_t>(clampedChannel)] = {
                static_cast<float>(channelAnchor.x - masterAnchor.x),
                static_cast<float>(channelAnchor.y - masterAnchor.y)
            };
            if (clampedChannel == selectedChannel)
                beatSpaceLinkedOffsets[static_cast<size_t>(selectedChannel)] = { 0.0f, 0.0f };
        }

        applyBeatSpaceLinkedChannelOffsets(
            beatSpaceChannelPoints[static_cast<size_t>(selectedChannel)],
            selectedChannel);
        applyBeatSpacePointToChannels(true, false);
    }
    else
    {
        beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)] =
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)]);
        applyBeatSpacePointToChannels(true, false);
    }

    savePersistentControlPages();
}

std::vector<int> StepVstHostAudioProcessor::getBeatSpacePresetDisplaySlotsForAssignedSpace(int channel) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int assignedSpace = getBeatSpaceChannelSpaceAssignment(clampedChannel);
    return getBeatSpaceVisiblePresetSlotsForSpace(assignedSpace);
}

juce::String StepVstHostAudioProcessor::getBeatSpacePresetLabelForSpaceSlot(int space, int slot) const
{
    const int clampedSpace = juce::jlimit(0, BeatSpaceChannels - 1, space);
    const int clampedSlot = juce::jlimit(0, BeatSpacePresetSlotsPerSpace - 1, slot);
    const auto idx = static_cast<size_t>(clampedSpace);
    const auto slotIdx = static_cast<size_t>(clampedSlot);
    const auto custom = beatSpaceCategoryPresetLabels[idx][slotIdx].trim();
    if (custom.isNotEmpty())
        return custom;

    if (beatSpaceCategoryPresetPointsReady[idx])
    {
        const auto point = beatSpaceCategoryPresetPoints[idx][slotIdx];
        const int tableIndex = (point.y * BeatSpaceTableSize) + point.x;
        if (tableIndex >= 0 && tableIndex < static_cast<int>(beatSpaceTable.size()))
            return describeBeatSpaceCategoryPresetPoint(
                clampedSpace,
                beatSpaceTable[static_cast<size_t>(tableIndex)].values);
    }

    return getBeatSpaceSpaceName(clampedSpace) + " "
        + juce::String(clampedSlot + 1).paddedLeft('0', 2);
}

bool StepVstHostAudioProcessor::moveBeatSpacePresetForAssignedSpace(int channel, int fromDisplayIndex, int toDisplayIndex)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int assignedSpace = getBeatSpaceChannelSpaceAssignment(clampedChannel);
    const auto idx = static_cast<size_t>(assignedSpace);
    normalizeBeatSpacePresetLayoutForSpace(assignedSpace);

    auto visible = getBeatSpaceVisiblePresetSlotsForSpace(assignedSpace);
    if (visible.size() < 2)
        return false;

    const int from = juce::jlimit(0, static_cast<int>(visible.size()) - 1, fromDisplayIndex);
    const int to = juce::jlimit(0, static_cast<int>(visible.size()) - 1, toDisplayIndex);
    if (from == to)
        return true;

    const int movingSlot = visible[static_cast<size_t>(from)];
    visible.erase(visible.begin() + from);
    visible.insert(visible.begin() + to, movingSlot);

    std::array<int, BeatSpacePresetSlotsPerSpace> newOrder{};
    std::array<bool, BeatSpacePresetSlotsPerSpace> used{};
    used.fill(false);
    int write = 0;

    for (const int slot : visible)
    {
        if (slot < 0 || slot >= BeatSpacePresetSlotsPerSpace)
            continue;
        if (used[static_cast<size_t>(slot)])
            continue;
        newOrder[static_cast<size_t>(write++)] = slot;
        used[static_cast<size_t>(slot)] = true;
    }

    const auto oldOrder = beatSpaceCategoryPresetOrder[idx];
    for (int pos = 0; pos < BeatSpacePresetSlotsPerSpace; ++pos)
    {
        const int slot = oldOrder[static_cast<size_t>(pos)];
        if (slot < 0 || slot >= BeatSpacePresetSlotsPerSpace)
            continue;
        if (used[static_cast<size_t>(slot)])
            continue;
        newOrder[static_cast<size_t>(write++)] = slot;
        used[static_cast<size_t>(slot)] = true;
    }

    for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
    {
        if (used[static_cast<size_t>(slot)])
            continue;
        newOrder[static_cast<size_t>(write++)] = slot;
    }

    beatSpaceCategoryPresetOrder[idx] = newOrder;
    normalizeBeatSpacePresetLayoutForSpace(assignedSpace);
    savePersistentControlPages();
    return true;
}

bool StepVstHostAudioProcessor::renameBeatSpacePresetForAssignedSpace(int channel, int displayIndex, const juce::String& label)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int assignedSpace = getBeatSpaceChannelSpaceAssignment(clampedChannel);
    const auto idx = static_cast<size_t>(assignedSpace);

    const auto visible = getBeatSpaceVisiblePresetSlotsForSpace(assignedSpace);
    if (visible.empty())
        return false;

    const int clampedDisplay = juce::jlimit(0, static_cast<int>(visible.size()) - 1, displayIndex);
    const int slot = visible[static_cast<size_t>(clampedDisplay)];
    beatSpaceCategoryPresetLabels[idx][static_cast<size_t>(slot)] = label.trim();
    savePersistentControlPages();
    return true;
}

bool StepVstHostAudioProcessor::deleteBeatSpacePresetForAssignedSpace(int channel, int displayIndex)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int assignedSpace = getBeatSpaceChannelSpaceAssignment(clampedChannel);
    const auto idx = static_cast<size_t>(assignedSpace);

    auto visible = getBeatSpaceVisiblePresetSlotsForSpace(assignedSpace);
    if (visible.size() <= 1)
        return false;

    const int clampedDisplay = juce::jlimit(0, static_cast<int>(visible.size()) - 1, displayIndex);
    const int slot = visible[static_cast<size_t>(clampedDisplay)];
    beatSpaceCategoryPresetHidden[idx][static_cast<size_t>(slot)] = true;
    normalizeBeatSpacePresetLayoutForSpace(assignedSpace);
    savePersistentControlPages();
    return true;
}

void StepVstHostAudioProcessor::resetBeatSpacePresetLayoutForAssignedSpace(int channel)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int assignedSpace = getBeatSpaceChannelSpaceAssignment(clampedChannel);
    const auto idx = static_cast<size_t>(assignedSpace);

    for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
    {
        beatSpaceCategoryPresetOrder[idx][static_cast<size_t>(slot)] = slot;
        beatSpaceCategoryPresetHidden[idx][static_cast<size_t>(slot)] = false;
        beatSpaceCategoryPresetLabels[idx][static_cast<size_t>(slot)] = {};
    }
    normalizeBeatSpacePresetLayoutForSpace(assignedSpace);
    savePersistentControlPages();
}

bool StepVstHostAudioProcessor::captureCurrentMicrotonicPatchValues(
    int channel,
    std::array<float, BeatSpacePatchParamCount>& outValues,
    juce::String* errorOut)
{
    auto setError = [errorOut](const juce::String& message)
    {
        if (errorOut != nullptr)
            *errorOut = message;
    };

    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    auto captureFromCurrentVector = [&]() -> bool
    {
        const auto idx = static_cast<size_t>(clampedChannel);
        for (int patch = 0; patch < BeatSpacePatchParamCount; ++patch)
        {
            const float value = beatSpaceCurrentVectors[idx][static_cast<size_t>(patch)];
            if (!std::isfinite(value))
                return false;
            outValues[static_cast<size_t>(patch)] = juce::jlimit(0.0f, 1.0f, value);
        }
        return true;
    };

    auto* instance = hostRack.getInstance();
    if (instance == nullptr)
    {
        if (captureFromCurrentVector())
            return true;
        setError("No hosted plugin loaded");
        return false;
    }

    if (!refreshBeatSpaceParameterMap(false))
    {
        if (captureFromCurrentVector())
            return true;
        setError("Microtonic parameter mapping unavailable");
        return false;
    }

    const auto idx = static_cast<size_t>(clampedChannel);
    if (!beatSpaceChannelMappingReady[idx])
    {
        if (captureFromCurrentVector())
            return true;
        setError("Lane " + juce::String(clampedChannel + 1) + " is not mapped");
        return false;
    }

    const auto& params = instance->getParameters();
    for (int patch = 0; patch < BeatSpacePatchParamCount; ++patch)
    {
        const int paramIndex = beatSpaceParamMap[idx][static_cast<size_t>(patch)];
        if (paramIndex < 0 || paramIndex >= static_cast<int>(params.size()))
        {
            if (captureFromCurrentVector())
                return true;
            setError("Patch parameter mapping incomplete");
            return false;
        }
        outValues[static_cast<size_t>(patch)] = juce::jlimit(0.0f, 1.0f, params[paramIndex]->getValue());
    }

    return true;
}

bool StepVstHostAudioProcessor::applyMicrotonicPatchValues(
    int channel,
    const std::array<float, BeatSpacePatchParamCount>& values,
    juce::String* errorOut)
{
    auto setError = [errorOut](const juce::String& message)
    {
        if (errorOut != nullptr)
            *errorOut = message;
    };

    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    auto* instance = hostRack.getInstance();
    if (instance == nullptr)
    {
        setError("No hosted plugin loaded");
        return false;
    }

    if (!refreshBeatSpaceParameterMap(false))
    {
        setError("Microtonic parameter mapping unavailable");
        return false;
    }

    const auto idx = static_cast<size_t>(clampedChannel);
    if (!beatSpaceChannelMappingReady[idx])
    {
        setError("Lane " + juce::String(clampedChannel + 1) + " is not mapped");
        return false;
    }

    const auto& params = instance->getParameters();
    float maxError = 0.0f;
    for (int patch = 0; patch < BeatSpacePatchParamCount; ++patch)
    {
        const int paramIndex = beatSpaceParamMap[idx][static_cast<size_t>(patch)];
        if (paramIndex < 0 || paramIndex >= static_cast<int>(params.size()))
        {
            setError("Patch parameter mapping incomplete");
            return false;
        }

        const float normalized = juce::jlimit(0.0f, 1.0f, values[static_cast<size_t>(patch)]);
        params[paramIndex]->setValueNotifyingHost(normalized);
        const float readBack = params[paramIndex]->getValue();
        maxError = juce::jmax(maxError, std::abs(readBack - normalized));
        beatSpaceCurrentVectors[idx][static_cast<size_t>(patch)] = normalized;
    }

    beatSpaceLastRecallMaxError[idx] = maxError;
    return true;
}

std::vector<int> StepVstHostAudioProcessor::getMicrotonicStripPresetSlots(int channel) const
{
    juce::ignoreUnused(channel);
    std::vector<int> slots;
    for (int slot = 0; slot < MicrotonicStripPresetSlots; ++slot)
    {
        bool slotUsed = false;
        for (int lane = 0; lane < BeatSpaceChannels; ++lane)
        {
            if (microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(slot)].used)
            {
                slotUsed = true;
                break;
            }
        }
        if (slotUsed)
            slots.push_back(slot);
    }
    return slots;
}

juce::String StepVstHostAudioProcessor::getMicrotonicStripPresetName(int channel, int slot) const
{
    juce::ignoreUnused(channel);
    const int clampedSlot = juce::jlimit(0, MicrotonicStripPresetSlots - 1, slot);
    for (int lane = 0; lane < BeatSpaceChannels; ++lane)
    {
        const auto& preset = microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(clampedSlot)];
        if (!preset.used)
            continue;
        const auto trimmed = preset.name.trim();
        if (trimmed.isNotEmpty())
            return trimmed;
    }

    bool slotUsed = false;
    for (int lane = 0; lane < BeatSpaceChannels; ++lane)
    {
        if (microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(clampedSlot)].used)
        {
            slotUsed = true;
            break;
        }
    }
    if (!slotUsed)
        return {};

    return "Kit " + juce::String(clampedSlot + 1).paddedLeft('0', 2);
}

std::vector<StepVstHostAudioProcessor::SiteDrumPatternPresetInfo> StepVstHostAudioProcessor::getSiteDrumPatternPresetInfos() const
{
    std::vector<SiteDrumPatternPresetInfo> infos;
    infos.reserve(kSiteDrumPatternPresets.size());
    for (const auto& preset : kSiteDrumPatternPresets)
    {
        infos.push_back({
            preset.source,
            preset.genre,
            preset.name,
            preset.bpm
        });
    }
    return infos;
}

bool StepVstHostAudioProcessor::applySiteDrumPatternPreset(int presetIndex, juce::String* errorOut)
{
    if (presetIndex < 0 || presetIndex >= static_cast<int>(kSiteDrumPatternPresets.size()))
    {
        if (errorOut != nullptr)
            *errorOut = "Invalid drum pattern selection.";
        return false;
    }

    if (audioEngine == nullptr)
    {
        if (errorOut != nullptr)
            *errorOut = "Audio engine is not ready.";
        return false;
    }

    const auto& preset = kSiteDrumPatternPresets[static_cast<size_t>(presetIndex)];
    const int stepCount = juce::jlimit(2, 64, preset.steps);
    std::array<std::vector<bool>, BeatSpaceChannels> categoryMasks{};
    std::array<std::vector<float>, BeatSpaceChannels> categoryVelocityMasks{};
    for (int category = 0; category < BeatSpaceChannels; ++category)
    {
        const auto categoryIdx = static_cast<size_t>(category);
        categoryMasks[categoryIdx] = parseStepMask(
            preset.categoryMasks[categoryIdx],
            stepCount);
        categoryVelocityMasks[categoryIdx] = parseStepVelocityMask(
            preset.categoryVelocityMasks[categoryIdx],
            stepCount);

        bool hasVelocityData = false;
        for (int step = 0; step < stepCount; ++step)
        {
            if (categoryVelocityMasks[categoryIdx][static_cast<size_t>(step)] > 0.0f)
            {
                hasVelocityData = true;
                break;
            }
        }

        for (int step = 0; step < stepCount; ++step)
        {
            const auto stepIdx = static_cast<size_t>(step);
            const bool enabled = categoryMasks[categoryIdx][stepIdx];
            float velocity = categoryVelocityMasks[categoryIdx][stepIdx];
            if (!hasVelocityData)
                velocity = enabled ? 1.0f : 0.0f;
            else if (enabled && velocity <= 0.0f)
                velocity = 1.0f;
            categoryVelocityMasks[categoryIdx][stepIdx] = juce::jlimit(0.0f, 1.0f, velocity);
        }
    }

    bool appliedAnyStrip = false;
    for (int stripIndex = 0; stripIndex < BeatSpaceChannels; ++stripIndex)
    {
        auto* strip = audioEngine->getStrip(stripIndex);
        if (strip == nullptr)
            continue;

        const int category = juce::jlimit(
            0,
            BeatSpaceChannels - 1,
            getBeatSpaceChannelSpaceAssignment(stripIndex));
        const auto& mask = categoryMasks[static_cast<size_t>(category)];
        const auto& velocityMask = categoryVelocityMasks[static_cast<size_t>(category)];

        strip->setStepPatternLengthSteps(stepCount);
        strip->setStepPage(0);
        for (int step = 0; step < stepCount; ++step)
        {
            const bool enabled = mask[static_cast<size_t>(step)];
            const float velocity = enabled
                ? velocityMask[static_cast<size_t>(step)]
                : 0.0f;
            strip->setStepSubdivisionAtIndex(step, 1);
            strip->setStepProbabilityAtIndex(step, 1.0f);
            strip->setStepEnabledAtIndex(step, enabled, false);
            strip->setStepSubdivisionVelocityRangeAtIndex(step, velocity, velocity);
        }

        appliedAnyStrip = true;
    }

    if (!appliedAnyStrip)
    {
        if (errorOut != nullptr)
            *errorOut = "No strip channels are available for pattern apply.";
        return false;
    }

    requestImmediateUiAndMonomeRefresh();
    return true;
}

bool StepVstHostAudioProcessor::applySiteDrumPatternPresetToCurrentSubPreset(
    int presetIndex, juce::String* errorOut)
{
    if (!applySiteDrumPatternPreset(presetIndex, errorOut))
        return false;

    const int mainPresetIndex = getActiveMainPresetIndexForSubPresets();
    const int subPresetSlot = juce::jlimit(0, SubPresetSlots - 1, activeSubPresetSlot);
    if (!saveSubPresetForMainPreset(mainPresetIndex, subPresetSlot))
    {
        if (errorOut != nullptr)
            *errorOut = "Pattern was applied, but saving current sub preset failed.";
        return false;
    }

    return true;
}

bool StepVstHostAudioProcessor::storeCurrentPatternToCurrentSubPreset(juce::String* errorOut)
{
    const int mainPresetIndex = getActiveMainPresetIndexForSubPresets();
    const int subPresetSlot = juce::jlimit(0, SubPresetSlots - 1, activeSubPresetSlot);
    if (!saveSubPresetForMainPreset(mainPresetIndex, subPresetSlot))
    {
        if (errorOut != nullptr)
            *errorOut = "Saving current sub preset failed.";
        return false;
    }

    return true;
}

bool StepVstHostAudioProcessor::clearCurrentPatternInCurrentSubPreset(juce::String* errorOut)
{
    if (!audioEngine)
    {
        if (errorOut != nullptr)
            *errorOut = "Audio engine unavailable.";
        return false;
    }

    bool anyStrip = false;
    for (int stripIndex = 0; stripIndex < BeatSpaceChannels; ++stripIndex)
    {
        auto* strip = audioEngine->getStrip(stripIndex);
        if (strip == nullptr)
            continue;

        const int stepCount = juce::jlimit(2, 64, strip->getStepPatternLengthSteps());
        strip->setStepPage(0);
        for (int step = 0; step < stepCount; ++step)
        {
            strip->setStepSubdivisionAtIndex(step, 1);
            strip->setStepProbabilityAtIndex(step, 1.0f);
            strip->setStepSubdivisionVelocityRangeAtIndex(step, 0.0f, 0.0f);
            strip->setStepEnabledAtIndex(step, false, false);
        }

        anyStrip = true;
    }

    if (!anyStrip)
    {
        if (errorOut != nullptr)
            *errorOut = "No strips available for pattern clear.";
        return false;
    }

    const int mainPresetIndex = getActiveMainPresetIndexForSubPresets();
    const int subPresetSlot = juce::jlimit(0, SubPresetSlots - 1, activeSubPresetSlot);
    if (!saveSubPresetForMainPreset(mainPresetIndex, subPresetSlot))
    {
        if (errorOut != nullptr)
            *errorOut = "Pattern was cleared, but saving current sub preset failed.";
        return false;
    }

    requestImmediateUiAndMonomeRefresh();
    return true;
}

bool StepVstHostAudioProcessor::storeCurrentMicrotonicStripPreset(
    int channel,
    int preferredSlot,
    const juce::String& customName,
    int* storedSlotOut,
    juce::String* errorOut)
{
    cancelPendingBeatSpaceStartupApply();

    juce::ignoreUnused(channel);

    int targetSlot = (preferredSlot >= 0 && preferredSlot < MicrotonicStripPresetSlots)
        ? preferredSlot
        : -1;
    if (targetSlot < 0)
    {
        for (int slot = 0; slot < MicrotonicStripPresetSlots; ++slot)
        {
            bool slotUsed = false;
            for (int lane = 0; lane < BeatSpaceChannels; ++lane)
            {
                if (microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(slot)].used)
                {
                    slotUsed = true;
                    break;
                }
            }
            if (!slotUsed)
            {
                targetSlot = slot;
                break;
            }
        }
    }

    if (targetSlot < 0 || targetSlot >= MicrotonicStripPresetSlots)
    {
        if (errorOut != nullptr)
            *errorOut = "No free kit preset slots";
        return false;
    }

    std::array<std::array<float, BeatSpacePatchParamCount>, BeatSpaceChannels> valuesByLane{};
    for (int lane = 0; lane < BeatSpaceChannels; ++lane)
    {
        juce::String laneError;
        if (!captureCurrentMicrotonicPatchValues(
                lane,
                valuesByLane[static_cast<size_t>(lane)],
                &laneError))
        {
            if (errorOut != nullptr)
            {
                *errorOut = "Lane " + juce::String(lane + 1)
                    + ": " + (laneError.isNotEmpty() ? laneError : juce::String("capture failed"));
            }
            return false;
        }
    }

    const auto trimmedCustomName = customName.trim();
    juce::String resolvedName = trimmedCustomName;
    if (resolvedName.isEmpty())
        resolvedName = getMicrotonicStripPresetName(0, targetSlot).trim();
    if (resolvedName.isEmpty())
        resolvedName = "Kit Patch " + juce::String(targetSlot + 1).paddedLeft('0', 2);

    for (int lane = 0; lane < BeatSpaceChannels; ++lane)
    {
        auto& preset = microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(targetSlot)];
        preset.used = true;
        preset.name = resolvedName;
        preset.patchValues = valuesByLane[static_cast<size_t>(lane)];
        preset.hasSavedBeatSpacePoint = true;
        preset.beatSpacePoint =
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(lane)]);
    }
    savePersistentControlPages();

    if (storedSlotOut != nullptr)
        *storedSlotOut = targetSlot;
    return true;
}

bool StepVstHostAudioProcessor::recallMicrotonicStripPreset(int channel, int slot, juce::String* errorOut)
{
    cancelPendingBeatSpaceStartupApply();

    juce::ignoreUnused(channel);
    const int clampedSlot = juce::jlimit(0, MicrotonicStripPresetSlots - 1, slot);
    bool anyUsed = false;
    for (int lane = 0; lane < BeatSpaceChannels; ++lane)
    {
        if (microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(clampedSlot)].used)
        {
            anyUsed = true;
            break;
        }
    }
    if (!anyUsed)
    {
        if (errorOut != nullptr)
            *errorOut = "Preset slot is empty";
        return false;
    }

    for (int lane = 0; lane < BeatSpaceChannels; ++lane)
    {
        const auto& preset = microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(clampedSlot)];
        if (!preset.used)
            continue;

        juce::String laneError;
        if (!applyMicrotonicPatchValues(lane, preset.patchValues, &laneError))
        {
            if (errorOut != nullptr)
            {
                *errorOut = "Lane " + juce::String(lane + 1)
                    + ": " + (laneError.isNotEmpty() ? laneError : juce::String("recall failed"));
            }
            return false;
        }
    }

    // New favorites store exact BeatSpace positions per lane. Restore those
    // positions when available; keep slot-based fallback for older presets.
    bool canRestoreSavedPoints = true;
    bool anySavedPoints = false;
    std::array<juce::Point<int>, BeatSpaceChannels> targetPoints{};
    for (int lane = 0; lane < BeatSpaceChannels; ++lane)
    {
        const auto& preset = microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(clampedSlot)];
        if (!preset.used)
            continue;
        if (!preset.hasSavedBeatSpacePoint)
        {
            canRestoreSavedPoints = false;
            break;
        }

        anySavedPoints = true;
        targetPoints[static_cast<size_t>(lane)] =
            constrainBeatSpacePointForChannel(lane, preset.beatSpacePoint);
    }

    if (canRestoreSavedPoints && anySavedPoints)
    {
        pendingFavoritesBeatSpaceJump.store(0, std::memory_order_release);
        pendingFavoritesBeatSpaceJumpSlot.store(-1, std::memory_order_release);

        bool anyMoved = false;
        for (int lane = 0; lane < BeatSpaceChannels; ++lane)
        {
            const auto& preset = microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(clampedSlot)];
            if (!preset.used)
                continue;

            const auto idx = static_cast<size_t>(lane);
            const auto previous = clampBeatSpacePointToTable(beatSpaceChannelPoints[idx]);
            const auto target = clampBeatSpacePointToTable(targetPoints[idx]);
            if (target == previous)
                continue;

            anyMoved = true;
            beatSpaceChannelPoints[idx] = target;

            auto& path = beatSpacePaths[idx];
            if (path.count <= 0)
                continue;

            const int dx = target.x - previous.x;
            const int dy = target.y - previous.y;
            if (dx == 0 && dy == 0)
                continue;

            for (int p = 0; p < path.count && p < BeatSpacePathMaxPoints; ++p)
            {
                auto shifted = path.points[static_cast<size_t>(p)];
                shifted.x += dx;
                shifted.y += dy;
                path.points[static_cast<size_t>(p)] = shifted;
            }
        }

        if (anyMoved)
        {
            if (beatSpaceLinkAllChannels)
            {
                const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
                rebuildBeatSpaceLinkedOffsetsFromCurrent(selectedChannel);
            }

            applyBeatSpacePointToChannels(true, false);
            requestImmediateUiAndMonomeRefresh();
        }
    }
    else
    {
        // Legacy fallback: align bubbles to the slot-mapped category anchors.
        pendingFavoritesBeatSpaceJumpSlot.store(clampedSlot, std::memory_order_release);
        pendingFavoritesBeatSpaceJump.store(1, std::memory_order_release);
        applyPendingFavoritesBeatSpaceJump();
    }

    return true;
}

bool StepVstHostAudioProcessor::deleteMicrotonicStripPreset(int channel, int slot)
{
    cancelPendingBeatSpaceStartupApply();

    juce::ignoreUnused(channel);
    const int clampedSlot = juce::jlimit(0, MicrotonicStripPresetSlots - 1, slot);
    bool deletedAny = false;
    for (int lane = 0; lane < BeatSpaceChannels; ++lane)
    {
        auto& preset = microtonicStripPresets[static_cast<size_t>(lane)][static_cast<size_t>(clampedSlot)];
        if (!preset.used)
            continue;
        preset = MicrotonicStripPreset{};
        deletedAny = true;
    }

    if (!deletedAny)
        return false;

    savePersistentControlPages();
    return true;
}

void StepVstHostAudioProcessor::beatSpaceSelectChannel(int channel)
{
    const int requested = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    if (beatSpaceChannelMappingReady[static_cast<size_t>(requested)])
    {
        const bool selectionChanged = (beatSpaceSelectedChannel != requested);
        beatSpaceSelectedChannel = requested;
        if (selectionChanged && beatSpaceLinkAllChannels)
            reanchorBeatSpaceLinkedOffsets(beatSpaceSelectedChannel);
        return;
    }

    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        if (beatSpaceChannelMappingReady[static_cast<size_t>(i)])
        {
            const bool selectionChanged = (beatSpaceSelectedChannel != i);
            beatSpaceSelectedChannel = i;
            if (selectionChanged && beatSpaceLinkAllChannels)
                reanchorBeatSpaceLinkedOffsets(beatSpaceSelectedChannel);
            return;
        }
    }

    const bool selectionChanged = (beatSpaceSelectedChannel != requested);
    beatSpaceSelectedChannel = requested;
    if (selectionChanged && beatSpaceLinkAllChannels)
        reanchorBeatSpaceLinkedOffsets(beatSpaceSelectedChannel);
}

void StepVstHostAudioProcessor::beatSpaceSetLinkAllChannels(bool shouldLink)
{
    const bool nextLinkState = shouldLink;
    if (beatSpaceLinkAllChannels == nextLinkState)
        return;

    beatSpaceLinkAllChannels = nextLinkState;

    if (!beatSpaceLinkAllChannels)
    {
        beatSpaceLinkedOffsetsReady = false;
        return;
    }

    if (!beatSpaceCategoryAnchorsReady)
        rebuildBeatSpaceCategoryAnchors();

    rebuildBeatSpaceLinkedOffsetsFromCurrent(beatSpaceSelectedChannel);
    const auto anchor = beatSpaceChannelPoints[static_cast<size_t>(beatSpaceSelectedChannel)];
    applyBeatSpaceLinkedChannelOffsets(anchor, beatSpaceSelectedChannel);
    applyBeatSpacePointToChannels(true, false);
}

void StepVstHostAudioProcessor::beatSpaceRandomizeSelection()
{
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
        return;

    if (beatSpaceLinkAllChannels)
    {
        const juce::Point<int> randomMaster =
            randomBeatSpacePointForChannel(beatSpaceSelectedChannel, beatSpaceRandomizeMode);
        applyBeatSpaceLinkedChannelOffsets(randomMaster, beatSpaceSelectedChannel);
    }
    else
    {
        // Match BeatSpace script behavior: randomize every channel, not only the selected one.
        for (int i = 0; i < BeatSpaceChannels; ++i)
            beatSpaceChannelPoints[static_cast<size_t>(i)] =
                randomBeatSpacePointForChannel(i, beatSpaceRandomizeMode);
    }
    applyBeatSpacePointToChannels(true, false);
}

bool StepVstHostAudioProcessor::beatSpaceRandomizeChannel(
    int channel, BeatSpaceRandomizeMode mode, bool applyLinkedIfSelected)
{
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
        return false;

    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const auto p = randomBeatSpacePointForChannel(clampedChannel, mode);
    const bool moveLinked = applyLinkedIfSelected
        && beatSpaceLinkAllChannels
        && clampedChannel == beatSpaceSelectedChannel;
    beatSpaceSetChannelPoint(clampedChannel, p, moveLinked, true);
    return true;
}

void StepVstHostAudioProcessor::setBeatSpaceRandomizeMode(BeatSpaceRandomizeMode mode)
{
    if (beatSpaceRandomizeMode == mode)
        return;
    beatSpaceRandomizeMode = mode;
    savePersistentControlPages();
}

StepVstHostAudioProcessor::BeatSpaceRandomizeMode StepVstHostAudioProcessor::getBeatSpaceRandomizeMode() const
{
    return beatSpaceRandomizeMode;
}

void StepVstHostAudioProcessor::beatSpaceAdjustZoom(int delta)
{
    const int oldViewW = getBeatSpaceViewWidth();
    const int oldViewH = getBeatSpaceViewHeight();
    const auto center = beatSpaceChannelPoints[static_cast<size_t>(beatSpaceSelectedChannel)];
    beatSpaceZoomLevel = juce::jlimit(0, BeatSpaceMaxZoom, beatSpaceZoomLevel + delta);
    const int newViewW = getBeatSpaceViewWidth();
    const int newViewH = getBeatSpaceViewHeight();
    beatSpaceViewX += (oldViewW - newViewW) / 2;
    beatSpaceViewY += (oldViewH - newViewH) / 2;
    beatSpaceViewX = center.x - (newViewW / 2);
    beatSpaceViewY = center.y - (newViewH / 2);
    clampBeatSpaceView();

    if (beatSpaceLinkAllChannels)
    {
        applyBeatSpaceLinkedChannelOffsets(
            beatSpaceChannelPoints[static_cast<size_t>(beatSpaceSelectedChannel)],
            beatSpaceSelectedChannel);
        applyBeatSpacePointToChannels(true, false);
    }
}

void StepVstHostAudioProcessor::beatSpacePan(int dx, int dy)
{
    const int stepX = juce::jmax(1, getBeatSpaceViewWidth() / 8);
    const int stepY = juce::jmax(1, getBeatSpaceViewHeight() / 8);
    beatSpaceViewX += dx * stepX;
    beatSpaceViewY += dy * stepY;
    clampBeatSpaceView();
}

void StepVstHostAudioProcessor::setBeatSpaceMorphDurationMs(double durationMs)
{
    const double clamped = juce::jlimit(40.0, 2000.0, durationMs);
    if (std::abs(beatSpaceMorphDurationMs - clamped) < 0.5)
        return;
    beatSpaceMorphDurationMs = clamped;
    savePersistentControlPages();
}

double StepVstHostAudioProcessor::getBeatSpaceMorphDurationMs() const
{
    return beatSpaceMorphDurationMs;
}

void StepVstHostAudioProcessor::setBeatSpaceMacroKnobValue(int macroIndex, float value)
{
    const int clampedIndex = juce::jlimit(0, BeatSpaceMacroKnobCount - 1, macroIndex);
    const auto idx = static_cast<size_t>(clampedIndex);
    const float clampedValue = juce::jlimit(0.0f, 1.0f, value);
    if (std::abs(beatSpaceMacroKnobValues[idx] - clampedValue) < 1.0e-5f)
        return;

    const float previousTraitTotal = beatSpaceMacroKnobValues[0]
        + beatSpaceMacroKnobValues[1]
        + beatSpaceMacroKnobValues[2]
        + beatSpaceMacroKnobValues[3];
    beatSpaceMacroKnobValues[idx] = clampedValue;
    const float nextTraitTotal = beatSpaceMacroKnobValues[0]
        + beatSpaceMacroKnobValues[1]
        + beatSpaceMacroKnobValues[2]
        + beatSpaceMacroKnobValues[3];

    if (previousTraitTotal <= 1.0e-5f && nextTraitTotal > 1.0e-5f)
        beatSpaceMacroBasePointsValid = false;

    if (clampedIndex < 4)
    {
        beatSpaceMacroTraitAnchorsReady = false;
        applyBeatSpaceMacroBubbleOffsetsFromKnobs();
        return;
    }

    applyBeatSpacePointToChannels(true, false);
    requestImmediateUiAndMonomeRefresh();
}

float StepVstHostAudioProcessor::getBeatSpaceMacroKnobValue(int macroIndex) const
{
    const int clampedIndex = juce::jlimit(0, BeatSpaceMacroKnobCount - 1, macroIndex);
    return juce::jlimit(0.0f, 1.0f, beatSpaceMacroKnobValues[static_cast<size_t>(clampedIndex)]);
}

void StepVstHostAudioProcessor::resetBeatSpaceMacroKnobs()
{
    beatSpaceMacroKnobValues.fill(0.0f);
    for (int i = 4; i < BeatSpaceMacroKnobCount; ++i)
        beatSpaceMacroKnobValues[static_cast<size_t>(i)] = 0.5f;

    beatSpaceMacroTraitAnchorsReady = false;
    applyBeatSpaceMacroBubbleOffsetsFromKnobs();
    beatSpaceMacroBasePointsValid = false;
    applyBeatSpacePointToChannels(true, false);
    requestImmediateUiAndMonomeRefresh();
}

void StepVstHostAudioProcessor::setBeatSpacePatchParamNormalized(
    int channel, int patchParamIndex, float normalized)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int clampedParam = juce::jlimit(0, BeatSpacePatchParamCount - 1, patchParamIndex);
    const float clampedValue = juce::jlimit(0.0f, 1.0f, normalized);
    const auto idx = static_cast<size_t>(clampedChannel);

    auto* instance = hostRack.getInstance();
    if (instance == nullptr || !refreshBeatSpaceParameterMap(false) || !beatSpaceChannelMappingReady[idx])
    {
        beatSpaceCurrentVectors[idx][static_cast<size_t>(clampedParam)] = clampedValue;
        return;
    }

    const int parameterIndex = beatSpaceParamMap[idx][static_cast<size_t>(clampedParam)];
    const auto& params = instance->getParameters();
    if (parameterIndex < 0 || parameterIndex >= static_cast<int>(params.size()))
    {
        beatSpaceCurrentVectors[idx][static_cast<size_t>(clampedParam)] = clampedValue;
        return;
    }

    params[parameterIndex]->setValueNotifyingHost(clampedValue);
    beatSpaceCurrentVectors[idx][static_cast<size_t>(clampedParam)] =
        juce::jlimit(0.0f, 1.0f, params[parameterIndex]->getValue());
}

float StepVstHostAudioProcessor::getBeatSpacePatchParamNormalized(int channel, int patchParamIndex) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int clampedParam = juce::jlimit(0, BeatSpacePatchParamCount - 1, patchParamIndex);
    const auto idx = static_cast<size_t>(clampedChannel);

    auto* instance = hostRack.getInstance();
    if (instance == nullptr || !beatSpaceChannelMappingReady[idx])
        return juce::jlimit(0.0f, 1.0f, beatSpaceCurrentVectors[idx][static_cast<size_t>(clampedParam)]);

    const int parameterIndex = beatSpaceParamMap[idx][static_cast<size_t>(clampedParam)];
    const auto& params = instance->getParameters();
    if (parameterIndex < 0 || parameterIndex >= static_cast<int>(params.size()))
        return juce::jlimit(0.0f, 1.0f, beatSpaceCurrentVectors[idx][static_cast<size_t>(clampedParam)]);

    return juce::jlimit(0.0f, 1.0f, params[parameterIndex]->getValue());
}

void StepVstHostAudioProcessor::setBeatSpaceZoneLockStrength(int channel, float strength)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const float clampedStrength = juce::jlimit(0.0f, 1.0f, strength);
    if (std::abs(beatSpaceZoneLockStrength[static_cast<size_t>(clampedChannel)] - clampedStrength) < 1.0e-4f)
        return;
    beatSpaceZoneLockStrength[static_cast<size_t>(clampedChannel)] = clampedStrength;
    if (beatSpaceLinkAllChannels)
    {
        applyBeatSpaceLinkedChannelOffsets(
            beatSpaceChannelPoints[static_cast<size_t>(beatSpaceSelectedChannel)],
            beatSpaceSelectedChannel);
    }
    else
    {
        beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)] =
            constrainBeatSpacePointForChannel(
                clampedChannel,
                beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)]);
    }
    applyBeatSpacePointToChannels(true, false);
    savePersistentControlPages();
}

float StepVstHostAudioProcessor::getBeatSpaceZoneLockStrength(int channel) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    return juce::jlimit(
        0.0f,
        1.0f,
        beatSpaceZoneLockStrength[static_cast<size_t>(clampedChannel)]);
}

void StepVstHostAudioProcessor::setBeatSpaceConfidenceOverlayEnabled(bool enabled)
{
    if (beatSpaceConfidenceOverlayEnabled == enabled)
        return;
    beatSpaceConfidenceOverlayEnabled = enabled;
    savePersistentControlPages();
}

bool StepVstHostAudioProcessor::isBeatSpaceConfidenceOverlayEnabled() const
{
    return beatSpaceConfidenceOverlayEnabled;
}

void StepVstHostAudioProcessor::setBeatSpacePathOverlayEnabled(bool enabled)
{
    if (beatSpacePathOverlayEnabled == enabled)
        return;
    beatSpacePathOverlayEnabled = enabled;
    savePersistentControlPages();
}

bool StepVstHostAudioProcessor::isBeatSpacePathOverlayEnabled() const
{
    return beatSpacePathOverlayEnabled;
}

void StepVstHostAudioProcessor::setBeatSpaceCategoryConstrainEnabled(bool enabled)
{
    if (beatSpaceCategoryConstrainEnabled == enabled)
        return;

    beatSpaceCategoryConstrainEnabled = enabled;
    if (beatSpaceCategoryConstrainEnabled)
    {
        if (!beatSpaceCategoryAnchorsReady)
            rebuildBeatSpaceCategoryAnchors();

        for (int channel = 0; channel < BeatSpaceChannels; ++channel)
        {
            const auto idx = static_cast<size_t>(channel);
            beatSpaceChannelPoints[idx] = constrainBeatSpacePointForChannel(
                channel,
                beatSpaceChannelPoints[idx]);
        }
    }

    if (beatSpaceLinkAllChannels)
    {
        const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        rebuildBeatSpaceLinkedOffsetsFromCurrent(selectedChannel);
    }

    applyBeatSpacePointToChannels(true, false);
    requestImmediateUiAndMonomeRefresh();
    savePersistentControlPages();
}

bool StepVstHostAudioProcessor::isBeatSpaceCategoryConstrainEnabled() const
{
    return beatSpaceCategoryConstrainEnabled;
}

void StepVstHostAudioProcessor::setBeatSpacePathRecordArmedChannel(int channel)
{
    const int clamped = (channel < 0) ? -1 : juce::jlimit(0, BeatSpaceChannels - 1, channel);
    if (beatSpacePathRecordArmedChannel == clamped)
        return;
    beatSpacePathRecordArmedChannel = clamped;
    savePersistentControlPages();
}

int StepVstHostAudioProcessor::getBeatSpacePathRecordArmedChannel() const
{
    return beatSpacePathRecordArmedChannel;
}

bool StepVstHostAudioProcessor::beatSpaceAddBookmark(int channel, const juce::String& tag)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    auto& channelBookmarks = beatSpaceBookmarks[static_cast<size_t>(clampedChannel)];
    const auto currentPoint =
        clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)]);
    const auto trimmedTag = tag.trim();
    juce::String effectiveTag = trimmedTag;
    if (effectiveTag.isEmpty())
        effectiveTag = "P " + juce::String(currentPoint.x) + "," + juce::String(currentPoint.y);

    for (auto& bookmark : channelBookmarks)
    {
        if (bookmark.used && bookmark.point == currentPoint)
        {
            bookmark.tag = effectiveTag;
            savePersistentControlPages();
            return true;
        }
    }

    for (auto& bookmark : channelBookmarks)
    {
        if (!bookmark.used)
        {
            bookmark.used = true;
            bookmark.point = currentPoint;
            bookmark.tag = effectiveTag;
            savePersistentControlPages();
            return true;
        }
    }

    for (int i = 1; i < BeatSpaceBookmarkSlots; ++i)
        channelBookmarks[static_cast<size_t>(i - 1)] = channelBookmarks[static_cast<size_t>(i)];
    channelBookmarks[static_cast<size_t>(BeatSpaceBookmarkSlots - 1)].used = true;
    channelBookmarks[static_cast<size_t>(BeatSpaceBookmarkSlots - 1)].point = currentPoint;
    channelBookmarks[static_cast<size_t>(BeatSpaceBookmarkSlots - 1)].tag = effectiveTag;
    savePersistentControlPages();
    return true;
}

bool StepVstHostAudioProcessor::beatSpaceRecallBookmark(int channel, int bookmarkIndex, bool morph)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int clampedBookmark = juce::jlimit(0, BeatSpaceBookmarkSlots - 1, bookmarkIndex);
    const auto& bookmark = beatSpaceBookmarks[static_cast<size_t>(clampedChannel)][static_cast<size_t>(clampedBookmark)];
    if (!bookmark.used)
        return false;
    beatSpaceSetChannelPoint(clampedChannel, bookmark.point, false, morph);
    return true;
}

int StepVstHostAudioProcessor::getBeatSpaceBookmarkCount(int channel) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    int count = 0;
    for (const auto& bookmark : beatSpaceBookmarks[static_cast<size_t>(clampedChannel)])
    {
        if (bookmark.used)
            ++count;
    }
    return count;
}

juce::String StepVstHostAudioProcessor::getBeatSpaceBookmarkTag(int channel, int bookmarkIndex) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int clampedBookmark = juce::jlimit(0, BeatSpaceBookmarkSlots - 1, bookmarkIndex);
    const auto& bookmark = beatSpaceBookmarks[static_cast<size_t>(clampedChannel)][static_cast<size_t>(clampedBookmark)];
    if (!bookmark.used)
        return {};
    return bookmark.tag;
}

void StepVstHostAudioProcessor::beatSpaceClearBookmarks(int channel)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    for (auto& bookmark : beatSpaceBookmarks[static_cast<size_t>(clampedChannel)])
        bookmark = BeatSpaceBookmark{};
    savePersistentControlPages();
}

std::vector<int> StepVstHostAudioProcessor::getBeatSpaceNearestPresetSlots(int channel, int count) const
{
    std::vector<int> result;
    if (count <= 0)
        return result;
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const int assignedSpace = getBeatSpaceChannelSpaceAssignment(clampedChannel);
    if (!beatSpaceCategoryPresetPointsReady[static_cast<size_t>(assignedSpace)])
        return result;

    const auto currentPoint = clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)]);
    struct Candidate
    {
        int slot = 0;
        float distance = 0.0f;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(static_cast<size_t>(BeatSpacePresetSlotsPerSpace));
    for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
    {
        const auto p = beatSpaceCategoryPresetPoints[static_cast<size_t>(assignedSpace)][static_cast<size_t>(slot)];
        const float dx = static_cast<float>(p.x - currentPoint.x);
        const float dy = static_cast<float>(p.y - currentPoint.y);
        candidates.push_back({ slot, std::sqrt((dx * dx) + (dy * dy)) });
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b)
    {
        if (std::abs(a.distance - b.distance) < 1.0e-6f)
            return a.slot < b.slot;
        return a.distance < b.distance;
    });
    const int num = juce::jmin(count, static_cast<int>(candidates.size()));
    result.reserve(static_cast<size_t>(num));
    for (int i = 0; i < num; ++i)
        result.push_back(candidates[static_cast<size_t>(i)].slot);
    return result;
}

void StepVstHostAudioProcessor::beatSpacePathClear(int channel)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const auto previousPlayMode = beatSpacePaths[static_cast<size_t>(clampedChannel)].playMode;
    beatSpacePaths[static_cast<size_t>(clampedChannel)] = BeatSpacePathState{};
    beatSpacePaths[static_cast<size_t>(clampedChannel)].playMode = previousPlayMode;
    savePersistentControlPages();
}

bool StepVstHostAudioProcessor::beatSpacePathAddCurrentPoint(int channel)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    auto& path = beatSpacePaths[static_cast<size_t>(clampedChannel)];
    if (!path.recording)
        path.recording = true;
    const auto p = clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)]);
    if (path.count > 0 && path.points[static_cast<size_t>(path.count - 1)] == p)
        return true;

    if (path.count < BeatSpacePathMaxPoints)
    {
        path.points[static_cast<size_t>(path.count)] = p;
        ++path.count;
    }
    else
    {
        for (int i = 1; i < BeatSpacePathMaxPoints; ++i)
            path.points[static_cast<size_t>(i - 1)] = path.points[static_cast<size_t>(i)];
        path.points[static_cast<size_t>(BeatSpacePathMaxPoints - 1)] = p;
    }
    savePersistentControlPages();
    return true;
}

bool StepVstHostAudioProcessor::beatSpacePathStart(int channel, BeatSpacePathMode mode)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    auto& path = beatSpacePaths[static_cast<size_t>(clampedChannel)];
    if (path.count < 2)
        return false;

    path.mode = mode;
    if (mode == BeatSpacePathMode::QuarterNote)
    {
        path.cycleBeats = 0.25;
        path.loopBars = 0;
    }
    else
    {
        // Keep path loops bar-quantized in even bars so longer gestures remain intact.
        int bars = (path.loopBars > 0) ? path.loopBars : 2;
        bars = juce::jlimit(2, 32, bars);
        if ((bars & 1) != 0)
            bars = juce::jlimit(2, 32, bars + 1);
        path.loopBars = bars;
        path.cycleBeats = static_cast<double>(bars) * 4.0;
    }
    path.active = true;
    path.recording = false;
    path.recordStartPpq = -1.0;
    path.startMs = juce::Time::getMillisecondCounterHiRes();
    path.startPpq = -1.0;
    path.pendingQuantizedStart = false;
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (position->getPpqPosition().hasValue())
            {
                const double hostPpq = *position->getPpqPosition();
                path.startPpq = (std::floor(hostPpq / 4.0) + 1.0) * 4.0;
                path.pendingQuantizedStart = true;
            }
        }
    }
    savePersistentControlPages();
    return true;
}

void StepVstHostAudioProcessor::beatSpacePathStop(int channel)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    auto& path = beatSpacePaths[static_cast<size_t>(clampedChannel)];
    if (!path.active)
        return;
    path.active = false;
    path.pendingQuantizedStart = false;
    path.recording = false;
    savePersistentControlPages();
}

bool StepVstHostAudioProcessor::beatSpacePathIsActive(int channel) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    return beatSpacePaths[static_cast<size_t>(clampedChannel)].active;
}

void StepVstHostAudioProcessor::beatSpacePathSetPlayMode(int channel, BeatSpacePathPlayMode playMode)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const auto clampedPlayMode = static_cast<BeatSpacePathPlayMode>(juce::jlimit(
        0,
        static_cast<int>(BeatSpacePathPlayMode::RandomSlice),
        static_cast<int>(playMode)));
    auto& path = beatSpacePaths[static_cast<size_t>(clampedChannel)];
    if (path.playMode == clampedPlayMode)
        return;

    path.playMode = clampedPlayMode;
    savePersistentControlPages();
}

StepVstHostAudioProcessor::BeatSpacePathPlayMode
StepVstHostAudioProcessor::beatSpacePathGetPlayMode(int channel) const
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    return beatSpacePaths[static_cast<size_t>(clampedChannel)].playMode;
}

bool StepVstHostAudioProcessor::beatSpacePathRecordStart(int channel, const juce::Point<int>& point)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const auto clampedPoint = clampBeatSpacePointToTable(point);
    auto& path = beatSpacePaths[static_cast<size_t>(clampedChannel)];
    const auto previousPlayMode = path.playMode;
    path = BeatSpacePathState{};
    path.playMode = previousPlayMode;
    path.mode = BeatSpacePathMode::OneBar;
    path.cycleBeats = 8.0;
    path.loopBars = 2;
    path.count = 1;
    path.points[0] = clampedPoint;
    path.active = false;
    path.recording = true;
    path.pendingQuantizedStart = false;
    path.recordStartPpq = std::numeric_limits<double>::quiet_NaN();
    path.startPpq = -1.0;
    path.startMs = juce::Time::getMillisecondCounterHiRes();

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (position->getPpqPosition().hasValue())
                path.recordStartPpq = *position->getPpqPosition();
        }
    }
    if (!std::isfinite(path.recordStartPpq))
        path.recordStartPpq = audioEngine->getTimelineBeat();

    beatSpaceSetChannelPoint(clampedChannel, clampedPoint, false, false);
    savePersistentControlPages();
    return true;
}

bool StepVstHostAudioProcessor::beatSpacePathRecordAppendPoint(int channel, const juce::Point<int>& point)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    auto& path = beatSpacePaths[static_cast<size_t>(clampedChannel)];
    if (!path.recording)
        return false;

    const auto clampedPoint = clampBeatSpacePointToTable(point);
    if (path.count > 0 && path.points[static_cast<size_t>(path.count - 1)] == clampedPoint)
        return true;

    if (path.count < BeatSpacePathMaxPoints)
    {
        path.points[static_cast<size_t>(path.count)] = clampedPoint;
        ++path.count;
    }
    else
    {
        for (int i = 1; i < BeatSpacePathMaxPoints; ++i)
            path.points[static_cast<size_t>(i - 1)] = path.points[static_cast<size_t>(i)];
        path.points[static_cast<size_t>(BeatSpacePathMaxPoints - 1)] = clampedPoint;
    }

    beatSpaceSetChannelPoint(clampedChannel, clampedPoint, false, false);
    return true;
}

bool StepVstHostAudioProcessor::beatSpacePathRecordFinishAndPlay(int channel)
{
    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    auto& path = beatSpacePaths[static_cast<size_t>(clampedChannel)];
    if (!path.recording)
        return false;

    path.recording = false;
    if (path.count < 2)
    {
        path.active = false;
        path.pendingQuantizedStart = false;
        savePersistentControlPages();
        return false;
    }

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    double nowPpq = std::numeric_limits<double>::quiet_NaN();
    double bpm = 120.0;
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (position->getPpqPosition().hasValue())
                nowPpq = *position->getPpqPosition();
            if (position->getBpm().hasValue() && *position->getBpm() > 0.0)
                bpm = *position->getBpm();
        }
    }
    if (!std::isfinite(nowPpq))
        nowPpq = audioEngine->getTimelineBeat();

    double recordedBeats = 0.0;
    if (std::isfinite(path.recordStartPpq) && std::isfinite(nowPpq))
    {
        recordedBeats = juce::jmax(0.25, nowPpq - path.recordStartPpq);
    }
    else
    {
        const double elapsedMs = juce::jmax(10.0, nowMs - path.startMs);
        recordedBeats = juce::jmax(0.25, elapsedMs * (bpm / 60000.0));
    }

    const int roundedBars = juce::jmax(1, static_cast<int>(std::lround(recordedBeats / 4.0)));
    int evenBars = static_cast<int>(2.0 * std::round(static_cast<double>(roundedBars) / 2.0));
    evenBars = juce::jlimit(2, 32, juce::jmax(2, evenBars));

    path.mode = BeatSpacePathMode::OneBar;
    path.loopBars = evenBars;
    path.cycleBeats = static_cast<double>(evenBars) * 4.0;
    path.active = true;
    path.startMs = nowMs;
    path.startPpq = -1.0;
    path.pendingQuantizedStart = false;
    if (std::isfinite(nowPpq))
    {
        path.startPpq = (std::floor(nowPpq / 4.0) + 1.0) * 4.0;
        path.pendingQuantizedStart = true;
    }

    beatSpaceSetChannelPoint(clampedChannel, path.points[0], false, true);
    savePersistentControlPages();
    return true;
}

void StepVstHostAudioProcessor::beatSpaceSetManualCategoryAnchor(int category, const juce::Point<int>& point)
{
    const int clampedCategory = juce::jlimit(0, BeatSpaceChannels - 1, category);
    const juce::Point<int> clampedPoint {
        juce::jlimit(0, BeatSpaceTableSize - 1, point.x),
        juce::jlimit(0, BeatSpaceTableSize - 1, point.y)
    };
    const auto catIdx = static_cast<size_t>(clampedCategory);
    auto& tagCount = beatSpaceCategoryManualTagCounts[catIdx];
    auto& tags = beatSpaceCategoryManualTagPoints[catIdx];

    bool duplicate = false;
    for (int i = 0; i < tagCount; ++i)
    {
        if (tags[static_cast<size_t>(i)] == clampedPoint)
        {
            duplicate = true;
            break;
        }
    }
    if (!duplicate)
    {
        if (tagCount < BeatSpacePresetSlotsPerSpace)
        {
            tags[static_cast<size_t>(tagCount)] = clampedPoint;
            ++tagCount;
        }
        else
        {
            for (int i = 1; i < BeatSpacePresetSlotsPerSpace; ++i)
                tags[static_cast<size_t>(i - 1)] = tags[static_cast<size_t>(i)];
            tags[static_cast<size_t>(BeatSpacePresetSlotsPerSpace - 1)] = clampedPoint;
        }
    }

    if (tagCount > 0)
    {
        int sumX = 0;
        int sumY = 0;
        for (int i = 0; i < tagCount; ++i)
        {
            sumX += tags[static_cast<size_t>(i)].x;
            sumY += tags[static_cast<size_t>(i)].y;
        }
        beatSpaceCategoryManualAnchors[catIdx] = {
            juce::jlimit(0, BeatSpaceTableSize - 1, sumX / juce::jmax(1, tagCount)),
            juce::jlimit(0, BeatSpaceTableSize - 1, sumY / juce::jmax(1, tagCount))
        };
        beatSpaceCategoryAnchorManual[catIdx] = true;
    }

    rebuildBeatSpaceCategoryAnchors();
    for (int i = 0; i < BeatSpaceChannels; ++i)
        beatSpaceChannelPoints[static_cast<size_t>(i)] =
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(i)]);

    if (beatSpaceLinkAllChannels)
    {
        const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        applyBeatSpaceLinkedChannelOffsets(
            beatSpaceChannelPoints[static_cast<size_t>(selectedChannel)],
            selectedChannel);
    }

    applyBeatSpacePointToChannels(true, false);
    beatSpaceStatusMessage = "BeatSpace table loaded (manual tags fill category presets)";
    savePersistentControlPages();
}

void StepVstHostAudioProcessor::beatSpaceClearManualCategoryAnchor(int category)
{
    const int clampedCategory = juce::jlimit(0, BeatSpaceChannels - 1, category);
    const auto catIdx = static_cast<size_t>(clampedCategory);
    if (beatSpaceCategoryManualTagCounts[catIdx] <= 0
        && !beatSpaceCategoryAnchorManual[catIdx])
        return;

    beatSpaceCategoryManualTagCounts[catIdx] = 0;
    beatSpaceCategoryAnchorManual[catIdx] = false;
    beatSpaceCategoryManualTagPoints[catIdx].fill(beatSpaceCategoryManualAnchors[catIdx]);
    rebuildBeatSpaceCategoryAnchors();

    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        beatSpaceChannelPoints[static_cast<size_t>(i)] =
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(i)]);
    }

    if (beatSpaceLinkAllChannels)
    {
        const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        applyBeatSpaceLinkedChannelOffsets(
            beatSpaceChannelPoints[static_cast<size_t>(selectedChannel)],
            selectedChannel);
    }

    applyBeatSpacePointToChannels(true, false);
    savePersistentControlPages();
}

void StepVstHostAudioProcessor::beatSpaceClearAllManualCategoryAnchors()
{
    bool anyCleared = false;
    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        if (beatSpaceCategoryManualTagCounts[idx] <= 0
            && !beatSpaceCategoryAnchorManual[idx])
            continue;
        beatSpaceCategoryManualTagCounts[idx] = 0;
        beatSpaceCategoryAnchorManual[idx] = false;
        beatSpaceCategoryManualTagPoints[idx].fill(beatSpaceCategoryManualAnchors[idx]);
        anyCleared = true;
    }
    if (!anyCleared)
        return;

    rebuildBeatSpaceCategoryAnchors();
    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        beatSpaceChannelPoints[static_cast<size_t>(i)] =
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(i)]);
    }

    if (beatSpaceLinkAllChannels)
    {
        const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        applyBeatSpaceLinkedChannelOffsets(
            beatSpaceChannelPoints[static_cast<size_t>(selectedChannel)],
            selectedChannel);
    }

    applyBeatSpacePointToChannels(true, false);
    savePersistentControlPages();
}

bool StepVstHostAudioProcessor::beatSpaceClearNearestManualCategoryAnchor(
    const juce::Point<int>& point, int maxDistance)
{
    const auto clampedPoint = juce::Point<int> {
        juce::jlimit(0, BeatSpaceTableSize - 1, point.x),
        juce::jlimit(0, BeatSpaceTableSize - 1, point.y)
    };
    const float maxDistanceF = static_cast<float>(juce::jmax(1, maxDistance));

    int bestCategory = -1;
    int bestTagIndex = -1;
    float bestDistance = maxDistanceF + 1.0f;
    for (int category = 0; category < BeatSpaceChannels; ++category)
    {
        const auto catIdx = static_cast<size_t>(category);
        const int tagCount = juce::jlimit(
            0,
            BeatSpacePresetSlotsPerSpace,
            beatSpaceCategoryManualTagCounts[catIdx]);
        for (int i = 0; i < tagCount; ++i)
        {
            const auto candidate = beatSpaceCategoryManualTagPoints[catIdx][static_cast<size_t>(i)];
            const float dx = static_cast<float>(candidate.x - clampedPoint.x);
            const float dy = static_cast<float>(candidate.y - clampedPoint.y);
            const float distance = std::sqrt((dx * dx) + (dy * dy));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestCategory = category;
                bestTagIndex = i;
            }
        }
    }

    if (bestCategory < 0 || bestTagIndex < 0 || bestDistance > maxDistanceF)
        return false;

    const auto bestIdx = static_cast<size_t>(bestCategory);
    auto& count = beatSpaceCategoryManualTagCounts[bestIdx];
    auto& tags = beatSpaceCategoryManualTagPoints[bestIdx];
    for (int i = bestTagIndex + 1; i < count; ++i)
        tags[static_cast<size_t>(i - 1)] = tags[static_cast<size_t>(i)];
    if (count > 0)
        --count;

    if (count > 0)
    {
        int sumX = 0;
        int sumY = 0;
        for (int i = 0; i < count; ++i)
        {
            sumX += tags[static_cast<size_t>(i)].x;
            sumY += tags[static_cast<size_t>(i)].y;
        }
        beatSpaceCategoryManualAnchors[bestIdx] = {
            juce::jlimit(0, BeatSpaceTableSize - 1, sumX / juce::jmax(1, count)),
            juce::jlimit(0, BeatSpaceTableSize - 1, sumY / juce::jmax(1, count))
        };
        beatSpaceCategoryAnchorManual[bestIdx] = true;
    }
    else
    {
        beatSpaceCategoryAnchorManual[bestIdx] = false;
    }

    rebuildBeatSpaceCategoryAnchors();
    for (int i = 0; i < BeatSpaceChannels; ++i)
        beatSpaceChannelPoints[static_cast<size_t>(i)] =
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(i)]);
    if (beatSpaceLinkAllChannels)
    {
        const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        applyBeatSpaceLinkedChannelOffsets(
            beatSpaceChannelPoints[static_cast<size_t>(selectedChannel)],
            selectedChannel);
    }
    applyBeatSpacePointToChannels(true, false);
    savePersistentControlPages();
    return true;
}

void StepVstHostAudioProcessor::beatSpaceSetPointFromGridCell(
    int gridX, int gridY, int gridWidth, int gridHeight)
{
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
        return;
    beatSpaceSetChannelPoint(
        beatSpaceSelectedChannel,
        gridCellToBeatSpacePoint(gridX, gridY, gridWidth, gridHeight),
        beatSpaceLinkAllChannels,
        true);
}

void StepVstHostAudioProcessor::beatSpaceSetChannelPoint(
    int channel,
    const juce::Point<int>& point,
    bool moveLinkedGroup,
    bool morph)
{
    if (!beatSpaceDecoderReady || beatSpaceTable.empty())
        return;

    const int clampedChannel = juce::jlimit(0, BeatSpaceChannels - 1, channel);
    const auto clampedPoint = clampBeatSpacePointToTable(point);
    const auto targetPoint = beatSpaceCategoryConstrainEnabled
        ? constrainBeatSpacePointForChannel(clampedChannel, clampedPoint)
        : clampedPoint;
    if (!beatSpaceApplyingMacroMove)
        beatSpaceMacroBasePointsValid = false;

    if (beatSpaceLinkAllChannels && moveLinkedGroup)
    {
        const auto previousAnchorPoint =
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)]);

        if (!beatSpaceLinkedOffsetsReady)
            rebuildBeatSpaceLinkedOffsetsFromCurrent(clampedChannel);
        const auto linkedAnchor = constrainBeatSpaceLinkedMasterPoint(targetPoint, clampedChannel);
        const int deltaX = linkedAnchor.x - previousAnchorPoint.x;
        const int deltaY = linkedAnchor.y - previousAnchorPoint.y;
        applyBeatSpaceLinkedChannelOffsets(linkedAnchor, clampedChannel);
        // Preserve the captured relative spread while dragging the link handle.
        // Channels can clamp at table edges temporarily, then recover spacing
        // when the anchor moves back inward.

        // Keep recorded paths attached to the linked constellation when the
        // whole group is moved manually.
        if (deltaX != 0 || deltaY != 0)
        {
            for (int c = 0; c < BeatSpaceChannels; ++c)
            {
                auto& path = beatSpacePaths[static_cast<size_t>(c)];
                if (path.count <= 0)
                    continue;
                for (int p = 0; p < path.count && p < BeatSpacePathMaxPoints; ++p)
                {
                    auto pointOnPath = path.points[static_cast<size_t>(p)];
                    pointOnPath.x += deltaX;
                    pointOnPath.y += deltaY;
                    path.points[static_cast<size_t>(p)] = pointOnPath;
                }
            }
        }

        applyBeatSpacePointToChannels(true, morph);
        return;
    }

    beatSpaceChannelPoints[static_cast<size_t>(clampedChannel)] = targetPoint;

    if (beatSpaceLinkAllChannels)
    {
        const auto pathIdx = static_cast<size_t>(clampedChannel);
        const auto& pathState = beatSpacePaths[pathIdx];
        if (pathState.active || pathState.recording)
        {
            // Path-driven movement should only move the path channel visually;
            // linked siblings remain fixed until explicitly moved.
            const int previousSelected = beatSpaceSelectedChannel;
            beatSpaceSelectedChannel = clampedChannel;
            applyBeatSpacePointToChannels(false, morph);
            beatSpaceSelectedChannel = previousSelected;
            return;
        }

        const int anchorChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        if (!beatSpaceLinkedOffsetsReady)
            rebuildBeatSpaceLinkedOffsetsFromCurrent(anchorChannel);

        if (clampedChannel != anchorChannel)
        {
            const auto anchorPoint =
                clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(anchorChannel)]);
            beatSpaceLinkedOffsets[static_cast<size_t>(clampedChannel)] = {
                static_cast<float>(targetPoint.x - anchorPoint.x),
                static_cast<float>(targetPoint.y - anchorPoint.y)
            };

            applyBeatSpaceLinkedChannelOffsets(
                beatSpaceChannelPoints[static_cast<size_t>(anchorChannel)],
                anchorChannel);
        }
        else
        {
            // In link mode, allow dragging the selected bubble independently:
            // keep sibling channel points fixed and rebuild offsets around the new anchor.
            for (int c = 0; c < BeatSpaceChannels; ++c)
            {
                if (c == anchorChannel)
                    continue;

                const auto p =
                    clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(c)]);
                beatSpaceLinkedOffsets[static_cast<size_t>(c)] = {
                    static_cast<float>(p.x - targetPoint.x),
                    static_cast<float>(p.y - targetPoint.y)
                };
            }
        }
        beatSpaceLinkedOffsets[static_cast<size_t>(anchorChannel)] = { 0.0f, 0.0f };

        applyBeatSpacePointToChannels(true, morph);
        return;
    }

    const int previousSelected = beatSpaceSelectedChannel;
    beatSpaceSelectedChannel = clampedChannel;
    applyBeatSpacePointToChannels(false, morph);
    beatSpaceSelectedChannel = previousSelected;
}

int StepVstHostAudioProcessor::getLaneMidiChannel(int stripIndex) const
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return 1;
    return juce::jlimit(1, 16, laneMidiChannel[static_cast<size_t>(stripIndex)].load(std::memory_order_relaxed));
}

int StepVstHostAudioProcessor::getLaneMidiNote(int stripIndex) const
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return 60;
    return juce::jlimit(0, 127, laneMidiNote[static_cast<size_t>(stripIndex)].load(std::memory_order_relaxed));
}

void StepVstHostAudioProcessor::setLaneMidiChannel(int stripIndex, int channel)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return;
    laneMidiChannel[static_cast<size_t>(stripIndex)].store(juce::jlimit(1, 16, channel), std::memory_order_relaxed);
}

void StepVstHostAudioProcessor::setLaneMidiNote(int stripIndex, int note)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return;
    laneMidiNote[static_cast<size_t>(stripIndex)].store(juce::jlimit(0, 127, note), std::memory_order_relaxed);
}

void StepVstHostAudioProcessor::queueHostedProgramChangeForStrip(int stripIndex, int deltaPrograms)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return;

    const int delta = juce::jlimit(-32, 32, deltaPrograms);
    if (delta == 0)
        return;

    hostedPendingProgramDelta[static_cast<size_t>(stripIndex)].fetch_add(delta, std::memory_order_relaxed);
}

void StepVstHostAudioProcessor::buildHostedLaneMidi(const juce::AudioPlayHead::PositionInfo& posInfo,
                                               int numSamples,
                                               juce::MidiBuffer& midi)
{
    midi.clear();
    if (!audioEngine || numSamples <= 0)
        return;

    auto* hostedInstance = hostRack.getInstance();
    const juce::Array<juce::AudioProcessorParameter*>* hostedParams =
        (hostedInstance != nullptr) ? &hostedInstance->getParameters() : nullptr;
    std::array<bool, MaxStrips> hostedStripMuted{};
    bool anyStripSolo = false;
    for (int stripIndex = 0; stripIndex < MaxStrips; ++stripIndex)
    {
        if (auto* strip = audioEngine->getStrip(stripIndex))
            anyStripSolo = anyStripSolo || strip->isSolo();
    }
    for (int stripIndex = 0; stripIndex < MaxStrips; ++stripIndex)
    {
        auto* strip = audioEngine->getStrip(stripIndex);
        const bool stripMuted = (strip == nullptr) ? true : strip->isMuted();
        const bool stripMutedBySolo = anyStripSolo && (strip == nullptr || !strip->isSolo());
        hostedStripMuted[static_cast<size_t>(stripIndex)] = stripMuted || stripMutedBySolo;
    }

    auto applyHostedParameter = [hostedParams](
        int paramIndex,
        float normalizedValue,
        float& cachedLastValue)
    {
        if (hostedParams == nullptr)
            return false;
        if (paramIndex < 0 || paramIndex >= hostedParams->size())
            return false;
        auto* param = (*hostedParams)[paramIndex];
        if (param == nullptr)
            return false;

        float normalized = juce::jlimit(0.0f, 1.0f, normalizedValue);
        const int steps = param->getNumSteps();
        if (steps > 1 && steps <= 4096)
        {
            const float denom = static_cast<float>(steps - 1);
            if (denom > 0.0f)
                normalized = std::round(normalized * denom) / denom;
        }

        if (cachedLastValue < 0.0f || std::abs(cachedLastValue - normalized) > 1.0e-5f)
        {
            param->setValueNotifyingHost(normalized);
            cachedLastValue = normalized;
        }
        else if (std::abs(param->getValue() - normalized) > 1.0e-5f)
        {
            // Another subsystem (e.g. BeatSpace path playback) moved this parameter.
            // Re-assert hosted lane control so offsets remain stable.
            param->setValueNotifyingHost(normalized);
            cachedLastValue = normalized;
        }
        return true;
    };

    for (int stripIndex = 0; stripIndex < MaxStrips; ++stripIndex)
    {
        const auto idx = static_cast<size_t>(stripIndex);
        const int channel = getLaneMidiChannel(stripIndex);
        auto* strip = audioEngine->getStrip(stripIndex);
        const bool stripOutputMuted = hostedStripMuted[idx];
        const bool stripMuteReleased = hostedStripOutputMutedLast[idx] && !stripOutputMuted;
        hostedStripOutputMutedLast[idx] = stripOutputMuted;
        const bool beatSpaceChannel = (stripIndex >= 0 && stripIndex < BeatSpaceChannels);
        const bool beatSpaceVectorReady = beatSpaceChannel && beatSpaceChannelMappingReady[idx];
        const bool beatSpaceBaseActive = beatSpaceChannel && beatSpaceVectorReady;
        const auto readBeatSpaceBaseNorm = [this, idx](int vectorIndex, float fallbackNorm)
        {
            if (vectorIndex < 0 || vectorIndex >= BeatSpaceVectorSize)
                return juce::jlimit(0.0f, 1.0f, fallbackNorm);
            const float value = beatSpaceCurrentVectors[idx][static_cast<size_t>(vectorIndex)];
            if (!std::isfinite(value))
                return juce::jlimit(0.0f, 1.0f, fallbackNorm);
            return juce::jlimit(0.0f, 1.0f, value);
        };

        const float volumeRaw = juce::jlimit(
            0.0f, 1.0f,
            stripVolumeParams[idx] != nullptr
                ? stripVolumeParams[idx]->load(std::memory_order_acquire)
                : (strip != nullptr ? strip->getVolume() : 1.0f));
        const bool neutralVolumeOffset = std::abs(volumeRaw - 1.0f) <= 1.0e-4f;
        if (beatSpaceBaseActive && neutralVolumeOffset && !stripOutputMuted && !stripMuteReleased)
        {
            // Neutral BeatSpace offset: do not touch hosted volume so the
            // loaded BeatSpace preset remains exactly as-is.
            hostedLastDirectParamVolume[idx] = -1.0f;
            hostedLastCcVolume[idx] = -1;
        }
        else
        {
            float volume = volumeRaw;
            if (beatSpaceBaseActive)
            {
                // BeatSpace lanes: strip volume is a multiplicative offset from the
                // current BeatSpace preset value (default 1.0f = neutral).
                const float baseVolume = readBeatSpaceBaseNorm(kMicrotonicPatchLevelIndex, volumeRaw);
                volume = juce::jlimit(0.0f, 1.0f, baseVolume * volumeRaw);
            }
            if (stripOutputMuted)
                volume = 0.0f;

            const bool volumeMapped = applyHostedParameter(
                hostedDirectParamVolume[idx],
                volume,
                hostedLastDirectParamVolume[idx]);

            if (volumeMapped)
            {
                hostedLastCcVolume[idx] = -1;
            }
            else
            {
                const int ccVolume = juce::jlimit(0, 127, static_cast<int>(std::round(volume * 127.0f)));
                if (ccVolume != hostedLastCcVolume[idx])
                {
                    midi.addEvent(juce::MidiMessage::controllerEvent(channel, kHostedCcVolume, ccVolume), 0);
                    hostedLastCcVolume[idx] = ccVolume;
                }
            }
        }

        const float pan = juce::jlimit(
            -1.0f, 1.0f,
            stripPanParams[idx] != nullptr
                ? stripPanParams[idx]->load(std::memory_order_acquire)
                : (strip != nullptr ? strip->getPan() : 0.0f));
        const bool neutralPanOffset = std::abs(pan) <= 1.0e-4f;
        if (beatSpaceBaseActive && neutralPanOffset)
        {
            // Neutral BeatSpace offset: keep hosted pan exactly at preset/path value.
            hostedLastDirectParamPan[idx] = -1.0f;
            hostedLastCcPan[idx] = -1;
        }
        else
        {
            float panWithOffset = pan;
            if (beatSpaceBaseActive)
            {
                // BeatSpace lanes: strip pan is an additive offset
                // (default 0.0f = neutral).
                const float basePanNorm = readBeatSpaceBaseNorm(kMicrotonicPatchPanIndex, (pan * 0.5f) + 0.5f);
                const float basePan = juce::jlimit(-1.0f, 1.0f, (basePanNorm * 2.0f) - 1.0f);
                panWithOffset = juce::jlimit(-1.0f, 1.0f, basePan + pan);
            }

            const bool panMapped = applyHostedParameter(
                hostedDirectParamPan[idx],
                juce::jlimit(0.0f, 1.0f, (panWithOffset + 1.0f) * 0.5f),
                hostedLastDirectParamPan[idx]);

            if (panMapped)
            {
                hostedLastCcPan[idx] = -1;
            }
            else
            {
                const int ccPan = juce::jlimit(0, 127, static_cast<int>(std::round(((panWithOffset + 1.0f) * 0.5f) * 127.0f)));
                if (ccPan != hostedLastCcPan[idx])
                {
                    midi.addEvent(juce::MidiMessage::controllerEvent(channel, kHostedCcPan, ccPan), 0);
                    hostedLastCcPan[idx] = ccPan;
                }
            }
        }

        const float pitchSemitonesRaw = juce::jlimit(
            -24.0f, 24.0f,
            stripPitchParams[idx] != nullptr
                ? stripPitchParams[idx]->load(std::memory_order_acquire)
                : (strip != nullptr ? strip->getPitchShift() : 0.0f));
        const float pitchOffsetSemitones = quantizePitchSemitonesToGlobalKit(pitchSemitonesRaw);
        const bool neutralPitchOffset = std::abs(pitchOffsetSemitones) <= 1.0e-3f;
        if (beatSpaceBaseActive && neutralPitchOffset)
        {
            // Neutral BeatSpace offset: keep hosted pitch exactly at preset/path value.
            hostedLastDirectParamPitch[idx] = -1.0f;
            hostedLastCcPitch[idx] = -1;
        }
        else
        {
            float pitchSemitones = pitchOffsetSemitones;
            if (beatSpaceBaseActive)
            {
                // BeatSpace lanes: strip pitch is a semitone offset from the
                // current BeatSpace preset pitch (default 0.0f = neutral).
                const float basePitchNorm = readBeatSpaceBaseNorm(
                    kMicrotonicPatchOscFreqIndex,
                    juce::jlimit(0.0f, 1.0f, (pitchOffsetSemitones + 24.0f) / 48.0f));
                const float basePitchSemitones = juce::jlimit(-24.0f, 24.0f, (basePitchNorm * 48.0f) - 24.0f);
                pitchSemitones = juce::jlimit(-24.0f, 24.0f, basePitchSemitones + pitchOffsetSemitones);
            }

            const bool pitchMapped = applyHostedParameter(
                hostedDirectParamPitch[idx],
                juce::jlimit(0.0f, 1.0f, (pitchSemitones + 24.0f) / 48.0f),
                hostedLastDirectParamPitch[idx]);

            if (pitchMapped)
            {
                hostedLastCcPitch[idx] = -1;
            }
            else
            {
                const int ccPitch = juce::jlimit(0, 127, static_cast<int>(std::round(((pitchSemitones + 24.0f) / 48.0f) * 127.0f)));
                if (ccPitch != hostedLastCcPitch[idx])
                {
                    midi.addEvent(juce::MidiMessage::controllerEvent(channel, kHostedCcPitch, ccPitch), 0);
                    hostedLastCcPitch[idx] = ccPitch;
                }
            }
        }

        const float attackMs = juce::jlimit(
            0.0f, 400.0f,
            stripStepAttackParams[idx] != nullptr
                ? stripStepAttackParams[idx]->load(std::memory_order_acquire)
                : (strip != nullptr ? strip->getStepEnvelopeAttackMs() : 0.0f));
        const float decayMs = juce::jlimit(
            1.0f, 4000.0f,
            stripStepDecayParams[idx] != nullptr
                ? stripStepDecayParams[idx]->load(std::memory_order_acquire)
                : (strip != nullptr ? strip->getStepEnvelopeDecayMs() : 4000.0f));
        const float releaseMs = juce::jlimit(
            1.0f, 4000.0f,
            stripStepReleaseParams[idx] != nullptr
                ? stripStepReleaseParams[idx]->load(std::memory_order_acquire)
                : (strip != nullptr ? strip->getStepEnvelopeReleaseMs() : 110.0f));

        const float attackNorm = juce::jlimit(0.0f, 1.0f, attackMs / 400.0f);
        const float decayNorm = juce::jlimit(0.0f, 1.0f, (decayMs - 1.0f) / 3999.0f);
        const float releaseNorm = juce::jlimit(0.0f, 1.0f, (releaseMs - 1.0f) / 3999.0f);
        const bool neutralAttackOffset = std::abs(attackMs) <= 1.0e-3f;
        const bool neutralDecayOffset = std::abs(decayMs - 4000.0f) <= 1.0e-3f;
        const bool neutralReleaseOffset = std::abs(releaseMs - 110.0f) <= 1.0e-3f;
        if (beatSpaceBaseActive && neutralAttackOffset && neutralDecayOffset && neutralReleaseOffset)
        {
            // Neutral BeatSpace offset: preserve the loaded BeatSpace envelope.
            hostedLastDirectParamAttack[idx] = -1.0f;
            hostedLastDirectParamAttackAux[idx] = -1.0f;
            hostedLastDirectParamDecay[idx] = -1.0f;
            hostedLastDirectParamRelease[idx] = -1.0f;
        }
        else
        {
            const bool attackMapped = applyHostedParameter(
                hostedDirectParamAttack[idx],
                attackNorm,
                hostedLastDirectParamAttack[idx]);
            const bool attackAuxMapped = applyHostedParameter(
                hostedDirectParamAttackAux[idx],
                attackNorm,
                hostedLastDirectParamAttackAux[idx]);
            const bool decayMapped = applyHostedParameter(
                hostedDirectParamDecay[idx],
                decayNorm,
                hostedLastDirectParamDecay[idx]);
            const bool releaseMapped = applyHostedParameter(
                hostedDirectParamRelease[idx],
                releaseNorm,
                hostedLastDirectParamRelease[idx]);

            juce::ignoreUnused(attackMapped, attackAuxMapped, decayMapped, releaseMapped);
        }

        const int pendingProgramDelta = hostedPendingProgramDelta[idx].exchange(0, std::memory_order_relaxed);
        if (pendingProgramDelta != 0)
        {
            int nextProgram = hostedProgramNumber[idx] + pendingProgramDelta;
            nextProgram %= 128;
            if (nextProgram < 0)
                nextProgram += 128;
            hostedProgramNumber[idx] = nextProgram;
            midi.addEvent(juce::MidiMessage::programChange(channel, nextProgram), 0);
        }
    }

    if (!posInfo.getIsPlaying())
    {
        for (int stripIndex = 0; stripIndex < MaxStrips; ++stripIndex)
        {
            const auto idx = static_cast<size_t>(stripIndex);
            hostedTraversalRatioAtLastTick[idx] = -1.0;
            hostedTraversalPhaseOffsetTicks[idx] = 0.0;
        }
        return;
    }

    const double bpm = (posInfo.getBpm().hasValue() && *posInfo.getBpm() > 1.0)
        ? *posInfo.getBpm()
        : 120.0;
    const double sampleRate = juce::jmax(1.0, currentSampleRate);
    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    double ppqStartRaw = audioEngine->getTimelineBeat();
    if (posInfo.getPpqPosition().hasValue())
        ppqStartRaw = *posInfo.getPpqPosition();
    const double ppqEndRaw = ppqStartRaw + (static_cast<double>(numSamples) / samplesPerBeat);

    constexpr double kMinBeatEpsilon = 1.0e-9;
    constexpr double kNoteLengthMs = 18.0;
    const int noteLengthSamples = juce::jmax(1, static_cast<int>((kNoteLengthMs * 0.001) * sampleRate));

    auto applyStripSwingToPpq = [](const EnhancedAudioStrip& strip, double ppq)
    {
        const double swing = static_cast<double>(strip.getSwingAmount());
        if (swing <= 1.0e-4)
            return ppq;

        const double unitBeats = [&strip]()
        {
            switch (strip.getSwingDivision())
            {
                case EnhancedAudioStrip::SwingDivision::Half: return 2.0;
                case EnhancedAudioStrip::SwingDivision::Quarter: return 1.0;
                case EnhancedAudioStrip::SwingDivision::Sixteenth: return 0.25;
                case EnhancedAudioStrip::SwingDivision::ThirtySecond: return 0.125;
                case EnhancedAudioStrip::SwingDivision::SixteenthTriplet: return 1.0 / 6.0;
                case EnhancedAudioStrip::SwingDivision::Triplet: return 1.0 / 3.0;
                case EnhancedAudioStrip::SwingDivision::Eighth:
                default: return 0.5;
            }
        }();

        const double pairLength = unitBeats * 2.0;
        if (pairLength <= 1.0e-9)
            return ppq;

        const double pairIndex = std::floor(ppq / pairLength);
        const double pairBase = pairIndex * pairLength;
        const double pairPhase = ppq - pairBase;
        const double shapedSwing = std::pow(juce::jlimit(0.0, 1.0, swing), 1.7);
        const double splitShift = juce::jlimit(0.0, 0.72, shapedSwing * 0.72);
        const double splitPoint = unitBeats * (1.0 + splitShift);

        if (pairPhase < unitBeats)
        {
            const double t = pairPhase / juce::jmax(1.0e-9, unitBeats);
            return pairBase + (splitPoint * t);
        }

        const double t = (pairPhase - unitBeats) / juce::jmax(1.0e-9, unitBeats);
        return pairBase + splitPoint + ((pairLength - splitPoint) * t);
    };

    auto probabilityPass = [](int stripIndex, int64_t sequenceTick, int subIndex, float probability)
    {
        if (probability >= 0.9999f)
            return true;
        if (probability <= 0.0f)
            return false;

        const uint32_t tickHash = static_cast<uint32_t>(sequenceTick & 0xffffffffLL)
            ^ static_cast<uint32_t>((sequenceTick >> 32) & 0xffffffffLL);
        const uint32_t stripHash = static_cast<uint32_t>(stripIndex + 1);
        const uint32_t subHash = static_cast<uint32_t>(subIndex + 1);
        uint32_t seed = stripHash * 0x9e3779b9u
                      ^ (tickHash + 1u) * 0x85ebca6bu
                      ^ subHash * 0xc2b2ae35u;
        seed ^= (seed >> 16);
        seed *= 0x7feb352du;
        seed ^= (seed >> 15);
        seed *= 0x846ca68bu;
        seed ^= (seed >> 16);

        const float normalized = static_cast<float>(seed & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
        return normalized <= probability;
    };

    for (int stripIndex = 0; stripIndex < MaxStrips; ++stripIndex)
    {
        const auto idx = static_cast<size_t>(stripIndex);
        auto* strip = audioEngine->getStrip(stripIndex);
        if (!strip || !strip->isPlaying() || strip->getPlayMode() != EnhancedAudioStrip::PlayMode::Step)
            continue;
        if (hostedStripMuted[idx])
            continue;

        const int totalSteps = juce::jmax(1, strip->getStepTotalSteps());
        const int channel = getLaneMidiChannel(stripIndex);
        const int note = getLaneMidiNote(stripIndex);
        const double ppqStart = applyStripSwingToPpq(*strip, ppqStartRaw);
        const double ppqEnd = applyStripSwingToPpq(*strip, ppqEndRaw);
        const double stepTraversalRatio = juce::jmax(
            0.125,
            static_cast<double>(PlayheadSpeedQuantizer::quantizeRatio(strip->getPlayheadSpeedRatio())));
        const double stepEventsPerPpq = juce::jmax(1.0e-6, 4.0 * stepTraversalRatio);
        auto& traversalRatioAtLastTick = hostedTraversalRatioAtLastTick[idx];
        auto& traversalPhaseOffsetTicks = hostedTraversalPhaseOffsetTicks[idx];
        if (traversalRatioAtLastTick <= 0.0
            || !std::isfinite(traversalRatioAtLastTick)
            || std::abs(stepTraversalRatio - traversalRatioAtLastTick) > 1.0e-6)
        {
            traversalRatioAtLastTick = stepTraversalRatio;
        }
        // Keep hosted step lanes strictly locked to host PPQ grid.
        // Speed changes alter density/rate but never retain a phase offset.
        traversalPhaseOffsetTicks = 0.0;

        const double stepLengthPpq = 1.0 / stepEventsPerPpq;
        const double sequenceStart = ppqStart * stepEventsPerPpq;
        const double sequenceEnd = ppqEnd * stepEventsPerPpq;
        int64_t startTick = static_cast<int64_t>(std::floor(sequenceStart));
        int64_t endTick = static_cast<int64_t>(std::floor(sequenceEnd));
        if (endTick < startTick)
            std::swap(startTick, endTick);

        for (int64_t sequenceTick = startTick; sequenceTick <= endTick; ++sequenceTick)
        {
            const double stepPpq = static_cast<double>(sequenceTick) / stepEventsPerPpq;

            const int baseStep = static_cast<int>(((sequenceTick % totalSteps) + totalSteps) % totalSteps);
            int stepWithin = baseStep;

            switch (strip->getDirectionMode())
            {
                case EnhancedAudioStrip::DirectionMode::Reverse:
                    stepWithin = (totalSteps - 1) - baseStep;
                    break;
                case EnhancedAudioStrip::DirectionMode::PingPong:
                {
                    const int pingPongLen = juce::jmax(1, totalSteps * 2);
                    const int cycle = static_cast<int>(((sequenceTick % pingPongLen) + pingPongLen) % pingPongLen);
                    stepWithin = (cycle < totalSteps) ? cycle : ((pingPongLen - 1) - cycle);
                    break;
                }
                case EnhancedAudioStrip::DirectionMode::Normal:
                case EnhancedAudioStrip::DirectionMode::Random:
                case EnhancedAudioStrip::DirectionMode::RandomWalk:
                case EnhancedAudioStrip::DirectionMode::RandomSlice:
                default:
                    stepWithin = baseStep;
                    break;
            }

            const float startVelocity = strip->getStepSubdivisionStartVelocityAtIndex(stepWithin);
            const float repeatVelocity = strip->getStepSubdivisionRepeatVelocityAtIndex(stepWithin);
            const int authoredSubdivisions = juce::jmax(1, strip->getStepSubdivisionAtIndex(stepWithin));
            const bool hasRampShape = std::abs(repeatVelocity - startVelocity) > 0.001f;
            // Ramps must always emit repeated hosted MIDI notes, even if an older preset/state
            // left subdivision at 1 while storing a non-flat velocity range.
            const int subdivisions = juce::jmax(authoredSubdivisions, hasRampShape ? 2 : 1);
            // Preserve backward compatibility for steps authored through divide/ramp tools:
            // a shaped or subdivided step should still emit hosted MIDI even if the gate bit is off.
            const bool stepActive = strip->stepPattern[static_cast<size_t>(stepWithin)] || subdivisions > 1;
            if (!stepActive)
                continue;

            const float probability = strip->getStepProbabilityAtIndex(stepWithin);
            const double subdivisionBeats = stepLengthPpq / static_cast<double>(subdivisions);

            for (int sub = 0; sub < subdivisions; ++sub)
            {
                const double hitPpq = stepPpq + (static_cast<double>(sub) * subdivisionBeats);
                if (hitPpq < ppqStart || hitPpq >= (ppqEnd - kMinBeatEpsilon))
                    continue;
                if (!probabilityPass(stripIndex, sequenceTick, sub, probability))
                    continue;

                const double ratio = (subdivisions > 1)
                    ? static_cast<double>(sub) / static_cast<double>(subdivisions - 1)
                    : 0.0;
                const float velocityNorm = juce::jlimit(0.0f, 1.0f,
                    juce::jmap(static_cast<float>(ratio), startVelocity, repeatVelocity));
                const int velocity = juce::jlimit(1, 127, static_cast<int>(std::round(velocityNorm * 127.0f)));

                const int sampleOffset = static_cast<int>(juce::jlimit(
                    0.0,
                    static_cast<double>(juce::jmax(0, numSamples - 1)),
                    (hitPpq - ppqStart) * samplesPerBeat));

                midi.addEvent(juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(velocity)),
                              sampleOffset);
                const int noteOffOffset = juce::jmin(numSamples - 1, sampleOffset + noteLengthSamples);
                midi.addEvent(juce::MidiMessage::noteOff(channel, note), noteOffOffset);
            }
        }
    }
}

//==============================================================================
bool StepVstHostAudioProcessor::loadSampleToStrip(int stripIndex, const juce::File& file)
{
    if (file.existsAsFile() && stripIndex >= 0 && stripIndex < MaxStrips)
    {
        // Remember the folder for browsing context, but do NOT change
        // default XML paths here. Those are updated only by explicit
        // manual path selections (load button / Paths tab).
        lastSampleFolder = file.getParentDirectory();

        const bool loaded = audioEngine->loadSampleToStrip(stripIndex, file);
        if (loaded)
            currentStripFiles[static_cast<size_t>(stripIndex)] = file;

        return loaded;
    }

    return false;
}

void StepVstHostAudioProcessor::setPendingBarLengthApply(int stripIndex, bool pending)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return;

    pendingBarLengthApply[static_cast<size_t>(stripIndex)] = pending;
}

bool StepVstHostAudioProcessor::canChangeBarLengthNow(int stripIndex) const
{
    if (!audioEngine || stripIndex < 0 || stripIndex >= MaxStrips)
        return false;

    auto* strip = audioEngine->getStrip(stripIndex);
    if (!strip)
        return false;

    if (!strip->hasAudio() || !strip->isPlaying())
        return true;

    if (!strip->isPpqTimelineAnchored())
        return false;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (position->getPpqPosition().hasValue())
                return std::isfinite(*position->getPpqPosition());
        }
    }

    return false;
}

void StepVstHostAudioProcessor::requestBarLengthChange(int stripIndex, int bars)
{
    if (!audioEngine || stripIndex < 0 || stripIndex >= MaxStrips)
        return;

    auto* strip = audioEngine->getStrip(stripIndex);
    if (!strip)
        return;

    const auto selection = decodeBarSelection(bars);
    setPendingBarLengthApply(stripIndex, false);

    if (!strip->hasAudio())
    {
        strip->setRecordingBars(selection.recordingBars);
        strip->setBeatsPerLoop(selection.beatsPerLoop);
        const juce::ScopedLock lock(pendingBarChangeLock);
        pendingBarChanges[static_cast<size_t>(stripIndex)].active = false;
        return;
    }

    if (!strip->isPlaying())
    {
        strip->setRecordingBars(selection.recordingBars);
        strip->setBeatsPerLoop(selection.beatsPerLoop);
        const juce::ScopedLock lock(pendingBarChangeLock);
        pendingBarChanges[static_cast<size_t>(stripIndex)].active = false;
        return;
    }

    const int quantizeDivision = getQuantizeDivision();
    // Bar changes are always PPQ-grid scheduled when host PPQ is valid.
    const bool useQuantize = (quantizeDivision >= 1);

    bool hasHostPpq = false;
    double currentPpq = std::numeric_limits<double>::quiet_NaN();
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (position->getPpqPosition().hasValue())
            {
                hasHostPpq = true;
                currentPpq = *position->getPpqPosition();
            }
        }
    }

    const bool syncReadyNow = hasHostPpq
        && std::isfinite(currentPpq)
        && strip->isPpqTimelineAnchored();

    const juce::ScopedLock lock(pendingBarChangeLock);
    auto& pending = pendingBarChanges[static_cast<size_t>(stripIndex)];
    pending.active = true;
    pending.recordingBars = selection.recordingBars;
    pending.beatsPerLoop = selection.beatsPerLoop;
    pending.quantized = useQuantize;
    pending.quantizeDivision = quantizeDivision;
    pending.targetPpq = std::numeric_limits<double>::quiet_NaN();

    // If PPQ/anchor is not currently valid, keep request pending and resolve the
    // target grid on the first PPQ-valid anchored audio block.
    if (!syncReadyNow)
        return;

    if (!pending.quantized)
        return;
    // Resolve quantized target on the audio thread to avoid GUI/playhead clock skew.
}

int StepVstHostAudioProcessor::getQuantizeDivision() const
{
    const auto* quantizeParamLocal = quantizeParam != nullptr
        ? quantizeParam
        : parameters.getRawParameterValue("quantize");
    const int quantizeChoice = quantizeParamLocal ? static_cast<int>(quantizeParamLocal->load(std::memory_order_acquire)) : 5;
    static constexpr std::array<int, 10> kDivisionMap {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};
    const int clampedChoice = juce::jlimit(0, static_cast<int>(kDivisionMap.size()) - 1, quantizeChoice);
    return kDivisionMap[static_cast<size_t>(clampedChoice)];
}

float StepVstHostAudioProcessor::getInnerLoopLengthFactor() const
{
    return 1.0f;
}

void StepVstHostAudioProcessor::queueLoopChange(int stripIndex, bool clearLoop, int startColumn, int endColumn, bool reverseDirection, int markerColumn)
{
    if (!audioEngine || stripIndex < 0 || stripIndex >= MaxStrips)
        return;

    auto* strip = audioEngine->getStrip(stripIndex);
    if (!strip)
        return;

    const int quantizeDivision = getQuantizeDivision();
    // PPQ safety: clearing an active inner loop must always be grid-scheduled.
    const bool useQuantize = clearLoop || (quantizeDivision > 1);

    if (!useQuantize)
    {
        {
            const juce::ScopedLock lock(pendingLoopChangeLock);
            pendingLoopChanges[static_cast<size_t>(stripIndex)].active = false;
        }

        bool markerApplied = false;
        if (clearLoop)
        {
            strip->clearLoop();
            strip->setReverse(false);
            strip->setDirectionMode(EnhancedAudioStrip::DirectionMode::Normal);
            if (markerColumn >= 0)
            {
                strip->setPlaybackMarkerColumn(markerColumn, audioEngine->getGlobalSampleCount());
                markerApplied = true;
            }
        }
        else
        {
            strip->setLoop(startColumn, endColumn);
            strip->setDirectionMode(reverseDirection
                ? EnhancedAudioStrip::DirectionMode::Reverse
                : EnhancedAudioStrip::DirectionMode::Normal);
            if (markerColumn >= 0)
            {
                strip->setPlaybackMarkerColumn(markerColumn, audioEngine->getGlobalSampleCount());
                markerApplied = true;
            }
        }

        if (!markerApplied && strip->isPlaying() && strip->hasAudio())
            strip->snapToTimeline(audioEngine->getGlobalSampleCount());
        return;
    }

    double currentPpq = audioEngine->getTimelineBeat();
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (position->getPpqPosition().hasValue())
                currentPpq = *position->getPpqPosition();
        }
    }

    if (!std::isfinite(currentPpq))
    {
        // Strict PPQ safety: reject quantized loop changes until PPQ is valid.
        return;
    }

    const double quantBeats = 4.0 / static_cast<double>(quantizeDivision);
    double targetPpq = std::ceil(currentPpq / quantBeats) * quantBeats;
    if (targetPpq <= (currentPpq + 1.0e-6))
        targetPpq += quantBeats;
    targetPpq = std::round(targetPpq / quantBeats) * quantBeats;

    const juce::ScopedLock lock(pendingLoopChangeLock);
    auto& pending = pendingLoopChanges[static_cast<size_t>(stripIndex)];
    pending.active = true;
    pending.clear = clearLoop;
    pending.startColumn = juce::jlimit(0, MaxColumns - 1, startColumn);
    pending.endColumn = juce::jlimit(pending.startColumn + 1, MaxColumns, endColumn);
    pending.markerColumn = juce::jlimit(-1, MaxColumns - 1, markerColumn);
    pending.reverse = reverseDirection;
    pending.quantized = true;
    pending.targetPpq = targetPpq;
    pending.quantizeDivision = quantizeDivision;
    pending.postClearTriggerArmed = false;
    pending.postClearTriggerColumn = 0;
}

void StepVstHostAudioProcessor::applyPendingLoopChanges(const juce::AudioPlayHead::PositionInfo& posInfo)
{
    if (!audioEngine)
        return;

    double currentPpq = audioEngine->getTimelineBeat();
    if (posInfo.getPpqPosition().hasValue())
        currentPpq = *posInfo.getPpqPosition();
    const double currentTempo = (posInfo.getBpm().hasValue() && *posInfo.getBpm() > 0.0)
        ? *posInfo.getBpm()
        : audioEngine->getCurrentTempo();

    std::array<PendingLoopChange, MaxStrips> readyChanges{};
    {
        const juce::ScopedLock lock(pendingLoopChangeLock);
        for (int i = 0; i < MaxStrips; ++i)
        {
            auto& pending = pendingLoopChanges[static_cast<size_t>(i)];
            if (!pending.active)
                continue;

            bool canApplyNow = false;
            if (!pending.quantized)
            {
                canApplyNow = std::isfinite(currentPpq);
            }
            else if (std::isfinite(currentPpq))
            {
                if (!std::isfinite(pending.targetPpq))
                {
                    const int division = juce::jmax(1, pending.quantizeDivision);
                    const double quantBeats = 4.0 / static_cast<double>(division);
                    double targetPpq = std::ceil(currentPpq / quantBeats) * quantBeats;
                    if (targetPpq <= (currentPpq + 1.0e-6))
                        targetPpq += quantBeats;
                    pending.targetPpq = std::round(targetPpq / quantBeats) * quantBeats;
                    continue;
                }

                auto* strip = audioEngine->getStrip(i);
                const bool hasAnchor = (strip != nullptr) && strip->isPpqTimelineAnchored();
                const bool targetReached = (currentPpq + 1.0e-6 >= pending.targetPpq);
                if (targetReached && !hasAnchor)
                {
                    // Strict PPQ safety: never apply late/off-grid.
                    // If not anchor-safe at this grid, roll to the next grid.
                    const int division = juce::jmax(1, pending.quantizeDivision);
                    const double quantBeats = 4.0 / static_cast<double>(division);
                    double nextTarget = std::ceil(currentPpq / quantBeats) * quantBeats;
                    if (nextTarget <= (currentPpq + 1.0e-6))
                        nextTarget += quantBeats;
                    pending.targetPpq = std::round(nextTarget / quantBeats) * quantBeats;
                    continue;
                }
                canApplyNow = hasAnchor && targetReached;
            }

            if (!canApplyNow)
                continue;

            readyChanges[static_cast<size_t>(i)] = pending;
            pending.active = false;
        }
    }

    const int64_t currentGlobalSample = audioEngine->getGlobalSampleCount();
    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto& change = readyChanges[static_cast<size_t>(i)];
        if (!change.active)
            continue;

        auto* strip = audioEngine->getStrip(i);
        if (!strip)
            continue;

        bool triggeredAtColumn = false;
        if (change.clear)
        {
            strip->clearLoop();
            strip->setReverse(false);
            strip->setDirectionMode(EnhancedAudioStrip::DirectionMode::Normal);
            if (change.markerColumn >= 0 && std::isfinite(currentPpq) && currentTempo > 0.0)
            {
                juce::AudioPlayHead::PositionInfo retriggerPosInfo;
                const double applyPpq = (change.quantized && std::isfinite(change.targetPpq))
                    ? change.targetPpq
                    : currentPpq;
                retriggerPosInfo.setPpqPosition(applyPpq);
                retriggerPosInfo.setBpm(currentTempo);
                strip->triggerAtSample(change.markerColumn, currentTempo, currentGlobalSample, retriggerPosInfo);
                triggeredAtColumn = true;
            }
            else if (change.markerColumn >= 0)
            {
                strip->setPlaybackMarkerColumn(change.markerColumn, currentGlobalSample);
            }
        }
        else
        {
            strip->setLoop(change.startColumn, change.endColumn);
            strip->setDirectionMode(change.reverse
                ? EnhancedAudioStrip::DirectionMode::Reverse
                : EnhancedAudioStrip::DirectionMode::Normal);
            if (change.markerColumn >= 0 && std::isfinite(currentPpq) && currentTempo > 0.0)
            {
                juce::AudioPlayHead::PositionInfo retriggerPosInfo;
                const double applyPpq = (change.quantized && std::isfinite(change.targetPpq))
                    ? change.targetPpq
                    : currentPpq;
                retriggerPosInfo.setPpqPosition(applyPpq);
                retriggerPosInfo.setBpm(currentTempo);
                strip->triggerAtSample(change.markerColumn, currentTempo, currentGlobalSample, retriggerPosInfo);
                triggeredAtColumn = true;
            }
            else if (change.markerColumn >= 0)
            {
                strip->setPlaybackMarkerColumn(change.markerColumn, currentGlobalSample);
                triggeredAtColumn = true;
            }
        }

        if (change.quantized && !triggeredAtColumn)
        {
            // Deterministic PPQ realign after loop-geometry change.
            const double applyPpq = std::isfinite(currentPpq)
                ? currentPpq
                : (std::isfinite(change.targetPpq) ? change.targetPpq : audioEngine->getTimelineBeat());
            strip->realignToPpqAnchor(applyPpq, currentGlobalSample);
            strip->setBeatsPerLoopAtPpq(strip->getBeatsPerLoop(), applyPpq);
        }
        else
        {
            const bool markerApplied = (change.markerColumn >= 0);
            if (!markerApplied && strip->isPlaying() && strip->hasAudio())
                strip->snapToTimeline(currentGlobalSample);
        }

        // Inner-loop clear gesture: the NEXT pad press while clear is pending
        // becomes the start column after exit, quantized like normal triggers.
        if (change.clear && change.postClearTriggerArmed)
        {
            const int targetColumn = juce::jlimit(0, MaxColumns - 1, change.postClearTriggerColumn);
            const int quantizeDivision = getQuantizeDivision();
            const bool useQuantize = quantizeDivision > 1;
            audioEngine->triggerStripWithQuantization(i, targetColumn, useQuantize);
        }
    }
}

void StepVstHostAudioProcessor::applyPendingBarChanges(const juce::AudioPlayHead::PositionInfo& posInfo)
{
    if (!audioEngine)
        return;

    if (!posInfo.getPpqPosition().hasValue())
        return;

    const double currentPpq = *posInfo.getPpqPosition();

    std::array<PendingBarChange, MaxStrips> readyChanges{};
    {
        const juce::ScopedLock lock(pendingBarChangeLock);
        for (int i = 0; i < MaxStrips; ++i)
        {
            auto& pending = pendingBarChanges[static_cast<size_t>(i)];
            if (!pending.active)
                continue;

            auto* strip = audioEngine->getStrip(i);
            const bool stripApplyReady = (strip != nullptr) && strip->hasAudio() && strip->isPlaying();
            const bool anchorReady = stripApplyReady && strip->isPpqTimelineAnchored();

            if (pending.quantized && !std::isfinite(pending.targetPpq))
            {
                if (!std::isfinite(currentPpq) || !anchorReady)
                    continue;

                const int division = juce::jmax(1, pending.quantizeDivision);
                const double quantBeats = 4.0 / static_cast<double>(division);
                double targetPpq = std::ceil(currentPpq / quantBeats) * quantBeats;
                if (targetPpq <= (currentPpq + 1.0e-6))
                    targetPpq += quantBeats;
                pending.targetPpq = std::round(targetPpq / quantBeats) * quantBeats;
                continue;
            }

            bool canApplyNow = false;
            if (!pending.quantized)
            {
                canApplyNow = std::isfinite(currentPpq)
                    && stripApplyReady
                    && strip->isPpqTimelineAnchored();
            }
            else if (std::isfinite(currentPpq) && std::isfinite(pending.targetPpq))
            {
                const bool hasAnchor = stripApplyReady && strip->isPpqTimelineAnchored();
                const bool targetReached = (currentPpq + 1.0e-6 >= pending.targetPpq);

                if (targetReached && !hasAnchor)
                {
                    // Keep the request alive and roll to the next grid if this
                    // strip is not anchor-safe on the current grid.
                    const int division = juce::jmax(1, pending.quantizeDivision);
                    const double quantBeats = 4.0 / static_cast<double>(division);
                    double nextTarget = std::ceil(currentPpq / quantBeats) * quantBeats;
                    if (nextTarget <= (currentPpq + 1.0e-6))
                        nextTarget += quantBeats;
                    pending.targetPpq = std::round(nextTarget / quantBeats) * quantBeats;
                    continue;
                }

                canApplyNow = hasAnchor && targetReached;
            }

            if (!canApplyNow)
                continue;

            readyChanges[static_cast<size_t>(i)] = pending;
            pending.active = false;
        }
    }

    double currentTempo = audioEngine->getCurrentTempo();
    if (posInfo.getBpm().hasValue() && *posInfo.getBpm() > 0.0)
        currentTempo = *posInfo.getBpm();

    const int64_t currentGlobalSample = audioEngine->getGlobalSampleCount();
    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto& change = readyChanges[static_cast<size_t>(i)];
        if (!change.active)
            continue;

        auto* strip = audioEngine->getStrip(i);
        if (!strip || !strip->hasAudio() || !strip->isPlaying())
            continue;

        const double applyPpq = (change.quantized && std::isfinite(change.targetPpq))
            ? change.targetPpq
            : currentPpq;
        strip->setRecordingBars(change.recordingBars);
        strip->setBeatsPerLoopAtPpq(change.beatsPerLoop, applyPpq);
        if (std::isfinite(applyPpq) && currentTempo > 0.0)
        {
            // Match the preset-restore path so bar remaps re-anchor deterministically.
            strip->restorePresetPpqState(true,
                                         true,
                                         strip->getPpqTimelineOffsetBeats(),
                                         strip->getCurrentColumn(),
                                         currentTempo,
                                         applyPpq,
                                         currentGlobalSample);
        }
        // After target-grid remap, force a hard lock to the *current* host PPQ
        // so trigger/fallback references are consistent within this audio block.
        strip->realignToPpqAnchor(currentPpq, currentGlobalSample);
    }
}

void StepVstHostAudioProcessor::applyPendingStutterStart(const juce::AudioPlayHead::PositionInfo& posInfo)
{
    if (!audioEngine || pendingStutterStartActive.load(std::memory_order_acquire) == 0)
        return;

    double currentPpq = audioEngine->getTimelineBeat();
    if (posInfo.getPpqPosition().hasValue())
        currentPpq = *posInfo.getPpqPosition();

    double targetPpq = pendingStutterStartPpq.load(std::memory_order_acquire);
    const int64_t currentSample = audioEngine->getGlobalSampleCount();
    const int startQuantizeDivision = juce::jmax(1,
        pendingStutterStartQuantizeDivision.load(std::memory_order_acquire));
    const double startQuantizeBeats = 4.0 / static_cast<double>(startQuantizeDivision);

    // Match inner-loop quantized scheduling:
    // resolve target grid on audio thread to avoid GUI/playhead clock skew.
    if (!(std::isfinite(targetPpq) && targetPpq >= 0.0))
    {
        if (!(std::isfinite(currentPpq) && currentPpq >= 0.0))
            return;

        targetPpq = std::ceil(currentPpq / startQuantizeBeats) * startQuantizeBeats;
        if (targetPpq <= (currentPpq + 1.0e-6))
            targetPpq += startQuantizeBeats;
        targetPpq = std::round(targetPpq / startQuantizeBeats) * startQuantizeBeats;
        pendingStutterStartPpq.store(targetPpq, std::memory_order_release);
        pendingStutterStartSampleTarget.store(-1, std::memory_order_release);
        return;
    }

    if (!(std::isfinite(currentPpq) && currentPpq >= 0.0))
        return;

    if (currentPpq + 1.0e-6 < targetPpq)
        return;

    double applyPpq = targetPpq;

    bool hasAnyPlayingStrip = false;
    bool anchorsReady = true;
    for (int i = 0; i < MaxStrips; ++i)
    {
        auto* strip = audioEngine->getStrip(i);
        const bool stepMode = (strip && strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step);
        const auto* stepSampler = stepMode && strip ? strip->getStepSampler() : nullptr;
        const bool hasPlayableContent = strip
            && (strip->hasAudio() || (stepSampler && stepSampler->getHasAudio()));
        if (!strip || !hasPlayableContent || !strip->isPlaying())
            continue;
        hasAnyPlayingStrip = true;
        if (!stepMode && !strip->isPpqTimelineAnchored())
        {
            anchorsReady = false;
            break;
        }
    }

    // If active strips exist but PPQ anchors are still settling, don't keep
    // deferring forever; start stutter from the current host PPQ.
    if (hasAnyPlayingStrip && !anchorsReady)
        applyPpq = currentPpq;

    pendingStutterStartActive.store(0, std::memory_order_release);
    pendingStutterStartPpq.store(-1.0, std::memory_order_release);
    pendingStutterStartSampleTarget.store(-1, std::memory_order_release);

    if (!std::isfinite(applyPpq))
        applyPpq = audioEngine->getTimelineBeat();
    performMomentaryStutterStartNow(applyPpq, currentSample);
}

void StepVstHostAudioProcessor::applyPendingStutterRelease(const juce::AudioPlayHead::PositionInfo& posInfo)
{
    if (!audioEngine || pendingStutterReleaseActive.load(std::memory_order_acquire) == 0)
        return;

    double currentPpq = audioEngine->getTimelineBeat();
    if (posInfo.getPpqPosition().hasValue())
        currentPpq = *posInfo.getPpqPosition();

    const double targetPpq = pendingStutterReleasePpq.load(std::memory_order_acquire);
    const int64_t currentSample = audioEngine->getGlobalSampleCount();
    const int64_t targetSample = pendingStutterReleaseSampleTarget.load(std::memory_order_acquire);

    bool releaseReady = false;
    double applyPpq = currentPpq;

    // Primary path: PPQ-locked release.
    if (std::isfinite(targetPpq) && std::isfinite(currentPpq))
    {
        releaseReady = (currentPpq + 1.0e-6 >= targetPpq);
        applyPpq = targetPpq;
    }
    // Fallback path: sample-target release if PPQ is unavailable.
    else if (targetSample >= 0)
    {
        releaseReady = (currentSample >= targetSample);
    }
    // Safety fallback: never stay latched forever when host is not playing.
    else if (!posInfo.getIsPlaying())
    {
        releaseReady = true;
    }

    if (!releaseReady)
        return;

    pendingStutterReleaseActive.store(0, std::memory_order_release);
    pendingStutterReleasePpq.store(-1.0, std::memory_order_release);
    pendingStutterReleaseSampleTarget.store(-1, std::memory_order_release);

    if (!std::isfinite(applyPpq))
        applyPpq = audioEngine->getTimelineBeat();
    performMomentaryStutterReleaseNow(applyPpq, currentSample);
}

void StepVstHostAudioProcessor::captureMomentaryStutterMacroBaseline()
{
    if (!audioEngine)
        return;

    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        auto& saved = momentaryStutterSavedState[idx];
        saved = MomentaryStutterSavedStripState{};

        auto* strip = audioEngine->getStrip(i);
        const bool stepMode = (strip && strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step);
        const auto* stepSampler = stepMode && strip ? strip->getStepSampler() : nullptr;
        const bool hasPlayableContent = strip
            && (strip->hasAudio() || (stepSampler && stepSampler->getHasAudio()));
        if (!strip || !momentaryStutterStripArmed[idx] || !hasPlayableContent || !strip->isPlaying())
            continue;

        saved.valid = true;
        saved.stepMode = stepMode;
        saved.pan = (stepSampler != nullptr) ? stepSampler->getPan() : strip->getPan();
        saved.playbackSpeed = strip->getPlaybackSpeed();
        saved.pitchSemitones = getPitchSemitonesForDisplay(*strip);
        saved.pitchShift = strip->getPitchShift();
        saved.loopSliceLength = strip->getLoopSliceLength();
        saved.filterEnabled = strip->isFilterEnabled();
        saved.filterFrequency = strip->getFilterFrequency();
        saved.filterResonance = strip->getFilterResonance();
        saved.filterMorph = strip->getFilterMorph();
        saved.filterAlgorithm = strip->getFilterAlgorithm();
        if (stepSampler != nullptr)
        {
            saved.stepFilterEnabled = stepSampler->isFilterEnabled();
            saved.stepFilterFrequency = stepSampler->getFilterFrequency();
            saved.stepFilterResonance = stepSampler->getFilterResonance();
            saved.stepFilterType = stepSampler->getFilterType();
        }
    }

    momentaryStutterMacroBaselineCaptured = true;
    momentaryStutterMacroCapturePending = false;
}

void StepVstHostAudioProcessor::applyMomentaryStutterMacro(const juce::AudioPlayHead::PositionInfo& posInfo)
{
    if (!audioEngine
        || !momentaryStutterHoldActive
        || momentaryStutterPlaybackActive.load(std::memory_order_acquire) == 0)
        return;

    if (!posInfo.getPpqPosition().hasValue())
        return;

    const double ppqNow = *posInfo.getPpqPosition();
    if (!std::isfinite(ppqNow))
        return;

    if (momentaryStutterMacroCapturePending || !momentaryStutterMacroBaselineCaptured)
        captureMomentaryStutterMacroBaseline();
    if (!momentaryStutterMacroBaselineCaptured)
        return;

    const uint8_t stutterMaskLimit = static_cast<uint8_t>((1u << static_cast<unsigned int>(kStutterButtonCount)) - 1u);
    uint8_t comboMask = static_cast<uint8_t>(momentaryStutterButtonMask.load(std::memory_order_acquire) & stutterMaskLimit);
    if (comboMask == 0)
        comboMask = stutterButtonBitFromColumn(momentaryStutterActiveDivisionButton);
    if (comboMask == 0)
        return;

    const int bitCount = countStutterBits(comboMask);
    const int highestBit = highestStutterBit(comboMask);
    const int lowestBit = lowestStutterBit(comboMask);
    const bool comboChanged = (comboMask != momentaryStutterLastComboMask);
    const int seed = (static_cast<int>(comboMask) * 97)
        + (bitCount * 19)
        + (highestBit * 11)
        + (lowestBit * 5);
    const int variant = seed % 8;
    const bool singleButton = (bitCount == 1);
    const bool multiButton = (bitCount >= 2);
    const bool twoButton = (bitCount == 2);
    const bool allowPitchSpeedMacro = (bitCount >= 3);
    const bool allowPitchMacro = (bitCount >= 3);
    const bool applySpeedMacro = (bitCount >= 2);
    const bool threeButton = (bitCount == 3);
    const bool hardStepMode = (variant >= 4);
    auto restoreSavedState = [this](EnhancedAudioStrip& strip, const MomentaryStutterSavedStripState& saved)
    {
        strip.setPan(saved.pan);
        strip.setPlaybackSpeedImmediate(saved.playbackSpeed);
        strip.setLoopSliceLength(saved.loopSliceLength);

        if (saved.stepMode)
        {
            applyPitchControlToStrip(strip, saved.pitchSemitones);
            if (auto* stepSampler = strip.getStepSampler())
            {
                stepSampler->setPan(saved.pan);
                stepSampler->setFilterEnabled(saved.stepFilterEnabled);
                stepSampler->setFilterFrequency(saved.stepFilterFrequency);
                stepSampler->setFilterResonance(saved.stepFilterResonance);
                stepSampler->setFilterType(saved.stepFilterType);
            }
        }
        else
        {
            applyPitchControlToStrip(strip, saved.pitchShift);
        }

        strip.setFilterAlgorithm(saved.filterAlgorithm);
        strip.setFilterFrequency(saved.filterFrequency);
        strip.setFilterResonance(saved.filterResonance);
        strip.setFilterMorph(saved.filterMorph);
        strip.setFilterEnabled(saved.filterEnabled);
    };

    if (singleButton)
    {
        audioEngine->setMomentaryStutterDivision(
            juce::jlimit(0.03125, 4.0, stutterDivisionBeatsFromBit(highestBit)));
        audioEngine->setMomentaryStutterRetriggerFadeMs(0.7f);

        for (int i = 0; i < MaxStrips; ++i)
        {
            const auto idx = static_cast<size_t>(i);
            const auto& saved = momentaryStutterSavedState[idx];
            if (!saved.valid || !momentaryStutterStripArmed[idx])
                continue;

            auto* strip = audioEngine->getStrip(i);
            const bool stepMode = (strip && strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step);
            const auto* stepSampler = stepMode && strip ? strip->getStepSampler() : nullptr;
            const bool hasPlayableContent = strip
                && (strip->hasAudio() || (stepSampler && stepSampler->getHasAudio()));
            if (!strip || !hasPlayableContent || !strip->isPlaying())
                continue;

            restoreSavedState(*strip, saved);
        }

        momentaryStutterLastComboMask = comboMask;
        momentaryStutterTwoButtonStepBaseValid = false;
        momentaryStutterTwoButtonStepBase = 0;
        return;
    }

    int lengthBars = 1 + ((seed / 13) % 4);
    if (twoButton)
        lengthBars = 1 + (((seed / 31) + highestBit + lowestBit) & 0x3);
    else if (bitCount >= 4)
        lengthBars = juce::jlimit(2, 4, 2 + (((seed / 17) + highestBit) & 0x1));
    const double cycleBeats = 4.0 * static_cast<double>(lengthBars);
    if (cycleBeats <= 0.0 || !std::isfinite(cycleBeats))
        return;

    const double cycleBeatPosRaw = std::fmod(ppqNow - momentaryStutterMacroStartPpq, cycleBeats);
    const double cycleBeatPos = cycleBeatPosRaw < 0.0 ? cycleBeatPosRaw + cycleBeats : cycleBeatPosRaw;
    const double phase = wrapUnitPhase(cycleBeatPos / cycleBeats);
    const int threeButtonContour = threeButton
        ? (((seed / 29) + variant + highestBit + (lowestBit * 2)) % 4)
        : 0;
    int stepsPerBar = 8;
    if (multiButton)
    {
        const int rhythmClass = ((seed / 7) + highestBit + lowestBit) % 4;
        if (rhythmClass == 1)
            stepsPerBar = 16;
    }
    const int totalSteps = juce::jmax(8, stepsPerBar * lengthBars);
    const int stepIndex = juce::jlimit(0, totalSteps - 1, static_cast<int>(std::floor(phase * static_cast<double>(totalSteps))));
    const int stepLoop = stepIndex % 8;
    const float normStep = static_cast<float>(stepLoop) / 7.0f;

    const uint8_t maskBit10 = static_cast<uint8_t>(1u << 1);
    const uint8_t maskBit12 = static_cast<uint8_t>(1u << 3);
    const uint8_t maskBit13 = static_cast<uint8_t>(1u << 4);
    const uint8_t maskBit15 = static_cast<uint8_t>(1u << 6);
    const uint8_t maskBit11 = static_cast<uint8_t>(1u << 2);
    const bool combo10And13 = (comboMask == static_cast<uint8_t>(maskBit10 | maskBit13));
    const bool combo11And13 = (comboMask == static_cast<uint8_t>(maskBit11 | maskBit13));
    const bool combo12And13And15 = (comboMask == static_cast<uint8_t>(maskBit12 | maskBit13 | maskBit15));
    const bool hasTopStutterBit = ((comboMask & maskBit15) != 0);
    const float comboIntensity = juce::jlimit(0.25f, 1.0f, 0.34f + (0.16f * static_cast<float>(bitCount - 1)));
    const double heldBeatsRaw = juce::jmax(0.0, ppqNow - momentaryStutterMacroStartPpq);
    const float heldRamp = juce::jlimit(0.0f, 1.0f, static_cast<float>(heldBeatsRaw / 8.0));

    float shapeIntensity = 1.0f;
    float speedMult = 1.0f;
    float panPattern = 0.0f;
    float pitchPattern = 0.0f;
    float cutoffNorm = 0.85f;
    float targetResonance = 1.2f;
    float targetMorph = 0.25f;
    float panDepthShape = 1.0f;
    float twoButtonSemitoneStep = 0.0f;
    float twoButtonSemitoneSpeedRatio = 1.0f;
    bool twoButtonUseFilter = true;
    bool twoButtonDirectionUp = true;
    int twoButtonStepAbs = 0;
    double dynamicStutterDivisionBeats = stutterDivisionBeatsFromBitForMacro(highestBit, multiButton);

    if (variant < 4)
    {
        // Smooth musical movement modes (continuous phase paths).
        const double fastPhase = wrapUnitPhase(phase * static_cast<double>(2 + ((seed >> 2) % 5)));
        const double panPhase = wrapUnitPhase(phase * static_cast<double>(1 + ((seed >> 4) % 4)));
        const double filterPhase = wrapUnitPhase(phase * static_cast<double>(1 + ((seed >> 6) % 3)));
        const double tri = 1.0 - std::abs((phase * 2.0) - 1.0);
        const double triSigned = (tri * 2.0) - 1.0;
        const double sawSigned = (phase * 2.0) - 1.0;
        const double sine = std::sin(juce::MathConstants<double>::twoPi * phase);
        const double sineFast = std::sin(juce::MathConstants<double>::twoPi * fastPhase);
        const double panSine = std::sin(juce::MathConstants<double>::twoPi * panPhase);
        const double filterTri = 1.0 - std::abs((filterPhase * 2.0) - 1.0);

        switch (variant)
        {
            case 0: // riser
                shapeIntensity = juce::jlimit(0.18f, 1.0f, static_cast<float>(phase));
                speedMult = juce::jlimit(0.70f, 2.40f, static_cast<float>(0.95 + (0.95 * phase) + (0.18 * sineFast)));
                panPattern = static_cast<float>(0.48 * panSine);
                pitchPattern = static_cast<float>(-1.0 + (11.5 * phase) + (1.8 * sineFast));
                cutoffNorm = static_cast<float>(0.18 + (0.78 * phase));
                targetResonance = static_cast<float>(0.9 + (2.9 * filterTri));
                targetMorph = static_cast<float>(0.12 + (0.58 * filterPhase));
                break;
            case 1: // faller
                shapeIntensity = juce::jlimit(0.18f, 1.0f, static_cast<float>(1.0 - phase));
                speedMult = juce::jlimit(0.70f, 2.30f, static_cast<float>(1.90 - (1.00 * phase) + (0.16 * sine)));
                panPattern = static_cast<float>(0.72 * triSigned);
                pitchPattern = static_cast<float>(8.0 - (14.0 * phase) + (1.3 * sine));
                cutoffNorm = static_cast<float>(0.92 - (0.70 * phase));
                targetResonance = static_cast<float>(1.1 + (3.1 * phase));
                targetMorph = static_cast<float>(0.88 - (0.62 * filterPhase));
                break;
            case 2: // swirl
                shapeIntensity = juce::jlimit(0.20f, 1.0f, static_cast<float>(tri));
                speedMult = juce::jlimit(0.75f, 2.15f, static_cast<float>(1.0
                    + (0.42 * std::sin(juce::MathConstants<double>::twoPi * phase * 2.0))
                    + (0.14 * sineFast)));
                panPattern = static_cast<float>(0.80 * std::sin(juce::MathConstants<double>::twoPi * (panPhase * 2.0)));
                pitchPattern = static_cast<float>((6.0 * sine) + (3.0 * std::sin(juce::MathConstants<double>::twoPi * (phase + 0.25))));
                cutoffNorm = static_cast<float>(0.24 + (0.66 * filterTri));
                targetResonance = static_cast<float>(0.9 + (2.5 * wrapUnitPhase(filterPhase * 2.0)));
                targetMorph = static_cast<float>(0.50 + (0.40 * std::sin(juce::MathConstants<double>::twoPi * filterPhase)));
                break;
            case 3:
            default: // surge
                shapeIntensity = juce::jlimit(0.22f, 1.0f, static_cast<float>(0.55 + (0.45 * std::abs(sineFast))));
                speedMult = juce::jlimit(0.70f, 2.40f, static_cast<float>(1.0 + (0.95 * triSigned) + (0.14 * sineFast)));
                panPattern = static_cast<float>(0.90 * sawSigned);
                pitchPattern = static_cast<float>((9.0 * sine) + (4.5 * triSigned));
                cutoffNorm = static_cast<float>(0.14 + (0.80 * wrapUnitPhase(phase + (0.25 * juce::jmax(0.0, sine)))));
                targetResonance = static_cast<float>(1.0 + (3.0 * wrapUnitPhase(filterPhase + (0.20 * triSigned))));
                targetMorph = static_cast<float>(wrapUnitPhase((0.40 * phase) + (0.60 * filterPhase)));
                break;
        }
    }
    else
    {
        // Hard step modes (deterministic rhythmic snapshots).
        static constexpr std::array<std::array<float, 8>, 8> kSpeedPatterns{{
            {{ 1.00f, 1.25f, 1.50f, 1.75f, 1.50f, 1.25f, 1.00f, 0.85f }},
            {{ 1.00f, 0.90f, 1.10f, 1.35f, 1.60f, 1.35f, 1.10f, 0.90f }},
            {{ 1.00f, 1.12f, 1.25f, 1.38f, 1.50f, 1.62f, 1.75f, 1.50f }},
            {{ 1.00f, 1.50f, 1.00f, 1.25f, 1.00f, 1.75f, 1.00f, 1.50f }},
            {{ 1.00f, 1.15f, 1.30f, 1.45f, 1.30f, 1.15f, 1.00f, 0.90f }},
            {{ 1.00f, 0.85f, 1.00f, 1.35f, 1.00f, 1.55f, 1.20f, 1.00f }},
            {{ 1.00f, 1.20f, 1.45f, 1.20f, 0.95f, 1.20f, 1.45f, 1.70f }},
            {{ 1.00f, 1.33f, 1.67f, 1.33f, 1.00f, 0.90f, 1.10f, 1.30f }}
        }};
        static constexpr std::array<std::array<float, 8>, 8> kPanPatterns{{
            {{ -1.00f, 1.00f, -0.80f, 0.80f, -0.60f, 0.60f, -0.35f, 0.35f }},
            {{ -0.70f, -0.30f, 0.30f, 0.70f, 1.00f, 0.70f, 0.30f, -0.30f }},
            {{ -1.00f, -0.60f, -0.20f, 0.20f, 0.60f, 1.00f, 0.40f, -0.20f }},
            {{ -1.00f, 1.00f, -1.00f, 1.00f, -0.50f, 0.50f, -0.20f, 0.20f }},
            {{ -0.25f, -0.75f, -1.00f, -0.50f, 0.50f, 1.00f, 0.75f, 0.25f }},
            {{ -0.90f, -0.20f, 0.90f, 0.20f, -0.90f, -0.20f, 0.90f, 0.20f }},
            {{ -0.40f, 0.40f, -0.70f, 0.70f, -1.00f, 1.00f, -0.60f, 0.60f }},
            {{ -1.00f, -0.50f, 0.00f, 0.50f, 1.00f, 0.50f, 0.00f, -0.50f }}
        }};
        static constexpr std::array<std::array<float, 8>, 8> kPitchPatterns{{
            {{ 0.0f, 2.0f, 5.0f, 7.0f, 10.0f, 7.0f, 5.0f, 2.0f }},
            {{ 0.0f, -2.0f, 3.0f, 5.0f, 8.0f, 5.0f, 3.0f, -2.0f }},
            {{ 0.0f, 3.0f, 7.0f, 10.0f, 12.0f, 10.0f, 7.0f, 3.0f }},
            {{ 0.0f, 5.0f, 0.0f, 7.0f, 0.0f, 10.0f, 0.0f, 12.0f }},
            {{ 0.0f, 2.0f, 4.0f, 7.0f, 9.0f, 7.0f, 4.0f, 2.0f }},
            {{ 0.0f, -3.0f, 0.0f, 4.0f, 7.0f, 4.0f, 0.0f, -3.0f }},
            {{ 0.0f, 1.0f, 5.0f, 8.0f, 12.0f, 8.0f, 5.0f, 1.0f }},
            {{ 0.0f, 4.0f, 7.0f, 11.0f, 7.0f, 4.0f, 2.0f, 0.0f }}
        }};

        const int patternBank = ((seed / 5) + (bitCount * 3) + highestBit + lowestBit) % 8;
        const auto& speedPattern = kSpeedPatterns[static_cast<size_t>((variant + patternBank) % 8)];
        const auto& panPatternTable = kPanPatterns[static_cast<size_t>((variant + highestBit + patternBank) % 8)];
        const auto& pitchPatternTable = kPitchPatterns[static_cast<size_t>((variant + lowestBit + (patternBank * 2)) % 8)];

        switch (variant % 4)
        {
            case 0: shapeIntensity = juce::jlimit(0.15f, 1.0f, normStep); break; // rise
            case 1: shapeIntensity = juce::jlimit(0.15f, 1.0f, 1.0f - normStep); break; // fall
            case 2: shapeIntensity = juce::jlimit(0.15f, 1.0f, 1.0f - std::abs((normStep * 2.0f) - 1.0f)); break; // triangle
            case 3:
            default: shapeIntensity = (stepLoop & 1) == 0 ? 1.0f : 0.45f; break; // pulse
        }

        speedMult = speedPattern[static_cast<size_t>(stepLoop)];
        panPattern = panPatternTable[static_cast<size_t>(stepLoop)];
        pitchPattern = pitchPatternTable[static_cast<size_t>(stepLoop)];
        cutoffNorm = juce::jlimit(0.10f, 1.0f, 0.25f + (0.70f * normStep));
        targetResonance = 0.9f + (3.2f * shapeIntensity);
        targetMorph = juce::jlimit(0.05f, 0.95f, 0.10f + (0.80f * normStep));

        // Hard-step variants escalate while held to create stronger breakdown/riser motion.
        const float hardExtreme = juce::jlimit(1.0f, 2.1f, 1.0f + (1.1f * heldRamp));
        shapeIntensity = juce::jlimit(0.15f, 1.0f, shapeIntensity + (0.50f * heldRamp));
        speedMult = 1.0f + ((speedMult - 1.0f) * hardExtreme);
        panPattern = juce::jlimit(-1.0f, 1.0f, panPattern * (1.0f + (0.45f * heldRamp)));
        pitchPattern = juce::jlimit(-18.0f, 18.0f, pitchPattern * (1.0f + (0.95f * heldRamp)));
        targetResonance = juce::jlimit(0.2f, 8.0f, targetResonance + (2.1f * heldRamp));
        targetMorph = juce::jlimit(0.02f, 0.98f, targetMorph + (0.14f * heldRamp));
    }

    if (allowPitchSpeedMacro)
    {
        // Hard-step speed scenes are always available for >2-button holds.
        static constexpr std::array<std::array<float, 8>, 4> kHardSpeedScenes {{
            {{ 0.30f, 0.55f, 1.15f, 2.20f, 3.40f, 2.40f, 1.20f, 0.45f }},
            {{ 1.00f, 0.35f, 0.70f, 1.60f, 3.20f, 2.20f, 1.10f, 0.40f }},
            {{ 3.40f, 2.40f, 1.60f, 1.00f, 0.50f, 0.75f, 1.35f, 2.20f }},
            {{ 0.28f, 0.50f, 0.85f, 1.50f, 2.60f, 3.60f, 1.80f, 0.42f }}
        }};
        const int hardSceneIdx = ((seed / 9) + highestBit + (lowestBit * 2)) % 4;
        const float hardStepSpeed = kHardSpeedScenes[static_cast<size_t>(hardSceneIdx)][static_cast<size_t>(stepLoop)];
        float hardMix = (variant >= 4) ? 0.76f : 0.42f;
        hardMix += 0.22f * heldRamp;
        if (threeButton)
            hardMix += 0.12f;
        hardMix = juce::jlimit(0.0f, 1.0f, hardMix);
        speedMult = juce::jmap(hardMix, speedMult, hardStepSpeed);
    }

    if (threeButton)
    {
        // 3-button combos start from a stronger base before contour shaping.
        shapeIntensity = juce::jlimit(0.2f, 1.0f, shapeIntensity + 0.20f + (0.25f * heldRamp));
        speedMult = juce::jlimit(0.25f, 4.0f, speedMult * (1.08f + (0.42f * heldRamp)));
        panPattern = juce::jlimit(-1.0f, 1.0f, panPattern * (1.20f + (0.35f * heldRamp)));
        pitchPattern = juce::jlimit(-14.0f, 14.0f, pitchPattern * (1.04f + (0.18f * heldRamp)));
    }

    if (!allowPitchSpeedMacro && hardStepMode)
    {
        // Hard-step depth envelope for 1/2-button stutters.
        // 1-button: subtle pan-only growth.
        // 2-button: stronger growth for pan + filter shape over hold time.
        const float hardDepth = juce::jlimit(0.0f, 1.0f, std::pow(heldRamp, 1.35f));
        if (singleButton)
        {
            panDepthShape = juce::jlimit(0.08f, 0.24f, 0.08f + (0.16f * hardDepth));
        }
        else
        {
            const float twoButtonDepth = juce::jlimit(0.28f, 1.0f, 0.28f + (0.72f * hardDepth));
            panDepthShape = twoButtonDepth;
            const float stepPolarity = ((stepLoop & 1) == 0) ? 1.0f : -1.0f;
            cutoffNorm = juce::jlimit(0.0f, 1.0f, cutoffNorm + (0.16f * twoButtonDepth * stepPolarity));
            targetMorph = juce::jlimit(0.0f, 1.0f, targetMorph + (0.18f * twoButtonDepth * stepPolarity));
            targetResonance = juce::jlimit(0.2f, 2.1f, targetResonance + (0.45f * twoButtonDepth));
        }
    }
    else if (singleButton)
    {
        // One-button stutter should remain mostly clean and centered.
        panDepthShape = 0.10f;
    }

    if (twoButton)
    {
        // Two-finger mode:
        // - dedicated speed/pitch up/down gestures,
        // - dynamic retrigger-rate movement over a 1..4 bar phrase,
        // - always starts from the current strip speed baseline.
        const int twoButtonMode = ((seed / 7) + (highestBit * 3) + lowestBit) & 0x7;
        twoButtonDirectionUp = ((twoButtonMode & 0x1) == 0);
        twoButtonUseFilter = false;
        const float phaseNorm = juce::jlimit(0.0f, 1.0f, static_cast<float>(phase));

        const double slowDivision = (twoButtonMode <= 1) ? 0.5 : 0.25;
        const double fastDivision = 0.125;
        const float phraseProgress = twoButtonDirectionUp ? phaseNorm : (1.0f - phaseNorm);
        const float gestureDrive = juce::jlimit(0.0f, 1.0f,
            phraseProgress * (0.45f + (0.55f * heldRamp)));
        const float shapedDrive = std::pow(gestureDrive, (twoButtonMode >= 4) ? 0.66f : 1.15f);
        dynamicStutterDivisionBeats = juce::jmap(static_cast<double>(shapedDrive), slowDivision, fastDivision);

        const double elapsedBeats = juce::jmax(0.0, ppqNow - momentaryStutterMacroStartPpq);
        const double stepPos = elapsedBeats / juce::jmax(0.03125, dynamicStutterDivisionBeats);
        const int globalTwoButtonStep = juce::jmax(0, static_cast<int>(std::floor((std::isfinite(stepPos) ? stepPos : 0.0) + 1.0e-6)));
        if (comboChanged || !momentaryStutterTwoButtonStepBaseValid)
        {
            momentaryStutterTwoButtonStepBase = globalTwoButtonStep;
            momentaryStutterTwoButtonStepBaseValid = true;
        }
        twoButtonStepAbs = juce::jmax(0, globalTwoButtonStep - momentaryStutterTwoButtonStepBase);
        const int semitoneStride = (twoButtonMode >= 4) ? 2 : 1;
        const int twoButtonMaxSemitones = (twoButtonMode <= 1) ? 36 : 24;
        int pacedStepAbs = twoButtonStepAbs;
        if (twoButtonMode >= 2)
        {
            const float paceScale = juce::jlimit(0.125f, 1.0f,
                static_cast<float>(dynamicStutterDivisionBeats / slowDivision));
            const float pacedContinuous = static_cast<float>(twoButtonStepAbs) * paceScale;
            pacedStepAbs = juce::jmax(0, static_cast<int>(std::floor(pacedContinuous + 1.0e-4f)));
        }

        const int linearSemitoneStep = juce::jlimit(0, twoButtonMaxSemitones, pacedStepAbs * semitoneStride);
        int semitoneStep = linearSemitoneStep;
        if (twoButtonMode >= 2)
        {
            const float expoK = (twoButtonMode >= 6) ? 0.74f
                : (twoButtonMode >= 4 ? 0.58f
                                      : (twoButtonMode >= 2 ? 0.36f : 0.30f));
            const float expoNorm = juce::jlimit(0.0f, 1.0f,
                1.0f - std::exp(-expoK * static_cast<float>(pacedStepAbs)));
            const int maxExpoStep = juce::jmax(1, twoButtonMaxSemitones / semitoneStride);
            const int expoStepIndex = juce::jlimit(0, maxExpoStep, static_cast<int>(std::round(expoNorm * static_cast<float>(maxExpoStep))));
            const int expoSemitoneStep = juce::jlimit(0, twoButtonMaxSemitones, expoStepIndex * semitoneStride);
            semitoneStep = juce::jmax(linearSemitoneStep, expoSemitoneStep);
        }
        twoButtonSemitoneStep = static_cast<float>(twoButtonDirectionUp ? semitoneStep : -semitoneStep);

        const float panDepthStep = juce::jlimit(0.0f, 1.0f,
            0.28f + (static_cast<float>(semitoneStep) / 18.0f));
        panDepthShape = panDepthStep;
        twoButtonSemitoneSpeedRatio = std::pow(2.0f, twoButtonSemitoneStep / 12.0f);
        cutoffNorm = 1.0f;
        targetMorph = 0.0f;
        targetResonance = 0.72f;
    }
    else
    {
        momentaryStutterTwoButtonStepBaseValid = false;
        momentaryStutterTwoButtonStepBase = 0;
    }

    // Multi-button combos add infinite ramp movement layers (looping every cycle)
    // that continue until release: retrigger-rate sweeps + coordinated speed/filter ramps.
    if (multiButton && !twoButton)
    {
        const float phaseNorm = static_cast<float>(phase);
        const float rampUp = juce::jlimit(0.0f, 1.0f, phaseNorm);
        const float rampDown = 1.0f - rampUp;
        const float rampPingPong = juce::jlimit(0.0f, 1.0f, static_cast<float>(1.0 - std::abs((phase * 2.0) - 1.0)));
        const float heldDrive = juce::jlimit(0.20f, 1.0f, 0.35f + (0.65f * heldRamp));

        const double baseDivision = juce::jlimit(0.125, 1.0, dynamicStutterDivisionBeats);
        const double minFastDivision = 0.125;
        const double fastDivision = juce::jlimit(minFastDivision, 1.0, baseDivision * (threeButton ? 0.30 : 0.42));
        const double slowDivision = juce::jlimit(0.125, 2.0, baseDivision * (threeButton ? 2.25 : 1.85));

        const int rampMode = ((seed / 17) + bitCount + highestBit + lowestBit) % 4;
        switch (rampMode)
        {
            case 0: // accel + high-pass rise
            {
                const float amt = rampUp * heldDrive;
                dynamicStutterDivisionBeats = juce::jmap(static_cast<double>(amt), baseDivision, fastDivision);
                if (allowPitchSpeedMacro)
                    speedMult = juce::jlimit(0.35f, 4.0f, speedMult * (1.0f + (1.35f * amt)));
                cutoffNorm = juce::jlimit(0.0f, 1.0f, amt);
                targetMorph = 1.0f; // High-pass
                targetResonance = juce::jlimit(0.2f, 8.0f, targetResonance + (1.0f * amt));
                break;
            }
            case 1: // accel + low-pass fall
            {
                const float amt = rampUp * heldDrive;
                dynamicStutterDivisionBeats = juce::jmap(static_cast<double>(amt), baseDivision, fastDivision);
                if (allowPitchSpeedMacro)
                    speedMult = juce::jlimit(0.35f, 4.0f, speedMult * (1.0f + (1.20f * amt)));
                cutoffNorm = juce::jlimit(0.0f, 1.0f, 1.0f - amt);
                targetMorph = 0.0f; // Low-pass
                targetResonance = juce::jlimit(0.2f, 8.0f, targetResonance + (0.7f * amt));
                break;
            }
            case 2: // decel + low-pass fall
            {
                const float amt = rampUp * heldDrive;
                dynamicStutterDivisionBeats = juce::jmap(static_cast<double>(amt), baseDivision, slowDivision);
                if (allowPitchSpeedMacro)
                    speedMult = juce::jlimit(0.35f, 4.0f, speedMult * (1.0f - (0.58f * amt)));
                cutoffNorm = juce::jlimit(0.0f, 1.0f, 1.0f - amt);
                targetMorph = 0.0f; // Low-pass
                targetResonance = juce::jlimit(0.2f, 8.0f, targetResonance + (0.6f * amt));
                break;
            }
            case 3:
            default: // infinite up/down ping-pong ramp
            {
                const float amt = rampPingPong * heldDrive;
                dynamicStutterDivisionBeats = juce::jmap(static_cast<double>(amt), slowDivision, fastDivision);
                if (allowPitchSpeedMacro)
                {
                    const float swing = ((rampPingPong * 2.0f) - 1.0f) * heldDrive;
                    speedMult = juce::jlimit(0.35f, 4.0f, speedMult * (1.0f + (0.65f * swing)));
                }

                // Alternate LP/HP flavor each half cycle while maintaining a continuous ramp.
                if (rampUp >= rampDown)
                {
                    cutoffNorm = juce::jlimit(0.0f, 1.0f, amt);
                    targetMorph = 1.0f; // High-pass
                }
                else
                {
                    cutoffNorm = juce::jlimit(0.0f, 1.0f, 1.0f - amt);
                    targetMorph = 0.0f; // Low-pass
                }
                targetResonance = juce::jlimit(0.2f, 8.0f, targetResonance + (0.8f * amt));
                break;
            }
        }
    }

    if (threeButton)
    {
        // Musical 3-button contours: exponential risers/fallers and curved macro motion.
        const float phaseNorm = juce::jlimit(0.0f, 1.0f, static_cast<float>(phase));
        const bool fastContour = (threeButtonContour <= 1);
        const float expPowerFast = fastContour
            ? (0.62f + (0.34f * heldRamp))
            : (1.12f + (0.48f * heldRamp));
        const float expPowerSlow = fastContour
            ? (0.78f + (0.30f * heldRamp))
            : (1.04f + (0.44f * heldRamp));
        const float expRise = std::pow(phaseNorm, expPowerFast);
        const float expFall = std::pow(1.0f - phaseNorm, expPowerFast);
        const float arc = (phaseNorm < 0.5f)
            ? std::pow(phaseNorm * 2.0f, expPowerSlow)
            : std::pow((1.0f - phaseNorm) * 2.0f, expPowerSlow);
        const float contourDrive = juce::jlimit(0.0f, 1.0f, 0.38f + (0.62f * heldRamp));
        const double longPatternSlow = fastContour
            ? (lengthBars >= 2 ? 1.58 : 1.26)
            : (lengthBars >= 2 ? 2.04 : 1.52);
        const double longPatternFast = fastContour
            ? (lengthBars >= 2 ? 0.19 : 0.28)
            : (lengthBars >= 2 ? 0.40 : 0.50);

        switch (threeButtonContour)
        {
            case 0: // Exponential riser
            {
                speedMult = juce::jlimit(0.25f, 4.0f, juce::jmap(expRise, 1.00f, 4.00f));
                pitchPattern = juce::jlimit(-14.0f, 14.0f, juce::jmap(expRise, -1.0f, 14.0f));
                cutoffNorm = juce::jlimit(0.0f, 1.0f, juce::jmap(expRise, 0.12f, 0.70f));
                targetMorph = 1.0f;
                targetResonance = juce::jlimit(0.2f, 2.4f, 0.72f + (0.72f * expRise));
                panDepthShape = juce::jlimit(0.0f, 1.0f, juce::jmap(expRise, 0.02f, 1.0f));
                dynamicStutterDivisionBeats = juce::jmap(static_cast<double>(expRise),
                    juce::jmin(2.0, dynamicStutterDivisionBeats * longPatternSlow),
                    juce::jmax(0.125, dynamicStutterDivisionBeats * longPatternFast));
                break;
            }
            case 1: // Exponential faller
            {
                speedMult = juce::jlimit(0.25f, 4.0f, juce::jmap(expFall, 0.55f, 3.85f));
                pitchPattern = juce::jlimit(-14.0f, 14.0f, juce::jmap(expFall, -13.0f, 10.0f));
                cutoffNorm = juce::jlimit(0.0f, 1.0f, juce::jmap(expFall, 0.18f, 0.92f));
                targetMorph = 0.0f;
                targetResonance = juce::jlimit(0.2f, 2.4f, 0.68f + (0.64f * expFall));
                panDepthShape = juce::jlimit(0.0f, 1.0f, juce::jmap(expFall, 0.05f, 1.0f));
                dynamicStutterDivisionBeats = juce::jmap(static_cast<double>(expFall),
                    juce::jmax(0.125, dynamicStutterDivisionBeats * longPatternFast),
                    juce::jmin(2.0, dynamicStutterDivisionBeats * longPatternSlow));
                break;
            }
            case 2: // Rise then fall arc
            {
                speedMult = juce::jlimit(0.25f, 4.0f, juce::jmap(arc, 0.70f, 3.95f));
                pitchPattern = juce::jlimit(-14.0f, 14.0f, juce::jmap(arc, -5.0f, 13.0f));
                cutoffNorm = juce::jlimit(0.0f, 1.0f, juce::jmap(arc, 0.16f, 0.76f));
                targetMorph = (phaseNorm < 0.5f) ? 1.0f : 0.0f;
                targetResonance = juce::jlimit(0.2f, 2.4f, 0.72f + (0.58f * arc));
                panDepthShape = juce::jlimit(0.0f, 1.0f, juce::jmap(arc, 0.05f, 1.0f));
                dynamicStutterDivisionBeats = juce::jmap(static_cast<double>(arc),
                    juce::jmin(2.0, dynamicStutterDivisionBeats * (longPatternSlow - 0.20)),
                    juce::jmax(0.125, dynamicStutterDivisionBeats * (longPatternFast + 0.05)));
                break;
            }
            case 3:
            default: // Fall then rise arc
            {
                const float invArc = 1.0f - arc;
                speedMult = juce::jlimit(0.25f, 4.0f, juce::jmap(invArc, 0.62f, 3.70f));
                pitchPattern = juce::jlimit(-14.0f, 14.0f, juce::jmap(invArc, -11.0f, 10.0f));
                cutoffNorm = juce::jlimit(0.0f, 1.0f, juce::jmap(invArc, 0.20f, 0.88f));
                targetMorph = (phaseNorm < 0.5f) ? 0.0f : 1.0f;
                targetResonance = juce::jlimit(0.2f, 2.4f, 0.66f + (0.58f * invArc));
                panDepthShape = juce::jlimit(0.0f, 1.0f, juce::jmap(invArc, 0.05f, 1.0f));
                dynamicStutterDivisionBeats = juce::jmap(static_cast<double>(invArc),
                    juce::jmin(2.0, dynamicStutterDivisionBeats * (longPatternSlow - 0.10)),
                    juce::jmax(0.125, dynamicStutterDivisionBeats * (longPatternFast + 0.08)));
                break;
            }
        }

        // Make contour ramps react faster as the hold deepens.
        speedMult = juce::jlimit(0.25f, 4.0f, speedMult * (1.0f + (0.35f * contourDrive)));
    }

    // Musical safety guard:
    // 2-button combos should stay expressive but avoid ultra-harsh ringing/noise at high stutter rates.
    if (!allowPitchSpeedMacro)
    {
        const double minDivision = 0.125;
        dynamicStutterDivisionBeats = juce::jlimit(minDivision, 4.0, dynamicStutterDivisionBeats);
        targetResonance = juce::jlimit(0.2f, 1.4f, targetResonance);
    }

    // High-density col15 combos can become brittle/noisy when all macro dimensions
    // align at the same time; keep them in a musical envelope.
    if (allowPitchSpeedMacro && hasTopStutterBit)
    {
        dynamicStutterDivisionBeats = juce::jlimit(0.125, 4.0, dynamicStutterDivisionBeats);
        speedMult = juce::jlimit(0.60f, 2.0f, speedMult);
        pitchPattern = juce::jlimit(-8.0f, 8.0f, pitchPattern);
        targetResonance = juce::jlimit(0.2f, 2.4f, targetResonance);
    }

    // Explicitly tame known harsh combinations.
    if (combo10And13)
    {
        dynamicStutterDivisionBeats = juce::jlimit(0.125, 4.0, dynamicStutterDivisionBeats);
        targetMorph = 0.0f;
        targetResonance = juce::jlimit(0.2f, 1.2f, targetResonance);
    }

    if (combo11And13)
    {
        dynamicStutterDivisionBeats = juce::jlimit(0.125, 4.0, dynamicStutterDivisionBeats);
        targetMorph = 0.0f;
        targetResonance = juce::jlimit(0.2f, 1.1f, targetResonance);
    }

    if (combo12And13And15)
    {
        dynamicStutterDivisionBeats = juce::jlimit(0.125, 4.0, dynamicStutterDivisionBeats);
        speedMult = juce::jlimit(0.70f, 1.60f, speedMult);
        pitchPattern = juce::jlimit(-6.0f, 6.0f, pitchPattern);
        targetResonance = juce::jlimit(0.2f, 1.8f, targetResonance);
    }

    if (multiButton)
    {
        static constexpr std::array<double, 4> kTwoButtonGrid { 1.0, 0.5, 0.25, 0.125 };
        static constexpr std::array<double, 4> kThreeButtonGrid { 1.0, 0.5, 0.25, 0.125 };
        static constexpr std::array<double, 3> kDenseButtonGrid { 0.5, 0.25, 0.125 };

        if (bitCount == 2)
            dynamicStutterDivisionBeats = snapDivisionToGrid(dynamicStutterDivisionBeats, kTwoButtonGrid);
        else if (bitCount == 3)
            dynamicStutterDivisionBeats = snapDivisionToGrid(dynamicStutterDivisionBeats, kThreeButtonGrid);
        else
            dynamicStutterDivisionBeats = snapDivisionToGrid(dynamicStutterDivisionBeats, kDenseButtonGrid);
    }

    const bool veryFastDivision = dynamicStutterDivisionBeats <= 0.1250001;
    const bool ultraFastDivision = dynamicStutterDivisionBeats <= 0.0835001;
    float retriggerFadeMs = 0.7f;
    if (bitCount == 2)
        retriggerFadeMs = veryFastDivision ? 1.30f : 1.00f;
    else if (bitCount >= 3)
        retriggerFadeMs = ultraFastDivision ? 2.00f : (veryFastDivision ? 1.70f : 1.35f);
    audioEngine->setMomentaryStutterRetriggerFadeMs(retriggerFadeMs);

    if (multiButton && veryFastDivision)
    {
        const float speedFloor = ultraFastDivision ? 0.72f : 0.60f;
        const float speedCeil = allowPitchSpeedMacro
            ? (ultraFastDivision ? 1.95f : (threeButton ? 2.60f : 2.20f))
            : (twoButton ? (ultraFastDivision ? 2.15f : 2.85f) : 1.25f);
        speedMult = juce::jlimit(speedFloor, speedCeil, speedMult);
        pitchPattern = juce::jlimit(-6.0f, 6.0f, pitchPattern);
        targetResonance = juce::jlimit(0.2f, ultraFastDivision ? 0.85f : 1.05f, targetResonance);
        if (targetMorph > 0.70f)
            targetMorph = ultraFastDivision ? 0.58f : 0.70f;
    }

    if (multiButton && targetMorph > 0.82f && cutoffNorm > 0.78f)
        targetResonance = juce::jmin(targetResonance, 0.9f);

    if (multiButton)
    {
        // Keep cutoff+morph inside audible zones to avoid click-only/no-audio states.
        if (targetMorph >= 0.70f)
            cutoffNorm = juce::jlimit(0.04f, 0.72f, cutoffNorm);
        else if (targetMorph <= 0.30f)
            cutoffNorm = juce::jlimit(0.16f, 0.98f, cutoffNorm);
        else
            cutoffNorm = juce::jlimit(0.08f, 0.94f, cutoffNorm);

        if ((targetMorph >= 0.72f && cutoffNorm >= 0.62f)
            || (targetMorph <= 0.16f && cutoffNorm <= 0.22f))
            targetResonance = juce::jmin(targetResonance, 0.82f);
    }

    if (applySpeedMacro && !twoButton)
    {
        // Stutter speed is hard-stepped by PPQ phase step index (no smooth glides).
        const float cycleStepNorm = (totalSteps > 1)
            ? juce::jlimit(0.0f, 1.0f, static_cast<float>(stepIndex) / static_cast<float>(totalSteps - 1))
            : 0.0f;
        const int rampShape = threeButton ? threeButtonContour : (variant & 0x3);
        float rampNorm = cycleStepNorm;
        switch (rampShape)
        {
            case 0: // up
                rampNorm = cycleStepNorm;
                break;
            case 1: // down
                rampNorm = 1.0f - cycleStepNorm;
                break;
            case 2: // up then down
                rampNorm = (cycleStepNorm < 0.5f)
                    ? (cycleStepNorm * 2.0f)
                    : ((1.0f - cycleStepNorm) * 2.0f);
                break;
            case 3: // down then up
            default:
                rampNorm = (cycleStepNorm < 0.5f)
                    ? (1.0f - (cycleStepNorm * 2.0f))
                    : ((cycleStepNorm - 0.5f) * 2.0f);
                break;
        }
        const float expShape = threeButton
            ? (0.90f + (0.95f * heldRamp))
            : (1.20f + (1.10f * heldRamp) + (twoButton ? 0.20f : 0.0f));
        const float shapedRamp = std::pow(juce::jlimit(0.0f, 1.0f, rampNorm), expShape);
        const float minHardSpeedMult = threeButton ? 0.45f : 0.55f;
        const float maxHardSpeedMult = threeButton ? 3.9f : 3.1f;
        const float hardStepSpeedMult = juce::jmap(shapedRamp, minHardSpeedMult, maxHardSpeedMult);
        const float hardStepBlend = threeButton ? 0.96f : (twoButton ? 0.88f : 0.84f);
        speedMult = juce::jmap(hardStepBlend, speedMult, hardStepSpeedMult);
    }

    const float intensity = juce::jlimit(0.20f, 1.0f, comboIntensity * shapeIntensity);
    const float speedIntensityScale = juce::jlimit(0.35f, 1.0f, 0.42f + (0.58f * intensity));
    const float shapedSpeedMult = twoButton
        ? juce::jlimit(0.03125f, 8.0f, twoButtonSemitoneSpeedRatio)
        : (1.0f + ((speedMult - 1.0f) * speedIntensityScale));
    const float pitchOffsetBasePattern = juce::jlimit(-12.0f, 12.0f, pitchPattern * (0.55f + (0.30f * intensity)));
    // Keep pitch secondary: speed carries the primary riser/faller motion.
    const float speedToPitchDepth = allowPitchMacro ? (threeButton ? 3.0f : 2.0f) : 0.0f;
    const float pitchOffsetFromSpeedShape = juce::jlimit(-12.0f, 12.0f, (shapedSpeedMult - 1.0f) * speedToPitchDepth);
    const float pitchOffsetBase = juce::jlimit(
        -12.0f, 12.0f, pitchOffsetBasePattern + ((allowPitchMacro && !twoButton) ? pitchOffsetFromSpeedShape : 0.0f));

    // Pan is always hard-stepped and locked to the active stutter subdivision.
    const double panDivisionBeats = juce::jmax(0.03125, dynamicStutterDivisionBeats);
    const double panStepPos = (ppqNow - momentaryStutterMacroStartPpq) / panDivisionBeats;
    const int panStepIndex = static_cast<int>(std::floor(std::isfinite(panStepPos) ? panStepPos : 0.0));
    const int panMode = ((seed / 23) + bitCount + highestBit + lowestBit) & 0x3;
    static constexpr std::array<float, 8> kPanSeqA { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f };
    static constexpr std::array<float, 8> kPanSeqB { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f };
    float panHardStep = -1.0f;
    switch (panMode)
    {
        case 0:
            panHardStep = (panStepIndex & 1) ? 1.0f : -1.0f;
            break;
        case 1:
            panHardStep = ((panStepIndex >> 1) & 1) ? 1.0f : -1.0f;
            break;
        case 2:
            panHardStep = kPanSeqA[static_cast<size_t>(juce::jmax(0, panStepIndex) & 7)];
            break;
        case 3:
        default:
            panHardStep = kPanSeqB[static_cast<size_t>(juce::jmax(0, panStepIndex) & 7)];
            break;
    }
    if (twoButton)
        panHardStep = (panStepIndex & 1) ? 1.0f : -1.0f;
    if (panPattern < 0.0f)
        panHardStep = -panHardStep;
    const float panDriveBase = juce::jlimit(0.72f, 1.0f,
        0.72f + (0.28f * intensity) + (threeButton ? 0.10f : 0.0f) + (veryFastDivision ? 0.08f : 0.0f));
    float panDepth = 1.0f;
    if (threeButton)
        panDepth = juce::jlimit(0.18f, 1.0f, panDepthShape);
    else if (singleButton)
        panDepth = juce::jlimit(0.05f, 0.28f, panDepthShape);
    else if (twoButton)
        panDepth = juce::jlimit(0.0f, 1.0f, panDepthShape);
    else
        panDepth = juce::jlimit(0.28f, 1.0f, panDepthShape);
    const float panDrive = twoButton
        ? juce::jlimit(0.0f, 1.0f, panDriveBase * panDepth)
        : juce::jlimit(0.18f, 1.0f, panDriveBase * panDepth);
    const float panOffsetBase = juce::jlimit(-1.0f, 1.0f, panHardStep * panDrive);

    cutoffNorm = juce::jlimit(0.0f, 1.0f, cutoffNorm);
    const float resonanceScale = threeButton
        ? juce::jlimit(0.75f, 1.15f, comboIntensity + 0.18f)
        : comboIntensity;
    targetResonance = juce::jlimit(0.2f, threeButton ? 2.4f : 8.0f, targetResonance * resonanceScale);
    targetMorph = juce::jlimit(0.0f, 1.0f, targetMorph);

    auto filterAlgorithm = filterAlgorithmFromIndex((variant + bitCount + highestBit + lowestBit) % 6);
    if (combo10And13 || combo11And13 || combo12And13And15
        || (!allowPitchSpeedMacro && highestBit >= 5 && targetMorph > 0.74f)
        || (multiButton && veryFastDivision))
        filterAlgorithm = EnhancedAudioStrip::FilterAlgorithm::Tpt12;
    const float targetCutoff = cutoffFromNormalized(cutoffNorm);
    const bool allowSliceLengthGesture = (twoButton || threeButton);
    const bool comboUsesSliceGesture = allowSliceLengthGesture
        && ((twoButton && (((seed + highestBit + lowestBit) & 0x1) == 0 || highestBit >= 4))
            || (threeButton && ((((seed >> 1) + highestBit) & 0x1) == 0 || highestBit >= 3)));
    const float phraseProgress = twoButton
        ? juce::jlimit(0.0f, 1.0f, twoButtonDirectionUp ? static_cast<float>(phase)
                                                         : (1.0f - static_cast<float>(phase)))
        : juce::jlimit(0.0f, 1.0f, static_cast<float>(phase));
    const float stutterDensityNorm = juce::jlimit(0.0f, 1.0f, static_cast<float>(
        (0.5 - juce::jlimit(0.125, 0.5, dynamicStutterDivisionBeats))
        / (0.5 - 0.125)));
    const float sliceGestureStrength = comboUsesSliceGesture
        ? juce::jlimit(0.0f, 1.0f,
            (0.50f * phraseProgress) + (0.30f * stutterDensityNorm) + (0.20f * heldRamp))
        : 0.0f;
    const float minSliceLengthForGesture = twoButton ? 0.06f : 0.03f;
    audioEngine->setMomentaryStutterDivision(juce::jlimit(0.125, 4.0, dynamicStutterDivisionBeats));
    const double speedStepDivisionBeats = juce::jmax(0.125, dynamicStutterDivisionBeats);
    const double speedStepPos = (ppqNow - momentaryStutterMacroStartPpq) / speedStepDivisionBeats;
    const int speedStepAbs = juce::jmax(0, static_cast<int>(std::floor(std::isfinite(speedStepPos) ? speedStepPos : 0.0)));
    const bool stutterStartStep = (speedStepAbs == 0);
    const bool firstSpeedStep = applySpeedMacro && (speedStepAbs == 0);
    const auto stepFilterTypeFromMorph = [](float morph)
    {
        if (morph < 0.34f)
            return FilterType::LowPass;
        if (morph > 0.66f)
            return FilterType::HighPass;
        return FilterType::BandPass;
    };

    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        const auto& saved = momentaryStutterSavedState[idx];
        if (!saved.valid || !momentaryStutterStripArmed[idx])
            continue;

        auto* strip = audioEngine->getStrip(i);
        const bool stepMode = (strip && strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step);
        auto* stepSampler = (stepMode && strip) ? strip->getStepSampler() : nullptr;
        const bool hasPlayableContent = strip
            && (strip->hasAudio() || (stepSampler && stepSampler->getHasAudio()));
        if (!strip || !hasPlayableContent || !strip->isPlaying())
            continue;

        const float stripOffset = static_cast<float>(i - (MaxStrips / 2));
        const float stripPanScale = juce::jlimit(0.45f, threeButton ? 1.35f : 1.15f,
            0.65f + (0.08f * static_cast<float>(bitCount)) + (0.05f * static_cast<float>(i)));
        const float stripPitchSpread = (allowPitchSpeedMacro && bitCount > 2) ? (stripOffset * 0.35f) : 0.0f;
        const float stripSpeedSpread = (applySpeedMacro && bitCount > 3) ? (stripOffset * 0.025f) : 0.0f;
        const float stripMorphOffset = static_cast<float>(0.08 * std::sin(
            juce::MathConstants<double>::twoPi * wrapUnitPhase(phase + (0.13 * static_cast<double>(i)))));

        const float savedSpeed = juce::jlimit(0.0f, 4.0f, saved.playbackSpeed);
        const float speedBaseline = savedSpeed;
        const float stutterSpeedFloor = applySpeedMacro
            ? (ultraFastDivision ? 0.72f : (veryFastDivision ? 0.56f : 0.30f))
            : speedBaseline;
        const float stutterSpeedCeil = applySpeedMacro
            ? (ultraFastDivision ? (threeButton ? 2.10f : 1.95f)
                                 : (veryFastDivision ? (threeButton ? 2.80f : 2.35f)
                                                     : (threeButton ? 4.0f : 3.2f)))
            : speedBaseline;
        const float modulatedTargetSpeed = twoButton
            // Two-finger speed always starts at current strip speed and moves
            // up/down in semitone steps relative to that baseline.
            ? juce::jlimit(0.03125f, 8.0f, speedBaseline * shapedSpeedMult)
            : juce::jlimit(stutterSpeedFloor, stutterSpeedCeil,
                (speedBaseline * shapedSpeedMult) + stripSpeedSpread);
        const bool holdBaselineSpeed = twoButton ? (twoButtonStepAbs == 0) : firstSpeedStep;
        const float targetSpeed = holdBaselineSpeed ? speedBaseline : modulatedTargetSpeed;
        if (!stepMode)
        {
            if (applySpeedMacro)
                strip->setPlaybackSpeedImmediate(targetSpeed);
            else
                strip->setPlaybackSpeed(speedBaseline);
        }
        else
        {
            // Step mode uses step-sampler pitch speed; keep strip traversal speed stable.
            strip->setPlaybackSpeed(saved.playbackSpeed);
        }

        const float targetPan = juce::jlimit(-1.0f, 1.0f, saved.pan + (panOffsetBase * stripPanScale));
        strip->setPan(targetPan);
        if (stepSampler != nullptr)
            stepSampler->setPan(targetPan);

        float targetPitch = saved.stepMode ? saved.pitchSemitones : saved.pitchShift;
        if (twoButton && applySpeedMacro)
        {
            if (stepMode)
            {
                targetPitch = juce::jlimit(-24.0f, 24.0f, saved.pitchSemitones + twoButtonSemitoneStep);
            }
            else
            {
                // Guarantee full contour even when speed reaches hard limits:
                // carry residual semitone motion into pitch shift.
                const float ratioBase = juce::jmax(0.03125f, speedBaseline);
                const float ratioActual = juce::jmax(0.03125f, targetSpeed / ratioBase);
                const float actualSemitoneFromSpeed = 12.0f * std::log2(ratioActual);
                const float residualSemitone = twoButtonSemitoneStep - actualSemitoneFromSpeed;
                targetPitch = juce::jlimit(-24.0f, 24.0f, saved.pitchShift + residualSemitone);
            }
        }
        else if (allowPitchMacro)
        {
            const float pitchBase = stepMode ? saved.pitchSemitones : saved.pitchShift;
            targetPitch = juce::jlimit(-12.0f, 12.0f, pitchBase + pitchOffsetBase + stripPitchSpread);
        }

        applyPitchControlToStrip(*strip, targetPitch);

        if (strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step)
        {
            float targetSliceLength = saved.loopSliceLength;
            if (comboUsesSliceGesture)
            {
                const float shortened = saved.loopSliceLength
                    - ((saved.loopSliceLength - minSliceLengthForGesture) * sliceGestureStrength);
                targetSliceLength = juce::jlimit(minSliceLengthForGesture, 1.0f, shortened);
            }
            strip->setLoopSliceLength(targetSliceLength);
        }

        const bool useMacroFilter = !(singleButton || (twoButton && !twoButtonUseFilter));
        if (!useMacroFilter)
        {
            // Clean stutter variants: no filter color.
            strip->setFilterAlgorithm(saved.filterAlgorithm);
            strip->setFilterFrequency(saved.filterFrequency);
            strip->setFilterResonance(saved.filterResonance);
            strip->setFilterMorph(saved.filterMorph);
            strip->setFilterEnabled(saved.filterEnabled);
            if (stepSampler != nullptr)
            {
                stepSampler->setFilterEnabled(saved.stepFilterEnabled);
                stepSampler->setFilterFrequency(saved.stepFilterFrequency);
                stepSampler->setFilterResonance(saved.stepFilterResonance);
                stepSampler->setFilterType(saved.stepFilterType);
            }
        }
        else
        {
            strip->setFilterEnabled(true);
            strip->setFilterAlgorithm(filterAlgorithm);
            if (stutterStartStep)
            {
                // Start every stutter with filter fully open and minimum resonance.
                strip->setFilterMorph(0.0f);
                strip->setFilterFrequency(20000.0f);
                strip->setFilterResonance(0.1f);
                if (stepSampler != nullptr)
                {
                    stepSampler->setFilterEnabled(true);
                    stepSampler->setFilterType(FilterType::LowPass);
                    stepSampler->setFilterFrequency(20000.0f);
                    stepSampler->setFilterResonance(0.1f);
                }
            }
            else
            {
                const float morphWithOffset = juce::jlimit(0.0f, 1.0f, targetMorph + stripMorphOffset);
                strip->setFilterFrequency(targetCutoff);
                strip->setFilterResonance(targetResonance);
                strip->setFilterMorph(morphWithOffset);
                if (stepSampler != nullptr)
                {
                    stepSampler->setFilterEnabled(true);
                    stepSampler->setFilterType(stepFilterTypeFromMorph(morphWithOffset));
                    stepSampler->setFilterFrequency(targetCutoff);
                    stepSampler->setFilterResonance(juce::jlimit(0.1f, 10.0f, targetResonance));
                }
            }
        }
    }

    momentaryStutterLastComboMask = comboMask;
}

void StepVstHostAudioProcessor::restoreMomentaryStutterMacroBaseline()
{
    if (!audioEngine || !momentaryStutterMacroBaselineCaptured)
        return;

    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        auto& saved = momentaryStutterSavedState[idx];
        if (!saved.valid)
            continue;

        if (auto* strip = audioEngine->getStrip(i))
        {
            strip->setPan(saved.pan);
            strip->setPlaybackSpeedImmediate(saved.playbackSpeed);
            strip->setLoopSliceLength(saved.loopSliceLength);
            if (saved.stepMode)
            {
                applyPitchControlToStrip(*strip, saved.pitchSemitones);
                if (auto* stepSampler = strip->getStepSampler())
                {
                    stepSampler->setPan(saved.pan);
                    stepSampler->setFilterEnabled(saved.stepFilterEnabled);
                    stepSampler->setFilterFrequency(saved.stepFilterFrequency);
                    stepSampler->setFilterResonance(saved.stepFilterResonance);
                    stepSampler->setFilterType(saved.stepFilterType);
                }
            }
            else
            {
                applyPitchControlToStrip(*strip, saved.pitchShift);
            }
            strip->setFilterAlgorithm(saved.filterAlgorithm);
            strip->setFilterFrequency(saved.filterFrequency);
            strip->setFilterResonance(saved.filterResonance);
            strip->setFilterMorph(saved.filterMorph);
            strip->setFilterEnabled(saved.filterEnabled);
        }

        saved.valid = false;
    }

    momentaryStutterMacroBaselineCaptured = false;
    momentaryStutterMacroCapturePending = false;
    momentaryStutterLastComboMask = 0;
    momentaryStutterTwoButtonStepBaseValid = false;
    momentaryStutterTwoButtonStepBase = 0;
}

juce::File StepVstHostAudioProcessor::getDefaultSampleDirectory(int stripIndex, SamplePathMode mode) const
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return {};

    const auto idx = static_cast<size_t>(stripIndex);
    return mode == SamplePathMode::Step ? defaultStepDirectories[idx] : defaultLoopDirectories[idx];
}

StepVstHostAudioProcessor::SamplePathMode StepVstHostAudioProcessor::getSamplePathModeForStrip(int stripIndex) const
{
    if (!audioEngine || stripIndex < 0 || stripIndex >= MaxStrips)
        return SamplePathMode::Loop;

    if (auto* strip = audioEngine->getStrip(stripIndex))
    {
        if (strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step)
            return SamplePathMode::Step;
    }

    return SamplePathMode::Loop;
}

juce::File StepVstHostAudioProcessor::getCurrentBrowserDirectoryForStrip(int stripIndex) const
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return {};

    const auto isValidDir = [](const juce::File& dir)
    {
        return dir != juce::File() && dir.exists() && dir.isDirectory();
    };

    const auto mode = getSamplePathModeForStrip(stripIndex);
    auto selectedDir = getDefaultSampleDirectory(stripIndex, mode);
    if (isValidDir(selectedDir))
        return selectedDir;

    const auto fallbackMode = (mode == SamplePathMode::Step)
        ? SamplePathMode::Loop
        : SamplePathMode::Step;
    auto fallbackDir = getDefaultSampleDirectory(stripIndex, fallbackMode);
    if (isValidDir(fallbackDir))
        return fallbackDir;

    const auto currentFile = currentStripFiles[static_cast<size_t>(stripIndex)];
    auto currentDir = currentFile.getParentDirectory();
    if (isValidDir(currentDir))
        return currentDir;

    // Cross-strip fallback so empty step strips can still browse immediately.
    for (int i = 0; i < MaxStrips; ++i)
    {
        if (isValidDir(defaultStepDirectories[static_cast<size_t>(i)]))
            return defaultStepDirectories[static_cast<size_t>(i)];
        if (isValidDir(defaultLoopDirectories[static_cast<size_t>(i)]))
            return defaultLoopDirectories[static_cast<size_t>(i)];

        const auto otherCurrentDir = currentStripFiles[static_cast<size_t>(i)].getParentDirectory();
        if (isValidDir(otherCurrentDir))
            return otherCurrentDir;
    }

    for (const auto& favoriteDir : browserFavoriteDirectories)
    {
        if (isValidDir(favoriteDir))
            return favoriteDir;
    }

    if (isValidDir(lastSampleFolder))
        return lastSampleFolder;

    // Last-resort fallback: allow browsing from home even with no configured paths.
    const auto homeDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    if (isValidDir(homeDir))
        return homeDir;

    return {};
}

juce::File StepVstHostAudioProcessor::getBrowserFavoriteDirectory(int slot) const
{
    if (slot < 0 || slot >= BrowserFavoriteSlots)
        return {};

    return browserFavoriteDirectories[static_cast<size_t>(slot)];
}

bool StepVstHostAudioProcessor::isBrowserFavoritePadHeld(int stripIndex, int slot) const
{
    if (stripIndex < 0 || stripIndex >= MaxStrips || slot < 0 || slot >= BrowserFavoriteSlots)
        return false;

    return browserFavoritePadHeld[static_cast<size_t>(stripIndex)][static_cast<size_t>(slot)];
}

bool StepVstHostAudioProcessor::isBrowserFavoriteSaveBurstActive(int slot, uint32_t nowMs) const
{
    if (slot < 0 || slot >= BrowserFavoriteSlots)
        return false;

    return nowMs < browserFavoriteSaveBurstUntilMs[static_cast<size_t>(slot)];
}

bool StepVstHostAudioProcessor::isBrowserFavoriteMissingBurstActive(int slot, uint32_t nowMs) const
{
    if (slot < 0 || slot >= BrowserFavoriteSlots)
        return false;

    return nowMs < browserFavoriteMissingBurstUntilMs[static_cast<size_t>(slot)];
}

void StepVstHostAudioProcessor::beginBrowserFavoritePadHold(int stripIndex, int slot)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips || slot < 0 || slot >= BrowserFavoriteSlots)
        return;

    const auto stripIdx = static_cast<size_t>(stripIndex);
    const auto slotIdx = static_cast<size_t>(slot);
    browserFavoritePadHeld[stripIdx][slotIdx] = true;
    browserFavoritePadHoldSaveTriggered[stripIdx][slotIdx] = false;
    browserFavoritePadPressStartMs[stripIdx][slotIdx] = juce::Time::getMillisecondCounter();
}

void StepVstHostAudioProcessor::endBrowserFavoritePadHold(int stripIndex, int slot)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips || slot < 0 || slot >= BrowserFavoriteSlots)
        return;

    const auto stripIdx = static_cast<size_t>(stripIndex);
    const auto slotIdx = static_cast<size_t>(slot);
    const bool wasHeld = browserFavoritePadHeld[stripIdx][slotIdx];
    const bool holdSaveTriggered = browserFavoritePadHoldSaveTriggered[stripIdx][slotIdx];

    if (wasHeld && !holdSaveTriggered)
    {
        if (!recallBrowserFavoriteDirectoryForStrip(stripIndex, slot))
            browserFavoriteMissingBurstUntilMs[slotIdx] = juce::Time::getMillisecondCounter() + browserFavoriteMissingBurstDurationMs;
    }

    browserFavoritePadHeld[stripIdx][slotIdx] = false;
    browserFavoritePadHoldSaveTriggered[stripIdx][slotIdx] = false;
}

void StepVstHostAudioProcessor::setDefaultSampleDirectory(int stripIndex, SamplePathMode mode, const juce::File& directory)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return;

    const auto idx = static_cast<size_t>(stripIndex);

    if (directory == juce::File())
    {
        if (mode == SamplePathMode::Step)
            defaultStepDirectories[idx] = juce::File();
        else
            defaultLoopDirectories[idx] = juce::File();
        savePersistentDefaultPaths();
        return;
    }

    if (!directory.exists() || !directory.isDirectory())
        return;

    if (mode == SamplePathMode::Step)
        defaultStepDirectories[idx] = directory;
    else
        defaultLoopDirectories[idx] = directory;

    savePersistentDefaultPaths();
}

bool StepVstHostAudioProcessor::saveBrowserFavoriteDirectoryFromStrip(int stripIndex, int slot)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips || slot < 0 || slot >= BrowserFavoriteSlots)
        return false;

    const auto directory = getCurrentBrowserDirectoryForStrip(stripIndex);
    if (!directory.exists() || !directory.isDirectory())
        return false;

    browserFavoriteDirectories[static_cast<size_t>(slot)] = directory;
    savePersistentDefaultPaths();
    return true;
}

bool StepVstHostAudioProcessor::recallBrowserFavoriteDirectoryForStrip(int stripIndex, int slot)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips || slot < 0 || slot >= BrowserFavoriteSlots)
        return false;

    const auto slotIdx = static_cast<size_t>(slot);
    const auto directory = browserFavoriteDirectories[slotIdx];
    if (!directory.exists() || !directory.isDirectory())
    {
        browserFavoriteDirectories[slotIdx] = juce::File();
        savePersistentDefaultPaths();
        return false;
    }

    const auto mode = getSamplePathModeForStrip(stripIndex);
    setDefaultSampleDirectory(stripIndex, mode, directory);
    lastSampleFolder = directory;
    return true;
}

bool StepVstHostAudioProcessor::isAudioFileSupported(const juce::File& file) const
{
    if (!file.existsAsFile())
        return false;

    return file.hasFileExtension(".wav")
        || file.hasFileExtension(".aif")
        || file.hasFileExtension(".aiff")
        || file.hasFileExtension(".mp3")
        || file.hasFileExtension(".ogg")
        || file.hasFileExtension(".flac");
}

void StepVstHostAudioProcessor::appendDefaultPathsToState(juce::ValueTree& state) const
{
    auto paths = state.getOrCreateChildWithName("DefaultPaths", nullptr);
    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        const auto loopKey = "loopDir" + juce::String(i);
        const auto stepKey = "stepDir" + juce::String(i);
        paths.setProperty(loopKey, defaultLoopDirectories[idx].getFullPathName(), nullptr);
        paths.setProperty(stepKey, defaultStepDirectories[idx].getFullPathName(), nullptr);
    }

    for (int slot = 0; slot < BrowserFavoriteSlots; ++slot)
    {
        const auto key = "favoriteDir" + juce::String(slot);
        paths.setProperty(key, browserFavoriteDirectories[static_cast<size_t>(slot)].getFullPathName(), nullptr);
    }
}

void StepVstHostAudioProcessor::appendHostedLaneMidiToState(juce::ValueTree& state) const
{
    auto hostedMidi = state.getOrCreateChildWithName("HostedLaneMidi", nullptr);
    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto chKey = "ch" + juce::String(i);
        const auto noteKey = "note" + juce::String(i);
        hostedMidi.setProperty(chKey, getLaneMidiChannel(i), nullptr);
        hostedMidi.setProperty(noteKey, getLaneMidiNote(i), nullptr);
    }
}

void StepVstHostAudioProcessor::appendControlPagesToState(juce::ValueTree& state) const
{
    auto controlPages = state.getOrCreateChildWithName("ControlPages", nullptr);
    const auto orderSnapshot = getControlPageOrder();
    for (int i = 0; i < NumControlRowPages; ++i)
    {
        const auto key = "slot" + juce::String(i);
        controlPages.setProperty(key, controlModeToKey(orderSnapshot[static_cast<size_t>(i)]), nullptr);
    }

    controlPages.setProperty("momentary", isControlPageMomentary(), nullptr);
    controlPages.setProperty("swingDivision", swingDivisionSelection.load(std::memory_order_acquire), nullptr);
    controlPages.setProperty("beatSpaceMorphMs", beatSpaceMorphDurationMs, nullptr);
    controlPages.setProperty("beatSpaceSelectedChannel", beatSpaceSelectedChannel, nullptr);
    controlPages.setProperty("beatSpaceLinkAllChannels", beatSpaceLinkAllChannels, nullptr);
    controlPages.setProperty("beatSpacePersistedPoints", true, nullptr);
    controlPages.setProperty("beatSpaceRandomMode", static_cast<int>(beatSpaceRandomizeMode), nullptr);
    controlPages.setProperty("beatSpaceConfidenceOverlay", beatSpaceConfidenceOverlayEnabled, nullptr);
    controlPages.setProperty("beatSpacePathOverlay", beatSpacePathOverlayEnabled, nullptr);
    controlPages.setProperty("beatSpaceCategoryConstrain", beatSpaceCategoryConstrainEnabled, nullptr);
    controlPages.setProperty("beatSpacePathArmedChannel", beatSpacePathRecordArmedChannel, nullptr);
    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        controlPages.setProperty(
            "beatSpacePointX" + juce::String(i),
            beatSpaceChannelPoints[idx].x,
            nullptr);
        controlPages.setProperty(
            "beatSpacePointY" + juce::String(i),
            beatSpaceChannelPoints[idx].y,
            nullptr);
        controlPages.setProperty(
            "beatSpaceZone" + juce::String(i),
            beatSpaceChannelCategoryAssignment[idx],
            nullptr);
        controlPages.setProperty(
            "beatSpaceZoneLock" + juce::String(i),
            beatSpaceZoneLockStrength[idx],
            nullptr);
        controlPages.setProperty(
            "beatSpaceManualCount" + juce::String(i),
            beatSpaceCategoryManualTagCounts[idx],
            nullptr);
        controlPages.setProperty(
            "beatSpaceManual" + juce::String(i),
            beatSpaceCategoryAnchorManual[idx],
            nullptr);
        controlPages.setProperty(
            "beatSpaceManualX" + juce::String(i),
            beatSpaceCategoryManualAnchors[idx].x,
            nullptr);
        controlPages.setProperty(
            "beatSpaceManualY" + juce::String(i),
            beatSpaceCategoryManualAnchors[idx].y,
            nullptr);
        const int tagCount = juce::jlimit(
            0,
            BeatSpacePresetSlotsPerSpace,
            beatSpaceCategoryManualTagCounts[idx]);
        for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
        {
            const auto slotIdx = static_cast<size_t>(slot);
            const auto p = (slot < tagCount)
                ? beatSpaceCategoryManualTagPoints[idx][slotIdx]
                : beatSpaceCategoryManualAnchors[idx];
            controlPages.setProperty(
                "beatSpaceTagX_" + juce::String(i) + "_" + juce::String(slot),
                p.x,
                nullptr);
            controlPages.setProperty(
                "beatSpaceTagY_" + juce::String(i) + "_" + juce::String(slot),
                p.y,
                nullptr);
            controlPages.setProperty(
                "beatSpacePresetOrder_" + juce::String(i) + "_" + juce::String(slot),
                beatSpaceCategoryPresetOrder[idx][slotIdx],
                nullptr);
            controlPages.setProperty(
                "beatSpacePresetHidden_" + juce::String(i) + "_" + juce::String(slot),
                beatSpaceCategoryPresetHidden[idx][slotIdx],
                nullptr);
            controlPages.setProperty(
                "beatSpacePresetLabel_" + juce::String(i) + "_" + juce::String(slot),
                beatSpaceCategoryPresetLabels[idx][slotIdx],
                nullptr);
        }

        const auto& path = beatSpacePaths[idx];
        controlPages.setProperty("beatSpacePathCount" + juce::String(i), path.count, nullptr);
        controlPages.setProperty("beatSpacePathActive" + juce::String(i), path.active, nullptr);
        controlPages.setProperty("beatSpacePathMode" + juce::String(i), static_cast<int>(path.mode), nullptr);
        controlPages.setProperty("beatSpacePathPlayMode" + juce::String(i), static_cast<int>(path.playMode), nullptr);
        controlPages.setProperty("beatSpacePathCycleBeats" + juce::String(i), path.cycleBeats, nullptr);
        controlPages.setProperty("beatSpacePathLoopBars" + juce::String(i), path.loopBars, nullptr);
        for (int p = 0; p < BeatSpacePathMaxPoints; ++p)
        {
            const auto& pathPoint = path.points[static_cast<size_t>(p)];
            controlPages.setProperty(
                "beatSpacePathX_" + juce::String(i) + "_" + juce::String(p),
                pathPoint.x,
                nullptr);
            controlPages.setProperty(
                "beatSpacePathY_" + juce::String(i) + "_" + juce::String(p),
                pathPoint.y,
                nullptr);
        }

        const auto& bookmarks = beatSpaceBookmarks[idx];
        for (int b = 0; b < BeatSpaceBookmarkSlots; ++b)
        {
            const auto& bookmark = bookmarks[static_cast<size_t>(b)];
            controlPages.setProperty(
                "beatSpaceBookmarkUsed_" + juce::String(i) + "_" + juce::String(b),
                bookmark.used,
                nullptr);
            controlPages.setProperty(
                "beatSpaceBookmarkX_" + juce::String(i) + "_" + juce::String(b),
                bookmark.point.x,
                nullptr);
            controlPages.setProperty(
                "beatSpaceBookmarkY_" + juce::String(i) + "_" + juce::String(b),
                bookmark.point.y,
                nullptr);
            controlPages.setProperty(
                "beatSpaceBookmarkTag_" + juce::String(i) + "_" + juce::String(b),
                bookmark.tag,
                nullptr);
        }

        const auto& stripPresets = microtonicStripPresets[idx];
        for (int slot = 0; slot < MicrotonicStripPresetSlots; ++slot)
        {
            const auto slotIdx = static_cast<size_t>(slot);
            const auto& preset = stripPresets[slotIdx];
            controlPages.setProperty(
                "microtonicPresetUsed_" + juce::String(i) + "_" + juce::String(slot),
                preset.used,
                nullptr);
            controlPages.setProperty(
                "microtonicPresetName_" + juce::String(i) + "_" + juce::String(slot),
                preset.name,
                nullptr);
            controlPages.setProperty(
                "microtonicPresetPointStored_" + juce::String(i) + "_" + juce::String(slot),
                preset.hasSavedBeatSpacePoint,
                nullptr);
            controlPages.setProperty(
                "microtonicPresetPointX_" + juce::String(i) + "_" + juce::String(slot),
                preset.beatSpacePoint.x,
                nullptr);
            controlPages.setProperty(
                "microtonicPresetPointY_" + juce::String(i) + "_" + juce::String(slot),
                preset.beatSpacePoint.y,
                nullptr);
            for (int patch = 0; patch < BeatSpacePatchParamCount; ++patch)
            {
                controlPages.setProperty(
                    "microtonicPresetVal_" + juce::String(i) + "_" + juce::String(slot) + "_"
                        + juce::String(patch),
                    preset.patchValues[static_cast<size_t>(patch)],
                    nullptr);
            }
        }
    }
}

void StepVstHostAudioProcessor::loadDefaultPathsFromState(const juce::ValueTree& state)
{
    auto paths = state.getChildWithName("DefaultPaths");
    if (!paths.isValid())
        return;

    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        const auto loopKey = "loopDir" + juce::String(i);
        const auto stepKey = "stepDir" + juce::String(i);

        juce::File loopDir(paths.getProperty(loopKey).toString());
        juce::File stepDir(paths.getProperty(stepKey).toString());

        if (loopDir.exists() && loopDir.isDirectory())
            defaultLoopDirectories[idx] = loopDir;
        else
            defaultLoopDirectories[idx] = juce::File();

        if (stepDir.exists() && stepDir.isDirectory())
            defaultStepDirectories[idx] = stepDir;
        else
            defaultStepDirectories[idx] = juce::File();
    }

    for (int slot = 0; slot < BrowserFavoriteSlots; ++slot)
    {
        const auto key = "favoriteDir" + juce::String(slot);
        juce::File favoriteDir(paths.getProperty(key).toString());
        if (favoriteDir.exists() && favoriteDir.isDirectory())
            browserFavoriteDirectories[static_cast<size_t>(slot)] = favoriteDir;
        else
            browserFavoriteDirectories[static_cast<size_t>(slot)] = juce::File();
    }

    savePersistentDefaultPaths();
}

void StepVstHostAudioProcessor::loadHostedLaneMidiFromState(const juce::ValueTree& state)
{
    auto hostedMidi = state.getChildWithName("HostedLaneMidi");
    if (!hostedMidi.isValid())
        return;

    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto chKey = "ch" + juce::String(i);
        const auto noteKey = "note" + juce::String(i);
        if (hostedMidi.hasProperty(chKey))
            setLaneMidiChannel(i, static_cast<int>(hostedMidi.getProperty(chKey)));
        if (hostedMidi.hasProperty(noteKey))
            setLaneMidiNote(i, static_cast<int>(hostedMidi.getProperty(noteKey)));
    }
}

void StepVstHostAudioProcessor::loadControlPagesFromState(const juce::ValueTree& state)
{
    auto controlPages = state.getChildWithName("ControlPages");
    if (!controlPages.isValid())
    {
        beatSpacePersistentLayoutLoaded = false;
        savePersistentControlPages();
        return;
    }

    const bool hasPersistedBeatSpaceLayoutFlag = controlPages.hasProperty("beatSpacePersistedLayout");
    bool hasPersistedBeatSpaceLayout = hasPersistedBeatSpaceLayoutFlag
        ? static_cast<bool>(controlPages.getProperty("beatSpacePersistedLayout", false))
        : false;
    const bool hasPersistedBeatSpacePointsFlag = controlPages.hasProperty("beatSpacePersistedPoints");
    bool hasPersistedBeatSpacePoints = hasPersistedBeatSpacePointsFlag
        ? static_cast<bool>(controlPages.getProperty("beatSpacePersistedPoints", false))
        : false;
    if (!hasPersistedBeatSpaceLayoutFlag)
    {
        for (int i = 0; i < BeatSpaceChannels; ++i)
        {
            const juce::String indexText(i);
            if (controlPages.hasProperty("beatSpaceZone" + indexText)
                || controlPages.hasProperty("beatSpaceManualCount" + indexText)
                || controlPages.hasProperty("beatSpaceTagX_" + indexText + "_0")
                || controlPages.hasProperty("beatSpacePresetOrder_" + indexText + "_0")
                || controlPages.hasProperty("beatSpacePresetHidden_" + indexText + "_0")
                || controlPages.hasProperty("beatSpacePresetLabel_" + indexText + "_0")
                || controlPages.hasProperty("beatSpaceBookmarkUsed_" + indexText + "_0")
                || controlPages.hasProperty("beatSpacePathCount" + indexText))
            {
                hasPersistedBeatSpaceLayout = true;
                break;
            }
        }
    }
    if (!hasPersistedBeatSpacePointsFlag)
    {
        for (int i = 0; i < BeatSpaceChannels; ++i)
        {
            const juce::String indexText(i);
            if (controlPages.hasProperty("beatSpacePointX" + indexText)
                || controlPages.hasProperty("beatSpacePointY" + indexText))
            {
                hasPersistedBeatSpacePoints = true;
                break;
            }
        }
    }
    beatSpacePersistentLayoutLoaded = hasPersistedBeatSpaceLayout && hasPersistedBeatSpacePoints;
    const bool shouldPersistNormalizedState = (!hasPersistedBeatSpaceLayoutFlag || !hasPersistedBeatSpacePointsFlag);

    ControlPageOrder parsedOrder{};
    int parsedCount = 0;

    for (int i = 0; i < NumControlRowPages; ++i)
    {
        const auto key = "slot" + juce::String(i);
        const auto value = controlPages.getProperty(key).toString();
        ControlMode mode = ControlMode::Normal;
        if (!controlModeFromKey(value, mode) || mode == ControlMode::Normal)
            continue;

        bool duplicate = false;
        for (int j = 0; j < parsedCount; ++j)
        {
            if (parsedOrder[static_cast<size_t>(j)] == mode)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        parsedOrder[static_cast<size_t>(parsedCount)] = mode;
        ++parsedCount;
    }

    const ControlPageOrder defaultOrder {
        ControlMode::Speed,
        ControlMode::Pan,
        ControlMode::Volume,
        ControlMode::Length,
        ControlMode::Swing,
        ControlMode::Gate,
        ControlMode::FileBrowser,
        ControlMode::GroupAssign,
        ControlMode::Filter,
        ControlMode::Pitch,
        ControlMode::Modulation,
        ControlMode::BeatSpace,
        ControlMode::Preset,
        ControlMode::StepEdit
    };

    for (const auto mode : defaultOrder)
    {
        bool alreadyPresent = false;
        for (int i = 0; i < parsedCount; ++i)
        {
            if (parsedOrder[static_cast<size_t>(i)] == mode)
            {
                alreadyPresent = true;
                break;
            }
        }
        if (!alreadyPresent && parsedCount < NumControlRowPages)
            parsedOrder[static_cast<size_t>(parsedCount++)] = mode;
    }

    if (parsedCount == NumControlRowPages)
    {
        const juce::ScopedLock lock(controlPageOrderLock);
        controlPageOrder = parsedOrder;
    }

    const bool momentary = controlPages.getProperty("momentary", true);
    controlPageMomentary.store(momentary, std::memory_order_release);
    const int swingDivision = static_cast<int>(controlPages.getProperty("swingDivision", 1));
    setSwingDivisionSelection(swingDivision);
    beatSpaceMorphDurationMs = juce::jlimit(
        40.0,
        2000.0,
        static_cast<double>(controlPages.getProperty("beatSpaceMorphMs", beatSpaceMorphDurationMs)));
    beatSpaceSelectedChannel = juce::jlimit(
        0,
        BeatSpaceChannels - 1,
        static_cast<int>(controlPages.getProperty("beatSpaceSelectedChannel", beatSpaceSelectedChannel)));
    beatSpaceLinkAllChannels = static_cast<bool>(
        controlPages.getProperty("beatSpaceLinkAllChannels", beatSpaceLinkAllChannels));
    beatSpaceRandomizeMode = static_cast<BeatSpaceRandomizeMode>(juce::jlimit(
        0,
        static_cast<int>(BeatSpaceRandomizeMode::FullWild),
        static_cast<int>(controlPages.getProperty(
            "beatSpaceRandomMode",
            static_cast<int>(BeatSpaceRandomizeMode::WithinCategory)))));
    beatSpaceConfidenceOverlayEnabled = static_cast<bool>(
        controlPages.getProperty("beatSpaceConfidenceOverlay", beatSpaceConfidenceOverlayEnabled));
    beatSpacePathOverlayEnabled = static_cast<bool>(
        controlPages.getProperty("beatSpacePathOverlay", beatSpacePathOverlayEnabled));
    beatSpaceCategoryConstrainEnabled = static_cast<bool>(
        controlPages.getProperty("beatSpaceCategoryConstrain", beatSpaceCategoryConstrainEnabled));
    beatSpacePathRecordArmedChannel = juce::jlimit(
        -1,
        BeatSpaceChannels - 1,
        static_cast<int>(controlPages.getProperty("beatSpacePathArmedChannel", -1)));

    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        beatSpaceChannelPoints[idx] = clampBeatSpacePointToTable({
            static_cast<int>(controlPages.getProperty(
                "beatSpacePointX" + juce::String(i),
                beatSpaceChannelPoints[idx].x)),
            static_cast<int>(controlPages.getProperty(
                "beatSpacePointY" + juce::String(i),
                beatSpaceChannelPoints[idx].y))
        });
        const auto key = "beatSpaceZone" + juce::String(i);
        const int fallbackZone = i;
        const int zone = static_cast<int>(controlPages.getProperty(key, fallbackZone));
        beatSpaceChannelCategoryAssignment[idx] =
            juce::jlimit(0, BeatSpaceChannels - 1, zone);
        beatSpaceZoneLockStrength[idx] = juce::jlimit(
            0.0f,
            1.0f,
            static_cast<float>(controlPages.getProperty(
                "beatSpaceZoneLock" + juce::String(i),
                beatSpaceZoneLockStrength[idx])));

        const int manualX = static_cast<int>(controlPages.getProperty(
            "beatSpaceManualX" + juce::String(i),
            beatSpaceCategoryManualAnchors[idx].x));
        const int manualY = static_cast<int>(controlPages.getProperty(
            "beatSpaceManualY" + juce::String(i),
            beatSpaceCategoryManualAnchors[idx].y));
        const bool legacyManual = static_cast<bool>(controlPages.getProperty(
            "beatSpaceManual" + juce::String(i),
            false));
        const int storedCount = static_cast<int>(controlPages.getProperty(
            "beatSpaceManualCount" + juce::String(i),
            legacyManual ? 1 : 0));
        int tagCount = juce::jlimit(0, BeatSpacePresetSlotsPerSpace, storedCount);
        auto& tags = beatSpaceCategoryManualTagPoints[idx];
        tags.fill({ manualX, manualY });

        if (tagCount <= 0 && legacyManual)
            tagCount = 1;

        for (int slot = 0; slot < tagCount; ++slot)
        {
            const int tagX = static_cast<int>(controlPages.getProperty(
                "beatSpaceTagX_" + juce::String(i) + "_" + juce::String(slot),
                (slot == 0 ? manualX : beatSpaceCategoryManualAnchors[idx].x)));
            const int tagY = static_cast<int>(controlPages.getProperty(
                "beatSpaceTagY_" + juce::String(i) + "_" + juce::String(slot),
                (slot == 0 ? manualY : beatSpaceCategoryManualAnchors[idx].y)));
            tags[static_cast<size_t>(slot)] = {
                juce::jlimit(0, BeatSpaceTableSize - 1, tagX),
                juce::jlimit(0, BeatSpaceTableSize - 1, tagY)
            };
        }

        for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
        {
            const auto slotIdx = static_cast<size_t>(slot);
            beatSpaceCategoryPresetOrder[idx][slotIdx] = static_cast<int>(controlPages.getProperty(
                "beatSpacePresetOrder_" + juce::String(i) + "_" + juce::String(slot),
                slot));
            beatSpaceCategoryPresetHidden[idx][slotIdx] = static_cast<bool>(controlPages.getProperty(
                "beatSpacePresetHidden_" + juce::String(i) + "_" + juce::String(slot),
                false));
            beatSpaceCategoryPresetLabels[idx][slotIdx] = controlPages.getProperty(
                "beatSpacePresetLabel_" + juce::String(i) + "_" + juce::String(slot),
                "").toString();
        }
        normalizeBeatSpacePresetLayoutForSpace(i);

        beatSpaceCategoryManualTagCounts[idx] = tagCount;
        beatSpaceCategoryAnchorManual[idx] = (tagCount > 0);
        beatSpaceCategoryManualAnchors[idx] = {
            juce::jlimit(0, BeatSpaceTableSize - 1, manualX),
            juce::jlimit(0, BeatSpaceTableSize - 1, manualY)
        };
        if (tagCount > 0)
        {
            int sumX = 0;
            int sumY = 0;
            for (int slot = 0; slot < tagCount; ++slot)
            {
                sumX += tags[static_cast<size_t>(slot)].x;
                sumY += tags[static_cast<size_t>(slot)].y;
            }
            beatSpaceCategoryManualAnchors[idx] = {
                juce::jlimit(0, BeatSpaceTableSize - 1, sumX / juce::jmax(1, tagCount)),
                juce::jlimit(0, BeatSpaceTableSize - 1, sumY / juce::jmax(1, tagCount))
            };
        }

        auto& path = beatSpacePaths[idx];
        path.count = juce::jlimit(
            0,
            BeatSpacePathMaxPoints,
            static_cast<int>(controlPages.getProperty("beatSpacePathCount" + juce::String(i), 0)));
        path.active = static_cast<bool>(controlPages.getProperty("beatSpacePathActive" + juce::String(i), false));
        path.mode = static_cast<BeatSpacePathMode>(juce::jlimit(
            0,
            static_cast<int>(BeatSpacePathMode::OneBar),
            static_cast<int>(controlPages.getProperty(
                "beatSpacePathMode" + juce::String(i),
                static_cast<int>(BeatSpacePathMode::QuarterNote)))));
        path.playMode = static_cast<BeatSpacePathPlayMode>(juce::jlimit(
            0,
            static_cast<int>(BeatSpacePathPlayMode::RandomSlice),
            static_cast<int>(controlPages.getProperty(
                "beatSpacePathPlayMode" + juce::String(i),
                static_cast<int>(BeatSpacePathPlayMode::Normal)))));
        const double fallbackCycleBeats = (path.mode == BeatSpacePathMode::QuarterNote) ? 0.25 : 4.0;
        const double storedCycleBeats = static_cast<double>(controlPages.getProperty(
            "beatSpacePathCycleBeats" + juce::String(i),
            fallbackCycleBeats));
        path.cycleBeats = (storedCycleBeats > 0.0)
            ? juce::jlimit(0.03125, 128.0, storedCycleBeats)
            : fallbackCycleBeats;
        path.loopBars = juce::jlimit(
            0,
            32,
            static_cast<int>(controlPages.getProperty(
                "beatSpacePathLoopBars" + juce::String(i),
                (path.mode == BeatSpacePathMode::OneBar) ? 1 : 0)));
        path.pendingQuantizedStart = false;
        path.recording = false;
        path.startMs = 0.0;
        path.startPpq = -1.0;
        path.recordStartPpq = -1.0;
        for (int p = 0; p < BeatSpacePathMaxPoints; ++p)
        {
            const int px = static_cast<int>(controlPages.getProperty(
                "beatSpacePathX_" + juce::String(i) + "_" + juce::String(p),
                beatSpaceChannelPoints[idx].x));
            const int py = static_cast<int>(controlPages.getProperty(
                "beatSpacePathY_" + juce::String(i) + "_" + juce::String(p),
                beatSpaceChannelPoints[idx].y));
            path.points[static_cast<size_t>(p)] = {
                juce::jlimit(0, BeatSpaceTableSize - 1, px),
                juce::jlimit(0, BeatSpaceTableSize - 1, py)
            };
        }
        if (path.count < 2)
            path.active = false;

        auto& bookmarks = beatSpaceBookmarks[idx];
        for (int b = 0; b < BeatSpaceBookmarkSlots; ++b)
        {
            auto& bookmark = bookmarks[static_cast<size_t>(b)];
            bookmark.used = static_cast<bool>(controlPages.getProperty(
                "beatSpaceBookmarkUsed_" + juce::String(i) + "_" + juce::String(b),
                false));
            bookmark.point = {
                juce::jlimit(0, BeatSpaceTableSize - 1, static_cast<int>(controlPages.getProperty(
                    "beatSpaceBookmarkX_" + juce::String(i) + "_" + juce::String(b),
                    beatSpaceChannelPoints[idx].x))),
                juce::jlimit(0, BeatSpaceTableSize - 1, static_cast<int>(controlPages.getProperty(
                    "beatSpaceBookmarkY_" + juce::String(i) + "_" + juce::String(b),
                    beatSpaceChannelPoints[idx].y)))
            };
            bookmark.tag = controlPages.getProperty(
                "beatSpaceBookmarkTag_" + juce::String(i) + "_" + juce::String(b),
                "").toString();
            if (bookmark.used && bookmark.tag.trim().isEmpty())
                bookmark.tag = "P " + juce::String(bookmark.point.x) + "," + juce::String(bookmark.point.y);
        }

        auto& stripPresets = microtonicStripPresets[idx];
        for (int slot = 0; slot < MicrotonicStripPresetSlots; ++slot)
        {
            const auto slotIdx = static_cast<size_t>(slot);
            auto& preset = stripPresets[slotIdx];
            preset.used = static_cast<bool>(controlPages.getProperty(
                "microtonicPresetUsed_" + juce::String(i) + "_" + juce::String(slot),
                false));
            preset.name = controlPages.getProperty(
                "microtonicPresetName_" + juce::String(i) + "_" + juce::String(slot),
                "").toString();
            preset.hasSavedBeatSpacePoint = static_cast<bool>(controlPages.getProperty(
                "microtonicPresetPointStored_" + juce::String(i) + "_" + juce::String(slot),
                false));
            preset.beatSpacePoint = clampBeatSpacePointToTable({
                juce::jlimit(0, BeatSpaceTableSize - 1, static_cast<int>(controlPages.getProperty(
                    "microtonicPresetPointX_" + juce::String(i) + "_" + juce::String(slot),
                    beatSpaceChannelPoints[idx].x))),
                juce::jlimit(0, BeatSpaceTableSize - 1, static_cast<int>(controlPages.getProperty(
                    "microtonicPresetPointY_" + juce::String(i) + "_" + juce::String(slot),
                    beatSpaceChannelPoints[idx].y)))
            });
            for (int patch = 0; patch < BeatSpacePatchParamCount; ++patch)
            {
                preset.patchValues[static_cast<size_t>(patch)] = juce::jlimit(
                    0.0f,
                    1.0f,
                    static_cast<float>(controlPages.getProperty(
                        "microtonicPresetVal_" + juce::String(i) + "_" + juce::String(slot) + "_"
                            + juce::String(patch),
                        0.0f)));
            }
            if (!preset.used)
                preset = MicrotonicStripPreset{};
        }
    }
    if (!beatSpacePersistentLayoutLoaded)
    {
        rebuildBeatSpaceCategoryAnchors();
        if (!hasPersistedBeatSpacePoints)
            applyBeatSpaceDefaultChannelLayout();
    }
    else
    {
        // Keep startup fast: defer expensive BeatSpace category derivation until
        // it is explicitly needed (fresh layout already has persisted points).
        beatSpaceCategoryAnchorsReady = false;
        beatSpaceCategoryPresetPointsReady.fill(false);
    }
    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        beatSpaceChannelPoints[static_cast<size_t>(i)] =
            clampBeatSpacePointToTable(beatSpaceChannelPoints[static_cast<size_t>(i)]);
    }
    if (beatSpaceLinkAllChannels)
    {
        const int selectedChannel = juce::jlimit(0, BeatSpaceChannels - 1, beatSpaceSelectedChannel);
        applyBeatSpaceLinkedChannelOffsets(
            beatSpaceChannelPoints[static_cast<size_t>(selectedChannel)],
            selectedChannel);
    }
    applyBeatSpacePointToChannels(true, false);
    if (shouldPersistNormalizedState)
        savePersistentControlPages();
}

void StepVstHostAudioProcessor::loadPersistentDefaultPaths()
{
    auto settingsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("step-vsthost")
        .getChildFile("DefaultPaths.xml");

    if (!settingsFile.existsAsFile())
    {
        savePersistentDefaultPaths();
        return;
    }

    auto xml = juce::XmlDocument::parse(settingsFile);
    if (xml == nullptr || xml->getTagName() != "DefaultPaths")
    {
        // Auto-heal missing/corrupt default path storage.
        savePersistentDefaultPaths();
        return;
    }

    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        juce::File loopDir(xml->getStringAttribute("loopDir" + juce::String(i)));
        juce::File stepDir(xml->getStringAttribute("stepDir" + juce::String(i)));

        if (loopDir.exists() && loopDir.isDirectory())
            defaultLoopDirectories[idx] = loopDir;
        else
            defaultLoopDirectories[idx] = juce::File();

        if (stepDir.exists() && stepDir.isDirectory())
            defaultStepDirectories[idx] = stepDir;
        else
            defaultStepDirectories[idx] = juce::File();
    }

    for (int slot = 0; slot < BrowserFavoriteSlots; ++slot)
    {
        juce::File favoriteDir(xml->getStringAttribute("favoriteDir" + juce::String(slot)));
        if (favoriteDir.exists() && favoriteDir.isDirectory())
            browserFavoriteDirectories[static_cast<size_t>(slot)] = favoriteDir;
        else
            browserFavoriteDirectories[static_cast<size_t>(slot)] = juce::File();
    }
}

void StepVstHostAudioProcessor::savePersistentDefaultPaths() const
{
    auto settingsDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("step-vsthost");
    if (!settingsDir.exists())
        settingsDir.createDirectory();

    auto settingsFile = settingsDir.getChildFile("DefaultPaths.xml");
    juce::XmlElement xml("DefaultPaths");

    for (int i = 0; i < MaxStrips; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        xml.setAttribute("loopDir" + juce::String(i), defaultLoopDirectories[idx].getFullPathName());
        xml.setAttribute("stepDir" + juce::String(i), defaultStepDirectories[idx].getFullPathName());
    }

    for (int slot = 0; slot < BrowserFavoriteSlots; ++slot)
        xml.setAttribute("favoriteDir" + juce::String(slot), browserFavoriteDirectories[static_cast<size_t>(slot)].getFullPathName());

    xml.writeTo(settingsFile);
}

void StepVstHostAudioProcessor::loadPersistentControlPages()
{
    const auto previousSuppress = suppressPersistentGlobalControlsSave.load(std::memory_order_acquire);
    suppressPersistentGlobalControlsSave.store(1, std::memory_order_release);
    auto xml = loadGlobalSettingsXml();
    if (xml == nullptr)
    {
        beatSpacePersistentLayoutLoaded = false;
        suppressPersistentGlobalControlsSave.store(previousSuppress, std::memory_order_release);
        savePersistentControlPages();
        return;
    }
    if (xml->getTagName() != "GlobalSettings")
    {
        beatSpacePersistentLayoutLoaded = false;
        suppressPersistentGlobalControlsSave.store(previousSuppress, std::memory_order_release);
        savePersistentControlPages();
        return;
    }

    juce::ValueTree state("StepVstHost");
    auto controlPages = juce::ValueTree("ControlPages");
    for (int i = 0; i < NumControlRowPages; ++i)
    {
        const auto key = "slot" + juce::String(i);
        controlPages.setProperty(key, xml->getStringAttribute(key), nullptr);
    }
    controlPages.setProperty("momentary", xml->getBoolAttribute("momentary", true), nullptr);
    controlPages.setProperty("swingDivision", xml->getIntAttribute("swingDivision", 1), nullptr);
    controlPages.setProperty("beatSpaceMorphMs", xml->getDoubleAttribute("beatSpaceMorphMs", beatSpaceMorphDurationMs), nullptr);
    controlPages.setProperty(
        "beatSpaceSelectedChannel",
        xml->getIntAttribute("beatSpaceSelectedChannel", beatSpaceSelectedChannel),
        nullptr);
    controlPages.setProperty(
        "beatSpaceLinkAllChannels",
        xml->getBoolAttribute("beatSpaceLinkAllChannels", beatSpaceLinkAllChannels),
        nullptr);
    controlPages.setProperty(
        "beatSpaceRandomMode",
        xml->getIntAttribute("beatSpaceRandomMode", static_cast<int>(BeatSpaceRandomizeMode::WithinCategory)),
        nullptr);
    controlPages.setProperty(
        "beatSpaceConfidenceOverlay",
        xml->getBoolAttribute("beatSpaceConfidenceOverlay", beatSpaceConfidenceOverlayEnabled),
        nullptr);
    controlPages.setProperty(
        "beatSpacePathOverlay",
        xml->getBoolAttribute("beatSpacePathOverlay", beatSpacePathOverlayEnabled),
        nullptr);
    controlPages.setProperty(
        "beatSpaceCategoryConstrain",
        xml->getBoolAttribute("beatSpaceCategoryConstrain", beatSpaceCategoryConstrainEnabled),
        nullptr);
    controlPages.setProperty(
        "beatSpacePathArmedChannel",
        xml->getIntAttribute("beatSpacePathArmedChannel", -1),
        nullptr);
    bool hasPersistedBeatSpaceLayout = false;
    bool hasPersistedBeatSpacePoints = xml->getBoolAttribute("beatSpacePersistedPoints", false);
    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        const juce::String indexText(i);
        const bool channelHasBeatSpaceData =
            xml->hasAttribute("beatSpaceZone" + indexText)
            || xml->hasAttribute("beatSpaceManualCount" + indexText)
            || xml->hasAttribute("beatSpaceTagX_" + indexText + "_0")
            || xml->hasAttribute("beatSpacePresetOrder_" + indexText + "_0")
            || xml->hasAttribute("beatSpacePresetHidden_" + indexText + "_0")
            || xml->hasAttribute("beatSpacePresetLabel_" + indexText + "_0")
            || xml->hasAttribute("beatSpaceBookmarkUsed_" + indexText + "_0")
            || xml->hasAttribute("beatSpacePathCount" + indexText)
            || xml->hasAttribute("beatSpacePathPlayMode" + indexText);
        hasPersistedBeatSpaceLayout = hasPersistedBeatSpaceLayout || channelHasBeatSpaceData;
        const bool channelHasPointData =
            xml->hasAttribute("beatSpacePointX" + indexText)
            || xml->hasAttribute("beatSpacePointY" + indexText);
        hasPersistedBeatSpacePoints = hasPersistedBeatSpacePoints || channelHasPointData;
        controlPages.setProperty(
            "beatSpacePointX" + juce::String(i),
            xml->getIntAttribute("beatSpacePointX" + juce::String(i), beatSpaceChannelPoints[idx].x),
            nullptr);
        controlPages.setProperty(
            "beatSpacePointY" + juce::String(i),
            xml->getIntAttribute("beatSpacePointY" + juce::String(i), beatSpaceChannelPoints[idx].y),
            nullptr);
        controlPages.setProperty(
            "beatSpaceZone" + juce::String(i),
            xml->getIntAttribute("beatSpaceZone" + juce::String(i), i),
            nullptr);
        controlPages.setProperty(
            "beatSpaceZoneLock" + juce::String(i),
            xml->getDoubleAttribute("beatSpaceZoneLock" + juce::String(i), beatSpaceZoneLockStrength[idx]),
            nullptr);
        controlPages.setProperty(
            "beatSpaceManualCount" + juce::String(i),
            xml->getIntAttribute("beatSpaceManualCount" + juce::String(i), 0),
            nullptr);
        controlPages.setProperty(
            "beatSpaceManual" + juce::String(i),
            xml->getBoolAttribute("beatSpaceManual" + juce::String(i), false),
            nullptr);
        controlPages.setProperty(
            "beatSpaceManualX" + juce::String(i),
            xml->getIntAttribute("beatSpaceManualX" + juce::String(i), beatSpaceCategoryManualAnchors[idx].x),
            nullptr);
        controlPages.setProperty(
            "beatSpaceManualY" + juce::String(i),
            xml->getIntAttribute("beatSpaceManualY" + juce::String(i), beatSpaceCategoryManualAnchors[idx].y),
            nullptr);
        for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
        {
            controlPages.setProperty(
                "beatSpaceTagX_" + juce::String(i) + "_" + juce::String(slot),
                xml->getIntAttribute(
                    "beatSpaceTagX_" + juce::String(i) + "_" + juce::String(slot),
                    beatSpaceCategoryManualAnchors[idx].x),
                nullptr);
            controlPages.setProperty(
                "beatSpaceTagY_" + juce::String(i) + "_" + juce::String(slot),
                xml->getIntAttribute(
                    "beatSpaceTagY_" + juce::String(i) + "_" + juce::String(slot),
                    beatSpaceCategoryManualAnchors[idx].y),
                nullptr);
            controlPages.setProperty(
                "beatSpacePresetOrder_" + juce::String(i) + "_" + juce::String(slot),
                xml->getIntAttribute(
                    "beatSpacePresetOrder_" + juce::String(i) + "_" + juce::String(slot),
                    slot),
                nullptr);
            controlPages.setProperty(
                "beatSpacePresetHidden_" + juce::String(i) + "_" + juce::String(slot),
                xml->getBoolAttribute(
                    "beatSpacePresetHidden_" + juce::String(i) + "_" + juce::String(slot),
                    false),
                nullptr);
            controlPages.setProperty(
                "beatSpacePresetLabel_" + juce::String(i) + "_" + juce::String(slot),
                xml->getStringAttribute(
                    "beatSpacePresetLabel_" + juce::String(i) + "_" + juce::String(slot),
                    {}),
                nullptr);
        }

        controlPages.setProperty(
            "beatSpacePathCount" + juce::String(i),
            xml->getIntAttribute("beatSpacePathCount" + juce::String(i), 0),
            nullptr);
        controlPages.setProperty(
            "beatSpacePathActive" + juce::String(i),
            xml->getBoolAttribute("beatSpacePathActive" + juce::String(i), false),
            nullptr);
        controlPages.setProperty(
            "beatSpacePathMode" + juce::String(i),
            xml->getIntAttribute(
                "beatSpacePathMode" + juce::String(i),
                static_cast<int>(BeatSpacePathMode::QuarterNote)),
            nullptr);
        controlPages.setProperty(
            "beatSpacePathPlayMode" + juce::String(i),
            xml->getIntAttribute(
                "beatSpacePathPlayMode" + juce::String(i),
                static_cast<int>(BeatSpacePathPlayMode::Normal)),
            nullptr);
        controlPages.setProperty(
            "beatSpacePathCycleBeats" + juce::String(i),
            xml->getDoubleAttribute(
                "beatSpacePathCycleBeats" + juce::String(i),
                0.0),
            nullptr);
        controlPages.setProperty(
            "beatSpacePathLoopBars" + juce::String(i),
            xml->getIntAttribute("beatSpacePathLoopBars" + juce::String(i), 0),
            nullptr);
        for (int p = 0; p < BeatSpacePathMaxPoints; ++p)
        {
            controlPages.setProperty(
                "beatSpacePathX_" + juce::String(i) + "_" + juce::String(p),
                xml->getIntAttribute(
                    "beatSpacePathX_" + juce::String(i) + "_" + juce::String(p),
                    beatSpaceChannelPoints[idx].x),
                nullptr);
            controlPages.setProperty(
                "beatSpacePathY_" + juce::String(i) + "_" + juce::String(p),
                xml->getIntAttribute(
                    "beatSpacePathY_" + juce::String(i) + "_" + juce::String(p),
                    beatSpaceChannelPoints[idx].y),
                nullptr);
        }

        for (int b = 0; b < BeatSpaceBookmarkSlots; ++b)
        {
            controlPages.setProperty(
                "beatSpaceBookmarkUsed_" + juce::String(i) + "_" + juce::String(b),
                xml->getBoolAttribute("beatSpaceBookmarkUsed_" + juce::String(i) + "_" + juce::String(b), false),
                nullptr);
            controlPages.setProperty(
                "beatSpaceBookmarkX_" + juce::String(i) + "_" + juce::String(b),
                xml->getIntAttribute(
                    "beatSpaceBookmarkX_" + juce::String(i) + "_" + juce::String(b),
                    beatSpaceChannelPoints[idx].x),
                nullptr);
            controlPages.setProperty(
                "beatSpaceBookmarkY_" + juce::String(i) + "_" + juce::String(b),
                xml->getIntAttribute(
                    "beatSpaceBookmarkY_" + juce::String(i) + "_" + juce::String(b),
                    beatSpaceChannelPoints[idx].y),
                nullptr);
            controlPages.setProperty(
                "beatSpaceBookmarkTag_" + juce::String(i) + "_" + juce::String(b),
                xml->getStringAttribute("beatSpaceBookmarkTag_" + juce::String(i) + "_" + juce::String(b), {}),
                nullptr);
        }

        for (int slot = 0; slot < MicrotonicStripPresetSlots; ++slot)
        {
            controlPages.setProperty(
                "microtonicPresetUsed_" + juce::String(i) + "_" + juce::String(slot),
                xml->getBoolAttribute(
                    "microtonicPresetUsed_" + juce::String(i) + "_" + juce::String(slot),
                    false),
                nullptr);
            controlPages.setProperty(
                "microtonicPresetName_" + juce::String(i) + "_" + juce::String(slot),
                xml->getStringAttribute(
                    "microtonicPresetName_" + juce::String(i) + "_" + juce::String(slot),
                    {}),
                nullptr);
            controlPages.setProperty(
                "microtonicPresetPointStored_" + juce::String(i) + "_" + juce::String(slot),
                xml->getBoolAttribute(
                    "microtonicPresetPointStored_" + juce::String(i) + "_" + juce::String(slot),
                    false),
                nullptr);
            controlPages.setProperty(
                "microtonicPresetPointX_" + juce::String(i) + "_" + juce::String(slot),
                xml->getIntAttribute(
                    "microtonicPresetPointX_" + juce::String(i) + "_" + juce::String(slot),
                    beatSpaceChannelPoints[idx].x),
                nullptr);
            controlPages.setProperty(
                "microtonicPresetPointY_" + juce::String(i) + "_" + juce::String(slot),
                xml->getIntAttribute(
                    "microtonicPresetPointY_" + juce::String(i) + "_" + juce::String(slot),
                    beatSpaceChannelPoints[idx].y),
                nullptr);
            for (int patch = 0; patch < BeatSpacePatchParamCount; ++patch)
            {
                controlPages.setProperty(
                    "microtonicPresetVal_" + juce::String(i) + "_" + juce::String(slot) + "_"
                        + juce::String(patch),
                    xml->getDoubleAttribute(
                        "microtonicPresetVal_" + juce::String(i) + "_" + juce::String(slot) + "_"
                            + juce::String(patch),
                        0.0),
                    nullptr);
            }
        }
    }
    controlPages.setProperty("beatSpacePersistedLayout", hasPersistedBeatSpaceLayout, nullptr);
    controlPages.setProperty("beatSpacePersistedPoints", hasPersistedBeatSpacePoints, nullptr);
    state.addChild(controlPages, -1, nullptr);

    loadControlPagesFromState(state);
    suppressPersistentGlobalControlsSave.store(previousSuppress, std::memory_order_release);
    appendGlobalSettingsDiagnostic("load-control-pages", xml.get());
}

void StepVstHostAudioProcessor::loadPersistentGlobalControls()
{
    auto xml = loadGlobalSettingsXml();
    if (xml == nullptr || xml->getTagName() != "GlobalSettings")
    {
        persistentGlobalControlsReady.store(1, std::memory_order_release);
        return;
    }

    {
        const juce::ScopedLock lock(hostedInstrumentFileLock);
        defaultHostedInstrumentFile = juce::File(xml->getStringAttribute("defaultHostedPluginPath"));
    }

    auto restoreFloatParam = [this, &xml](const char* attrName, const char* paramId, double minValue, double maxValue) -> bool
    {
        if (!xml->hasAttribute(attrName))
            return false;
        auto* param = parameters.getParameter(paramId);
        if (param == nullptr)
            return false;
        const auto restored = static_cast<float>(
            juce::jlimit(minValue, maxValue, xml->getDoubleAttribute(attrName)));
        param->setValueNotifyingHost(param->convertTo0to1(restored));
        return true;
    };

    auto restoreChoiceParam = [this, &xml](const char* attrName, const char* paramId, int minValue, int maxValue) -> bool
    {
        if (!xml->hasAttribute(attrName))
            return false;
        auto* param = parameters.getParameter(paramId);
        if (param == nullptr)
            return false;
        const auto restored = static_cast<float>(
            juce::jlimit(minValue, maxValue, xml->getIntAttribute(attrName)));
        param->setValueNotifyingHost(param->convertTo0to1(restored));
        return true;
    };

    auto restoreBoolParam = [this, &xml](const char* attrName, const char* paramId) -> bool
    {
        if (!xml->hasAttribute(attrName))
            return false;
        auto* param = parameters.getParameter(paramId);
        if (param == nullptr)
            return false;
        param->setValueNotifyingHost(param->convertTo0to1(xml->getBoolAttribute(attrName) ? 1.0f : 0.0f));
        return true;
    };

    suppressPersistentGlobalControlsSave.store(1, std::memory_order_release);
    bool anyRestored = false;
    anyRestored = restoreFloatParam("masterVolume", "masterVolume", 0.0, 1.0) || anyRestored;
    anyRestored = restoreFloatParam("limiterThreshold", "limiterThreshold", -24.0, 0.0) || anyRestored;
    anyRestored = restoreBoolParam("limiterEnabled", "limiterEnabled") || anyRestored;
    anyRestored = restoreChoiceParam("quantize", "quantize", 0, 9) || anyRestored;
    anyRestored = restoreFloatParam("pitchSmoothing", "pitchSmoothing", 0.0, 1.0) || anyRestored;
    anyRestored = restoreChoiceParam("outputRouting", "outputRouting", 0, 1) || anyRestored;
    anyRestored = restoreBoolParam("soundTouchEnabled", "soundTouchEnabled") || anyRestored;
    anyRestored = restoreBoolParam("kitScaleEnabled", "kitScaleEnabled") || anyRestored;
    anyRestored = restoreChoiceParam(
        "kitScaleMode",
        "kitScaleMode",
        0,
        static_cast<int>(ModernAudioEngine::PitchScale::PentatonicMinor)) || anyRestored;
    anyRestored = restoreChoiceParam("kitScaleRoot", "kitScaleRoot", 0, 11) || anyRestored;
    suppressPersistentGlobalControlsSave.store(0, std::memory_order_release);
    persistentGlobalControlsDirty.store(0, std::memory_order_release);

    if (!anyRestored)
        savePersistentControlPages();
    else
        saveGlobalSettingsXml(*xml);

    persistentGlobalControlsReady.store(1, std::memory_order_release);
    appendGlobalSettingsDiagnostic("load-globals", xml.get());
}

void StepVstHostAudioProcessor::savePersistentControlPages() const
{
    if (suppressPersistentGlobalControlsSave.load(std::memory_order_acquire) != 0)
        return;
    juce::XmlElement xml("GlobalSettings");
    const auto orderSnapshot = getControlPageOrder();
    for (int i = 0; i < NumControlRowPages; ++i)
    {
        const auto key = "slot" + juce::String(i);
        xml.setAttribute(key, controlModeToKey(orderSnapshot[static_cast<size_t>(i)]));
    }
    xml.setAttribute("momentary", isControlPageMomentary());
    xml.setAttribute("swingDivision", swingDivisionSelection.load(std::memory_order_acquire));
    xml.setAttribute("beatSpaceMorphMs", beatSpaceMorphDurationMs);
    xml.setAttribute("beatSpacePersistedPoints", true);
    xml.setAttribute("beatSpaceSelectedChannel", beatSpaceSelectedChannel);
    xml.setAttribute("beatSpaceLinkAllChannels", beatSpaceLinkAllChannels);
    xml.setAttribute("beatSpaceRandomMode", static_cast<int>(beatSpaceRandomizeMode));
    xml.setAttribute("beatSpaceConfidenceOverlay", beatSpaceConfidenceOverlayEnabled);
    xml.setAttribute("beatSpacePathOverlay", beatSpacePathOverlayEnabled);
    xml.setAttribute("beatSpaceCategoryConstrain", beatSpaceCategoryConstrainEnabled);
    xml.setAttribute("beatSpacePathArmedChannel", beatSpacePathRecordArmedChannel);
    for (int i = 0; i < BeatSpaceChannels; ++i)
    {
        const auto idx = static_cast<size_t>(i);
        xml.setAttribute(
            "beatSpacePointX" + juce::String(i),
            beatSpaceChannelPoints[idx].x);
        xml.setAttribute(
            "beatSpacePointY" + juce::String(i),
            beatSpaceChannelPoints[idx].y);
        xml.setAttribute(
            "beatSpaceZone" + juce::String(i),
            beatSpaceChannelCategoryAssignment[idx]);
        xml.setAttribute(
            "beatSpaceZoneLock" + juce::String(i),
            static_cast<double>(beatSpaceZoneLockStrength[idx]));
        xml.setAttribute(
            "beatSpaceManualCount" + juce::String(i),
            beatSpaceCategoryManualTagCounts[idx]);
        xml.setAttribute(
            "beatSpaceManual" + juce::String(i),
            beatSpaceCategoryAnchorManual[idx]);
        xml.setAttribute(
            "beatSpaceManualX" + juce::String(i),
            beatSpaceCategoryManualAnchors[idx].x);
        xml.setAttribute(
            "beatSpaceManualY" + juce::String(i),
            beatSpaceCategoryManualAnchors[idx].y);
        const int tagCount = juce::jlimit(
            0,
            BeatSpacePresetSlotsPerSpace,
            beatSpaceCategoryManualTagCounts[idx]);
        for (int slot = 0; slot < BeatSpacePresetSlotsPerSpace; ++slot)
        {
            const auto slotIdx = static_cast<size_t>(slot);
            const auto p = (slot < tagCount)
                ? beatSpaceCategoryManualTagPoints[idx][slotIdx]
                : beatSpaceCategoryManualAnchors[idx];
            xml.setAttribute(
                "beatSpaceTagX_" + juce::String(i) + "_" + juce::String(slot),
                p.x);
            xml.setAttribute(
                "beatSpaceTagY_" + juce::String(i) + "_" + juce::String(slot),
                p.y);
            xml.setAttribute(
                "beatSpacePresetOrder_" + juce::String(i) + "_" + juce::String(slot),
                beatSpaceCategoryPresetOrder[idx][slotIdx]);
            xml.setAttribute(
                "beatSpacePresetHidden_" + juce::String(i) + "_" + juce::String(slot),
                beatSpaceCategoryPresetHidden[idx][slotIdx]);
            xml.setAttribute(
                "beatSpacePresetLabel_" + juce::String(i) + "_" + juce::String(slot),
                beatSpaceCategoryPresetLabels[idx][slotIdx]);
        }

        const auto& path = beatSpacePaths[idx];
        xml.setAttribute("beatSpacePathCount" + juce::String(i), path.count);
        xml.setAttribute("beatSpacePathActive" + juce::String(i), path.active);
        xml.setAttribute("beatSpacePathMode" + juce::String(i), static_cast<int>(path.mode));
        xml.setAttribute("beatSpacePathPlayMode" + juce::String(i), static_cast<int>(path.playMode));
        xml.setAttribute("beatSpacePathCycleBeats" + juce::String(i), path.cycleBeats);
        xml.setAttribute("beatSpacePathLoopBars" + juce::String(i), path.loopBars);
        for (int p = 0; p < BeatSpacePathMaxPoints; ++p)
        {
            const auto& pathPoint = path.points[static_cast<size_t>(p)];
            xml.setAttribute(
                "beatSpacePathX_" + juce::String(i) + "_" + juce::String(p),
                pathPoint.x);
            xml.setAttribute(
                "beatSpacePathY_" + juce::String(i) + "_" + juce::String(p),
                pathPoint.y);
        }

        const auto& bookmarks = beatSpaceBookmarks[idx];
        for (int b = 0; b < BeatSpaceBookmarkSlots; ++b)
        {
            const auto& bookmark = bookmarks[static_cast<size_t>(b)];
            xml.setAttribute(
                "beatSpaceBookmarkUsed_" + juce::String(i) + "_" + juce::String(b),
                bookmark.used);
            xml.setAttribute(
                "beatSpaceBookmarkX_" + juce::String(i) + "_" + juce::String(b),
                bookmark.point.x);
            xml.setAttribute(
                "beatSpaceBookmarkY_" + juce::String(i) + "_" + juce::String(b),
                bookmark.point.y);
            xml.setAttribute(
                "beatSpaceBookmarkTag_" + juce::String(i) + "_" + juce::String(b),
                bookmark.tag);
        }

        const auto& stripPresets = microtonicStripPresets[idx];
        for (int slot = 0; slot < MicrotonicStripPresetSlots; ++slot)
        {
            const auto slotIdx = static_cast<size_t>(slot);
            const auto& preset = stripPresets[slotIdx];
            xml.setAttribute(
                "microtonicPresetUsed_" + juce::String(i) + "_" + juce::String(slot),
                preset.used);
            xml.setAttribute(
                "microtonicPresetName_" + juce::String(i) + "_" + juce::String(slot),
                preset.name);
            xml.setAttribute(
                "microtonicPresetPointStored_" + juce::String(i) + "_" + juce::String(slot),
                preset.hasSavedBeatSpacePoint);
            xml.setAttribute(
                "microtonicPresetPointX_" + juce::String(i) + "_" + juce::String(slot),
                preset.beatSpacePoint.x);
            xml.setAttribute(
                "microtonicPresetPointY_" + juce::String(i) + "_" + juce::String(slot),
                preset.beatSpacePoint.y);
            for (int patch = 0; patch < BeatSpacePatchParamCount; ++patch)
            {
                xml.setAttribute(
                    "microtonicPresetVal_" + juce::String(i) + "_" + juce::String(slot) + "_"
                        + juce::String(patch),
                    static_cast<double>(preset.patchValues[static_cast<size_t>(patch)]));
            }
        }
    }
    if (masterVolumeParam)
        xml.setAttribute("masterVolume", static_cast<double>(masterVolumeParam->load(std::memory_order_acquire)));
    if (limiterThresholdParam)
        xml.setAttribute("limiterThreshold", static_cast<double>(limiterThresholdParam->load(std::memory_order_acquire)));
    if (limiterEnabledParam)
        xml.setAttribute("limiterEnabled", limiterEnabledParam->load(std::memory_order_acquire) >= 0.5f);
    if (quantizeParam)
        xml.setAttribute("quantize", static_cast<int>(quantizeParam->load(std::memory_order_acquire)));
    if (pitchSmoothingParam)
        xml.setAttribute("pitchSmoothing", static_cast<double>(pitchSmoothingParam->load(std::memory_order_acquire)));
    if (outputRoutingParam)
        xml.setAttribute("outputRouting", static_cast<int>(outputRoutingParam->load(std::memory_order_acquire)));
    if (soundTouchEnabledParam)
        xml.setAttribute("soundTouchEnabled", soundTouchEnabledParam->load(std::memory_order_acquire) >= 0.5f);
    if (kitScaleEnabledParam)
        xml.setAttribute("kitScaleEnabled", kitScaleEnabledParam->load(std::memory_order_acquire) >= 0.5f);
    if (kitScaleModeParam)
        xml.setAttribute("kitScaleMode", static_cast<int>(kitScaleModeParam->load(std::memory_order_acquire)));
    if (kitScaleRootParam)
        xml.setAttribute("kitScaleRoot", static_cast<int>(kitScaleRootParam->load(std::memory_order_acquire)));
    {
        const juce::ScopedLock lock(hostedInstrumentFileLock);
        xml.setAttribute("defaultHostedPluginPath", defaultHostedInstrumentFile.getFullPathName());
    }
    saveGlobalSettingsXml(xml);
    appendGlobalSettingsDiagnostic("save-globals", &xml);
}

void StepVstHostAudioProcessor::triggerStrip(int stripIndex, int column)
{
    if (!audioEngine) return;
    
    auto* strip = audioEngine->getStrip(stripIndex);
    if (!strip) return;

    // If bar length was changed while playing, apply it on the next row trigger.
    const auto stripIdx = static_cast<size_t>(stripIndex);
    if (pendingBarLengthApply[stripIdx] && strip->hasAudio())
    {
        const int bars = juce::jlimit(1, 8, strip->getRecordingBars());
        strip->setBeatsPerLoop(static_cast<float>(bars * 4));
        pendingBarLengthApply[stripIdx] = false;
    }

    // CHECK: If inner loop is active, clear it and return to full loop
    if (strip->getLoopStart() != 0 || strip->getLoopEnd() != MaxColumns)
    {
        const int targetColumn = juce::jlimit(0, MaxColumns - 1, column);
        bool updatedPendingClear = false;
        {
            const juce::ScopedLock lock(pendingLoopChangeLock);
            auto& pending = pendingLoopChanges[static_cast<size_t>(stripIndex)];
            if (pending.active && pending.clear)
            {
                // Keep a single quantized clear request active, but allow the
                // user's latest pad press to define the post-exit position.
                pending.markerColumn = targetColumn;
                pending.postClearTriggerArmed = false;
                updatedPendingClear = true;
            }
        }

        if (updatedPendingClear)
        {
            DBG("Inner loop clear pending on strip " << stripIndex
                << " -> updated marker column " << targetColumn);
            return;
        }

        // Inner loop is active: this press both clears the loop and defines
        // the re-entry column, applied together on the quantized boundary.
        queueLoopChange(stripIndex, true, 0, MaxColumns, false, targetColumn);
        DBG("Inner loop clear+retrigger requested on strip " << stripIndex
            << " -> column " << targetColumn << " (quantized)");
        return;
    }
    
    const double timelineBeat = audioEngine->getTimelineBeat();

    juce::AudioPlayHead::PositionInfo posInfo;
    if (auto* playHead = getPlayHead())
        posInfo = playHead->getPosition().orFallback(juce::AudioPlayHead::PositionInfo());
    
    const int quantizeValue = getQuantizeDivision();
    
    // Calculate what the quantBeats will be
    double quantBeats = 4.0 / quantizeValue;
    
    // Use host PPQ when available. This must match quantized scheduler timing.
    const double currentPPQ = posInfo.getPpqPosition().hasValue() ? *posInfo.getPpqPosition() : timelineBeat;
    int64_t globalSample = audioEngine->getGlobalSampleCount();
    
    // Calculate next grid position
    double nextGridPPQ = std::ceil(currentPPQ / quantBeats) * quantBeats;
    nextGridPPQ = std::round(nextGridPPQ / quantBeats) * quantBeats;
    
    // Check if gate is closed (trigger pending)
    bool gateClosed = audioEngine->hasPendingTrigger(stripIndex);
    
    // Set quantization on the audio engine
    audioEngine->setQuantization(quantizeValue);
    
    // Apply quantization if enabled
    bool useQuantize = quantizeValue > 1;
    const bool isHoldScratchTransition = (strip->getScratchAmount() > 0.0f
        && ((strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step)
            ? strip->isButtonHeld()
            : (strip->getHeldButtonCount() > 1)));
    if (isHoldScratchTransition)
        useQuantize = false;
    
    // ============================================================
    // COMPREHENSIVE DEBUG LOGGING
    // ============================================================
    if (kEnableTriggerDebugLogging)
    {
        juce::File logFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                               .getChildFile("step-vsthost_debug.txt");
        juce::FileOutputStream stream(logFile, 1024);
        if (stream.openedOk())
        {
            juce::String timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
            juce::String msg =
                "═══════════════════════════════════════════════════════\n"
                "BUTTON PRESS: " + timestamp + "\n"
                "───────────────────────────────────────────────────────\n"
                "Strip: " + juce::String(stripIndex) + " | Column: " + juce::String(column) + "\n"
                "───────────────────────────────────────────────────────\n"
                "PLAYHEAD POSITION:\n"
                "  currentPPQ:     " + juce::String(currentPPQ, 6) + "\n"
                "  currentBeat:    " + juce::String(timelineBeat, 6) + "\n"
                "  globalSample:   " + juce::String(globalSample) + "\n"
                "───────────────────────────────────────────────────────\n"
                "QUANTIZATION SETTINGS:\n"
                "  quantizeValue:   " + juce::String(quantizeValue) + " (divisions per bar)\n"
                "  quantBeats:      " + juce::String(quantBeats, 4) + " beats per division\n"
                "  useQuantize:     " + juce::String(useQuantize ? "YES" : "NO") + "\n"
                "───────────────────────────────────────────────────────\n"
                "GRID CALCULATION:\n"
                "  nextGridPPQ:    " + juce::String(nextGridPPQ, 6) + "\n"
                "  beatsToWait:    " + juce::String(nextGridPPQ - currentPPQ, 6) + "\n"
                "───────────────────────────────────────────────────────\n"
                "GATE STATUS:\n"
                "  gateClosed:     " + juce::String(gateClosed ? "YES (trigger pending)" : "NO (ready)") + "\n"
                "  ACTION:         " + juce::String(gateClosed ? "IGNORE THIS PRESS" : "SCHEDULE TRIGGER") + "\n"
                "───────────────────────────────────────────────────────\n"
                "PATH: " + juce::String(useQuantize ? "QUANTIZED" : "IMMEDIATE") + "\n"
                "═══════════════════════════════════════════════════════\n\n";
            stream.writeText(msg, false, false, nullptr);
        }
    }
    
    // Strict gate behavior: ignore extra presses while quantized trigger is pending.
    if (useQuantize && gateClosed)
    {
        updateMonomeLEDs();
        return;
    }

    if (useQuantize)
    {
        // Schedule for next quantize point - group choke handled in batch execution
        DBG("=== SCHEDULING QUANTIZED TRIGGER === Strip " << stripIndex 
            << " Column " << column 
            << " Quantize: " << quantizeValue);
        audioEngine->scheduleQuantizedTrigger(stripIndex, column, currentPPQ);
    }
    else
    {
        // Immediate trigger - handle group choke here with short fade in engine path.
        audioEngine->enforceGroupExclusivity(stripIndex, false);
        
        // Trigger immediately with PPQ sync
        int64_t triggerGlobalSample = audioEngine->getGlobalSampleCount();
        
        strip->triggerAtSample(column, audioEngine->getCurrentTempo(), triggerGlobalSample, posInfo);
    }

    // Record pattern events at the exact trigger timeline position.
    const double eventBeat = useQuantize ? nextGridPPQ : currentPPQ;
    for (int i = 0; i < 4; ++i)
    {
        auto* pattern = audioEngine->getPattern(i);
        if (pattern && pattern->isRecording())
        {
            DBG("Recording to pattern " << i << ": strip=" << stripIndex << ", col=" << column << ", beat=" << eventBeat);
            pattern->recordEvent(stripIndex, column, true, eventBeat);
        }
    }
    
    updateMonomeLEDs();
}

void StepVstHostAudioProcessor::stopStrip(int stripIndex)
{
    if (auto* strip = audioEngine->getStrip(stripIndex))
    {
        strip->stop(false);
    }
}

void StepVstHostAudioProcessor::setCurrentProgram(int /*index*/)
{
}

const juce::String StepVstHostAudioProcessor::getProgramName(int /*index*/)
{
    return {};
}

void StepVstHostAudioProcessor::changeProgramName(int /*index*/, const juce::String& /*newName*/)
{
}

// Helper method: Update filter LED visualization based on sub-page
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StepVstHostAudioProcessor();
}

void StepVstHostAudioProcessor::timerCallback()
{
    applyCompletedPresetSaves();
    processPendingSubPresetApply();
    applyPendingFavoritesBeatSpaceJump();
    updateBeatSpaceMorph();
    if (pendingKitScaleSettingsApply.exchange(0, std::memory_order_acq_rel) != 0)
        applyGlobalKitScaleSettings();

    if (pendingBeatSpaceStartupApply.load(std::memory_order_acquire) != 0)
    {
        if (auto* instance = hostRack.getInstance(); instance != nullptr
            && beatSpaceDecoderReady
            && !beatSpaceTable.empty())
        {
            if (refreshBeatSpaceParameterMap())
            {
                int mappedReadyCount = 0;
                for (int channel = 0; channel < BeatSpaceChannels; ++channel)
                {
                    if (beatSpaceChannelMappingReady[static_cast<size_t>(channel)])
                        ++mappedReadyCount;
                }

                const bool likelyMicrotonic = isLikelyMicrotonicPlugin(instance->getName());
                const bool startupMappingReady = likelyMicrotonic
                    ? (mappedReadyCount == BeatSpaceChannels && beatSpaceMappingReady)
                    : (mappedReadyCount > 0);

                if (startupMappingReady)
                {
                    // Fast-path for normal relaunches: persisted BeatSpace channel
                    // points already exist, so skip expensive category/preset
                    // re-derivation on the startup timer tick.
                    if (beatSpacePersistentLayoutLoaded)
                    {
                        if (beatSpaceLinkAllChannels)
                            forceBeatSpaceLinkRefreshIfEnabled();
                        else
                            applyBeatSpacePointToChannels(true, false);

                        pendingBeatSpaceStartupFinalize.store(1, std::memory_order_release);
                        pendingBeatSpaceStartupFinalizeMs = juce::Time::currentTimeMillis() + 300;
                        pendingBeatSpaceStartupFinalizeRemaining = 4;
                        pendingBeatSpaceStartupApply.store(0, std::memory_order_release);
                    }
                    else
                    {
                        applyBeatSpaceDefaultChannelLayout();

                        if (!beatSpaceCategoryAnchorsReady)
                            rebuildBeatSpaceCategoryAnchors();

                    // Use a deterministic alternate startup preset batch for fresh layouts
                    // so initial channel sounds are clearly varied across categories.
                    static constexpr std::array<int, BeatSpaceChannels> kDefaultStartupDisplayBatch {
                        2, 4, 6, 8, 10, 12
                    };

                    const int previousSelectedChannel = beatSpaceSelectedChannel;
                    bool anyApplied = false;

                    for (int channel = 0; channel < BeatSpaceChannels; ++channel)
                    {
                        const auto idx = static_cast<size_t>(channel);
                        const int assignedSpace = getBeatSpaceChannelSpaceAssignment(channel);
                        const auto spaceIdx = static_cast<size_t>(assignedSpace);

                        if (!beatSpaceCategoryPresetPointsReady[spaceIdx])
                            rebuildBeatSpaceCategoryPresetPoints();

                        auto visibleSlots = getBeatSpaceVisiblePresetSlotsForSpace(assignedSpace);
                        if (visibleSlots.empty())
                        {
                            normalizeBeatSpacePresetLayoutForSpace(assignedSpace);
                            visibleSlots = getBeatSpaceVisiblePresetSlotsForSpace(assignedSpace);
                        }

                        int chosenDisplayIndex = beatSpaceLastLoadedDisplayPresetIndex[idx];
                        int chosenSlot = 0;
                        if (!visibleSlots.empty())
                        {
                            if (!beatSpacePersistentLayoutLoaded && chosenDisplayIndex < 0)
                            {
                                const int visibleCount = static_cast<int>(visibleSlots.size());
                                const int preferredDisplay = kDefaultStartupDisplayBatch[spaceIdx];
                                chosenDisplayIndex = juce::jlimit(
                                    0,
                                    visibleCount - 1,
                                    preferredDisplay % juce::jmax(1, visibleCount));
                            }

                            if (chosenDisplayIndex < 0 || chosenDisplayIndex >= static_cast<int>(visibleSlots.size()))
                            {
                                const auto currentPoint = constrainBeatSpacePointForChannel(
                                    channel,
                                    beatSpaceChannelPoints[idx]);
                                float bestDistance = std::numeric_limits<float>::max();
                                int bestDisplay = 0;
                                for (int displayIndex = 0; displayIndex < static_cast<int>(visibleSlots.size()); ++displayIndex)
                                {
                                    const int slot = juce::jlimit(
                                        0,
                                        BeatSpacePresetSlotsPerSpace - 1,
                                        visibleSlots[static_cast<size_t>(displayIndex)]);
                                    auto point = beatSpaceCategoryPresetPoints[spaceIdx][static_cast<size_t>(slot)];
                                    if (!beatSpaceCategoryPresetPointsReady[spaceIdx])
                                    {
                                        point = beatSpaceCategoryAnchorsReady
                                            ? beatSpaceCategoryAnchors[spaceIdx]
                                            : juce::Point<int> { BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 };
                                    }
                                    const float dx = static_cast<float>(point.x - currentPoint.x);
                                    const float dy = static_cast<float>(point.y - currentPoint.y);
                                    const float distance = (dx * dx) + (dy * dy);
                                    if (distance < bestDistance)
                                    {
                                        bestDistance = distance;
                                        bestDisplay = displayIndex;
                                    }
                                }
                                chosenDisplayIndex = bestDisplay;
                            }

                            chosenDisplayIndex = juce::jlimit(
                                0,
                                static_cast<int>(visibleSlots.size()) - 1,
                                chosenDisplayIndex);
                            chosenSlot = juce::jlimit(
                                0,
                                BeatSpacePresetSlotsPerSpace - 1,
                                visibleSlots[static_cast<size_t>(chosenDisplayIndex)]);
                        }

                        auto targetPoint = beatSpaceCategoryPresetPoints[spaceIdx][static_cast<size_t>(chosenSlot)];
                        if (!beatSpaceCategoryPresetPointsReady[spaceIdx])
                        {
                            targetPoint = beatSpaceCategoryAnchorsReady
                                ? beatSpaceCategoryAnchors[spaceIdx]
                                : juce::Point<int> { BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 };
                        }

                        beatSpaceChannelPoints[idx] = constrainBeatSpacePointForChannel(channel, targetPoint);
                        beatSpaceLastLoadedDisplayPresetIndex[idx] = chosenDisplayIndex;
                        anyApplied = true;
                    }

                    beatSpaceSelectChannel(previousSelectedChannel);
                    if (anyApplied)
                    {
                        if (beatSpaceLinkAllChannels)
                            forceBeatSpaceLinkRefreshIfEnabled();
                        else
                            applyBeatSpacePointToChannels(true, false);
                        savePersistentControlPages();

                        pendingBeatSpaceStartupFinalize.store(1, std::memory_order_release);
                        pendingBeatSpaceStartupFinalizeMs = juce::Time::currentTimeMillis() + 300;
                        pendingBeatSpaceStartupFinalizeRemaining = 4;
                    }
                        pendingBeatSpaceStartupApply.store(0, std::memory_order_release);
                    }
                }
                else if (likelyMicrotonic)
                {
                    beatSpaceStatusMessage = "BeatSpace: waiting for full Microtonic mapping ("
                        + juce::String(mappedReadyCount) + "/" + juce::String(BeatSpaceChannels) + ")";
                }
            }
        }
    }

    if (pendingBeatSpaceStartupFinalize.load(std::memory_order_acquire) != 0)
    {
        const auto nowMs = juce::Time::currentTimeMillis();
        if (nowMs >= pendingBeatSpaceStartupFinalizeMs)
        {
            bool finalizeApplied = false;
            if (auto* instance = hostRack.getInstance(); instance != nullptr
                && beatSpaceDecoderReady
                && !beatSpaceTable.empty()
                && refreshBeatSpaceParameterMap())
            {
                int mappedReadyCount = 0;
                for (int channel = 0; channel < BeatSpaceChannels; ++channel)
                {
                    if (beatSpaceChannelMappingReady[static_cast<size_t>(channel)])
                        ++mappedReadyCount;
                }

                const bool likelyMicrotonic = isLikelyMicrotonicPlugin(instance->getName());
                const bool mappingReady = likelyMicrotonic
                    ? (mappedReadyCount == BeatSpaceChannels && beatSpaceMappingReady)
                    : (mappedReadyCount > 0);

                if (mappingReady)
                {
                    if (beatSpaceLinkAllChannels)
                        forceBeatSpaceLinkRefreshIfEnabled();
                    else
                        applyBeatSpacePointToChannels(true, false);
                    finalizeApplied = true;
                }
            }

            if (finalizeApplied || pendingBeatSpaceStartupFinalizeRemaining <= 1)
            {
                pendingBeatSpaceStartupFinalizeRemaining = 0;
                pendingBeatSpaceStartupFinalize.store(0, std::memory_order_release);
            }
            else
            {
                --pendingBeatSpaceStartupFinalizeRemaining;
                pendingBeatSpaceStartupFinalizeMs = nowMs + 320;
            }
        }
    }

    if (persistentGlobalControlsDirty.load(std::memory_order_acquire) != 0)
    {
        const auto nowMs = juce::Time::currentTimeMillis();
        if (lastPersistentGlobalControlsSaveMs == 0
            || (nowMs - lastPersistentGlobalControlsSaveMs) >= kPersistentGlobalControlsSaveDebounceMs)
        {
            savePersistentControlPages();
            persistentGlobalControlsDirty.store(0, std::memory_order_release);
            lastPersistentGlobalControlsSaveMs = nowMs;
        }
    }

    if (pendingPersistentGlobalControlsRestore.load(std::memory_order_acquire) != 0)
    {
        const auto nowMs = juce::Time::currentTimeMillis();
        if (nowMs >= pendingPersistentGlobalControlsRestoreMs)
        {
            loadPersistentGlobalControls();
            persistentGlobalControlsApplied = true;
            if (pendingPersistentGlobalControlsRestoreRemaining > 1)
            {
                --pendingPersistentGlobalControlsRestoreRemaining;
                pendingPersistentGlobalControlsRestoreMs = nowMs + 400;
            }
            else
            {
                pendingPersistentGlobalControlsRestoreRemaining = 0;
                pendingPersistentGlobalControlsRestore.store(0, std::memory_order_release);
            }
        }
    }

    const int pendingPreset = pendingPresetLoadIndex.load(std::memory_order_acquire);
    if (pendingPreset >= 0)
    {
        double hostPpqSnapshot = std::numeric_limits<double>::quiet_NaN();
        double hostTempoSnapshot = std::numeric_limits<double>::quiet_NaN();
        const bool hasHostSync = getHostSyncSnapshot(hostPpqSnapshot, hostTempoSnapshot);
        constexpr uint32_t kPresetLoadFallbackDelayMs = 1200;
        const uint32_t nowMs = juce::Time::getMillisecondCounter();
        const bool allowFallbackLoad = pendingPresetLoadQueuedAtMs != 0
            && static_cast<uint32_t>(nowMs - pendingPresetLoadQueuedAtMs) >= kPresetLoadFallbackDelayMs;
        if (hasHostSync || allowFallbackLoad)
        {
            pendingPresetLoadIndex.store(-1, std::memory_order_release);
            pendingPresetLoadQueuedAtMs = 0;
            performPresetLoad(pendingPreset, hostPpqSnapshot, hostTempoSnapshot);
            loadedPresetIndex = PresetStore::presetExists(pendingPreset) ? pendingPreset : -1;
            // Re-apply BeatSpace once hosted plugin mapping is definitely settled.
            pendingBeatSpaceStartupApply.store(1, std::memory_order_release);
        }
    }

    // Update monome LEDs regularly for smooth playhead
    if (monomeConnection.isConnected() && audioEngine)
    {
        const auto nowMs = juce::Time::currentTimeMillis();
        if (monomeConnection.supportsGrid()
            && (lastGridLedUpdateTimeMs == 0 || (nowMs - lastGridLedUpdateTimeMs) >= kGridRefreshMs))
        {
            updateMonomeLEDs();
            lastGridLedUpdateTimeMs = nowMs;
        }
        if (monomeConnection.supportsArc())
            updateMonomeArcRings();
    }
}

void StepVstHostAudioProcessor::loadAdjacentFile(int stripIndex, int direction)
{
    if (stripIndex < 0 || stripIndex >= MaxStrips)
        return;
    
    auto* strip = audioEngine->getStrip(stripIndex);
    if (!strip) return;
    
    // Get current file for this strip.
    // If strip has no loaded audio, force first-file fallback regardless of any
    // stale path cached in currentStripFiles.
    juce::File currentFile = strip->hasAudio()
        ? currentStripFiles[static_cast<size_t>(stripIndex)]
        : juce::File();

    // Determine folder to browse from strip-specific browser path context.
    juce::File folderToUse = getCurrentBrowserDirectoryForStrip(stripIndex);
    if (!folderToUse.exists() || !folderToUse.isDirectory())
        return;
    
    // Get all audio files in folder
    juce::Array<juce::File> audioFiles;
    for (auto& file : folderToUse.findChildFiles(juce::File::findFiles, false))
    {
        if (isAudioFileSupported(file))
        {
            audioFiles.add(file);
        }
    }

    // If no files at top level, allow browsing into nested pack folders.
    if (audioFiles.size() == 0)
    {
        for (auto& file : folderToUse.findChildFiles(juce::File::findFiles, true))
        {
            if (isAudioFileSupported(file))
                audioFiles.add(file);
        }
    }

    if (audioFiles.size() == 0) return;
    audioFiles.sort();
    
    // Find current file index
    int currentIndex = -1;
    if (currentFile.existsAsFile())
    {
        for (int i = 0; i < audioFiles.size(); ++i)
        {
            if (audioFiles[i] == currentFile)
            {
                currentIndex = i;
                break;
            }
        }
    }

    juce::File fileToLoad;
    if (currentIndex < 0)
    {
        // Requirement: if no sample is currently loaded on this strip,
        // both Prev and Next should load the first file in the selected folder.
        fileToLoad = audioFiles[0];
    }
    else
    {
        // Calculate new index with wraparound
        int newIndex = currentIndex + direction;
        if (newIndex < 0) newIndex = audioFiles.size() - 1;
        if (newIndex >= audioFiles.size()) newIndex = 0;
        fileToLoad = audioFiles[newIndex];
    }
    
    if (!fileToLoad.existsAsFile())
    {
        return;
    }
    
    // Save playback state
    bool wasPlaying = strip->isPlaying();
    const bool isStepMode = (strip->getPlayMode() == EnhancedAudioStrip::PlayMode::Step);
    // Step mode playback is host-clock driven and does not rely on the loop PPQ anchor.
    // Do not block browse-load on missing timeline anchor in this mode.
    const bool requiresTimelineAnchor = wasPlaying && !isStepMode;
    float savedSpeed = strip->getPlaybackSpeed();
    float savedVolume = strip->getVolume();
    float savedPan = strip->getPan();
    int savedGroup = strip->getGroup();
    int savedLoopStart = strip->getLoopStart();
    int savedLoopEnd = strip->getLoopEnd();
    const bool savedTimelineAnchored = strip->isPpqTimelineAnchored();
    const double savedTimelineOffsetBeats = strip->getPpqTimelineOffsetBeats();
    const int savedColumn = strip->getCurrentColumn();

    double hostPpqBeforeLoad = 0.0;
    double hostTempoBeforeLoad = 0.0;
    const int64_t globalSampleBeforeLoad = audioEngine->getGlobalSampleCount();
    if (requiresTimelineAnchor)
    {
        // Strict PPQ safety for file browsing:
        // do not load when hard PPQ resync cannot be guaranteed.
        if (!savedTimelineAnchored || !getHostSyncSnapshot(hostPpqBeforeLoad, hostTempoBeforeLoad))
        {
            DBG("File browse load skipped on strip " << stripIndex
                << ": requires anchored strip + valid host PPQ/BPM.");
            return;
        }
    }
    
    try
    {
        // Load new file
        loadSampleToStrip(stripIndex, fileToLoad);
        
        // Restore parameters
        strip->setPlaybackSpeed(savedSpeed);
        strip->setVolume(savedVolume);
        strip->setPan(savedPan);
        strip->setGroup(savedGroup);
        strip->setLoop(savedLoopStart, savedLoopEnd);
        
        // If browsing while playing, hard-restore PPQ state with deterministic
        // host-time projection based on pre-load PPQ snapshot.
        if (requiresTimelineAnchor)
        {
            const int64_t globalSampleNow = audioEngine->getGlobalSampleCount();
            const int64_t deltaSamples = juce::jmax<int64_t>(0, globalSampleNow - globalSampleBeforeLoad);
            const double samplesPerQuarter = (60.0 / juce::jmax(1.0, hostTempoBeforeLoad)) * juce::jmax(1.0, currentSampleRate);
            const double hostPpqApply = hostPpqBeforeLoad + (static_cast<double>(deltaSamples) / juce::jmax(1.0, samplesPerQuarter));

            strip->restorePresetPpqState(true,
                                         savedTimelineAnchored,
                                         savedTimelineOffsetBeats,
                                         savedColumn,
                                         hostTempoBeforeLoad,
                                         hostPpqApply,
                                         globalSampleNow);
        }
    }
    catch (...)
    {
    }
}

//==============================================================================
// Preset Management
//==============================================================================

void StepVstHostAudioProcessor::resetRuntimePresetStateToDefaults()
{
    if (!audioEngine)
        return;

    pendingPresetLoadIndex.store(-1, std::memory_order_release);

    {
        const juce::ScopedLock lock(pendingLoopChangeLock);
        for (auto& pending : pendingLoopChanges)
            pending = PendingLoopChange{};
    }
    {
        const juce::ScopedLock lock(pendingBarChangeLock);
        for (auto& pending : pendingBarChanges)
            pending = PendingBarChange{};
    }
    pendingBarLengthApply.fill(false);
    momentaryScratchHoldActive = false;
    momentaryStutterHoldActive = false;
    momentaryStutterActiveDivisionButton = -1;
    momentaryStutterButtonMask.store(0, std::memory_order_release);
    momentaryStutterMacroBaselineCaptured = false;
    momentaryStutterMacroCapturePending = false;
    momentaryStutterMacroStartPpq = 0.0;
    momentaryStutterLastComboMask = 0;
    momentaryStutterTwoButtonStepBaseValid = false;
    momentaryStutterTwoButtonStepBase = 0;
    momentaryStutterPlaybackActive.store(0, std::memory_order_release);
    pendingStutterStartActive.store(0, std::memory_order_release);
    pendingStutterStartPpq.store(-1.0, std::memory_order_release);
    pendingStutterStartDivisionBeats.store(1.0, std::memory_order_release);
    pendingStutterStartQuantizeDivision.store(8, std::memory_order_release);
    pendingStutterStartSampleTarget.store(-1, std::memory_order_release);
    for (auto& saved : momentaryStutterSavedState)
        saved = MomentaryStutterSavedStripState{};
    pendingStutterReleaseActive.store(0, std::memory_order_release);
    pendingStutterReleasePpq.store(-1.0, std::memory_order_release);
    pendingStutterReleaseSampleTarget.store(-1, std::memory_order_release);
    audioEngine->setMomentaryStutterRetriggerFadeMs(0.7f);
    audioEngine->clearMomentaryStutterStrips();

    for (int i = 0; i < MaxStrips; ++i)
    {
        currentStripFiles[static_cast<size_t>(i)] = juce::File();

        if (auto* strip = audioEngine->getStrip(i))
        {
            strip->clearSample();
            strip->stop(true);
            strip->setLoop(0, MaxColumns);
            strip->setPlayMode(EnhancedAudioStrip::PlayMode::Step);
            strip->setDirectionMode(EnhancedAudioStrip::DirectionMode::Normal);
            strip->setReverse(false);
            strip->setVolume(1.0f);
            strip->setPan(0.0f);
            strip->setPlaybackSpeed(1.0f);
            strip->setBeatsPerLoop(-1.0f);
            strip->setScratchAmount(0.0f);
            strip->setTransientSliceMode(false);
            strip->setLoopSliceLength(1.0f);
            strip->setResamplePitchEnabled(false);
            strip->setResamplePitchRatio(1.0f);
            strip->setPitchShift(0.0f);
            strip->setRecordingBars(1);
            strip->setFilterFrequency(20000.0f);
            strip->setFilterResonance(0.707f);
            strip->setFilterMorph(0.0f);
            strip->setFilterAlgorithm(EnhancedAudioStrip::FilterAlgorithm::Tpt12);
            strip->setFilterEnabled(false);
            strip->setSwingAmount(0.0f);
            strip->setGateAmount(0.0f);
            strip->setGateSpeed(4.0f);
            strip->setGateEnvelope(0.5f);
            strip->setGateShape(0.5f);
            strip->setStepPatternBars(1);
            strip->setStepPage(0);
            strip->currentStep = 0;
            strip->stepPattern.fill(false);
            strip->stepSubdivisionStartVelocity.fill(1.0f);
            strip->stepSubdivisions.fill(1);
            strip->stepSubdivisionRepeatVelocity.fill(1.0f);
            strip->stepProbability.fill(1.0f);
            strip->setStepEnvelopeAttackMs(0.0f);
            strip->setStepEnvelopeDecayMs(4000.0f);
            strip->setStepEnvelopeReleaseMs(110.0f);
            strip->setGrainSizeMs(1240.0f);
            strip->setGrainDensity(0.05f);
            strip->setGrainPitch(0.0f);
            strip->setGrainPitchJitter(0.0f);
            strip->setGrainSpread(0.0f);
            strip->setGrainJitter(0.0f);
            strip->setGrainPositionJitter(0.0f);
            strip->setGrainRandomDepth(0.0f);
            strip->setGrainArpDepth(0.0f);
            strip->setGrainCloudDepth(0.0f);
            strip->setGrainEmitterDepth(0.0f);
            strip->setGrainEnvelope(0.0f);
            strip->setGrainShape(0.0f);
            strip->setGrainArpMode(0);
            strip->setGrainTempoSyncEnabled(true);
        }

        audioEngine->assignStripToGroup(i, -1);
        for (int slot = 0; slot < ModernAudioEngine::NumModSequencers; ++slot)
        {
            audioEngine->setModSequencerSlot(i, slot);
            audioEngine->setModTarget(i, ModernAudioEngine::ModTarget::None);
            audioEngine->setModBipolar(i, false);
            audioEngine->setModCurveMode(i, false);
            audioEngine->setModDepth(i, 1.0f);
            audioEngine->setModOffset(i, 0);
            audioEngine->setModLengthBars(i, 1);
            audioEngine->setModEditPage(i, 0);
            audioEngine->setModSmoothingMs(i, 0.0f);
            audioEngine->setModCurveBend(i, 0.0f);
            audioEngine->setModCurveShape(i, ModernAudioEngine::ModCurveShape::Linear);
            audioEngine->setModPitchScaleQuantize(i, false);
            audioEngine->setModPitchScale(i, ModernAudioEngine::PitchScale::Chromatic);
            for (int s = 0; s < ModernAudioEngine::ModTotalSteps; ++s)
                audioEngine->setModStepValueAbsolute(i, s, 0.0f);
        }
        audioEngine->setModSequencerSlot(i, 0);

        if (auto* param = parameters.getParameter("stripVolume" + juce::String(i)))
            param->setValueNotifyingHost(param->getDefaultValue());
        if (auto* param = parameters.getParameter("stripPan" + juce::String(i)))
            param->setValueNotifyingHost(param->getDefaultValue());
        if (auto* param = parameters.getParameter("stripSpeed" + juce::String(i)))
            param->setValueNotifyingHost(param->getDefaultValue());
        if (auto* param = parameters.getParameter("stripPitch" + juce::String(i)))
            param->setValueNotifyingHost(param->getDefaultValue());
        if (auto* param = parameters.getParameter("stripStepAttack" + juce::String(i)))
            param->setValueNotifyingHost(param->getDefaultValue());
        if (auto* param = parameters.getParameter("stripStepDecay" + juce::String(i)))
            param->setValueNotifyingHost(param->getDefaultValue());
        if (auto* param = parameters.getParameter("stripStepRelease" + juce::String(i)))
            param->setValueNotifyingHost(param->getDefaultValue());
        if (auto* param = parameters.getParameter("stripSliceLength" + juce::String(i)))
            param->setValueNotifyingHost(param->getDefaultValue());
    }

    for (int i = 0; i < ModernAudioEngine::MaxGroups; ++i)
    {
        if (auto* group = audioEngine->getGroup(i))
        {
            group->setVolume(1.0f);
            group->setMuted(false);
        }
    }

    for (int i = 0; i < ModernAudioEngine::MaxPatterns; ++i)
        audioEngine->clearPattern(i);
}

bool StepVstHostAudioProcessor::getHostSyncSnapshot(double& outPpq, double& outTempo) const
{
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (position->getPpqPosition().hasValue()
                && position->getBpm().hasValue()
                && std::isfinite(*position->getPpqPosition())
                && std::isfinite(*position->getBpm())
                && *position->getBpm() > 0.0)
            {
                outPpq = *position->getPpqPosition();
                outTempo = *position->getBpm();
                return true;
            }
        }
    }

    return false;
}

void StepVstHostAudioProcessor::performPresetLoad(int presetIndex,
                                                  double hostPpqSnapshot,
                                                  double hostTempoSnapshot,
                                                  bool resetRuntimeState)
{
    struct ScopedSuspendProcessing
    {
        explicit ScopedSuspendProcessing(StepVstHostAudioProcessor& p) : processor(p) { processor.suspendProcessing(true); }
        ~ScopedSuspendProcessing() { processor.suspendProcessing(false); }
        StepVstHostAudioProcessor& processor;
    };
    std::unique_ptr<ScopedSuspendProcessing> scopedSuspend;
    if (resetRuntimeState)
        scopedSuspend = std::make_unique<ScopedSuspendProcessing>(*this);

    if (resetRuntimeState)
    {
        // Always reset to a known clean runtime state before applying preset data.
        // This guarantees no strip audio/params leak across full preset transitions.
        resetRuntimePresetStateToDefaults();
        loadedPresetIndex = -1;
    }

    if (!PresetStore::presetExists(presetIndex))
    {
        // Empty full-preset recall keeps freshly reset defaults. For sub-preset
        // recalls we keep current playback/state untouched when the slot is empty.
        if (resetRuntimeState)
            presetRefreshToken.fetch_add(1, std::memory_order_acq_rel);
        return;
    }

    if (resetRuntimeState)
    {
        // Full preset loads rebuild file-backed strip state from disk.
        for (auto& f : currentStripFiles)
            f = juce::File();
    }

    const bool loadSucceeded = PresetStore::loadPreset(
        presetIndex,
        MaxStrips,
        audioEngine.get(),
        parameters,
        [this](int stripIndex, const juce::File& sampleFile)
        {
            return loadSampleToStrip(stripIndex, sampleFile);
        },
        hostPpqSnapshot,
        hostTempoSnapshot,
        !resetRuntimeState,
        [this, hostPpqSnapshot, hostTempoSnapshot](const juce::XmlElement& presetXml)
        {
            loadBeatSpaceStateFromPresetXml(
                presetXml,
                hostPpqSnapshot,
                hostTempoSnapshot,
                juce::Time::getMillisecondCounterHiRes());
        });

    if (loadSucceeded && PresetStore::presetExists(presetIndex))
    {
        loadedPresetIndex = presetIndex;
        // BeatSpace category/channel points remain the authoritative source.
        // Re-apply after any preset/sub-preset load so hosted plugin params
        // cannot drift to stale Microtonic values.
        if (beatSpaceLinkAllChannels)
            forceBeatSpaceLinkRefreshIfEnabled();
        else
            applyBeatSpacePointToChannels(true, false);

        // After preset/sub-preset apply, force unity-speed strips to hard-lock
        // traversal phase back to host PPQ on the next audio block.
        for (int i = 0; i < MaxStrips; ++i)
        {
            if (auto* strip = audioEngine->getStrip(i))
                strip->requestPlayheadUnityPhaseResync();
        }

        syncRuntimeStateFromParameters();
        requestImmediateUiAndMonomeRefresh();
    }
    if (resetRuntimeState)
        presetRefreshToken.fetch_add(1, std::memory_order_acq_rel);
}

bool StepVstHostAudioProcessor::runPresetSaveRequest(const PresetSaveRequest& request)
{
    if (!audioEngine || request.presetIndex < 0 || request.presetIndex >= MaxPresetSlots)
        return false;

    try
    {
        struct ScopedSuspendProcessing
        {
            explicit ScopedSuspendProcessing(StepVstHostAudioProcessor& p) : processor(p) { processor.suspendProcessing(true); }
            ~ScopedSuspendProcessing() { processor.suspendProcessing(false); }
            StepVstHostAudioProcessor& processor;
        } scopedSuspend(*this);

        return PresetStore::savePreset(request.presetIndex,
                                       MaxStrips,
                                       audioEngine.get(),
                                       parameters,
                                       request.stripFiles.data());
    }
    catch (const std::exception& e)
    {
        DBG("async savePreset exception for slot " << request.presetIndex << ": " << e.what());
        return false;
    }
    catch (...)
    {
        DBG("async savePreset exception for slot " << request.presetIndex << ": unknown");
        return false;
    }
}

void StepVstHostAudioProcessor::pushPresetSaveResult(const PresetSaveResult& result)
{
    {
        const juce::ScopedLock lock(presetSaveResultLock);
        presetSaveResults.push_back(result);
    }
    presetSaveJobsInFlight.fetch_sub(1, std::memory_order_acq_rel);
}

void StepVstHostAudioProcessor::applyCompletedPresetSaves()
{
    std::vector<PresetSaveResult> completed;
    {
        const juce::ScopedLock lock(presetSaveResultLock);
        if (presetSaveResults.empty())
            return;
        completed.swap(presetSaveResults);
    }

    uint32_t successfulSaves = 0;
    for (const auto& result : completed)
    {
        if (!result.success)
        {
            DBG("Preset save failed for slot " << result.presetIndex);
            continue;
        }

        loadedPresetIndex = result.presetIndex;
        ++successfulSaves;
    }

    if (successfulSaves > 0)
        presetRefreshToken.fetch_add(successfulSaves, std::memory_order_acq_rel);
}

int StepVstHostAudioProcessor::getActiveMainPresetIndexForSubPresets() const
{
    if (loadedPresetIndex >= 0 && loadedPresetIndex < MaxPresetSlots)
        return loadedPresetIndex;
    return juce::jlimit(0, MaxPresetSlots - 1, activeMainPresetIndex);
}

int StepVstHostAudioProcessor::getSubPresetStoragePresetIndex(int mainPresetIndex, int subPresetSlot) const
{
    const int clampedMain = juce::jlimit(0, MaxPresetSlots - 1, mainPresetIndex);
    const int clampedSub = juce::jlimit(0, SubPresetSlots - 1, subPresetSlot);
    return MaxPresetSlots + (clampedMain * SubPresetSlots) + clampedSub;
}

bool StepVstHostAudioProcessor::saveSubPresetForMainPreset(int mainPresetIndex, int subPresetSlot)
{
    if (!audioEngine)
        return false;

    const int storageIndex = getSubPresetStoragePresetIndex(mainPresetIndex, subPresetSlot);
    std::array<juce::File, MaxStrips> stripFiles{};
    for (int i = 0; i < MaxStrips; ++i)
        stripFiles[static_cast<size_t>(i)] = currentStripFiles[static_cast<size_t>(i)];
    double hostPpqSnapshot = audioEngine->getTimelineBeat();
    double hostTempoSnapshot = juce::jmax(1.0, audioEngine->getCurrentTempo());
    (void) getHostSyncSnapshot(hostPpqSnapshot, hostTempoSnapshot);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    return PresetStore::savePreset(storageIndex,
                                   MaxStrips,
                                   audioEngine.get(),
                                   parameters,
                                   stripFiles.data(),
                                   [this, hostPpqSnapshot, hostTempoSnapshot, nowMs](juce::XmlElement& presetXml)
                                   {
                                       appendBeatSpaceStateToPresetXml(
                                           presetXml,
                                           hostPpqSnapshot,
                                           hostTempoSnapshot,
                                           nowMs);
                                   });
}

void StepVstHostAudioProcessor::ensureSubPresetsInitializedForMainPreset(int mainPresetIndex)
{
    // Intentionally do not auto-create sub-presets.
    // Empty sub-slots must remain empty so recalling them resets to the
    // flat/default sequencer state until the user explicitly saves a slot.
    activeMainPresetIndex = juce::jlimit(0, MaxPresetSlots - 1, mainPresetIndex);
}

void StepVstHostAudioProcessor::requestSubPresetRecallQuantized(
    int mainPresetIndex, int subPresetSlot, bool sequenceDriven)
{
    if (!audioEngine)
        return;

    const int clampedMain = juce::jlimit(0, MaxPresetSlots - 1, mainPresetIndex);
    const int clampedSlot = juce::jlimit(0, SubPresetSlots - 1, subPresetSlot);
    activeMainPresetIndex = clampedMain;

    pendingSubPresetRecall.active = true;
    pendingSubPresetRecall.sequenceDriven = sequenceDriven;
    pendingSubPresetRecall.targetResolved = false;
    pendingSubPresetRecall.mainPresetIndex = clampedMain;
    pendingSubPresetRecall.subPresetSlot = clampedSlot;
    pendingSubPresetRecall.targetPpq = 0.0;
    pendingSubPresetRecall.intervalBeats = 4.0;

    if (!isTimerRunning())
        startTimer(kGridRefreshMs);
}

double StepVstHostAudioProcessor::getSubPresetRecallIntervalBeats() const
{
    const int quantizeDivision = juce::jmax(1, getQuantizeDivision());
    const double intervalBeats = 4.0 / static_cast<double>(quantizeDivision);
    return juce::jlimit(1.0 / 64.0, 256.0, intervalBeats);
}

void StepVstHostAudioProcessor::updateSubPresetQuantizedRecall(
    const juce::AudioPlayHead::PositionInfo& posInfo, int numSamples)
{
    if (!pendingSubPresetRecall.active)
        return;

    if (pendingSubPresetRecall.sequenceDriven
        && (!subPresetSequenceActive || subPresetSequenceSlots.size() < 2))
    {
        pendingSubPresetRecall.active = false;
        pendingSubPresetRecall.targetResolved = false;
        return;
    }

    if (pendingSubPresetApplySlot.load(std::memory_order_acquire) >= 0)
        return;

    const auto ppqOpt = posInfo.getPpqPosition();
    const auto bpmOpt = posInfo.getBpm();
    if (!ppqOpt.hasValue() || !bpmOpt.hasValue()
        || !std::isfinite(*ppqOpt) || !std::isfinite(*bpmOpt)
        || *bpmOpt <= 0.0 || currentSampleRate <= 1.0)
    {
        // No host PPQ/BPM means we cannot guarantee a phase-safe switch.
        // Keep request pending until clock info is valid again.
        return;
    }

    const double currentPpq = *ppqOpt;
    const double intervalBeatsNow = getSubPresetRecallIntervalBeats();
    if (pendingSubPresetRecall.targetResolved
        && std::abs(intervalBeatsNow - pendingSubPresetRecall.intervalBeats) > 1.0e-9)
    {
        // Global quantize changed while a switch was pending: re-target to the
        // new grid so sub-preset switching follows current quantize settings.
        pendingSubPresetRecall.targetResolved = false;
        pendingSubPresetRecall.targetPpq = 0.0;
    }

    if (!pendingSubPresetRecall.targetResolved)
    {
        const double intervalBeats = intervalBeatsNow;
        pendingSubPresetRecall.intervalBeats = intervalBeats;

        double nextBoundary = std::ceil(currentPpq / intervalBeats) * intervalBeats;
        if (nextBoundary <= currentPpq + 1.0e-9)
            nextBoundary += intervalBeats;
        nextBoundary = std::round(nextBoundary / intervalBeats) * intervalBeats;
        pendingSubPresetRecall.targetPpq = nextBoundary;
        pendingSubPresetRecall.targetResolved = true;
    }

    const double ppqPerSecond = *bpmOpt / 60.0;
    const double ppqPerSample = ppqPerSecond / currentSampleRate;
    const double blockEndPpq = currentPpq + (ppqPerSample * static_cast<double>(juce::jmax(1, numSamples)));
    if (blockEndPpq + 1.0e-9 < pendingSubPresetRecall.targetPpq)
        return;

    const double targetPpq = pendingSubPresetRecall.targetPpq;
    const double samplesToTarget = (targetPpq - currentPpq) / juce::jmax(1.0e-12, ppqPerSample);
    const int sampleOffset = juce::jlimit(
        0,
        juce::jmax(0, numSamples - 1),
        static_cast<int>(std::llround(samplesToTarget)));
    int64_t targetGlobalSample = -1;
    if (audioEngine)
        targetGlobalSample = audioEngine->getGlobalSampleCount() + static_cast<int64_t>(sampleOffset);

    pendingSubPresetApplyTargetPpq.store(targetPpq, std::memory_order_release);
    pendingSubPresetApplyTargetTempo.store(*bpmOpt, std::memory_order_release);
    pendingSubPresetApplyTargetSample.store(targetGlobalSample, std::memory_order_release);
    pendingSubPresetApplyMainPreset.store(
        pendingSubPresetRecall.mainPresetIndex, std::memory_order_release);
    pendingSubPresetApplySlot.store(
        pendingSubPresetRecall.subPresetSlot, std::memory_order_release);

    if (pendingSubPresetRecall.sequenceDriven
        && subPresetSequenceActive
        && subPresetSequenceSlots.size() >= 2)
    {
        int nextSlot = subPresetSequenceSlots.front();
        for (size_t i = 0; i < subPresetSequenceSlots.size(); ++i)
        {
            if (subPresetSequenceSlots[i] != pendingSubPresetRecall.subPresetSlot)
                continue;
            const size_t nextIndex = (i + 1u) % subPresetSequenceSlots.size();
            nextSlot = subPresetSequenceSlots[nextIndex];
            break;
        }

        pendingSubPresetRecall.subPresetSlot = juce::jlimit(0, SubPresetSlots - 1, nextSlot);
        pendingSubPresetRecall.targetPpq = 0.0;
        pendingSubPresetRecall.targetResolved = false;
    }
    else
    {
        pendingSubPresetRecall.active = false;
        pendingSubPresetRecall.targetResolved = false;
    }
}

void StepVstHostAudioProcessor::processPendingSubPresetApply()
{
    const int queuedSlot = pendingSubPresetApplySlot.exchange(-1, std::memory_order_acq_rel);
    if (queuedSlot < 0)
        return;

    const int queuedMain =
        pendingSubPresetApplyMainPreset.exchange(-1, std::memory_order_acq_rel);
    const double queuedTargetPpq =
        pendingSubPresetApplyTargetPpq.exchange(-1.0, std::memory_order_acq_rel);
    const double queuedTargetTempo =
        pendingSubPresetApplyTargetTempo.exchange(120.0, std::memory_order_acq_rel);
    const int64_t queuedTargetSample =
        pendingSubPresetApplyTargetSample.exchange(-1, std::memory_order_acq_rel);
    const int clampedMain = juce::jlimit(0, MaxPresetSlots - 1, queuedMain);
    const int clampedSlot = juce::jlimit(0, SubPresetSlots - 1, queuedSlot);
    const int storageIndex = getSubPresetStoragePresetIndex(clampedMain, clampedSlot);

    const bool queuedTimingValid = std::isfinite(queuedTargetPpq)
        && std::isfinite(queuedTargetTempo)
        && queuedTargetPpq >= 0.0
        && queuedTargetTempo > 0.0;

    const int64_t currentGlobalSample = audioEngine ? audioEngine->getGlobalSampleCount() : -1;
    if (queuedTargetSample >= 0
        && currentGlobalSample >= 0
        && currentGlobalSample + 1 < queuedTargetSample)
    {
        // Keep the request queued until the scheduled sample is reached.
        pendingSubPresetApplyMainPreset.store(clampedMain, std::memory_order_release);
        pendingSubPresetApplySlot.store(clampedSlot, std::memory_order_release);
        pendingSubPresetApplyTargetPpq.store(queuedTargetPpq, std::memory_order_release);
        pendingSubPresetApplyTargetTempo.store(queuedTargetTempo, std::memory_order_release);
        pendingSubPresetApplyTargetSample.store(queuedTargetSample, std::memory_order_release);
        return;
    }

    double hostPpqSnapshot = audioEngine ? audioEngine->getTimelineBeat() : 0.0;
    double hostTempoSnapshot = audioEngine ? juce::jmax(1.0, audioEngine->getCurrentTempo()) : 120.0;
    const bool hasHostSync = getHostSyncSnapshot(hostPpqSnapshot, hostTempoSnapshot)
        && std::isfinite(hostPpqSnapshot)
        && std::isfinite(hostTempoSnapshot)
        && hostTempoSnapshot > 0.0;

    if (!queuedTimingValid && !hasHostSync)
    {
        // Keep pending until valid host sync is available; do not apply off-grid.
        pendingSubPresetApplyMainPreset.store(clampedMain, std::memory_order_release);
        pendingSubPresetApplySlot.store(clampedSlot, std::memory_order_release);
        pendingSubPresetApplyTargetPpq.store(queuedTargetPpq, std::memory_order_release);
        pendingSubPresetApplyTargetTempo.store(queuedTargetTempo, std::memory_order_release);
        pendingSubPresetApplyTargetSample.store(queuedTargetSample, std::memory_order_release);
        return;
    }

    const double loadPpqSnapshot = queuedTimingValid ? queuedTargetPpq : hostPpqSnapshot;
    const double loadTempoSnapshot = queuedTimingValid ? queuedTargetTempo : hostTempoSnapshot;

    if (PresetStore::presetExists(storageIndex))
        performPresetLoad(storageIndex, loadPpqSnapshot, loadTempoSnapshot, false);

    if (audioEngine)
    {
        double anchorPpqNow = queuedTimingValid ? queuedTargetPpq : hostPpqSnapshot;
        double anchorTempoNow = hostTempoSnapshot;
        const bool anchorHostSync = getHostSyncSnapshot(anchorPpqNow, anchorTempoNow)
            && std::isfinite(anchorPpqNow)
            && std::isfinite(anchorTempoNow)
            && anchorTempoNow > 0.0;
        if (queuedTimingValid)
            anchorPpqNow = queuedTargetPpq;
        else if (!anchorHostSync)
            anchorPpqNow = hostPpqSnapshot;

        const int64_t anchorSample = (queuedTargetSample >= 0)
            ? queuedTargetSample
            : currentGlobalSample;
        for (int i = 0; i < MaxStrips; ++i)
        {
            if (auto* strip = audioEngine->getStrip(i))
                strip->realignToPpqAnchor(anchorPpqNow, anchorSample);
        }
    }

    loadedPresetIndex = clampedMain;
    activeMainPresetIndex = clampedMain;
    activeSubPresetSlot = clampedSlot;
    presetRefreshToken.fetch_add(1, std::memory_order_acq_rel);
}

void StepVstHostAudioProcessor::savePreset(int presetIndex)
{
    if (!audioEngine || presetIndex < 0 || presetIndex >= MaxPresetSlots)
        return;

    activeMainPresetIndex = presetIndex;

    if (!isTimerRunning())
        startTimer(kGridRefreshMs);

    PresetSaveRequest request;
    request.presetIndex = presetIndex;
    for (int i = 0; i < MaxStrips; ++i)
        request.stripFiles[static_cast<size_t>(i)] = currentStripFiles[static_cast<size_t>(i)];

    auto* job = new PresetSaveJob(*this, std::move(request));
    presetSaveJobsInFlight.fetch_add(1, std::memory_order_acq_rel);
    presetSaveThreadPool.addJob(job, true);

    // Keep UI/LED state responsive immediately; completion still updates token.
    loadedPresetIndex = presetIndex;
    presetRefreshToken.fetch_add(1, std::memory_order_acq_rel);
}

void StepVstHostAudioProcessor::loadPreset(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= MaxPresetSlots)
        return;

    try
    {
        activeMainPresetIndex = presetIndex;
        double hostPpqSnapshot = std::numeric_limits<double>::quiet_NaN();
        double hostTempoSnapshot = std::numeric_limits<double>::quiet_NaN();
        const bool hasHostSync = getHostSyncSnapshot(hostPpqSnapshot, hostTempoSnapshot);
        if (!hasHostSync)
        {
            DBG("Preset " << (presetIndex + 1)
                << " loaded without host PPQ/BPM snapshot; recalling audio/parameters only.");
        }

        pendingPresetLoadIndex.store(-1, std::memory_order_release);
        pendingPresetLoadQueuedAtMs = 0;
        performPresetLoad(presetIndex, hostPpqSnapshot, hostTempoSnapshot);
        loadedPresetIndex = PresetStore::presetExists(presetIndex) ? presetIndex : -1;
        ensureSubPresetsInitializedForMainPreset(presetIndex);
        activeSubPresetSlot = juce::jlimit(0, SubPresetSlots - 1, activeSubPresetSlot);
    }
    catch (const std::exception& e)
    {
        DBG("loadPreset exception for slot " << presetIndex << ": " << e.what());
    }
    catch (...)
    {
        DBG("loadPreset exception for slot " << presetIndex << ": unknown");
    }
}

bool StepVstHostAudioProcessor::deletePreset(int presetIndex)
{
    try
    {
        const bool deleted = PresetStore::deletePreset(presetIndex);
        if (deleted)
        {
            struct ScopedSuspendProcessing
            {
                explicit ScopedSuspendProcessing(StepVstHostAudioProcessor& p) : processor(p) { processor.suspendProcessing(true); }
                ~ScopedSuspendProcessing() { processor.suspendProcessing(false); }
                StepVstHostAudioProcessor& processor;
            } scopedSuspend(*this);

            // Deleting any preset slot should leave runtime in a clean state.
            resetRuntimePresetStateToDefaults();
            loadedPresetIndex = -1;
            updateMonomeLEDs();
        }
        if (deleted)
            presetRefreshToken.fetch_add(1, std::memory_order_acq_rel);
        return deleted;
    }
    catch (...)
    {
        return false;
    }
}

juce::String StepVstHostAudioProcessor::getPresetName(int presetIndex) const
{
    return PresetStore::getPresetName(presetIndex);
}

bool StepVstHostAudioProcessor::setPresetName(int presetIndex, const juce::String& name)
{
    try
    {
        const bool ok = PresetStore::setPresetName(presetIndex, name);
        if (ok)
            presetRefreshToken.fetch_add(1, std::memory_order_acq_rel);
        return ok;
    }
    catch (...)
    {
        return false;
    }
}

bool StepVstHostAudioProcessor::presetExists(int presetIndex) const
{
    try
    {
        return PresetStore::presetExists(presetIndex);
    }
    catch (...)
    {
        return false;
    }
}

//==============================================================================
// AudioProcessor Virtual Functions
//==============================================================================

const juce::String StepVstHostAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool StepVstHostAudioProcessor::acceptsMidi() const
{
    return false;
}

bool StepVstHostAudioProcessor::producesMidi() const
{
    return false;
}

bool StepVstHostAudioProcessor::isMidiEffect() const
{
    return false;
}

double StepVstHostAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int StepVstHostAudioProcessor::getNumPrograms()
{
    return 1;
}

int StepVstHostAudioProcessor::getCurrentProgram()
{
    return 0;
}
