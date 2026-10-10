# Installing psp5

*psp5 – made by Mille Weber*

You need a **PSP with ARK-5** and a **USB cable**. That's it.

## Install (5 minutes)

1. **Download** the newest `psp5-*.zip` from [Releases](../../releases) and unzip it.
2. **Connect your PSP:** on the PSP, go to *Settings → USB Connection* and plug in the cable.
   The PSP shows up on your computer as a drive (for example `E:`).
3. **Copy the `PSP` folder** from the zip onto the PSP drive. If your computer asks, say yes to merging folders.
4. **Unplug** (eject the drive first) and press ○ on the PSP to leave USB mode.
5. **Start psp5** from *Game → Memory Stick*. Done!

The first start takes a moment while psp5 finds your games. After that it opens fast.

## Where your games go

| Game type | Put it here |
|---|---|
| PSP games (`.iso`, `.cso`, `.zso`) | the `ISO` folder on the PSP |
| Homebrew, PSN and PS1 games | `PSP/GAME/<game folder>/` |

## Buttons

| Button | Does |
|---|---|
| ✕ or START | Start the game |
| L / R | Switch between Games and Apps |
| △ | Settings |
| HOME | Back to the XMB |

## Optional: replace ARK's menu with psp5

Do this if you want ARK's *Custom Launcher* to open psp5, or want the PSP to start straight into psp5.

1. Connect the PSP with USB again and open the folder `PSP/SAVEDATA/ARK_01234/`.
2. Rename the `VBOOT.PBP` that's already there to `VBOOT_ARKMENU.PBP` (this keeps ARK's own menu as a backup).
3. Copy `VBOOT.PBP` from the zip's `ARK_LAUNCHER` folder into the same place.
4. Unplug. *Custom Launcher* in the XMB now opens psp5.

**Start the PSP straight into psp5:** press △ → *psp5* → turn on *Boot into psp5*, then restart the PSP.

**Going to the XMB:** △ → *Power and USB* → *Exit to XMB*. This pauses *Boot into psp5* until you
open psp5 again from *Game → Memory Stick*. Then it turns back on.

## Updating

Copy the new files over the old ones, the same way you installed them:

- `PSP/GAME/PSP5/EBOOT.PBP`
- `PSP/SAVEDATA/ARK_01234/VBOOT.PBP` (only if you did the optional step)

Your settings and covers stay.

## Help!

- **Stuck in psp5 or something is broken?** Hold **SELECT** while turning on the PSP. You'll land in the normal XMB.
- **Want ARK's own menu back?** In `PSP/SAVEDATA/ARK_01234/`, delete `VBOOT.PBP` and rename
  `VBOOT_ARKMENU.PBP` back to `VBOOT.PBP`.
- **A game is missing?** Check that it's in the `ISO` folder (sub-folders are fine, but only one level deep).
- **Wrong or missing pictures?** △ → *psp5* → *Clear image cache*.
- **Settings didn't change anything?** CPU speed, plugins and other ARK settings need a restart.

## Uninstall

1. If you did the optional step: put ARK's menu back (see *Help!* above).
2. Delete the folders `PSP/GAME/PSP5` and `PSP5` from the PSP.

## Extra: custom covers (for advanced users)

psp5 uses each game's own pictures by itself, so you don't need this. If you want your own covers,
put pictures in `tools/covers-src/` (named like `ULES00502.jpg`, or `ULES00502_bg.jpg` for a background)
and run this with the PSP connected (needs Python 3):

```sh
py -m pip install --user pillow lz4
py tools/covers.py E:\
```
