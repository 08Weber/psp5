#!/bin/bash
# psp5 – made by Mille Weber
# Bygger launchern i WSL och lägger resultatet i dist/PSP/GAME/PSP5.
# Från Windows:  wsl -d pspdev -- bash /mnt/c/path/to/psp5/tools/build.sh [clean]
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

if [ "$1" = "clean" ]; then
    make clean
    rm -rf dist
    exit 0
fi

make -j"$(nproc)"
mkdir -p dist/PSP/GAME/PSP5
cp EBOOT.PBP dist/PSP/GAME/PSP5/EBOOT.PBP
ls -l dist/PSP/GAME/PSP5/EBOOT.PBP
