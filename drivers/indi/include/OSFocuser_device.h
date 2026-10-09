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
        virtual void TimerHit() override;

    private:
        Connection::Serial *serialConnection = nullptr;
        OSFprotocol *protocol = nullptr;

    protected:

        virtual bool Handshake() override;

        virtual IPState MoveFocuser(FocusDirection dir, int speed, uint16_t duration);
        virtual IPState MoveAbsFocuser(uint32_t targetTicks);
        virtual IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks);
        virtual IPState HomeFocuser();
        virtual bool AbortFocuser();
        
};
