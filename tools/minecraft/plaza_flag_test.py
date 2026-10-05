"""Run in gdb with Minecraft disabled to check the independent lighting flag."""
import gdb,os
expected=os.environ['SMS_CLEAN_SHINE_GATE']=='1'
seen=set()
def read(s):return gdb.parse_and_eval(s)
def fail(s):
 print('plaza-test: FAIL '+s,flush=True);gdb.execute('kill',to_string=True);return True
class Monument(gdb.Breakpoint):
 def stop(self):
  if 'monument' in seen:return False
  try:
   clean=int(read('this->unk13C'))==0 and int(read('this->unk138.a'))==0 and bool(read('this->unk149'))
   if clean!=expected:return fail('wrong monument state')
   if int(read('TFlagManager::smInstance->mCardBools[12]'))&8:return fail('collected gate Shine changed')
   seen.add('monument')
  except Exception as e:return fail(str(e))
  return False
class Tint(gdb.Breakpoint):
 def stop(self):
  try:
   if not int(read('gpMarDirector')) or int(read('gpMarDirector->mMap'))!=1:return False
   if (int(read('this->unk14.a'))==0)!=expected:return fail('wrong lighting state')
   seen.add('tint')
   if len(seen)==2 and int(read("'(anonymous namespace)::g_retrace_count'"))>=4880:
    print('plaza-test: PASS Minecraft disabled; flag '+('on cleans and brightens' if expected else 'off retains dirty gate and progression lighting')+'; collected gate Shine unchanged',flush=True)
    gdb.execute('kill',to_string=True);return True
  except Exception as e:return fail(str(e))
  return False
Monument('TMonumentShine::control')
Tint('TSunGlass::perform')
