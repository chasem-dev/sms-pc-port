#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
if [[ "${MSYSTEM:-}" != MINGW32 ]]; then
  echo 'Open the MSYS2 MINGW32 shell before running ./run_pc.sh' >&2
  exit 1
fi
if [[ ! -f build32/bin/sms.exe ]]; then
  echo 'Build the port with ./build_pc.sh first.' >&2
  exit 1
fi
if (( $# == 0 )); then
  shopt -s nullglob
  images=(build32/bin/rom/*.{iso,gcm,ciso,ISO,GCM,CISO})
  if (( ${#images[@]} != 1 )); then
    echo 'Pass a GMSE01 disc image, or place one ISO/GCM/CISO in build32/bin/rom/.' >&2
    exit 1
  fi
  set -- "${images[0]}"
fi
exec build32/bin/sms.exe "$@"
