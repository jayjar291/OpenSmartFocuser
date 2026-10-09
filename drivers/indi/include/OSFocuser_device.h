#pragma once

#include "indifocuser.h"

class OSFocuser : public INDI::Focuser
{
    public:
        OSFocuser() = default;

        const char *getDefaultName() override;
};
