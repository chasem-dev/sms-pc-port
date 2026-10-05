#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass your GMSE01 disc image}"
arch="${2:-64}"
out="${3:-build/minecraft/fludd-$arch}"
[[ "$arch" == 32 || "$arch" == 64 ]] || exit 1
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1
export SMS_TEXTURE_PACKS=0 SMS_AUDIO=0 SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1
export SMS_FRAME_RATE="${SMS_FRAME_RATE:-60}"
export SMS_SAVE_DIR="$out/card" SMS_SHOT_DIR="$out/shots"
export SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,KEY_3@4780,KEY_2@5040,STICK_DOWN@5210+90,A@5320+5,R@5440+100'
export SMS_SHOTS=4840,4920,5000,5080,5160,5260,5340,5480
timeout 240 gdb -q -batch -nx -ex 'set debuginfod enabled off' \
  -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' \
  -ex 'source tools/minecraft/fludd_test.py' -ex run \
  --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'fludd-test:' "$out/run.log"
! rg -q 'fludd-test: FAIL' "$out/run.log"
rg -q 'fludd-test: PASS' "$out/run.log"
