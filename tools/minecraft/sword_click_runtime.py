"""Native mouse down+up in one update and an enemy entering a released swing."""
import os
mode=os.environ.get('SMS_SWORD_CLICK_MODE','quick')
source=open('tools/minecraft/sword_runtime.py').read()
if mode=='moving':
 source=source.replace('if f<4945:','if f<4960:').replace('distance=-180 if f<4830 else 400 if f<4860 else 180','distance=-180 if f<4830 else 400 if f<4860 else 300 if 4935<=f<4946 else 180')
exec(source)
# Check real visible feedback at entry of the following update.
class Feedback(gdb.Breakpoint):
 def stop(self):
  f=field()
  if f in (4940,4970) and f not in state.setdefault('taps',set()):
   if bool(v('attackHeld')) or not bool(v('swordSwing.pressed')):return fail('same-pump mouse press/release was not retained')
   state['taps'].add(f);log('PASS quick left-click is latched after press/release in one event pump')
  if state['hits'] and f in (4942,4948,4972):
   if float(v('swordHitFlash'))<=0 or float(v('messageTime'))<=0 or v('message').string()!='SWORD HIT':return fail('native hit feedback was not set')
   log('PASS native sword hit sets the crosshair flash and SWORD HIT feedback')
  if mode=='moving' and f==4944:
   if state['hits']:return fail('enemy outside reach was hit before entering the swing')
   if float(v('swordSwing.seconds'))<=0 or bool(v('attackHeld')):return fail('released swing was not active')
   log('PASS released swing remains active while enemy approaches')
  return False
Feedback('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' == 4940 || '(anonymous namespace)::g_retrace_count' == 4970 || '(anonymous namespace)::g_retrace_count' == 4942 || '(anonymous namespace)::g_retrace_count' == 4944 || '(anonymous namespace)::g_retrace_count' == 4948 || '(anonymous namespace)::g_retrace_count' == 4972"

if mode=='natural':
 class Spawn(gdb.Breakpoint):
  def stop(self):
   if not state['ready'] or state.get('spawned'):return False
   state['spawned']=True;return True
 Spawn('sms_minecraft_update').condition="'(anonymous namespace)::g_retrace_count' >= 4780 && '(anonymous namespace)::g_retrace_count' < 4800"
def spawn_native():
 active=[bp for bp in gdb.breakpoints() if bp.enabled]
 for bp in active:bp.enabled=False
 gdb.execute('set scheduler-locking on',to_string=True)
 # Use the game's actual pool deactivation and respawn methods. Never clear
 # collision/lifecycle flags in the fixture for this regression scenario.
 gdb.execute('call ((TEnemyManager*)$swordEnemy->mManager)->killChildren()',to_string=True)
 gdb.execute('call ((TNameKuri*)$swordEnemy)->reset()',to_string=True)
 flags=int(v('$swordEnemy->mLiveFlag'))
 if not flags&64 or flags&3 or int(v('$swordEnemy->mHitFlags'))&5:raise AssertionError('native kill/reset lifecycle did not reproduce retained historical flag')
 log('PASS native pool kill/reset respawns a hittable Goopy Stu with historical killed flag retained')
 gdb.execute('set scheduler-locking off',to_string=True)
 for bp in active:bp.enabled=True
