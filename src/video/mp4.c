/* psp5 authentic – an MP4 file read for playback (see mp4.h) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mp4.h"

#define MAX_MOOV (16 << 20)

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

typedef struct {
    int used;
    uint32_t timescale;
    uint64_t duration;
    /* the tables, pointing into the movie box */
    const uint8_t *stts, *ctts, *stss, *stsc, *stsz, *stco;
    uint32_t n_stts, n_ctts, n_stss, n_stsc, n_samples, n_chunks, fixed_size;
    int co64;
    /* a place in stts and ctts that a walk ends at, to start from the next time (sequential reading is by far the most common) */
    uint32_t t_run, t_first;
    uint64_t t_dts;
    uint32_t c_run, c_first;
    /* video: the parameter sets (the avcC box), the length of the NAL length field; audio: the config, rate and channels */
    const uint8_t *avcc;
    uint32_t avcc_len;
    int width, height;
    const uint8_t *asc;
    int asc_len, rate, channels;
} Track;

struct Mp4 {
    FILE *fp;
    FILE *fpa; /* the sound has a file handle (and buffer) of its own: read in turn with the pictures through one, every other read would throw the buffer away and take 16 KB from the Memory Stick again */
    uint8_t *moov;
    long moov_size;
    Track v, a;
    uint64_t movie_duration;
    uint32_t movie_scale;
};

/* ---------------------------------------------------------------------- */
/* the movie box */

/* the children of a box: calls fn for each (type, body, size) */
typedef void (*BoxFn)(Mp4 *m, Track *t, uint32_t type, const uint8_t *body, long size, int depth);

static void walk(Mp4 *m, Track *t, const uint8_t *p, long size, int depth, BoxFn fn)
{
    while (size >= 8) {
        uint64_t sz = be32(p);
        uint32_t type = be32(p + 4);
        long hdr = 8;
        if (sz == 1) {
            if (size < 16)
                return;
            sz = be64(p + 8);
            hdr = 16;
        } else if (sz == 0) {
            sz = (uint64_t)size;
        }
        if (sz < (uint64_t)hdr || sz > (uint64_t)size)
            return;
        fn(m, t, type, p + hdr, (long)(sz - hdr), depth);
        p += sz;
        size -= (long)sz;
    }
}

static int handler_of(const uint8_t *mdia, long size)
{
    /* hdlr is a child of mdia: version/flags 4, pre_defined 4, handler_type 4 */
    while (size >= 8) {
        uint32_t sz = be32(mdia), type = be32(mdia + 4);
        if (sz < 8 || (long)sz > size)
            return 0;
        if (type == 0x68646c72 && sz >= 20) /* hdlr */
            return (int)be32(mdia + 16);
        mdia += sz;
        size -= (long)sz;
    }
    return 0;
}

/* the length of a descriptor (1 to 4 bytes of 7 bits) */
static int desc_len(const uint8_t *p, long avail, int *used)
{
    int len = 0, i;
    for (i = 0; i < 4 && i < avail; i++) {
        len = (len << 7) | (p[i] & 0x7F);
        if (!(p[i] & 0x80)) {
            *used = i + 1;
            return len;
        }
    }
    *used = 0;
    return -1;
}

