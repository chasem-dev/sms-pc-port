#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
disc="${1:?Pass your GMSE01 disc image}"
arch="${2:-64}"
out="${3:-build/minecraft/building-$arch}"
mkdir -p "$out/card" "$out/shots"
export SMS_SETTINGS=/dev/null SMS_MINECRAFT=1 SMS_CLEAN_SHINE_GATE=1 SMS_HEADLESS=1 SMS_SKIP_MOVIES=1
export SMS_TEXTURE_PACKS=0 SMS_AUDIO="${SMS_AUDIO:-0}" SMS_VI_DETERMINISTIC=1 SMS_WARP=1,0,1 SMS_FRAME_RATE=60
export SMS_SAVE_DIR="$out/card" SMS_SHOT_DIR="$out/shots"
export SMS_AUTOPRESS="$(python3 tools/minecraft/building_inputs.py)"
export SMS_SHOTS=5050,5150,5310,5570,5750,5850,5890,5940,5980,6110,6155,6295
timeout 240 gdb -q -batch -nx -ex 'set debuginfod enabled off' -ex 'set pagination off' -ex 'set confirm off' \
 -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/building_runtime.py' -ex run \
 --args "build/linux-$arch/sms" "$disc" > "$out/run.log" 2>&1 || true
rg 'building-test:|\[minecraft\] placed' "$out/run.log"
! rg -q 'building-test: FAIL' "$out/run.log"
rg -q 'PASS live clicks' "$out/run.log"
# A new process must read the same on-disk world and chest contents.
export SMS_BUILDING_RELOAD=1 SMS_AUTOPRESS='START@1400,STICK_LEFT@2000,A@2400,STICK_LEFT@3100,A@3250,A@3450,A@3800'
export SMS_SHOTS=4840
if [[ -n "${SMS_AUDIO_WAV:-}" ]]; then export SMS_AUDIO_WAV="${SMS_AUDIO_WAV%.wav}-reload.wav"; fi
timeout 180 gdb -q -batch -nx -ex 'set debuginfod enabled off' -ex 'set pagination off' -ex 'set confirm off' \
 -ex 'handle SIG34 nostop noprint' -ex 'source tools/minecraft/building_runtime.py' -ex run \
 --args "build/linux-$arch/sms" "$disc" > "$out/reload.log" 2>&1 || true
rg 'building-test:' "$out/reload.log"
! rg -q 'building-test: FAIL' "$out/reload.log"
rg -q 'PASS cold process restart' "$out/reload.log"
echo "PASS building/crafting/persistent chests ($arch-bit)"
