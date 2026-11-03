"""Find code that references given strings in a PE32+ image (RIP-relative LEA/MOV).
usage: python3 -I xref.py FILE 'string 1' 'string 2' ...
Prints, for each string: its VMA and every instruction address that points at it.
"""
import struct
import sys


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    out = []
    off = pe + 24 + optsz
    for i in range(nsec):
        name = data[off:off + 8].rstrip(b"\0").decode()
        vsize, va, rsize, roff = struct.unpack_from("<IIII", data, off + 8)
        out.append((name, base + va, vsize, roff, rsize))
        off += 40
    return base, out


def main():
    path = sys.argv[1]
    data = open(path, "rb").read()
    base, secs = sections(data)
    text = next(s for s in secs if s[0] == ".text")
    _, tva, tvs, toff, trs = text

    def off2va(o):
        for name, va, vs, ro, rs in secs:
            if ro <= o < ro + rs:
                return va + (o - ro)
        return None

    targets = {}
    for s in sys.argv[2:]:
        needle = s.encode() + b"\0"
        start = 0
        while True:
            o = data.find(needle, start)
            if o < 0:
                break
            # whole string only: previous byte must be NUL
            if o == 0 or data[o - 1] == 0:
                targets[off2va(o)] = s
            start = o + 1
    for va, s in targets.items():
        print(f"string {s!r} at 0x{va:x}")

    code = data[toff:toff + min(tvs, trs)]
    hits = []
    n = len(code)
    for i in range(n - 7):
        b0, b1, b2 = code[i], code[i + 1], code[i + 2]
        # REX.W(+R) 8D/8B modrm(mod=00, rm=101) disp32 -> 7-byte instruction
        if b0 in (0x48, 0x4C) and b1 in (0x8D, 0x8B) and (b2 & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", code, i + 3)[0]
            tgt = tva + i + 7 + disp
            if tgt in targets:
                hits.append((tva + i, targets[tgt]))
    for va, s in hits:
        print(f"xref 0x{va:x} -> {s!r}")


main()
