/* TrackerXXL own engine ("own"): our own code for the Tracker workflow on the Force. No Polyend firmware is used here.
 * The skin (the Tracker's pictures, port/skin) is driven by name through the scene slots; sound comes from our sample engine and the
 * TrackerXXL voice library (ZYNTH, BAZZLINE, LXRXXL drums).  Licence: LXR02 Open-Source (non-commercial, with source). */
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace own
{
/* ------------------------------------------------------------------ sizes */
constexpr int kTracks = 8;
constexpr int kInstr = 48;
constexpr int kPat = 64;          /* patterns 1..64 */
constexpr int kMaxLen = 128;      /* steps per pattern */
constexpr int kSong = 64;         /* song slots */
constexpr int kSr = 44100;
constexpr int kBlock = 128;
constexpr size_t kSampleMemMax = 64u * 1024u * 1024u;   /* bytes of sample data */

/* performance effects (the Tracker's per-track Performance page); values are offsets -100..100 unless noted */
enum PerfFx : uint8_t { PF_NONE = 0, PF_VOL, PF_PAN, PF_TUNE, PF_LP, PF_HP, PF_BP, PF_DEL, PF_REV, PF_POS, PF_END, PF_PLAY, PF_REPEAT, PF_BITS, PF_DRIVE, PF_COUNT };

/* ------------------------------------------------------------------ pattern data */
enum : uint8_t { N_NONE = 255, N_OFF = 254, N_CUT = 253, N_FADE = 252 };
enum FxType : uint8_t { FX_NONE = 0, FX_VOL, FX_PAN, FX_CUT, FX_RES, FX_TEMPO, FX_OFFSET, FX_RETRIG, FX_REVERSE, FX_CHANCE, FX_COUNT };
struct Step
{
    uint8_t note = N_NONE, instr = 255;
    uint8_t fx[2] = { 0, 0 };
    int16_t fv[2] = { 0, 0 };
};
struct Pattern
{
    int len = 16;
    char name[16] = "";
    Step s[kTracks][kMaxLen];
};

/* ------------------------------------------------------------------ instruments and samples */
enum PlayMode : uint8_t { PM_ONESHOT, PM_FWD, PM_BWD, PM_PING, PM_SLICE, PM_BEAT, PM_WAVETABLE, PM_GRANULAR, PM_COUNT };
struct Sample
{
    std::string name, path;
    int ch = 1;
    std::vector<int16_t> d[2];
    size_t frames() const { return d[0].size(); }
    size_t bytes() const { return (d[0].size() + d[1].size()) * 2; }
};
struct Instr
{
    bool used = false;
    char name[32] = "";
    int kind = 0;                 /* 0 sample, 1 synth voice */
    int vol = 50;                 /* 0..100, 50 = 0 dB */
    int pan = 0;                  /* -50..50 */
    int tune = 0;                 /* -24..24 semitones */
    int fine = 0;                 /* -100..100 cents */
    int ftype = 0;                /* 0 off, 1 LP, 2 HP, 3 BP */
    int cutoff = 100, res = 0;    /* 0..100 */
    int mode = PM_ONESHOT;
    double start = 0, end = 1, loop1 = 0, loop2 = 1;   /* 0..1 of the sample */
    int slices = 0; double slice[32] = {};
    int zoom = 100;               /* % x100 : 100 = 1.00 */
    int atk = 0, dec = 0, sus = 100, rel = 20;   /* amp envelope 0..100 */
    int rev = 0, del = 0;         /* sends 0..100 */
    int voice = 0;                /* synth: tv voice 0..24 */
    float ctl[20] = {};           /* synth controls 0..1 */
    int starter = 0;
    int poly = 1;
};

/* ------------------------------------------------------------------ project */
struct Master
{
    int volume = 50;              /* 0..100, 50 = 0 dB */
    int saturation = 1, bass = 0, space = 0;
    int revSize = 50, revDamp = 50, revPre = 0, revDiff = 70, revMix = 30;
    int delFb = 40, delTime = 5, delSync = 1, delPing = 0, delMix = 30;
    int limThr = 100, limAtk = 20, limRel = 40;
    int eqLo = 0, eqMid = 0, eqHi = 0;
};
struct Project
{
    char name[40] = "Project";
    float tempo = 130.f;
    int swing = 50;
    Pattern pat[kPat];
    Instr ins[kInstr];
    int songLen = 1; uint8_t song[kSong] = {};
    uint8_t mute[kTracks] = {};
    int trackVol[kTracks] = {};   /* 0..100, filled to 80 */
    int trackPan[kTracks] = {};
    Master master;
};

/* ------------------------------------------------------------------ helpers shared by the parts */
const char* noteName(int note, char* buf);
const char* cellText(const Step& st, int param, char* buf);   /* param 0 note, 1 instrument, 2 fx1, 3 fx2 */
const char* fxName(int type);
char fxChar(int type);
int fxMin(int type); int fxMax(int type);
int clampi(int v, int lo, int hi);
extern int g_log;
}
namespace xxl { void logf(const char* fmt, ...); }
