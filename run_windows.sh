#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
if [[ "${MSYSTEM:-}" != MINGW32 ]]; then
  echo 'Open the MSYS2 MINGW32 shell before running ./run_windows.sh' >&2
  exit 1
fi
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

if (( have_disc == 0 )) && [[ -f build32/bin/sms-standalone.exe ]]; then
  exec build32/bin/sms-standalone.exe "$@"
fi
if [[ ! -f build32/bin/sms.exe ]]; then
  echo 'Build the port with ./build_windows.sh first.' >&2
  exit 1
fi
if (( have_disc == 0 )); then
  shopt -s nullglob
  images=(build32/bin/rom/*.{iso,gcm,ciso,ISO,GCM,CISO})
  if (( ${#images[@]} != 1 )); then
    echo 'Pass a GMSE01 disc image, place one ISO/GCM/CISO in build32/bin/rom/,' >&2
    echo 'or build a standalone executable with ./build_windows.sh /path/to/GMSE01.iso.' >&2
    exit 1
  fi
  set -- "${images[0]}" "$@"
fi
exec build32/bin/sms.exe "$@"
