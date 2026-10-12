# psp5 – made by Mille Weber
"""Packar release-zipparna (en per ARK, plus en för PSP Go) från dist/, dist-ark4/ och dist-go/.

    py tools/make_release.py 1.3.4

Ger release/psp5-v<version>-ARK5.zip och release/psp5-v<version>-ARK4.zip med
    PSP/GAME/PSP5/EBOOT.PBP     – psp5 som vanlig app
    ARK_LAUNCHER/VBOOT.PBP      – samma fil, för att ersätta ARK:s launcher (se INSTALL.md)
    READ ME.txt                 – kort om vilken ARK zippen är till
och release/psp5-v<version>-PSPGo.zip med samma sak i ARK-5/ och ARK-4/.
Bygg först med tools/build.sh (ARK-5), tools/build.sh ark4 (ARK-4) och tools/build.sh go (PSP Go).
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

README_GO = """psp5 v{version} for the PSP Go
made by Mille Weber - https://github.com/08Weber/psp5

This zip is for the PSP Go (PSP-N1000). Use the folder for your custom firmware:
  ARK-5/  - PSP Go with ARK-5
  ARK-4/  - PSP Go with ARK-4
(Press SELECT in the XMB - ARK's VSH menu shows ARK 4.x or ARK 5.x.)

Install:
  1. Connect the PSP Go with USB (Settings -> USB Connection, internal storage).
  2. Copy the PSP folder from ARK-5/ or ARK-4/ onto the PSP Go (merge folders).
  3. Start psp5 from Game -> System Storage.

Games: ISO/CSO/ZSO in ISO, homebrew, PSN and PS1 games in PSP/GAME, music in MUSIC -
on the internal storage or on a Memory Stick Micro (M2).

Optional - use psp5 as ARK's launcher:
  rename PSP/SAVEDATA/ARK_01234/VBOOT.PBP to VBOOT_ARKMENU.PBP, then copy
  ARK_LAUNCHER/VBOOT.PBP from your ARK's folder into PSP/SAVEDATA/ARK_01234/.

Stuck? Hold SELECT while turning on the PSP - ARK then starts the normal XMB.
Full guide: https://github.com/08Weber/psp5/blob/main/INSTALL.md
"""


def eboot_path(dist):
    eboot = os.path.join(ROOT, dist, 'PSP', 'GAME', 'PSP5', 'EBOOT.PBP')
    if not os.path.isfile(eboot):
        sys.exit(f'saknas: {eboot} - bygg först')
    return eboot


def write_zip(name, files, readme):
    out = os.path.join(ROOT, 'release', name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for src, arc in files:
            z.write(src, arc)
        z.writestr('READ ME.txt', readme.replace('\n', '\r\n'))
    print(out, os.path.getsize(out))


def make(version, ark, dist):
    eboot = eboot_path(dist)
    other = ('Running ARK-4? Use psp5-v{v}-ARK4.zip instead.' if ark == 'ARK-5'
             else 'Running ARK-5? Use psp5-v{v}-ARK5.zip instead.').format(v=version)
    other += '\nPSP Go? Use psp5-v{v}-PSPGo.zip instead.'.format(v=version)
    write_zip(f'psp5-v{version}-{ark.replace("-", "")}.zip',
              [(eboot, 'PSP/GAME/PSP5/EBOOT.PBP'), (eboot, 'ARK_LAUNCHER/VBOOT.PBP')],
              README.format(version=version, ark=ark, other=other))


def make_go(version):
    files = []
    for ark in ('ARK-5', 'ARK-4'):
        eboot = eboot_path(os.path.join('dist-go', ark))
        files += [(eboot, f'{ark}/PSP/GAME/PSP5/EBOOT.PBP'), (eboot, f'{ark}/ARK_LAUNCHER/VBOOT.PBP')]
    write_zip(f'psp5-v{version}-PSPGo.zip', files, README_GO.format(version=version))


if __name__ == '__main__':
    v = sys.argv[1] if len(sys.argv) > 1 else '1.3.4'
    make(v, 'ARK-5', 'dist')
    make(v, 'ARK-4', 'dist-ark4')
    make_go(v)
