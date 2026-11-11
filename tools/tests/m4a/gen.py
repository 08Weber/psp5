# psp5 – made by 08Weber
# Skapar små syntetiska M4A-filer för test.c. Ljudet är inte riktigt AAC – m4a.c avkodar
# inget, den hittar bara rutorna. Ruta i är fsize(i) byte stor och fylld med i & 255.
import os, struct, sys

N = 500

def fsize(i):
    return 100 + (i * 37) % 200

def box(t, *parts):
    b = b''.join(parts)
    return struct.pack('>I', 8 + len(b)) + t + b

def full(t, *parts):
    return box(t, b'\0\0\0\0', *parts)

def esds(fi, ch):
    asc = bytes([(2 << 3) | (fi >> 1), ((fi & 1) << 7) | (ch << 3)])  # AAC-LC
    dsi = b'\x05' + bytes([len(asc)]) + asc
    dcd = b'\x04' + bytes([13 + len(dsi)]) + bytes([0x40, 0x15, 0, 0, 0]) + struct.pack('>II', 128000, 128000) + dsi
    es = b'\x03' + bytes([3 + len(dcd)]) + struct.pack('>HB', 1, 0) + dcd
    return full(b'esds', es)

def sample_entry(codec, rate, ch, fi):
    e = b'\0' * 6 + struct.pack('>H', 1) + b'\0' * 8 + struct.pack('>HHHHI', ch, 16, 0, 0, rate << 16)
    return box(codec, e + (esds(fi, ch) if codec == b'mp4a' else b''))

def trak(handler, codec, rate, ch, fi, offsets, co64):
    stsc_entries = [(1, 10), (5, 7), (20, 13)]  # första chunk (1-baserat), rutor per chunk
    stsd = full(b'stsd', struct.pack('>I', 1), sample_entry(codec, rate, ch, fi))
    stsz = full(b'stsz', struct.pack('>II', 0, N), b''.join(struct.pack('>I', fsize(i)) for i in range(N)))
    stsc = full(b'stsc', struct.pack('>I', len(stsc_entries)),
                b''.join(struct.pack('>III', f, n, 1) for f, n in stsc_entries))
    if co64:
        stco = full(b'co64', struct.pack('>I', len(offsets)), b''.join(struct.pack('>Q', o) for o in offsets))
    else:
        stco = full(b'stco', struct.pack('>I', len(offsets)), b''.join(struct.pack('>I', o) for o in offsets))
    mdhd = full(b'mdhd', struct.pack('>IIII', 0, 0, rate, N * 1024), b'\0' * 4)  # version 0: 24 byte
    hdlr = full(b'hdlr', b'\0' * 4 + handler + b'\0' * 13)
    minf = box(b'minf', full(b'smhd', b'\0' * 4), box(b'stbl', stsd, stsc, stsz, stco))
    return box(b'trak', full(b'tkhd', b'\0' * 80), box(b'mdia', mdhd, hdlr, minf))

def chunks():
    """rutorna per chunk enligt stsc-tabellen ovan"""
    out, i, c = [], 0, 1
    while i < N:
        n = 10 if c < 5 else 7 if c < 20 else 13
        out.append(list(range(i, min(i + n, N))))
        i += n
        c += 1
    return out

def item(t, kind, val):
    return box(t, box(b'data', struct.pack('>II', kind, 0) + val))

def make(path, codec=b'mp4a', rate=44100, ch=2, fi=4, tags=True, moov_last=False, co64=False):
    # mdat: chunkarna med 3 byte mellan, så att chunkens början måste komma från stco
    payload, local = b'', []
    for ch_frames in chunks():
        payload += b'xyz'
        local.append(len(payload))
        for i in ch_frames:
            payload += bytes([i & 255]) * fsize(i)
    mdat = box(b'mdat', payload)
    ftyp = box(b'ftyp', b'M4A \0\0\0\0M4A mp42isom')

    def moov(base):
        offs = [base + 8 + o for o in local]
        video = trak(b'vide', b'avc1', 0, 0, 0, offs, co64)  # ska hoppas över
        parts = [box(b'mvhd', b'\0' * 100), video, trak(b'soun', codec, rate, ch, fi, offs, co64)]
        if tags:
            ilst = box(b'ilst',
                       item(b'covr', 13, b'\xff\xd8' + bytes(9000)),  # omslaget först och större än 4 KB
                       item(b'\xa9nam', 1, 'Låt åäö'.encode()),
                       item(b'aART', 1, b'Album Artist'),
                       item(b'\xa9alb', 1, b'  Album  '),
                       item(b'trkn', 0, struct.pack('>HHHH', 0, 7, 12, 0)),
                       item(b'----', 1, b'x'))
            parts.append(box(b'udta', full(b'meta', full(b'hdlr', b'\0' * 4 + b'mdir' + b'\0' * 13), ilst)))
        return box(b'moov', *parts)

    if moov_last:
        data = ftyp + mdat + moov(len(ftyp))
    else:
        size = len(moov(0))  # offseten har fast bredd, så storleken ändras inte
        data = ftyp + moov(len(ftyp) + size) + mdat
    with open(path, 'wb') as f:
        f.write(data)

out = sys.argv[1]
make(os.path.join(out, 'front.m4a'))
make(os.path.join(out, 'tail.m4a'), moov_last=True, co64=True)
make(os.path.join(out, 'notags.m4a'), tags=False, rate=22050, ch=1, fi=7)
make(os.path.join(out, 'alac.m4a'), codec=b'alac')
