#!/usr/bin/env python3
"""Assemble the Windows release folder (the payload the installer and the portable zip carry) from the repository,
with an allowlist, a scrub and a manifest:

    tools/release/assemble_payload.py --out DIR [--replace] [--zip FILE] [--cache DIR] [--repo DIR]
    tools/release/assemble_payload.py --dry-run [--cache DIR] [--repo DIR]

The payload keeps the repository's own layout, so tools/kzbuild.py and tools/release/firstrun.py run in it as they
run in a clone (the player's files, the lift and the build go to its work/ like in a clone):

  tools/          kzbuild.py, check-game-files.py, psarc_ls.py, extract_spu_modules.py and, in tools/release/,
                  firstrun.py, launch.py, fetch_toolchain.py, build_win_deps.py, toolchain.json
  re/             function_seeds.txt (the lift's seeds)
  port/           port/kz1hd (its sources, no tests) and port/cmake/*.cmake
  work/ps3recomp-patched/  the patched ps3recomp (tools/ps3recomp-patched.sh), without .github, docs, packaging and
                  scratch: the player needs no Git, the patches are already applied. Its .patchset must be the one
                  of the repository's port/patches (a stale tree is refused).
  work/toolchain-cache/    the Windows entries of tools/release/toolchain.json (llvm-mingw, CMake, Ninja, the
                  embeddable Python, SDL2, zlib, glslang, the Vulkan headers, FFmpeg), unpacked by fetch_toolchain.py
                  --host windows, with a fetched.json for them only (the build fetches nothing), minus PRUNE
                  (clang's Linux runtimes, zlib's Amiga files). The Python's ._pth
                  also names tools/, tools/release/ and ps3recomp's tools/: the embeddable Python never puts a
                  script's own folder on sys.path, and the lifters import their neighbours.
  VERSION, LICENSE, NOTICE.md, README.md, licenses/, assets/icon.svg, docs/GETTING-FILES.md, docs/BUILDING.md
  run.cmd, play.cmd   installer/run.cmd (the first run: questions, check, lift, build) and installer/play.cmd
  Project Vekta.exe   with --launcher EXE: the launcher tools/release/build_launcher.py builds

Nothing else is copied. The scrub refuses the whole payload (nothing is written) on: game-data names and extensions
(EBOOT.*, PARAM.SFO, .psarc .elf .self .sprx .prx .edat .sdat .pkg .rap .pup .trp .pic), PS3 or ELF magics at a
file's start, home-folder paths, secrets, a file over 8 MB, or two paths equal under case-folding (Windows keeps one)
outside the toolchain; the toolchain's files get the name and magic checks only. manifest.json at the root lists
every file with its size and SHA-256, the version, the commit, the patch set and the toolchain's components; no
timestamps, so the same sources give the same manifest (and --zip the same zip).

After Project Pyrrhus's and Project Helghast's tools/release/assemble_payload.py (undecuple, MIT): the allowlist,
the scrub and the manifest are theirs. Exit 0 = ok, 1 = refused or a source is missing, 2 = usage. Standard library
only.
"""
import argparse
import fnmatch
import gzip
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import fetch_toolchain  # noqa: E402
import kzbuild  # noqa: E402

