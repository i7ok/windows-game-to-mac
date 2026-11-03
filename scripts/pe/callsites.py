"""Find call sites of imported functions in a PE32+ image.
usage: python3 -I callsites.py FILE FuncName [FuncName ...]
Prints the IAT slot of each function and every 'call qword ptr [rip+X]' (FF 15) / 'jmp [rip+X]' (FF 25)
and 'mov reg, [rip+X]' (48/4C 8B) that references it.
"""
import re
import struct
import sys


def main():
    data = open(sys.argv[1], "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<Q", data, opt + 24)[0]
    secs, off = [], opt + optsz
    for _ in range(nsec):
        name = data[off:off + 8].rstrip(b"\0").decode()
        vs, va, rs, ro = struct.unpack_from("<IIII", data, off + 8)
        secs.append((name, va, max(vs, rs), ro, rs))
        off += 40

    def r2o(rva):
        for _, va, sz, ro, _ in secs:
            if va <= rva < va + sz:
                return ro + rva - va
        raise ValueError(hex(rva))

    def cstr(rva):
        o = r2o(rva)
        return data[o:data.index(b"\0", o)].decode()

    want = set(sys.argv[2:])
    slots = {}
    imp = struct.unpack_from("<I", data, opt + 112 + 8)[0]
    d = r2o(imp)
    while True:
        ilt, _, _, name, iat = struct.unpack_from("<IIIII", data, d)
        if not name:
            break
        i = 0
        while True:
            e = struct.unpack_from("<Q", data, r2o(ilt or iat) + 8 * i)[0]
            if not e:
                break
            if not e >> 63:
                fn = cstr((e & 0x7fffffff) + 2)
                if fn in want:
                    slots[base + iat + 8 * i] = fn
            i += 1
        d += 20
    for a, f in slots.items():
        print(f"slot {f} 0x{a:x}")
    text = next(s for s in secs if s[0] == ".text")
    _, tva, tvs, toff, trs = text
    code = data[toff:toff + trs]
    for m in re.finditer(rb"\xff[\x15\x25]|[\x48\x4c]\x8b[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", code):
        i = m.start()
        ln = 6 if code[i] == 0xFF else 7
        disp = struct.unpack_from("<i", code, i + ln - 4)[0]
        tgt = base + tva + i + ln + disp
        if tgt in slots:
            print(f"use 0x{base + tva + i:x} {slots[tgt]}")


main()
