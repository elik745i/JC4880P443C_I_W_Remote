# JC4880P443C_I_W_Remote v1.2.12

## Highlights

- Added a complete Local Controller path with analog X/Y input, MCP23017 button expansion, live preview updates, serial diagnostics, and the missing `Key` action on the default MCP `B4` mapping.
- Added Local Controller Neopixel controls with configurable GPIO, brightness, palette, and effect selection backed by a non-blocking WS2812 runtime.
- Added Local Controller haptic GPIO and strength controls with immediate test pulses, and fixed the slow local analog preview refresh cadence.
- Resolved the ADC2 conflict between battery sampling and Local Controller analog input by handing ADC2 ownership to the active subsystem.
- Updated product versioning and README/release documentation for the v1.2.12 release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6ECD10` bytes, leaving `0x0A32F0` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.2.12_ota.bin`
- `JC4880P443C_I_W_Remote_v1.2.12_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.2.12_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.2.12_full_flash.zip`