# JC4880P443C_I_W_Remote v1.2.4

## Highlights

- Fixed GitHub OTA update reliability so the firmware checker follows redirected release assets correctly, keeps visible progress during checks and flashes, preserves failure status instead of silently dropping the flow, and no longer panics during the final OTA verification step.
- Reduced fresh-boot internal SRAM pressure by deferring heavy Settings screens until first use and moving more SEGA emulator permanent state, preview buffers, and SMS / Genesis scratch data into PSRAM-backed BSS.
- Includes the matching ESP32-C6 coprocessor firmware assets required for BLE and ZigBee support on this firmware line.

## Validated Build Sizes

- Main ESP32-P4 OTA app image: `0x65DE80` bytes, leaving `0x132180` bytes free in the smaller OTA slot.
- ESP32-C6 app image: `0x181AD0` bytes, leaving `0x05E530` bytes free in the 1920 KB app slot.
- ESP32-C6 merged flash image: `0x191AD0` bytes.

## Consolidated Changes From Previous Releases

### Firmware And OTA

- GitHub release OTA detection now works with raw `.bin` assets, installed-version display, and in-app firmware selection.
- OTA flashing now copes with GitHub redirect responses and keeps clearer status, notes, and post-install feedback in Settings.
- OTA finalization now keeps the firmware update worker on an internal RAM stack so cache-disabled image verification no longer panics at the end of flashing.
- Firmware Settings includes SD-card flashing support and a local factory reset path.
- Release assets continue to ship as OTA-safe main firmware plus full-flash bundles, with matching C6 firmware.

### Memory And Performance

- Multiple low-risk and then deeper PSRAM migrations were added across radio, music, workers, and emulator paths.
- Fresh-boot memory usage was reduced by deferring heavy Internet Radio, Image Display, SEGA, and Settings UI/runtime setup until first use.
- Large SEGA emulator permanent buffers and tables now live in PSRAM-backed BSS instead of internal SRAM where safe.

### SEGA Emulator

- Added native SEGA support for Master System, Game Gear, SG-1000, and Genesis / Mega Drive ROMs.
- Manual SAVE and LOAD replaced auto-resume behavior.
- LOAD now opens a rotated picker with up to five recent save slots and thumbnail previews stored under `/sdcard/saved_games`.
- The player also includes the optional FPS overlay and additional Genesis timing / audio pacing cleanup.

### Music Player And Internet Radio

- Radio browsing, buffering, startup prefill, and non-blocking refill behavior were hardened across several releases.
- Quick-access controls now stay visible while apps are open, with richer Music Player and Radio detail plus previous / next station controls.
- Music Player metadata, indexes, and worker stacks now prefer PSRAM more aggressively.

### System Stability

- Panic, watchdog, and reboot recovery flows were hardened with automatic reboot, boot-time recovery popups, and saved crash reports.
- Flash-backed coredumps and report export plumbing were added for post-crash analysis.
- Several stale-LVGL-object and modal-close panic paths were fixed in Settings, Radio, Music Player, and related teardown flows.

### Wireless And Coprocessor

- BLE and ZigBee support depend on the matching ESP32-C6 coprocessor release built from `coprocessor_c6/`.
- The C6 release line includes the ZigBee storage partition fix and current hosted-wireless firmware state.

## Included Assets

- `JC4880P443C_I_W_Remote_v1.2.4_ota.bin`
- `JC4880P443C_I_W_Remote_v1.2.4_full_flash.zip`
- `JC4880P443C_I_W_Remote_C6_v1.2.4_merged.bin`
- `JC4880P443C_I_W_Remote_C6_v1.2.4_full_flash.zip`
