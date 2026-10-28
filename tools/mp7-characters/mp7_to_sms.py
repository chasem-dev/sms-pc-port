"""Convert a Mario Party 7 character (HSF) into Super Mario Sunshine's Mario model set.

Builds ma_mdl1.bmd on Sunshine's 29-joint skeleton (same joints, materials, shapes and hierarchy as
the original, so the game's code and all of Mario's animations work), plus the hand and cap models,
and retargets the body animations (.bck) to the character's proportions.
"""
import os, sys, struct, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import hubin, hsf, hsfgeo, gxtex, j3d, bmdwrite, handfit

FINGER_WORDS = ('fing', 'thumb', 'middle', 'first', 'ring', 'child')


def resolve_roles(h, world):
    """Find each Sunshine joint's counterpart in an MP7 skeleton by role, whatever the rig calls it.

    Returns (weight map: MP7 object index -> Sunshine joint name, positions: Sunshine joint name ->
    world bind position or None)."""
    objs = h.objects
    joints = [o['index'] for o in objs if o['type'] == 4]
    def base(i):
        n = objs[i]['name'].lower()
        return n[4:] if n.startswith('ske_') else n
    def find(pred, pool=None):
        c = [i for i in (joints if pool is None else pool) if pred(base(i))]
        return c[0] if c else None
    def pos(i): return world[i][:3, 3].copy() if i is not None else None
    def ancestors(i):
        out = []; p = objs[i]['parent']
        while p != -1:
            out.append(p); p = objs[p]['parent']
        return out
    wmap = {}; P = {}
    hip = find(lambda n: n == 'hip')
    chest = find(lambda n: n in ('body', 'body1'))
    head = find(lambda n: n == 'head') or find(lambda n: 'head' in n, [o['index'] for o in objs if o['type'] != 2])
    if hip is not None: wmap[hip] = 'jnt_waist'
    if chest is not None: wmap[chest] = 'jnt_chest'
    for extra in ('body2', 'neck'):
        j = find(lambda n: n == extra)
        if j is not None: wmap[j] = 'jnt_chest'
    if head is not None: wmap[head] = 'M_head'
    thigh_tops = []
    for S in 'LR':
        s = S.lower()
        ankle = find(lambda n: n in (s + '_shoe1', s + '_reg1'))
        if ankle is not None:
            chain = [a for a in ancestors(ankle) if objs[a]['type'] == 4 and base(a).startswith(s + '_')][::-1]
            leg = chain[-3:] if len(chain) >= 3 else chain
            for extra in chain[:len(chain) - len(leg)]: wmap[extra] = 'jnt_waist'
            thigh, shin = (leg[0], leg[1]) if len(leg) >= 2 else (leg[0], leg[0])
            wmap[thigh] = 'jnt_leg_%s1' % S
            for j in leg[1:]: wmap[j] = 'jnt_leg_%s2' % S
            wmap[ankle] = 'M_foot_%s' % S
            P['jnt_leg_%s1' % S] = pos(thigh); P['jnt_leg_%s2' % S] = pos(shin)
            for n in ('chn_foot_', 'jnt_foot_', 'M_foot_'): P[n + S] = pos(ankle)
            thigh_tops.append(pos(thigh))
        sh = find(lambda n: n == s + '_shoulder')
        a1 = find(lambda n: n == s + '_arm1')
        a2 = find(lambda n: n == s + '_arm2')
        hd = find(lambda n: n in (s + '_hand', s + '_hand1'))
        for j, n in ((sh, 'jnt_sldr_'), (a1, 'jnt_arm_%s1'), (a2, 'jnt_arm_%s2'), (hd, 'M_hand1_')):
            if j is not None: wmap[j] = n % S if '%' in n else n + S
        p1, p2, ph = pos(a1), pos(a2), pos(hd)
        if p1 is not None:
            if p2 is None and ph is not None: p2 = (p1 + ph) / 2
            if ph is None and p2 is not None: ph = p2 + 0.5 * (p2 - p1)
            P['jnt_arm_%s1' % S] = p1; P['jnt_arm_%s2' % S] = p2
            P['jnt_hand_%s' % S] = ph; P['M_hand1_%s' % S] = ph
            P['jnt_sldr_%s' % S] = pos(sh) if sh is not None else np.array([np.sign(p1[0]) * 1.0, p1[1], p1[2]])
    if hip is not None: center = pos(hip)
    elif thigh_tops: center = np.mean(thigh_tops, 0); center[0] = 0
    elif chest is not None: center = pos(chest)
    else: center = None
    P['center'] = P['jnt_waist'] = center
    P['chn_chest'] = P['jnt_chest'] = pos(chest) if chest is not None else center
    if head is not None:
        for n in ('jnt_head', 'M_head_cap1', 'M_head'): P[n] = pos(head)
    if 'jnt_arm_L1' in P: P['eff_sldr_L'] = P['jnt_arm_L1']
    return wmap, P


