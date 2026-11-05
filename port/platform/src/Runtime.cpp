/* TrackerXXL execution model.
 *
 * The Polyend firmware is a cooperative MCU super-loop (main(): usb->update(); platform.update(); Tracker::update()) plus an
 * audio "interrupt" (I2S DMA every 128 samples -> Tracker::updateAudioBuffers) plus a few timer interrupts. On the plugin:
 *
 *   UI thread     (this file, uiMain)  = the super-loop. Runs Tracker::init() once, then every ~1 ms:
 *                                         pump host events -> SwitchGroup callbacks (onButtonPush ... onPotChange),
 *                                         button hold/tap timing, Platform::update, Tracker::update (display, file manager,
 *                                         sequencer UI), xxl_display_bridge_update().
 *   audio         (renderBlock)        = the audio interrupt: the host's processReplacing -> mpc_engine render() -> exactly one
 *                                         Tracker::updateAudioBuffers(left, right, 128) per block (MIDI in, sequencer tick,
 *                                         AudioStream::update_all). No malloc and no blocking beyond the audio-interrupt gate.
 *   timer thread  (PlatformLinux.cpp)  = the QTMR interrupt (scheduled MIDI clock out), polling every 0.3 ms.
 *
 * Mutual exclusion between the UI thread and the audio block is the firmware's own: DISABLE_AUDIO_INTERRUPTS() /
 * ENABLE_AUDIO_INTERRUPTS() (disableAudioInterrupt()) hold a gate that renderBlock respects (FirmwareGlue.cpp).
 * Everything else the firmware shares between its main loop and its audio ISR is shared exactly as on the MCU
 * (see README: this is the main correctness risk of the port, not verified on a device).
 *
 * The firmware is a set of C++ globals (mtInterface, sequencer, engine ...): ONE firmware instance per process. The Runtime is
 * created on the first create() and lives for the process; destroy() only detaches the host, a later create() re-attaches. */
#include "xxl_internal.h"
#include "xxl_firmware.h"
#include "Tracker.h"

#include <pthread.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "../build/ctl_table.h"



