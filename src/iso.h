/* psp5 – made by Mille Weber */
#ifndef ISO_H
#define ISO_H

#include <pspkernel.h>
#include <ciso.h>

/* Läser okomprimerade ISO:er och CSO/ZSO via libcisoread. */
typedef struct {
    SceUID fd;
    int compressed;
    CisoFile ciso;
} IsoReader;

/* Läge och storlek på en fil inuti ISO:n (i okomprimerade byte). */
typedef struct {
    u32 offset;
    u32 size;
} IsoFile;

int iso_open(IsoReader *r, const char *path);
void iso_close(IsoReader *r);
int iso_read(IsoReader *r, void *buf, u32 size, u32 offset);

/* Hittar en fil via sökväg relativt skivans rot, t.ex. "PSP_GAME/PARAM.SFO".
 * Returnerar 1 och fyller `out` om filen finns. */
int iso_find(IsoReader *r, const char *path, IsoFile *out);

/* Läser hela filen till en nyallokerad buffert (frigörs med free). */
void *iso_load(IsoReader *r, const char *path, u32 *size_out, u32 max_size);

#endif
