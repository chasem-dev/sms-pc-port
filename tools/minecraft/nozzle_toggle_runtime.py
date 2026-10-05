"""Physical V, native nozzle animations, P preview and both inventory axes."""
import gdb
state={'ready':False,'checks':set()}
def v(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def field():return int(v("'(anonymous namespace)::g_retrace_count'"))
def check(label,ok):
 if label in state['checks']:return
 if not ok:raise AssertionError(label+' mouse=(%s,%s) rotation=(%s,%s)'%(v('mouseX'),v('mouseY'),v('root[0][2]'),v('root[1][2]')))
 state['checks'].add(label);print('nozzle-toggle-test: PASS '+label,flush=True)
def mode(label,nozzle,second):
 check(label,int(v('gpMarioOriginal->mWaterGun->mCurrentNozzle'))==nozzle and int(v('gpMarioOriginal->mWaterGun->mSecondNozzle'))==second)
class Look(gdb.Breakpoint):
 def stop(self):
  try:
   f=field()
   if 5600<=f<5605:check('inventory cursor right/down produces right/down tracking',float(v('root[0][2]'))>0 and float(v('root[1][2]'))>0)
   if 5640<=f<5645:check('inventory cursor left/up produces left/up tracking',float(v('root[0][2]'))<0 and float(v('root[1][2]'))<0)
   return False
  except Exception as e:print('nozzle-toggle-test: FAIL '+str(e),flush=True);gdb.execute('kill',to_string=True);return True
Look('src/minecraft/minecraft.cpp:%d'%next(i for i,l in enumerate(open('src/minecraft/minecraft.cpp'),1) if "root[0][3]=cx" in l)).condition="'(anonymous namespace)::g_retrace_count' >= 5560"
class Run(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=1 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
   f=field()
   if not state['ready']:
    for k,n in zip(('x','y','z'),(6300,300,-3850)):put('gpMarioOriginal->mPosition.'+k,n)
    put('gpMarioOriginal->mFaceAngle.y',16384);put('gpMarioOriginal->mModelFaceAngle',16384);put('previousValid','false')
    state['initialPreview']=bool(v('showPlacement'));state['original']=int(v('gpMarioOriginal->mWaterGun->mCurrentNozzle'));state['second']=int(v('gpMarioOriginal->mWaterGun->mSecondNozzle'));state['ready']=True
   if 4875<=f<4880:mode('equipping Hover activates native Hover',4,4)
   if 4904<=f<4906:check('V starts native transition without toggling placement preview',float(v('gpMarioOriginal->mWaterGun->mSwitchToSecondNozzleProgress'))>0 and float(v('gpMarioOriginal->mWaterGun->mSwitchToSecondNozzleProgress'))<1 and float(v('gpMarioOriginal->mWaterGun->mSwitchToSecondNozzleSpeed'))<0 and bool(v('showPlacement'))==state['initialPreview'])
   if 4925<=f<4930:mode('V switches Hover to Spray and remains in Spray',0,4)
   if 4975<=f<4980:mode('next V restores equipped Hover',4,4)
   if 5000<=f<5005:check('P toggles preview without changing nozzle mode',bool(v('showPlacement'))!=state['initialPreview'] and int(v('gpMarioOriginal->mWaterGun->mCurrentNozzle'))==4)
   if 5020<=f<5025:check('second P restores preview',bool(v('showPlacement'))==state['initialPreview'])
   if 5026<=f<5030:state['water']=int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))
   if 5060<=f<5065:check('E activates equipped Hover and consumes native water',int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))<state['water'])
   if 5160<=f<5165:mode('equipping Rocket activates native Rocket',1,1)
   if 5205<=f<5210:mode('V switches Rocket to Spray',0,1)
   if 5245<=f<5250:mode('next V restores equipped Rocket',1,1)
   if 5330<=f<5335:mode('equipping Turbo activates native Turbo',5,5)
   if 5375<=f<5380:mode('V switches Turbo to Spray',0,5)
   if 5415<=f<5420:mode('next V restores equipped Turbo',5,5)
   if 5430<=f<5435:state['turboWater']=int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))
   if 5540<=f<5545:check('E activates equipped Turbo and consumes native water',int(v('gpMarioOriginal->mWaterGun->mCurrentWater'))<state['turboWater'])
   if 5710<=f<5715:check('unequipping armor restores prior native nozzle and animation endpoint',int(v('game.chestArmor.item'))==0 and int(v('gpMarioOriginal->mWaterGun->mCurrentNozzle'))==state['original'] and int(v('gpMarioOriginal->mWaterGun->mSecondNozzle'))==state['second'] and float(v('gpMarioOriginal->mWaterGun->mSwitchToSecondNozzleSpeed'))==0 and float(v('gpMarioOriginal->mWaterGun->mSwitchToSecondNozzleProgress')) in (0,1))
   if 5770<=f<5775:check('V still switches nozzles after armor is removed',int(v('gpMarioOriginal->mWaterGun->mCurrentNozzle'))!=(state['original']))
   if f>=5790:
    if len(state['checks'])!=18:raise AssertionError('missed checks '+str(state['checks']))
    print('nozzle-toggle-test: PASS physical V on all attachments, native animation/water, independent P preview and both mouse axes',flush=True);gdb.execute('kill',to_string=True);return True
  except Exception as e:print('nozzle-toggle-test: FAIL '+str(e),flush=True);gdb.execute('kill',to_string=True);return True
  return False
Run('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"
