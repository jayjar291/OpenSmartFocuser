# Documentation

This folder contains the operating notes for the firmware, menu system, serial protocol, and hardware configuration.

## What the Focuser Can Do

- Move the focuser to an absolute step position.
- Move the focuser relative to the current position.
- Home the focuser and report position, speed, limits, and movement state.
- Store, recall, edit, and remove presets.
- Control optional add-ons such as a shutter servo and flat panel.
- Run the on-device menu for local operation and settings changes.

## Menu Navigation

The on-device menu is driven by the physical buttons defined in the board pin configuration.

- `UP` moves through menu items or increases a setting.
- `DOWN` moves through menu items or decreases a setting.
- `SELECT` opens the highlighted item, confirms a setting, or starts the selected action.
- `ENDSTOP` is used by the firmware as a hardware input for homing and safety logic, not as a normal menu button.
- `BOOT` is the ESP32 bootloader pin and is used for flashing or recovery.

The main menu currently exposes presets, device information, homing, settings, motor enable/disable, and optional add-on controls when those features are compiled in.

## Serial Protocol

Serial commands must be framed as `:TOKENpayload#`.

The protocol is intended for host software, automation, and debugging. Use the commands below when you need to query state, control motion, manage presets, or integrate with external software.

### Status and Query Commands

- `:PP#` heartbeat. Use this to confirm the device is alive and the serial link is working.
- `:GP#` get the current focuser position in steps. Use this before or after movement when syncing state with host software.
- `:GM#` get the current movement state. Use this to check whether the focuser is idle, moving, homing, or in error.
- `:GS#` get the current speed preset. Use this when a client needs to mirror the active speed setting.
- `:GL#` get the configured software limits. Use this before issuing travel commands if your client needs to enforce range checks.
- `:CI#` get the configured motor current in milliamps. Use this for diagnostics or driver verification.
- `:CU#` get the configured microstep setting. Use this for diagnostics or startup validation.
- `:VF#` get firmware version. This command is listed in the protocol index but is currently a placeholder in the firmware.

### Movement Commands

- `:MA<steps>#` move to an absolute position. Use this for direct positioning when the target step count is already known.
- `:MR<steps>#` move relative to the current position. Use this for jog-style movement or small corrections.
- `:MH#` halt motion immediately. Use this as the emergency stop command.
- `:MS<0-4>#` set the movement speed preset. Use this when you need to switch between the configured speed profiles.
- `:SP<steps>#` override the current position. Use this only when you need to re-synchronize the software position after maintenance or a manual mechanical reset.

### Homing and System Commands

- `:HM#` start homing. Use this to find the reference position after power-up or when you need to re-establish the travel origin.
- `:TM#` toggle the motor enable state. Use this for quick local control when the current state is unknown.
- `:DM#` disable the motor. Use this when you want to cut motor drive without changing the rest of the device state.
- `:EM#` enable the motor. Use this before moving if the motor has been disabled.
- `:RB#` reboot the system. Use this only when the device is idle or when you intentionally need a restart.

### Preset Commands

- `:PL#` list stored presets. Use this to populate a preset browser in host software.
- `:PR<presetId>#` get a preset by ID. Use this when you need the name and step value for one stored preset.
- `:PA<steps>,<name>#` add a new preset. Use this when saving the current position as a named target.
- `:PS<presetId>,<steps>,<name>#` update an existing preset. Use this when renaming or retargeting a stored preset.
- `:PG<presetId>#` go to a preset by ID. Use this for one-touch recall from the stored list.
- `:PC<presetId>#` delete a preset by ID. Use this when removing outdated entries.

### Star Map Target Commands

- `:TI#` get the current DSO target.
- `:TG<RA>,<DEC>,<name>#` set the DSO target.
- `:TC#` clear the DSO target.

These commands are currently placeholders in the firmware and return a temporary response. Do not depend on them for production automation yet.

### Optional Add-on Commands

- `:AQ#` query installed add-ons. Use this to detect whether the shutter or flat panel is present.
- `:FP<brightness>#` set flat panel brightness from 0 to 255. Use this only when the flat panel add-on is installed.
- `:SV<position>#` set the shutter position. Use this only when the shutter add-on is installed.

## Optional Add-ons

The firmware can expose two optional accessories.

- Shutter servo: controlled by `PIN_SHUTTER_SERVO` and exposed through the shutter menu and `:SV` command.
- Flat panel: controlled by `PIN_FLAT_FRAME_PANEL` and exposed through the menu and `:FP` command.

If an add-on is not compiled into the current build, the related menu items and commands should not be relied on.

## Current Pin Definitions

These pins come from [firmware/include/board_config/esp32_s3_devkitc1_pins.h](../firmware/include/board_config/esp32_s3_devkitc1_pins.h).

| Function | Pin | Notes |
| --- | ---: | --- |
| `PIN_TMC_STEP` | 4 | Step output for the TMC2209 driver |
| `PIN_TMC_DIR` | 5 | Direction output for the TMC2209 driver |
| `PIN_TMC_ENABLE` | 6 | Enable output for the TMC2209 driver |
| `PIN_TMC_UART_TX` | 15 | UART TX for driver configuration |
| `PIN_TMC_UART_RX` | 16 | UART RX for driver configuration |
| `PIN_LCD_SCK` | 12 | ST7789 SPI clock |
| `PIN_LCD_MOSI` | 11 | ST7789 SPI data out |
| `PIN_LCD_MISO` | -1 | Not used |
| `PIN_LCD_CS` | 10 | ST7789 chip select |
| `PIN_LCD_DC` | 9 | ST7789 data/command |
| `PIN_LCD_RST` | 8 | ST7789 reset |
| `PIN_LCD_BL` | 17 | Backlight control |
| `PIN_LED` | 48 | On-board/status LED |
| `PIN_BUTTON_UP` | 35 | Menu up input |
| `PIN_BUTTON_DOWN` | 37 | Menu down input |
| `PIN_BUTTON_SELECT` | 36 | Menu select input |
| `PIN_BUTTON_ENDSTOP` | 38 | Endstop/safety input |
| `PIN_BUTTON_BOOT` | 0 | Bootloader input |
| `PIN_SHUTTER_SERVO` | 18 | Optional shutter add-on |
| `PIN_FLAT_FRAME_PANEL` | 13 | Optional flat panel add-on |

## Notes

- The menu and serial protocol should stay aligned with the current firmware behavior.
- If you change the board or add-ons, update the pin definition file first and then refresh this document.
- If a serial command is added or removed, update the command list here at the same time.