"""List imported function names (regular + delay-load) of a PE32+ file: python3 -I imports.py FILE"""
import struct, sys
data = open(sys.argv[1], "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
opt = pe + 24
secs, off = [], opt + optsz
for _ in range(nsec):
    vs, va, rs, ro = struct.unpack_from("<IIII", data, off + 8)
    secs.append((va, max(vs, rs), ro)); off += 40
def r2o(rva):
    for va, sz, ro in secs:
        if va <= rva < va + sz: return ro + rva - va
    raise ValueError(hex(rva))
def cstr(rva):
    o = r2o(rva); return data[o:data.index(b"\0", o)].decode()
def thunks(rva):
    i = 0
    while True:
        e = struct.unpack_from("<Q", data, r2o(rva) + 8 * i)[0]
        if not e: return
        yield None if e >> 63 else cstr((e & 0x7fffffff) + 2)
        i += 1
dd = lambda n: struct.unpack_from("<II", data, opt + 112 + 8 * n)
imp, _ = dd(1)
d = r2o(imp)
while True:
    ilt, _, _, name, iat = struct.unpack_from("<IIIII", data, d)
    if not name: break
    for f in thunks(ilt or iat):
        if f: print(cstr(name), f)
    d += 20
dimp, _ = dd(13)
if dimp:
    d = r2o(dimp)
    while True:
        attr, name, hmod, iat, int_ = struct.unpack_from("<IIIII", data, d)[:5]
        if not name: break
        for f in thunks(int_):
            if f: print(cstr(name), f, "(delay)")
        d += 32
