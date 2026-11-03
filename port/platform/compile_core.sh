#!/usr/bin/env bash
# Compile one core source for ARMv7 (Force / MPC OS): compile_core.sh <kind> <path relative to core/ or absolute> <out.o>
set -u
TXL="${TXL:-$(cd "$(dirname "$0")/../.." && pwd)}"
kind="$1"; src="$2"; out="$3"
CORE="$TXL/core"; PLAT="$TXL/port/platform"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-/tmp/zc}" ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-/tmp/zg}"
cd "$CORE"
INC=(); while IFS= read -r d; do INC+=("-I$d"); done < <(find . -mindepth 1 -type d | sort)
TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w -DXXL_PLUGIN=1"
# Tracker+ (port/synth): the 16-track / 60 MB-sample configuration (PLATFORM_IMX without any IMX hardware code) + the synth.
SYN="$TXL/port/synth"
COMMON="$COMMON -DPLATFORM_IMX -DFEATURE_SYNTH -DFMT_HEADER_ONLY"
SYNINC="-I$SYN/include -I$SYN/include/3rdparty -I$SYN/src -I$TXL/port/voices -I$TXL/port/skin/bridge/include -I$TXL/port/skin/bridge/src -I$TXL/port/skin/build"
case "$kind" in
  c)   CC=cc;  STD="-std=gnu11"; EXTRA="";;
  cpp) CC=c++; STD="-std=gnu++20"; EXTRA="-include cassert -include $SYN/include/xxl_synth_compat.h";;
esac
# shellcheck disable=SC2086
python3 -m ziglang $CC $TGT $COMMON $STD $EXTRA -c -I. -I"$PLAT/src" -I"$PLAT/wrapper" "${INC[@]}" $SYNINC "$src" -o "$out"
