/* psp5 – made by Mille Weber */
#ifndef BOOT_H
#define BOOT_H

/* Visar startskärmen (ca 5 s, valfri knapp hoppar över). Kräver gfx_init och sound_init.
 * Avbryts om *running blir 0. */
void boot_run(volatile int *running);

#endif
