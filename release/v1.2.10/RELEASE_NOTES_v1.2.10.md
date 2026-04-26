# JC4880P443C_I_W_Remote v1.2.10

## Highlights

- Reworked Joypad settings around generated BLE and Local controller layouts, including a live layout-driven BLE preview and on-device stick calibration preview that follows the active Joypad layout.
- Added the local Joypad Layout Configurator workflow under `tools/joypad_layout/`, with direct generated-header round-tripping, `Read` and `Apply` actions, artwork regeneration, and matching firmware-side layout assets.
- Hardened hosted Wi-Fi startup so Settings and Joypad screens remain accessible even when the ESP-Hosted C6 path is unavailable during Wi-Fi initialization.
- Updated the project README and release documentation to cover the current Joypad configurator flow, matching C6 release dependency, and manual build/flash expectations.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6E5800` bytes, leaving `0x0AA800` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.2.10_ota.bin`
- `JC4880P443C_I_W_Remote_v1.2.10_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.2.10_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.2.10_full_flash.zip`