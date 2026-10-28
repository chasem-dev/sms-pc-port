"""Refine converted characters after mp7_to_sms.py: the fixes that work on the finished Sunshine files.

refine.py <Sunshine data/mario.szs> <character .szs> [<out .szs>]   (out defaults to the input)
(the character: a pack's <id>/model.szs, or a loose <id>.szs)

Works on the files of a character archive (bmd/ma_mdl1.bmd, the hand models, bck/*.bck), so it runs
on archives already made as well as at the end of a conversion (mp7_to_sms.convert calls refine()):
- triangles whose texture is not in the atlas (an object the character only shows at times, as
  Birdo's on her left hand) are dropped;
- parts the converter left on the 'center' joint for want of a counterpart in Sunshine's skeleton
  (Peach's and Daisy's gowns, Dry Bones' shell) are bound to the waist and the chest by height,
  so they turn with the hips and the body as Mario's do;
- a character in a gown swings its legs less in every body animation and the hem follows the
  thighs a little, so the legs stay under it and it sways; its somersaults become pirouettes;
- each body animation is raised or lowered, frame by frame, so the character's lowest point is where
  Mario's is while he stands (Boo, with no legs, hovers); while he hangs by his hands, the character is
  moved and its arms bent so its hands are where his are;
- Peach's and Daisy's hands are made again from their open hands (REFIST), and every character's
  open hand models are turned to lie in the hand joint as Mario's do;
- the atlas's empty space takes its textures' edge colours.
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import j3d, bmdgeo, bmdwrite, rarc, rarcpack

SHAPE_JOINT = {0: 'M_head', 1: 'M_head', 2: 'M_head', 3: 'M_head', 4: 'M_head_cap1', 5: 'M_hand1_L',
               6: 'M_hand1_R', 7: 'M_foot_R', 8: 'M_foot_L', 9: None, 10: None}


# ------------------------------------------------------------------------------------------ models
class Model:
    """A BMD's triangles with each corner's bind-pose position, normal, texture coordinate and
    joint weights, which can be changed and written back (VTX1, EVP1, DRW1, SHP1 and INF1's
    counts are rebuilt; the rest is kept)."""

    def __init__(self, data):
        self.data = data
        self.bmd = j3d.BMD(data)
        g = bmdgeo.read(data)
        self.names = self.bmd.joint_names
        self.jid = {n: i for i, n in enumerate(self.names)}
        self.ibm = g['ibm'][:len(self.names)] if g['ibm'] else [np.eye(4)] * len(self.names)
        self.bind = [np.linalg.inv(m) for m in self.ibm]
        pos, nrm, uv = g['arrays'][9], g['arrays'][10], g['arrays'][13]
        self.mtypes = [sh['mtype'] for sh in g['shapes']]
        self.tris = []  # per shape: list of triangles, each 3 corners (pos, nrm, uv, weights)
        for sh in g['shapes']:
            slots = [None] * 10
            out = []
            for pk in sh['packets']:
                for k, m in enumerate(pk['mtx']):
                    if m != 0xFFFF: slots[k] = m
                for op, verts in bmdgeo.parse_dl(pk['dl'], sh['desc']):
                    corners = []
                    for v in verts:
                        di = slots[v[0] // 3] if 0 in v else slots[0]
                        weighted, idx = g['drw'][di]
                        if weighted:
                            w = tuple((j, float(x)) for j, x in g['evp'][idx])
                            p = pos[v[9]].copy(); n = nrm[v[10]].copy()
                        else:
                            w = ((idx, 1.0),)
                            B = self.bind[idx] if g['ibm'] else np.eye(4)
                            p = (B @ np.append(pos[v[9]], 1))[:3]; n = B[:3, :3] @ nrm[v[10]]
                        corners.append((p, n, uv[v[13]].copy(), w))
                    if op == 0x98:
                        for k in range(len(corners) - 2):
                            out.append([corners[t] for t in ((k, k+1, k+2) if k % 2 == 0 else (k+1, k, k+2))])
                    elif op == 0x90:
                        for k in range(0, len(corners), 3): out.append(corners[k:k+3])
            self.tris.append(out)

    def encode(self, shape_joints=None, part=False):
        """Write the model back. shape_joints[s]: the joint a single-matrix shape is drawn with
        (body); part: a hand or cap model, every vertex in its joint 0's frame."""
        pos, nrm, uvs = [], [], []
        pmap, nmap, umap = {}, {}, {}
        drw, dmap, env, emap = [], {}, [], {}

        def drw_index(b):
            if b[0] == 'r':
                key = b
                if key not in dmap: dmap[key] = len(drw); drw.append((0, b[1]))
            else:
                e = tuple((j, round(w, 4)) for j, w in b[1])
                if e not in emap: emap[e] = len(env); env.append(list(e))
                key = ('e', e)
                if key not in dmap: dmap[key] = len(drw); drw.append((1, emap[e]))
            return dmap[key]

        def corner(c, rigid):
            p, n, t, w = c
            if part:
                b = ('r', 0); M = np.eye(4)
            elif rigid is not None or len(w) == 1:
                j = rigid if rigid is not None else w[0][0]
                b = ('r', j); M = self.ibm[j]
            else:
                b = ('e', w); M = None
            lp = p if M is None else (M @ np.append(p, 1))[:3]
            ln = n if M is None else M[:3, :3] @ n
            ln = ln / (np.linalg.norm(ln) + 1e-9)
            kp = (b, tuple(np.round(lp, 4)))
            if kp not in pmap: pmap[kp] = len(pos); pos.append(lp)
            kn = tuple(np.round(ln, 4))
            if kn not in nmap: nmap[kn] = len(nrm); nrm.append(ln)
            ku = tuple(np.round(t, 6))
            if ku not in umap: umap[ku] = len(uvs); uvs.append(np.array(t, float))
            return drw_index(b), pmap[kp], nmap[kn], umap[ku]

        shapes = []
        for s, tris in enumerate(self.tris):
            rigid = None
            if not part and self.mtypes[s] == 0:
                rigid = shape_joints[s] if shape_joints else None
            corners = [[corner(c, rigid) for c in tri] for tri in tris]
            if not corners:
                # keep the slot: one zero-area triangle bound to the slot's joint
                j = rigid if rigid is not None else self.jid.get('center', 0)
                if not pos: pos.append(np.zeros(3)); nrm.append(np.array([0, 1., 0])); uvs.append(np.zeros(2))
                d = drw_index(('r', 0 if part else j))
                corners = [[(d, 0, 0, 0)] * 3]
            if part or self.mtypes[s] == 0:
                packets = [dict(mtx=[corners[0][0][0]], tris=[[(None, p, n, t) for _, p, n, t in tri] for tri in corners])]
            else:
                packets = []; cur = []; slots = {}
                for tri in corners:
                    need = {d for d, _, _, _ in tri} - set(slots)
                    if len(slots) + len(need) > 10:
                        packets.append((slots, cur)); cur = []; slots = {}
                    for d, _, _, _ in tri:
                        if d not in slots: slots[d] = len(slots)
                    cur.append(tri)
                packets.append((slots, cur))
                packets = [dict(mtx=list(sl), tris=[[(sl[d], p, n, t) for d, p, n, t in tri] for tri in tl]) for sl, tl in packets]
            shapes.append(dict(mtype=0 if part else self.mtypes[s], packets=packets))
        if part:
            for sh in shapes:
                for pk in sh['packets']: pk['mtx'] = [0]
            drw = [(0, 0)]
        else:
            # every rigid draw matrix before the weighted ones, weighted entry i = envelope i
            order = sorted(range(len(drw)), key=lambda i: (drw[i][0], i))
            new = {old: k for k, old in enumerate(order)}
            drw = [drw[i] for i in order]
            for sh in shapes:
                for pk in sh['packets']: pk['mtx'] = [new[m] for m in pk['mtx']]
        for sh in shapes:
            idx = [p for pk in sh['packets'] for tri in pk['tris'] for _, p, _, _ in tri]
            P = np.array([pos[i] for i in idx]); mn, mx = P.min(0), P.max(0)
            sh['bbox'] = (mn, mx); sh['radius'] = float(np.linalg.norm(np.maximum(abs(mn), abs(mx))))
        secs = j3d.sections(self.data)
        sec = lambda tag: self.data[secs[tag][0]:secs[tag][0] + secs[tag][1]]
        shp, npk = bmdwrite.shp1(shapes)
        new = {
            'INF1': bmdwrite.inf1_patch(sec('INF1'), npk, len(pos)),
            'VTX1': bmdwrite.vtx1(np.array(pos), np.array(nrm), np.array(uvs)),
            'EVP1': bmdwrite.evp1(env, self.ibm) if not part else bmdwrite.evp1([], []),
            'DRW1': bmdwrite.drw1(drw),
            'SHP1': shp,
        }
        return bmdwrite.assemble(self.data, new)

    def body_shape_joints(self):
        return [self.jid[SHAPE_JOINT[s]] if SHAPE_JOINT.get(s) else None for s in range(len(self.tris))]

    def vertices(self, skip=()):
        """Unique bind-pose positions with weights, as (P (n,3), W (n, joints))."""
        seen = {}
        for s, tris in enumerate(self.tris):
            if s in skip: continue
            for tri in tris:
                for p, n, t, w in tri:
                    seen.setdefault((tuple(np.round(p, 3)), w), (p, w))
        P = np.array([p for p, w in seen.values()]); W = np.zeros((len(P), len(self.names)))
        for k, (p, w) in enumerate(seen.values()):
            for j, x in w: W[k, j] += x
        return P, W


