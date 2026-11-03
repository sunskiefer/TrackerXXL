# port/synth: the open polysynth layer (Tracker+ synth for TrackerXXL)

The Tracker+ core (`core/synth/`, `core/libs/Audio/synth_PolySynth.cpp`, `core/AudioEngine`, the Synth editors) talks to the
`polysynth::` SDK. Polyend implements that SDK in the proprietary `libPolySynth.a` / `libPolyDSP.a` (Cortex-M7, not usable
on ARMv7-A, never linked here). This directory re-implements the part of the SDK the core uses, open, on top of the voice
library in `port/voices` (`trackerxxl_voices.h`).

## Layout
| Path | What |
|---|---|
| `include/PolySynth/`, `include/PolyUtil/` | The SDK headers, copied unchanged from `tracker-ce/synth/include` (MIT, LICENSE Part 1/3) except `config.h`: `InstrumentTypeId` gains `kXxlVoiceBase = 100` and `kLastSynth`. |
| `include/3rdparty/` | boost 1.80 subset (config, predef, ...; BSL-1.0, notice in the headers), tiny (optional), fmt (header only), constexpr-string, enum_util.h: copied from `tracker-ce/util/3rdparty`. Licences in `licenses/`. |
| `include/xxl_synth_compat.h` | Force-included: `_GLIBCXX_THROW_OR_ABORT` for tiny/optional.h under libc++. |
| `src/xxl_hub.{h,cpp}` | One `TvInstance` (16 synth slots + 6 drum voices, 44.1 kHz) shared by the three parts, slot allocation, the per-part delay / gate-duck FX unit, drum block rendering. |
| `src/PatchImpl.{h,cpp}` | `polysynth::Patch`: base values, curves, macro modifiers (kAdd / kMult), per-voice velocity/pressure macros, listeners, batch update, file image ("XPAT"). |
| `src/xxl_types.{h,cpp}` | One `InstrumentType` per voice: metadata (sections, parameters, macros), factory patches, the `InstrumentVoice` that drives `tv_note_on/off`. |
| `src/SynthImpl.cpp` | `Synth::createSynth`, instrument (voice allocation and stealing, MIDI, 128-frame render), mixer, patch store, patch pool, type registry. |

Core files edited (everything else in `core/` is untouched): `synth/registry.cpp` (registers the 25 open types + the open CHIP
engine instead of the closed externs), `synth/Synth.h` (`kSynthTypes`: 26 entries), `synth/Synth.cpp` (default part types),
`interface/instrumentEditor/SynthEditor.cpp` and `SynthPatchEditor.cpp` (26 names / bitmaps).

## Instrument types (InstrumentTypeId = 100 + voice; editor order = voice order, CHIP last)
Synth voices (mono slots, polyphony = several slots per part, up to 8): 0 ANALOG, 1 TRIO, 2 FM6, 3 FM4, 4 PHASE, 5 WAVE,
6 CHIP (voice-library chip voice), 7 MACRO, 8 WHEEL, 9 VOICE, 10 PHYS, 11 GRAIN, 12 NOISE, 13 LXR TONE, 14 LXR METAL,
15 BAZZ, 16 DRONE, 17 TEXTURE, 18 FX.
Drum voices (one slot each, monophonic): 19 DRUM1, 20 DRUM2, 21 DRUM3, 22 SNARE, 23 CLAP/CYMBAL, 24 HIHAT.
Plus Polyend's open-source ChiptuneSynth (`core/synth/engines`), kept as "CHIP" (index 25).

Each type shows its 20 controls with the labels and ranges of the voice tables, the shared chain (filter, envelopes, LFO, mixer,
shaper), and the part's delay and gate/duck as an FX section. TIMBRE and MORPH are the first two macros (the voice's
`tv_voice_macro_control`), the other macros are free. Factory patches (starters per voice, BAZZLINE's bass bank on BAZZ, kits for
the drums) are listed with the saved ones.

## Patch files
`Patches/<type name>/<name>.patch` for the typed store, `Workspace/<n>.patch` for the flat store (as `Synth.cpp` expects).
Format: "XPAT", version, type id, base values, macro values / modifiers. Reads happen outside the audio gate, the values are applied
inside it.

## Behaviour notes
* Per part, `setVoiceCount(n)` creates `min(n, maxVoices)` mono voices; a new note takes the same note's voice, else an idle one, else
  the oldest released, else the oldest held (steal).
* Velocity and pressure are per-voice macros (ids 6 and 7). CC 102..107 drive user macros 0..5. CC 120 / 123 release all notes.
* `renderAudioBuffer` is allocation free: voices are created when a patch/voice count is assigned (UI thread, inside the audio gate).
* Drum voices of a part are rendered together in 32-frame engine blocks and cached for the 128-frame block.

## Not implemented (the Tracker does not use it)
Grid, GridMapper(Store), SampleStore / sample slots in patches (`getSample` is null, `loadSample` returns an error), the SDK
sequencer (data stored, never played), MIDI sinks / MIDI-out instrument, the delay / reverb effect instruments (the core mixer's
send effects are the Tracker's own), pitch bend, program change.

## Build
`port/platform/build.sh` compiles `src/*.cpp` with the same flags as the core (`-DPLATFORM_IMX -DFEATURE_SYNTH -DFMT_HEADER_ONLY`,
`-std=gnu++20`) and links `port/voices/build/libtrackerxxl_voices.a` (`port/voices/build_voices.sh`). No x86 build, nothing here has run.
