/* psp5 authentic – Annex B, ADTS and the MPEG program stream writer (see mpegps.h) */
#include <string.h>
#include "mpegps.h"

#define SPS_BUF 320 /* room for an SPS and the VUI of the PSP's own streams in it */

/* what kind of slices a NAL unit has: 0 = I, 1 = P, 2 = B (the first two numbers of a slice header are the first macroblock and the slice type); -1 when it cannot be read */
static int slice_kind(const uint8_t *nal, int len)
{
    uint8_t b[12];
    int n = 0;
    for (int i = 1; i < len && n < 12; i++) {
        if (n >= 2 && b[n - 1] == 0 && b[n - 2] == 0 && nal[i] == 3)
            continue; /* emulation prevention byte */
        b[n++] = nal[i];
    }
    int pos = 0, v[2];
    for (int k = 0; k < 2; k++) {
        int zeros = 0;
        while (pos < n * 8 && !((b[pos >> 3] >> (7 - (pos & 7))) & 1)) {
            zeros++;
            pos++;
        }
        if (zeros > 15 || pos + zeros + 1 > n * 8)
            return -1;
        pos++;
        int val = 0;
        for (int i = 0; i < zeros; i++, pos++)
            val = (val << 1) | ((b[pos >> 3] >> (7 - (pos & 7))) & 1);
        v[k] = (1 << zeros) - 1 + val;
    }
    int t = v[1] % 5; /* 0 P, 1 B, 2 I, 3 SP, 4 SI */
    return t == 2 || t == 4 ? 0 : t == 1 ? 2 : 1;
}

int mpegps_sample_has_params(const uint8_t *s, int len, int nal_size)
{
    if (nal_size < 1 || nal_size > 4)
        return 0;
    for (int pos = 0; pos + nal_size <= len;) {
        int l = 0;
        for (int i = 0; i < nal_size; i++)
            l = (l << 8) | s[pos + i];
        pos += nal_size;
        if (l <= 0 || pos + l > len)
            return 0;
        int t = s[pos] & 0x1F;
        if (t == 7 || t == 8)
            return 1;
        pos += l;
    }
    return 0;
}

int mpegps_unit_has_sps(const uint8_t *u, int len)
{
    for (int i = 0; i + 4 < len && i < 64; i++)
        if (u[i] == 0 && u[i + 1] == 0 && u[i + 2] == 1 && (u[i + 3] & 0x1F) == 7)
            return 1;
    return 0;
}

/* The unit the way the PSP's own streams have it: an access unit delimiter that says what kinds of slices the unit has (10 for I, 30 for I and P, 50 for B too), the parameter sets of a key frame
 * behind it, and a start code of four bytes in front of the delimiter and the parameter sets and of three in front of everything else. A delimiter that the sample has is the one that is used,
 * wherever it is in the sample (an encoder that writes the stream headers in front of the first picture puts it behind them); there is only one in a unit. */
/* a NAL unit (header byte and all) behind a start code of sc bytes (3 or 4) */
static int put_nal(uint8_t *out, int n, int cap, int sc, const uint8_t *nal, int len)
{
    if (n + sc + len > cap)
        return -1;
    if (sc == 4)
        out[n++] = 0;
    out[n++] = 0;
    out[n++] = 0;
    out[n++] = 1;
    memcpy(out + n, nal, (size_t)len);
    return n + len;
}

