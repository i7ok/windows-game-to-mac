#!/bin/bash
# Builds the Win32 diagnostic helpers next to their sources (needs: brew install mingw-w64).
# x86_64 by default; pass "i686" to build 32-bit copies (*.x86.exe) for 32-bit games/prefixes.
#
#   wtool      list [all] | click HWND | shot HWND FILE [0|1|2]   windows, child controls, BM_CLICK
#   winfo      [HWND]          foreground/focus/capture state, styles, client rect of a window
#   wclose     HWND            post WM_CLOSE (graceful quit of a game window)
#   focusprobe [SEC] [CLASS]   log foreground/focus/capture/left-button changes every 100 ms
#   rawtest                    does Wine deliver raw mouse input (WM_INPUT) to the foreground window?
#   tcptest                    does GetExtendedTcpTable work and report owning PIDs?
#   ptrtest                    what Wine's GetPointerType/GetPointerInfo/EnableMouseInPointer return
set -euo pipefail
cd "$(dirname "$0")"
arch="${1:-x86_64}"
cc="$arch-w64-mingw32-gcc"
suffix=$([ "$arch" = i686 ] && echo .x86 || echo "")
build() { "$cc" -O2 -D_WIN32_WINNT=0x0A00 -o "$1$suffix.exe" "$1.c" "${@:2}"; echo "built $1$suffix.exe"; }
build wtool -luser32 -lgdi32
build winfo -luser32
build wclose -luser32
build focusprobe -luser32
build rawtest -luser32
build tcptest -lws2_32 -liphlpapi
build ptrtest -luser32
