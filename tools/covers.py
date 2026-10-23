#!/usr/bin/env python3
# psp5 – made by Mille Weber
"""Förbereder omslagsbilder för psp5 på datorn.

Skannar minneskortet precis som launchern (ISO/ med iso/cso/zso, PSP/GAME/*/EBOOT.PBP)
och skriver färdiga texturer till PSP5/covers/:
    <nyckel>.ico  kortbild 144×80, RGBA8888, rundade hörn
    <nyckel>.bg   bakgrund 320×182, RGB565
Nyckeln är DISC_ID för ISO-spel och mappnamnet för EBOOT-appar.

Bildkällor, i den här ordningen:
    1. egna bilder i --src (standard: tools/covers-src), namngivna efter nyckel eller titel:
         ULUS10041.jpg  /  "Aurora Drift.png"       → kortbild (beskärs till 16:9-ish)
         ULUS10041_bg.jpg / "Aurora Drift_bg.png"   → bakgrund
    2. spelets egen ICON0.PNG / PIC1.PNG

Exempel:
    py tools\\covers.py E:\\              PSP:n inkopplad som E:
    py tools\\covers.py E:\\ --force      skriv om alla
    py tools\\covers.py E:\\ --clean      ta också bort omslag för spel som inte längre finns
    py tools\\covers.py E:\\ --dry-run    visa bara vad som skulle göras

Skriptet skriver bara i PSP5/covers på minneskortet.
"""
import argparse
import io
import re
import struct
import sys
import zlib
from pathlib import Path

try:
    import lz4.block
    from PIL import Image, ImageDraw, ImageFilter
except ImportError:
    sys.exit("Saknar paket – kör:  py -m pip install --user pillow lz4")

# måste stämma med src/art.h och src/image.c
ICON_W, ICON_H = 144, 80
BG_W, BG_H = 480, 272  # skarp bakgrund i skärmens storlek (suddiga ICON0 blir också det – ingen skillnad i utseende)
ICON_RADIUS = 7.0
NTX_MAGIC = 0x3158544E  # "NTX1"
PSM_5650, PSM_8888 = 0, 3
IMAGE_EXTS = (".png", ".jpg", ".jpeg", ".webp", ".bmp")
SECTOR = 2048


# --- läsning av ISO / CSO / ZSO ---

class IsoReader:
    """Läser okomprimerade ISO:er och CSO/ZSO (v1 och v2)."""

    def __init__(self, path):
        self.f = open(path, "rb")
        head = self.f.read(24)
        self.magic = head[:4]
        self.compressed = self.magic in (b"CISO", b"ZISO")
        if self.compressed:
            _, hsize, self.total, self.block, self.ver, self.align = struct.unpack("<IIQIBB", head[:22])
            n = (self.total + self.block - 1) // self.block
            self.f.seek(hsize if hsize >= 24 else 24)
            self.index = struct.unpack("<%dI" % (n + 1), self.f.read((n + 1) * 4))
            self.cache = {}

    def close(self):
        self.f.close()

    def _block(self, i):
        if i in self.cache:
            return self.cache[i]
        a, b = self.index[i], self.index[i + 1]
        start = (a & 0x7FFFFFFF) << self.align
        end = (b & 0x7FFFFFFF) << self.align
        self.f.seek(start)
        raw = self.f.read(end - start)
        flag = a & 0x80000000
        if self.ver >= 2 and len(raw) >= self.block:
            data = raw[:self.block]
        elif self.ver < 2 and flag:
            data = raw[:self.block]
        elif self.magic == b"ZISO" or (self.ver >= 2 and flag):
            data = lz4.block.decompress(raw, uncompressed_size=self.block)
        else:
            data = zlib.decompressobj(-15).decompress(raw, self.block)
        if len(self.cache) > 64:
            self.cache.clear()
        self.cache[i] = data
        return data

    def read(self, offset, size):
        if not self.compressed:
            self.f.seek(offset)
            return self.f.read(size)
        out = bytearray()
        while size > 0:
            i, off = divmod(offset, self.block)
            chunk = self._block(i)[off:off + size]
            if not chunk:
                break
            out += chunk
            offset += len(chunk)
            size -= len(chunk)
        return bytes(out)

    def find(self, path):
        """Returnerar (offset, storlek) för en fil, t.ex. 'PSP_GAME/ICON0.PNG'."""
        pvd = self.read(16 * SECTOR, 256)
        if pvd[1:6] != b"CD001":
            return None
        lba, size = struct.unpack_from("<I", pvd, 156 + 2)[0], struct.unpack_from("<I", pvd, 156 + 10)[0]
        for part in path.upper().split("/"):
            found = None
            data = self.read(lba * SECTOR, size)
            for sec in range(0, len(data), SECTOR):
                i = sec
                while i < sec + SECTOR and i < len(data):
                    rec_len = data[i]
                    if rec_len == 0:
                        break
                    name_len = data[i + 32]
                    name = data[i + 33:i + 33 + name_len].decode("ascii", "replace").split(";")[0]
                    if name.upper() == part:
                        found = struct.unpack_from("<I", data, i + 2)[0], struct.unpack_from("<I", data, i + 10)[0]
                        break
                    i += rec_len
                if found:
                    break
            if not found:
                return None
            lba, size = found
        return lba * SECTOR, size

    def load(self, path):
        hit = self.find(path)
        return self.read(*hit) if hit and hit[1] else None


