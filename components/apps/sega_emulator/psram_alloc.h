#pragma once

#include <stdlib.h>

#ifdef IRAM_ATTR
#undef IRAM_ATTR
#endif

#include "esp_heap_caps.h"

static inline void *sega_psram_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        return ptr;
    }

    return malloc(size);
}

static inline void *sega_psram_calloc(size_t count, size_t size)
{
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        return ptr;
    }

    return calloc(count, size);
}