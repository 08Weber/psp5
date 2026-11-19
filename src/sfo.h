/* psp5 – made by 08Weber */
#ifndef SFO_H
#define SFO_H

#include <psptypes.h>

/* Kopierar strängvärdet för nyckeln `key` till out (nollterminerat).
 * Returnerar 1 om nyckeln hittades, annars 0. */
int sfo_get_string(const u8 *sfo, u32 sfo_size, const char *key, char *out, int out_size);

/* Antal poster i PARAM.SFO (0 om datat är trasigt). */
int sfo_count(const u8 *sfo, u32 sfo_size);
/* Post nr `i` (0..sfo_count-1) som läsbara texter (nyckel och värde). 1 om giltig. */
int sfo_entry(const u8 *sfo, u32 sfo_size, int i, char *key, int key_size, char *val, int val_size);

#endif
