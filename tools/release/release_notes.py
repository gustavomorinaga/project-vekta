#!/usr/bin/env python3
"""The GitHub release notes for a tag, written from its CHANGELOG entry, so nobody writes them by hand.

    python3 tools/release/release_notes.py --tag vX.Y.Z [--out FILE] [--root DIR]
    python3 tools/release/release_notes.py --preview X.Y.Z [--out FILE]   # the release workflow's dry run

After Project Pyrrhus's and Project Helghast's tools/release/release_notes.py (undecuple, MIT), with this project's
wording and assets:
  Project Vekta X.Y.Z, a pre-release: playable from start to end, see docs/RELEASE-CRITERIA.md.
  How to install: <a link to docs/INSTALL.md, then the fixed paragraph below>   (after 0.4.0: here, before the lists)
  New in X.Y.Z: <the entry's Added/Changed/... items, one bullet each>
  Fixed: <its fixes>                       (part of the list above for the first published release, which also
                                           carries the entries before the first version: the first public tree)
  See CHANGELOG.md for the full list.
  <the fixed paragraph: a source release, what you need, none of it included, the SHA-256 sums>
Links point at the tag's own files. A tag with no CHANGELOG entry, an entry with no items, or an entry that is not a
release (not dated with "(pre-release)" or "(release)", such as [Unreleased]) is refused: exit 1. --preview takes the
version's entry or, when it has none or an empty one, the [Unreleased] one. Exit 0 = notes written, 1 = refused,
2 = usage. Standard library only.
"""
import argparse
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import release_version  # noqa: E402

ROOT = HERE.parents[1]
REPO = "https://github.com/gustavomorinaga/project-vekta"
STATUS = "playable from start to end"   # the page's headline since Levels 2 to 11 work (tools/website.py)
HEADING = re.compile(r"## \[([^\]]+)\](?:[ \t]+-[ \t]+(\d{4}-\d\d-\d\d))?[ \t]*(?:\(([^)]*)\))?[ \t]*$")
NEW_SECTIONS = ("Added", "Changed", "Removed", "Deprecated", "Security")
RELEASED = ("pre-release", "release")
# docs/RELEASE-CRITERIA.md, gate 2, relaxed for v0.1.0 only: its notes say so.
FIRST_BUILD_NOTE = ("This first preview has been built only on the maintainer's machine (from this release's own "
                    "archive, in a fresh folder); no other machine has built it yet. Reports of a build elsewhere, "
                    "working or not, are welcome as issues.")
# v0.2.0, the first release with the setup program: gate 2 relaxed again (maintainer's decision, 2026-11-01).
SETUP_BUILD_NOTE = ("The setup program has been tried only on the maintainer's machine (Linux, and Windows on the same "
                    "PC: installed, built and played); no other machine has built this release yet. Reports of an "
                    "install elsewhere, working or not, are welcome as issues.")
# v0.3.0: gate 2 relaxed a third time (maintainer's decision, 2026-11-06), after an install over 0.2.0 on Windows.
OVER_020_NOTE = ("This release has been tried only on the maintainer's machine (Linux, and Windows on the same PC: "
                 "installed over 0.2.0, built and played); no other machine has built it yet. Reports of an install "
                 "or a build elsewhere, working or not, are welcome as issues.")
# v0.5.0 and v0.5.1 (0.5.0's release build failed, 0.5.1 is that release): gate 2 relaxed a fourth time (maintainer's
# decision, 2026-11-16), for the simplified install.
INSTALL_050_NOTE = ("This release has been tried only on the maintainer's machine (Linux, and Windows on the same PC: "
                    "the new install, from RPCS3 and from prepared files, built and played); no other machine has built "
                    "it yet. Reports of an install or a build elsewhere, working or not, are welcome as issues.")
# v0.5.2: gate 2 relaxed a fifth time (maintainer's decision, 2026-11-18), for the RPCS3 search and path-space fixes.
FIXES_052_NOTE = ("This release has been tried only on the maintainer's machine (Windows: the test setup with both "
                  "fixes installed and built); no other machine has built it yet. Reports of an install or a "
                  "build elsewhere, working or not, are welcome as issues.")
