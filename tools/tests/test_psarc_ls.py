#!/usr/bin/env python3
"""Host test for tools/psarc_ls.py, which the vfs step runs to take the movies out of the game's archive
(tools/kzbuild.py, extract_movies): a movie of many blocks (deflated ones, full blocks stored as they are with a zero
in the block table, and a short stored one at the end) comes out byte for byte, and only the entries asked for; the
listing prints each entry's length and name; a movie is written block by block, so extracting one never holds it in
memory (it was joined with += before, which copied everything read so far at every block: a 31 MB movie took 2.8 s
of copying instead of 0.01 s, and most of the vfs step went there); and an archive cut short stops with an error
that names it and the entry, not with a traceback from the block table. No game data: the archive is made here."""
import hashlib
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "psarc_ls.py"
BSZ = 65536
MOVIE_BLOCKS = 256   # 16 MiB and a bit
PEAK_LIMIT = 4 << 20

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


def movie():
    """(the movie's bytes, its blocks as the archive stores them, their block-table values)."""
    data, stored, table = [], [], []
    for k in range(MOVIE_BLOCKS):
        seed = hashlib.sha256(str(k).encode()).digest()
        if k % 3 == 0:     # a full block stored as it is: 0 in the table, and its first byte is no zlib header
            raw = (b"\0" + seed * (BSZ // 32))[:BSZ]
            data.append(raw), stored.append(raw), table.append(0)
        else:              # a deflated block
            raw = (seed * (BSZ // 32))[:BSZ]
            z = zlib.compress(raw)
            data.append(raw), stored.append(z), table.append(len(z))
    tail = b"\0tail" * 700  # the last, short block, stored as it is
    data.append(tail), stored.append(tail), table.append(len(tail))
    return b"".join(data), stored, table


def make_psarc(path, files):
    """files: name -> (length, stored blocks, block-table values). Entry 0 is the name list, as in the game's."""
    names = "\n".join(files).encode()
    zn = zlib.compress(names)
    entries = [(len(names), [zn], [len(zn)])] + list(files.values())
    n, esz = len(entries), 30
    nblocks = sum(len(t) for _, _, t in entries)
    toclen = 32 + esz * n + 2 * nblocks
    off, idx, toc, table, body = toclen, 0, b"", [], b""
    for ln, stored, t in entries:
        toc += b"\0" * 16 + struct.pack(">I", idx) + ln.to_bytes(5, "big") + off.to_bytes(5, "big")
        idx += len(t)
        table += t
        blob = b"".join(stored)
        body += blob
        off += len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack(">4sI4sIIIII", b"PSAR", 0x10004, b"zlib", toclen, esz, n, BSZ, 2))
        f.write(toc + struct.pack(f">{nblocks}H", *table) + body)


# Runs the tool as the vfs step does, with Python's memory tracing on, and prints the peak after it.
TRACED = """import runpy, sys, tracemalloc
tracemalloc.start()
sys.argv = sys.argv[1:]
code = 0
try:
    runpy.run_path(sys.argv[0], run_name="__main__")
except SystemExit as e:
    code = e.code
print("PEAK", tracemalloc.get_traced_memory()[1])
sys.exit(code)
"""


def run(*args, traced=False):
    cmd = [sys.executable, "-c", TRACED, str(TOOL)] if traced else [sys.executable, str(TOOL)]
    return subprocess.run(cmd + [str(a) for a in args], capture_output=True, text=True, timeout=120)


with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    big, stored, table = movie()
    small = b"AVI2" * 10
    core = b"core" * 5000
    files = {"/Movies/big.avi": (len(big), stored, table),
             "/Menu/small.AVI": (len(small), [zlib.compress(small)], [len(zlib.compress(small))]),
             "/levels/l1.core": (len(core), [zlib.compress(core)], [len(zlib.compress(core))])}
    arc = tmp / "data.psarc"
    make_psarc(arc, files)

    # the listing: the header, then each entry's length and name
    r = run(arc)
    lines = r.stdout.splitlines()
    check(r.returncode == 0 and lines[1:] == [f"{len(big)} /Movies/big.avi", f"{len(small)} /Menu/small.AVI",
                                              f"{len(core)} /levels/l1.core"], f"the listing: {r.stdout}{r.stderr}")

    # the movies, and only them, byte for byte; the substring matches without regard to case
    out = tmp / "out"
    r = run(arc, out, ".avi", traced=True)
    check(r.returncode == 0, f"extracting the movies: {r.stdout[-300:]}{r.stderr[-300:]}")
    check((out / "Movies" / "big.avi").is_file() and (out / "Movies" / "big.avi").read_bytes() == big,
          "a movie of deflated, stored and short blocks comes out byte for byte")
    check((out / "Menu" / "small.AVI").read_bytes() == small and not (out / "levels").exists(),
          "the other movie comes out too, and nothing that was not asked for")
    check(r.stdout.splitlines()[:2] == [str(out / "Movies" / "big.avi"), str(out / "Menu" / "small.AVI")],
          f"each extracted file is named: {r.stdout[:300]}")
    peak = [int(s.split()[1]) for s in r.stdout.splitlines() if s.startswith("PEAK ")]
    check(peak and peak[0] < PEAK_LIMIT,
          f"a {len(big) >> 20} MiB movie is written block by block: the peak was {peak} bytes, limit {PEAK_LIMIT}")

    # an archive cut short: an error that names it and the entry
    cut = tmp / "cut.psarc"
    cut.write_bytes(arc.read_bytes()[:arc.stat().st_size // 2])
    r = run(cut, tmp / "out2", ".avi")
    check(r.returncode != 0 and f"{cut} ends inside entry 1" in r.stderr and "Traceback" not in r.stderr,
          f"a cut archive: {r.returncode} {r.stderr}")

print("test_psarc_ls:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
