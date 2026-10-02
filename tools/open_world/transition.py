#!/usr/bin/env python3
"""Capture consecutive rendered frames through a coastal handoff.

Checks the real displayed XFB (including held frames), speed carry, absence
of black frames, and visual continuity when the destination first appears.
"""
import argparse
from datetime import datetime
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
BOOT='start@1400+20,a@3000+30,stick_left@3300+40,a@3500+30,stick_right@3700+60,a@3900+30'

def pixels(path):
    with path.open('rb') as f:
        assert f.readline()==b'P6\n', path
        w,h=map(int,f.readline().split())
        assert f.readline()==b'255\n', path
        data=f.read()
        assert len(data)==w*h*3, path
        return data

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso',type=Path,required=True)
    p.add_argument('--seed-card',type=Path,required=True)
    p.add_argument('--exe',type=Path,default=ROOT/'build/linux-64/sms')
    p.add_argument('--from-stage',choices=(1,3),type=int,default=1)
    p.add_argument('--fps',choices=(30,60),type=int,default=30)
    p.add_argument('--jump',action='store_true')
    p.add_argument('--out',type=Path)
    p.add_argument('--timeout',type=float,default=240)
    a=p.parse_args()
    out=a.out or ROOT/'build/open-world'/datetime.now().strftime('transition-%Y%m%d-%H%M%S')
    out.mkdir(parents=True,exist_ok=False)
    shutil.copytree(a.seed_card,out/'card');(out/'shots').mkdir()
    fields=list(range(4480,4681,2 if a.fps==30 else 1))
    controls=BOOT+',stick_up@4500+180'
    if a.jump:controls+=',a@4516+4'
    env=os.environ.copy()
    env.pop('SMS_OPEN_WORLD_TEST_WALK',None)
    env.update(SMS_OPEN_WORLD='1',SMS_OPEN_WORLD_TEST_SPAWN='2766.0,-1314.1',
        SMS_OPEN_WORLD_LOG='1',SMS_FRAME_RATE=str(a.fps),SMS_GX_SCALE='1',
        SMS_TEXTURE_PACKS='0',SMS_AUDIO='0',SMS_SKIP_MOVIES='1',
        SMS_WARP=f'{a.from_stage},{2 if a.from_stage==1 else 0},50',
        SMS_VI_DETERMINISTIC='1',SMS_FIELD_CLOCK='retrace',
        SMS_SAVE_DIR=str((out/'card').resolve()),SMS_AUTOPRESS=controls,
        SMS_SHOTS=','.join(map(str,fields)),SMS_SHOT_DIR=str((out/'shots').resolve()))
    with (out/'run.log').open('w') as log:
        proc=subprocess.Popen([str(a.exe.resolve()),str(a.iso.resolve()),'--headless'],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:
            deadline=time.monotonic()+a.timeout
            while proc.poll() is None and time.monotonic()<deadline and 'captured field 4680 ' not in (out/'run.log').read_text(errors='replace'):time.sleep(.2)
            if 'captured field 4680 ' not in (out/'run.log').read_text(errors='replace'):
                raise RuntimeError(f"capture did not finish; inspect {out/'run.log'}")
        finally:
            proc.terminate()
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
    validate(out,a.from_stage,a.fps,a.jump,fields)

def validate(out,stage,fps,jump,fields):
    text=(out/'run.log').read_text(errors='replace')
    assert 'fatal signal' not in text and 'capacity exceeded' not in text
    assert text.count('arrived stage=')==1, 'expected one actual crossing'
    assert 'continuous frame released crossing=1' in text
    speed=re.search(r'continuity speed=([\d.]+) -> ([\d.]+).*status=([0-9a-f]+)',text)
    assert speed and speed[1]==speed[2] and float(speed[1])>1
    if jump:assert int(speed[3],16)&0x800, 'jump was not airborne at the handoff'
    previous=None;run=[];longest=[]
    paths=sorted((out/'shots').glob('field*.ppm'))
    assert len(paths)>=len(fields)-12, 'too many missing display frames'
    for path in paths:
        data=pixels(path)
        assert sum(data)/len(data)>50, f'black/loading frame: {path}'
        if data==previous:run.append(path)
        else:run=[path]
        if len(run)>len(longest):longest=run[:]
        previous=data
    assert len(longest)>=2, 'no retained-frame interval found'
    idx=paths.index(longest[-1]);after=paths[idx+1]
    before=pixels(longest[-1]);after_data=pixels(after)
    error=sum(abs(x-y) for x,y in zip(before,after_data))/len(before)
    brightness=abs(sum(before)-sum(after_data))/len(before)
    # Detailed paving (and jumping) moves many more pixels than flat colors.
    # The held frame precedes the trigger; compare the resumed image against
    # a small number of adjacent live motion steps, with a hard upper bound.
    adjacent=[pixels(p) for p in paths[idx+1:idx+5]]
    motion=sorted(sum(abs(x-y) for x,y in zip(a,b))/len(a) for a,b in zip(adjacent,adjacent[1:]))
    limit=min(35,max(8,3*motion[len(motion)//2]))
    assert error<limit, f'visible handoff discontinuity: mean RGB error {error:.2f}, limit {limit:.2f}'
    assert brightness<3, f'lighting pop: mean brightness change {brightness:.2f}'
    result=(f'PASS: stage {stage}, {fps} fps, '+('airborne' if jump else 'walking')+
        f', continuous speed, no black frames; first destination frame mean RGB change {error:.2f}/255, brightness change {brightness:.2f}/255.\n'
        f'Retained frames: {longest[0].name} through {longest[-1].name}; destination: {after.name}.\n')
    (out/'result.txt').write_text(result);print(result)

if __name__=='__main__':main()