#include <signal.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <unistd.h>
namespace xxl {
static struct sigaction g_oldSig[32];
static char g_crashPath[512];
static void crashHandler(int sig, siginfo_t* si, void* uc)
{
    int fd = open(g_crashPath, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd >= 0)
    {
        char b[160];
        Dl_info di; memset(&di, 0, sizeof di);
        dladdr((void*)&crashHandler, &di);
        int n = snprintf(b, sizeof b, "CRASH signal=%d addr=%p base=%p\n", sig, si ? si->si_addr : 0, di.dli_fbase);
        write(fd, b, (size_t)n);
        void* bt[40]; int c = backtrace(bt, 40);
        for (int i = 0; i < c; i++) { n = snprintf(b, sizeof b, "  bt %d %p\n", i, bt[i]); write(fd, b, (size_t)n); }
        fsync(fd); close(fd);
    }
    if (sig > 0 && sig < 32 && (g_oldSig[sig].sa_flags & SA_SIGINFO) && g_oldSig[sig].sa_sigaction) { g_oldSig[sig].sa_sigaction(sig, si, uc); return; }
    if (sig > 0 && sig < 32 && g_oldSig[sig].sa_handler && g_oldSig[sig].sa_handler != SIG_DFL && g_oldSig[sig].sa_handler != SIG_IGN) { g_oldSig[sig].sa_handler(sig); return; }
    signal(sig, SIG_DFL); raise(sig);
}
static void installCrashHandler(const std::string& logPath)
{
    snprintf(g_crashPath, sizeof g_crashPath, "%s", logPath.c_str());
    static bool done = false; if (done) return; done = true;
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crashHandler; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    for (int s : { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL }) sigaction(s, &sa, &g_oldSig[s]);
}
}
namespace xxl
{
namespace
{
/* Host thread -> UI thread ring (single producer: the host calls are serialized by the wrapper's per-instance lock). */
struct EvRing
{
    static constexpr uint32_t kSize = 1024;
    std::atomic<uint32_t> head{ 0 }, tail{ 0 };
    Event slot[kSize];
    bool push(const Event& e)
    {
        uint32_t h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) >= kSize) return false;
        slot[h & (kSize - 1)] = e;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(Event& e)
    {
        uint32_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        e = slot[t & (kSize - 1)];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
};


/* Diagnostics: <root>/trackerxxl.log (one line per event, written and closed at once so a freeze leaves the last line) and
 * <root>/safe.ini (nofw=1: do not start the firmware; nobridge=1: no screen bridge; noaudio=1: silent). */
std::string g_logPath;
int g_swNoFw = 0, g_swNoBridge = 0, g_swNoAudio = 0;
}
void logf(const char* fmt, ...)
{
    if (g_logPath.empty()) return;
    char line[400];
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    int n = snprintf(line, sizeof line, "%6ld.%03ld ", (long)ts.tv_sec % 100000, ts.tv_nsec / 1000000);
    va_list ap; va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - (size_t)n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    int fd = open(g_logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    ssize_t w = write(fd, line, (size_t)n); (void)w;
    fsync(fd);
    close(fd);
}
namespace
{
void readSwitches(const std::string& root)
{
    FILE* f = fopen((root + "/safe.ini").c_str(), "r");
    if (!f) return;
    char b[128];
    while (fgets(b, sizeof b, f))
    {
        if (!strncmp(b, "nofw=1", 6)) g_swNoFw = 1;
        else if (!strncmp(b, "nobridge=1", 10)) g_swNoBridge = 1;
        else if (!strncmp(b, "noaudio=1", 9)) g_swNoAudio = 1;
    }
    fclose(f);
}

struct Rt
{
    std::atomic<bool> started{ false };
    std::atomic<bool> attached{ false };
    std::atomic<bool> ready{ false };
    std::atomic<bool> quit{ false };
    bool uiRunning = false;
    std::atomic<int> padCols{ 0 };
    std::atomic<int> hostSync{ 1 };
    pthread_t ui;
    PlatformImpl* plat = nullptr;
    std::string root;
    EvRing ring;
    std::atomic<int> peak{ 0 };
    std::atomic<uint32_t> cRender{ 0 }, cNotReady{ 0 }, cGate{ 0 }, cUi{ 0 }, cEvDrop{ 0 }, cMidiDrop{ 0 };
} R;

int16_t g_left[128], g_right[128];

void pump(SwitchGroupImpl* b, SwitchGroupImpl* p, uint32_t now)
{
    Event e;
    while (R.ring.pop(e))
    {
        switch (e.type)
        {
            case EV_BTN_PRESS: b->press(e.a, now); break;
            case EV_BTN_RELEASE: b->release(e.a); break;
            case EV_BTN_TAP: { static int n = 0; if (n < 120) { n++; logf("btn tap %d", (int)e.a); } b->tap(e.a, now, CTL_TAP_MS); break; }
            case EV_PAD_PRESS: p->press(e.a, now); break;
            case EV_PAD_RELEASE: p->release(e.a); break;
            case EV_PAD_TAP: p->tap(e.a, now, CTL_TAP_MS); break;
            case EV_ENC: if (e.a) fw_encoder(e.a); break;
            case EV_STOP: if (fw_isPlaying()) b->tap(e.a, now, CTL_TAP_MS); break;
            case EV_SYNC_MODE: fw_applyHostSync(e.a != 0); break;
            case EV_RELEASE_ALL: b->releaseAll(); p->releaseAll(); break;
        }
    }
    b->poll(now);
    p->poll(now);
}

void* uiMain(void*)
{
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 5);   /* below the host's audio thread */
    Platform* plat = asPlatform(R.plat);
    SwitchGroupImpl* buttons = platformButtons(R.plat);
    SwitchGroupImpl* pads = platformPads(R.plat);
    Tracker* trk = Tracker::getInstance();
    Timer* timer = platformTimer(R.plat);

    /* main() of platforms/imx: initHardware() starts the display (startDisplayFunc), installs the input callbacks, then
     * platform.init(), Tracker::init(). */
    logf("ui: thread up, starting display");
    trk->getStartDisplayFunc()(plat);
    logf("ui: display started");
    fw_buttonCallbacksInstall(buttons, pads);
    plat->getUartMidi()->init();
    logf("ui: Tracker::init begin");
    trk->init(plat);
    logf("ui: Tracker::init done");
    fw_applyHostSync(R.hostSync.load() != 0);
    if (!g_swNoBridge) xxl_display_bridge_init(plat);
    logf("ui: bridge init %s; ready", g_swNoBridge ? "SKIPPED" : "done");
    R.ready.store(true, std::memory_order_release);

    while (!R.quit.load(std::memory_order_relaxed))
    {
        const bool att = R.attached.load(std::memory_order_relaxed);
        pump(buttons, pads, timer->millis());
        if (att)
        {
            trk->update();
            if (!g_swNoBridge) xxl_display_bridge_update();
        }
        R.cUi.fetch_add(1, std::memory_order_relaxed);
        usleep(att ? 1000 : 20000);
    }
    logf("ui: quit");
    xxl_display_bridge_shutdown();
    return nullptr;
}
} // namespace

/* The firmware threads start on the first audio block, not at create(): MPC creates and drops an instance while it scans
 * plugins, and booting the firmware (and leaving its threads running) inside a scan is what could take MPC down. */
static pthread_t g_hb;
static bool g_hbRun = false;
static std::atomic<bool> g_hbQuit{ false };

static void ensureStarted()
{
    if (R.started.exchange(true)) return;
    R.plat = newPlatform(R.root);
    timerThreadStart();
    logf("start: timer thread started");
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8u << 20);
    if (pthread_create(&R.ui, &at, uiMain, nullptr) != 0)
    {
        logf("start: ui thread FAILED");
        R.started.store(false);
        R.attached.store(false);
        pthread_attr_destroy(&at);
        return;
    }
    pthread_attr_destroy(&at);
    R.uiRunning = true;
    logf("start: ui thread created");
    g_hbQuit.store(false);
    g_hbRun = pthread_create(&g_hb, nullptr, [](void*) -> void* {
        int tick = 0;
        while (!g_hbQuit.load())
        {
            usleep(100000);
            if (++tick % 20) continue;
            logf("beat: ui=%u render=%u notready=%u gate=%u att=%d evdrop=%u peak=%d play=%d", R.cUi.load(), R.cRender.load(), R.cNotReady.load(), R.cGate.load(), (int)R.attached.load(), R.cEvDrop.load(), R.peak.exchange(0), R.ready.load() ? (int)fw_isPlaying() : -1);
        }
        return nullptr;
    }, nullptr) == 0;
}

