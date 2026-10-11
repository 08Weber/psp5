# psp5 – made by Mille Weber
"""Packar release-zipparna (en per ARK) från dist/ och dist-ark4/.

    py tools/make_release.py 1.3.1

Ger release/psp5-v<version>-ARK5.zip och release/psp5-v<version>-ARK4.zip med
    PSP/GAME/PSP5/EBOOT.PBP     – psp5 som vanlig app
    ARK_LAUNCHER/VBOOT.PBP      – samma fil, för att ersätta ARK:s launcher (se INSTALL.md)
    READ ME.txt                 – kort om vilken ARK zippen är till
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
Full guide: https://github.com/08Weber/psp5/blob/main/INSTALL.md
"""


def make(version, ark, dist):
    eboot = os.path.join(ROOT, dist, 'PSP', 'GAME', 'PSP5', 'EBOOT.PBP')
    if not os.path.isfile(eboot):
        sys.exit(f'saknas: {eboot} - bygg först')
    name = f'psp5-v{version}-{ark.replace("-", "")}.zip'
    out = os.path.join(ROOT, 'release', name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    other = ('Running ARK-4? Use psp5-v{v}-ARK4.zip instead.' if ark == 'ARK-5'
             else 'Running ARK-5? Use psp5-v{v}-ARK5.zip instead.').format(v=version)
    text = README.format(version=version, ark=ark, other=other).replace('\n', '\r\n')
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(eboot, 'PSP/GAME/PSP5/EBOOT.PBP')
        z.write(eboot, 'ARK_LAUNCHER/VBOOT.PBP')
        z.writestr('READ ME.txt', text)
    print(out, os.path.getsize(out))


if __name__ == '__main__':
    v = sys.argv[1] if len(sys.argv) > 1 else '1.3.1'
    make(v, 'ARK-5', 'dist')
    make(v, 'ARK-4', 'dist-ark4')
