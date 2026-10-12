#!/bin/bash
# psp5 – made by Mille Weber
# Bygger launchern i WSL och lägger resultatet i dist/PSP/GAME/PSP5 (ARK-5-versionen)
# eller dist-ark4/PSP/GAME/PSP5 (ARK-4-versionen, "ark4"), PSP Go-versionerna i dist-go/ ("go").
# Från Windows:  wsl -d pspdev -- bash /mnt/c/path/to/psp5/tools/build.sh [ark4|go|test|clean]
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

if [ "$1" = "go" ]; then
    # PSP Go-versionerna (ef0:) för ARK-5 och ARK-4 – hamnar i dist-go/ARK-5 och dist-go/ARK-4
    for ark in 5 4; do
        make clean >/dev/null
        make -j"$(nproc)" GO=1 ARK=$ark
        mkdir -p dist-go/ARK-$ark/PSP/GAME/PSP5
        cp EBOOT.PBP dist-go/ARK-$ark/PSP/GAME/PSP5/EBOOT.PBP
    done
    make clean >/dev/null
    ls -l dist-go/ARK-*/PSP/GAME/PSP5/EBOOT.PBP
    exit 0
fi

if [ "$1" = "clean" ]; then
    make clean
    rm -rf dist dist-ark4 dist-go
    exit 0
fi

make clean >/dev/null # inga objektfiler från ett annat bygge (test/ARK-4)
make -j"$(nproc)"
mkdir -p dist/PSP/GAME/PSP5
cp EBOOT.PBP dist/PSP/GAME/PSP5/EBOOT.PBP
ls -l dist/PSP/GAME/PSP5/EBOOT.PBP
