"""Live SDL flow. Fixture positions native camera/player; never bypasses targeting."""
import gdb,os
state={'ready':False,'checks':set(),'focus':None}
def v(s):
 try:return gdb.parse_and_eval(s)
 except Exception as e:raise RuntimeError(s+": "+str(e))
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def field():return int(v("'(anonymous namespace)::g_retrace_count'"))
def check(label,ok):
 if label in state['checks']:return
 if not ok:raise AssertionError(label+' target=%s tree=%s block-progress=%s aim=%s menu=%s'%(v('blockTarget'),v('target'),v('blockBreak.seconds'),v('aimHit'),v('game.menu')))
 state['checks'].add(label);print('polish-test: PASS '+label,flush=True)
def position(x,y,z):
 for k,n in zip(('x','y','z'),(x,y,z)):put('gpMarioOriginal->mPosition.'+k,n)
 put('gpMarioOriginal->mForwardVel',0);put('gpMarioOriginal->mVel.y',0);put('previousValid','false')
def slot(i,item,count=1):put('game.inventory.slots[%d].item'%i,item);put('game.inventory.slots[%d].count'%i,count if item else 0)
class Camera(gdb.Breakpoint):
 def stop(self):
  focus=state['focus']
  if focus:
   x,y,z=focus
   for k,n in zip(('x','y','z'),(x+220,y+170,z-500)):put('camPos->'+k,n)
   for k,n in zip(('x','y','z'),focus):put('target->'+k,n)
  return False
Camera('C_MTXLookAt').condition="gpCamera != 0 && camPos == (Vec*)&gpCamera->unk124 && '(anonymous namespace)::g_retrace_count' >= 4900"
class Polish(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=1 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
   f=field()
   if not state['ready']:
    slot(3,0);slot(4,0);put('game.inventory.selected',2);state['ready']=True
   if 4820<=f<4830:check('inventory key unlocks Taking Inventory once',bool(v('game.achievements & 1')) and int(v('game.menu'))==1)
   if f>=4900 and 'palm' not in state:
    p=v('gpMarioOriginal->mPosition');i=min(range(int(v('treeCount'))),key=lambda i:sum((float(v('trees[%d].actor->mPosition.%s'%(i,k)))-float(p[k]))**2 for k in ('x','z')))
    state['palm']=i;t=v('trees[%d].actor->mPosition'%i);x,y,z=[float(t[k]) for k in ('x','y','z')];state['treepos']=(x,y,z)
    position(x,y,z-150);put('game.inventory.selected',1);state['focus']=(x,y+90,z)
   if 4928<=f<4932:check('camera ray targets palm and held mouse shows localized cracks',int(v('target'))==state['palm'] and float(v('treeBreak.seconds'))>.2 and int(v('crackMeshDraws'))>0)
   if 4950<=f<4954:check('release cancels palm mining',float(v('treeBreak.seconds'))==0 and int(v('treeBreak.target'))==-1)
   if 5066<=f<5070:check('one hold fells palm and spawns four textured drops',bool(v('trees[%d].felled'%state['palm'])) and sum(bool(v('game.drops[%d].active'%i)) for i in range(512))==4)
   if f>=5090 and 'collect' not in state:position(*state['treepos']);state['collect']=True
   if 5110<=f<5114:check('log collection unlocks Getting Wood and preserves four logs',bool(v('game.achievements & 2')) and sum(int(v('game.inventory.slots[%d].count'%i)) for i in range(36) if int(v('game.inventory.slots[%d].item'%i))==4)==4)
   if f>=5120 and 'plaza' not in state:position(6300,300,-3850);put('gpMarioOriginal->mFaceAngle.y',0);put('gpMarioOriginal->mModelFaceAngle',0);state['focus']=(6280,300,-3640);state['plaza']=True
   if 5240<=f<5244:check('live inventory crafting produces sixteen planks',int(v('game.inventory.slots[3].item'))==5 and int(v('game.inventory.slots[3].count'))==16)
   if 5410<=f<5414:check('live table crafting unlocks Benchmaking',int(v('game.achievements'))==7 and int(v('game.inventory.slots[4].item'))==6)
   if f>=5485 and 'table' not in state:
    tables=[i for i in range(256) if int(v('game.cells[%d].item'%i))==6]
    if tables:
     i=tables[0];state['table']=i;state['focus']=(float(v('game.cells[%d].x'%i))*80+40,340,float(v('game.cells[%d].z'%i))*80+40)
   if 5520<=f<5524:check('preview placement commits to camera grid and right-click opens real crafting GUI','table' in state and int(v('game.menu'))==2)
   if f>=5600 and 'chest' not in state:
    i=1;state['chest']=i
    for name,n in [('item',8),('x',79),('y',0),('z',-46),('facing',0)]:put('game.cells[1].'+name,n)
    put('game.cells[1].slots[0].item',5);put('game.cells[1].slots[0].count',37);put('game.cells[1].slots[26].item',3);put('game.cells[1].slots[26].count',1)
    state['focus']=(6360,335,-3640)
   if 5650<=f<5654:check('camera-selected chest opens lid with container GUI',int(v('game.menu'))==3 and float(v('chestLid[1]'))>.9)
   if f>=5725 and 'full' not in state:
    for i in range(3,36):slot(i,4,64)
    state['full']=True
   if 5738<=f<5740:check('placed chest cracks progress during held mining',int(v('blockBreak.target'))==1 and float(v('blockBreak.seconds'))>.09)
   if 5750<=f<5754:check('release cancels placed chest mining without losing contents',float(v('blockBreak.seconds'))==0 and int(v('game.cells[1].slots[0].count'))==37)
   if 5806<=f<5810:check('continuous mining spills full chest despite full inventory',int(v('game.cells[1].item'))==0 and sum(bool(v('game.drops[%d].active'%i)) for i in range(512))==3)
   if f>=5840 and 'chestpickup' not in state:
    for i in range(3,36):slot(i,0,0)
    position(6360,300,-3640);state['chestpickup']=True
   if 5890<=f<5894:check('chest contents and block collect without loss',sum(int(v('game.inventory.slots[%d].count'%i)) for i in range(36) if int(v('game.inventory.slots[%d].item'%i))==5)==37 and sum(bool(v('game.drops[%d].active'%i)) for i in range(512))==0)
   if f>=5930 and 'pose' not in state:put('game.inventory.selected',2);state['pose']=True
   if f>=6030:
    check('achievements do not retrigger on later inventory opens',int(v('game.achievements'))==7 and int(v('achievementQueued'))==0)
    if len(state['checks'])!=14:raise AssertionError('missed checks '+str(state['checks']))
    print('polish-test: PASS native mining, targeting, GUI, live crafting, safe chest spills and all achievements',flush=True);gdb.execute('kill',to_string=True);return True
  except Exception as e:print('polish-test: FAIL '+str(e),flush=True);gdb.execute('kill',to_string=True);return True
  return False
Polish('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"
