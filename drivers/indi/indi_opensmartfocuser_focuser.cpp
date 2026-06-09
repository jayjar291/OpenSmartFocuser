#include "config.h"
#include "indi_opensmartfocuser_focuser.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstring>
#include <memory>
#include <string>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

static std::unique_ptr<OpenSmartFocuser> openSmartFocuser(new OpenSmartFocuser());

namespace
{

// UI tab names and protocol/runtime constants used across the driver.
constexpr const char *CUSTOM_TAB = "Custom";
constexpr int SERIAL_BAUD = B115200;
constexpr int SERIAL_TIMEOUT_MS = 1500;
constexpr uint32_t MAX_SPEED_INDEX = 4;
constexpr int32_t INDI_CLIENT_SAFE_MAX_STEPS = INT32_MAX;
constexpr double SHUTTER_MIN = 0.0;
constexpr double SHUTTER_MAX = 270.0;
constexpr double FLAT_PANEL_MIN = 0.0;
constexpr double FLAT_PANEL_MAX = 255.0;
constexpr uint32_t SHUTTER_OFF = 0;
constexpr uint32_t SHUTTER_ON = 270;
constexpr uint32_t FLAT_PANEL_OFF = 0;
constexpr uint32_t FLAT_PANEL_ON = 255;

// Parse a full unsigned integer payload and reject partial/invalid conversions.
bool parsePositiveInteger(const std::string &text, uint32_t &value)
{
    if (text.empty())
        return false;

    char *endptr = nullptr;
    errno = 0;
    const unsigned long converted = std::strtoul(text.c_str(), &endptr, 10);
    if (errno != 0 || endptr == text.c_str() || *endptr != '\0')
        return false;

    value = static_cast<uint32_t>(converted);
    return true;
}

bool parseSignedInteger32(const std::string &text, int32_t &value)
{
    if (text.empty())
        return false;

    char *endptr = nullptr;
    errno = 0;
    const long converted = std::strtol(text.c_str(), &endptr, 10);
    if (errno != 0 || endptr == text.c_str() || *endptr != '\0')
        return false;
    if (converted < static_cast<long>(INT32_MIN) || converted > static_cast<long>(INT32_MAX))
        return false;

    value = static_cast<int32_t>(converted);
    return true;
}

bool startsWith(const std::string &text, const std::string &prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// Escape non-printable bytes so logs and monitor output remain readable.
std::string escapeFrameForLog(const std::string &text)
{
    static constexpr char HEX[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(text.size() * 4);

    for (unsigned char ch : text)
    {
        if (std::isprint(ch) != 0)
        {
            escaped.push_back(static_cast<char>(ch));
            continue;
        }

        escaped.push_back('\\');
        escaped.push_back('x');
        escaped.push_back(HEX[(ch >> 4) & 0x0F]);
        escaped.push_back(HEX[ch & 0x0F]);
    }

    return escaped;
}

} // namespace

// Driver constructor: declare focuser capabilities and disable built-in connection plugins
// because transport is handled manually in this class.
OpenSmartFocuser::OpenSmartFocuser() : INDI::DustCapInterface(this), INDI::LightBoxInterface(this)
{
    FI::SetCapability(FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_CAN_ABORT);
    setSupportedConnections(CONNECTION_NONE);
    setVersion(CDRIVER_VERSION_MAJOR, CDRIVER_VERSION_MINOR);
}

const char *OpenSmartFocuser::getDefaultName()
{
    return "OpenSmartFocuser";
}

void OpenSmartFocuser::ISGetProperties(const char *dev)
{
    INDI::Focuser::ISGetProperties(dev);
    INDI::LightBoxInterface::ISGetProperties(dev);
}

// Initialize base focuser interface and custom properties owned by this driver.
bool OpenSmartFocuser::initProperties()
{
    INDI::Focuser::initProperties();
    INDI::DustCapInterface::initProperties(CUSTOM_TAB, 0);
    INDI::LightBoxInterface::initProperties(CUSTOM_TAB, INDI::LightBoxInterface::CAN_DIM);
    syncAdvertisedInterfaces();

    initCustomProperties();
    defineProperty(UsbPortTP);

    cachedPosition = static_cast<uint32_t>(FocusAbsPosNP[0].getValue());
    LOG_INFO("Initialized minimal OpenSmartFocuser USB serial driver.");
    return true;
}

// Keep dynamic properties in sync with connection state and refresh absolute position.
bool OpenSmartFocuser::updateProperties()
{
    INDI::Focuser::updateProperties();
    updateAddonInterfaces();
    updateCustomPropertyVisibility();

    if (isConnected())
    {
        uint32_t position = cachedPosition;
        if (queryPosition(position))
        {
            cachedPosition = position;
            FocusAbsPosNP[0].setValue(cachedPosition);
            FocusAbsPosNP.setState(IPS_OK);
            FocusAbsPosNP.apply();
        }
    }

    return true;
}

// Open serial transport, verify heartbeat, and synchronize runtime UI state.
bool OpenSmartFocuser::Connect()
{
    if (!openSerialPort())
        return false;

    std::string response;
    if (!sendCommand(":PP", "", response) || response != ":PP#")
    {
        LOG_ERROR("Connect failed: heartbeat did not return :PP#.");
        closeSerialPort();
        return false;
    }

    publishRawOutput(response);

    int32_t minSteps = 0;
    int32_t maxSteps = 0;
    if (queryLimits(minSteps, maxSteps))
    {
        applyLimits(minSteps, maxSteps);
    }
    else
    {
        LOG_WARN("Could not query limits via :GL# during connect. Keeping current INDI limits.");
    }

    uint32_t speedIndex = 0;
    if (querySpeedIndex(speedIndex))
        updateSpeedSelection(speedIndex);

    bool detectedShutter = false;
    bool detectedFlatPanel = false;
    if (queryAddons(detectedShutter, detectedFlatPanel))
    {
        hasShutterAddon = detectedShutter;
        hasFlatPanelAddon = detectedFlatPanel;
    }
    else
    {
        LOG_WARN("Failed to query add-ons via :AQ#. Assuming no shutter/flat panel add-ons.");
        hasShutterAddon = false;
        hasFlatPanelAddon = false;
    }

    updateAddonInterfaces();
    syncAdvertisedInterfaces();

    LOG_INFO("Connected over USB serial.");
    return true;
}

// Close serial transport and leave properties to be hidden by updateProperties().
bool OpenSmartFocuser::Disconnect()
{
    closeSerialPort();
    hasShutterAddon = false;
    hasFlatPanelAddon = false;
    updateAddonInterfaces();
    syncAdvertisedInterfaces();
    LOG_INFO("Disconnected USB serial.");
    return true;
}

// Absolute move handler from INDI clients.
// Sends :MA<target># and updates cached/displayed position on success.
IPState OpenSmartFocuser::MoveAbsFocuser(uint32_t targetTicks)
{
    if (!commandAck(":MA", std::to_string(targetTicks)))
        return IPS_ALERT;

    cachedPosition = targetTicks;
    FocusAbsPosNP[0].setValue(cachedPosition);
    FocusAbsPosNP.setState(IPS_OK);
    FocusAbsPosNP.apply();
    return IPS_OK;
}

// Relative move handler from INDI clients.
// Converts IN/OUT direction to signed delta and sends :MR<delta>#.
IPState OpenSmartFocuser::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    const int32_t signedDelta = (dir == FOCUS_INWARD ? -1 : 1) * static_cast<int32_t>(ticks);
    if (!commandAck(":MR", std::to_string(signedDelta)))
        return IPS_ALERT;

    uint32_t position = cachedPosition;
    if (queryPosition(position))
        cachedPosition = position;

    FocusAbsPosNP[0].setValue(cachedPosition);
    FocusAbsPosNP.setState(IPS_OK);
    FocusAbsPosNP.apply();
    return IPS_OK;
}

// Abort current motion via :MH#.
bool OpenSmartFocuser::AbortFocuser()
{
    return commandAck(":MH", "");
}

bool OpenSmartFocuser::ISSnoopDevice(XMLEle *root)
{
    return INDI::LightBoxInterface::snoop(root) || INDI::Focuser::ISSnoopDevice(root);
}

bool OpenSmartFocuser::saveConfigItems(FILE *fp)
{
    INDI::LightBoxInterface::saveConfigItems(fp);
    return INDI::Focuser::saveConfigItems(fp);
}

IPState OpenSmartFocuser::ParkCap()
{
    if (!hasShutterAddon)
        return IPS_ALERT;

    return commandAck(":SV", std::to_string(SHUTTER_OFF)) ? IPS_OK : IPS_ALERT;
}

IPState OpenSmartFocuser::UnParkCap()
{
    if (!hasShutterAddon)
        return IPS_ALERT;

    return commandAck(":SV", std::to_string(SHUTTER_ON)) ? IPS_OK : IPS_ALERT;
}

IPState OpenSmartFocuser::AbortCap()
{
    return IPS_OK;
}

bool OpenSmartFocuser::SetLightBoxBrightness(uint16_t value)
{
    if (!hasFlatPanelAddon || value > static_cast<uint16_t>(FLAT_PANEL_MAX))
        return false;

    const bool ok = commandAck(":FP", std::to_string(value));
    if (ok)
    {
        FlatPanelBrightnessNP[0].setValue(value);
        FlatPanelBrightnessNP.setState(IPS_OK);
        FlatPanelBrightnessNP.apply();
    }

    return ok;
}

bool OpenSmartFocuser::EnableLightBox(bool enable)
{
    if (!hasFlatPanelAddon)
        return false;

    const uint32_t value = enable ? FLAT_PANEL_ON : FLAT_PANEL_OFF;
    return commandAck(":FP", std::to_string(value));
}

// Dispatch editable number properties for shutter angle and flat panel brightness.
bool OpenSmartFocuser::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (INDI::LightBoxInterface::processNumber(dev, name, values, names, n))
        return true;

