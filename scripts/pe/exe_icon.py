"""Pull the largest icon image out of the game's exe (RT_GROUP_ICON) as PNG.

    python3 exe_icon.py /path/to/Game.exe exe_icon.png

No pefile needed: walks the PE resource directory by hand. Needs Pillow; -I would hide a Pillow
installed in the user site, so run it from your own folder (never from inside the game folder) and
pass the exe as an argument.
"""
import io
import struct
import sys

from PIL import Image

exe, out = sys.argv[1], sys.argv[2]
data = open(exe, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
opt = pe + 24
magic = struct.unpack_from("<H", data, opt)[0]
ddir = opt + (112 if magic == 0x20B else 96)
rsrc_rva, _ = struct.unpack_from("<II", data, ddir + 8 * 2)
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


base = r2o(rsrc_rva)


def entries(dir_off):
    named, ids = struct.unpack_from("<HH", data, dir_off + 12)
    for i in range(named + ids):
        name, target = struct.unpack_from("<II", data, dir_off + 16 + 8 * i)
        yield name, target


def leaves(target):
    """(id, bytes) of every language leaf under a name/id directory."""
    if target & 0x80000000:
        for _, t in entries(base + (target & 0x7FFFFFFF)):
            yield from leaves(t)
    else:
        rva, size = struct.unpack_from("<II", data, base + target)
        yield data[r2o(rva):r2o(rva) + size]


icons, groups = {}, []
for type_id, type_t in entries(base):
    if type_id not in (3, 14):
        continue
    for nid, nt in entries(base + (type_t & 0x7FFFFFFF)):
        for blob in leaves(nt):
            if type_id == 3:
                icons[nid] = blob
            else:
                groups.append(blob)

best = None
for g in groups:
    cnt = struct.unpack_from("<H", g, 4)[0]
    for i in range(cnt):
        w, h, _, _, _, bpp, size, iid = struct.unpack_from("<BBBBHHIH", g, 6 + 14 * i)
        w = w or 256
        if best is None or (w, bpp) > best[0]:
            best = ((w, bpp), iid)
blob = icons[best[1]]
if blob[:8] == b"\x89PNG\r\n\x1a\n":
    img = Image.open(io.BytesIO(blob))
else:  # bare DIB -> wrap it into a one-image .ico
    w = best[0][0]
    hdr = struct.pack("<HHH", 0, 1, 1) + struct.pack("<BBBBHHII", w % 256, w % 256, 0, 0, 1, best[0][1], len(blob), 22)
    img = Image.open(io.BytesIO(hdr + blob))
img.convert("RGBA").save(out)
print(out, img.size)
