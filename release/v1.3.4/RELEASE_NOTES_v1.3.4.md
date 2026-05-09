# JC4880P443C_I_W_Remote v1.3.4

## Highlights

- Adds the native LoRa Mesh launcher app with persisted radio-module selection, configurable GPIO mapping for supported SPI and UART radio modules, and integrated common-chat flows.
- Hardens the LoRa Mesh startup/send path so radio bring-up and message dispatch no longer need to block the rest of the UI while the app is opening.
- Fixes the hosted Wi-Fi STA reconnect path so saved and manual connection attempts no longer reboot the device when the remote Wi-Fi stack reports transient start/config errors.
- Refreshes the release cut with the latest launcher, joystick, and firmware workspace changes already present in this repository.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x753EC0` bytes, leaving `0x06C140` bytes free in either `0x7C0000` OTA slot.
- ESP32-C6 app image: `0x1C8690` bytes, leaving `0x017970` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D8690` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.4_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.4_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.4_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.4_full_flash.zip`