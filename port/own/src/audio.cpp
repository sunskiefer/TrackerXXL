#include "audio.h"
#include "model.h"
#include "trackerxxl_voices.h"
#include <algorithm>
#include <math.h>
#include <time.h>

using namespace own;
namespace audio
{
namespace
{
constexpr int kSynthSlots = 9, kScratch = 8, kDrum0 = 9;
constexpr float kPi = 3.14159265f;

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1[2] = {}, x2[2] = {}, y1[2] = {}, y2[2] = {};
    void shelf(bool high, float f, float gdb)
    {
        const float A = powf(10.f, gdb / 40.f), w = 2 * kPi * f / kSr, cs = cosf(w), sn = sinf(w), al = sn / 2 * sqrtf(2.f), tq = 2 * sqrtf(A) * al;
        float a0;
        if (!high) { b0 = A * ((A + 1) - (A - 1) * cs + tq); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - tq); a0 = (A + 1) + (A - 1) * cs + tq; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - tq; }
        else { b0 = A * ((A + 1) + (A - 1) * cs + tq); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - tq); a0 = (A + 1) - (A - 1) * cs + tq; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - tq; }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    void peak(float f, float gdb, float q)
    {
        const float A = powf(10.f, gdb / 40.f), w = 2 * kPi * f / kSr, al = sinf(w) / (2 * q), cs = cosf(w);
        const float a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * cs / a0; b2 = (1 - al * A) / a0; a1 = -2 * cs / a0; a2 = (1 - al / A) / a0;
    }
    inline float run(int c, float x)
    {
        const float y = b0 * x + b1 * x1[c] + b2 * x2[c] - a1 * y1[c] - a2 * y2[c];
        x2[c] = x1[c]; x1[c] = x; y2[c] = y1[c]; y1[c] = y; return y;
    }
};

struct Voice
{
    bool on = false;
    int instr = -1, mode = 0, stage = 0, ftype = 0;
    const Sample* s = nullptr;
    double pos = 0, inc = 1, a = 0, b = 0, l1 = 0, l2 = 0, wtW = 2048, gA = 0, gB = 0;
    int dir = 1;
    float env = 0, atkR = 1, decR = 0, sus = 1, relR = 0.001f, gain = 1, pl = 1, pr = 1, fc = 1, fq = 0.5f, lp[2] = {}, bp[2] = {}, rev = 0, del = 0, fade = 0, gp = 0;
    int gT[2] = { 0, 1200 };
    bool oneShotDone = false;
    int trk = 0, vel = 100, bits = 16; double inc0 = 1; float drive = 0; bool pfApplied = false;
};
struct Track
{
    Voice v[2];                                   /* v[0] sounding, v[1] being cut */
    int lastInstr = -1, synthKey = -1;
    int volMod = -1, panMod = -999, cutMod = -1, resMod = -1;
    int retrigPeriod = 0, retrigLeft = 0, retrigNote = 0, retrigVel = 100, retrigInstr = 0;
    bool synthOn = false; int drumSlot = -1;
    float meter = 0;
    int pf[PF_COUNT] = {}; uint32_t pfOn = 0; int lastNote = 60, rpLeft = 0;   /* performance effects */
};

struct Rev
{
    static constexpr int NC = 4, NA = 2;
    std::vector<float> c[2][NC], ap[2][NA], pre[2];
    int ci[2][NC] = {}, ai[2][NA] = {}, pi = 0; float lp[2][NC] = {};
    void init()
    {
        static const int cl[NC] = { 1116, 1188, 1277, 1356 }, al[NA] = { 556, 441 };
        for (int ch = 0; ch < 2; ch++)
        {
            for (int i = 0; i < NC; i++) c[ch][i].assign(cl[i] + ch * 23, 0.f);
            for (int i = 0; i < NA; i++) ap[ch][i].assign(al[i] + ch * 11, 0.f);
            pre[ch].assign(8820, 0.f);
        }
    }
    void run(float in, float& oL, float& oR, float fb, float damp, int preN, float diff)
    {
        const float x = in;
        float o[2];
        for (int ch = 0; ch < 2; ch++)
        {
            pre[ch][pi] = x;
            const int pr = (pi + (int)pre[ch].size() - preN) % (int)pre[ch].size();
            const float xin = pre[ch][pr] * 0.25f;
            float acc = 0;
            for (int i = 0; i < NC; i++)
            {
                auto& b = c[ch][i]; const float y = b[ci[ch][i]];
                lp[ch][i] = y * (1 - damp) + lp[ch][i] * damp;
                b[ci[ch][i]] = xin + lp[ch][i] * fb;
                if (++ci[ch][i] >= (int)b.size()) ci[ch][i] = 0;
                acc += y;
            }
            for (int i = 0; i < NA; i++)
            {
                auto& b = ap[ch][i]; const float y = b[ai[ch][i]];
                const float g = 0.3f + 0.4f * diff;
                const float out = -acc + y;
                b[ai[ch][i]] = acc + y * g;
                if (++ai[ch][i] >= (int)b.size()) ai[ch][i] = 0;
                acc = out;
            }
            o[ch] = acc;
        }
        if (++pi >= (int)pre[0].size()) pi = 0;
        oL = o[0]; oR = o[1];
    }
};

struct A_t
{
    TvInstance* tv = nullptr;
    Track tr[kTracks];
    Voice prev;
    const Sample* prevS = nullptr;
    float prevVol = 1.f;
    bool hostSync = true, playing = false, song = false, hostDriven = false;
    bool hostPlayingNow = false, hostWas = false, hostFresh = false; double hostPpq = -1, hostBpm = 0;
    double pos = 0, lastBpm = 0, tempoNow = 130; int64_t lastFired = -1;
    int editPat = 0, curPat = -1, curStep = -1, curSlot = -1; int tempoFx = 0;
    int patChange = -1;
    double timeSec = 0;
    Rev rev; std::vector<float> dl[2]; int dp = 0; float dlFb[2] = {};
    Biquad eqLo, eqMid, eqHi, bassB; Master lastM; bool eqInit = false;
    float limG = 1.f, peak = 0, lastPos = -1; uint32_t rnd = 12345;
    double tapT = 0; int tapN = 0;
} A;

