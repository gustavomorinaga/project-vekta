#!/usr/bin/env python3
"""Host test for tools/release/version_check.py: off unless turned on (vekta-install.ini or KZ_VERSION_CHECK), at
most one request a day, a failed request silent and retried, a newer release named with its link, the same or an
older one not. The request is a stand-in: no network."""
import os
import pathlib
import sys
import tempfile
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "release"))
import version_check as vc  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


calls = []


def fetch_of(answer):
    def f():
        calls.append(1)
        return answer
    return f


check(vc.version_key("v0.10.0") > vc.version_key("0.9.9") and vc.version_key("v1.2") is None,
      "versions compare as numbers; anything else is not a version")
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    (root / "VERSION").write_text("0.5.0\n")
    work = str(root / "work")
    new = fetch_of(("v0.6.0", "https://example.invalid/releases/v0.6.0"))

    check(vc.check(str(root), work, {}, "2026-11-15", new) == "" and not calls, "off by default: no request")
    check(vc.check(str(root), work, {"KZ_VERSION_CHECK": "1"}, "2026-11-15", new).startswith(
          "Project Vekta 0.6.0 is out (this is 0.5.0): https://example.invalid/") and len(calls) == 1,
          "on with KZ_VERSION_CHECK=1: a newer release is named with its link")
    msg = vc.check(str(root), work, {"KZ_VERSION_CHECK": "1"}, "2026-11-15", new)
    check(msg and len(calls) == 1, "the same day: no second request, the answer kept")
    check(vc.check(str(root), work, {"KZ_VERSION_CHECK": "1"}, "2026-11-16", fetch_of(("v0.5.0", "u"))) == ""
          and len(calls) == 2, "the next day: asked again; the same version is not news")
    check(vc.check(str(root), work, {"KZ_VERSION_CHECK": "1"}, "2026-11-17", fetch_of(None)) == ""
          and len(calls) == 3, "a failed request is silent")
    vc.check(str(root), work, {"KZ_VERSION_CHECK": "1"}, "2026-11-17", fetch_of(None))
    check(len(calls) == 4, "and is tried again (the day is not recorded)")

    vc.set_enabled(str(root), True)
    check(vc.enabled(str(root), {}) and not vc.enabled(str(root), {"KZ_VERSION_CHECK": "0"}),
          "--enable writes vekta-install.ini; KZ_VERSION_CHECK=0 wins over it")
    (root / "vekta-install.ini").write_text("[install]\nportable=0\n")
    vc.set_enabled(str(root), False)
    text = (root / "vekta-install.ini").read_text()
    check("portable = 0" in text and not vc.enabled(str(root), {}), f"--disable keeps the ini's other keys: {text}")

    seen = []

    class Resp:
        def __init__(self, body):
            self.body = body

        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

        def read(self, n):
            return self.body

    def opener(req, timeout):
        seen.append((req.full_url, req.get_header("User-agent"), req.get_header("Cookie"), timeout))
        return Resp(b'[{"tag_name": "v0.5.1", "html_url": "https://github.com/a", "prerelease": true},'
                    b' {"tag_name": "v0.7.0", "html_url": "https://github.com/d", "draft": true},'
                    b' {"tag_name": "v0.6.0", "html_url": "https://github.com/x", "prerelease": true},'
                    b' {"tag_name": "nightly", "html_url": "https://github.com/n"}]')
    check(vc.fetch_latest(opener) == ("v0.6.0", "https://github.com/x"),
          "fetch_latest: the highest version, pre-releases included, drafts and other tags left out")
    check(seen == [(vc.URL, vc.AGENT, None, vc.TIMEOUT)] and urllib.parse.urlsplit(vc.URL).hostname == "api.github.com",
          f"one request to GitHub's API, no cookie, a plain User-Agent: {seen}")
    check(vc.fetch_latest(lambda req, timeout: Resp(b"not json")) is None, "a bad answer is a failed request")
    check("version-check" in (ROOT / "README.md").read_text() or "New-version check" in
          (ROOT / "README.md").read_text(), "README's What goes online names it")
    os.environ.pop("KZ_VERSION_CHECK", None)

print("test_version_check:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
