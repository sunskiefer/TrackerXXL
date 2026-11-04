/* TrackerXXL: the mpc_engine_t the VST2 wrapper (wrapper/vst2_wrap.c) drives. Host -> firmware mapping lives here:
 *   set_param  -> controls.json table (build/ctl_table.h): data wheel / Q-Links -> encoder clicks or cursor taps, buttons, pads,
 *                 STOP, options (PAD COLUMNS, HOST SYNC);
 *   midi       -> notes CTL_PAD_NOTE_BASE.. (64 pads, PAD MODE PLAY/CONTROL) -> firmware pad grid / buttons; other notes/CC -> the firmware's MIDI input;
 *   song_pos   -> MIDI start / continue / stop / song position / 24 ppqn clock into the firmware's MIDI input;
 *   render     -> one Tracker::updateAudioBuffers block.
 * Threads: the wrapper serializes all of these per instance; none of them blocks on the UI thread (events go through a ring). */
#include "xxl_internal.h"
#include "xxl_firmware.h"
#include "../build/ctl_table.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "xxl_scene.h"   // screen bridge (port/skin/bridge): C++ namespace, must not sit inside extern "C"
extern "C" {
#include "engine.h"
}

namespace
{
struct Inst
{
    bool owner = false;
    float last[NCTLS];
    float acc[NCTLS];
    uint8_t on[NCTLS];          /* previous "pressed" state of buttons/pads (edge detection) */
    int recentre[NCTLS];        /* queue of relative controls to report back at 0.5 */
    int nRecentre = 0;
    int padCols = 0;
    int hostSync = 1;
    bool wasPlaying = false;
    double expectedPpq = 0;
    int64_t nextPulse = 0;
    int padMode = 0;            /* 0 PLAY, 1 CONTROL */
    int16_t dnKind[64] = {};    /* per held pad note: 0 none, 1 firmware pad (arg = pad index), 2 latched button (arg = control index) */
    int16_t dnArg[64] = {};
};

int findCtl(const char* key)
{
    for (int i = 0; i < NCTLS; i++)
        if (!strcmp(CTLS[i].key, key)) return i;
    return -1;
}

int padIndex(int k, int cols) { return (3 - k / 4) * 12 + cols * 4 + (k & 3); }


/* Fixed CONTROL layout of the Force's 8x8 pads: row 0 = bottom, row 7 = top; note = CTL_PAD_NOTE_BASE + row*8 + col. Pad (row 0, col 7) toggles PAD MODE in both modes. */
const char* const kCtlPad[8][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { "copy", "insert", "delete", 0, 0, "wheel_down", "wheel_up", 0 },
    { "cur_up", "cur_down", "cur_left", "cur_right", "play", "stop", "shift", "edit" },
    { "scr_srec", "scr_song", "scr_note", "scr_instr", "scr_vol", "scr_fx", "scr_pattern", 0 },
    { "scr_params", "scr_perf", "scr_file", "scr_config", "scr_master", "scr_sload", "scr_splay", "scr_sedit" },
    { "btn1", "btn2", "btn3", "btn4", "btn5", "btn6", "btn7", "btn8" } };
const int kModePad = 7;

int playPadIndex(int k)   /* PLAY: bottom 4 rows = grid columns 0-7, top 4 rows = columns 4-11 (4-7 are reachable twice) */
{
    const int row = k / 8, col = k & 7;
    return row < 4 ? (3 - row) * 12 + col : (3 - (row - 4)) * 12 + 4 + col;
}

void releaseHeld(Inst* in)
{
    for (int k = 0; k < 64; k++)
    {
        if (in->dnKind[k] == 1) xxl::post({ xxl::EV_PAD_RELEASE, in->dnArg[k] });
        else if (in->dnKind[k] == 2) xxl::post({ xxl::EV_BTN_RELEASE, in->dnArg[k] });
        in->dnKind[k] = 0;
    }
}

void tapButton(int idx) { xxl::post({ xxl::EV_BTN_TAP, (int16_t)idx }); }

void* eng_create(const char* data_dir)
{
    Inst* in = new Inst();
    for (int i = 0; i < NCTLS; i++)
    {
        in->last[i] = CTLS[i].type == CT_REL ? 0.5f : 0.f;
        in->acc[i] = 0;
        in->on[i] = 0;
    }
    const char* root = getenv("TRACKERXXL_ROOT");   /* offline/testing override */
    if (!root || !*root) root = data_dir;
    in->owner = xxl::runtimeAcquire(root);
    return in;
}

void eng_destroy(void* p)
{
    Inst* in = (Inst*)p;
    if (in->owner) xxl::runtimeRelease();
    delete in;
}

void relative(Inst* in, int i, float v)
{
    const ctl_t& c = CTLS[i];
    float delta = v - in->last[i];
    in->last[i] = v;
    if (fabsf(delta) > 0.25f) { in->acc[i] = 0; return; }   /* a jump (project load, automation reset), not a turn */
    in->acc[i] += delta * (float)c.a;
    int n = (int)in->acc[i];   /* toward zero */
    in->acc[i] -= (float)n;
    if (v <= 0.001f || v >= 0.999f)   /* endless: put the host's value back in the middle */
    {
        in->last[i] = 0.5f;
        if (in->nRecentre < NCTLS) in->recentre[in->nRecentre++] = i;
    }
    if (!n) return;
    if (c.x == ROLE_FOCUS)
    {
        const int btn = n > 0 ? (c.b >> 8) : (c.b & 255);
        int taps = n < 0 ? -n : n;
        if (taps > 8) taps = 8;
        for (int t = 0; t < taps; t++) tapButton(btn);
    }
    else
    {
        if (n > 32000) n = 32000;
        if (n < -32000) n = -32000;
        xxl::post({ xxl::EV_ENC, (int16_t)n });
    }
}

void eng_set_param(void* p, const char* key, const char* val)
{
    Inst* in = (Inst*)p;
    if (xxl_scene::set(key, val)) return;   // scene parameter (screen text/state/touch)
    const int i = findCtl(key);
    if (i < 0) return;
    const ctl_t& c = CTLS[i];
    const float v = (float)atof(val);
    const bool hi = v > 0.5f;
    switch (c.type)
    {
        case CT_REL:
            if (in->owner) relative(in, i, v);
            break;
        case CT_CLICKS:
            if (in->owner && hi && !in->on[i]) xxl::post({ xxl::EV_ENC, (int16_t)c.a });
            in->on[i] = hi;
            break;
        case CT_BTN:
            if (c.b)   /* latch (SHIFT): pressed while on */
            {
                if (in->owner && hi != (bool)in->on[i]) xxl::post({ hi ? xxl::EV_BTN_PRESS : xxl::EV_BTN_RELEASE, (int16_t)c.a });
            }
            else if (in->owner && hi && !in->on[i]) tapButton(c.a);
            in->on[i] = hi;
            break;
        case CT_VIRT:
            if (in->owner && hi && !in->on[i] && c.x == VIRT_STOP) xxl::post({ xxl::EV_STOP, (int16_t)xxl::kFwPlayButton });
            in->on[i] = hi;
            break;
        case CT_PAD:
            if (in->owner && hi && !in->on[i]) xxl::post({ xxl::EV_PAD_TAP, (int16_t)padIndex(c.a, in->padCols) });
            in->on[i] = hi;
            break;
        case CT_OPT:
        {
            int o = atoi(val);
            in->last[i] = (float)o;
            if (c.x == OPT_PAD_COLS)
            {
                in->padCols = o < 0 ? 0 : o > 2 ? 2 : o;
                if (in->owner) xxl::setPadCols(in->padCols);
            }
            else if (c.x == OPT_PAD_MODE)
            {
                const int m = o ? 1 : 0;
                if (m != in->padMode) { releaseHeld(in); in->padMode = m; }
            }
            else if (c.x == OPT_HOST_SYNC)
            {
                in->hostSync = o ? 1 : 0;
                if (in->owner)
                {
                    xxl::setHostSync(in->hostSync);
                    xxl::post({ xxl::EV_SYNC_MODE, (int16_t)in->hostSync });
                }
            }
            break;
        }
    }
}

int eng_get_param(void* p, const char* key, char* buf, int len)
{
    Inst* in = (Inst*)p;
    if (!strncmp(key, "stat_", 5))   /* debug counters, not in params.json */
    {
        const xxl::Counters c = xxl::counters();
        unsigned v = 0;
        if (!strcmp(key, "stat_blocks")) v = c.renderBlocks;
        else if (!strcmp(key, "stat_notready")) v = c.renderSkippedNotReady;
        else if (!strcmp(key, "stat_gate")) v = c.renderSkippedGate;
        else if (!strcmp(key, "stat_ui")) v = c.uiLoops;
        else if (!strcmp(key, "stat_evdrop")) v = c.eventsDropped;
        else if (!strcmp(key, "stat_mididrop")) v = c.midiDropped;
        else if (!strcmp(key, "stat_midiout")) v = c.midiOutDropped;
        else if (!strcmp(key, "stat_ready")) v = xxl::ready();
        return snprintf(buf, (size_t)len, "%u", v);
    }
    { const int n = xxl_scene::get(key, buf, len); if (n) return n; }   // scene parameter
    const int i = findCtl(key);
    if (i < 0) return 0;
    if (CTLS[i].type == CT_OPT) return snprintf(buf, (size_t)len, "%d", (int)in->last[i]);
    return snprintf(buf, (size_t)len, "%.4f", in->last[i]);
}

void eng_midi(void* p, const uint8_t* msg, int len)
{
    Inst* in = (Inst*)p;
    if (!in->owner || len < 1) return;
    const uint8_t st = msg[0];
    if (st >= 0xF0) return;                       /* host realtime/system: the transport comes through song_pos() */
    const uint8_t type = st & 0xF0, d1 = len > 1 ? msg[1] : 0, d2 = len > 2 ? msg[2] : 0;
    if ((type == 0x90 || type == 0x80) && d1 >= CTL_PAD_NOTE_BASE && d1 < CTL_PAD_NOTE_BASE + CTL_PAD_NOTE_COUNT)
    {
        const int k = d1 - CTL_PAD_NOTE_BASE;
        const bool downEv = type == 0x90 && d2 > 0;
        if (downEv)
        {
            if (k == kModePad)
            {
                releaseHeld(in);
                in->padMode ^= 1;
                const int pm = findCtl("pad_mode");
                if (pm >= 0) in->last[pm] = (float)in->padMode;
                return;
            }
            if (in->padMode == 0)
            {
                const int idx = playPadIndex(k);
                in->dnKind[k] = 1; in->dnArg[k] = (int16_t)idx;
                xxl::post({ xxl::EV_PAD_PRESS, (int16_t)idx });
            }
            else
            {
                const char* key = kCtlPad[k / 8][k & 7];
                const int ci = key ? findCtl(key) : -1;
                if (ci < 0) return;
                const ctl_t& c = CTLS[ci];
                if (c.type == CT_BTN && c.b) { in->dnKind[k] = 2; in->dnArg[k] = (int16_t)c.a; xxl::post({ xxl::EV_BTN_PRESS, (int16_t)c.a }); }
                else if (c.type == CT_BTN) tapButton(c.a);
                else if (c.type == CT_CLICKS) xxl::post({ xxl::EV_ENC, (int16_t)c.a });
                else if (c.type == CT_VIRT && c.x == VIRT_STOP) xxl::post({ xxl::EV_STOP, (int16_t)xxl::kFwPlayButton });
            }
        }
        else if (in->dnKind[k])
        {
            if (in->dnKind[k] == 1) xxl::post({ xxl::EV_PAD_RELEASE, in->dnArg[k] });
            else xxl::post({ xxl::EV_BTN_RELEASE, in->dnArg[k] });
            in->dnKind[k] = 0;
        }
        return;
    }
    if (type == 0x80 || type == 0x90 || type == 0xB0) xxl::midiToFirmware(st, d1, d2);
}

void eng_render(void* p, int16_t* out, int frames)
{
    (void)p;
    if (frames == 128) { xxl::renderBlock(out); return; }
    int16_t blk[256];   /* defensive: the wrapper always asks for 128 here (no SAMPLE_ACCURATE) */
    int done = 0;
    while (done < frames)
    {
        xxl::renderBlock(blk);
        const int n = frames - done < 128 ? frames - done : 128;
        memcpy(out + 2 * done, blk, (size_t)n * 2 * sizeof(int16_t));
        done += n;
    }
}

/* Host transport -> MIDI start/continue/stop/song position/clock (24 ppqn), consumed in the next audio block by the firmware's
 * midiUpdate(). Called once per host block before render(). */
void eng_song_pos(void* p, double ppq, double bpm, int playing, int frames)
{
    Inst* in = (Inst*)p;
    if (!in->owner || !xxl::ready()) return;
    if (!in->hostSync || ppq < 0.0) { in->wasPlaying = false; return; }
    if (playing)
    {
        const double end = ppq + (double)frames * bpm / 60.0 / 44100.0;
        const bool jump = in->wasPlaying && fabs(ppq - in->expectedPpq) > 0.05;
        if (!in->wasPlaying || jump)
        {
            if (ppq < 1.0 / 96.0)
                xxl::midiToFirmware(0xFA, 0, 0);                                         /* Start */
            else
            {
                const unsigned beats = (unsigned)(ppq * 4.0 + 0.5);                      /* MIDI beat = 1/16 note */
                xxl::midiToFirmware(0xF2, (uint8_t)(beats & 0x7F), (uint8_t)((beats >> 7) & 0x7F));
                xxl::midiToFirmware(0xFB, 0, 0);                                         /* Continue */
            }
            in->nextPulse = (int64_t)ceil(ppq * 24.0 - 1e-6);
        }
        int guard = 0;
        while ((double)in->nextPulse / 24.0 < end && guard++ < 16)
        {
            xxl::midiToFirmware(0xF8, 0, 0);
            in->nextPulse++;
        }
        in->expectedPpq = end;
        in->wasPlaying = true;
    }
    else if (in->wasPlaying)
    {
        xxl::midiToFirmware(0xFC, 0, 0);   /* Stop */
        in->wasPlaying = false;
    }
}

const mpc_engine_t ENGINE = { eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, nullptr, eng_song_pos };
} // namespace

extern "C" const mpc_engine_t* mpc_engine(void) { return &ENGINE; }

/* Report relative controls that were put back at 0.5 so the host's knob follows (see wrapper engine.h). */
extern "C" int mpc_engine_changed(void* p, char* key, int len)
{
    Inst* in = (Inst*)p;
    if (in->nRecentre <= 0) return 0;
    const int i = in->recentre[--in->nRecentre];
    snprintf(key, (size_t)len, "%s", CTLS[i].key);
    return 1;
}
