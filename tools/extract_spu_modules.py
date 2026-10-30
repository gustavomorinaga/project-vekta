#!/usr/bin/env python3
"""Extract the SPU code a Guerrilla EBOOT keeps outside SPU ELFs, for ps3recomp.

- WWS job modules: raw images in the EBOOT's data, each starting with the header
  `4 x ila | entryOffset | imageSize | 0 | 0 | 0xC0DEC0DE` (the format
  ps3recomp/tools/lift_jobmods.py lifts). Written as
  <out>/jobmods/jobmod_<sha1[:12]>_e<entry>.bin, up to the next module's header.
- Policy modules given with --pm VADDR:SIZE (raw images SPURS copies to LS 0xA00,
  as the runtime logs them: "PM@0x005B9180 ... size=12160"). Wrapped as SPU ELFs
  at base 0xA00 into <out>/images/, where build_spu_workloads.py picks them up.

Usage: extract_spu_modules.py EBOOT.elf --out <port>/spu [--pm 0x5B9180:12160 ...]
"""
import argparse, hashlib, os, re, struct, sys

ap = argparse.ArgumentParser()
ap.add_argument("eboot")
ap.add_argument("--out", required=True)
ap.add_argument("--pm", action="append", default=[], metavar="VADDR:SIZE")
ap.add_argument("--ps3recomp", default=os.environ.get("PS3RECOMP_DIR", ""),
                help="ps3recomp checkout (for tools/wrap_spu_elf.py)")
a = ap.parse_args()

d = open(a.eboot, "rb").read()
# PT_LOAD table: vaddr -> file offset
phoff, = struct.unpack_from(">Q", d, 0x20)
phentsize, phnum = struct.unpack_from(">HH", d, 0x36)
loads = []
for k in range(phnum):
    p_type, _, off, vaddr, _, filesz = struct.unpack_from(">IIQQQQ", d, phoff + k * phentsize)
    if p_type == 1:
        loads.append((vaddr, off, filesz))

def file_off(vaddr):
    for v, o, n in loads:
        if v <= vaddr < v + n:
            return o + vaddr - v
    sys.exit(f"0x{vaddr:X} is not in a PT_LOAD segment")

os.makedirs(os.path.join(a.out, "jobmods"), exist_ok=True)
starts = [m.start() - 32 for m in re.finditer(rb"\xC0\xDE\xC0\xDE", d)]
n = 0
for i, start in enumerate(starts):
    ila, entry, size, z0, z1 = struct.unpack_from(">I12x4I", d, start)
    if ila >> 25 != 0x21 or z1 or not 24 < size <= 0x40000 or entry >= size:   # z0: 0, 0x10 or 0x20 seen
        print(f"skipped file 0x{start:07X}: {d[start:start + 36].hex()}")
        continue                                   # 0x21 = ila's 7-bit opcode
    # imageSize counts the module's bss too; the bytes on file end where the next
    # module begins (modules are packed back to back).
    if i + 1 < len(starts):
        size = min(size, starts[i + 1] - start)
    img = d[start:start + size]
    name = f"jobmod_{hashlib.sha1(img).hexdigest()[:12]}_e{entry:04X}.bin"
    open(os.path.join(a.out, "jobmods", name), "wb").write(img)
    print(f"jobmod  file 0x{start:07X} entry 0x{entry:04X} size {size:6d} -> {name}")
    n += 1
print(f"{n} job module(s)")

if a.pm:
    sys.path.insert(0, os.path.join(a.ps3recomp, "tools"))
    from wrap_spu_elf import wrap
    os.makedirs(os.path.join(a.out, "images"), exist_ok=True)
    for spec in a.pm:
        vaddr, size = (int(x, 0) for x in spec.split(":"))
        o = file_off(vaddr)
        name = f"pm_{vaddr:08X}.elf"
        open(os.path.join(a.out, "images", name), "wb").write(wrap(d[o:o + size], base=0xA00))
        print(f"pm      0x{vaddr:08X} size {size} -> images/{name}")
