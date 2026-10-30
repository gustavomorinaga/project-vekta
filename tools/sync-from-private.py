#!/usr/bin/env python3
"""Brings changes from the maintainer's private working repositories into this public tree, for review before a commit.

    tools/sync-from-private.py --private <repo> [--private-ref main] [--private-work <dir>]
                               --ps3recomp <repo> --ps3recomp-ref <branch> [--base v0.18.0] [--dry-run]

What it does, and nothing else (it never commits, pushes or touches the private repositories):

1. The port: copies `port/kz1hd/` of --private-ref over `port/kz1hd/` here, except what this tree keeps elsewhere
   (the build and run scripts now in `tools/`, the toolchain file now in `port/cmake/`), and copies new or changed files
   of the private `port/tools/` into `tools/`. Files deleted in the private tree are listed, not deleted.
2. The patches: `git format-patch <base>..<ps3recomp-ref>` of the private ps3recomp branch, matched to the patches here
   by their subject. A patch whose content changed replaces the file of the same number; a new patch takes the next
   free number in its area's block (from the paths it touches: 1xxx SPU and SPURS, 2xxx graphics, 3xxx lifter and system
   libraries, 4xxx input and file system, 5xxx data and trophies, 0xxx anything else) and is added at the end of
   `port/patches/series`; a patch that is gone is listed, not removed. Author lines take the public identity.
3. Lists the private docs that changed since --since (default: the date of this tree's last commit), which are written
   in Portuguese and need translating into `docs/` by hand.

Then run `tools/ps3recomp-patched.sh`, `tools/lift.sh`, `tools/build.sh` and `tools/check-tree.sh`, read `git diff`,
fix the paths and the language of anything new, and commit.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import io

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCHES = os.path.join(ROOT, "port", "patches")
SERIES = os.path.join(PATCHES, "series")
PUBLIC_AUTHOR = "From: Gustavo Morinaga <me@gustavomorinaga.dev>"
# port/kz1hd files this tree keeps somewhere else (or not at all).
SKIP_PORT = {"lift.sh", "run.sh", "play.sh", "pgo.sh", "build-win.sh", "install_movies.sh", "install_trophies.sh"}
SKIP_PORT_DIRS = ("cmake/",)
SKIP_TOOLS = set()
# The same rewrites this tree made when it was published, applied to every text file brought over: doc names after
# the translation, scripts that moved. Add a line when a private name changes.
REWRITES = [
    ("docs/recomp-ambiente.md", "docs/dev/port-journal.md"), ("docs/recomp-plan.md", "docs/dev/recomp-plan.md"),
    ("docs/migracao-v018.md", "docs/dev/ps3recomp-v018-migration.md"), ("docs/wws-jm-post.md", "docs/dev/wws-jm-post.md"),
    ("docs/desempenho.md", "docs/dev/performance.md"), ("docs/build-windows.md", "docs/dev/windows-build.md"),
    ("docs/referencias-upstream.md", "docs/dev/upstream-and-reference-ports.md"), ("docs/formats.md", "docs/dev/formats.md"),
    ("(see docs/dev/port-journal.md)", "(tools/lift.sh; docs/dev/port-journal.md)"),
    ("port/kz1hd/install_movies.sh extracts", "tools/setup-vfs.sh extracts"),
    ("(run.sh cds there)", "(tools/run.sh and play.sh cd there)"),
    ("port/kz1hd/build-win.sh", "tools/build-win.sh"), ("port/kz1hd/lift.sh", "tools/lift.sh"),
    ("port/kz1hd/run.sh", "tools/run.sh"), ("port/kz1hd/play.sh", "tools/play.sh"), ("port/kz1hd/pgo.sh", "tools/pgo.sh"),
    ("port/tools/", "tools/"),
    # Headings of docs/dev/wws-jm-post.md that the private code cites in Portuguese.
    ('"O MLAA em C"', '"The MLAA in C"'), ('"O MLAA fora do jogo"', '"The MLAA outside the game"'),
    ('"O MLAA fora do\n * jogo"', '"The MLAA outside\n * the game"'),
    ('"O MLAA medido no pr\u00f3prio m\u00f3dulo"', '"The MLAA measured on the game\'s own module"'),
    # The MLAA tools' defaults: work/ of this tree instead of the maintainer's folders; the private test kit by name.
    ("'{W}/recomp/kz1hd/mlaa_cap'",
     "os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'work', 'mlaa_cap')"),
    ("{W}/recomp/kz1hd/mlaa_cap", "$H/../../work/mlaa_cap"),
    ("{W}/recomp/ps3recomp-v018", "$H/../../work/ps3recomp-patched"),
    ("{W}/recomp-v018/kz1hd/spu", "$H/../../work/lift/spu"),
    ("T={W}/recomp/kz1hd/testkit",
     "T=${KZ_TESTKIT:?set KZ_TESTKIT to a test kit with b7.sh and a Level 7 save (not published)}"),
    ('$(cd "$(dirname "$0")/../../kz1hd" && pwd)/build-v018/KZ1HD', '${KZ_BIN:-$(cd "$(dirname "$0")/../.." && pwd)/work/build/KZ1HD}'),
    ("{P}/port/kz1hd/build-v018/KZ1HD", '${KZ_BIN:-$(cd "$(dirname "$0")/../.." && pwd)/work/build/KZ1HD}'),
    ('"$H/../../kz1hd/', '"$H/../../port/kz1hd/'),
    ('"../../kz1hd/native/', '"../../port/kz1hd/native/'),
]


CTX = {}   # {P}: the private repository, {W}: the maintainer's work folder (--private-work)


def rewrite(data):
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return data
    for a, b in REWRITES:
        if "{W}" in a and "W" not in CTX:
            continue
        text = text.replace(a.replace("{W}", CTX.get("W", "")).replace("{P}", CTX["P"]), b)
    return text.encode("utf-8")


def git(repo, *args, binary=False):
    out = subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True)
    return out.stdout if binary else out.stdout.decode()


def archive(repo, ref, path):
    """{relative path: bytes} for every file under path at ref."""
    data = git(repo, "archive", "--format=tar", ref, path, binary=True)
    files = {}
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        for m in tar.getmembers():
            if m.isfile():
                files[m.name[len(path):]] = tar.extractfile(m).read()
    return files


def write_if_changed(dst, data, dry, report, label):
    old = open(dst, "rb").read() if os.path.exists(dst) else None
    if old == data:
        return
    report.append(f"{'new' if old is None else 'changed'}: {label}")
    if not dry:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(data)


def sync_port(args, report):
    port = archive(args.private, args.private_ref, "port/kz1hd/")
    for rel, data in sorted(port.items()):
        if rel in SKIP_PORT or rel.startswith(SKIP_PORT_DIRS):
            continue
        write_if_changed(os.path.join(ROOT, "port", "kz1hd", rel), rewrite(data), args.dry_run, report, f"port/kz1hd/{rel}")
    here = os.path.join(ROOT, "port", "kz1hd")
    for d, _, names in os.walk(here):
        for n in names:
            rel = os.path.relpath(os.path.join(d, n), here)
            if rel not in port:
                report.append(f"only here (deleted in private?): port/kz1hd/{rel}")
    try:
        tools = archive(args.private, args.private_ref, "port/tools/")
    except subprocess.CalledProcessError:
        tools = {}
    for rel, data in sorted(tools.items()):
        if rel.split("/")[0] in SKIP_TOOLS:
            continue
        write_if_changed(os.path.join(ROOT, "tools", rel), rewrite(data), args.dry_run, report, f"tools/{rel} (from port/tools/)")


def subject_of(text):
    """The patch's Subject, unfolded, without the [PATCH n/m] tag."""
    m = re.search(r"^Subject: (.*(?:\n[ \t].*)*)", text, re.M)
    if not m:
        return ""
    s = re.sub(r"\n[ \t]+", " ", m.group(1))
    return re.sub(r"^\[PATCH[^\]]*\]\s*", "", s).strip()


