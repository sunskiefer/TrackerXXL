/* TrackerXXL voice library -- the C API.
 *
 *   * 19 synth voices (LXRXXL ZYNTH, 9441e6a) with their shared chain (FM, SUB, NOISE, swarm, DRIVE, Polivoks /
 *     diode filter, ADSRs, pitch envelope, accent / slide, modulator), 152 starter sounds (19 voices x 8);
 *   * LXRXXL BAZZLINE's eight factory bass sounds (a bank on the BAZZ voice), its delay and its gate/duck;
 *   * the LXRXXL 0.5.10 drum engine on LXR02Open: DRUM1, DRUM2, DRUM3, SNARE, CLAP/CYMBAL, HIHAT, 65 kits.
 *
 * Threading: all calls on one instance must come from one thread at a time (the audio thread, or the UI thread while the
 * audio is stopped); different instances may run on different threads (the drum engine's global state is swapped per
 * instance under a lock, see VENDORED.md). tv_create / tv_destroy may be called from any thread but not while another
 * thread is inside tv_create. Nothing allocates after tv_create (tv_set_sample_rate does not allocate either).
 *
 * Slots. A slot is one independent voice with its own sound and state.
 *   slots 0 .. synth_slots-1                  monophonic synth tracks (any of the 19 synth voices, tv_slot_set_voice)
 *   slots synth_slots .. synth_slots+drums-1  the drum voices in order DRUM1 DRUM2 DRUM3 SNARE CLAP/CYMBAL HIHAT
 *                                             (fixed voice per slot; the six voices share one engine and one kit)
 * Voice ids (tv_voice_*): 0..18 the synth voices, 19..24 the drum voices.
 *
 * Rendering. The engines run at 44.1 kHz (the voices' tables and time constants are in that rate). Any other rate set
 * with tv_set_sample_rate() is served by a 4-point Hermite resampler on every stream (adds two frames of latency, no
 * anti-alias filter when the output rate is below 44.1 kHz); at 44100 nothing is resampled. All frame counts are
 * unrestricted up to TV_MAX_BLOCK. Per audio block use EITHER tv_render_mix() OR tv_render_slot() for every slot (plus
 * tv_render_drum_fx()); never both (the drum engine would be asked for the same audio twice). */
#ifndef TRACKERXXL_VOICES_H
#define TRACKERXXL_VOICES_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TV_API
#define TV_API __attribute__((visibility("default")))
#endif

#define TV_VERSION_STRING "0.1.0"
#define TV_NUM_SYNTH_VOICES 19
#define TV_NUM_DRUM_VOICES  6
#define TV_NUM_VOICES       (TV_NUM_SYNTH_VOICES + TV_NUM_DRUM_VOICES)
#define TV_NUM_CONTROLS     20       /* V1..V20 of every voice, index 0..19, value 0..1 */
#define TV_STARTERS_PER_SYNTH_VOICE 8   /* index 0 = the voice's home sound */
#define TV_MAX_SYNTH_SLOTS  32
#define TV_MAX_BLOCK        2048     /* frames per render call */
#define TV_DEFAULT_SYNTH_SLOTS 16

typedef struct TvInstance TvInstance;

typedef struct {
    int synth_slots;     /* 0..TV_MAX_SYNTH_SLOTS synth slots (tv_config_init: 16) */
    int drum_voices;     /* 0..6 drum slots, the first n of DRUM1 DRUM2 DRUM3 SNARE CLAP/CYMBAL HIHAT (tv_config_init: 6);
                            0 leaves the drum engine out altogether */
    float sample_rate;   /* output rate, 8000..192000; 0 means 44100 */
} TvConfig;

/* note flags */
#define TV_NOTE_ACCENT      1u   /* accented note (the 303-style accent, sweep, filter decay) */
#define TV_NOTE_SLIDE       2u   /* legato slide from the held note (call tv_note_on without tv_note_off first) */
#define TV_NOTE_NO_PENV     4u   /* leave out the pitch envelope for this note (default: on) */
#define TV_NOTE_NO_MOD      8u   /* leave out the modulator for this note (default: on) */
#define TV_NOTE_VEL_ACCENT  16u  /* accent when velocity > 100 (as the ZYNTH keyboard does) */
#define TV_NOTE_HAT_OPEN    32u  /* drum slot HIHAT: the open hat (default: closed) */

