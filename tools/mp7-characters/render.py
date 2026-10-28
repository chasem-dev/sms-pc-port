"""Tiny textured software rasterizer for previews."""
import numpy as np
from PIL import Image

def render(tris_pos, tris_uv, tris_tex, textures, tris_nrm, view_rot, size=512, tint=None):
    """tris_pos: (n,3,3) world; tris_uv: (n,3,2); tris_tex: (n,) texture id or -1; tris_nrm: (n,3)."""
    P = tris_pos @ view_rot.T
    lo = P.reshape(-1, 3).min(0); hi = P.reshape(-1, 3).max(0)
    sc = (size * 0.9) / max(hi[0] - lo[0], hi[1] - lo[1])
    cx = (lo[0] + hi[0]) / 2; cy = (lo[1] + hi[1]) / 2
    X = (P[..., 0] - cx) * sc + size / 2; Y = size / 2 - (P[..., 1] - cy) * sc; Z = P[..., 2]
    img = np.full((size, size, 3), 40, np.float32); zb = np.full((size, size), -1e9)
    N = tris_nrm @ view_rot.T
    light = np.array([0.3, 0.5, 0.8]); light /= np.linalg.norm(light)
    for i in range(len(P)):
        x0, x1, x2 = X[i]; y0, y1, y2 = Y[i]
        minx = max(int(np.floor(min(x0, x1, x2))), 0); maxx = min(int(np.ceil(max(x0, x1, x2))), size - 1)
        miny = max(int(np.floor(min(y0, y1, y2))), 0); maxy = min(int(np.ceil(max(y0, y1, y2))), size - 1)
        if minx > maxx or miny > maxy: continue
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9: continue
        gx, gy = np.meshgrid(np.arange(minx, maxx + 1) + 0.5, np.arange(miny, maxy + 1) + 0.5)
        a = ((y1 - y2) * (gx - x2) + (x2 - x1) * (gy - y2)) / den
        b = ((y2 - y0) * (gx - x2) + (x0 - x2) * (gy - y2)) / den
        c = 1 - a - b
        m = (a >= 0) & (b >= 0) & (c >= 0)
        if not m.any(): continue
        z = a * Z[i, 0] + b * Z[i, 1] + c * Z[i, 2]
        sub = zb[miny:maxy+1, minx:maxx+1]
        m &= z > sub
        if not m.any(): continue
        shade = 0.45 + 0.55 * max(0.0, float(np.dot(N[i], light)))
        t = tris_tex[i]
        if t >= 0:
            tex = textures[t]; th, tw = tex.shape[:2]
            u = a * tris_uv[i, 0, 0] + b * tris_uv[i, 1, 0] + c * tris_uv[i, 2, 0]
            v = a * tris_uv[i, 0, 1] + b * tris_uv[i, 1, 1] + c * tris_uv[i, 2, 1]
            tu = (np.floor(u * tw).astype(int)) % tw; tv = (np.floor(v * th).astype(int)) % th
            col = tex[tv, tu, :3].astype(np.float32)
            alpha = tex[tv, tu, 3]
            m &= alpha > 100
        else:
            col = np.broadcast_to(np.array(tint[i] if tint is not None else (200, 200, 200), np.float32), gx.shape + (3,))
        sub[m] = z[m]
        img[miny:maxy+1, minx:maxx+1][m] = col[m] * shade
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))

def view(yaw_deg, pitch_deg=0):
    y = np.radians(yaw_deg); p = np.radians(pitch_deg)
    Ry = np.array([[np.cos(y), 0, np.sin(y)], [0, 1, 0], [-np.sin(y), 0, np.cos(y)]])
    Rx = np.array([[1, 0, 0], [0, np.cos(p), -np.sin(p)], [0, np.sin(p), np.cos(p)]])
    return Rx @ Ry
