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
    SetCapability(FOCUSER_CAN_ABORT | FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_HAS_VARIABLE_SPEED);
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
    if (isConnected()) {
        HomeSP.define();
        RebootSP.define();
        MotorToggleSP.define();
    }
}

bool OSFocuser::updateProperties()
{
    if (!INDI::Focuser::updateProperties())
        return false;

    if (isConnected()) {
        defineProperty(HomeSP);
        defineProperty(RebootSP);
        defineProperty(MotorToggleSP);
        if (timerID < 0)
            timerID = SetTimer(getCurrentPollingPeriod());
    } else {
        deleteProperty(HomeSP);
        deleteProperty(RebootSP);
        deleteProperty(MotorToggleSP);
        homePending = false;
        if (timerID >= 0) {
            RemoveTimer(timerID);
            timerID = -1;
        }
    }
    return true;
}

bool OSFocuser::initProperties()
{
    if (!INDI::Focuser::initProperties())
        return false;

    HomeSP.fill(getDeviceName(), "FOCUS_HOME", "Home", "System", IP_RW, ISR_ATMOST1, 0, IPS_IDLE);
    HomeSP[0].fill("HOME", "Home", ISS_OFF);
    RebootSP.fill(getDeviceName(), "FOCUS_REBOOT", "Reboot", "System", IP_RW, ISR_ATMOST1, 0, IPS_IDLE);
    RebootSP[0].fill("REBOOT", "Reboot", ISS_OFF);
    MotorToggleSP.fill(getDeviceName(), "FOCUS_MOTOR_TOGGLE", "Toggle Motor", "System", IP_RW, ISR_ATMOST1, 0, IPS_IDLE);
    MotorToggleSP[0].fill("TOGGLE", "Toggle", ISS_OFF);
    return true;
}

bool OSFocuser::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (!dev || std::strcmp(dev, getDeviceName()) != 0 || !name)
        return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);

    INDI::PropertySwitch *actionProperty = nullptr;
    const char *actionElement = nullptr;
    const char *command = nullptr;
    if (HomeSP.isNameMatch(name)) {
        actionProperty = std::addressof(HomeSP);
        actionElement = "HOME";
    } else if (RebootSP.isNameMatch(name)) {
        actionProperty = std::addressof(RebootSP);
        actionElement = "REBOOT";
        command = ":RB#";
    } else if (MotorToggleSP.isNameMatch(name)) {
        actionProperty = std::addressof(MotorToggleSP);
        actionElement = "TOGGLE";
        command = ":TM#";
    } else {
        return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);
    }

    if (!actionProperty->update(states, names, n))
        return false;

    const bool requested = actionProperty->isSwitchOn(actionElement);
    actionProperty->reset();
    if (!requested) {
        actionProperty->setState(IPS_IDLE);
        actionProperty->apply();
        return true;
    }

    if (actionProperty == std::addressof(HomeSP)) {
        const IPState state = HomeFocuser();
        homePending = state == IPS_BUSY;
        HomeSP.setState(state);
    } else if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        actionProperty->setState(IPS_ALERT);
    } else {
        const auto response = protocol->sendCommand(command);
        if (actionProperty == std::addressof(RebootSP)) {
            const auto *accepted = std::get_if<bool>(&response.response);
            actionProperty->setState(accepted && *accepted ? IPS_OK : IPS_ALERT);
            if (!accepted || !*accepted)
                LOG_ERROR("Failed to reboot focuser");
        } else {
            const auto *motorEnabled = std::get_if<int32_t>(&response.response);
            actionProperty->setState(motorEnabled ? IPS_OK : IPS_ALERT);
            if (motorEnabled)
                LOGF_INFO("Focuser motor is %s", *motorEnabled ? "enabled" : "disabled");
            else
                LOG_ERROR("Failed to toggle focuser motor");
        }
    }
    actionProperty->apply();
    return true;
}

