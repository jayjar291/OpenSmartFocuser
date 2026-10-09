#include "OSFocuser_device.h"

#include <memory>

static std::unique_ptr<OSFocuser> osFocuser(new OSFocuser());

const char *OSFocuser::getDefaultName()
{
    return "OpenSmartFocuser";
}