# v0.5.3: gate 2 relaxed a sixth time (maintainer's decision, 2026-11-19), for the PlayStation Store copy.
FIXES_053_NOTE = ("This release has been tried only on the maintainer's machine (Windows: the test setup installed, "
                  "built and played from both the disc and the PlayStation Store copies); no other machine has built "
                  "it yet. Reports of an install or a build elsewhere, working or not, are welcome as issues.")
# v0.5.4: the fix was confirmed on the reporter's own Windows 11 machine (issue #94, 2026-11-20).
FIXES_054_NOTE = ("The fix in this release was confirmed on the reporter's own Windows 11 machine with a test build of "
                  "the same code (#94): the setup, given the PATH that had stopped 0.5.3, decrypted both files with "
                  "RPCS3, built and played. Reports of an install or a build elsewhere, working or not, are welcome "
                  "as issues.")

# v0.1.0 carried the source archive only; later releases also carry the Windows setup program and folder (release.yml).
SOURCE_ONLY = (
    "A source release: project-vekta-{version}-source.tar.gz holds this tag's tree with ps3recomp at its pinned "
    "commit, and builds the port on your own Linux machine (and the Windows exe from there): "
    "[docs/BUILDING.md]({blob}/docs/BUILDING.md). You need your own Killzone HD (NPUA80856) from the Killzone "
    "Trilogy disc with its decrypted EBOOT and data archive, and the decrypted libsre and dev_flash of your own PS3 "
    "firmware; none of them is included or downloaded. What each must contain: "
    "[docs/GETTING-FILES.md]({blob}/docs/GETTING-FILES.md). SHA-256 sums: SHA256SUMS (and the .sha256 file).")
LINUX_SOURCE = (
    "Linux: project-vekta-{version}-source.tar.gz holds this tag's tree with ps3recomp at its pinned commit; "
    "tools/install.sh builds it ([docs/BUILDING.md]({blob}/docs/BUILDING.md)). ")
LINUX_FOLDER = (
    "Linux: project-vekta-{version}-linux.tar.gz is the same kind of folder with its own toolchain (Zig, CMake, "
    "Ninja; nothing to install but Python 3): unpack it, ./run.sh builds and offers an applications-menu entry, "
    "./play.sh plays. The game needs glibc 2.31 or newer, a Vulkan driver and the system's SDL2. "
    "project-vekta-{version}-source.tar.gz holds this tag's tree with ps3recomp at its pinned commit, for building "
    "with your own compilers ([docs/BUILDING.md]({blob}/docs/BUILDING.md)). ")
# From 0.6.0 each folder has one program at its top (Project Vekta.exe, project-vekta) that builds on the first start
# and then plays; the first run's questions and the launchers' prepare step are in scripts/.
LINUX_FOLDER_060 = (
    "Linux: project-vekta-{version}-linux.tar.gz is the same kind of folder with its own toolchain (Zig, CMake, "
    "Ninja; nothing to install but Python 3): unpack it and start ./project-vekta, which builds the first time and "
    "then plays (scripts/run.sh asks for your files step by step). The game needs glibc 2.31 or newer, a Vulkan "
    "driver and the system's SDL2. project-vekta-{version}-source.tar.gz holds this tag's tree with ps3recomp at its "
    "pinned commit, for building with your own compilers ([docs/BUILDING.md]({blob}/docs/BUILDING.md)). ")
FILES = (
    "Windows: project-vekta-{version}-setup.exe installs the port's sources and the pinned toolchain for your user "
    "(no admin rights). It asks where your files are and builds the port in the wizard (about five minutes; untick "
    "\"Build now\" and the first start builds); its launcher, Project Vekta.exe, plays. "
    "project-vekta-{version}-windows.zip is the same folder without the "
    "setup program: run.cmd builds, play.cmd plays. {linux}"
    "None of the files holds anything of the game: {need}; none of them is included or downloaded. What each must "
    "contain: "
    "[docs/GETTING-FILES.md]({blob}/docs/GETTING-FILES.md). SHA-256 sums: SHA256SUMS (and a .sha256 beside each "
    "file).")
