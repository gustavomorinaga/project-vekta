#!/usr/bin/env python3
"""The source release: project-vekta-X.Y.Z-source.tar.gz and its .sha256, from the files git tracks at HEAD, the
ps3recomp submodule at its pinned commit included (GitHub's own "Source code" archives leave the submodule out, and
the build needs it).

    python3 tools/release/pack_source.py --out DIR [--version X.Y.Z] [--root DIR]

Reproducible: the same commit gives the same bytes. Paths sorted, all under project-vekta-X.Y.Z/; every entry dated with
HEAD's commit time, owned by 0:0 with no names; modes 0755 for executables and 0644 for the rest; symbolic links kept;
gzip with no name and no time. It reads only what git tracks, so work/, build output and anything ignored stay out;
it refuses a tree with uncommitted changes to tracked files, and a --version that is not VERSION. One file is added:
third_party/ps3recomp.pin, the submodule's pinned commit, which tools/ps3recomp-patched.sh reads where there is no git. Nothing of the
game is in the tree (tools/check-tree.sh), so nothing of it is in the archive. Standard library and git only.
Exit 0 = written, 1 = refused, 2 = usage.
"""
import argparse
import gzip
import hashlib
import io
import os
import pathlib
import subprocess
import sys
import tarfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import release_version  # noqa: E402


def git(root, *args):
    return subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True).stdout


def name(version):
    return f"project-vekta-{version}-source.tar.gz"


def tracked(root):
    """Tracked paths at HEAD, the submodule's files included (not the submodule's own entry)."""
    out = git(root, "ls-files", "-z", "--recurse-submodules").decode("utf-8").split("\0")
    return sorted(p for p in out if p and not (root / p).is_dir())


def pack(root, version, out_dir):
    root = pathlib.Path(root)
    if git(root, "status", "--porcelain", "--untracked-files=no", "--ignore-submodules=none").strip():
        raise SystemExit("pack_source: tracked files have uncommitted changes; commit or stash them first")
    when = int(git(root, "log", "-1", "--format=%ct").strip())
    prefix = f"project-vekta-{version}/"
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for rel in tracked(root):
            p = root / rel
            ti = tarfile.TarInfo(prefix + rel)
            ti.mtime, ti.uid, ti.gid, ti.uname, ti.gname = when, 0, 0, "", ""
            if p.is_symlink():
                ti.type, ti.linkname, ti.mode = tarfile.SYMTYPE, os.readlink(p), 0o777
                tar.addfile(ti)
                continue
            data = p.read_bytes()
            ti.size, ti.mode = len(data), 0o755 if os.access(p, os.X_OK) else 0o644
            tar.addfile(ti, io.BytesIO(data))
        pin = git(root, "-C", "third_party/ps3recomp", "rev-parse", "HEAD").strip() + b"\n"
        ti = tarfile.TarInfo(prefix + "third_party/ps3recomp.pin")
        ti.mtime, ti.uid, ti.gid, ti.uname, ti.gname, ti.size, ti.mode = when, 0, 0, "", "", len(pin), 0o644
        tar.addfile(ti, io.BytesIO(pin))
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / name(version)
    with open(target, "wb") as f, gzip.GzipFile(filename="", mode="wb", fileobj=f, mtime=0, compresslevel=9) as gz:
        gz.write(raw.getvalue())
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    (out_dir / (target.name + ".sha256")).write_text(f"{digest}  {target.name}\n", encoding="utf-8", newline="\n")
    return target, digest


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True, help="folder for the archive and its .sha256")
    ap.add_argument("--version", default="", help="X.Y.Z; must be VERSION (default: VERSION)")
    ap.add_argument("--root", default=str(HERE.parents[1]), help="repository root")
    a = ap.parse_args(argv)
    try:
        v = release_version.read(a.root)
    except ValueError as e:
        print(f"pack_source: {e}", file=sys.stderr)
        return 1
    if a.version and a.version != v:
        print(f"pack_source: --version {a.version} is not VERSION ({v})", file=sys.stderr)
        return 1
    try:
        target, digest = pack(a.root, v, a.out)
    except subprocess.CalledProcessError as e:
        print(f"pack_source: git failed: {e.stderr.decode(errors='replace').strip()}", file=sys.stderr)
        return 1
    print(f"pack_source: {target} ({target.stat().st_size} bytes) sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
