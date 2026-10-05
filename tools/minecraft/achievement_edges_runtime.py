"""Check the native toast's grouped HUD anchor in each widescreen mode."""
import gdb, os
from pathlib import Path
checks=set()
edges=os.environ.get('SMS_WIDESCREEN_HUD')=='edges' and os.environ.get('SMS_WIDESCREEN')!='off'
def value(expr):return gdb.parse_and_eval(expr)
def field():return int(value("'(anonymous namespace)::g_retrace_count'"))
def check(label,ok):
 if not ok:raise AssertionError(label)
 if label not in checks:
  checks.add(label);print('achievement-edges-test: PASS '+label,flush=True)
def fail(error):
 print('achievement-edges-test: FAIL '+str(error),flush=True)
 gdb.execute('kill',to_string=True);return True
class Toast(gdb.Breakpoint):
 def stop(self):
  try:
   f=field()
   if f<4800:return False
   kind='unlock' if int(value('achievementQueued')) else 'reminder'
   check(kind+' uses the selected HUD mapping',bool(value("'gx::s_hud'"))==edges)
   check(kind+' panel, icon and text share one right-side anchor',float(value("'gx::s_hudAnchor'"))==(480 if edges else -1))
   if kind=='unlock':check('actual Tab input unlocks Taking Inventory',int(value('game.achievements'))&1 and int(value('game.menu'))==1)
  except Exception as error:return fail(error)
  return False
source=Path('src/minecraft/minecraft.cpp').read_text().splitlines()
line=next(i+1 for i,text in enumerate(source) if 'float x=320,y=-int(d*36)*2' in text)
Toast('minecraft.cpp:'+str(line))
class Done(gdb.Breakpoint):
 def stop(self):
  try:
   if field()<4980:return False
   check('toast leaves following HUD and menu draws centred',not bool(value("'gx::s_hud'")) and float(value("'gx::s_hudAnchor'"))==-1)
   if len(checks)!=6:raise AssertionError('missed reminder or unlock checks: '+str(checks))
   print('achievement-edges-test: PASS reminder and achievement follow widescreen preference',flush=True)
   gdb.execute('kill',to_string=True);return True
  except Exception as error:return fail(error)
Done('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4980"
