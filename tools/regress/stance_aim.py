# Run under gdb with a fresh memory card and SMS_WARP=2,7,1 (Bianco Hills),
# headless. See docs/DEVELOPMENT.md for the invocation.
# Aim with the C-stick in the spray-in-place stance
# (decomp-patches/zzz-camera-07-stance-cstick-aim.patch): holds keys through
# the pad layer's key state and checks what moved. With the setting on, R all
# the way down (Space) and the C-stick turn Mario, the camera keeps up with him, and
# the C-stick's up and down tilt FLUDD the way they tilt the Y close-up
# camera (also checked); with L (Left Shift) the control stick walks him
# sideways while the C-stick turns him. With the setting off the C-stick turns
# the camera and not Mario, as on the GameCube. No game code is skipped.
import gdb

PAD = "'(anonymous namespace)::%s'"
# SDL scancodes of the default bindings
SPACE, LSHIFT, KEY_D, KEY_E = 44, 225, 7, 8          # R, L, STICK_RIGHT, Y
KEY_I, KEY_K, KEY_L = 12, 14, 15                     # CSTICK_UP, _DOWN, _RIGHT
ENTER = 20      # frames the stance (or close-up camera) gets before the C-stick
HOLD = 30       # frames the C-stick is held
REST = 45       # frames between checks, everything released
SQUAT = 0xC008220


def ev(e):
    return gdb.parse_and_eval(e)


def setv(name, value, index=None):
    target = PAD % name + ('' if index is None else '[%d]' % index)
    gdb.execute('set var %s = %d' % (target, value), to_string=True)


def fail(message):
    print('stance_aim: error: ' + message, flush=True)
    gdb.execute('kill')
    gdb.execute('quit 1')


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


# name, aim setting, Invert camera Y, keys held from the start, C-stick key,
# toggle the close-up camera with Y
CHECKS = [
    ('on, R, C right', 1, 0, [SPACE], KEY_L, False),
    ('on, R, C up', 1, 0, [SPACE], KEY_I, False),
    ('on, R, C up, Invert Y', 1, 1, [SPACE], KEY_I, False),
    ('on, close-up camera, C up', 1, 0, [], KEY_I, True),
    ('on, L + stick right, C right', 1, 0, [LSHIFT, KEY_D], KEY_L, False),
    ('on, standing, C right', 1, 0, [], KEY_L, False),
    ('off, R, C right', 0, 0, [SPACE], KEY_L, False),
    ('off, R, C down', 0, 0, [SPACE], KEY_K, False),
]


def values():
    m = '((TMario*)gpMarioAddress)'
    return {
        'yaw': int(ev(m + '->mFaceAngle.y')),
        'cam': int(ev('gpCamera->mCurrentTarget.mYaw')),
        'ratio': float(ev('gpCamera->mCurrentTarget.unk28')),
        'gun': int(ev(m + '->mWaterGun->mNozzleList[' + m + '->mWaterGun->mCurrentNozzle]->unk36E')),
        'x': float(ev(m + '->mPosition.x')),
        'z': float(ev(m + '->mPosition.z')),
        'status': int(ev(m + '->mStatus')) & 0xFFFFFFFF,
        'water': int(ev(m + '->mWaterGun->mCurrentWater')),
        'mode': int(ev('gpCamera->mMode')),
    }


class NormalCamera(gdb.Breakpoint):
    # counts the frames the normal camera reads the sticks (Bianco's opening
    # cameras and the file select come first)
    hits = 0

    def stop(self):
        self.hits += 1
        return False