inline float frand() { A.rnd = A.rnd * 1664525u + 1013904223u; return (A.rnd >> 8) * (1.f / 16777216.f); }
float volGain(int v) { if (v <= 0) return 0.f; if (v <= 50) { const float x = v / 50.f; return x * x; } return powf(10.f, (v - 50) * 0.24f / 20.f); }
float envTime(int v, float mx) { const float x = v / 100.f; return std::max(0.0005f, x * x * mx); }

/* ------------------------------------------------------------ voices */
void startVoice(Voice& V, int instrIdx, const Sample* s, int note, int vel, int trk, int offsetPct, bool reverse)
{
    Project& P = proj(); const Instr& I = P.ins[instrIdx];
    Track& PT = A.tr[trk];
    if ((PT.pfOn >> PF_PLAY & 1) && PT.pf[PF_PLAY] == 1) reverse = !reverse;
    V = Voice(); V.on = true; V.instr = instrIdx; V.s = s; V.mode = I.mode; V.ftype = I.ftype; V.trk = trk; V.vel = vel;
    const double N = (double)s->frames();
    V.a = I.start * N; V.b = I.end * N; if (V.b <= V.a + 1) V.b = N;
    V.l1 = I.loop1 * N; V.l2 = I.loop2 * N; if (V.l2 <= V.l1 + 8) { V.l1 = V.a; V.l2 = V.b; }
    double st = (note - 60) + I.tune + I.fine / 100.0;
    if (I.mode == PM_SLICE || I.mode == PM_BEAT)
    {
        st = I.tune + I.fine / 100.0;
        int n = I.slices > 0 ? I.slices : 1; int idx = (note - 36) % n; if (idx < 0) idx += n;
        double sa = I.slices > 0 ? I.slice[idx] : 0.0, sb = (I.slices > 0 && idx + 1 < I.slices) ? I.slice[idx + 1] : 1.0;
        V.a = sa * N; V.b = sb * N; if (V.b <= V.a + 1) V.b = N;
    }
    if (I.mode == PM_WAVETABLE)
    {
        V.wtW = std::min(2048.0, N); V.gA = I.start * std::max(0.0, N - V.wtW);
        V.inc = 440.0 * pow(2.0, (note - 69 + I.tune + I.fine / 100.0) / 12.0) * V.wtW / kSr; V.pos = 0;
    }
    else if (I.mode == PM_GRANULAR) { V.gA = I.start * N; V.inc = pow(2.0, st / 12.0); V.gT[0] = 0; V.gT[1] = 600; }
    else V.inc = pow(2.0, st / 12.0);
    if (PT.pfOn >> PF_POS & 1)
    {
        const double d = PT.pf[PF_POS] / 100.0 * N;
        if (I.mode == PM_WAVETABLE || I.mode == PM_GRANULAR) V.gA = std::max(0.0, std::min(N - 2.0, V.gA + d));
        else { V.a = std::max(0.0, std::min(N - 2.0, V.a + d)); if (V.b <= V.a + 1) V.b = N; if (V.l1 < V.a) V.l1 = V.a; if (V.l2 <= V.l1 + 8) V.l2 = V.b; }
    }
    if (PT.pfOn >> PF_END & 1)
    {
        const double d = PT.pf[PF_END] / 100.0 * N;
        V.b = std::max(V.a + 2.0, std::min((double)N, V.b + d)); if (V.l2 > V.b) V.l2 = V.b; if (V.l2 <= V.l1 + 8) V.l1 = V.a;
    }
    V.inc0 = V.inc;
    V.dir = 1; V.pos = V.a;
    if (offsetPct > 0 && I.mode <= PM_PING) V.pos = V.a + (V.b - V.a) * offsetPct / 100.0;
    if (I.mode == PM_BWD) { V.dir = -1; V.pos = V.l2 - 1; }
    if (reverse) { V.dir = -V.dir; V.pos = V.dir < 0 ? V.b - 1 : V.a; }
    V.atkR = 1.f / (envTime(I.atk, 2.f) * kSr); V.decR = (1.f - I.sus / 100.f) / (envTime(I.dec, 3.f) * kSr); V.sus = I.sus / 100.f;
    V.relR = 1.f / (envTime(I.rel, 3.f) * kSr);
    V.stage = I.atk > 0 ? 1 : (I.dec > 0 && I.sus < 100 ? 2 : 3); V.env = I.atk > 0 ? 0.f : 1.f; if (V.stage == 2) V.env = 1.f;
    Track& T = A.tr[trk];
    int vol = I.vol; float g = volGain(vol) * (vel / 100.f);
    if (T.volMod >= 0) g *= volGain(T.volMod) * 1.0f;
    V.gain = g;
    int pn = T.panMod > -999 ? T.panMod : I.pan; pn = clampi(pn, -50, 50);
    const float ang = (pn + 50) / 100.f * kPi * 0.5f; V.pl = cosf(ang) * 1.4142f; V.pr = sinf(ang) * 1.4142f;
    if (V.ftype) { const int cu = T.cutMod >= 0 ? T.cutMod : I.cutoff, rs = T.resMod >= 0 ? T.resMod : I.res; const float fc = 20.f * powf(1000.f, cu / 100.f); V.fc = std::min(1.2f, 2 * sinf(kPi * std::min(fc, 18000.f) / kSr)); V.fq = 1.f - 0.97f * rs / 100.f * 0.97f; }
    V.rev = I.rev / 100.f; V.del = I.del / 100.f;
}
inline float rd(const Sample* s, int c, double p)
{
    const auto& d = s->d[c < s->ch ? c : 0]; const int64_t n = (int64_t)d.size();
    if (n < 2) return 0;
    int64_t i = (int64_t)p; if (i < 0) i = 0; if (i >= n - 1) return d[n - 1] * (1.f / 32768.f);
    const float f = (float)(p - i);
    return (d[i] + (d[i + 1] - d[i]) * f) * (1.f / 32768.f);
}
/* the track's performance effects on a sounding voice, recomputed from the instrument every block while any is on (and once more when they end) */
void perfApplyLive(Voice& V)
{
    Track& T = A.tr[V.trk]; const bool on = T.pfOn != 0;
    if (!on && !V.pfApplied) return;
    V.pfApplied = on;
    const Instr& I = proj().ins[V.instr]; const uint32_t m = T.pfOn;
    auto has = [m](int fx) { return (m >> fx & 1) != 0; };
    const int vol = has(PF_VOL) ? clampi(I.vol + T.pf[PF_VOL], 0, 100) : I.vol;
    V.gain = volGain(vol) * (V.vel / 100.f) * (T.volMod >= 0 ? volGain(T.volMod) : 1.f);
    int pn = T.panMod > -999 ? T.panMod : I.pan; if (has(PF_PAN)) pn += T.pf[PF_PAN] / 2; pn = clampi(pn, -50, 50);
    { const float ang = (pn + 50) / 100.f * kPi * 0.5f; V.pl = cosf(ang) * 1.4142f; V.pr = sinf(ang) * 1.4142f; }
    V.inc = has(PF_TUNE) ? V.inc0 * pow(2.0, T.pf[PF_TUNE] / 12.0) : V.inc0;
    int ft = I.ftype, cu = T.cutMod >= 0 ? T.cutMod : I.cutoff; const int rs = T.resMod >= 0 ? T.resMod : I.res;
    for (int k = 0; k < 3; k++) { const int fx = PF_LP + k; if (has(fx)) { ft = 1 + k; cu = clampi(cu + T.pf[fx], 0, 100); } }
    if (ft != V.ftype) { V.ftype = ft; for (int c = 0; c < 2; c++) V.lp[c] = V.bp[c] = 0; }
    if (V.ftype) { const float fc = 20.f * powf(1000.f, cu / 100.f); V.fc = std::min(1.2f, 2 * sinf(kPi * std::min(fc, 18000.f) / kSr)); V.fq = 1.f - 0.97f * rs / 100.f * 0.97f; }
    V.rev = clampi(I.rev + (has(PF_REV) ? T.pf[PF_REV] : 0), 0, 100) / 100.f; V.del = clampi(I.del + (has(PF_DEL) ? T.pf[PF_DEL] : 0), 0, 100) / 100.f;
    V.bits = has(PF_BITS) ? clampi(16 + T.pf[PF_BITS], 4, 16) : 16;
    V.drive = has(PF_DRIVE) ? clampi(T.pf[PF_DRIVE], 0, 100) / 100.f : 0.f;
}
/* adds one block of a voice to L/R; sends to rl (mono) */
bool renderVoice(Voice& V, float* L, float* R, float* sr_, float* sd, int n)
{
    if (!V.on || !V.s) return false;
    perfApplyLive(V);
    const Sample* s = V.s; const int64_t N = (int64_t)s->frames();
    for (int i = 0; i < n; i++)
    {
        if (V.stage == 0) { V.on = false; break; }
        float xl, xr;
        double p = V.pos;
        if (V.mode == PM_WAVETABLE)
        {
            const double q = V.gA + fmod(p, V.wtW); xl = rd(s, 0, q); xr = s->ch > 1 ? rd(s, 1, q) : xl;
            V.pos += V.inc; if (V.pos >= V.wtW) V.pos -= V.wtW;
        }
        else if (V.mode == PM_GRANULAR)
        {
            xl = xr = 0;
            for (int g = 0; g < 2; g++)
            {
                const float ph = V.gT[g] / 2400.f; const float w = 0.5f - 0.5f * cosf(2 * kPi * ph);
                const double q = V.gA + (V.gT[g]) * V.inc;
                xl += rd(s, 0, q) * w; xr += (s->ch > 1 ? rd(s, 1, q) : rd(s, 0, q)) * w;
                if (++V.gT[g] >= 2400) V.gT[g] = 0;
            }
        }
        else
        {
            if (p < 0 || p >= N) { V.on = false; V.stage = 0; break; }
            xl = rd(s, 0, p); xr = s->ch > 1 ? rd(s, 1, p) : xl;
            V.pos += V.inc * V.dir;
            switch (V.mode)
            {
                case PM_FWD: if (V.pos >= V.l2) V.pos = V.l1 + (V.pos - V.l2); break;
                case PM_BWD: if (V.pos < V.l1) V.pos = V.l2 - (V.l1 - V.pos); break;
                case PM_PING: if (V.dir > 0 && V.pos >= V.l2) { V.dir = -1; V.pos = V.l2 - (V.pos - V.l2); } else if (V.dir < 0 && V.pos < V.l1) { V.dir = 1; V.pos = V.l1 + (V.l1 - V.pos); } break;
                default: if ((V.dir > 0 && V.pos >= V.b) || (V.dir < 0 && V.pos < V.a)) { if (V.stage < 4) { V.stage = 4; V.relR = 1.f / 220.f; } if (V.pos >= N || V.pos < 0) V.on = false; } break;
            }
        }
        switch (V.stage)
        {
            case 1: V.env += V.atkR; if (V.env >= 1.f) { V.env = 1.f; V.stage = (V.decR > 0 ? 2 : 3); } break;
            case 2: V.env -= V.decR; if (V.env <= V.sus) { V.env = V.sus; V.stage = 3; } break;
            case 4: V.env -= V.relR; if (V.env <= 0) { V.env = 0; V.stage = 0; V.on = false; } break;
        }
        if (V.ftype)
        {
            float in[2] = { xl, xr }, out[2];
            for (int c = 0; c < 2; c++)
            {
                const float hp = in[c] - V.lp[c] - V.fq * V.bp[c];
                V.bp[c] += V.fc * hp; V.lp[c] += V.fc * V.bp[c];
                out[c] = V.ftype == 1 ? V.lp[c] : V.ftype == 2 ? hp : V.bp[c];
                if (!(fabsf(V.lp[c]) < 100.f)) { V.lp[c] = V.bp[c] = 0; }
            }
            xl = out[0]; xr = out[1];
        }
        if (V.bits < 16) { const float q = (float)(1 << (V.bits - 1)); xl = floorf(xl * q + 0.5f) / q; xr = floorf(xr * q + 0.5f) / q; }
        if (V.drive > 0.f) { const float k = 1.f + V.drive * 12.f, nk = 1.f / tanhf(k); xl = tanhf(xl * k) * nk; xr = tanhf(xr * k) * nk; }
        const float g = V.gain * V.env;
        const float m = (xl + xr) * 0.5f;
        const float ol = (V.s->ch > 1 ? xl : m) * g, or_ = (V.s->ch > 1 ? xr : m) * g;
        L[i] += ol * V.pl * 0.7071f; R[i] += or_ * V.pr * 0.7071f;
        const float mo = (ol + or_) * 0.5f; sr_[i] += mo * V.rev; sd[i] += mo * V.del;
    }
    A.lastPos = (V.mode != PM_WAVETABLE && V.mode != PM_GRANULAR && V.on) ? (float)(V.pos / std::max<double>(1.0, (double)N)) : (V.on ? A.lastPos : -1.f);
    return V.on;
}
void release(Voice& V, float sec) { if (V.on && V.stage < 4) { V.stage = 4; V.relR = V.env > 0 ? V.env / std::max(1.f, sec * kSr) : 1.f; } }
void cutVoice(Voice& V) { if (V.on) { V.stage = 4; V.relR = std::max(V.env, 0.0001f) / 96.f; } }

