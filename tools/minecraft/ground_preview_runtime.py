"""Native level-camera ground targeting, real walking and block placement."""
import gdb
state={'ready':False,'checks':set()}
def v(expr):return gdb.parse_and_eval(expr)
def put(expr,n):gdb.execute('set var %s = %s'%(expr,n),to_string=True)
def field():return int(v("'(anonymous namespace)::g_retrace_count'"))
def check(label,ok):
 if label in state['checks']:return
 if not ok:raise AssertionError(label+' hit='+str(v('aimHit'))+' position='+str(v('gpMarioOriginal->mPosition'))+' target='+str(v('blockTarget')))
 state['checks'].add(label);print('ground-preview-test: PASS '+label,flush=True)
class Camera(gdb.Breakpoint):
 def stop(self):
  if state['ready']:
   p=v('gpMarioOriginal->mPosition');x,y,z=[float(p[k]) for k in ('x','y','z')]
   for key,n in zip(('x','y','z'),(x,y+220,z-500)):put('camPos->'+key,n)
   for key,n in zip(('x','y','z'),(x,y+220,z+180)):put('target->'+key,n)
  return False
Camera('C_MTXLookAt').condition="gpCamera != 0 && camPos == (Vec*)&gpCamera->unk124 && '(anonymous namespace)::g_retrace_count' >= 4780"
class Preview(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=1 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
   f=field()
   if f<4780:return False
   if not state['ready']:
    for key,n in zip(('x','y','z'),(6300,300,-3850)):put('gpMarioOriginal->mPosition.'+key,n)
    put('gpMarioOriginal->mForwardVel',0);put('previousValid','false')
    put('game.inventory.slots[5].item',5);put('game.inventory.slots[5].count',32)
    state['ready']=True
   if 4820<=f<4830:
    check('level camera still finds nearby ground while standing',bool(v('aimGround')) and abs(float(v('aimDirection.y')))<.03 and int(v('blockTarget'))<0)
    hit=v('aimHit');pos=v('gpMarioOriginal->mPosition')
    check('ground detector stays close and below the camera ray',90<float(hit['z'])-float(pos['z'])<210 and abs(float(hit['y'])-float(pos['y']))<5)
    state['startZ']=float(pos['z'])
   if 4880<=f<4890:
    check('actual native walking keeps a ground placement target',bool(v('aimGround')) and float(v('gpMarioOriginal->mForwardVel'))>0 and float(v('gpMarioOriginal->mPosition.z'))>state['startZ']+30)
   if 4930<=f<4940:
    cells=[i for i in range(256) if int(v('game.cells[%d].item'%i))==5]
    check('right click places a plank from the lower ground preview',len(cells)==1 and int(v('game.inventory.slots[5].count'))==31)
    state['cell']=cells[0]
    check('lower check detects the placed block before terrain behind it',int(v('blockTarget'))==state['cell'])
   if f>=4960:
    if len(state['checks'])!=5:raise AssertionError('missed checks: '+str(state['checks']))
    print('ground-preview-test: PASS standing, walking, placement and nearby block targeting',flush=True)
    gdb.execute('kill',to_string=True);return True
  except Exception as error:
   print('ground-preview-test: FAIL '+str(error),flush=True);gdb.execute('kill',to_string=True);return True
  return False
Preview('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"
