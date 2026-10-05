#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass GMSE01 disc}";arch="${2:-64}";out="${3:-build/minecraft/nozzles-$arch}"
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_CLEAN_SHINE_GATE=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1 SMS_TEXTURE_PACKS=0 SMS_AUDIO=0 SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1 SMS_FRAME_RATE=60 SMS_GX_SCALE=2
export SMS_SAVE_DIR="$out/card" SMS_SHOT_DIR="$out/shots" SMS_SHOTS=4820,4930,5060,5120,5180,5280,5400
export SMS_AUTOPRESS="$(python3 tools/minecraft/nozzle_inputs.py)"
timeout 220 gdb -q -batch -nx -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/nozzle_runtime.py' -ex run --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'nozzle-test:|picked up' "$out/run.log"
! rg -q 'nozzle-test: FAIL' "$out/run.log"
rg -q 'PASS native box drop' "$out/run.log"
export SMS_NOZZLE_RELOAD=1 SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800' SMS_SHOTS=4830
 timeout 200 gdb -q -batch -nx -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/nozzle_runtime.py' -ex run --args "build/linux-$arch/sms" "$disc" > "$out/reload.log" 2>&1 || true
rg 'nozzle-test:' "$out/reload.log"
! rg -q 'nozzle-test: FAIL' "$out/reload.log"
rg -q 'PASS cold process restart' "$out/reload.log"
