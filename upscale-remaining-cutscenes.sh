#!/usr/bin/env bash
# Generate local 6x AI previews using this machine's configured media tools.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
config=${SMS_AI_CUTSCENES_CONFIG:-"$root/build/media/6x-ai-config.json"}
if [[ ! -f "$config" ]]; then
  echo "The AI batch configuration is missing: $config" >&2
  echo "See $root/docs/HD-CUTSCENES.md for the local 6x preview setup." >&2
  exit 1
fi
exec python3 "$root/tools/media/upscale_batch.py" --config "$config" "$@"
