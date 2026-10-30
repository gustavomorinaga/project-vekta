# Lua 4.0 bytecode (KillZoneScript / MissionScript) loader and disassembler.
# Chunk layout follows lua-4.0 lundump.c; this build is big-endian with 4-byte float numbers and the
# "Lua" signature dropped (header: 1b 40 01 04 04 04 20 06 09 04 + test number 314159265.0).
# One opcode, STMT, is inserted at 13 compared to stock 4.0: no operands, no stack effect, emitted at
# the start of statements (a line/debug hook marker). Treat it as a no-op.
# Usage: python3 lua4dis.py <file.core> [--calls|--src]   (--calls: global/method names used; --src: rough decompile)
import struct, sys

OPS = ('END RETURN CALL TAILCALL PUSHNIL POP PUSHINT PUSHSTRING PUSHNUM PUSHNEGNUM PUSHUPVALUE GETLOCAL '
       'GETGLOBAL STMT GETTABLE GETDOTTED GETINDEXED PUSHSELF CREATETABLE SETLOCAL SETGLOBAL SETTABLE SETLIST '
       'SETMAP ADD ADDI SUB MULT DIV POW CONCAT MINUS NOT JMPNE JMPEQ JMPLT JMPLE JMPGT JMPGE JMPT JMPF '
       'JMPONT JMPONF JMP PUSHNILJMP FORPREP FORLOOP LFORPREP LFORLOOP CLOSURE').split()
MAXARG_S = (1 << 25) - 1
# operand kind per opcode: U unsigned, S signed, K string const, N number const, J jump, AB two args
KIND = dict(RETURN='U', CALL='AB', TAILCALL='AB', PUSHNIL='U', POP='U', PUSHINT='S', PUSHSTRING='K',
            PUSHNUM='N', PUSHNEGNUM='N', PUSHUPVALUE='U', GETLOCAL='U', GETGLOBAL='K', GETDOTTED='K',
            GETINDEXED='U', PUSHSELF='K', CREATETABLE='U', SETLOCAL='U', SETGLOBAL='K', SETTABLE='AB',
            SETLIST='AB', SETMAP='U', ADDI='S', CONCAT='U', CLOSURE='AB')
for j in 'JMPNE JMPEQ JMPLT JMPLE JMPGT JMPGE JMPT JMPF JMPONT JMPONF JMP FORPREP FORLOOP LFORPREP LFORLOOP'.split():
    KIND[j] = 'J'


class Func:
    pass


class Loader:
    def __init__(self, d, o):
        self.d, self.o = d, o

    def int(self):
        v = struct.unpack_from('>i', self.d, self.o)[0]; self.o += 4
        return v

    def byte(self):
        self.o += 1
        return self.d[self.o - 1]

    def str(self):
        n = self.int()
        s = self.d[self.o:self.o + n - 1].decode('latin1') if n else None
        self.o += n
        return s

    def func(self):
        f = Func()
        f.source, f.line, f.nparams, f.vararg, f.maxstack = self.str(), self.int(), self.int(), self.byte(), self.int()
        f.locals = [(self.str(), self.int(), self.int()) for _ in range(self.int())]
        f.lines = [self.int() for _ in range(self.int())]
        f.kstr = [self.str() for _ in range(self.int())]
        n = self.int()
        f.knum = list(struct.unpack_from(f'>{n}f', self.d, self.o)); self.o += 4 * n
        f.kproto = [self.func() for _ in range(self.int())]
        n = self.int()
        f.code = list(struct.unpack_from(f'>{n}I', self.d, self.o)); self.o += 4 * n
        assert f.code and f.code[-1] & 63 == 0, 'code does not end with OP_END'
        return f


def load(d):
    o = d.index(b'\x1b\x40\x01\x04\x04\x04\x20\x06\x09\x04')
    assert struct.unpack_from('>f', d, o + 10)[0] == 314159264.0  # 314159265 as f32
    return Loader(d, o + 14).func()


def decode(i):
    op, u = OPS[i & 63], i >> 6
    return op, u, u - MAXARG_S, (i >> 15), (i >> 6) & 511


