# OpenSmartFocuser

OpenSmartFocuser is an open-source telescope focuser project with:
- ESP32-S3 firmware (display/menu, motor control, serial command handling)
- an INDI driver scaffold for host-side integration
- hardware design files for PCB and related fabrication assets

## Build Targets

- `firmware/` contains the PlatformIO project for the device firmware.
- `drivers/indi/` contains the CMake-based INDI focuser driver.

## Folder Structure

```text
OpenSmartFocuser/
├── .github/
│   ├── ISSUE_TEMPLATE/        # GitHub issue templates
│   └── workflows/             # CI workflows (firmware and INDI driver builds)
├── docs/                      # Project documentation and operating notes
├── drivers/
│   └── indi/                  # INDI driver scaffold and CMake config
├── firmware/
│   ├── include/               # Firmware headers/config
│   ├── src/                   # Firmware source code
│   ├── platformio.ini         # PlatformIO project config
│   └── backup/                # Local backup files
└── hardware/
    ├── README.md              # Hardware folder overview
    ├── cad/                   # Mechanical CAD-related files
    │   └── README.md          # CAD file overview
    └── pcb/                   # KiCad project and fabrication outputs
        ├── README.md          # PCB project overview
        └── fabrication/       # Fabrication export guidance and outputs
```

## Continuous Integration

- `firmware-build.yml` builds the firmware project on Ubuntu with PlatformIO.
- `indi-driver-build.yml` configures and builds the INDI driver on Ubuntu with system INDI, Nova, GSL, and ZLIB development packages.

## Local Builds

Firmware:

```bash
cd firmware
pio run
```

INDI driver:

```bash
cd drivers/indi
cmake -S . -B build
cmake --build build --parallel
sudo cmake --install build
sudo udevadm control --reload-rules
sudo udevadm trigger
```

The INDI driver build expects CMake, a C++ compiler, and the development packages for INDI, Nova, GSL, and ZLIB.
The install step also deploys a udev rule that creates the stable serial symlink `/dev/OSF` for OpenSmartFocuser devices (USB VID:PID `1209:F0C1`).
