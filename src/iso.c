/* psp5 – made by Mille Weber */
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <zlib.h>
#include "iso.h"

#define SECTOR 2048
#define PVD_OFFSET (16 * SECTOR)

/* --- Funktioner som libcisoread förväntar sig av programmet --- */

/* Rå deflate (CSO). zlib i stället för sctrlDeflateDecompress, så att det
 * fungerar även utan CFW (PPSSPP). Returnerar antal dekomprimerade byte. */
int zlib_inflate(void *dst, int dst_len, void *src)
{
    z_stream s;
    memset(&s, 0, sizeof(s));
    if (inflateInit2(&s, -15) != Z_OK)
        return -1;
    s.next_in = src;
    s.avail_in = dst_len + 1024; /* ett block är aldrig större än okomprimerat + marginal */
    s.next_out = dst;
    s.avail_out = dst_len;
    int ret = inflate(&s, Z_FINISH);
    int out = dst_len - s.avail_out;
    inflateEnd(&s);
    return (ret == Z_STREAM_END || ret == Z_OK || ret == Z_BUF_ERROR) ? out : -1;
}

/* JSO (LZO) stöds inte – vi skannar bara iso/cso/zso. */
int lzo1x_decompress(const void *src, unsigned src_len, void *dst, unsigned *dst_len, void *wrk)
{
    return -1;
}

static int read_raw(void *arg, void *buf, uint32_t size, uint32_t offset)
{
    SceUID fd = (SceUID)arg;
    sceIoLseek32(fd, offset, PSP_SEEK_SET);
    return sceIoRead(fd, buf, size);
}

/* --- Läsare --- */

int iso_open(IsoReader *r, const char *path)
{
    memset(r, 0, sizeof(*r));
    r->fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (r->fd < 0)
        return r->fd;

    r->ciso.reader_arg = (void *)r->fd;
    r->ciso.read_data = read_raw;
    r->ciso.memalign = (void *(*)(unsigned int, size_t))memalign;
    r->ciso.free = free;
    r->compressed = (ciso_open(&r->ciso) == CISO_OK);
    return 0;
}

void iso_close(IsoReader *r)
{
    if (r->compressed)
        ciso_close(&r->ciso);
    if (r->fd >= 0)
        sceIoClose(r->fd);
    r->fd = -1;
}

int iso_read(IsoReader *r, void *buf, u32 size, u32 offset)
{
    if (r->compressed)
        return ciso_read(&r->ciso, buf, size, offset);
    return read_raw((void *)r->fd, buf, size, offset);
}

static u32 le32(const u8 *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

/* Söker efter `name` i en katalog (extent/size). Namn i ISO9660 kan sluta på ";1". */
static int find_in_dir(IsoReader *r, u32 dir_lba, u32 dir_size, const char *name, IsoFile *out)
{
    static u8 sector[SECTOR];
    int name_len = strlen(name);

    for (u32 pos = 0; pos < dir_size; pos += SECTOR) {
        if (iso_read(r, sector, SECTOR, dir_lba * SECTOR + pos) != SECTOR)
            return 0;

        for (u32 i = 0; i < SECTOR;) {
            u8 rec_len = sector[i];
            if (rec_len == 0)
                break; /* resten av sektorn är tom */
            if (i + rec_len > SECTOR)
                break;

            const u8 *rec = sector + i;
            u8 id_len = rec[32];
            const char *id = (const char *)rec + 33;
            int len = id_len;
            if (len >= 2 && id[len - 2] == ';')
                len -= 2;

            if (len == name_len && strncasecmp(id, name, len) == 0) {
                out->offset = le32(rec + 2) * SECTOR;
                out->size = le32(rec + 10);
                return 1;
            }
            i += rec_len;
        }
    }
    return 0;
}

int iso_find(IsoReader *r, const char *path, IsoFile *out)
{
    u8 pvd[256];
    if (iso_read(r, pvd, sizeof(pvd), PVD_OFFSET) != sizeof(pvd))
        return 0;
    if (memcmp(pvd + 1, "CD001", 5) != 0)
        return 0;

    /* rotkatalogens post ligger på offset 156 i PVD */
    IsoFile cur = { le32(pvd + 156 + 2) * SECTOR, le32(pvd + 156 + 10) };

    char part[64];
    const char *p = path;
    while (*p) {
        const char *slash = strchr(p, '/');
        int len = slash ? slash - p : (int)strlen(p);
        if (len <= 0 || len >= (int)sizeof(part))
            return 0;
        memcpy(part, p, len);
        part[len] = 0;

        if (!find_in_dir(r, cur.offset / SECTOR, cur.size, part, &cur))
            return 0;
        p += len;
        if (*p == '/')
            p++;
    }
    *out = cur;
    return 1;
}

void *iso_load(IsoReader *r, const char *path, u32 *size_out, u32 max_size)
{
    IsoFile f;
    if (!iso_find(r, path, &f) || f.size == 0 || f.size > max_size)
        return NULL;

    void *buf = malloc(f.size);
    if (!buf)
        return NULL;
    if (iso_read(r, buf, f.size, f.offset) != (int)f.size) {
        free(buf);
        return NULL;
    }
    if (size_out)
        *size_out = f.size;
    return buf;
}
