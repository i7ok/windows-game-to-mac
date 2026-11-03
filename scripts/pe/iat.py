"""Map IAT slot addresses of a PE32+ image to 'dll!function'.
usage: python3 -I iat.py FILE 0xADDR [0xADDR ...]
"""
import struct
import sys


def main():
    data = open(sys.argv[1], "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<Q", data, opt + 24)[0]
    imp_rva = struct.unpack_from("<I", data, opt + 112 + 8)[0]  # data directory 1 = imports
    secs = []
    off = opt + optsz
    for _ in range(nsec):
        vs, va, rs, ro = struct.unpack_from("<IIII", data, off + 8)
        secs.append((va, max(vs, rs), ro))
        off += 40

    def r2o(rva):
        for va, sz, ro in secs:
            if va <= rva < va + sz:
                return ro + rva - va
        raise ValueError(hex(rva))

    def cstr(rva):
        o = r2o(rva)
        return data[o:data.index(b"\0", o)].decode()

    names = {}
    d = r2o(imp_rva)
    while True:
        ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", data, d)
        if not name_rva:
            break
        dll = cstr(name_rva)
        i = 0
        src = ilt or iat
        while True:
            entry = struct.unpack_from("<Q", data, r2o(src) + i * 8)[0]
            if not entry:
                break
            fn = f"#{entry & 0xffff}" if entry >> 63 else cstr((entry & 0x7fffffff) + 2)
            names[base + iat + i * 8] = f"{dll}!{fn}"
            i += 1
        d += 20
    for a in sys.argv[2:]:
        print(a, names.get(int(a, 16), "?"))


main()