int bazz() { static int b = -2; if (b == -2) { b = -1; for (int i = 0; i < TV_NUM_SYNTH_VOICES; i++) if (strstr(tv_voice_name(i), "BAZZ")) b = i; } return b; }

void synthNote(int trk, const Instr& I, int note, int vel, int instrIdx)
{
    if (!A.tv) return;
    Track& T = A.tr[trk];
    int slot; const int voice = I.voice;
    if (tv_voice_is_drum(voice)) slot = kDrum0 + (voice - TV_NUM_SYNTH_VOICES); else slot = trk;
    const int key = instrIdx * 1000 + voice * 40 + I.starter;
    if (!tv_voice_is_drum(voice))
    {
        if (T.synthKey != key) { tv_slot_set_voice(A.tv, slot, voice); const int b = bazz(); if (voice == b && I.starter >= 8) tv_load_bass_bank(A.tv, slot, I.starter - 8); else tv_load_starter(A.tv, slot, voice, I.starter % 8); T.synthKey = key; }
        tv_set_level(A.tv, slot, 1.f); tv_set_pan(A.tv, slot, 0.5f);
        for (int k = 0; k < TV_NUM_CONTROLS; k++) tv_set_param(A.tv, slot, k, I.ctl[k]);
        T.synthOn = true; T.drumSlot = -1;
        tv_note_on(A.tv, slot, note, std::max(1, std::min(127, vel * 127 / 100)), 0);
    }
    else
    {
        if (T.synthKey != key) { tv_load_starter(A.tv, slot, voice, I.starter); T.synthKey = key; }
        tv_set_level(A.tv, slot, 1.f); tv_set_pan(A.tv, slot, 0.5f);
        for (int k = 0; k < TV_NUM_CONTROLS; k++) tv_set_param(A.tv, slot, k, I.ctl[k]);
        T.drumSlot = slot; T.synthOn = false;
        tv_note_on(A.tv, slot, std::max(0, std::min(127, 63 + note - 60)), std::max(1, std::min(127, vel * 127 / 100)), (voice == TV_NUM_VOICES - 1 && I.starter >= 100) ? TV_NOTE_HAT_OPEN : 0);
    }
}