TOOLKIT = "work/ps3recomp-patched"
CACHE = "work/toolchain-cache"
# (source in the repository, path in the payload, kind, options)
RULES = [
    ("tools/kzbuild.py", "tools/kzbuild.py", "file", {}),
    ("tools/check-game-files.py", "tools/check-game-files.py", "file", {}),
    ("tools/psarc_ls.py", "tools/psarc_ls.py", "file", {}),
    ("tools/extract_spu_modules.py", "tools/extract_spu_modules.py", "file", {}),
    ("tools/release/firstrun.py", "tools/release/firstrun.py", "file", {}),
    ("tools/release/launch.py", "tools/release/launch.py", "file", {}),
    ("tools/release/fetch_toolchain.py", "tools/release/fetch_toolchain.py", "file", {}),
    ("tools/release/build_win_deps.py", "tools/release/build_win_deps.py", "file", {}),
    ("tools/release/toolchain.json", "tools/release/toolchain.json", "file", {}),
    ("re/function_seeds.txt", "re/function_seeds.txt", "file", {}),
    ("port/kz1hd", "port/kz1hd", "tree", {"exclude_dirs": ["tests"]}),
    ("port/cmake", "port/cmake", "tree", {"include": ["*.cmake"]}),
    (TOOLKIT, TOOLKIT, "tree", {"exclude_dirs": [".github", "docs", "packaging", "scratch"],
                                "exclude": ["*.pyc"]}),
    ("VERSION", "VERSION", "file", {}),
    ("LICENSE", "LICENSE", "file", {}),
    ("NOTICE.md", "NOTICE.md", "file", {}),
    ("README.md", "README.md", "file", {}),
    ("licenses", "licenses", "tree", {}),
    ("assets/icon.svg", "assets/icon.svg", "file", {}),
    ("docs/GETTING-FILES.md", "docs/GETTING-FILES.md", "file", {}),
    ("docs/BUILDING.md", "docs/BUILDING.md", "file", {}),
]
RULES_WINDOWS = [
    ("installer/run.cmd", "run.cmd", "file", {}),
    ("installer/play.cmd", "play.cmd", "file", {}),
]
RULES_LINUX = [
    ("tools/release/build_linux_deps.py", "tools/release/build_linux_deps.py", "file", {}),
    ("tools/release/zig", "tools/release/zig", "tree", {}),
    ("tools/env.sh", "tools/env.sh", "file", {}),
    ("tools/play.sh", "tools/play.sh", "file", {}),
    ("installer/linux/run.sh", "run.sh", "file", {}),
    ("installer/linux/play.sh", "play.sh", "file", {}),
]
GAME_NAMES = ["EBOOT.*", "PARAM.SFO", "*.psarc", "*.elf", "*.self", "*.sprx", "*.prx", "*.edat", "*.sdat", "*.pkg",
              "*.rap", "*.pup", "*.trp", "*.pic"]
MAGICS = [b"SCE\0", b"PSAR", b"\x7fELF", b"\0PSF", b"NPD\0", b"RTTIBin"]
TEXT_RULES = [
    ("home path", re.compile(rb"/home/[a-z][a-z0-9_.-]*/")),
    ("home path", re.compile(rb"[A-Za-z]:[\\/]+Users[\\/]+(?!Public\b|Default\b|<|%|\{|\$)[A-Za-z0-9_. -]+[\\/]",
                             re.I)),
    ("secret", re.compile(b"sk-" b"ant-|gh" b"p_[A-Za-z0-9]{20}|github_" b"pat_|BEGIN [A-Z ]*PRIVATE" b" KEY")),
]
MAX_FILE = 8 << 20
# Toolchain files the Windows build never uses, left out (paths inside the entry's tree): among them clang's Linux
# runtimes in the Windows llvm-mingw (ELF files) and zlib's Amiga makefiles (Makefile.pup reads as a game-data name).
# Project Pyrrhus's llvm-mingw list also drops the other targets (ARM, 32-bit), LLDB, its Python and make.
PRUNE = {"llvm-mingw": ["aarch64-w64-mingw32/*", "armv7-w64-mingw32/*", "i686-w64-mingw32/*", "arm64ec-w64-mingw32/*",
                        "bin/aarch64-w64-mingw32-*", "bin/armv7-w64-mingw32-*", "bin/i686-w64-mingw32-*",
                        "bin/arm64ec-w64-mingw32-*", "bin/*widl*", "busybox/*", "python/*", "bin/lldb*",
                        "bin/liblldb.dll", "bin/libpython3*.dll", "bin/mingw32-make.exe", "bin/gendef.exe",
                        "share/gendef/*", "share/make/*", "share/widl/*", "lib/clang/*/lib/linux/*",
                        "bin/clangd.exe", "bin/clang-tidy.exe"],
         "zlib": ["amiga/*"],
         # the build takes FFmpeg's headers and DLLs only (build_win_deps.py), SDL2's x86_64 tree only
         "ffmpeg": ["bin/*.exe", "bin/avdevice-*.dll", "bin/avfilter-*.dll", "doc/*", "lib/*"], "sdl2": ["i686-w64-mingw32/*", "docs/*", "test/*"],
         "cmake": ["doc/*", "man/*"]}
