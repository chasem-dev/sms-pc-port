#!/usr/bin/env python3
"""Replay real controller boarding and the open-water Blooper crossings.

Runs against a private copy of the player's card. Saves actual displayed XFB
frames, including retained/dissolved frames, and checks arrival/ride state.
"""
import argparse,os,re,shutil,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BOOT='start@1400+20,a@3000+30,stick_left@3300+40,a@3500+30,stick_right@3700+60,a@3900+30'
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--iso',type=Path,required=True);p.add_argument('--seed-card',type=Path,required=True)
 p.add_argument('--exe',type=Path,default=ROOT/'build/linux-64/sms');p.add_argument('--fps',type=int,choices=[30,60],default=30)
 p.add_argument('--player-settings',action='store_true',help='use saved resolution and texture settings instead of the lightweight capture overrides')
 p.add_argument('--rides',type=int,default=2);p.add_argument('--from-stage',type=int,default=1)
 p.add_argument('--controls',action='store_true');p.add_argument('--turn-back',action='store_true');
 p.add_argument('--turn-back-after-swap',action='store_true',help='reverse after entering the neighboring map, then return home')
 p.add_argument('--enter-park',action='store_true',help='walk through the native park gate after one Plaza ferry ride')
 p.add_argument('--record',action='store_true');p.add_argument('--audio',action='store_true');p.add_argument('--out',type=Path,required=True)
 p.add_argument('--timeout',type=float,default=420);a=p.parse_args();out=a.out;out.mkdir(parents=True,exist_ok=False)
 turn_back=a.turn_back or a.turn_back_after_swap
 if a.turn_back and a.turn_back_after_swap:p.error('choose one turn-back mode')
 if turn_back and a.rides!=1:p.error('turn-back tests require --rides 1')
 if a.enter_park and (a.rides!=1 or a.from_stage!=1 or turn_back):p.error('--enter-park requires --rides 1 --from-stage 1 without a turn-back mode')
 shutil.copytree(a.seed_card,out/'card');(out/'shots').mkdir()
 fields=list(range(3980,8001 if a.enter_park else 6001,2 if a.record else 20))
 env=os.environ.copy();env.pop('SMS_OPEN_WORLD_TEST_SPAWN',None);env.pop('SMS_OPEN_WORLD_TEST_WALK',None)
 env.update(SMS_OPEN_WORLD='1',SMS_SEA_TEST_SPAWN='1',SMS_SEA_TEST_RIDES=str(a.rides),SMS_OPEN_WORLD_LOG='1',SMS_FRAME_RATE=str(a.fps),SMS_GX_SCALE='1',SMS_TEXTURE_PACKS='0',SMS_AUDIO='1' if a.audio else '0',SMS_SKIP_MOVIES='1',SMS_WARP=f'{a.from_stage},{2 if a.from_stage==1 else 0},50',SMS_VI_DETERMINISTIC='1',SMS_FIELD_CLOCK='retrace',SMS_SAVE_DIR=str((out/'card').resolve()),SMS_AUTOPRESS=BOOT,SMS_SHOTS=','.join(map(str,fields)),SMS_SHOT_DIR=str((out/'shots').resolve()))
 if a.player_settings:
  env.pop('SMS_GX_SCALE',None);env.pop('SMS_TEXTURE_PACKS',None)
 if a.controls or turn_back:env['SMS_SEA_TEST_CONTROL']='1'
 if turn_back:env['SMS_SEA_TEST_TURNBACK']='2' if a.turn_back_after_swap else '1'
 if a.enter_park:env['SMS_SEA_TEST_ENTER_PARK']='1'
 with (out/'run.log').open('w') as log:
  proc=subprocess.Popen([str(a.exe.resolve()),str(a.iso.resolve()),'--headless'],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
  try:
   deadline=time.monotonic()+a.timeout
   while proc.poll() is None and time.monotonic()<deadline:
    text=(out/'run.log').read_text(errors='replace')
    if f'captured field {fields[-1]} ' in text and len(re.findall(r'\[sea-route\] landed stage=',text))>=a.rides and (not a.enter_park or '[sea-route] park control proven stage=' in text):break
    time.sleep(.2)
   if proc.poll() is not None:raise RuntimeError(f'early exit {proc.returncode}; inspect {out}/run.log')
   if len(re.findall(r'\[sea-route\] landed stage=',text))<a.rides:raise RuntimeError(f'ride timed out; inspect {out}/run.log')
   if a.enter_park and '[sea-route] park control proven stage=' not in text:raise RuntimeError(f'park entry/control timed out; inspect {out}/run.log')
  finally:
   proc.terminate()
   try:proc.wait(timeout=5)
   except subprocess.TimeoutExpired:proc.kill();proc.wait()
 validate(out,a.rides,a.fps,fields,a.from_stage,a.controls,turn_back,a.record,a.enter_park,a.turn_back_after_swap)
def validate(out,rides,fps,fields,source=1,controls=False,turn_back=False,record=False,enter_park=False,turn_back_after_swap=False):
 text=(out/'run.log').read_text(errors='replace')
 assert 'unknown FIFO opcode' not in text
 assert 'fatal signal' not in text and 'missing resource' not in text and 'ready=0' not in text
 arrivals=re.findall(r'\[sea-route\] arrived stage=(\d+) episode=(\d+) crossing=(\d+) elapsed_ms=(\d+) speed=([\d.]+) -> ([\d.]+) squid_frame=([\d.]+) health=(\d+) water=(\d+)',text)
 expected=2 if turn_back_after_swap else 0 if turn_back else rides
 assert len(arrivals)==expected,(len(arrivals),expected)
 carries=re.findall(r'\[sea-route\] crossing stage=(\d+) -> stage=(\d+) episode=(\d+) progress=([\d.]+) speed=([\d.]+) health=(\d+) water=(\d+)',text)
 for i,(arr,carry) in enumerate(zip(arrivals,carries),1):
  stage,episode,n,ms,old,new,frame,health,water=arr
  assert (stage,episode,n)==(carry[1],carry[2],str(i))
  assert old==new==carry[4] and float(new)>20
  assert health==carry[5] and water==carry[6] and float(frame)>=0
 phases=re.findall(r'\[sea-route\] animation mario=([\d.]+) -> ([\d.]+) blooper=([\d.]+) -> ([\d.]+) hop=([\d.]+) lateral=([\d.-]+)',text)
 assert len(phases)==expected and all(m==n and b==c for m,n,b,c,h,l in phases)
 if controls and expected:assert any(float(a[4])>0 for a in phases)
 lands=re.findall(r'\[sea-route\] landed stage=(\d+) ride=(\d+) xyz=\(([^,]+),([^,]+),([^\)]+)\)',text)
 assert len(lands)==rides and all(float(l[3])>=100 for l in lands)
 if turn_back:assert int(lands[-1][0])==source,'turn-back did not return to departure shore'
 assert text.count('[sea-route] continuous frame released crossing=')==expected
 if controls:
  if expected:assert '[sea-route] hopped progress=' in text
  assert any(abs(float(x))>10 for x in re.findall(r'lateral=([\d.-]+)',text))
 if turn_back:assert '[sea-route] turned back progress=' in text
 assert all((out/'shots'/f'field{n:05d}.ppm').exists() for n in fields)
 from transition import pixels
 ferry_end=fields[-1]
 if enter_park:
  preceding=None
  for line in text.splitlines():
   captured=re.search(r'captured field (\d+) ',line)
   if captured:preceding=int(captured[1])
   if '[sea-route] landed stage=' in line:ferry_end=preceding;break
  entry=re.search(r'entered park stage=(\d+) episode=(\d+).*health=(\d+)',text)
  moved=re.search(r'park control proven stage=(\d+) distance=([\d.]+).*health=(\d+)',text)
  assert entry and moved and entry[1]==moved[1]=='13' and int(entry[3])>0 and int(moved[3])>0
  assert float(moved[2])>120
  assert [int(n) for n in re.findall(r'park walk node=(\d+)',text)]==list(range(5))
  # Normal native stage loads do not print their archive names. The park's
  # stage ID comes from the runtime pinnaParco scenario table, and movement
  # is logged only after the native entrance sequence and fade finish.
  latest=pixels(out/'shots'/f'field{fields[-1]:05d}.ppm')
  assert sum(latest)/len(latest)>50,'park gameplay frame was not visible'
 for path in (out/'shots').glob('*.ppm'):
  # The native beach-to-park gate keeps its original fade. Only the ferry
  # promises no loading/black frames; do not apply that invariant to the gate.
  if int(path.stem[5:])>ferry_end:continue
  data=pixels(path);assert sum(data)/len(data)>45,path
 visual=check_handoffs(out,text) if record and arrivals else ''
 behavior='controller turn-back to departure pier' if turn_back else 'offshore stage swaps, continuous speed/animation/health/water, beach/pier landings'
 result=f'PASS: {fps} fps, {rides} Blooper rides, {behavior}, no black ferry frames.\nTrigger-to-arrival ms: '+', '.join(a[3] for a in arrivals)+'\n'+visual
 ready_times=re.findall(r'continuous frame released crossing=\d+ elapsed_ms=(\d+)',text)
 if ready_times:result+='Trigger-to-frame-ready ms: '+', '.join(ready_times)+'\n'
 if enter_park:result+=f'Native gate entered Pinna Park stage {entry[1]}, episode {entry[2]}; controller movement inside park {moved[2]} units, health {moved[3]}.\n'
 (out/'result.txt').write_text(result);print(result)

def check_handoffs(out,log):
 """Check the first live XFB after each hold, rather than later ride shots."""
 from transition import pixels
 paths=sorted((out/'shots').glob('field*.ppm'))
 last_field=None;results=[]
 for line in log.splitlines():
  capture=re.search(r'captured field (\d+) ',line)
  if capture:last_field=int(capture[1])
  if '[sea-route] continuous frame released crossing=' not in line:continue
  assert last_field is not None
  before_path=out/'shots'/f'field{last_field:05d}.ppm'
  before=pixels(before_path);idx=paths.index(before_path)
  after=next((p for p in paths[idx+1:idx+5] if pixels(p)!=before),None)
  assert after is not None,'display did not resume after releasing the hold'
  live=pixels(after)
  error=sum(abs(x-y) for x,y in zip(before,live))/len(before)
  brightness=abs(sum(before)-sum(live))/len(before)
  assert error<12 and brightness<3,(after,error,brightness)
  # The fixed test camera keeps Mario's red hat/shirt in this central region.
  # Exclude the HUD and shoreline, so a missing actor cannot pass as scenery.
  for path,data in ((before_path,before),(after,live)):
   with path.open('rb') as f:f.readline();w,h=map(int,f.readline().split())
   red=0
   for y in range(int(h*.27),int(h*.65)):
    for x in range(int(w*.375),int(w*.62)):
     i=(y*w+x)*3;r,g,b=data[i:i+3]
     red+=r>90 and r>g*1.35 and r>b*1.15
   assert red>=10,f'Mario missing from first displayed handoff frame: {path}'
  results.append(f'XFB {before_path.name} -> {after.name}: RGB change {error:.2f}/255, brightness change {brightness:.2f}/255; Mario visible.\n')
 return ''.join(results)
if __name__=='__main__':main()