def hidden_objects(motions, h):
    """Objects the character's motions start hidden (blink lids, spare eye and brow frames): the
    first motion that sets an object's visibility decides it."""
    if not motions: return set()
    decided = {}
    for data in motions:
        m = hsf.HSF(data).motion
        if m is None: continue
        for tr in m['tracks']:
            if tr['type'] == 2 and tr['channel'] == 0x18 and tr['target'] not in decided:
                decided[tr['target']] = (tr['keys'] if tr['curve'] == 4 else tr['keys'][0][1]) != 0
    hidden = {n for n, visible in decided.items() if not visible}
    idx = {o['name']: o['index'] for o in h.objects}
    out = set()
    for o in h.objects:
        i = o['index']
        while i != -1:
            if h.objects[i]['name'] in hidden: out.add(o['index']); break
            i = h.objects[i]['parent']
    return out


# Limb joints whose bone keeps Sunshine's direction (with the character's length): animations twist
# these around their x axis, so the character's limb has to lie on it. Joint -> child that ends the bone.
ALIGN = {'jnt_arm_L1': 'jnt_arm_L2', 'jnt_arm_L2': 'jnt_hand_L', 'jnt_arm_R1': 'jnt_arm_R2', 'jnt_arm_R2': 'jnt_hand_R',
         'jnt_leg_L1': 'jnt_leg_L2', 'jnt_leg_L2': 'chn_foot_L', 'jnt_leg_R1': 'jnt_leg_R2', 'jnt_leg_R2': 'chn_foot_R'}
# A limb further than this from Sunshine's rest direction keeps its own direction: turning a whole
# limb that far would tear it from the body.
ALIGN_MAX_DEG = 35


def rotation_between(a, b):
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b)
    v = np.cross(a, b); c = float(a @ b)
    if np.linalg.norm(v) < 1e-9: return np.eye(3)
    K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + K + K @ K / (1 + c)


# Sunshine body shape slots: index -> rigid joint (None = skinned multi-matrix shape)
SHAPE_JOINT = {0: 'M_head', 1: 'M_head', 2: 'M_head', 3: 'M_head', 4: 'M_head_cap1', 5: 'M_hand1_L',
               6: 'M_hand1_R', 7: 'M_foot_R', 8: 'M_foot_L', 9: None, 10: None}


