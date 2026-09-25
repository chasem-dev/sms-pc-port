#!/usr/bin/env bash
# ./run_mac.sh [/path/to/GMSE01.iso] [--headless ...]
# Without a disc image (argument, SMS_DISC_IMAGE or SMS_DISC_ROOT), runs
# build-mac/SMS.app if it was built (in this terminal, so its log shows),
# else the single image in build-mac/rom/.
set -euo pipefail

cd "$(dirname "$0")"
if [[ "$(uname -s)" != Darwin ]]; then
  echo 'Run ./run_mac.sh on macOS. See BUILD.md for Linux and Windows.' >&2
  exit 1
fi

bdir=build-mac
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

if (( have_disc == 0 )) && [[ -x "$bdir/SMS.app/Contents/MacOS/sms" ]]; then
  exec "$bdir/SMS.app/Contents/MacOS/sms" "$@"
fi
if [[ ! -x "$bdir/sms" ]]; then
  echo 'Build the port with ./build_mac.sh first.' >&2
  exit 1
fi
if (( have_disc == 0 )); then
  shopt -s nullglob
  images=($bdir/rom/*.{iso,gcm,ciso,ISO,GCM,CISO})
  if (( ${#images[@]} != 1 )); then
    echo "Pass a GMSE01 disc image, set SMS_DISC_IMAGE, place one ISO/GCM/CISO in $bdir/rom/," >&2
    echo 'or build SMS.app with ./build_mac.sh /path/to/GMSE01.iso.' >&2
    exit 1
  fi
  set -- "${images[0]}" "$@"
fi
exec "$bdir/sms" "$@"
