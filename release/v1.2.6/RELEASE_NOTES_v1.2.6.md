# JC4880P443C_I_W_Remote v1.2.6

## Highlights

- Added Wi-Fi AP mode controls in Settings so the device can expose its own hotspot with saved enable, SSID, and password configuration.
- Hardened the SEGA app with safer Genesis save-state loading, compatibility rejection for older broken Genesis states, save-slot validation, and corrected save-preview orientation across handheld rotations.
- Unified on-screen keyboard behavior across Settings, File Manager, and Music Player so password visibility and shift / caps handling behave consistently.
- Expanded the top-right quick-access curtain into a full-width power-and-audio panel with restart, shutdown confirmation, sleep placeholder, and airplane-mode toggle controls.
- Includes the matching ESP32-C6 coprocessor firmware assets required for BLE and ZigBee support on this firmware line.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x6682F0` bytes, leaving `0x127D10` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x181AD0` bytes, leaving `0x05E530` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x191AD0` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.2.6_ota.bin`
- `JC4880P443C_I_W_Remote_v1.2.6_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.2.6_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.2.6_full_flash.zip`