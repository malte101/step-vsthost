/*
  ==============================================================================

    MonomeConnection.cpp
    Step Host build - serialosc connection

  ==============================================================================
*/

#include "MonomeConnection.h"

MonomeConnection::MonomeConnection()
{
    oscReceiver.addListener(this);
    startTimer(100);
}

MonomeConnection::~MonomeConnection()
{
    disconnect();
    oscReceiver.removeListener(this);
    stopTimer();
}

void MonomeConnection::connect(int appPort)
{
    applicationPort = appPort;
    if (connected)
        return;

    if (!oscReceiver.connect(applicationPort))
        return;

    refreshDeviceList();
}

void MonomeConnection::refreshDeviceList()
{
    discoverDevices();
}

void MonomeConnection::disconnect()
{
    if (!connected && !oscReceiver.isConnected())
        return;

    oscReceiver.disconnect();
    oscSender.disconnect();
    connected = false;
    devices.clear();
    if (onDeviceDisconnected)
        onDeviceDisconnected();
}

void MonomeConnection::discoverDevices()
{
    if (!serialoscSender.connect("127.0.0.1", 12002))
        return;

    devices.clear();
    awaitingDeviceResponse = true;
    lastDiscoveryTime = juce::Time::currentTimeMillis();
    serialoscSender.send("/serialosc/list", "si", "127.0.0.1", applicationPort);
}

void MonomeConnection::selectDevice(int deviceIndex)
{
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(devices.size()))
        return;

    currentDevice = devices[static_cast<size_t>(deviceIndex)];
    configureCurrentDevice();
}

void MonomeConnection::setLED(int x, int y, int state)
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/grid/led/set", x, y, state);
}

void MonomeConnection::setAllLEDs(int state)
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/grid/led/all", state);
}

void MonomeConnection::setLEDRow(int xOffset, int y, const std::array<int, 8>& data)
{
    if (!connected)
        return;
    juce::OSCMessage message(oscPrefix + "/grid/led/row");
    message.addInt32(xOffset);
    message.addInt32(y);
    for (auto value : data)
        message.addInt32(value);
    oscSender.send(message);
}

void MonomeConnection::setLEDColumn(int x, int yOffset, const std::array<int, 8>& data)
{
    if (!connected)
        return;
    juce::OSCMessage message(oscPrefix + "/grid/led/col");
    message.addInt32(x);
    message.addInt32(yOffset);
    for (auto value : data)
        message.addInt32(value);
    oscSender.send(message);
}

void MonomeConnection::setLEDMap(int xOffset, int yOffset, const std::array<int, 8>& data)
{
    if (!connected)
        return;
    juce::OSCMessage message(oscPrefix + "/grid/led/map");
    message.addInt32(xOffset);
    message.addInt32(yOffset);
    for (auto value : data)
        message.addInt32(value);
    oscSender.send(message);
}

void MonomeConnection::setLEDLevel(int x, int y, int level)
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/grid/led/level/set", x, y, level);
}

void MonomeConnection::setAllLEDLevels(int level)
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/grid/led/level/all", level);
}

void MonomeConnection::setLEDLevelRow(int xOffset, int y, const std::array<int, 8>& levels)
{
    if (!connected)
        return;
    juce::OSCMessage message(oscPrefix + "/grid/led/level/row");
    message.addInt32(xOffset);
    message.addInt32(y);
    for (auto level : levels)
        message.addInt32(level);
    oscSender.send(message);
}

void MonomeConnection::setLEDLevelColumn(int x, int yOffset, const std::array<int, 8>& levels)
{
    if (!connected)
        return;
    juce::OSCMessage message(oscPrefix + "/grid/led/level/col");
    message.addInt32(x);
    message.addInt32(yOffset);
    for (auto level : levels)
        message.addInt32(level);
    oscSender.send(message);
}

void MonomeConnection::setLEDLevelMap(int xOffset, int yOffset, const std::array<int, 64>& levels)
{
    if (!connected)
        return;
    juce::OSCMessage message(oscPrefix + "/grid/led/level/map");
    message.addInt32(xOffset);
    message.addInt32(yOffset);
    for (auto level : levels)
        message.addInt32(level);
    oscSender.send(message);
}

void MonomeConnection::setArcRingMap(int encoder, const std::array<int, 64>& levels)
{
    if (!connected)
        return;
    juce::OSCMessage message(oscPrefix + "/ring/map");
    message.addInt32(encoder);
    for (auto level : levels)
        message.addInt32(level);
    oscSender.send(message);
}

void MonomeConnection::setArcRingLevel(int encoder, int ledIndex, int level)
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/ring/set", encoder, ledIndex, level);
}

void MonomeConnection::setArcRingRange(int encoder, int start, int end, int level)
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/ring/range", encoder, start, end, level);
}

bool MonomeConnection::supportsGrid() const
{
    return currentDevice.sizeX > 0 && currentDevice.sizeY > 0;
}

bool MonomeConnection::supportsArc() const
{
    return currentDevice.type.containsIgnoreCase("arc");
}

int MonomeConnection::getArcEncoderCount() const
{
    return supportsArc() ? juce::jmax(1, currentDevice.sizeX) : 0;
}

void MonomeConnection::setRotation(int degrees)
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/grid/rotation", degrees);
}

void MonomeConnection::setPrefix(const juce::String& newPrefix)
{
    oscPrefix = newPrefix;
    if (connected)
        oscSender.send(oscPrefix + "/sys/prefix", oscPrefix);
}

