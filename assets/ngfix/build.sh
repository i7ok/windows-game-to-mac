#!/bin/bash
# Rebuilds payload/ngfix/version.dll (needs mingw-w64: brew install mingw-w64).
set -euo pipefail
cd "$(dirname "$0")"
x86_64-w64-mingw32-gcc -O2 -Wall -shared -D_WIN32_WINNT=0x0A00 -o version.dll ngfix.c version.def \
    -luser32 -static-libgcc -Wl,--kill-at
x86_64-w64-mingw32-strip version.dll
echo "built $(pwd)/version.dll"
