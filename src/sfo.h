/* psp5 – made by Mille Weber */
#ifndef SFO_H
#define SFO_H

#include <psptypes.h>

/* Kopierar strängvärdet för nyckeln `key` till out (nollterminerat).
 * Returnerar 1 om nyckeln hittades, annars 0. */
int sfo_get_string(const u8 *sfo, u32 sfo_size, const char *key, char *out, int out_size);

#endif