    if (dev == nullptr || std::strcmp(dev, getDeviceName()) != 0)
        return INDI::Focuser::ISNewNumber(dev, name, values, names, n);

    if (ShutterPositionNP.isNameMatch(name))
    {
        ShutterPositionNP.update(values, names, n);

        const uint32_t shutterPosition = static_cast<uint32_t>(ShutterPositionNP[0].getValue());
        const bool ok = shutterPosition <= static_cast<uint32_t>(SHUTTER_MAX) &&
                        commandAck(":SV", std::to_string(shutterPosition));

        if (!ok)
            ShutterPositionNP[0].setValue(ShutterPositionNP[0].getMin());

        ShutterPositionNP.setState(ok ? IPS_OK : IPS_ALERT);
        ShutterPositionNP.apply();
        return true;
    }

    if (FlatPanelBrightnessNP.isNameMatch(name))
    {
        FlatPanelBrightnessNP.update(values, names, n);

        const uint32_t brightness = static_cast<uint32_t>(FlatPanelBrightnessNP[0].getValue());
        const bool ok = brightness <= static_cast<uint32_t>(FLAT_PANEL_MAX) &&
                        commandAck(":FP", std::to_string(brightness));

        if (!ok)
            FlatPanelBrightnessNP[0].setValue(FlatPanelBrightnessNP[0].getMin());

        FlatPanelBrightnessNP.setState(ok ? IPS_OK : IPS_ALERT);
        FlatPanelBrightnessNP.apply();
        return true;
    }

