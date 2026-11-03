/* TrackerXXL voice library -- the drum engine: LXRXXL 0.5.10 (lxr02xxl b449268) on LXR02Open. NOT the 0.6 line
 * (that is the GPL-3 Erica firmware engine and is not in this library).
 *
 * The six drum voices DRUM1, DRUM2, DRUM3, SNARE, CLAP/CYMBAL and HIHAT (open and closed) are LXR02Open's own DSP; the
 * parameters go in through the firmware's own handler (lxr/src/lxr/cc_handler.inc, via lxr_dsp_setParam). The engine keeps
 * its whole state in global variables; lxr_state.c gives each TvDrum its own copy and swaps it in under a lock for every
 * call that touches the engine (ENTER / LEAVE below). The engine renders all six voices together in 32-frame blocks
 * (lxr_dsp_render32_split(), a local addition next to lxr_dsp_render32()); each voice's output waits in a ring until its
 * slot asks for it, so the slots can be rendered one by one. Written for this library.
 *
 * Timing: a trigger is heard from the next 32-frame block the engine renders, i.e. up to 31 frames (0.7 ms) later than
 * the call, plus any frames the rings still hold from the previous block. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lxr.h"
#include "lxr_state.h"
#include "ParameterArray.h"
#include "tv_internal.h"

void lxr_dsp_render32_split(float vL[6][TVD_BLOCK], float vR[6][TVD_BLOCK], float *fxL, float *fxR, const float fader[6],
                            struct LxrFx *fx);   /* lxr/src/lxr/lxr_dsp.c */

#include "lxr_initkit.inc"

#define RING 4096                       /* frames per ring: TV_MAX_BLOCK (2048) + a block + room for a slot rendered late */
#define IDLE_FRAMES 11025               /* a voice counts as active until it has been silent this long (0.25 s) */

struct TvDrum {
    LxrState *st;
    LxrFx fx;
    Kit kit;                            /* the working kit */
    float fader[TVD_VOICES];            /* 0..1, the LXR-02 fader taper is applied when rendering */
    int rd[TVD_VOICES], cnt[TVD_VOICES];
    uint32_t idle[TVD_VOICES];
    int frd, fcnt;
    float ringL[TVD_VOICES][RING], ringR[TVD_VOICES][RING];
    float fxL[RING], fxR[RING];
};

#define ENTER(d) lxr_state_enter((d)->st)
#define LEAVE() lxr_state_leave()

/* ---------------------------------------------------------------- tables */
static Kit g_init;                      /* the init kit, built once */
static char g_kitnames[TVD_KITS][20];
static int g_ready;

static const char *const VOICE_NAMES[TVD_VOICES] = {"DRUM1", "DRUM2", "DRUM3", "SNARE", "CLAP/CYMBAL", "HIHAT"};
static const char *const VOICE_SHORT[TVD_VOICES] = {"DR1", "DR2", "DR3", "SNR", "CLP", "HAT"};
static const char *const WAVES[6] = {"Sin", "Tri", "Saw", "Rec", "Noi", "PWM"};                 /* MENU_WAVEFORM (meta.c) */
static const char *const FILTERS[8] = {"LP", "HP", "BP", "UBP", "Nch", "Pek", "LP2", "off"};     /* filterTypes (MenuText.h) */

typedef struct { const char *name, *label; uint16_t par; uint8_t opts; } DC;   /* opts: 0 = 0..127, else that many options */
#define W 6
#define F 8
/* DRUM1..3 (n = voice + 1; the pan / fm parameters are not contiguous, so they are spelt out per voice below) */
#define DRUM_CTLS(n, wave, modwave, coarse, fine, mgeg, modamnt, pslope, fmamnt, fmfreq, vola, vold, slope, ftype, ffreq, reso, fdrive, drive, dec, vol, pan) \
    {{"WAVE", "WAVE", wave, W}, {"TUNE", "TUNE", coarse, 0}, {"FINE TUNE", "FINE", fine, 0}, {"ATTACK", "ATTACK", vola, 0}, \
     {"DECAY", "DECAY", vold, 0}, {"SLOPE", "SLOPE", slope, 0}, {"FILTER TYPE", "FILTER|TYPE", ftype, F}, \
     {"CUTOFF", "CUTOFF", ffreq, 0}, {"RESONANCE", "RESO", reso, 0}, {"FILTER DRIVE", "FLT|DRIVE", fdrive, 0}, \
     {"DRIVE", "DRIVE", drive, 0}, {"DECIMATE", "DECI|MATE", dec, 0}, {"MOD WAVE", "MOD|WAVE", modwave, W}, \
     {"MOD EG", "MOD|EG", mgeg, 0}, {"MOD AMOUNT", "MOD|AMT", modamnt, 0}, {"PITCH SLOPE", "PITCH|SLOPE", pslope, 0}, \
     {"FM AMOUNT", "FM|AMT", fmamnt, 0}, {"FM FREQ", "FM|FREQ", fmfreq, 0}, {"VOLUME", "VOLUME", vol, 0}, {"PAN", "PAN", pan, 0}}

