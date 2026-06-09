Import("env")


def _drop_define(defines, name):
    out = []
    for item in defines:
        if isinstance(item, (list, tuple)):
            key = item[0]
        else:
            key = item
        if key != name:
            out.append(item)
    return out


def _set_define(defines, name, value):
    defines = _drop_define(defines, name)
    defines.append((name, value))
    return defines


cpp_defines = env.get("CPPDEFINES", [])

# Force native USB CDC via TinyUSB so VID/PID and strings are configurable.
cpp_defines = _set_define(cpp_defines, "ARDUINO_USB_MODE", 0)
cpp_defines = _set_define(cpp_defines, "ARDUINO_USB_CDC_ON_BOOT", 1)

# USB identity shown to host OS when using TinyUSB path.
cpp_defines = _set_define(cpp_defines, "USB_VID", 0x1209)
cpp_defines = _set_define(cpp_defines, "USB_PID", 0xF0C1)
cpp_defines = _set_define(cpp_defines, "USB_MANUFACTURER", '\"JayTek\"')
cpp_defines = _set_define(cpp_defines, "USB_PRODUCT", '\"OpenSmartFocuser\"')

env.Replace(CPPDEFINES=cpp_defines)