int mpegps_annexb(const uint8_t *s, int len, int nal_size, const uint8_t *params, int params_len, int key, uint8_t *out, int cap, const PsSynth *sy)
{
    int n = 0;
    if (nal_size < 1 || nal_size > 4 || len < nal_size)
        return -1;
    /* what the sample has in it: its delimiter, whether it carries the parameter sets itself, and what kinds of slices */
    int has_params = 0, kind = -1, aud_pos = -1, aud_len = 0, idr = 0;
    for (int pos = 0; pos + nal_size <= len;) {
        int l = 0;
        for (int i = 0; i < nal_size; i++)
            l = (l << 8) | s[pos + i];
        pos += nal_size;
        if (l <= 0 || pos + l > len)
            return -1;
        int t = s[pos] & 0x1F;
        if (t == 9 && aud_pos < 0) {
            aud_pos = pos;
            aud_len = l;
        }
        if (t == 7 || t == 8)
            has_params = 1;
        if (t == 5)
            idr = 1;
        if (t == 1 || t == 5) {
            int sk = slice_kind(s + pos, l);
            if (sk > kind)
                kind = sk;
        }
        pos += l;
    }
    if (aud_pos >= 0) {
        n = put_nal(out, 0, cap, 4, s + aud_pos, aud_len);
        if (n < 0)
            return -1;
    } else {
        if (cap < 6)
            return -1;
        out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1; out[4] = 0x09;
        out[5] = (uint8_t)(kind >= 0 ? (kind << 5) | 0x10 : 0xF0);
        n = 6;
    }
    if (key && params_len > 0 && !has_params) {
        if (n + params_len > cap)
            return -1;
        memcpy(out + n, params, (size_t)params_len);
        n += params_len;
    }
    int sei_done = 0;
    for (int pos = 0; pos + nal_size <= len;) {
        int l = 0;
        for (int i = 0; i < nal_size; i++)
            l = (l << 8) | s[pos + i];
        pos += nal_size;
        int t = s[pos] & 0x1F;
        if (sy && sy->timing && !sei_done && (t == 1 || t == 5)) { /* the messages of the PSP's own streams, in front of the first slice */
            uint8_t m[64];
            int ml;
            if (idr) {
                if ((ml = h264_sei_buffering(m, sizeof(m))) < 0 || (n = put_nal(out, n, cap, 3, m, ml)) < 0)
                    return -1;
            }
            if ((ml = h264_sei_pic_timing(sy->cpb_removal_delay, sy->dpb_output_delay, m, sizeof(m))) < 0 || (n = put_nal(out, n, cap, 3, m, ml)) < 0)
                return -1;
            if (idr) {
                if ((ml = h264_sei_recovery(m, sizeof(m))) < 0 || (n = put_nal(out, n, cap, 3, m, ml)) < 0)
                    return -1;
            }
            sei_done = 1;
        }
        if (t == 9 || (sy && sy->timing && t == 6)) {
            /* (a delimiter is written first, and a stream that is given the PSP's messages does not keep its own) */
        } else if (sy && t == 7) {
            uint8_t sps[SPS_BUF];
            int sl = sy->timing ? h264_sps_rewrite(s + pos, l, sy->tick, sps, sizeof(sps)) : sy->tick.to_main ? h264_sps_relabel(s + pos, l, sps, sizeof(sps)) : -2;
            if (sl == -2) { /* (nothing to change in it) */
                if ((n = put_nal(out, n, cap, 4, s + pos, l)) < 0)
                    return -1;
                pos += l;
                continue;
            }
            if (sl < 0 || (n = put_nal(out, n, cap, 4, sps, sl)) < 0)
                return -1;
        } else {
            int sc = (t == 7 || t == 8) ? 4 : 3;
            if ((n = put_nal(out, n, cap, sc, s + pos, l)) < 0)
                return -1;
        }
        pos += l;
    }
    return n;
}

