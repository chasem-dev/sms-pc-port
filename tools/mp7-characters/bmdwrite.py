"""Write BMD (J3D2bmd3) files by replacing geometry sections of a template BMD.

Sections not given are copied from the template byte for byte (MAT3, INF1 hierarchy, string tables).
"""
import struct
import numpy as np
import j3d

GX_VA_PNMTXIDX, GX_VA_POS, GX_VA_NRM, GX_VA_TEX0, GX_VA_TEX1, GX_VA_TEX2 = 0, 9, 10, 13, 14, 15
GX_TRIANGLES = 0x90


def pad32(b, fill=b'\0'):
    return b + fill * ((-len(b)) % 32)


def section(tag, body):
    body = pad32(body)
    return tag + struct.pack('>I', len(body) + 8) + body


def vtx1(pos, nrm, uv):
    """pos/nrm (n,3) float, uv (n,2) float shared by TEX0..2."""
    fmts = [(GX_VA_POS, 1, 4, 0), (GX_VA_NRM, 0, 4, 0), (GX_VA_TEX0, 1, 4, 0), (GX_VA_TEX1, 1, 4, 0),
            (GX_VA_TEX2, 1, 4, 0), (0xFF, 1, 0, 0)]
    fl = b''.join(struct.pack('>IIIB3x', *f) for f in fmts)
    head_len = 0x40 - 8
    body = bytearray(head_len) + fl
    body = bytearray(pad32(bytes(body)))
    offs = [0]*13

    def add(slot, arr):
        nonlocal body
        offs[slot] = len(body) + 8
        body += pad32(np.asarray(arr, '>f4').tobytes())
    add(0, pos); add(1, nrm)
    for k in range(3): add(5 + k, uv)
    struct.pack_into('>I13I', body, 0, 0x40, *offs)
    return section(b'VTX1', bytes(body))


def evp1(envelopes, inv_binds):
    """envelopes: list of [(joint, weight)], inv_binds: list of 4x4 per joint."""
    if not envelopes:
        return section(b'EVP1', struct.pack('>HHIIII', 0, 0xFFFF, 0, 0, 0, 0))
    counts = bytes(len(e) for e in envelopes)
    idx = b''.join(struct.pack('>H', j) for e in envelopes for j, _ in e)
    wts = b''.join(struct.pack('>f', w) for e in envelopes for _, w in e)
    mtx = b''.join(np.asarray(m[:3], '>f4').tobytes() for m in inv_binds)
    o = 0x1C - 8 + 8
    oc = o; o += len(counts); o = (o + 1) & ~1
    oi = o; o += len(idx); o = (o + 3) & ~3
    ow = o; o += len(wts); o = (o + 31) & ~31
    om = o
    body = bytearray(om - 8 + len(mtx))
    struct.pack_into('>HHIIII', body, 0, len(envelopes), 0xFFFF, oc, oi, ow, om)
    body[oc-8:oc-8+len(counts)] = counts
    body[oi-8:oi-8+len(idx)] = idx
    body[ow-8:ow-8+len(wts)] = wts
    body[om-8:om-8+len(mtx)] = mtx
    return section(b'EVP1', bytes(body))


def drw1(entries):
    """entries: list of (is_weighted, index)."""
    n = len(entries)
    flags = bytes(1 if w else 0 for w, _ in entries)
    o_flags = 0x14
    o_data = (o_flags + n + 1) & ~1
    body = bytearray(o_data - 8 + 2*n)
    struct.pack_into('>HHII', body, 0, n, 0xFFFF, o_flags, o_data)
    body[o_flags-8:o_flags-8+n] = flags
    for k, (_, i) in enumerate(entries):
        struct.pack_into('>H', body, o_data - 8 + 2*k, i)
    return section(b'DRW1', bytes(body))