# -------------------------------------------------------------------------------------- animations
class Anim:
    """A joint animation (BCK) whose keys can be changed and written back. tracks[joint][axis][c]
    (c: 0 scale, 1 rotation in degrees, 2 translation) is an array of keys (time, value, tangent
    [, out tangent]), or a single (value,) for a constant."""

    def __init__(self, data):
        self.data = data
        k = j3d.BCK(data)
        self.loop, self.ang, self.duration = k.loop, k.ang, k.duration
        self.rscale = (2 ** k.ang) * 180 / 32768
        tables = (k.scale, k.rot, k.trans)
        self.tan = []
        self.tracks = []
        for comps in k.anims:
            joint = []
            for axis in range(3):
                t3 = []
                for c in range(3):
                    cnt, idx, tan = comps[axis][c]
                    tab = tables[c]
                    if cnt == 1:
                        t3.append(np.array([tab[idx]]))
                    else:
                        stride = 4 if tan == 1 else 3
                        keys = tab[idx: idx + cnt * stride].reshape(cnt, stride).copy()
                        if c == 1: keys[:, 0] /= self.rscale
                        t3.append(keys)
                joint.append(t3)
            self.tracks.append(joint)

    def last_frame(self):
        last = 0
        for joint in self.tracks:
            for axis in joint:
                for keys in axis:
                    if keys.ndim == 2: last = max(last, keys[-1, 0])
        return int(np.ceil(last))

    def value(self, j, axis, c, t):
        keys = self.tracks[j][axis][c]
        if keys.ndim == 1: return keys[0]
        if t <= keys[0, 0]: return keys[0, 1]
        if t >= keys[-1, 0]: return keys[-1, 1]
        n = np.searchsorted(keys[:, 0], t, side='right') - 1
        a, b = keys[n], keys[n + 1]
        return j3d._hermite(t, a[0], a[1], a[3] if len(a) == 4 else a[2], b[0], b[1], b[2])

    def locals(self, t):
        out = []
        for j in range(len(self.tracks)):
            s = [self.value(j, a, 0, t) for a in range(3)]
            r = [self.value(j, a, 1, t) for a in range(3)]
            tr = [self.value(j, a, 2, t) for a in range(3)]
            out.append(j3d.trs(tr, r, s))
        return out

    def encode(self):
        big = max([abs(float(k[0])) if k.ndim == 1 else float(np.abs(k[:, 1:]).max())
                   for joint in self.tracks for axis in joint for k in (axis[1],)] + [0])
        while big > 180 * (2 ** self.ang) * 32767 / 32768: self.ang += 1
        self.rscale = (2 ** self.ang) * 180 / 32768
        tabs = ([], [], [])
        heads = []
        for joint in self.tracks:
            h = []
            for axis in range(3):
                for c in range(3):
                    keys = joint[axis][c]
                    tab = tabs[c]
                    if keys.ndim == 1:
                        v = keys[0] / self.rscale if c == 1 else keys[0]
                        h.append((1, len(tab), 0)); tab.append(v)
                    else:
                        k = keys.copy()
                        if c == 1: k[:, 1:] /= self.rscale
                        h.append((len(k), len(tab), 1 if k.shape[1] == 4 else 0)); tab.extend(k.reshape(-1))
            heads.append(h)
        for v in tabs[1]:
            if not -32768.5 < v < 32767.5: raise ValueError('rotation out of range for the animation\'s angle scale')
        body = bytearray(0x40 - 8)
        def place(b):
            nonlocal body
            off = len(body) + 8
            body += b; body += b'\0' * ((-(len(body) + 8)) % 32)
            return off
        jt = b''.join(struct.pack('>HHH', *x) for h in heads for x in h)
        jo = place(jt)
        so = place(np.asarray(tabs[0], '>f4').tobytes())
        ro = place(np.round(np.asarray(tabs[1], float)).astype('>i2').tobytes())
        to = place(np.asarray(tabs[2], '>f4').tobytes())
        struct.pack_into('>BBHHHHHIIII', body, 0, self.loop, self.ang, self.duration, len(self.tracks),
                         len(tabs[0]), len(tabs[1]), len(tabs[2]), jo, so, ro, to)
        sec = b'ANK1' + struct.pack('>I', len(body) + 8) + bytes(body)
        head = bytearray(self.data[:0x20])
        struct.pack_into('>I', head, 8, 0x20 + len(sec))
        return bytes(head) + sec


def world(model, local):
    n = len(local); W = [None] * n
    for i in range(n):
        p = model.bmd.parent[i]
        W[i] = (W[p] if p is not None else np.eye(4)) @ local[i]
    return W