void trigger(int t, int note, int instr, int vel, int offsetPct = 0, bool reverse = false)
{
    Project& P = proj(); Track& T = A.tr[t];
    if (instr < 0 || instr >= kInstr || !P.ins[instr].used) return;
    const Instr& I = P.ins[instr];
    T.lastInstr = instr; T.lastNote = note;
    if (I.kind == 1) { synthNote(t, I, note, vel, instr); return; }
    const Sample* s = &smp(instr);
    if (s->frames() < 2) return;
    if (T.v[0].on) { T.v[1] = T.v[0]; cutVoice(T.v[1]); }
    startVoice(T.v[0], instr, s, note, vel, t, offsetPct, reverse);
    if (T.synthOn) { T.synthOn = false; if (A.tv) tv_note_off(A.tv, t); }
}
void trackOff(int t)
{
    Track& T = A.tr[t]; release(T.v[0], 0.005f);
    if (T.v[0].on) { const Instr& I = proj().ins[T.v[0].instr]; const float r = envTime(I.rel, 3.f); T.v[0].stage = 4; T.v[0].relR = std::max(T.v[0].env, 0.0001f) / (r * kSr); }
    if (T.synthOn && A.tv) { tv_note_off(A.tv, t); }
    T.retrigPeriod = 0;
}
void trackCut(int t) { Track& T = A.tr[t]; cutVoice(T.v[0]); if (A.tv && T.synthOn) { tv_slot_kill(A.tv, t); T.synthOn = false; } T.retrigPeriod = 0; }

