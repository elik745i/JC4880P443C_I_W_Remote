# JC4880P443C_I_W_Remote v1.2.5

## Highlights

- Hardware Monitor now keeps always-on background history in PSRAM so CPU load, SRAM, PSRAM, Wi-Fi, battery, and CPU temperature charts still have data even if the page was closed for most of the last hour.
- Added CPU temperature history at a 10-second cadence for the last hour and removed the duplicate non-functional temperature card from the Hardware screen.
- Fixed Hardware Monitor SD-card mounted-state reporting so it follows the same app-wide storage state as File Manager instead of showing false "Not mounted" states.
- Hardware Monitor now uses the common system back gesture instead of its own dedicated top-left back button.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x662730` bytes, leaving `0x12D8D0` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x181AD0` bytes, leaving `0x05E530` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x191AD0` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.2.5_ota.bin`
- `JC4880P443C_I_W_Remote_v1.2.5_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.2.5_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.2.5_full_flash.zip`