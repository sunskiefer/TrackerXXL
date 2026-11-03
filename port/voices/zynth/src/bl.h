/* TrackerXXL voice library -- LOCAL REPLACEMENT for LXRXXL ZYNTH's src/bl.h.
 *
 * Upstream bl.h declares the whole plugin (project, sequencer, front panel, presets, randomizers). The voice sources
 * vendored here (voice.c, sound.c, engines.c, eng_*.c) only need the pieces below, which are copied verbatim from
 * ZYNTH 9441e6a src/bl.h: SR, the Sound and Voice structures (without the sequencer's NUM_TRACKS-sized project), the
 * voice.c / sound.c prototypes, clampf and bl_rand. Nothing here changes behaviour; see VENDORED.md.
 *
 * Original header comment: LXRXXL ZYNTH -- one instance ... the same voice chain (FM, sub, noise, swarm, drive, filter,
 * VCA), with the raw tone of each track made by one of sixteen (now nineteen) voice engines picked per track (eng.h). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ids.h"
#include "eng.h"

#define SR            44100.0f

/* ---------------------------------------------------------------- a track's sound (verbatim) */
typedef struct {
    float v[SP_COUNT];  /* 0..1, or an option's index; v[SP_V1..SP_V20]: the twenty controls of the voice in use */
    float full[NUM_ENGINES][SP_COUNT];   /* each voice's whole sound as last left on this track (its home sound until
                                            then): switching back finds it; LEVEL and PAN stay the track's */
} Sound;

/* ---------------------------------------------------------------- the voice (voice.c) (verbatim) */
#define BBD_LEN 2048
#define PM_LEN 4096                  /* FM LVL on any voice: the tone through a delay line swung by a sine */
typedef struct {
    Eng eng[2];                      /* the voice's engine; the second one: UNISON's detuned copy */
    uint32_t phSub, phPm;            /* the sub oscillator; FM LVL's modulator */
    uint32_t noise;
    float pm[PM_LEN];
    int pmW;
    uint8_t trig;                    /* a new note for the engine (not a slide) */
    /* pitch */
    float note, target, glide;       /* the playing note (semitones, with slide), where it slides to, the slide rate */
    float penv;                      /* the pitch envelope 0..1 */
    /* envelopes */
    float aenv, fenv;                /* amp, filter */
    uint8_t aStage, fStage;          /* 0 off, 1 attack, 2 decay */
    /* the VOICE page's play row: the modulation envelope (attack, decay) and the vibrato */
    float menv;
    uint8_t menvStage;
    float vibPh, vibAmt;
    uint8_t gate;
    float accent;                    /* this note's accent 0..1 */
    float sweep;                     /* the accent sweep's charge */
    float vel;
    /* modulator */
    double lfoPh;
    float lfoHold, lfoPrev, lfoNext, lfoOut;
    uint32_t lfoRnd;
    uint8_t modOn;                   /* this note takes the modulator (MOD steps) */
    /* filter (2x oversampled) */
    float s1, s2, d1, d2, d3, d4;    /* the Polivoks SVF's two integrators; the diode ladder's four stages */
    float toneZ, dcX;
    float cutSm, resSm;
    /* swarm */
    float bbd[BBD_LEN];
    int bbdW;
    double swPh[2];
    float bbdLp;
    uint8_t active;                  /* sounding (or a tail still running) */
    uint32_t idle;                   /* frames of silence */
    /* CHAOS (alien.c, not part of the voice library: stays 0), this block: the track's value and where it goes */
    float chaos;
    uint8_t chaosDest;
} Voice;

/* voice.c */
void voice_init(Voice *v);
void voice_noteOn(Voice *v, const Sound *s, float note, float vel, int accent, int slide, int penv, int mod);
void voice_noteOff(Voice *v);
void voice_render(Voice *v, const Sound *s, float bpm, float *L, float *R, int n);   /* adds into L/R */
void voice_kill(Voice *v);
void voice_attach(Voice *v);                  /* the Mutable engines' state (once, at the instance's start) */
void voice_detach(Voice *v);

/* sound.c: the voice selector and each voice's sliders */
void sound_setVoice(Sound *s, int type);      /* change the voice: the sliders go to (and come back from) its memory */
void sound_setSlider(Sound *s, int k, float v);   /* V1..V6: the voice in use's slider (and its memory) */
void sound_resetVoice(Sound *s, int type);    /* every slider of that voice back to its default */
void sound_sync(Sound *s);                    /* v[] written into the voice in use's memory */
void sound_home(int type, float *v);          /* a voice's home sound: the chain set so the voice sounds as itself */
void sound_starter(Sound *s, int type, int idx);   /* a voice's starter sound (0: the home sound) */
const char *sound_starterName(int type, int idx);
void init_sound(Sound *s);                    /* the default sound (every control at its default) */
#define NUM_STARTER_SOUNDS 8

/* shared helpers */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline uint32_t bl_rand(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }
