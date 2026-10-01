#ifndef PCSHIM_HEAP_CAPS_H
#define PCSHIM_HEAP_CAPS_H
#include <stdlib.h>
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_SPIRAM 0
static inline void *heap_caps_malloc(size_t n, unsigned caps) { (void)caps; return malloc(n); }
#endif
