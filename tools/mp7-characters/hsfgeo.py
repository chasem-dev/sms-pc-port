"""Turn a parsed HSF into bind-pose, world-space skinned geometry."""
import numpy as np

def rot_xyz_deg(r):
    x, y, z = np.radians(r)
    cx, sx, cy, sy, cz, sz = np.cos(x), np.sin(x), np.cos(y), np.sin(y), np.cos(z), np.sin(z)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx

def trs(pos, rot, scale):
    m = np.eye(4)
    m[:3, :3] = rot_xyz_deg(rot) @ np.diag(scale)
    m[:3, 3] = pos
    return m

def bind_matrices(h):
    """World bind matrix per object index (skeleton section overrides base transforms)."""
    n = len(h.objects); world = [None]*n; local = [None]*n
    for o in h.objects:
        sk = h.skeleton.get(o['name'])
        p, r, s = sk if sk else (o['pos'], o['rot'], o['scale'])
        local[o['index']] = trs(p, r, s)
    def walk(i, parent):
        world[i] = parent @ local[i]
        for c in h.objects[i]['children']: walk(c, world[i])
    for o in h.objects:
        if o['parent'] == -1: walk(o['index'], np.eye(4))
    return world, local

def mesh_geometry(h, obj, world):
    """Returns dict with world-space positions/normals, per-position weights, and triangle corners."""
    mw = world[obj['index']]
    pos = h.vertex[obj['vertex']][1].astype(np.float64)
    nrm = h.normal[obj['normal']][1].astype(np.float64) if obj['normal'] >= 0 else np.zeros((1, 3))
    st = h.st[obj['st']][1] if obj['st'] >= 0 else np.zeros((1, 2), np.float32)
    col = h.color[obj['color']][1] if obj['color'] >= 0 else None
    npos = len(pos)
    weights = [{obj['index']: 1.0} for _ in range(npos)]
    for c in range(obj['cenvNum']):
        cenv = h.cenv[obj['cenv'] + c]
        for tgt, p, pn, nn, nnum in cenv['single']:
            for k in range(p, p + pn): weights[k] = {tgt: 1.0}
        for t1, t2, ws in cenv['dual']:
            for w, p, pn, nn, nnum in ws:
                for k in range(p, p + pn):
                    weights[k] = {t1: w, t2: 1.0 - w} if t1 != t2 else {t1: 1.0}
        for p, pn, nn, nnum, ws in cenv['multi']:
            for k in range(p, p + pn):
                d = {}
                for t, w in ws: d[t] = d.get(t, 0) + w
                rest = 1.0 - sum(d.values())
                if rest > 1e-4: d[obj['index']] = d.get(obj['index'], 0) + rest
                weights[k] = d
    wpos = (mw[:3, :3] @ pos.T).T + mw[:3, 3]
    nm = np.linalg.inv(mw[:3, :3]).T
    wnrm = (nm @ nrm.T).T
    wnrm /= np.maximum(np.linalg.norm(wnrm, axis=1, keepdims=True), 1e-9)
    tris = []  # (material, [corner0, corner1, corner2]) corner = (pos, nrm, col, st)
    for typ, mat, idx in h.face[obj['face']][1]:
        mat &= 0xFFF
        c = [tuple(int(v) for v in row) for row in idx]
        if typ == 2:
            tris.append((mat, (c[0], c[2], c[1])))
        elif typ == 3:
            q = (c[0], c[2], c[3], c[1])
            tris.append((mat, (q[0], q[1], q[2]))); tris.append((mat, (q[0], q[2], q[3])))
        elif typ == 4:
            s = [c[0], c[2], c[1]] + c[3:]
            for k in range(len(s) - 2):
                a, b, d = s[k], s[k+1], s[k+2]
                tris.append((mat, (a, b, d) if k % 2 == 0 else (b, a, d)))
    return dict(name=obj['name'], pos=wpos, nrm=wnrm, st=st, col=col, weights=weights, tris=tris)

DEFAULT_SKIP = ('itemhook', 'eyelid', 'frame2', 'frame3', 'mabuta')

def model_geometry(h, skip=DEFAULT_SKIP, hidden=None, lift=0.0):
    world, local = bind_matrices(h)
    for m in world: m[1, 3] += lift
    if hidden is not None:
        keep = lambda o: o['index'] not in hidden and 'itemhook' not in o['name']
    else:
        keep = lambda o: not any(s in o['name'] for s in skip)
    meshes = [mesh_geometry(h, o, world) for o in h.objects
              if o['type'] == 2 and o['vertex'] >= 0 and o['face'] >= 0 and keep(o)]
    return meshes, world
