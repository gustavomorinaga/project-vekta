#!/usr/bin/env python3
"""Fetch the pinned Windows toolchain and libraries (tools/release/toolchain.json) into work/toolchain-cache:
download, check SHA-256, unpack.

    tools/release/fetch_toolchain.py [--host linux|windows|all] [--only NAME[,NAME]] [--cache DIR] [--fill | --offline]
    tools/release/fetch_toolchain.py --check [--strict]

After Project Pyrrhus's and Project Helghast's tools/release/fetch_toolchain.py (undecuple, MIT): the manifest's
TO-FILL / TO-CONFIRM rules, the refusal of a download whose hash is not the pin, and the zip member checks are
theirs; ours also unpacks .tar.gz and .tar.xz (SDL2's MinGW package, sources, the Linux-hosted llvm-mingw) and picks
the entries by host.

  --host     the entries for the cross build on Linux (linux, the default on Linux), for the Windows-hosted build
             (windows, the default on Windows), or all of them; "any" entries come with each
  --cache    default work/toolchain-cache: downloads/<file>, <name>/ (the unpacked tree), fetched.json
  --fill     an entry whose sha256 is TO-FILL is fetched and its hash written into the manifest (trust on first fetch:
             compare it with the release page's published hash where there is one). Without it, TO-FILL is refused.
  --offline  only downloads already in downloads/ whose hash is the pin; never the network
  --check    validate the manifest only (no network); --strict also refuses TO-FILL hashes and TO-CONFIRM pins

A tree already unpacked from the pinned hash is kept (fetched.json says which). A download whose hash is not the
pin is deleted and refused. Archive members with absolute paths, "..", or (in a zip) symlink modes are refused, and a
tar's links must stay inside the tree. Prints `<name> <folder>` for each entry. Exit 0 = ok, 1 = a check failed,
2 = usage. Standard library only, Python 3.8+.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import sys
import tarfile
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MANIFEST = os.path.join(HERE, "toolchain.json")
TO_FILL, TO_CONFIRM = "TO-FILL", "TO-CONFIRM"
HEX64 = re.compile(r"[0-9a-f]{64}")
NAME_RX = re.compile(r"[a-z0-9][a-z0-9.-]*")
HOSTS = ("linux", "windows", "any")
ARCHIVES = ("zip", "tar.gz", "tar.xz")
REQUIRED = ("name", "host", "version", "pin", "release_page", "url", "sha256", "archive", "license")
MAX_DOWNLOAD = 1 << 30


def load(path=MANIFEST):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def expand(e):
    """The entry with '{version}' (and any other of its own string fields) filled into its url."""
    e = dict(e)
    if TO_CONFIRM not in e["url"]:
        e["url"] = e["url"].format(**{k: v for k, v in e.items() if isinstance(v, str)})
    return e


def validate(m, strict=False):
    """Problems with the manifest, as strings: [] when it is sound."""
    out, seen = [], set()
    if m.get("format") != 1:
        out.append("format must be 1")
    for e in m.get("components", []):
        name = e.get("name", "?")
        miss = [k for k in REQUIRED if k not in e]
        if miss:
            out.append(f"{name}: missing {', '.join(miss)}")
            continue
        if not NAME_RX.fullmatch(name) or name in seen:
            out.append(f"{name}: bad or repeated name")
        seen.add(name)
        if e["host"] not in HOSTS:
            out.append(f"{name}: host must be one of {', '.join(HOSTS)}")
        if e["archive"] not in ARCHIVES:
            out.append(f"{name}: archive must be one of {', '.join(ARCHIVES)}")
        if e["sha256"] != TO_FILL and not HEX64.fullmatch(e["sha256"]):
            out.append(f"{name}: sha256 is neither 64 hex digits nor {TO_FILL}")
        if TO_CONFIRM not in e["url"] and not e["url"].startswith("https://"):
            out.append(f"{name}: url must be https")
        if strict and e["sha256"] == TO_FILL:
            out.append(f"{name}: sha256 is {TO_FILL}")
        if strict and TO_CONFIRM in (e["pin"], e["version"], e["url"]):
            out.append(f"{name}: pin is {TO_CONFIRM}")
    return out


def entries_for(m, host, only=None):
    hosts = {"linux": ("linux", "any"), "windows": ("windows", "any"), "all": HOSTS}[host]
    es = [e for e in m["components"] if e["host"] in hosts]
    if only:
        es = [e for e in m["components"] if e["name"] in only]
    return es


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 22), b""):
            h.update(block)
    return h.hexdigest()


def open_url(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "project-vekta-fetch"}),
                                  timeout=60)


def download(url, dest, opener=open_url):
    tmp = dest + ".part"
    n = 0
    with opener(url) as r, open(tmp, "wb") as f:
        while True:
            block = r.read(1 << 20)
            if not block:
                break
            n += len(block)
            if n > MAX_DOWNLOAD:
                raise ValueError(f"{url}: larger than {MAX_DOWNLOAD >> 20} MiB")
            f.write(block)
    os.replace(tmp, dest)


def safe_rel(name):
    """The member's path inside the tree, or None when it would leave it."""
    p = name.replace("\\", "/")
    if p.startswith("/") or re.match(r"^[A-Za-z]:", p):
        return None
    parts = [x for x in p.split("/") if x not in ("", ".")]
    if any(x == ".." for x in parts):
        return None
    return "/".join(parts)


