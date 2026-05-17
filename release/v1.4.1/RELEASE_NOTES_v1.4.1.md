# JC4880P443C_I_W_Remote v1.4.1

## Highlights

- Bumps the main ESP32-P4 firmware to `1.4.1` so the on-device version display, OTA comparison logic, packaged assets, and GitHub release tag stay aligned for this release.
- Improves LoRa Mesh E22 handling by applying radio register writes from the active runtime config, debouncing Settings-side LoRa saves, and attempting UART TX/RX self-recovery on common swapped-pin mappings when the E22 self-check fails.
- Refines Settings `GPIO Control` behavior so the on-screen keyboard resizes and scrolls the panel correctly, timer and alarm level selectors map to the right logical state, alarm rows show armed versus active state more clearly, and GPIO output/PWM handoff is more reliable.
- Updates the shared top status bar clock to refresh every second, use the 24-hour format consistently, and stay on a single line even when the center area gets tight.
- Refreshes the README and rebuilds the packaged P4 and C6 release assets for the `v1.4.1` GitHub release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `7,850,736` bytes, leaving `341,264` bytes free in either `0x7D0000` OTA slot.
- ESP32-C6 app image: `1,869,456` bytes.
- ESP32-C6 merged flash image: `1,934,992` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.4.1_ota.bin`
- `JC4880P443C_I_W_Remote_v1.4.1_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.4.1_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.4.1_full_flash.zip`