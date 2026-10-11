#include "flat_device.h"
#include "protocol.h"

FlatDevice::FlatDevice(INDI::DefaultDevice *device, OSFprotocol *protocol)
	: INDI::LightBoxInterface(device), protocol(protocol)
{
}

void FlatDevice::setProtocol(OSFprotocol *value)
{
	protocol = value;
}

void FlatDevice::initProperties()
{
	INDI::LightBoxInterface::initProperties("Flat Panel", INDI::LightBoxInterface::CAN_DIM);
	LightIntensityNP[0].setMin(0);
	LightIntensityNP[0].setMax(255);
	LightIntensityNP[0].setStep(1);
	LightIntensityNP.updateMinMax();
}

bool FlatDevice::updateProperties()
{
	return INDI::LightBoxInterface::updateProperties();
}

void FlatDevice::ISGetProperties(const char *dev)
{
	INDI::LightBoxInterface::ISGetProperties(dev);
}

bool FlatDevice::processSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
	return INDI::LightBoxInterface::processSwitch(dev, name, states, names, n);
}

bool FlatDevice::processNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
	return INDI::LightBoxInterface::processNumber(dev, name, values, names, n);
}

bool FlatDevice::processText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
	return INDI::LightBoxInterface::processText(dev, name, texts, names, n);
}

void FlatDevice::updateStatus(std::optional<int16_t> brightness)
{
	if (!brightness || *brightness < 0 || *brightness > 255)
		return;

	const double value = *brightness;
	const ISState onState = value > 0 ? ISS_ON : ISS_OFF;
	const ISState offState = value == 0 ? ISS_ON : ISS_OFF;
	if (LightIntensityNP[0].getValue() == value &&
		LightIntensityNP.getState() == IPS_OK &&
		LightSP[INDI::LightBoxInterface::FLAT_LIGHT_ON].getState() == onState &&
		LightSP[INDI::LightBoxInterface::FLAT_LIGHT_OFF].getState() == offState &&
		LightSP.getState() == IPS_OK)
		return;

	LightIntensityNP[0].setValue(value);
	LightIntensityNP.setState(IPS_OK);
	LightIntensityNP.apply();
	LightSP[INDI::LightBoxInterface::FLAT_LIGHT_ON].setState(onState);
	LightSP[INDI::LightBoxInterface::FLAT_LIGHT_OFF].setState(offState);
	LightSP.setState(IPS_OK);
	LightSP.apply();
}

bool FlatDevice::EnableLightBox(bool enable)
{
	if (!protocol)
		return false;

	const auto response = protocol->sendCommand(enable ? ":FP255#" : ":FP0#");
	const auto *accepted = std::get_if<bool>(&response.response);
	return accepted && *accepted;
}

bool FlatDevice::SetLightBoxBrightness(uint16_t value)
{
	if (!protocol || value > 255)
		return false;

	const auto response = protocol->sendCommand(":FP" + std::to_string(value) + "#");
	const auto *accepted = std::get_if<bool>(&response.response);
	return accepted && *accepted;
}
