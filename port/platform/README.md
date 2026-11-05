# TrackerXXL platform layer (Linux Platform + execution model + VST2 glue)

Port of the Polyend Tracker firmware (`core/`, from polyend/tracker-ce) to an Akai Force / MPC OS VST2 (ARMv7 hard-float,
glibc <= 2.31). This folder is the board-support layer the firmware's `Platform` interface needs, the threading model, and the
glue to the `mpc-vst-plugins` wrapper. The display -> skin bridge is a separate piece that plugs in through `display_hook.h`.

**Status (honest): it compiles and links. It has never run.** No qemu and no device was available; the x86/desktop binary is
deliberately not built. Everything under "Verified" below is compile/link/static-inspection only.

## Build
```
./build.sh              # incremental; CLEAN=1 for from scratch; NOSTRIP=1 keeps symbols; SKIN=1 also runs the framework skin generator
```
Zig (`pip install ziglang`) is the only toolchain; nothing is fetched at build time (the wrapper/tools are vendored in
`third_party/mpc-vst-plugins`, `wrapper/vst2_wrap.c` is the ZYNTH copy with the Q-Link/automation UpdateDisplay fix #486-#489).
Output `build/arm/trackerxxl.so` (package folder name `L'Cronx - VST - TrackerXXL`). The apostrophe is only ever inside double
quotes in `build.sh`, and `build.sh` fails if `PLUG_VENDOR` in the generated `params.h` lost it. `vst.json`: name TrackerXXL,
vendor L'Cronx, uid `TrXL`, `HAS_SONG_POS`, `MODULE_DIR "/sdcard/TrackerXXL"`.

Files
| File | Role |
|---|---|
| `controls.json` | the control set = single source; `tools/gen_controls.py` writes `params.json`, `build/ctl_table.h`, `build/ctl_check.h` (static_asserts that every button index equals the firmware enum) |
| `build_srcs.sh` | source lists: all core `.cpp/.c` (`core_files`), `CORE_PATCHED`, `DISPLAY_REPLACED_LIST` (empty; the display agent lists core files it replaces there), `PLATFORM_CPP` |
| `compile_core.sh` | one-file ARMv7 compile (same flags as /tmp/cc1.sh plus `-O2 -fPIC -fvisibility=hidden`) |
| `src/PlatformLinux.cpp` | Platform + codec/radio/battery/timer/encoder/USB stubs, SwitchGroup, Leds, MIDI queue, ConfigManager, CRC |
| `src/FileSystemLinux.cpp` | SD card -> folder |
| `src/NullDisplayDriver.cpp` | null FT812 + counters + Sink + coprocessor pacing |
| `src/Runtime.cpp` | UI thread, event ring, render block |
| `src/FirmwareGlue.cpp` | the symbols a board must provide (audio-interrupt gate, mute, `sin_q15`), calls into core headers |
| `src/EngineGlue.cpp` | `mpc_engine()`: set_param/midi/song_pos/render mapping |
| `display_hook.h`, `xxl_runtime.h` | public headers for the display agent |
| `STUBS.md` | every stub and deviation |

## Execution model
```
 host (MPC)                                   TrackerXXL
 ----------                                   ----------
 setParameter / MIDI --(wrapper lock)--> EngineGlue --Event ring--> UI THREAD  (nice +5, 8 MB stack, loop ~1 kHz)
 song position/tempo --(song_pos)-------> EngineGlue --MIDI queue-+            Tracker::init() once, then each loop:
                                                                  |             pump events -> onButtonPush/Release/Hold,
 processReplacing --> wrapper --> render() --> renderBlock() <-----+             onPadPush..., onPotChange(0, clicks)
                                      (audio thread)                            SwitchGroup hold/tap timers
                                      gate try-lock (<= 2 ms, else silence)     Platform update, Tracker::update(),
                                      Tracker::updateAudioBuffers(L, R, 128)     xxl_display_bridge_update()
                                        midiUpdate()  <- reads the MIDI queue  TIMER THREAD (0.3 ms poll)
                                        sequencer.tick(128/44100)                scheduled MIDI-clock callbacks (QTMR ISR)
                                        AudioStream::update_all()
```
* The firmware is one set of C++ globals, so **one firmware instance per process**. `create()` claims it; a second simultaneous
  instance is silent and ignores its controls; `destroy()` only detaches (the firmware keeps its state and the UI thread idles at
  50 Hz), the next `create()` re-attaches. Re-running `Tracker::init()` is not supported by the firmware (it pushes new
  AudioConnections every time).
* UI/logic never runs on the audio thread; the audio thread never allocates (the firmware's own audio path is allocation-free) and
  never blocks except the 2 ms gate try-lock.
* **Audio-interrupt gate.** The MCU firmware protects main-loop critical sections with `DISABLE_AUDIO_INTERRUPTS()`; that is a
  mutex here (NVIC semantics, owner-only unlock, no-op inside the audio block). Everything the firmware shares without it, it
  shares like it did between main loop and ISR on one core; on a multicore host those are real data races. This is the main
  correctness risk and the first thing to watch on the device (symptoms: clicks, crashes while editing during playback).
* Sample rate is exactly 44.1 kHz (`kAudioSampleRate` patched in the generated Tracker.cpp copy; MK66 used 44117.647 Hz).
* Boot: `Tracker::init()` takes a while on the UI thread (fonts, themes, project); until it finishes render() outputs silence
  (`stat_notready` counts those blocks).

## Host events -> firmware (controls.json, 59 parameters, append-only order = VST index)
| Parameters | Mapping |
|---|---|
| `wheel` (DATA WHEEL) | MPC data wheel while this control is selected: relative, 0.01 per click -> `onPotChange(0, clicks)`: the active module scrolls the selected row/list or changes the selected value, exactly as the Tracker+ encoder. Endless: at either end the value is put back to 0.5 (`mpc_engine_changed`). `wheel_up` / `wheel_down` are +/-1 click triggers with no end stop. |
| `ql1`..`ql4` (Q-Links) | Relative like the wheel. QL1 = value, 1 click per Q-Link event; QL2 = value coarse, 4 clicks per event; QL3 = focus horizontal (cursor right/left taps, 1 tap per ~5 events); QL4 = focus vertical (down/up). A jump > 0.25 (project load, automation) is treated as a re-sync, not a turn. |
| `btn1`..`btn8` | the 8 soft keys under the screen, `interfaceButton0..7` |
| `cur_up/down/left/right` | `interfaceButtonUp/Down/Left/Right` (26/31/30/32) |
| `play` | `interfaceButtonPlay` (23): the firmware's `functPlayAction` toggles play/stop |
| `stop` | the firmware has no STOP key; presses PLAY only while `sequencer.isPlay()` |
| `shift` | `interfaceButtonShift` (29), a latch: held while the host value is on |
| `edit` | the firmware's REC key (`interfaceButtonRec`, 28) = edit/record mode |
| `copy`, `insert`, `delete` | 24, 25, 27 |
| `scr_*` (15) | the screen buttons 8..22: PARAMS, PERFORM, FILE, CONFIG, MASTER, SAMPLE LOAD/PLAY/EDIT/REC, SONG, NOTE, INSTRUMENT, VOLUME, FX, PATTERN |
| `pad1`..`pad16` + `pad_cols` | 16 pad triggers = a 4x4 window of the firmware's 4x12 grid (pad k, 0 = bottom-left: row `3-k/4`, column `pad_cols*4 + k%4`, row 0 top). Triggers press for 60 ms |
| MIDI notes 36..51 | the same 16 pads with real press/release (note on / note off); every other note and CC goes to the firmware's MIDI input (`handleUsbNoteOn/Off/ControlChange`, so the firmware's own Config -> MIDI "notes in" / channel / CC settings apply) |
| `host_sync` (default ON) | `song_pos()` -> MIDI Start / Song Position + Continue / Stop / 24 ppqn Clock into the firmware's USB MIDI input, and `mtConfig.midi.clkIn/transportIn` are set to USB (user values saved and restored on OFF). The sequencer then follows MPC's play/stop/position and derives tempo from the clock, through the firmware's own external-clock code. OFF = firmware's own clock and PLAY. |

Buttons are *triggers* (`momentary`): press, auto-release after 60 ms, so button *hold* callbacks (`kHold`, 500 ms) only occur for
`shift` (latch) - hold-to-do behaviours of other keys need the host to keep the value high, which a momentary does not. Stated
assumption, easy to change per control in `controls.json` (`"kind": "latch"`).

All geometry/placement is the display agent's; nothing here decides where a control sits.

## Display seam
`display_hook.h`: the null FT812 driver counts and forwards the command stream (`xxl_display::setSink`), paces the coprocessor
(`setCoProLatencyUs`, default 2 ms per list), and the bridge defines `xxl_display_bridge_init/update/shutdown` (weak no-ops in
FirmwareGlue.cpp, so a strong definition just wins at link time). `xxl_runtime.h` exposes button/pad/LED state.
`DISPLAY_REPLACED_LIST` in `build_srcs.sh` is where core display sources go if the bridge replaces them.

## Verified vs not
Verified (by compiling/linking/inspecting, no execution):
* all 232 core sources + generated Tracker.cpp + 6 platform sources + the wrapper compile for `arm-linux-gnueabihf.2.31`
  (`generic+v7a+vfp3d16-d32-neon+thumb2`, hard-float);
* link with `-Wl,--no-undefined`: 0 unresolved symbols; the only dynamic export is `VSTPluginMain`; NEEDED = libm, libc, libdl,
  libpthread, ld-linux-armhf; highest GLIBC version referenced 2.27; ELF32 ARM EABI5 hard-float;
* `.bss` 13.7 MB (sample/delay/reverb/scratch arrays are NOBITS, not in the file), `.so` 2.8 MB stripped;
* controls.json button indices equal the firmware enum (static_assert in FirmwareGlue.cpp); apostrophe survives in `params.h`.

Not verified (needs the device): everything dynamic - boot of `Tracker::init()` under this Platform, the gate/threading, audio output,
input mapping, the wheel on a selected control, host sync, file-system behaviour against what the core expects, the 60 ms trigger
timing, CPU load of the UI loop, `dlopen` with 14 MB of static data. Known approximations: see STUBS.md.

## Assumptions to confirm (A1..A6)
* A1 "Q-Link 1-4 focus params": interpreted as value / coarse value / focus-H / focus-V (above). Change roles in `controls.json`.
* A2 Pad grid orientation: pad index 0 = top-left (inferred from the on-screen keyboard layout in mtKeyboardManager.h).
* A3 `kind: trigger` for all keys except SHIFT.
* A4 The original-Tracker (MK66) build configuration, not Tracker+: no `PLATFORM_IMX`, no `FEATURE_SYNTH` (proprietary Cortex-M7
  library), 11.25 MB mono sample memory.
* A5 `MODULE_DIR` is the absolute `/sdcard/TrackerXXL` as requested (the framework prefers `MODULE_SUBDIR`; it warns).
* A6 MIDI out is dropped (the host ignores it); MIDI notes-in mode is whatever the firmware's config says.

Correction (port/synth): A4 above is superseded. The build is now the Tracker+ configuration (PLATFORM_IMX, FEATURE_SYNTH, 60 MB stereo sample memory) with the open synth in port/synth; see STUBS.md.
