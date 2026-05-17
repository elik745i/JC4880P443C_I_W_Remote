# JC4880P443C_I_W_Remote v1.4.2

## Highlights

- Bumps the main ESP32-P4 firmware to `1.4.2` so the on-device version display, OTA comparison logic, packaged assets, and GitHub release tag stay aligned for this release.
- Restores reliable E22-400T22S LoRa UART communication by keeping the in-tree LoRa Mesh runtime/config path, correcting the E22 mode-pin handling and config timing, and dropping the failed replacement-library direction.
- Fixes the hosted ESP32-C6 Wi-Fi/audio regression path by keeping the validated hosted compatibility layer in the release build again, including the ESP-IDF 5.5.4 Wi-Fi remote compatibility fixes needed for clean packaging.
- Refreshes the README and rebuilds the packaged P4 and C6 release assets for the `v1.4.2` GitHub release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `7,857,680` bytes, leaving `334,320` bytes free in either `0x7D0000` OTA slot.
- ESP32-C6 app image: `1,869,456` bytes.
- ESP32-C6 merged flash image: `1,934,992` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.4.2_ota.bin`
- `JC4880P443C_I_W_Remote_v1.4.2_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.4.2_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.4.2_full_flash.zip`