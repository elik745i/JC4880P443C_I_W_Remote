# JC4880P443C_I_W_Remote v1.3.6

## Highlights

- Adds app-local Labyrinth axis remapping with `X Axis`, `Y Axis`, and `Z Axis` dropdowns that can independently map ball movement to `+X`, `-X`, `+Y`, `-Y`, `+Z`, or `-Z` without changing system-wide IMU orientation behavior.
- Persists IMU calibration and zero-orientation data to NVS immediately after capture, so calibration survives reboot and reload instead of needing to be redone after restart.
- Fixes BMI160 zero-yaw restore so saved orientation zeroing loads correctly on the next boot instead of coming back distorted.
- Keeps display autorotate and Labyrinth ball controls separated: system autorotate continues to use the global IMU/display path, while Labyrinth remapping only affects in-game ball motion.
- Refreshes the README, VS Code release task, and packaged release assets for a new `v1.3.6` GitHub release.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x766AE0` bytes, leaving `0x059520` bytes free in either `0x7C0000` OTA slot.
- ESP32-C6 app image: `0x1C8690` bytes, leaving `0x017970` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x1D8690` bytes.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.3.6_ota.bin`
- `JC4880P443C_I_W_Remote_v1.3.6_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.3.6_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.3.6_full_flash.zip`