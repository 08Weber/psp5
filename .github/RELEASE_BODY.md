**PSP5 REMAKE __VERSION__** (fork by **medhatk**, built on [08Weber/psp5](https://github.com/08Weber/psp5)) brings improved game icon & PMF card-preview fitting, the restored procedural UI sound engine, Standby Screen rendering & brightness normalization, and the full **PS5 UI overhaul**. Your existing settings, covers, pins and music stay when you update from psp5 or v1.0.0.

> [!NOTE]
> **Based on psp5 by 08Weber**
> This release is the **PSP5 REMAKE** fork by **medhatk** at [github.com/medhatk/PSP5-REMAKE](https://github.com/medhatk/PSP5-REMAKE), built on top of 08Weber's original [psp5](https://github.com/08Weber/psp5) foundation (`v2.1.3`, GPL-3.0).

## Which file do I download?

Not sure which ARK you have? Press **SELECT** in the XMB: ARK's menu shows *ARK 4.x* or *ARK 5.x*.

| Your PSP | Download this |
|---|---|
| PSP-1000, 2000, 3000 or E1000 Street with **ARK-5** | **`psp5-remake-v__VERSION__-ARK5.zip`** |
| PSP-1000, 2000, 3000 or E1000 Street with **ARK-4** | **`psp5-remake-v__VERSION__-ARK4.zip`** |
| **PSP Go** with **ARK-5** | **`psp5-remake-v__VERSION__-PSPGo-ARK5.zip`** |
| **PSP Go** with **ARK-4** | **`psp5-remake-v__VERSION__-PSPGo-ARK4.zip`** |

## What's new in __VERSION__

**Standby Screen Rendering & Brightness Fix**
- Removed Standby-specific automatic brightness step-down/restore so your chosen system brightness is always preserved.
- Restored continuous normal vblank rendering in `standby_mode()` (removing the 10 FPS / 100ms idle throttling), fixing intermittent black-screen transitions while preserving the theme/wallpaper clock, Now Playing card, Power menu, and `HOME` unlock.

**Game Icon Fitting & PMF Card Previews**
- Automatically trims transparent and symmetric black letterbox/pillarbox margins on game icons (`src/art.c`), cover-fits wide icons to the 16:9 card, and centers narrow/square icons cleanly (`icons3` cache refresh).
- Aspect-crops and rounded-corner-masks `ICON1.PMF` video previews (`src/preview.c`, `src/gfx.c`) so animated previews align seamlessly inside rounded game cards.

**Procedural UI Sound Engine Restored**
- Restored the original lightweight procedural synthesized UI sound engine (`src/sound.c`), removing the large embedded FLAC/zlib sound blob for a cleaner, smaller EBOOT.

**Full PSP5 REMAKE Features Included**
- Custom **PSP5™** boot logo & **Welcome Back** startup backdrop, unified PS5 top navigation bar & outline icons, dedicated **Apps** tab, **Now playing** mini-card in Control Center, and **Standby Screen** with **△** Sleep / Shut down power overlay.

## Thank you

A huge thank you to **[08Weber](https://github.com/08Weber/psp5)** for creating psp5, as well as the **ARK-5**, **ARK-4**, and **pspdev** teams whose CFW and toolchain power this project.

## Install or update

Unzip the file for your PSP and copy the `PSP` folder over the old one: to the memory stick on a PSP-1000/2000/3000/E1000, or to the internal storage on a PSP Go. Your settings and covers stay. If you use PSP5 REMAKE as ARK's launcher, also copy `ARK_LAUNCHER/VBOOT.PBP` into `PSP/SAVEDATA/ARK_01234/` again. The full guide is in [INSTALL.md](https://github.com/medhatk/PSP5-REMAKE/blob/main/INSTALL.md).

## If your PSP gets stuck

Hold **SELECT** while turning on the PSP. ARK then skips its settings and starts the normal XMB.
