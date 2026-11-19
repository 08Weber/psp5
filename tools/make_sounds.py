#!/usr/bin/env python3
"""
PSP5 REMAKE – tools/make_sounds.py
Decodes Deck-UI_Sound/*.flac (bit-exact pure-Python FLAC decoder with MD5 check),
mixes to mono 44100 Hz, trims leading/trailing silence with smooth fade-out,
normalizes for maximum 4-bit IMA ADPCM precision with per-sound playback gain,
and writes src/deck_sounds.h for zero-heap on-the-fly ADPCM playback.
"""
import hashlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOUND_DIR = os.path.join(ROOT, "Deck-UI_Sound")
OUT_HEADER = os.path.join(ROOT, "src", "deck_sounds.h")
TARGET_RATE = 44100

# (C enum suffix, FLAC filename, target playback peak [0.0 - 0.60])
SOUND_MAP = [
    ("MOVE",       "Deck UI Navigation.flac",          0.26),
    ("TILE",       "Deck UI Tile Scroll.flac",         0.28),
    ("BUMPER",     "Deck UI Bumper End 02.flac",       0.26),
    ("SWIPE",      "Deck UI Tab Transition 01.flac",   0.24),
    ("SELECT",     "Confirmation Positive.flac",       0.32),
    ("BACK",       "Deck UI Misc 10.flac",             0.26),
    ("TOGGLE",     "Deck UI Switch Toggle On.flac",    0.30),
    ("TOGGLE_OFF", "Deck UI Switch Toggle Off.flac",   0.30),
    ("SLIDER_UP",  "Deck UI Slider Up.flac",           0.26),
    ("SLIDER_DN",  "Deck UI Slider Down.flac",         0.26),
    ("OPEN",       "Deck UI Show Modal.flac",          0.28),
    ("CLOSE",      "Deck UI Hide Modal.flac",          0.28),
    ("SIDE_IN",    "Deck UI Side Menu Fly In.flac",    0.28),
    ("SIDE_OUT",   "Deck UI Side Menu Fly Out.flac",   0.28),
    ("DETAIL_IN",  "Deck UI Into Game Detail.flac",    0.30),
    ("DETAIL_OUT", "Deck UI Out Of Game Detail.flac",  0.28),
    ("TYPE",       "Deck UI Typing.flac",              0.24),
    ("TOAST",      "Deck UI Toast.flac",               0.32),
    ("MSG_TOAST",  "Deck UI Message Toast.flac",       0.30),
    ("VOLUME",     "Deck UI Volume.flac",              0.30),
    ("MUSIC_ACT",  "Deck UI Misc 01.flac",             0.24),
    ("LAUNCH",     "Deck UI Launch Game.flac",         0.38),
    ("ERROR",      "Confirmation Negative.flac",       0.30),
    ("BOOT",       "Steam Os Startup.flac",            0.42),
]

IMA_INDEX_TABLE = [
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
]

IMA_STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
]


class BitReader:
    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos
        self.bitbuf = 0
        self.bits = 0

    def align(self):
        self.bits = 0
        self.bitbuf = 0

    def read_bits(self, n):
        while self.bits < n:
            if self.pos >= len(self.data):
                raise EOFError("Unexpected EOF in FLAC stream")
            self.bitbuf = (self.bitbuf << 8) | self.data[self.pos]
            self.pos += 1
            self.bits += 8
        self.bits -= n
        val = (self.bitbuf >> self.bits) & ((1 << n) - 1)
        self.bitbuf &= (1 << self.bits) - 1
        return val

    def read_signed(self, n):
        v = self.read_bits(n)
        if n > 0 and (v & (1 << (n - 1))):
            v -= 1 << n
        return v

    def read_unary(self):
        cnt = 0
        while True:
            while self.bits == 0:
                if self.pos >= len(self.data):
                    raise EOFError("Unexpected EOF in unary")
                self.bitbuf = self.data[self.pos]
                self.pos += 1
                self.bits = 8
            self.bits -= 1
            if (self.bitbuf >> self.bits) & 1:
                self.bitbuf &= (1 << self.bits) - 1
                return cnt
            cnt += 1

    def read_utf8_uint(self):
        b0 = self.read_bits(8)
        if b0 < 0x80:
            return b0
        if (b0 & 0xE0) == 0xC0:
            n, v = 1, b0 & 0x1F
        elif (b0 & 0xF0) == 0xE0:
            n, v = 2, b0 & 0x0F
        elif (b0 & 0xF8) == 0xF0:
            n, v = 3, b0 & 0x07
        elif (b0 & 0xFC) == 0xF8:
            n, v = 4, b0 & 0x03
        elif (b0 & 0xFE) == 0xFC:
            n, v = 5, b0 & 0x01
        else:
            n, v = 6, 0
        for _ in range(n):
            v = (v << 6) | (self.read_bits(8) & 0x3F)
        return v


