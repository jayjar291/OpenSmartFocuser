#include "dustcap_device.h"
#include "protocol.h"

DustCapDevice::DustCapDevice(INDI::DefaultDevice *device, OSFprotocol *protocol)
	: INDI::DustCapInterface(device), protocol(protocol)
{
}

void DustCapDevice::setProtocol(OSFprotocol *value)
{
	protocol = value;
}

void DustCapDevice::initProperties()
{
	INDI::DustCapInterface::initProperties("Dust Cap");
}

bool DustCapDevice::updateProperties()
{
	return INDI::DustCapInterface::updateProperties();
}

bool DustCapDevice::processSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
	return INDI::DustCapInterface::processSwitch(dev, name, states, names, n);
}

void DustCapDevice::updateStatus(std::optional<int16_t> position)
{
	if (!position) {
		statusUnavailable();
		return;
	}

	const int32_t capPosition = *position;
	const bool parked = capPosition == 0;
	const bool unparked = capPosition == 270;
	IPState capState = (parked || unparked) ? IPS_OK : IPS_ALERT;
	if (targetPosition >= 0) {
		capState = capPosition == targetPosition ? IPS_OK : IPS_BUSY;
		if (capState == IPS_OK)
			targetPosition = -1;
	}

	const ISState parkSwitchState = parked ? ISS_ON : ISS_OFF;
	const ISState unparkSwitchState = unparked ? ISS_ON : ISS_OFF;
	if (ParkCapSP[INDI::DustCapInterface::CAP_PARK].getState() == parkSwitchState &&
		ParkCapSP[INDI::DustCapInterface::CAP_UNPARK].getState() == unparkSwitchState &&
		ParkCapSP.getState() == capState)
		return;

	ParkCapSP[INDI::DustCapInterface::CAP_PARK].setState(parkSwitchState);
	ParkCapSP[INDI::DustCapInterface::CAP_UNPARK].setState(unparkSwitchState);
	ParkCapSP.setState(capState);
	ParkCapSP.apply();
}

void DustCapDevice::statusUnavailable()
{
	if (targetPosition < 0)
		return;

	targetPosition = -1;
	ParkCapSP.setState(IPS_ALERT);
	ParkCapSP.apply();
}

IPState DustCapDevice::ParkCap()
{
	if (!protocol)
		return IPS_ALERT;

	const auto response = protocol->sendCommand(":SV0#");
	const auto *accepted = std::get_if<bool>(&response.response);
	if (!accepted || !*accepted)
		return IPS_ALERT;

	targetPosition = 0;
	return IPS_BUSY;
}

IPState DustCapDevice::UnParkCap()
{
	if (!protocol)
		return IPS_ALERT;

	const auto response = protocol->sendCommand(":SV270#");
	const auto *accepted = std::get_if<bool>(&response.response);
	if (!accepted || !*accepted)
		return IPS_ALERT;

	targetPosition = 270;
	return IPS_BUSY;
}
