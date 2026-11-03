#!/usr/bin/env bash
# Compile-check the bridge for ARMv7 (Force / MPC OS): same flags as port/platform/compile_core.sh.  Compile only; nothing is linked or run.
#   bridge/build_bridge.sh [skin build dir]     (default port/skin/build, the output of gen/build_skin.py)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
TXL="$(cd "$HERE/../../.." && pwd)"
BUILD="${1:-$HERE/../build}"
OUT="${OUT:-/tmp/xxl_bridge_obj}"; mkdir -p "$OUT"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-/tmp/zc}" ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-/tmp/zg}"
cd "$TXL/core"
INC=(); while IFS= read -r d; do INC+=("-I$d"); done < <(find . -mindepth 1 -type d | sort)
TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
for f in scene bridge map_ctl map_pattern map_wave; do
  # shellcheck disable=SC2086
  python3 -m ziglang c++ $TGT -O2 -fPIC -fvisibility=hidden -DNDEBUG -w -DXXL_PLUGIN=1 -std=gnu++17 -include cassert \
    -c -I. -I"$TXL/port/platform/src" -I"$TXL/port/platform/wrapper" "${INC[@]}" -I"$HERE/include" -I"$HERE/src" -I"$BUILD" \
    "$HERE/src/$f.cpp" -o "$OUT/$f.o"
  echo "ok $f.cpp"
done
