#!/usr/bin/env python3
"""Host test for tools/edition_map.py and port/kz1hd/edition.h (the US to EU address table, re/eu/address_map.tsv):
the table loads, port/kz1hd/edition.h is what it gives, KZ_GA() compiled as C gives the table's answers in both
editions, every address the port passes to KZ_GA has an EU value, and every code or rodata address port/kz1hd writes
in code goes through KZ_GA. With both decrypted EBOOTs (KZ_EBOOT for the US one, KZ_EU_EBOOT for the EU one) it also
checks the table against them; without them it says that part was not run. No game data needed otherwise."""
import importlib.util
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("edition_map", os.path.join(ROOT, "tools", "edition_map.py"))
em = importlib.util.module_from_spec(spec)
spec.loader.exec_module(em)
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", what)


rows = em.load()
eu = lambda a: em.eu(a, rows)

# The table's answers for addresses whose EU place was found by hand (stage 1 and 2, 2026-11-24).
for us, want, what in [
    (0x004CBBB4, 0x004CBB14, "the crosshair's site (inside the changed 0x4CB798)"),
    (0x004CDC0C, 0x004CDB24, "the grenade row's first site"),
    (0x004CB798, 0x004CB6F8, "the start of a changed function"),
    (0x004C8980, 0x004C88E0, "the function after the changed 0x4C7A28"),
    (0x00291FA8, 0x00291F08, "the player's update (-160)"),
    (0x0021C0F0, 0x0021C0A0, "the clock's double between functions (-80)"),
    (0x00086758, 0x00086750, "the first byte after the 8 dropped ones (-8)"),
    (0x00544E90, 0x00544D90, "a rodata float (-256)"),
    (0x00567340, 0x00567240, "a vtable after the changed strings (-256)"),
    (0x005AB500, 0x005AB400, "an SPU image in section 14 (-256)"),
    (0x00828734, 0x00828734, "data: the same address"),
    (0x0131E4DC, 0x0131E4DC, "bss: the same address"),
    (0x00000100, 0x00000100, "below the EBOOT: unchanged"),
]:
    check(eu(us) == want, f"{what}: 0x{us:08X} -> {eu(us)}, want 0x{want:08X}")
for us, what in [
    (0x004CCB0C, "a site inside 0x4CB798 that no row proves (the step would give 0x4CCAE4; the EU one is 0x4CCA48)"),
    (0x002263FC, "inside the changed 0x2263F8"),
    (0x00086750, "the 8 bytes the EU EBOOT drops"),
    (0x00565600, "inside the changed rodata strings"),
    (0x0052FF50, "the padding before the rodata"),
]:
    check(eu(us) is None, f"{what}: 0x{us:08X} has no EU value, got {eu(us)}")

# us_of is eu's inverse; an EU address with no US one has none.
for us in (0x004CBBB4, 0x00291FA8, 0x0021C0F0, 0x00544E90, 0x00828734, 0x004CB798, 0x00010230):
    check(em.us_of(eu(us), rows) == us, f"us_of(eu(0x{us:08X})) is 0x{us:08X}")
check(em.us_of(0x004CCA48, rows) is None, "an EU site inside the changed 0x4CB798 that no row names has no US address")

# The EU lift's names: each function under its US name, an address with none keeps its own with _eu, one line per
# start that moved.
starts = [eu(0x00291FA8), eu(0x004CB798), 0x004CCA48, eu(0x00010230)]
names = em.func_names(starts, rows)
check(names.get(eu(0x00291FA8)) == "func_00291FA8", f"the player's update keeps its US name: {names.get(0x291F08)}")
check(names.get(0x004CCA48) == "func_004CCA48_eu", "an address with no US one is func_ADDR_eu")
check(0x00010230 not in names, "an address that does not move needs no line")
check(len(names) == 3 and len(set(names.values())) == 3, f"one line per moved start, no name twice: {names}")

# The edition from a PARAM.SFO's TITLE_ID.
def sfo(title):
    key, val = b"TITLE_ID\0", title.encode() + b"\0"
    keys, data = 20 + 16, 20 + 16 + len(key)
    return (b"\0PSF" + (0x101).to_bytes(4, "little") + keys.to_bytes(4, "little") + data.to_bytes(4, "little")
            + (1).to_bytes(4, "little") + (0).to_bytes(2, "little") + (0x0204).to_bytes(2, "little")
            + len(val).to_bytes(4, "little") + len(val).to_bytes(4, "little") + (0).to_bytes(4, "little") + key + val)


d0 = tempfile.mkdtemp()
for title, ed in (("NPUA80856", "US"), ("BCUS98116", "US"), ("NPEA00418", "EU"), ("BCES01743", "EU")):
    p = os.path.join(d0, "PARAM.SFO")
    with open(p, "wb") as f:
        f.write(sfo(title))
    check(em.sfo_title(p) == title and em.edition_of(title) == ed, f"{title} is the {ed} edition")
