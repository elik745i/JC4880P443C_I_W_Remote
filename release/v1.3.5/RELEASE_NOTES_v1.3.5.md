# JC4880P443C_I_W_Remote v1.3.5

## Highlights

- Adds the native Labyrinth launcher app with 100 IMU-driven levels, decreasing per-level time limits, score tracking, and a last-10-attempt results chart with retry and quit actions.
- Finishes the IMU integration path with runtime autodetection, shared live telemetry, and live display autorotation using a user-selectable `X`, `Y`, or `Z` control axis.
- Sets BMI160 as the default IMU profile and wiring reference: `VCC -> JP1 VCC3V3`, `GND -> JP1 GND`, `SDA -> GPIO31`, `SCL -> GPIO30`, `INT1 -> GPIO50` optional, `INT2 -> GPIO51` optional.
- Fixes the Labyrinth build integration by registering the app in the launcher build surface and removing the invalid Kconfig dependency that blocked clean reconfigure.
- Verified flash and boot on the ESP32-P4 target after the release build; serial output reaches the normal runtime command banner without an immediate panic or reset loop.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x7652F0` bytes, leaving `0x05AD10` bytes free in either `0x7C0000` OTA slot.
- ESP32-C6 app image: `0x1C8690` bytes, leaving `0x017970` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D8690` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.5_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.5_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.5_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.5_full_flash.zip`
