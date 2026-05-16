# JC4880P443C_I_W_Remote v1.4.0

## Highlights

- Bumps the main ESP32-P4 firmware to `1.4.0` so the on-device version display, OTA comparison logic, and GitHub release tag stay aligned for this release.
- Adds a dedicated Settings `GPIO Control` page for ESP32-P4 header testing with input, output, PWM, wave, timer, and alarm modes plus centralized multi-pin output actions.
- Makes GPIO timer and alarm flows asynchronous and non-blocking, restores editable on-screen keyboard entry where needed, rescans `/sdcard/wav` and other supported audio formats for timer and alarm playback, and keeps only one active timer alert owner to avoid overlapping melodies.
- Continues the Settings split-out work while fixing regressions that showed up around music playback, shared audio control, Wi-Fi startup behavior, and GPIO/audio runtime handling after the refactor.
- Refreshes the README and packaged release assets for the `v1.4.0` GitHub release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `7,845,760` bytes, leaving `346,240` bytes free in either `0x7D0000` OTA slot.
- ESP32-C6 app image: `1,869,456` bytes.
- ESP32-C6 merged flash image: `1,934,992` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.4.0_ota.bin`
- `JC4880P443C_I_W_Remote_v1.4.0_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.4.0_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.4.0_full_flash.zip`
