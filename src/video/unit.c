/* psp5 authentic – the units of an MP4 for the PSP's program stream (see unit.h) */
#include <math.h>
#include <string.h>
#include "unit.h"
#include "mpegps.h"

/* the SPS in an Annex B block of parameter sets: its offset (behind the start code) and length; 0 when there is none */
static int find_sps(const uint8_t *p, int n, int *off, int *len)
{
    int i = 0;
    while (i + 4 < n) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
            int start = i + 3, j = start;
            while (j + 2 < n && !(p[j] == 0 && p[j + 1] == 0 && p[j + 2] <= 1))
                j++;
            int end = j + 2 < n ? j : n;
            if ((p[start] & 0x1F) == 7) {
                *off = start;
                *len = end - start;
                return 1;
            }
            i = end;
        } else {
            i++;
        }
    }
    return 0;
}

int unit_init(UnitMaker *u, Mp4 *m, uint8_t *sample, int cap)
{
    int no_synth = u->no_synth, no_main = u->no_main, force = u->force; /* (set by the caller before) */
    memset(u, 0, sizeof(*u));
    u->no_synth = no_synth;
    u->no_main = no_main;
    u->force = force;
    u->m = m;
    u->sample = sample;
    u->cap = cap;
    u->params_len = mp4_param_sets(m, u->params, sizeof(u->params));
    int s0 = mp4_video_size(m, 0);
    const uint8_t *sps = NULL;
    int sps_len = 0;
    if (s0 > 0 && s0 <= cap && mp4_video_read(m, 0, sample, s0) == s0) {
        u->inband = mpegps_sample_has_params(sample, s0, mp4_nal_size(m));
        if (u->inband) { /* the SPS of the first sample */
            int nal = mp4_nal_size(m);
            for (int pos = 0; pos + nal <= s0;) {
                int l = 0;
                for (int i = 0; i < nal; i++)
                    l = (l << 8) | sample[pos + i];
                pos += nal;
                if (l <= 0 || pos + l > s0)
                    break;
                if ((sample[pos] & 0x1F) == 7) {
                    sps = sample + pos;
                    sps_len = l;
                    break;
                }
                pos += l;
            }
        }
    }
    int off = 0, len = 0;
    if (!sps && u->params_len > 0 && find_sps(u->params, u->params_len, &off, &len)) {
        sps = u->params + off;
        sps_len = len;
    }
    if (!sps)
        return u->params_len > 0 || u->inband; /* no SPS found: left as it is */
    u->synth = !h264_sps_has_hrd(sps, sps_len) && !u->no_synth;
    h264_sps_info(sps, sps_len, &u->profile, &u->nref, &u->wmb, &u->hmb, &u->poc);
    /* the PPS: slice groups, the entropy mode, the reference pictures (the one of the header's block, or else the first sample's) */
    int fmo = 1, cabac = 0, found = 0;
    for (int i = 0; i + 4 < u->params_len && !found; i++)
        if (u->params[i] == 0 && u->params[i + 1] == 0 && u->params[i + 2] == 1 && (u->params[i + 3] & 0x1F) == 8) {
            int e = i + 4;
            while (e + 2 < u->params_len && !(u->params[e] == 0 && u->params[e + 1] == 0 && u->params[e + 2] <= 1))
                e++;
            fmo = h264_pps_info(u->params + i + 3, (e + 2 < u->params_len ? e : u->params_len) - (i + 3), &cabac, &u->nref0, &u->weighted);
            found = 1;
        }
    if (!found && u->inband && s0 > 0) {
        int nal = mp4_nal_size(m);
        for (int pos = 0; pos + nal <= s0 && !found;) {
            int l = 0;
            for (int i = 0; i < nal; i++)
                l = (l << 8) | sample[pos + i];
            pos += nal;
            if (l <= 0 || pos + l > s0)
                break;
            if ((sample[pos] & 0x1F) == 8) {
                fmo = h264_pps_info(sample + pos, l, &cabac, &u->nref0, &u->weighted);
                found = 1;
            }
            pos += l;
        }
    }
    u->cabac = cabac;
    /* what the PSP's decoder (the one of its sceMpeg library) is known not to take: it answers its fatal error, after which no video decodes until psp5 is started again. CAVLC: the maintainer's tests
     * (Baseline with one reference frame, relabelled Main) gave the fatal error, CABAC streams decoded. A High profile: the PSP's own player says "corrupted". */
    /* a CAVLC stream the PSP's decoder in its Baseline mode may be tried with (hwavc.h): I and P pictures only (picture order type 2), no slice groups, Main or Baseline profile */
    u->hw_ok = found && !cabac && !fmo && (u->profile == 66 || u->profile == 77) && u->poc == 2;
    u->hw_main = found && !fmo && u->profile == 77 && !u->hw_ok; /* the rest of the Main profile (CABAC, or B pictures): the PSP's decoder in its Main mode */
    /* what the PSP's decoder (the one of its sceMpeg library) is known not to take: it answers its fatal error, after which no video decodes until psp5 is started again. CAVLC: the maintainer's tests
     * (Baseline with one reference frame, relabelled Main) gave the fatal error, CABAC streams decoded. A High profile: the PSP's own player says "corrupted". */
    if (!u->force) {
        if (found && !cabac)
            u->unsupported = 1;
        else if (u->profile >= 100)
            u->unsupported = 2;
    }
    /* a Baseline stream that has no slice groups is made a Main one (see h264_sps_rewrite) */
    if (sps_len > 1 && (sps[0] & 0x1F) == 7 && sps[1] == 66) {
        u->to_main = found && !fmo && !u->no_main;
        u->tick.to_main = u->to_main;
    }
    if (!u->synth) { /* the PSP's timing is in it already: only a Baseline SPS is relabelled, in the header's block and (by the converter) in the samples */
        if (u->to_main && !u->inband && u->params_len > 0 && off >= 4) {
            uint8_t block[512];
            int n = off;
            memcpy(block, u->params, (size_t)n);
            int sl = h264_sps_relabel(u->params + off, len, block + n, (int)sizeof(block) - n);
            if (sl < 0 || n + sl + (u->params_len - off - len) > (int)sizeof(block))
                return 0;
            memcpy(block + n + sl, u->params + off + len, (size_t)(u->params_len - off - len));
            u->params_len = n + sl + (u->params_len - off - len);
            memcpy(u->params, block, (size_t)u->params_len);
        }
        return 1;
    }
    /* the clock tick is a field of the video: 4004 / 240000 for 29.97 pictures a second, else 1000 / (2000 times the pictures a second) */
    double fps = mp4_duration(m) > 0 && mp4_video_count(m) > 1 ? mp4_video_count(m) / (double)mp4_duration(m) : 30000.0 / 1001.0;
    if (fps < 1)
        fps = 1;
    if (fps > 120)
        fps = 120;
    if (fabs(fps - 30000.0 / 1001.0) < 0.05) {
        u->tick.num_units = 4004;
        u->tick.time_scale = 240000;
    } else {
        u->tick.num_units = 1000;
        u->tick.time_scale = (unsigned)(2000.0 * fps + 0.5);
    }
    u->tick_s = (double)u->tick.num_units / u->tick.time_scale;
    if (!u->inband && u->params_len > 0 && off >= 4) { /* the SPS of the header is rewritten in the block (the sets behind it stay) */
        uint8_t block[512];
        int n = off; /* the start code in front of it */
        memcpy(block, u->params, (size_t)n);
        int sl = h264_sps_rewrite(u->params + off, len, u->tick, block + n, (int)sizeof(block) - n);
        if (sl < 0 || n + sl + (u->params_len - off - len) > (int)sizeof(block))
            return 0;
        memcpy(block + n + sl, u->params + off + len, (size_t)(u->params_len - off - len));
        u->params_len = n + sl + (u->params_len - off - len);
        memcpy(u->params, block, (size_t)u->params_len);
    }
    return 1;
}