def dis(f, depth=0, out=print):
    pad = '  ' * depth
    out(f'{pad}function line {f.line} params {f.nparams}{"..." if f.vararg else ""} stack {f.maxstack} '
        f'locals {[l[0] for l in f.locals]}')
    for pc, i in enumerate(f.code):
        op, u, s, a, b = decode(i)
        k = KIND.get(op)
        arg = {'U': u, 'S': s, 'K': repr(f.kstr[u]) if k == 'K' else None, 'N': f.knum[u] if k == 'N' else None,
               'J': f'-> {pc + 1 + s}', 'AB': f'{a} {b}', None: ''}[k]
        out(f'{pad}  {pc:4} {op:11} {arg}')
    for p in f.kproto:
        dis(p, depth + 1, out)


def calls(f, acc):
    """Names used as call targets: GETGLOBAL/GETDOTTED/PUSHSELF names (rough but good for an API inventory)."""
    for i in f.code:
        op, u, *_ = decode(i)
        if op in ('GETGLOBAL', 'PUSHSELF', 'GETDOTTED'):
            acc[(op, f.kstr[u])] = acc.get((op, f.kstr[u]), 0) + 1
    for p in f.kproto:
        calls(p, acc)
    return acc



def pseudo(f, name='main', depth=0, out=print):
    """Rough decompile: rebuilds expressions on a symbolic stack, prints control flow as gotos.
    Locals are unnamed in these chunks (no debug info): l0.. (params first), upvalues u0.."""
    pad = '  ' * depth
    out(f'{pad}function {name}({", ".join(f"l{i}" for i in range(f.nparams))}{", ..." if f.vararg else ""})')
    st, targets = [], set()
    nloc = f.nparams + (1 if f.vararg else 0)
    for pc, i in enumerate(f.code):
        op, u, s, a, b = decode(i)
        if KIND.get(op) == 'J':
            targets.add(pc + 1 + s)
    def emit(text, pc):
        out(f'{pad}  {pc:4}  {text}')
    def pop(n=1):
        nonlocal st
        if len(st) < n:
            st = ['?'] * (n - len(st)) + st
        v = st[len(st) - n:] if n else []
        st = st[:len(st) - n]
        return v
    for pc, i in enumerate(f.code):
        if pc in targets:
            out(f'{pad}  ::L{pc}::')
        op, u, s, a, b = decode(i)
        k = lambda: repr(f.kstr[u])
        if op == 'STMT' or (op in ('GETLOCAL', 'SETLOCAL', 'GETINDEXED') and u >= nloc):
            # statement boundary (or a use of a local not yet declared): values left above the
            # declared locals are new local declarations
            keep = len(st) if op == 'STMT' else u + 1
            while nloc < keep and len(st) > nloc:
                emit(f'local l{nloc} = {st[nloc]}', pc); st[nloc] = f'l{nloc}'; nloc += 1
        if op == 'STMT':
            pass
        elif op == 'END': emit('end', pc)
        elif op == 'RETURN': emit(f'return {", ".join(st[u:])}', pc); st = st[:u]
        elif op in ('CALL', 'TAILCALL'):
            st += ['?'] * (a + 1 - len(st))
            fn, *args = st[a:]; st = st[:a]
            if args and args[0].endswith(':self'):
                args = args[1:]
            c = f'{fn}({", ".join(args)})'
            if op == 'TAILCALL': emit(f'return {c}', pc)
            elif b == 0: emit(c, pc)
            elif b == 255: st.append(c + '...')
            else: st += [c] + [f'{c}#{j}' for j in range(1, b)]
        elif op == 'PUSHNIL': st += ['nil'] * u
        elif op == 'POP':
            for v in pop(u):
                if '(' in v: emit(v, pc)
        elif op == 'PUSHINT': st.append(str(s))
        elif op == 'PUSHSTRING': st.append(k())
        elif op in ('PUSHNUM', 'PUSHNEGNUM'): st.append(('-' if op == 'PUSHNEGNUM' else '') + f'{f.knum[u]:g}')
        elif op == 'PUSHUPVALUE': st.append(f'%u{u}')
        elif op == 'GETLOCAL': st.append(f'l{u}')
        elif op == 'GETGLOBAL': st.append(f.kstr[u])
        elif op == 'GETTABLE': t, x = pop(2); st.append(f'{t}[{x}]')
        elif op == 'GETDOTTED': t, = pop(); st.append(f'{t}.{f.kstr[u]}')
        elif op == 'GETINDEXED': t, = pop(); st.append(f'{t}[l{u}]')
        elif op == 'PUSHSELF': t, = pop(); st += [f'{t}:{f.kstr[u]}', f'{t}:self']
        elif op == 'CREATETABLE': st.append('{}')
        elif op == 'SETLOCAL': v, = pop(); emit(f'l{u} = {v}', pc)
        elif op == 'SETGLOBAL': v, = pop(); emit(f'{f.kstr[u]} = {v}', pc)
        elif op == 'SETTABLE':
            v, = pop(); t, x = st[-a + 1 - 1:][:2] if a >= 2 else ('?', '?')
            emit(f'{t}[{x}] = {v}', pc); pop(b - 1)
        elif op == 'SETLIST':
            vals = pop(b); st = st or ['{}']; st[-1] = '{' + ', '.join(vals) + '}' if st[-1] == '{}' else st[-1][:-1] + ', ' + ', '.join(vals) + '}'
        elif op == 'SETMAP':
            kv = pop(2 * u); st = st or ['{}']; body = ', '.join(f'{kv[j][1:-1] if kv[j][0] == chr(39) else "[" + kv[j] + "]"}={kv[j + 1]}' for j in range(0, len(kv), 2))
            st[-1] = '{' + body + '}' if st[-1] == '{}' else st[-1][:-1] + ', ' + body + '}'
        elif op in ('ADD', 'SUB', 'MULT', 'DIV', 'POW'):
            x, y = pop(2); st.append(f'({x} {dict(ADD="+", SUB="-", MULT="*", DIV="/", POW="^")[op]} {y})')
        elif op == 'ADDI': x, = pop(); st.append(f'({x} + {s})')
        elif op == 'CONCAT': st.append('(' + ' .. '.join(pop(u)) + ')')
        elif op == 'MINUS': x, = pop(); st.append(f'-{x}')
        elif op == 'NOT': x, = pop(); st.append(f'not {x}')
        elif op in ('JMPNE', 'JMPEQ', 'JMPLT', 'JMPLE', 'JMPGT', 'JMPGE'):
            x, y = pop(2); rel = dict(JMPNE='~=', JMPEQ='==', JMPLT='<', JMPLE='<=', JMPGT='>', JMPGE='>=')[op]
            emit(f'if {x} {rel} {y} goto L{pc + 1 + s}', pc)
        elif op in ('JMPT', 'JMPF'):
            x, = pop(); emit(f'if {"" if op == "JMPT" else "not "}{x} goto L{pc + 1 + s}', pc)
        elif op in ('JMPONT', 'JMPONF'):
            x, = pop(); emit(f'if {"" if op == "JMPONT" else "not "}{x} goto L{pc + 1 + s} (keeping it)', pc)
        elif op == 'JMP': emit(f'goto L{pc + 1 + s}', pc)
        elif op == 'PUSHNILJMP': emit('push nil; skip next', pc)
        elif op == 'FORPREP':
            i0, lim, step = st[-3:]; emit(f'for l{len(st) - 3} = {i0}, {lim}, {step} do  (exit L{pc + 1 + s})', pc)
            st[-3:] = [f'l{len(st) - 3}', '(limit)', '(step)']
        elif op == 'FORLOOP': emit(f'next for, goto L{pc + 1 + s}', pc)
        elif op == 'LFORPREP':
            t, = st[-1:]; n = len(st)
            emit(f'for l{n}, l{n + 1} in {t} do  (exit L{pc + 1 + s})', pc); st[-1:] = ['(table)', '(index)', f'l{n}', f'l{n + 1}']
        elif op == 'LFORLOOP': emit(f'next for in, goto L{pc + 1 + s}', pc)
        elif op == 'CLOSURE':
            up = pop(b); st.append(f'function#{a}' + (f'[{", ".join(up)}]' if up else ''))
        else:
            emit(f'?? {op}', pc)
    for j, p in enumerate(f.kproto):
        pseudo(p, f'{name}/function#{j}', depth + 1, out)


if __name__ == '__main__':
    f = load(open(sys.argv[1], 'rb').read())
    if '--src' in sys.argv:
        pseudo(f)
    elif '--calls' in sys.argv:
        for (op, n), c in sorted(calls(f, {}).items(), key=lambda x: (x[0][0], -x[1])):
            print(f'{op:9} {c:4} {n}')
    else:
        dis(f)
