/* psp5 – made by 08Weber */
/* M4A-filer (issue #44): MP4-boxarna läses direkt ur filen med sökningar, så att varken
 * ljuddatat (mdat) eller omslaget behöver läsas vid skanningen. För uppspelningen läses
 * ljudspårets tabeller (stsz, stco/co64, stsc) in – 4 byte per bildruta. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m4a.h"

#define FOURCC(a, b, c, d) ((u32)(a) << 24 | (u32)(b) << 16 | (u32)(c) << 8 | (u32)(d))
#define MAX_TABLE (4 * 1024 * 1024) /* större tabeller (många timmars ljud) läses inte */
#define MAX_COVER (2 * 1024 * 1024)

static const u32 AAC_RATES[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                   22050, 16000, 12000, 11025, 8000,  7350 };

static u32 be32(const unsigned char *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

typedef struct {
    u32 type, body, end; /* typ, var innehållet börjar, var boxen slutar */
} Box;

/* Läsare med ett litet fönster: boxhuvudena ligger tätt, så de flesta läsningar under
 * skanningen tas ur fönstret i stället för minneskortet. Ett fönster per anrop (inte statiskt):
 * omslagen läses i en annan tråd än skanningen och uppspelningen. */
#define WIN 4096
typedef struct {
    SceUID fd;
    unsigned char *win; /* NULL = läs direkt */
    u32 pos, len;       /* fönstrets läge i filen */
} Rd;

static void rd_init(Rd *rd, SceUID fd)
{
    rd->fd = fd;
    rd->win = malloc(WIN);
    rd->pos = rd->len = 0;
}

static void rd_free(Rd *rd)
{
    free(rd->win);
    rd->win = NULL;
}

static int read_at(Rd *rd, u32 pos, void *buf, int len)
{
    if (len <= 0)
        return len == 0;
    if (rd->win && len <= WIN) {
        if (pos < rd->pos || pos - rd->pos > rd->len || rd->len - (pos - rd->pos) < (u32)len) {
            if (sceIoLseek32(rd->fd, pos, PSP_SEEK_SET) < 0)
                return 0;
            int got = sceIoRead(rd->fd, rd->win, WIN);
            rd->pos = pos;
            rd->len = got > 0 ? (u32)got : 0;
            if (rd->len < (u32)len)
                return 0; /* filen slutar för tidigt */
        }
        memcpy(buf, rd->win + (pos - rd->pos), len);
        return 1;
    }
    return sceIoLseek32(rd->fd, pos, PSP_SEEK_SET) >= 0 && sceIoRead(rd->fd, buf, len) == len;
}

/* Boxen som börjar på pos (inom end). 0 = ingen eller trasig. */
static int box_at(Rd *fd, u32 pos, u32 end, Box *b)
{
    unsigned char h[16];
    if (pos >= end || end - pos < 8 || !read_at(fd, pos, h, 8))
        return 0;
    u32 size = be32(h), hl = 8;
    if (size == 1) { /* 64-bitars storlek – bara under 4 GB går att läsa */
        if (!read_at(fd, pos + 8, h + 8, 8) || be32(h + 8))
            return 0;
        size = be32(h + 12);
        hl = 16;
    } else if (size == 0) { /* till slutet */
        size = end - pos;
    }
    if (size < hl || size > end - pos)
        return 0;
    b->type = be32(h + 4);
    b->body = pos + hl;
    b->end = pos + size;
    return 1;
}

static int find_box(Rd *fd, u32 start, u32 end, u32 type, Box *out)
{
    Box b;
    for (u32 pos = start; box_at(fd, pos, end, &b); pos = b.end)
        if (b.type == type) {
            *out = b;
            return 1;
        }
    return 0;
}

static int find_child(Rd *fd, const Box *parent, u32 type, Box *out)
{
    return find_box(fd, parent->body, parent->end, type, out);
}

/* Första ljudspårets (hdlr = soun) mdia-box */
static int find_audio(Rd *fd, const Box *moov, Box *mdia)
{
    Box b;
    for (u32 pos = moov->body; box_at(fd, pos, moov->end, &b); pos = b.end) {
        Box md, hd;
        unsigned char h[12];
        if (b.type == FOURCC('t', 'r', 'a', 'k') && find_child(fd, &b, FOURCC('m', 'd', 'i', 'a'), &md) &&
            find_child(fd, &md, FOURCC('h', 'd', 'l', 'r'), &hd) && hd.end - hd.body >= 12 &&
            read_at(fd, hd.body, h, 12) && be32(h + 8) == FOURCC('s', 'o', 'u', 'n')) {
            *mdia = md;
            return 1;
        }
    }
    return 0;
}

static int find_stbl(Rd *fd, const Box *moov, Box *mdia, Box *stbl)
{
    Box minf;
    return find_audio(fd, moov, mdia) && find_child(fd, mdia, FOURCC('m', 'i', 'n', 'f'), &minf) &&
           find_child(fd, &minf, FOURCC('s', 't', 'b', 'l'), stbl);
}

/* --- AudioSpecificConfig --- */

typedef struct {
    const unsigned char *p;
    int n, bit;
} Bits;

static u32 get_bits(Bits *b, int count)
{
    u32 v = 0;
    while (count-- > 0) {
        int byte = b->bit >> 3;
        int bit = byte < b->n ? (b->p[byte] >> (7 - (b->bit & 7))) & 1 : 0;
        v = v << 1 | bit;
        b->bit++;
    }
    return v;
}

/* Deskriptorhuvud i esds: tagg och längd (1–4 byte à 7 bitar) */
static int descriptor(const unsigned char *p, int n, int *pos, int *len)
{
    if (*pos >= n)
        return -1;
    int tag = p[(*pos)++], l = 0;
    for (int k = 0; k < 4 && *pos < n; k++) {
        int c = p[(*pos)++];
        l = l << 7 | (c & 0x7F);
        if (!(c & 0x80))
            break;
    }
    *len = l;
    return tag;
}

/* Läser esds: kodek och AudioSpecificConfig. 1 = AAC som PSP:n kan avkoda. */
static int parse_esds(const unsigned char *p, int n, M4a *m)
{
    int pos = 4, len; /* version och flaggor */
    if (descriptor(p, n, &pos, &len) != 0x03 || pos + 3 > n)
        return 0;
    int flags = p[pos + 2];
    pos += 3;
    if (flags & 0x80)
        pos += 2;
    if ((flags & 0x40) && pos < n)
        pos += 1 + p[pos];
    if (flags & 0x20)
        pos += 2;
    if (descriptor(p, n, &pos, &len) != 0x04 || pos + 13 > n)
        return 0;
    int oti = p[pos];
    if (oti != 0x40 && oti != 0x66 && oti != 0x67 && oti != 0x68) /* MPEG-4-ljud, MPEG-2 AAC */
        return 0;
    pos += 13;
    if (descriptor(p, n, &pos, &len) != 0x05 || len < 2 || pos + 2 > n)
        return 1; /* ingen konfiguration: anta AAC-LC med värdena ur mp4a */
    Bits b = { p + pos, n - pos, 0 };
    int ot = get_bits(&b, 5);
    if (ot == 31)
        ot = 32 + get_bits(&b, 6);
    int fi = get_bits(&b, 4);
    u32 rate = fi == 15 ? get_bits(&b, 24) : fi < 13 ? AAC_RATES[fi] : 0;
    int ch = get_bits(&b, 4);
    if (ot != 2 && ot != 5 && ot != 29) /* LC, och HE-AAC (v2) som spelas som sin LC-kärna */
        return 0;
    m->object_type = 2;
    m->freq_index = fi < 13 ? fi : 15;
    if (rate)
        m->rate = rate;
    if (ch == 1 || ch == 2)
        m->channels = ch;
    return 1;
}

/* stsd: första posten måste vara mp4a med AAC */
static int parse_stsd(Rd *fd, const Box *stbl, M4a *m)
{
    Box sd;
    unsigned char b[512];
    if (!find_child(fd, stbl, FOURCC('s', 't', 's', 'd'), &sd))
        return 0;
    u32 len = sd.end - sd.body;
    if (len > sizeof(b))
        len = sizeof(b);
    if (len < 44 || !read_at(fd, sd.body, b, len))
        return 0;
    /* version/flaggor, antal, post: storlek, typ, 6 reserverade, dataref, version, revision, leverantör,
     * kanaler (32), provstorlek, komprimering, paketstorlek, frekvens 16.16 (40) */
    if (be32(b + 12) != FOURCC('m', 'p', '4', 'a'))
        return 0;
    m->channels = b[32] << 8 | b[33];
    m->rate = be32(b + 40) >> 16;
    m->object_type = 2;
    m->freq_index = 15;
    for (int k = 0; k < 13; k++)
        if (AAC_RATES[k] == m->rate)
            m->freq_index = k;
    /* esds ligger efter postens fasta del, som är längre i QuickTime-filer (version 1/2) */
    u32 entry_end = 8 + be32(b + 8);
    if (entry_end > len)
        entry_end = len;
    for (u32 i = 44; i + 8 <= entry_end; i++)
        if (!memcmp(b + i + 4, "esds", 4)) {
            u32 bl = be32(b + i);
            if (bl < 12 || i + bl > entry_end)
                return 0;
            if (!parse_esds(b + i + 8, bl - 8, m))
                return 0;
            break;
        }
    if (m->channels < 1 || m->channels > 2 || !m->rate)
        return 0;
    return 1;
}

/* --- taggar --- */

static int find_ilst(Rd *fd, const Box *moov, Box *ilst)
{
    Box udta, meta;
    unsigned char h[8];
    if (!find_child(fd, moov, FOURCC('u', 'd', 't', 'a'), &udta) ||
        !find_child(fd, &udta, FOURCC('m', 'e', 't', 'a'), &meta) || !read_at(fd, meta.body, h, 8))
        return 0;
    /* meta har 4 byte version/flaggor – utom i en del QuickTime-filer */
    u32 start = meta.body;
    if (be32(h + 4) != FOURCC('h', 'd', 'l', 'r') && be32(h + 4) != FOURCC('i', 'l', 's', 't'))
        start += 4;
    return find_box(fd, start, meta.end, FOURCC('i', 'l', 's', 't'), ilst);
}

/* Postens data-box: var värdet ligger och hur långt det är. Returnerar datatypen, −1 = ingen. */
static int item_data(Rd *fd, const Box *item, u32 *pos, u32 *len)
{
    Box d;
    unsigned char h[4];
    if (!find_child(fd, item, FOURCC('d', 'a', 't', 'a'), &d) || d.end - d.body < 8 || !read_at(fd, d.body, h, 4))
        return -1;
    *pos = d.body + 8; /* typ (4), språk (4) */
    *len = d.end - d.body - 8;
    return (int)(be32(h) & 0xFFFFFF);
}

/* UTF-8-text; skär inte mitt i ett tecken och tar bort mellanslag runt om */
static void copy_text(Rd *fd, u32 pos, u32 len, char *out, int size)
{
    int n = len < (u32)(size - 1) ? (int)len : size - 1;
    out[0] = 0;
    if (n <= 0 || !read_at(fd, pos, out, n))
        return;
    out[n] = 0;
    n = strlen(out);
    if (n > 0) { /* sista tecknet ofullständigt (avskuret)? */
        int i = n - 1;
        while (i > 0 && ((unsigned char)out[i] & 0xC0) == 0x80)
            i--;
        unsigned c = (unsigned char)out[i];
        int need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (i + need > n)
            n = i;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\r' || out[n - 1] == '\n'))
        n--;
    out[n] = 0;
    int lead = 0;
    while (out[lead] == ' ')
        lead++;
    if (lead)
        memmove(out, out + lead, n - lead + 1);
}

static int read_tags(Rd *fd, u32 file_size, char *title, int title_size, char *artist, int artist_size,
                     char *album, int album_size, int *trackno)
{
    Box moov, mdia, stbl, ilst, it;
    M4a m;
    memset(&m, 0, sizeof(m));
    if (!find_box(fd, 0, file_size, FOURCC('m', 'o', 'o', 'v'), &moov) || !find_stbl(fd, &moov, &mdia, &stbl) ||
        !parse_stsd(fd, &stbl, &m))
        return 0;
    if (!find_ilst(fd, &moov, &ilst))
        return 1;
    char album_artist[64] = "";
    for (u32 pos = ilst.body; box_at(fd, pos, ilst.end, &it); pos = it.end) {
        u32 dp, dl;
        if (item_data(fd, &it, &dp, &dl) < 0)
            continue;
        switch (it.type) {
        case FOURCC(0xA9, 'n', 'a', 'm'):
            copy_text(fd, dp, dl, title, title_size);
            break;
        case FOURCC(0xA9, 'A', 'R', 'T'):
            copy_text(fd, dp, dl, artist, artist_size);
            break;
        case FOURCC('a', 'A', 'R', 'T'):
            copy_text(fd, dp, dl, album_artist, sizeof(album_artist));
            break;
        case FOURCC(0xA9, 'a', 'l', 'b'):
            copy_text(fd, dp, dl, album, album_size);
            break;
        case FOURCC('t', 'r', 'k', 'n'): { /* 2 reserverade, spår, antal */
            unsigned char t[4];
            if (dl >= 4 && read_at(fd, dp, t, 4))
                *trackno = t[2] << 8 | t[3];
            break;
        }
        }
    }
    if (!artist[0] && album_artist[0])
        snprintf(artist, artist_size, "%s", album_artist);
    return 1;
}

int m4a_read_tags(SceUID file, u32 file_size, char *title, int title_size, char *artist, int artist_size,
                  char *album, int album_size, int *trackno)
{
    Rd rd;
    rd_init(&rd, file);
    int r = read_tags(&rd, file_size, title, title_size, artist, artist_size, album, album_size, trackno);
    rd_free(&rd);
    return r;
}

void *m4a_cover(const char *path, u32 *size_out)
{
    SceUID file = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (file < 0)
        return NULL;
    void *buf = NULL;
    u32 file_size = sceIoLseek32(file, 0, PSP_SEEK_END);
    Rd rd, *fd = &rd;
    rd_init(&rd, file);
    Box moov, ilst, covr;
    u32 dp, dl;
    if (find_box(fd, 0, file_size, FOURCC('m', 'o', 'o', 'v'), &moov) && find_ilst(fd, &moov, &ilst) &&
        find_child(fd, &ilst, FOURCC('c', 'o', 'v', 'r'), &covr) && item_data(fd, &covr, &dp, &dl) >= 0 &&
        dl > 16 && dl <= MAX_COVER && (buf = malloc(dl))) {
        if (read_at(fd, dp, buf, dl)) {
            *size_out = dl;
        } else {
            free(buf);
            buf = NULL;
        }
    }
    rd_free(&rd);
    sceIoClose(file);
    return buf;
}

/* --- uppspelning --- */

/* Läser n stycken 32-bitarstal från pos (inom end) och vänder byteordningen */
static u32 *read_table(Rd *fd, u32 pos, u32 end, u32 n)
{
    if (!n || n > MAX_TABLE / 4 || pos > end || (end - pos) / 4 < n)
        return NULL;
    u32 *t = malloc(n * 4);
    if (!t)
        return NULL;
    if (!read_at(fd, pos, t, n * 4)) {
        free(t);
        return NULL;
    }
    for (u32 i = 0; i < n; i++)
        t[i] = be32((const unsigned char *)&t[i]);
    return t;
}

void m4a_free(M4a *m)
{
    free(m->sizes);
    free(m->chunk_off);
    free(m->stsc);
    memset(m, 0, sizeof(*m));
}

static int open_rd(M4a *m, Rd *fd, u32 file_size)
{
    Box moov, mdia, stbl, b;
    unsigned char h[32];
    if (!find_box(fd, 0, file_size, FOURCC('m', 'o', 'o', 'v'), &moov) || !find_stbl(fd, &moov, &mdia, &stbl) ||
        !parse_stsd(fd, &stbl, m))
        return -1;
    /* längden ur mdhd */
    if (find_child(fd, &mdia, FOURCC('m', 'd', 'h', 'd'), &b) && b.end - b.body >= 20 &&
        read_at(fd, b.body, h, b.end - b.body >= 32 ? 32 : 20)) {
        u32 scale;
        float dur;
        if (h[0] == 1 && b.end - b.body >= 32) {
            scale = be32(h + 20);
            dur = (float)be32(h + 24) * 4294967296.0f + be32(h + 28);
        } else {
            scale = be32(h + 12);
            dur = be32(h + 16);
        }
        m->duration = scale ? dur / scale : 0;
    }
    /* bildrutornas storlek */
    if (!find_child(fd, &stbl, FOURCC('s', 't', 's', 'z'), &b) || b.end - b.body < 12 || !read_at(fd, b.body, h, 12))
        goto fail;
    m->fixed_size = be32(h + 4);
    m->nsamples = be32(h + 8);
    if (!m->nsamples || (!m->fixed_size && !(m->sizes = read_table(fd, b.body + 12, b.end, m->nsamples))))
        goto fail;
    /* var varje chunk börjar (stco, eller co64 för stora filer) */
    if (find_child(fd, &stbl, FOURCC('s', 't', 'c', 'o'), &b) && b.end - b.body >= 8 && read_at(fd, b.body, h, 8)) {
        m->nchunks = be32(h + 4);
        if (!(m->chunk_off = read_table(fd, b.body + 8, b.end, m->nchunks)))
            goto fail;
    } else if (find_child(fd, &stbl, FOURCC('c', 'o', '6', '4'), &b) && b.end - b.body >= 8 &&
               read_at(fd, b.body, h, 8)) {
        m->nchunks = be32(h + 4);
        if (m->nchunks > MAX_TABLE / 8 || !(m->chunk_off = read_table(fd, b.body + 8, b.end, m->nchunks * 2)))
            goto fail;
        for (u32 i = 0; i < m->nchunks; i++) /* låga ordet – filer över 4 GB finns inte på FAT32 */
            m->chunk_off[i] = m->chunk_off[i * 2 + 1];
    } else {
        goto fail;
    }
    /* bildrutor per chunk: (första chunk, antal, beskrivning) → par */
    if (!find_child(fd, &stbl, FOURCC('s', 't', 's', 'c'), &b) || b.end - b.body < 8 || !read_at(fd, b.body, h, 8))
        goto fail;
    m->nstsc = be32(h + 4);
    if (m->nstsc > MAX_TABLE / 12 || !(m->stsc = read_table(fd, b.body + 8, b.end, m->nstsc * 3)))
        goto fail;
    for (u32 i = 0; i < m->nstsc; i++) {
        m->stsc[i * 2] = m->stsc[i * 3];
        m->stsc[i * 2 + 1] = m->stsc[i * 3 + 1];
    }
    m4a_seek(m, 0);
    return 0;
fail:
    m4a_free(m);
    return -1;
}

int m4a_open(M4a *m, SceUID file, u32 file_size)
{
    memset(m, 0, sizeof(*m));
    Rd rd;
    rd_init(&rd, file);
    int r = open_rd(m, &rd, file_size);
    rd_free(&rd);
    return r;
}

static u32 sample_size(const M4a *m, u32 k)
{
    return m->fixed_size ? m->fixed_size : m->sizes[k];
}

/* bildrutor i chunk c (0-baserat); flyttar stsc-positionen framåt */
static u32 chunk_samples(const M4a *m, u32 c, u32 *si)
{
    while (*si + 1 < m->nstsc && c + 1 >= m->stsc[(*si + 1) * 2])
        (*si)++;
    return m->stsc[*si * 2 + 1];
}

void m4a_seek(M4a *m, u32 n)
{
    if (n >= m->nsamples) { /* förbi slutet (t.ex. fel längd i mdhd) */
        m->cur = m->nsamples;
        return;
    }
    u32 first = 0, si = 0;
    for (u32 c = 0; c < m->nchunks && first < m->nsamples; c++) {
        u32 spc = chunk_samples(m, c, &si);
        if (n < first + spc) {
            u32 off = m->chunk_off[c];
            for (u32 k = first; k < n; k++)
                off += sample_size(m, k);
            m->cur = n;
            m->cur_chunk = c;
            m->cur_in_chunk = n - first;
            m->cur_off = off;
            m->cur_stsc = si;
            return;
        }
        first += spc;
    }
    m->cur = m->nsamples; /* förbi slutet */
}

int m4a_next(M4a *m, u32 *offset, u32 *size)
{
    if (m->cur >= m->nsamples || m->cur_chunk >= m->nchunks)
        return 0;
    *offset = m->cur_off;
    *size = sample_size(m, m->cur);
    m->cur++;
    m->cur_off += *size;
    m->cur_in_chunk++;
    /* nästa chunk (och hoppa över tomma) */
    while (m->cur_chunk < m->nchunks && m->cur_in_chunk >= chunk_samples(m, m->cur_chunk, &m->cur_stsc)) {
        m->cur_in_chunk = 0;
        m->cur_chunk++;
        if (m->cur_chunk < m->nchunks)
            m->cur_off = m->chunk_off[m->cur_chunk];
    }
    return 1;
}
