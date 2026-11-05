# Building TrackerXXL from source

Target: Akai Force / MPC OS, ARMv7 hard-float VST2, glibc <= 2.31. Needs Python 3 and Zig (`pip install ziglang`); no other toolchain.
Run from `port/`:

1. `bash voices/build_voices.sh` - the synth/drum voice library (LXRXXL, ZYNTH, BAZZLINE voices) -> `voices/build/libtrackerxxl_voices.a`
2. `python3 platform/tools/gen_controls.py` - control table from `platform/controls.json`
3. `python3 skin/gen/build_skin.py` - generates the Force skin (one tab per screen) and checks it (`skin_check rc=0`)
4. `bash own/build.sh` - compiles the engine and plugin -> `platform/build/arm/trackerxxl.so`
5. Package: `MPC_RELEASE_TOOLS=<mpc-vst-plugins tools dir> bash package_release.sh <version>` -> `dist/TrackerXXL-<version>-mpc-armv7.zip`
   (use the L'Cronx-patched mpc-vst-plugins tools, sunskiefer/mpc-vst-plugins @ 3845bea)

Layout: `own/src` engine and UI (`app.cpp` screens and controls, `audio.cpp` sample engine, `glue.cpp` plugin glue), `skin/gen` skin generator,
`platform` controls and wrapper, `voices` synth/drum voices, `own/test/harness.cpp` offline harness (development only).
Status: 0.5.3 is work in progress and not verified on a Force. Problems: attach `trackerxxl.log` from the plugin folder.