    return INDI::Focuser::ISNewNumber(dev, name, values, names, n);
}

// Dispatch all custom switch properties (motor, home, speed, reboot, raw send, monitor clear).
bool OpenSmartFocuser::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (INDI::DustCapInterface::processSwitch(dev, name, states, names, n))
        return true;

    if (INDI::LightBoxInterface::processSwitch(dev, name, states, names, n))
        return true;

    if (dev == nullptr || std::strcmp(dev, getDeviceName()) != 0)
        return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);

    if (MotorControlSP.isNameMatch(name))
    {
        // Motor power/state control: :EM# enable, :DM# disable.
        MotorControlSP.update(states, names, n);

        bool ok = false;
        if (MotorControlSP[0].getState() == ISS_ON)
            ok = commandAck(":EM", "");
        else if (MotorControlSP[1].getState() == ISS_ON)
            ok = commandAck(":DM", "");

        MotorControlSP.reset();
        MotorControlSP.setState(ok ? IPS_OK : IPS_ALERT);
        MotorControlSP.apply();
        return true;
    }

    if (HomeSP.isNameMatch(name))
    {
        // Start firmware homing routine.
        HomeSP.update(states, names, n);
        const bool ok = HomeSP[0].getState() == ISS_ON ? commandAck(":HM", "") : false;

        HomeSP.reset();
        HomeSP.setState(ok ? IPS_OK : IPS_ALERT);
        HomeSP.apply();
        return true;
    }

    if (ShutterPresetSP.isNameMatch(name))
    {
        ShutterPresetSP.update(states, names, n);

        bool ok = false;
        uint32_t shutterPosition = SHUTTER_OFF;
        if (ShutterPresetSP[0].getState() == ISS_ON)
        {
            shutterPosition = SHUTTER_ON;
            ok = commandAck(":SV", std::to_string(shutterPosition));
        }
        else if (ShutterPresetSP[1].getState() == ISS_ON)
        {
            ok = commandAck(":SV", std::to_string(shutterPosition));
        }

        if (ok)
            ShutterPositionNP[0].setValue(shutterPosition);

        ShutterPresetSP.reset();
        ShutterPresetSP.setState(ok ? IPS_OK : IPS_ALERT);
        ShutterPresetSP.apply();

        ShutterPositionNP.setState(ok ? IPS_OK : IPS_ALERT);
        ShutterPositionNP.apply();
        return true;
    }

    if (FlatPanelPresetSP.isNameMatch(name))
    {
        FlatPanelPresetSP.update(states, names, n);

        bool ok = false;
        uint32_t brightness = FLAT_PANEL_OFF;
        if (FlatPanelPresetSP[0].getState() == ISS_ON)
        {
            brightness = FLAT_PANEL_ON;
            ok = commandAck(":FP", std::to_string(brightness));
        }
        else if (FlatPanelPresetSP[1].getState() == ISS_ON)
        {
            ok = commandAck(":FP", std::to_string(brightness));
        }

        if (ok)
            FlatPanelBrightnessNP[0].setValue(brightness);

        FlatPanelPresetSP.reset();
        FlatPanelPresetSP.setState(ok ? IPS_OK : IPS_ALERT);
        FlatPanelPresetSP.apply();

        FlatPanelBrightnessNP.setState(ok ? IPS_OK : IPS_ALERT);
        FlatPanelBrightnessNP.apply();
        return true;
    }

    if (SpeedPresetSP.isNameMatch(name))
    {
        // Map selected speed radio button to firmware speed command :MS<0..4>#.
        SpeedPresetSP.update(states, names, n);

        int selectedIndex = -1;
        for (int i = 0; i < 5; ++i)
        {
            if (SpeedPresetSP[i].getState() == ISS_ON)
            {
                selectedIndex = i;
                break;
            }
        }

        const bool ok = selectedIndex >= 0 && commandAck(":MS", std::to_string(selectedIndex));
        if (ok)
            updateSpeedSelection(static_cast<uint32_t>(selectedIndex));
        else
            SpeedPresetSP.reset();

        SpeedPresetSP.setState(ok ? IPS_OK : IPS_ALERT);
        SpeedPresetSP.apply();
        return true;
    }

    if (RebootSP.isNameMatch(name))
    {
        // Trigger firmware reboot command.
        RebootSP.update(states, names, n);
        const bool ok = RebootSP[0].getState() == ISS_ON ? commandAck(":RB", "") : false;

        RebootSP.reset();
        RebootSP.setState(ok ? IPS_OK : IPS_ALERT);
        RebootSP.apply();
        return true;
    }

    if (RawSendSP.isNameMatch(name))
    {
        // Manual raw-frame send for debugging protocol behavior from INDI UI.
        RawSendSP.update(states, names, n);

        bool ok = false;
        if (RawSendSP[0].getState() == ISS_ON)
        {
            std::string response;
            const std::string commandText = RawCommandTP[0].getText();
            ok = sendFrame(commandText) && readFrame(response, SERIAL_TIMEOUT_MS);
            publishRawOutput(ok ? response : "<no response>");
        }

        RawSendSP.reset();
        RawSendSP.setState(ok ? IPS_OK : IPS_ALERT);
        RawSendSP.apply();
        return true;
    }

    return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);
}

