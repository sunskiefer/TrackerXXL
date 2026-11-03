/* TrackerXXL voice library -- the public API (trackerxxl_voices.h): instances, slots, tables, the mixer and the
 * sample-rate conversion. The three engines it ties together live in tv_synth.c (ZYNTH voices, BAZZLINE bass bank),
 * tv_drum.c (LXRXXL 0.5.10 drums) and tv_fx.c (BAZZLINE delay and gate/duck); see tv_internal.h.
 * Written for this library. Nothing here allocates after tv_create(). */
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "trackerxxl_voices.h"
#include "tv_internal.h"

#define MAX_SLOTS (TV_MAX_SYNTH_SLOTS + TV_NUM_DRUM_VOICES)
#define RS_DRUMFX MAX_SLOTS
#define RS_MIX (MAX_SLOTS + 1)
#define CHUNK 128                      /* output frames per resampling step and per mix step */
#define TMPN 720                       /* 128 * 44100 / 8000 + 2: engine frames needed for one chunk at the lowest rate */

typedef struct { double ph; float h[2][4]; } Rs;

struct TvInstance {
    int nsyn, ndrum;
    float sr;
    double step;                       /* engine frames per output frame: 44100 / sr */
    int native;                        /* sr == 44100: nothing is resampled */
    float bpm, master;
    uint64_t mutes, solos;
    float send[MAX_SLOTS];
    size_t ssize;                      /* bytes per synth slot */
    unsigned char *synth;
    TvDrum *drum;
    TvFx *fx;
    Rs rs[MAX_SLOTS + 2];
};

/* ---------------------------------------------------------------- global tables */
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void init_once(void) { tvs_global_init(); tvd_global_init(); }
static void ensure(void) { pthread_once(&g_once, init_once); }

static int svoice(int v) { return v >= 0 && v < TV_NUM_SYNTH_VOICES; }
static int dvoice(int v) { return v >= TV_NUM_SYNTH_VOICES && v < TV_NUM_VOICES; }

int tv_voice_count(void) { return TV_NUM_VOICES; }
const char *tv_voice_name(int v) { ensure(); return svoice(v) ? tvs_voice_name(v) : dvoice(v) ? tvd_voice_name(v - TV_NUM_SYNTH_VOICES) : ""; }
const char *tv_voice_short_name(int v) { ensure(); return svoice(v) ? tvs_voice_short(v) : dvoice(v) ? tvd_voice_short(v - TV_NUM_SYNTH_VOICES) : ""; }
int tv_voice_is_drum(int v) { return dvoice(v); }

int tv_voice_macro_control(int v, int which) { ensure(); return svoice(v) ? tvs_voice_macro(v, which) : -1; }

const char *tv_control_name(int v, int c) { ensure(); return svoice(v) ? tvs_control_name(v, c) : dvoice(v) ? tvd_control_name(v - TV_NUM_SYNTH_VOICES, c) : ""; }
const char *tv_control_label(int v, int c) { ensure(); return svoice(v) ? tvs_control_label(v, c) : dvoice(v) ? tvd_control_label(v - TV_NUM_SYNTH_VOICES, c) : ""; }
float tv_control_default(int v, int c) { ensure(); return svoice(v) ? tvs_control_default(v, c) : dvoice(v) ? tvd_control_default(v - TV_NUM_SYNTH_VOICES, c) : 0.f; }
int tv_control_steps(int v, int c) { ensure(); return svoice(v) ? tvs_control_steps(v, c) : dvoice(v) ? tvd_control_steps(v - TV_NUM_SYNTH_VOICES, c) : 0; }

void tv_control_text(int v, int c, float value, char *buf, int n, int full)
{
    ensure();
    if (n <= 0 || !buf) return;
    buf[0] = 0;
    if (svoice(v)) tvs_control_text(v, c, value, buf, n, full);
    else if (dvoice(v)) tvd_control_text(v - TV_NUM_SYNTH_VOICES, c, value, buf, n, full);
}

