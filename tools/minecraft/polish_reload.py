import gdb
def v(s):return gdb.parse_and_eval(s)
class Reload(gdb.Breakpoint):
 def stop(self):
  if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=1 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
  f=int(v("'(anonymous namespace)::g_retrace_count'"))
  if f>=4840:
   try:
    assert int(v('game.achievements'))==7
    assert int(v('game.menu'))==1 and int(v('achievementQueued'))==0
    assert sum(int(v('game.inventory.slots[%d].count'%i)) for i in range(36) if int(v('game.inventory.slots[%d].item'%i))==5)==37
    assert int(v('game.cells[0].item'))==6 and int(v('game.cells[1].item'))==0
    print('polish-test: PASS cold restart restores world/items and suppresses already earned achievement popups',flush=True)
   except Exception as e:print('polish-test: FAIL reload '+str(e),flush=True)
   gdb.execute('kill',to_string=True);return True
  return False
Reload('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780"
