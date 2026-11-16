#!/usr/bin/env python3
"""The new-version check, off unless the player turns it on: at most once a day, before the game starts, one anonymous
request to GitHub's public API for this project's latest release; when it is newer than this folder's VERSION, the
launcher says so, with the release page's link. Nothing is downloaded or installed, and nothing about the player or
the PC is sent (no cookie, no identifier; the request's User-Agent is "project-vekta-version-check").

    tools/release/version_check.py [--root DIR] [--force]       print what it would say, and why
    tools/release/version_check.py --enable | --disable [--root DIR]

On: vekta-install.ini [update] check=1 (the setup program's task, or --enable), or KZ_VERSION_CHECK=1 in the
environment (the zip, the Linux folder, a Git clone; default 0 = off). KZ_VERSION_CHECK=0 turns it off whatever the
ini says. The day of the last request and its answer are kept in work/version-check.json. A failed request (no
network, a proxy, GitHub's rate limit) is silent and tried again the next day. README.md's "What goes online" lists it.

After Project Helghast's tools/release/version_check.py (undecuple, MIT): opt-in, one anonymous request a day; the
code is written here. Standard library only.
"""
import argparse
import configparser
import datetime
import json
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
# The release list, not .../releases/latest: that one skips pre-releases, and every version so far is one.
URL = "https://api.github.com/repos/gustavomorinaga/project-vekta/releases?per_page=10"
AGENT = "project-vekta-version-check"
INSTALL_INI = "vekta-install.ini"
STATE = "version-check.json"
TIMEOUT = 4


def version_key(v):
    """'v0.5.0' / '0.5.0' -> (0, 5, 0); None when it is not three numbers."""
    m = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)", (v or "").strip())
    return tuple(int(x) for x in m.groups()) if m else None


def enabled(root, environ=None):
    environ = os.environ if environ is None else environ
    env = environ.get("KZ_VERSION_CHECK", "")
    if env in ("0", "1"):
        return env == "1"
    c = configparser.ConfigParser(interpolation=None)
    try:
        c.read(os.path.join(root, INSTALL_INI), encoding="utf-8-sig")
    except (configparser.Error, OSError, UnicodeError):
        return False
    return c.get("update", "check", fallback="0").strip() == "1"


def set_enabled(root, on):
    path = os.path.join(root, INSTALL_INI)
    c = configparser.ConfigParser(interpolation=None)
    try:
        c.read(path, encoding="utf-8-sig")
    except (configparser.Error, OSError, UnicodeError):
        pass
    if not c.has_section("update"):
        c.add_section("update")
    c.set("update", "check", "1" if on else "0")
    with open(path, "w", encoding="utf-8") as f:
        c.write(f)


def fetch_latest(opener=None):
    """(tag, html_url) of the highest published release (pre-releases count, drafts do not), or None on any
    failure."""
    req = urllib.request.Request(URL, headers={"User-Agent": AGENT, "Accept": "application/vnd.github+json"})
    try:
        with (opener or urllib.request.urlopen)(req, timeout=TIMEOUT) as r:
            rels = json.loads(r.read(1 << 20).decode("utf-8"))
        best = max((x for x in rels if not x.get("draft") and version_key(x.get("tag_name"))),
                   key=lambda x: version_key(x["tag_name"]), default=None)
        return (best["tag_name"], best.get("html_url", "")) if best else None
    except (OSError, ValueError, AttributeError, TypeError):
        return None


def check(root, work=None, environ=None, today=None, fetch=fetch_latest, force=False):
    """The message for the player, or "" (off, checked today already with nothing new, the request failed, up to date).
    force: ask even when off or done today (the command line's --force)."""
    if not force and not enabled(root, environ):
        return ""
    work = work or os.path.join(root, "work")
    today = today or datetime.date.today().isoformat()
    state_path = os.path.join(work, STATE)
    try:
        with open(state_path, encoding="utf-8") as f:
            state = json.load(f)
    except (OSError, ValueError):
        state = {}
    if force or state.get("day") != today:
        got = fetch()
        if got is None:
            return ""   # tried again tomorrow (the day is not recorded)
        state = {"day": today, "tag": got[0], "url": got[1]}
        try:
            os.makedirs(work, exist_ok=True)
            with open(state_path, "w", encoding="utf-8") as f:
                json.dump(state, f)
        except OSError:
            pass
    try:
        with open(os.path.join(root, "VERSION"), encoding="utf-8") as f:
            mine = f.read().strip()
    except OSError:
        return ""
    new, have = version_key(state.get("tag")), version_key(mine)
    if new and have and new > have:
        return f"Project Vekta {state['tag'].lstrip('v')} is out (this is {mine}): {state.get('url', '')}"
    return ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--force", action="store_true", help="ask now, even when off or done today")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--enable", action="store_true", help="turn the daily check on (vekta-install.ini)")
    g.add_argument("--disable", action="store_true", help="turn it off")
    a = ap.parse_args(argv)
    root = os.path.abspath(a.root)
    if a.enable or a.disable:
        set_enabled(root, a.enable)
        print(f"version check: {'on' if a.enable else 'off'} ({os.path.join(root, INSTALL_INI)})")
        return 0
    if not a.force and not enabled(root):
        print("version check: off (KZ_VERSION_CHECK=1 or --enable turns it on)")
        return 0
    print(check(root, force=a.force) or "version check: nothing new")
    return 0


if __name__ == "__main__":
    sys.exit(main())
