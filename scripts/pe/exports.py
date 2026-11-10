"""Print the exported names of a PE file, one per line (ordinal-only exports as @N).

    python3 -I exports.py FILE
"""
import struct
import sys

data = open(sys.argv[1], "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
opt = pe + 24
magic = struct.unpack_from("<H", data, opt)[0]
ddir = opt + (112 if magic == 0x20B else 96)
exp_rva, exp_size = struct.unpack_from("<II", data, ddir)
secs, off = [], opt + optsz
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


e = r2o(exp_rva)
base, nfunc, nnames, afunc, anames, aords = struct.unpack_from("<IIIIII", data, e + 16)
named = set()
for i in range(nnames):
    name = cstr(struct.unpack_from("<I", data, r2o(anames) + 4 * i)[0])
    ordinal = struct.unpack_from("<H", data, r2o(aords) + 2 * i)[0]
    named.add(ordinal)
    func = struct.unpack_from("<I", data, r2o(afunc) + 4 * ordinal)[0]
    fwd = exp_rva <= func < exp_rva + exp_size
    print(name + ("  -> " + cstr(func) if fwd else ""))
for i in range(nfunc):
    if i not in named and struct.unpack_from("<I", data, r2o(afunc) + 4 * i)[0]:
        print("@%d" % (base + i))