def sfo_strings(data):
    """PARAM.SFO → dict med strängvärden."""
    if not data or data[:4] != b"\0PSF":
        return {}
    _, _, key_start, data_start, count = struct.unpack_from("<4sIIII", data)
    out = {}
    for n in range(count):
        koff, fmt, length, _, doff = struct.unpack_from("<HHIII", data, 20 + n * 16)
        key = data[key_start + koff:data.index(b"\0", key_start + koff)].decode("ascii", "replace")
        if fmt == 0x0204:
            out[key] = data[data_start + doff:data_start + doff + length].split(b"\0")[0].decode("utf-8", "replace")
    return out


# --- spellistan (samma regler som src/scan.c) ---

class Game:
    def __init__(self, key, title, kind, icon=None, pic=None):
        self.key, self.title, self.kind, self.icon, self.pic = key, title, kind, icon, pic


def scan_isos(root):
    iso_dir = root / "ISO"
    if not iso_dir.is_dir():
        return
    files = [p for p in iso_dir.iterdir() if p.is_file()]
    files += [p for d in iso_dir.iterdir() if d.is_dir() for p in d.iterdir() if p.is_file()]
    for path in sorted(files):
        if path.suffix.lower() not in (".iso", ".cso", ".zso"):
            continue
        try:
            r = IsoReader(path)
            try:
                sfo = sfo_strings(r.load("PSP_GAME/PARAM.SFO"))
                if not sfo:
                    continue  # inget PSP-spel – launchern visar det inte heller
                key = sfo.get("DISC_ID") or path.stem
                yield Game(key, sfo.get("TITLE", path.stem), "ISO",
                           r.load("PSP_GAME/ICON0.PNG"), r.load("PSP_GAME/PIC1.PNG"))
            finally:
                r.close()
        except (OSError, struct.error, zlib.error, lz4.block.LZ4BlockError) as e:
            print(f"  hoppar över {path.name}: {e}")


def scan_eboots(root):
    game_dir = root / "PSP" / "GAME"
    if not game_dir.is_dir():
        return
    for folder in sorted(p for p in game_dir.iterdir() if p.is_dir()):
        eboot = folder / "EBOOT.PBP"
        if folder.name.upper() in ("PSP5", "NOVA") or not eboot.is_file():
            continue
        with open(eboot, "rb") as f:
            head = f.read(40)
            if len(head) < 40 or head[:4] != b"\0PBP":
                continue
            off = struct.unpack_from("<8I", head, 8)

            def part(a, b):
                if off[b] <= off[a] or off[b] - off[a] > 4 * 1024 * 1024:
                    return None
                f.seek(off[a])
                return f.read(off[b] - off[a])

            sfo = sfo_strings(part(0, 1))
            icon, pic = part(1, 2), part(4, 5)
        yield Game(folder.name, sfo.get("TITLE", folder.name), "EBOOT", icon, pic)


# --- bildbehandling (samma resultat som src/image.c) ---

def open_png(data):
    try:
        return Image.open(io.BytesIO(data)).convert("RGBA") if data else None
    except OSError:
        return None


