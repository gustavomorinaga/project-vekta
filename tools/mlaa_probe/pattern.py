import os
# MLAA probe pattern: 8x8 tiles of 160x90 (see layout in comments)
import numpy as np, json
W,H,TW,TH=1280,720,160,90
img=np.zeros((H,W,3),np.uint8); lay={}
def tile(r,c): return img[r*TH:(r+1)*TH, c*TW:(c+1)*TW]
slopes=[1,2,3,4,5,6,8,10,12,16,20,24,32,48,64,128]
for i,n in enumerate(slopes):   # rows 0-1: near-horizontal staircase, step 1px up every n px; rows 2-3: transposed
    r,c=divmod(i,8); t=tile(r,c); t[:]=0
    for x in range(TW): y0=45 - (x - 16)//n if x>=16 else 45; t[max(0,min(TH,y0)):,x]=255
    lay[f'h{n}']=(r,c)
    r2=r+2; t=tile(r2,c); t[:]=0
    for y in range(TH):
        x0=80 - (y - 10)//n if y>=10 else 80; t[y,max(0,min(TW,x0)):]=255
    lay[f'v{n}']=(r2,c)
ds=[2,4,6,8,10,12,16,20,24,32,40,48,64,96,128,155]
for i,d in enumerate(ds):   # rows 4-5: slope-8 staircase, gray 100 vs 100+d
    r,c=divmod(i,8); r+=4; t=tile(r,c); t[:]=100
    for x in range(TW): y0=45-(x-16)//8 if x>=16 else 45; t[max(0,y0):,x]=100+d
    lay[f'd{d}']=(r,c)
for i,(ch,d) in enumerate([(0,32),(1,32),(2,32),(0,64),(1,64),(2,64),(0,16),(1,16)]):   # row 6: one channel
    t=tile(6,i); t[:]=100
    for x in range(TW):
        y0=45-(x-16)//8 if x>=16 else 45; t[max(0,y0):,x,ch]=100+d
    lay[f'c{"RGB"[ch]}{d}']=(6,i)
# row 7: shapes
t=tile(7,0); t[45,80]=255; lay['dot']=(7,0)
t=tile(7,1); t[45,20:140]=255; lay['line1']=(7,1)
t=tile(7,2); t[44:46,20:140]=255; lay['line2']=(7,2)
t=tile(7,3); yy,xx=np.mgrid[0:TH,0:TW]; t[((yy+xx)%2)==0]=255; lay['checker']=(7,3)
t=tile(7,4); t[50:,:]=255; t[46:50,60:100]=255; lay['bump']=(7,4)          # U shape (bump up)
t=tile(7,5); t[50:,:80]=255; t[46:,80:]=255; lay['zstep4']=(7,5)           # single step of 4 px
t=tile(7,6); t[50:,:80]=255; t[49:,80:]=255; lay['zstep1']=(7,6)
t=tile(7,7); yy,xx=np.mgrid[0:TH,0:TW]; t[(xx-80)**2+(yy-45)**2<35**2]=255; lay['disc']=(7,7)
with open(os.environ.get('OUT','.')+'/pattern.ppm','wb') as f:
    f.write(b'P6 1280 720 255\n'); f.write(img.tobytes())
json.dump(lay,open(os.environ.get('OUT','.')+'/layout.json','w'))
