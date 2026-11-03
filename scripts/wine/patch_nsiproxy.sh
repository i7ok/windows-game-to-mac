#!/bin/bash
# Fixes GetExtendedTcpTable/GetTcpTable in the Sikarugir WS12WineCX24.0.7_7 engine (WineCX 24.0.7 r6).
#
#   patch_nsiproxy.sh <path to lib/wine/x86_64-unix/nsiproxy.so>
#
# Bug: its nsiproxy.so calls sysctlnametomib("net.inet.tcp.pcblist64"/"...udp.pcblist64") with the MIB
# length still 0 (it lives in .bss), so macOS fills nothing, the following sysctl() fails and every
# TCP/UDP table query returns ERROR_NOT_SUPPORTED ("err:nsi:tcp_conns_enumerate_all Failure to read
# net.inet.tcp.pcblist via sysctl"). Windows Steam then rejects its own webhelper ("Unexpected
# Transport Error 0x3008", "Rejecting connection attempt from unknown source" in transport logs).
#
# Patch: both one-time init functions are tail calls; drop their frame setup and use the freed bytes
# for "mov byte [rdx], 12" (CTL_MAXNAME) before jumping to sysctlnametomib. Original bytes are checked,
# so a different build is refused rather than damaged. Re-signs the file ad hoc. Idempotent.
set -euo pipefail
f="${1:?usage: $0 path/to/nsiproxy.so}"
patch_bytes() { # file offset old_hex new_hex
    local cur
    cur="$(xxd -p -s "$2" -l $((${#3} / 2)) "$1" | tr -d '\n')"
    [ "$cur" = "$4" ] && { echo "$2: already patched"; return 0; }
    [ "$cur" = "$3" ] || { echo "$2: unexpected bytes ($cur), not this engine build" >&2; return 1; }
    printf "$(printf '%s' "$4" | sed 's/../\\x&/g')" | dd of="$1" bs=1 seek=$(($2)) conv=notrunc 2>/dev/null
    echo "$2: patched"
}
patch_bytes "$f" 0x6dc0 554889e5488d3db00d0000488d35be600000488d15e76000005de9c306000090 \
                        488d3db40d0000488d35c2600000488d15eb600000c6020ce9c5060000909090
patch_bytes "$f" 0x73b0 554889e5488d3d72080000488d350e5b0000488d15375b00005de9d300000090 \
                        488d3d76080000488d35125b0000488d153b5b0000c6020ce9d5000000909090
codesign -f -s - "$f" 2>/dev/null && codesign -v "$f" && echo "re-signed ad hoc"
# Verify inside Wine: scripts/win32/tcptest.exe must print "GetExtendedTcpTable ret=0".