def strip(rel, top):
    if not top:
        return rel
    parts = rel.split("/", 1)
    return parts[1] if len(parts) == 2 else ""


def unpack(path, kind, out, strip_top):
    """Unpack into out (which must not exist yet); refuses unsafe members."""
    os.makedirs(out)
    if kind == "zip":
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                rel = safe_rel(info.filename)
                if rel is None or stat.S_ISLNK(info.external_attr >> 16):
                    raise ValueError(f"{os.path.basename(path)}: unsafe member {info.filename!r}")
                rel = strip(rel, strip_top)
                if not rel:
                    continue
                dest = os.path.join(out, *rel.split("/"))
                if info.is_dir():
                    os.makedirs(dest, exist_ok=True)
                    continue
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with z.open(info) as src, open(dest, "wb") as f:
                    shutil.copyfileobj(src, f)
                mode = (info.external_attr >> 16) & 0o777
                if mode:
                    os.chmod(dest, mode | 0o600)
        return
    with tarfile.open(path, "r:" + kind.split(".")[1]) as t:
        links = []
        for m in t.getmembers():
            rel = safe_rel(m.name)
            if rel is None:
                raise ValueError(f"{os.path.basename(path)}: unsafe member {m.name!r}")
            rel = strip(rel, strip_top)
            if not rel:
                continue
            dest = os.path.join(out, *rel.split("/"))
            if m.isdir():
                os.makedirs(dest, exist_ok=True)
            elif m.isfile():
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with t.extractfile(m) as src, open(dest, "wb") as f:
                    shutil.copyfileobj(src, f)
                os.chmod(dest, (m.mode & 0o777) | 0o600)
            elif m.issym() or m.islnk():
                target = m.linkname if m.issym() else strip(safe_rel(m.linkname) or "", strip_top)
                base = os.path.dirname(rel) if m.issym() else ""
                inside = safe_rel(os.path.normpath(os.path.join(base, target)).replace(os.sep, "/"))
                if inside is None or os.path.isabs(target):
                    raise ValueError(f"{os.path.basename(path)}: link {m.name!r} leaves the tree")
                links.append((dest, target, m.issym(), inside))
        for dest, target, sym, inside in links:   # after the files, so a hard link's target exists
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            if sym:
                os.symlink(target, dest)
            else:
                shutil.copy2(os.path.join(out, *inside.split("/")), dest)


