**PSP5 REMAKE __VERSION__** (fork by **medhatk**, built on [08Weber/psp5](https://github.com/08Weber/psp5)) introduces a new **Animated PSP Startup Screen**, **30th Anniversary Dark** theme, major **Performance & RAM Optimizations**, alongside the built-in hardware-accelerated **MP4 Video Player**, unified **Media Hub** (Music, Pictures, Videos), **Single-Game Categories Support**, **File Manager Conflict Resolution & Deletion Overhaul**, **Game / App Information**, and **Saved Data Manager**. Your existing settings, covers, pins and music stay intact when updating from psp5 or earlier PSP5 REMAKE releases.

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

You can also download the standalone `EBOOT.PBP` files directly from the release assets below.

---

## What's new in v__VERSION__

### 🎬 New Features

- **Unified Animated PSP Startup Screen (`src/boot.c`, `src/psp_boot_logo.h`)**:
  - Replaced the previous 3-stage boot sequence (old logo, health warning, and Welcome Back screen) with a single unified startup animation.
  - Features a crisp white **PSP** wordmark over an animated, swirling cyan-to-cobalt blue dust wave and floating bokeh sparks on black (skippable with any button).
- **30th Anniversary Dark Theme (`src/theme.c`, `src/anniv_dark_bg.h`)**:
  - Added a dark variant of the PlayStation 30th Anniversary theme with matte charcoal surfaces, turquoise LED accents, and a dedicated dark backdrop.
- **Built-in MP4 Video Player (`src/video/*`, `src/ui.c`)**:
  - Direct hardware-accelerated MP4 playback supporting H.264 (AVC) decoding via the PSP's Media Engine (ME) and AAC audio.
  - Smooth RGB565 rendering directly to display buffers.
  - Intelligent resume playback memory (prompts to continue from where you left off or start from the beginning).
  - Quick 10-second forward/backward seeking with `←` / `→`.
  - Automatic audio lease coordination: safely pauses background music upon video launch and restores audio on exit.
- **Unified Media Hub Tab (`src/ui.c`, `src/music.c`)**:
  - Added a dedicated **Media** hub tab housing **Music**, **Pictures**, and **Videos** cards accessible directly from the top navigation bar.
- **Folder-Based Categories & Single-Game Category Support (`src/scan.c`, `src/ui.c`)**:
  - Categorization strictly follows actual folder names (`ISO/<category>/<game>` and `PSP/GAME/<category>/<game>/EBOOT.PBP`) across `ms0:` and `ef0:`.
  - Removed the legacy restriction that hid categories containing only a single game. Now every folder (e.g. `ISO/Racing/`) appears as a full category.
  - Full support for `g_` (games) and `s_` (apps) folder prefixes, plus `CAT_` (from Game Categories plugins). Prefixes are neatly trimmed from display labels.
  - **Games** and **Library** tabs maintain their active category filter and cursor position completely independently.
- **Game / App Information Screen (`src/info.c`, `src/info.h`)**:
  - Accessible from options menu (`□` → Information) on any game or app card.
  - Displays DISC ID, DISC_VERSION, APP_VER, PSP_SYSTEM_VER from `PARAM.SFO`, background directory size measurement, free card space, and last played timestamp.
- **Saved Data Manager (`src/savedata.c`, `src/savedata.h`)**:
  - Accessible from options menu (`□` → Saved Data) filtered by game ID, or view all savedata on the storage.
  - Inspect save timestamps, sizes, titles, and delete saves with safety confirmation.
- **Smart Conflict Resolution in File Manager (`src/files.c`)**:
  - When copying or moving files/directories to an existing destination, an interactive dialog offers:
    - **Replace**: Safe atomic overwrite with backup rollback.
    - **Merge**: Directory tree merging.
    - **Skip**: Skip conflicting items.
    - **Auto-rename**: Automatically append `(2)`, `(3)`, etc.
    - **Apply to all**: Set action for all remaining conflicts in the operation.

### ⚡ Improvements & Fixes

- **Performance & RAM Optimizations (`src/boot.c`, `src/theme.c`, `src/music.c`, `src/ui.c`, `src/osd.c`, `src/preview.c`, `src/video/videoplay.c`, `Makefile`)**:
  - **Lower RAM Footprint**: Switched startup and 30th Anniversary theme backdrops from `GU_PSM_8888` to `GU_PSM_5650` (~262 KB saved per backdrop), freed unused theme/boot/UI icon textures on switch and teardown, fixed media list leaks (`s_pics`/`s_vids`) in `media_free()`, terminated the idle video loader thread (`64 KB` stack) on player close, merged MP3 and AAC decoder buffers into a shared `union` (~50 KB `.bss` saved), reduced bold font cache size, and enabled `-ffunction-sections -fdata-sections` + `--gc-sections`.
  - **Smoother Frame Times & Reduced I/O**: Disabled synchronous `preview.log` Memory Stick writes in release builds, throttled `kuKernelCall` volume polling (`osd.c`) and `scePower*`/`sceRtc*` top-bar syscalls, precomputed static PlayStation watermark geometry, and cached ellipsized labels, tab widths, library tile titles, and search results.
- **File Manager Deletion & Move Hardening**:
  - Clears read-only file/directory attributes before deletion/removal to prevent permission errors on stubborn files.
  - Accurate progress bar accounting for byte transfers and item counts during copy/move/delete operations.
- **12/24-Hour Time Format Integration (`src/datetime.c`, `src/ui.c`)**:
  - Configurable in *Settings → System → Date and time* (12-hour with AM/PM or 24-hour).
  - Consistently formatted across the top navigation bar clock and the large Standby Screen clock without touching hardware RTC.
- **Top Navigation Bar Flow**:
  - Direct `L` / `R` cycling across **Games**, **Apps**, **Library**, and **Media**, with persistent Now Playing music pill and battery/clock readout.
- **Control Center & Standby Screen**:
  - Includes Now Playing mini-card, battery health, and Triangle Power menu for Sleep or double-confirm Shut down.

### 📌 Compatibility Notes & Known Limitations

- **MP4 Video Player Constraints**: Supports standard MP4 container files with H.264 video and AAC audio at PSP-native resolutions (e.g. 480×272). High-profile/unsupported resolutions or non-AAC audio codecs are not supported.
- **PMF Videos**: Standalone PMF video playback is not part of the integrated video player. PMF remains used exclusively for animated game icon card previews (`ICON1.PMF`).
- **UMD Video ISO Behavior**: Selecting a UMD Video ISO in `ISO/VIDEO` prompts for confirmation, mounts the ISO via ARK, and cleanly redirects to the XMB, where the user starts movie playback from the XMB Video column.

---

## Thank you

A huge thank you to **[08Weber](https://github.com/08Weber/psp5)** for creating psp5, as well as the **ARK-5**, **ARK-4**, and **pspdev** teams whose CFW and toolchain power this project.

## Install or update

Unzip the file for your PSP and copy the `PSP` folder over the old one: to the memory stick on a PSP-1000/2000/3000/E1000, or to the internal storage on a PSP Go. Your settings and covers stay. If you use PSP5 REMAKE as ARK's launcher, also copy `ARK_LAUNCHER/VBOOT.PBP` into `PSP/SAVEDATA/ARK_01234/` again. The full guide is in [INSTALL.md](https://github.com/medhatk/PSP5-REMAKE/blob/main/INSTALL.md).

## If your PSP gets stuck

Hold **SELECT** while turning on the PSP. ARK then skips its settings and starts the normal XMB.
