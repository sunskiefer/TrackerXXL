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

#include "../build/ctl_table.h"


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

struct Rt
{
    std::atomic<bool> started{ false };
    std::atomic<bool> attached{ false };
    std::atomic<bool> ready{ false };
    std::atomic<bool> quit{ false };
    std::atomic<int> padCols{ 0 };
    std::atomic<int> hostSync{ 1 };
    pthread_t ui;
    PlatformImpl* plat = nullptr;
    std::string root;
    EvRing ring;
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
            case EV_BTN_TAP: b->tap(e.a, now, CTL_TAP_MS); break;
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
    trk->getStartDisplayFunc()(plat);
    fw_buttonCallbacksInstall(buttons, pads);
    plat->getUartMidi()->init();
    trk->init(plat);
    fw_applyHostSync(R.hostSync.load() != 0);
    xxl_display_bridge_init(plat);
    R.ready.store(true, std::memory_order_release);

    while (!R.quit.load(std::memory_order_relaxed))
    {
        const bool att = R.attached.load(std::memory_order_relaxed);
        pump(buttons, pads, timer->millis());
        if (att)
        {
            trk->update();
            xxl_display_bridge_update();
        }
        R.cUi.fetch_add(1, std::memory_order_relaxed);
        usleep(att ? 1000 : 20000);
    }
    xxl_display_bridge_shutdown();
    return nullptr;
}
} // namespace

bool runtimeAcquire(const char* root)
{
    bool exp = false;
    if (!R.attached.compare_exchange_strong(exp, true)) return false;   /* another instance owns the firmware */
    if (!R.started.exchange(true))
    {
        R.root = root && *root ? root : "/sdcard/TrackerXXL";
        R.plat = newPlatform(R.root);
        timerThreadStart();
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, 8u << 20);
        if (pthread_create(&R.ui, &at, uiMain, nullptr) != 0)
        {
            R.started.store(false);
            R.attached.store(false);
            pthread_attr_destroy(&at);
            return false;
        }
        pthread_attr_destroy(&at);
    }
    return true;
}

void runtimeRelease()
{
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
    R.cRender++;
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
    for (int i = 0; i < 128; i++)
    {
        out[2 * i] = g_left[i];
        out[2 * i + 1] = g_right[i];
    }
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
