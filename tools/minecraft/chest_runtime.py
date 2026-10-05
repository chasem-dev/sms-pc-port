# Native chest preview: live placement, opening, item transfer and closing.
import gdb,os
lidPreview=bool(os.getenv('SMS_CHEST_LID_PREVIEW'))
state={'ready':False,'checks':set()}
def value(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def check(name,expr):
 if name not in state['checks']:
  if not (bool(value(expr)) if isinstance(expr,str) else expr):raise AssertionError(name)
  state['checks'].add(name);print('chest-test: PASS '+name,flush=True)
class ChestPreview(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(value('gpMarDirector')) or int(value('gpMarDirector->mMap'))!=1 or int(value('gpMarDirector->mState'))!=4 or int(value('gpMarDirector->unk124'))!=0:return False
   f=int(value("'(anonymous namespace)::g_retrace_count'"))
   if not state['ready']:
    for k,n in [('x',6300 if lidPreview else 6500),('y',300),('z',-3850)]:put('gpMarioOriginal->mPosition.'+k,n)
    put('gpMarioOriginal->mFaceAngle.y',8192 if lidPreview else 0);put('gpMarioOriginal->mModelFaceAngle',8192 if lidPreview else 0)
    put('game.inventory.slots[5].item',8);put('game.inventory.slots[5].count',4)
    put('game.inventory.slots[6].item',5);put('game.inventory.slots[6].count',12)
    put('game.inventory.selected',5);state['ready']=True
   if 4820<=f<4830:check('placed chest faces its owner','game.cells[0].item == 8 && game.cells[0].facing == %d && game.inventory.slots[5].count == 3'%(1 if lidPreview else 0))
   if 4864<=f<4868:check('lid opens through intermediate angles','chestLid[0] > 0 && chestLid[0] < 1')
   if 4880<=f<4890:check('E opens chest and lid','game.menu == 3 && game.activeChest == 0')
   if 4890<=f<4894:check('lid reaches fully open','chestLid[0] == 1')
   if 4930<=f<4940:check('live Shift click stores planks','game.cells[0].slots[0].item == 5 && game.cells[0].slots[0].count == 12 && game.inventory.slots[6].count == 0')
   if 5004<=f<5008:check('lid closes through intermediate angles','game.menu == 0 && chestLid[0] > 0 && chestLid[0] < 1')
   if 5008<=f<5010 and 'reversal' not in state:state['reversal']=float(value('chestLid[0]'))
   if 5014<=f<5018:check('reopening reverses the moving lid without a snap',int(value('game.menu'))==3 and state['reversal']<float(value('chestLid[0]'))<1)
   if 5050<=f<5058:
    check('closing preserves chest contents','game.menu == 0 && game.cells[0].slots[0].count == 12')
    check('lid settles fully closed','chestLid[0] == 0')
   if 5080<=f<5090:check('chest icon appears in inventory','game.menu == 1 && game.inventory.slots[5].item == 8')
   if 5142<=f<5150 and 'view' not in state:
    put('gpMarioOriginal->mPosition.x',6300)
    put('gpMarioOriginal->mFaceAngle.y',16384);put('gpMarioOriginal->mModelFaceAngle',16384);state['view']=True
   # Rotate the placed model for a visual check of all four saved facings.
   if f>=5160:put('game.cells[0].facing',min(3,(f-5160)//40))
   if 5320<=f<5330 and 'landing' not in state:
    put('gpMarioOriginal->mPosition.x',int(value('game.cells[0].x'))*80+40)
    put('gpMarioOriginal->mPosition.z',int(value('game.cells[0].z'))*80+40)
    put('gpMarioOriginal->mPosition.y',float(value('game.originY'))+100)
    put('gpMarioOriginal->mVel.y',-1);put('previousValid','false');state['landing']=True
   if f>=5400:
    height=float(value('game.originY'))+70
    check('native feet rest on the visible chest lid',abs(float(value('gpMarioOriginal->mPosition.y'))-height)<.1 and abs(float(value('gpMarioOriginal->mFloorPosition.y'))-height)<.1)
    if len(state['checks'])!=11:raise AssertionError('missed live checks')
    print('chest-test: PASS chest renderer preview and native interactions',flush=True)
    gdb.execute('kill',to_string=True);return True
  except Exception as e:
   print('chest-test: FAIL '+str(e),flush=True);gdb.execute('kill',to_string=True);return True
  return False
ChestPreview('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"

# Inspection capture only: retain the real chest-menu state and live inputs,
# but omit its overlay so the world-space hinge and latch can be reviewed.
if lidPreview:
 class LidView(gdb.Breakpoint):
  def stop(self):
   if int(value('game.menu'))==3:gdb.execute('return',to_string=True)
   return False
 LidView('sms_minecraft_hud')
