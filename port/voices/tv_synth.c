/* TrackerXXL voice library -- the synth slot: LXRXXL ZYNTH's nineteen voices and shared chain (zynth/src), plus
 * LXRXXL BAZZLINE's eight-sound bass bank (bazz/bass_bank.h, generated).
 *
 * One slot is one monophonic track: a ZYNTH Voice (engines, FM, SUB, NOISE, swarm, drive, Polivoks / diode filter,
 * ADSRs, pitch envelope, accent / slide, modulator) and the Sound it plays (the 20 controls V1..V20 of every voice,
 * and the chain's parameters, a whole sound remembered per voice). Written for this library; the vendored sources are
 * not modified. No allocation outside tvs_slot_init() (the Mutable Instruments engines' state, once). */
#include <math.h>
#include <string.h>
#include "bl.h"
#include "param_map.h"
#include "tv_internal.h"
#include "bass_bank.h"
#include "trackerxxl_voices.h"

typedef struct { Sound snd; Voice v; } Slot;

/* the chain's parameters: every sound parameter that is not the VOICE selector or one of V1..V20 */
static uint8_t g_chain[SP_COUNT];
static int g_nchain;

size_t tvs_slot_size(void) { return sizeof(Slot); }

void tvs_global_init(void)
{
    int k;
    if (g_nchain) return;
    eng_tables_init();
    for (k = 0; k < SP_COUNT; k++) {
        if (k == SP_VOICE || (k >= SP_V1 && k <= SP_V20)) continue;
        g_chain[g_nchain++] = (uint8_t)k;
    }
}

int tvs_slot_init(void *p)
{
    Slot *s = (Slot *)p;
    memset(s, 0, sizeof *s);
    init_sound(&s->snd);
    voice_init(&s->v);
    voice_attach(&s->v);
    return s->v.eng[0].mi && s->v.eng[1].mi ? 0 : -1;
}

void tvs_slot_free(void *p) { voice_detach(&((Slot *)p)->v); }

void tvs_slot_reset(void *p)
{
    Slot *s = (Slot *)p;
    voice_kill(&s->v);
    init_sound(&s->snd);
}

int tvs_set_voice(void *p, int voice)
{
    if (voice < 0 || voice >= NUM_ENGINES) return -1;
    sound_setVoice(&((Slot *)p)->snd, voice);
    return 0;
}

int tvs_get_voice(const void *p)
{
    const int t = (int)(((const Slot *)p)->snd.v[SP_VOICE] + 0.5f);
    return t < 0 ? 0 : t >= NUM_ENGINES ? NUM_ENGINES - 1 : t;
}

void tvs_note_on(void *p, int note, int vel127, unsigned flags)
{
    Slot *s = (Slot *)p;
    const int accent = (flags & TV_NOTE_ACCENT) || ((flags & TV_NOTE_VEL_ACCENT) && vel127 > 100);
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    if (vel127 < 1) vel127 = 1;
    if (vel127 > 127) vel127 = 127;
    voice_noteOn(&s->v, &s->snd, (float)note, (float)vel127 * (1.f / 127.f), accent, (flags & TV_NOTE_SLIDE) != 0,
                 (flags & TV_NOTE_NO_PENV) == 0, (flags & TV_NOTE_NO_MOD) == 0);
}

void tvs_note_off(void *p) { voice_noteOff(&((Slot *)p)->v); }
void tvs_kill(void *p) { voice_kill(&((Slot *)p)->v); }
int tvs_active(const void *p) { return ((const Slot *)p)->v.active; }

void tvs_set_control(void *p, int ctl, float v)
{
    if (ctl < 0 || ctl >= ENG_SLIDERS) return;
    sound_setSlider(&((Slot *)p)->snd, ctl, v);
}

float tvs_get_control(const void *p, int ctl)
{
    if (ctl < 0 || ctl >= ENG_SLIDERS) return 0.f;
    return ((const Slot *)p)->snd.v[SP_V1 + ctl];
}

void tvs_set_chain(void *p, int chain, float v)
{
    Slot *s = (Slot *)p;
    int sp;
    if (chain < 0 || chain >= g_nchain) return;
    sp = g_chain[chain];
    if (SOUND_DEFS[sp].nopts) {
        int i = (int)(v + 0.5f);
        if (i < 0) i = 0;
        if (i >= SOUND_DEFS[sp].nopts) i = SOUND_DEFS[sp].nopts - 1;
        s->snd.v[sp] = (float)i;
    } else s->snd.v[sp] = clampf(v, 0.f, 1.f);
    sound_sync(&s->snd);
}

float tvs_get_chain(const void *p, int chain)
{
    if (chain < 0 || chain >= g_nchain) return 0.f;
    return ((const Slot *)p)->snd.v[g_chain[chain]];
}