def decode_residual(br, blocksize, pred_order, samples):
    method = br.read_bits(2)
    assert method in (0, 1)
    p_order = br.read_bits(4)
    n_parts = 1 << p_order
    part_samples = blocksize >> p_order
    param_bits = 4 if method == 0 else 5
    esc_val = (1 << param_bits) - 1

    for p in range(n_parts):
        k = br.read_bits(param_bits)
        cnt = part_samples - pred_order if p == 0 else part_samples
        if k == esc_val:
            rbits = br.read_bits(5)
            for _ in range(cnt):
                samples.append(br.read_signed(rbits))
        else:
            for _ in range(cnt):
                q = br.read_unary()
                r = br.read_bits(k) if k > 0 else 0
                u = (q << k) | r
                samples.append(-(u >> 1) - 1 if (u & 1) else (u >> 1))


def decode_subframe(br, blocksize, bps):
    pad = br.read_bits(1)
    assert pad == 0
    stype = br.read_bits(6)
    wasted = br.read_bits(1)
    if wasted:
        wasted = br.read_unary() + 1
    eff_bps = bps - wasted

    if stype == 0:
        samples = [br.read_signed(eff_bps)] * blocksize
    elif stype == 1:
        samples = [br.read_signed(eff_bps) for _ in range(blocksize)]
    elif 8 <= stype <= 12:
        order = stype - 8
        samples = [br.read_signed(eff_bps) for _ in range(order)]
        decode_residual(br, blocksize, order, samples)
        if order == 1:
            for i in range(1, blocksize):
                samples[i] += samples[i - 1]
        elif order == 2:
            for i in range(2, blocksize):
                samples[i] += 2 * samples[i - 1] - samples[i - 2]
        elif order == 3:
            for i in range(3, blocksize):
                samples[i] += 3 * samples[i - 1] - 3 * samples[i - 2] + samples[i - 3]
        elif order == 4:
            for i in range(4, blocksize):
                samples[i] += 4 * samples[i - 1] - 6 * samples[i - 2] + 4 * samples[i - 3] - samples[i - 4]
    elif 32 <= stype <= 63:
        order = stype - 31
        samples = [br.read_signed(eff_bps) for _ in range(order)]
        prec = br.read_bits(4) + 1
        shift = br.read_signed(5)
        coeffs = [br.read_signed(prec) for _ in range(order)]
        decode_residual(br, blocksize, order, samples)
        for i in range(order, blocksize):
            acc = 0
            for j in range(order):
                acc += coeffs[j] * samples[i - 1 - j]
            samples[i] += acc >> shift
    else:
        raise ValueError(f"Unsupported subframe type: {stype}")

    if wasted:
        samples = [s << wasted for s in samples]
    return samples


