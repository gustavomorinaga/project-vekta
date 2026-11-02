#!/usr/bin/env python3
"""Check an installed (or unpacked) Windows release folder against the payload's manifest.json:

    tools/release/check_install.py DIR [--after-uninstall]

Every file manifest.json lists must be in DIR with its size and SHA-256, and DIR must hold nothing else but
manifest.json and the setup program's own files (unins000.exe, unins000.dat, vekta-install.ini). With --after-uninstall: DIR must be
gone, or hold only what the uninstaller keeps (work\\vfs, the saves and settings). release.yml runs it on the Windows
runner after a silent install and after the silent uninstall. Prints the problems, at most 20, and a summary line.
Exit 0 = ok, 1 = a problem, 2 = usage. Standard library only.
"""
import argparse
import hashlib
import json
import os
import sys

OWN = {"manifest.json", "unins000.exe", "unins000.dat", "vekta-install.ini"}   # the list, the uninstaller, [INI]
KEPT = "work/vfs/"                        # the uninstaller keeps the saves and settings ([UninstallDelete])


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def walk(d):
    out = []
    for top, _dirs, names in os.walk(d):
        for n in names:
            out.append(os.path.relpath(os.path.join(top, n), d).replace(os.sep, "/"))
    return sorted(out)


def check(d, manifest):
    problems = []
    listed = {e["path"]: e for e in manifest["files"]}
    for rel, e in listed.items():
        p = os.path.join(d, *rel.split("/"))
        if not os.path.isfile(p):
            problems.append(f"missing: {rel}")
        elif os.path.getsize(p) != e["size"]:
            problems.append(f"size {os.path.getsize(p)}, manifest {e['size']}: {rel}")
        elif sha256(p) != e["sha256"]:
            problems.append(f"sha256 differs from the manifest: {rel}")
    problems += [f"not in the payload: {rel}" for rel in walk(d) if rel not in listed and rel not in OWN]
    return problems


def check_uninstalled(d):
    if not os.path.isdir(d):
        return []
    return [f"left after uninstall: {rel}" for rel in walk(d) if not rel.startswith(KEPT)]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("dir")
    ap.add_argument("--manifest", help="default: DIR/manifest.json (the payload ships it)")
    ap.add_argument("--after-uninstall", action="store_true")
    a = ap.parse_args(argv)
    if a.after_uninstall:
        problems = check_uninstalled(a.dir)
        what = "uninstall"
    else:
        try:
            with open(a.manifest or os.path.join(a.dir, "manifest.json"), encoding="utf-8") as f:
                manifest = json.load(f)
        except (OSError, ValueError) as err:
            print(f"check_install: no manifest: {err}", file=sys.stderr)
            return 1
        problems = check(a.dir, manifest)
        what = f"install ({manifest['count']} files, version {manifest['version']})"
    for p in problems[:20]:
        print("  " + p)
    if len(problems) > 20:
        print(f"  ... and {len(problems) - 20} more")
    print(f"check_install: {what}: " + (f"{len(problems)} problem(s)" if problems else "ok"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
