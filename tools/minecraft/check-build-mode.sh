#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass GMSE01 disc}";arch="${2:-64}";out="${3:-build/minecraft/build-mode-$arch}"
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1 SMS_TEXTURE_PACKS=0 SMS_AUDIO=0 SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1 SMS_FRAME_RATE=60 SMS_GX_SCALE=2
export SMS_SAVE_DIR="$out/card" SMS_SHOT_DIR="$out/shots" SMS_SHOTS=4840,4888,4980,5030
export SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,KEY_6@4790,KEY_B@4800+80,MOUSE_MOVE_320_240@4820+1,MOUSE_MOVE_430_280@4860+1,KEY_TAB@4900,MOUSE_MOVE_350_200@4910+1,KEY_TAB@4930,MOUSE_MOVE_430_280@4940+1,MOUSE_RIGHT_430_280@4960+2,KEY_B@5000+60,MOUSE_MOVE_260_180@5050+1'
timeout 240 gdb -q -batch -nx -ex 'set debuginfod enabled off' -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/build_mode_runtime.py' -ex run --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'build-mode-test:' "$out/run.log"
! rg -q 'build-mode-test: FAIL' "$out/run.log"
rg -q 'PASS B toggle, free pointer' "$out/run.log"