def lowest(model, P, Wt, anim, t):
    """The lowest point of the model posed by anim at frame t, and where its hands are (between them)."""
    A = world(model, anim.locals(t))
    S = np.array([A[j] @ model.ibm[j] for j in range(len(A))])  # (joints, 4, 4)
    used = np.nonzero(Wt.sum(0))[0]
    Y = sum(Wt[:, j] * (P @ S[j, 1, :3] + S[j, 1, 3]) for j in used)
    hands = (A[model.jid['M_hand1_L']][:3, 3] + A[model.jid['M_hand1_R']][:3, 3]) / 2
    return float(Y.min()), hands


# ------------------------------------------------------------------------------------------ fixes
def drop_untextured(model, parts):
    """Triangles that sample outside the atlas (no texture of their own): an object the character
    only shows at times. Returns the number dropped from the body."""
    bad = lambda tri: any((c[2] < -1e-3).any() or (c[2] > 1 + 1e-3).any() for c in tri)
    n = 0
    for s in range(len(model.tris)):
        if s in (1, 3): continue  # the eyes have their own textures
        keep = [t for t in model.tris[s] if not bad(t)]
        n += len(model.tris[s]) - len(keep); model.tris[s] = keep
    for m in parts:
        for s in range(len(m.tris)): m.tris[s] = [t for t in m.tris[s] if not bad(t)]
    return n


def rebind_center(model):
    """Weights the converter left on 'center' go to the waist and the chest, by the vertex's height
    between those joints. Returns the number of vertices moved."""
    jid = model.jid
    c, wj, cj = jid['center'], jid['jnt_waist'], jid['jnt_chest']
    y0 = model.bind[wj][1, 3]; y1 = model.bind[cj][1, 3]
    moved = set()
    for s in (9, 10):
        if s >= len(model.tris): continue
        for tri in model.tris[s]:
            for k, (p, n, t, w) in enumerate(tri):
                d = dict(w)
                if c not in d: continue
                x = d.pop(c)
                f = float(np.clip((p[1] - y0) / max(y1 - y0, 1e-3), 0, 1))
                d[wj] = d.get(wj, 0) + x * (1 - f)
                d[cj] = d.get(cj, 0) + x * f
                nw = tuple(sorted(((j, v) for j, v in d.items() if v > 1e-3), key=lambda e: -e[1])[:4])
                s_ = sum(v for _, v in nw)
                tri[k] = (p, n, t, tuple((j, v / s_) for j, v in nw))
                moved.add(tuple(np.round(p, 3)))
    return np.array(sorted(moved)).reshape(-1, 3)


def gown(model, rebound):
    """A gown: what was on 'center' is a skirt reaching from the hips down to the floor."""
    if len(rebound) < 100: return False
    hip = model.bind[model.jid['jnt_leg_L1']][1, 3]
    return bool(rebound[:, 1].min() < 0.15 * hip)


def legless(model):
    """No feet (Boo): the character hovers."""
    return len(model.tris[7]) + len(model.tris[8]) <= 2


HOVER = 0.1  # a legless character's height above the ground, of its own height


# a gown's legs: how much of each joint's turn from rest is kept (thigh, knee, ankle)
GOWN_LEGS = {'jnt_leg_L1': 0.6, 'jnt_leg_R1': 0.6, 'jnt_leg_L2': 0.25, 'jnt_leg_R2': 0.25,
             'jnt_foot_L': 0.5, 'jnt_foot_R': 0.5}
# how much of the hem follows the thighs (none at the hips)
GOWN_SWAY = 0.5


def sway_gown(model):
    """A gown hangs from the waist and the hem follows the thighs a little, each side its own."""
    jid = model.jid
    wj, L, R = jid['jnt_waist'], jid['jnt_leg_L1'], jid['jnt_leg_R1']
    top = model.bind[L][1, 3]
    P, W = model.vertices()
    skirt = W[:, wj] > 0.99
    bottom = P[skirt, 1].min() if skirt.any() else 0
    xL, xR = model.bind[L][0, 3], model.bind[R][0, 3]
    for s in (9, 10):
        if s >= len(model.tris): continue
        for tri in model.tris[s]:
            for k, (p, n, t, w) in enumerate(tri):
                if len(w) != 1 or w[0][0] != wj or p[1] >= top: continue
                f = GOWN_SWAY * float(np.clip((top - p[1]) / max(top - bottom, 1e-3), 0, 1)) ** 1.5
                side = float(np.clip((p[0] - xR) / (xL - xR), 0, 1))  # 1 at the left thigh
                d = {wj: 1 - f, L: f * side, R: f * (1 - side)}
                nw = tuple(sorted(((j, v) for j, v in d.items() if v > 1e-3), key=lambda e: -e[1]))
                tri[k] = (p, n, t, nw)


def damp_legs(anim, model, factors):
    """Bring the legs' rotations toward their rest pose (Sunshine's): factors[joint] of the turn is kept."""
    for n, factor in factors.items():
        j = model.jid[n]
        rest = model.bmd.joints[j]['r']
        for axis in range(3):
            keys = anim.tracks[j][axis][1]
            wrap = lambda a: (a + 180) % 360 - 180
            if keys.ndim == 1:
                keys[0] = rest[axis] + factor * wrap(keys[0] - rest[axis])
            else:
                # the turn from rest, the shorter way round at the first key and continuous after it
                d = wrap(keys[0, 1] - rest[axis]) + (keys[:, 1] - keys[0, 1])
                keys[:, 1] = rest[axis] + factor * d
                keys[:, 2:] *= factor


def simplify(f, v, tol):
    """Keys (frame, value) for v sampled at frames f, within tol (Ramer-Douglas-Peucker)."""
    keep = {0, len(f) - 1}
    stack = [(0, len(f) - 1)]
    while stack:
        a, b = stack.pop()
        if b <= a + 1: continue
        line = v[a] + (v[b] - v[a]) * (f[a+1:b] - f[a]) / (f[b] - f[a])
        err = np.abs(v[a+1:b] - line); k = int(err.argmax())
        if err[k] > tol:
            m = a + 1 + k; keep.add(m); stack += [(a, m), (m, b)]
    return sorted(keep)


GROUNDED = (-8.0, 4.0)  # Mario's lowest point this near his origin: his feet are on the ground
HANGING = -100.0  # Mario's lowest point this far below his origin: he hangs by his hands (a ledge, a rope)
HANG_BLEND = 8  # frames: hanging (hands on Mario's) and standing (feet) blend over this long


# animations the game plays only in the air (the trample bounces off enemies, the jumps and flips, the
# ground pound's start): nothing to stand on, so no frame of theirs is matched to the floor; they keep
# the character's standing correction (from its idle) throughout, so the jump keeps its own shape
AIRBORNE = {'ma_step1', 'ma_step2', 'ma_step3', 'ma_jump', 'ma_2jmp1', 'ma_2jmp2', 'ma_tjmp1', 'ma_roll',
            'ma_roll_jump', 'ma_jump_rolling', 'ma_broad_jump', 'ma_wjmp', 'ma_firejmp', 'ma_hipsr', 'ma_hipat',
            'ma_yo_jump', 'ma_yo_jump_fall', 'ma_yo_hold_jump', 'ma_dive_wait'}
# and those with nothing under the feet but what Mario holds or swims in: climbing a fence, a tree or a
# ladder, swimming
OFF_FLOOR = ('ma_fence_', 'ma_tree_', 'ma_ladder_', 'ma_swim', 'ma_paddle_', 'ma_wait_to_paddle',
             'ma_wait_to_swim', 'ma_float')


