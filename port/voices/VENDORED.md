# What is vendored, from where, and every local change

All sources are committed files in this directory; nothing is fetched at build time. Upstream trees were read at the
commits below. The checked-out trees under `<local path> were not modified (the 0.5.10 files were taken with
`git archive b449268` into a scratch directory, not by checking that commit out).

| Part | Upstream | Commit | Vendored at |
|---|---|---|---|
| LXRXXL ZYNTH (19 voices, 152 starters, chain) | https://github.com/sunskiefer/LXRXXL-ZYNTH | `9441e6a` | `zynth/` |
| LXRXXL BAZZLINE (bass sound bank, delay, gate/duck) | https://github.com/sunskiefer/LXRXXL-BAZZLINE | `f47ebb6` | `bazz/` |
| LXRXXL 0.5.10 (drum engine glue, kits, FX unit) | https://github.com/sunskiefer/LXR02XXL | `b449268` (0.5.10; NOT 0.6) | `lxr/` |
| LXR02Open (engine DSP) | https://github.com/brendanclarke/LXR02Open | `c896bfd0efe86a4037418bf9f46f1fcc39255958` (file `third_party/lxr02open/VENDORED_COMMIT`) | `third_party/lxr02open/` |
| Braids, Plaits, stmlib | https://github.com/pichenettes/eurorack, https://github.com/pichenettes/stmlib | as vendored by ZYNTH 9441e6a (no separate commit is recorded there) | `third_party/mutable/` |
| DX7 algorithm table | music-synthesizer-for-android (`fm_core.cc`) | n/a (inside ZYNTH's `eng_fm.c`) | `zynth/src/eng_fm.c` |

`third_party/lxr02open/` is byte-identical in ZYNTH 9441e6a and in LXR02XXL b449268. `third_party/mutable/` is a copy of
ZYNTH's. The repository URLs are those of each checkout's `origin`.

## Byte-identical files

`zynth/src/`: voice.c, sound.c, engines.c, eng_osc.c, eng_fm.c, eng_more.c, eng_new.c, mi_bridge.cc, eng.h, eng_dsp.h,
voice_defs.h, starter_defs.h, ids.h. `zynth/VOICE_MAP.md`, `zynth/LICENSE`. `bazz/src/fx.c`, `factory.c`, `ids.h`,
`param_map.h`, `bazz/LICENSE`. `lxr/src/lxr/{lxr_dsp.h, lxr_fx.c, lxr_fx.h, cc_handler.inc, menu_tables.inc}`,
`lxr/src/shim/*`, `lxr/src/{lxr.h, factory_data.c, lxr_state.c, lxr_state.h, meta.c}`, `lxr/LICENSE`, `lxr/PATCHES.md`
(0.5.10's own list of its changes to LXR02Open), everything under `third_party/`. Anything not listed below as changed is
unmodified. (Re-diff against the upstream commits to confirm before relying on this.)

## Local changes

1. `lxr/src/lxr/lxr_dsp.c` -- upstream 0.5.10 file plus **one function appended at the end**:
   `lxr_dsp_render32_split(...)`, a copy of the 32-frame render that keeps each voice's stereo output and the FX-unit
   return apart instead of summing them (so each drum voice can be a separate slot). Guarded by
   `_Static_assert(OUTPUT_DMA_SIZE == 32)`. No existing line was changed.
2. `lxr/src/lxr_initkit.inc` -- new file: `voice_common()` and `lxr_initKit()` extracted verbatim from 0.5.10's
   `lxr.c` (which is the plugin, not vendored), with a header comment saying so. Included by `tv_drum.c`.
3. `zynth/src/bl.h` -- **replacement** of ZYNTH's header, trimmed to what the vendored ZYNTH `.c` files use (`SR`,
   `Sound`, `Voice`, and the prototypes, copied verbatim; `init_sound` prototype added). The original pulls in ZYNTH's
   plugin and file I/O.
4. `zynth/src/param_map.h` -- **replacement**: only `SoundDef`, `OPTS_SP_*` and `SOUND_DEFS`, copied verbatim.
5. `bazz/src/bl.h` -- **replacement**: BAZZLINE's `Fx` struct verbatim and a reduced `Bl` holding only the fx state.
6. `bazz/bass_bank.h` -- **generated** by `tools/gen_bass_bank.py` from BAZZLINE's `src/factory.c` and `src/ids.h`: its
   eight bass sounds (ACID, DB-01, REESE, SUB, FM GROWL, 808, PLUCK, WAVE) re-expressed as ZYNTH BAZZ-voice (ENG_BAZZ)
   settings (OSC_MODE -> V1 = (m+.5)/3, WAVE_POS -> V2, TONE -> V6; V3 .5, V4 .25, V5 0, V7 0, V8 .2, V9 .5, V10-12 0;
   if VCA_DEC <= .02: SUSTAIN 1, VCA_REL .3). **BAZZLINE's own voice.c is not linked**: ZYNTH's `voice.c` carries the
   same chain, so the sounds are played through it. They are approximations, not audited against BAZZLINE's output.
7. `bazz/reference/voice.c`, `bazz/reference/preset.c` -- copies of BAZZLINE's files, **not compiled**, kept so that
   part (b) of the brief is traceable (preset.c is file I/O with no DSP).
8. `zynth/voices.py.reference` -- copy of ZYNTH's `tools/voices.py`, the generator of `voice_defs.h`; not run here.
9. `third_party/mutable/` -- Mutable's own files plus ZYNTH's two marked edits (`plaits/dsp/dsp.h` 44.1 kHz;
   `wavetable_engine.cc` wave map). See `third_party/mutable/LICENSE`.
10. `third_party/lxr02open/` -- LXR02Open with 0.5.10's patches (list in `lxr/PATCHES.md`: PWM waveform, `calcNoise`
    phase-wrap, modulation-node guards, `random.c` and `mixer.c` not built, MIDI CC handler and menu tables extracted).
    Only the files the engine needs are present.
11. New files written for this library: `trackerxxl_voices.h`, `trackerxxl_voices.c`, `tv_internal.h`, `tv_synth.c`,
    `tv_fx.c`, `tv_drum.c`, `build_voices.sh`, `tools/gen_bass_bank.py`, `tests/check_tables.py`, and these documents.

## Not included

* LXR 0.6 (GPL-3 Erica firmware), the sequencer, front panel, plugin shell, storage and UI of any LXRXXL variant.
* BAZZLINE's own `voice.c`/`preset.c` in the build (see 6, 7). ZYNTH's CHAOS/SHIFT effects (`alien.c`).
* A drum choke (the 0.5.10 engine has none; the open/closed hat is handled by the HAT_OPEN note flag).

## Per-instance state of the drum engine

LXR02Open keeps all its state in globals. `lxr/src/lxr_state.c` (0.5.10) gives each instance its own copy by swapping
the two linker sections (`lxrstate_data`, `lxrstate_bss`) under a process-wide recursive mutex around every drum call
(`lxr_state_enter/leave`). The `-include lxr/src/shim/lxr_state.h` flag in `build_voices.sh` is what places the
engine's globals in those sections.

## Additions for the synth layer (port/synth)
* `tv_synth.c` / `tv_internal.h`: `tvs_voice_macro(voice, which)`; `trackerxxl_voices.h/.c`: `tv_voice_macro_control` (which controls are TIMBRE and MORPH) and the `TvFxUnit` API (per-part delay and gate/duck, `tv_fxunit_*`), processing in 128-frame chunks.
* `lxr/src/lxr_state.c`: the `__start_/__stop_lxrstate_*` references are `visibility("hidden")` so the linker-made section symbols are not exported from the plugin (only VSTPluginMain is).
