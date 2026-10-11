#pragma once

#include <cstdint>
#include <optional>

#include <indidustcapinterface.h>

class OSFprotocol;

class DustCapDevice final : public INDI::DustCapInterface
{
	public:
		DustCapDevice(INDI::DefaultDevice *device, OSFprotocol *protocol);

		void setProtocol(OSFprotocol *protocol);
		void initProperties();
		bool updateProperties();
		bool processSwitch(const char *dev, const char *name, ISState *states, char *names[], int n);
		void updateStatus(std::optional<int16_t> position);
		void statusUnavailable();

	protected:
		IPState ParkCap() override;
		IPState UnParkCap() override;

	private:
		OSFprotocol *protocol;
		int32_t targetPosition = -1;
};
