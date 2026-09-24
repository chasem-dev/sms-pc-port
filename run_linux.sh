#!/usr/bin/env bash
# ./run_linux.sh [/path/to/GMSE01.iso] [--headless ...]
# Without a disc image (argument, SMS_DISC_IMAGE or SMS_DISC_ROOT), runs
# build/sms-standalone (build-64/ with SMS_ARCH=64) if it was built, else the single image in build/rom/.
set -euo pipefail

cd "$(dirname "$0")"
if [[ "$(uname -s)" != Linux ]]; then
  echo 'Run ./run_linux.sh on Linux. See BUILD.md for Windows instructions.' >&2
  exit 1
fi

if [[ "${SMS_ARCH:-32}" == 64 ]]; then bdir=build-64; else bdir=build; fi
have_disc=0
for arg in "$@"; do
  if [[ "$arg" != -* ]]; then
    have_disc=1
    if [[ ! -f "$arg" && ! -d "$arg" ]]; then
      echo "Disc image or extracted disc folder not found: $arg" >&2
      exit 1
    fi
  fi
done
if [[ -n "${SMS_DISC_IMAGE:-}" || -n "${SMS_DISC_ROOT:-}" ]]; then
  have_disc=1
fi

if (( have_disc == 0 )) && [[ -x $bdir/sms-standalone ]]; then
  exec $bdir/sms-standalone "$@"
fi
if [[ ! -x $bdir/sms ]]; then
  echo 'Build the port with ./build_linux.sh first (SMS_ARCH=64 for the 64-bit build).' >&2
  exit 1
fi
if (( have_disc == 0 )); then
  shopt -s nullglob
  images=($bdir/rom/*.{iso,gcm,ciso,ISO,GCM,CISO})
  if (( ${#images[@]} != 1 )); then
    echo 'Pass a GMSE01 disc image, set SMS_DISC_IMAGE, place one ISO/GCM/CISO in build/rom/,' >&2
    echo 'or build a standalone executable with ./build_linux.sh /path/to/GMSE01.iso.' >&2
    exit 1
  fi
  set -- "${images[0]}" "$@"
fi
exec $bdir/sms "$@"
