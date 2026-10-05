# Live native pooled enemies; only fixture state/positions are written in gdb.
# All swings and menu actions use the real key event route.
import gdb,os
state={'ready':False,'checks':set(),'hits':0,'kills':0,'killEvents':set(),'goo':0,'walker':0,'pos':None}
def v(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def field():return int(v("'(anonymous namespace)::g_retrace_count'"))
def log(s):print('sword-test: '+s,flush=True)
def fail(s):log('FAIL '+s);gdb.execute('kill',to_string=True);return True

def actors():
 managers=v('gpConductor->unk20');node=managers['oEnd_']['pNext_'];end=int(managers['oEnd_'].address)
 while int(node)!=end:
  manager=(node+1).cast(gdb.lookup_type('TEnemyManager').pointer().pointer()).dereference()
  for i in range(int(manager['mObjNum'])):
   actor=manager['unk18'][i].cast(gdb.lookup_type('TSpineEnemy').pointer())
   if int(actor):yield actor
  node=node['pNext_']

class Update(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=2 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
   f=field()
   if f<4780:return False
   if not state['ready']:
    enemies=list(actors())
    goo=next(e for e in enemies if int(e['mActorType'])==0x10000003)
    walker=next(e for e in enemies if int(e['mActorType'])==0x10000002 and not int(e['mLiveFlag'])&3)
    state['goo']=int(goo);state['walker']=int(walker)
    gdb.set_convenience_variable('swordEnemy',goo);gdb.set_convenience_variable('swordWalker',walker)
    # Activate an already initialized native Goopy Stu pool entry in its
    # existing generation nerve. Its receiveMessage/kill are never replaced.
    if os.environ.get('SMS_SWORD_CLICK_MODE')!='natural':
     put('$swordEnemy->mLiveFlag',int(goo['mLiveFlag'])&~(1|2|4|64|128))
     put('$swordEnemy->mHitFlags',int(goo['mHitFlags'])&~(1|4))
     put('$swordEnemy->mHitPoints',1)
     put('$swordEnemy->mSpine->mCurrent','$swordEnemy->mSpine->unk18')
     put('$swordEnemy->mSpine->mTime',0)
    p=v('gpMarioOriginal->mPosition');state['pos']=[float(p[k]) for k in ('x','y','z')]
    state['ready']=True
    log('fixture activated a native Goopy Stu pool entry; real Strollin Stu also selected')
   if f<4990:
    x,y,z=state['pos']
    for key,val in zip(('x','y','z'),(x,y,z)):put('gpMarioOriginal->mPosition.'+key,val)
    angle=16384 if f>=4960 else 0
    put('gpMarioOriginal->mFaceAngle.y',angle);put('gpMarioOriginal->mModelFaceAngle',angle)
    put('gpMarioOriginal->mForwardVel',0)
    if f<4945:
     distance=-180 if f<4830 else 400 if f<4860 else 180
     for key,val in zip(('x','y','z'),(x,y,z+distance)):put('$swordEnemy->mPosition.'+key,val)
     for key in ('x','y','z'):put('$swordEnemy->mLinearVelocity.'+key,0)
    if 4960<=f<4990:
     for key,val in zip(('x','y','z'),(x+180,y,z)):put('$swordWalker->mPosition.'+key,val)
     for key in ('x','y','z'):put('$swordWalker->mLinearVelocity.'+key,0)
   for at,name in [(4820,'behind Steve'),(4850,'out of reach'),(4880,'axe equipped'),(4920,'inventory open')]:
    if at<=f<at+10 and name not in state['checks']:
     if state['hits'] or state['kills']:return fail('attack occurred with '+name)
     state['checks'].add(name);log('PASS no attack: '+name)
   if 4950<=f<4960 and 'goo' not in state['checks']:
    if state['hits']!=1 or state['kills']!=1:return fail('Goopy Stu did not receive exactly one native stomp and kill: hits=%d kills=%d flags=%x'%(state['hits'],state['kills'],int(v('$swordEnemy->mLiveFlag'))))
    if not int(v('$swordEnemy->mLiveFlag'))&64:return fail('native death reaction not started')
    if float(v('aimDirection.z'))>-.8:return fail('camera fixture was not looking opposite Steve')
    state['checks'].add('goo');log('PASS model-forward swing hits Goopy Stu while camera looks backward; native stomp/death reaction')
   if 4990<=f<5000 and 'walker' not in state['checks']:
    if state['hits']!=2 or state['kills']!=2:return fail('Strollin Stu did not receive one native stomp and kill')
    if float(v('aimDirection.x'))>-.8:return fail('rotated camera fixture was not looking opposite Steve')
    state['checks'].add('walker');log('PASS model rotated 90 degrees hits Strollin Stu independently of opposite camera; one enemy per swing')
   if f>=5160:
    if len(state['checks'])!=6:return fail('missed scenario checks')
    if not int(v('$swordEnemy->mLiveFlag'))&1:return fail('Goopy Stu death animation never completed')
    log('PASS real input, forward reach, tool/menu restrictions, native enemy death and completion')
    gdb.execute('kill',to_string=True);return True
  except Exception as e:return fail(str(e))
  return False

class Message(gdb.Breakpoint):
 def stop(self):
  if not state['ready']:return False
  if int(v('this')) in (state['goo'],state['walker']) and int(v('message'))==0:
   if int(v('sender'))!=int(v('gpMarioOriginal')):return fail('wrong stomp sender')
   if not any('swordAttack' in frame.name() for frame in frames() if frame.name()):return fail('test enemy stomp did not come from sword')
   state['hits']+=1;log('native stomp entered: type=%x'%int(v('this->mActorType')))
  return False

def frames():
 frame=gdb.newest_frame()
 while frame:
  yield frame;frame=frame.older()

class Kill(gdb.Breakpoint):
 def stop(self):
  if state['ready'] and int(v('this')) in (state['goo'],state['walker']):
   if any('swordAttack' in frame.name() for frame in frames() if frame.name()):
    # Optimized code lists several inline breakpoint locations for the
    # same native kill path. Record its entry once per actor/stomp message.
    event=(int(v('this')),state['hits'])
    if event not in state['killEvents']:
     state['killEvents'].add(event);state['kills']+=1
     log('native kill reached: type=%x'%int(v('this->mActorType')))
  return False
Update('sms_minecraft_update')
Message('TSmallEnemy::receiveMessage')
Kill('TSmallEnemy::kill')

# Look opposite the model's forward direction. The behind-player rejection
# and both native kills must depend on model rotation, not the cached camera.
class CameraAim(gdb.Breakpoint):
 def stop(self):
  if state['pos']:
   x,y,z=state['pos']
   rotated=field()>=4960
   camera=(x+500,y+320,z+220) if rotated else (x+220,y+320,z+500)
   target=(x-180,y+40,z) if rotated else (x,y+40,z-180)
   for k,n in zip(('x','y','z'),camera):put('camPos->'+k,n)
   for k,n in zip(('x','y','z'),target):put('target->'+k,n)
  return False
CameraAim('C_MTXLookAt').condition="gpCamera != 0 && camPos == (Vec*)&gpCamera->unk124 && '(anonymous namespace)::g_retrace_count' >= 4780"
