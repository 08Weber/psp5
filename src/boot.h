/* psp5 – made by 08Weber */
#ifndef BOOT_H
#define BOOT_H

#include "gfx.h"

/* Visar startskärmen (ca 5 s, valfri knapp hoppar över). Kräver gfx_init och sound_init.
 * Avbryts om *running blir 0. */
void boot_run(volatile int *running);

/* psp5-loggan (vit med alfa, skapas första gången) och ritad 1:1 centrerad kring (cx, cy) */
Texture *logo_texture(void);
void logo_draw(float cx, float cy, u32 color);

#endif
