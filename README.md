# OpenSmartFocuser

> An open-source motorised telescope focuser with INDI and ASCOM support.

![License](https://img.shields.io/github/license/YOUR-USERNAME/OpenSmartFocuser)
![Firmware Build](https://img.shields.io/github/actions/workflow/status/YOUR-USERNAME/OpenSmartFocuser/firmware-build.yml?label=firmware%20build)
![GitHub Release](https://img.shields.io/github/v/release/YOUR-USERNAME/OpenSmartFocuser)

---

## Overview

OpenSmartFocuser is an open-source, open-hardware smart focuser designed for amateur telescope setups. It provides motorised focus control with full integration into popular astronomy capture software via INDI (Linux/macOS) and ASCOM (Windows).

_TODO: Add a photo or render of the assembled focuser here._

---

## Features

- Stepper motor driven with configurable microstepping
- INDI driver for Linux/macOS (KStars/Ekos, Cartes du Ciel)
- ASCOM driver for Windows (N.I.N.A, SGP, etc.)
- Custom PCB designed in KiCad
- 3D printable enclosure (Fusion 360 source + 3MF exports)
- Fully open hardware and firmware

---

## Repository Structure

```
OpenSmartFocuser/
├── hardware/
│   ├── pcb/                  # KiCad schematic & PCB files
│   │   └── fabrication/      # Gerbers, BOM, pick-and-place exports
│   └── cad/
│       ├── step/             # Fusion 360 STEP exports
│       └── 3mf/              # 3MF print files (also on Printables)
├── firmware/                 # PlatformIO / VSCode project
│   ├── src/
│   ├── include/
│   └── platformio.ini
├── drivers/
│   ├── indi/                 # INDI driver (C++/CMake)
│   └── ascom/                # ASCOM driver (C#/.NET)
├── docs/
│   ├── assembly/             # Assembly guide
│   ├── wiring/               # Wiring diagrams & pinouts
│   └── usage/                # Software setup & usage guides
└── .github/
    └── workflows/            # CI — PlatformIO build check
```

---

## Getting Started

### 1. Build the Hardware

- PCB files are in `hardware/pcb/` — open with **KiCad 7+**
- Fabrication outputs (Gerbers, BOM) are in `hardware/pcb/fabrication/`
- 3D print the enclosure from `hardware/cad/3mf/` or download from [Printables](https://www.printables.com/model/YOUR-MODEL-ID)
- See the [Assembly Guide](docs/assembly/README.md) for step-by-step instructions

### 2. Flash the Firmware

Prerequisites: [VSCode](https://code.visualstudio.com/) + [PlatformIO extension](https://platformio.org/)

```bash
# Clone the repo
git clone https://github.com/YOUR-USERNAME/OpenSmartFocuser.git
cd OpenSmartFocuser/firmware

# Build and upload via PlatformIO
pio run --target upload
```

Edit `firmware/include/config.h` to match your motor and hardware configuration.

### 3. Install the Driver

**INDI (Linux/macOS)**  
See [drivers/indi/README.md](drivers/indi/README.md)

**ASCOM (Windows)**  
Download the installer from the [Releases](../../releases) page, or see [drivers/ascom/README.md](drivers/ascom/README.md) to build from source.

---

## Documentation

| Guide | Description |
|-------|-------------|
| [Assembly](docs/assembly/README.md) | Full build instructions |
| [Wiring](docs/wiring/README.md) | Pinouts and wiring diagrams |
| [Usage](docs/usage/README.md) | Software setup and configuration |

---

## 3D Printing

Print files are available in two places:

- **This repo:** `hardware/cad/3mf/`
- **Printables:** [https://www.printables.com/model/YOUR-MODEL-ID](https://www.printables.com/model/YOUR-MODEL-ID)

Recommended material: PETG. See [hardware/cad/3mf/README.md](hardware/cad/3mf/README.md) for full print settings.

---

## Contributing

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on PCB changes, firmware, drivers, and documentation.

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

---

## License

Hardware: [CERN-OHL-S v2](https://ohwr.org/cern_ohl_s_v2.txt)  
Firmware & Drivers: [GPL-3.0](LICENSE)  
Documentation: [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
