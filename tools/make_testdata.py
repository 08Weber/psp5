#!/usr/bin/env python3
"""Tillverkar påhittade testspel (iso/cso/zso + EBOOT.PBP) för PPSSPP.

Körs i WSL (behöver genisoimage, pack-pbp, python3-pil, python3-lz4):
    wsl -d pspdev -- python3 /mnt/c/path/to/psp5/tools/make_testdata.py
Resultat: testdata/ms0/ISO och testdata/ms0/PSP/GAME
"""
import os
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

import lz4.block
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "testdata" / "ms0"
PACK_PBP = "/usr/local/pspdev/bin/pack-pbp"

# (fil, titel, disc_id, färg1, färg2)
ISOS = [
    ("ISO/Aurora Drift.iso", "Aurora Drift", "ULUS90001", (40, 90, 200), (150, 40, 180)),
    ("ISO/crimson_vault_v1.cso", "Crimson Vault", "ULES90002", (170, 30, 40), (40, 10, 20)),
    ("ISO/Neon Harbor.zso", "ネオン・ハーバー Neon Harbor", "ULJM90003", (0, 160, 150), (20, 30, 90)),
    ("ISO/Racing/Velocity Line.iso", "Velocity Line", "ULUS90004", (230, 140, 20), (90, 20, 10)),
]
# (mapp, titel, kategori, färg1, färg2 eller None = ingen ikon)
EBOOTS = [
    ("NOVATEST_HB", "Pixel Garden", "MG", (60, 170, 70), (10, 60, 40)),
    ("NOVATEST_PSN", "Starlit Run™ ® Ω", "EG", (90, 60, 200), (10, 10, 50)),
    ("NOVATEST_PS1", "Retro Quest", "ME", (120, 120, 130), (30, 30, 35)),
    ("NOVATEST_NOICON", "No Icon App", "MG", None, None),
]


def make_sfo(entries):
    """entries: dict nyckel -> str | int. Returnerar PARAM.SFO som bytes."""
    keys = sorted(entries)
    key_table = b""
    data_table = b""
    index = b""
    for k in keys:
        v = entries[k]
        if isinstance(v, int):
            fmt, data, used, max_len = 0x0404, struct.pack("<I", v), 4, 4
        else:
            raw = v.encode("utf-8") + b"\0"
            used, max_len = len(raw), max(len(raw) + 3 & ~3, 8)
            fmt, data = 0x0204, raw.ljust(max_len, b"\0")
        index += struct.pack("<HHIII", len(key_table), fmt, used, max_len, len(data_table))
        key_table += k.encode() + b"\0"
        data_table += data
    key_table = key_table.ljust(len(key_table) + 3 & ~3, b"\0")
    key_start = 20 + len(index)
    data_start = key_start + len(key_table)
    header = struct.pack("<4sIIII", b"\0PSF", 0x101, key_start, data_start, len(keys))
    return header + index + key_table + data_table


def font(size):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def gradient(w, h, c1, c2):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            t = (x / w * 0.6 + y / h * 0.4)
            px[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(c1, c2))
    return img