def no_floor(clip):
    return clip in AIRBORNE or clip.startswith(OFF_FLOOR)

LIFT_RATE = 0.012  # the ground correction changes by this much of the character's height a frame, and as
# fast as Mario's own body rises and falls in the animation


def ground_offsets(model, P, Wt, anim, mario, mP, mWt, manim, hover):
    """Per-frame offsets of the root (frames, (n, 3)): while Mario stands, the character's lowest point
    is put where Mario's is (plus hover), raised or lowered (the converter scales the hips' height,
    which left some characters in the floor and others above it); in the air it eases from the
    lift before to the lift after, so a jump keeps its own shape; while Mario hangs
    by his hands, the character's hands are put where his are, so they hold the ledge and not the
    wall or the air. Between, it eases from one to the next."""
    last = max(anim.last_frame(), 1)
    frames = np.arange(0, last + 1, dtype=float)
    height = float(np.ptp(P[:, 1]))
    # how fast the correction may change: a little everywhere, and as fast as Mario's own body rises
    # and falls in this animation (a fall or getting up again), at the character's size
    waist = np.array([world(mario, manim.locals(t))[mario.jid['jnt_waist']][1, 3] for t in frames])
    mvy = float(np.percentile(np.abs(np.diff(waist)), 95)) if len(frames) > 1 else 0.0  # not one jump
    rate = LIFT_RATE * height + mvy * height / float(np.ptp(mP[:, 1]))
    # (and, below, as fast as the need itself usually changes from one standing frame to the next: a
    # shell getting up)
    me = [lowest(mario, mP, mWt, manim, t) for t in frames]
    ch = [lowest(model, P, Wt, anim, t) for t in frames]
    mlow = np.array([m[0] for m in me])
    ground = (mlow >= GROUNDED[0]) & (mlow <= GROUNDED[1])
    hang = mlow < HANGING
    if not (ground | hang).any():
        return frames, None, None
    want = np.zeros((len(frames), 3))
    for i in range(len(frames)):
        if hang[i]: want[i] = me[i][1] - ch[i][1]
        elif ground[i]: want[i, 1] = me[i][0] + hover - ch[i][0]
    held = ground | hang
    if ground.sum() > 2:
        gi = np.nonzero(ground)[0]; consec = np.diff(gi) == 1
        dn = np.abs(np.diff(want[gi, 1]))[consec]
        if len(dn): rate = max(rate, float(np.percentile(dn, 90)))
    hang_w = smooth(np.interp(frames, frames[held], hang[held].astype(float)), anim.loop == 2, HANG_BLEND) if hang.any() else None
    if anim.loop == 2 and hang_w is not None: hang_w[-1] = hang_w[0]
    off = np.zeros_like(want)
    for ax in range(3):
        if anim.loop == 2:
            # a loop: ease across its seam too, and close what is left so the last frame meets the first
            # (the last frame is the first again: left out, or the two collapse the curve)
            h = held & (frames < last) if (held & (frames < last)).any() else held
            o = np.interp(frames, frames[h], want[h, ax], period=last)
            o += (frames / last) * (o[0] - o[-1])
        else:
            o = np.interp(frames, frames[held], want[held, ax])
        if ax == 1 and not hang.any(): o = smooth_up(o, anim.loop == 2, rate)
        elif hang.any():
            o = smooth(o, anim.loop == 2, HANG_BLEND)
            if ax == 1:
                # and where he stands, never below the floor
                floor = np.where(ground, want[:, 1], -1e9)
                o = np.maximum(o, smooth_up(floor, anim.loop == 2, rate))
        off[:, ax] = o
    return frames, off, hang_w


def smooth(off, loop, w=8, _period=False):
    """A running mean over w frames (the ends of a clip keep their own values)."""
    if loop and not _period and len(off) > 3:
        out = smooth(off[:-1], True, w, True)
        return np.append(out, out[0])
    n = len(off); r = min(w // 2, max((n - 1) // 2, 1))
    if n < 3: return off
    pad = np.pad(off, r, mode='wrap' if loop else 'edge')
    out = np.array([pad[i:i + 2 * r + 1].mean() for i in range(n)])
    if not loop:
        out[0], out[-1] = off[0], off[-1]
    return out




def smooth_up(off, loop, rate):
    """The lowest curve that is never below off and changes by at most rate a frame: anything lifted
    out of the ground stays out, with no jumps from one frame to the next, and nothing is lifted
    more than that needs (a running maximum over a window floated characters between footfalls).
    A loop's distances wrap round, and its last frame is its first again."""
    n = len(off)
    if n < 2: return off
    if loop and n > 3:
        base = off[:-1]; m = len(base)
        i = np.arange(m); d = np.abs(i[:, None] - i[None, :]); d = np.minimum(d, m - d)
        out = (base[None, :] - rate * d).max(1)
        return np.append(out, out[0])
    i = np.arange(n); d = np.abs(i[:, None] - i[None, :])
    return (off[None, :] - rate * d).max(1)


def rotation_between(a, b):
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b)
    v = np.cross(a, b); c = float(a @ b)
    if c < -0.9999:  # opposite: half a turn about any axis square to a
        p = np.cross(a, [1.0, 0, 0]) if abs(a[0]) < 0.9 else np.cross(a, [0, 1.0, 0])
        p /= np.linalg.norm(p); return 2 * np.outer(p, p) - np.eye(3)
    if np.linalg.norm(v) < 1e-9: return np.eye(3)
    K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + K + K @ K / (1 + c)


REACH_TOL = 0.05  # how far short of its aim a hand may stay
GRIP_RATE = 0.05  # how fast a narrowed grip may change a frame
BEND_RATE = np.radians(6)  # how fast the side an elbow bends to may turn a frame
ARMS = {S: ('jnt_arm_%s1' % S, 'jnt_arm_%s2' % S, 'jnt_hand_%s' % S) for S in 'LR'}


