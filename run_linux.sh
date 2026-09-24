#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
if [[ "$(uname -s)" != Linux ]]; then
  echo 'Run ./run_linux.sh on Linux. See BUILD.md for Windows instructions.' >&2
  exit 1
fi
if [[ ! -x build/sms ]]; then
  echo 'Build the port with ./build_linux.sh first.' >&2
  exit 1
fi
if (( $# == 0 )) && [[ -z "${SMS_DISC_IMAGE:-}" && -z "${SMS_DISC_ROOT:-}" ]]; then
  shopt -s nullglob
  images=(build/rom/*.{iso,gcm,ciso,ISO,GCM,CISO})
  if (( ${#images[@]} != 1 )); then
    echo 'Pass a GMSE01 disc image, set SMS_DISC_IMAGE, or place one ISO/GCM/CISO in build/rom/.' >&2
    exit 1
  fi
  set -- "${images[0]}"
fi
if (( $# > 0 )) && [[ "$1" != --* && ! -f "$1" && ! -d "$1" ]]; then
  echo "Disc image or extracted disc folder not found: $1" >&2
  exit 1
fi
exec build/sms "$@"