int tv_chain_count(void) { ensure(); return tvs_chain_count(); }
const char *tv_chain_key(int i) { ensure(); return tvs_chain_key(i); }
const char *tv_chain_name(int i) { ensure(); return tvs_chain_name(i); }
float tv_chain_default(int i) { ensure(); return tvs_chain_default(i); }
int tv_chain_options(int i) { ensure(); return tvs_chain_options(i); }
const char *tv_chain_option_name(int i, int o) { ensure(); return tvs_chain_option_name(i, o); }

int tv_starter_count(int v) { return svoice(v) ? TV_STARTERS_PER_SYNTH_VOICE : dvoice(v) ? TVD_KITS : 0; }
const char *tv_starter_name(int v, int i)
{
    ensure();
    return svoice(v) ? tvs_starter_name(v, i) : dvoice(v) ? tvd_kit_name(i) : "";
}

int tv_bass_bank_count(void) { ensure(); return tvs_bass_count(); }
const char *tv_bass_bank_name(int i) { ensure(); return tvs_bass_name(i); }
int tv_kit_count(void) { return TVD_KITS; }
const char *tv_kit_name(int k) { ensure(); return tvd_kit_name(k); }

int tv_fx_param_count(void) { return tvfx_param_count(); }
const char *tv_fx_param_key(int i) { return tvfx_param_key(i); }
const char *tv_fx_param_name(int i) { return tvfx_param_name(i); }
float tv_fx_param_default(int i) { return tvfx_param_default(i); }
int tv_fx_param_options(int i) { return tvfx_param_options(i); }
const char *tv_fx_param_option_name(int i, int o) { return tvfx_param_option_name(i, o); }

/* ---------------------------------------------------------------- instance */
void tv_config_init(TvConfig *c)
{
    c->synth_slots = TV_DEFAULT_SYNTH_SLOTS;
    c->drum_voices = TV_NUM_DRUM_VOICES;
    c->sample_rate = 44100.f;
}

static void mixer_defaults(TvInstance *t)
{
    int i;
    t->master = 1.f;
    t->mutes = t->solos = 0;
    for (i = 0; i < MAX_SLOTS; i++) t->send[i] = 1.f;
}

TvInstance *tv_create(const TvConfig *cfg)
{
    TvConfig c;
    TvInstance *t;
    int i;
    ensure();
    if (cfg) c = *cfg; else tv_config_init(&c);
    if (c.synth_slots < 0 || c.synth_slots > TV_MAX_SYNTH_SLOTS || c.drum_voices < 0 || c.drum_voices > TV_NUM_DRUM_VOICES) return NULL;
    if (c.sample_rate == 0.f) c.sample_rate = 44100.f;
    if (c.sample_rate < 8000.f || c.sample_rate > 192000.f) return NULL;
    t = (TvInstance *)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->nsyn = c.synth_slots;
    t->ndrum = c.drum_voices;
    t->bpm = 120.f;
    mixer_defaults(t);
    tv_set_sample_rate(t, c.sample_rate);
    t->ssize = (tvs_slot_size() + 15u) & ~(size_t)15u;
    if (t->nsyn) {
        t->synth = (unsigned char *)calloc((size_t)t->nsyn, t->ssize);
        if (!t->synth) { free(t); return NULL; }
        for (i = 0; i < t->nsyn; i++)
            if (tvs_slot_init(t->synth + (size_t)i * t->ssize) != 0) {
                int j;
                for (j = 0; j <= i; j++) tvs_slot_free(t->synth + (size_t)j * t->ssize);   /* frees what was allocated */
                free(t->synth);
                free(t);
                return NULL;
            }
    }
    t->fx = tvfx_create();
    if (!t->fx) { tv_destroy(t); return NULL; }
    if (t->ndrum) {
        t->drum = tvd_create();
        if (!t->drum) { tv_destroy(t); return NULL; }
    }
    return t;
}

void tv_destroy(TvInstance *t)
{
    int i;
    if (!t) return;
    for (i = 0; i < t->nsyn && t->synth; i++) tvs_slot_free(t->synth + (size_t)i * t->ssize);
    free(t->synth);
    tvd_destroy(t->drum);
    tvfx_destroy(t->fx);
    free(t);
}

