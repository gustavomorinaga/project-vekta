import sys, struct, zlib
f = open(sys.argv[1], 'rb')
magic, ver, comp, toclen, esz, n, bsz, flags = struct.unpack('>4sI4sIIIII', f.read(32))
ents = []
for i in range(n):
    e = f.read(esz); idx = struct.unpack('>I', e[16:20])[0]
    ents.append((idx, int.from_bytes(e[20:25], 'big'), int.from_bytes(e[25:30], 'big')))
nb = (toclen - 32 - esz * n) // 2
blocks = struct.unpack('>%dH' % nb, f.read(nb * 2))
def read(i):
    idx, ln, off = ents[i]; out = b''; f.seek(off)
    while len(out) < ln:
        b = blocks[idx]; idx += 1; c = f.read(b or bsz)
        try: out += zlib.decompress(c)
        except zlib.error: out += c
    return out[:ln]
names = read(0).decode().split('\n')
if len(sys.argv) > 3:  # psarc_ls.py <psarc> <outdir> <substring>...: extract matching entries
    import os
    for i, nm in enumerate(names, 1):
        if any(p.lower() in nm.lower() for p in sys.argv[3:]):
            dst = os.path.join(sys.argv[2], nm.lstrip('/'))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(read(i)); print(dst)
    sys.exit()
print(magic, ver, comp, n, bsz, flags)
for nm, (_, ln, _) in zip(names, ents[1:]): print(ln, nm)