/* the descriptors of an esds box (ES_Descriptor 3 holds DecoderConfigDescriptor 4, which holds DecoderSpecificInfo 5: for AAC (object type 0x40) that is the AudioSpecificConfig) */
static void parse_desc(Track *t, const uint8_t *p, long size)
{
    while (size >= 2) {
        int tag = p[0], used;
        int len = desc_len(p + 1, size - 1, &used);
        if (len < 0 || 1 + used + len > size)
            return;
        const uint8_t *body = p + 1 + used;
        if (tag == 0x03) { /* ES_ID 2, flags 1, then (by the flags) 2 + 1 + n + 2 more, then the descriptors inside */
            if (len >= 3) {
                int skip = 3, fl = body[2];
                if ((fl & 0x80) && skip + 2 <= len)
                    skip += 2;
                if ((fl & 0x40) && skip < len)
                    skip += 1 + body[skip];
                if ((fl & 0x20) && skip + 2 <= len)
                    skip += 2;
                if (skip < len)
                    parse_desc(t, body + skip, len - skip);
            }
        } else if (tag == 0x04) { /* object type 1, stream type 1, buffer 3, max rate 4, average rate 4, then the descriptors inside */
            if (len > 13 && body[0] == 0x40)
                parse_desc(t, body + 13, len - 13);
        } else if (tag == 0x05) {
            if (len >= 2 && !t->asc) {
                t->asc = body;
                t->asc_len = len;
            }
        }
        p = body + len;
        size -= 1 + used + len;
    }
}

static void parse_esds(Track *t, const uint8_t *p, long size)
{
    if (size > 4)
        parse_desc(t, p + 4, size - 4); /* (version and flags first) */
}

static void stbl_box(Mp4 *m, Track *t, uint32_t type, const uint8_t *b, long size, int depth)
{
    switch (type) {
    case 0x73747364: /* stsd: version/flags 4, entry count 4, then the entries */
        if (size >= 16 && be32(b + 4) >= 1) {
            const uint8_t *e = b + 8;
            uint32_t esz = be32(e), etype = be32(e + 4);
            if (esz < 16 || (long)esz > size - 8)
                break;
            if (etype == 0x61766331 || etype == 0x61766333) { /* avc1, avc3: 78 bytes of visual sample entry, then the boxes */
                if (esz >= 86) {
                    t->width = (int)be16(e + 8 + 24);
                    t->height = (int)be16(e + 8 + 26);
                    const uint8_t *c = e + 8 + 78;
                    long left = (long)esz - 8 - 78;
                    while (left >= 8) {
                        uint32_t csz = be32(c), ctype = be32(c + 4);
                        if (csz < 8 || (long)csz > left)
                            break;
                        if (ctype == 0x61766343) { /* avcC */
                            t->avcc = c + 8;
                            t->avcc_len = csz - 8;
                        }
                        c += csz;
                        left -= (long)csz;
                    }
                }
            } else if (etype == 0x6d703461) { /* mp4a: 28 bytes of audio sample entry, then the boxes */
                if (esz >= 36) {
                    t->channels = (int)be16(e + 8 + 16);
                    t->rate = (int)(be32(e + 8 + 24) >> 16);
                    const uint8_t *c = e + 8 + 28;
                    long left = (long)esz - 8 - 28;
                    while (left >= 8) {
                        uint32_t csz = be32(c), ctype = be32(c + 4);
                        if (csz < 8 || (long)csz > left)
                            break;
                        if (ctype == 0x65736473) /* esds */
                            parse_esds(t, c + 8, csz - 8);
                        c += csz;
                        left -= (long)csz;
                    }
                }
            }
        }
        break;
    case 0x73747473: /* stts: version/flags 4, count 4, then (count, delta) pairs */
        if (size >= 8) {
            t->n_stts = be32(b + 4);
            t->stts = b + 8;
            if ((uint64_t)t->n_stts * 8 > (uint64_t)(size - 8))
                t->n_stts = 0;
        }
        break;
    case 0x63747473: /* ctts: (count, offset) pairs */
        if (size >= 8) {
            t->n_ctts = be32(b + 4);
            t->ctts = b + 8;
            if ((uint64_t)t->n_ctts * 8 > (uint64_t)(size - 8))
                t->n_ctts = 0;
        }
        break;
    case 0x73747373: /* stss: sample numbers of the sync samples */
        if (size >= 8) {
            t->n_stss = be32(b + 4);
            t->stss = b + 8;
            if ((uint64_t)t->n_stss * 4 > (uint64_t)(size - 8))
                t->n_stss = 0;
        }
        break;
    case 0x73747363: /* stsc: (first chunk, samples per chunk, description) */
        if (size >= 8) {
            t->n_stsc = be32(b + 4);
            t->stsc = b + 8;
            if ((uint64_t)t->n_stsc * 12 > (uint64_t)(size - 8))
                t->n_stsc = 0;
        }
        break;
    case 0x7374737a: /* stsz: version/flags 4, sample size 4 (0: a table), count 4, the sizes */
        if (size >= 12) {
            t->fixed_size = be32(b + 4);
            t->n_samples = be32(b + 8);
            t->stsz = b + 12;
            if (!t->fixed_size && (uint64_t)t->n_samples * 4 > (uint64_t)(size - 12))
                t->n_samples = 0;
        }
        break;
    case 0x7374636f: /* stco */
    case 0x636f3634: /* co64 */
        if (size >= 8) {
            t->co64 = type == 0x636f3634;
            t->n_chunks = be32(b + 4);
            t->stco = b + 8;
            if ((uint64_t)t->n_chunks * (t->co64 ? 8 : 4) > (uint64_t)(size - 8))
                t->n_chunks = 0;
        }
        break;
    default:
        break;
    }
}

