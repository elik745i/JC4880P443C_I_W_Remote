# JC4880P443C_I_W_Remote v1.3.0

## Highlights

- Added background OTA release discovery that periodically checks GitHub for newer firmware while the device is running.
- Added a one-per-boot update popup that shows the current and newer firmware versions, lets the user open the Firmware OTA page directly, and stays dismissed until the next reboot after `Cancel`.
- Added a passive update-available status icon in the top bar that does not hijack other notification icons or navigate on tap.
- Updated the post-update release-notes popup to render fullscreen on fresh reboot after a successful firmware update.
- Updated product versioning and README/release documentation for the v1.3.0 release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6EDC40` bytes, leaving `0x0A23C0` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.0_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.0_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.0_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.0_full_flash.zip`