static const DC CTL[TVD_VOICES][TVD_CONTROLS] = {
    DRUM_CTLS(1, PAR_OSC_WAVE_DRUM1, PAR_MOD_WAVE_DRUM1, PAR_COARSE1, PAR_FINE1, PAR_MOD_EG1, PAR_MODAMNT1, PAR_PITCH_SLOPE1,
              PAR_FMAMNT1, PAR_FM_FREQ1, PAR_VELOA1, PAR_VELOD1, PAR_VOL_SLOPE1, PAR_FILTER_TYPE_1, PAR_FILTER_FREQ_1, PAR_RESO_1,
              PAR_FILTER_DRIVE_1, PAR_DRIVE1, PAR_VOICE_DECIMATION1, PAR_VOL1, PAR_PAN1),
    DRUM_CTLS(2, PAR_OSC_WAVE_DRUM2, PAR_MOD_WAVE_DRUM2, PAR_COARSE2, PAR_FINE2, PAR_MOD_EG2, PAR_MODAMNT2, PAR_PITCH_SLOPE2,
              PAR_FMAMNT2, PAR_FM_FREQ2, PAR_VELOA2, PAR_VELOD2, PAR_VOL_SLOPE2, PAR_FILTER_TYPE_2, PAR_FILTER_FREQ_2, PAR_RESO_2,
              PAR_FILTER_DRIVE_2, PAR_DRIVE2, PAR_VOICE_DECIMATION2, PAR_VOL2, PAR_PAN2),
    DRUM_CTLS(3, PAR_OSC_WAVE_DRUM3, PAR_MOD_WAVE_DRUM3, PAR_COARSE3, PAR_FINE3, PAR_MOD_EG3, PAR_MODAMNT3, PAR_PITCH_SLOPE3,
              PAR_FMAMNT3, PAR_FM_FREQ3, PAR_VELOA3, PAR_VELOD3, PAR_VOL_SLOPE3, PAR_FILTER_TYPE_3, PAR_FILTER_FREQ_3, PAR_RESO_3,
              PAR_FILTER_DRIVE_3, PAR_DRIVE3, PAR_VOICE_DECIMATION3, PAR_VOL3, PAR_PAN3),
    /* SNARE */
    {{"WAVE", "WAVE", PAR_OSC_WAVE_SNARE, W}, {"TUNE", "TUNE", PAR_COARSE4, 0}, {"FINE TUNE", "FINE", PAR_FINE4, 0},
     {"ATTACK", "ATTACK", PAR_VELOA4, 0}, {"DECAY", "DECAY", PAR_VELOD4, 0}, {"SLOPE", "SLOPE", PAR_VOL_SLOPE4, 0},
     {"FILTER TYPE", "FILTER|TYPE", PAR_FILTER_TYPE_4, F}, {"CUTOFF", "CUTOFF", PAR_FILTER_FREQ_4, 0},
     {"RESONANCE", "RESO", PAR_RESO_4, 0}, {"FILTER DRIVE", "FLT|DRIVE", PAR_FILTER_DRIVE_4, 0},
     {"DRIVE", "DRIVE", PAR_SNARE_DISTORTION, 0}, {"DECIMATE", "DECI|MATE", PAR_VOICE_DECIMATION4, 0},
     {"NOISE FREQ", "NOISE|FREQ", PAR_NOISE_FREQ1, 0}, {"NOISE MIX", "NOISE|MIX", PAR_MIX1, 0},
     {"REPEAT", "REPEAT", PAR_REPEAT4, 0}, {"MOD EG", "MOD|EG", PAR_MOD_EG4, 0}, {"MOD AMOUNT", "MOD|AMT", PAR_MODAMNT4, 0},
     {"PITCH SLOPE", "PITCH|SLOPE", PAR_PITCH_SLOPE4, 0}, {"VOLUME", "VOLUME", PAR_VOL4, 0}, {"PAN", "PAN", PAR_PAN4, 0}},
    /* CLAP / CYMBAL */
    {{"WAVE 1", "WAVE 1", PAR_WAVE1_CYM, W}, {"WAVE 2", "WAVE 2", PAR_WAVE2_CYM, W}, {"WAVE 3", "WAVE 3", PAR_WAVE3_CYM, W},
     {"TUNE", "TUNE", PAR_COARSE5, 0}, {"FINE TUNE", "FINE", PAR_FINE5, 0}, {"ATTACK", "ATTACK", PAR_VELOA5, 0},
     {"DECAY", "DECAY", PAR_VELOD5, 0}, {"SLOPE", "SLOPE", PAR_VOL_SLOPE5, 0}, {"REPEAT", "REPEAT", PAR_REPEAT5, 0},
     {"FILTER TYPE", "FILTER|TYPE", PAR_FILTER_TYPE_5, F}, {"CUTOFF", "CUTOFF", PAR_FILTER_FREQ_5, 0},
     {"RESONANCE", "RESO", PAR_RESO_5, 0}, {"DRIVE", "DRIVE", PAR_CYMBAL_DISTORTION, 0},
     {"MOD OSC F1", "MOD F1", PAR_MOD_OSC_F1_CYM, 0}, {"MOD OSC F2", "MOD F2", PAR_MOD_OSC_F2_CYM, 0},
     {"MOD OSC GAIN 1", "MOD|GAIN 1", PAR_MOD_OSC_GAIN1_CYM, 0}, {"MOD OSC GAIN 2", "MOD|GAIN 2", PAR_MOD_OSC_GAIN2_CYM, 0},
     {"VOLUME", "VOLUME", PAR_VOL5, 0}, {"PAN", "PAN", PAR_PAN5, 0}, {"DECIMATE", "DECI|MATE", PAR_VOICE_DECIMATION5, 0}},
    /* HIHAT */
    {{"WAVE 1", "WAVE 1", PAR_WAVE1_HH, W}, {"WAVE 2", "WAVE 2", PAR_WAVE2_HH, W}, {"WAVE 3", "WAVE 3", PAR_WAVE3_HH, W},
     {"TUNE", "TUNE", PAR_COARSE6, 0}, {"FINE TUNE", "FINE", PAR_FINE6, 0}, {"ATTACK", "ATTACK", PAR_VELOA6, 0},
     {"CLOSED DECAY", "CLOSED|DECAY", PAR_VELOD6_CLOSED, 0}, {"OPEN DECAY", "OPEN|DECAY", PAR_VELOD6_OPEN, 0},
     {"SLOPE", "SLOPE", PAR_VOL_SLOPE6, 0}, {"FILTER TYPE", "FILTER|TYPE", PAR_FILTER_TYPE_6, F},
     {"CUTOFF", "CUTOFF", PAR_FILTER_FREQ_6, 0}, {"RESONANCE", "RESO", PAR_RESO_6, 0}, {"DRIVE", "DRIVE", PAR_HAT_DISTORTION, 0},
     {"MOD OSC F1", "MOD F1", PAR_MOD_OSC_F1, 0}, {"MOD OSC F2", "MOD F2", PAR_MOD_OSC_F2, 0},
     {"MOD OSC GAIN 1", "MOD|GAIN 1", PAR_MOD_OSC_GAIN1, 0}, {"MOD OSC GAIN 2", "MOD|GAIN 2", PAR_MOD_OSC_GAIN2, 0},
     {"VOLUME", "VOLUME", PAR_VOL6, 0}, {"PAN", "PAN", PAR_PAN6, 0}, {"DECIMATE", "DECI|MATE", PAR_VOICE_DECIMATION6, 0}},
};

