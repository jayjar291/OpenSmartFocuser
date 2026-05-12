#pragma once

#include "indifocuser.h"

class OpenSmartFocuserDummy : public INDI::Focuser
{
	public:
		OpenSmartFocuserDummy();
		~OpenSmartFocuserDummy() override = default;

		const char *getDefaultName() override;

	protected:
		bool initProperties() override;
		bool updateProperties() override;

		bool Connect() override;
		bool Disconnect() override;

		IPState MoveAbsFocuser(uint32_t targetTicks) override;
		IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks) override;
		bool AbortFocuser() override;
		bool ReverseFocuser(bool enabled) override;
		bool SyncFocuser(uint32_t ticks) override;
		bool SetFocuserMaxPosition(uint32_t ticks) override;
		bool SetFocuserBacklash(int32_t steps) override;
		bool SetFocuserBacklashEnabled(bool enabled) override;
		bool SetFocuserSpeed(int speed) override;

	private:
		uint32_t simulatedPosition { 0 };
		bool reverseEnabled { false };
};
