#!/usr/bin/env python3
"""Symbolic replay of a lifted SPU leaf loop (integer byte/word ops) to recover
what each output byte is, in terms of local-store input bytes.
usage: sym.py <spu_recomp.c> <func> <capture.bin> <iters> [stream regs...]"""
import re, sys, struct
src, fn, cap, iters = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
streams = [int(x) for x in sys.argv[5:]]
text = open(src).read()
body = re.search(r'^void %s\(spu_context\* ctx\) \{\n(.*?)\n\}' % re.escape(fn), text, re.S | re.M).group(1)
lines = [l.strip() for l in body.split('\n') if l.strip() and 'nop' not in l and 'hint' not in l and not l.strip().startswith('loc_')]
# the loop body ends at its back edge; what follows runs once, after the loop
for _i, _l in enumerate(lines):
    if re.match(r'if \(.*\) goto loc_', _l):
        lines = lines[:_i + 1]; break
d = open(cap, 'rb').read()
def capreg(n): return list(d[n*16:n*16+16])  # host order: word lanes LE
# value: list of 4 word-exprs or 16 byte-exprs; we keep words as ('w', lin) where lin=(frozenset items,const) or ('shr',e,k)
# Represent a register as 16 bytes, SPU order (byte 0 = MSB of word 0). Byte = expression.
def const_reg(n):
    b = capreg(n); out = []
    for w in range(4):
        word = b[w*4:w*4+4][::-1]  # host LE -> SPU BE bytes
        out += [('c', x) for x in word]
    return out
CONST = {}
state = {}
# words <-> bytes
def word_of(r, w):
    bs = r[w*4:w*4+4]
    if all(b[0] == 'c' for b in bs):
        return L({}, (bs[0][1] << 24) | (bs[1][1] << 16) | (bs[2][1] << 8) | bs[3][1])
    if all(b == ('c', 0) for b in bs[:3]):
        x = bs[3]
        if x[0] == 'lo': return x[1]
        return L({x: 1}, 0)
    if all(b[0] in ('wb','lo') for b in bs) and all(b[1] == bs[0][1] for b in bs) and [b[2] for b in bs[:3]] == [0, 1, 2] and bs[3][0] == 'lo':
        return bs[0][1]
    return ('opaque', tuple(bs))
def bytes_of_word(e):
    if e[0] == 'lin' and not e[1]:
        v = e[2] & 0xFFFFFFFF; return [('c', (v >> s) & 255) for s in (24, 16, 8, 0)]
    return [('wb', e, k) for k in range(3)] + [('lo', e)]
def mk(words): 
    out = []
    for e in words: out += bytes_of_word(e)
    return out
def L(m, c): return ('lin', tuple(sorted(((k, v) for k, v in m.items() if v), key=lambda kv: repr(kv[0]))), c)
def lin(e):
    if e[0] == 'lin': return e
    return L({e: 1}, 0)
def add(a, b):
    a, b = lin(a), lin(b); m = dict(a[1])
    for k, v in b[1]: m[k] = m.get(k, 0) + v
    return L(m, a[2] + b[2])
def scale(a, s):
    a = lin(a); return L({k: v * s for k, v in a[1]}, a[2] * s)
def low16_const(e):
    if e[0] == 'lin' and not e[1]: v = e[2] & 0xFFFF; return v - 0x10000 if v & 0x8000 else v
    return None
reads = []
def op(name, args):
    if name == 'mpya':
        a, b, c = args; out = []
        for w in range(4):
            ka = low16_const(word_of(a, w)); kb = low16_const(word_of(b, w))
            if kb is not None: p = scale(word_of(a, w), kb)
            elif ka is not None: p = scale(word_of(b, w), ka)
            else: p = ('mul', word_of(a, w), word_of(b, w))
            out.append(add(p, word_of(c, w)))
        return mk(out)
    if name == 'a': return mk([add(word_of(args[0], w), word_of(args[1], w)) for w in range(4)])
    if name == 'rotmi':
        return mk([('shr', word_of(args[0], w), -args[1]) for w in range(4)])
    if name == 'shufb':
        a, b, c = args; out = []
        for i in range(16):
            s = c[i]
            assert s[0] == 'c', s
            s = s[1]
            if s & 0x80: out.append(('c', 0xFF if (s & 0xE0) == 0xC0 else (0x80 if (s & 0xE0) == 0xE0 else 0)))
            else: out.append((a + b)[s & 0x1F])
        return out
    if name == 'or':
        a, b = args; out = []
        for x, y in zip(a, b):
            if x == ('c', 0): out.append(y)
            elif y == ('c', 0): out.append(x)
            else: out.append(('or', x, y))
        return out
    if name in ('fa','fs','fm','fma','fms','fnms','mpys','mpy','sf','and','xor','cgt','clgt','ceq','selb','rotqbyi','cuflt','csflt','cflts','cfltu','frest','fi','rotmai','shli','andi','ori','il','ilhu','iohl','sfi','mpyu','mpyh','roti','rotqbii'):
        if name in ('rotqbyi',):
            a, k = args; k &= 15; return [a[(i + k) & 15] for i in range(16)]
        words = []
        for w in range(4):
            ws = tuple(word_of(x, w) if isinstance(x, list) else ('lin', (), x) for x in args)
            words.append(('f', name, ws))
        return mk(words)
    raise KeyError(name)