static int vok(int v) { return v >= 0 && v < TVD_VOICES; }
static int cok(int c) { return c >= 0 && c < TVD_CONTROLS; }
static int to_byte(const DC *c, float v)
{
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    if (c->opts) { int i = (int)(v * (float)c->opts); return i >= c->opts ? c->opts - 1 : i; }
    return (int)(v * 127.f + 0.5f);
}
static float from_byte(const DC *c, int b)
{
    if (c->opts) return ((float)(b >= c->opts ? c->opts - 1 : b) + 0.5f) / (float)c->opts;
    return (float)(b > 127 ? 127 : b) * (1.f / 127.f);
}

void tvd_global_init(void)
{
    int i, j;
    if (g_ready) return;
    lxr_initKit(&g_init);
    for (i = 0; i < TVD_KITS - 1; i++) {
        char nm[NAME_LEN + 1];
        int len, dup = 0;
        memcpy(nm, lxr_factoryKits[i].name, NAME_LEN);
        nm[NAME_LEN] = 0;
        for (len = NAME_LEN; len > 0 && nm[len - 1] == ' '; len--) nm[len - 1] = 0;
        for (j = 0; j < TVD_KITS - 1; j++)
            if (j != i && !memcmp(lxr_factoryKits[j].name, lxr_factoryKits[i].name, NAME_LEN)) dup = 1;
        if (dup) snprintf(g_kitnames[i], sizeof g_kitnames[i], "%s %02d", nm, i + 1);
        else snprintf(g_kitnames[i], sizeof g_kitnames[i], "%s", nm);
    }
    snprintf(g_kitnames[TVD_KITS - 1], sizeof g_kitnames[0], "Init");
    g_ready = 1;
}