/* ------------------------------------------------------------ sequencer */
double stepsPerSample() { return A.tempoNow * 4.0 / 60.0 / kSr; }
int patternAt(int64_t k, int& step, int& slot)
{
    Project& P = proj();
    if (!A.song) { const int p = A.editPat; const int L = std::max(1, P.pat[p].len); step = (int)(k % L); slot = -1; return p; }
    int64_t tot = 0; for (int i = 0; i < P.songLen; i++) tot += std::max(1, P.pat[P.song[i]].len);
    if (tot <= 0) { step = 0; slot = 0; return P.song[0]; }
    int64_t r = k % tot;
    for (int i = 0; i < P.songLen; i++) { const int L = std::max(1, P.pat[P.song[i]].len); if (r < L) { step = (int)r; slot = i; return P.song[i]; } r -= L; }
    step = 0; slot = 0; return P.song[0];
}
void fireStep(int64_t k)
{
    Project& P = proj(); int step, slot; const int pi = patternAt(k, step, slot);
    if (pi != A.curPat) { A.patChange = pi; }
    A.curPat = pi; A.curStep = step; A.curSlot = slot;
    const Pattern& pt = P.pat[pi];
    const double stepFrames = kSr * 60.0 / (A.tempoNow * 4.0);
    for (int t = 0; t < kTracks; t++)
    {
        Track& T = A.tr[t]; const Step& st = pt.s[t][step];
        T.retrigPeriod = 0; T.volMod = -1; T.panMod = -999; T.cutMod = -1; T.resMod = -1;
        bool skip = false, rev = false; int offs = 0, retrig = 0;
        for (int f = 0; f < 2; f++)
        {
            const int ty = st.fx[f], v = st.fv[f];
            switch (ty)
            {
                case FX_VOL: T.volMod = clampi(v, 0, 100); break;
                case FX_PAN: T.panMod = clampi(v, -50, 50); break;
                case FX_CUT: T.cutMod = clampi(v, 0, 100); break;
                case FX_RES: T.resMod = clampi(v, 0, 100); break;
                case FX_TEMPO: if (!A.hostDriven || !A.hostSync) A.tempoFx = clampi(v, 40, 300); break;
                case FX_OFFSET: offs = clampi(v, 0, 100); break;
                case FX_RETRIG: retrig = clampi(v, 1, 16); break;
                case FX_REVERSE: rev = true; break;
                case FX_CHANCE: if (frand() * 100.f >= v) skip = true; break;
            }
        }
        if (P.mute[t]) continue;
        const int ins = st.instr != 255 ? st.instr : T.lastInstr;
        if (st.note == N_CUT) { trackCut(t); continue; }
        if (st.note == N_OFF) { trackOff(t); continue; }
        if (st.note == N_FADE) { release(T.v[0], 0.25f); if (A.tv && T.synthOn) tv_note_off(A.tv, t); continue; }
        if (st.note == N_NONE)
        {
            if (T.v[0].on && (T.volMod >= 0 || T.panMod > -999 || T.cutMod >= 0 || T.resMod >= 0))
            {
                Voice& V = T.v[0]; const Instr& I = P.ins[V.instr];
                if (T.volMod >= 0) V.gain = volGain(T.volMod);
                if (T.panMod > -999) { const float ang = (T.panMod + 50) / 100.f * kPi * 0.5f; V.pl = cosf(ang) * 1.4142f; V.pr = sinf(ang) * 1.4142f; }
                if (V.ftype && (T.cutMod >= 0 || T.resMod >= 0)) { const float fc = 20.f * powf(1000.f, (T.cutMod >= 0 ? T.cutMod : I.cutoff) / 100.f); V.fc = std::min(1.2f, 2 * sinf(kPi * std::min(fc, 18000.f) / kSr)); V.fq = 1.f - 0.94f * (T.resMod >= 0 ? T.resMod : I.res) / 100.f; }
            }
            continue;
        }
        if (skip || ins == 255 || ins < 0) continue;
        trigger(t, st.note, ins, 100, offs, rev);
        if (retrig > 1) { T.retrigPeriod = (int)(stepFrames / retrig); T.retrigLeft = T.retrigPeriod; T.retrigNote = st.note; T.retrigInstr = ins; }
    }
}
double swungOffset() { const int sw = proj().swing; return clampi(sw, 50, 75) <= 50 ? 0.0 : (sw - 50) / 75.0; }
}   // anon

void perfSet(int track, int fx, int value)
{
    if (track < 0 || track >= kTracks || fx <= PF_NONE || fx >= PF_COUNT) return;
    Track& T = A.tr[track];
    if (fx >= PF_LP && fx <= PF_BP) for (int k = PF_LP; k <= PF_BP; k++) T.pfOn &= ~(1u << k);   /* one filter at a time */
    if (fx == PF_REPEAT && !(T.pfOn >> PF_REPEAT & 1)) T.rpLeft = 1;
    T.pf[fx] = value; T.pfOn |= 1u << fx;
}
void perfClear(int track, int fx)
{
    if (track < 0 || track >= kTracks || fx <= PF_NONE || fx >= PF_COUNT) return;
    A.tr[track].pfOn &= ~(1u << fx);
}
float dbOf(int vol) { const float g = volGain(vol); return g <= 0 ? -99.f : 20.f * log10f(g); }
void dbText(int vol, char* b) { if (vol <= 0) strcpy(b, "-inf"); else snprintf(b, 16, "%.2f dB", dbOf(vol)); }