def cover_crop(img, w, h):
    """Beskär mitten till rätt proportioner och skalar till w×h."""
    sw, sh = img.size
    target = w / h
    if sw / sh > target:
        nw = round(sh * target)
        img = img.crop(((sw - nw) // 2, 0, (sw - nw) // 2 + nw, sh))
    else:
        nh = round(sw / target)
        img = img.crop((0, (sh - nh) // 2, sw, (sh - nh) // 2 + nh))
    return img.resize((w, h), Image.LANCZOS)


def round_corners(img, radius):
    scale = 4
    mask = Image.new("L", (img.width * scale, img.height * scale), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, mask.width - 1, mask.height - 1), radius * scale, fill=255)
    mask = mask.resize(img.size, Image.LANCZOS)
    alpha = Image.composite(img.getchannel("A"), Image.new("L", img.size, 0), mask)
    img.putalpha(alpha)
    return img


def ntx(img, psm):
    w, h = img.size
    stride = (w + 7) & ~7
    if psm == PSM_8888:
        rows = img.convert("RGBA").tobytes()
        bpp, pad = 4, b"\0" * ((stride - w) * 4)
        body = b"".join(rows[y * w * 4:(y + 1) * w * 4] + pad for y in range(h))
    else:
        px = img.convert("RGB").load()
        out = bytearray()
        for y in range(h):
            for x in range(stride):
                r, g, b = px[min(x, w - 1), y]
                out += struct.pack("<H", (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11))
        body = bytes(out)
    return struct.pack("<IHHHHI", NTX_MAGIC, w, h, psm, stride, 0) + body


# --- egna bilder ---

def norm(s):
    return re.sub(r"[^0-9a-z]", "", s.lower())


def index_sources(src):
    """normaliserat namn → sökväg, för alla bilder i src."""
    table = {}
    if src.is_dir():
        for p in src.rglob("*"):
            if p.suffix.lower() in IMAGE_EXTS:
                table[norm(p.stem)] = p
    return table


def find_source(table, game, suffix=""):
    for name in (game.key, game.title):
        hit = table.get(norm(name + suffix))
        if hit:
            return hit
    return None


# --- huvudprogram ---

def main():
    ap = argparse.ArgumentParser(description="Förbereder omslagsbilder för psp5.")
    ap.add_argument("root", type=Path, help="minneskortets rot, t.ex. E:\\")
    ap.add_argument("--src", type=Path, default=Path(__file__).resolve().parent / "covers-src",
                    help="mapp med egna omslag (standard: tools/covers-src)")
    ap.add_argument("--force", action="store_true", help="skriv om befintliga omslag")
    ap.add_argument("--clean", action="store_true", help="ta bort omslag för spel som inte finns kvar")
    ap.add_argument("--dry-run", action="store_true", help="visa bara vad som skulle göras")
    args = ap.parse_args()
    sys.stdout.reconfigure(errors="replace")  # t.ex. japanska titlar i en Windows-konsol

    root = args.root
    if not (root / "PSP").is_dir():
        sys.exit(f"Hittar ingen PSP-mapp i {root} – är det rätt enhet?")
    out_dir = root / "PSP5" / "covers"
    sources = index_sources(args.src)
    print(f"Minneskort: {root}   egna bilder: {len(sources)} i {args.src}")

    games = list(scan_isos(root)) + list(scan_eboots(root))
    written = skipped = 0
    keys = set()
    for g in games:
        keys.add(g.key)
        ico_path, bg_path = out_dir / f"{g.key}.ico", out_dir / f"{g.key}.bg"
        notes = []

        if args.force or not ico_path.exists():
            own = find_source(sources, g)
            img = Image.open(own).convert("RGBA") if own else open_png(g.icon)
            if img:
                img = round_corners(cover_crop(img, ICON_W, ICON_H), ICON_RADIUS)
                notes.append("kort: " + ("eget " + own.name if own else "ICON0"))
                if not args.dry_run:
                    out_dir.mkdir(parents=True, exist_ok=True)
                    ico_path.write_bytes(ntx(img, PSM_8888))
                written += 1
        else:
            skipped += 1

        if args.force or not bg_path.exists():
            own = find_source(sources, g, "_bg")
            if own:
                img, blur, what = Image.open(own), 0, "eget " + own.name
            elif open_png(g.pic):
                img, blur, what = open_png(g.pic), 0, "PIC1"
            elif open_png(g.icon):
                img, blur, what = open_png(g.icon), 10, "suddig ICON0"
            else:
                img = None
            if img:
                img = cover_crop(img.convert("RGB"), BG_W, BG_H)
                if blur:
                    img = img.filter(ImageFilter.GaussianBlur(blur))
                notes.append("bakgrund: " + what)
                if not args.dry_run:
                    out_dir.mkdir(parents=True, exist_ok=True)
                    bg_path.write_bytes(ntx(img, PSM_5650))
                written += 1
        else:
            skipped += 1

        if notes:
            status = ", ".join(notes)
        elif ico_path.exists() or bg_path.exists():
            status = "redan klart"
        else:
            status = "ingen bild (launchern visar ett kort med initial)"
        print(f"  [{g.kind:<5}] {g.key:<15} {g.title[:40]:<40} {status}")

    removed = 0
    if args.clean and out_dir.is_dir():
        for p in out_dir.iterdir():
            if p.suffix in (".ico", ".bg") and p.stem not in keys:
                print(f"  tar bort {p.name} (spelet finns inte kvar)")
                if not args.dry_run:
                    p.unlink()
                removed += 1

    print(f"\n{len(games)} spel/appar, {written} bilder skrivna, {skipped} fanns redan"
          + (f", {removed} borttagna" if args.clean else "")
          + (" (torrkörning – inget sparat)" if args.dry_run else ""))


if __name__ == "__main__":
    main()
