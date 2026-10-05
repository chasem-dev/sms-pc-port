# Native mouse holds exercise interruption, visible crack stages, felling and drops.
import gdb
state={'ready':False,'checks':set(),'tree':-1}
def v(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def field():return int(v("'(anonymous namespace)::g_retrace_count'"))
def fail(s):
 print('mining-test: FAIL '+s,flush=True);gdb.execute('kill',to_string=True);return True
def check(label,condition):
 if label in state['checks']:return
 if not condition:raise AssertionError(label+' (target=%s, progress=%s, mesh=%s, position=%s, manager=%s)'%(v('treeBreak.target'),v('treeBreak.seconds'),v('crackMeshDraws'),v('gpMarioOriginal->mPosition'),v('trees[%d].actor->mMapCollisionManager'%state['tree'])))
 state['checks'].add(label);print('mining-test: PASS '+label,flush=True)
class Mining(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=1 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
   f=field()
   if not state['ready']:
    if not int(v('treeCount')):return fail('no native palms registered')
    p=v('gpMarioOriginal->mPosition');candidates=[]
    for i in range(int(v('treeCount'))):
     t=v('trees[%d].actor->mPosition'%i);candidates.append(((float(t['x'])-float(p['x']))**2+(float(t['z'])-float(p['z']))**2,i))
    _,state['tree']=min(candidates);i=state['tree'];t=v('trees[%d].actor->mPosition'%i)
    state['position']=[float(t[k]) for k in ('x','y','z')];x,y,z=state['position']
    put('gpMarioOriginal->mPosition.x',x);put('gpMarioOriginal->mPosition.y',y);put('gpMarioOriginal->mPosition.z',z-150)
    put('gpMarioOriginal->mFaceAngle.y',0);put('gpMarioOriginal->mModelFaceAngle',0);put('game.inventory.selected',1)
    state['ready']=True;print('mining-test: palm=%d position=%s'%(i,state['position']),flush=True)
   i=state['tree'];felled=bool(v('trees[%d].felled'%i));progress=float(v('treeBreak.seconds'));active=int(v('treeBreak.target'))
   if 4816<=f<=4820:
    check('mouse hold starts time-based cracks on the trunk',not felled and active==i and .15<progress<.4 and int(v('crackMeshDraws'))>0)
   if 4830<=f<=4834:
    check('mouse release removes cracks and resets progress',not felled and active<0 and progress==0 and int(v('crackMeshDraws'))==0)
   if 4848<=f<=4849:
    check('interrupted second hold starts fresh',not felled and active==i and .1<progress<.2)
   if 4852<=f<=4853:
    check('switching to the sword cancels mining while the button is held',not felled and active<0 and progress==0 and int(v('crackMeshDraws'))==0)
   if 4910<=f<=4914:
    check('new hold progresses continuously from zero',not felled and active==i and .4<progress<.65 and int(v('crackMeshDraws'))>0)
   if 4957<=f<=4960:
    check('cracks reach the final texture before felling',not felled and active==i and 1.26<progress<1.4 and int(v('crackMeshDraws'))>0)
   if 4970<=f<=4974:
    count=sum(bool(v('drops[%d].active'%j)) for j in range(512))
    check('one continuous hold fells the native palm and drops four logs',felled and count==4 and bool(int(v('trees[%d].actor->mLiveFlag'%i))&1))
    check('crack overlay is removed after the tree breaks',active<0 and int(v('crackMeshDraws'))==0)
   if 5040<=f<=5044 and 'pickup-position' not in state['checks']:
    x,y,z=state['position']
    for k,n in [('x',x),('y',y),('z',z)]:put('gpMarioOriginal->mPosition.'+k,n)
    state['checks'].add('pickup-position')
   if f>=5084:
    logs=sum(int(v('game.inventory.slots[%d].count'%j)) for j in range(36) if int(v('game.inventory.slots[%d].item'%j))==4)
    check('four log pickups still collect and persist',logs==4)
    if len(state['checks'])!=10:return fail('missed a runtime assertion: '+str(state['checks']))
    print('mining-test: PASS continuous tree mining and localized Minecraft cracks',flush=True)
    gdb.execute('kill',to_string=True);return True
  except Exception as e:return fail(str(e))
  return False
Mining('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"
