# JC4880P443C_I_W_Remote v1.2.9

## Highlights

- Added ESP32-C6-backed BLE game controller support with Bluepad32-based input forwarding to the ESP32-P4 host, including analog sticks, analog triggers, and broader controller button coverage.
- Added a dedicated BLE controller settings flow with a live controller visualizer, faster live refresh, and persistent per-controller calibration that is reused across reconnects.
- Documented tested controller setup for the TOBO BSP-D9, including the pairing shortcut, BLE settings requirement, and matching C6 release dependency.
- Added top-level modular menuconfig switches for major launcher apps and feature domains so trimmed firmware variants can be built without hand-editing the project.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6DE150` bytes, leaving `0x0B1EB0` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.2.9_ota.bin`
- `JC4880P443C_I_W_Remote_v1.2.9_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.2.9_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.2.9_full_flash.zip`