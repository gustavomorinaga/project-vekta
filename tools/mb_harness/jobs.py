#!/usr/bin/env python3
"""Per-job transfers of a jm_post capture: jobs(cap) -> [Job(n, module, xfers=[(kind, ea, lsa, size, bytes)])].
   jobs.py <cap> <job n>   prints that job's transfers (consecutive rows merged)."""
import sys, collections
Job = collections.namedtuple('Job', 'n module xfers')

def jobs(cap, want=None):
    data = open(f'{cap}/dma.bin', 'rb').read()
    cur, out = {}, {}
    for line in open(f'{cap}/dma.log'):
        f = line.split()
        if f[1] == 'J':
            j = Job(int(f[3]), int(f[2]), []); out[j.n] = j; cur[f[0]] = j; continue
        j = cur.get(f[0])
        if not j: continue
        ea, lsa, size, off = int(f[2], 16), int(f[3], 16), int(f[4], 16), int(f[5], 16)
        j.xfers.append((f[1], ea, lsa, size, data[off:off + size] if want is None or j.n in want else None))
    return out

if __name__ == '__main__':
    j = jobs(sys.argv[1], {int(sys.argv[2])})[int(sys.argv[2])]
    print('module', j.module)
    for k, ea, lsa, size, b in j.xfers:
        print(k, '%08X' % ea, '%05X' % lsa, '%X' % size, b[:32].hex() if size <= 0x80 else '')
