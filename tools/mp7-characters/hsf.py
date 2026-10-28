"""Hudson HSF (HSFV037) model parser: objects, meshes, skinning, materials, bitmaps, motions."""
import struct
import numpy as np

SECTIONS = ['scene', 'color', 'material', 'attribute', 'vertex', 'normal', 'st', 'face', 'object',
            'bitmap', 'palette', 'motion', 'cenv', 'skeleton', 'part', 'cluster', 'shape', 'mapAttr',
            'matrix', 'symbol', 'string']

class HSF:
    def __init__(self, data):
        self.d = d = data
        assert d[:7] == b'HSFV037', d[:8]
        self.sec = {n: struct.unpack_from('>ii', d, 8 + i*8) for i, n in enumerate(SECTIONS)}
        self.load_buffers()
        self.load_bitmaps()
        self.load_materials()
        self.load_objects()
        self.load_cenv()
        self.load_skeleton()
        self.load_motion()

    def u32(self, o): return struct.unpack_from('>I', self.d, o)[0]
    def s32(self, o): return struct.unpack_from('>i', self.d, o)[0]
    def str(self, ofs):
        if ofs in (-1, 0xFFFFFFFF): return None
        base = self.sec['string'][0] + ofs
        return self.d[base:self.d.index(b'\0', base)].decode('shift_jis', 'replace')
    def sym(self, i): return self.u32(self.sec['symbol'][0] + i*4)

    def _bufs(self, name, decode):
        ofs, cnt = self.sec[name]
        out = []
        data = ofs + cnt*12
        for i in range(cnt):
            nm, n, dofs = struct.unpack_from('>Iii', self.d, ofs + i*12)
            out.append((self.str(nm), decode(data + dofs, n)))
        return out

    def load_buffers(self):
        d = self.d
        f3 = lambda o, n: np.frombuffer(d, '>f4', n*3, o).reshape(n, 3).astype(np.float32)
        f2 = lambda o, n: np.frombuffer(d, '>f4', n*2, o).reshape(n, 2).astype(np.float32)
        self.vertex = self._bufs('vertex', f3)
        skinned = self.sec['cenv'][1] > 0
        if skinned:
            self.normal = self._bufs('normal', f3)
        else:
            self.normal = self._bufs('normal', lambda o, n: np.frombuffer(d, 'i1', n*3, o).reshape(n, 3).astype(np.float32) / 64.0)
        self.st = self._bufs('st', f2)
        self.color = self._bufs('color', lambda o, n: np.frombuffer(d, 'u1', n*4, o).reshape(n, 4).copy())
        # faces
        ofs, cnt = self.sec['face']
        data = ofs + cnt*12
        self.face = []
        # strip data for every buffer is indexed from the end of the last face buffer
        last = struct.unpack_from('>Iii', d, ofs + (cnt-1)*12)
        strip_base = data + last[2] + last[1]*0x30
        for i in range(cnt):
            nm, n, dofs = struct.unpack_from('>Iii', d, ofs + i*12)
            base = data + dofs
            faces = []
            for k in range(n):
                o = base + k*0x30
                typ, mat = struct.unpack_from('>Hh', d, o)
                idx = np.frombuffer(d, '>i2', 16, o + 4).reshape(4, 4)
                if typ & 7 == 4:
                    scount, sofs = struct.unpack_from('>II', d, o + 4 + 24)
                    extra = np.frombuffer(d, '>i2', scount*4, strip_base + sofs*8).reshape(scount, 4)
                    faces.append((4, mat, np.vstack([idx[:3], extra])))
                else:
                    faces.append((typ & 7, mat, idx))
            self.face.append((self.str(nm), faces))

    def load_bitmaps(self):
        d = self.d
        pofs, pcnt = self.sec['palette']
        pdata = pofs + pcnt*16
        self.palettes = []
        for i in range(pcnt):
            nm, unk, size, dofs = struct.unpack_from('>IiII', d, pofs + i*16)
            self.palettes.append(d[pdata+dofs:pdata+dofs+size*2])
        bofs, bcnt = self.sec['bitmap']
        bdata = bofs + bcnt*0x20
        self.bitmaps = []
        for i in range(bcnt):
            o = bofs + i*0x20
            nm, maxlod, fmt, pix, w, h, palsize = struct.unpack_from('>IIBBhhh', d, o)
            tint = d[o+0x10:o+0x14]
            pal, unk, dofs = struct.unpack_from('>iII', d, o+0x14)
            self.bitmaps.append(dict(name=self.str(nm), fmt=fmt, pix=pix, w=w, h=h, palsize=palsize, tint=tint,
                                     pal=self.palettes[pal] if pal >= 0 else None, ofs=bdata+dofs))

    def load_materials(self):
        d = self.d
        aofs, acnt = self.sec['attribute']
        self.attributes = []
        for i in range(acnt):
            o = aofs + i*0x84
            nm = self.u32(o)
            sx, sy, tx, ty = struct.unpack_from('>4f', d, o+0x28)
            wrapS, wrapT = struct.unpack_from('>II', d, o+0x64)
            flag, bmp = struct.unpack_from('>Ii', d, o+0x7C)
            self.attributes.append(dict(name=self.str(nm), scale=(sx, sy), trans=(tx, ty), wrapS=wrapS, wrapT=wrapT,
                                        flag=flag, bitmap=bmp))
        mofs, mcnt = self.sec['material']
        self.materials = []
        for i in range(mcnt):
            o = mofs + i*0x3C
            nm = self.u32(o)
            pas, vtxmode = struct.unpack_from('>HB', d, o+8)
            lit = d[o+0xB:o+0xE]; col = d[o+0xE:o+0x11]; shd = d[o+0x11:o+0x14]
            hilite, u18, invAlpha = struct.unpack_from('>3f', d, o+0x14)
            flags, attrNum, attrSym = struct.unpack_from('>III', d, o+0x30)
            attrs = [self.sym(attrSym + k) for k in range(attrNum)]
            self.materials.append(dict(name=self.str(nm), vtxMode=vtxmode, litColor=lit, color=col, invAlpha=invAlpha,
                                       flags=flags, attrs=attrs, pass_=pas))

    def load_objects(self):
        d = self.d
        oofs, ocnt = self.sec['object']
        self.objects = []
        for i in range(ocnt):
            o = oofs + i*0x144
            nm, typ, const, flags = struct.unpack_from('>IIII', d, o)
            m = o + 16
            parent, nchild, childsym = struct.unpack_from('>iIi', d, m)
            base = struct.unpack_from('>9f', d, m+12)
            ob = dict(index=i, name=self.str(nm), type=typ, flags=flags, parent=parent,
                      children=[self.sym(childsym + k) for k in range(nchild)] if nchild else [],
                      pos=base[0:3], rot=base[3:6], scale=base[6:9])
            if typ == 2:
                face, vtx, nrm, col, st, mat, attr = struct.unpack_from('>7i', d, m+0xF4)
                cenvnum, cenv = struct.unpack_from('>Ii', d, m+0x124)
                ob.update(face=face, vertex=vtx, normal=nrm, color=col, st=st, cenvNum=cenvnum, cenv=cenv)
            self.objects.append(ob)

    def load_cenv(self):
        d = self.d
        cofs, ccnt = self.sec['cenv']
        hdr = 0x24
        data = cofs + ccnt*hdr
        self.cenv = []
        wbase = data
        raw = []
        for i in range(ccnt):
            nm, so, do, mo, sc, dc, mc, vtx, cpy = struct.unpack_from('>9I', d, cofs + i*hdr)
            raw.append((nm, so, do, mo, sc, dc, mc, vtx, cpy))
            wbase += sc*12 + dc*16 + mc*16
        for nm, so, do, mo, sc, dc, mc, vtx, cpy in raw:
            single = [struct.unpack_from('>IHHHH', d, data + so + k*12) for k in range(sc)]
            dual = []
            for k in range(dc):
                t1, t2, wn, wo = struct.unpack_from('>IIII', d, data + do + k*16)
                ws = [struct.unpack_from('>fHHHH', d, wbase + wo + w*12) for w in range(wn)]
                dual.append((t1, t2, ws))
            multi = []
            for k in range(mc):
                wn, pos, posn, nrm, nrmn, wo = struct.unpack_from('>IHHHHI', d, data + mo + k*16)
                ws = [struct.unpack_from('>If', d, wbase + wo + w*8) for w in range(wn)]
                multi.append((pos, posn, nrm, nrmn, ws))
            self.cenv.append(dict(single=single, dual=dual, multi=multi, vtxCount=vtx, copyCount=cpy))

    def load_skeleton(self):
        sofs, scnt = self.sec['skeleton']
        self.skeleton = {}
        for i in range(scnt):
            o = sofs + i*40
            nm = self.str(self.u32(o))
            t = struct.unpack_from('>9f', self.d, o+4)
            self.skeleton[nm] = (t[0:3], t[3:6], t[6:9])

    def load_motion(self):
        d = self.d
        mofs, mcnt = self.sec['motion']
        self.motion = None
        if not mcnt: return
        nm, ntracks, trk, maxtime = struct.unpack_from('>IiIf', d, mofs)
        tbase = mofs + mcnt*16
        tdata = tbase + ntracks*16
        tracks = []
        for i in range(ntracks):
            o = tbase + i*16
            typ, start, target, idx, chan, curve, nkeys = struct.unpack_from('>BBHhHHH', d, o)
            if curve == 4:
                keys = struct.unpack_from('>f', d, o+12)[0]
            else:
                dofs = self.u32(o+12)
                width = {0: 2, 1: 2, 2: 4, 3: 2}[curve]
                keys = np.frombuffer(d, '>f4', nkeys*width, tdata + dofs).reshape(nkeys, width).astype(np.float64)
            tname = self.str(target) if target != 0xFFFF else None
            tracks.append(dict(type=typ, target=tname, index=idx, channel=chan, curve=curve, keys=keys))
        self.motion = dict(name=self.str(nm), maxTime=maxtime, tracks=tracks)
