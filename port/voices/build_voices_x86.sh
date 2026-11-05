#!/usr/bin/env bash
# Build the TrackerXXL voice library for MPC OS / Force: ARMv7 hard-float Linux, glibc <= 2.31, with Zig's clang.
#   ./build_voices.sh                # -> build/libtrackerxxl_voices.a  (+ build/compile_report.txt)
#   ZIG="python3 -m ziglang" ./build_voices.sh     (the default; "zig" if you have the binary)
#   OUT=/some/dir ./build_voices.sh
# Only the ARMv7 target is built (nothing for x86 desktops: it would not run on the Force). The compile flags are the
# ones LXRXXL / LXRXXL ZYNTH use for the Force (-mcpu=generic+v7a+vfp3d16-d32-neon+thumb2, -ffp-contract=off).
# The LXR engine files are compiled with `-include lxr/src/shim/lxr_state.h` (needs clang's section pragma, i.e. zig cc):
# that is what gives every library instance its own copy of the engine's global state.
set -euo pipefail
cd "$(dirname "$0")"
ZIG="${ZIG:-python3 -m ziglang}"
OUT="${OUT:-build}"
OBJ="$OUT/obj"
REPORT="$OUT/compile_report.txt"
rm -rf "$OBJ"; mkdir -p "$OBJ"
: > "$REPORT"

TGT="-target x86_64-linux-gnu.2.31"
COMMON="-O2 -g0 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -std=gnu11"
QUIET="-w"                                              # vendored sources: as upstream builds them
OURS="-Wall -Wextra -Wno-unused-parameter"              # the library's own files: warnings on

L=third_party/lxr02open/Core
M=third_party/mutable
# include sets, one per part (their headers share names: bl.h, ids.h, ...)
INC_LXR="-Ilxr/src -Ilxr/src/shim -Ilxr/src/lxr -Ithird_party/lxr02open -I$L -I$L/DSPAudio -I$L/Preset -I$L/MIDI -I$L/Menu -I$L/SampleRom"
INC_ZYN="-Izynth/src -Ilxr/src/shim -Ithird_party/lxr02open -I$L -I$L/DSPAudio"
INC_BZ="-Ibazz/src"

OBJS=()
FAIL=0
# compile <class> <file> <flags...> ; class: c | cxx
compile() {
  local cls="$1" f="$2"; shift 2
  local o="$OBJ/$(echo "$f" | tr / _).o"
  local drv=cc; [ "$cls" = cxx ] && drv=c++
  if $ZIG $drv $TGT "$@" -c "$f" -o "$o" 2> "$o.log"; then
    printf '%-62s OK%s\n' "$f" "$( [ -s "$o.log" ] && echo "  (warnings: $(grep -c 'warning:' "$o.log"))" )" | tee -a "$REPORT"
    OBJS+=("$o")
  else
    printf '%-62s FAILED\n' "$f" | tee -a "$REPORT"; cat "$o.log" | head -30 | tee -a "$REPORT"; FAIL=1
  fi
}

echo "== LXR02Open engine + LXRXXL 0.5.10 glue (per-instance state: -include lxr_state.h) =="
for f in $(ls $L/DSPAudio/*.c | grep -v -e '/mixer.c$' -e '/random.c$') lxr/src/shim/random.c $L/Preset/ParameterArray.c $L/Menu/Cc2Text.c lxr/src/lxr/lxr_dsp.c; do
  compile c "$f" $COMMON $QUIET $INC_LXR -include lxr/src/shim/lxr_state.h
done
for f in lxr/src/lxr/lxr_fx.c lxr/src/lxr_state.c lxr/src/factory_data.c lxr/src/meta.c; do
  compile c "$f" $COMMON $QUIET $INC_LXR
done

echo "== LXRXXL ZYNTH voices and chain =="
for f in zynth/src/voice.c zynth/src/sound.c zynth/src/engines.c zynth/src/eng_osc.c zynth/src/eng_fm.c zynth/src/eng_more.c zynth/src/eng_new.c; do
  compile c "$f" $COMMON $QUIET $INC_ZYN
done
CXXFLAGS="-O2 -g0 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -DTEST"
for f in zynth/src/mi_bridge.cc $M/braids/macro_oscillator.cc $M/braids/digital_oscillator.cc $M/braids/analog_oscillator.cc $M/braids/resources.cc \
         $M/plaits/dsp/engine/wavetable_engine.cc $M/plaits/dsp/physical_modelling/modal_voice.cc $M/plaits/dsp/physical_modelling/resonator.cc \
         $M/plaits/dsp/physical_modelling/string.cc $M/plaits/dsp/physical_modelling/string_voice.cc $M/plaits/resources.cc \
         $M/stmlib/dsp/units.cc $M/stmlib/utils/random.cc; do
  compile cxx "$f" $CXXFLAGS $QUIET -Izynth/src -I$M
done

echo "== LXRXXL BAZZLINE delay + gate/duck =="
compile c bazz/src/fx.c $COMMON $QUIET $INC_BZ

echo "== the library =="
compile c tv_synth.c $COMMON $OURS $INC_ZYN -Ibazz -I.
compile c tv_fx.c    $COMMON $OURS $INC_BZ -I.
compile c tv_drum.c  $COMMON $OURS $INC_LXR -I.
compile c trackerxxl_voices.c $COMMON $OURS -I.

if [ "$FAIL" -ne 0 ]; then echo "BUILD FAILED (see $REPORT)"; exit 1; fi
rm -f "$OUT/libtrackerxxl_voices.a"
$ZIG ar rcs "$OUT/libtrackerxxl_voices.a" "${OBJS[@]}"
echo "-> $OUT/libtrackerxxl_voices.a (${#OBJS[@]} objects)" | tee -a "$REPORT"
