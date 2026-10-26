#!/bin/bash
# psp5 – made by 08Weber
# Bygger launchern i WSL och lägger resultatet i dist/PSP/GAME/PSP5 (ARK-5-versionen)
# eller dist-ark4/PSP/GAME/PSP5 (ARK-4-versionen, "ark4").
# Båda fungerar också på PSP Go – psp5 känner själv av internminnet (ef0:).
# Från Windows:  wsl -d pspdev -- bash /mnt/c/path/to/psp5/tools/build.sh [ark4|test|clean]
set -e
. /etc/profile.d/pspdev.sh
cd "$(dirname "$0")/.."

if [ "$1" = "test" ]; then
    # testbygge med skriptat testläge (src/testrun.c) – hamnar i dist-test/
    make clean >/dev/null
    make -j"$(nproc)" EXTRA=-DPSP5_TEST
    mkdir -p dist-test/PSP/GAME/PSP5
    cp EBOOT.PBP dist-test/PSP/GAME/PSP5/EBOOT.PBP
    make clean >/dev/null
    ls -l dist-test/PSP/GAME/PSP5/EBOOT.PBP
    exit 0
fi

if [ "$1" = "ark4" ]; then
    # ARK-4-versionen: byggs utan ARK-5:s ArkCtrl-bibliotek
    make clean >/dev/null
    make -j"$(nproc)" ARK=4
    mkdir -p dist-ark4/PSP/GAME/PSP5
    cp EBOOT.PBP dist-ark4/PSP/GAME/PSP5/EBOOT.PBP
    make clean >/dev/null
    ls -l dist-ark4/PSP/GAME/PSP5/EBOOT.PBP
    exit 0
fi

if [ "$1" = "clean" ]; then
    make clean
    rm -rf dist dist-ark4
    exit 0
fi

make clean >/dev/null # inga objektfiler från ett annat bygge (test/ARK-4)
make -j"$(nproc)"
mkdir -p dist/PSP/GAME/PSP5
cp EBOOT.PBP dist/PSP/GAME/PSP5/EBOOT.PBP
ls -l dist/PSP/GAME/PSP5/EBOOT.PBP