static void minf_box(Mp4 *m, Track *t, uint32_t type, const uint8_t *b, long size, int depth)
{
    if (type == 0x7374626c) /* stbl */
        walk(m, t, b, size, depth + 1, stbl_box);
}

static void mdia_box(Mp4 *m, Track *t, uint32_t type, const uint8_t *b, long size, int depth)
{
    if (type == 0x6d646864 && size >= 24) { /* mdhd */
        if (b[0] == 1 && size >= 32) {
            t->timescale = be32(b + 20);
            t->duration = be64(b + 24);
        } else {
            t->timescale = be32(b + 12);
            t->duration = be32(b + 16);
        }
    } else if (type == 0x6d696e66) { /* minf */
        walk(m, t, b, size, depth + 1, minf_box);
    }
}

static void trak_box(Mp4 *m, Track *unused, uint32_t type, const uint8_t *b, long size, int depth)
{
    if (type != 0x6d646961) /* mdia */
        return;
    int h = handler_of(b, size);
    Track tmp;
    memset(&tmp, 0, sizeof(tmp));
    walk(m, &tmp, b, size, depth + 1, mdia_box);
    if (h == 0x76696465 && !m->v.used && tmp.avcc && tmp.stsz && tmp.stco && tmp.stsc && tmp.stts && tmp.timescale) { /* vide */
        m->v = tmp;
        m->v.used = 1;
    } else if (h == 0x736f756e && !m->a.used && tmp.asc && tmp.stsz && tmp.stco && tmp.stsc && tmp.stts && tmp.timescale) { /* soun, AAC */
        m->a = tmp;
        m->a.used = 1;
    }
}

static void moov_box(Mp4 *m, Track *t, uint32_t type, const uint8_t *b, long size, int depth)
{
    if (type == 0x7472616b) /* trak */
        walk(m, t, b, size, depth + 1, trak_box);
    else if (type == 0x6d766864 && size >= 20) { /* mvhd */
        if (b[0] == 1 && size >= 32) {
            m->movie_scale = be32(b + 20);
            m->movie_duration = be64(b + 24);
        } else {
            m->movie_scale = be32(b + 12);
            m->movie_duration = be32(b + 16);
        }
    }
}

