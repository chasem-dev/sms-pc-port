#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass GMSE01 disc}";arch="${2:-64}";out="${3:-build/minecraft/starter-$arch}"
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_CLEAN_SHINE_GATE=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1 SMS_TEXTURE_PACKS=0 SMS_AUDIO=1 SMS_AUDIO_OUT=none SMS_MINECRAFT_AUDIO_TRACE=1 SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1 SMS_FRAME_RATE=60 SMS_GX_SCALE=2
export SMS_SAVE_DIR="$out/card" SMS_SHOT_DIR="$out/shots" SMS_SHOTS=4930,4955,4995,5062,5070,5098 SMS_AUDIO_WAV="$out/game.wav"
export SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,KEY_TAB@4800,MOUSE_LEFT_356_366@4820,MOUSE_RIGHT_356_118@4840,MOUSE_RIGHT_356_154@4860,MOUSE_LEFT_356_366@4880,MOUSE_LEFT_468_138@4900,MOUSE_LEFT_392_366@4920,MOUSE_LEFT_600_210@4940,MOUSE_LEFT_200_210@4980,KEY_TAB@5040,KEY_TAB@5068'
timeout 240 gdb -q -batch -nx -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/starter_runtime.py' -ex run -ex 'python run_sound_checks()' -ex continue --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'starter-test:' "$out/run.log"
! rg -q 'starter-test: FAIL' "$out/run.log"
rg -q 'PASS native starter items' "$out/run.log"
