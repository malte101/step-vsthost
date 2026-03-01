/*
  ==============================================================================

    MonomeConnection.h
    Step Host build - serialosc connection

  ==============================================================================
*/

#pragma once

#include <juce_osc/juce_osc.h>
#include <functional>
#include <vector>
#include <array>

class MonomeConnection : public juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>,
                          public juce::Timer
{
public:
    MonomeConnection();
    ~MonomeConnection() override;

    void connect(int applicationPort);
    void disconnect();
    bool isConnected() const { return connected; }

    void discoverDevices();
    void selectDevice(int deviceIndex);
    void refreshDeviceList();

    void setLED(int x, int y, int state);
    void setAllLEDs(int state);
    void setLEDRow(int xOffset, int y, const std::array<int, 8>& data);
    void setLEDColumn(int x, int yOffset, const std::array<int, 8>& data);
    void setLEDMap(int xOffset, int yOffset, const std::array<int, 8>& data);

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

    void setRotation(int degrees);
    void setPrefix(const juce::String& newPrefix);
    void requestInfo();
    void requestSize();

    void enableTilt(int sensor, bool enable);

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
    juce::OSCSender serialoscSender;
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