def reach_hands(anim, model, mario, manim, frames, weight):
    """While Mario hangs by his hands, bend each arm (shoulder and elbow, in the plane it already
    bends in) so the hand is where Mario's is: arms longer or shorter than his would otherwise hold
    the air above the ledge or the wall below it. weight: per frame, 0..1, how far toward Mario's."""
    if weight is None or weight.max() < 1e-3: return False
    new = {j: [] for S in 'LR' for j in (model.jid[ARMS[S][0]], model.jid[ARMS[S][1]])}
    shifts = []; bend = {}
    cache = []
    for i, t in enumerate(frames):
        loc = anim.locals(t); W = world(model, loc); MW = world(mario, manim.locals(t))
        own = {S: W[model.jid[ARMS[S][2]]][:3, 3] for S in 'LR'}
        his = {S: MW[mario.jid[ARMS[S][2]]][:3, 3] for S in 'LR'}
        cache.append((W, MW, own, his))

    def fit(i, k):
        """Aims k of the way from the character's own hands (moved to Mario's middle) to Mario's, and
        the move of the body that lets the arms reach them (arms too short, as Koopa Kid's).
        Returns aims, move, what is still missing."""
        W, MW, own, his = cache[i]
        # k = 1: Mario's hands; k = 0: the character's own two hands moved together so their middle
        # is where the middle of Mario's is (the body's move alone always reaches that)
        om, hm = (own['L'] + own['R']) / 2, (his['L'] + his['R']) / 2
        aims = {}
        for S in 'LR':
            h = hm + (own[S] - om) + k * ((his[S] - hm) - (own[S] - om))
            aims[S] = own[S] + weight[i] * (h - own[S])
        # the body's move v must put each shoulder within reach of its hand's aim: v inside one
        # sphere per hand; found by projecting onto each in turn from no move at all, so it is the
        # least move needed and changes smoothly from frame to frame
        balls = []
        for S in 'LR':
            j1, j2, jh = (model.jid[x] for x in ARMS[S])
            reach = 0.97 * (np.linalg.norm(W[j2][:3, 3] - W[j1][:3, 3]) + np.linalg.norm(W[jh][:3, 3] - W[j2][:3, 3]))
            balls.append((aims[S] - W[j1][:3, 3], reach))
        v = np.zeros(3)
        for _ in range(200):
            moved = 0.0
            for c, r in balls:
                e = v - c; n = np.linalg.norm(e)
                if n > r: v = c + e * (r / n); moved = max(moved, n - r)
            if moved < 1e-4: break
        return aims, v, max(max(np.linalg.norm(v - c) - r, 0.0) for c, r in balls)

    # a grip short arms cannot make (Toad, Dry Bones) gives way toward the character's own: as near to
    # Mario's as reaches, frame by frame (bisection), then never nearer than that and changing by at
    # most GRIP_RATE a frame, so the hands do not jump
    ks = []
    for i in range(len(frames)):
        if fit(i, 1.0)[2] < REACH_TOL: ks.append(1.0); continue
        lo, hi = 0.0, 1.0
        for _ in range(12):
            k = (lo + hi) / 2
            if fit(i, k)[2] < REACH_TOL: lo = k
            else: hi = k
        ks.append(lo)
    ks = -smooth_up(-np.array(ks), anim.loop == 2, GRIP_RATE)
    for i, t in enumerate(frames):
        W, MW, own, his = cache[i]
        aims, v, miss = fit(i, float(np.clip(ks[i], 0, 1)))
        shifts.append(v)
        for S in 'LR':
            j1, j2, jh = (model.jid[x] for x in ARMS[S])
            A, B, C = W[j1][:3, 3] + v, W[j2][:3, 3] + v, W[jh][:3, 3] + v
            T = aims[S]
            l1, l2 = np.linalg.norm(B - A), np.linalg.norm(C - B)
            d = float(np.clip(np.linalg.norm(T - A), abs(l1 - l2) + 1e-3, l1 + l2 - 1e-3))
            u = (T - A) / (np.linalg.norm(T - A) + 1e-9)
            w = (B - A) - ((B - A) @ u) * u
            # the side the elbow bends to: the animation's, kept from the frame before where the arm is
            # nearly straight (there it is ill-defined, and flipping it twists the whole arm)
            pw = bend.get(S)
            if pw is not None:
                pw = pw - (pw @ u) * u
                if np.linalg.norm(pw) > 1e-6:
                    pw /= np.linalg.norm(pw)
                    # how much the animation's own bend counts: none for a straight arm, all once the
                    # elbow is well off the line; and never turned more than BEND_RATE from the frame before
                    f = float(np.clip(np.linalg.norm(w) / (0.3 * l1), 0, 1))
                    wn = w / (np.linalg.norm(w) + 1e-9)
                    if wn @ pw < 0: f = 0.0
                    w = f * wn + (1 - f) * pw
                    if np.linalg.norm(w) < 1e-6: w = pw
                    w /= np.linalg.norm(w)
                    ang = np.arccos(np.clip(w @ pw, -1, 1))
                    if ang > BEND_RATE:
                        ax = np.cross(pw, w); ax /= np.linalg.norm(ax) + 1e-12
                        w = _turn(ax, BEND_RATE) @ pw
            if np.linalg.norm(w) < 1e-6: w = np.cross(u, [0, 0, 1.0])
            w /= np.linalg.norm(w)
            bend[S] = w
            ca = (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d)
            B2 = A + l1 * (ca * u + np.sqrt(max(0.0, 1 - ca * ca)) * w)
            T2 = A + d * u
            R1 = rotation_between(B - A, B2 - A)
            R2 = rotation_between(R1 @ (C - B), T2 - B2)
            P = W[model.bmd.parent[j1]][:3, :3]
            W1 = R1 @ W[j1][:3, :3]; W2 = R2 @ R1 @ W[j2][:3, :3]
            for j, R in ((j1, np.linalg.inv(P) @ W1), (j2, np.linalg.inv(W1) @ W2)):
                old = np.array([anim.value(j, ax, 1, t) for ax in range(3)])
                prev = new[j][-1] if new[j] else old
                e = old if weight[i] < 1e-3 else _euler(R / np.linalg.norm(R, axis=0), prev)
                if new[j]: e = new[j][-1] + (e - new[j][-1] + 180) % 360 - 180
                new[j].append(e)
    for j, vals in new.items():
        vals = np.array(vals)
        # a key on every frame: arms turn fast here, and fewer keys with their slopes overshoot
        for ax in range(3): set_track(anim, j, ax, 1, frames, vals[:, ax], 0.0)
    shifts = np.array(shifts)
    if np.abs(shifts).max() > 1e-3:
        add_center_offset(anim, model, frames, shifts)
    return True


def add_center_offset(anim, model, frames, off):
    """off: (n, 3) in the model's space, added to the root's translation; an axis with no change is
    left as it was. The root (center) hangs from mdl1, which some animations turn (the roll jump
    tilts Mario 31 degrees), so the offset is turned into mdl1's frame first."""
    j = model.jid['center']
    off = np.array([anim.locals(t)[0][:3, :3].T @ off[i] for i, t in enumerate(frames)])
    for ax in range(3):
        if np.abs(off[:, ax]).max() < 1e-3: continue
        base = np.array([anim.value(j, ax, 2, t) for t in frames])
        set_track(anim, j, ax, 2, frames, base + off[:, ax], 0.15)


# ------------------------------------------------------------------------------------------ fists
# characters whose rig bends each finger at one joint, so the converter's curl makes a hook, not a
# fist: their hands are made again from the open hand, each finger curved a little at three joints,
# a relaxed hand (their long thin fingers make a hollow ring of a full fist)
REFIST = {'peach', 'daisy'}
FIST_JOINTS = ((0.0, 40), (0.45, 45), (0.78, 35))  # (place along the finger, turn in degrees)
FIST_JOINT_WIDTH = 0.12


def _turn(a, t):
    K = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + np.sin(t) * K + (1 - np.cos(t)) * K @ K


