#!/usr/bin/env python3
"""The guest addresses of a second edition of Killzone HD: re/eu/address_map.tsv takes an address of the US EBOOT.elf
(NPUA80856, the one the port's sources name) to the EU one (NPEA00418). port/kz1hd/edition.h, which this writes, turns
the table into KZ_GA(), the macro the port passes every code and rodata address through.

    python3 tools/edition_map.py header [--check]   write port/kz1hd/edition.h (--check: exit 1 if it is stale)
    python3 tools/edition_map.py eu ADDR...         the EU address of each US one ("none": not proven)
    python3 tools/edition_map.py inventory          every EBOOT address port/kz1hd, port/patches and tools/ cite
    python3 tools/edition_map.py check US.elf EU.elf
                                                    the table against both decrypted EBOOTs (reads them only)

What an address maps to: a site row's EU address; the start of a changed function its eu_start; anything else inside
a changed row has none (the code differs there, so a site in it needs a site row with its evidence); an address in a
range row moves with it; below 0x10000 and above the data segment (not the EBOOT) it stays. Numbers only: the table and
this tool hold no bytes of the game.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "re", "eu", "address_map.tsv")
HEADER = os.path.join(ROOT, "port", "kz1hd", "edition.h")
COLUMNS = ["kind", "us_start", "us_end", "eu_start", "eu_end", "source", "note"]
KINDS = ("range", "changed", "site")
EBOOT_LO, EBOOT_HI = 0x10000, 0x137FE98        # the two LOAD segments: code 0x10000.., data and bss ..0x137FE98
CODE_HI = 0x5BC3E8                             # the end of the code segment (text, stubs, rodata): what moves


def load(path=TSV):
    """The rows as dicts (addresses as ints). Raises ValueError on a malformed or inconsistent table."""
    rows, header = [], None
    for n, line in enumerate(open(path, encoding="utf-8").read().split("\n"), 1):
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if header is None:
            if f != COLUMNS:
                raise ValueError(f"{path}:{n}: header {f}, expected {COLUMNS}")
            header = f
            continue
        if len(f) != len(COLUMNS) or f[0] not in KINDS:
            raise ValueError(f"{path}:{n}: bad row")
        r: dict = dict(zip(COLUMNS, f))
        for k in COLUMNS[1:5]:
            if not re.fullmatch(r"0x[0-9A-F]{8}", r[k]):
                raise ValueError(f"{path}:{n}: {k} {r[k]!r} is not 0x + 8 upper-case hex digits")
            r[k] = int(r[k], 16)
        r["line"] = n
        if r["us_end"] <= r["us_start"] or r["eu_end"] < r["eu_start"]:
            raise ValueError(f"{path}:{n}: empty or reversed span")
        if r["kind"] != "changed" and r["us_end"] - r["us_start"] != r["eu_end"] - r["eu_start"]:
            raise ValueError(f"{path}:{n}: a {r['kind']} row keeps its length")
        rows.append(r)
    for kind in KINDS:
        k = [r for r in rows if r["kind"] == kind]
        for a, b in zip(k, k[1:]):
            if b["us_start"] < a["us_end"] or b["eu_start"] < a["eu_start"]:
                raise ValueError(f"{path}:{b['line']}: {kind} rows out of order or overlapping")
    for s in (r for r in rows if r["kind"] == "site"):
        if not any(c["us_start"] < s["us_start"] < c["us_end"] for c in rows if c["kind"] == "changed"):
            raise ValueError(f"{path}:{s['line']}: a site row must lie inside a changed row")
    for c in (r for r in rows if r["kind"] == "changed"):
        if not any(g["us_start"] <= c["us_start"] < g["us_end"] for g in rows if g["kind"] == "range"):
            raise ValueError(f"{path}:{c['line']}: a changed row must start in a range row")
    return rows


def is_function(row):
    """A changed function (its start maps), as against bytes between functions or data (nothing in it maps)."""
    return row["kind"] == "changed" and row["source"] != "walk"


def eu(a, rows):
    """The EU address of US address a, or None when the table proves none."""
    for r in rows:
        if r["kind"] == "site" and a == r["us_start"]:
            return r["eu_start"]
    for r in rows:
        if r["kind"] == "changed" and r["us_start"] <= a < r["us_end"]:
            return r["eu_start"] if is_function(r) and a == r["us_start"] else None
    for r in rows:
        if r["kind"] == "range" and r["us_start"] <= a < r["us_end"]:
            return a - r["us_start"] + r["eu_start"]
    return a if a < EBOOT_LO or a >= EBOOT_HI else None


# ------------------------------------------------------------------------------------------------------------ header
def header(rows):
    """port/kz1hd/edition.h: KZ_GA() as one constant expression, so it works in case labels and plain C."""
    out = [
        "/*",
        " * Killzone HD's editions: the guest addresses the port names (always the US EBOOT's, NPUA80856) and where",
        " * they are in the edition this build runs (KZ_EDITION_EU=1: the EU EBOOT, NPEA00418). Written by",
        " * tools/edition_map.py from re/eu/address_map.tsv; never edit it by hand (the host tests check it).",
        " *",
        " * KZ_GA(a): the address a (a constant from the US EBOOT's code or rodata) in this edition. Data and bss",
        " * addresses (0x5C0000 and up) are the same in both and need no KZ_GA. An address inside code that differs",
        " * between the editions and that no site row proves gives KZ_GA_NONE, which no guest address equals",
        " * (tools/tests/test_edition_map.py fails on any such use). Function names (func_XXXXXXXX) stay the US ones.",
        " */",
        "#ifndef KZ_EDITION_H",
        "#define KZ_EDITION_H",
        "#include <stdint.h>",
        "",
        "#ifndef KZ_EDITION_EU",
        "#define KZ_EDITION_EU 0",
        "#endif",
        "#define KZ_GA_NONE 0xFFFFFFFFu",
        "",
        "#if KZ_EDITION_EU",
        "#define KZ_GA(a) KZ_GA_EU_((uint32_t)(a))",
        "#define KZ_GA_EU_(a) ( \\",
    ]
    x = lambda v: f"0x{v:08X}u"
    for r in rows:
        if r["kind"] == "site":
            out.append(f"    (a) == {x(r['us_start'])} ? {x(r['eu_start'])} : \\")
    for r in rows:
        if is_function(r):
            out.append(f"    (a) == {x(r['us_start'])} ? {x(r['eu_start'])} : \\")
    for r in rows:
        if r["kind"] == "changed":
            out.append(f"    (a) >= {x(r['us_start'])} && (a) < {x(r['us_end'])} ? KZ_GA_NONE : \\")
    out.append(f"    (a) < {x(EBOOT_LO)} ? (a) : \\")
    end = EBOOT_LO
    for r in (r for r in rows if r["kind"] == "range"):
        if r["us_start"] > end:
            out.append(f"    (a) < {x(r['us_start'])} ? KZ_GA_NONE : \\")
        d = r["eu_start"] - r["us_start"]
        out.append(f"    (a) < {x(r['us_end'])} ? " + ("(a)" if d == 0 else f"(a) {'-' if d < 0 else '+'} {abs(d)}u") + " : \\")
        end = r["us_end"]
    out += [f"    (a) < {x(EBOOT_HI)} ? KZ_GA_NONE : (a))" if end < EBOOT_HI else "    (a))",
            "#else",
            "#define KZ_GA(a) ((uint32_t)(a))",
            "#endif",
            "#endif",
            ""]
    return "\n".join(out)


# --------------------------------------------------------------------------------------------------------- inventory
# Values in the code segment's span that port/kz1hd writes in code but that are not EBOOT addresses, so take no KZ_GA:
# file -> values (each with why).
NOT_ADDRESSES = {
    "port/kz1hd/main.cpp": {0x10000},                                     # a page size
    "port/kz1hd/native/6b71.c": {0x10000},                                # an SPU local-store bound
    "port/kz1hd/native/flashmp3.c": {0x40000, 0x10600, 0x10000, 0x10010, 0x103E0, 0x10460, 0x10480},   # SPU ELF layout
    "port/kz1hd/native/jm.c": {0x10000, 0xF1000},
    "port/kz1hd/native/mlaa_ref.c": {0x7FFFF},                            # a mask                         # a size, guest bound; an RSX IO offset
    "port/kz1hd/native_ppu/hud.cpp": {0x10000},                           # a stack span
    "port/kz1hd/native_ppu/psgl_vp.cpp": {0x81E9C, 0x41FF0, 0x41EF8, 0x41478, 0x41FF4, 0x141EFC, 0x441EFC, 0x341EFC,
                                          0x30FFFF, 0x20FFFF, 0x3FFFC, 0x10000},   # RSX method headers, masks, a size
    "port/kz1hd/native_ppu/verify.cpp": {0x100000},                       # a stack span
    "port/kz1hd/native_ppu/clock.cpp": {0x540000},   # r4/r5 as the game leaves them: lis 0x54 in both editions
    "port/kz1hd/stubs.cpp": {0x21D20},                                    # an SPU local-store address
}


def table_line(line):
    """A line of a data table (SPIR-V words, MLAA constants): nothing but three or more hex numbers. Not addresses."""
    nums = re.findall(r"\b0x[0-9A-Fa-f]+[uUlL]*\b", line)
    return len(nums) >= 3 and not re.sub(r"\b0x[0-9A-Fa-f]+[uUlL]*\b|[\s,{};]", "", line)


LITERAL = re.compile(r"\b(?:0x([0-9A-Fa-f]{5,8})[uUlL]*|func_([0-9A-Fa-f]{8}))\b")


def strip_comments(src, hash_comments=False):
    """src with its comments blanked (lines and columns kept), so what is left is code. C and C++; with hash_comments,
    Python and shell (# to the end of the line)."""
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("//", i) and not hash_comments or hash_comments and src[i] == "#":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i) and not hash_comments:
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        elif src[i] in "\"'":
            q, j = src[i], i + 1
            while j < n and src[j] != q and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def sources():
    """(path relative to the root, text with only its code, the whole text): the files that may cite guest addresses.
    Patches: only their added lines."""
    out = []
    for d, _, files in sorted(os.walk(os.path.join(ROOT, "port", "kz1hd"))):
        for f in sorted(files):
            if f.endswith((".c", ".cpp", ".h")) and f != "edition.h":
                p = os.path.join(d, f)
                src = open(p, encoding="utf-8", errors="replace").read()
                out.append((os.path.relpath(p, ROOT), strip_comments(src), src))
    pd = os.path.join(ROOT, "port", "patches")
    for f in sorted(os.listdir(pd)):
        if f.endswith(".patch"):
            lines = open(os.path.join(pd, f), encoding="utf-8", errors="replace").read().split("\n")
            added = "\n".join(line[1:] if line.startswith("+") and not line.startswith("+++") else "" for line in lines)
            out.append((f"port/patches/{f}", strip_comments(added), added))
    for f in ("tools/kzbuild.py", "tools/extract_spu_modules.py"):
        src = open(os.path.join(ROOT, f), encoding="utf-8").read()
        out.append((f, strip_comments(src, hash_comments=True), src))
    return out


