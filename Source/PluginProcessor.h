/*
  ==============================================================================

    PluginProcessor.h
    step-vsthost - Modern Edition
    
    Complete modernization for JUCE 8.x with advanced audio engine

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_osc/juce_osc.h>
#include <cstdint>
#include "AudioEngine.h"
#include "HostedInstrumentRack.h"

//==============================================================================
/**
 * MonomeConnection - Handles serialosc protocol communication
 * 
 * Complete serialosc protocol implementation:
 * - Device discovery and enumeration
 * - Automatic connection and reconnection
 * - All LED control methods (set, all, row, col, map, level row/col/map)
 * - Grid key input
 * - System configuration (prefix, rotation, size, info)
 * - Device hot-plug support
 */
class MonomeConnection : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>,
                          public juce::Timer
{
public:
    MonomeConnection();
    ~MonomeConnection() override;
    
    void connect(int applicationPort);
    void disconnect();
    bool isConnected() const { return connected; }
    
    // SerialOSC device discovery
    void discoverDevices();
    void selectDevice(int deviceIndex);
    void refreshDeviceList();
    
    // LED control - Basic (0/1 states)
    void setLED(int x, int y, int state);
    void setAllLEDs(int state);
    void setLEDRow(int xOffset, int y, const std::array<int, 8>& data);
    void setLEDColumn(int x, int yOffset, const std::array<int, 8>& data);
    void setLEDMap(int xOffset, int yOffset, const std::array<int, 8>& data);
    
    // LED control - Variable brightness (0-15)
    void setLEDLevel(int x, int y, int level);
    void setAllLEDLevels(int level);
    void setLEDLevelRow(int xOffset, int y, const std::array<int, 8>& levels);
    void setLEDLevelColumn(int x, int yOffset, const std::array<int, 8>& levels);
    void setLEDLevelMap(int xOffset, int yOffset, const std::array<int, 64>& levels);
    void setArcRingMap(int encoder, const std::array<int, 64>& levels);
    void setArcRingLevel(int encoder, int ledIndex, int level);
    void setArcRingRange(int encoder, int start, int end, int level);

    bool supportsGrid() const;
    bool supportsArc() const;
    int getArcEncoderCount() const;
    
    // System commands
    void setRotation(int degrees); // 0, 90, 180, 270
    void setPrefix(const juce::String& newPrefix);
    void requestInfo();
    void requestSize();
    
    // Tilt support (for grids with tilt sensors)
    void enableTilt(int sensor, bool enable);
    
    // Device info
    struct DeviceInfo
    {
        juce::String id;
        juce::String type;
        int port;
        int sizeX = 16;
        int sizeY = 8;
        bool hasTilt = false;
        juce::String host = "127.0.0.1";
    };
    
    std::vector<DeviceInfo> getDiscoveredDevices() const { return devices; }
    DeviceInfo getCurrentDevice() const { return currentDevice; }
    juce::String getConnectionStatus() const;
    
    // Callbacks
    std::function<void(int x, int y, int state)> onKeyPress;
    std::function<void(int sensor, int x, int y, int z)> onTilt;
    std::function<void(int encoder, int delta)> onArcDelta;
    std::function<void(int encoder, int state)> onArcKey;
    std::function<void()> onDeviceConnected;
    std::function<void()> onDeviceDisconnected;
    std::function<void(const std::vector<DeviceInfo>&)> onDeviceListUpdated;
    
private:
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void handleSerialOSCMessage(const juce::OSCMessage& message);
    void handleGridMessage(const juce::OSCMessage& message);
    void handleSystemMessage(const juce::OSCMessage& message);
    void handleTiltMessage(const juce::OSCMessage& message);
    void handleArcMessage(const juce::OSCMessage& message);
    
    void timerCallback() override;
    void attemptReconnection();
    void sendPing();
    void markDisconnected();
    void configureCurrentDevice();
    
    juce::OSCSender oscSender;
    juce::OSCSender serialoscSender; // Separate sender for serialosc queries
    juce::OSCReceiver oscReceiver;
    
    std::vector<DeviceInfo> devices;
    DeviceInfo currentDevice;
    
    juce::String oscPrefix = "/monome";
    int applicationPort = 8000;
    bool connected = false;
    bool autoReconnect = true;
    
    int reconnectAttempts = 0;
    static constexpr int maxReconnectAttempts = 10;
    static constexpr int reconnectIntervalMs = 2000;
    static constexpr int discoveryIntervalMs = 2000;
    
    juce::int64 lastMessageTime = 0;
    juce::int64 lastConnectAttemptTime = 0;
    juce::int64 lastPingTime = 0;
    juce::int64 lastDiscoveryTime = 0;
    juce::int64 lastReconnectAttemptTime = 0;
    bool awaitingDeviceResponse = false;
    static constexpr int pingIntervalMs = 5000;
    static constexpr int handshakeTimeoutMs = 3000;
    static constexpr int connectionTimeoutMs = 15000;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonomeConnection)
};

//==============================================================================
/**
 * StepVstHostAudioProcessor - Main plugin processor
 */
