# Native box drop, real collision pickup, live inventory armor clicks and reload.
import gdb,os
state={'ready':False,'checks':set()}
reload=bool(os.getenv('SMS_NOZZLE_RELOAD'))
def val(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def check(name,test):
 if name in state['checks']:return
 if not (bool(val(test)) if isinstance(test,str) else test):raise AssertionError(name)
 state['checks'].add(name);print('nozzle-test: PASS '+name,flush=True)
def position(x,y,z):
 for k,n in [('x',x),('y',y),('z',z)]:put('gpMarioOriginal->mPosition.'+k,n)
 put('gpMarioOriginal->mForwardVel',0);put('previousValid','false')
class Nozzles(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(val('gpMarDirector')) or int(val('gpMarDirector->mMap'))!=1 or int(val('gpMarDirector->mState'))!=4 or int(val('gpMarDirector->unk124'))!=0:return False
   f=int(val("'(anonymous namespace)::g_retrace_count'"))
   if reload:
    if f>=4840:
     check('cold restart restores equipped Turbo and native FLUDD','game.chestArmor.item == 10 && gpMarioOriginal->mWaterGun->mCurrentNozzle == 5')
     print('nozzle-test: PASS cold process restart',flush=True);gdb.execute('kill',to_string=True);return True
    return False
   if not state['ready']:
    for i in range(int(val('gpItemManager->mObjNum'))):
     if int(val('gpItemManager->unk18[%d]->mActorType'%i))==0x20000068:
      box=int(val('gpItemManager->unk18[%d]'%i));break
    else:raise AssertionError('native nozzle box missing')
    state['box']='((TNozzleBox*)%d)'%box
    b=state['box'];state['pickup']='((TItemNozzle*)%d)'%int(val(b+'->mContainedNozzleItem'))
    # Move the native box to a clear plaza floor for a reproducible stomp.
    for field in ['mPosition','mInitialPosition']:
     for k,n in [('x',6500),('y',300),('z',-3650)]:put(b+'->'+field+'.'+k,n)
    put(b+'->unk15C','true');put(b+'->mLiveFlag',0);put(b+'->mHitFlags',int(val(b+'->mHitFlags'))&~5)
    # Preserve the normal Hover / axe / diamond-sword hotbar while filling
    # the remaining slots for the full-inventory pickup check.
    for i in range(36):
     put('game.inventory.slots[%d].item'%i,(1,2,3)[i] if i<3 else 2)
     put('game.inventory.slots[%d].count'%i,1)
    put('game.inventory.selected',1)
    put('gpMarioOriginal->mFreezeTimer',0)
    position(6500,520,-3650);put('gpMarioOriginal->mStatus',0x2000880);put('gpMarioOriginal->mVel.y',-8)
    state['original']=int(val('gpMarioOriginal->mWaterGun->mCurrentNozzle'));state['ready']=True
   pickup=state['pickup'];b=state['box']
   if 4840<=f<4850:
    check('stomping native box releases its attachment',int(val(pickup+'->mState'))!=0 and not (int(val(pickup+'->mLiveFlag'))&1))
   if 4880<=f<4950:
    position(float(val(pickup+'->mPosition.x')),300,float(val(pickup+'->mPosition.z')))
   if 4920<=f<4930:
    check('full inventory leaves the native drop collectible',not (int(val(pickup+'->mLiveFlag'))&1) and all(int(val('game.inventory.slots[%d].item'%i))!=9 for i in range(36)))
   if f>=4940 and 'space' not in state:
    put('game.inventory.slots[3].item',0);put('game.inventory.slots[3].count',0);state['space']=True
   if 4960<=f<4970:
    check('native collision pickup creates one Rocket item','game.inventory.slots[3].item == 9 && game.inventory.slots[3].count == 1')
    check('pickup does not equip or hold the nozzle',int(val('gpMarioOriginal->mWaterGun->mCurrentNozzle'))==state['original'] and int(val('gpMarioOriginal->mHeldObject'))==0)
   if f>=4980 and 'ui' not in state:
    position(6300,300,-3850);put('gpMarioOriginal->mFaceAngle.y',16384);put('gpMarioOriginal->mModelFaceAngle',16384)
    put('game.inventory.slots[4].item',10);put('game.inventory.slots[4].count',1)
    for i in range(5,36):put('game.inventory.slots[%d].item'%i,0);put('game.inventory.slots[%d].count'%i,0)
    state['ui']=True
   if 5050<=f<5060:check('dragging Rocket into chest armor forces native Rocket','game.chestArmor.item == 9 && game.cursor.item == 0 && gpMarioOriginal->mWaterGun->mCurrentNozzle == 1')
   if 5110<=f<5120:check('tool is rejected without losing cursor or armor','game.chestArmor.item == 9 && game.cursor.item == 2')
   if 5170<=f<5180:check('Shift click swaps Hover with equipped Rocket and preserves diamond sword','game.chestArmor.item == 1 && game.inventory.slots[0].item == 9 && game.inventory.slots[2].item == 3 && game.inventory.slots[2].count == 1 && gpMarioOriginal->mWaterGun->mCurrentNozzle == 4')
   if 5210<=f<5220:check('physical V switches equipped Hover to native Spray','gpMarioOriginal->mWaterGun->mCurrentNozzle == 0 && gpMarioOriginal->mWaterGun->mSecondNozzle == 4')
   if 5230<=f<5238:state['water']=int(val('gpMarioOriginal->mWaterGun->mCurrentWater'))
   if 5290<=f<5300:check('equipped Hover uses native water emission',int(val('gpMarioOriginal->mWaterGun->mCurrentWater'))<state['water'])
   if 5350<=f<5360:check('Shift removing armor restores previous native nozzle',int(val('game.chestArmor.item'))==0 and int(val('gpMarioOriginal->mWaterGun->mCurrentNozzle'))==state['original'])
   if 5390<=f<5400:check('Shift equipping Turbo forces native Turbo','game.chestArmor.item == 10 && gpMarioOriginal->mWaterGun->mCurrentNozzle == 5')
   if 5420<=f<5430:state['turboWater']=int(val('gpMarioOriginal->mWaterGun->mCurrentWater'))
   if f>=5440 and float(val('gpMarioOriginal->mDashSpeed'))>1:state['turboBoost']=True
   if 5540<=f<5550:check('equipped Turbo uses native boost and water',int(val('gpMarioOriginal->mWaterGun->mCurrentWater'))<state['turboWater'] and state.get('turboBoost',False))
   if f>=5560:
    if len(state['checks'])!=12:raise AssertionError('missed live checks '+str(state['checks']))
    print('nozzle-test: PASS native box drop, full inventory, real pickup, live armor clicks and all attachments',flush=True);gdb.execute('kill',to_string=True);return True
  except Exception as e:
   print('nozzle-test: FAIL '+str(e),flush=True);gdb.execute('kill',to_string=True);return True
  return False
Nozzles('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"
