#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass GMSE01 disc}";arch="${2:-64}";mode="${3:-quick}";out="${4:-build/minecraft/sword-click-$mode-$arch}"
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1 SMS_TEXTURE_PACKS=0 SMS_AUDIO=0 SMS_VI_DETERMINISTIC=1 SMS_WARP=2,0,1 SMS_FRAME_RATE=60
export SMS_SAVE_DIR="$out/card" SMS_SWORD_CLICK_MODE="$mode" SMS_SHOT_DIR="$out/shots" SMS_SHOTS=4942,4948,4972,5010
export SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,KEY_3@4800,KEY_G@4810+4,KEY_G@4840+4,KEY_2@4860,KEY_G@4870+4,KEY_3@4890,KEY_TAB@4900,KEY_G@4910+4,KEY_TAB@4930,MOUSE_LEFT_320_240@4940+0,MOUSE_LEFT_320_240@4970+0'
commands=(-ex run)
if [[ "$mode" == natural ]]; then commands+=(-ex 'python spawn_native()' -ex continue); fi
timeout 240 gdb -q -batch -nx -ex 'set debuginfod enabled off' -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/sword_click_runtime.py' "${commands[@]}" --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'sword-test:|\[minecraft\] sword' "$out/run.log"
! rg -q 'sword-test: FAIL|Error occurred in Python' "$out/run.log"
rg -q 'PASS real input' "$out/run.log"
rg -q 'PASS native sword hit sets' "$out/run.log"
if [[ "$mode" == moving ]]; then rg -q 'PASS released swing remains active' "$out/run.log"; fi
