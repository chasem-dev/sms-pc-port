"""Real B toggles, mouse motion, native camera direction and pointer placement."""
import gdb,math
state={'ready':False,'checks':set()}
def v(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def field():return int(v("'(anonymous namespace)::g_retrace_count'"))
def check(label,ok):
 if label in state['checks']:return
 if not ok:raise AssertionError(label+' mode='+str(v('buildMode'))+' target='+str(v('blockTarget'))+' aim='+str(v('aimHit'))+' message='+v('message').string())
 state['checks'].add(label);print('build-mode-test: PASS '+label,flush=True)
def angle_difference(a,b):return abs((a-b+math.pi)%(2*math.pi)-math.pi)
class Build(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=1 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
   f=field()
   if f<4780:return False
   if not state['ready']:
    for k,n in zip(('x','y','z'),(6300,300,-3850)):put('gpMarioOriginal->mPosition.'+k,n)
    put('gpMarioOriginal->mForwardVel',0);put('previousValid','false')
    put('game.inventory.slots[5].item',5);put('game.inventory.slots[5].count',32)
    state['ready']=True
   if 4840<=f<4845:
    check('B enables mouse build mode',bool(v('buildMode')))
    state['yaw']=float(v('buildYaw'));state['pitch']=float(v('buildPitch'))
    state['angle']=int(v('gpMarioOriginal->mModelFaceAngle'))
   if 4888<=f<4892:
    check('holding and releasing B leaves the toggle enabled',bool(v('buildMode')))
    check('mouse motion turns camera yaw and pitch',angle_difference(float(v('buildYaw')),state['yaw'])>.4 and float(v('buildPitch'))>state['pitch']+.1)
    check('Steve turns toward the pointer',abs(int(v('gpMarioOriginal->mModelFaceAngle'))-state['angle'])>1000)
    check('pointer camera still finds a placement surface',bool(v('aimGround')) or int(v('blockTarget'))>=0)
    # Pointer ray differs from the centre ray and follows the rendered view.
    check('placement ray follows the off-centre pointer',-sum(float(v('aimDirection.'+axis))*float(v('view[2][%d]'%i)) for i,axis in enumerate(('x','y','z')))<.99)
    state['menuYaw']=float(v('buildYaw'));state['menuPitch']=float(v('buildPitch'))
   if 4920<=f<4925:
    check('inventory pointer does not orbit the world camera',int(v('game.menu'))==1 and abs(float(v('buildYaw'))-state['menuYaw'])<.001 and abs(float(v('buildPitch'))-state['menuPitch'])<.001)
   if 4980<=f<4985:
    check('right click places a plank in mouse build mode',int(v('game.inventory.slots[5].count'))==31 and any(int(v('game.cells[%d].item'%i))==5 for i in range(512)))
   if 5030<=f<5035:
    check('second B press toggles back to normal controls',not bool(v('buildMode')))
    state['offYaw']=float(v('buildYaw'))
   if 5070<=f<5075:
    check('mouse motion after toggling off does not steer build camera',not bool(v('buildMode')) and abs(float(v('buildYaw'))-state['offYaw'])<.001)
   if f>=5080:
    if len(state['checks'])!=10:raise AssertionError('missed checks '+str(state['checks']))
    print('build-mode-test: PASS B toggle, free pointer aiming, camera/Steve rotation, menu pause, placement and exit',flush=True)
    gdb.execute('kill',to_string=True);return True
  except Exception as error:print('build-mode-test: FAIL '+str(error),flush=True);gdb.execute('kill',to_string=True);return True
  return False
Build('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"
