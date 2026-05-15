#include "config.h"
#include "indi_opensmartfocuser_focuser.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
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

constexpr const char *CUSTOM_TAB = "Custom";
constexpr const char *MONITOR_TAB = "Serial Monitor";
constexpr int SERIAL_BAUD = B115200;
constexpr int SERIAL_TIMEOUT_MS = 1500;
constexpr uint32_t MAX_SPEED_INDEX = 4;
constexpr size_t SERIAL_MONITOR_MAX_LINES = 80;

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

bool startsWith(const std::string &text, const std::string &prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

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

OpenSmartFocuser::OpenSmartFocuser()
{
    FI::SetCapability(FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_CAN_ABORT);
    setSupportedConnections(CONNECTION_NONE);
    setVersion(CDRIVER_VERSION_MAJOR, CDRIVER_VERSION_MINOR);
}

const char *OpenSmartFocuser::getDefaultName()
{
    return "OpenSmartFocuser";
}

bool OpenSmartFocuser::initProperties()
{
    INDI::Focuser::initProperties();
    initCustomProperties();
    defineProperty(UsbPortTP);

    cachedPosition = static_cast<uint32_t>(FocusAbsPosNP[0].getValue());
    LOG_INFO("Initialized minimal OpenSmartFocuser USB serial driver.");
    return true;
}

bool OpenSmartFocuser::updateProperties()
{
    INDI::Focuser::updateProperties();
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

bool OpenSmartFocuser::Connect()
{
    if (!openSerialPort())
        return false;

    clearSerialMonitor();

    std::string response;
    if (!sendCommand(":PP", "", response) || response != ":PP#")
    {
        LOG_ERROR("Connect failed: heartbeat did not return :PP#.");
        closeSerialPort();
        return false;
    }

    publishRawOutput(response);

    uint32_t speedIndex = 0;
    if (querySpeedIndex(speedIndex))
        updateSpeedSelection(speedIndex);

    LOG_INFO("Connected over USB serial.");
    return true;
}

bool OpenSmartFocuser::Disconnect()
{
    closeSerialPort();
    LOG_INFO("Disconnected USB serial.");
    return true;
}

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

bool OpenSmartFocuser::AbortFocuser()
{
    return commandAck(":MH", "");
}

bool OpenSmartFocuser::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (dev == nullptr || std::strcmp(dev, getDeviceName()) != 0)
        return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);

    if (MotorControlSP.isNameMatch(name))
    {
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
        HomeSP.update(states, names, n);
        const bool ok = HomeSP[0].getState() == ISS_ON ? commandAck(":HM", "") : false;

        HomeSP.reset();
        HomeSP.setState(ok ? IPS_OK : IPS_ALERT);
        HomeSP.apply();
        return true;
    }

    if (SpeedPresetSP.isNameMatch(name))
    {
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
        RebootSP.update(states, names, n);
        const bool ok = RebootSP[0].getState() == ISS_ON ? commandAck(":RB", "") : false;

        RebootSP.reset();
        RebootSP.setState(ok ? IPS_OK : IPS_ALERT);
        RebootSP.apply();
        return true;
    }

    if (RawSendSP.isNameMatch(name))
    {
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

    if (SerialMonitorClearSP.isNameMatch(name))
    {
        SerialMonitorClearSP.update(states, names, n);
        const bool clearRequested = SerialMonitorClearSP[0].getState() == ISS_ON;
        if (clearRequested)
            clearSerialMonitor();

        SerialMonitorClearSP.reset();
        SerialMonitorClearSP.setState(clearRequested ? IPS_OK : IPS_IDLE);
        SerialMonitorClearSP.apply();
        return true;
    }

    return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);
}

bool OpenSmartFocuser::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
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

