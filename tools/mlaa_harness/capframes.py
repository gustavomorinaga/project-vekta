# capframes.py: for each full chain in a capture, the 1280x768 buffer before (what the first Transposes read)
# and after (that buffer with every PUT of the 14 jobs applied), as the game had them
import numpy as np
def chains(cap):
    jobs=[]
    cur=None
    for l in open(cap+'/dma.log'):
        f=l.split()
        if f[1]=='J': cur=[]; jobs.append((int(f[2]),cur)); continue
        if cur is None: continue
        if f[1]=='G' and f[3]=='00C00': cur=None; continue
        cur.append((f[1],int(f[2],16),int(f[4],16),int(f[5],16)))
    b=open(cap+'/dma.bin','rb')
    def data(r): b.seek(r[3]); return np.frombuffer(b.read(r[2]),np.uint8)
    out=[]
    for c0 in range(0,len(jobs)-13,14):
        js=jobs[c0:c0+14]
        if [m for m,_ in js]!=[203]*2+[206]*5+[203]*2+[206]*5: break
        base=[r for r in js[0][1] if r[0]=='G'][0][1]
        buf=np.zeros(0x1400*768,np.uint8); seen=np.zeros(0x1400*768,bool)
        for m,rs in js[:2]:
            for r in rs:
                if r[0]=='G' and base<=r[1]<base+len(buf): o=r[1]-base; buf[o:o+r[2]]=data(r); seen[o:o+r[2]]=True
        assert seen.all()
        before=buf.copy()
        for m,rs in js:
            for r in rs:
                if r[0]=='P' and base<=r[1]<base+len(buf): o=r[1]-base; buf[o:o+r[2]]=data(r)
        out.append((c0,base,before.reshape(768,1280,4),buf.reshape(768,1280,4)))
    return out
