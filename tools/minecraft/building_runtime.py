import gdb,os
state={'ready':False,'checks':set(),'clean':False,'bright':False,'physics':0}
def value(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def field():return int(value("'(anonymous namespace)::g_retrace_count'"))
def log(s):print('building-test: '+s,flush=True)
def stop(message):log('FAIL '+message);gdb.execute('kill',to_string=True);return True
class Run(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(value('gpMarDirector')) or int(value('gpMarDirector->mMap'))!=1 or int(value('gpMarDirector->mState'))!=4 or int(value('gpMarDirector->unk124'))!=0:return False
   f=field()
   if f<4780:return False
   if not state['ready']:
    state['ready']=True
    put('gpMarioOriginal->mPosition.x',6500);put('gpMarioOriginal->mPosition.y',300);put('gpMarioOriginal->mPosition.z',-3850)
    put('gpMarioOriginal->mFaceAngle.y',0);put('gpMarioOriginal->mModelFaceAngle',0)
    if os.getenv('SMS_BUILDING_RELOAD'):
     if int(value('game.cells[1].slots[0].count'))!=7 or int(value('game.cells[1].slots[1].item'))!=3:return stop('saved chest contents missing on cold restart')
     if int(value('game.cells[0].item'))!=6 or int(value('game.cells[2].item'))!=7:return stop('placed table/door missing on restart')
     log('PASS cold process restart restored placed table, door, planks, chest and chest contents')
    else:
     put('game.inventory.slots[4].item',0);put('game.inventory.slots[4].count',0)
     put('game.inventory.slots[3].item',4);put('game.inventory.slots[3].count',8)
     log('fixture supplied eight logs; all crafting and transfers use live mouse events')
   if os.getenv('SMS_BUILDING_RELOAD'):
    if f>=4860:gdb.execute('kill',to_string=True);return True
    return False
   if 5620<=f<5780:put('gpMarioOriginal->mFaceAngle.y',16384);put('gpMarioOriginal->mModelFaceAngle',16384)
   elif 5780<=f<5860:put('gpMarioOriginal->mFaceAngle.y',-16384);put('gpMarioOriginal->mModelFaceAngle',-16384)
   elif 5860<=f<5910:put('gpMarioOriginal->mFaceAngle.y',0);put('gpMarioOriginal->mModelFaceAngle',0)
   elif 5910<=f<5960:put('gpMarioOriginal->mFaceAngle.y',-32768);put('gpMarioOriginal->mModelFaceAngle',-32768)
   checks=[
    (4890,4915,'planks',"game.inventory.slots[3].count == 32 && game.craft[0].count == 0"),
    (5070,5080,'table crafted',"game.inventory.slots[4].item == 6 && game.inventory.slots[3].count == 28"),
    (5130,5139,'table placed',"game.cells[0].item == 6 && game.inventory.slots[4].count == 0"),
    (5150,5159,'E opens 3x3 crafting',"game.menu == 2"),
    (5350,5359,'door recipe',"game.inventory.slots[4].item == 7 && game.inventory.slots[4].count == 3"),
    (5590,5599,'chest recipe',"game.inventory.slots[5].item == 8 && game.inventory.slots[3].count == 14"),
    (5670,5679,'E opens chest',"game.menu == 3 && game.cells[1].item == 8"),
    (5750,5759,'chest deposits',"game.cells[1].slots[0].count == 7 && game.cells[1].slots[1].item == 3 && game.inventory.slots[2].count == 0"),
    (5810,5819,'door placed',"game.cells[2].item == 7 && game.inventory.slots[4].count == 2"),
    (5830,5839,'door opens',"game.cells[2].open"),
    (5850,5859,'door closes',"!game.cells[2].open"),
    (5890,5909,'stacked grid block',"game.cells[3].item == 5 && game.cells[3].y == 1"),
    (5930,5959,'ground grid block',"game.cells[4].item == 5 && game.cells[4].y == 0")]
   for lo,hi,name,expr in checks:
    if lo<=f<=hi and name not in state['checks']:
     if not bool(value(expr)):return stop(name+' / '+str(value('game.menu'))+' / '+str(value('game.cursor')))
     state['checks'].add(name);log('PASS '+name)
   if 6000<=f<6010 and state['physics']==0:
    put('gpMarioOriginal->mPosition.x',6500);put('gpMarioOriginal->mPosition.y',490);put('gpMarioOriginal->mPosition.z',-3640)
    put('gpMarioOriginal->mVel.y',-1);put('previousValid','false');state['physics']=1
   if 6100<=f<6120 and state['physics']==1:
    if abs(float(value('gpMarioOriginal->mPosition.y'))-460)>.1:return stop('did not stand on stacked block')
    if abs(float(value('gpMarioOriginal->mFloorPosition.y'))-460)>.1:return stop('native controller did not recognize block floor')
    log('PASS native ground support on stacked blocks');state['physics']=2
   if 6150<=f<6170 and state['physics']==2:
    if float(value('gpMarioOriginal->mPosition.y'))<475:return stop('cannot jump off placed block')
    log('PASS jumping from placed block');state['physics']=3
   if 6250<=f<6260 and state['physics']==3:
    put('gpMarioOriginal->mPosition.x',6250);put('gpMarioOriginal->mPosition.y',300);put('gpMarioOriginal->mPosition.z',-3880)
    put('gpMarioOriginal->mVel.y',0);put('gpMarioOriginal->mFaceAngle.y',16384);put('gpMarioOriginal->mModelFaceAngle',16384)
    put('previousPosition.x',6250);put('previousPosition.y',300);put('previousPosition.z',-3880);put('previousValid','true')
    state['physics']=4
   if 6260<=f<6270 and state['physics']==4:
    put('gpMarioOriginal->mPosition.x',6325);state['physics']=5
   if 6270<=f<6280 and state['physics']==5:
    if float(value('gpMarioOriginal->mPosition.x'))>6302.1:return stop('closed door did not block movement')
    log('PASS closed door blocks player');put('gpMarioOriginal->mFaceAngle.y',16384);put('gpMarioOriginal->mModelFaceAngle',16384);state['physics']=6
   if 6290<=f<6300 and state['physics']==6:
    if not bool(value('game.cells[2].open')):return stop('E did not reopen door')
    put('gpMarioOriginal->mPosition.x',6325);state['physics']=7
   if 6300<=f<6310 and state['physics']==7:
    if abs(float(value('gpMarioOriginal->mPosition.x'))-6325)>.1:return stop('open doorway obstructs passage')
    log('PASS open doorway allows passage');state['physics']=8
   if f>=6320:
    if len(state['checks'])!=len(checks) or state['physics']!=8:return stop('missed live scenario checks')
    if bool(value('dirty')):return stop('world changes not saved')
    if not state['clean'] or not state['bright']:return stop('clean/bright plaza flag checks missed')
    log('PASS live clicks, splitting, 2x2 and 3x3 crafting, grid placement, E interaction, door toggles and chest storage')
    gdb.execute('kill',to_string=True);return True
  except Exception as e:return stop(str(e))
  return False
Run('sms_minecraft_update')

class Monument(gdb.Breakpoint):
 def stop(self):
  if state['clean']:return False
  try:
   obj=value('this')
   if int(obj['unk13C'])!=0 or int(obj['unk138']['a'])!=0 or not int(obj['unk149']):return stop('monument still dirty')
   if int(value('TFlagManager::smInstance->mCardBools[12]'))&8:return stop('hack changed collected gate Shine flag')
   state['clean']=True;log('PASS clean monument with collected gate Shine flag unchanged')
  except Exception as e:return stop(str(e))
  return False
class Tint(gdb.Breakpoint):
 def stop(self):
  if state['bright']:return False
  try:
   if not int(value('gpMarDirector')) or int(value('gpMarDirector->mMap'))!=1:return False
   if int(value('this->unk14.a'))!=0:return stop('plaza darkness overlay remains')
   state['bright']=True;log('PASS plaza darkness overlay disabled')
  except Exception as e:return stop(str(e))
  return False
Monument('TMonumentShine::control')
Tint('TSunGlass::perform')
