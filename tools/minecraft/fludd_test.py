# Run inside gdb. Check the live authored model mount while turning,
# running, jumping and spraying; no inferior function calls are used.
import gdb
import math
samples=set()
water=None
jumped=False
ran=False
sprayed=False

def value(s): return gdb.parse_and_eval(s)
def field(): return int(value("'(anonymous namespace)::g_retrace_count'"))
def ready():
    return int(value('gpMarDirector')) and int(value('gpMarDirector->mMap'))==1 and int(value('gpMarDirector->mState'))==4 and int(value('gpMarDirector->unk124'))==0

def fail(message):
    print('fludd-test: FAIL '+message,flush=True)
    gdb.execute('kill',to_string=True)
    return True

class Turn(gdb.Breakpoint):
    def stop(self):
        global water
        if not ready(): return False
        f=field()
        if f<4800:return False
        if water is None:water=int(value('gpMarioOriginal->mWaterGun->mCurrentWater'))
        # Cardinal directions, a diagonal and the signed angle boundary.
        if f<5204:
            angle=[0,16384,-32768,-16384,8192,0][min((f-4800)//80,5)]
            gdb.execute('set var gpMarioOriginal->mModelFaceAngle = %d'%angle,to_string=True)
            gdb.execute('set var gpMarioOriginal->mFaceAngle.y = %d'%angle,to_string=True)
        return False

class Check(gdb.Breakpoint):
    def stop(self):
        global jumped,ran,sprayed
        try:
            if not ready() or field()<4800:return False
            f=field()
            p=value('gpMarioOriginal->mPosition')
            m=value('gpMarioOriginal->mWaterGun->mFluddModel->mModel->unk20')
            angle=int(value('gpMarioOriginal->mModelFaceAngle'))*math.pi/32768
            c,s=math.cos(angle),math.sin(angle)
            # Independent world-space invariants: local X stays vertical;
            # local Y points forward, local Z right; no skew or mirrored basis.
            expected=[[0,.75*s,.75*c],[.75,0,0],[0,.75*c,-.75*s]]
            for r in range(3):
                for col in range(3):
                    if abs(float(m[r][col])-expected[r][col])>.0002:return fail('pack rotation diverged from Steve')
            position=[float(p['x'])-4*s,float(p['y'])+78,float(p['z'])-4*c]
            if any(abs(float(m[r][3])-position[r])>.003 for r in range(3)):return fail('pack detached from torso')
            if int(value('gpMarioOriginal->mModelFaceAngle')) in [0,16384,-32768,-16384,8192]:samples.add(int(value('gpMarioOriginal->mModelFaceAngle')))
            if f>=5204 and abs(float(value('gpMarioOriginal->mForwardVel')))>1:ran=True
            if f>=5320 and abs(float(value('gpMarioOriginal->mVel.y')))>1:jumped=True
            if water is not None and int(value('gpMarioOriginal->mWaterGun->mCurrentWater'))<water:sprayed=True
            if f>=5600:
                if len(samples)<5:return fail('missing turn directions')
                if not ran or not jumped or not sprayed:return fail('missing running, jumping or water spray')
                print('fludd-test: PASS stable torso mount at five angles, signed-angle wrap, running, jumping and real water spray',flush=True)
                gdb.execute('kill',to_string=True)
                return True
        except Exception as e:return fail(str(e))
        return False
Turn('sms_minecraft_attach_fludd')
Check('sms_minecraft_draw')
