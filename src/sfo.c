/* psp5 – made by Mille Weber */
#include <string.h>
#include "sfo.h"

#define SFO_MAGIC 0x46535000 /* "\0PSF" */

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 key_table;
    u32 data_table;
    u32 count;
} SfoHeader;

typedef struct __attribute__((packed)) {
    u16 key_offset;
    u16 fmt;
    u32 len;
    u32 max_len;
    u32 data_offset;
} SfoEntry;

int sfo_get_string(const u8 *sfo, u32 sfo_size, const char *key, char *out, int out_size)
{
    const SfoHeader *h = (const SfoHeader *)sfo;
    if (sfo_size < sizeof(SfoHeader) || h->magic != SFO_MAGIC)
        return 0;
    if (sizeof(SfoHeader) + h->count * sizeof(SfoEntry) > sfo_size)
        return 0;

    const SfoEntry *e = (const SfoEntry *)(sfo + sizeof(SfoHeader));
    for (u32 i = 0; i < h->count; i++) {
        u32 koff = h->key_table + e[i].key_offset;
        if (koff >= sfo_size || strcmp((const char *)sfo + koff, key) != 0)
            continue;

        u32 doff = h->data_table + e[i].data_offset;
        u32 len = e[i].len;
        if (doff + len > sfo_size)
            return 0;
        if (len >= (u32)out_size)
            len = out_size - 1;
        memcpy(out, sfo + doff, len);
        out[len] = 0;
        return 1;
    }
    return 0;
}
