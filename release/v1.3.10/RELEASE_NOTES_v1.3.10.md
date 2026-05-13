# JC4880P443C_I_W_Remote v1.3.10

## Highlights

- Bumps the main ESP32-P4 firmware to `1.3.10` so the on-device version display, OTA comparison logic, and GitHub release tag stay aligned for this release.
- Replaces the hardcoded VS Code publish task with a version-derived release helper so future releases publish the tag and asset set that matches the firmware build version.
- Extends LoRa Mesh with SD-card transcript restore and clear flows, optimistic async send bubbles, chat event sounds, and serial debug commands for visible app control and self-test workflows.
- Adds generated system event tones for boot, OTA availability, OTA success, reboot flows, chat send, chat receive, and error recovery.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `7,785,136` bytes, leaving `341,328` bytes free in either `0x7C0000` OTA slot.
- ESP32-C6 app image: `1,869,520` bytes.
- ESP32-C6 merged flash image: `1,935,056` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.10_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.10_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.10_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.10_full_flash.zip`