class Character:
    def __init__(self, mdl_path, sms_bmd, mot_path=None, lift=None):
        self.data = hubin.read(mdl_path)[0]
        self.h = h = hsf.HSF(self.data)
        mot = hubin.read(mot_path) if mot_path and os.path.exists(mot_path) else None
        self.hidden = hidden_objects(mot, h)
        world0, _ = hsfgeo.bind_matrices(h)
        if lift is None:
            # characters modelled around the origin (Boo) are raised to stand where Mario does
            self.meshes, _ = hsfgeo.model_geometry(h, hidden=self.hidden)
            low = min(m['pos'][:, 1].min() for m in self.meshes)
            lift = 0.0 if low > -5 else 10.0 - low
        self.lift = lift
        self.meshes, self.world = hsfgeo.model_geometry(h, hidden=self.hidden, lift=lift)
        self.objname = {o['index']: o['name'] for o in h.objects}
        self.textures = [gxtex.decode_hsf(bm, self.data) for bm in h.bitmaps]
        self.sms = j3d.BMD(sms_bmd)
        self.jnames = [j['name'] for j in self.sms.joints]
        self.jidx = {n: i for i, n in enumerate(self.jnames)}
        self.wmap, self.role_pos = resolve_roles(h, self.world)
        self._retarget_skeleton()
        self._map_weights()

    # ---------------------------------------------------------------- skeleton
    def _retarget_skeleton(self):
        sms = self.sms
        rest = sms.world()
        self.sms_rest = rest
        ci = self.jidx['center']
        center = self.role_pos.get('center')
        if center is None: center = rest[ci][:3, 3].copy()
        pos = {}
        for i, n in enumerate(self.jnames):
            p = self.role_pos.get(n)
            # joints the character has no counterpart for keep Mario's offset from the center
            pos[i] = p if p is not None else center + (rest[i][:3, 3] - rest[ci][:3, 3])
        pos[self.jidx['mdl1']] = rest[self.jidx['mdl1']][:3, 3].copy()
        # world bind = Sunshine rest rotation at the character's joint position
        self.bind = []
        for i in range(len(self.jnames)):
            m = rest[i].copy(); m[:3, 3] = pos[i]; self.bind.append(m)
        self.new_t = []
        for i in range(len(self.jnames)):
            p = sms.parent[i]
            if p is None:
                self.new_t.append(tuple(sms.joints[i]['t'])); continue
            local = np.linalg.inv(self.bind[p]) @ self.bind[i]
            self.new_t.append(tuple(local[:3, 3]))
        # aligned limbs: Sunshine's bone direction at the character's length, and the character's
        # limb mesh rotated onto that direction
        for jn, cn in ALIGN.items():
            j, c = self.jidx[jn], self.jidx[cn]
            d_sms = rest[c][:3, 3] - rest[j][:3, 3]; d_new = pos[c] - pos[j]
            if np.linalg.norm(d_new) < 1e-3 or np.linalg.norm(d_sms) < 1e-3: continue
            cosang = d_sms @ d_new / np.linalg.norm(d_sms) / np.linalg.norm(d_new)
            if np.degrees(np.arccos(np.clip(cosang, -1, 1))) > ALIGN_MAX_DEG: continue
            self.bind[j][:3, :3] = rotation_between(d_sms, d_new) @ rest[j][:3, :3]
            scale = np.linalg.norm(d_new) / np.linalg.norm(d_sms)
            self.new_t[c] = tuple(np.array(sms.joints[c]['t']) * scale)
        self.inv_bind = [np.linalg.inv(m) for m in self.bind]
        self.center_ratio = pos[ci][1] / rest[ci][1, 3]

    def rotate_hand(self, side, R):
        """Turn the hand mesh by R inside its joint frame (so it sits in the frame like Mario's)."""
        R4 = np.eye(4); R4[:3, :3] = R
        for jn in ('jnt_hand_' + side, 'M_hand1_' + side):
            j = self.jidx[jn]
            self.bind[j] = self.bind[j] @ np.linalg.inv(R4)
            self.inv_bind[j] = np.linalg.inv(self.bind[j])

    def _sms_joint_for(self, obj):
        i = obj
        while i is not None and i >= 0:
            if i in self.wmap: return self.jidx[self.wmap[i]]
            i = self.h.objects[i]['parent']
        return self.jidx['center']

    def _map_weights(self):
        for m in self.meshes:
            out = []
            for wd in m['weights']:
                acc = {}
                for obj, w in wd.items():
                    j = self._sms_joint_for(obj); acc[j] = acc.get(j, 0) + w
                acc = {j: w for j, w in acc.items() if w > 0.01}
                s = sum(acc.values())
                out.append(tuple(sorted(((j, w / s) for j, w in acc.items()), key=lambda x: -x[1])[:4]))
            m['sms_weights'] = out

    # ---------------------------------------------------------------- posing (finger curl for the fist)
    def finger_joints(self, side):
        hand = [i for i, n in self.wmap.items() if n == 'M_hand1_' + side]
        out = []
        for o in self.h.objects:
            if o['type'] != 4: continue
            i = o['parent']
            while i != -1:
                if i in hand: out.append(o['index']); break
                i = self.h.objects[i]['parent']
        return out

    def posed_positions(self, curl):
        """World positions with the finger joints curled (Sunshine's default fist).
        curl: {'L': degrees, 'R': degrees}, signed for the rig's bend direction."""
        h = self.h
        fingers = {i: S for S in 'LR' for i in self.finger_joints(S)}
        local = {}
        for o in h.objects:
            sk = h.skeleton.get(o['name'])
            p, r, s = sk if sk else (o['pos'], o['rot'], o['scale'])
            m = hsfgeo.trs(p, r, s)
            n = o['name'].lower()
            if o['index'] in fingers:
                thumb = 'thumb' in n or 'fing1' in n
                ang = curl[fingers[o['index']]] * (0.5 if thumb else 1.0)
                c, s_ = np.cos(np.radians(ang)), np.sin(np.radians(ang))
                m = m @ np.array([[c, -s_, 0, 0], [s_, c, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])
            local[o['index']] = m
        posed = [None]*len(h.objects)
        def walk(i, parent):
            posed[i] = parent @ local[i]
            for c in h.objects[i]['children']: walk(c, posed[i])
        lift = np.eye(4); lift[1, 3] = self.lift
        for o in h.objects:
            if o['parent'] == -1: walk(o['index'], lift)
        skin = [posed[i] @ np.linalg.inv(self.world[i]) for i in range(len(posed))]
        out = []
        for m in self.meshes:
            P = np.zeros_like(m['pos'])
            for k, wd in enumerate(m['weights']):
                v = np.append(m['pos'][k], 1)
                P[k] = sum(w * (skin[o] @ v)[:3] for o, w in wd.items())
            out.append(P)
        return out

    # ---------------------------------------------------------------- textures
    def tex_of(self, mat):
        M = self.h.materials[mat]
        if not M['attrs']: return None
        return self.h.attributes[M['attrs'][0]]['bitmap']

    def _named_eye(self, mat):
        for a in self.h.materials[mat]['attrs']:
            nm = (self.h.attributes[a]['name'] or '').upper()
            if 'L_EYE' in nm: return 'L'
            if 'R_EYE' in nm: return 'R'
        return None

    def eye_side(self, mat):
        """'L'/'R' for a material that draws only an eye (Sunshine blinks it), else None. A texture
        attribute named like an eye is not enough: Boo's whole body is drawn through one."""
        if not hasattr(self, '_eyes'):
            count = {}; users = {}
            for m in self.meshes:
                for mt, _ in m['tris']:
                    count[mt] = count.get(mt, 0) + 1
                    users.setdefault(self.tex_of(mt), set()).add(mt)
            small = {mt for mt in count if self._named_eye(mt) and count[mt] < 300}
            self._eyes = {mt: self._named_eye(mt) for mt in small if users[self.tex_of(mt)] <= small}
            # both eyes blink or neither does
            if set(self._eyes.values()) != {'L', 'R'}: self._eyes = {}
        return self._eyes.get(mat)

    def build_atlas(self):
        """Pack every non-eye texture the body uses into one atlas (texture 0), the smallest of
        512x512, 1024x512 and 1024x1024 that holds them."""
        used = set()
        for m in self.meshes:
            for mat, _ in m['tris']:
                t = self.tex_of(mat)
                if t is not None and self.eye_side(mat) is None: used.add(t)
        big = [t for t in sorted(used, key=lambda t: -self.textures[t].shape[0]*self.textures[t].shape[1])]
        def grown(img):
            # tiny textures are enlarged so filtering does not bleed in their neighbours
            hh, ww = img.shape[:2]
            if max(hh, ww) < 64:
                f = 64 // max(hh, ww); img = img.repeat(f, 0).repeat(f, 1)
            return img
        imgs = {t: grown(self.textures[t]) for t in big}
        big.sort(key=lambda t: (-imgs[t].shape[0], -imgs[t].shape[1]))
        for W, H in ((512, 512), (1024, 512), (1024, 1024)):
            rects = {}; x = y = shelf = 0; ok = True
            for t in big:
                hh, ww = imgs[t].shape[:2]
                if x + ww > W: x = 0; y += shelf; shelf = 0
                if y + hh > H or ww > W: ok = False; break
                rects[t] = (x, y); x += ww; shelf = max(shelf, hh)
            if ok: break
        else:
            raise RuntimeError('textures do not fit a 1024x1024 atlas')
        atlas = np.zeros((H, W, 4), np.uint8); atlas[..., 3] = 255
        for t, (x, y) in rects.items():
            hh, ww = imgs[t].shape[:2]
            atlas[y:y+hh, x:x+ww] = imgs[t]
            rects[t] = (x / W, y / H, ww / W, hh / H)
        self.atlas = atlas; self.rects = rects
        return atlas

    def eye_frames(self):
        """Sunshine's five eye textures: open, then closing lids drawn over the character's eye."""
        eye_tex = None
        for m in self.meshes:
            for mat, _ in m['tris']:
                if self.eye_side(mat): eye_tex = self.tex_of(mat)
        if eye_tex is None: return []
        img = self.textures[eye_tex].copy()
        skin = self.skin_color()
        # the eye geometry only samples this UV window
        uvs = []
        for m in self.meshes:
            for mat, c in m['tris']:
                if self.eye_side(mat): uvs += [m['st'][k[3]] for k in c]
        uvs = np.array(uvs); v0, v1 = uvs[:, 1].min(), uvs[:, 1].max()
        H = img.shape[0]
        frames = []
        for frac in (0.0, 0.4, 0.7, 1.0, 1.0):
            f = img.copy()
            if frac > 0:
                edge = int((v0 + (v1 - v0) * frac) * H)
                f[:edge, :, :3] = skin; f[:edge, :, 3] = 255
                line = slice(max(edge - 3, 0), min(edge + 1, H))
                f[line, :, :3] = (skin * 0.35).astype(np.uint8)
            frames.append(f)
        return frames

    def skin_color(self):
        """Most common light, warm color on the main texture (the character's skin)."""
        t = self.textures[max(self.rects, key=lambda t: self.textures[t].size)] if hasattr(self, 'rects') else self.textures[0]
        px = t[..., :3].reshape(-1, 3).astype(int)
        warm = px[(px[:, 0] > 200) & (px[:, 1] > 120) & (px[:, 1] < 210) & (px[:, 2] > 80) & (px[:, 2] < 190)]
        if len(warm) < 50: return np.array([250, 180, 140], np.uint8)
        return np.median(warm, 0).astype(np.uint8)


def classify(ch, hat_tex):
    """Assign each MP7 triangle to a Sunshine body shape slot, a hand, or the cap."""
    jid = ch.jidx
    rigid = {'M_head': jid['M_head'], 'M_hand1_L': jid['M_hand1_L'], 'M_hand1_R': jid['M_hand1_R'],
             'M_foot_L': jid['M_foot_L'], 'M_foot_R': jid['M_foot_R']}
    out = []  # (mesh_i, tri_i, slot)
    for mi, m in enumerate(ch.meshes):
        for ti, (mat, c) in enumerate(m['tris']):
            joints = [m['sms_weights'][k[0]] for k in c]
            single = [w[0][0] if len(w) == 1 else None for w in joints]
            tex = ch.tex_of(mat)
            side = ch.eye_side(mat)
            if side:
                slot = 1 if side == 'L' else 3
            elif hat_tex is not None and tex == hat_tex:
                slot = 'cap'
            elif all(s == rigid['M_head'] for s in single):
                slot = 0
            elif all(s == rigid['M_hand1_L'] for s in single):
                slot = 5
            elif all(s == rigid['M_hand1_R'] for s in single):
                slot = 6
            elif all(s == rigid['M_foot_R'] for s in single):
                slot = 7
            elif all(s == rigid['M_foot_L'] for s in single):
                slot = 8
            else:
                slot = 9
            out.append((mi, ti, slot))
    return out


class GeometryBuilder:
    """Dedupes positions/normals/uvs and builds drw/envelope tables for one BMD."""
    def __init__(self, ch, positions, rigid_space=None):
        self.ch = ch; self.positions = positions
        self.pos = []; self.nrm = []; self.uv = []
        self.pmap = {}; self.nmap = {}; self.umap = {}
        self.drw = []; self.dmap = {}; self.env = []; self.emap = {}
        self.rigid_space = rigid_space  # force all vertices into this joint's local space (part models)

    def binding(self, mi, pi):
        w = self.ch.meshes[mi]['sms_weights'][pi]
        if self.rigid_space is not None: return ('rigid', self.rigid_space)
        if len(w) == 1: return ('rigid', w[0][0])
        return ('env', w)

    def drw_index(self, b):
        if b[0] == 'rigid':
            key = ('r', b[1])
            if key not in self.dmap: self.dmap[key] = len(self.drw); self.drw.append((0, b[1]))
        else:
            env = tuple((j, round(w, 4)) for j, w in b[1])
            if env not in self.emap: self.emap[env] = len(self.env); self.env.append(list(env))
            key = ('e', env)
            if key not in self.dmap: self.dmap[key] = len(self.drw); self.drw.append((1, self.emap[env]))
        return self.dmap[key]

    def finalize(self, shapes):
        """J3D expects every rigid draw matrix before the weighted ones, and weighted entry i to be
        envelope i: reorder the table (weighted entries keep their envelope order) and remap packets."""
        order = sorted(range(len(self.drw)), key=lambda i: (self.drw[i][0], i))
        new = {old: k for k, old in enumerate(order)}
        self.drw = [self.drw[i] for i in order]
        for sh in shapes:
            for pk in sh['packets']:
                pk['mtx'] = [new[m] for m in pk['mtx']]

    def to_space(self, b, p, is_normal=False):
        if b[0] == 'env': return p
        M = self.ch.inv_bind[b[1]]
        if is_normal: return M[:3, :3] @ p
        return (M @ np.append(p, 1))[:3]

    def corner(self, mi, k, uv):
        m = self.ch.meshes[mi]
        b = self.binding(mi, k[0])
        pk = (mi, k[0])
        if pk not in self.pmap:
            self.pmap[pk] = len(self.pos); self.pos.append(self.to_space(b, self.positions[mi][k[0]]))
        nk = (mi, k[0], max(k[1], 0))
        if nk not in self.nmap:
            n = self.to_space(b, m['nrm'][max(k[1], 0)], True); n = n / (np.linalg.norm(n) + 1e-9)
            self.nmap[nk] = len(self.nrm); self.nrm.append(n)
        uk = tuple(np.round(uv, 6))
        if uk not in self.umap: self.umap[uk] = len(self.uv); self.uv.append(uv)
        return self.drw_index(b), self.pmap[pk], self.nmap[nk], self.umap[uk]


def atlas_uv(ch, mat, st):
    t = ch.tex_of(mat)
    if t is None or t not in ch.rects: return np.array(st, float)
    x, y, w, h = ch.rects[t]
    u = np.clip(st[0], 0, 1); v = np.clip(st[1], 0, 1)
    return np.array([x + u * w, y + v * h])


def make_shapes(builder, tri_lists, mtypes, joints):
    """tri_lists[s] = list of (mi, mat, corners); build SHP1 shape dicts."""
    shapes = []
    for s, tris in enumerate(tri_lists):
        corners = []
        for mi, mat, c in tris:
            cc = []
            for k in c:
                st = builder.ch.meshes[mi]['st'][max(k[3], 0)]
                uv = st if mat == 'eye' else atlas_uv(builder.ch, mat, st)
                cc.append(builder.corner(mi, k, uv))
            corners.append(cc)
        if not corners:
            # keep the slot: one zero-area triangle bound to the slot's joint
            j = joints[s] if joints[s] is not None else builder.ch.jidx['center']
            if not builder.pos: builder.pos.append(np.zeros(3)); builder.nrm.append(np.array([0, 1, 0.])); builder.uv.append(np.zeros(2))
            d = builder.drw_index(('rigid', j))
            corners = [[(d, 0, 0, 0)]*3]
        if mtypes[s] == 0:
            d0 = corners[0][0][0]
            packets = [dict(mtx=[d0], tris=[[(None, p, n, t) for _, p, n, t in tri] for tri in corners])]
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
        shapes.append(dict(mtype=mtypes[s], packets=packets))
    return shapes


def bounds(shapes, builder):
    for sh in shapes:
        idx = [p for pk in sh['packets'] for tri in pk['tris'] for _, p, _, _ in tri]
        P = np.array([builder.pos[i] for i in idx])
        mn, mx = P.min(0), P.max(0)
        sh['bbox'] = (mn, mx); sh['radius'] = float(np.linalg.norm(np.maximum(abs(mn), abs(mx))))


def build_body(ch, tmpl, positions, slots):
    tri_lists = [[] for _ in range(11)]
    for mi, ti, slot in slots:
        if slot == 'cap': continue
        mat, c = ch.meshes[mi]['tris'][ti]
        tri_lists[slot].append((mi, 'eye' if slot in (1, 3) else mat, c))
    b = GeometryBuilder(ch, positions)
    mtypes = [0]*9 + [3, 3]
    joints = [ch.jidx[SHAPE_JOINT[s]] if SHAPE_JOINT[s] else None for s in range(11)]
    shapes = make_shapes(b, tri_lists, mtypes, joints)
    b.finalize(shapes)
    bounds(shapes, b)
    secs = j3d.sections(tmpl)
    sec = lambda tag: tmpl[secs[tag][0]:secs[tag][0] + secs[tag][1]]
    shp, npk = bmdwrite.shp1(shapes)
    eyes = ch.eye_frames()
    skin = np.zeros((32, 16, 4), np.uint8); skin[..., :3] = ch.skin_color(); skin[..., 3] = 255
    rep = {'H_ma_new_main_s3tc': (14, ch.atlas), 'H_ma_mouse1_s3tc': skin}
    if eyes:
        for k in range(5): rep['H_ma_eye%d_s3tc' % (k+1)] = eyes[k]
    new = {
        'INF1': bmdwrite.inf1_patch(sec('INF1'), npk, len(b.pos)),
        'VTX1': bmdwrite.vtx1(np.array(b.pos), np.array(b.nrm), np.array(b.uv)),
        'EVP1': bmdwrite.evp1(b.env, ch.inv_bind),
        'DRW1': bmdwrite.drw1(b.drw),
        'JNT1': bmdwrite.jnt1_patch(sec('JNT1'), ch.new_t),
        'SHP1': shp,
        'TEX1': bmdwrite.tex1(sec('TEX1'), rep),
    }
    return bmdwrite.assemble(tmpl, new), tri_lists


def build_part(ch, tmpl, positions, tris, space_joint):
    """A single-shape part model (hand or cap) rigid in one Sunshine joint's frame."""
    b = GeometryBuilder(ch, positions, rigid_space=space_joint)
    shapes = make_shapes(b, [tris], [0], [space_joint])
    # every vertex is in space_joint's frame, drawn with the part model's joint 0
    for pk in shapes[0]['packets']: pk['mtx'] = [0]
    b.drw = [(0, 0)]
    bounds(shapes, b)
    secs = j3d.sections(tmpl)
    sec = lambda tag: tmpl[secs[tag][0]:secs[tag][0] + secs[tag][1]]
    shp, npk = bmdwrite.shp1(shapes)
    new = {
        'INF1': bmdwrite.inf1_patch(sec('INF1'), npk, len(b.pos)),
        'VTX1': bmdwrite.vtx1(np.array(b.pos), np.array(b.nrm), np.array(b.uv)),
        'EVP1': bmdwrite.evp1([], []),
        'DRW1': bmdwrite.drw1([(0, 0)]),
        'SHP1': shp,
    }
    return bmdwrite.assemble(tmpl, new)


def retarget_bck(ch, data):
    """Rewrite a 29-joint body animation so joint offsets match the character's skeleton."""
    k = j3d.BCK(data)
    if len(k.anims) != len(ch.jnames): return None
    a = k.base
    d = bytearray(data)
    jo, so, ro, to = k.offsets
    trans = []
    center = ch.jidx['center']
    for i, comps in enumerate(k.anims):
        for ax in range(3):
            cnt, idx, tan = comps[ax][2]
            stride = 1 if cnt == 1 else (4 if tan == 1 else 3)
            vals = k.trans[idx: idx + cnt*stride].reshape(cnt, stride).copy() if cnt > 1 else np.array([[k.trans[idx]]])
            col = 0 if cnt == 1 else 1
            if i == center:
                if ax == 1:
                    vals[:, col:] *= ch.center_ratio
            else:
                vals[:, col] += ch.new_t[i][ax] - ch.sms.joints[i]['t'][ax]
            new_idx = len(trans)
            trans += list(vals.reshape(-1))
            struct.pack_into('>HHH', d, a + jo + i*54 + ax*18 + 12, cnt, new_idx, tan)
    tbytes = np.asarray(trans, '>f4').tobytes()
    # translation table is the section's last table: replace it and fix sizes
    head = bytes(d[:a + to])
    out = bytearray(head + tbytes)
    out += b'\0' * ((-len(out)) % 32)
    struct.pack_into('>H', out, a + 0x12, len(trans))
    struct.pack_into('>I', out, a + 4, len(out) - a)
    struct.pack_into('>I', out, 8, len(out))
    return bytes(out)


def convert(mdl_path, sms_dir, out_dir, mot_path=None, hat=True, curl=75, lift=None):
    bmd = lambda n: open(os.path.join(sms_dir, 'bmd', n + '.bmd'), 'rb').read()
    tmpl = bmd('ma_mdl1')
    ch = Character(mdl_path, tmpl, mot_path, lift)
    ch.build_atlas()
    hat_tex = None
    if hat:
        names = [bm['name'].lower() for bm in ch.h.bitmaps]
        hat_tex = next((i for i, n in enumerate(names) if 'hat' in n or 'cap' in n), None)
    slots = classify(ch, hat_tex)
    rest = [m['pos'] for m in ch.meshes]
    mario_fist = {S: handfit.points(tmpl, {shape}) for S, shape in (('L', 5), ('R', 6))}
    # rigs bend their fingers either way round: curl each hand both ways and keep the one that
    # fits Mario's fist best, then turn the hand inside its joint to match his
    best = {}
    for sign in (1, -1):
        fist = ch.posed_positions({'L': curl * sign, 'R': -curl * sign})
        body, tri_lists = build_body(ch, tmpl, fist, slots)
        for S, shape in (('L', 5), ('R', 6)):
            if not tri_lists[shape]: continue
            R, err = handfit.fit(mario_fist[S], handfit.points(body, {shape}))
            if S not in best or err < best[S][2]: best[S] = (sign, R, err)
    curls = {S: (curl if S == 'L' else -curl) * best[S][0] if S in best else 0 for S in 'LR'}
    for S, (sign, R, err) in best.items():
        ch.rotate_hand(S, R)
        print('  hand %s: curl %+d, turned %.1f deg (fit residual %.2f)' % (
            S, curls[S], np.degrees(np.arccos(np.clip((np.trace(R) - 1) / 2, -1, 1))), err))
    fist = ch.posed_positions(curls)
    body, tri_lists = build_body(ch, tmpl, fist, slots)
    files = {'bmd/ma_mdl1.bmd': body}
    jid = ch.jidx
    for side, slot in (('l', 5), ('r', 6)):
        hand_joint = jid['jnt_hand_' + side.upper()]
        for n in ('ma_hnd2', 'ma_hnd3') + (('ma_hnd4',) if side == 'r' else ()):
            files['bmd/%s%s.bmd' % (n, side)] = build_part(ch, bmd(n + side), rest, tri_lists[slot], hand_joint)
    cap_tris = [(mi, ch.meshes[mi]['tris'][ti][0], ch.meshes[mi]['tris'][ti][1]) for mi, ti, s in slots if s == 'cap']
    for n in ('ma_cap1', 'ma_cap3'):
        files['bmd/%s.bmd' % n] = build_part(ch, bmd(n), rest, cap_tris, jid['M_head'])
    nbck = 0
    for p in glob.glob(os.path.join(sms_dir, 'bck', '*.bck')):
        r = retarget_bck(ch, open(p, 'rb').read())
        if r is not None:
            files['bck/' + os.path.basename(p)] = r; nbck += 1
    os.makedirs(out_dir, exist_ok=True)
    for rel, b in files.items():
        os.makedirs(os.path.dirname(os.path.join(out_dir, rel)), exist_ok=True)
        open(os.path.join(out_dir, rel), 'wb').write(b)
    counts = {s: len(t) for s, t in enumerate(tri_lists)}
    print('  shapes', counts, 'cap', len(cap_tris), 'bcks', nbck, 'center ratio %.3f' % ch.center_ratio,
          'lift %.1f' % ch.lift, 'atlas', ch.atlas.shape[1::-1])
    return ch, files


if __name__ == '__main__':
    # mp7_to_sms.py <MP7 files/data> <Sunshine mario/ folder> <output folder> <character>...
    data, sms_dir, out = sys.argv[1:4]
    for c in sys.argv[4:]:
        print(c)
        convert(os.path.join(data, c + 'mdl0.bin'), sms_dir, os.path.join(out, c),
                os.path.join(data, c + 'mot.bin'))