class StepVstHostAudioProcessor : public juce::AudioProcessor,
                             public juce::Timer,
                             public juce::AudioProcessorValueTreeState::Listener
{
public:
    StepVstHostAudioProcessor();
    ~StepVstHostAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void markPersistentGlobalUserChange();
    void queuePersistentGlobalControlsSave();

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Public API
    ModernAudioEngine* getAudioEngine() { return audioEngine.get(); }
    MonomeConnection& getMonomeConnection() { return monomeConnection; }
    HostedInstrumentRack& getHostRack() { return hostRack; }
    bool loadHostedInstrument(const juce::File& file, juce::String& error);
    bool setLoadedHostedInstrumentAsDefault(juce::String& error);
    juce::File getLoadedHostedInstrumentFile() const;
    juce::File getDefaultHostedInstrumentFile() const;
    int getLaneMidiChannel(int stripIndex) const;
    int getLaneMidiNote(int stripIndex) const;
    void setLaneMidiChannel(int stripIndex, int channel);
    void setLaneMidiNote(int stripIndex, int note);
    void queueHostedProgramChangeForStrip(int stripIndex, int deltaPrograms);
    
    bool loadSampleToStrip(int stripIndex, const juce::File& file);
    void loadAdjacentFile(int stripIndex, int direction);  // Browse files
    void requestBarLengthChange(int stripIndex, int bars);
    bool canChangeBarLengthNow(int stripIndex) const;
    void setPendingBarLengthApply(int stripIndex, bool pending);
    void triggerStrip(int stripIndex, int column);
    void stopStrip(int stripIndex);
    
    enum class SamplePathMode
    {
        Loop,
        Step
    };
    static constexpr int BrowserFavoriteSlots = 6;
    juce::File getDefaultSampleDirectory(int stripIndex, SamplePathMode mode) const;
    void setDefaultSampleDirectory(int stripIndex, SamplePathMode mode, const juce::File& directory);
    juce::File getCurrentBrowserDirectoryForStrip(int stripIndex) const;
    juce::File getBrowserFavoriteDirectory(int slot) const;
    bool isBrowserFavoritePadHeld(int stripIndex, int slot) const;
    bool isBrowserFavoriteSaveBurstActive(int slot, uint32_t nowMs) const;
    bool isBrowserFavoriteMissingBurstActive(int slot, uint32_t nowMs) const;
    void beginBrowserFavoritePadHold(int stripIndex, int slot);
    void endBrowserFavoritePadHold(int stripIndex, int slot);

    enum class PitchControlMode
    {
        PitchShift = 0,
        Resample = 1
    };
    PitchControlMode getPitchControlMode() const;
    bool isPitchControlResampleMode() const { return getPitchControlMode() == PitchControlMode::Resample; }
    void applyPitchControlToStrip(EnhancedAudioStrip& strip, float semitones);
    float getPitchSemitonesForDisplay(const EnhancedAudioStrip& strip) const;
    
    // Control mode (for GUI to check if level/pan/etc controls are active)
    enum class ControlMode
    {
        Normal,
        Speed,
        Pitch,
        Pan,
        Volume,
        Length,
        Filter,
        Swing,
        Gate,
        FileBrowser,
        GroupAssign,
        Modulation,
        BeatSpace,
        Preset,
        StepEdit
    };
    ControlMode getCurrentControlMode() const { return currentControlMode; }
    bool isControlModeActive() const { return controlModeActive; }
    static juce::String getControlModeName(ControlMode mode);
    static constexpr int NumControlRowPages = 14;
    static constexpr int MaxStrips = ModernAudioEngine::MaxStrips;
    static constexpr int BeatSpaceChannels = 6;
    static constexpr int BeatSpacePresetSlotsPerSpace = 16;
    static constexpr int BeatSpacePathMaxPoints = 32;
    static constexpr int MaxColumns = 16;
    static constexpr int MaxGridWidth = 16;
    static constexpr int MaxGridHeight = 16;
    using ControlPageOrder = std::array<ControlMode, NumControlRowPages>;
    ControlPageOrder getControlPageOrder() const;
    ControlMode getControlModeForControlButton(int buttonIndex) const;
    int getControlButtonForMode(ControlMode mode) const;
    void moveControlPage(int fromIndex, int toIndex);
    bool isControlPageMomentary() const { return controlPageMomentary.load(std::memory_order_acquire); }
    void setControlPageMomentary(bool shouldBeMomentary);
    void setControlModeFromGui(ControlMode mode, bool shouldBeActive);
    void setSwingDivisionSelection(int mode);
    int getSwingDivisionSelection() const { return swingDivisionSelection.load(std::memory_order_acquire); }
    int getLastMonomePressedStripRow() const { return lastMonomePressedStripRow.load(std::memory_order_acquire); }
    bool isStepEditModeActive() const
    {
        return controlModeActive && currentControlMode == ControlMode::StepEdit;
    }
    int getStepEditToolIndex() const
    {
        switch (stepEditTool)
        {
            case StepEditTool::Gate: return 0;
            case StepEditTool::Velocity: return 1;
            case StepEditTool::Divide: return 2;
            case StepEditTool::RampUp: return 3;
            case StepEditTool::RampDown: return 4;
            case StepEditTool::Probability: return 5;
            case StepEditTool::Attack: return 6;
            case StepEditTool::Decay: return 7;
            case StepEditTool::Release: return 8;
            default: return 0;
        }
    }
    int getStepEditSelectedStrip() const { return juce::jlimit(0, MaxStrips - 1, stepEditSelectedStrip); }
    int getMonomeGridWidth() const;
    int getMonomeGridHeight() const;
    int getMonomeControlRow() const;
    int getMonomeActiveStripCount() const;
    struct BeatSpaceVisualState
    {
        bool decoderReady = false;
        bool mappingReady = false;
        bool microtonicExactMapping = false;
        bool colorClustersReady = false;
        bool linkAllChannels = false;
        bool confidenceOverlayEnabled = false;
        bool pathOverlayEnabled = true;
        int mappedChannels = 0;
        int selectedChannel = 0;
        int pathRecordArmedChannel = -1;
        int tableSize = 1000;
        int zoomLevel = 0;
        int viewX = 0;
        int viewY = 0;
        int viewWidth = 1000;
        int viewHeight = 1000;
        float maxRecallError = 0.0f;
        float selectedConfidence = 0.0f;
        double morphDurationMs = 160.0;
        bool anyMorphActive = false;
        juce::String statusMessage;
        std::array<bool, ModernAudioEngine::MaxStrips> channelMapped{};
        std::array<juce::Point<int>, ModernAudioEngine::MaxStrips> channelPoints{};
        std::array<bool, ModernAudioEngine::MaxStrips> channelMorphActive{};
        std::array<float, ModernAudioEngine::MaxStrips> channelMorphProgress{};
        std::array<juce::Point<int>, ModernAudioEngine::MaxStrips> channelMorphFrom{};
        std::array<juce::Point<int>, ModernAudioEngine::MaxStrips> channelMorphTo{};
        std::array<juce::Point<int>, ModernAudioEngine::MaxStrips> channelMorphCurrent{};
        std::array<juce::Point<int>, BeatSpaceChannels> categoryAnchors{};
        std::array<int, BeatSpaceChannels> categoryRadiusX{};
        std::array<int, BeatSpaceChannels> categoryRadiusY{};
        std::array<bool, BeatSpaceChannels> categoryManual{};
        std::array<int, BeatSpaceChannels> channelCategoryAssignment{};
        std::array<float, BeatSpaceChannels> zoneLockStrength{};
        std::array<int, BeatSpaceChannels> bookmarkCounts{};
        std::array<int, BeatSpaceChannels> pathPointCounts{};
        std::array<int, BeatSpaceChannels> pathLoopBars{};
        std::array<bool, BeatSpaceChannels> pathActive{};
        std::array<std::array<juce::Point<int>, BeatSpacePathMaxPoints>, BeatSpaceChannels> pathPoints{};
    };
    enum class BeatSpaceRandomizeMode
    {
        WithinCategory = 0,
        NearCurrent = 1,
        PreserveCharacter = 2,
        FullWild = 3
    };
    enum class BeatSpacePathMode
    {
        QuarterNote = 0,
        OneBar = 1
    };
    BeatSpaceVisualState getBeatSpaceVisualState() const;
    juce::Image getBeatSpaceTablePreviewImage() const;
    juce::Image getBeatSpaceConfidencePreviewImage() const;
    bool isBeatSpaceReady() const;
    void beatSpaceSelectChannel(int channel);
    void beatSpaceSetLinkAllChannels(bool shouldLink);
    void beatSpaceRandomizeSelection();
    bool beatSpaceRandomizeChannel(int channel, BeatSpaceRandomizeMode mode, bool applyLinkedIfSelected = false);
    void setBeatSpaceRandomizeMode(BeatSpaceRandomizeMode mode);
    BeatSpaceRandomizeMode getBeatSpaceRandomizeMode() const;
    void beatSpaceAdjustZoom(int delta);
    void beatSpacePan(int dx, int dy);
    void beatSpaceSetPointFromGridCell(int gridX, int gridY, int gridWidth, int gridHeight);
    void beatSpaceSetChannelPoint(int channel,
                                  const juce::Point<int>& point,
                                  bool moveLinkedGroup,
                                  bool morph);
    void setBeatSpaceMorphDurationMs(double durationMs);
    double getBeatSpaceMorphDurationMs() const;
    void setBeatSpaceZoneLockStrength(int channel, float strength);
    float getBeatSpaceZoneLockStrength(int channel) const;
    void setBeatSpaceConfidenceOverlayEnabled(bool enabled);
    bool isBeatSpaceConfidenceOverlayEnabled() const;
    void setBeatSpacePathOverlayEnabled(bool enabled);
    bool isBeatSpacePathOverlayEnabled() const;
    void setBeatSpacePathRecordArmedChannel(int channel);
    int getBeatSpacePathRecordArmedChannel() const;
    bool beatSpaceAddBookmark(int channel, const juce::String& tag);
    bool beatSpaceRecallBookmark(int channel, int bookmarkIndex, bool morph);
    int getBeatSpaceBookmarkCount(int channel) const;
    juce::String getBeatSpaceBookmarkTag(int channel, int bookmarkIndex) const;
    void beatSpaceClearBookmarks(int channel);
    std::vector<int> getBeatSpaceNearestPresetSlots(int channel, int count) const;
    void beatSpacePathClear(int channel);
    bool beatSpacePathAddCurrentPoint(int channel);
    bool beatSpacePathStart(int channel, BeatSpacePathMode mode);
    void beatSpacePathStop(int channel);
    bool beatSpacePathIsActive(int channel) const;
    bool beatSpacePathRecordStart(int channel, const juce::Point<int>& point);
    bool beatSpacePathRecordAppendPoint(int channel, const juce::Point<int>& point);
    bool beatSpacePathRecordFinishAndPlay(int channel);
    void beatSpaceSetManualCategoryAnchor(int category, const juce::Point<int>& point);
    void beatSpaceClearManualCategoryAnchor(int category);
    void beatSpaceClearAllManualCategoryAnchors();
    bool beatSpaceClearNearestManualCategoryAnchor(const juce::Point<int>& point, int maxDistance);
    bool loadBeatSpacePresetFromAssignedSpace(int channel, int presetSlot);
    bool loadBeatSpacePresetFromAssignedSpaceDisplayIndex(int channel, int displayIndex);
    bool loadAdjacentBeatSpacePresetForAssignedSpace(int channel, int direction);
    static constexpr int MicrotonicStripPresetSlots = 16;
    std::vector<int> getMicrotonicStripPresetSlots(int channel) const;
    juce::String getMicrotonicStripPresetName(int channel, int slot) const;
    bool storeCurrentMicrotonicStripPreset(
        int channel,
        int preferredSlot,
        const juce::String& customName,
        int* storedSlotOut,
        juce::String* errorOut = nullptr);
    bool recallMicrotonicStripPreset(int channel, int slot, juce::String* errorOut = nullptr);
    bool deleteMicrotonicStripPreset(int channel, int slot);
    int getBeatSpaceChannelSpaceAssignment(int channel) const;
    void setBeatSpaceChannelSpaceAssignment(int channel, int space);
    std::vector<int> getBeatSpacePresetDisplaySlotsForAssignedSpace(int channel) const;
    juce::String getBeatSpacePresetLabelForSpaceSlot(int space, int slot) const;
    bool moveBeatSpacePresetForAssignedSpace(int channel, int fromDisplayIndex, int toDisplayIndex);
    bool renameBeatSpacePresetForAssignedSpace(int channel, int displayIndex, const juce::String& label);
    bool deleteBeatSpacePresetForAssignedSpace(int channel, int displayIndex);
    void resetBeatSpacePresetLayoutForAssignedSpace(int channel);
    static juce::String getBeatSpaceSpaceName(int space);
    
    // Preset management
    void savePreset(int presetIndex);
    void loadPreset(int presetIndex);
    bool deletePreset(int presetIndex);
    int getLoadedPresetIndex() const { return loadedPresetIndex; }
    juce::String getPresetName(int presetIndex) const;
    bool setPresetName(int presetIndex, const juce::String& name);
    bool presetExists(int presetIndex) const;
    uint32_t getPresetRefreshToken() const { return presetRefreshToken.load(std::memory_order_acquire); }
    static constexpr int PresetColumns = 16;
    static constexpr int PresetRows = 7;
    static constexpr int MaxPresetSlots = PresetColumns * PresetRows;
    static constexpr int SubPresetSlots = 4;
    
    // Parameters
    juce::AudioProcessorValueTreeState parameters;

private:
    //==============================================================================
    enum class FilterSubPage
    {
        Frequency,    // Button 0 on group row
        Resonance,    // Button 1 on group row
        Type          // Button 2 on group row
    };

    enum class StepEditTool
    {
        Gate,
        Velocity,
        Divide,
        RampUp,
        RampDown,
        Probability,
        Attack,
        Decay,
        Release
    };

    enum class ArcControlMode
    {
        SelectedStrip,
        Modulation
    };
    
    std::unique_ptr<ModernAudioEngine> audioEngine;
    HostedInstrumentRack hostRack;
    MonomeConnection monomeConnection;
    std::array<std::atomic<int>, MaxStrips> laneMidiChannel{};
    std::array<std::atomic<int>, MaxStrips> laneMidiNote{};
    std::array<int, MaxStrips> hostedLastCcVolume{};
    std::array<int, MaxStrips> hostedLastCcPan{};
    std::array<int, MaxStrips> hostedLastCcPitch{};
    std::array<int, MaxStrips> hostedDirectParamVolume{};
    std::array<int, MaxStrips> hostedDirectParamPan{};
    std::array<int, MaxStrips> hostedDirectParamPitch{};
    std::array<int, MaxStrips> hostedDirectParamAttack{};
    std::array<int, MaxStrips> hostedDirectParamDecay{};
    std::array<int, MaxStrips> hostedDirectParamRelease{};
    std::array<int, MaxStrips> hostedDirectParamAttackAux{};
    std::array<float, MaxStrips> hostedLastDirectParamVolume{};
    std::array<float, MaxStrips> hostedLastDirectParamPan{};
    std::array<float, MaxStrips> hostedLastDirectParamPitch{};
    std::array<float, MaxStrips> hostedLastDirectParamAttack{};
    std::array<float, MaxStrips> hostedLastDirectParamDecay{};
    std::array<float, MaxStrips> hostedLastDirectParamRelease{};
    std::array<float, MaxStrips> hostedLastDirectParamAttackAux{};
    std::array<int, MaxStrips> hostedProgramNumber{};
    std::array<std::atomic<int>, MaxStrips> hostedPendingProgramDelta{};
    std::array<double, MaxStrips> hostedTraversalRatioAtLastTick{};
    std::array<double, MaxStrips> hostedTraversalPhaseOffsetTicks{};
    bool hostedDefaultAutoLoadAttempted = false;
    mutable juce::CriticalSection hostedInstrumentFileLock;
    juce::File loadedHostedInstrumentFile;
    juce::File defaultHostedInstrumentFile;
    static constexpr int BeatSpaceTableSize = 1000;
    static constexpr int BeatSpaceVectorSize = 73;
    static constexpr int BeatSpacePatchParamCount = 25;
    // Quarter-octave zoom increments give finer monome/wheel control.
    static constexpr int BeatSpaceMaxZoom = 32;
    static constexpr int BeatSpaceZoomStepsPerOctave = 4;
    static constexpr int BeatSpaceMinViewWidth = 8;
    static constexpr int BeatSpaceMinViewHeight = 6;
    static constexpr int BeatSpaceBookmarkSlots = 8;
    struct BeatSpaceCell
    {
        std::array<float, BeatSpaceVectorSize> values{};
    };
    struct BeatSpaceBookmark
    {
        juce::Point<int> point{ BeatSpaceTableSize / 2, BeatSpaceTableSize / 2 };
        juce::String tag;
        bool used = false;
    };
    struct BeatSpacePathState
    {
        std::array<juce::Point<int>, BeatSpacePathMaxPoints> points{};
        int count = 0;
        bool active = false;
        BeatSpacePathMode mode = BeatSpacePathMode::QuarterNote;
        double cycleBeats = 4.0;
        int loopBars = 1;
        bool pendingQuantizedStart = false;
        bool recording = false;
        double startPpq = 0.0;
        double startMs = 0.0;
        double recordStartPpq = -1.0;
    };
    std::vector<BeatSpaceCell> beatSpaceTable;
    std::array<std::array<int, BeatSpaceVectorSize>, MaxStrips> beatSpaceParamMap{};
    std::array<std::array<float, BeatSpaceVectorSize>, MaxStrips> beatSpaceCurrentVectors{};
    std::array<std::array<float, BeatSpaceVectorSize>, MaxStrips> beatSpaceMorphStartVectors{};
    std::array<std::array<float, BeatSpaceVectorSize>, MaxStrips> beatSpaceMorphTargetVectors{};
    std::array<juce::Point<int>, MaxStrips> beatSpaceMorphStartPoints{};
    std::array<juce::Point<int>, MaxStrips> beatSpaceMorphTargetPoints{};
    std::array<juce::Point<int>, MaxStrips> beatSpaceMorphCurrentPoints{};
    std::array<float, MaxStrips> beatSpaceMorphProgress{};
    std::vector<int> beatSpaceColorClusters;
    std::vector<float> beatSpaceHotspotWeights;
    std::array<juce::Point<int>, MaxStrips> beatSpaceChannelPoints{};
    std::array<juce::Point<int>, BeatSpaceChannels> beatSpaceCategoryAnchors{};
    std::array<int, BeatSpaceChannels> beatSpaceCategoryRegionRadiusX{};
    std::array<int, BeatSpaceChannels> beatSpaceCategoryRegionRadiusY{};
    std::array<int, BeatSpaceChannels> beatSpaceCategoryColorCluster{};
    std::array<bool, BeatSpaceChannels> beatSpaceCategoryAnchorManual{};
    std::array<juce::Point<int>, BeatSpaceChannels> beatSpaceCategoryManualAnchors{};
    std::array<std::array<juce::Point<int>, BeatSpacePresetSlotsPerSpace>, BeatSpaceChannels> beatSpaceCategoryManualTagPoints{};
    std::array<int, BeatSpaceChannels> beatSpaceCategoryManualTagCounts{};
    std::array<std::array<juce::Point<int>, BeatSpacePresetSlotsPerSpace>, BeatSpaceChannels> beatSpaceCategoryPresetPoints{};
    std::array<std::array<int, BeatSpacePresetSlotsPerSpace>, BeatSpaceChannels> beatSpaceCategoryPresetOrder{};
    std::array<std::array<bool, BeatSpacePresetSlotsPerSpace>, BeatSpaceChannels> beatSpaceCategoryPresetHidden{};
    std::array<std::array<juce::String, BeatSpacePresetSlotsPerSpace>, BeatSpaceChannels> beatSpaceCategoryPresetLabels{};
    struct MicrotonicStripPreset
    {
        bool used = false;
        juce::String name;
        std::array<float, BeatSpacePatchParamCount> patchValues{};
    };
    std::array<std::array<MicrotonicStripPreset, MicrotonicStripPresetSlots>, BeatSpaceChannels> microtonicStripPresets{};
    std::array<bool, BeatSpaceChannels> beatSpaceCategoryPresetPointsReady{};
    std::array<juce::Point<float>, BeatSpaceChannels> beatSpaceLinkedOffsets{};
    std::array<float, BeatSpaceChannels> beatSpaceZoneLockStrength{};
    std::array<std::array<BeatSpaceBookmark, BeatSpaceBookmarkSlots>, BeatSpaceChannels> beatSpaceBookmarks{};
    std::array<BeatSpacePathState, BeatSpaceChannels> beatSpacePaths{};
    bool beatSpaceLinkedOffsetsReady = false;
    std::array<int, BeatSpaceChannels> beatSpaceChannelCategoryAssignment { 0, 1, 2, 3, 4, 5 };
    std::array<int, BeatSpaceChannels> beatSpaceLastLoadedDisplayPresetIndex { -1, -1, -1, -1, -1, -1 };
    std::array<int, MaxStrips> beatSpaceLastAppliedTableIndex{};
    std::array<float, MaxStrips> beatSpaceLastRecallMaxError{};
    std::array<bool, MaxStrips> beatSpaceChannelMappingReady{};
    std::array<bool, MaxStrips> beatSpaceMorphActive{};
    bool beatSpaceDecoderReady = false;
    bool beatSpaceMappingReady = false;
    bool beatSpaceCategoryAnchorsReady = false;
    bool beatSpaceColorClustersReady = false;
    bool beatSpaceMicrotonicExactMapping = false;
    bool beatSpaceLinkAllChannels = false;
    bool beatSpaceConfidenceOverlayEnabled = true;
    bool beatSpacePathOverlayEnabled = true;
    BeatSpaceRandomizeMode beatSpaceRandomizeMode = BeatSpaceRandomizeMode::WithinCategory;
    juce::Image beatSpaceTablePreviewImage;
    juce::Image beatSpaceConfidencePreviewImage;
    int beatSpaceSelectedChannel = 0;
    int beatSpacePathRecordArmedChannel = -1;
    int beatSpaceZoomLevel = 0;
    int beatSpaceViewX = 0;
    int beatSpaceViewY = 0;
    double beatSpaceMorphStartTimeMs = 0.0;
    double beatSpaceMorphDurationMs = 160.0;
    juce::String beatSpaceDecoderName;
    juce::String beatSpaceStatusMessage;

    struct PendingLoopChange
    {
        bool active = false;
        bool clear = false;
        int startColumn = 0;
        int endColumn = MaxColumns;
        int markerColumn = -1;
        bool reverse = false;
        bool quantized = false;
        double targetPpq = 0.0;
        int quantizeDivision = 8;
        bool postClearTriggerArmed = false;
        int postClearTriggerColumn = 0;
    };

    struct PendingBarChange
    {
        bool active = false;
        int recordingBars = 1;
        float beatsPerLoop = 4.0f;
        bool quantized = false;
        double targetPpq = 0.0;
        int quantizeDivision = 8;
    };

    // Cached parameter pointers to avoid string lookups in processBlock
    std::atomic<float>* masterVolumeParam = nullptr;
    std::atomic<float>* limiterThresholdParam = nullptr;
    std::atomic<float>* limiterEnabledParam = nullptr;
    std::atomic<float>* pitchSmoothingParam = nullptr;
    std::atomic<float>* outputRoutingParam = nullptr;
    std::atomic<float>* soundTouchEnabledParam = nullptr;
    std::array<std::atomic<float>*, MaxStrips> stripVolumeParams{};
    std::array<std::atomic<float>*, MaxStrips> stripPanParams{};
    std::array<std::atomic<float>*, MaxStrips> stripSpeedParams{};
    std::array<std::atomic<float>*, MaxStrips> stripPitchParams{};
    std::array<std::atomic<float>*, MaxStrips> stripStepAttackParams{};
    std::array<std::atomic<float>*, MaxStrips> stripStepDecayParams{};
    std::array<std::atomic<float>*, MaxStrips> stripStepReleaseParams{};
    std::array<std::atomic<float>*, MaxStrips> stripSliceLengthParams{};
    juce::CriticalSection pendingLoopChangeLock;
    std::array<PendingLoopChange, MaxStrips> pendingLoopChanges{};
    juce::CriticalSection pendingBarChangeLock;
    std::array<PendingBarChange, MaxStrips> pendingBarChanges{};
    std::array<bool, MaxStrips> pendingBarLengthApply{};
    
    double currentSampleRate = 44100.0;
    ControlMode currentControlMode = ControlMode::Normal;
    bool controlModeActive = false;  // True when control button is held
    FilterSubPage filterSubPage = FilterSubPage::Frequency;  // Current filter sub-page
    std::atomic<int> lastMonomePressedStripRow{0};
    mutable juce::CriticalSection controlPageOrderLock;
    ControlPageOrder controlPageOrder {
        ControlMode::Speed,
        ControlMode::Pitch,
        ControlMode::Pan,
        ControlMode::Volume,
        ControlMode::Length,
        ControlMode::Swing,
        ControlMode::Gate,
        ControlMode::FileBrowser,
        ControlMode::GroupAssign,
        ControlMode::Filter,
        ControlMode::Modulation,
        ControlMode::BeatSpace,
        ControlMode::Preset,
        ControlMode::StepEdit
    };
    StepEditTool stepEditTool = StepEditTool::Gate;
    int stepEditSelectedStrip = 0;
    int stepEditStripBank = 0;
    std::array<bool, MaxColumns> stepEditVelocityGestureActive{};
    std::array<int, MaxColumns> stepEditVelocityGestureStrip{};
    std::array<int, MaxColumns> stepEditVelocityGestureStep{};
    std::array<float, MaxColumns> stepEditVelocityGestureAnchorStart{};
    std::array<float, MaxColumns> stepEditVelocityGestureAnchorEnd{};
    std::array<float, MaxColumns> stepEditVelocityGestureAnchorValue{};
    std::array<uint32_t, MaxColumns> stepEditVelocityGestureLastActivityMs{};
    static constexpr uint32_t stepEditVelocityGestureLatchMs = 180;
    std::atomic<bool> controlPageMomentary{true};
    std::atomic<int> swingDivisionSelection{1}; // 0=1/4,1=1/8,2=1/16,3=1/8T,4=1/2,5=1/32,6=1/16T
    int lastAppliedSoundTouchEnabled = -1; // -1 = force initial sync on first process block
    
    // LED state cache to prevent flickering
    int ledCache[MaxGridWidth][MaxGridHeight] = {{0}};
    
    // Last loaded folder for file browsing
    juce::File lastSampleFolder;
    std::array<juce::File, MaxStrips> defaultLoopDirectories;
    std::array<juce::File, MaxStrips> defaultStepDirectories;
    std::array<juce::File, BrowserFavoriteSlots> browserFavoriteDirectories;
    std::atomic<int> persistentGlobalControlsDirty{0};
    std::atomic<int> suppressPersistentGlobalControlsSave{0};
    std::atomic<int> persistentGlobalControlsSaveQueued{0};
    juce::int64 lastPersistentGlobalControlsSaveMs = 0;
    bool persistentGlobalControlsApplied = false;
    std::atomic<int> pendingPersistentGlobalControlsRestore{0};
    juce::int64 pendingPersistentGlobalControlsRestoreMs = 0;
    int pendingPersistentGlobalControlsRestoreRemaining = 0;
    std::atomic<int> persistentGlobalControlsReady{0};
    std::atomic<int> persistentGlobalUserTouched{0};
    std::array<std::array<bool, BrowserFavoriteSlots>, MaxStrips> browserFavoritePadHeld{};
    std::array<std::array<bool, BrowserFavoriteSlots>, MaxStrips> browserFavoritePadHoldSaveTriggered{};
    std::array<std::array<uint32_t, BrowserFavoriteSlots>, MaxStrips> browserFavoritePadPressStartMs{};
    std::array<uint32_t, BrowserFavoriteSlots> browserFavoriteSaveBurstUntilMs{};
    std::array<uint32_t, BrowserFavoriteSlots> browserFavoriteMissingBurstUntilMs{};
    std::array<int, 4> arcKeyHeld{};
    std::array<std::array<int, 64>, 4> arcRingCache{};
    ArcControlMode arcControlMode = ArcControlMode::SelectedStrip;
    int arcSelectedModStep = 0;
    juce::int64 lastGridLedUpdateTimeMs = 0;
    static constexpr int kGridRefreshMs = 33;
    static constexpr int kArcRefreshMs = 33;
    static constexpr uint32_t browserFavoriteHoldSaveMs = 3000;
    static constexpr uint32_t browserFavoriteSaveBurstDurationMs = 320;
    static constexpr uint32_t browserFavoriteMissingBurstDurationMs = 260;
    
    // Current file per strip for proper next/prev browsing
    std::array<juce::File, MaxStrips> currentStripFiles;
    
    // LED update
    void updateMonomeLEDs();
    void updateMonomeArcRings();
    void timerCallback() override;
    
    void handleMonomeKeyPress(int x, int y, int state);
    void resetStepEditVelocityGestures();
    bool isArcModulationMode() const;
    void setArcControlMode(ArcControlMode mode);
    void handleMonomeArcDelta(int encoder, int delta);
    void handleMonomeArcKey(int encoder, int state);
    void buildHostedLaneMidi(const juce::AudioPlayHead::PositionInfo& posInfo,
                             int numSamples,
                             juce::MidiBuffer& midi);
    void setMomentaryScratchHold(bool shouldEnable);
    void setMomentaryStutterHold(bool shouldEnable);
    
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void cacheParameterPointers();
    SamplePathMode getSamplePathModeForStrip(int stripIndex) const;
    bool saveBrowserFavoriteDirectoryFromStrip(int stripIndex, int slot);
    bool recallBrowserFavoriteDirectoryForStrip(int stripIndex, int slot);
    bool isAudioFileSupported(const juce::File& file) const;
    void loadDefaultPathsFromState(const juce::ValueTree& state);
    void appendDefaultPathsToState(juce::ValueTree& state) const;
    void loadHostedLaneMidiFromState(const juce::ValueTree& state);
    void appendHostedLaneMidiToState(juce::ValueTree& state) const;
    void loadControlPagesFromState(const juce::ValueTree& state);
    void appendControlPagesToState(juce::ValueTree& state) const;
    void stripPersistentGlobalControlsFromState(juce::ValueTree& state) const;
    void loadPersistentDefaultPaths();
    void savePersistentDefaultPaths() const;
    void loadPersistentControlPages();
    void savePersistentControlPages() const;
    void loadPersistentGlobalControls();
    bool loadDefaultHostedInstrumentIfNeeded(juce::String& error);
    void applyBeatSpaceDefaultChannelLayout();
    bool initializeBeatSpaceTable();
    void rebuildBeatSpaceTablePreviewImage();
    void rebuildBeatSpaceColorClusters(const juce::File& scriptDir, const juce::String& decoderName);
    void rebuildBeatSpaceCategoryAnchors();
    void rebuildBeatSpaceCategoryPresetPoints();
    bool captureCurrentMicrotonicPatchValues(
        int channel,
        std::array<float, BeatSpacePatchParamCount>& outValues,
        juce::String* errorOut);
    bool applyMicrotonicPatchValues(
        int channel,
        const std::array<float, BeatSpacePatchParamCount>& values,
        juce::String* errorOut);
    void clearHostedDirectParameterMap();
    void refreshHostedDirectParameterMapFromBeatSpace(bool likelyMicrotonic);
    bool refreshBeatSpaceParameterMap();
    void clampBeatSpaceView();
    int getBeatSpaceViewWidth() const;
    int getBeatSpaceViewHeight() const;
    juce::Point<int> clampBeatSpacePointToTable(const juce::Point<int>& point) const;
    juce::Point<int> gridCellToBeatSpacePoint(int gridX, int gridY, int gridWidth, int gridHeight) const;
    juce::Point<int> beatSpacePointToGridCell(const juce::Point<int>& point, int gridWidth, int gridHeight) const;
    juce::Point<int> constrainBeatSpacePointForChannel(int channel, const juce::Point<int>& point) const;
    void rebuildBeatSpaceLinkedOffsetsFromCurrent(int masterChannel);
    void applyBeatSpaceLinkedChannelOffsets(const juce::Point<int>& masterPoint, int masterChannel);
    void beginBeatSpaceMorphForChannel(int channel,
                                       const std::array<float, BeatSpaceVectorSize>& targetValues,
                                       const juce::Point<int>& targetPoint);
    void applyBeatSpaceVectorToChannel(int channel, const std::array<float, BeatSpaceVectorSize>& values);
    void applyBeatSpacePointToChannels(bool applyAllChannels, bool morph);
    juce::Point<int> randomBeatSpacePointForChannel(int channel, BeatSpaceRandomizeMode mode) const;
    float getBeatSpacePointConfidence(const juce::Point<int>& point) const;
    void updateBeatSpacePathMorph(const juce::AudioPlayHead::PositionInfo& posInfo);
    void updateBeatSpaceMorph();
    void normalizeBeatSpacePresetLayoutForSpace(int space);
    std::vector<int> getBeatSpaceVisiblePresetSlotsForSpace(int space) const;
    int getQuantizeDivision() const;
    float getInnerLoopLengthFactor() const;
    void queueLoopChange(int stripIndex, bool clearLoop, int startColumn, int endColumn, bool reverseDirection, int markerColumn = -1);
    void applyPendingLoopChanges(const juce::AudioPlayHead::PositionInfo& posInfo);
    void applyPendingBarChanges(const juce::AudioPlayHead::PositionInfo& posInfo);
    void applyPendingStutterStart(const juce::AudioPlayHead::PositionInfo& posInfo);
    void applyPendingStutterRelease(const juce::AudioPlayHead::PositionInfo& posInfo);
    void performMomentaryStutterStartNow(double hostPpqNow, int64_t nowSample);
    void performMomentaryStutterReleaseNow(double hostPpqNow, int64_t nowSample);
    void captureMomentaryStutterMacroBaseline();
    void applyMomentaryStutterMacro(const juce::AudioPlayHead::PositionInfo& posInfo);
    void restoreMomentaryStutterMacroBaseline();
    bool getHostSyncSnapshot(double& outPpq, double& outTempo) const;
    void performPresetLoad(int presetIndex, double hostPpqSnapshot, double hostTempoSnapshot);
    struct PresetSaveRequest
    {
        int presetIndex = -1;
        std::array<juce::File, MaxStrips> stripFiles;
    };
    struct PresetSaveResult
    {
        int presetIndex = -1;
        bool success = false;
    };
    class PresetSaveJob;
    bool runPresetSaveRequest(const PresetSaveRequest& request);
    void pushPresetSaveResult(const PresetSaveResult& result);
    void applyCompletedPresetSaves();
    void resetRuntimePresetStateToDefaults();
    int getActiveMainPresetIndexForSubPresets() const;
    int getSubPresetStoragePresetIndex(int mainPresetIndex, int subPresetSlot) const;
    bool saveSubPresetForMainPreset(int mainPresetIndex, int subPresetSlot);
    void ensureSubPresetsInitializedForMainPreset(int mainPresetIndex);
    void requestSubPresetRecallQuantized(int mainPresetIndex, int subPresetSlot, bool sequenceDriven);
    void updateSubPresetQuantizedRecall(const juce::AudioPlayHead::PositionInfo& posInfo, int numSamples);
    void processPendingSubPresetApply();

    // Row 0, col 8: global momentary scratch modifier.
    bool momentaryScratchHoldActive = false;
    std::array<float, MaxStrips> momentaryScratchSavedAmount{};
    std::array<EnhancedAudioStrip::DirectionMode, MaxStrips> momentaryScratchSavedDirection{};
    std::array<bool, MaxStrips> momentaryScratchWasStepMode{};

    // Row 0, cols 9..15: PPQ stutter-hold with fixed divisions.
    bool momentaryStutterHoldActive = false;
    double momentaryStutterDivisionBeats = 1.0; // one-button map spans 2.0 (1/2) ... 0.03125 (1/128)
    int momentaryStutterActiveDivisionButton = -1;
    std::atomic<uint8_t> momentaryStutterButtonMask{0};
    std::array<bool, MaxStrips> momentaryStutterStripArmed{};
    struct MomentaryStutterSavedStripState
    {
        bool valid = false;
        bool stepMode = false;
        float pan = 0.0f;
        float playbackSpeed = 1.0f;
        float pitchSemitones = 0.0f;
        float pitchShift = 0.0f;
        float loopSliceLength = 1.0f;
        bool filterEnabled = false;
        float filterFrequency = 20000.0f;
        float filterResonance = 0.707f;
        float filterMorph = 0.0f;
        EnhancedAudioStrip::FilterAlgorithm filterAlgorithm = EnhancedAudioStrip::FilterAlgorithm::Tpt12;
        bool stepFilterEnabled = false;
        float stepFilterFrequency = 1000.0f;
        float stepFilterResonance = 0.7f;
        FilterType stepFilterType = FilterType::LowPass;
    };
    std::array<MomentaryStutterSavedStripState, MaxStrips> momentaryStutterSavedState{};
    bool momentaryStutterMacroBaselineCaptured = false;
    bool momentaryStutterMacroCapturePending = false;
    double momentaryStutterMacroStartPpq = 0.0;
    uint8_t momentaryStutterLastComboMask = 0;
    bool momentaryStutterTwoButtonStepBaseValid = false;
    int momentaryStutterTwoButtonStepBase = 0;
    std::atomic<int> momentaryStutterPlaybackActive{0};
    std::atomic<int> pendingStutterStartActive{0};
    std::atomic<double> pendingStutterStartPpq{-1.0};
    std::atomic<double> pendingStutterStartDivisionBeats{1.0};
    std::atomic<int> pendingStutterStartQuantizeDivision{8};
    std::atomic<int64_t> pendingStutterStartSampleTarget{-1};
    std::atomic<int> pendingStutterReleaseActive{0};
    std::atomic<double> pendingStutterReleasePpq{-1.0};
    std::atomic<int> pendingStutterReleaseQuantizeDivision{8};
    std::atomic<int64_t> pendingStutterReleaseSampleTarget{-1};

    // Preset page hold/double-tap state (used when control mode == Preset).
    struct PendingSubPresetRecall
    {
        bool active = false;
        bool sequenceDriven = false;
        bool targetResolved = false;
        int mainPresetIndex = 0;
        int subPresetSlot = 0;
        double targetPpq = 0.0;
    };
    int loadedPresetIndex = -1;
    int activeMainPresetIndex = 0;
    int activeSubPresetSlot = 0;
    std::array<bool, MaxPresetSlots> presetPadHeld{};
    std::array<bool, MaxPresetSlots> presetPadHoldSaveTriggered{};
    std::array<bool, MaxPresetSlots> presetPadDeleteTriggered{};
    std::array<uint32_t, MaxPresetSlots> presetPadPressStartMs{};
    std::array<uint32_t, MaxPresetSlots> presetPadSaveBurstUntilMs{};
    std::atomic<uint32_t> presetRefreshToken{0};
    std::array<uint32_t, MaxPresetSlots> presetPadLastTapMs{};
    static constexpr uint32_t presetHoldSaveMs = 3000;
    static constexpr uint32_t presetDoubleTapMs = 350;
    static constexpr uint32_t presetSaveBurstDurationMs = 260;
    static constexpr uint32_t presetSaveBurstIntervalMs = 55;
    std::array<bool, SubPresetSlots> subPresetPadHeld{};
    std::array<bool, SubPresetSlots> subPresetPadHoldSaveTriggered{};
    std::array<uint32_t, SubPresetSlots> subPresetPadPressStartMs{};
    std::array<uint32_t, SubPresetSlots> subPresetPadSaveBurstUntilMs{};
    std::vector<int> subPresetSequenceSlots;
    bool subPresetSequenceActive = false;
    PendingSubPresetRecall pendingSubPresetRecall;
    std::atomic<int> pendingSubPresetApplyMainPreset{-1};
    std::atomic<int> pendingSubPresetApplySlot{-1};
    std::atomic<int> pendingPresetLoadIndex{-1};
    juce::ThreadPool presetSaveThreadPool{1};
    juce::CriticalSection presetSaveResultLock;
    std::vector<PresetSaveResult> presetSaveResults;
    std::atomic<int> presetSaveJobsInFlight{0};
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepVstHostAudioProcessor)
    JUCE_DECLARE_WEAK_REFERENCEABLE(StepVstHostAudioProcessor)
};
