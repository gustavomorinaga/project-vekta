#!/usr/bin/env python3
"""Host test for tools/release/check_install.py: an exact copy of a payload passes; a missing, changed or extra file
is reported; Inno Setup's uninstaller files are allowed; after uninstall only work/vfs may stay."""
import hashlib
import json
import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
import check_install  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


with tempfile.TemporaryDirectory() as tmp:
    d = pathlib.Path(tmp) / "app"
    files = {"VERSION": b"0.2.0\n", "tools/kzbuild.py": b"print()\n", "work/vfs/kz1hd_pc.ini": b"[pc]\n"}
    entries = []
    for rel, data in files.items():
        (d / rel).parent.mkdir(parents=True, exist_ok=True)
        (d / rel).write_bytes(data)
        entries.append({"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    m = {"count": len(entries), "version": "0.2.0", "files": entries}
    (d / "manifest.json").write_text(json.dumps(m))
    (d / "unins000.exe").write_bytes(b"MZ")
    (d / "work" / "dlss-choice").write_text("decline\n")
    check(check_install.main([str(d)]) == 0, "an exact install passes, with the setup's own files")
    (d / "extra.txt").write_text("x")
    check(check_install.check(str(d), m) == ["not in the payload: extra.txt"], "an extra file is reported")
    (d / "extra.txt").unlink()
    (d / "VERSION").write_bytes(b"0.2.1\n")
    check(any("sha256" in p for p in check_install.check(str(d), m)), "a changed file is reported")
    (d / "tools/kzbuild.py").unlink()
    check("missing: tools/kzbuild.py" in check_install.check(str(d), m), "a missing file is reported")
    for rel in ("VERSION", "manifest.json", "unins000.exe", "work/dlss-choice", "tools"):
        p = d / rel
        p.rmdir() if p.is_dir() else p.unlink()
    check(check_install.main([str(d), "--after-uninstall"]) == 0, "after uninstall, work/vfs may stay")
    (d / "work" / "lift").mkdir()
    (d / "work" / "lift" / "a.cpp").write_text("")
    check(check_install.check_uninstalled(str(d)) == ["left after uninstall: work/lift/a.cpp"],
          "a build leftover after uninstall is reported")
    check(check_install.main([str(pathlib.Path(tmp) / "gone"), "--after-uninstall"]) == 0, "a removed folder passes")

print("test_check_install:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
