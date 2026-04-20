#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool app_storage_is_sdcard_mounted(void);
bool app_storage_ensure_sdcard_available(void);

#ifdef __cplusplus
}
#endif