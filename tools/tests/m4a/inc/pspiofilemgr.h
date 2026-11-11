/* psp5 – made by 08Weber */
#pragma once
#include <fcntl.h>
#include <unistd.h>
#include "psptypes.h"
#define PSP_O_RDONLY O_RDONLY
#define PSP_SEEK_SET SEEK_SET
#define PSP_SEEK_END SEEK_END
static inline int sceIoOpen(const char *p, int f, int m) { (void)m; return open(p, f); }
static inline int sceIoClose(int fd) { return close(fd); }
static inline int sceIoRead(int fd, void *b, int n) { return (int)read(fd, b, n); }
static inline int sceIoLseek32(int fd, int o, int w) { return (int)lseek(fd, o, w); }
