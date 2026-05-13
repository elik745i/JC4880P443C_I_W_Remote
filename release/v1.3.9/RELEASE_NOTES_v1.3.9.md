# JC4880P443C_I_W_Remote v1.3.9

## Highlights

- Bumps the main ESP32-P4 firmware to `1.3.9` and keeps the OTA-visible release tag aligned with the on-device firmware version handling.
- Fixes post-OTA release-version matching so `v1.3.9` GitHub tags and `1.3.9` firmware builds are treated as the same release instead of drifting into repeated update prompts or mismatched post-install notes.
- Extends LoRa Mesh chat with SD-card transcript restore, clear-chat deletion, optimistic outgoing bubbles with async delivery markers, and send/receive notification sounds.
- Adds generated system event tones for boot, OTA update available, OTA success, reboot flows, chat send, chat receive, and error recovery.
- Updates the README, VS Code release task, and release packaging metadata for the `v1.3.9` release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `7,785,136` bytes, leaving `341,328` bytes free in either `0x7C0000` OTA slot.
- ESP32-C6 app image: `1,869,520` bytes.
- ESP32-C6 merged flash image: `1,935,056` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.9_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.9_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.9_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.9_full_flash.zip`
