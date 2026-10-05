#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass GMSE01 disc}";arch="${2:-64}";mode="${3:-edges}";out="${4:-build/minecraft/achievement-$mode-$arch}"
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1 SMS_TEXTURE_PACKS=0 SMS_AUDIO=0 SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1 SMS_FRAME_RATE=60 SMS_GX_SCALE=2
export SMS_WIDESCREEN="${SMS_WIDESCREEN:-16:9}" SMS_WIDESCREEN_HUD="$mode"
export SMS_SAVE_DIR="$out/card" SMS_SHOT_DIR="$out/shots" SMS_SHOTS=4820,4910
export SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800,KEY_TAB@4860'
timeout 240 gdb -q -batch -nx -ex 'set debuginfod enabled off' -ex 'set pagination off' -ex 'set confirm off' -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/achievement_edges_runtime.py' -ex run --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'achievement-edges-test:' "$out/run.log"
! rg -q 'achievement-edges-test: FAIL' "$out/run.log"
rg -q 'PASS reminder and achievement' "$out/run.log"