def fill_hash(manifest_path, name, digest):
    """Write the hash for one entry into the manifest, keeping the rest of the file as it is."""
    with open(manifest_path, encoding="utf-8") as f:
        text = f.read()
    rx = re.compile(r'("name": "' + re.escape(name) + r'",(?:(?!"name":)[\s\S])*?"sha256": ")' + TO_FILL + '"')
    new, n = rx.subn(lambda mm: mm.group(1) + digest + '"', text, count=1)
    if n != 1:
        raise ValueError(f"{name}: no {TO_FILL} sha256 to fill in {manifest_path}")
    with open(manifest_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(new)


def fetch_one(raw, cache, fill=False, offline=False, manifest_path=MANIFEST, opener=open_url, out=sys.stdout):
    """Fetch, check and unpack one entry; returns its folder. Raises ValueError with the reason."""
    e = expand(raw)
    name, want = e["name"], e["sha256"]
    if TO_CONFIRM in (e["url"], e["version"]):
        raise ValueError(f"{name}: its version or url is still {TO_CONFIRM} (see {e['release_page']})")
    if want == TO_FILL and not fill:
        raise ValueError(f"{name}: sha256 is {TO_FILL}; fetch it once with --fill and check the hash")
    tree = os.path.join(cache, name)
    done = read_fetched(cache)
    if want != TO_FILL and done.get(name) == want and os.path.isdir(tree):
        return tree
    dl_dir = os.path.join(cache, "downloads")
    os.makedirs(dl_dir, exist_ok=True)
    dl = os.path.join(dl_dir, os.path.basename(e["url"].split("?")[0]))
    if not (os.path.isfile(dl) and want != TO_FILL and sha256_file(dl) == want):
        if offline:
            raise ValueError(f"{name}: --offline and no download with the pinned hash at {dl}")
        print(f"{name}: downloading {e['url']}", file=out)
        download(e["url"], dl, opener)
    got = sha256_file(dl)
    if want == TO_FILL:
        fill_hash(manifest_path, name, got)
        print(f"{name}: sha256 {got} written to {os.path.relpath(manifest_path, ROOT)} (check it against "
              f"{e['release_page']})", file=out)
    elif got != want:
        os.remove(dl)
        raise ValueError(f"{name}: the download's sha256 is {got}, the pin is {want}; deleted it")
    if os.path.lexists(tree):
        shutil.rmtree(tree)
    unpack(dl, e["archive"], tree, e.get("strip_top", False))
    done[name] = got
    write_fetched(cache, done)
    return tree


def read_fetched(cache):
    try:
        with open(os.path.join(cache, "fetched.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def write_fetched(cache, data):
    os.makedirs(cache, exist_ok=True)
    tmp = os.path.join(cache, "fetched.json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, sort_keys=True)
    os.replace(tmp, os.path.join(cache, "fetched.json"))


def main(argv=None, out=sys.stdout, opener=open_url):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--manifest", default=MANIFEST)
    ap.add_argument("--cache", default=os.path.join(os.environ.get("KZ_WORK") or os.path.join(ROOT, "work"),
                                                    "toolchain-cache"))
    ap.add_argument("--host", choices=("linux", "windows", "all"), default="windows" if os.name == "nt" else "linux")
    ap.add_argument("--only", help="comma-separated entry names (any host)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--fill", action="store_true", help="fetch TO-FILL entries and write their hash")
    g.add_argument("--offline", action="store_true", help="never the network")
    ap.add_argument("--check", action="store_true", help="validate the manifest only")
    ap.add_argument("--strict", action="store_true", help="with --check: refuse TO-FILL and TO-CONFIRM")
    a = ap.parse_args(argv)
    try:
        m = load(a.manifest)
    except (OSError, ValueError) as e:
        print(f"fetch_toolchain: {a.manifest}: {e}", file=sys.stderr)
        return 2
    problems = validate(m, strict=a.strict)
    if a.check:
        for p in problems:
            print(f"fetch_toolchain: {p}", file=out)
        print(f"fetch_toolchain: manifest {'ok' if not problems else 'has problems'}", file=out)
        return 1 if problems else 0
    if problems:
        for p in problems:
            print(f"fetch_toolchain: {p}", file=sys.stderr)
        return 1
    only = set(a.only.split(",")) if a.only else None
    es = entries_for(m, a.host, only)
    if only and len(es) != len(only):
        print(f"fetch_toolchain: no entry named {', '.join(sorted(only - {e['name'] for e in es}))}", file=sys.stderr)
        return 2
    bad = 0
    for e in es:
        try:
            print(f"{e['name']} {fetch_one(e, a.cache, a.fill, a.offline, a.manifest, opener, out)}", file=out)
        except (OSError, ValueError) as err:
            print(f"fetch_toolchain: {err}", file=sys.stderr)
            bad = 1
    return bad


if __name__ == "__main__":
    sys.exit(main())
