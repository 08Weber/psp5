# Installing psp5

*psp5 – made by Mille Weber*

You need a **PSP-1000, PSP-2000, PSP-3000 or PSP Go with ARK-4 or ARK-5** and a **USB cable**. That's it.

## Which download?

Every release has four zips – pick the one for your PSP and its custom firmware:

| Your PSP runs | Download |
|---|---|
| **ARK-5** | `psp5-v1.3.4-ARK5.zip` |
| **ARK-4** | `psp5-v1.3.4-ARK4.zip` |
| **PSP Go with ARK-5** | `psp5-v1.3.4-PSPGo-ARK5.zip` |
| **PSP Go with ARK-4** | `psp5-v1.3.4-PSPGo-ARK4.zip` |

Not sure? Press **SELECT** in the XMB to open ARK's VSH menu – it shows the version (*ARK 4.x* or *ARK 5.x*).
The same zip works on the PSP-1000, 2000 and 3000; psp5 checks the model and the ARK version by itself
(so even the "wrong" zip will still start your games).

**PSP Go:** everything below is the same, but on the internal storage instead of the memory stick: copy the
`PSP` folder from the PSP Go zip onto the PSP Go, and start psp5 from *Game → System Storage*.
Games and music can be on the internal storage or on a Memory Stick Micro – psp5 looks in both. The PSP Go zips
hold the same psp5 as the other zips (psp5 notices the PSP Go by itself), just with PSP Go instructions.

## Install (5 minutes)

1. **Download** the zip for your ARK (see above) from [Releases](../../releases) and unzip it.
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
| Music (`.mp3`) | the `MUSIC` folder (sub-folders are fine) |

## Buttons

| Button | Does |
|---|---|
| ✕ or START | Start the game (in Music: play / pause the selected track) |
| L / R | Switch between Games, Apps, Library and Music |
| □ | Library: change the sort order · Music: pause / resume |
| ← → in Music | Previous / next track |
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

Download the zip for your ARK again and copy the new files over the old ones, the same way you installed them:

- `PSP/GAME/PSP5/EBOOT.PBP`
- `PSP/SAVEDATA/ARK_01234/VBOOT.PBP` (only if you did the optional step)

Your settings and covers stay.

## Help!

- **Stuck in psp5 or something is broken?** Hold **SELECT** while turning on the PSP. You'll land in the normal XMB.
- **psp5 says "Safe start"?** The last start didn't finish – the PSP froze or was switched off while psp5 was
  starting. psp5 then starts without the startup screen, music and UMD, and clears its image cache. If that start
  doesn't finish either, the next one goes to the XMB instead, so the PSP can't get stuck in psp5. *Boot into psp5*
  turns back on the next time psp5 starts properly from *Game → Memory Stick*.
- **The HOME menu in a game doesn't react, and the game still gets the buttons?** psp5 isn't running at that
  point. It shuts itself down and starts the game the same way ARK's own menu does, so the HOME menu belongs to
  the PSP and ARK. Turn your plugins off (△ → *Plugins*), restart the PSP and try again – cheat, button-remap
  and analog plugins read the buttons too. Also check that L and R aren't stuck. To leave a game anyway, press
  **L + R + START + ↓** together. That combination is built into ARK and quits the game straight away.
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
