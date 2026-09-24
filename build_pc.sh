#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
if [[ "${MSYSTEM:-}" != MINGW32 ]]; then
  echo 'Open the MSYS2 MINGW32 shell before running ./build_pc.sh' >&2
  exit 1
fi
for program in git cmake ninja patch python; do
  if ! command -v "$program" >/dev/null 2>&1; then
    echo "Missing $program. See BUILD.md for the MSYS2 packages." >&2
    exit 1
  fi
done

git submodule update --init decomp
cmake -S . -B build32 -G Ninja -DSMS_ARCH=32 -DSMS_GX_BUILD_TESTS=OFF
cmake --build build32 --target sms -j "${JOBS:-4}"
echo 'Built build32/bin/sms.exe'
echo 'Run: ./run_pc.sh /path/to/your/GMSE01.iso'