# initial state: constants from capture for regs not written in loop; written regs symbolic init
written = set(int(x) for x in re.findall(r'r(\d+) = ', ' '.join(re.sub(r'ctx->gpr\[(\d+)\]', r'r\1', l) for l in lines)))
for n in range(128):
    state[n] = [('init', n, i) for i in range(16)] if n in written else const_reg(n)
ptr = {}  # stream reg -> word0 numeric value (tracked numerically)
for n in streams: ptr[n] = struct.unpack_from('<I', d, n*16)[0]
base = dict(ptr)
writes = []
def numval(r):
    if r in ptr: return ptr[r]
    assert r not in written, 'data-dependent address via r%d' % r
    return struct.unpack_from('<I', d, r*16)[0]
def addr_expr(s):
    m = re.match(r'ctx->gpr\[(\d+)\]\._u32\[0\] \+ ctx->gpr\[(\d+)\]\._u32\[0\]$', s.strip())
    if m:
        a, b = int(m.group(1)), int(m.group(2))
        if a in ptr: return a, numval(b)
        return b, numval(a)
    m = re.match(r'ctx->gpr\[(\d+)\]\._u32\[0\] \+ (-?0x[0-9A-Fa-f]+|-?\d+)$', s.strip())
    if m: return int(m.group(1)), int(m.group(2), 0)
    m = re.match(r'ctx->gpr\[(\d+)\]\._u32\[0\]$', s.strip())
    return int(m.group(1)), 0
for it in range(iters):
    for l in lines:
        m = re.match(r'ctx->gpr\[(\d+)\] = spu_ls_read128\(ctx, (.*)\);', l)
        if m:
            r, off = addr_expr(m.group(2)); a = ptr[r] + off
            state[int(m.group(1))] = [('in', a + i) for i in range(16)]; continue
        m = re.match(r'spu_ls_write128\(ctx, (.*), ctx->gpr\[(\d+)\]\);', l)
        if m:
            r, off = addr_expr(m.group(1)); a = ptr[r] + off
            writes.append((it, a, state[int(m.group(2))])); continue
        m = re.match(r'ctx->gpr\[(\d+)\] = spu_ai\(ctx->gpr\[(\d+)\], (-?\d+)\);', l)
        if m:
            dst, s_, k = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if s_ in ptr: ptr[dst] = ptr[s_] + k
            state[dst] = mk([add(word_of(state[s_], w), L({}, k)) for w in range(4)]); continue
        m = re.match(r'ctx->gpr\[(\d+)\] = spu_(?:rotqbii|ori|rotqbyi|shlqbii)\(ctx->gpr\[(\d+)\], 0\);', l)
        if m:
            dst, s_ = int(m.group(1)), int(m.group(2))
            if s_ in ptr: ptr[dst] = ptr[s_]
            elif dst in ptr: del ptr[dst]
            state[dst] = list(state[s_]); continue
        m = re.match(r'ctx->gpr\[(\d+)\] = spu_(\w+)\((.*)\);', l)
        if m:
            dst, name, args = int(m.group(1)), m.group(2), m.group(3)
            av = []
            for a in [x.strip() for x in args.split(',')]:
                mm = re.match(r'ctx->gpr\[(\d+)\]$', a)
                av.append(state[int(mm.group(1))] if mm else int(a, 0))
            state[dst] = op(name, av); continue
        if l.startswith('if') or l.startswith('SPU_RET'): continue
        raise SystemExit('unhandled: ' + l)
def show(e, depth=0):
    if e[0] == 'c': return '%d' % e[1]
    if e[0] == 'in': return 'L[%#x]' % e[1]
    if e[0] == 'lo': return 'lo(%s)' % show(e[1])
    if e[0] == 'lin':
        t = ['%s%s' % (('%d*' % v) if v != 1 else '', show(k)) for k, v in e[1]]
        if e[2]: t.append(str(e[2]))
        return '(' + ' + '.join(t) + ')'
    if e[0] == 'shr': return '(%s >> %d)' % (show(e[1]), e[2])
    if e[0] == 'init': return 'I%d.%d' % (e[1], e[2])
    if e[0] == 'f': return '%s(%s)' % (e[1], ', '.join(show(x) for x in e[2]))
    if e[0] == 'opaque': return 'OPQ[' + ','.join(show(b) for b in e[1]) + ']'
    if e[0] == 'wb': return 'b%d(%s)' % (e[2], show(e[1]))
    return str(e)[:80]
for it, a, v in writes:
    if it >= iters - 1:
        print('iter %d write %#x:' % (it, a))
        for i in range(16):
            print('   byte %2d: %s' % (i, show(v[i])))