void OpenSmartFocuser::initCustomProperties()
{
    UsbPortTP[0].fill("PORT", "USB Port", "/dev/ttyACM0");
    UsbPortTP.fill(getDeviceName(), "USB_SERIAL_PORT", "USB Serial", MAIN_CONTROL_TAB, IP_RW, 60, IPS_IDLE);

    SpeedPresetSP[0].fill("SPEED_FINE", "Fine", ISS_OFF);
    SpeedPresetSP[1].fill("SPEED_SLOW", "Slow", ISS_ON);
    SpeedPresetSP[2].fill("SPEED_MED", "Medium", ISS_OFF);
    SpeedPresetSP[3].fill("SPEED_FAST", "Fast", ISS_OFF);
    SpeedPresetSP[4].fill("SPEED_MAX", "Max", ISS_OFF);
    SpeedPresetSP.fill(getDeviceName(), "SPEED_PRESET", "Speed", CUSTOM_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    MotorControlSP[0].fill("ENABLE_MOTOR", "Enable", ISS_OFF);
    MotorControlSP[1].fill("DISABLE_MOTOR", "Disable", ISS_OFF);
    MotorControlSP.fill(getDeviceName(), "MOTOR_CONTROL", "Motor", CUSTOM_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);

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

    SerialMonitorTP[0].fill("MONITOR", "Monitor", "");
    SerialMonitorTP.fill(getDeviceName(), "SERIAL_MONITOR", "Serial Monitor", MONITOR_TAB, IP_RO, 60, IPS_IDLE);

    SerialMonitorClearSP[0].fill("CLEAR", "Clear", ISS_OFF);
    SerialMonitorClearSP.fill(getDeviceName(), "SERIAL_MONITOR_CLEAR", "Serial Monitor", MONITOR_TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);
}

void OpenSmartFocuser::updateCustomPropertyVisibility()
{
    if (isConnected())
    {
        defineProperty(UsbPortTP);
        defineProperty(SpeedPresetSP);
        defineProperty(MotorControlSP);
        defineProperty(HomeSP);
        defineProperty(RebootSP);
        defineProperty(RawCommandTP);
        defineProperty(RawSendSP);
        defineProperty(RawOutputTP);
        defineProperty(SerialMonitorTP);
        defineProperty(SerialMonitorClearSP);
        return;
    }

    deleteProperty(SpeedPresetSP.getName());
    deleteProperty(MotorControlSP.getName());
    deleteProperty(HomeSP.getName());
    deleteProperty(RebootSP.getName());
    deleteProperty(RawCommandTP.getName());
    deleteProperty(RawSendSP.getName());
    deleteProperty(RawOutputTP.getName());
    deleteProperty(SerialMonitorTP.getName());
    deleteProperty(SerialMonitorClearSP.getName());
    defineProperty(UsbPortTP);
}

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

void OpenSmartFocuser::closeSerialPort()
{
    if (serialFD >= 0)
    {
        ::close(serialFD);
        serialFD = -1;
    }
}

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

bool OpenSmartFocuser::sendCommand(const std::string &token, const std::string &payload, std::string &response)
{
    const std::string frame = token + payload + "#";
    if (!sendFrame(frame))
        return false;

    if (!readFrame(response, SERIAL_TIMEOUT_MS))
        return false;

    publishRawOutput(response);
    return true;
}

bool OpenSmartFocuser::commandAck(const std::string &token, const std::string &payload)
{
    std::string response;
    if (!sendCommand(token, payload, response))
        return false;

    return response == ":ACK#";
}

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

void OpenSmartFocuser::updateSpeedSelection(uint32_t speedIndex)
{
    if (speedIndex > MAX_SPEED_INDEX)
        return;

    SpeedPresetSP.reset();
    SpeedPresetSP[speedIndex].setState(ISS_ON);
    SpeedPresetSP.setState(IPS_OK);
    SpeedPresetSP.apply();
}

void OpenSmartFocuser::appendSerialMonitorLine(const std::string &prefix, const std::string &payload)
{
    const std::string line = prefix + " len=" + std::to_string(payload.size()) + " data=" + payload;
    LOGF_INFO("%s", line.c_str());

    serialMonitorLines.push_back(line);
    while (serialMonitorLines.size() > SERIAL_MONITOR_MAX_LINES)
        serialMonitorLines.pop_front();

    std::string joined;
    for (const auto &entry : serialMonitorLines)
    {
        joined += entry;
        joined += '\n';
    }

    SerialMonitorTP[0].setText(joined.c_str());
    SerialMonitorTP.setState(IPS_OK);
    SerialMonitorTP.apply();
}

void OpenSmartFocuser::clearSerialMonitor()
{
    serialMonitorLines.clear();
    SerialMonitorTP[0].setText("");
    SerialMonitorTP.setState(IPS_OK);
    SerialMonitorTP.apply();
}

void OpenSmartFocuser::publishRawOutput(const std::string &output)
{
    RawOutputTP[0].setText(output.c_str());
    RawOutputTP.setState(IPS_OK);
    RawOutputTP.apply();
}
