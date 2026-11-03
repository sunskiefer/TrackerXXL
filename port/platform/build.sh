#!/usr/bin/env bash
# Build the TrackerXXL platform + firmware into an ARMv7 hard-float VST2 for the Force / MPC OS (glibc <= 2.31), Zig as the toolchain.
#   ./build.sh            incremental (object files are kept in build/obj)
#   CLEAN=1 ./build.sh   from scratch
#   SKIN=1 ./build.sh    also run the framework's skin generator (needs a host gcc for tools' shadow_art; the display agent's job)
# Output: build/arm/trackerxxl.so (exports only VSTPluginMain). No x86/desktop binary is built or run.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here"
export TXL="${TXL:-$(cd ../.. && pwd)}"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-/tmp/zc}" ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-/tmp/zg}"
ZIG="${ZIG:-python3 -m ziglang}"
JOBS="${JOBS:-$(nproc)}"
MPC_VST="$here/third_party/mpc-vst-plugins"
NAME="L'Cronx - VST - TrackerXXL"     # the apostrophe is only ever inside double quotes here; quote "$NAME" everywhere it is used
[ "${CLEAN:-0}" = 1 ] && rm -rf build/obj build/arm build/gen
mkdir -p build/obj build/gen build/arm build/log

. ./build_srcs.sh

# 1. controls -> params.json, build/ctl_table.h, build/ctl_check.h ; params.json -> build/params.h (the wrapper's table)
python3 tools/gen_controls.py "$here"
if [ "${FULLSKIN:-1}" = 1 ]; then
  # full skin: the generated skin's parameter table (platform controls first, then the scene slots); staged so the platform's own inputs stay untouched
  rm -rf build/full && mkdir -p build/full
  cp "$TXL/port/skin/build/params.json" "$TXL/port/skin/build/vst.json" build/full/
  python3 "$MPC_VST/tools/gen_vst.py" build/full/vst.json --params-h
  cp build/full/build/params.h build/params.h
else
  python3 "$MPC_VST/tools/gen_vst.py" vst.json --params-h
fi
[ -f build/params.h ]
grep -q 'PLUG_VENDOR "L'"'"'Cronx"' build/params.h || { echo "vendor string lost its apostrophe in params.h"; exit 1; }
[ "${SKIN:-0}" = 1 ] && python3 "$MPC_VST/tools/gen_vst.py" vst.json

# 2. the firmware: every core source, plus the plugin-adjusted Tracker.cpp (external-RAM arrays -> .bss, 44.1 kHz)
sed -e 's/section("\.sample_memory")/section(".bss.xxl_sample_memory")/' \
    -e 's/section("\.delay_memory")/section(".bss.xxl_delay_memory")/' \
    -e 's/section("\.reverb_memory")/section(".bss.xxl_reverb_memory")/' \
    -e 's/section("\.scratch_memory")/section(".bss.xxl_scratch_memory")/' \
    -e 's/kAudioSampleRate = 44117\.6470588f/kAudioSampleRate = 44100.0f/' \
    "$CORE/source/Tracker.cpp" > build/gen/Tracker.cpp
grep -q 'xxl_sample_memory' build/gen/Tracker.cpp && grep -q '= 44100.0f' build/gen/Tracker.cpp

JOBLIST=build/joblist.txt
: > "$JOBLIST"
add_job() {  # kind src(abs or rel to core) out
  local kind="$1" src="$2" out="$3" abs="$2"
  case "$src" in /*) ;; *) abs="$CORE/$src";; esac
  if [ ! -f "$out" ] || [ "$abs" -nt "$out" ] || [ "$here/build_srcs.sh" -nt "$out" ] || [ "$here/compile_core.sh" -nt "$out" ]; then
    echo "$kind $src $out" >> "$JOBLIST"
  fi
}
OBJS=()
while read -r kind f; do
  o="$here/build/obj/$(echo "$f" | tr / _).o"; OBJS+=("$o"); add_job "$kind" "$f" "$o"
done < <(core_files)
o="$here/build/obj/GEN_Tracker.cpp.o"; OBJS+=("$o"); add_job cpp "$here/build/gen/Tracker.cpp" "$o"
for f in $PLATFORM_CPP; do
  o="$here/build/obj/PLAT_$(echo "$f" | tr / _).o"; OBJS+=("$o")
  # platform sources depend on the generated tables and their headers too
  if [ ! -f "$o" ] || [ -n "$(find "$here/src" "$here/build/ctl_table.h" "$here/build/ctl_check.h" "$here/display_hook.h" "$here/xxl_runtime.h" -newer "$o" -print -quit)" ]; then
    echo "cpp $here/$f $o" >> "$JOBLIST"
  fi
done
for f in $SYNTH_CPP; do   # Tracker+ polysynth layer (port/synth)
  o="$here/build/obj/SYN_$(echo "$f" | tr / _).o"; OBJS+=("$o")
  if [ ! -f "$o" ] || [ -n "$(find "$TXL/port/synth/src" "$TXL/port/synth/include/PolySynth" "$TXL/port/voices/trackerxxl_voices.h" "$here/compile_core.sh" -newer "$o" -print -quit)" ]; then
    echo "cpp $TXL/$f $o" >> "$JOBLIST"
  fi
done
echo "compiling $(wc -l < "$JOBLIST") of ${#OBJS[@]} objects with $JOBS jobs"
if [ -s "$JOBLIST" ]; then
  xargs -P "$JOBS" -L1 bash -c './compile_core.sh "$0" "$1" "$2" 2> "build/log/$(basename "$2").err" || { echo "FAILED: $1"; cat "build/log/$(basename "$2").err" | head -30; exit 255; }' < "$JOBLIST"
fi

# 3. the VST2 wrapper (C, the framework's vst2_wrap.c with the Q-Link/automation UpdateDisplay fix #486-#489)
TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
WRAP_O="$here/build/obj/WRAP_vst2_wrap.c.o"
$ZIG cc $TGT -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w -std=gnu11 \
    -Ibuild -I"$MPC_VST/wrapper" -c wrapper/vst2_wrap.c -o "$WRAP_O"

# 4. link: only VSTPluginMain is exported; every symbol must resolve
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/arm/exports.map
STRIP_FLAG="-Wl,-s"; [ "${NOSTRIP:-0}" = 1 ] && STRIP_FLAG=""
$ZIG c++ $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/arm/exports.map -Wl,--gc-sections $STRIP_FLAG \
    "${OBJS[@]}" "$WRAP_O" "$VOICES_A" -lm -ldl -lpthread -o build/arm/trackerxxl.so
ls -l build/arm/trackerxxl.so
echo "OK: build/arm/trackerxxl.so  (package folder name: $NAME)"
