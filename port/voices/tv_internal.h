/* TrackerXXL voice library -- internal interfaces between the four translation units. Each of tv_synth.c, tv_fx.c and
 * tv_drum.c is compiled against a different vendored header set (ZYNTH, BAZZLINE, LXR), whose type and enum names collide
 * (Voice, Sound, SP_*, G_*, Kit...), so they only meet through this header, which has no engine types in it. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define TV_INTERNAL_SR 44100.0f      /* every vendored engine runs at 44.1 kHz; other rates are resampled (tv_main) */

/* ------------------------------------------------------------------ tv_synth.c: one synth slot (ZYNTH voice + sound) */
#define TVS_NUM_VOICES   19
#define TVS_NUM_CONTROLS 20
size_t tvs_slot_size(void);
void tvs_global_init(void);                              /* the shared tables; call once (tv_main serialises it) */
int tvs_slot_init(void *slot);                           /* 0 ok; allocates the Mutable engines' state (create time) */
void tvs_slot_free(void *slot);
void tvs_slot_reset(void *slot);                         /* back to the default sound, voice 0 (ANALOG), silent */
int tvs_set_voice(void *slot, int voice);
int tvs_get_voice(const void *slot);
void tvs_note_on(void *slot, int note, int vel127, unsigned flags);
void tvs_note_off(void *slot);
void tvs_kill(void *slot);                               /* silence at once */
int tvs_active(const void *slot);
void tvs_set_control(void *slot, int ctl, float v);      /* V1..V20 of the voice in use, 0..1 */
float tvs_get_control(const void *slot, int ctl);
void tvs_set_chain(void *slot, int chain, float v);      /* the shared chain: index into the chain table below */
float tvs_get_chain(const void *slot, int chain);
int tvs_load_starter(void *slot, int voice, int idx);    /* idx 0 = the home sound */
void tvs_set_level(void *slot, float v);
void tvs_set_pan(void *slot, float v);
int tvs_load_bass(void *slot, int idx);                  /* BAZZLINE's bank, on the BAZZ voice */
void tvs_render(void *slot, float bpm, float *L, float *R, int n);   /* ADDS n frames into L/R (n <= any) */
/* tables */
const char *tvs_voice_name(int voice);
const char *tvs_voice_short(int voice);
const char *tvs_control_name(int voice, int ctl);        /* the readout's name, "FM AMOUNT" */
const char *tvs_control_label(int voice, int ctl);       /* the panel label, two lines split by '|' */
float tvs_control_default(int voice, int ctl);
int tvs_control_steps(int voice, int ctl);               /* 0: continuous, n: n options */
void tvs_control_text(int voice, int ctl, float v, char *buf, int n, int full);
int tvs_voice_macro(int voice, int which);               /* the control the front's TIMBRE (0) / MORPH (1) moves, V1..V12 0-based */
const char *tvs_starter_name(int voice, int idx);
int tvs_chain_count(void);
const char *tvs_chain_key(int chain);
const char *tvs_chain_name(int chain);
float tvs_chain_default(int chain);
int tvs_chain_options(int chain);                        /* 0: continuous 0..1, n: option index 0..n-1 */
const char *tvs_chain_option_name(int chain, int opt);
int tvs_bass_count(void);
const char *tvs_bass_name(int idx);

/* ------------------------------------------------------------------ tv_fx.c: BAZZLINE's delay and gate/duck */
typedef struct TvFx TvFx;
TvFx *tvfx_create(void);                                 /* allocates the 2.97 s buffers */
void tvfx_destroy(TvFx *f);
void tvfx_reset(TvFx *f);
void tvfx_set(TvFx *f, int g, float v);                  /* g: TVFX_* below, v as the BAZZLINE knob */
float tvfx_get(const TvFx *f, int g);
void tvfx_set_sync(TvFx *f, int mode);                   /* 0 free (ms), 1 the tempo's beat, 2 tapped period */
void tvfx_set_tap_period(TvFx *f, float seconds);
void tvfx_set_tempo(TvFx *f, float bpm);
void tvfx_set_gate_duck_on(TvFx *f, int on);
void tvfx_set_toggle(TvFx *f, int which, int on);        /* TVFX_HOLD, TVFX_ADD, TVFX_REVERSE */
void tvfx_duck_trigger(TvFx *f);
float tvfx_delay_seconds(TvFx *f);
void tvfx_render(TvFx *f, float *L, float *R, const float *sL, const float *sR, int n);
int tvfx_param_count(void);
const char *tvfx_param_key(int g);
const char *tvfx_param_name(int g);
float tvfx_param_default(int g);
int tvfx_param_options(int g);
const char *tvfx_param_option_name(int g, int opt);
enum { TVFX_HOLD, TVFX_ADD, TVFX_REVERSE };

/* ------------------------------------------------------------------ tv_drum.c: the LXRXXL 0.5.10 drum engine */
#define TVD_VOICES    6      /* DRUM1 DRUM2 DRUM3 SNARE CLAP/CYMBAL HIHAT */
#define TVD_CONTROLS  20
#define TVD_KITS      65     /* 64 factory kits + the init kit */
#define TVD_BLOCK     32     /* the engine's block (frames) */
typedef struct TvDrum TvDrum;
TvDrum *tvd_create(void);                                /* allocates the per-instance engine state */
void tvd_destroy(TvDrum *d);
void tvd_reset(TvDrum *d);                               /* init kit, silent */
void tvd_set_tempo(TvDrum *d, float bpm);
void tvd_trigger(TvDrum *d, int voice, int vel127, int note, int hat_open);
void tvd_load_kit(TvDrum *d, int kit);
int tvd_load_voice(TvDrum *d, int voice, int kit);       /* one voice's sound from a kit into the working kit */
void tvd_set_control(TvDrum *d, int voice, int ctl, float v);
float tvd_get_control(const TvDrum *d, int voice, int ctl);
void tvd_set_raw(TvDrum *d, int par, int value);         /* firmware parameter 0..227 (0..127, options as index) */
int tvd_get_raw(const TvDrum *d, int par);
void tvd_set_ext(TvDrum *d, int ext, int value);         /* LXR-02 extras: pulse widths, FX unit */
int tvd_get_ext(const TvDrum *d, int ext);
void tvd_set_fader(TvDrum *d, int voice, float v);       /* 0..1 (LXR-02 fader taper applied inside) */
float tvd_get_fader(const TvDrum *d, int voice);
void tvd_set_pan(TvDrum *d, int voice, float v);         /* 0..1 -> the kit's PAN */
/* render: all six voices run together in 32-frame blocks; each voice has a ring from which its slot reads. */
void tvd_render_voice(TvDrum *d, int voice, float *L, float *R, int n);   /* ADDS */
void tvd_render_fx(TvDrum *d, float *L, float *R, int n);                 /* ADDS the FX unit's return */
int tvd_voice_active(const TvDrum *d, int voice);
/* tables */
const char *tvd_voice_name(int voice);
const char *tvd_voice_short(int voice);
const char *tvd_control_name(int voice, int ctl);
const char *tvd_control_label(int voice, int ctl);
float tvd_control_default(int voice, int ctl);
int tvd_control_steps(int voice, int ctl);
void tvd_control_text(int voice, int ctl, float v, char *buf, int n, int full);
const char *tvd_kit_name(int kit);
void tvd_global_init(void);
