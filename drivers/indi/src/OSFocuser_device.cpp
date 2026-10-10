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

// Keep one device instance alive for the lifetime of this INDI driver process.
static std::unique_ptr<OSFocuser> osFocuser(new OSFocuser());

OSFocuser::OSFocuser()
{
    // Report the build-time version and advertise only controls implemented by
    // this focuser driver to INDI clients.
    setVersion(CDRIVER_VERSION_MAJOR, CDRIVER_VERSION_MINOR);
    setSupportedConnections(CONNECTION_SERIAL);
    SetCapability(FOCUSER_CAN_ABORT | FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_HAS_VARIABLE_SPEED);

    // Configure the serial transport; its handshake callback creates the
    // protocol object and validates the attached focuser before connecting.
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
    // Let the base class publish standard focuser and connection properties.
    INDI::Focuser::ISGetProperties(dev);

    // Custom controls only exist while the hardware connection is active.
    if (isConnected()) {
        HomeSP.define();
        RebootSP.define();
        MotorToggleSP.define();
    }
}

bool OSFocuser::updateProperties()
{
    // Preserve the base focuser's property lifecycle before handling local
    // controls; stop if its update fails.
    if (!INDI::Focuser::updateProperties())
        return false;

    // Publish custom controls and start polling after connection succeeds.
    if (isConnected()) {
        defineProperty(HomeSP);
        defineProperty(RebootSP);
        defineProperty(MotorToggleSP);

        // timerID prevents reconnect callbacks from creating duplicate polls.
        if (timerID < 0)
            timerID = SetTimer(getCurrentPollingPeriod());
    } else {
        // Remove connection-only controls and discard pending operation state.
        deleteProperty(HomeSP);
        deleteProperty(RebootSP);
        deleteProperty(MotorToggleSP);
        homePending = false;

        // Cancel the outstanding one-shot callback before a future reconnect.
        if (timerID >= 0) {
            RemoveTimer(timerID);
            timerID = -1;
        }
    }
    return true;
}

bool OSFocuser::initProperties()
{
    // Initialize INDI's standard focuser vectors before adding driver-specific
    // controls; failure here means the device cannot be initialized.
    if (!INDI::Focuser::initProperties())
        return false;

    // Each custom action is a one-member, momentary switch in the System tab.
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
    // Requests for another device or without a property name belong to INDI's
    // normal dispatcher, not these custom action vectors.
    if (!dev || std::strcmp(dev, getDeviceName()) != 0 || !name)
        return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);

    // Resolve a property request into the switch to acknowledge and its
    // corresponding hardware command, if it is not handled by a method.
    INDI::PropertySwitch *actionProperty = nullptr;
    const char *actionElement = nullptr;
    const char *command = nullptr;
    if (HomeSP.isNameMatch(name)) {
        // Homing has a method that also tracks completion through :PF polling.
        actionProperty = std::addressof(HomeSP);
        actionElement = "HOME";
    } else if (RebootSP.isNameMatch(name)) {
        // Reboot returns an ACK before the device restarts.
        actionProperty = std::addressof(RebootSP);
        actionElement = "REBOOT";
        command = ":RB#";
    } else if (MotorToggleSP.isNameMatch(name)) {
        // Toggle returns the motor's new state rather than a generic ACK.
        actionProperty = std::addressof(MotorToggleSP);
        actionElement = "TOGGLE";
        command = ":TM#";
    } else {
        // Preserve standard focuser switch handling for all other properties.
        return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);
    }

    // Reject malformed switch updates before issuing any hardware command.
    if (!actionProperty->update(states, names, n))
        return false;

    // Read the requested momentary action, then reset the switch so it behaves
    // like a button instead of remaining selected in the client.
    const bool requested = actionProperty->isSwitchOn(actionElement);
    actionProperty->reset();
    if (!requested) {
        // A client can send the reset/off transition after the button press.
        actionProperty->setState(IPS_IDLE);
        actionProperty->apply();
        return true;
    }

    // Route Home through its asynchronous handler; other buttons issue their
    // protocol command and use the response type to determine their result.
    if (actionProperty == std::addressof(HomeSP)) {
        const IPState state = HomeFocuser();
        homePending = state == IPS_BUSY;
        HomeSP.setState(state);
    } else if (!protocol) {
        // Do not attempt writes when the serial handshake has not created a
        // usable protocol transport.
        LOG_ERROR("Protocol not initialized");
        actionProperty->setState(IPS_ALERT);
    } else {
        // Send the selected system command and interpret its command-specific
        // response before reporting the switch state to the client.
        const auto response = protocol->sendCommand(command);
        if (actionProperty == std::addressof(RebootSP)) {
            // Reboot succeeds only when the firmware returns a positive ACK.
            const auto *accepted = std::get_if<bool>(&response.response);
            actionProperty->setState(accepted && *accepted ? IPS_OK : IPS_ALERT);
            if (!accepted || !*accepted)
                LOG_ERROR("Failed to reboot focuser");
        } else {
            // :TM reports 0 or 1; either value is valid, so test the variant
            // type rather than treating zero as failure.
            const auto *motorEnabled = std::get_if<int32_t>(&response.response);
            actionProperty->setState(motorEnabled ? IPS_OK : IPS_ALERT);
            if (motorEnabled)
                LOGF_INFO("Focuser motor is %s", *motorEnabled ? "enabled" : "disabled");
            else
                LOG_ERROR("Failed to toggle focuser motor");
        }
    }

    // Send the final action state after the command has been processed.
    actionProperty->apply();
    return true;
}

