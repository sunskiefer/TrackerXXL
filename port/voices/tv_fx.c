/* TrackerXXL voice library -- LXRXXL BAZZLINE's delay and gate/duck: bazz/src/fx.c (vendored unchanged) driven through the
 * reduced Bl of bazz/src/bl.h. The delay follows the Erica Synths DIY Delay manual (see fx.c's header). Written for this
 * library. The 2 x 131072-float buffers are allocated in tvfx_create() (fx_init); rendering allocates nothing. */
#include <stdlib.h>
#include <string.h>
#include "bl.h"
#include "tv_internal.h"

struct TvFx { Bl bl; };

/* the parameter table: BAZZLINE src/param_map.h GLOBAL_DEFS (key, name, default, options), without "gd_ch" (the MIDI channel
 * of the duck trigger: the host decides what triggers, tvfx_duck_trigger) */
typedef struct { const char *key, *name; float def; int nopts; const char *const *opts; } FxDef;
static const char *const OPTS_DLY_MODE[] = {"TAPE", "DIGITAL"};
static const char *const OPTS_GD_MODE[] = {"GATE", "DUCK"};
static const FxDef FXDEFS[G_GD_CH] = {
    {"dly_time", "DELAY TIME", 0.5f, 0, 0},
    {"dly_in", "DELAY IN LVL", 0.7f, 0, 0},
    {"dly_fb", "FEEDBACK", 0.35f, 0, 0},
    {"dly_wet", "DRY/WET", 0.0f, 0, 0},
    {"dly_mode", "DELAY MODE", 0.0f, 2, OPTS_DLY_MODE},
    {"gd_mode", "GATE/DUCK", 1.0f, 2, OPTS_GD_MODE},
    {"gd_depth", "DEPTH", 0.7f, 0, 0},
    {"gd_hold", "HOLD", 0.15f, 0, 0},
    {"gd_atk", "ATTACK", 0.1f, 0, 0},
    {"gd_rel", "RELEASE", 0.35f, 0, 0},
};

float bl_tempo(Bl *x) { return x->tempo > 0.f ? x->tempo : 120.f; }

TvFx *tvfx_create(void)
{
    TvFx *f = (TvFx *)calloc(1, sizeof *f);
    int i;
    if (!f) return NULL;
    for (i = 0; i < G_GD_CH; i++) f->bl.p.g[i] = FXDEFS[i].def;
    f->bl.p.g[G_GD_CH] = 9.f;
    f->bl.tempo = 120.f;
    fx_init(&f->bl.fx);
    if (!f->bl.fx.buf[0] || !f->bl.fx.buf[1]) { fx_free(&f->bl.fx); free(f); return NULL; }
    return f;
}

void tvfx_destroy(TvFx *f)
{
    if (!f) return;
    fx_free(&f->bl.fx);
    free(f);
}

void tvfx_reset(TvFx *f)
{
    int i;
    fx_init(&f->bl.fx);   /* keeps the buffers, clears them and every state */
    for (i = 0; i < G_GD_CH; i++) f->bl.p.g[i] = FXDEFS[i].def;
    f->bl.p.dlySync = 0;
    f->bl.p.dlyTap = 0.f;
    f->bl.p.gdOn = 0;
}

void tvfx_set(TvFx *f, int g, float v)
{
    if (g < 0 || g >= G_GD_CH) return;
    if (FXDEFS[g].nopts) {
        int i = (int)(v + 0.5f);
        if (i < 0) i = 0;
        if (i >= FXDEFS[g].nopts) i = FXDEFS[g].nopts - 1;
        f->bl.p.g[g] = (float)i;
    } else f->bl.p.g[g] = clampf(v, 0.f, 1.f);
}

float tvfx_get(const TvFx *f, int g) { return g >= 0 && g < G_GD_CH ? f->bl.p.g[g] : 0.f; }
void tvfx_set_sync(TvFx *f, int mode) { f->bl.p.dlySync = (uint8_t)(mode < 0 ? 0 : mode > 2 ? 2 : mode); }
void tvfx_set_tap_period(TvFx *f, float s) { f->bl.p.dlyTap = s > 0.f ? s : 0.f; }
void tvfx_set_tempo(TvFx *f, float bpm) { f->bl.tempo = bpm > 0.f ? bpm : 120.f; }
void tvfx_set_gate_duck_on(TvFx *f, int on) { f->bl.p.gdOn = on ? 1 : 0; }

void tvfx_set_toggle(TvFx *f, int which, int on)
{
    switch (which) {
    case TVFX_HOLD: f->bl.fx.hold = on ? 1 : 0; break;
    case TVFX_ADD: f->bl.fx.add = on ? 1 : 0; break;
    case TVFX_REVERSE:
        if ((on ? 1 : 0) != f->bl.fx.rev) f->bl.fx.revPos = 0;
        f->bl.fx.rev = on ? 1 : 0;
        break;
    default: break;
    }
}

void tvfx_duck_trigger(TvFx *f) { fx_duckTrigger(&f->bl); }
float tvfx_delay_seconds(TvFx *f) { return fx_delaySeconds(&f->bl); }
void tvfx_render(TvFx *f, float *L, float *R, const float *sL, const float *sR, int n) { fx_render(&f->bl, L, R, sL, sR, n); }

int tvfx_param_count(void) { return G_GD_CH; }
const char *tvfx_param_key(int g) { return g >= 0 && g < G_GD_CH ? FXDEFS[g].key : ""; }
const char *tvfx_param_name(int g) { return g >= 0 && g < G_GD_CH ? FXDEFS[g].name : ""; }
float tvfx_param_default(int g) { return g >= 0 && g < G_GD_CH ? FXDEFS[g].def : 0.f; }
int tvfx_param_options(int g) { return g >= 0 && g < G_GD_CH ? FXDEFS[g].nopts : 0; }
const char *tvfx_param_option_name(int g, int o)
{
    return g >= 0 && g < G_GD_CH && o >= 0 && o < FXDEFS[g].nopts ? FXDEFS[g].opts[o] : "";
}