# The Linux folder (--platform linux): Zig keeps only what the x86_64-linux-gnu target reads (its libc headers for
# Linux and glibc, glibc's stubs, libc++ and the standard library it builds compiler_rt from); FFmpeg its libraries.
ZIG_KEEP_INCLUDE = ("any-linux-any", "generic-glibc", "x86-linux-any", "x86-linux-gnu")
PRUNE_LINUX = {"zig": ["lib/libc/mingw/*", "lib/libc/musl/*", "lib/libc/wasi/*", "lib/libc/darwin/*",
                       "lib/libc/freebsd/*", "lib/libc/netbsd/*", "lib/libc/openbsd/*", "doc/*",
                       "METADATA", "RECORD", "WHEEL", "entry_points.txt", "__init__.py", "__main__.py"],
               "cmake-linux": ["doc/*", "man/*", "bin/cmake-gui", "bin/ccmake", "bin/cpack", "bin/ctest"],
               "zlib": ["amiga/*"],
               # SDL2's tests and IDE projects (SDL_TEST is off; the build needs only CMake's files)
               "sdl2-src": ["test/*", "Xcode/*", "Xcode-iOS/*", "VisualC/*", "VisualC-GDK/*", "VisualC-WinRT/*",
                            "android-project/*", "android-project-ant/*", "docs/*"],
               "ffmpeg-linux": ["bin/*", "doc/*", "man/*", "share/*", "lib/pkgconfig/*", "lib/libavdevice*",
                                "lib/libavfilter*", "lib/*.so"]}
# The ._pth of the embeddable Python: its own zip and folder, then the folders whose scripts import their neighbours
# (relative to the ._pth, in work/toolchain-cache/python/).
PTH_EXTRA = ["..\\..\\..\\tools", "..\\..\\..\\tools\\release", "..\\..\\ps3recomp-patched\\tools"]


class Refused(Exception):
    pass


def skipped(name, opts, is_dir):
    if is_dir:
        return name in opts.get("exclude_dirs", ()) or name in ("__pycache__", ".git")
    if any(fnmatch.fnmatch(name, p) for p in opts.get("exclude", ())):
        return True
    inc = opts.get("include")
    return bool(inc) and not any(fnmatch.fnmatch(name, p) for p in inc)


def collect(repo, rules=None):
    """{payload path: source path} from the rules (default RULES + RULES_WINDOWS); a missing source raises Refused."""
    files = {}
    for src, dst, kind, opts in (RULES + RULES_WINDOWS if rules is None else rules):
        s = os.path.join(repo, src)
        if kind == "file":
            if not os.path.isfile(s):
                raise Refused(f"missing: {src}")
            files[dst] = s
            continue
        if not os.path.isdir(s):
            raise Refused(f"missing: {src}/" + (" (run tools/ps3recomp-patched.sh)" if src == TOOLKIT else ""))
        for d, dirs, fs in os.walk(s):
            dirs[:] = sorted(x for x in dirs if not skipped(x, opts, True) and not os.path.islink(os.path.join(d, x)))
            for f in sorted(fs):
                p = os.path.join(d, f)
                if skipped(f, opts, False) or os.path.islink(p):
                    continue
                rel = os.path.relpath(p, s).replace(os.sep, "/")
                files[f"{dst}/{rel}"] = p
    return files


def name_problem(rel):
    base = rel.rsplit("/", 1)[-1]
    for p in GAME_NAMES:
        if fnmatch.fnmatch(base.lower(), p.lower()):
            return f"game-data name ({p})"
    return None


def scrub(rel, path, toolchain=False, elf_ok=False):
    """The reason a file may not ship, or None. elf_ok: the Linux toolchain's own programs and libraries."""
    why = name_problem(rel)
    if why:
        return why
    with open(path, "rb") as f:
        head = f.read(8)
    if any(head.startswith(m) for m in MAGICS if not (elf_ok and m == b"\x7fELF")):
        return f"game or ELF data ({head[:4]!r})"
    if toolchain:
        return None
    size = os.path.getsize(path)
    if size > MAX_FILE:
        return f"over {MAX_FILE >> 20} MB"
    with open(path, "rb") as f:
        data = f.read()
    for what, rx in TEXT_RULES:
        if rx.search(data):
            return what
    return None


def case_clashes(paths):
    seen, out = {}, []
    for p in sorted(paths):
        k = p.lower()
        if k in seen:
            out.append(f"{seen[k]} / {p}")
        seen.setdefault(k, p)
    return out


def windows_entries(m):
    return fetch_toolchain.entries_for(m, "windows")


LINUX_SOURCES = ("zlib", "glslang", "vulkan-headers")   # the "any" sources the Linux build also builds


def linux_entries(m):
    return [e for e in m["components"] if e["host"] == "native" or e["name"] in LINUX_SOURCES]


def pruned(platform, name, inner):
    if platform == "linux":
        if name == "zig" and inner.startswith("lib/libc/include/") and \
                inner.split("/")[3] not in ZIG_KEEP_INCLUDE:
            return True
        return any(fnmatch.fnmatch(inner, g) for g in PRUNE_LINUX.get(name, ()))
    return any(fnmatch.fnmatch(inner, g) for g in PRUNE.get(name, ()))