try:
    em.edition_of("BCUS98232")
    check(False, "an unknown title is refused")
except ValueError:
    pass
check(em.sfo_title(os.path.join(d0, "none")) is None, "no PARAM.SFO: no title")

# Malformed tables are refused.
with open(em.TSV, encoding="utf-8") as f:
    good = f.read()
d = tempfile.mkdtemp()
for old, new, what in [
    ("site\t0x004CBBB4\t0x004CBBB8\t0x004CBB14\t0x004CBB18", "site\t0x004CBBB4\t0x004CBBBC\t0x004CBB14\t0x004CBB18",
     "a site that changes length"),
    ("site\t0x004CBBB4\t0x004CBBB8", "site\t0x00291FA8\t0x00291FAC", "a site outside every changed row"),
    ("range\t0x00278888\t", "range\t0x00278880\t", "overlapping ranges"),
    ("0x004CBB14", "0x4CBB14", "an address not written 0x + 8 digits"),
]:
    check(old in good, f"the table has {old!r}")
    p = os.path.join(d, "map.tsv")
    with open(p, "w", encoding="utf-8") as f:
        f.write(good.replace(old, new, 1))
    try:
        em.load(p)
        check(False, f"{what} is refused")
    except ValueError:
        pass

# port/kz1hd/edition.h is current.
r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "edition_map.py"), "header", "--check"],
                   capture_output=True, text=True)
check(r.returncode == 0, "port/kz1hd/edition.h is what the table gives: " + r.stderr.strip())

# Every KZ_GA in the port has an EU value, and no code or rodata address in port/kz1hd's code goes around KZ_GA.
args = em.ga_args()
check(len(args) > 50, f"the port passes its addresses through KZ_GA ({len(args)} found)")
for path, n, v in args:
    check(eu(v) is not None, f"{path}:{n}: KZ_GA(0x{v:08X}) has no proven EU address")
for path, n, text, v, e, in_code, cls in em.inventory(rows):
    if path.startswith("port/kz1hd/") and in_code and cls == "code":
        line = open(os.path.join(ROOT, path), encoding="utf-8").read().split("\n")[n - 1]
        check(f"KZ_GA({text})" in line, f"{path}:{n}: {text} is not passed through KZ_GA (or list it in "
                                        "edition_map.NOT_ADDRESSES if it is not an EBOOT address)")

# KZ_GA as the compiler sees it, in both editions, against the table: every port argument, every row's edges and the
# addresses on both sides of them.
probe = sorted({v for _, _, v in args} | {a + k for r in rows for a in (r["us_start"], r["us_end"]) for k in (-4, 0, 4)}
               | {0, 0x100, 0x10000, 0x5C0000, 0x137FE98, 0x30000000})
c = os.path.join(d, "probe.c")
with open(c, "w") as f:
    f.write('#include <stdio.h>\n#include "edition.h"\n'
            "#define P(a) printf(\"%08X %08X\\n\", (unsigned)(a), (unsigned)KZ_GA(a));\n"
            "int main(void) {\n" + "".join(f"P(0x{a:08X}u)\n" for a in probe) +
            "switch (0u) { case KZ_GA(0x004CBBB4u): break; default: break; }   /* a constant expression */\n"
            "return 0; }\n")
cc = os.environ.get("CC", "cc")
for flag, want in (("-DKZ_EDITION_EU=1", lambda a: 0xFFFFFFFF if eu(a) is None else eu(a)), ("", lambda a: a)):
    exe = os.path.join(d, "probe" + flag[-1:])
    b = subprocess.run([cc, "-std=c11", "-Wall", "-Werror", "-I", os.path.join(ROOT, "port", "kz1hd"), c, "-o", exe]
                       + ([flag] if flag else []), capture_output=True, text=True)
    check(b.returncode == 0, f"the probe builds {flag or 'as US'}: {b.stderr.strip()[:400]}")
    if b.returncode == 0:
        got = subprocess.run([exe], capture_output=True, text=True, timeout=60).stdout.split("\n")
        for a, line in zip(probe, got):
            check(line == f"{a:08X} {want(a):08X}", f"KZ_GA(0x{a:08X}) {flag or 'US'}: {line!r}")

# Against the EBOOTs, when this machine has both.
us_elf, eu_elf = os.environ.get("KZ_EBOOT", ""), os.environ.get("KZ_EU_EBOOT", "")
if os.path.isfile(us_elf) and os.path.isfile(eu_elf):
    problems = em.check(us_elf, eu_elf, rows)
    for p in problems:
        check(False, "EBOOTs: " + p)
    print("test_edition_map: the table checked against both EBOOTs")
else:
    print("test_edition_map: no KZ_EBOOT and KZ_EU_EBOOT: the table was not checked against the EBOOTs")

print("test_edition_map:", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
