# Firmware

ESP32-S3 firmware for OpenSmartFocuser, including motor control, menu/display UX, and serial command handling.

## Folder Structure

```text
firmware/
├── include/        # Firmware headers and configuration values
├── src/            # Firmware implementation files
├── platformio.ini  # PlatformIO build configuration
└── backup/         # Local backup snapshots (not part of firmware runtime)
```

## Build

```bash
cd firmware
pio run
```
