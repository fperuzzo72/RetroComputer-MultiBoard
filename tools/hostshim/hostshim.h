#ifndef HOSTSHIM_H
#define HOSTSHIM_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
extern volatile int64_t hostshim_now_us;
#ifdef __cplusplus
}
#endif
#endif
