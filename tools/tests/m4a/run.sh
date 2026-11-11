#!/bin/sh
# psp5 – made by 08Weber
# Testar M4A-läsaren (src/m4a.c) på datorn, med AddressSanitizer:  sh tools/tests/m4a/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../../src"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
python3 "$here/gen.py" "$out"
cc -std=gnu99 -g -Wall -fsanitize=address,undefined -fno-sanitize-recover=all \
   -I "$here/inc" -I "$src" "$here/test.c" "$src/m4a.c" -o "$out/test"
cd "$out" && ./test
