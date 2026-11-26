"""Check the startup CLI with no disc or display. Usage: python3 startup_flags.py EXE"""
from pathlib import Path
import re
import subprocess
import sys

exe = Path(sys.argv[1]).resolve()
settings = Path(__file__).resolve().parents[2] / 'settings.txt'
original = settings.read_bytes()
flags = subprocess.check_output([exe, '--help'], text=True)
names = set(re.findall(r'^#?\s*([a-z][a-z_0-9]*)\s*=', original.decode(), re.M))
assert names and all(f'--{name}=value' in flags for name in names), names
for arg in ('--not_a_setting=1', '--volume='):
    result = subprocess.run([exe, arg], capture_output=True, text=True)
    assert result.returncode == 2 and arg in result.stderr, result
assert settings.read_bytes() == original
print(f'Help covers {len(names)} documented setting names; invalid overrides rejected.')
