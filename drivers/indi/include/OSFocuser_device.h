#pragma once
#include <string>
#include <variant>
#include <cstdint>
#include <vector>
#include "indifocuser.h"
#include "protocol.h"

// Main INDI device: standard focuser properties are supplied by the base
// class, while this class binds their callbacks and custom system buttons to
// the OpenSmartFocuser serial protocol.
class OSFocuser : public INDI::Focuser
{
    public:
        // Construct the INDI device and register its serial connection plugin.
        OSFocuser();

        // The process owns this instance; default destruction releases members.
        virtual ~OSFocuser() = default;

        // Name shown to clients when this device is discovered.
        const char *getDefaultName() override;

        // Initialize standard and custom INDI vectors before client requests.
        virtual bool initProperties() override;

        // Publish connection-dependent properties and manage status polling.
        virtual bool updateProperties() override;

        // Publish available properties when a client connects or refreshes.
        virtual void ISGetProperties(const char *dev) override;

        // Route custom System-tab buttons before delegating normal switches.
        virtual bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;

        // One-shot event-loop callback that polls :PF and updates INDI states.
        virtual void TimerHit() override;

    private:
        // INDI owns and configures this serial connection; protocol uses its fd.
        Connection::Serial *serialConnection = nullptr;

        // Created during Handshake after the serial plugin opens the port.
        OSFprotocol *protocol = nullptr;

        // Momentary switch vectors for device actions, published while online.
        INDI::PropertySwitch HomeSP {1};
        INDI::PropertySwitch RebootSP {1};
        INDI::PropertySwitch MotorToggleSP {1};

        // Tracks asynchronous homing until polling reports completion or error.
        bool homePending = false;

        // ID of the outstanding one-shot TimerHit callback; -1 means none.
        int timerID = -1;

    protected:

        // Establish transport and query the initial focuser configuration.
        virtual bool Handshake() override;

        // Translate INDI step targets into the firmware's absolute :MA command.
        virtual IPState MoveAbsFocuser(uint32_t targetTicks);

        // Convert INDI direction/magnitude into a signed firmware :MR delta.
        virtual IPState MoveRelFocuser(FocusDirection dir, uint32_t ticks);

        // Start asynchronous homing using firmware command :HM.
        virtual IPState HomeFocuser();

        // Stop active motion using firmware command :MH.
        virtual bool AbortFocuser();

        // Apply INDI's selected discrete speed through firmware command :MS.
        virtual bool SetFocuserSpeed(int speed) override;
        
};
