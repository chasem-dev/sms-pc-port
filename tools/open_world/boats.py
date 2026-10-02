#!/usr/bin/env python3
"""Observe complete native Plaza boat circuits beside the optional walkway.

Uses a private memory-card copy and a fixed overview camera. The native boat
AI and animation remain unchanged. The probe tests a hull expanded by 100
units against the rendered addition, using the boat's current tilted pose.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
from smoke import BOOT, ROOT

PATTERN = re.compile(r'\[boat-probe\] route=(\S+) ticks=(\d+) checks=(\d+) loops=(\d+) overlaps=(\d+) max_step=([\d.]+)')
ROUTES = {'S_ship0', 'S_ship1', 'S_ship2'}

def observations(text):
    return {m[0]: m for m in PATTERN.findall(text)}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', type=Path, required=True)
    p.add_argument('--seed-card', type=Path, required=True)
    p.add_argument('--exe', type=Path, default=ROOT/'build/linux-64/sms')
    p.add_argument('--fps', type=int, choices=(30, 60), default=30)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--timeout', type=float, default=600)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=False)
    shutil.copytree(a.seed_card, a.out/'card')
    (a.out/'shots').mkdir()
    env = os.environ.copy()
    for key in ('SMS_OPEN_WORLD_TEST_WALK', 'SMS_SEA_TEST_SPAWN', 'SMS_SEA_TEST_RIDES', 'SMS_SEA_TEST_CONTROL'):
        env.pop(key, None)
    env.update(SMS_OPEN_WORLD='1', SMS_OPEN_WORLD_TEST_SPAWN='-300',
        SMS_OPEN_WORLD_TEST_BOATS='1', SMS_FRAME_RATE=str(a.fps),
        SMS_GX_SCALE='1', SMS_TEXTURE_PACKS='0', SMS_AUDIO='0',
        SMS_SKIP_MOVIES='1', SMS_WARP='1,2,50', SMS_VI_DETERMINISTIC='1',
        SMS_FIELD_CLOCK='retrace', SMS_SAVE_DIR=str((a.out/'card').resolve()),
        SMS_AUTOPRESS=BOOT, SMS_SHOTS=','.join(map(str, range(4000, 60001, 500))),
        SMS_SHOT_DIR=str((a.out/'shots').resolve()))
    logpath = a.out/'run.log'
    with logpath.open('w') as log:
        proc = subprocess.Popen([str(a.exe.resolve()), str(a.iso.resolve()), '--headless'],
            cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic()+a.timeout
            while proc.poll() is None and time.monotonic()<deadline:
                text = logpath.read_text(errors='replace')
                found = observations(text)
                # Two wraps ensure a whole circuit after each boat's partial
                # initial circuit, regardless of its starting position.
                if ROUTES <= found.keys() and all(int(found[r][3])>=2 for r in ROUTES):
                    break
                time.sleep(.2)
            if proc.poll() is not None:
                raise RuntimeError(f'game exited {proc.returncode}; inspect {logpath}')
        finally:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill(); proc.wait()
    text = logpath.read_text(errors='replace')
    found = observations(text)
    assert 'fatal signal' not in text and 'unknown FIFO opcode' not in text
    assert ROUTES <= found.keys(), f'missing boats: {ROUTES-found.keys()}'
    lines = []
    for r in sorted(ROUTES):
        _, ticks, checks, loops, overlaps, step = found[r]
        assert int(loops)>=2, f'{r}: complete circuit not observed'
        assert int(overlaps)==0, f'{r}: expanded hull overlaps the addition {overlaps} times'
        assert float(step)*4<100, f'{r}: sampling margin insufficient for measured movement'
        lines.append(f'{r}: {loops} wraps, {checks} hull checks, no overlap; max step {step}.')
    result = f'PASS: native Plaza boats at {a.fps} fps completed full circuits with 100-unit expanded hull clearance.\n'+'\n'.join(lines)+'\n'
    (a.out/'result.txt').write_text(result)
    print(result)

if __name__=='__main__':
    main()