def collect_toolchain(cache, m, platform="windows"):
    """{payload path: source} for the platform's entries' unpacked trees, and their fetched.json."""
    fetched = fetch_toolchain.read_fetched(cache)
    files, done, problems = {}, {}, []
    for e in (linux_entries(m) if platform == "linux" else windows_entries(m)):
        name = e["name"]
        tree = os.path.join(cache, name)
        if e["sha256"] == fetch_toolchain.TO_FILL:
            problems.append(f"{name}: its sha256 is still TO-FILL")
            continue
        if fetched.get(name) != e["sha256"] or not os.path.isdir(tree):
            problems.append(f"{name}: not fetched from its pin (tools/release/fetch_toolchain.py --host "
                            f"{'native' if platform == 'linux' else 'windows'})")
            continue
        done[name] = e["sha256"]
        for d, dirs, fs in os.walk(tree):
            dirs.sort()
            for f in sorted(fs):
                p = os.path.join(d, f)
                if os.path.islink(p):
                    continue
                inner = os.path.relpath(p, tree).replace(os.sep, "/")
                if pruned(platform, name, inner):
                    continue
                files[f"{CACHE}/{name}/{inner}"] = p
    if problems:
        raise Refused("toolchain:\n  " + "\n  ".join(problems))
    return files, done


def pth_name(files):
    """The embeddable Python's ._pth in the payload (one, or Refused)."""
    found = [p for p in files if p.startswith(f"{CACHE}/python/") and p.endswith("._pth") and p.count("/") == 3]
    if len(found) != 1:
        raise Refused(f"the embeddable Python has {len(found)} ._pth files (one expected)")
    return found[0]