void tv_reset(TvInstance *t)
{
    int i;
    for (i = 0; i < t->nsyn; i++) tvs_slot_reset(t->synth + (size_t)i * t->ssize);
    if (t->drum) tvd_reset(t->drum);
    tvfx_reset(t->fx);
    tvfx_set_tempo(t->fx, t->bpm);
    mixer_defaults(t);
    memset(t->rs, 0, sizeof t->rs);
}

int tv_set_sample_rate(TvInstance *t, float sr)
{
    if (!(sr >= 8000.f && sr <= 192000.f)) return -1;
    t->sr = sr;
    t->step = (double)TV_INTERNAL_SR / (double)sr;
    t->native = fabsf(sr - TV_INTERNAL_SR) < 0.5f;
    if (t->native) t->step = 1.0;
    memset(t->rs, 0, sizeof t->rs);
    return 0;
}

float tv_get_sample_rate(const TvInstance *t) { return t->sr; }

void tv_set_tempo(TvInstance *t, float bpm)
{
    if (!(bpm >= 20.f)) bpm = 20.f;
    if (bpm > 400.f) bpm = 400.f;
    t->bpm = bpm;
    tvfx_set_tempo(t->fx, bpm);
    if (t->drum) tvd_set_tempo(t->drum, bpm);
}

int tv_slot_count(const TvInstance *t) { return t->nsyn + t->ndrum; }
int tv_synth_slot_count(const TvInstance *t) { return t->nsyn; }
int tv_drum_slot_count(const TvInstance *t) { return t->ndrum; }
int tv_slot_is_drum(const TvInstance *t, int s) { return s >= t->nsyn && s < t->nsyn + t->ndrum; }

static void *sslot(const TvInstance *t, int s) { return s >= 0 && s < t->nsyn ? t->synth + (size_t)s * t->ssize : NULL; }
static int dslot(const TvInstance *t, int s) { return tv_slot_is_drum(t, s) ? s - t->nsyn : -1; }

int tv_slot_voice(const TvInstance *t, int s)
{
    if (sslot(t, s)) return tvs_get_voice(sslot(t, s));
    if (dslot(t, s) >= 0) return TV_NUM_SYNTH_VOICES + dslot(t, s);
    return -1;
}

int tv_slot_set_voice(TvInstance *t, int s, int voice)
{
    void *p = sslot(t, s);
    return p && svoice(voice) ? tvs_set_voice(p, voice) : -1;
}

/* ---------------------------------------------------------------- parameters and sounds */
int tv_set_param(TvInstance *t, int s, int ctl, float v)
{
    void *p = sslot(t, s);
    if (ctl < 0 || ctl >= TV_NUM_CONTROLS) return -1;
    if (p) { tvs_set_control(p, ctl, v); return 0; }
    if (dslot(t, s) >= 0 && t->drum) { tvd_set_control(t->drum, dslot(t, s), ctl, v); return 0; }
    return -1;
}

float tv_get_param(const TvInstance *t, int s, int ctl)
{
    void *p = sslot(t, s);
    if (ctl < 0 || ctl >= TV_NUM_CONTROLS) return 0.f;
    if (p) return tvs_get_control(p, ctl);
    if (dslot(t, s) >= 0 && t->drum) return tvd_get_control(t->drum, dslot(t, s), ctl);
    return 0.f;
}

int tv_set_chain(TvInstance *t, int s, int idx, float v)
{
    void *p = sslot(t, s);
    if (!p || idx < 0 || idx >= tvs_chain_count()) return -1;
    tvs_set_chain(p, idx, v);
    return 0;
}

float tv_get_chain(const TvInstance *t, int s, int idx)
{
    void *p = sslot(t, s);
    return p ? tvs_get_chain(p, idx) : 0.f;
}

int tv_set_level(TvInstance *t, int s, float v)
{
    void *p = sslot(t, s);
    if (p) { tvs_set_level(p, v); return 0; }
    if (dslot(t, s) >= 0 && t->drum) { tvd_set_fader(t->drum, dslot(t, s), v); return 0; }
    return -1;
}

int tv_set_pan(TvInstance *t, int s, float v)
{
    void *p = sslot(t, s);
    if (p) { tvs_set_pan(p, v); return 0; }
    if (dslot(t, s) >= 0 && t->drum) { tvd_set_pan(t->drum, dslot(t, s), v); return 0; }
    return -1;
}

