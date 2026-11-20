#!/usr/bin/env python3
"""Checks the player's own files against what the port was made from (docs/GETTING-FILES.md), before a lift.

    tools/check-game-files.py [--quick] [--root DIR] [--work DIR]

Reads the paths from the same environment variables as tools/env.sh (KZ_GAME, KZ_EBOOT, KZ_PSARC,
PS3_DEV_FLASH), with the same defaults under Game/. Each file is checked by size and SHA-256 (--quick: size and
magic only, skipping the 3 GB archive's hash). Prints one line per file and what to do about a mismatch; exits 1
if a required file is missing or different. Standard library only; reads, never writes.

With --root (the port's folder) and --work (its work folder), tools/release/firstrun.py's check step also checks the
place the build runs in, each FAIL saying what to do: a path holding [ ] or ; (CMake reads them as list syntax and
stops while configuring), a folder inside OneDrive (it syncs and locks the build's files while they are written), and
a PC clock behind the port's own files (Ninja then sees them as changed forever). After Project Helghast's
firstrun_check.py checks P1, O1 and T1 (undecuple, MIT); written here. It also checks the CPU against the x86-64
level the build is made for (KZ_MARCH, default x86-64-v3): port/kz1hd/cpu_check.c's test, before an hour of building.
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


# The x86-64 levels by /proc/cpuinfo's flag names (port/kz1hd/cpu_level.h has the same rules by CPUID bit; the
# kernel drops the AVX flags when it does not save the YMM state)
CPU_FLAGS = {2: {"cx16", "lahf_lm", "popcnt", "pni", "sse4_1", "sse4_2", "ssse3"},
             3: {"avx", "avx2", "bmi1", "bmi2", "f16c", "fma", "abm", "movbe", "xsave"},
             4: {"avx512f", "avx512bw", "avx512cd", "avx512dq", "avx512vl"}}
CPU_NEEDS = {2: "SSE4.2, POPCNT: Intel Nehalem (2008) or AMD Bulldozer (2011) and later",
             3: "AVX2, FMA, BMI2: Intel Haswell (2013) or AMD Excavator (2015) and later",
             4: "AVX-512: Intel Skylake-X, Ice Lake or later, AMD Zen 4 or later"}
LAUNCHER = "Project Vekta.exe"


def cpu_level_of_flags(flags):
    level = 1
    for n in (2, 3, 4):
        if not CPU_FLAGS[n] <= flags:
            break
        level = n
    return level


def cpu_level(root, windows=os.name == "nt", cpuinfo="/proc/cpuinfo", run=None):
    """This CPU's x86-64 level, or None when it cannot be told: Linux reads /proc/cpuinfo; Windows asks the release
    folder's launcher (Project Vekta.exe --cpu-check, which exits with it; a Git clone has no launcher)."""
    if windows:
        exe = os.path.join(root or ROOT, LAUNCHER)
        if not os.path.isfile(exe):
            return None
        import subprocess
        try:
            rc = (run or subprocess.run)([exe, "--cpu-check"], timeout=30).returncode
        except (OSError, subprocess.SubprocessError):
            return None
        return rc if rc in (1, 2, 3, 4) else None
    try:
        with open(cpuinfo, encoding="ascii", errors="replace") as f:
            line = next((ln for ln in f if ln.startswith("flags")), "")
    except OSError:
        return None
    return cpu_level_of_flags(set(line.partition(":")[2].split())) if line else None


def cpu_line(have, need):
    """check-game-files' line for the CPU: FAIL when it is below the level the build is made for. The FAIL's label
    is "CPU:" (firstrun.py ends the first run on it: no file the player gives can fix it)."""
    if not need:
        return None
    if have is None:
        return f"ok       CPU: not checked here (the build needs x86-64-v{need})"
    if have >= need:
        return f"ok       CPU: x86-64-v{have} (the build needs x86-64-v{need})"
    return (f"FAIL     CPU: this build of Project Vekta needs a CPU with x86-64-v{need} ({CPU_NEEDS[need]}), and this "
            f"one has x86-64-v{have}. The game cannot run on it")


FIRMWARE = "4.93"   # the PS3 system software flashMP3.pic was tested from


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


def edat_license(path):
    """The licence type in an .edat's NPD header (big-endian word at 0x08): 3 = free (the Killzone Trilogy disc's
    package, which the port opens itself), 2 = local and 1 = network (a PlayStation Store copy, bound to the buyer's
    .rap, which only the player's RPCS3 can open); None when the file is not NPD."""
    with open(path, "rb") as f:
        head = f.read(12)
    return int.from_bytes(head[8:12], "big") if len(head) == 12 and head[:4] == b"NPD\0" else None


def wrong_kind(label, head):
    """What to say about a file that starts with the wrong bytes, by what it is instead."""
    if head == b"SCE\0":
        return ("this is still encrypted (an EBOOT.BIN). The port needs the decrypted file; when the game is "
                "installed in RPCS3, `firstrun.py --rpcs3` (offered by run.cmd, run.sh and "
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
    if opt("--root"):   # the first run's check: the CPU against the level the build will be made for (KZ_MARCH)
        import kzbuild
        line = cpu_line(cpu_level(opt("--root")), kzbuild.cpu_required(kzbuild.march(os.environ)))
        if line:
            print(line)
            bad = bad or line.startswith("FAIL")
    fw = firmware_version(DEV_FLASH) if os.path.isdir(DEV_FLASH) else None
    if fw and fw != FIRMWARE:
        print(f"WRONG    firmware: {DEV_FLASH} holds PS3 system software {fw}; the port needs {FIRMWARE} (the music "
              f"decoder, flashMP3.pic, comes from it)\n         install system software {FIRMWARE} in RPCS3 (File, "
              "Install Firmware), then take dev_flash from it again")
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
        if label == "data_ps3.psarc.edat" and edat_license(path) in (1, 2):
            print(f"WRONG    {label}: {path} is the PlayStation Store copy's archive, bound to your licence "
                  f"(licence type {edat_license(path)}), which the port cannot open itself. Run the first run again "
                  "(run.cmd, run.sh or install.sh) and choose \"Use my RPCS3 install\": your RPCS3 opens a copy of it "
                  "with your licence (.rap) into data_ps3.psarc\n         expected: the disc package's .edat (licence "
                  "type 3), or a decrypted data_ps3.psarc")
            bad = True
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
