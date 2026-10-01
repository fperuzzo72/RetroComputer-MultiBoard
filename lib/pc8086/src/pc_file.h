/* pc_file.h - NOT UPSTREAM. disk.c reads its images with stdio; on the
 * e-ink boards the card is freeink-sdk's SdFat, with no stdio behind it.
 * disk.c includes this, and these names stand in for stdio's. The
 * implementations are this project's (src/pc/pc_files.cpp), stdio again
 * on the development machine. */
#ifndef PC_FILE_H
#define PC_FILE_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pc_file pc_file;
pc_file *pc_fopen(const char *path, const char *mode);
int pc_fclose(pc_file *f);
size_t pc_fread(void *b, size_t sz, size_t n, pc_file *f);
size_t pc_fwrite(const void *b, size_t sz, size_t n, pc_file *f);
int pc_fseek(pc_file *f, long off, int whence);
long pc_ftell(pc_file *f);
int pc_fflush(pc_file *f);

#ifdef __cplusplus
}
#endif

#define FILE pc_file
#define fopen pc_fopen
#define fclose pc_fclose
#define fread pc_fread
#define fwrite pc_fwrite
#define fseek pc_fseek
#define ftell pc_ftell
#define fflush pc_fflush

#endif
