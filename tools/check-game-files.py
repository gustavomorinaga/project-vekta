#!/usr/bin/env python3
"""Checks the player's own files against what the port was made from (docs/GETTING-FILES.md), before a lift.

    tools/check-game-files.py [--quick] [--root DIR] [--work DIR]

Reads the paths from the same environment variables as tools/env.sh (KZ_GAME, KZ_EBOOT, KZ_PSARC, KZ_LIBSRE,
PS3_DEV_FLASH), with the same defaults under Game/. Each file is checked by size and SHA-256 (--quick: size and
magic only, skipping the 3 GB archive's hash). Prints one line per file and what to do about a mismatch; exits 1
if a required file is missing or different. Standard library only; reads, never writes.

With --root (the port's folder) and --work (its work folder), tools/release/firstrun.py's check step also checks the
place the build runs in, each FAIL saying what to do: a path holding [ ] or ; (CMake reads them as list syntax and
stops while configuring), a folder inside OneDrive (it syncs and locks the build's files while they are written), and
a PC clock behind the port's own files (Ninja then sees them as changed forever). After Project Helghast's
firstrun_check.py checks P1, O1 and T1 (undecuple, MIT); written here.
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.environ.get("KZ_GAME", os.path.join(ROOT, "Game", "NPUA80856"))
# tools/kzbuild.py's default_dev_flash: on Windows RPCS3 keeps dev_flash beside rpcs3.exe, anywhere
DEV_FLASH = os.environ.get("PS3_DEV_FLASH") or (os.path.join(ROOT, "Game", "dev_flash") if os.name == "nt"
                                                else os.path.expanduser("~/.config/rpcs3/dev_flash"))

# The data archive: the player's decrypted data_ps3.psarc when there is one (as up to 0.4.0), else the installed
# game's data_ps3.psarc.edat, which the port opens itself (tools/kzbuild.py data, patch 4014).
PSARC = os.environ.get("KZ_PSARC", os.path.join(ROOT, "Game", "data_ps3.psarc"))
PSARC_FILE = ("data_ps3.psarc", PSARC, 3147174243,
              "341f3c61ee14836047659ebca863ab454fff14f712301ccf8e7f9029deb65d2d", b"PSAR", True,
              "a decrypted data archive (a PSARC); without one the port reads the game's own data_ps3.psarc.edat")
EDAT_FILE = ("data_ps3.psarc.edat", os.path.join(GAME, "USRDIR", "data_ps3.psarc.edat"), 3153321376,
             "3c85c5321bccab33079896f5e74c06902e84648154b315af21e14878d93fe58b", b"NPD\x00", True,
             "the installed game's data archive, USRDIR/data_ps3.psarc.edat, as the package or RPCS3 installs it")
ARCHIVE = PSARC_FILE if os.path.isfile(PSARC) else EDAT_FILE

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
    ARCHIVE,
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


CMAKE_BAD_CHARS = "[];"
CLOCK_SLACK = 24 * 3600   # a day: file times from another time zone are not a wrong clock
CLOCK_FILES = ("VERSION", "tools/kzbuild.py", "tools/check-game-files.py", "port/kz1hd/main.cpp")
ONEDRIVE_VARS = ("OneDrive", "OneDriveConsumer", "OneDriveCommercial")


def _inside(path, base):
    p, b = os.path.normcase(os.path.abspath(path)), os.path.normcase(os.path.abspath(base))
    return p == b or p.startswith(b.rstrip(os.sep) + os.sep)


def place_problems(root, work, environ=None, now=None):
    """The FAIL lines for the place the build runs in (see the docstring); [] when fine."""
    environ = os.environ if environ is None else environ
    out = []
    for what, path in (("port folder", root), ("work folder", work)):
        if not path:
            continue
        bad = sorted({ch for ch in path if ch in CMAKE_BAD_CHARS})
        if bad:
            out.append(f"FAIL     the {what} {path} has {' '.join(bad)} in its path: the build tools (CMake) stop on "
                       "these characters. Move or unpack the port to a folder whose whole path has none of [ ] ;")
        od = next((environ[v] for v in ONEDRIVE_VARS if environ.get(v) and _inside(path, environ[v])), "")
        if od:
            out.append(f"FAIL     the {what} {path} is inside OneDrive ({od}), which syncs and locks the build's files "
                       "while they are written. Move the port to a folder outside OneDrive")
    if root:
        import time
        now = time.time() if now is None else now
        times = [os.path.getmtime(os.path.join(root, f)) for f in CLOCK_FILES if os.path.isfile(os.path.join(root, f))]
        if times and max(times) > now + CLOCK_SLACK:
            day = lambda t: time.strftime("%Y-%m-%d", time.localtime(t))  # noqa: E731
            out.append(f"FAIL     the PC's clock ({day(now)}) is behind the port's own files ({day(max(times))}): the "
                       "build would never finish. Set the date and time right (on Windows: set the time "
                       "automatically), then run it again")
    return out


FIRMWARE = "4.93"   # the PS3 system software libsre.prx and flashMP3.pic were tested from


def firmware_version(dev_flash):
    """The system software version installed in a dev_flash folder ("4.93"), from vsh/etc/version.txt's
    "release:04.9300:" line; None when the file is not there or not readable."""
    try:
        with open(os.path.join(dev_flash, "vsh", "etc", "version.txt"), encoding="ascii", errors="replace") as f:
            for line in f:
                if line.startswith("release:"):
                    v = line.split(":")[1]
                    return f"{int(v.split('.')[0])}.{v.split('.')[1][:2]}"
    except (OSError, ValueError, IndexError):
        pass
    return None


RPCS3_ROUTE = ("when the game and the firmware are installed in RPCS3, run the first run again (run.cmd, run.sh or "
               "install.sh) and choose \"Use my RPCS3 install\": your RPCS3 makes the decrypted copies")


def folder_note(label, path):
    """What to say when a folder was given where a file is wanted (the setup asks for files, a player may give the
    game's folder for each)."""
    usr = os.path.join(path, "USRDIR") if os.path.isdir(os.path.join(path, "USRDIR")) else path
    if os.path.isfile(os.path.join(usr, "EBOOT.BIN")) or os.path.isfile(os.path.join(usr, "data_ps3.psarc.edat")):
        return (f"{path} is the installed game's folder, which holds only the encrypted EBOOT.BIN and .edat, not "
                f"{label}; {RPCS3_ROUTE}")
    if os.path.isdir(os.path.join(path, "sys", "external")):
        return f"{path} is a firmware folder (dev_flash), which holds the encrypted .sprx, not {label}; {RPCS3_ROUTE}"
    return f"{path} is a folder; give the file {label} itself"


def wrong_kind(label, head):
    """What to say about a file that starts with the wrong bytes, by what it is instead."""
    if head == b"SCE\0":
        return ("this is still encrypted (an EBOOT.BIN or .sprx). The port needs the decrypted file; when the game and "
                "the firmware are installed in RPCS3, `firstrun.py --rpcs3` (offered by run.cmd, run.sh and "
                "install.sh) has your RPCS3 decrypt a copy")
    if head == b"NPD\0" and label == "data_ps3.psarc":
        return ("this is the .edat the package installs. Leave data_ps3.psarc unset (KZ_PSARC, the setup's field "
                "empty): the port then reads the game folder's own data_ps3.psarc.edat")
    return f"it starts with {head.hex()}"


def main():
    args = sys.argv[1:]
    quick = "--quick" in args
    opt = lambda name: args[args.index(name) + 1] if name in args[:-1] else ""  # noqa: E731
    bad = False
    for line in place_problems(opt("--root"), opt("--work")):
        print(line)
        bad = True
    fw = firmware_version(DEV_FLASH) if os.path.isdir(DEV_FLASH) else None
    if fw and fw != FIRMWARE:
        print(f"WRONG    firmware: {DEV_FLASH} holds PS3 system software {fw}; the port needs {FIRMWARE} (libsre.prx and "
              f"flashMP3.pic come from it)\n         install system software {FIRMWARE} in RPCS3 (File, Install "
              "Firmware), then take dev_flash and libsre from it again")
        bad = True
    elif fw:
        print(f"ok       firmware {fw}")
    for label, path, size, digest, magic, required, what in FILES:
        tag = "required" if required else "optional"
        if not os.path.isfile(path):
            where = folder_note(label, path) if os.path.isdir(path) else f"nothing at {path}"
            if label == "flashMP3.pic" and os.path.isdir(DEV_FLASH) and not os.path.isdir(os.path.join(DEV_FLASH, "sys")):
                where = (f"{DEV_FLASH} is not a dev_flash folder (no sys{os.sep}external in it); RPCS3 keeps dev_flash "
                         "in its own folder, beside rpcs3.exe")
            print(f"MISSING  {label} ({tag}): {where}\n         expected: {what}")
            bad |= required
            continue
        got_size = os.path.getsize(path)
        with open(path, "rb") as f:
            head = f.read(4)
        if magic and head != magic:
            print(f"WRONG    {label}: {path}: {wrong_kind(label, head)}\n         expected: {what}")
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
            then = ("the check stops here: the port is made from the tested file, and no other has been tried"
                    if required else "nobody has tried this version; it is optional, so the check goes on")
            print(f"DIFFERENT {label}: SHA-256 {got[:16]}..., the tested file is {digest[:16]}...\n"
                  f"         {then}; expected: {what}")
            bad |= required
            continue
        print(f"ok       {label}")
    if bad:
        print("\nSomething above stops the build: see docs/GETTING-FILES.md.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
