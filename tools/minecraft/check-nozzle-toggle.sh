#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass GMSE01 disc}";arch="${2:-64}";out="${3:-build/minecraft/nozzle-toggle-$arch}"
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_CLEAN_SHINE_GATE=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1 SMS_TEXTURE_PACKS=0 SMS_AUDIO=0 SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1 SMS_FRAME_RATE=60 SMS_GX_SCALE=2
export SMS_SAVE_DIR="$out/card" SMS_SHOT_DIR="$out/shots" SMS_SHOTS=4905,4925,4975,5205,5245,5375,5415,5602,5642
export SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,KEY_TAB@4800,KEY_LSHIFT@4820+20,MOUSE_LEFT_176_366@4830,KEY_TAB@4860,KEY_V@4900,KEY_V@4950,KEY_P@4990,KEY_P@5010,KEY_E@5030+20,KEY_TAB@5100,KEY_LSHIFT@5110+25,MOUSE_LEFT_284_366@5120,KEY_TAB@5150,KEY_V@5180,KEY_V@5220,KEY_TAB@5270,KEY_LSHIFT@5280+25,MOUSE_LEFT_320_366@5290,KEY_TAB@5320,KEY_V@5350,KEY_V@5390,KEY_E@5440+100,KEY_TAB@5560,MOUSE_LEFT_600_210@5580,MOUSE_LEFT_200_80@5620,KEY_LSHIFT@5680+25,MOUSE_LEFT_176_134@5690,KEY_TAB@5720,KEY_V@5740'
timeout 240 gdb -q -batch -nx -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/nozzle_toggle_runtime.py' -ex run --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'nozzle-toggle-test:' "$out/run.log"
! rg -q 'nozzle-toggle-test: FAIL' "$out/run.log"
rg -q 'PASS physical V on all attachments' "$out/run.log"