int tv_load_starter(TvInstance *t, int s, int voice, int idx)
{
    void *p = sslot(t, s);
    if (p) return svoice(voice) && idx >= 0 && idx < TV_STARTERS_PER_SYNTH_VOICE ? tvs_load_starter(p, voice, idx) : -1;
    if (dslot(t, s) >= 0 && t->drum && voice == TV_NUM_SYNTH_VOICES + dslot(t, s)) return tvd_load_voice(t->drum, dslot(t, s), idx);
    return -1;
}

int tv_load_bass_bank(TvInstance *t, int s, int idx)
{
    void *p = sslot(t, s);
    return p ? tvs_load_bass(p, idx) : -1;
}

int tv_load_kit(TvInstance *t, int kit)
{
    if (!t->drum || kit < 0 || kit >= TVD_KITS) return -1;
    tvd_load_kit(t->drum, kit);
    return 0;
}

int tv_drum_set_raw(TvInstance *t, int par, int value)
{
    if (!t->drum || tvd_get_raw(t->drum, par) < 0) return -1;
    tvd_set_raw(t->drum, par, value);
    return 0;
}
int tv_drum_get_raw(const TvInstance *t, int par) { return t->drum ? tvd_get_raw(t->drum, par) : -1; }
int tv_drum_set_ext(TvInstance *t, int ext, int value)
{
    if (!t->drum || tvd_get_ext(t->drum, ext) < 0) return -1;
    tvd_set_ext(t->drum, ext, value);
    return 0;
}
int tv_drum_get_ext(const TvInstance *t, int ext) { return t->drum ? tvd_get_ext(t->drum, ext) : -1; }

/* ---------------------------------------------------------------- notes */
void tv_note_off(TvInstance *t, int s)
{
    void *p = sslot(t, s);
    if (p) tvs_note_off(p);
}

void tv_note_on(TvInstance *t, int s, int note, int vel, unsigned flags)
{
    void *p = sslot(t, s);
    if (vel <= 0) { tv_note_off(t, s); return; }
    if (p) tvs_note_on(p, note, vel, flags);
    else if (dslot(t, s) >= 0 && t->drum) tvd_trigger(t->drum, dslot(t, s), vel, note, (flags & TV_NOTE_HAT_OPEN) != 0);
}

void tv_all_notes_off(TvInstance *t)
{
    int i;
    for (i = 0; i < t->nsyn; i++) tvs_note_off(t->synth + (size_t)i * t->ssize);
}

void tv_slot_kill(TvInstance *t, int s)
{
    void *p = sslot(t, s);
    if (p) tvs_kill(p);   /* the drum engine has no choke: a drum slot keeps ringing */
}

int tv_slot_active(const TvInstance *t, int s)
{
    void *p = sslot(t, s);
    if (p) return tvs_active(p);
    if (dslot(t, s) >= 0 && t->drum) return tvd_voice_active(t->drum, dslot(t, s));
    return 0;
}

/* ---------------------------------------------------------------- mixer and fx */
void tv_set_send(TvInstance *t, int s, float v)
{
    if (s >= 0 && s < t->nsyn + t->ndrum) t->send[s] = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
}
void tv_set_mute(TvInstance *t, int s, int on)
{
    if (s < 0 || s >= t->nsyn + t->ndrum) return;
    if (on) t->mutes |= (uint64_t)1 << s; else t->mutes &= ~((uint64_t)1 << s);
}
void tv_set_solo(TvInstance *t, int s, int on)
{
    if (s < 0 || s >= t->nsyn + t->ndrum) return;
    if (on) t->solos |= (uint64_t)1 << s; else t->solos &= ~((uint64_t)1 << s);
}
void tv_set_master(TvInstance *t, float g) { t->master = g < 0.f ? 0.f : g > 4.f ? 4.f : g; }

