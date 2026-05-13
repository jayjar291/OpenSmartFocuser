#pragma once

#include "indifocuser.h"

#include "indipropertyswitch.h"
#include "indipropertytext.h"

#include <cstdint>
#include <string>

class OpenSmartFocuser : public INDI::Focuser
{
	public:
		OpenSmartFocuser();
		~OpenSmartFocuser() override = default;

		const char *getDefaultName() override;

	protected:
		bool initProperties() override;
		bool updateProperties() override;

		bool Connect() override;
		bool Disconnect() override;

		IPState MoveAbsFocuser(uint32_t targetTicks) override;
		IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks) override;
		bool AbortFocuser() override;

		bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
		bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;

	private:
		void initCustomProperties();
		void updateCustomPropertyVisibility();
		bool openSerialPort();
		void closeSerialPort();
		bool sendFrame(const std::string &frame);
		bool readFrame(std::string &frame, int timeoutMs);
		bool sendCommand(const std::string &token, const std::string &payload, std::string &response);
		bool commandAck(const std::string &token, const std::string &payload);
		bool queryPosition(uint32_t &position);
		void publishRawOutput(const std::string &output);

		INDI::PropertyText UsbPortTP {1};
		INDI::PropertyText RawCommandTP {1};
		INDI::PropertyText RawOutputTP {1};
		INDI::PropertySwitch MotorControlSP {2};
		INDI::PropertySwitch RebootSP {1};
		INDI::PropertySwitch RawSendSP {1};

		int serialFD { -1 };
		uint32_t cachedPosition { 0 };
};
