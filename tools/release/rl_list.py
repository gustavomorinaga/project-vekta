#!/usr/bin/env python3
"""RecompLauncher's hosted achievement list (Recomp Achievements, third_party/rl_achievements/docs/SPEC.md) for
Killzone HD, made from the player's own TROPHY.TRP and rl_list_meta.json.

The repository keeps only rl_list_meta.json: the list's header and each trophy's points, type and bonus. The titles
and descriptions are game text, so they are read from the TRP (TROP.SFM, the English names) at the time the list is
made and the result goes to work/ (gitignored). docs/dev/trophies.md, stage 4, says where to paste it.

  python3 tools/release/rl_list.py [--trp TROPHY.TRP] [--out FILE]
"""
import argparse
import html
import json
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
META = pathlib.Path(__file__).with_name("rl_list_meta.json")
TRP = ROOT / "work" / "vfs" / "PS3_GAME" / "TROPDIR" / "NPWR03819_00" / "TROPHY.TRP"
OUT = ROOT / "work" / "trophies" / "recomplauncher-list.json"
POINTS = {5, 10, 25, 50}
TYPES = {"progression", "win", "missable"}
KEYS = {"id", "points", "type", "bonus"}


def trp_files(data):
    """The TRP's .SFM entries (XML text) by name: magic DCA24D00, entry count at +0x10, entry size at +0x14, entries from +0x40
    (name, 32 bytes; offset and size, u64 big-endian, at +0x20 and +0x28)."""
    if len(data) < 0x40 or struct.unpack_from(">I", data)[0] != 0xDCA24D00:
        raise ValueError("not a TRP (magic)")
    n, size = struct.unpack_from(">II", data, 0x10)
    files = {}
    for i in range(n):
        p = 0x40 + i * size
        if size < 0x30 or p + 0x30 > len(data):
            raise ValueError(f"TRP entry {i} outside the file")
        name = data[p:p + 0x20].split(b"\0")[0].decode("ascii")
        off, ln = struct.unpack_from(">QQ", data, p + 0x20)
        if off + ln > len(data):
            raise ValueError(f"TRP entry {name} outside the file")
        if name.endswith(".SFM"):
            files[name] = data[off:off + ln].decode("utf-8")
    return files


def text(s):
    return html.unescape(s).strip()


def trophies(files):
    """{id: (grade, title, description)}: the grade (B, S, G, P) from TROPCONF.SFM, the text from TROP.SFM."""
    for need in ("TROPCONF.SFM", "TROP.SFM"):
        if need not in files:
            raise ValueError(f"TRP has no {need}")
    grade = {int(i): g for i, g in re.findall(r'<trophy\s+id="(\d+)"[^>]*?\sttype="(\w)"', files["TROPCONF.SFM"])}
    names = {int(i): (text(n), text(d)) for i, n, d in re.findall(
        r'<trophy\s+id="(\d+)"[^>]*>\s*<name>(.*?)</name>\s*<detail>(.*?)</detail>', files["TROP.SFM"], re.S)}
    if not grade or set(grade) != set(names):
        raise ValueError("TROPCONF.SFM and TROP.SFM list different trophies")
    return {i: (grade[i], *names[i]) for i in sorted(grade)}


def build(trp_data, meta):
    """The list: meta's header, then each meta row with the TRP's title and description. Raises ValueError when
    the two do not agree with each other or with SPEC.md."""
    got = trophies(trp_files(trp_data))
    rows = meta["achievements"]
    ids = [r["id"] for r in rows]
    want = [i for i, (g, _, _) in got.items() if g != "P"]
    if ids != want:
        raise ValueError(f"meta ids {ids} are not the TRP's non-platinum trophies {want}")
    out = {k: v for k, v in meta.items() if k != "achievements"}
    if out.get("format") != 1 or out.get("source") != "native" or out.get("console") != "ps3":
        raise ValueError("header is not format 1, native, ps3")
    out["achievements"] = []
    for r in rows:
        if set(r) - KEYS or r["points"] not in POINTS or r.get("type", "progression") not in TYPES \
                or r.get("bonus", True) is not True:
            raise ValueError(f"meta row {r} is not SPEC.md's")
        _, title, desc = got[r["id"]]
        a = {"id": r["id"], "title": title, "description": desc, "points": r["points"]}
        a.update({k: r[k] for k in ("type", "bonus") if k in r})
        out["achievements"].append(a)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("--trp", type=pathlib.Path, default=TRP, help=f"the player's TROPHY.TRP (default {TRP})")
    ap.add_argument("--out", type=pathlib.Path, default=OUT, help=f"where to write the list (default {OUT})")
    a = ap.parse_args(argv)
    try:
        doc = build(a.trp.read_bytes(), json.loads(META.read_text()))
    except (OSError, ValueError) as e:
        sys.exit(f"rl_list: {e}")
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"rl_list: {a.out}: {len(doc['achievements'])} achievements, "
          f"{sum(r['points'] for r in doc['achievements'])} points")


if __name__ == "__main__":
    main()
