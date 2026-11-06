#!/bin/bash
# Rebuilds payload/dockfix/dockfix.dylib (needs the Xcode command line tools).
# Universal: Wine's processes are x86_64 (Rosetta 2); arm64 only keeps it loadable everywhere.
set -euo pipefail
cd "$(dirname "$0")"
clang -arch x86_64 -arch arm64 -dynamiclib -O2 -Wall -Wextra -mmacosx-version-min=11.0 \
    -o dockfix.dylib dockfix.c -lobjc
codesign -f -s - dockfix.dylib
echo "built $(pwd)/dockfix.dylib"