void init()
{
    TvConfig cfg; tv_config_init(&cfg); cfg.synth_slots = kSynthSlots; cfg.drum_voices = 6; cfg.sample_rate = (float)kSr;
    A.tv = tv_create(&cfg);
    A.rev.init(); A.dl[0].assign(kSr * 3, 0.f); A.dl[1].assign(kSr * 3, 0.f);
    A.tempoNow = proj().tempo;
}
void shutdown() { if (A.tv) { tv_destroy(A.tv); A.tv = nullptr; } }
void setHostSync(bool on) { A.hostSync = on; if (!on && A.hostDriven) { stop(); } }
bool hostSync() { return A.hostSync; }
void hostPos(double ppq, double bpm, int playing, int frames)
{
    (void)frames;
    A.hostPpq = ppq; A.hostBpm = bpm; A.hostPlayingNow = playing != 0 && ppq >= 0;
    if (bpm > 0) A.lastBpm = bpm;
    A.hostFresh = true;
}
void play(bool song, bool fromStart)
{
    A.song = song; A.playing = true; A.hostDriven = false;
    if (fromStart) { A.pos = 0; A.lastFired = -1; }
    A.curPat = -1; A.tempoFx = 0;
}
void stop()
{
    if (A.playing) for (int t = 0; t < kTracks; t++) trackOff(t);
    A.playing = false; A.hostDriven = false; A.curStep = -1; A.curPat = -1; A.curSlot = -1; A.tempoFx = 0;
}
bool playing() { return A.playing; }
bool songMode() { return A.song; }
int playStep() { return A.playing ? A.curStep : -1; }
int playPattern() { return A.playing ? A.curPat : -1; }
int playSongSlot() { return A.playing ? A.curSlot : -1; }
double tempo() { return A.tempoNow; }
double timeSeconds() { return A.timeSec; }
void setEditPattern(int p) { A.editPat = clampi(p, 0, kPat - 1); }
bool popPatternChange(int& p) { if (A.patChange < 0) return false; p = A.patChange; A.patChange = -1; return true; }
float level() { return A.peak; }
float lastPlayPos() { return A.lastPos; }
float samplePlayPos(int t, int instr) { const Voice& V = A.tr[t].v[0]; return V.on && V.instr == instr && V.s && V.s->frames() ? (float)(V.pos / (double)V.s->frames()) : -1.f; }
static bool g_metro = false; static float g_metroVol = 0.5f; static int g_clickLeft = 0; static float g_clickPh = 0;
void setMetronome(bool on, int vol) { g_metro = on; g_metroVol = vol / 100.f; }
void tapTempo()
{
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); const double t = ts.tv_sec + ts.tv_nsec * 1e-9;
    if (t - A.tapT > 2.0) A.tapN = 0; else { const double bpm = 60.0 / (t - A.tapT); proj().tempo = (float)std::max(40.0, std::min(300.0, A.tapN ? (proj().tempo * 0.5 + bpm * 0.5) : bpm)); }
    A.tapT = t; A.tapN++;
}

void noteOn(int t, int note, int instr, int vel) { if (t >= 0 && t < kTracks) trigger(t, note, instr, vel * 100 / 127 > 0 ? vel * 100 / 127 : 1); }
void noteOff(int t) { if (t >= 0 && t < kTracks) trackOff(t); }
void allOff() { for (int t = 0; t < kTracks; t++) trackCut(t); if (A.tv) tv_all_notes_off(A.tv); A.prev.on = false; }
void refreshInstrument(int i) { for (auto& T : A.tr) if (T.lastInstr == i) T.synthKey = -1; }
void setPreviewVol(int pct) { A.prevVol = volGain(pct); }
bool previewing() { return A.prev.on; }
void previewSample(const Sample* s, int volPct)
{
    if (!s || s->frames() < 2) { release(A.prev, 0.01f); return; }
    static Instr tmp; tmp = Instr(); tmp.used = true; tmp.vol = volPct; tmp.rel = 5;
    Project& P = proj(); Instr keep = P.ins[kInstr - 1]; P.ins[kInstr - 1] = tmp;
    Track dummy; (void)dummy;
    startVoice(A.prev, kInstr - 1, s, 60, 100, 0, 0, false);
    P.ins[kInstr - 1] = keep;
    A.prev.gain = volGain(volPct); A.prev.stage = 3; A.prev.env = 1.f; A.prev.mode = PM_ONESHOT;
}
int patchCount(int v) { if (v < 0 || v >= TV_NUM_VOICES) return 0; return tv_voice_is_drum(v) ? tv_kit_count() : tv_starter_count(v) + (v == bazz() ? tv_bass_bank_count() : 0); }
const char* patchName(int v, int i)
{
    if (tv_voice_is_drum(v)) return tv_kit_name(i);
    const int n = tv_starter_count(v);
    return i < n ? tv_starter_name(v, i) : tv_bass_bank_name(i - n);
}
int bazzVoice() { return bazz(); }
void loadPatch(Instr& in)
{
    if (!A.tv) return;
    const int v = in.voice;
    if (tv_voice_is_drum(v))
    {
        const int slot = kDrum0 + (v - TV_NUM_SYNTH_VOICES);
        tv_load_starter(A.tv, slot, v, in.starter);
        for (int k = 0; k < TV_NUM_CONTROLS; k++) in.ctl[k] = tv_get_param(A.tv, slot, k);
    }
    else
    {
        tv_slot_set_voice(A.tv, kScratch, v);
        const int ns = tv_starter_count(v);
        if (v == bazz() && in.starter >= ns) tv_load_bass_bank(A.tv, kScratch, in.starter - ns); else tv_load_starter(A.tv, kScratch, v, in.starter);
        for (int k = 0; k < TV_NUM_CONTROLS; k++) in.ctl[k] = tv_get_param(A.tv, kScratch, k);
    }
}

