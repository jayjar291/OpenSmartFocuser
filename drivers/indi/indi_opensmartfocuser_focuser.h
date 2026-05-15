#pragma once

#include "indifocuser.h"

#include "indipropertyswitch.h"
#include "indipropertytext.h"

#include <cstdint>
#include <deque>
#include <string>

// INDI driver for OpenSmartFocuser.
// This class owns serial transport, protocol mapping, and custom UI properties.
class OpenSmartFocuser : public INDI::Focuser
{
	public:
		// Construct driver instance and advertise supported focuser capabilities.
		OpenSmartFocuser();
		~OpenSmartFocuser() override = default;

		// INDI-visible default device label.
		const char *getDefaultName() override;

	protected:
		// Build base focuser properties and custom OpenSmartFocuser properties.
		bool initProperties() override;
		// Define/delete runtime properties based on connection state and refresh cached data.
		bool updateProperties() override;

		// Open/close serial transport and perform startup handshake.
		bool Connect() override;
		bool Disconnect() override;

		// Absolute/relative/abort motion handlers invoked by INDI focuser interface.
		IPState MoveAbsFocuser(uint32_t targetTicks) override;
		IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks) override;
		bool AbortFocuser() override;

		// Handle custom switch and text property updates from clients.
		bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
		bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;

	private:
		// Create and define all non-standard driver properties.
		void initCustomProperties();
		// Keep custom property visibility in sync with connection state.
		void updateCustomPropertyVisibility();

		// Low-level serial port lifecycle.
		bool openSerialPort();
		void closeSerialPort();

		// Protocol frame I/O helpers.
		bool sendFrame(const std::string &frame);
		bool readFrame(std::string &frame, int timeoutMs);
		// Command helpers that send token/payload frames and parse expected replies.
		bool sendCommand(const std::string &token, const std::string &payload, std::string &response);
		bool commandAck(const std::string &token, const std::string &payload);
		bool queryPosition(uint32_t &position);
		bool querySpeedIndex(uint32_t &speedIndex);

		// UI/state helpers for speed controls and serial monitor buffer management.
		void updateSpeedSelection(uint32_t speedIndex);
		void appendSerialMonitorLine(const std::string &prefix, const std::string &payload);
		void clearSerialMonitor();
		// Publish latest raw response/debug line in the simple Raw Output field.
		void publishRawOutput(const std::string &output);

		// Text properties.
		// UsbPortTP: user-selected serial device path.
		// RawCommandTP/RawOutputTP: manual command testing and last output display.
		// SerialMonitorTP: rolling monitor transcript (TX/RX/DBG).
		INDI::PropertyText UsbPortTP {1};
		INDI::PropertyText RawCommandTP {1};
		INDI::PropertyText RawOutputTP {1};
		INDI::PropertyText SerialMonitorTP {1};

		// Switch properties.
		// MotorControlSP: enable/disable motor driver.
		// SpeedPresetSP: 5 speed presets mapped to firmware :MS0..:MS4.
		// HomeSP/RebootSP: trigger one-shot system actions.
		// RawSendSP: send manual frame from RawCommandTP.
		// SerialMonitorClearSP: clear rolling monitor transcript.
		INDI::PropertySwitch MotorControlSP {2};
		INDI::PropertySwitch SpeedPresetSP {5};
		INDI::PropertySwitch HomeSP {1};
		INDI::PropertySwitch RebootSP {1};
		INDI::PropertySwitch RawSendSP {1};
		INDI::PropertySwitch SerialMonitorClearSP {1};

		// Runtime device state.
		// serialFD: active POSIX serial descriptor or -1 when disconnected.
		// cachedPosition: last known absolute position in ticks.
		// serialMonitorLines: bounded transcript used by SerialMonitorTP.
		int serialFD { -1 };
		uint32_t cachedPosition { 0 };
		std::deque<std::string> serialMonitorLines;
};
