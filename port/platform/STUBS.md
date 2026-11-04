# TrackerXXL platform: what is real, what is a stub

Every symbol the firmware (core/, 232 sources + a plugin-adjusted copy of source/Tracker.cpp) needs resolves at link time
(`-Wl,--no-undefined`). "Real" = an implementation that does what the hardware version does for the plugin; "stub" = a deliberate
no-op/constant. Nothing here has run on a Force yet.

## Link-level symbols the firmware expects from the board support package
| Symbol | Where | Status |
|---|---|---|
| `enableAudioInterrupt()` / `disableAudioInterrupt()` | FirmwareGlue.cpp | Real: a gate mutex between the UI thread and the audio block (NVIC semantics: non-counting, owner-only unlock, no-op inside the audio block). |
| `muteAudioOutByKey()` / `unmuteAudioOutByKey()` / `isAudioOutMuted()` | FirmwareGlue.cpp | Real flag; the render block outputs silence while muted (the codec mute of hardware.cpp). Starts unmuted. |
| `sin_q15(int16_t)` | FirmwareGlue.cpp | Real: CMSIS `arm_sin_q15` algorithm (512-entry table built at first use, linear interpolation), no CMSIS dependency. |
| `xxl_display_bridge_init/update/shutdown` | FirmwareGlue.cpp | Weak no-ops: the display agent defines the strong versions (display_hook.h). |
| `mpc_engine()`, `mpc_engine_changed()` | EngineGlue.cpp | Real (wrapper contract). |

