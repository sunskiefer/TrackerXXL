/* glue: the mpc_engine_t the VST2 wrapper drives.  Force buttons / pads / wheel -> app, host transport -> audio, 128-frame blocks out.
 * No threads: everything runs inside the wrapper's calls (serialised per instance). */
#include "app.h"
#include "audio.h"
#include "model.h"
#include "own.h"
#include "xxl_scene.h"
#include "scene_layout.h"
#include "ctl_table.h"
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string>
extern "C" {
#include "engine.h"
}

namespace
{
std::string g_logPath;
int g_owners = 0;
void writeLog(const char* s, size_t n)
{
    if (g_logPath.empty()) return;
    const int fd = open(g_logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) { if (write(fd, s, n) < 0) {} close(fd); }
}
void crashHandler(int sig, siginfo_t* si, void*)
{
    char b[160]; const int n = snprintf(b, sizeof b, "CRASH signal=%d addr=%p\n", sig, si ? si->si_addr : nullptr); writeLog(b, (size_t)n);
    void* bt[24]; const int c = backtrace(bt, 24);
    if (!g_logPath.empty()) { const int fd = open(g_logPath.c_str(), O_WRONLY | O_APPEND); if (fd >= 0) { backtrace_symbols_fd(bt, c, fd); close(fd); } }
    signal(sig, SIG_DFL); raise(sig);
}
void installCrash()
{
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = crashHandler; sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr); sigaction(SIGBUS, &sa, nullptr); sigaction(SIGFPE, &sa, nullptr); sigaction(SIGILL, &sa, nullptr); sigaction(SIGABRT, &sa, nullptr);
}

struct Inst
{
    bool owner = false, started = false;
    float last[NCTLS]; float acc[NCTLS]; uint8_t on[NCTLS];
    int recentre[NCTLS]; int nRecentre = 0;
    int padCols = 0, hostSync = 1, padMode = 0; int frames = 0;
    int16_t held[64] = {};
};
const char* const kCtlPad[8][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 },
    { "copy", "insert", "delete", 0, 0, "wheel_down", "wheel_up", 0 },
    { "cur_up", "cur_down", "cur_left", "cur_right", "play", "stop", "shift", "edit" },
    { "scr_srec", "scr_song", "scr_note", "scr_instr", "scr_vol", "scr_fx", "scr_pattern", 0 },
    { "scr_params", "scr_perf", "scr_file", "scr_config", "scr_master", "scr_sload", "scr_splay", "scr_sedit" },
    { "btn1", "btn2", "btn3", "btn4", "btn5", "btn6", "btn7", "btn8" } };
enum { NCTLS_MAX = 512 };
int findCtl(const char* key) { for (int i = 0; i < NCTLS; i++) if (!strcmp(CTLS[i].key, key)) return i; return -1; }

