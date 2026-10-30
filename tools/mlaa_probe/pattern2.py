import os
# MLAA probe pattern 2 (strips of 90 rows)
import numpy as np, json
W,H=1280,720
img=np.zeros((H,W,3),np.uint8); lay={}
def stair(t, L, lo, hi, y=45, x0=16):
    th,tw=t.shape[:2]
    for x in range(tw):
        y0 = y - (x-x0)//L if x>=x0 else y
        t[:,x]=lo; t[max(0,y0):,x]=hi
# strip 0: threshold fine, 8 tiles of 160
for i,(d,ch) in enumerate([(17,None),(18,None),(19,None),(17,0),(18,0),(19,0),(18,1),(18,2)]):
    t=img[0:90,i*160:(i+1)*160]
    hi=[100,100,100]
    if ch is None: hi=[100+d]*3
    else: hi[ch]=100+d
    stair(t,8,np.array([100,100,100],np.uint8),np.array(hi,np.uint8))
    lay[f'thr{d}{"" if ch is None else "RGB"[ch]}']=[0,i]
# strip 1 (y 90..179): long edge at y=135 (white below), steps up by 1 at cumulative x positions
steps=[8,24,48,88,152,248,376,568,824,1080]   # segment lengths 8,16,24,40,64,96,128,192,256,256
lvl=135
for x in range(W):
    k=sum(1 for s in steps if x>=s)
    img[90:180,x]=0; img[lvl-k:180,x]=255
lay['zsteps']=steps
# strip 2 (y 180..269): bumps (U) of length L raised 1 px on an edge at y=225, separated by 64 px
x=16; bumps=[]
img[180:270,:]=0; img[225:270,:]=255
for L in [1,2,3,4,6,8,12,16,24,32,48,64,96,128]:
    if x+L>W-16: break
    img[224,x:x+L]=255; bumps.append((x,L)); x+=L+64
lay['bumps']=bumps
# strip 3 (y 270..359): color staircases L=8 in 160 tiles
pairs=[((60,120,30),(200,90,160)),((40,40,40),(240,240,240)),((0,0,0),(128,128,128)),((128,128,128),(255,255,255)),((10,200,10),(200,10,200)),((90,90,90),(110,140,90)),((255,0,0),(0,0,255)),((50,60,70),(53,90,72))]
for i,(a,b) in enumerate(pairs):
    t=img[270:360,i*160:(i+1)*160]; stair(t,8,np.array(a,np.uint8),np.array(b,np.uint8))
lay['colors']=pairs
# strip 4 (y 360..449): shapes: isolated px, 2x2, 3x3, rectangle corners, diagonal 45 line, thin vertical line
t=img[360:450,:]; t[:]=0
t[45,40]=255; t[44:46,120:122]=255; t[43:46,200:203]=255
t[20:70,300:420]=255           # rectangle (4 L corners)
for k in range(80): t[5+k,500+k]=255   # 1px diagonal
for k in range(80): t[5+k,600+k:700]=255  # 45-degree filled staircase (L=1)
t[10:80,760]=255                  # 1 px vertical line
t[10:80,800:802]=255
lay['shapes']='px@40,405 2x2@120 3x3@200 rect@300..420x380..430 diag@500 fill45@600 vline@760 vline2@800'
# strip 5 (y 450..539): vertical counterpart of strip 1: near-vertical edge, steps left at cumulative y (only 90 rows: use x)
t=img[450:540,:]; t[:]=0
for y in range(90):
    k=sum(1 for s in [4,12,24,40,64] if y>=s)
    t[y,640-k:]=255
# strips 6-7 (y 540..719): staircases L=8 at y-offsets, period with long tiles: L = 80, 100, 120, 160 (beyond 64?)
for j,L in enumerate([80,100,128,160]):
    t=img[540+j*45:540+(j+1)*45,:]
    for x in range(W):
        y0=30 - (x//L)%20
        t[:,x]=0; t[max(0,y0):,x]=255
lay['longL']=[80,100,128,160]
with open(os.environ.get('OUT','.')+'/pattern2.ppm','wb') as f:
    f.write(b'P6 1280 720 255\n'); f.write(img.tobytes())
json.dump(lay,open(os.environ.get('OUT','.')+'/layout2.json','w'))
