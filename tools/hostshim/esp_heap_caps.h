#ifndef HOSTSHIM_HEAP_CAPS_H
#define HOSTSHIM_HEAP_CAPS_H
#include "hostshim.h"
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DEFAULT 0
#define MALLOC_CAP_DMA 0
static inline void *heap_caps_malloc(size_t n, unsigned caps) { (void)caps; return malloc(n); }
static inline void *heap_caps_calloc(size_t a, size_t n, unsigned caps) { (void)caps; return calloc(a, n); }
static inline size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 1u << 20; }
static inline size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 1u << 20; }
#endif