def inventory(rows):
    """Every literal that may be an EBOOT address: (file, line, text, US value, EU value or None, in code, class).
    class: code (moves), data (same in EU) or name (a func_ name)."""
    out = []
    for path, code, src in sources():
        for n, (line, cline) in enumerate(zip(src.split("\n"), code.split("\n")), 1):
            if table_line(line):
                continue
            patch = path.startswith("port/patches/")
            for m in LITERAL.finditer(line):
                v = int(m.group(1) or m.group(2), 16)
                if not EBOOT_LO <= v < EBOOT_HI or not m.group(2) and (patch or v in NOT_ADDRESSES.get(path, ())):
                    continue   # the patches' numbers are masks, sizes, IO offsets and test values: only names count
                cls = "name" if m.group(2) else "code" if v < CODE_HI else "data"
                out.append((path, n, m.group(0), v, eu(v, rows), bool(cline[m.start():m.end()].strip()), cls))
    return out


def ga_args():
    """The constants port/kz1hd passes to KZ_GA: (file, line, value)."""
    out = []
    for path, code, _ in sources():
        if path.startswith("port/kz1hd/"):
            for n, line in enumerate(code.split("\n"), 1):
                out += [(path, n, int(m.group(1), 16)) for m in re.finditer(r"KZ_GA\(\s*0x([0-9A-Fa-f]+)[uU]?\s*\)",
                                                                             line)]
    return out


