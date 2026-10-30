"""Runs the harness from Python. MLAA_WORK: the directory with the captures (cap/, cap_real/, ...)
and the built harness (hb/harness); defaults to the one on the SSD."""
import numpy as np, subprocess, os, tempfile
C = os.environ.get('MLAA_WORK', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'work', 'mlaa_cap'))
os.makedirs(C + '/tmp', exist_ok=True)
T = tempfile.mkdtemp(dir=C + '/tmp')
def run(img, first=0, n=None, cap='cap', h=None):
    """img: (768|720, 1280, 4) uint8 ARGB, or (rows, 1280, 3) RGB (alpha 255).
    Returns the 1280x768 buffer after jobs first..first+n-1 of the chain (all 14 by default)."""
    img = np.asarray(img, np.uint8)
    if img.shape[2] == 3: img = np.dstack([np.full(img.shape[:2], 255, np.uint8), img])
    img.tofile(T + '/i.argb')
    env = dict(os.environ, HARNESS_FIRST=str(first), HARNESS_RAW=T + '/raw')
    if n is not None: env['HARNESS_NJOBS'] = str(n)
    subprocess.run([h or C + '/hb/harness', C + '/' + cap, 'chain', '0', T + '/i.argb', T + '/o.argb'],
                   check=True, env=env, stderr=subprocess.DEVNULL)
    return np.fromfile(T + '/raw', np.uint8).reshape(768, 1280, 4)
