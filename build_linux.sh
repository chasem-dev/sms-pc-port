#!/usr/bin/env bash
# ./build_linux.sh [/path/to/GMSE01.iso]
# With a disc image (the argument, SMS_DISC_IMAGE, or a single image in
# build/rom/), also builds build/sms-standalone: the game with the disc's
# files bundled into the executable, which runs with no image beside it.
set -euo pipefail

cd "$(dirname "$0")"
if [[ "$(uname -s)" != Linux ]]; then
  echo 'Run ./build_linux.sh on Linux. See BUILD.md for Windows instructions.' >&2
  exit 1
fi
for program in git cmake make patch python3 objcopy; do
  if ! command -v "$program" >/dev/null 2>&1; then
    echo "Missing $program. See BUILD.md for Linux dependencies." >&2
    exit 1
  fi
done

disc="${1:-${SMS_DISC_IMAGE:-}}"
if [[ -z "$disc" ]]; then
  shopt -s nullglob
  images=(build/rom/*.{iso,gcm,ciso,ISO,GCM,CISO})
  shopt -u nullglob
  if (( ${#images[@]} == 1 )); then
    disc="${images[0]}"
  fi
fi
if [[ -n "$disc" ]]; then
  if [[ ! -f "$disc" ]]; then
    echo "Disc image not found: $disc" >&2
    exit 1
  fi
  disc="$(cd "$(dirname "$disc")" && pwd)/$(basename "$disc")"
fi

git submodule update --init decomp
cmake -S . -B build -DSMS_ARCH=32 -DSMS_GX_BUILD_TESTS=OFF -DSMS_BUNDLE_DISC="$disc"
cmake --build build --target sms --parallel "${JOBS:-4}"
mkdir -p build/rom
echo 'Built build/sms'
if [[ -n "$disc" ]]; then
  cmake --build build --target sms_standalone
  echo "Built build/sms-standalone with the assets of $disc bundled in"
  echo 'Run: ./run_linux.sh   (or build/sms-standalone directly; it needs no disc image)'
else
  echo 'Run: ./run_linux.sh /path/to/your/GMSE01.iso'
  echo 'For a standalone executable: ./build_linux.sh /path/to/your/GMSE01.iso'
fi