/* Library unload (MPC dlclose, or process exit): no thread may still be running code of this file afterwards. */
void runtimeShutdown()
{
    if (!R.started.load()) return;
    logf("unload: stopping threads");
    R.quit.store(true);
    if (R.uiRunning) { pthread_join(R.ui, nullptr); R.uiRunning = false; }
    g_hbQuit.store(true);
    if (g_hbRun) { pthread_join(g_hb, nullptr); g_hbRun = false; }
    timerThreadStop();
    logf("unload: threads joined");
}

bool runtimeAcquire(const char* root)
{
    bool exp = false;
    if (!R.attached.compare_exchange_strong(exp, true)) return false;   /* another instance owns the firmware */
    if (R.root.empty())
    {
        R.root = root && *root ? root : "/sdcard/TrackerXXL";
        mkdir(R.root.c_str(), 0755);
        g_logPath = R.root + "/trackerxxl.log";
        installCrashHandler(g_logPath);
        readSwitches(R.root);
    }
    logf("acquire: root=%s nofw=%d nobridge=%d noaudio=%d started=%d", R.root.c_str(), g_swNoFw, g_swNoBridge, g_swNoAudio, (int)R.started.load());
    if (g_swNoFw) { R.attached.store(false); return false; }
    return true;
}

void runtimeRelease()
{
    logf("release");
    if (!R.attached.load()) return;
    Event e = { EV_RELEASE_ALL, 0 };
    R.ring.push(e);
    R.attached.store(false);
}

bool post(const Event& e)
{
    if (!R.started.load() || R.ring.push(e)) return true;
    R.cEvDrop++;
    return false;
}

void midiToFirmware(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (!R.ready.load(std::memory_order_acquire)) return;
    if (!platformUsbMidi(R.plat)->q.push(status, d1, d2)) R.cMidiDrop++;
}

void setPadCols(int c) { R.padCols.store(c < 0 ? 0 : c > 2 ? 2 : c); }
void setHostSync(int on) { R.hostSync.store(on ? 1 : 0); }
bool runtimeStarted() { return R.started.load(); }

/* One 128-frame block: interleaved int16 stereo. */
void renderBlock(int16_t* out)
{
    if (R.cRender++ == 0) logf("render: first block");
    if (!R.started.load(std::memory_order_relaxed) && R.attached.load(std::memory_order_relaxed)) ensureStarted();
    if (g_swNoAudio) { memset(out, 0, 128 * 2 * sizeof(int16_t)); return; }
    if (!R.ready.load(std::memory_order_acquire) || !R.attached.load(std::memory_order_relaxed))
    {
        if (!R.ready.load()) R.cNotReady++;
        memset(out, 0, 128 * 2 * sizeof(int16_t));
        return;
    }
    if (!fw_gateAcquire())
    {
        R.cGate++;
        memset(out, 0, 128 * 2 * sizeof(int16_t));
        return;
    }
    fw_gateEnterRender();
    Tracker::getInstance()->updateAudioBuffers(g_left, g_right, 128);
    fw_gateLeaveRender();
    fw_gateRelease();
    if (fw_audioMuted()) { memset(out, 0, 128 * 2 * sizeof(int16_t)); return; }
    int pk = 0;
    for (int i = 0; i < 128; i++)
    {
        out[2 * i] = g_left[i];
        out[2 * i + 1] = g_right[i];
        const int a = g_left[i] < 0 ? -g_left[i] : g_left[i];
        if (a > pk) pk = a;
    }
    if (pk > R.peak.load(std::memory_order_relaxed)) R.peak.store(pk, std::memory_order_relaxed);
}

/* ---- public accessors (xxl_runtime.h) ---- */
bool buttonDown(int i) { return R.plat && platformButtons(R.plat)->down_(i); }
bool padDown(int i) { return R.plat && platformPads(R.plat)->down_(i); }
uint8_t ledLevel(int i) { return R.plat ? platformLeds(R.plat)->get(i) : 0; }
int padColumnsOffset() { return R.padCols.load() * 4; }
bool hostSyncOn() { return R.hostSync.load() != 0; }
bool ready() { return R.ready.load(); }
Counters counters()
{
    Counters c;
    c.renderBlocks = R.cRender;
    c.renderSkippedNotReady = R.cNotReady;
    c.renderSkippedGate = R.cGate;
    c.uiLoops = R.cUi;
    c.eventsDropped = R.cEvDrop;
    c.midiDropped = R.cMidiDrop;
    c.midiOutDropped = g_cnt_midiOutDropped;
    return c;
}
} // namespace xxl

__attribute__((destructor)) static void txl_unload() { xxl::runtimeShutdown(); }
