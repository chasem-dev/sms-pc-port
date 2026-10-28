"""GameCube GX texture decoders -> RGBA uint8 arrays (h, w, 4)."""
import numpy as np

def _tiles(blocks, w, h, bw, bh):
    """blocks: (ntiles, bh, bw, C) in tile order -> image (h, w, C)."""
    tx = (w + bw - 1) // bw; ty = (h + bh - 1) // bh
    img = blocks[:tx*ty].reshape(ty, tx, bh, bw, -1).transpose(0, 2, 1, 3, 4).reshape(ty*bh, tx*bw, -1)
    return img[:h, :w]

def _ntiles(w, h, bw, bh): return ((w + bw - 1) // bw) * ((h + bh - 1) // bh)

def rgb565(v):
    v = v.astype(np.uint32)
    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), np.full_like(r, 255)], -1).astype(np.uint8)

def rgb5a3(v):
    v = v.astype(np.uint32)
    op = (v & 0x8000) != 0
    r1 = (v >> 10) & 31; g1 = (v >> 5) & 31; b1 = v & 31
    a2 = (v >> 12) & 7; r2 = (v >> 8) & 15; g2 = (v >> 4) & 15; b2 = v & 15
    r = np.where(op, (r1 << 3) | (r1 >> 2), r2 * 17)
    g = np.where(op, (g1 << 3) | (g1 >> 2), g2 * 17)
    b = np.where(op, (b1 << 3) | (b1 >> 2), b2 * 17)
    a = np.where(op, 255, (a2 << 5) | (a2 << 2) | (a2 >> 1))
    return np.stack([r, g, b, a], -1).astype(np.uint8)

def ia8(v):
    v = v.astype(np.uint32); a = v >> 8; i = v & 255
    return np.stack([i, i, i, a], -1).astype(np.uint8)

def decode(fmt, data, w, h, palette=None, palfmt=None):
    """fmt: GX texture format id (0 I4,1 I8,2 IA4,3 IA8,4 RGB565,5 RGB5A3,6 RGBA8,8 C4,9 C8,14 CMPR)."""
    b = np.frombuffer(data, np.uint8)
    if fmt == 0:
        n = _ntiles(w, h, 8, 8); t = b[:n*32]
        px = np.stack([t >> 4, t & 15], -1).reshape(n, 8, 8, 1) * 17
        i = _tiles(px, w, h, 8, 8)[..., 0]; return np.stack([i, i, i, i], -1).astype(np.uint8)
    if fmt == 1:
        n = _ntiles(w, h, 8, 4); i = _tiles(b[:n*32].reshape(n, 4, 8, 1), w, h, 8, 4)[..., 0]
        return np.stack([i, i, i, i], -1)
    if fmt == 2:
        n = _ntiles(w, h, 8, 4); t = b[:n*32]
        a = (t >> 4) * 17; i = (t & 15) * 17
        img = _tiles(np.stack([i, i, i, a], -1).reshape(n, 4, 8, 4), w, h, 8, 4); return img.astype(np.uint8)
    if fmt == 3:
        n = _ntiles(w, h, 4, 4); v = np.frombuffer(data, '>u2', n*16)
        return _tiles(ia8(v).reshape(n, 4, 4, 4), w, h, 4, 4)
    if fmt in (4, 5):
        n = _ntiles(w, h, 4, 4); v = np.frombuffer(data, '>u2', n*16)
        c = rgb565(v) if fmt == 4 else rgb5a3(v)
        return _tiles(c.reshape(n, 4, 4, 4), w, h, 4, 4)
    if fmt == 6:
        n = _ntiles(w, h, 4, 4); t = b[:n*64].reshape(n, 2, 16, 2)
        a = t[:, 0, :, 0]; r = t[:, 0, :, 1]; g = t[:, 1, :, 0]; bb = t[:, 1, :, 1]
        return _tiles(np.stack([r, g, bb, a], -1).reshape(n, 4, 4, 4), w, h, 4, 4)
    if fmt in (8, 9):
        pal = np.frombuffer(palette, '>u2')
        pal = {0: ia8, 1: rgb565, 2: rgb5a3}[palfmt](pal)
        if fmt == 8:
            n = _ntiles(w, h, 8, 8); t = b[:n*32]
            idx = np.stack([t >> 4, t & 15], -1).reshape(n, 8, 8)
            return _tiles(pal[np.minimum(idx, len(pal)-1)].reshape(n, 8, 8, 4), w, h, 8, 8)
        n = _ntiles(w, h, 8, 4); idx = b[:n*32].reshape(n, 4, 8)
        return _tiles(pal[np.minimum(idx, len(pal)-1)].reshape(n, 4, 8, 4), w, h, 8, 4)
    if fmt == 14:
        n = _ntiles(w, h, 8, 8)
        blk = b[:n*32].reshape(n*4, 8)
        c0 = blk[:, 0].astype(np.uint32) << 8 | blk[:, 1]; c1 = blk[:, 2].astype(np.uint32) << 8 | blk[:, 3]
        p0 = rgb565(c0).astype(np.int32); p1 = rgb565(c1).astype(np.int32)
        gt = (c0 > c1)[:, None]
        p2 = np.where(gt, (2*p0 + p1) // 3, (p0 + p1) // 2)
        p3 = np.where(gt, (p0 + 2*p1) // 3, 0)
        p3[:, 3] = np.where(gt[:, 0], 255, 0)
        pal = np.stack([p0, p1, p2, p3], 1)  # (nb, 4, 4)
        bits = blk[:, 4:8]
        idx = np.stack([(bits >> s) & 3 for s in (6, 4, 2, 0)], -1).reshape(-1, 16)
        px = np.take_along_axis(pal, idx[:, :, None].astype(np.int64), 1).reshape(n, 2, 2, 4, 4, 4)
        tile = px.transpose(0, 1, 3, 2, 4, 5).reshape(n, 8, 8, 4)
        return _tiles(tile, w, h, 8, 8).astype(np.uint8)
    raise ValueError('format %d' % fmt)

HSF_TO_GX = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 14}

def decode_hsf(bm, data):
    fmt = bm['fmt']
    if fmt in HSF_TO_GX:
        return decode(HSF_TO_GX[fmt], data[bm['ofs']:], bm['w'], bm['h'])
    palfmt = {9: 1, 10: 2, 11: 0}[fmt]
    gx = 8 if bm['pix'] <= 4 else 9
    return decode(gx, data[bm['ofs']:], bm['w'], bm['h'], bm['pal'], palfmt)
