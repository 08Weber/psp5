**PSP5 REMAKE __VERSION__** (fork by **medhatk**, built on [08Weber/psp5](https://github.com/08Weber/psp5)) introduces the **PlayStation 30th Anniversary Theme** with classic four-colour button symbols and shoulder-button icons, plus expanded **System Settings** (Brightness, Volume, Cross/Circle Button Assign, USB Mode, Date & Time formatting, and detailed System Information). Your existing settings, covers, pins and music stay when you update from psp5 or an earlier PSP5 REMAKE release.

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

**Expanded System Settings (`Settings → System`)**
- **Brightness & Sound Volume**: Adjust screen brightness and main system volume directly from *Settings → System* through the kernel's Impose service.
- **Button Assign (`✕` / `○`)**: Swap confirm/cancel between Cross and Circle with immediate launcher & Settings UI feedback and PSP system registry persistence.
- **USB Mode**: Toggle USB Mass Storage directly from *Settings → System* (as well as *Power and USB* and Control Center).
- **Date & Time Sub-page**: Configure Daylight Saving (`-1 hr`, `As is`, `+1 hr`), Date format (`YYYY-MM-DD`, `DD/MM/YYYY`, `MM/DD/YYYY`), and Time format (`24-hour` / `12-hour`), saved in `psp5.cfg` and reflected live in the top bar clock without altering the PSP's hardware RTC.
- **System Information Sub-page**: Inspect PSP nickname, model name, full ARK version, Wi-Fi MAC address, free RAM, and live battery level, charging state, voltage, temperature (`°C`), and capacity (`mAh`).
- Localized across all 10 supported UI languages.

**PlayStation 30th Anniversary Theme & Classic Icons**
- Added the **30th Anniversary** theme with an embedded classic grey Anniversary backdrop, light grey card surfaces, and adaptive dark ink contrast across the launcher, Settings, File Manager, and Control Center.
- Added classic four-colour PlayStation button symbols (`✕`, `○`, `□`, `△`), PlayStation logo icon, and custom `L` / `R` shoulder-button keycaps.

**Full PSP5 REMAKE Features Included**
- Custom **PSP5™** boot logo & **Welcome Back** startup backdrop, unified PS5 top navigation bar & outline icons, dedicated **Apps** tab, automatic game icon border trimming & rounded `ICON1.PMF` video card previews, procedural UI sound engine, **Now playing** mini-card in Control Center, and **Standby Screen** with **△** Sleep / Shut down power overlay.

## Thank you

A huge thank you to **[08Weber](https://github.com/08Weber/psp5)** for creating psp5, as well as the **ARK-5**, **ARK-4**, and **pspdev** teams whose CFW and toolchain power this project.

## Install or update

Unzip the file for your PSP and copy the `PSP` folder over the old one: to the memory stick on a PSP-1000/2000/3000/E1000, or to the internal storage on a PSP Go. Your settings and covers stay. If you use PSP5 REMAKE as ARK's launcher, also copy `ARK_LAUNCHER/VBOOT.PBP` into `PSP/SAVEDATA/ARK_01234/` again. The full guide is in [INSTALL.md](https://github.com/medhatk/PSP5-REMAKE/blob/main/INSTALL.md).

## If your PSP gets stuck

Hold **SELECT** while turning on the PSP. ARK then skips its settings and starts the normal XMB.
