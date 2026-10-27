// Host build: ESP-IDF heap_caps on the plain heap.
#pragma once
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)
inline void*  heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
inline void*  heap_caps_calloc(size_t n, size_t s, uint32_t) { return calloc(n, s); }
inline void*  heap_caps_realloc(void* p, size_t n, uint32_t) { return realloc(p, n); }
inline void   heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(uint32_t) { return 64u << 20; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 16u << 20; }
inline size_t heap_caps_get_minimum_free_size(uint32_t) { return 64u << 20; }
inline size_t heap_caps_get_total_size(uint32_t) { return 128u << 20; }
