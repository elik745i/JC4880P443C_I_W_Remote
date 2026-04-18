# App Structure

Use this layout for new apps under `components/apps` so they match the existing Brookesia apps in this project.

## Required shape

- Create one folder per app: `components/apps/<app_name>/`
- Keep the app entrypoints in that folder:
  - `<AppName>.hpp`
  - `<AppName>.cpp`
- Derive the app class from `ESP_Brookesia_PhoneApp`
- Implement the Brookesia lifecycle used by the other apps:
  - `init()`
  - `run()`
  - `pause()` when needed
  - `resume()` when needed
  - `back()`
  - `close()`

## Recommended folders

- `assets/` for launcher icons and app-local image assets
- `ui/` or `app_gui/` only if the app has generated or separated UI code

## Wiring rules

- Add the app header include to `components/apps/apps.h`
- Instantiate and install the app in `main/main.cpp`
- Keep app-specific storage paths inside the app instead of hardcoding behavior in `main/main.cpp`
- Fail gracefully when optional resources are missing so boot continues

## UI rules

- Use a dedicated root screen or screen tree owned by the app
- Keep navigation behavior inside the app, including back handling
- Prefer app-local helper functions over global state unless the app already follows a different established pattern

## Storage rules

- Treat `/sdcard` as optional and check availability before using it
- Treat `BSP_SPIFFS_MOUNT_POINT` as always-on app storage when mounted by startup
- Avoid `ESP_ERROR_CHECK` for optional content paths that can be absent on user devices

## Build rules

- Keep new sources under `components/apps/<app_name>/`; the app component already auto-collects sources recursively
- Reuse the existing ESP-IDF build workflow and verify with a full project build after adding the app