const char *tvd_voice_name(int v) { return vok(v) ? VOICE_NAMES[v] : ""; }
const char *tvd_voice_short(int v) { return vok(v) ? VOICE_SHORT[v] : ""; }
const char *tvd_control_name(int v, int c) { return vok(v) && cok(c) ? CTL[v][c].name : ""; }
const char *tvd_control_label(int v, int c) { return vok(v) && cok(c) ? CTL[v][c].label : ""; }
int tvd_control_steps(int v, int c) { return vok(v) && cok(c) ? CTL[v][c].opts : 0; }
float tvd_control_default(int v, int c)
{
    if (!vok(v) || !cok(c) || !g_ready) return 0.f;
    return from_byte(&CTL[v][c], g_init.b[CTL[v][c].par]);
}

void tvd_control_text(int v, int c, float val, char *buf, int n, int full)
{
    const DC *dc;
    int b;
    if (n <= 0) return;
    buf[0] = 0;
    if (!vok(v) || !cok(c)) return;
    dc = &CTL[v][c];
    b = to_byte(dc, val);
    if (dc->opts == W) snprintf(buf, (size_t)n, "%s", WAVES[b]);
    else if (dc->opts == F) snprintf(buf, (size_t)n, "%s", FILTERS[b]);
    else if (dc->par == PAR_PAN1 || dc->par == PAR_PAN2 || dc->par == PAR_PAN3 || dc->par == PAR_PAN4 || dc->par == PAR_PAN5 ||
             dc->par == PAR_PAN6)
        snprintf(buf, (size_t)n, full ? "%d" : "%d", b - 63);
    else snprintf(buf, (size_t)n, "%d", b);
}

const char *tvd_kit_name(int k) { return k >= 0 && k < TVD_KITS && g_ready ? g_kitnames[k] : ""; }

/* ---------------------------------------------------------------- the engine */
/* the LXR-02's fader taper (adcPots.c, SLIDER_LOG_TAPER_DB 30), as lxr.c applies it */
static float taper(float v)
{
    const float minGain = 0.0316227766f;   /* 10^(-30/20) */
    if (v <= 0.f) return 0.f;
    if (v >= 1.f) return 1.f;
    return (powf(10.f, (v - 1.f) * 1.5f) - minGain) / (1.f - minGain);
}

/* lxr_applyKit(), lxr.c: the kit into the engine, the FX unit's bytes into the FX unit. Engine state in place. */
static void apply_kit(TvDrum *d)
{
    int i;
    lxr_dsp_applyKit(d->kit.b, NULL, 0);
    for (i = EXT_FX_TYPE; i < LXR_NUM_EXT; i++) lxr_fx_set(&d->fx, (uint8_t)i, d->kit.b[LXR_NUM_SOUND_PARAMS + i]);
}

static void clear_rings(TvDrum *d)
{
    int v;
    for (v = 0; v < TVD_VOICES; v++) { d->rd[v] = 0; d->cnt[v] = 0; d->idle[v] = 1u << 30; }
    d->frd = 0;
    d->fcnt = 0;
    memset(d->ringL, 0, sizeof d->ringL);
    memset(d->ringR, 0, sizeof d->ringR);
    memset(d->fxL, 0, sizeof d->fxL);
    memset(d->fxR, 0, sizeof d->fxR);
}