class Frames(gdb.Breakpoint):
    def __init__(self):
        super().__init__('TMarDirector::direct')
        self.settled = 0
        self.check = 0
        self.frame = 0
        self.start = None
        self.statuses = set()
        self.results = []
        self.waited = 0
        self.normal_hits = 0
        self.error = None

    def keys(self, codes, down):
        for k in codes:
            setv('g_key', 1 if down else 0, k)

    def stop(self):
        self.waited += 1
        if self.waited > 30000:
            return True
        if self.settled < 120:
            playing = (int(ev('gpMarDirector->mState')) == 4
                       and not int(ev('gpApplication.mGamePads[0]->mFlags')) & 1
                       and normal.hits > self.normal_hits)
            self.normal_hits = normal.hits
            self.settled = self.settled + 1 if playing else 0
            return False
        if self.check == len(CHECKS):
            return True
        name, aim, invert, held, cstick, closeup = CHECKS[self.check]
        f = self.frame
        if f == 0:
            setv('g_closeCameraCStick', aim)
            setv('g_invert_cy', invert)
            # a full tank, so the stance holds while it sprays
            gdb.execute('set var ((TMario*)gpMarioAddress)->mWaterGun->mCurrentWater = '
                        '((TMario*)gpMarioAddress)->mWaterGun->mNozzleList[0]->mEmitParams.mAmountMax.value',
                        to_string=True)
            self.keys(held, True)
            if closeup:
                self.keys([KEY_E], True)
            self.statuses = set()
        elif f == 3 and closeup:
            self.keys([KEY_E], False)
        elif f == ENTER:
            self.start = values()
            self.keys([cstick], True)
        elif ENTER < f <= ENTER + HOLD:
            self.statuses.add(values()['status'])
        if f == ENTER + HOLD:
            end = values()
            self.results.append((CHECKS[self.check], self.start, end, sorted(self.statuses)))
            self.keys(held + [cstick], False)
            if closeup:
                self.keys([KEY_E], True)
        if f == ENTER + HOLD + 3 and closeup:
            self.keys([KEY_E], False)
        self.frame += 1
        if self.frame == ENTER + HOLD + REST:
            self.frame = 0
            self.check += 1
        return False


gdb.execute('set confirm off')
normal = NormalCamera('CPolarSubCamera::ctrlNormalOrTowerCamera_')
frames = Frames()
gdb.execute('run')
if frames.error:
    fail(frames.error)
if frames.check != len(CHECKS):
    fail('stopped after %d of %d checks (settled %d frames)' % (frames.check, len(CHECKS), frames.settled))

ok = True
gun = {}
for check, a, b, statuses in frames.results:
    name, aim, invert, held, cstick, closeup = check
    d = {
        'yaw': s16(b['yaw'] - a['yaw']),
        'cam': s16(b['cam'] - a['cam']),
        'ratio': b['ratio'] - a['ratio'],
        'gun': b['gun'] - a['gun'],
        'move': ((b['x'] - a['x']) ** 2 + (b['z'] - a['z']) ** 2) ** 0.5,
    }
    gun[name] = d['gun']
    behind = s16(b['cam'] - (b['yaw'] - 0x8000))
    stance = statuses == [SQUAT]
    if name == 'on, R, C right':
        # the camera keeps up with him
        good = stance and d['yaw'] > 1000 and abs(behind) < d['yaw'] / 4
    elif name == 'on, R, C up':
        good = stance and d['gun'] < -100 and d['yaw'] == 0 and abs(d['ratio']) < 1e-4
    elif name == 'on, R, C up, Invert Y':
        good = stance and d['gun'] > 100 and d['yaw'] == 0
    elif name == 'on, close-up camera, C up':
        # the Y close-up camera's mode (CAMERA_MODE_L_NORMAL) through the hold,
        # and the C-stick's up tilts it, and FLUDD, the same way as in the stance
        good = (a['mode'] == b['mode'] == int(ev('(int)CAMERA_MODE_L_NORMAL'))
                and d['gun'] < -100)
    elif name == 'on, L + stick right, C right':
        good = stance and d['yaw'] > 1000 and d['move'] > 20 and abs(behind) < d['yaw'] / 4
    elif name == 'on, standing, C right':
        good = not stance and d['yaw'] == 0 and d['cam'] > 1000
    elif name == 'off, R, C right':
        good = stance and d['yaw'] == 0 and d['cam'] > 1000
    else:  # off, R, C down: the camera's distance moves, FLUDD stays put
        good = stance and abs(d['ratio']) > 0.01 and abs(d['gun']) < 50
    print('stance_aim: %-30s yaw %+6d camera %+6d (behind %+6d) ratio %+.3f FLUDD %+5d moved %6.1f '
          'stance %s  %s' % (name, d['yaw'], d['cam'], behind, d['ratio'], d['gun'], d['move'],
                             stance, 'ok' if good else 'WRONG'), flush=True)
    ok &= good
print('stance_aim: %s' % ('passed' if ok else 'FAILED'), flush=True)
gdb.execute('kill')
gdb.execute('quit %d' % (0 if ok else 1))
