/* config_defs.h - NOT UPSTREAM: reduced to what the core reads. */
#ifndef CONFIG_DEFS_H
#define CONFIG_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 0: hard disk geometry from the image's own BPB/partition table (the
 * default upstream); 1: DOS 3.3-style fixed CHS. Defined in pc_glue. */
extern int g_c_drive_dos33_compat;

#ifdef __cplusplus
}
#endif

#endif
