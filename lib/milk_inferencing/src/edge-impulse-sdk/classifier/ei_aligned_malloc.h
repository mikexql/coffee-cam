/*
 * EDGE IMPULSE - PSRAM ALLOCATOR
 */

#ifndef _EI_ALIGNED_MALLOC_H_
#define _EI_ALIGNED_MALLOC_H_

#include <stdlib.h>
#include <Arduino.h>

inline void *ei_aligned_malloc(size_t size, size_t alignment) {
    // Explicitly allocate in PSRAM (SPIRAM)
    // MALLOC_CAP_8BIT ensures it is byte-accessible (required for TFLite)
    void *ptr = heap_caps_aligned_alloc(alignment, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!ptr) {
        // Fallback to internal RAM if PSRAM fails (optional, but good safety)
        ptr = heap_caps_aligned_alloc(alignment, size, MALLOC_CAP_8BIT);
    }

    return ptr;
}

inline void *ei_aligned_calloc(size_t size, size_t alignment) {
    void *ptr = ei_aligned_malloc(size, alignment);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

inline void ei_aligned_free(void *ptr) {
    free(ptr);
}

#endif // _EI_ALIGNED_MALLOC_H_