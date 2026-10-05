#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass your GMSE01 disc image}"
arch="${2:-64}"
out="${3:-build/minecraft/check-$arch}"
[[ "$arch" == 32 || "$arch" == 64 ]] || { echo 'Architecture must be 32 or 64' >&2; exit 1; }
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1
export SMS_TEXTURE_PACKS=0 SMS_AUDIO="${SMS_AUDIO:-0}" SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1
export SMS_FRAME_RATE="${SMS_FRAME_RATE:-30}"
export SMS_SAVE_DIR="$out/card" SMS_SHOT_DIR="$out/shots"
export SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,KEY_G@4570+170,R@4650+80,R@4740+80,KEY_9@4850,WHEEL_DOWN@4860,WHEEL_DOWN@4870,KEY_TAB@4880,KEY_TAB@4900,KEY_3@4910'
export SMS_SHOTS=4590,4610,4630,4650,4670,4690,4730,4800,4886,4930
# gdb's intentional kill after the assertions may give a nonzero run status.
timeout 240 gdb -q -batch -nx -ex 'set debuginfod enabled off' \
  -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' \
  -ex 'source tools/minecraft/runtime_test.py' -ex run \
  --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'minecraft-test:|\[minecraft\]' "$out/run.log"
if rg -q 'minecraft-test: FAIL' "$out/run.log"; then exit 1; fi
rg -q 'PASS Steve retains functioning FLUDD' "$out/run.log"
rg -q 'PASS live input route: diamond sword reselected' "$out/run.log"
echo "PASS Minecraft runtime ($arch-bit); screenshots in $out/shots"