# ------------------------------------------------------------------------------------------------------------- check
DS = set(range(7, 16)) - {9, 10, 11} | set(range(24, 30)) | set(range(32, 56)) | {58, 62}


def norm(x):
    """An instruction word with what relocation changes masked: a branch's displacement, a D/DS-form's low 16 bits."""
    op = x >> 26
    return x & 0xFC000003 if op == 18 else x & 0xFFFF0003 if op == 16 else x & 0xFFFF0000 if op in DS else x


def sections(img):
    """(vaddr, size, flags, type) of an ELF64 big-endian image's sections."""
    shoff, = struct.unpack_from(">Q", img, 0x28)
    shentsize, shnum = struct.unpack_from(">HH", img, 0x3A)
    out = []
    for i in range(shnum):
        _, typ, flags, addr, _, size = struct.unpack_from(">IIQQQQ", img, shoff + i * shentsize)
        out.append((addr, size, flags, typ))
    return out


def check(us_path, eu_path, rows):
    """Problems found comparing the two EBOOTs through the table (empty: the table holds). Every word with bytes in
    a range row, outside the changed rows, must be the same up to relocation: in code the same instruction (a branch
    to the mapped target), elsewhere the same word or a pointer into code or rodata that moved with the table."""
    ub, eb = open(us_path, "rb").read(), open(eu_path, "rb").read()
    word = lambda b, a: struct.unpack_from(">I", b, a - 0x10000)[0]   # both LOAD segments: file offset = vaddr - 0x10000
    problems = []
    us_s, eu_s = sections(ub), sections(eb)
    if len(us_s) != len(eu_s):
        return [f"{len(us_s)} sections in US, {len(eu_s)} in EU"]
    for i, ((ua, un, fl, _), (ea, en, _, _)) in enumerate(zip(us_s, eu_s)):
        if fl & 2 and ua >= EBOOT_LO and (eu(ua, rows) != ea or un != en and i != 2):
            problems.append(f"section {i}: US 0x{ua:X}+0x{un:X} maps to {eu(ua, rows)}, EU is 0x{ea:X}+0x{en:X}")
    code = [(a, a + n) for a, n, fl, typ in us_s if fl & 4]                                      # SHF_EXECINSTR
    bytes_at = [(a, a + n) for a, n, fl, typ in us_s if fl & 2 and typ == 1 and a >= EBOOT_LO]   # PROGBITS, loaded
    changed = [(r["us_start"], r["us_end"]) for r in rows if r["kind"] == "changed"]
    bad = 0
    for r in (r for r in rows if r["kind"] == "range"):
        for lo, hi in bytes_at:
            a, end = max(lo, r["us_start"]), min(hi, r["us_end"])
            a += -a % 4
            while a + 4 <= end:
                c = next((c for c in changed if c[0] <= a < c[1]), None)
                if c:
                    a = c[1]
                    continue
                e = a - r["us_start"] + r["eu_start"]
                u, v = word(ub, a), word(eb, e)
                ok = u == v
                if not ok and any(lo <= a < hi for lo, hi in code):
                    op = u >> 26
                    ok = norm(u) == norm(v)
                    if ok and op in (16, 18) and not u & 2:
                        m, sx = (0x03FFFFFC, 0x02000000) if op == 18 else (0xFFFC, 0x8000)
                        tu, te = a + (u & m) - ((u & sx) << 1), e + (v & m) - ((v & sx) << 1)
                        ok = eu(tu, rows) == te or any(c[0] <= tu < c[1] for c in changed)
                elif not ok:
                    ok = EBOOT_LO <= u < CODE_HI and eu(u, rows) == v    # a pointer into code or rodata, moved
                if not ok:
                    bad += 1
                    if bad <= 20:
                        problems.append(f"0x{a:08X} (EU 0x{e:08X}): 0x{u:08X} against 0x{v:08X}")
                a += 4
    if bad > 20:
        problems.append(f"... {bad} words in all")
    dis = lambda x: (x & 0x03FFFFFC) - ((x & 0x02000000) << 1)
    for s in (r for r in rows if r["kind"] == "site"):
        u, v = word(ub, s["us_start"] - 4), word(eb, s["eu_start"] - 4)
        if u >> 26 != 18 or u & 3 != 1 or v >> 26 != 18 or v & 3 != 1:
            problems.append(f"site 0x{s['us_start']:08X}: not the return of a bl on both sides")
            continue
        tu, te = s["us_start"] - 4 + dis(u), s["eu_start"] - 4 + dis(v)
        if eu(tu, rows) != te:
            problems.append(f"site 0x{s['us_start']:08X}: calls 0x{tu:X} in US, 0x{te:X} in EU")
    for r in (r for r in rows if is_function(r)):
        if norm(word(ub, r["us_start"])) != norm(word(eb, r["eu_start"])):
            problems.append(f"changed 0x{r['us_start']:08X}: its first instruction differs at EU 0x{r['eu_start']:08X}")
    return problems