void MonomeConnection::requestInfo()
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/sys/info");
}

void MonomeConnection::requestSize()
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/sys/size");
}

void MonomeConnection::enableTilt(int sensor, bool enable)
{
    if (!connected)
        return;
    oscSender.send(oscPrefix + "/tilt/set", sensor, enable ? 1 : 0);
}

juce::String MonomeConnection::getConnectionStatus() const
{
    if (!connected)
        return "Disconnected";
    return "Connected: " + currentDevice.type + " (" + juce::String(currentDevice.sizeX) + "x"
           + juce::String(currentDevice.sizeY) + ")";
}

void MonomeConnection::oscMessageReceived(const juce::OSCMessage& message)
{
    handleSerialOSCMessage(message);
}

void MonomeConnection::handleSerialOSCMessage(const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();
    if (address.startsWith("/serialosc"))
    {
        handleSystemMessage(message);
        return;
    }
    if (address.contains("/grid"))
    {
        handleGridMessage(message);
        return;
    }
    if (address.contains("/tilt"))
    {
        handleTiltMessage(message);
        return;
    }
    if (address.contains("/enc") || address.contains("/ring"))
    {
        handleArcMessage(message);
        return;
    }
}

void MonomeConnection::handleGridMessage(const juce::OSCMessage& message)
{
    if (message.getAddressPattern().toString().contains(oscPrefix + "/grid/key") && message.size() >= 3)
    {
        const int x = message[0].getInt32();
        const int y = message[1].getInt32();
        const int s = message[2].getInt32();
        if (onKeyPress)
            onKeyPress(x, y, s);
        lastMessageTime = juce::Time::currentTimeMillis();
    }
}

void MonomeConnection::handleSystemMessage(const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();
    if (address == "/sys/size" && message.size() >= 2)
    {
        currentDevice.sizeX = message[0].getInt32();
        currentDevice.sizeY = message[1].getInt32();
        return;
    }
    if (address == "/sys/id" && message.size() >= 1)
    {
        currentDevice.id = message[0].getString();
        return;
    }
    if (address == "/sys/host" && message.size() >= 1)
    {
        currentDevice.host = message[0].getString();
        return;
    }
    if (address == "/sys/prefix" && message.size() >= 1)
        return;
    if (address == "/sys/rotation" && message.size() >= 1)
        return;
    if (address == "/sys/port" && message.size() >= 1)
        return;
    if (address.endsWith("/device") && message.size() >= 3)
    {
        DeviceInfo info;
        info.id = message[0].getString();
        info.type = message[1].getString();
        info.port = message[2].getInt32();
        devices.push_back(info);
        if (onDeviceListUpdated)
            onDeviceListUpdated(devices);
        lastMessageTime = juce::Time::currentTimeMillis();
        return;
    }

    if (address.endsWith("/add") && message.size() >= 3)
    {
        DeviceInfo info;
        info.id = message[0].getString();
        info.type = message[1].getString();
        info.port = message[2].getInt32();
        devices.push_back(info);
        if (onDeviceListUpdated)
            onDeviceListUpdated(devices);
        lastMessageTime = juce::Time::currentTimeMillis();
        return;
    }
}

void MonomeConnection::handleTiltMessage(const juce::OSCMessage& message)
{
    if (message.size() < 4)
        return;
    if (onTilt)
        onTilt(message[0].getInt32(), message[1].getInt32(), message[2].getInt32(), message[3].getInt32());
}

void MonomeConnection::handleArcMessage(const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();
    if (address.contains("/enc/delta") && message.size() >= 2)
    {
        if (onArcDelta)
            onArcDelta(message[0].getInt32(), message[1].getInt32());
        return;
    }
    if (address.contains("/enc/key") && message.size() >= 2)
    {
        if (onArcKey)
            onArcKey(message[0].getInt32(), message[1].getInt32());
    }
}

void MonomeConnection::timerCallback()
{
    const auto now = juce::Time::currentTimeMillis();
    if (connected && (now - lastMessageTime) > connectionTimeoutMs)
        markDisconnected();

    if (!connected && autoReconnect && (now - lastReconnectAttemptTime) > reconnectIntervalMs)
        attemptReconnection();

    if (connected && (now - lastPingTime) > pingIntervalMs)
        sendPing();
}

void MonomeConnection::attemptReconnection()
{
    if (reconnectAttempts >= maxReconnectAttempts)
        return;
    ++reconnectAttempts;
    lastReconnectAttemptTime = juce::Time::currentTimeMillis();
    refreshDeviceList();
}

void MonomeConnection::sendPing()
{
    lastPingTime = juce::Time::currentTimeMillis();
    requestInfo();
}

void MonomeConnection::markDisconnected()
{
    if (!connected)
        return;
    connected = false;
    if (onDeviceDisconnected)
        onDeviceDisconnected();
}

void MonomeConnection::configureCurrentDevice()
{
    if (!oscSender.connect(currentDevice.host, currentDevice.port))
        return;
    connected = true;
    reconnectAttempts = 0;
    lastMessageTime = juce::Time::currentTimeMillis();
    if (onDeviceConnected)
        onDeviceConnected();
    oscSender.send(juce::OSCMessage("/sys/port", applicationPort));
    oscSender.send(juce::OSCMessage("/sys/host", juce::String("127.0.0.1")));
    oscSender.send(juce::OSCMessage("/sys/prefix", oscPrefix));
    requestInfo();
    requestSize();
}