void tvs_set_level(void *p, float v) { Slot *s = (Slot *)p; s->snd.v[SP_LEVEL] = clampf(v, 0.f, 1.f); sound_sync(&s->snd); }
void tvs_set_pan(void *p, float v) { Slot *s = (Slot *)p; s->snd.v[SP_PAN] = clampf(v, 0.f, 1.f); sound_sync(&s->snd); }

int tvs_load_starter(void *p, int voice, int idx)
{
    Slot *s = (Slot *)p;
    if (voice < 0 || voice >= NUM_ENGINES) return -1;
    sound_setVoice(&s->snd, voice);
    sound_starter(&s->snd, voice, idx);
    return 0;
}

int tvs_load_bass(void *p, int idx)
{
    Slot *s = (Slot *)p;
    float home[SP_COUNT];
    int k;
    if (idx < 0 || idx >= BASS_BANK_N) return -1;
    sound_setVoice(&s->snd, ENG_BAZZ);
    sound_home(ENG_BAZZ, home);
    /* the whole sound from BAZZ's home (the play row V13..V20 included), then BAZZLINE's own values over it; the track's
     * PAN stays (as for every voice change), LEVEL is part of a BAZZLINE sound */
    for (k = 0; k < SP_COUNT; k++)
        if (k != SP_VOICE && k != SP_PAN && k != SP_LEVEL) s->snd.v[k] = home[k];
    for (k = 0; k < BASS_BANK_SETS && BASS_BANK[idx][k].sp != 255; k++)
        if (BASS_BANK[idx][k].sp != SP_PAN) s->snd.v[BASS_BANK[idx][k].sp] = BASS_BANK[idx][k].v;
    s->snd.v[SP_VOICE] = (float)ENG_BAZZ;
    sound_sync(&s->snd);
    return 0;
}

void tvs_render(void *p, float bpm, float *L, float *R, int n)
{
    Slot *s = (Slot *)p;
    if (n <= 0 || !s->v.active) return;
    voice_render(&s->v, &s->snd, bpm, L, R, n);
}

/* ---------------------------------------------------------------- tables */
static int vok(int voice) { return voice >= 0 && voice < NUM_ENGINES; }
static int cok(int ctl) { return ctl >= 0 && ctl < ENG_SLIDERS; }

int tvs_voice_macro(int voice, int which) { return vok(voice) && which >= 0 && which < 2 ? ENG_DEFS[voice].macro[which] : -1; }
const char *tvs_voice_name(int voice) { return vok(voice) ? ENG_DEFS[voice].name : ""; }
const char *tvs_voice_short(int voice) { return vok(voice) ? ENG_DEFS[voice].shortName : ""; }
const char *tvs_control_name(int voice, int ctl) { return vok(voice) && cok(ctl) ? ENG_DEFS[voice].slider[ctl] : ""; }
const char *tvs_control_label(int voice, int ctl) { return vok(voice) && cok(ctl) ? ENG_DEFS[voice].label[ctl] : ""; }
float tvs_control_default(int voice, int ctl) { return vok(voice) && cok(ctl) ? ENG_DEFS[voice].def[ctl] : 0.f; }
int tvs_control_steps(int voice, int ctl) { return vok(voice) && cok(ctl) ? ENG_DEFS[voice].steps[ctl] : 0; }

void tvs_control_text(int voice, int ctl, float v, char *buf, int n, int full)
{
    if (n <= 0) return;
    buf[0] = 0;
    if (!vok(voice) || !cok(ctl)) return;
    eng_value_text(voice, ctl, clampf(v, 0.f, 1.f), buf, n, full);
}

const char *tvs_starter_name(int voice, int idx)
{
    if (!vok(voice) || idx < 0 || idx >= NUM_STARTER_SOUNDS) return "";
    return sound_starterName(voice, idx);
}

int tvs_chain_count(void) { return g_nchain; }
const char *tvs_chain_key(int c) { return c >= 0 && c < g_nchain ? SOUND_DEFS[g_chain[c]].key : ""; }
const char *tvs_chain_name(int c) { return c >= 0 && c < g_nchain ? SOUND_DEFS[g_chain[c]].name : ""; }
float tvs_chain_default(int c) { return c >= 0 && c < g_nchain ? SOUND_DEFS[g_chain[c]].def : 0.f; }
int tvs_chain_options(int c) { return c >= 0 && c < g_nchain ? SOUND_DEFS[g_chain[c]].nopts : 0; }

const char *tvs_chain_option_name(int c, int opt)
{
    if (c < 0 || c >= g_nchain || opt < 0 || opt >= SOUND_DEFS[g_chain[c]].nopts || !SOUND_DEFS[g_chain[c]].opts) return "";
    return SOUND_DEFS[g_chain[c]].opts[opt];
}

int tvs_bass_count(void) { return BASS_BANK_N; }
const char *tvs_bass_name(int idx) { return idx >= 0 && idx < BASS_BANK_N ? BASS_BANK_NAMES[idx] : ""; }