NEED_PREPARED = ("you need your own Killzone HD (NPUA80856) from the Killzone Trilogy disc with its decrypted EBOOT "
                 "and data archive, and the decrypted libsre and dev_flash of your own PS3 firmware")
# From 0.5.0 the installed game is read as it is, and the player's own RPCS3 can make the decrypted copies.
NEED_050 = ("you need your own Killzone HD (NPUA80856) from the Killzone Trilogy disc, installed, and PS3 system software "
            "4.93; when both are installed in your RPCS3, the installers use them and your RPCS3 makes the decrypted "
            "copies of the EBOOT and libsre, otherwise you give those two decrypted files yourself")
# From 0.5.3 the PlayStation Store copy installs too: its data archive is bound to the buyer's licence (licence type 2),
# which only the player's RPCS3 holds (the .rap named after the content ID), so it goes through the RPCS3 route.
STORE_COPY = ("or the PlayStation Store copy (content ID UP9000-NPUA80856_00-GKILLZONE1000001, through your RPCS3, which "
              "holds its licence, UP9000-NPUA80856_00-GKILLZONE1000001.rap)")
NEED_053 = NEED_050.replace("from the Killzone Trilogy disc, installed,", "from the Killzone Trilogy disc " + STORE_COPY +
                            ", installed,")
# From 0.6.0 no PS3 system software is needed: the EBOOT is the one decrypted file.
NEED_060 = ("you need your own Killzone HD (NPUA80856) from the Killzone Trilogy disc " + STORE_COPY + ", installed; when "
            "it is installed in your RPCS3, the installers use it and your RPCS3 makes the decrypted copy of the EBOOT, "
            "otherwise you give that decrypted file yourself. No PS3 system software is needed")


def need_text(v):
    return (NEED_060 if v >= (0, 6, 0) else NEED_053 if v >= (0, 5, 3) else NEED_050 if v >= (0, 5, 0) else
            NEED_PREPARED)


# After 0.4.0 the paragraph on the downloads comes first, under this heading, with the player's install page.
INSTALL_FIRST = (0, 4, 0)
INSTALL_PAGE = ("How to install, step by step, with what each step asks and shows: "
                "[docs/INSTALL.md]({blob}/docs/INSTALL.md).")


def entries(text):
    """{name: {date, note, items: [(section, text)]}} for every '## [...]' heading of the whole file."""
    out, cur, section = {}, None, ""
    for line in text.splitlines():
        m = HEADING.match(line)
        if m:
            cur = {"date": m.group(2), "note": (m.group(3) or "").strip(), "items": []}
            out.setdefault(m.group(1), cur)
            section = ""
        elif cur is None:
            continue
        elif line.startswith("### "):
            section = line[4:].strip()
        elif line.startswith("- "):
            cur["items"].append((section, line[2:].strip()))
        elif line.startswith("  ") and cur["items"] and line.strip():   # a wrapped item line
            s, t = cur["items"][-1]
            cur["items"][-1] = (s, t + " " + line.strip())
    return out


def released(e):
    return bool(e["date"]) and e["note"] in RELEASED


def sentence(t, blob):
    """An item as a sentence: a full stop added when missing; relative links point at the tag's files."""
    t = re.sub(r"\]\((?!https?:|#|mailto:)\.?/?([^)\s]+)\)", lambda m: f"]({blob}/{m.group(1)})", t.strip())
    return t if t.rstrip(")\"'`").endswith((".", "!", "?")) else t + "."


def first_release(version, es):
    """True when no earlier version was published: the first release has nothing to fix or install over."""
    return not any(release_version.valid(v) and release_version.key(v) < release_version.key(version) and released(e)
                   for v, e in es.items())


def before_versions(es):
    """Items of the entries that are not versions (the first public tree): the first release brings them to players."""
    return [i for name, e in es.items() if name != "Unreleased" and not release_version.valid(name) for i in e["items"]]