/* finds the movie box in the file (it may come after the media data) and reads it whole */
static int load_moov(Mp4 *m)
{
    uint8_t h[16];
    long pos = 0;
    fseek(m->fp, 0, SEEK_END);
    long end = ftell(m->fp);
    for (int guard = 0; guard < 64 && pos + 8 <= end; guard++) {
        fseek(m->fp, pos, SEEK_SET);
        if (fread(h, 1, 8, m->fp) != 8)
            return 0;
        uint64_t sz = be32(h);
        uint32_t type = be32(h + 4);
        long hdr = 8;
        if (sz == 1) {
            if (fread(h + 8, 1, 8, m->fp) != 8)
                return 0;
            sz = be64(h + 8);
            hdr = 16;
        } else if (sz == 0) {
            sz = (uint64_t)(end - pos);
        }
        if (sz < (uint64_t)hdr || pos + (long)sz > end)
            return 0;
        if (type == 0x6d6f6f76) { /* moov */
            long body = (long)sz - hdr;
            if (body <= 0 || body > MAX_MOOV)
                return 0;
            m->moov = malloc((size_t)body);
            if (!m->moov)
                return 0;
            fseek(m->fp, pos + hdr, SEEK_SET);
            if (fread(m->moov, 1, (size_t)body, m->fp) != (size_t)body) {
                free(m->moov);
                m->moov = NULL;
                return 0;
            }
            m->moov_size = body;
            return 1;
        }
        pos += (long)sz;
    }
    return 0;
}

Mp4 *mp4_open(const char *path)
{
    Mp4 *m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->fp = fopen(path, "rb");
    if (!m->fp || !load_moov(m)) {
        mp4_close(m);
        return NULL;
    }
    setvbuf(m->fp, NULL, _IOFBF, 32768);
    walk(m, NULL, m->moov, m->moov_size, 0, moov_box);
    if (!m->v.used || !m->v.n_samples || !m->v.width || !m->v.height || m->v.avcc_len < 7) {
        mp4_close(m);
        return NULL;
    }
    if (m->a.used && (!m->a.n_samples || m->a.rate <= 0 || m->a.channels <= 0))
        m->a.used = 0;
    if (m->a.used) {
        m->fpa = fopen(path, "rb");
        if (m->fpa)
            setvbuf(m->fpa, NULL, _IOFBF, 32768);
    }
    return m;
}

/* An audio file in the MP4 family (.m4a, .m4b): no video track is needed, the first AAC audio track is the file. The sound has its file handle (fpa) and the movie box is read whole (it holds the tags
 * and the cover too). NULL when there is no movie box, or no AAC track. */
Mp4 *mp4_open_audio(const char *path)
{
    Mp4 *m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->fp = fopen(path, "rb");
    if (!m->fp || !load_moov(m)) {
        mp4_close(m);
        return NULL;
    }
    setvbuf(m->fp, NULL, _IOFBF, 32768);
    walk(m, NULL, m->moov, m->moov_size, 0, moov_box);
    if (!m->a.used || !m->a.n_samples || m->a.rate <= 0 || m->a.channels <= 0 || !m->a.asc || m->a.asc_len < 2 || !m->a.timescale) {
        mp4_close(m);
        return NULL;
    }
    m->fpa = fopen(path, "rb");
    if (m->fpa)
        setvbuf(m->fpa, NULL, _IOFBF, 32768);
    return m;
}

void mp4_close(Mp4 *m)
{
    if (!m)
        return;
    if (m->fp)
        fclose(m->fp);
    if (m->fpa)
        fclose(m->fpa);
    free(m->moov);
    free(m);
}

int mp4_width(const Mp4 *m) { return m->v.width; }
int mp4_height(const Mp4 *m) { return m->v.height; }

float mp4_duration(const Mp4 *m)
{
    if (m->v.timescale && m->v.duration)
        return (float)((double)m->v.duration / m->v.timescale);
    return m->movie_scale ? (float)((double)m->movie_duration / m->movie_scale) : 0;
}

/* ---------------------------------------------------------------------- */
/* samples */

static int sample_size(const Track *t, int i)
{
    if (i < 0 || (uint32_t)i >= t->n_samples)
        return -1;
    return t->fixed_size ? (int)t->fixed_size : (int)be32(t->stsz + 4 * (size_t)i);
}