TvDrum *tvd_create(void)
{
    TvDrum *d = (TvDrum *)calloc(1, sizeof *d);
    int v;
    if (!d) return NULL;
    d->st = lxr_state_create();
    if (!d->st) { free(d); return NULL; }
    d->kit = g_init;
    for (v = 0; v < TVD_VOICES; v++) d->fader[v] = 1.f;
    lxr_fx_init(&d->fx);
    ENTER(d);
    lxr_dsp_init();
    apply_kit(d);
    LEAVE();
    clear_rings(d);
    return d;
}

void tvd_destroy(TvDrum *d)
{
    if (!d) return;
    lxr_state_destroy(d->st);
    free(d);
}

void tvd_reset(TvDrum *d)
{
    int v;
    d->kit = g_init;
    for (v = 0; v < TVD_VOICES; v++) d->fader[v] = 1.f;
    lxr_fx_init(&d->fx);
    ENTER(d);
    lxr_dsp_init();
    apply_kit(d);
    LEAVE();
    clear_rings(d);
}

void tvd_set_tempo(TvDrum *d, float bpm)
{
    ENTER(d);
    lxr_dsp_setTempo(bpm);
    LEAVE();
}

void tvd_trigger(TvDrum *d, int voice, int vel127, int note, int hat_open)
{
    uint8_t track;
    if (!vok(voice)) return;
    if (vel127 < 1) vel127 = 1;
    if (vel127 > 127) vel127 = 127;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    track = (uint8_t)(voice == 5 && hat_open ? 6 : voice);
    ENTER(d);
    lxr_dsp_trigger(track, (uint8_t)vel127, (uint8_t)note);
    LEAVE();
    d->idle[voice] = 0;
}

void tvd_load_kit(TvDrum *d, int kit)
{
    if (kit < 0 || kit >= TVD_KITS) return;
    d->kit = kit == TVD_KITS - 1 ? g_init : lxr_factoryKits[kit];
    ENTER(d);
    apply_kit(d);
    LEAVE();
}

/* every parameter that belongs to voice v (0..5) but not its mixer settings (VOL, PAN, AUDIO OUT stay the slot's):
 * `o` gets the plain ones, `m` the modulation-target selections (applied last, as the firmware does). */
static void voice_params(int v, uint16_t *o, int *no, uint16_t *m, int *nm)
{
    int n = 0, k = 0;
#define ADD(p) o[n++] = (uint16_t)(p)
    ADD(PAR_COARSE1 + 2 * v); ADD(PAR_FINE1 + 2 * v);
    ADD(PAR_FILTER_FREQ_1 + v); ADD(PAR_RESO_1 + v); ADD(PAR_FILTER_DRIVE_1 + v); ADD(PAR_FILTER_TYPE_1 + v);
    ADD(PAR_VOL_SLOPE1 + v); ADD(PAR_DRIVE1 + v); ADD(PAR_VOICE_DECIMATION1 + v);
    ADD(PAR_FREQ_LFO1 + v); ADD(PAR_AMOUNT_LFO1 + v); ADD(PAR_WAVE_LFO1 + v); ADD(PAR_RETRIGGER_LFO1 + v);
    ADD(PAR_SYNC_LFO1 + v); ADD(PAR_OFFSET_LFO1 + v); ADD(PAR_VOICE_LFO1 + v);
    ADD(PAR_VOLUME_MOD_ON_OFF1 + v); ADD(PAR_VELO_MOD_AMT_1 + v);
    ADD(PAR_TRANS1_VOL + v); ADD(PAR_TRANS1_WAVE + v); ADD(PAR_TRANS1_FREQ + v);
    if (v < 5) { ADD(PAR_VELOA1 + 2 * v); ADD(PAR_VELOD1 + 2 * v); }
    else { ADD(PAR_VELOA6); ADD(PAR_VELOD6_CLOSED); ADD(PAR_VELOD6_OPEN); }
    if (v < 3) {
        ADD(PAR_OSC_WAVE_DRUM1 + v); ADD(PAR_MOD_WAVE_DRUM1 + v); ADD(PAR_MIX_MOD_1 + v);
        ADD(PAR_FMAMNT1 + 2 * v); ADD(PAR_FM_FREQ1 + 2 * v);
    }
    if (v < 4) { ADD(PAR_MOD_EG1 + v); ADD(PAR_MODAMNT1 + v); ADD(PAR_PITCH_SLOPE1 + v); }
    if (v == 3) { ADD(PAR_OSC_WAVE_SNARE); ADD(PAR_NOISE_FREQ1); ADD(PAR_MIX1); ADD(PAR_REPEAT4); }
    if (v == 4) {
        ADD(PAR_WAVE1_CYM); ADD(PAR_WAVE2_CYM); ADD(PAR_WAVE3_CYM); ADD(PAR_REPEAT5);
        ADD(PAR_MOD_OSC_F1_CYM); ADD(PAR_MOD_OSC_F2_CYM); ADD(PAR_MOD_OSC_GAIN1_CYM); ADD(PAR_MOD_OSC_GAIN2_CYM);
    }
    if (v == 5) {
        ADD(PAR_WAVE1_HH); ADD(PAR_WAVE2_HH); ADD(PAR_WAVE3_HH);
        ADD(PAR_MOD_OSC_F1); ADD(PAR_MOD_OSC_F2); ADD(PAR_MOD_OSC_GAIN1); ADD(PAR_MOD_OSC_GAIN2);
    }
#undef ADD
    m[k++] = (uint16_t)(PAR_VEL_DEST_1 + v);
    m[k++] = (uint16_t)(PAR_TARGET_LFO1 + v);
    *no = n;
    *nm = k;
}

