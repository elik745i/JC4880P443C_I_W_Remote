# JC4880P443C_I_W_Remote v1.3.1

## Highlights

- Added bootloader-backed OTA rollback handling so a newly updated P4 image can roll back automatically if it crashes or bootloops before the post-update grace period marks it healthy.
- Added a default-enabled `Auto Update` switch on the Firmware page under the installed project/version block so OTA discovery can either start the preferred update immediately or wait for manual confirmation.
- Renamed the main ESP-IDF P4 project and generated firmware image from `esp_brookesia_demo` to `ESP32P4_Remote`.
- Updated product versioning and README/release documentation for the v1.3.1 release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6EF1F0` bytes, leaving `0x0A0E10` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x1C7E50` bytes, leaving `0x0181B0` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D7E50` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.1_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.1_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.1_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.1_full_flash.zip`
