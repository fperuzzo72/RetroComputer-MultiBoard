/* pc8086.h - lib/pc8086's headers, by path. They are kept off the global
 * include path on purpose (memory.h, video.h, disk.h would shadow other
 * headers by those names); see lib/pc8086/include/README. */
#ifndef PC8086_H
#define PC8086_H
#include "../../lib/pc8086/src/bios.h"
#include "../../lib/pc8086/src/cpu8086.h"
#include "../../lib/pc8086/src/disk.h"
#include "../../lib/pc8086/src/embedded_8086tiny_bios.h"
#include "../../lib/pc8086/src/font8x16.h"
#include "../../lib/pc8086/src/interrupts.h"
#include "../../lib/pc8086/src/memory.h"
#include "../../lib/pc8086/src/ports.h"
#include "../../lib/pc8086/src/speaker.h"
#include "../../lib/pc8086/src/video.h"
#include "../../lib/pc8086/src/xms.h"
#endif
