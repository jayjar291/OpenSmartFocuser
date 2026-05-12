# OpenSmartFocuser

OpenSmartFocuser is an open-source telescope focuser project with:
- ESP32-S3 firmware (display/menu, motor control, serial command handling)
- an INDI driver scaffold for host-side integration
- hardware design files for PCB and related fabrication assets

## Folder Structure

```text
OpenSmartFocuser/
├── .github/
│   ├── ISSUE_TEMPLATE/        # GitHub issue templates
│   └── workflows/             # CI workflows (firmware build)
├── drivers/
│   └── indi/                  # INDI driver scaffold and CMake config
├── firmware/
│   ├── include/               # Firmware headers/config
│   ├── src/                   # Firmware source code
│   ├── platformio.ini         # PlatformIO project config
│   └── backup/                # Local backup files
└── hardware/
    ├── cad/                   # Mechanical CAD-related files
    └── pcb/                   # KiCad project and fabrication outputs
```
