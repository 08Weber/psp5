# Installing PSP5 REMAKE

*PSP5 REMAKE – fork by medhatk (based on psp5 by 08Weber)*

**You need:** a PSP with **ARK-4 or ARK-5** (PSP-1000, 2000, 3000, E1000 Street, or PSP Go) and a **USB cable**.

## 1. Pick your zip

Not sure which ARK you have? In the XMB, press **SELECT**. ARK's menu shows *ARK 4.x* or *ARK 5.x*.

| Your PSP | Download this |
|---|---|
| PSP-1000, 2000, 3000, or E1000 Street with **ARK-5** | `psp5-remake-vX.Y.Z-ARK5.zip` |
| PSP-1000, 2000, 3000, or E1000 Street with **ARK-4** | `psp5-remake-vX.Y.Z-ARK4.zip` |
| **PSP Go** with **ARK-5** | `psp5-remake-vX.Y.Z-PSPGo-ARK5.zip` |
| **PSP Go** with **ARK-4** | `psp5-remake-vX.Y.Z-PSPGo-ARK4.zip` |

Download the latest release from [Releases](https://github.com/medhatk/PSP5-REMAKE/releases), then choose the
file whose version and ARK label match your setup. `X.Y.Z` stands for the release number. Unzip that file.

## 2. Copy it to the PSP

1. On the PSP, open *Settings → USB Connection* and plug in the cable. The PSP shows up as a drive on your
   computer (for example `E:`).
2. Copy the **`PSP` folder** from the zip onto that drive. If your computer asks, say yes to merging folders.
3. Eject the drive, unplug the cable and press ○ on the PSP.

## 3. Start PSP5 REMAKE

- **PSP-1000, 2000, 3000 or E1000 Street:** *Game → Memory Stick → PSP5 REMAKE*
- **PSP Go:** *Game → System Storage → PSP5 REMAKE*

That's it! The first start takes a moment while PSP5 REMAKE finds your games. After that it opens fast.

## Optional: replace ARK's Custom Launcher with PSP5 REMAKE

Then *Custom Launcher* in the XMB opens PSP5 REMAKE, and your PSP can start straight into PSP5 REMAKE.

1. Connect the PSP with USB and open `PSP/SAVEDATA/ARK_01234/`.
2. Rename the `VBOOT.PBP` that's there to `VBOOT_ARKMENU.PBP`. That keeps ARK's own menu as a backup.
3. Copy `VBOOT.PBP` from the zip's `ARK_LAUNCHER` folder into `ARK_01234`.
4. In PSP5 REMAKE, press △ → *PSP5 REMAKE* → turn on *Boot into PSP5 REMAKE*, then restart the PSP.

To get to the XMB from there: △ → *Power and USB* → *Exit to XMB*.

## Where your games go

| What | Folder |
|---|---|
| PSP games (`.iso`, `.cso`, `.zso`, `.dax`) | `ISO` |
| Homebrew, PSN and PS1 games | `PSP/GAME` |
| Music (`.mp3`, `.m4a`, `.m4b`) | `MUSIC` (sub-folders are fine) |
| MP4 videos | `VIDEO`, `PSP/VIDEO`, or `MP_ROOT` |
| Pictures (`.jpg`, `.jpeg`, `.png`, `.bmp`) | `PICTURE`, `PSP/PHOTO`, or `PSP/SCREENSHOT` |
| UMD Video movies (`.iso`) | `ISO/VIDEO` |

On a PSP Go, PSP5 REMAKE looks for these folders on both the internal storage and a Memory Stick Micro. PSP Go
builds are available, but PSP Go operation has not yet been verified on real hardware.

## Buttons

| Button | What it does |
|---|---|
| ✕ or START | Start the game (in Music: play / pause) |
| L / R | Switch between Games, Apps, Library and Media |
| □ | Options: pin to front · move (pinned games) · Library: also sort · Music: shuffle |
| △ | Settings · Music: albums |
| ← → in Music | Previous / next track · hold to seek |
| ✕ in Pictures | Open a picture · open again to set it as wallpaper |
| ← → in Videos | Seek 10 seconds backward / forward during MP4 playback |
| ○ | Back |
| SELECT | Categories (folders in ISO and PSP/GAME) |
| HOME | Control Center: music, Standby Screen, screenshot, sleep, USB, files, settings, XMB, restart, power off |

## File manager

*Settings → Power and USB → File manager* lets you manage files right on the PSP. ✕ opens a folder, □ marks
files, △ shows what you can do (copy, move, paste, rename, delete, new folder) and ○ goes back. ✕ on a JPG,
JPEG, PNG, or BMP file shows the picture; ✕ again sets it as your wallpaper.

## Updating

Do steps 1 and 2 again with the new zip. Your settings and covers stay.
If you replaced ARK's Custom Launcher with PSP5 REMAKE (see above), copy `ARK_LAUNCHER/VBOOT.PBP` again too.

## Help!

- **Stuck, or something is broken?** Hold **SELECT** while you turn on the PSP. You'll land in the normal XMB.
- **A game is missing?** Check that it's in the right folder (see *Where your games go*). ISO files can be one
  sub-folder deep, not more.
- **Wrong or missing pictures?** △ → *PSP5 REMAKE* → *Clear image cache*.
- **A setting didn't change anything?** CPU speed, plugins and other ARK settings need a restart.
- **PSP5 REMAKE says "Safe start"?** The last start didn't finish, for example because the PSP froze. PSP5 REMAKE then starts
  without the startup screen and music. If that fails as well, the PSP goes to the XMB, so it can't get stuck.
- **The HOME menu doesn't react in a game?** That menu belongs to the PSP and ARK, not PSP5 REMAKE. Turn your plugins off
  (△ → *Plugins*) and restart. To leave a game anyway, press **L + R + START + ↓** together.
- **Want ARK's own menu back?** In `PSP/SAVEDATA/ARK_01234/`, delete `VBOOT.PBP` and rename
  `VBOOT_ARKMENU.PBP` back to `VBOOT.PBP`.

## Uninstall

1. If PSP5 REMAKE is ARK's launcher, put ARK's menu back (see *Help!*).
2. Delete the folders `PSP/GAME/PSP5` and `PSP5`.
