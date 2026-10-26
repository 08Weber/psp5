#!/usr/bin/env python3
# psp5 – made by 08Weber
"""Ritar psp5:s egen ikon (ICON0.PNG 144×80) och XMB-bakgrund (PIC1.PNG 480×272) till res/.
Helt egen grafik: mörkblå yta, mjuka ljusvågor och ordet "psp5" i tunn stil.
    py tools\\make_icon.py
"""
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

RES = Path(__file__).resolve().parent.parent / "res"
FONTS = [
    r"C:\Windows\Fonts\segoeuisl.ttf",  # Segoe UI Semilight – läsbar även i 144×80
    r"C:\Windows\Fonts\segoeuil.ttf",
    r"C:\Windows\Fonts\segoeui.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]
TOP, BOTTOM = (16, 30, 72), (4, 5, 12)


def font(size):
    for p in FONTS:
        if Path(p).exists():
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def backdrop(w, h):
    """Diagonal gradient från djupblått uppe till vänster till nästan svart."""
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            t = min(1.0, (x / w) * 0.45 + (y / h) * 0.75)
            px[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(TOP, BOTTOM))
    return img


def waves(w, h, count, amp, width, alpha):
    """Mjuka, böljande ljuslinjer som korsar bilden."""
    layer = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(layer)
    for i in range(count):
        phase = i * 0.55
        base = h * (0.42 + i * 0.07)
        pts = []
        for x in range(-10, w + 11, max(2, w // 120)):
            u = x / w
            y = base + math.sin(u * math.pi * 1.6 + phase) * amp * (0.6 + 0.4 * u) \
                - u * h * 0.18 + math.sin(u * math.pi * 4.2 + phase * 2) * amp * 0.15
            pts.append((x, y))
        d.line(pts, fill=int(alpha * (1 - i / (count + 1))), width=width)
    return layer


def compose(w, h, scale, with_text):
    W, H = w * scale, h * scale
    img = backdrop(W, H).convert("RGBA")

    # ljusvågor: en suddig glöd + en skarpare kärna
    core = waves(W, H, 5, H * 0.16, max(1, scale), 150)
    glow = core.filter(ImageFilter.GaussianBlur(5 * scale))
    light = ImageChops.add(glow, glow)
    for mask, color in ((light, (90, 150, 255)), (core, (215, 230, 255))):
        img = Image.alpha_composite(img, Image.merge("RGBA", (*Image.new("RGB", (W, H), color).split(), mask)))

    if with_text:
        f = font(int(H * 0.42))
        d = ImageDraw.Draw(img)
        # svag glöd bakom texten
        halo = Image.new("L", (W, H), 0)
        ImageDraw.Draw(halo).text((W / 2, H / 2), "psp5", font=f, fill=150, anchor="mm")
        halo = halo.filter(ImageFilter.GaussianBlur(4 * scale))
        img = Image.alpha_composite(img, Image.merge("RGBA", (*Image.new("RGB", (W, H), (120, 170, 255)).split(), halo)))
        d = ImageDraw.Draw(img)
        d.text((W / 2, H / 2), "psp5", font=f, fill=(245, 248, 255, 255), anchor="mm")
    return img.resize((w, h), Image.LANCZOS)


def icon():
    img = compose(144, 80, 4, True)
    mask = Image.new("L", (144 * 4, 80 * 4), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, 144 * 4 - 1, 80 * 4 - 1), 9 * 4, fill=255)
    img.putalpha(mask.resize((144, 80), Image.LANCZOS))
    img.save(RES / "ICON0.PNG", optimize=True)


def pic1():
    compose(480, 272, 2, False).convert("RGB").save(RES / "PIC1.PNG", optimize=True)


if __name__ == "__main__":
    RES.mkdir(exist_ok=True)
    icon()
    pic1()
    print("res/ICON0.PNG, res/PIC1.PNG")
