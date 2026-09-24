#!/usr/bin/env bash
# ./build_windows.sh [/path/to/GMSE01.iso]
# With a disc image (the argument, SMS_DISC_IMAGE, or a single image in
# build32/bin/rom/), also builds build32/bin/sms-standalone.exe with the
# disc's files bundled into the executable.
set -euo pipefail

cd "$(dirname "$0")"
if [[ "${MSYSTEM:-}" != MINGW32 ]]; then
  echo 'Open the MSYS2 MINGW32 shell before running ./build_windows.sh' >&2
  exit 1
fi
for program in git cmake ninja patch python; do
  if ! command -v "$program" >/dev/null 2>&1; then
    echo "Missing $program. See BUILD.md for the MSYS2 packages." >&2
    exit 1
  fi
done

disc="${1:-${SMS_DISC_IMAGE:-}}"
if [[ -z "$disc" ]]; then
  shopt -s nullglob
  images=(build32/bin/rom/*.{iso,gcm,ciso,ISO,GCM,CISO})
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
  disc="$(cd "$(dirname "$disc")" && pwd -W)/$(basename "$disc")"
fi

git submodule update --init decomp
cmake -S . -B build32 -G Ninja -DSMS_ARCH=32 -DSMS_GX_BUILD_TESTS=OFF -DSMS_BUNDLE_DISC="$disc"
cmake --build build32 --target sms -j "${JOBS:-4}"
echo 'Built build32/bin/sms.exe'
if [[ -n "$disc" ]]; then
  cmake --build build32 --target sms_standalone
  echo "Built build32/bin/sms-standalone.exe with the assets of $disc bundled in"
  echo 'Run: ./run_windows.sh   (or build32/bin/sms-standalone.exe; it needs no disc image)'
else
  echo 'Run: ./run_windows.sh /path/to/your/GMSE01.iso'
  echo 'From PowerShell: .\run_windows.cmd "C:\path\to\your\GMSE01.iso"'
  echo 'For a standalone executable: ./build_windows.sh /path/to/your/GMSE01.iso'
fi