def shp1(shapes):
    """shapes: list of dict(mtype, packets=[dict(mtx=[drw...], tris=[(corner,...)...])], bbox, radius).
    corner = (slot_or_None, pos, nrm, uv)."""
    multi_desc = [(GX_VA_PNMTXIDX, 1), (GX_VA_POS, 3), (GX_VA_NRM, 3), (GX_VA_TEX0, 3), (GX_VA_TEX1, 3), (GX_VA_TEX2, 3), (0xFF, 0)]
    single_desc = multi_desc[1:]
    attr_tab = b''.join(struct.pack('>II', *a) for a in single_desc)
    multi_off = len(attr_tab)
    attr_tab += b''.join(struct.pack('>II', *a) for a in multi_desc)
    shape_recs = b''; mtx_table = []; mtx_data = b''; dls = b''; pkt_locs = b''
    npk_total = 0
    for sh in shapes:
        multi = sh['mtype'] == 3
        first_md = len(mtx_data) // 8; first_pk = npk_total
        for pk in sh['packets']:
            mtx_data += struct.pack('>HHI', pk['mtx'][0], len(pk['mtx']), len(mtx_table))
            mtx_table += pk['mtx']
            dl = bytearray()
            dl += struct.pack('>BH', GX_TRIANGLES, 3*len(pk['tris']))
            for tri in pk['tris']:
                for slot, p, n, t in tri:
                    if multi: dl += struct.pack('>B', slot*3)
                    dl += struct.pack('>HHHHH', p, n, t, t, t)
            dl = pad32(bytes(dl))
            pkt_locs += struct.pack('>II', len(dl), len(dls))
            dls += dl
            npk_total += 1
        mn, mx = sh['bbox']
        shape_recs += struct.pack('>BBHHHHH', sh['mtype'], 0xFF, len(sh['packets']), multi_off if multi else 0,
                                  first_md, first_pk, 0xFFFF)
        shape_recs += struct.pack('>f6f', sh['radius'], *mn, *mx)
    n = len(shapes)
    remap = b''.join(struct.pack('>H', i) for i in range(n))
    mtab = b''.join(struct.pack('>H', m) for m in mtx_table)
    parts = []
    o = 0x2C
    def place(b, align=4):
        nonlocal o
        o = (o + align - 1) & ~(align - 1)
        parts.append((o, b)); off = o; o += len(b); return off
    o_shape = place(shape_recs)
    o_remap = place(remap)
    o_attr = place(attr_tab)
    o_mtab = place(mtab)
    o_dl = place(dls, 32)
    o_md = place(mtx_data)
    o_pk = place(pkt_locs)
    body = bytearray(o - 8)
    struct.pack_into('>HH8I', body, 0, n, 0xFFFF, o_shape, o_remap, 0, o_attr, o_mtab, o_dl, o_md, o_pk)
    for off, b in parts: body[off-8:off-8+len(b)] = b
    return section(b'SHP1', bytes(body)), npk_total


def jnt1_patch(template_sec, translations):
    """Patch joint translations (list of (x,y,z) or None) into a copy of the template JNT1 section."""
    b = bytearray(template_sec)
    jd = struct.unpack_from('>I', b, 0xC)[0]
    for i, t in enumerate(translations):
        if t is not None:
            struct.pack_into('>3f', b, jd + i*0x40 + 0x18, *t)
    return bytes(b)


def inf1_patch(template_sec, packet_count, vertex_count):
    b = bytearray(template_sec)
    struct.pack_into('>II', b, 0xC, packet_count, vertex_count)
    return bytes(b)


def rgba8(img):
    """RGBA uint8 (h,w,4) -> GX RGBA8 tiled bytes."""
    h, w = img.shape[:2]
    t = img.reshape(h//4, 4, w//4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)
    ar = np.stack([t[:, :, 3], t[:, :, 0]], -1).reshape(-1, 32)
    gb = np.stack([t[:, :, 1], t[:, :, 2]], -1).reshape(-1, 32)
    return np.concatenate([ar, gb], 1).astype(np.uint8).tobytes()


def _to565(c):
    c = np.clip(np.round(c), 0, 255).astype(np.uint32)
    return ((c[..., 0] >> 3) << 11) | ((c[..., 1] >> 2) << 5) | (c[..., 2] >> 3)


def _from565(v):
    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.float64)


