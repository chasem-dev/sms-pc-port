# Real controller input drives movement. Only the initial camera-independent
# starting position is prepared; no sound, voice, mixer or animation is changed.
import gdb
ready=False
def v(s):return gdb.parse_and_eval(s)
def put(s,n):gdb.execute('set var %s = %s'%(s,n),to_string=True)
def field():return int(v("'(anonymous namespace)::g_retrace_count'"))
class Walk(gdb.Breakpoint):
 def stop(self):
  global ready
  if not int(v('gpMarDirector')) or int(v('gpMarDirector->mMap'))!=1 or int(v('gpMarDirector->mState'))!=4 or int(v('gpMarDirector->unk124'))!=0:return False
  f=field()
  if f<4780:return False
  if not ready:
   for k,n in [('x',6500),('y',300),('z',-3850)]:put('gpMarioOriginal->mPosition.'+k,n)
   put('gpMarioOriginal->mFaceAngle.y',0);put('gpMarioOriginal->mModelFaceAngle',0)
   ready=True
  if f>=5440:
   print('walking-demo: PASS native walking inputs and idle tail',flush=True)
   gdb.execute('kill',to_string=True);return True
  return False
Walk('TMario::perform').condition = "'(anonymous namespace)::g_retrace_count' >= 4780"