def main(argv):
    rows = load()
    if argv[:1] == ["header"]:
        text = header(rows)
        if argv[1:] == ["--check"]:
            cur = open(HEADER, encoding="utf-8").read() if os.path.exists(HEADER) else ""
            if cur != text:
                print("edition_map: port/kz1hd/edition.h is stale: run python3 tools/edition_map.py header",
                      file=sys.stderr)
                return 1
            return 0
        open(HEADER, "w", encoding="utf-8").write(text)
        return 0
    if argv[:1] == ["eu"] and argv[1:]:
        for a in argv[1:]:
            v = eu(int(a, 16), rows)
            print(f"0x{int(a, 16):08X} {'none' if v is None else f'0x{v:08X}'}")
        return 0
    if argv == ["inventory"]:
        print("file\tline\tliteral\tus\teu\tin_code\tclass")
        for path, n, text, v, e, in_code, cls in inventory(rows):
            print(f"{path}\t{n}\t{text}\t0x{v:08X}\t{'none' if e is None else f'0x{e:08X}'}\t"
                  f"{'code' if in_code else 'comment'}\t{cls}")
        return 0
    if argv[:1] == ["check"] and len(argv) == 3:
        problems = check(argv[1], argv[2], rows)
        for p in problems:
            print("edition_map:", p, file=sys.stderr)
        print("edition_map: the table holds for both EBOOTs" if not problems else f"edition_map: {len(problems)} problems")
        return 1 if problems else 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
