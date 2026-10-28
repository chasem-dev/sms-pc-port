"""BMD -> world-space triangles (rest pose or posed), for previews."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, j3d, bmdgeo, gxtex

def textures(d):
    secs = j3d.sections(d); t, _ = secs['TEX1']
    n, = struct.unpack_from('>H', d, t+8); ho = struct.unpack_from('>I', d, t+0xC)[0]
    out = []
    for i in range(n):
        h = t + ho + i*0x20
        fmt, _, w, hh = struct.unpack_from('>BBHH', d, h)
        palfmt = d[h+9]; npal, palofs = struct.unpack_from('>HI', d, h+0xA)
        dofs, = struct.unpack_from('>I', d, h+0x1C)
        try:
            pal = d[h+palofs:h+palofs+npal*2] if npal else None
            out.append(gxtex.decode(fmt, d[h+dofs:], w, hh, pal, palfmt))
        except Exception as e:
            out.append(np.full((4, 4, 4), 255, np.uint8))
    return out

def triangles(d, joint_world=None, shapes=None):
    """Return list of (shape_idx, P(3,3), UV(3,2)). joint_world: list of 4x4 world joint matrices (posed)."""
    b = j3d.BMD(d); g = bmdgeo.read(d)
    rest = b.world()
    W = joint_world if joint_world is not None else rest
    ibm = g['ibm']
    def drw_matrix(i):
        weighted, idx = g['drw'][i]
        if not weighted: return W[idx]
        m = np.zeros((4, 4))
        for j, w in g['evp'][idx]:
            m += w * (W[j] @ ibm[j])
        return m
    pos = g['arrays'][9]; uv = g['arrays'].get(13)
    out = []
    for si, sh in enumerate(g['shapes']):
        if shapes is not None and si not in shapes: continue
        slots = [None]*10
        for pk in sh['packets']:
            for k, m in enumerate(pk['mtx']):
                if m != 0xFFFF: slots[k] = m
            for op, verts in bmdgeo.parse_dl(pk['dl'], sh['desc']):
                def P(v):
                    di = slots[v[0] // 3] if 0 in v else slots[0]
                    M = drw_matrix(di)
                    if sh['mtype'] == 3 and g['drw'][di][0] == 0:
                        pass
                    p = pos[v[9]]; return (M @ np.append(p, 1))[:3]
                pts = [P(v) for v in verts]; uvs = [uv[v[13]] if uv is not None else (0, 0) for v in verts]
                if op == 0x98:
                    for k in range(len(verts) - 2):
                        tri = (k, k+1, k+2) if k % 2 == 0 else (k+1, k, k+2)
                        out.append((si, np.array([pts[t] for t in tri]), np.array([uvs[t] for t in tri])))
                elif op == 0x90:
                    for k in range(0, len(verts), 3):
                        out.append((si, np.array(pts[k:k+3]), np.array(uvs[k:k+3])))
    return out