def body_of(text):
    """What a patch changes, for comparing two versions: everything from the first diff, minus the index lines."""
    i = text.find("\ndiff --git ")
    body = text[i:] if i >= 0 else text
    return re.sub(r"^index [0-9a-f]+\.\.[0-9a-f]+.*$", "", body, flags=re.M).rstrip("\n -0123456789.\n")


def area(text):
    paths = re.findall(r"^diff --git a/(\S+)", text, re.M)
    def block(p):
        if p.startswith(("libs/spurs", "runtime/spu", "tools/spu_lifter", "tools/lift_jobmods")):
            return 1
        if p.startswith(("libs/video", "libs/gcm")):
            return 2
        if p.startswith(("tools/ppu_lifter", "runtime/ppu", "runtime/syscalls", "runtime/host", "libs/audio", "libs/system",
                         "libs/misc", "runtime/ps3_trace")):
            return 3
        if p.startswith(("libs/input", "libs/fs", "libs/filesystem")):
            return 4
        if p.startswith(("libs/network", "libs/trophy")):
            return 5
        return 0
    votes = {}
    for p in paths:
        b = block(p)
        votes[b] = votes.get(b, 0) + 1
    return max(votes, key=votes.get) if votes else 0


def clean(text):
    text = re.sub(r"^From: .*$", PUBLIC_AUTHOR, text, count=1, flags=re.M)
    # No co-author trailers in the public tree (AI use is described once, in the README).
    text = re.sub(r"^Co-Authored-By: .*\n", "", text, flags=re.M)
    return re.sub(r"^Subject: \[PATCH[^\]]*\]", "Subject: [PATCH]", text, count=1, flags=re.M)


