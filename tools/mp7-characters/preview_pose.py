"""Render a BMD posed by a BCK at given frames: preview_pose.py out.png yaw bmd bck frame [bmd bck frame ...]"""
import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, j3d, bmdtris, render
from PIL import Image
out = sys.argv[1]; yaw = float(sys.argv[2]); args = sys.argv[3:]
tiles = []
for k in range(0, len(args), 3):
    d = open(args[k], 'rb').read(); b = j3d.BMD(d)
    if args[k+1] != '-':
        anim = j3d.BCK(open(args[k+1], 'rb').read())
        W = b.world([anim.local(i, float(args[k+2])) for i in range(len(b.joints))])
    else:
        W = None
    tris = bmdtris.triangles(d, W); texs = bmdtris.textures(d)
    P = np.array([t[1] for t in tris]); UV = np.array([t[2] for t in tris])
    N = np.array([-np.cross(t[1][1]-t[1][0], t[1][2]-t[1][0]) for t in tris]); N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
    T = np.array([1 if t[0] in (1, 3) else 0 for t in tris])
    # eye shapes sample texture index 5 (eye1) in the body model
    tex_for = {0: texs[0], 1: texs[5] if len(texs) > 5 else texs[0]}
    hidden = {4, 10}
    keep = [i for i, t in enumerate(tris) if t[0] not in hidden]
    tiles.append(render.render(P[keep], UV[keep], T[keep], tex_for, N[keep], render.view(yaw, 5), 360))
sheet = Image.new('RGB', (360*len(tiles), 360))
for i, t in enumerate(tiles): sheet.paste(t, (i*360, 0))
sheet.save(out)
