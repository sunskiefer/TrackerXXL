/* TrackerXXL voice library -- LOCAL REPLACEMENT for LXRXXL BAZZLINE's src/bl.h (f47ebb6).
 *
 * BAZZLINE's bl.h declares the whole plugin. fx.c (the delay and the gate/duck, vendored unchanged next to this file)
 * only needs: SR, the Fx structure (copied verbatim, with DLY_LEN), a reduced Bl holding just the members fx.c reads
 * (p.g[], p.dlySync, p.dlyTap, p.gdOn and fx), bl_tempo() and clampf(). fx.c calls bl_tempo(); it is defined in
 * tv_fx.c from the tempo the host gives the library. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ids.h"

#define SR 44100.0f

/* ---- verbatim from BAZZLINE src/bl.h ---- */
#define DLY_LEN 65536                /* 1.48 s at 44.1 kHz: 800 ms plus room for the tape's slew */
typedef struct {
    float *buf[2];
    int w;
    float rd;                        /* the read distance in frames (slewed: tape mode bends pitch) */
    float rdOld, xfade;              /* digital mode: the old tap while crossfading to a new time */
    float lpz[2], hpz[2];
    double wowPh;
    uint8_t hold, add, rev;
    float holdLen;                   /* HOLD: the frozen loop's length */
    int revPos;                      /* REVERSE: position in the reversed window */
    float wetSm, fbSm, inSm;
    /* gate/duck */
    float gdEnv;
    uint32_t gdHold;
    uint8_t gdStage;                 /* 0 idle, 1 attack, 2 hold, 3 release */
    /* tap */
    double lastTap;
    int taps;
} Fx;

/* ---- reduced: only what fx.c reads ---- */
typedef struct {
    float g[G_COUNT];   /* the delay and the gate/duck (ids.h G_*) */
    float dlyTap;       /* the tapped period in seconds (dlySync 2) */
    uint8_t dlySync;    /* 0 free (ms), 1 the host's beat, 2 tapped */
    uint8_t gdOn;       /* the gate/duck is on */
} BzProject;
typedef struct Bl {
    BzProject p;
    Fx fx;
    float tempo;        /* bl_tempo()'s source (BAZZLINE: the sequencer's tempo) */
} Bl;

float bl_tempo(Bl *x);                       /* tv_fx.c */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* fx.c */
void fx_init(Fx *f);
void fx_free(Fx *f);
void fx_render(Bl *x, float *L, float *R, const float *sL, const float *sR, int n);   /* s: the delay sends */
void fx_duckTrigger(Bl *x);
float fx_delaySeconds(Bl *x);
int fx_syncFactorIndex(float knob);
