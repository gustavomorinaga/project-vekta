#!/usr/bin/env python3
"""Stage the toolchain downloads this repository serves from its own GitHub releases (tools/release/toolchain.json
entries with an upstream_url), for the deps-mirror workflow to publish.

    tools/release/mirror_deps.py --names NAME[,NAME] --out DIR [--cache DIR] [--offline]

For each named entry: the file at its upstream_url (or the one already in <cache>/downloads), refused unless its
SHA-256 is the entry's pin, copied to <out>/<the url's file name> with a <file>.sha256 beside it (sha256sum format).
All the names must share one release tag (the url's path after /releases/download/), which is printed last as
`tag=<tag>` (the workflow appends it to $GITHUB_OUTPUT). The release then serves the very bytes the maintainer
tested, under a name that never changes: a re-pin is a new tag, and an asset is never replaced.

Why: BtbN/FFmpeg-Builds removes most dated builds after a while, which once left a release unbuildable (0.5.0).
Exit 0 = staged, 1 = a check failed, 2 = usage. Standard library only, Python 3.8+.
"""
import argparse
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fetch_toolchain as ft  # noqa: E402


def release_tag(url):
    """'deps-ffmpeg-x' from https://github.com/<owner>/<repo>/releases/download/deps-ffmpeg-x/<file>."""
    if not url.startswith(ft.OWN_RELEASES):
        raise ValueError(f"{url}: not under {ft.OWN_RELEASES}")
    parts = url[len(ft.OWN_RELEASES):].split("/")
    if len(parts) != 2 or not parts[0].startswith("deps-") or not parts[1]:
        raise ValueError(f"{url}: expected {ft.OWN_RELEASES}deps-<name>/<file>")
    return parts[0]


def stage(manifest, names, out_dir, cache, offline=False, opener=ft.open_url, out=sys.stdout):
    """Stage the named entries into out_dir; return the shared release tag."""
    if ft.validate(manifest, strict=True):
        raise ValueError("toolchain.json: " + "; ".join(ft.validate(manifest, strict=True)))
    by = {e["name"]: e for e in manifest["components"]}
    tags = set()
    os.makedirs(out_dir, exist_ok=True)
    dl_dir = os.path.join(cache, "downloads")
    os.makedirs(dl_dir, exist_ok=True)
    for name in names:
        raw = by.get(name)
        if raw is None or "upstream_url" not in raw:
            raise ValueError(f"{name}: not a toolchain.json entry with an upstream_url")
        e = ft.expand(raw)
        up = raw["upstream_url"].format(**{k: v for k, v in raw.items() if isinstance(v, str)})
        tags.add(release_tag(e["url"]))
        fname = e["url"].rsplit("/", 1)[1]
        dl = os.path.join(dl_dir, fname)
        if not (os.path.isfile(dl) and ft.sha256_file(dl) == e["sha256"]):
            if offline:
                raise ValueError(f"{name}: {fname} is not in {dl_dir} with the pinned hash (--offline)")
            print(f"{name}: downloading {up}", file=out)
            ft.download(up, dl, opener)
        got = ft.sha256_file(dl)
        if got != e["sha256"]:
            os.remove(dl)
            raise ValueError(f"{name}: {up} has SHA-256 {got}, the pin is {e['sha256']}; nothing staged")
        shutil.copyfile(dl, os.path.join(out_dir, fname))
        with open(os.path.join(out_dir, fname + ".sha256"), "w", encoding="utf-8", newline="\n") as f:
            f.write(f"{got}  {fname}\n")
        print(f"{name}: {fname} {got}", file=out)
    if len(tags) != 1:
        raise ValueError(f"the names must share one release tag, they have {sorted(tags) or 'none'}")
    return tags.pop()


def main(argv=None, out=sys.stdout, opener=ft.open_url):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--names", required=True, help="comma-separated toolchain.json entry names")
    ap.add_argument("--out", required=True)
    ap.add_argument("--cache", default=os.path.join(ft.ROOT, "work", "toolchain-cache"))
    ap.add_argument("--offline", action="store_true")
    ap.add_argument("--manifest", default=ft.MANIFEST)
    a = ap.parse_args(argv)
    names = [n for n in a.names.split(",") if n]
    if not names:
        ap.error("--names is empty")
    try:
        tag = stage(ft.load(a.manifest), names, a.out, a.cache, a.offline, opener, out)
    except (ValueError, OSError) as ex:
        print(f"mirror_deps: {ex}", file=sys.stderr)
        return 1
    print(f"tag={tag}", file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