def refist(body, part, side):
    """Bend the fingers of the open hand (part, ma_hnd2) into the body's hand (shape 5 or 6) at three
    joints. The converter's fist (the same triangles, curled at one joint) gives the finger
    direction, the side they curl to, the knuckles and the thumb, which is kept as it is."""
    s = 5 if side == 'L' else 6
    F, O = body.tris[s], part.tris[0]
    if len(F) != len(O) or not F: return False
    Bh = body.bind[body.jid['jnt_hand_' + side]]
    r = np.array([(Bh @ np.append(c[0], 1))[:3] for tri in O for c in tri])
    rn = np.array([Bh[:3, :3] @ c[1] for tri in O for c in tri])
    f = np.array([c[0] for tri in F for c in tri])
    d = np.linalg.norm(f - r, axis=1); size = np.ptp(r, 0).max()
    moved = d > 0.08 * size
    if moved.sum() < 10 or (~moved).sum() < 10: return False
    tips = moved & (d >= np.percentile(d[moved], 70))
    origin = r[~moved].mean(0)
    u = r[tips].mean(0) - origin; u /= np.linalg.norm(u)
    v = (f[tips] - r[tips]).mean(0); v -= (v @ u) * u; v /= np.linalg.norm(v)
    a = np.cross(u, v)
    sv = (r - origin) @ u
    # the knuckles: past them nearly everything moved
    sk = sv.max()
    for t in np.sort(sv)[::-1]:
        if moved[sv >= t].mean() < 0.9: break
        sk = t
    fing = sv >= sk
    L = sv[fing].max() - sk
    hv = (r - origin) @ v
    h0 = np.median(hv[fing & (sv <= sk + 0.25 * L)])
    grid = np.linspace(-0.1, 1.05, 400); ds = (grid[1] - grid[0]) * L
    th = np.zeros_like(grid)
    for p, deg in FIST_JOINTS:
        t = np.clip((grid - p + FIST_JOINT_WIDTH / 2) / FIST_JOINT_WIDTH, 0, 1)
        th += np.radians(deg) * t * t * (3 - 2 * t)
    line = np.cumsum(np.cos(th)[:, None] * u + np.sin(th)[:, None] * v, 0) * ds
    line -= line[np.searchsorted(grid, 0)]
    base = origin + sk * u + h0 * v
    k = 0
    for ti, tri in enumerate(F):
        new = []
        for c in tri:
            p, n, t_, w = c
            if fing[k]:
                q = r[k] - origin
                g = min(np.searchsorted(grid, (q @ u - sk) / L), len(grid) - 1)
                vp = -np.sin(th[g]) * u + np.cos(th[g]) * v
                p = base + line[g] + (q @ v - h0) * vp + (q @ a) * a
                n = _turn(a, th[g]) @ rn[k]
            new.append((p, n, t_, w)); k += 1
        F[ti] = new
    return True


# ------------------------------------------------------------------------------------ open hands
def _surface(m):
    out = []
    for tri in m.tris[0]:
        a, b, c = [cn[0] for cn in tri]
        for u, v in ((1/3, 1/3), (0.6, 0.2), (0.2, 0.6), (0.2, 0.2)): out.append(a + u*(b-a) + v*(c-a))
    return np.array(out)


def _rotations24():
    out = []
    for perm in ((0, 1, 2), (0, 2, 1), (1, 0, 2), (1, 2, 0), (2, 0, 1), (2, 1, 0)):
        for sx in (1, -1):
            for sy in (1, -1):
                for sz in (1, -1):
                    R = np.zeros((3, 3))
                    for i, sg in zip(range(3), (sx, sy, sz)): R[i, perm[i]] = sg
                    if np.linalg.det(R) > 0: out.append(R)
    return out


def turn_part(part, R):
    for s in range(len(part.tris)):
        part.tris[s] = [[(R @ p, R @ n, t, w) for p, n, t, w in tri] for tri in part.tris[s]]


def fit_open_hand(part, mario_part):
    """The turn about the wrist that puts the open hand (ma_hnd2) in the hand joint as Mario's lies:
    the converter turned the hand to fit the fist, and the open hand, made from the same mesh, came
    with that turn (Donkey Kong's open hand stood up on a ledge instead of lying on it). The best of 24 starting turns, by ICP. Returns the turn."""
    import handfit
    src, dst = _surface(part), _surface(mario_part)
    if len(src) < 10 or len(dst) < 10: return np.eye(3)  # no hand (Boo)
    sc = np.linalg.norm(dst, axis=1).mean() / np.linalg.norm(src, axis=1).mean()
    best = (np.eye(3), handfit.icp(src * sc, dst, np.eye(3), iters=1)[1])
    for R0 in _rotations24():
        R, e = handfit.icp(src * sc, dst, R0, iters=25)
        # a flat hand fits almost as well palm up: no more than OPEN_HAND_MAX_TURN from the converter's
        if e < best[1] and handfit.angle(R) <= OPEN_HAND_MAX_TURN: best = (R, e)
    return best[0]


OPEN_HAND_MAX_TURN = 60


