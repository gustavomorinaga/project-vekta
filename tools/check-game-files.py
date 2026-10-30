#!/usr/bin/env python3
"""Checks the player's own files against what the port was made from (docs/GETTING-FILES.md), before a lift.

    tools/check-game-files.py [--quick]

Reads the paths from the same environment variables as tools/env.sh (KZ_GAME, KZ_EBOOT, KZ_PSARC, KZ_LIBSRE,
PS3_DEV_FLASH), with the same defaults under Game/. Each file is checked by size and SHA-256 (--quick: size and
magic only, skipping the 3 GB archive's hash). Prints one line per file and what to do about a mismatch; exits 1
if a required file is missing or different. Standard library only; reads, never writes.
"""
import hashlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.environ.get("KZ_GAME", os.path.join(ROOT, "Game", "NPUA80856"))
DEV_FLASH = os.environ.get("PS3_DEV_FLASH", os.path.expanduser("~/.config/rpcs3/dev_flash"))

# (label, path, size, sha256, magic, required, what it is)
FILES = [
    ("PARAM.SFO", os.path.join(GAME, "PARAM.SFO"), None,
     "352712c456e94be80fca0cd46dda271b96e81d7bf39365cf13448415dca6f642", b"\x00PSF", True,
     "the installed game's PARAM.SFO (title NPUA80856, version 01.00)"),
    ("TROPHY.TRP", os.path.join(GAME, "TROPDIR", "NPWR03819_00", "TROPHY.TRP"), None,
     "6a770bcec111b64c66a5f674f2107b8957334845d4876f4e8dbcfc93bbc10858", None, False,
     "the installed game's trophy set (only for trophy names; the game runs without it)"),
    ("EBOOT.elf", os.environ.get("KZ_EBOOT", os.path.join(ROOT, "Game", "EBOOT.elf")), 19706080,
     "c81aa91e35a76ff7d856095c392cee98bcc4b01879d7f13e5a389c6ee2d903fc", b"\x7fELF", True,
     "the decrypted EBOOT of the installed game (an ELF, not the EBOOT.BIN container)"),
    ("data_ps3.psarc", os.environ.get("KZ_PSARC", os.path.join(ROOT, "Game", "data_ps3.psarc")), 3147174243,
     "341f3c61ee14836047659ebca863ab454fff14f712301ccf8e7f9029deb65d2d", b"PSAR", True,
     "the decrypted data archive (a PSARC, not the .edat the package installs)"),
    ("libsre.prx", os.environ.get("KZ_LIBSRE", os.path.join(ROOT, "Game", "libsre.prx")), 239344,
     "9e7ee6edc99b1d372686cf927b883b18f19ed4d32d1faf35e214cca4ae0737b6", b"\x7fELF", True,
     "the decrypted libsre from PS3 firmware 4.93 (sys/external/libsre.sprx in dev_flash)"),
    ("flashMP3.pic", os.path.join(DEV_FLASH, "sys", "external", "flashMP3.pic"), 69695,
     "adbd993483a41f53c994e080665fb5fbf36af783956d0e4ded30057b8b4e894a", None, False,
     "the firmware's MP3 decoder, read as installed (without it the music and sound stop at the first MP3)"),
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 22), b""):
            h.update(block)
    return h.hexdigest()


def main():
    quick = "--quick" in sys.argv[1:]
    bad = False
    for label, path, size, digest, magic, required, what in FILES:
        tag = "required" if required else "optional"
        if not os.path.isfile(path):
            print(f"MISSING  {label} ({tag}): nothing at {path}\n         expected: {what}")
            bad |= required
            continue
        got_size = os.path.getsize(path)
        with open(path, "rb") as f:
            head = f.read(4)
        if magic and head != magic:
            print(f"WRONG    {label}: {path} starts with {head.hex()}, not {magic.hex()}\n         expected: {what}")
            bad |= required
            continue
        if size is not None and got_size != size:
            print(f"WRONG    {label}: {path} is {got_size} bytes, the tested file is {size}\n         expected: {what}")
            bad |= required
            continue
        if quick and got_size > 64 << 20:
            print(f"ok       {label}: size and type match (hash skipped with --quick)")
            continue
        got = sha256(path)
        if got != digest:
            print(f"DIFFERENT {label}: SHA-256 {got[:16]}..., the tested file is {digest[:16]}...\n"
                  f"         it may still work, but nobody has tried this version; expected: {what}")
            bad |= required
            continue
        print(f"ok       {label}")
    if bad:
        print("\nSome required files are missing or differ: see docs/GETTING-FILES.md.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
