#!/usr/bin/env python3
"""Drive a real controller replay through the coastal connection (Linux/EGL).

Uses a copy of an existing memory card; never writes to the player's card.
Runs ordinary Mario movement, captures rendered frames, and checks both land
joins, two/eight crossings, episode preservation, state carry and cache use.
"""
import argparse
from datetime import datetime
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
BOOT = "start@1400+20,a@3000+30,stick_left@3300+40,a@3500+30,stick_right@3700+60,a@3900+30"

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, default=ROOT / "build/linux-64/sms")
    p.add_argument("--iso", type=Path, required=True)
    p.add_argument("--seed-card", type=Path, required=True)
    p.add_argument("--fps", choices=(30,60), type=int, default=30)
    p.add_argument("--stress", action="store_true")
    p.add_argument("--record", action="store_true", help="capture the whole first round trip at 15 fps")
    p.add_argument("--audio", action="store_true")
    p.add_argument("--spray", action="store_true", help="spend FLUDD water before the first crossing")
    p.add_argument("--timeout", type=float, default=420, help="maximum replay time in seconds")
    p.add_argument("--out", type=Path)
    a=p.parse_args()
    out=a.out or ROOT / "build/open-world" / datetime.now().strftime("check-%Y%m%d-%H%M%S")
    out.mkdir(parents=True,exist_ok=False)
    shutil.copytree(a.seed_card,out/"card")
    shots=out/"shots";shots.mkdir()
    controls=BOOT
    if a.spray:
        controls+=",r@4200+80"
    fields=[4200,4300,4500,4700,4900,5100,5400,5700,6000,6200,6500]
    expected=8 if a.stress else 2
    if a.stress:
        fields += list(range(7000,16001,1000))
    if a.record:
        fields=sorted(set(fields+list(range(4100,6501,4))))
    # At 60 fps, the default screenshot/input clock counts each displayed
    # frame as two fields. Use actual VI retraces for a shared 30/60 timeline.
    env=os.environ.copy()
    env.update(SMS_OPEN_WORLD="1",SMS_OPEN_WORLD_TEST_SPAWN="-300",
        SMS_OPEN_WORLD_LOG="1",SMS_OPEN_WORLD_TEST_WALK=str(expected),SMS_FRAME_RATE=str(a.fps),SMS_GX_SCALE="1",
        SMS_TEXTURE_PACKS="0",SMS_AUDIO="1" if a.audio else "0",
        SMS_SKIP_MOVIES="1",SMS_WARP="1,2,50",SMS_VI_DETERMINISTIC="1",
        SMS_FIELD_CLOCK="retrace",SMS_SAVE_DIR=str((out/"card").resolve()),
        SMS_AUTOPRESS=controls,SMS_SHOTS=",".join(map(str,fields)),
        SMS_SHOT_DIR=str(shots.resolve()))
    with (out/"run.log").open("w") as log:
        proc=subprocess.Popen([str(a.exe.resolve()),str(a.iso.resolve()),"--headless"],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+a.timeout
        try:
            while proc.poll() is None and time.monotonic()<deadline:
                if (shots/f"field{fields[-1]:05d}.ppm").exists() and f"walk test landed stage=1 crossing={expected}" in (out/"run.log").read_text(errors="replace"):break
                time.sleep(0.2)
            if proc.poll() is not None:
                raise RuntimeError(f"game exited early ({proc.returncode}); inspect {out/'run.log'}")
            if not (shots/f"field{fields[-1]:05d}.ppm").exists():
                raise RuntimeError(f"replay timed out; inspect {out/'run.log'}")
        finally:
            proc.terminate()
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
    validate(out,expected,a.fps,a.spray,fields)

def validate(out,expected,fps,spray,fields):
    shots=out/"shots"
    text=(out/"run.log").read_text(errors="replace")
    assert "fatal signal" not in text and "capacity exceeded" not in text, "runtime failure"
    assert "missing material" not in text, "native bridge texture missing"
    for slot in (3,4,5):
        assert text.count(f"material {slot} =")==1, "shared passage texture was not retained across stages"
    arrivals=re.findall(r"arrived stage=(\d+) episode=(\d+) crossing=(\d+) elapsed_ms=(\d+)",text)
    assert len(arrivals)==expected, f"expected {expected} crossings, got {len(arrivals)}"
    for i,(stage,ep,number,ms) in enumerate(arrivals,1):
        assert (int(stage),int(ep),int(number))==((3,0,i) if i%2 else (1,2,i)), "wrong destination / episode"
    positions=[];crossing=0
    for line in text.splitlines():
        if "arrived stage=" in line:crossing+=1
        m=re.search(r"position stage=(\d+) xyz=\([^,]+,([^,]+),[^)]+\) u=([^ ]+) v=([^ ]+)",line)
        if m:positions.append((crossing,int(m[1]),float(m[2]),float(m[3])))
    assert any(c==1 and s==3 and y>=299 and u < 0 for c,s,y,u in positions), "Harbor land join unproven"
    assert any(c==2 and s==1 and y>=299 and u < 0 for c,s,y,u in positions), "Plaza return land join unproven"
    assert len(re.findall(r"walk test landed stage=",text))==expected, "did not walk through every bend and onto both waterfronts"
    assert text.count("continuous frame released crossing=")==expected, "presentation did not resume"
    speeds=re.findall(r"continuity speed=([\d.]+) -> ([\d.]+)",text)
    assert len(speeds)==expected and all(a==b and float(a)>1 for a,b in speeds), "speed discontinuity"
    carry=re.findall(r"carry health=(\d+) water=(\d+)",text)
    restored=re.findall(r"restored health=(\d+) water=(\d+)",text)
    assert len(carry)==expected and carry==restored, "health / water carry mismatch"
    if spray:
        assert int(carry[0][1]) < 10000, "scripted spray did not deplete water"
    assert "archive cache hit: ricco0.szs" in text and "archive cache hit: dolpic10.szs" in text, "prefetch not used"
    assert all((shots/f"field{n:05d}.ppm").exists() for n in fields), "missing captures"
    summary=f"PASS: {fps} fps, {expected} crossings, both land joins and passage bends, continuous speed, saved episode, health/water carry, archive cache, retained native materials.\nTrigger-to-arrival ms: "+", ".join(x[3] for x in arrivals)+f"\nEvidence: {out.resolve()}\n"
    (out/"result.txt").write_text(summary)
    print(summary)

if __name__=="__main__": main()
