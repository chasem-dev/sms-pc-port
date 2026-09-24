#!/usr/bin/env bash
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

git submodule update --init decomp
cmake -S . -B build -DSMS_ARCH=32 -DSMS_GX_BUILD_TESTS=OFF
cmake --build build --target sms --parallel "${JOBS:-4}"
mkdir -p build/rom
echo 'Built build/sms'
echo 'Run: ./run_linux.sh /path/to/your/GMSE01.iso'
