# TrackerXXL porting notes

## Patches to the Polyend source (core/)
- P1: libs/NewDisplay/displayStructs.h: removed the embedded `extern "C" memcpy` declaration (clashes with glibc); includes <string.h> instead.
- P2: libs/NewDisplay/textBox.cpp: `char*` -> `const char*` for two strchr results (C++ overloads).
- P3: interface/sampleRecorder/fSampleRec.cpp: `case SR->X:` -> `case cSampleRecorder::X:` (constant expressions).

## Platform layer (port/platform/) - written by the platform agent
- PL1: No core source was modified for the platform. Two adjustments are applied to a *generated copy* of `source/Tracker.cpp` (build.sh sed -> `port/platform/build/gen/Tracker.cpp`, the original is excluded from the build): (a) the four external-RAM arrays' `section(".sample_memory"/".delay_memory"/".reverb_memory"/".scratch_memory")` become `.bss.xxl_*` (otherwise PROGBITS, +13.6 MB in the .so); (b) `kAudioSampleRate` 44117.6470588f -> 44100.0f (the host is exactly 44.1 kHz).
- PL2: Build configuration is the original Tracker (MK66-class): no `-DPLATFORM_IMX`, `-DFEATURE_SYNTH`, `-DKINETISK`, `-DTRACKER_MINI` (synth lib is Cortex-M7 proprietary; KINETISK selects Cortex-M DSP inline asm). All 232 core files + generated Tracker.cpp compile with `-O2 -fPIC -fvisibility=hidden -DNDEBUG -DXXL_PLUGIN=1`.
- PL3: The firmware expects these from the board, now provided in port/platform/src/FirmwareGlue.cpp: `enableAudioInterrupt/disableAudioInterrupt` (audio gate), `muteAudioOutByKey/unmuteAudioOutByKey/isAudioOutMuted`, `sin_q15` (CMSIS arm_sin_q15 algorithm). Input callbacks `onButtonPush/Release/Hold`, `onPadPush/Release/Hold`, `onPotChange` are core functions (source/mtActions.cpp) that the platform's SwitchGroups/encoder call on the UI thread.
- PL4: Execution model: UI thread = main() super-loop (Tracker::init then pump events + Tracker::update every ~1 ms); audio = host render() -> exactly one `Tracker::updateAudioBuffers(L,R,128)` per block (MIDI in -> sequencer.tick -> AudioStream::update_all); timer thread = scheduled MIDI clock. One firmware instance per process (C++ globals); destroy() detaches, create() re-attaches, `Tracker::init` is never re-run.
- PL5: Host sync works through the firmware's own MIDI-sync receive path: song_pos() pushes Start/SongPosition+Continue/Stop/24 ppqn Clock into the USB MIDI queue and `mtConfig.midi.clkIn/transportIn` are forced to `clockIn_Usb` (saved/restored). `Sequencer::handleMidiInStart/Stop/Clock` are the consumers (mtSequencer.cpp ~3541-3700).
- PL6: Display: `DisplayDriver` is a null FT812 (counts, optional Sink, coprocessor busy for 2 ms per list so display.update() is paced). The bridge defines `xxl_display_bridge_init/update/shutdown` (display_hook.h); core display sources it replaces go into `DISPLAY_REPLACED_LIST` in port/platform/build_srcs.sh.
- PL7: FileSystem: folder `/sdcard/TrackerXXL`; fast-seek/async SD reads unavailable (VoiceStreamManager falls back to synchronous reads); directory listings sorted by name; case-insensitive lookups; hidden = leading '.' or `mkdir(hidden=1)` registry. Firmware-update filter matches nothing.
- PL8: Link result: `build/arm/trackerxxl.so`, `--no-undefined`, 0 unresolved, only `VSTPluginMain` exported, highest GLIBC 2.27, NEEDED libm/libc/libdl/libpthread/ld-linux-armhf. Never executed.

## S. Skin and scene bridge (port/skin, 2026-11-03)

Skin-only UI: the bridge (port/skin/bridge, C++) turns every drawn control of the Polyend display into a flat table of numbered slots; the generator (port/skin/gen, python) emits vst.json, params.json, TUI.json, the layout file and the numbered overlays. Nothing was run on a Force, in an emulator or on x86.

Build: `python3 -I port/skin/gen/build_skin.py --views core --detail low` then `port/skin/bridge/build_bridge.sh` (ARMv7 compile-only, Zig, glibc 2.31). The five bridge objects also link without undefined symbols against the platform objects. skin_check.py rc=0.

Numbers (core, low): 1983 parameters (59 platform + 1924 scene slots), 7502 widgets (3268 text, 1796 image, 1567 filmstrip, 16 button, 855 enum), TUI.json 3.5 MB, 290 layout controls. `--views all` (all 12 pattern view modes) was 16,973 widgets / 7.5 MB and is not the default.

Text slots: each text is a pair `<key>` (state) and `<key>_t` (16-bit FNV hash, display = the text), because MPC repaints a label only when its bound value changes. vst.json defines HAS_DISPLAY_REV=1, DISPLAY_REV_NO_UPDATE=1, PARAM_TEXT_MAX=64.

S5 hook (not applied, platform is outside port/skin): in EngineGlue eng_get_param / eng_set_param call `xxl_scene::get/set` before `findCtl`; include bridge/include/xxl_scene.h and call xxl_display_bridge_init/update/shutdown from display_hook.

Done: pattern editor (15 rows, cursor row 7, 6 cell states, popups, track-end line, playhead band, title/timer, 8 buttons), pooled labels/bars/lists (including cParamValueList and NewList value column)/frames, waveform picture columns plus marker layer, touch by cursor-key emulation, Q-Link anchors.
Partial / not done: cMultiLabel (performance mode), cHorizontalBar, cHorizontalTracker, cSongPlayer, cImg/ImgGrid, cGridEditorPopup, cNotePopout, cNotesKeyboard, cBoxMenu, cBattery, cStartScreen, SimpleBox, cWtProgress; patternInfo layouts, horizontal pattern arrangement, partial-track playhead, selection outline, vertical text; 10 non-default pattern view modes; only 30 of 33 icons exist on disk, icons in label bitmaps and model-list rows not placed; Roboto stands in for Roboto Mono; waveform quantised to 6 px, markers 4 px; the zoomed line waveform is drawn as bars.
Untested: whether MPC refreshes label text on Automate alone (fallback: drop DISPLAY_REV_NO_UPDATE), the 7.5k-widget budget on a Force, key-emulated touch, rate limiting (33 ms), the handle mapping on real firmware. DRM overlay stays the last-resort fallback.
- PL9: port/platform/src/EngineGlue.cpp: eng_set_param/eng_get_param call xxl_scene::set/get first (screen bridge hook).
- PL10 (synth): Tracker+ configuration enabled (PLATFORM_IMX + FEATURE_SYNTH, gnu++20). Open polysynth implementation in port/synth backed by port/voices; the proprietary libPolySynth.a/libPolyDSP.a are never linked. 26 instrument types (19 synth voices, 6 drum voices, CHIP). Linked trackerxxl.so: 0 unresolved symbols, exports only VSTPluginMain. Not run anywhere (no emulator/Force); see port/synth/README.md and port/platform/STUBS.md.