/* ------------------------------------------------------------------ instance */
TV_API void tv_config_init(TvConfig *cfg);                     /* 16 synth slots, 6 drum voices, 44100 */
TV_API TvInstance *tv_create(const TvConfig *cfg);             /* cfg may be NULL (defaults); NULL on failure */
TV_API void tv_destroy(TvInstance *t);
TV_API void tv_reset(TvInstance *t);                           /* every slot to its default sound, silent; init kit; fx reset */
TV_API int tv_set_sample_rate(TvInstance *t, float sr);        /* 0 ok, -1 out of 8000..192000 */
TV_API float tv_get_sample_rate(const TvInstance *t);
TV_API void tv_set_tempo(TvInstance *t, float bpm);            /* synced LFOs / modulators, the delay's beat, the drum LFOs; default 120 */
TV_API int tv_slot_count(const TvInstance *t);                 /* synth + drum slots */
TV_API int tv_synth_slot_count(const TvInstance *t);
TV_API int tv_drum_slot_count(const TvInstance *t);
TV_API int tv_slot_is_drum(const TvInstance *t, int slot);

/* ------------------------------------------------------------------ voices and tables (no instance needed) */
TV_API int tv_voice_count(void);                               /* 25 */
TV_API const char *tv_voice_name(int voice);                   /* "FM6", "LXR METAL", "DRUM 1", ... */
TV_API const char *tv_voice_short_name(int voice);             /* three characters */
TV_API int tv_voice_is_drum(int voice);
TV_API int tv_slot_voice(const TvInstance *t, int slot);       /* the voice id a slot plays, -1 bad slot */
TV_API int tv_slot_set_voice(TvInstance *t, int slot, int voice);   /* synth slots, voice 0..18; the slot keeps each
                                                                  voice's last sound; 0 ok, -1 not allowed */

/* the 20 controls (V1..V20) of a voice: names, panel labels (two lines split by '|'), defaults, steps (0: continuous,
 * n: an n-option control, value = (option + 0.5) / n), the value's display text */
TV_API int tv_voice_macro_control(int voice, int which);       /* synth voices: the control (0-based) the front panel's TIMBRE
                                                                  (which 0) or MORPH (1) moves; -1 for the drum voices */
TV_API const char *tv_control_name(int voice, int ctl);
TV_API const char *tv_control_label(int voice, int ctl);
TV_API float tv_control_default(int voice, int ctl);
TV_API int tv_control_steps(int voice, int ctl);
TV_API void tv_control_text(int voice, int ctl, float value, char *buf, int buf_len, int full);
TV_API int tv_set_param(TvInstance *t, int slot, int ctl, float value01);   /* 0 ok */
TV_API float tv_get_param(const TvInstance *t, int slot, int ctl);

/* the shared chain of a synth slot (filter, drive, envelopes, ...): 38 parameters. Values are natural: 0..1 for a
 * continuous one, the option index 0..n-1 (as a float) for an option one (tv_chain_options() > 0). */
TV_API int tv_chain_count(void);
TV_API const char *tv_chain_key(int idx);                      /* "cutoff" */
TV_API const char *tv_chain_name(int idx);                     /* "CUTOFF" */
TV_API float tv_chain_default(int idx);
TV_API int tv_chain_options(int idx);
TV_API const char *tv_chain_option_name(int idx, int opt);
TV_API int tv_set_chain(TvInstance *t, int slot, int idx, float value);
TV_API float tv_get_chain(const TvInstance *t, int slot, int idx);
TV_API int tv_set_level(TvInstance *t, int slot, float v01);   /* synth: the sound's LEVEL; drum: the voice's fader */
TV_API int tv_set_pan(TvInstance *t, int slot, float v01);     /* 0 left .. 0.5 centre .. 1 right */

/* starter sounds. Synth voice: 8 per voice (index 0 = the home sound). Drum voice: the 65 kits (see tv_kit_*): loading
 * takes that one voice's sound from the kit into the working kit (the other voices keep theirs). */
TV_API int tv_starter_count(int voice);
TV_API const char *tv_starter_name(int voice, int idx);
TV_API int tv_load_starter(TvInstance *t, int slot, int voice, int idx);  /* synth slot: switches the slot to the voice.
                                                                       Drum slot: voice must be the slot's own */
/* BAZZLINE's bass sounds (on the BAZZ voice; switches the slot to BAZZ) */
TV_API int tv_bass_bank_count(void);                           /* 8 */
TV_API const char *tv_bass_bank_name(int idx);
TV_API int tv_load_bass_bank(TvInstance *t, int slot, int idx);

/* drum kits: 64 factory kits (the LXR-02 "Hrtl" DLC project) + the init kit (index 64) */
TV_API int tv_kit_count(void);                                 /* 65 */
TV_API const char *tv_kit_name(int kit);                       /* unique names ("Lambo", "Initkit 17", ... "Init") */
TV_API int tv_load_kit(TvInstance *t, int kit);                /* all six drum voices (and the LXR-02 FX unit's settings) */
TV_API int tv_drum_set_raw(TvInstance *t, int par, int value); /* firmware parameter 0..227, value 0..127 (options: index) */
TV_API int tv_drum_get_raw(const TvInstance *t, int par);
TV_API int tv_drum_set_ext(TvInstance *t, int ext, int value); /* LXR-02 extras: 0..5 pulse width of voice n, 6.. the FX unit */
TV_API int tv_drum_get_ext(const TvInstance *t, int ext);

