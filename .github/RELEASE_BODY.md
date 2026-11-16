**PSP5 REMAKE __VERSION__** (fork by **medhatk**, built on [08Weber/psp5](https://github.com/08Weber/psp5)) brings a refreshed **PS5 UI overhaul**, a dedicated **Apps** tab, compressed **PS5 UI sound effects**, custom **PSP5™** startup visuals, and an enhanced **Control Center & Standby Screen**. Your existing settings, covers, pins and music stay when you update from psp5.

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

**Custom PSP5™ Boot Logo & Welcome Back Screen**
- Uses the pre-rendered stacked **PSP5™** emblem & boot logo (`res/BOOT_SOURCE.png` → `src/logo.h`) and adjusts scanning bar spacing automatically.
- Adds a custom **Welcome Back** startup backdrop (`res/WELCOME.JPG` → `src/welcome.h`) at the end of the boot sequence.

**Unified Top Bar, Outline Icons & Apps Tab**
- Reworked the top navigation bar into a shared pill bar across every screen with custom pre-rendered outline icons (`src/icons.h`) cycling via **L / R** across **Games**, **Apps**, **Library**, **Music**, and **Settings**.
- Separated homebrew apps into their own dedicated **Apps** tab alongside **Games**, with hero card framing over a calm animated wave backdrop (`ui_draw_calm_backdrop` with PlayStation watermark) and a unified footer bar with format badges (`PSP`, `APP`, `PSN`, `PS1`, `UMD`) and Disc ID.
- Added item count and quick-action header pills (`[SELECT] Categories`, `[□] Sort`) in **Library**, plus outline category icons and contextual info callouts in **Settings**.

**PS5 Sound Effects**
- Embedded zlib-compressed 44.1 kHz 16-bit mono PCM PS5 UI sound assets (`src/ps5_sounds.h`), unpacked into memory at boot with automatic fallback to built-in synthesized sounds, plus dedicated Control Center open/close cues.

**Control Center & Standby Screen Enhancements**
- Added a **Now playing** mini-card directly inside the Control Center dock alongside quick actions.
- Updated the **Standby Screen** to match the active theme or custom user wallpaper, with a compact square **Now playing** music card (album art, track title/artist, progress bar, and playback controls) under the large clock and a **△** Sleep / Shut down power overlay.

**PSP5 REMAKE Branding**
- Updated the XMB EBOOT display title (`PSP5 REMAKE`), module identifier (`PSP5_REMAKE`), Settings category, localized launcher strings, and release archives (`psp5-remake-v__VERSION__-*.zip`).

## Thank you

A huge thank you to **[08Weber](https://github.com/08Weber/psp5)** for creating psp5, as well as the **ARK-5**, **ARK-4**, and **pspdev** teams whose CFW and toolchain power this project.

## Install or update

Unzip the file for your PSP and copy the `PSP` folder over the old one: to the memory stick on a PSP-1000/2000/3000/E1000, or to the internal storage on a PSP Go. Your settings and covers stay. If you use PSP5 REMAKE as ARK's launcher, also copy `ARK_LAUNCHER/VBOOT.PBP` into `PSP/SAVEDATA/ARK_01234/` again. The full guide is in [INSTALL.md](https://github.com/medhatk/PSP5-REMAKE/blob/main/INSTALL.md).

## If your PSP gets stuck

Hold **SELECT** while turning on the PSP. ARK then skips its settings and starts the normal XMB.