void ensure(Inst* in)
{
    if (!in->owner || in->started) return;
    in->started = true;
    app::init(own::g_root.c_str());
}
void ctlAction(Inst* in, const ctl_t& c, bool press)
{
    if (c.type == CT_BTN) { if (c.b) { if (press) app::buttonPress(c.a); else app::buttonRelease(c.a); } else if (press) app::buttonTap(c.a); }
    else if (c.type == CT_CLICKS) { if (press) app::wheel(c.a); }
    else if (c.type == CT_VIRT && c.x == VIRT_STOP) { if (press) app::stopButton(); }
}
void* eng_create(const char* data_dir)
{
    Inst* in = new Inst();
    for (int i = 0; i < NCTLS; i++) { in->last[i] = CTLS[i].type == CT_REL ? 0.5f : 0.f; in->acc[i] = 0; in->on[i] = 0; }
    memset(in->held, -1, sizeof in->held);
    const char* root = getenv("TRACKERXXL_ROOT"); if (!root || !*root) root = data_dir ? data_dir : "";
    in->owner = g_owners == 0; if (in->owner) { g_owners = 1; own::g_root = root; g_logPath = own::g_root + "/trackerxxl.log"; struct stat st; if (!stat(g_logPath.c_str(), &st) && st.st_size > 1000000) unlink(g_logPath.c_str()); installCrash(); }
    xxl::logf("eng_create owner=%d root=%s (own engine)", (int)in->owner, root);
    return in;
}
void eng_destroy(void* p)
{
    Inst* in = (Inst*)p; xxl::logf("eng_destroy");
    if (in->owner) { if (in->started) app::shutdown(); g_owners = 0; }
    delete in;
}
void relative(Inst* in, int i, float v)
{
    const ctl_t& c = CTLS[i]; const float delta = v - in->last[i]; in->last[i] = v;
    if (fabsf(delta) > 0.25f) { in->acc[i] = 0; return; }
    in->acc[i] += delta * (float)c.a; int n = (int)in->acc[i]; in->acc[i] -= (float)n;
    if (v <= 0.001f || v >= 0.999f) { in->last[i] = 0.5f; if (in->nRecentre < NCTLS) in->recentre[in->nRecentre++] = i; }
    if (!n) return;
    if (c.x == ROLE_FOCUS) { const int btn = n > 0 ? (c.b >> 8) : (c.b & 255); int t = n < 0 ? -n : n; if (t > 2) t = 2; for (int k = 0; k < t; k++) app::buttonTap(btn); }
    else if (c.x == ROLE_WHEEL) { if (c.b > 0) app::wheelTab(c.b); app::wheel(n); }
    else if (c.x == ROLE_MACRO) app::macro(c.b, n);
    else app::knob(n, c.x == ROLE_VALUE_COARSE);
}
void eng_set_param(void* p, const char* key, const char* val)
{
    Inst* in = (Inst*)p;
    if (xxl_scene::set(key, val)) return;
    { static int n = 0; if (n < 600) { n++; xxl::logf("set_param %s=%s", key, val ? val : ""); } }
    const int i = findCtl(key); if (i < 0) return;
    ensure(in);
    const ctl_t& c = CTLS[i]; const float v = (float)atof(val); const bool hi = v > 0.5f;
    switch (c.type)
    {
        case CT_REL: if (in->owner) relative(in, i, v); break;
        case CT_CLICKS: if (in->owner && hi && !in->on[i]) app::wheel(c.a); in->on[i] = hi; break;
        case CT_BTN:
            if (c.b) { if (in->owner && hi != (bool)in->on[i]) { if (hi) app::buttonPress(c.a); else app::buttonRelease(c.a); } }
            else if (in->owner)
            {
                /* a touch button is a skin Toggle Switch: if the host never resets it to 0, every second touch arrives as a 1->0 change; the wrapper's own reset arrives within a few ms, a real touch much later */
                static double lastUp[NCTLS_MAX]; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); const double now = ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
                if (hi && !in->on[i]) { lastUp[i] = now; app::buttonTap(c.a); }
                else if (!hi && in->on[i] && now - lastUp[i] > 120.0) app::buttonTap(c.a);
            }
            in->on[i] = hi; break;
        case CT_VIRT: if (in->owner && hi && !in->on[i] && c.x == VIRT_STOP) app::stopButton(); in->on[i] = hi; break;
        case CT_PAD:
        {
            if (in->owner && hi != (bool)in->on[i])
            {
                const int trow = 3 - c.a / 4, col = in->padCols * 4 + (c.a & 3); const int note = 48 + trow * 12 + col;
                app::midiNote(note, 100, hi);
            }
            in->on[i] = hi; break;
        }
        case CT_OPT:
        {
            const int o = atoi(val); in->last[i] = (float)o;
            if (c.x == OPT_PAD_COLS) in->padCols = o < 0 ? 0 : o > 2 ? 2 : o;
            else if (c.x == OPT_PAD_MODE) in->padMode = o ? 1 : 0;
            else if (c.x == OPT_HOST_SYNC) { in->hostSync = o ? 1 : 0; if (in->owner) audio::setHostSync(in->hostSync); }
            break;
        }
    }
}
int eng_get_param(void* p, const char* key, char* buf, int len)
{
    Inst* in = (Inst*)p;
    if (key[0] == 'm' && key[1] == 't' && strlen(key) > 7 && key[4] == '_')
    {
        const int scr = (key[2] - '0') * 10 + (key[3] - '0'), idx = key[5] - '1';
        char nm[48], vl[48];
        if (in->owner && in->started) app::macroLabel(scr, idx, nm, vl, sizeof nm); else { snprintf(nm, sizeof nm, "Macro %c", key[5]); vl[0] = 0; }
        if (!strcmp(key + 7, "name")) return snprintf(buf, (size_t)len, "%.31s", nm);
        if (!strcmp(key + 7, "display")) return snprintf(buf, (size_t)len, "%.31s", vl);
    }
    { const int n = xxl_scene::get(key, buf, len); if (n) return n; }
    const int i = findCtl(key); if (i < 0) return 0;
    if (CTLS[i].type == CT_OPT) return snprintf(buf, (size_t)len, "%d", (int)in->last[i]);
    return snprintf(buf, (size_t)len, "%.4f", in->last[i]);
}
void releaseHeld(Inst* in) { for (int k = 0; k < 64; k++) if (in->held[k] >= 0) { if (in->held[k] == 1000) app::padUp(k); else { for (int j = 0; j < NCTLS; j++) if (j == in->held[k] - 2000) ctlAction(in, CTLS[j], false); } in->held[k] = -1; } }
void eng_midi(void* p, const uint8_t* msg, int len)
{
    Inst* in = (Inst*)p; if (!in->owner || len < 1) return;
    ensure(in);
    const uint8_t st = msg[0];
    { static int n = 0; if (n < 60 && st < 0xF0) { n++; xxl::logf("midi %02x %02x %02x", st, len > 1 ? msg[1] : 0, len > 2 ? msg[2] : 0); } }
    if (st >= 0xF0) return;
    const uint8_t type = st & 0xF0, d1 = len > 1 ? msg[1] : 0, d2 = len > 2 ? msg[2] : 0;
    if (type != 0x90 && type != 0x80) return;
    const bool down = type == 0x90 && d2 > 0;
    if (d1 >= CTL_PAD_NOTE_BASE && d1 < CTL_PAD_NOTE_BASE + CTL_PAD_NOTE_COUNT)
    {
        const int k = d1 - CTL_PAD_NOTE_BASE;
        if (down)
        {
            if (k == 7) { releaseHeld(in); in->padMode ^= 1; const int pm = findCtl("pad_mode"); if (pm >= 0) in->last[pm] = (float)in->padMode; return; }
            if (in->padMode == 0) { in->held[k] = 1000; app::padDown(k, d2); }
            else
            {
                const char* key = kCtlPad[k / 8][k & 7]; const int ci = key ? findCtl(key) : -1; if (ci < 0) return;
                ctlAction(in, CTLS[ci], true); if (CTLS[ci].type == CT_BTN && CTLS[ci].b) in->held[k] = (int16_t)(2000 + ci);
            }
        }
        else if (in->held[k] >= 0)
        {
            if (in->held[k] == 1000) app::padUp(k); else ctlAction(in, CTLS[in->held[k] - 2000], false);
            in->held[k] = -1;
        }
        return;
    }
    app::midiNote(d1, d2, down);
}
void eng_render(void* p, int16_t* out, int frames)
{
    Inst* in = (Inst*)p;
    if (!in->owner) { memset(out, 0, (size_t)frames * 4); return; }
    ensure(in);
    int done = 0; int16_t blk[256];
    while (done < frames)
    {
        audio::render(blk, 128);
        const int n = frames - done < 128 ? frames - done : 128;
        memcpy(out + 2 * done, blk, (size_t)n * 4); done += n;
        app::tick(128); in->frames += 128;
        if (in->frames >= 1323) { in->frames = 0; app::frame(); }
    }
}
void eng_song_pos(void* p, double ppq, double bpm, int playing, int frames)
{
    Inst* in = (Inst*)p; if (!in->owner) return;
    { static int n = 0; if (n < 12 && (playing || n == 0)) { n++; xxl::logf("song_pos ppq=%.3f bpm=%.1f play=%d frames=%d", ppq, bpm, playing, frames); } }
    ensure(in); audio::hostPos(ppq, bpm, playing, frames);
}
const mpc_engine_t ENGINE = { eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, nullptr, eng_song_pos };
}

namespace xxl
{
void logf(const char* fmt, ...)
{
    if (g_logPath.empty()) return;
    char b[512]; timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    int n = snprintf(b, sizeof b, "%ld.%03ld ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
    va_list ap; va_start(ap, fmt); n += vsnprintf(b + n, sizeof b - 2 - n, fmt, ap); va_end(ap);
    if (n > (int)sizeof b - 2) n = (int)sizeof b - 2; b[n++] = '\n'; writeLog(b, (size_t)n);
}
}
extern "C" const mpc_engine_t* mpc_engine(void) { return &ENGINE; }
extern "C" int mpc_engine_changed(void* p, char* key, int len)
{
    Inst* in = (Inst*)p; if (in->nRecentre <= 0) return 0;
    const int i = in->recentre[--in->nRecentre]; snprintf(key, (size_t)len, "%s", CTLS[i].key); return 1;
}
