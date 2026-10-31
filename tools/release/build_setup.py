#!/usr/bin/env python3
"""Compile the Windows setup program from a payload folder with Inno Setup (installer/vekta.iss):

    tools/release/build_setup.py --payload DIR [--out DIR] [--iscc ISCC.exe]

ISCC: --iscc, else $ISCC, else the pinned Inno Setup (toolchain.json build_tools: the official installer, checked
against its SHA-256 and installed silently into work/release/innosetup on the first run). On Linux it runs under Wine
(WINEPREFIX as set; `wine` and `winepath` on PATH), as Project Pyrrhus's and Project Helghast's tools compile theirs
under Proton. Output: <out>/project-vekta-<version>-setup.exe (default out: work/release) and its .sha256 ("HASH  NAME",
LF), printed at the end. Exit 0 = built, 1 = failed. Standard library only.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import fetch_toolchain  # noqa: E402
import release_version  # noqa: E402


def wine(argv):
    return argv if os.name == "nt" else ["wine", *argv]


if os.name != "nt":
    # Inno Setup's own install must not reach the host's applications menu or desktop (winemenubuilder).
    os.environ["WINEDLLOVERRIDES"] = ";".join(filter(None, [os.environ.get("WINEDLLOVERRIDES"),
                                                            "winemenubuilder.exe=d"]))


def winpath(p):
    """A host path as the Windows programs see it (winepath on Linux)."""
    if os.name == "nt":
        return os.path.abspath(p)
    return subprocess.run(["winepath", "-w", os.path.abspath(p)], capture_output=True, text=True,
                          check=True).stdout.strip()


def pinned_iscc(work, cache):
    """ISCC.exe of the pinned Inno Setup, installed into work/release/innosetup when it is not there yet."""
    with open(fetch_toolchain.MANIFEST, encoding="utf-8") as f:
        pin = next(t for t in json.load(f)["build_tools"] if t["name"] == "innosetup")
    dest = os.path.join(work, "release", "innosetup")
    iscc = os.path.join(dest, "ISCC.exe")
    stamp = os.path.join(dest, "pinned.txt")
    try:
        with open(stamp, encoding="utf-8") as f:
            if f.read().strip() == pin["sha256"] and os.path.isfile(iscc):
                return iscc
    except OSError:
        pass
    url = pin["url"].replace("{version}", pin["version"])
    dl = os.path.join(cache, "downloads", os.path.basename(url))
    os.makedirs(os.path.dirname(dl), exist_ok=True)
    if not (os.path.isfile(dl) and fetch_toolchain.sha256_file(dl) == pin["sha256"]):
        print(f"innosetup: downloading {url}")
        fetch_toolchain.download(url, dl)
    got = fetch_toolchain.sha256_file(dl)
    if got != pin["sha256"]:
        os.remove(dl)
        raise ValueError(f"innosetup: the download's sha256 is {got}, the pin is {pin['sha256']}; deleted it")
    subprocess.run(wine([dl, "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
                         f"/DIR={winpath(dest)}"]), check=True)
    if not os.path.isfile(iscc):
        raise ValueError(f"innosetup: installed, but no {iscc}")
    with open(stamp, "w", encoding="utf-8") as f:
        f.write(pin["sha256"] + "\n")
    return iscc


def main(argv=None):
    work = os.environ.get("KZ_WORK") or os.path.join(ROOT, "work")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--payload", required=True)
    ap.add_argument("--out", default=os.path.join(work, "release"))
    ap.add_argument("--iscc")
    ap.add_argument("--cache", default=os.path.join(work, "toolchain-cache"))
    a = ap.parse_args(argv)
    try:
        version = release_version.read(ROOT)
        with open(os.path.join(a.payload, "VERSION"), encoding="utf-8") as f:
            if f.read().strip() != version:
                raise ValueError(f"the payload's VERSION is not {version}: assemble it again")
        if os.name != "nt":
            # Wine shows a name that starts with a dot as hidden, and ISCC's wildcards skip hidden files.
            subprocess.run(["wine", "reg", "add", "HKCU\\Software\\Wine", "/v", "ShowDotFiles", "/d", "Y", "/f"],
                           check=True, stdout=subprocess.DEVNULL)
        iscc = a.iscc or os.environ.get("ISCC") or pinned_iscc(work, a.cache)
        os.makedirs(a.out, exist_ok=True)
        subprocess.run(wine([iscc, "/Q", f"/DPayload={winpath(a.payload)}", f"/O{winpath(a.out)}",
                             winpath(os.path.join(ROOT, "installer", "vekta.iss"))]), check=True)
    except (ValueError, OSError, subprocess.CalledProcessError) as err:
        print(f"build_setup: {err}", file=sys.stderr)
        return 1
    name = f"project-vekta-{version}-setup.exe"
    exe = os.path.join(a.out, name)
    digest = fetch_toolchain.sha256_file(exe)
    with open(exe + ".sha256", "w", encoding="utf-8", newline="\n") as f:
        f.write(f"{digest}  {name}\n")
    print(f"setup: {exe} ({os.path.getsize(exe) / 1e6:.1f} MB)\n{digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
