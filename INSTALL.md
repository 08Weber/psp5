# Installing psp5

*psp5 – made by 08Weber*

**You need:** a PSP with **ARK-4 or ARK-5** (PSP-1000, 2000, 3000 or PSP Go) and a **USB cable**.

## 1. Pick your zip

Not sure which ARK you have? In the XMB, press **SELECT**. ARK's menu shows *ARK 4.x* or *ARK 5.x*.

| Your PSP | Download this |
|---|---|
| PSP-1000, 2000 or 3000 with **ARK-5** | `psp5-v2.0.0-ARK5.zip` |
| PSP-1000, 2000 or 3000 with **ARK-4** | `psp5-v2.0.0-ARK4.zip` |
| **PSP Go** with **ARK-5** | `psp5-v2.0.0-PSPGo-ARK5.zip` |
| **PSP Go** with **ARK-4** | `psp5-v2.0.0-PSPGo-ARK4.zip` |

You'll find them under [Releases](../../releases). Unzip the one you downloaded.

## 2. Copy it to the PSP

1. On the PSP, open *Settings → USB Connection* and plug in the cable. The PSP shows up as a drive on your
   computer (for example `E:`).
2. Copy the **`PSP` folder** from the zip onto that drive. If your computer asks, say yes to merging folders.
3. Eject the drive, unplug the cable and press ○ on the PSP.

## 3. Start psp5

- **PSP-1000, 2000 or 3000:** *Game → Memory Stick → psp5*
- **PSP Go:** *Game → System Storage → psp5*

That's it! The first start takes a moment while psp5 finds your games. After that it opens fast.

## Optional: replace ARK's Custom Launcher with psp5

Then *Custom Launcher* in the XMB opens psp5, and your PSP can start straight into psp5.

1. Connect the PSP with USB and open `PSP/SAVEDATA/ARK_01234/`.
2. Rename the `VBOOT.PBP` that's there to `VBOOT_ARKMENU.PBP`. That keeps ARK's own menu as a backup.
3. Copy `VBOOT.PBP` from the zip's `ARK_LAUNCHER` folder into `ARK_01234`.
4. In psp5, press △ → *psp5* → turn on *Boot into psp5*, then restart the PSP.

To get to the XMB from there: △ → *Power and USB* → *Exit to XMB*.

## Where your games go

| What | Folder |
|---|---|
| PSP games (`.iso`, `.cso`, `.zso`, `.dax`) | `ISO` |
| Homebrew, PSN and PS1 games | `PSP/GAME` |
| Music (`.mp3`) | `MUSIC` (sub-folders are fine) |

On a PSP Go, psp5 looks in these folders on both the internal storage and a Memory Stick Micro.

## Buttons

| Button | What it does |
|---|---|
| ✕ or START | Start the game (in Music: play / pause) |
| L / R | Switch between Games, Apps, Library and Music |
| □ | Games / Apps: options (pin, move, sort, move to the other tab) · Library: sort · Music: shuffle |
| ← → in Music | Previous / next track · hold to seek |
| △ | Settings |
| ○ | Back |
| SELECT | Categories (folders in ISO and PSP/GAME) |
| HOME | Control Center: sleep, USB, files, settings, XMB, restart, power off |

## File manager

*Settings → Power and USB → File manager* lets you manage files right on the PSP. ✕ opens a folder, □ marks
files, △ shows what you can do (copy, move, paste, rename, delete, new folder) and ○ goes back. ✕ on a JPEG or
PNG shows the picture, and ✕ again makes it your wallpaper.

## Updating

Do steps 1 and 2 again with the new zip. Your settings and covers stay.
If you replaced ARK's Custom Launcher with psp5 (see above), copy `ARK_LAUNCHER/VBOOT.PBP` again too.

## Help!

- **Stuck, or something is broken?** Hold **SELECT** while you turn on the PSP. You'll land in the normal XMB.
- **A game is missing?** Check that it's in the right folder (see *Where your games go*). ISO files can be one
  sub-folder deep, not more.
- **Wrong or missing pictures?** △ → *psp5* → *Clear image cache*.
- **A setting didn't change anything?** CPU speed, plugins and other ARK settings need a restart.
- **psp5 says "Safe start"?** The last start didn't finish, for example because the PSP froze. psp5 then starts
  without the startup screen and music. If that fails as well, the PSP goes to the XMB, so it can't get stuck.
- **The HOME menu doesn't react in a game?** That menu belongs to the PSP and ARK, not psp5. Turn your plugins off
  (△ → *Plugins*) and restart. To leave a game anyway, press **L + R + START + ↓** together.
- **Want ARK's own menu back?** In `PSP/SAVEDATA/ARK_01234/`, delete `VBOOT.PBP` and rename
  `VBOOT_ARKMENU.PBP` back to `VBOOT.PBP`.

## Uninstall

1. If psp5 is ARK's launcher, put ARK's menu back (see *Help!*).
2. Delete the folders `PSP/GAME/PSP5` and `PSP5`.
