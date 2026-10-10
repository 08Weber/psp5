# Installing psp5

*psp5 – made by Mille Weber*

There are two ways to use psp5, and you can start with the first and switch to the second later –
it's the same file either way.

- **As a normal app.** psp5 shows up in the XMB under *Game → Memory Stick*. Nothing else on your PSP changes.
- **As ARK-5's launcher.** psp5 replaces ARK's own menu (arkMenu). ARK's *Custom Launcher* item opens psp5,
  and if you like, the PSP can boot straight into psp5 instead of the XMB.

## What you need

- A PSP with the **ARK-5** custom firmware. I made psp5 on a PSP-2000; the PSP Go and PS Vita aren't supported.
- A USB cable (or another way to copy files to the memory stick).
- For the optional cover tool: a computer with Python 3.

## 1. Download

Download the newest `psp5-*.zip` from the [Releases](../../releases) page and unzip it. You'll get:

```
PSP/
  GAME/
    PSP5/
      EBOOT.PBP       ← psp5 as a normal app
ARK_LAUNCHER/
  VBOOT.PBP           ← the same psp5, already named for ARK's launcher (only needed for step 4)
```

Both files are the same program – there's nothing to rename.

## 2. Copy it to your PSP

1. On the PSP, go to *Settings → USB Connection* and plug in the cable. The memory stick shows up as a drive
   on your computer, for example `E:`.
2. Copy the `PSP` folder from the zip to the **root** of the memory stick and let it merge with the `PSP`
   folder that's already there. You should end up with `E:\PSP\GAME\PSP5\EBOOT.PBP`.

While you're there, check that your games are where psp5 looks for them:

| What | Where it goes |
|---|---|
| PSP games as `.iso`, `.cso` or `.zso` | `ms0:/ISO/` – one level of sub-folders is fine, e.g. `ISO/Racing/` |
| Homebrew, PSN games and PS1 games (`EBOOT.PBP`) | `ms0:/PSP/GAME/<folder>/EBOOT.PBP` |

## 3. Start it

Eject the memory stick safely on your computer, leave USB mode, and start **psp5** from *Game → Memory Stick*.

The very first time, psp5 spends a moment reading all your games. After that it remembers them and opens
quickly. It keeps its cache, covers and settings in a folder called `ms0:/PSP5/`.

psp5 picks its language from the PSP's system language. Want a different one – or Swedish, which the PSP
itself doesn't have? Press △ → *psp5* → *Language*.

## 4. Make psp5 your ARK launcher (optional)

ARK-5 starts its launcher from `ms0:/PSP/SAVEDATA/ARK_01234/VBOOT.PBP`. (`ARK_01234` is the usual name of
the ARK folder – if yours is called something else, use that.)

1. Connect the PSP over USB again.
2. In `PSP/SAVEDATA/ARK_01234/`, **rename** ARK's own `VBOOT.PBP` to `VBOOT_ARKMENU.PBP`. That's ARK's
   original menu, and keeping it means you can always switch back.
3. Copy `VBOOT.PBP` from the zip's `ARK_LAUNCHER` folder into `PSP/SAVEDATA/ARK_01234/`.
4. Eject, leave USB mode, and pick ARK's *Custom Launcher* in the XMB – it now opens psp5.

> The `VBOOT.PBP` is kept in its own folder in the zip on purpose, so that copying the `PSP` folder never
> overwrites ARK's menu by accident.

To have the PSP boot straight into psp5: press △ → *psp5* → turn on *Boot into psp5*, go back with ○ and
restart the PSP. From then on, quitting a game with HOME brings you back to psp5.

> If you update ARK, it may put its own `VBOOT.PBP` back. Just copy psp5's `VBOOT.PBP` in again.

## 5. Covers (optional)

psp5 uses each game's own icon and background on its own. If you want to prepare everything in advance or use
your own covers, run the cover tool on your computer while the PSP is connected:

```sh
py -m pip install --user pillow lz4      # only the first time
py tools/covers.py E:\                   # E: is your PSP
```

Put your own pictures in `tools/covers-src/`, named after the game ID or the title – `ULES00502.jpg` or
`LocoRoco.png` for the tile, and `ULES00502_bg.jpg` for a background. They're cropped to fit automatically.
Useful options: `--force` redoes everything, `--clean` removes covers for games you've deleted, and `--dry-run`
shows what would happen without changing anything.

## Updating

Copy the new `EBOOT.PBP` over the old one in `PSP/GAME/PSP5/`, and – if psp5 is your ARK launcher – the new
`VBOOT.PBP` over the one in `PSP/SAVEDATA/ARK_01234/`. Your settings, covers and cache in `ms0:/PSP5/` stay as
they are.

## A few things worth knowing

- Changes to ARK's settings (CPU speed, plugins and so on) are saved to ARK's own `SETTINGS.TXT` and
  `PLUGINS.TXT`, and take effect **after a restart**. Language and sounds change right away.
- psp5 hides files in `ms0:/ISO` that aren't real PSP games, so a broken download won't show up as a tile.

## If something goes wrong

- **Hold SELECT while the PSP boots.** ARK skips its settings, so you land in the normal XMB even if
  *Boot into psp5* is on.
- **Hold START while the PSP boots** to skip all plugins.
- **Want arkMenu back?** In `PSP/SAVEDATA/ARK_01234/`, delete `VBOOT.PBP` and rename `VBOOT_ARKMENU.PBP`
  back to `VBOOT.PBP`.
- **Pictures look wrong or are missing?** △ → *psp5* → *Clear image cache*, or delete `ms0:/PSP5/cache/`.
- **A game doesn't show up?** Make sure it's in `ms0:/ISO` (at most one sub-folder deep).

## Uninstalling

1. If psp5 is your ARK launcher, turn off *Boot into psp5* first (or hold SELECT when booting), then put
   `VBOOT_ARKMENU.PBP` back as `VBOOT.PBP`.
2. Delete `ms0:/PSP/GAME/PSP5/` and `ms0:/PSP5/`.
