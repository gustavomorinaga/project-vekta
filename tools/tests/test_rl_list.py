#!/usr/bin/env python3
"""Host test for tools/release/rl_list.py (RecompLauncher's hosted list from the player's TRP and rl_list_meta.json):
the committed table follows SPEC.md and has no text; a synthetic TRP (no game text) gives the list with the
titles, the platinum left out and the table's points, type and bonus; a TRP and a table that disagree are refused."""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
import rl_list  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


def refused(trp, meta):
    try:
        rl_list.build(trp, meta)
    except ValueError:
        return True
    return False


def trp(entries):
    """A TRP as the game's: magic, count at +0x10, entry size 0x40, then name/offset/size per entry."""
    head = 0x40 + 0x40 * len(entries)
    out, body = bytearray(head), bytearray()
    struct.pack_into(">IIQII", out, 0, 0xDCA24D00, 1, 0, len(entries), 0x40)
    for i, (name, data) in enumerate(entries):
        struct.pack_into(">32sQQ", out, 0x40 + 0x40 * i, name.encode(), head + len(body), len(data))
        body += data
    return bytes(out + body)


def sfm(rows, conf):
    body = "".join(f'<trophy id="{i:03d}" hidden="no" ttype="{g}" pid="000"></trophy>\n' if conf else
                   f'<trophy id="{i:03d}">\n  <name> {n} </name>\n  <detail>{d}</detail>\n</trophy>\n'
                   for i, g, n, d in rows)
    return ('<!--Sce-Np-Trophy-Signature: 00-->\n<?xml version="1.0" encoding="utf-8"?>\n<trophyconf>\n'
            + body + "</trophyconf>\n").encode()


ROWS = [(0, "P", "All", "Every one"), (1, "B", "One &amp; only", "Do one"), (2, "S", "Two", "Do two"),
        (3, "G", "Três", "Do three")]
GOOD = trp([("TROPCONF.SFM", sfm(ROWS, True)), ("TROP.SFM", sfm(ROWS, False)),
            ("TROP_02.SFM", sfm([(i, g, "fr", "fr") for i, g, _, _ in ROWS], False)), ("ICON0.PNG", b"\x89PNG\0\1")])
META = {"format": 1, "game": "t", "source": "native", "console": "ps3", "versions": ">=1.0.0", "voids": [],
        "achievements": [{"id": 1, "points": 5, "type": "win"}, {"id": 2, "points": 10},
                         {"id": 3, "points": 50, "bonus": True}]}

# The committed table.
meta = json.loads(rl_list.META.read_text())
rows = meta["achievements"]
check([r["id"] for r in rows] == list(range(1, 59)), "table: ids 1..58, the platinum (0) left out")
check(all(set(r) <= rl_list.KEYS and r["points"] in rl_list.POINTS for r in rows),
      "table: only id/points/type/bonus, points in 5/10/25/50 (no titles or descriptions)")
check({r.get("type") for r in rows} <= rl_list.TYPES | {None} and any(r.get("type") == "win" for r in rows),
      "table: types are SPEC.md's, and there is a win")
check(all(meta[k] == v for k, v in {"format": 1, "source": "native", "console": "ps3"}.items()),
      "table: format 1, native, ps3")

# A synthetic TRP.
doc = rl_list.build(GOOD, META)
a = doc["achievements"]
check([x["id"] for x in a] == [1, 2, 3], "list: the TRP's trophies but the platinum")
check(a[0] == {"id": 1, "title": "One & only", "description": "Do one", "points": 5, "type": "win"},
      f"list: English title unescaped and stripped, table's points and type: {a[0]}")
check(a[2]["title"] == "Três" and a[2]["points"] == 50 and a[2]["bonus"] is True and "type" not in a[1],
      "list: UTF-8 text, the table's points over the grade, bonus kept, no type when the table has none")
check({k: v for k, v in doc.items() if k != "achievements"} == {k: v for k, v in META.items() if k != "achievements"},
      "list: the table's header as it is")

check(refused(GOOD, {**META, "achievements": META["achievements"][:2]}), "refused: an id of the TRP missing")
check(refused(GOOD, {**META, "achievements": [{"id": 0, "points": 5}] + META["achievements"]}),
      "refused: the platinum in the table")
check(refused(GOOD, {**META, "achievements": [{"id": 1, "points": 15}] + META["achievements"][1:]}),
      "refused: points not 5/10/25/50")
check(refused(GOOD, {**META, "achievements": [{"id": 1, "points": 5, "type": "x"}] + META["achievements"][1:]}),
      "refused: an unknown type")
check(refused(GOOD, {**META, "achievements": [{"id": 1, "points": 5, "title": "x"}] + META["achievements"][1:]}),
      "refused: text in the table")
check(refused(b"\0" * 0x40, META), "refused: not a TRP")
check(refused(trp([("TROPCONF.SFM", sfm(ROWS, True))]), META), "refused: no TROP.SFM")

# The command line, on the committed table: a TRP with ids 1..58 (and a platinum) gives 58 entries.
full = [(0, "P", "p", "p")] + [(i, "B", f"t{i}", f"d{i}") for i in range(1, 59)]
with tempfile.TemporaryDirectory() as d:
    t, o = pathlib.Path(d) / "TROPHY.TRP", pathlib.Path(d) / "out" / "list.json"
    t.write_bytes(trp([("TROPCONF.SFM", sfm(full, True)), ("TROP.SFM", sfm(full, False))]))
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "release" / "rl_list.py"), "--trp", str(t), "--out", str(o)],
                       capture_output=True, text=True)
    out = json.loads(o.read_text()) if o.is_file() else {}
    check(r.returncode == 0 and len(out.get("achievements", [])) == 58 and out["achievements"][57]["title"] == "t58",
          f"command line: 58 entries written: {r.stdout}{r.stderr}")
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "release" / "rl_list.py"), "--trp", str(t / "x")],
                       capture_output=True, text=True)
    check(r.returncode != 0 and "rl_list:" in r.stderr, "command line: a missing TRP is an error")

print("test_rl_list: " + ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
