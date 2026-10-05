#!/usr/bin/env python3
"""Record a repeatable native walking sequence in Mario or Steve mode."""
import argparse
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/minecraft'))
import record_demo

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--disc',required=True)
parser.add_argument('--executable',default=str(ROOT/'build/linux-64/sms'))
parser.add_argument('--minecraft',choices=['0','1'],default='0')
parser.add_argument('--fx',choices=['0','1'],default='1')
parser.add_argument('--output',required=True)
args=parser.parse_args()
args.scene='walking'
def walking(name):
    script=(ROOT/'tools/audio/walking_fixture.py').read_text()
    inputs=record_demo.BOOT+',STICK_DOWN@4840+150,STICK_UP@5050+150,STICK_DOWN@5230+90'
    return 1,4780,5430,script,inputs,{'SMS_MINECRAFT':args.minecraft,'SMS_AUDIO_FX':args.fx,'SMS_AUDIO_TRACE':'1'}
record_demo.scenario=walking
record_demo.record(args)