## Platform classes (core/include/Platform.h)
| Class | Status |
|---|---|
| `Platform` | Real container. `setPowerMode`, `resetMCU`, `runBootloader` are no-ops (no power rail / MCU / bootloader on a plugin); `getPowerButtonState` false; `readForceStartUp` true. |
| `Sgtl5000` (codec) | Stub: every setter ignored (the Force's own mixer does the job of master volume / EQ / bass enhance - the firmware's *software* master enhancer in AudioEngine/ still runs). |
| `Radio` | Stub: not initialised, no stations (`isinitialized()` false). |
| `SwitchGroup` buttons (128) and pads (48) | Real: state, press/release/hold callbacks (hold time from `setHoldTime`), trigger auto-release (`tap_ms` in controls.json). Fed by the host (EngineGlue.cpp). |
| `Leds` | Real state store (`xxl::ledLevel(i)`) - nothing is drawn; the pad backlight colours are for the display bridge. |
| `Battery` | Stub: 100 %, not charging, never empty. |
| `Timer` | Real: CLOCK_MONOTONIC; `clockCycles()` counts nanoseconds (`clockCyclesPerUs()` = 1000); `scheduleMidiClockUs` runs on the timer thread. |
| `Encoder` | Stub (acceleration/resolution stored). The host's data wheel is delivered as clicks straight to `onPotChange(0, clicks)`; acceleration is the host's. |
| `UartMidi` | Stub: no DIN jack. Output dropped (counted). `isReadAvailable()` false. |
| `UsbMidi` | Real input queue: host notes/CC and the transport (start/continue/stop/song position/24 ppqn clock) arrive here and are read by the firmware's `midiUpdate()` in the audio block. Output dropped and counted: MPC ignores a VST's MIDI out. |
| `UsbAudio`, `Usb` | Stub (no USB device mode). |
| `ConfigManager` | Real: the EEPROM is the file `<root>/trackerConfig` (same name as Tracker+). |
| `CrcCalculator` | Real CRC-32 (IEEE, same as the MK66's FastCRC32, so project files are interchangeable; Tracker+ returns 0). `datalen` is a `uint16_t` as in the interface. |
| `FileSystem` | Real on a folder (see below). |
| `DisplayDriver` | Null FT812: accepts and counts everything, coprocessor always empty, reads return zeros, `getImgProps` reports 0x0 (LOADIMAGE has no decoder behind it). |

## FileSystem deviations from FatFs
* Root `<root>` = `/sdcard/TrackerXXL` (MODULE_DIR), created with `Projects/ Samples/ Themes/ Fonts/` on first start; `TRACKERXXL_ROOT` overrides it.
* Case-insensitive path fallback (FAT semantics on ext4); directory listings are sorted by name (FatFs returns on-disk order) because the core pages by `skip_count`.
* "Hidden": names starting with `.` and folders created with `mkdir(hidden=1)` (recorded in `<root>/.xxl_hidden`).
* `enableFastSeek` returns false, `readSectorsAsync`/`pollAsyncRead`/`waitAsyncRead`/`isAsyncReadBusy` are inert: the SD DMA streaming path of Tracker+ is not available; the core falls back to its synchronous reads (`hasValidClmt == false`).
* `enListFileFilter_firmware` matches nothing (no firmware updates through the card).
* `mkdir(pFlag)` always creates parents; `isSdCardInserted()` is "the root folder exists"; the mount-event callback is never fired.

## Firmware-level deviations (all in generated/adjusted copies, core/ itself is untouched by the platform work)
* `source/Tracker.cpp` is used as a generated copy (`build/gen/Tracker.cpp`, sed in build.sh): the four external-RAM arrays (sample 11.25 MB, delay, reverb, scratch) are pinned to `.bss.xxl_*` instead of MCU linker sections (they would be PROGBITS and add 13.6 MB to the .so), and `kAudioSampleRate` is 44100 instead of the MK66's 44117.647 (the host runs 44.1 kHz exactly).
* Built as the **Tracker+ configuration** (`PLATFORM_IMX` + `FEATURE_SYNTH`). The Tracker+ synth engine of the real hardware is Polyend's proprietary prebuilt `libPolySynth.a` (Cortex-M7); it is **not** in this repository and **not** linked, see the next point.

## Tracker+ configuration (port/synth) -- supersedes the "Built as the original Tracker" bullet above
* Built as **Tracker+**: `-DPLATFORM_IMX -DFEATURE_SYNTH -DFMT_HEADER_ONLY`, `-std=gnu++20` (16 tracks, 60 MB stereo sample memory in `.bss`, synth parts). No i.MX hardware code runs: everything under `PLATFORM_IMX` compiles and links against this Platform; hardware-only drivers are the same stubs as before.
* `polysynth::` is the open implementation in `port/synth/src` (voice library `port/voices`), NOT Polyend's `libPolySynth.a` / `libPolyDSP.a` (never linked). Types: 19 ZYNTH-family synth voices + 6 LXR drum voices + the open ChiptuneSynth (26). Polyend's factory engines (ACD, FAT, VAP, WTFM, PERC, DIRT, MIDI-out) do not exist; icons of the existing engines are reused for the new types.
* Not implemented: Grid, GridMapper(Store), SampleStore / patch sample slots, the SDK sequencer, MIDI sinks, pitch bend. `createGrid/createSampleStore/...` return null; the core does not call them in the Tracker+ paths exercised here (unverified, nothing was run).
* Core files edited: `synth/registry.cpp`, `synth/Synth.h`, `synth/Synth.cpp` (default part types), `interface/instrumentEditor/SynthEditor.cpp`, `SynthPatchEditor.cpp`.
* `FileSystemLinux::resolve` now maps `\` to `/` (`Synth::copyWorkspacePatches` builds backslash paths).
* The pad back-light logic in `fIinstrEdit.cpp` still tests `kDrumSynth` (cosmetic: the drum voices get no percussion section lights).
* Two fixes to the display agent's files so the link works: `EngineGlue.cpp` includes `xxl_scene.h` outside `extern "C"`; `bridge_core.h` includes the C++20 headers before its `#define private public`. The bridge sources are compiled and linked by `build_srcs.sh` (`BRIDGE_CPP`).