static long long sample_offset(const Track *t, int i)
{
    if (i < 0 || (uint32_t)i >= t->n_samples || !t->n_stsc)
        return -1;
    /* the run of chunks that sample i is in */
    uint32_t seen = 0; /* samples before the run */
    uint32_t run = 0;
    for (; run < t->n_stsc; run++) {
        const uint8_t *e = t->stsc + 12 * (size_t)run;
        uint32_t first = be32(e), per = be32(e + 4);
        uint32_t next_first = run + 1 < t->n_stsc ? be32(e + 12) : t->n_chunks + 1;
        if (next_first < first || per == 0)
            return -1;
        uint64_t in_run = (uint64_t)(next_first - first) * per;
        if ((uint64_t)i < (uint64_t)seen + in_run) {
            uint32_t k = ((uint32_t)i - seen) / per; /* the chunk, counted in the run */
            uint32_t chunk = first - 1 + k;
            uint32_t sample_in = ((uint32_t)i - seen) - k * per; /* the number of the sample in the chunk */
            if (chunk >= t->n_chunks)
                return -1;
            long long off = t->co64 ? (long long)be64(t->stco + 8 * (size_t)chunk) : (long long)be32(t->stco + 4 * (size_t)chunk);
            for (uint32_t j = 0; j < sample_in; j++) {
                int sz = sample_size(t, i - (int)sample_in + (int)j);
                if (sz < 0)
                    return -1;
                off += sz;
            }
            return off;
        }
        seen += (uint32_t)in_run;
    }
    return -1;
}

/* the decoding time of sample i in timescale units */
static uint64_t sample_dts(Track *t, int i)
{
    if (!t->n_stts)
        return 0;
    if ((uint32_t)i < t->t_first || t->t_run >= t->n_stts) { /* back before the place that was kept: from the start */
        t->t_run = 0;
        t->t_first = 0;
        t->t_dts = 0;
    }
    for (;;) {
        const uint8_t *e = t->stts + 8 * (size_t)t->t_run;
        uint32_t count = be32(e), delta = be32(e + 4);
        if ((uint32_t)i < t->t_first + count || t->t_run + 1 >= t->n_stts)
            return t->t_dts + (uint64_t)((uint32_t)i - t->t_first) * delta;
        t->t_dts += (uint64_t)count * delta;
        t->t_first += count;
        t->t_run++;
    }
}

static int64_t sample_cts_offset(Track *t, int i)
{
    if (!t->n_ctts)
        return 0;
    if ((uint32_t)i < t->c_first || t->c_run >= t->n_ctts) {
        t->c_run = 0;
        t->c_first = 0;
    }
    for (;;) {
        const uint8_t *e = t->ctts + 8 * (size_t)t->c_run;
        uint32_t count = be32(e);
        if ((uint32_t)i < t->c_first + count || t->c_run + 1 >= t->n_ctts)
            return (int32_t)be32(e + 4);
        t->c_first += count;
        t->c_run++;
    }
}

static int is_sync(const Track *t, int i)
{
    if (!t->n_stss)
        return 1; /* every sample is a sync sample */
    uint32_t want = (uint32_t)i + 1, lo = 0, hi = t->n_stss;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2, v = be32(t->stss + 4 * (size_t)mid);
        if (v < want)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < t->n_stss && be32(t->stss + 4 * (size_t)lo) == want;
}

/* the sync sample at or before sample i */
static int sync_before(const Track *t, int i)
{
    if (!t->n_stss)
        return i;
    uint32_t want = (uint32_t)i + 1, lo = 0, hi = t->n_stss;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (be32(t->stss + 4 * (size_t)mid) <= want)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo == 0 ? 0 : (int)be32(t->stss + 4 * (size_t)(lo - 1)) - 1;
}

/* the sample that is shown or decoded at time t (seconds): the last one that starts at or before it */
static int sample_at(Track *t, double sec)
{
    if (sec <= 0 || !t->n_stts)
        return 0;
    uint64_t want = (uint64_t)(sec * t->timescale);
    uint32_t first = 0;
    uint64_t dts = 0;
    for (uint32_t r = 0; r < t->n_stts; r++) {
        const uint8_t *e = t->stts + 8 * (size_t)r;
        uint32_t count = be32(e), delta = be32(e + 4);
        uint64_t span = (uint64_t)count * delta;
        if (delta && want < dts + span)
            return (int)(first + (uint32_t)((want - dts) / delta));
        dts += span;
        first += count;
    }
    return t->n_samples ? (int)t->n_samples - 1 : 0;
}