int tv_fx_set_param(TvInstance *t, int i, float v)
{
    if (i < 0 || i >= tvfx_param_count()) return -1;
    tvfx_set(t->fx, i, v);
    return 0;
}
float tv_fx_get_param(const TvInstance *t, int i) { return tvfx_get(t->fx, i); }
void tv_fx_set_sync(TvInstance *t, int m) { tvfx_set_sync(t->fx, m); }
void tv_fx_set_tap_period(TvInstance *t, float s) { tvfx_set_tap_period(t->fx, s); }
void tv_fx_set_toggle(TvInstance *t, int which, int on) { tvfx_set_toggle(t->fx, which, on); }
void tv_fx_set_gate_duck_on(TvInstance *t, int on) { tvfx_set_gate_duck_on(t->fx, on); }
void tv_fx_duck_trigger(TvInstance *t) { tvfx_duck_trigger(t->fx); }
float tv_fx_delay_seconds(TvInstance *t) { return tvfx_delay_seconds(t->fx); }

/* ---------------------------------------------------------------- producers (engine rate, ADD into L/R) */
typedef void (*Produce)(TvInstance *t, int arg, float *L, float *R, int n);

static void prod_slot(TvInstance *t, int s, float *L, float *R, int n)
{
    void *p = sslot(t, s);
    if (p) tvs_render(p, t->bpm, L, R, n);
    else if (dslot(t, s) >= 0 && t->drum) tvd_render_voice(t->drum, dslot(t, s), L, R, n);
}

static void prod_drumfx(TvInstance *t, int unused, float *L, float *R, int n)
{
    (void)unused;
    if (t->drum) tvd_render_fx(t->drum, L, R, n);
}

static int audible(const TvInstance *t, int s)
{
    const uint64_t b = (uint64_t)1 << s;
    return !(t->mutes & b) && (!t->solos || (t->solos & b));
}

static void prod_mix(TvInstance *t, int unused, float *L, float *R, int n)
{
    float vL[CHUNK], vR[CHUNK], sL[CHUNK], sR[CHUNK];
    (void)unused;
    while (n > 0) {
        const int m = n > CHUNK ? CHUNK : n;
        int s, i;
        memset(sL, 0, sizeof(float) * (size_t)m);
        memset(sR, 0, sizeof(float) * (size_t)m);
        for (s = 0; s < t->nsyn + t->ndrum; s++) {
            const int drum = s >= t->nsyn;
            if (!drum && !tvs_active(sslot(t, s))) continue;
            memset(vL, 0, sizeof(float) * (size_t)m);
            memset(vR, 0, sizeof(float) * (size_t)m);
            prod_slot(t, s, vL, vR, m);       /* drum voices are always consumed: their rings must not back up */
            if (!audible(t, s)) continue;
            {
                const float snd = t->send[s];
                for (i = 0; i < m; i++) {
                    L[i] += vL[i];
                    R[i] += vR[i];
                    sL[i] += vL[i] * snd;
                    sR[i] += vR[i] * snd;
                }
            }
        }
        if (t->drum) tvd_render_fx(t->drum, L, R, m);
        tvfx_render(t->fx, L, R, sL, sR, m);
        if (t->master != 1.f)
            for (i = 0; i < m; i++) { L[i] *= t->master; R[i] *= t->master; }
        L += m;
        R += m;
        n -= m;
    }
}

/* ---------------------------------------------------------------- sample-rate conversion */
/* Catmull-Rom (4-point Hermite) between h[1] and h[2], 0 <= x < 1 */
static inline float hermite(const float *h, float x)
{
    const float c1 = 0.5f * (h[2] - h[0]);
    const float c2 = h[0] - 2.5f * h[1] + 2.f * h[2] - 0.5f * h[3];
    const float c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);
    return ((c3 * x + c2) * x + c1) * x + h[1];
}

