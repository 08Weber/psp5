# psp5 – made by Mille Weber
"""Packar release-zipparna (en per ARK och PSP-sort) från dist/ och dist-ark4/.

    py tools/make_release.py 1.6.0-beta

Ger release/psp5-v<version>-ARK5.zip, -ARK4.zip, -PSPGo-ARK5.zip och -PSPGo-ARK4.zip med
    PSP/GAME/PSP5/EBOOT.PBP     – psp5 som vanlig app
    ARK_LAUNCHER/VBOOT.PBP      – samma fil, för att ersätta ARK:s launcher (se INSTALL.md)
    READ ME.txt                 – kort om vilken PSP och ARK zippen är till
PSP Go-zipparna har samma EBOOT – psp5 känner själv av internminnet (ef0:).
Bygg först med tools/build.sh (ARK-5) och tools/build.sh ark4 (ARK-4).
"""
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

README = """psp5 v{version} for {ark}
made by Mille Weber - https://github.com/08Weber/psp5

This zip is for PSPs running {ark} (PSP-1000, PSP-2000 and PSP-3000).
{other}

Install:
  1. Copy the PSP folder onto your PSP's memory stick (merge folders).
  2. Start psp5 from Game -> Memory Stick.

Optional - use psp5 as ARK's launcher:
  rename PSP/SAVEDATA/ARK_01234/VBOOT.PBP to VBOOT_ARKMENU.PBP, then copy
  ARK_LAUNCHER/VBOOT.PBP from this zip into PSP/SAVEDATA/ARK_01234/.

Music: put .mp3 files in the MUSIC folder.
Stuck? Hold SELECT while turning on the PSP - ARK then starts the normal XMB.
Full guide: https://github.com/08Weber/psp5/blob/main/INSTALL.md
"""

README_GO = """psp5 v{version} for the PSP Go with {ark}
made by Mille Weber - https://github.com/08Weber/psp5

This zip is for the PSP Go (PSP-N1000) running {ark}.
{other}

Install:
  1. Connect the PSP Go with USB (Settings -> USB Connection).
  2. Copy the PSP folder onto the PSP Go's internal storage (merge folders).
  3. Start psp5 from Game -> System Storage.

Games: ISO/CSO/ZSO in ISO, homebrew, PSN and PS1 games in PSP/GAME, music in MUSIC -
on the internal storage or on a Memory Stick Micro (M2).

Optional - use psp5 as ARK's launcher:
  rename PSP/SAVEDATA/ARK_01234/VBOOT.PBP to VBOOT_ARKMENU.PBP, then copy
  ARK_LAUNCHER/VBOOT.PBP from this zip into PSP/SAVEDATA/ARK_01234/
  (on the internal storage).

Stuck? Hold SELECT while turning on the PSP - ARK then starts the normal XMB.
Full guide: https://github.com/08Weber/psp5/blob/main/INSTALL.md
"""


def make(version, ark, dist, go=False):
    eboot = os.path.join(ROOT, dist, 'PSP', 'GAME', 'PSP5', 'EBOOT.PBP')
    if not os.path.isfile(eboot):
        sys.exit(f'saknas: {eboot} - bygg först')
    kind = 'PSPGo-' if go else ''
    other_ark = 'ARK-4' if ark == 'ARK-5' else 'ARK-5'
    other = (f'Running {other_ark}? Use psp5-v{version}-{kind}{other_ark.replace("-", "")}.zip instead.\n'
             + (f'Not a PSP Go? Use psp5-v{version}-{ark.replace("-", "")}.zip instead.' if go
                else f'PSP Go? Use psp5-v{version}-PSPGo-{ark.replace("-", "")}.zip instead.'))
    name = f'psp5-v{version}-{kind}{ark.replace("-", "")}.zip'
    out = os.path.join(ROOT, 'release', name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    text = (README_GO if go else README).format(version=version, ark=ark, other=other).replace('\n', '\r\n')
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(eboot, 'PSP/GAME/PSP5/EBOOT.PBP')
        z.write(eboot, 'ARK_LAUNCHER/VBOOT.PBP')
        z.writestr('READ ME.txt', text)
    print(out, os.path.getsize(out))


if __name__ == '__main__':
    v = sys.argv[1] if len(sys.argv) > 1 else '1.6.0-beta'
    make(v, 'ARK-5', 'dist')
    make(v, 'ARK-4', 'dist-ark4')
    make(v, 'ARK-5', 'dist', go=True)
    make(v, 'ARK-4', 'dist-ark4', go=True)