def cmpr(img):
    """RGBA uint8 (h,w,4), h and w multiples of 8 -> GX CMPR (DXT1 in 8x8 tiles) bytes.
    Endpoints from each block's principal color axis; blocks with cut-out texels use the
    3-color mode with index 3 transparent."""
    h, w = img.shape[:2]
    blk = img.reshape(h//4, 4, w//4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(h//4, w//4, 16, 4).astype(np.float64)
    col = blk[..., :3]; alpha = blk[..., 3] >= 128
    mean = col.mean(2, keepdims=True)
    cov = np.einsum('...ki,...kj->...ij', col - mean, col - mean)
    axis = np.ones(cov.shape[:-1]) / np.sqrt(3)
    for _ in range(8):
        axis = np.einsum('...ij,...j->...i', cov, axis)
        axis /= np.linalg.norm(axis, axis=-1, keepdims=True) + 1e-9
    proj = np.einsum('...ki,...i->...k', col - mean, axis)
    proj_o = np.where(alpha, proj, np.nan)
    has = alpha.any(-1)
    lo = np.where(has, np.nanmin(np.where(has[..., None], proj_o, 0), -1), 0)
    hi = np.where(has, np.nanmax(np.where(has[..., None], proj_o, 0), -1), 0)
    e0 = mean[..., 0, :] + hi[..., None] * axis
    e1 = mean[..., 0, :] + lo[..., None] * axis
    c0 = _to565(e0); c1 = _to565(e1)
    transparent = ~alpha.all(-1)
    # 4-color mode needs c0 > c1, 3-color mode c0 <= c1
    swap = np.where(transparent, c0 > c1, c0 < c1)
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    same = (c0 == c1) & ~transparent
    c1 = np.where(same & (c0 > 0), c0 - 1, c1)
    c0 = np.where(same & (c0 == 0), 1, c0)
    p0 = _from565(c0); p1 = _from565(c1)
    four = np.stack([p0, p1, (2*p0 + p1) / 3, (p0 + 2*p1) / 3], -2)
    three = np.stack([p0, p1, (p0 + p1) / 2, np.full_like(p0, 1e9)], -2)
    pal = np.where(transparent[..., None, None], three, four)
    d = ((col[..., :, None, :] - pal[..., None, :, :])**2).sum(-1)
    d[..., 3] = np.where(transparent[..., None], np.inf, d[..., 3])
    idx = d.argmin(-1)
    idx = np.where(transparent[..., None] & ~alpha, 3, idx)
    bits = idx.reshape(*idx.shape[:-1], 4, 4)
    rows = (bits[..., 0] << 6) | (bits[..., 1] << 4) | (bits[..., 2] << 2) | bits[..., 3]
    out = np.zeros(blk.shape[:2] + (8,), np.uint8)
    out[..., 0] = c0 >> 8; out[..., 1] = c0 & 255; out[..., 2] = c1 >> 8; out[..., 3] = c1 & 255
    out[..., 4:] = rows
    # 8x8 tiles of 2x2 blocks
    tiles = out.reshape(h//8, 2, w//8, 2, 8).transpose(0, 2, 1, 3, 4)
    return tiles.tobytes()


def tex1(template_sec, replacements):
    """Rebuild TEX1 keeping headers/names; replacements: {name: RGBA image} re-encoded as RGBA8."""
    t = template_sec
    n, = struct.unpack_from('>H', t, 8)
    ho, no = struct.unpack_from('>II', t, 0xC)
    names = j3d.strtab(t, no)
    headers = [bytearray(t[ho + i*0x20: ho + (i+1)*0x20]) for i in range(n)]
    datas = []; cache = {}
    for i, h in enumerate(headers):
        nm = names[i]
        if nm in replacements:
            img = replacements[nm]
            fmt = 6
            if isinstance(img, tuple): fmt, img = img
            key = ('new', nm)
            if key not in cache: cache[key] = rgba8(img) if fmt == 6 else cmpr(img)
            h[0] = fmt; struct.pack_into('>HH', h, 2, img.shape[1], img.shape[0])
            h[0x08] = 0; h[0x09] = 0; struct.pack_into('>HI', h, 0x0A, 0, 0)
            h[0x10] = 0; h[0x14] = 1; h[0x15] = 1; h[0x16] = 0; h[0x17] = 0; h[0x18] = 1
            datas.append(key)
        else:
            fmt = h[0]; w, hh = struct.unpack_from('>HH', h, 2); mips = h[0x18]
            dofs = struct.unpack_from('>I', h, 0x1C)[0]
            src = ho + i*0x20 + dofs
            size = _tex_size(fmt, w, hh, mips)
            key = ('old', src)
            if key not in cache: cache[key] = t[src:src+size]
            datas.append(key)
    name_tab = t[no:]
    body_hdr = 0x20
    o_headers = body_hdr
    o_data = o_headers + 0x20*n
    blob = bytearray(); placed = {}
    for key in datas:
        if key not in placed:
            placed[key] = len(blob); blob += pad32(bytes(cache[key]))
    o_names = o_data + len(blob)
    out = bytearray(struct.pack('>4sIHHII', b'TEX1', 0, n, 0xFFFF, o_headers, o_names)) + bytes(body_hdr - 0x14)
    for i, h in enumerate(headers):
        struct.pack_into('>I', h, 0x1C, o_data + placed[datas[i]] - (o_headers + i*0x20))
        out += h
    out += blob + name_tab
    out = pad32(bytes(out))
    out = bytearray(out); struct.pack_into('>I', out, 4, len(out))
    return bytes(out)


def _tex_size(fmt, w, h, mips):
    bpp = {0: 4, 1: 8, 2: 8, 3: 16, 4: 16, 5: 16, 6: 32, 8: 4, 9: 8, 10: 16, 14: 4}[fmt]
    bw, bh = {4: (8, 8), 8: (8, 4), 16: (4, 4), 32: (4, 4)}[bpp]
    total = 0
    for _ in range(max(mips, 1)):
        tw = (w + bw - 1)//bw*bw; th = (h + bh - 1)//bh*bh
        total += tw*th*bpp//8
        w = max(w//2, 1); h = max(h//2, 1)
    return total


def assemble(template, new_sections):
    """template: original BMD bytes; new_sections: {tag: section bytes}. Keeps section order."""
    secs = j3d.sections(template)
    order = sorted(secs.items(), key=lambda kv: kv[1][0])
    out = bytearray(template[:0x20])
    for tag, (off, size) in order:
        out += new_sections.get(tag, template[off:off+size])
    struct.pack_into('>I', out, 8, len(out))
    return bytes(out)
