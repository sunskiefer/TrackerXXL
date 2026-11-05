#!/usr/bin/env bash
# Build the TrackerXXL own engine (no Polyend firmware) into an ARMv7 hard-float VST2 for the Force / MPC OS (glibc <= 2.31).
# Output: platform/build/arm/trackerxxl.so (the path package_release.sh expects).  Run skin/gen/build_skin.py first.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; PORT="$(cd "$here/.." && pwd)"; PLAT="$PORT/platform"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-/tmp/zc}" ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-/tmp/zg}"
ZIG="${ZIG:-python3 -m ziglang}"; MPC_VST="$PLAT/third_party/mpc-vst-plugins"
cd "$PLAT"; mkdir -p build/full build/arm; mkdir -p "$here/build/obj"
python3 tools/gen_controls.py "$PLAT"
rm -rf build/full && mkdir -p build/full
cp "$PORT/skin/build/params.json" "$PORT/skin/build/vst.json" build/full/
python3 "$MPC_VST/tools/gen_vst.py" build/full/vst.json --params-h
cp build/full/build/params.h build/params.h
TGT="-target arm-linux-gnueabihf.2.31 -mcpu=generic+v7a+vfp3d16-d32-neon+thumb2 -ffp-contract=off"
INC="-I$here/src -I$PORT/voices -I$PORT/skin/bridge/include -I$PORT/skin/build -I$PLAT/build -I$PLAT/wrapper -I$MPC_VST/wrapper"
OBJ="$here/build/obj"; rm -f "$OBJ"/*.o
SRCS="$here/src/model.cpp $here/src/audio.cpp $here/src/app.cpp $here/src/ui.cpp $here/src/grid.cpp $here/src/glue.cpp $PORT/skin/bridge/src/scene.cpp"
for f in $SRCS; do
  echo "$ZIG c++ $TGT -std=gnu++20 -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -DNDEBUG -w $INC -c $f -o $OBJ/$(basename $f).o"
done | xargs -P "$(nproc)" -I{} bash -c "{}"
$ZIG cc $TGT -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w -std=gnu11 \
  -DHAS_SONG_POS -DHAS_DISPLAY_REV -DDISPLAY_REV_NO_UPDATE -DPARAM_TEXT_MAX=64 $INC -c "$PLAT/wrapper/vst2_wrap.c" -o "$OBJ/vst2_wrap.o"
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/arm/exports.map
$ZIG c++ $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/arm/exports.map -Wl,--gc-sections -Wl,-s \
  "$OBJ"/*.o "$PORT/voices/build/libtrackerxxl_voices.a" -lm -ldl -lpthread -o build/arm/trackerxxl.so
ls -l build/arm/trackerxxl.so