int unit_make(UnitMaker *u, int i, uint8_t *out, int out_cap, int *group_start)
{
    int size = mp4_video_size(u->m, i);
    float dts, pts;
    int key = 0;
    *group_start = 0;
    if (size <= 0 || size > u->cap)
        return -1;
    mp4_video_times(u->m, i, &dts, &pts, &key);
    if (mp4_video_read(u->m, i, u->sample, size) != size)
        return -1;
    PsSynth sy;
    const PsSynth *syp = NULL;
    sy.timing = 0;
    sy.tick = u->tick;
    sy.cpb_removal_delay = sy.dpb_output_delay = 0;
    if (!u->synth && u->to_main)
        syp = &sy; /* (the timing is in the stream: only the profile of its SPS) */
    if (u->synth) {
        /* in ticks: the decoding time since the key frame this unit belongs to, and the time from its decoding to its showing; the decoding time is the one the program stream gives: one picture
         * (3003 ticks of 90 kHz) earlier than the container's */
        const double gap = 3003.0 / 90000.0;
        double d = dts - gap;
        float kd, kp;
        int kk, ki = mp4_video_at(u->m, dts);
        mp4_video_times(u->m, ki, &kd, &kp, &kk);
        long cpb = lround((dts - kd) / u->tick_s), dpb = lround((pts - d) / u->tick_s);
        sy.timing = 1;
        sy.cpb_removal_delay = (unsigned)(cpb < 0 ? 0 : cpb);
        sy.dpb_output_delay = (unsigned)(dpb < 0 ? 0 : dpb > 127 ? 127 : dpb);
        syp = &sy;
    }
    int l = mpegps_annexb(u->sample, size, mp4_nal_size(u->m), u->inband ? NULL : u->params, u->inband ? 0 : u->params_len, key, out, out_cap, syp);
    if (l > 0)
        *group_start = mpegps_unit_has_sps(out, l);
    return l;
}
