#pragma once

#include <stdint.h>

class ESP_Brookesia_Phone;

namespace device_security {

constexpr const char *kNvsNamespace = "storage";
constexpr const char *kDeviceLockEnabledKey = "lock_dev_en";
constexpr const char *kDeviceLockPinKey = "lock_dev_pin";
constexpr const char *kSettingsLockEnabledKey = "lock_set_en";
constexpr const char *kSettingsLockPinKey = "lock_set_pin";

enum class LockType : uint8_t {
    Device = 0,
    Settings,
};

using RequestCallback = void (*)(bool success, void *user_data);

void init(ESP_Brookesia_Phone *phone);
void promptBootUnlockIfNeeded(void);
bool isLockEnabled(LockType type);
bool isPromptActive(void);
void requestEnable(LockType type, RequestCallback callback, void *user_data);
void requestDisable(LockType type, RequestCallback callback, void *user_data);

} // namespace device_security

extern "C" bool jc_security_handle_app_launch_request(int app_id, const char *app_name);