#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass your GMSE01 disc image}"
arch="${2:-64}"
out="${3:-build/minecraft/plaza-flag-$arch}"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=0 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1
export SMS_TEXTURE_PACKS=0 SMS_AUDIO=0 SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1 SMS_FRAME_RATE=60
export SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800'
export SMS_SHOTS=4840
for flag in 0 1; do
 mkdir -p "$out/$flag/card" "$out/$flag/shots"
 export SMS_CLEAN_SHINE_GATE="$flag" SMS_SAVE_DIR="$out/$flag/card" SMS_SHOT_DIR="$out/$flag/shots"
 timeout 180 gdb -q -batch -nx -ex 'set debuginfod enabled off' -ex 'set pagination off' -ex 'set confirm off' \
  -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/plaza_flag_test.py' -ex run \
  --args "build/linux-$arch/sms" "$disc" > "$out/$flag/run.log" 2>&1 || true
 rg 'plaza-test:' "$out/$flag/run.log"
 ! rg -q 'plaza-test: FAIL' "$out/$flag/run.log"
 rg -q 'plaza-test: PASS' "$out/$flag/run.log"
done