int tvd_load_voice(TvDrum *d, int voice, int kit)
{
    uint16_t o[48], m[2];
    int no, nm, i;
    const uint8_t *src;
    if (!vok(voice) || kit < 0 || kit >= TVD_KITS) return -1;
    src = kit == TVD_KITS - 1 ? g_init.b : lxr_factoryKits[kit].b;
    voice_params(voice, o, &no, m, &nm);
    ENTER(d);
    for (i = 0; i < no; i++) { d->kit.b[o[i]] = src[o[i]]; lxr_dsp_setParam(o[i], src[o[i]]); }
    for (i = 0; i < nm; i++) { d->kit.b[m[i]] = src[m[i]]; lxr_dsp_setParam(m[i], src[m[i]]); }
    if (voice < 6) {   /* the LXR-02's pulse width of this voice */
        d->kit.b[LXR_NUM_SOUND_PARAMS + EXT_PWM1 + voice] = src[LXR_NUM_SOUND_PARAMS + EXT_PWM1 + voice];
        lxr_dsp_setExt((uint8_t)(EXT_PWM1 + voice), src[LXR_NUM_SOUND_PARAMS + EXT_PWM1 + voice]);
    }
    LEAVE();
    return 0;
}

void tvd_set_control(TvDrum *d, int voice, int ctl, float v)
{
    const DC *c;
    int b;
    if (!vok(voice) || !cok(ctl)) return;
    c = &CTL[voice][ctl];
    b = to_byte(c, v);
    d->kit.b[c->par] = (uint8_t)b;
    ENTER(d);
    lxr_dsp_setParam(c->par, (uint8_t)b);
    LEAVE();
}

float tvd_get_control(const TvDrum *d, int voice, int ctl)
{
    if (!vok(voice) || !cok(ctl)) return 0.f;
    return from_byte(&CTL[voice][ctl], d->kit.b[CTL[voice][ctl].par]);
}

void tvd_set_raw(TvDrum *d, int par, int value)
{
    if (par < 0 || par >= LXR_NUM_SOUND_PARAMS) return;
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    d->kit.b[par] = (uint8_t)value;
    ENTER(d);
    lxr_dsp_setParam((uint16_t)par, (uint8_t)value);
    LEAVE();
}

int tvd_get_raw(const TvDrum *d, int par) { return par >= 0 && par < LXR_NUM_SOUND_PARAMS ? d->kit.b[par] : -1; }

void tvd_set_ext(TvDrum *d, int ext, int value)
{
    if (ext < 0 || ext >= LXR_NUM_EXT) return;
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    d->kit.b[LXR_NUM_SOUND_PARAMS + ext] = (uint8_t)value;
    if (ext <= EXT_PWM6) {
        ENTER(d);
        lxr_dsp_setExt((uint8_t)ext, (uint8_t)value);
        LEAVE();
    } else lxr_fx_set(&d->fx, (uint8_t)ext, (uint8_t)value);
}