def slug(subject):
    s = re.sub(r"[^A-Za-z0-9_.]+", "-", subject).strip("-")
    return s[:52].rstrip("-")


def sync_patches(args, report):
    tmp = tempfile.mkdtemp(prefix="sync-patches-")
    try:
        git(args.ps3recomp, "format-patch", "-q", "--no-signature", "-o", tmp, f"{args.base}..{args.ps3recomp_ref}")
        incoming = [open(os.path.join(tmp, f), encoding="utf-8", errors="surrogateescape").read() for f in sorted(os.listdir(tmp))]
    finally:
        shutil.rmtree(tmp)
    series = [l.strip() for l in open(SERIES) if l.strip() and not l.startswith("#")]
    by_subject = {}
    for name in series:
        text = open(os.path.join(PATCHES, name), encoding="utf-8", errors="surrogateescape").read()
        by_subject[subject_of(text)] = (name, text)
    used = {int(n[:4]) for n in series}
    seen = set()
    new_names = []
    for text in incoming:
        subj = subject_of(text)
        text = clean(text)
        if subj in by_subject:
            name, old = by_subject[subj]
            seen.add(name)
            if body_of(old) != body_of(text):
                report.append(f"changed patch: {name}")
                if not args.dry_run:
                    open(os.path.join(PATCHES, name), "w", encoding="utf-8", errors="surrogateescape").write(text)
            continue
        b = area(text)
        num = next(n for n in range(b * 1000 + 1, b * 1000 + 1000) if n not in used)
        used.add(num)
        name = f"{num:04d}-{slug(subj)}.patch"
        new_names.append(name)
        report.append(f"new patch: {name} (block {b}xxx: check the area)")
        if not args.dry_run:
            open(os.path.join(PATCHES, name), "w", encoding="utf-8", errors="surrogateescape").write(text)
    for name in series:
        if name not in seen:
            report.append(f"patch not in the private branch any more (kept): {name}")
    if new_names and not args.dry_run:
        with open(SERIES, "a") as f:
            f.write("".join(n + "\n" for n in new_names))


def changed_docs(args, report):
    since = args.since or git(ROOT, "log", "-1", "--format=%cI").strip()
    out = git(args.private, "log", f"--since={since}", "--diff-merges=first-parent", "--name-only", "--format=", args.private_ref, "--",
              "docs", "ROADMAP.md", "README.md")
    for path in sorted({l for l in out.splitlines() if l}):
        report.append(f"private doc changed since {since[:10]} (translate into docs/ by hand): {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--private", required=True, help="the private port repository")
    ap.add_argument("--private-ref", default="main")
    ap.add_argument("--ps3recomp", required=True, help="the private ps3recomp repository holding our commits")
    ap.add_argument("--ps3recomp-ref", required=True, help="its branch, e.g. kz-windows")
    ap.add_argument("--base", default="v0.18.0", help="the release our patches sit on (the submodule's pin)")
    ap.add_argument("--since", help="list private doc changes since this date (default: this tree's last commit)")
    ap.add_argument("--private-work", help="the maintainer's work folder the private scripts default to (rewritten to work/)")
    ap.add_argument("--dry-run", action="store_true", help="report only")
    args = ap.parse_args()
    pin = git(ROOT, "-C", "third_party/ps3recomp", "rev-parse", "HEAD").strip()
    base = git(args.ps3recomp, "rev-parse", f"{args.base}^{{commit}}").strip()
    if pin != base:
        sys.exit(f"--base {args.base} is {base[:10]}, but the submodule is pinned at {pin[:10]}: move the pin first")
    CTX["P"] = os.path.abspath(args.private)
    if args.private_work:
        CTX["W"] = os.path.abspath(args.private_work)
    report = []
    sync_port(args, report)
    sync_patches(args, report)
    changed_docs(args, report)
    print("\n".join(report) if report else "nothing to bring over")
    if report and not args.dry_run:
        print("\nnext: tools/ps3recomp-patched.sh, tools/lift.sh, tools/build.sh, tools/check-tree.sh, then review git diff")


if __name__ == "__main__":
    main()
