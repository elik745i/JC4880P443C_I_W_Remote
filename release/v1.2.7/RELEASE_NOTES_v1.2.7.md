# JC4880P443C_I_W_Remote v1.2.7

## Highlights

- Added a native Web Server launcher app with quick-access curtain control, local mDNS discovery, captive-portal friendly AP behavior, SD-card `/web` hosting with SPIFFS fallback, and an embedded recovery uploader when site files are missing.
- Fixed system tap-sound volume handling so changes from the quick-access curtain take effect immediately instead of only after closing the sheet.
- Keeps the matching ESP32-C6 coprocessor firmware assets required for BLE and ZigBee support on this firmware line.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6C86C0` bytes, leaving `0x0C7940` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x181AD0` bytes, leaving `0x05E530` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x191AD0` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.2.7_ota.bin`
- `JC4880P443C_I_W_Remote_v1.2.7_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.2.7_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.2.7_full_flash.zip`