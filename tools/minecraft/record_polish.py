#!/usr/bin/env python3
"""Record live mining, crafting, GUI polish and original achievements with audio."""
import argparse,subprocess
from pathlib import Path
import record_demo
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--disc',required=True);p.add_argument('--executable',default=str(ROOT/'build/linux-64/sms'));p.add_argument('--output',required=True)
a=p.parse_args();a.scene='polish'
def scenario(_):
 inputs=subprocess.check_output(['python3',str(ROOT/'tools/minecraft/polish_inputs.py')],text=True).strip()
 return 1,4780,5580,(ROOT/'tools/minecraft/polish_runtime.py').read_text(),inputs,{}
record_demo.scenario=scenario
record_demo.record(a)