// Dispatch editable text properties (port path and manual raw command text).
bool OpenSmartFocuser::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (INDI::LightBoxInterface::processText(dev, name, texts, names, n))
        return true;

    if (dev == nullptr || std::strcmp(dev, getDeviceName()) != 0)
        return INDI::Focuser::ISNewText(dev, name, texts, names, n);

    if (UsbPortTP.isNameMatch(name))
    {
        UsbPortTP.update(texts, names, n);
        UsbPortTP.setState(IPS_OK);
        UsbPortTP.apply();
        return true;
    }

    if (RawCommandTP.isNameMatch(name))
    {
        RawCommandTP.update(texts, names, n);
        RawCommandTP.setState(IPS_OK);
        RawCommandTP.apply();
        return true;
    }

    return INDI::Focuser::ISNewText(dev, name, texts, names, n);
}

// Declare all custom properties and their tabs/options.
void OpenSmartFocuser::initCustomProperties()
{
    UsbPortTP[0].fill("PORT", "USB Port", "/dev/OSF");
    UsbPortTP.fill(getDeviceName(), "USB_SERIAL_PORT", "USB Serial", MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    ShutterPositionNP[0].fill("SHUTTER_POSITION", "Shutter", "%3.0f", SHUTTER_MIN, SHUTTER_MAX, 1, SHUTTER_OFF);
    ShutterPositionNP.fill(getDeviceName(), "SHUTTER_POSITION", "Shutter Position", CUSTOM_TAB, IP_RW, 60, IPS_IDLE);

    FlatPanelBrightnessNP[0].fill("FLAT_PANEL_BRIGHTNESS", "Brightness", "%3.0f", FLAT_PANEL_MIN, FLAT_PANEL_MAX, 1,
                                  FLAT_PANEL_OFF);
    FlatPanelBrightnessNP.fill(getDeviceName(), "FLAT_PANEL_BRIGHTNESS", "Flat Panel", CUSTOM_TAB, IP_RW, 60, IPS_IDLE);

    SpeedPresetSP[0].fill("SPEED_FINE", "Fine", ISS_OFF);
    SpeedPresetSP[1].fill("SPEED_SLOW", "Slow", ISS_ON);
    SpeedPresetSP[2].fill("SPEED_MED", "Medium", ISS_OFF);
    SpeedPresetSP[3].fill("SPEED_FAST", "Fast", ISS_OFF);
    SpeedPresetSP[4].fill("SPEED_MAX", "Max", ISS_OFF);
    SpeedPresetSP.fill(getDeviceName(), "SPEED_PRESET", "Speed", CUSTOM_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    MotorControlSP[0].fill("ENABLE_MOTOR", "Enable", ISS_OFF);
    MotorControlSP[1].fill("DISABLE_MOTOR", "Disable", ISS_OFF);
    MotorControlSP.fill(getDeviceName(), "MOTOR_CONTROL", "Motor", CUSTOM_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

    ShutterPresetSP[0].fill("SHUTTER_OPEN", "On", ISS_OFF);
    ShutterPresetSP[1].fill("SHUTTER_CLOSE", "Off", ISS_OFF);
    ShutterPresetSP.fill(getDeviceName(), "SHUTTER_PRESET", "Shutter Preset", CUSTOM_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

    FlatPanelPresetSP[0].fill("FLAT_PANEL_ON", "On", ISS_OFF);
    FlatPanelPresetSP[1].fill("FLAT_PANEL_OFF", "Off", ISS_OFF);
    FlatPanelPresetSP.fill(getDeviceName(), "FLAT_PANEL_PRESET", "Flat Panel Preset", CUSTOM_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

    HomeSP[0].fill("START_HOME", "Home", ISS_OFF);
    HomeSP.fill(getDeviceName(), "HOME_CONTROL", "Home", CUSTOM_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

    RebootSP[0].fill("REBOOT", "Reboot", ISS_OFF);
    RebootSP.fill(getDeviceName(), "DEVICE_REBOOT", "Reboot", CUSTOM_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

    RawCommandTP[0].fill("COMMAND", "Raw Command", ":GP#");
    RawCommandTP.fill(getDeviceName(), "RAW_COMMAND", "Raw Command", CUSTOM_TAB, IP_RW, 60, IPS_IDLE);

    RawSendSP[0].fill("SEND", "Send", ISS_OFF);
    RawSendSP.fill(getDeviceName(), "RAW_SEND", "Send Raw", CUSTOM_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

    RawOutputTP[0].fill("OUTPUT", "Raw Output", "");
    RawOutputTP.fill(getDeviceName(), "RAW_OUTPUT", "Raw Output", CUSTOM_TAB, IP_RO, 60, IPS_IDLE);

}

// Define custom properties only while connected, except USB port which stays visible.
void OpenSmartFocuser::updateCustomPropertyVisibility()
{
    if (isConnected())
    {
        defineProperty(UsbPortTP);
        if (hasShutterAddon)
            defineProperty(ShutterPositionNP);
        if (hasFlatPanelAddon)
            defineProperty(FlatPanelBrightnessNP);
        defineProperty(SpeedPresetSP);
        defineProperty(MotorControlSP);
        if (hasShutterAddon)
            defineProperty(ShutterPresetSP);
        if (hasFlatPanelAddon)
            defineProperty(FlatPanelPresetSP);
        defineProperty(HomeSP);
        defineProperty(RebootSP);
        defineProperty(RawCommandTP);
        defineProperty(RawSendSP);
        defineProperty(RawOutputTP);
        return;
    }

    deleteProperty(SpeedPresetSP.getName());
    deleteProperty(MotorControlSP.getName());
    deleteProperty(ShutterPositionNP.getName());
    deleteProperty(FlatPanelBrightnessNP.getName());
    deleteProperty(ShutterPresetSP.getName());
    deleteProperty(FlatPanelPresetSP.getName());
    deleteProperty(HomeSP.getName());
    deleteProperty(RebootSP.getName());
    deleteProperty(RawCommandTP.getName());
    deleteProperty(RawSendSP.getName());
    deleteProperty(RawOutputTP.getName());
    defineProperty(UsbPortTP);
}

// Open/configure POSIX serial device with 115200 8N1 raw mode.
bool OpenSmartFocuser::openSerialPort()
{
    closeSerialPort();

    const std::string portPath = UsbPortTP[0].getText();
    serialFD = ::open(portPath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serialFD < 0)
    {
        LOGF_ERROR("Failed to open serial port %s: %s", portPath.c_str(), std::strerror(errno));
        return false;
    }

    termios tty {};
    if (tcgetattr(serialFD, &tty) != 0)
    {
        LOGF_ERROR("tcgetattr failed on %s: %s", portPath.c_str(), std::strerror(errno));
        closeSerialPort();
        return false;
    }

    cfsetispeed(&tty, SERIAL_BAUD);
    cfsetospeed(&tty, SERIAL_BAUD);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(serialFD, TCSANOW, &tty) != 0)
    {
        LOGF_ERROR("tcsetattr failed on %s: %s", portPath.c_str(), std::strerror(errno));
        closeSerialPort();
        return false;
    }

    const int flags = fcntl(serialFD, F_GETFL, 0);
    if (flags >= 0)
        fcntl(serialFD, F_SETFL, flags & ~O_NONBLOCK);

    tcflush(serialFD, TCIOFLUSH);
    return true;
}

// Close serial descriptor if currently open.
void OpenSmartFocuser::closeSerialPort()
{
    if (serialFD >= 0)
    {
        ::close(serialFD);
        serialFD = -1;
    }
}

// Write one protocol frame to firmware and mirror TX data into monitor/log.
bool OpenSmartFocuser::sendFrame(const std::string &frame)
{
    if (serialFD < 0 || frame.empty())
        return false;

    const std::string escapedFrame = escapeFrameForLog(frame);
    appendSerialMonitorLine("TX", escapedFrame);

    const ssize_t written = ::write(serialFD, frame.c_str(), frame.size());
    if (written != static_cast<ssize_t>(frame.size()))
    {
        LOGF_ERROR("Failed to write frame '%s'", frame.c_str());
        return false;
    }

    return true;
}

// Read one command response frame while also consuming async debug frames.
// Command frames are :...# and debug frames are !...*.
bool OpenSmartFocuser::readFrame(std::string &frame, int timeoutMs)
{
    frame.clear();
    if (serialFD < 0)
        return false;

    enum class ParseState
    {
        Idle,
        Command,
        Debug
    };

    ParseState state = ParseState::Idle;
    std::string collected;

    while (true)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serialFD, &readfds);

        timeval timeout {};
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;

        const int ready = select(serialFD + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0)
            return false;

        char ch = '\0';
        const ssize_t bytes = ::read(serialFD, &ch, 1);
        if (bytes != 1)
            continue;

        if (state == ParseState::Idle)
        {
            // Wait for explicit frame start markers.
            if (ch == ':')
            {
                state = ParseState::Command;
                collected.clear();
                collected.push_back(ch);
            }
            else if (ch == '!')
            {
                state = ParseState::Debug;
                collected.clear();
                collected.push_back(ch);
            }
            continue;
        }

        if (state == ParseState::Debug)
        {
            // Consume firmware debug stream and publish it immediately.
            collected.push_back(ch);
            if (ch == '*')
            {
                const std::string escapedDebugFrame = escapeFrameForLog(collected);
                appendSerialMonitorLine("DBG", escapedDebugFrame);
                // Publish debug stream lines for diagnostics, but keep waiting
                // for a real command response frame.
                publishRawOutput(collected);
                collected.clear();
                state = ParseState::Idle;
            }

            if (collected.size() > 512)
            {
                collected.clear();
                state = ParseState::Idle;
            }
            continue;
        }

        // ParseState::Command
        // Collect protocol reply until terminating '#'.
        collected.push_back(ch);
        if (ch == '#')
        {
            frame = collected;
            const std::string escapedFrame = escapeFrameForLog(frame);
            appendSerialMonitorLine("RX", escapedFrame);
            return true;
        }

        if (collected.size() > 256)
        {
            collected.clear();
            state = ParseState::Idle;
        }
    }
}

// Send command token/payload as :<token><payload># and wait for one reply frame.
bool OpenSmartFocuser::sendCommand(const std::string &token, const std::string &payload, std::string &response)
{
    const std::string frame = token + payload + "#";
    if (!sendFrame(frame))
        return false;

    do
    {
        if (!readFrame(response, SERIAL_TIMEOUT_MS))
            return false;

        if (response == ":HD#")
        {
            int32_t minSteps = 0;
            int32_t maxSteps = 0;
            if (queryLimits(minSteps, maxSteps))
            {
                applyLimits(minSteps, maxSteps);
            }
            else
            {
                LOG_WARN("Received :HD# but failed to refresh limits via :GL#.");
            }

            publishRawOutput(response);
        }
    }
    while (response == ":HD#");

    publishRawOutput(response);
    return true;
}

// Helper for commands that are expected to return :ACK#.
bool OpenSmartFocuser::commandAck(const std::string &token, const std::string &payload)
{
    std::string response;
    if (!sendCommand(token, payload, response))
        return false;

    return response == ":ACK#";
}

// Query current absolute position via :GP# and parse :GP<ticks>#.
bool OpenSmartFocuser::queryPosition(uint32_t &position)
{
    std::string response;
    if (!sendCommand(":GP", "", response))
        return false;

    if (!startsWith(response, ":GP") || response.size() < 5 || response.back() != '#')
        return false;

    const std::string payload = response.substr(3, response.size() - 4);
    uint32_t parsed = 0;
    if (!parsePositiveInteger(payload, parsed))
        return false;

    position = parsed;
    return true;
}

// Query current speed preset index via :GS# and parse :GS<index>#.
bool OpenSmartFocuser::querySpeedIndex(uint32_t &speedIndex)
{
    std::string response;
    if (!sendCommand(":GS", "", response))
        return false;

    if (!startsWith(response, ":GS") || response.size() < 5 || response.back() != '#')
        return false;

    const std::string payload = response.substr(3, response.size() - 4);
    uint32_t parsed = 0;
    if (!parsePositiveInteger(payload, parsed) || parsed > MAX_SPEED_INDEX)
        return false;

    speedIndex = parsed;
    return true;
}

// Query soft limits via :GL# and parse :GL<min>,<max>#.
bool OpenSmartFocuser::queryLimits(int32_t &minSteps, int32_t &maxSteps)
{
    std::string response;
    if (!sendFrame(":GL#"))
        return false;

    do
    {
        if (!readFrame(response, SERIAL_TIMEOUT_MS))
            return false;
    }
    while (response == ":HD#");

    publishRawOutput(response);

    if (response == ":ER03#")
        return false;

    if (!startsWith(response, ":GL") || response.size() < 7 || response.back() != '#')
        return false;

    const std::string payload = response.substr(3, response.size() - 4);
    const size_t comma = payload.find(',');
    if (comma == std::string::npos)
        return false;

    const std::string minText = payload.substr(0, comma);
    const std::string maxText = payload.substr(comma + 1);

    int32_t parsedMin = 0;
    int32_t parsedMax = 0;
    if (!parseSignedInteger32(minText, parsedMin) || !parseSignedInteger32(maxText, parsedMax))
        return false;
    if (parsedMin > parsedMax)
        return false;

    minSteps = parsedMin;
    maxSteps = parsedMax;
    return true;
}

bool OpenSmartFocuser::queryAddons(bool &hasShutter, bool &hasFlatPanel)
{
    hasShutter = false;
    hasFlatPanel = false;

    if (!sendFrame(":AQ#"))
        return false;

    bool receivedAny = false;
    for (int i = 0; i < 4; ++i)
    {
        std::string response;
        if (!readFrame(response, 250))
            break;

        receivedAny = true;
        publishRawOutput(response);

        if (response == ":AQShutter#")
            hasShutter = true;
        else if (response == ":AQFlatPanel#")
            hasFlatPanel = true;
        else if (response == ":AQNONE#" || response == ":AQ!#")
            break;
    }

    return receivedAny;
}

void OpenSmartFocuser::updateAddonInterfaces()
{
    if (isConnected() && hasShutterAddon)
        INDI::DustCapInterface::updateProperties();
    else
        deleteProperty("CAP_PARK");

    if (isConnected() && hasFlatPanelAddon)
        INDI::LightBoxInterface::updateProperties();
    else
    {
        deleteProperty("FLAT_LIGHT_CONTROL");
        deleteProperty("FLAT_LIGHT_INTENSITY");
        deleteProperty("ACTIVE_DEVICES");
        deleteProperty("ACTIVE_FILTER");
    }
}

void OpenSmartFocuser::syncAdvertisedInterfaces()
{
    uint32_t interfaces = INDI::BaseDevice::FOCUSER_INTERFACE;

    if (hasShutterAddon)
        interfaces |= INDI::BaseDevice::DUSTCAP_INTERFACE;

    if (hasFlatPanelAddon)
        interfaces |= INDI::BaseDevice::LIGHTBOX_INTERFACE;

    if (hasShutterAddon || hasFlatPanelAddon)
        interfaces |= INDI::BaseDevice::AUX_INTERFACE;

    setDriverInterface(interfaces);
    syncDriverInfo();
}

void OpenSmartFocuser::applyLimits(int32_t minSteps, int32_t maxSteps)
{
    const int32_t clampedMax = std::min(maxSteps, INDI_CLIENT_SAFE_MAX_STEPS);
    const int32_t clampedMin = std::min(minSteps, clampedMax);

    cachedMinSteps = clampedMin;
    cachedMaxSteps = clampedMax;

    FocusAbsPosNP[0].setMin(static_cast<double>(cachedMinSteps));
    FocusAbsPosNP[0].setMax(static_cast<double>(cachedMaxSteps));
    FocusAbsPosNP[0].setStep(1.0);
    FocusAbsPosNP.updateMinMax();

    FocusMaxPosNP[0].setMin(static_cast<double>(cachedMinSteps));
    FocusMaxPosNP[0].setMax(static_cast<double>(cachedMaxSteps));
    FocusMaxPosNP[0].setStep(1.0);
    FocusMaxPosNP[0].setValue(static_cast<double>(cachedMaxSteps));
    FocusMaxPosNP.updateMinMax();

    FocusRelPosNP[0].setMin(0.0);
    FocusRelPosNP[0].setMax(static_cast<double>(cachedMaxSteps - cachedMinSteps));
    FocusRelPosNP[0].setStep(1.0);
    FocusRelPosNP[0].setValue(100.0);
    FocusRelPosNP.updateMinMax();

    const uint32_t clampedPosition = static_cast<uint32_t>(std::clamp<int64_t>(
        static_cast<int64_t>(cachedPosition),
        static_cast<int64_t>(cachedMinSteps),
        static_cast<int64_t>(cachedMaxSteps)));
    cachedPosition = clampedPosition;
    FocusAbsPosNP[0].setValue(static_cast<double>(cachedPosition));
    FocusAbsPosNP.setState(IPS_OK);
    FocusAbsPosNP.apply();

    FocusMaxPosNP.setState(IPS_OK);
    FocusMaxPosNP.apply();
    FocusRelPosNP.setState(IPS_OK);
    FocusRelPosNP.apply();

    LOGF_INFO("Applied firmware limits: min=%d max=%d", cachedMinSteps, cachedMaxSteps);
}

// Update speed selector radio switch to match current firmware speed index.
void OpenSmartFocuser::updateSpeedSelection(uint32_t speedIndex)
{
    if (speedIndex > MAX_SPEED_INDEX)
        return;

    SpeedPresetSP.reset();
    SpeedPresetSP[speedIndex].setState(ISS_ON);
    SpeedPresetSP.setState(IPS_OK);
    SpeedPresetSP.apply();
}

// Mirror one line to INDI log output.
void OpenSmartFocuser::appendSerialMonitorLine(const std::string &prefix, const std::string &payload)
{
    const std::string line = prefix + " len=" + std::to_string(payload.size()) + " data=" + payload;
    LOGF_INFO("%s", line.c_str());
}

// Publish single latest raw output line for quick diagnostics in Custom tab.
void OpenSmartFocuser::publishRawOutput(const std::string &output)
{
    RawOutputTP[0].setText(output.c_str());
    RawOutputTP.setState(IPS_OK);
    RawOutputTP.apply();
}
