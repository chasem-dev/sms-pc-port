#!/usr/bin/env bash
# ./build_mac.sh [/path/to/GMSE01.iso]
# 64-bit x86_64 build → build-mac/ (runs under Rosetta on Apple Silicon).
#
# Uses the normal Apple Silicon Homebrew (/opt/homebrew) plus Apple Clang
# targeting x86_64. Does not need Intel Homebrew. See BUILD.md#macos.
set -euo pipefail

cd "$(dirname "$0")"
if [[ "$(uname -s)" != Darwin ]]; then
  echo 'Run ./build_mac.sh on macOS. See BUILD.md for Linux and Windows.' >&2
  exit 1
fi
if [[ "${SMS_ARCH:-64}" == 32 ]]; then
  echo 'macOS cannot build SMS_ARCH=32 (Apple dropped 32-bit userspace).' >&2
  exit 1
fi

bdir=build-mac
arch=64
sdl_ver=2.30.11
# Keep the framework outside build-mac/ so wiping the CMake tree does not delete it.
sdl2_fw="third_party/SDL2.framework"

missing=()
for program in git cmake make patch python3 clang++ curl; do
  if ! command -v "$program" >/dev/null 2>&1; then
    missing+=("$program")
  fi
done
if (( ${#missing[@]} )); then
  echo "Missing: ${missing[*]}." >&2
  echo 'Install Xcode CLT (xcode-select --install) and: brew install cmake python3' >&2
  echo 'See BUILD.md#macos.' >&2
  exit 1
fi

if [[ "$(uname -m)" == arm64 ]] && ! arch -x86_64 true >/dev/null 2>&1; then
  echo 'Rosetta 2 is required on Apple Silicon (the binary is x86_64).' >&2
  echo 'Install with: softwareupdate --install-rosetta' >&2
  exit 1
fi

objcopy_bin=""
for candidate in \
  "$(brew --prefix llvm 2>/dev/null)/bin/llvm-objcopy" \
  /opt/homebrew/opt/llvm/bin/llvm-objcopy \
  /usr/local/opt/llvm/bin/llvm-objcopy \
  llvm-objcopy; do
  if [[ -n "$candidate" && -x "$candidate" ]]; then
    objcopy_bin=$candidate
    break
  elif [[ -n "$candidate" ]] && command -v "$candidate" >/dev/null 2>&1; then
    objcopy_bin=$(command -v "$candidate")
    break
  fi
done
if [[ -z "$objcopy_bin" ]]; then
  echo 'Missing llvm-objcopy (needed to rename the game'\''s operator new/delete).' >&2
  echo 'Install with: brew install llvm' >&2
  exit 1
fi
export PATH="$(dirname "$objcopy_bin"):$PATH"

ensure_sdl2_framework() {
  if [[ -f "$sdl2_fw/SDL2" ]]; then
    return 0
  fi
  echo "Fetching universal SDL2.framework into $sdl2_fw ..."
  mkdir -p third_party
  local dmg="/tmp/SDL2-${sdl_ver}.dmg"
  if [[ ! -f "$dmg" ]]; then
    curl -fsSL -L -o "$dmg" \
      "https://github.com/libsdl-org/SDL/releases/download/release-${sdl_ver}/SDL2-${sdl_ver}.dmg"
  fi
  local mnt
  mnt=$(hdiutil attach "$dmg" -nobrowse -readonly | awk -F'\t' '/\/Volumes\//{print $NF; exit}')
  if [[ -z "$mnt" || ! -d "$mnt/SDL2.framework" ]]; then
    echo 'Failed to mount SDL2.dmg' >&2
    exit 1
  fi
  rm -rf "$sdl2_fw"
  cp -R "$mnt/SDL2.framework" "$sdl2_fw"
  hdiutil detach "$mnt" -quiet || true
}
ensure_sdl2_framework

disc="${1:-${SMS_DISC_IMAGE:-}}"
if [[ -z "$disc" ]]; then
  shopt -s nullglob
  images=($bdir/rom/*.{iso,gcm,ciso,ISO,GCM,CISO})
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
  disc="$(cd "$(dirname "$disc")" && pwd)/$(basename "$disc")"
fi

git submodule update --init decomp

if [[ -f "$bdir/CMakeCache.txt" ]] && ! grep -q 'CMAKE_OSX_ARCHITECTURES:STRING=x86_64' "$bdir/CMakeCache.txt" 2>/dev/null; then
  echo "Removing $bdir (was not an x86_64 CMake tree)..."
  if [[ -d "$bdir/rom" ]]; then
    rm -rf /tmp/sms-mac-rom-backup
    mv "$bdir/rom" /tmp/sms-mac-rom-backup
  fi
  rm -rf "$bdir"
fi
mkdir -p "$bdir"
if [[ -d /tmp/sms-mac-rom-backup ]]; then
  mkdir -p "$bdir/rom"
  mv /tmp/sms-mac-rom-backup/* "$bdir/rom/" 2>/dev/null || true
  rm -rf /tmp/sms-mac-rom-backup
fi

jobs="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
fw_abs="$(cd "$(dirname "$sdl2_fw")" && pwd)/$(basename "$sdl2_fw")"

cmake -S . -B "$bdir" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_C_COMPILER="$(command -v clang)" \
  -DCMAKE_CXX_COMPILER="$(command -v clang++)" \
  -DSMS_SDL2_FRAMEWORK="$fw_abs" \
  -DSMS_ARCH="$arch" \
  -DSMS_GX_BUILD_TESTS=OFF \
  -DSMS_BUNDLE_DISC="$disc"
cmake --build "$bdir" --target sms --parallel "$jobs"

# Runtime: SDL2.framework beside the executable (@rpath).
rm -rf "$bdir/SDL2.framework"
cp -R "$sdl2_fw" "$bdir/SDL2.framework"

mkdir -p "$bdir/rom"
echo "Built $bdir/sms (x86_64; runs under Rosetta on Apple Silicon)"
file "$bdir/sms"
if [[ -n "$disc" ]]; then
  cmake --build "$bdir" --target sms_standalone
  echo "Built $bdir/sms-standalone with the assets of $disc bundled in"
  echo 'Run: ./run_mac.sh   (or build-mac/sms-standalone directly; it needs no disc image)'
else
  echo 'Run: ./run_mac.sh /path/to/your/GMSE01.iso'
  echo 'For a standalone executable: ./build_mac.sh /path/to/your/GMSE01.iso'
fi