static int read_sample(Mp4 *m, const Track *t, int i, unsigned char *buf, int cap)
{
    int size = sample_size(t, i);
    long long off = sample_offset(t, i);
    FILE *fp = t == &m->a && m->fpa ? m->fpa : m->fp;
    if (size < 0 || off < 0 || size > cap || fseek(fp, (long)off, SEEK_SET) || fread(buf, 1, (size_t)size, fp) != (size_t)size)
        return -1;
    return size;
}

int mp4_video_count(const Mp4 *m) { return (int)m->v.n_samples; }
int mp4_nal_size(const Mp4 *m) { return (m->v.avcc[4] & 3) + 1; }
int mp4_video_size(Mp4 *m, int i) { return sample_size(&m->v, i); }

int mp4_video_max_size(const Mp4 *m)
{
    int max = 0;
    for (uint32_t i = 0; i < m->v.n_samples; i++) {
        int s = sample_size(&m->v, (int)i);
        if (s > max)
            max = s;
    }
    return max;
}
int mp4_video_read(Mp4 *m, int i, unsigned char *buf, int cap) { return read_sample(m, &m->v, i, buf, cap); }

void mp4_video_times(Mp4 *m, int i, float *dts, float *pts, int *key)
{
    double sc = m->v.timescale;
    uint64_t d = sample_dts(&m->v, i);
    int64_t c = sample_cts_offset(&m->v, i);
    if (dts)
        *dts = (float)(d / sc);
    if (pts)
        *pts = (float)(((double)d + (double)c) / sc);
    if (key)
        *key = is_sync(&m->v, i);
}

int mp4_video_at(Mp4 *m, float t) { return sync_before(&m->v, sample_at(&m->v, t)); }

int mp4_param_sets(const Mp4 *m, unsigned char *out, int cap)
{
    const uint8_t *p = m->v.avcc, *end = p + m->v.avcc_len;
    int n = 0, count;
    if (m->v.avcc_len < 7)
        return 0;
    p += 5;
    for (int pass = 0; pass < 2; pass++) {
        if (p >= end)
            return 0;
        count = pass == 0 ? (*p & 0x1F) : *p;
        p++;
        for (int k = 0; k < count; k++) {
            if (p + 2 > end)
                return 0;
            int len = (int)be16(p);
            p += 2;
            if (p + len > end || n + 4 + len > cap)
                return 0;
            out[n++] = 0;
            out[n++] = 0;
            out[n++] = 0;
            out[n++] = 1;
            memcpy(out + n, p, (size_t)len);
            n += len;
            p += len;
        }
    }
    return n;
}

int mp4_has_audio(const Mp4 *m) { return m->a.used; }
int mp4_audio_rate(const Mp4 *m) { return m->a.rate; }
int mp4_audio_channels(const Mp4 *m) { return m->a.channels; }
const unsigned char *mp4_audio_config(const Mp4 *m, int *len)
{
    *len = m->a.asc_len;
    return m->a.asc;
}
int mp4_audio_count(const Mp4 *m) { return (int)m->a.n_samples; }
int mp4_audio_size(Mp4 *m, int i) { return sample_size(&m->a, i); }
int mp4_audio_read(Mp4 *m, int i, unsigned char *buf, int cap) { return read_sample(m, &m->a, i, buf, cap); }
float mp4_audio_time(Mp4 *m, int i) { return (float)((double)sample_dts(&m->a, i) / m->a.timescale); }
int mp4_audio_at(Mp4 *m, float t) { return sample_at(&m->a, t); }