void OSFocuser::TimerHit()
{
    // The event loop has consumed this one-shot timer; a later valid poll will
    // register its successor at the end of this function.
    timerID = -1;

    // Do not communicate after disconnection or before a protocol was created.
    if (!isConnected() || !protocol)
        return;
    

    LOG_DEBUG("Polling focuser status");
    auto response = protocol->sendCommand(":PF#");

    // The typed status response contains movement state and the encoder count.
    if (auto status = std::get_if<OSF::FocuserStatus>(&response.response)) {
        LOGF_INFO("Focuser status %s position %d", status->status.c_str(), status->position);

        IPState propertyState = IPS_ALERT;
        // Firmware currently emits title-case words; uppercase aliases are
        // accepted for compatibility with earlier firmware builds.
        if (status->status == "Idle" || status->status == "IDLE")
            propertyState = IPS_OK;
        else if (status->status == "Moving" || status->status == "Homing" ||
                 status->status == "MOVING" || status->status == "HOMING")
            propertyState = IPS_BUSY;

        const int32_t position = static_cast<int32_t>(status->position);
        // Only publish encoder readings inside the range established by :GL.
        if (position >= FocusAbsPosNP[0].getMin() && position <= FocusAbsPosNP[0].getMax())
            FocusAbsPosNP[0].setValue(position);
        else
            propertyState = IPS_ALERT;

        FocusAbsPosNP.setState(propertyState);
        FocusAbsPosNP.apply();

        // Absolute and relative commands share the same observed movement
        // state; an idle poll completes either command, and an invalid poll
        // alerts the relative vector as well.
        if (propertyState == IPS_OK) {
            FocusRelPosNP.setState(IPS_OK);
            FocusRelPosNP.apply();
        } else if (propertyState == IPS_ALERT) {
            FocusRelPosNP.setState(IPS_ALERT);
            FocusRelPosNP.apply();
        }

        // Home is asynchronous: keep its switch busy through motion, then
        // publish the first verified idle or error state.
        if (homePending && propertyState != IPS_BUSY) {
            homePending = false;
            HomeSP.setState(propertyState);
            HomeSP.apply();
        }
    } else {
        // An unparseable poll cannot verify either position command or Home.
        LOG_ERROR("Failed to parse focuser status");
        FocusAbsPosNP.setState(IPS_ALERT);
        FocusAbsPosNP.apply();
        FocusRelPosNP.setState(IPS_ALERT);
        FocusRelPosNP.apply();
        // Stop waiting for Home because this poll can no longer confirm it.
        if (homePending) {
            homePending = false;
            HomeSP.setState(IPS_ALERT);
            HomeSP.apply();
        }
    }

    // Keep status updates periodic; SetTimer schedules a one-shot callback.
    timerID = SetTimer(getCurrentPollingPeriod());
}

