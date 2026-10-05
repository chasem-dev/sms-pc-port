#!/usr/bin/env python3
"""Record continuous tree mining with real mouse presses and native game audio."""
import argparse
from pathlib import Path
import record_demo
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--disc',required=True)
p.add_argument('--arch',choices=['32','64'],default='64')
p.add_argument('--fps',choices=['30','60'],default='60')
p.add_argument('--output',required=True)
a=p.parse_args();a.scene='mining';a.executable=str(ROOT/f'build/linux-{a.arch}/sms')
def scenario(_):
 script=(ROOT/'tools/minecraft/mining_runtime.py').read_text()
 inputs=record_demo.BOOT+',MOUSE_LEFT_320_240@4800+22,MOUSE_LEFT_320_240@4840+14,KEY_3@4850,KEY_2@4870,MOUSE_LEFT_320_240@4880+110'
 return 1,4780,5082,script,inputs,{'SMS_FRAME_RATE':a.fps}
record_demo.scenario=scenario
record_demo.record(a)