float mp4_audio_duration(const Mp4 *m)
{
    if (m->a.timescale && m->a.duration)
        return (float)((double)m->a.duration / m->a.timescale);
    return m->movie_scale ? (float)((double)m->movie_duration / m->movie_scale) : 0;
}

/* ---------------------------------------------------------------------- */
/* the tags and the cover (the iTunes way: moov / udta / meta / ilst, or moov / meta / ilst) */

static const uint8_t *find_box(const uint8_t *p, long size, uint32_t type, long *body)
{
    while (size >= 8) {
        uint64_t sz = be32(p);
        uint32_t t = be32(p + 4);
        long hdr = 8;
        if (sz == 1) {
            if (size < 16)
                return NULL;
            sz = be64(p + 8);
            hdr = 16;
        } else if (sz == 0) {
            sz = (uint64_t)size;
        }
        if (sz < (uint64_t)hdr || sz > (uint64_t)size)
            return NULL;
        if (t == type) {
            *body = (long)(sz - hdr);
            return p + hdr;
        }
        p += sz;
        size -= (long)sz;
    }
    return NULL;
}

/* the box `ilst` that holds the items (NULL when the file has none) */
static const uint8_t *ilst_of(const Mp4 *m, long *size)
{
    long n = 0, k = 0;
    const uint8_t *meta = NULL;
    const uint8_t *udta = find_box(m->moov, m->moov_size, 0x75647461, &n); /* udta */
    if (udta)
        meta = find_box(udta, n, 0x6d657461, &k); /* meta */
    if (!meta)
        meta = find_box(m->moov, m->moov_size, 0x6d657461, &k);
    if (!meta || k < 4)
        return NULL;
    return find_box(meta + 4, k - 4, 0x696c7374, size); /* (meta has its version and flags first) */
}

/* the first `data` of the item `key`: its body (the type and the place come first: 8 bytes), the size of that, and the type (1 = text, 13 = JPEG, 14 = PNG) */
static const uint8_t *item_data(const Mp4 *m, uint32_t key, long *size, int *type)
{
    long n = 0, k = 0;
    const uint8_t *ilst = ilst_of(m, &n);
    if (!ilst)
        return NULL;
    const uint8_t *item = find_box(ilst, n, key, &k);
    if (!item)
        return NULL;
    long d = 0;
    const uint8_t *data = find_box(item, k, 0x64617461, &d); /* data */
    if (!data || d < 8)
        return NULL;
    *type = (int)(be32(data) & 0xFFFFFF);
    *size = d - 8;
    return data + 8;
}

int mp4_tag(const Mp4 *m, uint32_t key, char *out, int cap)
{
    long n = 0;
    int type = 0;
    out[0] = 0;
    const uint8_t *d = item_data(m, key, &n, &type);
    if (!d || type != 1 || cap < 2)
        return 0;
    if (n > cap - 1)
        n = cap - 1;
    memcpy(out, d, (size_t)n);
    out[n] = 0;
    /* a cut in the middle of a character is taken off */
    int i = (int)n;
    while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80)
        i--;
    if (i > 0 && (unsigned char)out[i - 1] >= 0xC0) {
        int need = (unsigned char)out[i - 1] >= 0xF0 ? 4 : (unsigned char)out[i - 1] >= 0xE0 ? 3 : 2;
        if ((int)n - (i - 1) < need)
            out[i - 1] = 0;
    }
    for (char *c = out; *c; c++) /* (a line feed or a tab in a tag is a space) */
        if ((unsigned char)*c < 32)
            *c = ' ';
    return (int)strlen(out);
}

const unsigned char *mp4_cover(const Mp4 *m, int *len, int *is_png)
{
    long n = 0;
    int type = 0;
    const uint8_t *d = item_data(m, MP4_TAG_COVER, &n, &type);
    if (!d || (type != 13 && type != 14) || n <= 0 || n > 0x7FFFFFFF)
        return NULL;
    *len = (int)n;
    *is_png = type == 14;
    return d;
}
