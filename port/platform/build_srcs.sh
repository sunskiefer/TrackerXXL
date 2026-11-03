# Source lists for TrackerXXL (sourced by build.sh). Paths relative to the TrackerXXL root (TXL).
# TXL is set by the caller (build.sh) to the absolute path of <local path> (or wherever the tree lives).
: "${TXL:?TXL must point at the TrackerXXL root}"
CORE="$TXL/core"
PLAT="$TXL/port/platform"

# Core files the DISPLAY agent replaces (the FT812 display stack -> skin bridge). One path per line, relative to core/.
# Empty = the whole Polyend display stack (libs/NewDisplay, libs/FT812) is compiled and driven against the null FT812
# driver (src/NullDisplayDriver.cpp), which is the verified-linking state.
DISPLAY_REPLACED_LIST=""

# Core files replaced by a generated, plugin-adjusted copy (see build.sh gen_core_patched): Tracker.cpp has its external-RAM
# arrays pinned to MCU linker sections (.sample_memory etc.) -> rewritten to .bss.* (NOBITS) so they do not bloat the .so.
CORE_PATCHED="source/Tracker.cpp"

core_files() {  # prints "<kind> <relpath>" for every core source that is part of the build
  ( cd "$CORE"
    for d in source libs interface effector FileManager Exporter AudioEngine synth; do find "./$d" -name '*.cpp'; done | sed 's|^\./||' | sed 's/^/cpp /'
    find ./libs -name '*.c' | sed 's|^\./||' | sed 's/^/c /'
  ) | sort -k2 | while read -r kind f; do
        skip=0
        for r in $CORE_PATCHED $DISPLAY_REPLACED_LIST; do [ "$f" = "$r" ] && skip=1; done
        [ $skip = 0 ] && echo "$kind $f"
      done
}

# Platform sources (this directory): the Linux Platform + the execution model + the mpc_engine glue.
PLATFORM_CPP="src/PlatformLinux.cpp src/NullDisplayDriver.cpp src/FileSystemLinux.cpp src/Runtime.cpp src/EngineGlue.cpp src/FirmwareGlue.cpp"

# Tracker+ synth (port/synth): the open polysynth layer + the voice library (libtrackerxxl_voices.a, built by port/voices/build_voices.sh)
SYNTH_CPP="port/synth/src/PatchImpl.cpp port/synth/src/xxl_hub.cpp port/synth/src/xxl_types.cpp port/synth/src/SynthImpl.cpp"
VOICES_A="$TXL/port/voices/build/libtrackerxxl_voices.a"
# Screen bridge (port/skin/bridge, the display agent's): EngineGlue.cpp calls xxl_scene::set/get, so its sources are part of the link
BRIDGE_CPP=""; for f in scene bridge map_ctl map_pattern map_wave; do [ -f "$TXL/port/skin/bridge/src/$f.cpp" ] && BRIDGE_CPP="$BRIDGE_CPP port/skin/bridge/src/$f.cpp"; done
SYNTH_CPP="$SYNTH_CPP $BRIDGE_CPP"