/* ------------------------------------------------------------ one block */
void render(int16_t* out, int n)
{
    Project& P = proj(); const Master& M = P.master;
    /* clock */
    const bool hostOn = A.hostSync && A.hostPlayingNow;
    if (A.hostSync)
    {
        if (hostOn && !A.hostWas) { A.playing = true; A.hostDriven = true; A.song = A.song; A.lastFired = (int64_t)floor(A.hostPpq * 4.0) - 1; A.pos = A.hostPpq * 4.0; A.hostFresh = false; A.curPat = -1; }
        if (!hostOn && A.hostWas && A.hostDriven) { stop(); }
        A.hostWas = hostOn;
    }
    double bpm = (A.hostDriven && A.hostBpm > 0) ? A.hostBpm : (A.tempoFx ? A.tempoFx : P.tempo);
    if (!A.hostDriven && A.hostSync && A.lastBpm > 0 && !A.playing) bpm = P.tempo;
    if (fabs(bpm - A.tempoNow) > 0.001) { A.tempoNow = bpm; if (A.tv) tv_set_tempo(A.tv, (float)bpm); }
    if (A.playing)
    {
        if (A.hostDriven && A.hostFresh) { A.pos = A.hostPpq * 4.0; A.hostFresh = false; if (fabs(A.pos - A.pos) > 0) {} }
        const double endPos = A.pos + n * stepsPerSample(), so = swungOffset();
        for (int64_t k = (int64_t)floor(A.pos) - 1; k <= (int64_t)floor(endPos) + 1; k++)
        {
            if (k < 0 || k <= A.lastFired) continue;
            const double te = k + ((k & 1) ? so : 0.0);
            if (te >= A.pos && te < endPos) { fireStep(k); A.lastFired = k; if (g_metro && (k % 4) == 0) { g_clickLeft = 1200; g_clickPh = 0; } }
        }
        A.pos = endPos;
        for (int t = 0; t < kTracks; t++)
        {
            Track& T = A.tr[t];
            if (T.pfOn >> PF_REPEAT & 1)
            {
                static const double kMul[16] = { 0, 16, 12, 8, 6, 4, 3, 2, 1, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125, 1.0 / 12, 0.0625 };
                const double sf = kSr * 60.0 / (A.tempoNow * 4.0); const int per = std::max(256, (int)(sf * kMul[clampi(T.pf[PF_REPEAT], 1, 15)]));
                T.rpLeft -= n; if (T.rpLeft <= 0) { T.rpLeft = per; if (!P.mute[t] && T.lastInstr >= 0) trigger(t, T.lastNote, T.lastInstr, 90); }
            }
            if (T.retrigPeriod > 0) { T.retrigLeft -= n; if (T.retrigLeft <= 0) { T.retrigLeft += T.retrigPeriod; if (!P.mute[t]) trigger(t, T.retrigNote, T.retrigInstr, 80); } }
        }
        A.timeSec += (double)n / kSr;
    }
    /* mix */
    float mL[kBlock] = {}, mR[kBlock] = {}, sR[kBlock] = {}, sD[kBlock] = {};
    float tl[kBlock], tr_[kBlock], rs[kBlock], ds[kBlock];
    const int nn = n > kBlock ? kBlock : n;
    for (int t = 0; t < kTracks; t++)
    {
        Track& T = A.tr[t];
        memset(tl, 0, sizeof tl); memset(tr_, 0, sizeof tr_); memset(rs, 0, sizeof rs); memset(ds, 0, sizeof ds);
        bool any = false;
        for (int v = 1; v >= 0; v--) any |= renderVoice(T.v[v], tl, tr_, rs, ds, nn);
        if (A.tv && (T.synthOn || tv_slot_active(A.tv, t)))
        {
            float sl[kBlock] = {}, sr[kBlock] = {};
            tv_render_slot(A.tv, t, sl, sr, nn);
            const Instr& I = P.ins[T.lastInstr >= 0 ? T.lastInstr : 0];
            const float vg = volGain(I.vol), rv = I.rev / 100.f, dv = I.del / 100.f;
            const float ang = (clampi(I.pan, -50, 50) + 50) / 100.f * kPi * 0.5f, pl = cosf(ang) * 1.4142f * 0.7071f, pr = sinf(ang) * 1.4142f * 0.7071f;
            for (int i = 0; i < nn; i++) { tl[i] += sl[i] * vg * pl; tr_[i] += sr[i] * vg * pr; const float m = (sl[i] + sr[i]) * 0.5f * vg; rs[i] += m * rv; ds[i] += m * dv; }
            any = true;
        }
        if (!any) continue;
        const float g = P.mute[t] ? 0.f : volGain(P.trackVol[t]);
        const float ang = (clampi(P.trackPan[t], -50, 50) + 50) / 100.f * kPi * 0.5f, pl = cosf(ang) * 1.4142f * 0.7071f, pr = sinf(ang) * 1.4142f * 0.7071f;
        float pk = 0;
        for (int i = 0; i < nn; i++) { mL[i] += tl[i] * g * pl; mR[i] += tr_[i] * g * pr; sR[i] += rs[i] * g; sD[i] += ds[i] * g; pk = std::max(pk, fabsf(tl[i])); }
        T.meter = std::max(T.meter * 0.9f, pk * g);
    }
    if (A.tv) { float dl[kBlock] = {}, dr[kBlock] = {}; tv_render_drum_fx(A.tv, dl, dr, nn);
        for (int d = 0; d < 6; d++)
        {
            const int slot = kDrum0 + d; if (!tv_slot_active(A.tv, slot)) continue;
            int owner = -1; for (int t = 0; t < kTracks; t++) if (A.tr[t].drumSlot == slot) owner = t;
            float sl[kBlock] = {}, sr[kBlock] = {}; tv_render_slot(A.tv, slot, sl, sr, nn);
            const Instr& I = P.ins[owner >= 0 && A.tr[owner].lastInstr >= 0 ? A.tr[owner].lastInstr : 0];
            const float g = (owner >= 0 && P.mute[owner]) ? 0.f : volGain(I.vol) * (owner >= 0 ? volGain(P.trackVol[owner]) : 1.f);
            const float rv = I.rev / 100.f, dv = I.del / 100.f;
            for (int i = 0; i < nn; i++) { mL[i] += sl[i] * g; mR[i] += sr[i] * g; const float m = (sl[i] + sr[i]) * 0.5f * g; sR[i] += m * rv; sD[i] += m * dv; }
        }
        for (int i = 0; i < nn; i++) { mL[i] += dl[i]; mR[i] += dr[i]; } }
    for (int i = 0; i < nn && g_clickLeft > 0; i++, g_clickLeft--) { const float c = sinf(g_clickPh) * g_metroVol * 0.5f * (g_clickLeft / 1200.f); g_clickPh += 0.18f; mL[i] += c; mR[i] += c; }
    if (A.prev.on) { float pl_[kBlock] = {}, pr_[kBlock] = {}, a1[kBlock] = {}, a2[kBlock] = {}; renderVoice(A.prev, pl_, pr_, a1, a2, nn); for (int i = 0; i < nn; i++) { mL[i] += pl_[i]; mR[i] += pr_[i]; } }
    /* effects */
    if (!A.eqInit || memcmp(&A.lastM, &M, sizeof M))
    {
        A.eqLo.shelf(false, 120, M.eqLo * 0.24f); A.eqMid.peak(1200, M.eqMid * 0.24f, 0.8f); A.eqHi.shelf(true, 7000, M.eqHi * 0.24f); A.bassB.shelf(false, 90, M.bass * 0.12f);
        A.lastM = M; A.eqInit = true;
    }
    const float fb = 0.55f + 0.42f * M.revSize / 100.f, damp = 0.1f + 0.8f * M.revDamp / 100.f, rvRet = M.revMix / 100.f * 2.f, diff = M.revDiff / 100.f; const int preN = 1 + M.revPre * 40;
    static const float beats[14] = { 0.125f, 1 / 6.f, 0.1875f, 0.25f, 1 / 3.f, 0.375f, 0.5f, 2 / 3.f, 0.75f, 1.f, 4 / 3.f, 1.5f, 2.f, 4.f };
    const float dsec = M.delSync ? beats[clampi(M.delTime, 0, 13)] * 60.f / (float)A.tempoNow : (M.delTime + 1) * 0.035f;
    int dN = clampi((int)(dsec * kSr), 1, kSr * 3 - 1); const float dfb = M.delFb / 100.f * 0.9f, dRet = M.delMix / 100.f * 2.f;
    static const float drive[5] = { 0, 1.5f, 3.f, 6.f, 12.f }; const float dr = drive[clampi(M.saturation, 0, 4)], dn = dr > 0 ? 1.f / tanhf(dr) : 1.f;
    const float mg = volGain(M.volume), thr = 0.25f + 0.75f * M.limThr / 100.f, relC = 1.f - expf(-1.f / ((0.01f + M.limRel * 0.01f) * kSr));
    const float side = 1.f + M.space / 60.f;
    float pk = 0;
    for (int i = 0; i < nn; i++)
    {
        float rl, rr; A.rev.run(sR[i], rl, rr, fb, damp, preN, diff);
        const int ri = (A.dp - dN + kSr * 3) % (kSr * 3);
        const float dL = A.dl[0][ri], dR = A.dl[1][ri];
        const float in = sD[i];
        if (M.delPing) { A.dl[0][A.dp] = in + dR * dfb; A.dl[1][A.dp] = dL * dfb; } else { A.dl[0][A.dp] = in + dL * dfb; A.dl[1][A.dp] = in + dR * dfb; }
        if (++A.dp >= kSr * 3) A.dp = 0;
        float l = mL[i] + rl * rvRet + dL * dRet, r = mR[i] + rr * rvRet + dR * dRet;
        l = A.bassB.run(0, A.eqHi.run(0, A.eqMid.run(0, A.eqLo.run(0, l)))); r = A.bassB.run(1, A.eqHi.run(1, A.eqMid.run(1, A.eqLo.run(1, r))));
        if (dr > 0) { l = tanhf(l * dr) * dn; r = tanhf(r * dr) * dn; }
        if (M.space) { const float m = (l + r) * 0.5f, s = (l - r) * 0.5f * side; l = m + s; r = m - s; }
        l *= mg; r *= mg;
        const float p = std::max(fabsf(l), fabsf(r)); const float want = p > thr ? thr / p : 1.f;
        if (want < A.limG) A.limG = want; else A.limG += (want - A.limG) * relC;
        l *= A.limG; r *= A.limG;
        if (!(fabsf(l) < 8.f)) l = 0; if (!(fabsf(r) < 8.f)) r = 0;
        l = std::max(-1.f, std::min(1.f, l)); r = std::max(-1.f, std::min(1.f, r));
        pk = std::max(pk, std::max(fabsf(l), fabsf(r)));
        out[2 * i] = (int16_t)lrintf(l * 32767.f); out[2 * i + 1] = (int16_t)lrintf(r * 32767.f);
    }
    for (int i = nn; i < n; i++) { out[2 * i] = 0; out[2 * i + 1] = 0; }
    A.peak = std::max(A.peak * 0.995f, pk);
}
}
