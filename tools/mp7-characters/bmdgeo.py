"""Read BMD vertex/shape data (VTX1, EVP1, DRW1, SHP1) into triangles."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np, j3d

GX_ATTR = {0: 'PNMTXIDX', 1: 'TEX0MTX', 9: 'POS', 10: 'NRM', 11: 'CLR0', 12: 'CLR1', 13: 'TEX0', 14: 'TEX1', 15: 'TEX2', 0x19: 'NBT'}

def read(d):
    secs = j3d.sections(d)
    v, vsize = secs['VTX1']
    fmt_off = struct.unpack_from('>I', d, v+8)[0]
    arr_offs = struct.unpack_from('>13I', d, v+0xC)
    fmts = {}
    q = v + fmt_off
    while True:
        attr, cnt, typ, frac = struct.unpack_from('>IIIB', d, q); q += 16
        if attr == 0xFF: break
        fmts[attr] = (cnt, typ, frac)
    order = [9, 10, 0x19, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20]
    # array lengths: next nonzero offset or section end
    starts = [(a, arr_offs[i]) for i, a in enumerate(order) if arr_offs[i]]
    arrays = {}
    for k, (a, o) in enumerate(starts):
        end = starts[k+1][1] if k+1 < len(starts) else vsize
        cnt, typ, frac = fmts[a]
        comps = {9: 3 if cnt else 2, 10: 3, 11: 4, 12: 4}.get(a, 2 if cnt else 1)
        if a in (11, 12):
            arrays[a] = np.frombuffer(d, 'u1', (end - o) // 4 * 4, v + o).reshape(-1, 4); continue
        dt = {0: 'u1', 1: 'i1', 2: '>u2', 3: '>i2', 4: '>f4'}[typ]
        sz = np.dtype(dt).itemsize * comps
        arr = np.frombuffer(d, dt, (end - o) // sz * comps, v + o).reshape(-1, comps).astype(np.float64)
        if typ != 4: arr /= (1 << frac)
        arrays[a] = arr
    # DRW1 / EVP1
    w, _ = secs['DRW1']
    nd, = struct.unpack_from('>H', d, w+8)
    fo, io = struct.unpack_from('>II', d, w+0xC)
    drw = [(d[w+fo+i], struct.unpack_from('>H', d, w+io+i*2)[0]) for i in range(nd)]
    e, _ = secs['EVP1']
    ne, = struct.unpack_from('>H', d, e+8)
    co, ido, wo, mo = struct.unpack_from('>IIII', d, e+0xC)
    evp = []; ip = e + ido; wp = e + wo
    for i in range(ne):
        n = d[e+co+i]
        ids = struct.unpack_from('>%dH' % n, d, ip); ws = struct.unpack_from('>%df' % n, d, wp)
        ip += 2*n; wp += 4*n; evp.append(list(zip(ids, ws)))
    nib = len(set(j for ev in evp for j, _ in ev))
    ibm = [np.vstack([np.frombuffer(d, '>f4', 12, e+mo+i*48).reshape(3, 4), [0, 0, 0, 1]]) for i in range((secs['EVP1'][1] - mo) // 48)] if mo else []
    # SHP1
    s, _ = secs['SHP1']
    ns, = struct.unpack_from('>H', d, s+8)
    sd, remap, nametab, attrtab, mtxtab, dldata, mtxdata, pkttab = struct.unpack_from('>8I', d, s+0xC)
    shapes = []
    for i in range(ns):
        o = s + sd + i*0x28
        mtype, npk, attr_off, mdi, pki = struct.unpack_from('>BxHHHH', d, o)
        desc = []; q = s + attrtab + attr_off
        while True:
            a, t = struct.unpack_from('>II', d, q); q += 8
            if a == 0xFF: break
            desc.append((a, t))
        packets = []
        for p in range(npk):
            dsize, doff = struct.unpack_from('>II', d, s + pkttab + (pki + p)*8)
            unk, mcount, mfirst = struct.unpack_from('>HHI', d, s + mtxdata + (mdi + p)*8)
            mtx = struct.unpack_from('>%dH' % mcount, d, s + mtxtab + mfirst*2)
            dl = d[s + dldata + doff: s + dldata + doff + dsize]
            packets.append(dict(mtx=mtx, dl=dl, unk=unk))
        radius = struct.unpack_from('>f', d, o+0xC)[0]; bbox = struct.unpack_from('>6f', d, o+0x10)
        shapes.append(dict(mtype=mtype, desc=desc, packets=packets, radius=radius, bbox=bbox))
    return dict(fmts=fmts, arrays=arrays, drw=drw, evp=evp, ibm=ibm, shapes=shapes)

def parse_dl(dl, desc):
    """Yield primitives: (prim_type, [vertex dict attr->index])."""
    p = 0; prims = []
    while p < len(dl):
        op = dl[p]; p += 1
        if op == 0: continue
        n = struct.unpack_from('>H', dl, p)[0]; p += 2
        verts = []
        for _ in range(n):
            vtx = {}
            for a, t in desc:
                if t == 1: vtx[a] = dl[p]; p += 1
                elif t == 2: vtx[a] = dl[p]; p += 1
                elif t == 3: vtx[a] = struct.unpack_from('>H', dl, p)[0]; p += 2
            verts.append(vtx)
        prims.append((op & 0xF8, verts))
    return prims

if __name__ == '__main__':
    g = read(open(sys.argv[1], 'rb').read())
    print('formats', {GX_ATTR.get(a, a): f for a, f in g['fmts'].items()})
    print('arrays', {GX_ATTR.get(a, a): arr.shape for a, arr in g['arrays'].items()})
    print('drw', len(g['drw']), 'weighted', sum(1 for w, i in g['drw'] if w), 'evp', len(g['evp']), 'ibm', len(g['ibm']))
    for i, sh in enumerate(g['shapes']):
        prims = [parse_dl(pk['dl'], sh['desc']) for pk in sh['packets']]
        ops = set(op for pr in prims for op, _ in pr)
        print('shape', i, 'mtype', sh['mtype'], 'desc', [(GX_ATTR.get(a, a), t) for a, t in sh['desc']], 'packets', len(sh['packets']),
              'verts', sum(len(v) for pr in prims for _, v in pr), 'ops', [hex(o) for o in ops])
