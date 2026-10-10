#pragma once
#include <string>
#include <variant>
#include <cstdint>
#include <vector>
#include "indifocuser.h"
#include "protocol.h"

class OSFocuser : public INDI::Focuser
{
    public:
        OSFocuser();
        virtual ~OSFocuser() = default;
        const char *getDefaultName() override;

        virtual bool initProperties() override;
        virtual bool updateProperties() override;
        virtual void ISGetProperties(const char *dev) override;
        virtual bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
        virtual void TimerHit() override;

    private:
        Connection::Serial *serialConnection = nullptr;
        OSFprotocol *protocol = nullptr;
        INDI::PropertySwitch HomeSP {1};
        INDI::PropertySwitch RebootSP {1};
        INDI::PropertySwitch MotorToggleSP {1};
        bool homePending = false;
        int timerID = -1;

    protected:

        virtual bool Handshake() override;

        virtual IPState MoveAbsFocuser(uint32_t targetTicks);
        virtual IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks);
        virtual IPState HomeFocuser();
        virtual bool AbortFocuser();
        virtual bool SetFocuserSpeed(int speed) override;
        
};
