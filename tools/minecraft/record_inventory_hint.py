#!/usr/bin/env python3
"""Record the original inventory reminder, Tab achievement transition and FLUDD."""
import argparse
from pathlib import Path
import record_demo
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--disc',required=True);p.add_argument('--executable',default=str(ROOT/'build/linux-64/sms'));p.add_argument('--output',required=True)
a=p.parse_args();a.scene='inventory-reminder'
def scenario(_):
 inputs=record_demo.BOOT+',KEY_E@4900+60,KEY_TAB@5100+60,KEY_TAB@5180,KEY_E@5350+60,KEY_TAB@5420,KEY_TAB@5450'
 return 1,4780,5458,(ROOT/'tools/minecraft/inventory_hint_runtime.py').read_text(),inputs,{}
record_demo.scenario=scenario
record_demo.record(a)
