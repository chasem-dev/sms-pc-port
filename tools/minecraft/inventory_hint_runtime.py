"""Original persistent reminder, Tab inventory edges and native E spray in-game."""
import gdb,os
state={'ready':False,'checks':set()};reload=bool(os.getenv('SMS_INVENTORY_HINT_RELOAD'))
def v(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def check(label,ok):
 if label in state['checks']:return
 if not ok:raise AssertionError(label)
 state['checks'].add(label);print('inventory-hint-test: PASS '+label,flush=True)
class Camera(gdb.Breakpoint):
 def stop(self):
  if state['ready'] and not reload:
   for k,n in zip(('x','y','z'),(6500,510,-4140)):put('camPos->'+k,n)
   for k,n in zip(('x','y','z'),(6280,340,-3640)):put('target->'+k,n)
  return False
Camera('C_MTXLookAt').condition="gpCamera != 0 && camPos == (Vec*)&gpCamera->unk124 && '(anonymous namespace)::g_retrace_count' >= 4780"
class Hint(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=1 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
   f=int(v("'(anonymous namespace)::g_retrace_count'"))
   if not state['ready']:
    if not reload:
     for k,n in zip(('x','y','z'),(6300,300,-3850)):put('gpMarioOriginal->mPosition.'+k,n)
     put('gpMarioOriginal->mForwardVel',0);put('previousValid','false');put('game.inventory.selected',2)
     put('game.originY',300);put('game.originSet','true')
     for k,n in [('item',6),('x',78),('y',0),('z',-46),('facing',0)]:put('game.cells[0].'+k,n)
    state['ready']=True
   if reload:
    if 4840<=f<4845:check('saved restart suppresses the inventory reminder',bool(v('game.achievements & 1')) and int(v('achievementQueued'))==0 and int(v('game.menu'))==0)
    if 4890<=f<4895:check('Tab reopens inventory without another achievement toast',int(v('game.menu'))==1 and int(v('achievementQueued'))==0)
    if f>=4920:
     print('inventory-hint-test: PASS persisted completion and no repeated reminder/toast',flush=True);gdb.execute('kill',to_string=True);return True
    return False
   if 4880<=f<4885:state['firstWater']=int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))
   if 4950<=f<4955:
    check('physical E sprays while a crafting table is targeted',bool(v("'(anonymous namespace)::g_key'[8]")) and int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))<state['firstWater'] and int(v('blockTarget'))==0)
    check('E spray leaves inventory closed and Taking Inventory unearned',int(v('game.menu'))==0 and not bool(v('game.achievements & 1')))
   if 5000<=f<5005:
    check('original reminder stays visible before Taking Inventory',not bool(v('game.achievements & 1')) and int(v('achievementQueued'))==0 and float(v('inventoryHintTime'))>=.375 and int(v('game.menu'))==0)
    check('spray conflict fixture really targets a crafting table',int(v('blockTarget'))==0)
    state['water']=int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))
   if 5070<=f<5075:check('reminder remains indefinitely instead of expiring',not bool(v('game.achievements & 1')) and float(v('inventoryHintTime'))>=1.5)
   if 5140<=f<5145:
    check('held Tab opens inventory and replaces reminder with Taking Inventory',int(v('game.menu'))==1 and int(v('achievementQueued'))==1 and int(v('achievementQueue[0]'))==0 and bool(v('game.achievements & 1')))
    check('Tab is consumed and default keyboard FLUDD stays E',not bool(v("'(anonymous namespace)::g_key'[43]")) and int(v("'(anonymous namespace)::g_bind'[6][0]"))==8 and int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))==state['water'])
   if 5170<=f<5175:check('Tab key release leaves inventory open',int(v('game.menu'))==1)
   if 5200<=f<5205:check('next Tab press closes inventory',int(v('game.menu'))==0)
   if 5330<=f<5335:check('unlock toast expires and original reminder stays dismissed',int(v('achievementQueued'))==0 and bool(v('game.achievements & 1')))
   if 5360<=f<5365:state['sprayWater']=int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))
   if 5400<=f<5405:check('physical E still sprays native FLUDD after unlocking',bool(v("'(anonymous namespace)::g_key'[8]")) and int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))<state['sprayWater'])
   if 5440<=f<5445:check('Tab reopening does not repeat the achievement',int(v('game.menu'))==1 and int(v('achievementQueued'))==0)
   if f>=5460:
    if len(state['checks'])!=12:raise AssertionError('missed checks '+str(state['checks']))
    print('inventory-hint-test: PASS original reminder lifecycle, Tab inventory, native E spray and one-time popup',flush=True);gdb.execute('kill',to_string=True);return True
  except Exception as e:print('inventory-hint-test: FAIL '+str(e),flush=True);gdb.execute('kill',to_string=True);return True
  return False
Hint('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"
