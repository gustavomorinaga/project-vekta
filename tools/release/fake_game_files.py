#!/usr/bin/env python3
"""Stand-in files for testing the Windows setup program's pages and its build in the wizard without the game:

    tools/release/fake_game_files.py DIR

Writes into DIR a game folder NPUA80856 whose PARAM.SFO holds only TITLE_ID, and EBOOT.elf, data_ps3.psarc and
libsre.prx with the right first bytes and the tested sizes (tools/check-game-files.py; the archive is a sparse 3 GB
file where the file system allows) but no content. The setup program takes them, and the build's first step then
refuses them by their SHA-256: release.yml runs that path on the Windows runner. Nothing here comes from the game.
Prints the five setup switches (/GAME= ... /DEVFLASH=), one per line. Standard library only.
"""
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cgf", os.path.join(os.path.dirname(HERE), "check-game-files.py"))
cgf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cgf)
SIZES = {label: size for label, _p, size, *_ in cgf.FILES}
TITLE = "NPUA80856"


def sfo(fields):
    """A PARAM.SFO with UTF-8 string fields (format 0x0204): header, index, key table, data table."""
    keys = b"".join(k.encode() + b"\0" for k in fields)
    keys += b"\0" * (-len(keys) % 4)
    index, data, koff = b"", b"", 0
    for k, v in fields.items():
        raw = v.encode() + b"\0"
        size = len(raw) + (-len(raw) % 4)
        index += struct.pack("<HHIII", koff, 0x0204, len(raw), size, len(data))
        data += raw + b"\0" * (size - len(raw))
        koff += len(k) + 1
    kt = 20 + len(index)
    return struct.pack("<4sIIII", b"\0PSF", 0x101, kt, kt + len(keys), len(fields)) + index + keys + data


def stand_in(path, magic, size):
    with open(path, "wb") as f:
        f.write(magic)
        f.truncate(size)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    d = os.path.abspath(argv[0])
    game = os.path.join(d, TITLE)
    os.makedirs(os.path.join(game, "USRDIR"), exist_ok=True)
    with open(os.path.join(game, "PARAM.SFO"), "wb") as f:
        f.write(sfo({"TITLE_ID": TITLE}))
    files = {"eboot": ("EBOOT.elf", b"\x7fELF"), "psarc": ("data_ps3.psarc", b"PSAR"),
             "libsre": ("libsre.prx", b"\x7fELF")}
    out = {"GAME": game}
    for opt, (name, magic) in files.items():
        out[opt.upper()] = os.path.join(d, name)
        stand_in(out[opt.upper()], magic, SIZES[name])
    flash = os.path.join(d, "dev_flash")
    os.makedirs(os.path.join(flash, "sys", "external"), exist_ok=True)
    stand_in(os.path.join(flash, "sys", "external", "flashMP3.pic"), b"\0\0\0\0", 16)
    out["DEVFLASH"] = flash
    for k, v in out.items():
        print(f"/{k}={v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