bool OSFocuser::Handshake()
{
    LOG_INFO("Performing handshake with the focuser hardware");

    // Require the serial plugin because it owns the active file descriptor.
    if (!serialConnection) {
        LOG_ERROR("Serial connection not initialized");
        return false;
    }

    // Bind protocol I/O to the port opened by the serial connection plugin.
    protocol = new OSFprotocol(serialConnection->getPortFD());

    // Reject an unusable protocol object before sending commands.
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return false;
    }

    // The heartbeat proves framing and transport work before configuration
    // queries are attempted.
    auto response = protocol->sendCommand(":PP#");

    // Forward firmware debug frames to the INDI log for diagnostics.
    for (const auto &frame : response.debugFrames)
        LOGF_DEBUG("Debug frame: %s", frame.c_str());

    // The protocol represents heartbeat ACK as bool; require true, not just
    // the presence of a bool alternative in the response variant.
    const auto *heartbeat = std::get_if<bool>(&response.response);
    if (!heartbeat || !*heartbeat) {
        LOG_ERROR("Handshake failed");
        return false;
    }

    // Query hardware travel bounds; reject empty, inverted, or negative ranges
    // before using them to constrain client input.
    const auto limitsResponse = protocol->sendCommand(":GL#");
    const auto *limits = std::get_if<OSF::StepLimits>(&limitsResponse.response);
    if (!limits || limits->minSteps < 0 || limits->maxSteps <= limits->minSteps) {
        LOG_ERROR("Failed to query valid focuser travel limits");
        return false;
    }

    // Read current position and ensure it is consistent with the reported
    // travel range before publishing the standard INDI position property.
    const auto positionResponse = protocol->sendCommand(":GP#");
    const auto *currentPosition = std::get_if<int32_t>(&positionResponse.response);
    if (!currentPosition || *currentPosition < limits->minSteps || *currentPosition > limits->maxSteps) {
        LOG_ERROR("Failed to query a valid focuser position");
        return false;
    }

    // Speed is an advertised standard focuser control and firmware accepts
    // discrete settings from zero through four.
    const auto speedResponse = protocol->sendCommand(":GS#");
    const auto *currentSpeed = std::get_if<int32_t>(&speedResponse.response);
    if (!currentSpeed || *currentSpeed < 0 || *currentSpeed > 4) {
        LOG_ERROR("Failed to query a valid focuser speed");
        return false;
    }

    // Convert the validated firmware limits into INDI's numeric property
    // representation and derive the relative travel span.
    const double minSteps = static_cast<double>(limits->minSteps);
    const double maxSteps = static_cast<double>(limits->maxSteps);
    const double travel = maxSteps - minSteps;

    // Absolute position is bounded by physical limits and initialized from
    // the encoder reading obtained during this handshake.
    FocusAbsPosNP[0].setMin(minSteps);
    FocusAbsPosNP[0].setMax(maxSteps);
    FocusAbsPosNP[0].setStep(1);
    FocusAbsPosNP[0].setValue(static_cast<double>(*currentPosition));
    FocusAbsPosNP.updateMinMax();
    FocusAbsPosNP.setState(IPS_OK);

    // Relative moves are magnitudes, so expose the complete travel as their
    // maximum and preserve a value that remains within the new range.
    FocusRelPosNP[0].setMin(0);
    FocusRelPosNP[0].setMax(travel);
    FocusRelPosNP[0].setStep(1);
    if (FocusRelPosNP[0].getValue() > travel)
        FocusRelPosNP[0].setValue(travel);
    FocusRelPosNP.updateMinMax();

    // The INDI maximum-position control reflects the device maximum, while
    // SetFocuserMaxPosition synchronizes the base class's preset limits.
    FocusMaxPosNP[0].setMin(minSteps);
    FocusMaxPosNP[0].setMax(maxSteps);
    FocusMaxPosNP[0].setStep(1);
    FocusMaxPosNP[0].setValue(maxSteps);
    FocusMaxPosNP.updateMinMax();
    FocusMaxPosNP.setState(IPS_OK);
    SetFocuserMaxPosition(static_cast<uint32_t>(limits->maxSteps));

    // Configure INDI's speed number vector to match firmware's five levels.
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

    // Refuse a move if the connection handshake did not initialize protocol.
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return IPS_ALERT;
    }

    // :MA accepts an absolute step target and returns ACK while motion starts.
    std::string cmd = ":MA" + std::to_string(targetTicks) + "#";
    auto response = protocol->sendCommand(cmd);
    // Report asynchronous progress only for a positive ACK; the timer poll
    // later reports completion and updates the absolute-position property.
    const auto *accepted = std::get_if<bool>(&response.response);
    if (accepted && *accepted) {
        return IPS_BUSY;
    } else {
        // A NAK, malformed response, or transport failure is an immediate
        // command error, not a move in progress.
        LOG_ERROR("Failed to move focuser absolutely");
        return IPS_ALERT;
    }
    return IPS_OK;
}

IPState OSFocuser::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    LOGF_INFO("Moving relative focuser by %u ticks", ticks);

    // Avoid serial I/O if the transport is unavailable.
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return IPS_ALERT;
    }

    // Firmware expects signed deltas; INDI provides direction and magnitude
    // separately, so convert them before constructing :MR.
    const int32_t signedDelta = (dir == FOCUS_INWARD ? -1 : 1) * static_cast<int32_t>(ticks);
    std::string cmd = ":MR" + std::to_string(signedDelta) + "#";
    auto response = protocol->sendCommand(cmd);
    // Return busy after a positive ACK; :PF polling determines when motion
    // has ended and changes the property state to OK.
    const auto *accepted = std::get_if<bool>(&response.response);
    if (accepted && *accepted) {
        return IPS_BUSY;
    } else {
        // Treat NAKs and invalid responses as immediate failures.
        LOG_ERROR("Failed to move focuser relatively");
        return IPS_ALERT;
    }
    return IPS_BUSY;
}

// Abort focuser movement
bool OSFocuser::AbortFocuser()
{
    LOG_INFO("Aborting focuser movement");

    // An abort cannot be sent before the serial protocol is available.
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return false;
    }

    // :MH requests an immediate halt; the base INDI focuser consumes this
    // boolean result to update its abort property.
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

    // Homing is only possible after the serial connection has completed setup.
    if (!protocol) {
        LOG_ERROR("Protocol not initialized");
        return IPS_ALERT;
    }

    // :HM starts an asynchronous home sequence; TimerHit waits for :PF to
    // report a non-busy state before completing the Home switch.
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
