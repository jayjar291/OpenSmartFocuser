#include "config.h"
#include "indi_opensmartfocuser_focuser.h"

#include <algorithm>
#include <memory>

static std::unique_ptr<OpenSmartFocuserDummy> openSmartFocuserDummy(new OpenSmartFocuserDummy());

OpenSmartFocuserDummy::OpenSmartFocuserDummy()
{
	FI::SetCapability(FOCUSER_CAN_ABS_MOVE | FOCUSER_CAN_REL_MOVE | FOCUSER_CAN_ABORT | FOCUSER_CAN_REVERSE |
					  FOCUSER_CAN_SYNC | FOCUSER_HAS_VARIABLE_SPEED | FOCUSER_HAS_BACKLASH);
	setVersion(CDRIVER_VERSION_MAJOR, CDRIVER_VERSION_MINOR);
}

const char *OpenSmartFocuserDummy::getDefaultName()
{
	return "OpenSmartFocuser Dummy";
}

bool OpenSmartFocuserDummy::initProperties()
{
	INDI::Focuser::initProperties();

	simulatedPosition = static_cast<uint32_t>(FocusAbsPosNP[0].getValue());
	LOG_INFO("Initialized dummy focuser properties (log-only mode).");
	return true;
}

bool OpenSmartFocuserDummy::updateProperties()
{
	INDI::Focuser::updateProperties();

	if (isConnected())
		LOG_INFO("Dummy focuser connected: properties are active.");
	else
		LOG_INFO("Dummy focuser disconnected: properties are inactive.");

	return true;
}

bool OpenSmartFocuserDummy::Connect()
{
	LOG_INFO("Connect requested. No hardware connection is performed in dummy mode.");
	return true;
}

bool OpenSmartFocuserDummy::Disconnect()
{
	LOG_INFO("Disconnect requested. No hardware teardown is needed in dummy mode.");
	return true;
}

IPState OpenSmartFocuserDummy::MoveAbsFocuser(uint32_t targetTicks)
{
	LOGF_INFO("MoveAbsFocuser requested: target=%u (simulated only).", targetTicks);

	simulatedPosition = targetTicks;
	FocusAbsPosNP[0].setValue(simulatedPosition);
	FocusAbsPosNP.setState(IPS_OK);
	FocusAbsPosNP.apply();
	return IPS_OK;
}

IPState OpenSmartFocuserDummy::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
	const char *direction = (dir == FOCUS_INWARD) ? "inward" : "outward";
	LOGF_INFO("MoveRelFocuser requested: dir=%s ticks=%u (simulated only).", direction, ticks);

	int64_t signedPosition = static_cast<int64_t>(simulatedPosition);
	signedPosition += (dir == FOCUS_INWARD ? -1 : 1) * static_cast<int64_t>(ticks);

	const int64_t minPos = static_cast<int64_t>(FocusAbsPosNP[0].getMin());
	const int64_t maxPos = static_cast<int64_t>(FocusAbsPosNP[0].getMax());
	signedPosition = std::max(minPos, std::min(maxPos, signedPosition));

	return MoveAbsFocuser(static_cast<uint32_t>(signedPosition));
}

bool OpenSmartFocuserDummy::AbortFocuser()
{
	LOG_INFO("AbortFocuser requested. No active motion exists in dummy mode.");
	return true;
}

bool OpenSmartFocuserDummy::ReverseFocuser(bool enabled)
{
	reverseEnabled = enabled;
	LOGF_INFO("ReverseFocuser requested: enabled=%s (simulated only).", reverseEnabled ? "true" : "false");
	return true;
}

bool OpenSmartFocuserDummy::SyncFocuser(uint32_t ticks)
{
	LOGF_INFO("SyncFocuser requested: ticks=%u (simulated only).", ticks);

	simulatedPosition = ticks;
	FocusAbsPosNP[0].setValue(simulatedPosition);
	FocusAbsPosNP.setState(IPS_OK);
	FocusAbsPosNP.apply();
	return true;
}

bool OpenSmartFocuserDummy::SetFocuserMaxPosition(uint32_t ticks)
{
	LOGF_INFO("SetFocuserMaxPosition requested: max=%u (simulated only).", ticks);
	INDI::Focuser::SetFocuserMaxPosition(ticks);

	if (simulatedPosition > ticks)
		simulatedPosition = ticks;

	return true;
}

bool OpenSmartFocuserDummy::SetFocuserBacklash(int32_t steps)
{
	LOGF_INFO("SetFocuserBacklash requested: steps=%d (simulated only).", steps);
	return true;
}

bool OpenSmartFocuserDummy::SetFocuserBacklashEnabled(bool enabled)
{
	LOGF_INFO("SetFocuserBacklashEnabled requested: enabled=%s (simulated only).", enabled ? "true" : "false");
	return true;
}

bool OpenSmartFocuserDummy::SetFocuserSpeed(int speed)
{
	LOGF_INFO("SetFocuserSpeed requested: speed=%d (simulated only).", speed);
	return true;
}
