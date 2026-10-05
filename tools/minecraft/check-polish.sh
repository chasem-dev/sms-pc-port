#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass GMSE01 disc}";arch="${2:-64}";out="${3:-build/minecraft/polish-$arch}"
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_CLEAN_SHINE_GATE=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1 SMS_TEXTURE_PACKS=0 SMS_AUDIO=0 SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1 SMS_FRAME_RATE=60 SMS_GX_SCALE=2
export SMS_SAVE_DIR="$out/card" SMS_SHOT_DIR="$out/shots" SMS_SHOTS=4840,4938,5020,5140,5390,5440,5490,5540,5670,5780,5820,5990
export SMS_AUTOPRESS="$(python3 tools/minecraft/polish_inputs.py)"
timeout 240 gdb -q -batch -nx -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/polish_runtime.py' -ex run --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'polish-test:|\[minecraft\]' "$out/run.log"
! rg -q 'polish-test: FAIL' "$out/run.log"
rg -q 'PASS native mining' "$out/run.log"

export SMS_SHOTS=4840 SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,KEY_TAB@4800'
timeout 180 gdb -q -batch -nx -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/polish_reload.py' -ex run --args "build/linux-$arch/sms" "$disc" > "$out/reload.log" 2>&1 || true
rg 'polish-test:' "$out/reload.log"
! rg -q 'polish-test: FAIL' "$out/reload.log"
rg -q 'PASS cold restart' "$out/reload.log"
