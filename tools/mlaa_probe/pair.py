import os
import os, sys
from PIL import Image
def pair(d):
    fs=sorted(f for f in os.listdir(d) if f.endswith('.ppm'))
    for i,f in enumerate(fs):
        if '_out_' in f:
            off=f.split('_')[2]
            for g in fs[i+1:]:
                if '_in_' in g and g.split('_')[2]==off:
                    return Image.open(os.path.join(d,f)), Image.open(os.path.join(d,g))
