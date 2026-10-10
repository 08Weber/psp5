# Installing psp5

psp5 can be installed in two ways:

- **A – as a normal app**: it shows up in the XMB under *Game → Memory Stick*. Nothing else changes.
- **B – as ARK-5's launcher**: it replaces ARK's own launcher (arkMenu). ARK's *Custom Launcher* entry
  starts psp5, and if you want, the PSP can boot straight into psp5 instead of the XMB.

You can do A first and B later. Both use the same file.

## Requirements

- A PSP (made and tested on a PSP-2000) with **ARK-5** custom firmware installed.
  PSP Go and PS Vita are not supported.
- A USB cable, or another way to copy files to the memory stick.
- Optional, for covers: a PC with Python 3.

## 1. Download

Download the latest `psp5-*.zip` from the [Releases](../../releases) page and unzip it.
It contains:

```
PSP/
  GAME/
    PSP5/
      EBOOT.PBP
```

## 2. Copy to the PSP

1. On the PSP, go to *Settings → USB Connection* and connect the cable. The memory stick appears as a drive
   on your computer (for example `E:`).
2. Copy the `PSP` folder from the zip to the **root** of the memory stick, so that you get
   `E:\PSP\GAME\PSP5\EBOOT.PBP`. Let it merge with the existing `PSP` folder.

### Where your games should be

| What | Where |
|---|---|
| PSP games as `.iso`, `.cso` or `.zso` | `ms0:/ISO/` (sub-folders one level deep are fine, e.g. `ISO/Racing/`) |
| Homebrew, PSN games, PS1 games (`EBOOT.PBP`) | `ms0:/PSP/GAME/<folder>/EBOOT.PBP` |

## 3. Start it (option A)

Eject the memory stick safely on the computer, leave USB mode, and start **psp5** from
*Game → Memory Stick*.

The first start shows a short loading screen while psp5 reads all your games. After that the list is
cached and psp5 starts quickly. psp5 creates the folder `ms0:/PSP5/` for its cache and covers.

## 4. Use psp5 as ARK-5's launcher (option B, optional)

ARK-5 starts its launcher from `ms0:/PSP/SAVEDATA/ARK_01234/VBOOT.PBP`
(`ARK_01234` is the default name of the ARK folder – use the name your ARK folder has).

1. Connect the PSP over USB.
2. In `PSP/SAVEDATA/ARK_01234/`, **rename** `VBOOT.PBP` to `VBOOT_ARKMENU.PBP`.
   This keeps ARK's original launcher so you can go back at any time.
3. Copy psp5's `EBOOT.PBP` into the same folder and rename the copy to `VBOOT.PBP`.
4. Eject and leave USB mode. ARK's *Custom Launcher* item in the XMB now opens psp5.

**Boot straight into psp5:** in psp5 press **△** → *psp5* → turn on *Starta psp5 vid uppstart*
("boot into psp5"), go back with ○ and restart the PSP. Games you quit with HOME now return to psp5.

> An ARK update may replace `VBOOT.PBP` with arkMenu again. Just copy psp5's `EBOOT.PBP` back as `VBOOT.PBP`.

## 5. Covers (optional)

psp5 uses the games' own icon and background automatically. To prepare all images in advance – or use your
own covers – run the cover tool on your computer while the PSP is connected over USB:

```sh
py -m pip install --user pillow lz4      # once
py tools/covers.py E:\                   # E: = the PSP
```

Your own pictures go in `tools/covers-src/`, named after the game ID or title
(`ULES00502.jpg`, `LocoRoco.png`). Add `_bg` for a background picture (`ULES00502_bg.jpg`).
Images are cropped automatically. Options: `--force` (redo everything), `--clean` (remove covers of games
you no longer have), `--dry-run`.

The images are written to `ms0:/PSP5/covers/`.

## Controls

| Button | Action |
|---|---|
| ← → / analog stick | move between games |
| L / R | switch between *Spel* (games) and *Appar* (apps) |
| ✕ or START | launch the selected game |
| △ | settings (*Inställningar*) |
| HOME | quit to the XMB |

The menus are in Swedish. A few translations for the settings screen:

| Swedish | English |
|---|---|
| Prestanda | Performance |
| Ström och USB | Power and USB |
| Starta om / Stäng av / Viloläge | Restart / Shut down / Sleep |
| Gå till XMB | Exit to XMB |
| Återställningsmeny | Recovery menu |
| Rensa bildcache | Clear image cache |

CFW settings changed in psp5 are saved to ARK's `SETTINGS.TXT` and apply **after a restart**.

## If something goes wrong

- **Hold SELECT while the PSP boots** – ARK skips its settings, so the PSP starts in the normal XMB
  even if *boot into psp5* is on.
- **Hold START while the PSP boots** – ARK skips all plugins.
- **Go back to arkMenu:** in `PSP/SAVEDATA/ARK_01234/`, delete `VBOOT.PBP` and rename
  `VBOOT_ARKMENU.PBP` back to `VBOOT.PBP`.
- **Wrong or missing pictures:** psp5 → △ → *psp5* → *Rensa bildcache*, or delete `ms0:/PSP5/cache/`.
- **A game is missing:** check that it is in `ms0:/ISO` (at most one sub-folder deep) and that the file
  is a real PSP game (psp5 hides files without `PSP_GAME/PARAM.SFO`).

## Uninstall

1. If you used option B: restore `VBOOT_ARKMENU.PBP` as `VBOOT.PBP` (see above) and turn off
   *boot into psp5* first (or hold SELECT when booting).
2. Delete `ms0:/PSP/GAME/PSP5/` and `ms0:/PSP5/`.