int mpegps_adts(const uint8_t *asc, int asc_len, int payload, uint8_t out[7])
{
    static const int RATES[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    (void)RATES;
    if (asc_len < 2)
        return 0;
    int obj = asc[0] >> 3;
    int freq = ((asc[0] & 7) << 1) | (asc[1] >> 7);
    int chan = (asc[1] >> 3) & 15;
    if (freq == 15 || freq > 12 || obj < 1 || obj > 4 || chan < 1 || chan > 7)
        return 0;
    int total = payload + 7;
    out[0] = 0xFF;
    out[1] = 0xF1; /* MPEG-4, no CRC */
    out[2] = (uint8_t)(((obj - 1) << 6) | (freq << 2) | (chan >> 2));
    out[3] = (uint8_t)(((chan & 3) << 6) | ((total >> 11) & 3));
    out[4] = (uint8_t)((total >> 3) & 0xFF);
    out[5] = (uint8_t)(((total & 7) << 5) | 0x1F);
    out[6] = 0xFC;
    return 1;
}

/* ---------------------------------------------------------------------- */
/* the program stream */

#define PS_PACK 14
#define PS_SYSTEM 18
#define PS_PES_FIXED 9 /* the start code, the length and the three bytes of flags */
#define PS_STAMPS_FIRST 13 /* PTS, DTS and the P-STD extension */
#define PS_STAMPS 10       /* PTS and DTS */
#define PS_TABLE_PACKET(n) (6 + 18 + 4 * (n))

static const uint8_t SYSTEM_HEADER[PS_SYSTEM] = { 0x00, 0x00, 0x01, 0xBB, 0x00, 0x0C, 0x80, 0xC3, 0x51, 0x80, 0xF0, 0x7F, 0xB9, 0xE0, 0x55, 0xBD, 0xE0, 0x08 };

void ps_init(PsWriter *w, int first, int count, PsSize size, PsPull pull, void *arg)
{
    memset(w, 0, sizeof(*w));
    w->size = size;
    w->pull = pull;
    w->arg = arg;
    w->plan_at = first;
    w->count = count;
    w->last_stamp_idx = -1000;
    w->cur_idx = -1;
}

/* the units of the next group: from the next key frame (or the first unit) to the one before the next, at most PS_TABLE_MAX; their table and the four sector numbers. Returns their number. */
static int plan_group(PsWriter *w)
{
    int sizes[PS_TABLE_MAX], n = 0, big = 0, j = w->plan_at;
    for (; j < w->count && n < PS_TABLE_MAX; j++) {
        int key = 0, len = w->size(w->arg, j, &key);
        if (len < 0)
            continue;
        if (n > 0 && key)
            break;
        sizes[n++] = len;
        if (len > 65535)
            big = 1; /* it does not fit the table (16 bits): the group has none */
    }
    w->plan_at = j;
    w->table_n = 0;
    if (n > 0 && !big) {
        int room_first = 2048 - PS_PACK - PS_SYSTEM - PS_TABLE_PACKET(n) - PS_PES_FIXED - PS_STAMPS_FIRST, room_next = 2048 - PS_PACK - PS_PES_FIXED;
        int end = 0;
        for (int k = 0; k < n; k++) {
            end += sizes[k];
            w->table[k] = (uint16_t)sizes[k];
            if (k < 4)
                w->words[k] = (uint16_t)(end - 1 < room_first ? 0 : 1 + (end - 1 - room_first) / room_next); /* the sector the last byte of the unit is in, counted from the first */
        }
        for (int k = n; k < 4; k++)
            w->words[k] = w->words[n - 1];
        w->table_n = n;
    }
    w->group_left = n;
    w->new_group = n > 0;
    return n;
}

static void take_ts(int64_t pts, int64_t dts, int64_t *out_pts, int64_t *out_dts)
{
    /* the PSP's own streams have the decoding time one picture (3003 ticks) before the time to show it also when nothing is reordered: every decoding time is put one picture earlier (the order
     * of the units and the delay that reordering needs stay as they are) */
    *out_pts = pts < 0 ? 0 : pts;
    *out_dts = dts - 3003;
    if (*out_dts < 0)
        *out_dts = 0;
}

/* the unit after the current one, pulled but not yet current */
static int ahead(PsWriter *w)
{
    if (w->has_ahead)
        return 1;
    if (w->group_left <= 0)
        return 0;
    int64_t pts, dts;
    w->group_left--;
    if (!w->pull(w->arg, &w->adata, &w->alen, &pts, &dts) || w->alen <= 0) {
        w->group_left = 0; /* the stream gave less than was planned (it is the same samples: it cannot be) */
        return 0;
    }
    take_ts(pts, dts, &w->apts, &w->adts);
    w->has_ahead = 1;
    return 1;
}

static int advance(PsWriter *w)
{
    if (!ahead(w))
        return 0;
    w->data = w->adata;
    w->len = w->alen;
    w->pos = 0;
    w->pts = w->apts;
    w->dts = w->adts;
    w->has_ahead = 0;
    w->cur_idx++;
    return 1;
}

static void put_ts(uint8_t *p, int prefix, int64_t ts)
{
    p[0] = (uint8_t)((prefix << 4) | (((ts >> 30) & 7) << 1) | 1);
    p[1] = (uint8_t)((ts >> 22) & 0xFF);
    p[2] = (uint8_t)((((ts >> 15) & 0x7F) << 1) | 1);
    p[3] = (uint8_t)((ts >> 7) & 0xFF);
    p[4] = (uint8_t)(((ts & 0x7F) << 1) | 1);
}

int ps_sector(PsWriter *w, uint8_t out[2048])
{
    w->cur_starts = 0;
    if (w->pos >= w->len) { /* the unit is written: the next one of the group, or the first of the next group */
        if (w->group_left <= 0 && !w->has_ahead && !plan_group(w))
            return 0;
        if (!advance(w))
            return 0;
        w->cur_starts = 1;
    }
    int group_start = w->new_group;
    w->new_group = 0;
    memset(out, 0xFF, 2048);
    uint8_t *p = out + PS_PACK;
    int table = group_start && w->table_n > 0;
    if (table)
        p += PS_SYSTEM + PS_TABLE_PACKET(w->table_n);
    /* does a time stamp go in: always at the start of a group; later when a unit starts in this packet and enough units have started since the last stamp */
    int stamp = 0, ext = 0;
    int64_t spts = w->pts, sdts = w->dts;
    if (group_start) {
        stamp = 1;
        ext = 1;
        w->last_stamp_idx = w->cur_idx;
    } else {
        int idx = -1;
        int room_s = 2048 - PS_PACK - PS_PES_FIXED - PS_STAMPS;
        if (w->cur_starts) {
            idx = w->cur_idx;
        } else if (w->len - w->pos < room_s && ahead(w)) {
            idx = w->cur_idx + 1;
            spts = w->apts;
            sdts = w->adts;
        }
        if (idx >= 0 && idx - w->last_stamp_idx >= PS_STAMP_EVERY) {
            stamp = 1;
            w->last_stamp_idx = idx;
        }
    }
    int hdr = stamp ? (ext ? PS_STAMPS_FIRST : PS_STAMPS) : 0;
    int room = 2048 - (int)(p - out) - PS_PES_FIXED - hdr; /* payload that fits behind the PES header of this sector */
    /* the system clock leads the decoding time by about a second */
    int64_t scr = (stamp ? sdts : w->dts) - 86000;
    if (scr < w->scr)
        scr = w->scr;
    if (scr < 0)
        scr = 0;
    w->scr = scr;
    out[0] = 0; out[1] = 0; out[2] = 1; out[3] = 0xBA;
    out[4] = (uint8_t)(0x44 | (((scr >> 30) & 7) << 3) | ((scr >> 28) & 3));
    out[5] = (uint8_t)((scr >> 20) & 0xFF);
    out[6] = (uint8_t)(0x04 | (((scr >> 15) & 0x1F) << 3) | ((scr >> 13) & 3));
    out[7] = (uint8_t)((scr >> 5) & 0xFF);
    out[8] = (uint8_t)(0x04 | ((scr & 0x1F) << 3));
    out[9] = 0x01;
    out[10] = 0x01; out[11] = 0x86; out[12] = 0xA3; /* 25000 * 50 bytes a second, as the PSP's own streams */
    out[13] = 0xF8;
    if (table) { /* the start of a group: the system header and the table of the sizes of its units */
        uint8_t *t = out + PS_PACK;
        memcpy(t, SYSTEM_HEADER, PS_SYSTEM);
        t += PS_SYSTEM;
        int n = w->table_n, len = 18 + 4 * n;
        t[0] = 0; t[1] = 0; t[2] = 1; t[3] = 0xBF;
        t[4] = (uint8_t)(len >> 8); t[5] = (uint8_t)len;
        t[6] = 0x01; t[7] = 0xE0;
        for (int k = 0; k < 4; k++) {
            t[8 + 2 * k] = (uint8_t)(w->words[k] >> 8);
            t[9 + 2 * k] = (uint8_t)w->words[k];
        }
        memset(t + 16, 0, 4);
        t[20] = (uint8_t)((2 + 4 * n) >> 8); t[21] = (uint8_t)(2 + 4 * n);
        t[22] = (uint8_t)(n >> 8); t[23] = (uint8_t)n;
        for (int k = 0; k < n; k++) { /* (the flag is 0x0080, and 0 for the last unit of the table) */
            t[24 + 4 * k] = 0;
            t[25 + 4 * k] = k == n - 1 ? 0x00 : 0x80;
            t[26 + 4 * k] = (uint8_t)(w->table[k] >> 8);
            t[27 + 4 * k] = (uint8_t)w->table[k];
        }
    }
    /* the payload: the rest of the unit, and the units after it of the group while there is room */
    uint8_t *q = p + PS_PES_FIXED + hdr;
    int left = room;
    while (left > 0) {
        int rem = w->len - w->pos;
        if (rem == 0) {
            if (!advance(w))
                break; /* the group is at its end */
            rem = w->len;
        }
        int take = rem < left ? rem : left;
        memcpy(q, w->data + w->pos, (size_t)take);
        w->pos += take;
        q += take;
        left -= take;
    }
    int slack = left, stuff = 0, pad = 0;
    if (slack > 0 && slack < 6)
        stuff = slack; /* too little for a padding packet: stuffing bytes in the PES header */
    else
        pad = slack;
    if (stuff) { /* the payload moves behind the stuffing bytes */
        memmove(p + PS_PES_FIXED + hdr + stuff, p + PS_PES_FIXED + hdr, (size_t)(room - slack));
        memset(p + PS_PES_FIXED + hdr, 0xFF, (size_t)stuff);
    }
    int payload = room - slack;
    int pes_len = 3 + hdr + stuff + payload;
    p[0] = 0; p[1] = 0; p[2] = 1; p[3] = 0xE0;
    p[4] = (uint8_t)(pes_len >> 8);
    p[5] = (uint8_t)(pes_len & 0xFF);
    p[6] = 0x81;
    p[7] = (uint8_t)(stamp ? (ext ? 0xC1 : 0xC0) : 0x00);
    p[8] = (uint8_t)(hdr + stuff);
    if (stamp) {
        put_ts(p + 9, 3, spts);
        put_ts(p + 14, 1, sdts);
        if (ext) {
            p[19] = 0x1E; /* the extension holds the size of the decoder's buffer (P-STD): 85 KB, as in the PSP's own streams */
            p[20] = 0x60;
            p[21] = 0x55;
        }
    }
    if (pad >= 6) { /* a padding packet to the end of the sector */
        uint8_t *e = p + PS_PES_FIXED + hdr + payload;
        e[0] = 0; e[1] = 0; e[2] = 1; e[3] = 0xBE;
        e[4] = (uint8_t)((pad - 6) >> 8);
        e[5] = (uint8_t)((pad - 6) & 0xFF);
        memset(e + 6, 0xFF, (size_t)(pad - 6));
    }
    return 1;
}