void OSFocuser::TimerHit()
{
    timerID = -1;
    if (!isConnected() || !protocol)
        return;
    

    LOG_DEBUG("Polling focuser status");
    auto response = protocol->sendCommand(":PF#");
    if (auto status = std::get_if<OSF::FocuserStatus>(&response.response)) {
        LOGF_INFO("Focuser status %s position %d", status->status.c_str(), status->position);

        IPState propertyState = IPS_ALERT;
        if (status->status == "Idle" || status->status == "IDLE")
            propertyState = IPS_OK;
        else if (status->status == "Moving" || status->status == "Homing" ||
                 status->status == "MOVING" || status->status == "HOMING")
            propertyState = IPS_BUSY;

        const int32_t position = static_cast<int32_t>(status->position);
        if (position >= FocusAbsPosNP[0].getMin() && position <= FocusAbsPosNP[0].getMax())
            FocusAbsPosNP[0].setValue(position);
        else
            propertyState = IPS_ALERT;

        FocusAbsPosNP.setState(propertyState);
        FocusAbsPosNP.apply();

        if (propertyState == IPS_OK) {
            FocusRelPosNP.setState(IPS_OK);
            FocusRelPosNP.apply();
        } else if (propertyState == IPS_ALERT) {
            FocusRelPosNP.setState(IPS_ALERT);
            FocusRelPosNP.apply();
        }

        if (homePending && propertyState != IPS_BUSY) {
            homePending = false;
            HomeSP.setState(propertyState);
            HomeSP.apply();
        }
    } else {
        LOG_ERROR("Failed to parse focuser status");
        FocusAbsPosNP.setState(IPS_ALERT);
        FocusAbsPosNP.apply();
        FocusRelPosNP.setState(IPS_ALERT);
        FocusRelPosNP.apply();
        if (homePending) {
            homePending = false;
            HomeSP.setState(IPS_ALERT);
            HomeSP.apply();
        }
    }
    timerID = SetTimer(getCurrentPollingPeriod());
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

    // Verify communication before reading the device configuration.
    auto response = protocol->sendCommand(":PP#");
    for (const auto &frame : response.debugFrames)
        LOGF_DEBUG("Debug frame: %s", frame.c_str());

    const auto *heartbeat = std::get_if<bool>(&response.response);
    if (!heartbeat || !*heartbeat) {
        LOG_ERROR("Handshake failed");
        return false;
    }

    const auto limitsResponse = protocol->sendCommand(":GL#");
    const auto *limits = std::get_if<OSF::StepLimits>(&limitsResponse.response);
    if (!limits || limits->minSteps < 0 || limits->maxSteps <= limits->minSteps) {
        LOG_ERROR("Failed to query valid focuser travel limits");
        return false;
    }

    const auto positionResponse = protocol->sendCommand(":GP#");
    const auto *currentPosition = std::get_if<int32_t>(&positionResponse.response);
    if (!currentPosition || *currentPosition < limits->minSteps || *currentPosition > limits->maxSteps) {
        LOG_ERROR("Failed to query a valid focuser position");
        return false;
    }

    const auto speedResponse = protocol->sendCommand(":GS#");
    const auto *currentSpeed = std::get_if<int32_t>(&speedResponse.response);
    if (!currentSpeed || *currentSpeed < 0 || *currentSpeed > 4) {
        LOG_ERROR("Failed to query a valid focuser speed");
        return false;
    }

    const double minSteps = static_cast<double>(limits->minSteps);
    const double maxSteps = static_cast<double>(limits->maxSteps);
    const double travel = maxSteps - minSteps;

    FocusAbsPosNP[0].setMin(minSteps);
    FocusAbsPosNP[0].setMax(maxSteps);
    FocusAbsPosNP[0].setStep(1);
    FocusAbsPosNP[0].setValue(static_cast<double>(*currentPosition));
    FocusAbsPosNP.updateMinMax();
    FocusAbsPosNP.setState(IPS_OK);

    FocusRelPosNP[0].setMin(0);
    FocusRelPosNP[0].setMax(travel);
    FocusRelPosNP[0].setStep(1);
    if (FocusRelPosNP[0].getValue() > travel)
        FocusRelPosNP[0].setValue(travel);
    FocusRelPosNP.updateMinMax();

    FocusMaxPosNP[0].setMin(minSteps);
    FocusMaxPosNP[0].setMax(maxSteps);
    FocusMaxPosNP[0].setStep(1);
    FocusMaxPosNP[0].setValue(maxSteps);
    FocusMaxPosNP.updateMinMax();
    FocusMaxPosNP.setState(IPS_OK);
    SetFocuserMaxPosition(static_cast<uint32_t>(limits->maxSteps));

    FocusSpeedNP[0].setMin(0);
    FocusSpeedNP[0].setMax(4);
    FocusSpeedNP[0].setStep(1);
    FocusSpeedNP[0].setValue(static_cast<double>(*currentSpeed));
    FocusSpeedNP.updateMinMax();
    FocusSpeedNP.setState(IPS_OK);

    LOG_INFO("Focuser handshake and configuration query successful");
    return true;
}

bool OSFocuser::SetFocuserSpeed(int speed)
{
    if (!protocol || speed < 0 || speed > 4) {
        LOG_ERROR("Invalid focuser speed or protocol not initialized");
        return false;
    }

    const auto response = protocol->sendCommand(":MS" + std::to_string(speed) + "#");
    const auto *accepted = std::get_if<bool>(&response.response);
    if (!accepted || !*accepted) {
        LOG_ERROR("Failed to set focuser speed");
        return false;
    }
    return true;
}

IPState OSFocuser::MoveAbsFocuser(uint32_t targetTicks)
{
    LOGF_INFO("Moving absolute focuser to %u ticks", targetTicks);
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return IPS_ALERT;
    }
    std::string cmd = ":MA" + std::to_string(targetTicks) + "#";
    auto response = protocol->sendCommand(cmd);
    const auto *accepted = std::get_if<bool>(&response.response);
    if (accepted && *accepted) {
        return IPS_BUSY;
    } else {
        LOG_ERROR("Failed to move focuser absolutely");
        return IPS_ALERT;
    }
    return IPS_OK;
}

IPState OSFocuser::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    LOGF_INFO("Moving relative focuser by %u ticks", ticks);
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return IPS_ALERT;
    }
    const int32_t signedDelta = (dir == FOCUS_INWARD ? -1 : 1) * static_cast<int32_t>(ticks);
    std::string cmd = ":MR" + std::to_string(signedDelta) + "#";
    auto response = protocol->sendCommand(cmd);
    const auto *accepted = std::get_if<bool>(&response.response);
    if (accepted && *accepted) {
        return IPS_BUSY;
    } else {
        LOG_ERROR("Failed to move focuser relatively");
        return IPS_ALERT;
    }
    return IPS_BUSY;
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
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return IPS_ALERT;
    }
    auto response = protocol->sendCommand(":HM#");
    const auto *accepted = std::get_if<bool>(&response.response);
    if (accepted && *accepted) {
        return IPS_BUSY;
    } else {
        LOG_ERROR("Failed to home focuser");
        return IPS_ALERT;
    }
    return IPS_OK;
}