/* ------------------------------------------------------------------ notes */
TV_API void tv_note_on(TvInstance *t, int slot, int midi_note, int velocity, unsigned flags);
                                  /* velocity 1..127 (0 acts as tv_note_off). Drum slots: midi_note 63 = the voice's own
                                     pitch, other notes transpose (the LXR convention) */
TV_API void tv_note_off(TvInstance *t, int slot);              /* releases a synth slot's gate; drums ignore it */
TV_API void tv_all_notes_off(TvInstance *t);
TV_API void tv_slot_kill(TvInstance *t, int slot);             /* silence at once (a click is possible) */
TV_API int tv_slot_active(const TvInstance *t, int slot);      /* sounding, or a tail still running */

/* ------------------------------------------------------------------ mixer (used by tv_render_mix) */
TV_API void tv_set_send(TvInstance *t, int slot, float v01);   /* the slot's send to the delay, default 1 */
TV_API void tv_set_mute(TvInstance *t, int slot, int on);
TV_API void tv_set_solo(TvInstance *t, int slot, int on);
TV_API void tv_set_master(TvInstance *t, float gain);          /* linear, default 1 */

/* ------------------------------------------------------------------ BAZZLINE's delay and gate/duck (on the mix) */
TV_API int tv_fx_param_count(void);                            /* 10 */
TV_API const char *tv_fx_param_key(int idx);
TV_API const char *tv_fx_param_name(int idx);
TV_API float tv_fx_param_default(int idx);
TV_API int tv_fx_param_options(int idx);
TV_API const char *tv_fx_param_option_name(int idx, int opt);
TV_API int tv_fx_set_param(TvInstance *t, int idx, float value);   /* natural values, as tv_set_chain */
TV_API float tv_fx_get_param(const TvInstance *t, int idx);
TV_API void tv_fx_set_sync(TvInstance *t, int mode);           /* 0 free (TIME = 0..800 ms), 1 the tempo's beat (TIME picks
                                                                  /8../x8), 2 the tapped period */
TV_API void tv_fx_set_tap_period(TvInstance *t, float seconds);
#define TV_FX_HOLD 0
#define TV_FX_ADD 1
#define TV_FX_REVERSE 2
TV_API void tv_fx_set_toggle(TvInstance *t, int which, int on);
TV_API void tv_fx_set_gate_duck_on(TvInstance *t, int on);     /* default off */
TV_API void tv_fx_duck_trigger(TvInstance *t);                 /* fires the gate/duck envelope */
TV_API float tv_fx_delay_seconds(TvInstance *t);               /* the time in use */

/* ------------------------------------------------------------------ the delay and gate/duck as a stand-alone insert
 * (added for the Tracker port: one unit per synth part, outside any TvInstance). Same parameters as tv_fx_*; rendering is
 * allocation-free, tv_fxunit_create allocates two 131072-float buffers. In place on L / R at 44100 Hz; the delay's input is
 * the block itself (a full send). */
typedef struct TvFxUnit TvFxUnit;
TV_API TvFxUnit *tv_fxunit_create(void);
TV_API void tv_fxunit_destroy(TvFxUnit *f);
TV_API void tv_fxunit_reset(TvFxUnit *f);
TV_API int tv_fxunit_set_param(TvFxUnit *f, int idx, float value);   /* natural values as tv_fx_set_param */
TV_API void tv_fxunit_set_sync(TvFxUnit *f, int mode);
TV_API void tv_fxunit_set_tempo(TvFxUnit *f, float bpm);
TV_API void tv_fxunit_set_gate_duck_on(TvFxUnit *f, int on);
TV_API void tv_fxunit_duck_trigger(TvFxUnit *f);
TV_API void tv_fxunit_process(TvFxUnit *f, float *L, float *R, int frames);   /* frames <= TV_MAX_BLOCK */

/* ------------------------------------------------------------------ render */
/* Adds `frames` frames of one slot into L and R (the slot's own level and pan applied; no send, mute, solo or master). */
TV_API void tv_render_slot(TvInstance *t, int slot, float *L, float *R, int frames);
/* Adds the LXR-02 FX unit's return (the drum voices routed to the FX bus). Needed with tv_render_slot() only. */
TV_API void tv_render_drum_fx(TvInstance *t, float *L, float *R, int frames);
/* OVERWRITES L and R with every slot mixed (mute, solo, send), the drum FX return, the delay and gate/duck, master. */
TV_API void tv_render_mix(TvInstance *t, float *L, float *R, int frames);

#ifdef __cplusplus
}
#endif
#endif