def make_art(title, c1, c2, w, h, text_size):
    img = gradient(w, h, c1, c2)
    d = ImageDraw.Draw(img)
    # några mjuka cirklar så att bilderna går att skilja åt
    for i in range(3):
        r = h // (2 + i)
        cx, cy = (w * (i + 1)) // 4, h // 2 + (i - 1) * h // 5
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=(255, 255, 255), width=max(1, h // 60))
    ascii_title = title.split(" ", 1)[-1] if not title.isascii() else title
    d.text((w // 14, h - text_size * 2), ascii_title, font=font(text_size), fill=(255, 255, 255))
    return img


def png_bytes(img):
    tmp = tempfile.NamedTemporaryFile(suffix=".png", delete=False)
    tmp.close()
    img.save(tmp.name, optimize=True)
    data = Path(tmp.name).read_bytes()
    os.unlink(tmp.name)
    return data


def compress_ciso(iso, magic, compress):
    """CSO (deflate) / ZSO (LZ4) version 1, blockstorlek 2048."""
    block = 2048
    n = (len(iso) + block - 1) // block
    header = struct.pack("<4sIQIBB2x", magic, 0x18, len(iso), block, 1, 0)
    pos = len(header) + (n + 1) * 4
    index, blocks = [], []
    for i in range(n):
        raw = iso[i * block:(i + 1) * block].ljust(block, b"\0")
        comp = compress(raw)
        if len(comp) >= block:
            index.append(pos | 0x80000000)
            comp = raw
        else:
            index.append(pos)
        blocks.append(comp)
        pos += len(comp)
    index.append(pos)
    return header + struct.pack("<%dI" % (n + 1), *index) + b"".join(blocks)


def deflate_raw(data):
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


def make_iso(rel, title, disc_id, c1, c2):
    with tempfile.TemporaryDirectory() as tmp:
        disc = Path(tmp) / "disc"
        game = disc / "PSP_GAME"
        (game / "SYSDIR").mkdir(parents=True)
        (game / "PARAM.SFO").write_bytes(make_sfo({
            "BOOTABLE": 1, "CATEGORY": "UG", "DISC_ID": disc_id, "DISC_NUMBER": 1,
            "DISC_TOTAL": 1, "DISC_VERSION": "1.00", "PARENTAL_LEVEL": 1,
            "PSP_SYSTEM_VER": "6.60", "REGION": 32768, "TITLE": title,
        }))
        (game / "ICON0.PNG").write_bytes(png_bytes(make_art(title, c1, c2, 144, 80, 12)))
        (game / "PIC1.PNG").write_bytes(png_bytes(make_art(title, c1, c2, 480, 272, 28)))
        (game / "SYSDIR" / "EBOOT.BIN").write_bytes(b"\0" * 4096)
        (disc / "UMD_DATA.BIN").write_bytes(f"{disc_id[:4]}-{disc_id[4:]}|0000000000000001|0001|G".encode())
        iso_path = Path(tmp) / "out.iso"
        subprocess.run(["genisoimage", "-quiet", "-iso-level", "2", "-V", disc_id,
                        "-o", str(iso_path), str(disc)], check=True)
        iso = iso_path.read_bytes()

    dest = OUT / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    if rel.endswith(".cso"):
        iso = compress_ciso(iso, b"CISO", deflate_raw)
    elif rel.endswith(".zso"):
        iso = compress_ciso(iso, b"ZISO", lambda d: lz4.block.compress(d, mode="high_compression", store_size=False))
    dest.write_bytes(iso)
    print(f"{rel}: {len(iso) // 1024} KB")


def make_eboot(folder, title, category, c1, c2):
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        (t / "PARAM.SFO").write_bytes(make_sfo({
            "BOOTABLE": 1, "CATEGORY": category, "DISC_ID": "NOVA" + folder[-5:].replace("_", "0"),
            "DISC_VERSION": "1.00", "PARENTAL_LEVEL": 1, "PSP_SYSTEM_VER": "6.60",
            "REGION": 32768, "TITLE": title,
        }))
        icon = pic1 = "NULL"
        if c1:
            (t / "ICON0.PNG").write_bytes(png_bytes(make_art(title, c1, c2, 144, 80, 12)))
            (t / "PIC1.PNG").write_bytes(png_bytes(make_art(title, c1, c2, 480, 272, 28)))
            icon, pic1 = str(t / "ICON0.PNG"), str(t / "PIC1.PNG")
        (t / "DATA.PSP").write_bytes(b"\0" * 1024)
        dest = OUT / "PSP" / "GAME" / folder
        dest.mkdir(parents=True, exist_ok=True)
        subprocess.run([PACK_PBP, str(dest / "EBOOT.PBP"), str(t / "PARAM.SFO"), icon, "NULL",
                        "NULL", pic1, "NULL", str(t / "DATA.PSP"), "NULL"],
                       check=True, stdout=subprocess.DEVNULL)
    print(f"PSP/GAME/{folder}/EBOOT.PBP")


def main():
    if OUT.exists():
        shutil.rmtree(OUT)
    for args in ISOS:
        make_iso(*args)
    for args in EBOOTS:
        make_eboot(*args)
    # trasig fil – launchern ska klara den utan att krascha
    (OUT / "ISO" / "Broken File.iso").write_bytes(os.urandom(8192))
    print("ISO/Broken File.iso")


if __name__ == "__main__":
    main()
