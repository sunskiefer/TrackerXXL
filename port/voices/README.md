# TrackerXXL voice library

One static ARMv7 library holding, behind a single C API (`trackerxxl_voices.h`):

* the 19 LXRXXL ZYNTH synth voices, their 152 starter sounds and the shared chain (FM, SUB, NOISE, swarm, DRIVE,
  Polivoks/diode filter, ADSRs, pitch envelope, accent/slide);
* BAZZLINE's bass bank (8 sounds), its delay and its gate/duck;
* the LXRXXL 0.5.10 drum engine on LXR02Open: DRUM1, DRUM2, DRUM3, SNARE, CLAP/CYMBAL, HIHAT, the 65 kits (64 factory
  kits including "Lambo" plus the init kit) and the LXR-02 FX unit.

It is the voice layer for the Polyend Tracker port to Akai Force / MPC OS (VST2). **Licence: LXR02 Open-Source
(non-commercial, complete source with binaries); see `LICENSES.md`.** Provenance and local changes: `VENDORED.md`.

## Build

    ./build_voices.sh          # ZIG="python3 -m ziglang" by default; OUT=build

Produces `build/libtrackerxxl_voices.a` and `build/compile_report.txt`. Target is
`arm-linux-gnueabihf.2.31`, `-mcpu=generic+v7a+vfp3d16-d32-neon+thumb2`, `-ffp-contract=off`, `-fPIC`,
`-fvisibility=hidden` (only `tv_*` is exported when linked into a shared object). Needs Zig's clang: the LXR engine
files use `-include lxr/src/shim/lxr_state.h`, which relies on clang's `#pragma clang section`. C++ parts are built
with no exceptions, RTTI or thread-safe statics, so libc++ is not required. `tests/check_tables.py` is a static check
of the tables in the sources (run with `python3 -I`).

## Use

    TvConfig c; tv_config_init(&c);       /* synth_slots 16, drum_voices 6, sample_rate 44100 */
    c.sample_rate = 48000;
    TvInstance *t = tv_create(&c);               /* allocates everything; nothing is allocated after this */
    tv_slot_set_voice(t, 0, 7);           /* synth slot 0 plays voice 7; ids 0..18 synth, 19..24 drums */
    tv_load_starter(t, 0, 7, 5);      /* slot, voice, starter idx (0 = home sound) */
    tv_set_param(t, 0, 3, 0.6f);          /* control V4, 0..1 */
    tv_note_on(t, 0, 60, 100, 0);         /* slot, midi note, velocity, flags */
    tv_render_mix(t, L, R, n);            /* overwrites L/R, or per slot: */
    tv_render_slot(t, 0, L, R, n);        /* ADDS into L/R */
    tv_note_off(t, 0);
    tv_destroy(t);

The exact names and signatures are in the header; the groups are: create/destroy/reset/sample rate/tempo; slot and
voice queries; the 20-control table per voice (`tv_control_name/label/default/steps/text`, `tv_set_param/get_param`);
the shared chain (`tv_chain_*`, `tv_set_chain`); starters (`tv_starter_*`, `tv_load_starter`), bass bank
(`tv_bass_bank_*`) and kits (`tv_kit_*`, `tv_load_kit`); raw drum parameters; notes (accent, slide, no-pitch-env,
no-mod, velocity-accent, hat-open flags); mixer (send, mute, solo, master); delay and gate/duck; render.

Layout of slots: synth slots `0..synth_slots-1` (default 16, at most 32), each mono and able to play any of the 19
voices; the drum slots follow in the order DRUM1, DRUM2, DRUM3, SNARE, CLAP/CYMBAL, HIHAT (voice ids 19..24). Every
slot has independent state. Drum note 63 plays the voice at its own pitch.

Tables for the host UI: voice names (long and short), per-voice control names and panel labels, defaults, number of
steps for stepped controls, starter names, bass bank names, kit names, chain and FX parameter names.

## Contract and limits

* Render never allocates or does I/O. `tv_create` does the one-time table setup (under `pthread_once`) and all allocation. Use **either** `tv_render_mix` **or** per-slot `tv_render_slot` (plus `tv_render_drum_fx` for the
  FX unit return) for a given block, not both: they consume the same internal rings.
* Threads: one instance must not be called from two threads at once. Several instances may run in different threads:
  the drum engine keeps its globals in per-instance sections swapped under a process-wide recursive mutex, so
  instances serialise on that lock while they render drums. Only the synth path is lock-free. (Not tested.)
* Sample rate: all engines run at 44.1 kHz. Other rates use a 4-point Hermite resampler per stream: allocation-free,
  2 frames of latency, no anti-alias filter when the host rate is below 44.1 kHz.
* Drum timing is quantised to the engine's 32-frame block (a note may start up to 31 frames late). The drum engine
  has no choke; `tv_slot_active` for drums is heuristic (silent for 0.25 s).
* The PRNG state in stmlib is shared between instances (noise streams are not independent across instances).
* Block size up to `TV_MAX_BLOCK` (2048) frames per call.

## Verification status

Everything was compiled for ARMv7, all objects, and a whole-archive link into a shared object had no undefined
symbols. **Nothing was run**: no audio, no CPU measurement, no listening. See the notes at the end of `VENDORED.md`
and the task report for what could not be verified offline.
