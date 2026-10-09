#include <cstring>
#include <string>
#include <variant>
#include <cstdint>
#include <vector>
#include "OSFocuser_device.h"
#include "libindi/indicom.h"
#include <connectionplugins/connectionserial.h>
#include "config.h"

#include <memory>

static std::unique_ptr<OSFocuser> osFocuser(new OSFocuser());

OSFocuser::OSFocuser()
{
    setVersion(CDRIVER_VERSION_MAJOR, CDRIVER_VERSION_MINOR);
    setSupportedConnections(CONNECTION_SERIAL);
    SetCapability(FOCUSER_CAN_ABORT | FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE);
    serialConnection = new Connection::Serial(this);
    serialConnection->setDefaultBaudRate(Connection::Serial::B_115200);
    serialConnection->setDefaultPort("/dev/OSF");
    serialConnection->registerHandshake([&]() {return Handshake();});
    registerConnection(serialConnection);
    
}

const char *OSFocuser::getDefaultName()
{
    return "Jaytek OpenSmartFocuser";
}

void OSFocuser::ISGetProperties(const char *dev)
{
    INDI::Focuser::ISGetProperties(dev);
}

bool OSFocuser::updateProperties()
{
    INDI::Focuser::updateProperties();
    if (isConnected())
    {
        
    }
    else
    {

    }
    return true;
}

bool OSFocuser::initProperties()
{
    return INDI::Focuser::initProperties();
}

void OSFocuser::TimerHit()
{
    if (!isConnected())
        return;
    LOG_DEBUG("Polling focuser status");
    std::string status;
    // Send command to get focuser status
    SetTimer(getCurrentPollingPeriod());
}

bool OSFocuser::Handshake()
{
    LOG_INFO("Performing handshake with the focuser hardware");
    // Perform handshake with the focuser hardware
    if (!serialConnection) {
        LOG_ERROR("Serial connection not initialized");
        return false;
    }
    protocol = new OSFprotocol(serialConnection->getPortFD());
    // Handshake implementation goes here
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return false;
    }
    // Example handshake command
    auto response = protocol->sendCommand(":PP#");
    if (const auto *commandFrame = std::get_if<std::string>(&response.response))
    {
        LOGF_DEBUG("Command frame: %s", commandFrame->c_str());
        LOGF_INFO("Command frame: %s", commandFrame->c_str());
    }
    else
    {
        LOG_ERROR("Failed to get command frame");
    }
    for (const auto &frame : response.debugFrames)
    {
        LOGF_DEBUG("Debug frame: %s", frame.c_str());
        LOGF_INFO("Debug frame: %s", frame.c_str());
    }
        

    //LOG_ERROR("Handshake failed");
    return true;
}

IPState OSFocuser::MoveFocuser(FocusDirection dir, int speed, uint16_t duration)
{
    LOGF_INFO("Moving focuser %s at speed %d for duration %u ms", dir == FOCUS_INWARD ? "in" : "out", speed, duration);
    return IPS_OK;
}

IPState OSFocuser::MoveAbsFocuser(uint32_t targetTicks)
{
    LOGF_INFO("Moving absolute focuser to %u ticks", targetTicks);
    return IPS_OK;
}

IPState OSFocuser::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    LOGF_INFO("Moving relative focuser by %u ticks", ticks);
    return IPS_OK;
}

bool OSFocuser::AbortFocuser()
{
    LOG_INFO("Aborting focuser movement");
    return true;
}

IPState OSFocuser::HomeFocuser()
{
    LOG_INFO("Homing focuser");
    return IPS_OK;
}