def pth_text(src):
    with open(src, encoding="utf-8") as f:
        lines = [ln.rstrip("\r\n") for ln in f]
    keep = [ln for ln in lines if ln.strip() and not ln.startswith("#")]
    return "\r\n".join(keep + PTH_EXTRA) + "\r\n"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def git_commit(repo):
    try:
        r = subprocess.run(["git", "-C", repo, "rev-parse", "HEAD"], capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else None
    except OSError:
        return None


def check_toolkit(repo):
    """The patched tree's .patchset against the repository's port/patches."""
    try:
        with open(os.path.join(repo, TOOLKIT, ".patchset"), encoding="utf-8") as f:
            have = f.read().strip()
    except OSError:
        raise Refused(f"no {TOOLKIT}/.patchset: run tools/ps3recomp-patched.sh") from None
    e = kzbuild.resolve(root=repo)
    want = kzbuild.patchset_id(repo, shutil.which("git"), e)
    if have != want:
        raise Refused(f"{TOOLKIT} is stale (.patchset {have}, port/patches {want}): run tools/ps3recomp-patched.sh")
    return have


def plan(repo, cache, launcher=None, platform="windows"):
    """(files {payload path: source}, generated {payload path: bytes}, manifest)."""
    patchset = check_toolkit(repo)
    files = collect(repo, RULES + (RULES_LINUX if platform == "linux" else RULES_WINDOWS))
    if launcher:
        if not os.path.isfile(launcher):
            raise Refused(f"no launcher at {launcher} (tools/release/build_launcher.py)")
        files["Project Vekta.exe"] = launcher
    problems = [f"{rel}: {why}" for rel, src in sorted(files.items()) if (why := scrub(rel, src))]
    m = fetch_toolchain.load(os.path.join(repo, "tools", "release", "toolchain.json"))
    tfiles, done = collect_toolchain(cache, m, platform)
    problems += [f"{rel}: {why}" for rel, src in sorted(tfiles.items())
                 if (why := scrub(rel, src, toolchain=True, elf_ok=platform == "linux"))]
    problems += [f"case clash: {c}" for c in case_clashes(list(files) + list(tfiles))]
    if problems:
        raise Refused("scrub:\n  " + "\n  ".join(problems))
    files.update(tfiles)
    gen = {}
    if platform == "windows":
        pth = pth_name(files)
        gen[pth] = pth_text(files.pop(pth)).encode()
    gen[f"{CACHE}/fetched.json"] = (json.dumps(done, indent=1, sort_keys=True) + "\n").encode()
    with open(os.path.join(repo, "VERSION"), encoding="utf-8") as f:
        version = f.read().strip()
    entries = []
    for rel in sorted(set(files) | set(gen)):
        if rel in gen:
            entries.append({"path": rel, "size": len(gen[rel]), "sha256": hashlib.sha256(gen[rel]).hexdigest()})
        else:
            entries.append({"path": rel, "size": os.path.getsize(files[rel]), "sha256": sha256(files[rel])})
    manifest = {"format": 1, "version": version, "platform": platform, "commit": git_commit(repo),
                "patchset": patchset,
                "toolchain": [{"name": e["name"], "version": e["version"], "url": fetch_toolchain.expand(e)["url"],
                               "sha256": e["sha256"], "license": e["license"]}
                              for e in (linux_entries(m) if platform == "linux" else windows_entries(m))],
                "count": len(entries), "bytes": sum(e["size"] for e in entries), "files": entries}
    return files, gen, manifest


def write_payload(out, files, gen, manifest, replace):
    if os.path.exists(out):
        if not replace:
            raise Refused(f"{out} exists (--replace to write it again)")
        shutil.rmtree(out)
    for rel, src in files.items():
        dst = os.path.join(out, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
    for rel, data in gen.items():
        dst = os.path.join(out, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(data)
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")


def write_zip(path, out, top):
    """A zip of the payload under top/, in name order, with fixed times: the same payload gives the same bytes."""
    names = sorted(os.path.relpath(os.path.join(d, f), out).replace(os.sep, "/")
                   for d, _, fs in os.walk(out) for f in fs)
    tmp = path + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for rel in names:
            info = zipfile.ZipInfo(f"{top}/{rel}", date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(os.path.join(out, *rel.split("/")), "rb") as f:
                z.writestr(info, f.read())
    os.replace(tmp, path)


def write_tar(path, out, top):
    """A tar.gz of the payload under top/, in name order, with fixed times and owners and the files' own execute
    bits (the Linux folder's programs and scripts must run): the same payload gives the same bytes."""
    names = sorted(os.path.relpath(os.path.join(d, f), out).replace(os.sep, "/")
                   for d, _, fs in os.walk(out) for f in fs)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=6) as gz, \
            tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as t:
        for rel in names:
            src = os.path.join(out, *rel.split("/"))
            info = tarfile.TarInfo(f"{top}/{rel}")
            info.size = os.path.getsize(src)
            info.mtime = 1767225600   # 2026-01-01
            info.mode = 0o755 if os.stat(src).st_mode & 0o111 else 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            with open(src, "rb") as f:
                t.addfile(info, f)
    os.replace(tmp, path)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out")
    ap.add_argument("--repo", default=ROOT)
    ap.add_argument("--cache", help="default: <repo>/work/toolchain-cache")
    ap.add_argument("--replace", action="store_true")
    ap.add_argument("--launcher", help="the launcher exe to ship as Project Vekta.exe (build_launcher.py)")
    ap.add_argument("--zip", help="also write the portable zip (its top folder: project-vekta-<version>)")
    ap.add_argument("--platform", choices=("windows", "linux"), default="windows",
                    help="the Windows folder (default) or the Linux one (the pinned Zig toolchain, run.sh, play.sh)")
    ap.add_argument("--tar", help="also write the folder as a .tar.gz with its execute bits (the Linux folder)")
    ap.add_argument("--dry-run", action="store_true", help="check and list, write nothing")
    a = ap.parse_args(argv)
    if not a.out and not a.dry_run:
        ap.error("--out DIR (or --dry-run)")
    cache = a.cache or os.path.join(a.repo, *CACHE.split("/"))
    try:
        if a.platform == "linux" and (a.launcher or a.zip):
            raise Refused("--launcher and --zip are for the Windows folder (the Linux one ships as --tar)")
        files, gen, manifest = plan(a.repo, cache, a.launcher, a.platform)
        if a.dry_run:
            print(f"payload ok: {manifest['count']} files, {manifest['bytes'] / 1e6:.1f} MB "
                  f"(version {manifest['version']}, patch set {manifest['patchset']})")
            return 0
        write_payload(a.out, files, gen, manifest, a.replace)
        if a.zip:
            write_zip(a.zip, a.out, f"project-vekta-{manifest['version']}")
        if a.tar:
            write_tar(a.tar, a.out, f"project-vekta-{manifest['version']}-linux")
    except Refused as err:
        print(f"assemble_payload: refused: {err}", file=sys.stderr)
        return 1
    print(f"payload: {a.out} ({manifest['count']} files, {manifest['bytes'] / 1e6:.1f} MB)"
          + (f"; zip: {a.zip}" if a.zip else "") + (f"; tar: {a.tar}" if a.tar else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
