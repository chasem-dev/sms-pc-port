#!/usr/bin/env bash
# Launch the native Minecraft crossover with Sunshine's third-person camera.
set -euo pipefail
cd "$(dirname "$0")"
export SMS_MINECRAFT=1
export SMS_ARCH="${SMS_ARCH:-64}"
exec ./run.sh "$@"
