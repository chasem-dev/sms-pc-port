#!/usr/bin/env python3
"""Record the native chest model, icon and live item storage."""
import argparse
from pathlib import Path
import record_demo
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--disc',required=True)
p.add_argument('--executable',default=str(ROOT/'build/linux-64/sms'))
p.add_argument('--output',required=True)
p.add_argument('--lid-preview',action='store_true',help='Hide only the storage overlay in the debugger to expose the native lid animation')
a=p.parse_args();a.scene='chest'
def scenario(_):
 script=(ROOT/'tools/minecraft/chest_runtime.py').read_text()
 inputs=record_demo.BOOT+',MOUSE_RIGHT_320_240@4800+2,MOUSE_RIGHT_320_240@4860+2,KEY_LSHIFT@4900+8,MOUSE_LEFT_392_366@4902+2,KEY_TAB@5000+8,MOUSE_RIGHT_320_240@5010+2,KEY_E@5020+2,KEY_TAB@5060+8,KEY_TAB@5140+8'
 return 1,4780,5320,script,inputs,{'SMS_CHEST_LID_PREVIEW':'1'} if a.lid_preview else {}
record_demo.scenario=scenario
record_demo.record(a)
