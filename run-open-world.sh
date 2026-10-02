#!/usr/bin/env bash
# ./run-open-world.sh [GMSE01 disc image or extracted files/ folder] [--headless]
# Runs the optional Plaza / Harbor coastal path and Pinna Blooper ferry.
# See docs/OPEN_WORLD.md for the path entrances and prototype scope.
set -euo pipefail
cd "$(dirname "$0")"
export SMS_OPEN_WORLD=1
export SMS_ARCH="${SMS_ARCH:-64}"
exec ./run.sh "$@"
