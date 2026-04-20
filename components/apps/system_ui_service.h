#pragma once

#include <stdbool.h>

#ifdef __cplusplus

class ESP_Brookesia_Phone;

namespace system_ui_service {

bool initialize(ESP_Brookesia_Phone &phone);
void set_wifi_connected(bool connected);
void refresh_wifi_from_driver(void);

} // namespace system_ui_service

#endif