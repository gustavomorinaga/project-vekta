#!/usr/bin/env python3
"""Capture the entry state of lifted SPU functions from the running game.

usage: spu_capture.py <spu dir> <module>:<function addr> [...]     (instrument)
       spu_capture.py <spu dir> --restore                            (undo)

Instruments <spu dir>/<module>/spu_recomp.c (keeping a .orig copy): the
first call after the unix time in MB_DUMP_EPOCH writes the 128 registers and
the 256 KB local store to $MB_DUMP/<module>_<addr>.bin. Rebuild, run with
MB_DUMP and MB_DUMP_EPOCH=$(( $(date +%s) + <seconds to the scene> )), then
--restore and rebuild. The files feed tools/spu_sym.py and a
microbenchmark that reruns the function on them.
"""
import glob, os, shutil, sys

spu = sys.argv[1]
if sys.argv[2:] == ["--restore"]:
    for o in glob.glob(os.path.join(spu, "*", "spu_recomp.c.orig")):
        shutil.move(o, o[:-5])
    sys.exit(0)
targets = {}
for t in sys.argv[2:]:
    mod, fn = t.split(":")
    targets.setdefault(mod, []).append(fn.upper().zfill(8))
for mod, fns in targets.items():
    p = os.path.join(spu, mod, "spu_recomp.c")
    if not os.path.exists(p + ".orig"):
        shutil.copy(p, p + ".orig")
    s = "#include <stdio.h>\n#include <stdlib.h>\n#include <time.h>\n" + open(p + ".orig").read()
    for fn in fns:
        sig = "void %s_spu_func_%s(spu_context* ctx) {\n" % (mod, fn)
        if s.count(sig) != 1:
            sys.exit("%s: no single definition of %s" % (p, sig.strip()))
        s = s.replace(sig, sig + (
            '{ static int n; const char* e = getenv("MB_DUMP");'
            ' if (e && !n && time(0) >= atol(getenv("MB_DUMP_EPOCH"))) { n = 1; char b[512];'
            ' snprintf(b, sizeof b, "%%s/%s_%s.bin", e); FILE* f = fopen(b, "wb");'
            ' fwrite(ctx->gpr, 16, 128, f); fwrite(ctx->ls, 1, 0x40000, f); fclose(f); } }\n') % (mod, fn))
    open(p, "w").write(s)
