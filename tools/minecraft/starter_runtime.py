"""Real mouse crafting, starting items, preview tracking and native sound hooks."""
import gdb
state={'ready':False,'checks':set(),'hurt':0}
def v(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def f():return int(v("'(anonymous namespace)::g_retrace_count'"))
def check(label,ok):
 if label in state['checks']:return
 if not ok:raise AssertionError(label)
 state['checks'].add(label);print('starter-test: PASS '+label,flush=True)
class Look(gdb.Breakpoint):
 def stop(self):
  if 4950<=f()<4960:check('right mouse turns Steve right',float(v('lookX'))>0)
  if 4990<=f()<5000:check('left mouse turns Steve left',float(v('lookX'))<0)
  return False
Look('src/minecraft/minecraft.cpp:%d'%next(i for i,l in enumerate(open('src/minecraft/minecraft.cpp'),1) if "MTXRotRad(yaw,'y',lookX" in l)).condition="'(anonymous namespace)::g_retrace_count' >= 4950"
class Run(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=1 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
   frame=f()
   if not state['ready']:
    check('fresh inventory has all three nozzles and approved tools',all(int(v('game.inventory.slots[%d].item'%i))==item and int(v('game.inventory.slots[%d].count'%i))==1 for i,item in enumerate((1,2,3,9,10))))
    put('game.inventory.slots[5].item',5);put('game.inventory.slots[5].count',8);state['ready']=True
   if 4870<=frame<4875:check('real mouse in 2x2 grid shows four-stick recipe',int(v('game.menu'))==1 and int(v('game.craft[0].item'))==5 and int(v('game.craft[2].item'))==5)
   if 4930<=frame<4935:check('result click crafts four sticks and consumes exactly two planks',int(v('game.inventory.slots[6].item'))==11 and int(v('game.inventory.slots[6].count'))==4 and int(v('game.inventory.slots[5].count'))==6 and int(v('game.craft[0].count'))==0 and int(v('game.craft[2].count'))==0)
   if frame>=5060 and 'sounds' not in state:
    state['sounds']=True;return True
   if 5070<=frame<5075:check('hurt overlay remains visible in inventory',float(v('hurtFlash'))>0 and int(v('game.menu'))==1)
   if 5098<=frame<5100:check('hurt overlay expires after half a second',float(v('hurtFlash'))==0)
   if frame>=5100:
    if len(state['checks'])!=14:raise AssertionError('missed checks '+str(state['checks']))
    print('starter-test: PASS native starter items, mouse crafting/tracking and scoped sound replacement',flush=True);gdb.execute('kill',to_string=True);return True
  except Exception as e:print('starter-test: FAIL '+str(e),flush=True);gdb.execute('kill',to_string=True);return True
  return False
Run('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"

def run_sound_checks():
 try:
  breaks=gdb.breakpoints()
  for bp in breaks:bp.enabled=False
  gdb.execute('set scheduler-locking off',to_string=True)
  before=int(v('gpMarioOriginal->mHealth'));serial=int(v("'audio.cpp'::serial"))
  gdb.execute('call gpMarioOriginal->decHP(1)',to_string=True)
  check('one native health loss plays exactly one Minecraft hurt sample',int(v('gpMarioOriginal->mHealth'))==before-1 and int(v("'audio.cpp'::serial"))==serial+1)
  check('native damage activates a half-second red skin overlay',abs(float(v('hurtFlash'))-.5)<.001)
  for id in (0x1001,0x1141,0x4823):
   result=v('MSoundSESystem::MSoundSE::startSoundActorInner(%d, 0, (JAIActor*)-1, 0, 4)'%id)
   check('native effect %04x is suppressed only in Minecraft'%id,int(result)==0)
  check('native damage voice is suppressed',int(v('gpMSound->startMarioVoice(0x7830, 8, 0)'))==0xffffffff)
  put('enabledCache',0)
  gdb.execute('call gpMarioOriginal->decHP(1)',to_string=True)
  check('base game health loss does not emit Minecraft audio',int(v("'audio.cpp'::serial"))==serial+1 and int(v('gpMarioOriginal->mHealth'))==before-2)
  put('enabledCache',1)
  for bp in breaks:bp.enabled=True
 except Exception as e:
  print('starter-test: FAIL '+str(e),flush=True);gdb.execute('kill',to_string=True)