int tvd_get_ext(const TvDrum *d, int ext) { return ext >= 0 && ext < LXR_NUM_EXT ? d->kit.b[LXR_NUM_SOUND_PARAMS + ext] : -1; }

void tvd_set_fader(TvDrum *d, int voice, float v)
{
    if (!vok(voice)) return;
    d->fader[voice] = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
}

float tvd_get_fader(const TvDrum *d, int voice) { return vok(voice) ? d->fader[voice] : 0.f; }

void tvd_set_pan(TvDrum *d, int voice, float v)
{
    static const uint16_t pans[TVD_VOICES] = {PAR_PAN1, PAR_PAN2, PAR_PAN3, PAR_PAN4, PAR_PAN5, PAR_PAN6};
    int b;
    if (!vok(voice)) return;
    b = (int)((v < 0.f ? 0.f : v > 1.f ? 1.f : v) * 127.f + 0.5f);
    d->kit.b[pans[voice]] = (uint8_t)b;
    ENTER(d);
    lxr_dsp_setParam(pans[voice], (uint8_t)b);
    LEAVE();
}

/* ---------------------------------------------------------------- render */
/* a stereo ring (left and right share the read index and the count) */
static void ring_push(float *rl, float *rr, int *rd, int *cnt, const float *l, const float *r, int n)
{
    int i, w;
    if (*cnt + n > RING) {   /* nobody is reading this one: drop the oldest */
        const int drop = *cnt + n - RING;
        *rd = (*rd + drop) % RING;
        *cnt -= drop;
    }
    w = (*rd + *cnt) % RING;
    for (i = 0; i < n; i++) { rl[w] = l[i]; rr[w] = r[i]; if (++w == RING) w = 0; }
    *cnt += n;
}

/* ADDS up to n frames out of a ring; returns how many it took */
static int ring_take(const float *rl, const float *rr, int *rd, int *cnt, float *L, float *R, int n)
{
    int i, r = *rd;
    if (n > *cnt) n = *cnt;
    for (i = 0; i < n; i++) { L[i] += rl[r]; R[i] += rr[r]; if (++r == RING) r = 0; }
    *rd = r;
    *cnt -= n;
    return n;
}

/* one 32-frame block of every voice and of the FX unit, into the rings */
static void pump(TvDrum *d)
{
    float vL[TVD_VOICES][TVD_BLOCK], vR[TVD_VOICES][TVD_BLOCK], fL[TVD_BLOCK], fR[TVD_BLOCK], g[TVD_VOICES];
    int v, i;
    for (v = 0; v < TVD_VOICES; v++) g[v] = taper(d->fader[v]);
    ENTER(d);
    lxr_dsp_render32_split(vL, vR, fL, fR, g, &d->fx);
    LEAVE();
    for (v = 0; v < TVD_VOICES; v++) {
        float pk = 0.f;
        for (i = 0; i < TVD_BLOCK; i++) {
            const float a = fabsf(vL[v][i]) + fabsf(vR[v][i]);
            if (a > pk) pk = a;
        }
        if (pk > 1e-5f) d->idle[v] = 0;
        else if (d->idle[v] < (1u << 30)) d->idle[v] += TVD_BLOCK;
        ring_push(d->ringL[v], d->ringR[v], &d->rd[v], &d->cnt[v], vL[v], vR[v], TVD_BLOCK);
    }
    ring_push(d->fxL, d->fxR, &d->frd, &d->fcnt, fL, fR, TVD_BLOCK);
}

void tvd_render_voice(TvDrum *d, int voice, float *L, float *R, int n)
{
    if (!vok(voice)) return;
    while (n > 0) {
        int took;
        if (d->cnt[voice] == 0) pump(d);
        took = ring_take(d->ringL[voice], d->ringR[voice], &d->rd[voice], &d->cnt[voice], L, R, n);
        L += took;
        R += took;
        n -= took;
    }
}

void tvd_render_fx(TvDrum *d, float *L, float *R, int n)
{
    while (n > 0) {
        int took;
        if (d->fcnt == 0) pump(d);
        took = ring_take(d->fxL, d->fxR, &d->frd, &d->fcnt, L, R, n);
        L += took;
        R += took;
        n -= took;
    }
}

int tvd_voice_active(const TvDrum *d, int voice) { return vok(voice) && d->idle[voice] < IDLE_FRAMES; }
