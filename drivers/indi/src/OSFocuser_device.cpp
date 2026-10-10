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
    auto response = protocol->sendCommand(":PF#");
    if (auto status = std::get_if<OSF::FocuserStatus>(&response.response)) {
        LOGF_INFO("Focuser status %s position %d", status->status.c_str(), status->position);
        
    }
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

    // handshake command
    auto response = protocol->sendCommand(":PP#");
    // Log debug frames for troubleshooting
    for (const auto &frame : response.debugFrames)
    {
        LOGF_DEBUG("Debug frame: %s", frame.c_str());
    }

    // Check if the response indicates success
    if (std::get_if<bool>(&response.response))
    {
        LOG_INFO("Handshake successful");
        return true;
    }

    LOG_ERROR("Handshake failed");
    return false;
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

// Abort focuser movement
bool OSFocuser::AbortFocuser()
{
    LOG_INFO("Aborting focuser movement");
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return false;
    }
    auto response = protocol->sendCommand(":MH#");
    if (std::get_if<bool>(&response.response)) {
        LOG_INFO("Focuser movement aborted successfully");
        return true;
    }
    LOG_ERROR("Failed to abort focuser movement");
    return false;
}

IPState OSFocuser::HomeFocuser()
{
    LOG_INFO("Homing focuser");
    return IPS_OK;
}
