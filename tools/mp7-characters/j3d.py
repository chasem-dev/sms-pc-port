"""J3D (BMD/BDL) and BCK readers for Super Mario Sunshine."""
import struct
import numpy as np

def sections(d):
    p = 0x20; secs = {}
    while p < len(d):
        tag, size = struct.unpack_from('>4sI', d, p); secs[tag.decode()] = (p, size); p += size
    return secs

def strtab(d, o):
    n = struct.unpack_from('>H', d, o)[0]
    return [d[o+so:d.index(b'\0', o+so)].decode('shift_jis') for so in (struct.unpack_from('>HH', d, o+4+i*4)[1] for i in range(n))]

def rot_j3d(rx, ry, rz):
    """J3D rotation (degrees): R = Rz * Ry * Rx."""
    x, y, z = np.radians([rx, ry, rz])
    cx, sx, cy, sy, cz, sz = np.cos(x), np.sin(x), np.cos(y), np.sin(y), np.cos(z), np.sin(z)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx

def trs(t, r, s):
    m = np.eye(4); m[:3, :3] = rot_j3d(*r) @ np.diag(s); m[:3, 3] = t; return m

class BMD:
    def __init__(self, data):
        self.d = d = data
        self.secs = secs = sections(d)
        j, _ = secs['JNT1']
        nj, = struct.unpack_from('>H', d, j+8)
        jd, jr, jn = struct.unpack_from('>III', d, j+0xC)
        self.joint_names = strtab(d, j + jn)
        self.joints = []
        for i in range(nj):
            o = j + jd + i*0x40
            kind, ign = struct.unpack_from('>HB', d, o)
            s = struct.unpack_from('>3f', d, o+4); r = struct.unpack_from('>3h', d, o+0x10); t = struct.unpack_from('>3f', d, o+0x18)
            self.joints.append(dict(name=self.joint_names[i], kind=kind, ign=ign, s=s, r=[v*180/32768 for v in r], t=t,
                                    raw=d[o:o+0x40]))
        inf, _ = secs['INF1']
        q = inf + struct.unpack_from('>I', d, inf+0x14)[0]
        self.parent = {}; stack = []; last = None
        while True:
            tp, v = struct.unpack_from('>HH', d, q); q += 4
            if tp == 0: break
            if tp == 1: stack.append(last)
            elif tp == 2: last = stack.pop()
            elif tp == 0x10:
                self.parent[v] = None
                for s_ in reversed(stack):
                    if s_ is not None and s_[0] == 'j': self.parent[v] = s_[1]; break
                last = ('j', v)
            else: last = ('o', v)
        self.parent = {k: v for k, v in self.parent.items()}

    def world(self, local=None):
        """World matrices of all joints, from rest pose or given local matrices."""
        n = len(self.joints)
        if local is None: local = [trs(j['t'], j['r'], j['s']) for j in self.joints]
        W = [None]*n
        def get(i):
            if W[i] is None:
                p = self.parent[i]
                W[i] = (get(p) if p is not None else np.eye(4)) @ local[i]
            return W[i]
        for i in range(n): get(i)
        return W

def _hermite(t, t0, v0, o0, t1, v1, i1):
    if t1 == t0: return v0
    u = (t - t0) / (t1 - t0); dt = t1 - t0
    u2 = u*u; u3 = u2*u
    return (2*u3 - 3*u2 + 1)*v0 + (u3 - 2*u2 + u)*o0*dt/ dt * 1 * (t1 - t0) / (t1 - t0) * 1 if False else \
        (2*u3 - 3*u2 + 1)*v0 + (-2*u3 + 3*u2)*v1 + (u3 - 2*u2 + u)*o0*dt + (u3 - u2)*i1*dt

class BCK:
    def __init__(self, data):
        self.d = d = data
        secs = sections(d); a, _ = secs['ANK1']
        self.loop, self.ang, self.duration, nj, ns, nr, nt = struct.unpack_from('>BBHHHHH', d, a+8)
        jo, so, ro, to = struct.unpack_from('>IIII', d, a+0x14)
        self.scale = np.frombuffer(d, '>f4', ns, a+so).astype(np.float64)
        self.rot = np.frombuffer(d, '>i2', nr, a+ro).astype(np.float64) * (2 ** self.ang) * 180 / 32768
        self.rotraw = np.frombuffer(d, '>i2', nr, a+ro)
        self.trans = np.frombuffer(d, '>f4', nt, a+to).astype(np.float64)
        self.anims = []
        for i in range(nj):
            comps = []
            for c in range(3):
                o = a + jo + i*54 + c*18
                comps.append([struct.unpack_from('>HHH', d, o + k*6) for k in range(3)])  # S, R, T
            self.anims.append(comps)
        self.base = a; self.offsets = (jo, so, ro, to)

    def _eval(self, table, key, t):
        count, idx, tan = key
        if count == 1: return table[idx]
        stride = 4 if tan == 1 else 3
        k = table[idx: idx + count*stride].reshape(count, stride)
        if t <= k[0, 0]: return k[0, 1]
        if t >= k[-1, 0]: return k[-1, 1]
        n = np.searchsorted(k[:, 0], t, side='right') - 1
        a_, b_ = k[n], k[n+1]
        out_t = a_[3] if stride == 4 else a_[2]
        in_t = b_[2]
        return _hermite(t, a_[0], a_[1], out_t, b_[0], b_[1], in_t)

    def local(self, joint, t):
        c = self.anims[joint]
        s = [self._eval(self.scale, c[i][0], t) for i in range(3)]
        r = [self._eval(self.rot, c[i][1], t) for i in range(3)]
        tr = [self._eval(self.trans, c[i][2], t) for i in range(3)]
        return trs(tr, r, s)