def decode_flac_mono(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"fLaC", f"Not FLAC: {path}"
    pos = 4
    sr = ch = bps = total_samples = 0
    expected_md5 = b""
    while True:
        hdr = data[pos]
        is_last = (hdr & 0x80) != 0
        btype = hdr & 0x7F
        blen = int.from_bytes(data[pos + 1 : pos + 4], "big")
        pos += 4
        block = data[pos : pos + blen]
        pos += blen
        if btype == 0:
            packed = int.from_bytes(block[10:18], "big")
            sr = (packed >> 44) & 0xFFFFF
            ch = ((packed >> 41) & 0x7) + 1
            bps = ((packed >> 36) & 0x1F) + 1
            total_samples = packed & 0xFFFFFFFFF
            expected_md5 = block[18:34]
        if is_last:
            break

    br = BitReader(data, pos)
    pcm_ch = [[] for _ in range(ch)]
    decoded = 0
    while decoded < total_samples:
        sync = br.read_bits(14)
        assert sync == 0x3FFE
        assert br.read_bits(1) == 0
        br.read_bits(1)
        bs_code = br.read_bits(4)
        sr_code = br.read_bits(4)
        ch_assign = br.read_bits(4)
        bps_code = br.read_bits(3)
        assert br.read_bits(1) == 0
        br.read_utf8_uint()

        if bs_code == 1:
            bs = 192
        elif 2 <= bs_code <= 5:
            bs = 576 << (bs_code - 2)
        elif bs_code == 6:
            bs = br.read_bits(8) + 1
        elif bs_code == 7:
            bs = br.read_bits(16) + 1
        else:
            bs = 256 << (bs_code - 8)

        if sr_code == 12:
            br.read_bits(8)
        elif sr_code in (13, 14):
            br.read_bits(16)
        br.read_bits(8)

        frame_bps = bps if bps_code == 0 else {1: 8, 2: 12, 4: 16, 5: 20, 6: 24}[bps_code]
        nch = ch if ch_assign < 8 else 2
        subs = []
        for c in range(nch):
            sbps = frame_bps
            if (ch_assign in (8, 10) and c == 1) or (ch_assign == 9 and c == 0):
                sbps += 1
            subs.append(decode_subframe(br, bs, sbps))
        br.align()
        br.read_bits(16)

        if ch_assign == 8:
            left, side = subs[0], subs[1]
            subs = [left, [l - s for l, s in zip(left, side)]]
        elif ch_assign == 9:
            side, right = subs[0], subs[1]
            subs = [[r + s for r, s in zip(right, side)], right]
        elif ch_assign == 10:
            mid, side = subs[0], subs[1]
            left = [((m << 1) + (s & 1) + s) >> 1 for m, s in zip(mid, side)]
            subs = [left, [l - s for l, s in zip(left, side)]]

        for c in range(ch):
            pcm_ch[c].extend(subs[c])
        decoded += bs

    md5 = hashlib.md5()
    raw = bytearray()
    for i in range(total_samples):
        for c in range(ch):
            raw.extend(struct.pack("<h", pcm_ch[c][i]))
    md5.update(raw)
    assert md5.digest() == expected_md5, f"MD5 mismatch for {path}"

    if ch == 1:
        mono = pcm_ch[0][:total_samples]
    else:
        mono = [(pcm_ch[0][i] + pcm_ch[1][i]) >> 1 for i in range(total_samples)]
    return sr, mono


def resample_linear(samples, src_rate, dst_rate):
    if src_rate == dst_rate:
        return samples
    n_src = len(samples)
    n_dst = int(round(n_src * dst_rate / src_rate))
    out = [0] * n_dst
    ratio = src_rate / dst_rate
    for i in range(n_dst):
        pos = i * ratio
        idx = int(pos)
        frac = pos - idx
        s0 = samples[idx]
        s1 = samples[idx + 1] if idx + 1 < n_src else s0
        out[i] = int(round(s0 + (s1 - s0) * frac))
    return out


def trim_and_normalize(samples, rate, target_peak):
    thr = 40  # ~-58 dB threshold
    n = len(samples)
    first = 0
    while first < n and abs(samples[first]) < thr:
        first += 1
    last = n - 1
    while last > first and abs(samples[last]) < thr:
        last -= 1

    # Preserve tiny pre-roll (2ms) and post-roll (16ms) so transients and tails stay natural
    pre = min(first, int(0.002 * rate))
    post = min(n - 1 - last, int(0.016 * rate))
    seg = list(samples[first - pre : last + 1 + post])
    if not seg:
        return [0], 256

    # Smooth 8ms fade-out at end to prevent any stop-click
    fade_n = min(len(seg), int(0.008 * rate))
    for i in range(fade_n):
        idx = len(seg) - fade_n + i
        seg[idx] = int(round(seg[idx] * (fade_n - 1 - i) / max(1, fade_n)))

    # Normalize segment to ~88% of 16-bit full scale for max IMA ADPCM precision
    peak = max(abs(x) for x in seg)
    if peak < 1:
        return [0], 256
    norm_scale = 28800.0 / peak
    norm = [max(-32768, min(32767, int(round(x * norm_scale)))) for x in seg]

    # Compute Q8 playback gain so decoded peak * (gain_q8 / 256) ~= target_peak * 32767
    gain_q8 = max(16, min(255, int(round((target_peak * 32767.0 / 28800.0) * 256.0))))
    return norm, gain_q8


def encode_ima_adpcm(samples):
    pred = 0
    step_idx = 0
    out = bytearray()
    n = len(samples)
    for i in range(0, n, 2):
        byte_val = 0
        for shift in (0, 4):
            idx = i + (shift >> 2)
            sample = samples[idx] if idx < n else pred
            step = IMA_STEP_TABLE[step_idx]
            diff = sample - pred
            nibble = 0
            if diff < 0:
                nibble = 8
                diff = -diff
            delta = step >> 3
            if diff >= step:
                nibble |= 4
                diff -= step
                delta += step
            if diff >= (step >> 1):
                nibble |= 2
                diff -= (step >> 1)
                delta += (step >> 1)
            if diff >= (step >> 2):
                nibble |= 1
                delta += (step >> 2)
            if nibble & 8:
                pred -= delta
            else:
                pred += delta
            if pred > 32767:
                pred = 32767
            elif pred < -32768:
                pred = -32768
            step_idx += IMA_INDEX_TABLE[nibble]
            if step_idx < 0:
                step_idx = 0
            elif step_idx > 88:
                step_idx = 88
            byte_val |= (nibble & 0xF) << shift
        out.append(byte_val)
    return bytes(out)


def main():
    blob = bytearray()
    meta = []
    for enum_name, fname, target_peak in SOUND_MAP:
        path = os.path.join(SOUND_DIR, fname)
        sr, mono = decode_flac_mono(path)
        mono = resample_linear(mono, sr, TARGET_RATE)
        norm, gain_q8 = trim_and_normalize(mono, TARGET_RATE, target_peak)
        adpcm = encode_ima_adpcm(norm)
        offset = len(blob)
        blob.extend(adpcm)
        meta.append((enum_name, fname, offset, len(norm), len(adpcm), gain_q8))
        print(f"SND_{enum_name:11s} <- {fname:32s} | {len(norm)/TARGET_RATE:5.3f}s | {len(adpcm):6d} B | gain={gain_q8}/256")

    lines = [
        "/* Generated by tools/make_sounds.py from Deck-UI_Sound FLAC files - do not edit by hand.",
        f" * {len(meta)} mono {TARGET_RATE} Hz sounds encoded as 4-bit IMA ADPCM ({len(blob)} bytes total). */",
        "#ifndef DECK_SOUNDS_H",
        "#define DECK_SOUNDS_H",
        "",
        "typedef struct {",
        "    unsigned int offset;",
        "    int samples;",
        "    unsigned short gain_q8;",
        "} DeckSoundMeta;",
        "",
        f"static const DeckSoundMeta s_deck_meta[{len(meta)}] = {{",
    ]
    for enum_name, fname, offset, nsamp, nbytes, gain_q8 in meta:
        lines.append(f"    {{ {offset:6d}u, {nsamp:6d}, {gain_q8:3d} }}, /* SND_{enum_name}: {fname} ({nbytes} B) */")
    lines.append("};")
    lines.append("")
    lines.append(f"static const unsigned char s_deck_blob[{len(blob)}] = {{")
    for i in range(0, len(blob), 24):
        chunk = blob[i : i + 24]
        lines.append("    " + ",".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")

    with open(OUT_HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"Wrote {OUT_HEADER} ({len(blob)} bytes ADPCM payload)")


if __name__ == "__main__":
    main()