/* Adds n output frames of a producer, resampled from the engine's 44.1 kHz when the instance runs at another rate. */
static void rs_run(TvInstance *t, int ri, Produce fn, int arg, float *oL, float *oR, int n)
{
    Rs *rs = &t->rs[ri];
    if (t->native) { fn(t, arg, oL, oR, n); return; }
    while (n > 0) {
        float tl[TMPN], tr[TMPN];
        const int m = n > CHUNK ? CHUNK : n;
        int i, c, cnt = 0, src = 0;
        double p = rs->ph;
        for (i = 0; i < m; i++) { p += t->step; while (p >= 1.0) { p -= 1.0; cnt++; } }
        if (cnt > TMPN) cnt = TMPN;   /* unreachable for 8 kHz and up; keeps the arrays safe */
        memset(tl, 0, sizeof(float) * (size_t)cnt);
        memset(tr, 0, sizeof(float) * (size_t)cnt);
        if (cnt) fn(t, arg, tl, tr, cnt);
        for (i = 0; i < m; i++) {
            oL[i] += hermite(rs->h[0], (float)rs->ph);
            oR[i] += hermite(rs->h[1], (float)rs->ph);
            rs->ph += t->step;
            while (rs->ph >= 1.0) {
                rs->ph -= 1.0;
                for (c = 0; c < 3; c++) { rs->h[0][c] = rs->h[0][c + 1]; rs->h[1][c] = rs->h[1][c + 1]; }
                rs->h[0][3] = src < cnt ? tl[src] : 0.f;
                rs->h[1][3] = src < cnt ? tr[src] : 0.f;
                src++;
            }
        }
        oL += m;
        oR += m;
        n -= m;
    }
}

/* ---------------------------------------------------------------- render */
void tv_render_slot(TvInstance *t, int s, float *L, float *R, int n)
{
    if (s < 0 || s >= t->nsyn + t->ndrum) return;
    while (n > 0) {
        const int m = n > TV_MAX_BLOCK ? TV_MAX_BLOCK : n;
        rs_run(t, s, prod_slot, s, L, R, m);
        L += m;
        R += m;
        n -= m;
    }
}

void tv_render_drum_fx(TvInstance *t, float *L, float *R, int n)
{
    while (n > 0) {
        const int m = n > TV_MAX_BLOCK ? TV_MAX_BLOCK : n;
        rs_run(t, RS_DRUMFX, prod_drumfx, 0, L, R, m);
        L += m;
        R += m;
        n -= m;
    }
}

void tv_render_mix(TvInstance *t, float *L, float *R, int n)
{
    if (n <= 0) return;
    memset(L, 0, sizeof(float) * (size_t)n);
    memset(R, 0, sizeof(float) * (size_t)n);
    while (n > 0) {
        const int m = n > TV_MAX_BLOCK ? TV_MAX_BLOCK : n;
        rs_run(t, RS_MIX, prod_mix, 0, L, R, m);
        L += m;
        R += m;
        n -= m;
    }
}

/* ---------------------------------------------------------------- stand-alone delay and gate/duck */
TvFxUnit *tv_fxunit_create(void) { ensure(); return (TvFxUnit *)tvfx_create(); }
void tv_fxunit_destroy(TvFxUnit *f) { tvfx_destroy((TvFx *)f); }
void tv_fxunit_reset(TvFxUnit *f) { if (f) tvfx_reset((TvFx *)f); }
int tv_fxunit_set_param(TvFxUnit *f, int i, float v)
{
    if (!f || i < 0 || i >= tvfx_param_count()) return -1;
    tvfx_set((TvFx *)f, i, v);
    return 0;
}
void tv_fxunit_set_sync(TvFxUnit *f, int m) { if (f) tvfx_set_sync((TvFx *)f, m); }
void tv_fxunit_set_tempo(TvFxUnit *f, float bpm) { if (f) tvfx_set_tempo((TvFx *)f, bpm); }
void tv_fxunit_set_gate_duck_on(TvFxUnit *f, int on) { if (f) tvfx_set_gate_duck_on((TvFx *)f, on); }
void tv_fxunit_duck_trigger(TvFxUnit *f) { if (f) tvfx_duck_trigger((TvFx *)f); }
void tv_fxunit_process(TvFxUnit *f, float *L, float *R, int n)
{
    float sL[CHUNK], sR[CHUNK];
    if (!f || n <= 0) return;
    while (n > 0) {
        const int m = n > CHUNK ? CHUNK : n;
        memcpy(sL, L, sizeof(float) * (size_t)m);
        memcpy(sR, R, sizeof(float) * (size_t)m);
        tvfx_render((TvFx *)f, L, R, sL, sR, m);
        L += m;
        R += m;
        n -= m;
    }
}