# ------------------------------------------------------------------------------------------ atlas
def atlas_gutter(data, width=8):
    """The atlas's empty space next to its textures takes their edge colours, so filtering at a
    texture's edge (texture coordinates the converter clamped there) does not blend in black.
    Only the CMPR blocks that change are written; the rest stay byte for byte."""
    import gxtex
    secs = j3d.sections(data); t, _ = secs['TEX1']
    h = t + struct.unpack_from('>I', data, t + 0xC)[0]
    fmt, _, w, hh = struct.unpack_from('>BBHH', data, h)
    if fmt != 14 or w % 8 or hh % 8: return data
    dofs = h + struct.unpack_from('>I', data, h + 0x1C)[0]
    size = w * hh // 2
    img = gxtex.decode(14, data[dofs:dofs + size], w, hh).copy()
    black = (img[..., :3].sum(-1) == 0)
    # the empty space: black reachable from the atlas's far corner
    empty = np.zeros_like(black)
    if not black[-1, -1]: return data
    stack = [(hh - 1, w - 1)]; empty[hh - 1, w - 1] = True
    while stack:
        y, x = stack.pop()
        for yy, xx in ((y+1, x), (y-1, x), (y, x+1), (y, x-1)):
            if 0 <= yy < hh and 0 <= xx < w and black[yy, xx] and not empty[yy, xx]:
                empty[yy, xx] = True; stack.append((yy, xx))
    filled = ~empty
    out = img.astype(np.float64)
    for _ in range(width):
        acc = np.zeros_like(out); cnt = np.zeros((hh, w))
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh = np.roll(np.roll(out * filled[..., None], dy, 0), dx, 1)
            m = np.roll(np.roll(filled, dy, 0), dx, 1)
            if dy == 1: m[0] = False; sh[0] = 0
            if dy == -1: m[-1] = False; sh[-1] = 0
            if dx == 1: m[:, 0] = False; sh[:, 0] = 0
            if dx == -1: m[:, -1] = False; sh[:, -1] = 0
            acc += sh; cnt += m
        grow = ~filled & (cnt > 0)
        out[grow] = acc[grow] / cnt[grow][:, None]
        filled |= grow
    new = np.clip(np.round(out), 0, 255).astype(np.uint8)
    changed = (new != img).any(-1)
    if not changed.any(): return data
    enc = np.frombuffer(bmdwrite.cmpr(new), np.uint8).reshape(hh // 8, w // 8, 2, 2, 8)
    old = np.frombuffer(data[dofs:dofs + size], np.uint8).reshape(hh // 8, w // 8, 2, 2, 8).copy()
    blk = changed.reshape(hh // 4, 4, w // 4, 4).any((1, 3))
    for by, bx in zip(*np.nonzero(blk)):
        old[by // 2, bx // 2, by % 2, bx % 2] = enc[by // 2, bx // 2, by % 2, bx % 2]
    d = bytearray(data); d[dofs:dofs + size] = old.tobytes()
    return bytes(d)


# ------------------------------------------------------------------------------------- pirouettes
# a gown turned upside down is a bell: in a gown, the somersaults (rolls, the triple jump's flip, the
# ground pound's) become a pirouette, the turn about the vertical instead, keeping a lean
# in a pirouette the hips stay in line and the legs (and the hem that follows them) under the gown:
# what is kept of the turns left after GOWN_LEGS
PIROUETTE_DAMP = {'jnt_waist': 0.3, 'jnt_leg_L1': 0.25, 'jnt_leg_R1': 0.25, 'jnt_leg_L2': 0.5, 'jnt_leg_R2': 0.5}
NO_PIROUETTE = ('swim', 'sand')  # diving down in water; stuck head first in sand, as the clip before
GOWN_INVERTED = -0.2  # the gown's axis pointing down this much (cosine from up): upside down


def set_track(anim, j, axis, comp, frames, values, tol):
    """Keys for a track sampled at frames, as few as keep the game's Hermite curve within tol (and at
    most 0.05) of every sample: thinned as straight lines first, then a key added wherever the curve
    strays, with each key's slope taken from the samples."""
    tol = max(min(tol, 0.05), 1e-6) if tol > 0 else 0.0
    dv = np.gradient(values, frames) if len(frames) > 1 else np.zeros(1)
    keep = set(simplify(frames, values, tol)) if tol > 0 else set(range(len(frames)))
    while True:
        k = sorted(keep)
        keys = np.stack([frames[k], values[k], dv[k]], 1)
        if len(keys) < 2: break
        curve = np.array([_hermite_eval(keys, t) for t in frames])
        err = np.abs(curve - values); worst = int(err.argmax())
        if err[worst] <= max(tol, 1e-4) or worst in keep: break
        keep.add(worst)
    anim.tracks[j][axis][comp] = keys if len(keys) > 1 else np.array([values[0]])


def _hermite_eval(keys, t):
    if t <= keys[0, 0]: return keys[0, 1]
    if t >= keys[-1, 0]: return keys[-1, 1]
    n = np.searchsorted(keys[:, 0], t, side='right') - 1
    a, b = keys[n], keys[n + 1]
    return j3d._hermite(t, a[0], a[1], a[2], b[0], b[1], b[2])


def _lean(u):
    """How much of a turn u (degrees from upright, -180..180) is kept as a lean: most of a small one,
    at most about 32 degrees, nothing upside down."""
    return 0.7 * u * np.cos(np.radians(u) / 2) ** 2


def _spin(frames, s):
    """A turn that starts at 0 and ends a whole number of turns round, as the clips before and after."""
    s = s - s[0]
    n = np.round(s[-1] / 360.0)
    return s + (n * 360 - s[-1]) * frames / max(frames[-1], 1)


def _euler(M, prev):
    """J3D angles (x, y, z: R = Rz Ry Rx, degrees) of M, the solution nearest prev."""
    b = np.arcsin(np.clip(-M[2, 0], -1, 1))
    a = np.arctan2(M[2, 1], M[2, 2]); c = np.arctan2(M[1, 0], M[0, 0])
    cands = [np.degrees([a, b, c]), np.degrees([a + np.pi, np.pi - b, c + np.pi])]
    best = None
    for e in cands:
        if prev is not None: e = prev + (e - prev + 180) % 360 - 180
        if best is None or prev is None or np.abs(e - prev).sum() < np.abs(best - prev).sum(): best = e
        if prev is None: break
    return best


def pirouette(name, anim, model):
    """Turn a somersault into a pirouette (see above). Returns True if the clip had one."""
    if any(w in name for w in NO_PIROUETTE): return False
    w, c = model.jid['jnt_waist'], model.jid['center']
    L = max(anim.last_frame(), 1); frames = np.arange(0, L + 1, dtype=float)
    up0 = np.linalg.inv(model.bind[w][:3, :3]) @ np.array([0, 1.0, 0])  # the gown's axis, in the waist's frame

    def measure():
        ups, Rc = [], []
        for t in frames:
            loc = anim.locals(t); W = world(model, loc)
            ups.append(W[w][:3, :3] @ up0); Rc.append(loc[c][:3, :3])
        ups = np.array(ups); ups /= np.linalg.norm(ups, axis=1, keepdims=True)
        k = np.cross(ups[:-1], ups[1:]).sum(0)
        if np.linalg.norm(k) < 1e-6: return None
        k /= np.linalg.norm(k)
        e1 = np.array([0, 1.0, 0]) - k[1] * k; e1 /= np.linalg.norm(e1); e2 = np.cross(k, e1)
        return ups, Rc, k, np.degrees(np.unwrap(np.arctan2(ups @ e2, ups @ e1)))

    m = measure()
    if m is None or m[0][:, 1].min() > GOWN_INVERTED: return False
    ups, Rc, k, th = m
    if abs(k[1]) > 0.5: return False  # a turn about the vertical already
    # any turn that tips the gown past lying down is taken out (a full somersault, or a slide that
    # ends head down)
    damp_legs(anim, model, PIROUETTE_DAMP)
    ups, Rc, k, th = measure()
    rx = np.array([anim.value(c, 0, 1, t) for t in frames])
    ry = np.array([anim.value(c, 1, 1, t) for t in frames])
    rz = np.array([anim.value(c, 2, 1, t) for t in frames])
    if np.ptp(rx) >= 170 and np.ptp(ry) < 20 and np.ptp(rz) < 20:
        # the somersault is the root's own x turn: R = Ry(spin) Rx(lean)
        u = (rx + 180) % 360 - 180
        lean = _lean(u)
        spin = _spin(frames, rx - lean)
        set_track(anim, c, 0, 1, frames, lean, 0.2)
        set_track(anim, c, 1, 1, frames, ry + spin, 0.2)
        return True
    # otherwise: take the turn about k off the root and spin it about the vertical instead
    u = (th + 180) % 360 - 180
    lean = _lean(u)
    spin = _spin(frames, th - lean)
    out = []; prev = None
    for i in range(len(frames)):
        R = j3d.rot_j3d(0, spin[i], 0) @ _turn(k, -np.radians(th[i] - lean[i])) @ Rc[i]
        prev = _euler(R, prev); out.append(prev)
    out = np.array(out)
    for axis in range(3): set_track(anim, c, axis, 1, frames, out[:, axis], 0.2)
    return True


def clamp_gown(name, anim, model):
    """Wherever a gown would still tip past GOWN_INVERTED (a turn that is no somersault: falling onto
    the back, the spin into a bottle), the body is turned back, about its middle, just far enough.
    Returns True if any frame needed it."""
    if any(w in name for w in NO_PIROUETTE): return False
    w, c = model.jid['jnt_waist'], model.jid['center']
    up0 = np.linalg.inv(model.bind[w][:3, :3]) @ np.array([0, 1.0, 0])
    L = max(anim.last_frame(), 1); frames = np.arange(0, L + 1, dtype=float)
    fixes = []; need = False
    for t in frames:
        loc = anim.locals(t); W = world(model, loc)
        g = W[w][:3, :3] @ up0; g /= np.linalg.norm(g)
        if g[1] >= GOWN_INVERTED:
            fixes.append(np.eye(3)); continue
        need = True
        hz = np.array([g[0], 0, g[2]]); hz = hz / np.linalg.norm(hz) if np.linalg.norm(hz) > 1e-6 else np.array([0, 0, 1.0])
        lim = GOWN_INVERTED + 0.02  # a little inside the limit, against rounding in the keys
        g2 = hz * np.sqrt(1 - lim * lim) + np.array([0, lim, 0])
        fixes.append(rotation_between(g, g2))
    if not need: return False
    out = []; prev = None
    for i, t in enumerate(frames):
        loc = anim.locals(t)
        P0 = loc[0][:3, :3]  # mdl1, the root's parent
        R = P0.T @ fixes[i] @ P0 @ loc[c][:3, :3]
        prev = _euler(R / np.linalg.norm(R, axis=0), prev); out.append(prev)
    out = np.array(out)
    for ax in range(3): set_track(anim, c, ax, 1, frames, out[:, ax], 0.0)
    return True


def body_anim(model, data):
    try:
        a = Anim(data)
    except Exception:
        return None
    return a if len(a.tracks) == len(model.names) else None


def refine(files, mario_files, cid=None, log=print):
    """files: {'bmd/ma_mdl1.bmd': bytes, 'bck/ma_wait.bck': bytes, ...} of a converted character
    (changed in place); mario_files: the same of Sunshine's own mario archive; cid: the archive's
    id (peach, luigi, ...)."""
    body = Model(files['bmd/ma_mdl1.bmd'])
    parts = {n: Model(files[n]) for n in files if n.startswith('bmd/ma_hnd')}
    dropped = drop_untextured(body, parts.values())
    rebound = rebind_center(body)
    dress = gown(body, rebound)
    if dress: sway_gown(body)
    fists = [S for S in 'LR' if cid in REFIST and 'bmd/ma_hnd2%s.bmd' % S.lower() in parts
             and refist(body, parts['bmd/ma_hnd2%s.bmd' % S.lower()], S)]
    import handfit
    turns = []
    for side in 'lr':
        # every hand model of a side is the same open hand: fitted once, on ma_hnd2, to Mario's
        key = 'bmd/ma_hnd2%s.bmd' % side
        if key not in parts or key not in mario_files: continue
        R = fit_open_hand(parts[key], Model(mario_files[key]))
        for n, m in parts.items():
            if n.endswith('%s.bmd' % side): turn_part(m, R)
        turns.append(handfit.angle(R))
    files['bmd/ma_mdl1.bmd'] = atlas_gutter(body.encode(body.body_shape_joints()))
    for n, m in parts.items(): files[n] = m.encode(part=True)
    body = Model(files['bmd/ma_mdl1.bmd'])
    log('  untextured triangles dropped %d, vertices off center %d, gown %s, hands made again %s, '
        'hand models turned %s' % (dropped, len(rebound), dress, ''.join(fists) or '-',
                                    ' '.join('%.0f' % t for t in turns) or '-'))
    mario = Model(mario_files['bmd/ma_mdl1.bmd'])
    skip = (4, 10)  # the cap and the hidden shape of Mario's own model; the character's are empty
    P, Wt = body.vertices(skip); mP, mWt = mario.vertices(skip)
    # the height the character hovers at: his lowest point at rest against Mario's
    hover = max(0.0, float(P[:, 1].min() - mP[:, 1].min()))
    if legless(body): hover = max(hover, HOVER * float(P[:, 1].max() - P[:, 1].min()))
    n = 0; lifts = []; spun = []; reached = []; tipped = []
    # the standing correction: the idle's, on average
    stand = 0.0
    if 'bck/ma_wait.bck' in files and 'bck/ma_wait.bck' in mario_files:
        w = body_anim(body, files['bck/ma_wait.bck']); mw = body_anim(mario, mario_files['bck/ma_wait.bck'])
        if w is not None and mw is not None:
            if dress: damp_legs(w, body, GOWN_LEGS)
            r = ground_offsets(body, P, Wt, w, mario, mP, mWt, mw, hover)[1]
            if r is not None: stand = float(r[:, 1].mean())
    for name in sorted(files):
        if not (name.startswith('bck/') and name in mario_files): continue
        anim = body_anim(body, files[name]); manim = body_anim(mario, mario_files[name])
        if anim is None or manim is None: continue
        if dress:
            damp_legs(anim, body, GOWN_LEGS)
            if pirouette(name, anim, body): spun.append(os.path.basename(name)[:-4])
            if clamp_gown(name, anim, body): tipped.append(os.path.basename(name)[:-4])
        if no_floor(os.path.basename(name)[:-4]):
            frames = np.arange(0, max(anim.last_frame(), 1) + 1, dtype=float)
            off = np.zeros((len(frames), 3)); off[:, 1] = stand; hang_w = None
        else:
            frames, off, hang_w = ground_offsets(body, P, Wt, anim, mario, mP, mWt, manim, hover)
        if off is not None:
            add_center_offset(anim, body, frames, off)
            if reach_hands(anim, body, mario, manim, frames, hang_w): reached.append(os.path.basename(name)[:-4])
            lifts.append((name, float(off[:, 1].min()), float(off[:, 1].max())))
        files[name] = anim.encode(); n += 1
    if spun: log('  pirouettes instead of somersaults: %s' % ' '.join(spun))
    if tipped: log('  gown kept from turning over: %s' % ' '.join(tipped))
    if reached: log('  arms bent to Mario\'s hands while hanging: %d animations' % len(reached))
    big = sorted(lifts, key=lambda x: -max(abs(x[1]), abs(x[2])))[:4]
    log('  animations %d, hover %.1f, largest ground corrections %s' % (
        n, hover, ', '.join('%s %+.1f..%+.1f' % (os.path.basename(a), b, c) for a, b, c in big)))
    return files


def character_id(path):
    """The character's id: its pack's folder for <id>/model.szs, the file's name for a loose <id>.szs."""
    path = os.path.abspath(path)
    if os.path.basename(path).lower() == 'model.szs':
        return os.path.basename(os.path.dirname(path))
    return os.path.splitext(os.path.basename(path))[0]


def archive_files(data):
    d = rarc.yaz0(data)
    out = {}
    for path, e in rarcpack.paths(d).items():
        eo = struct.unpack_from('>I', d, 0x2C)[0] + 0x20
        data_off = struct.unpack_from('>I', d, 0xC)[0] + 0x20
        off, size = struct.unpack_from('>II', d, eo + e * 20 + 8)
        out[path] = bytes(d[data_off + off:data_off + off + size])
    return out


if __name__ == '__main__':
    mario_path, src = sys.argv[1:3]
    dst = sys.argv[3] if len(sys.argv) > 3 else src
    raw = open(src, 'rb').read()
    files = archive_files(raw)
    mario_files = archive_files(open(mario_path, 'rb').read())
    print(character_id(src))
    before = dict(files)
    refine(files, mario_files, character_id(src))
    changed = {k: v for k, v in files.items() if v != before[k]}
    open(dst, 'wb').write(rarcpack.repack(raw, changed))