def notes(version, entry, first=False):
    blob = f"{REPO}/blob/v{version}"
    new = [sentence(t, blob) for s, t in entry["items"] if s in NEW_SECTIONS or not s]
    fixed = [sentence(t, blob) for s, t in entry["items"] if s == "Fixed"]
    kind = "a pre-release" if version.split(".")[0] == "0" else "a release"
    bullets = lambda items: "\n".join(f"- {t}" for t in items)  # noqa: E731
    if first:
        lists = [f"New in {version}:\n{bullets(new + fixed)}"]
    else:
        lists = ([f"New in {version}:\n{bullets(new)}"] if new else []) + ([f"Fixed:\n{bullets(fixed)}"] if fixed else [])
    v = tuple(int(x) for x in version.split("."))
    linux = LINUX_FOLDER_060 if v >= (0, 6, 0) else LINUX_FOLDER if v > (0, 2, 0) else LINUX_SOURCE
    files = (SOURCE_ONLY.format(version=version, blob=blob) if version == "0.1.0" else
             FILES.format(version=version, blob=blob, need=need_text(v), linux=linux.format(version=version, blob=blob)))
    if v >= (0, 6, 0):
        files = files.replace("setup program: run.cmd builds, play.cmd plays.",
                              "setup program: Project Vekta.exe builds the first time and then plays (scripts\\run.cmd "
                              "asks for your files step by step).")
    install_first = v > INSTALL_FIRST
    return "\n\n".join([
        f"Project Vekta {version}, {kind}: {STATUS}, see [docs/RELEASE-CRITERIA.md]({blob}/docs/RELEASE-CRITERIA.md).",
        *([f"{INSTALL_PAGE.format(blob=blob)}\n\n{files}"] if install_first else []),
        *lists,
        f"See [CHANGELOG.md]({blob}/CHANGELOG.md) for the full list.",
        *([] if install_first else [files]),
        *({"0.1.0": [FIRST_BUILD_NOTE], "0.2.0": [SETUP_BUILD_NOTE], "0.3.0": [OVER_020_NOTE], "0.5.0": [INSTALL_050_NOTE], "0.5.1": [INSTALL_050_NOTE],
           "0.5.2": [FIXES_052_NOTE], "0.5.3": [FIXES_053_NOTE], "0.5.4": [FIXES_054_NOTE]}.get(version, [])),
    ]) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--tag", help="the release tag, vX.Y.Z")
    g.add_argument("--preview", metavar="X.Y.Z", help="dry run: the version's entry, else [Unreleased]")
    ap.add_argument("--out", help="write the notes here (default: standard output)")
    ap.add_argument("--root", default=str(ROOT), help="repository root")
    a = ap.parse_args(argv)
    version = release_version.from_tag(a.tag) if a.tag else (a.preview if release_version.valid(a.preview) else None)
    if version is None:
        print(f"release_notes: {a.tag or a.preview!r} is not {'vX.Y.Z' if a.tag else 'X.Y.Z'}", file=sys.stderr)
        return 2
    p = pathlib.Path(a.root) / "CHANGELOG.md"
    if not p.is_file():
        print(f"release_notes: no CHANGELOG.md at {p.parent}", file=sys.stderr)
        return 1
    es = entries(p.read_text(encoding="utf-8"))
    e = es.get(version)
    if a.preview and (e is None or not e["items"]):
        e = es.get("Unreleased") or e
    if e is None:
        print(f"release_notes: CHANGELOG.md has no entry for {version}; tools/release/bump_version.py turns "
              "[Unreleased] into it", file=sys.stderr)
        return 1
    if a.tag and not released(e):
        print(f"release_notes: the CHANGELOG entry for {version} is not a release ({e['note'] or 'no date'})",
              file=sys.stderr)
        return 1
    if not e["items"]:
        print(f"release_notes: the CHANGELOG entry for {version} lists nothing", file=sys.stderr)
        return 1
    first = first_release(version, es)
    body = notes(version, dict(e, items=e["items"] + (before_versions(es) if first else [])), first)
    if a.out:
        out = pathlib.Path(a.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(body, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
