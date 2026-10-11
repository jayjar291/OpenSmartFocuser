#pragma once

#include <cstdint>
#include <optional>

#include <indilightboxinterface.h>

class OSFprotocol;

class FlatDevice final : public INDI::LightBoxInterface
{
	public:
		FlatDevice(INDI::DefaultDevice *device, OSFprotocol *protocol);

		void setProtocol(OSFprotocol *protocol);
		void initProperties();
		bool updateProperties();
		void ISGetProperties(const char *dev);
		bool processSwitch(const char *dev, const char *name, ISState *states, char *names[], int n);
		bool processNumber(const char *dev, const char *name, double values[], char *names[], int n);
		bool processText(const char *dev, const char *name, char *texts[], char *names[], int n);
		void updateStatus(std::optional<int16_t> brightness);

	protected:
		bool EnableLightBox(bool enable) override;
		bool SetLightBoxBrightness(uint16_t value) override;

	private:
		OSFprotocol *protocol;
};
