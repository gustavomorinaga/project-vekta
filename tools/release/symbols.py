#!/usr/bin/env python3
"""Names for the frames of a crash or hang report, from the player's own build: tools/release/support.py puts a
copy of each report with them into the support package.

    tools/release/symbols.py EXE REPORT...      (prints each report with the names added)

A frame of the game's own program is written `<module>+0x<offset>` (KZ1HD.exe+0x37deb0, KZ1HD+0x37deb0; the
offset is from the module's load address: the RVA on Windows). The name is the symbol of the local executable at or
before that offset, read from its own symbol table: on Windows the COFF symbol table the MinGW (lld) link keeps in
the PE (the file header's PointerToSymbolTable and NumberOfSymbols, the string table right after it), on Linux the
ELF .symtab and .strtab. Nothing else is read and nothing runs. A lifted function's C++ name
(_Z13func_0037DE70P11ppu_context) is shown as the function it was lifted from (func_0037DE70); any other name is
shown as it is. Frames in other modules, and frames past the end of a section, are left as they are. A build
without a symbol table (stripped) gives no names: the report is then copied unchanged.

Standard library only.
"""
import bisect
import re
import struct
import sys

FRAME_RX = re.compile(r"(?P<mod>[A-Za-z0-9_.-]+)\+0x(?P<off>[0-9A-Fa-f]+)")
# the simple Itanium name of a lifted function, _Z<length><name>, takes one ppu_context* argument (or none)
SIMPLE_ARGS = ("P11ppu_context", "v")
MODULES = ("kz1hd.exe", "kz1hd")


def demangle(name):
    """func_0037DE70 for _Z13func_0037DE70P11ppu_context (and _Z<n><name>v); any other name as it is."""
    m = re.match(r"_Z(\d+)", name)
    if not m:
        return name
    n, rest = int(m.group(1)), name[m.end():]
    ident, args = rest[:n], rest[n:]
    if len(ident) == n and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", ident) and args in SIMPLE_ARGS:
        return ident
    return name


class Table:
    """Symbols by offset from the module's load address, and the ranges they may cover."""

    def __init__(self, syms, ranges, kind):
        syms = sorted(syms)
        self.addrs = [a for a, _, _ in syms]
        self.syms = syms
        self.ranges = sorted(ranges)   # (start, end) of the executable sections
        self.kind = kind

    def __len__(self):
        return len(self.addrs)

    def lookup(self, off):
        """(name, delta) of the symbol at or before off within its section, or None."""
        i = bisect.bisect_right(self.addrs, off) - 1
        if i < 0:
            return None
        addr, size, name = self.syms[i]
        r = next(((s, e) for s, e in self.ranges if s <= off < e), None)
        if r is None or not r[0] <= addr < r[1]:
            return None
        if size and off >= addr + size:
            return None
        return demangle(name), off - addr


def _pe(data):
    lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[lfanew:lfanew + 4] != b"PE\0\0":
        return None
    nsec, _, symptr, nsyms, optsize, _ = struct.unpack_from("<HIIIHH", data, lfanew + 6)
    sec_at = lfanew + 24 + optsize
    secs, ranges = [], []
    for i in range(nsec):
        _, vsize, va, rawsize, _, _, _, _, _, chars = struct.unpack_from("<8sIIIIIIHHI", data, sec_at + 40 * i)
        secs.append(va)
        if chars & 0x20000000 or chars & 0x20:        # IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_CNT_CODE
            ranges.append((va, va + max(vsize, rawsize)))
    if not symptr or not nsyms or symptr + 18 * nsyms > len(data):
        return Table([], ranges, "pe (no symbol table)")
    strtab = symptr + 18 * nsyms
    syms, i = [], 0
    while i < nsyms:
        raw, value, secno, typ, cls, naux = struct.unpack_from("<8sIhHBB", data, symptr + 18 * i)
        i += 1 + naux
        if secno <= 0 or secno > len(secs) or cls not in (2, 3, 6):    # EXTERNAL, STATIC, LABEL
            continue
        if raw[:4] == b"\0\0\0\0":
            at = strtab + struct.unpack_from("<I", raw, 4)[0]
            end = data.find(b"\0", at)
            name = data[at:end if end >= 0 else len(data)]
        else:
            name = raw.rstrip(b"\0")
        name = name.decode("utf-8", "replace")
        if not name or name.startswith("."):         # section symbols (.text and its aux record)
            continue
        if cls != 2 and typ >> 4 != 2:                # a static symbol counts only as a function
            continue
        syms.append((secs[secno - 1] + value, 0, name))
    return Table(syms, ranges, "pe")


def _elf(data):
    if data[4] != 2 or data[5] != 1:                  # 64-bit, little-endian (x86-64) only
        return None
    phoff, shoff = struct.unpack_from("<QQ", data, 0x20)
    phentsize, phnum, shentsize, shnum = struct.unpack_from("<HHHH", data, 0x36)
    loads = []
    for i in range(phnum):
        ptype, _, _, vaddr = struct.unpack_from("<IIQQ", data, phoff + phentsize * i)
        if ptype == 1:                                # PT_LOAD
            loads.append(vaddr)
    base = (min(loads) & ~0xFFF) if loads else 0
    shdrs = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + shentsize * i) for i in range(shnum)]
    ranges = [(sh[3] - base, sh[3] - base + sh[5]) for sh in shdrs if sh[2] & 0x4 and sh[3]]   # SHF_EXECINSTR
    syms = []
    for sh in shdrs:
        if sh[1] != 2:                                # SHT_SYMTAB
            continue
        st = shdrs[sh[6]]
        stoff = st[4]
        for j in range(sh[5] // 24):
            name_at, info, _, shndx, value, size = struct.unpack_from("<IBBHQQ", data, sh[4] + 24 * j)
            if info & 0xF != 2 or shndx == 0 or not value:   # STT_FUNC, defined
                continue
            end = data.find(b"\0", stoff + name_at)
            syms.append((value - base, size, data[stoff + name_at:end].decode("utf-8", "replace")))
    return Table(syms, ranges, "elf" if syms else "elf (no symbol table)")


def load(path):
    """The executable's Table, or None when it is not a PE or an ELF file (or cannot be read)."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    try:
        if data[:2] == b"MZ":
            return _pe(data)
        if data[:4] == b"\x7fELF":
            return _elf(data)
    except (struct.error, IndexError):
        return None
    return None


def symbolize(text, table, modules=MODULES):
    """(text with a name after each frame of the game's own module, how many frames were named)."""
    named = 0
    out = []
    for line in text.splitlines():
        m = FRAME_RX.search(line)
        if m and table is not None and m.group("mod").lower() in modules:
            hit = table.lookup(int(m.group("off"), 16))
            if hit:
                line = f"{line}  {hit[0]}+0x{hit[1]:x}"
                named += 1
        out.append(line)
    return "\n".join(out) + ("\n" if text.endswith("\n") else ""), named


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) < 2:
        print(__doc__.split("\n\n")[1].strip(), file=sys.stderr)
        return 2
    table = load(argv[0])
    if table is None:
        print(f"symbols: {argv[0]} is not a PE or ELF file", file=sys.stderr)
        return 1
    for p in argv[1:]:
        with open(p, encoding="utf-8", errors="replace") as f:
            sys.stdout.write(symbolize(f.read(), table)[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
