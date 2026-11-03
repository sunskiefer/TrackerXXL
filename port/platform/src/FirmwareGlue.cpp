/* TrackerXXL: everything the firmware expects its board support to provide, plus the calls into the firmware that need core
 * headers. See STUBS.md for the list of what is real and what is a documented stub. */
#include "xxl_internal.h"
#include "xxl_firmware.h"

#include "core/interface.h"
#include "mtConfig.h"
#include "mtMidi.h"
#include "mtSequencer.h"
#include "platform_math.h"

#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include "../build/ctl_check.h"   /* static_asserts: controls.json button indices == the firmware's enum */

/* callbacks defined in core/source/mtActions.cpp */
void onPotChange(uint8_t n, int16_t value);
void onButtonPush(uint8_t n);
void onButtonRelease(uint8_t n);
void onButtonHold(uint8_t n);
void onPadPush(uint8_t n);
void onPadRelease(uint8_t n);
void onPadHold(uint8_t n);

/* ---- weak defaults of the display-bridge hooks (display_hook.h) ---- */
__attribute__((weak)) void xxl_display_bridge_init(Platform*) {}
__attribute__((weak)) void xxl_display_bridge_update() {}
__attribute__((weak)) void xxl_display_bridge_shutdown() {}

namespace xxl
{
const int kFwPlayButton = interfaceButtonPlay;

void fw_encoder(int clicks)
{
    if (clicks > 32767) clicks = 32767;
    if (clicks < -32768) clicks = -32768;
    onPotChange(0, (int16_t)clicks);
}

bool fw_isPlaying() { return sequencer.isPlay(); }

void fw_buttonCallbacksInstall(SwitchGroupImpl* buttons, SwitchGroupImpl* pads)
{
    buttons->setActionCallback(SwitchGroup::kPress, onButtonPush);
    buttons->setActionCallback(SwitchGroup::kRelease, onButtonRelease);
    buttons->setActionCallback(SwitchGroup::kHold, onButtonHold);
    pads->setActionCallback(SwitchGroup::kPress, onPadPush);
    pads->setActionCallback(SwitchGroup::kRelease, onPadRelease);
    pads->setActionCallback(SwitchGroup::kHold, onPadHold);
}

/* HOST SYNC: the sequencer follows MPC through the firmware's own MIDI-sync path (mtMidi.cpp receiveStart/Clock/... gated by
 * mtConfig.midi.clkIn / transportIn == the port the message arrives on; host messages arrive on the "USB" port). The user's
 * configured values are saved on the first ON and restored on OFF. UI thread only. */
void fw_applyHostSync(bool on)
{
    static bool applied = false;
    static uint8_t savedClk = 0, savedTransport = 0;
    if (on)
    {
        if (!applied)
        {
            savedClk = mtConfig.midi.clkIn;
            savedTransport = mtConfig.midi.transportIn;
            applied = true;
        }
        mtConfig.midi.clkIn = clockIn_Usb;
        mtConfig.midi.transportIn = clockIn_Usb;
    }
    else if (applied)
    {
        mtConfig.midi.clkIn = savedClk;
        mtConfig.midi.transportIn = savedTransport;
        applied = false;
    }
}

/* ---- the audio "interrupt" gate: DISABLE_AUDIO_INTERRUPTS() / ENABLE_AUDIO_INTERRUPTS() ----
 * NVIC semantics (not counting): disable twice + enable once = enabled. Only the thread that closed the gate opens it; calls from
 * the audio block itself are no-ops (an ISR cannot be preempted by itself). The block holds the gate while it runs, so a UI
 * thread closing the gate waits for the block in progress, then keeps the audio block out until it opens the gate again. */
namespace
{
pthread_mutex_t g_gate = PTHREAD_MUTEX_INITIALIZER;
pthread_t g_owner;
bool g_held = false;
thread_local bool t_inRender = false;
std::atomic<bool> g_muted{ false };
}

void fw_gateEnterRender() { t_inRender = true; }
void fw_gateLeaveRender() { t_inRender = false; }

bool fw_gateAcquire()
{
    for (int i = 0; i < 200; i++)   /* up to ~2 ms */
    {
        if (pthread_mutex_trylock(&g_gate) == 0) return true;
        usleep(10);
    }
    return false;
}
void fw_gateRelease() { pthread_mutex_unlock(&g_gate); }
bool fw_audioMuted() { return g_muted.load(std::memory_order_relaxed); }
} // namespace xxl

void disableAudioInterrupt()
{
    using namespace xxl;
    if (t_inRender) return;
    const pthread_t me = pthread_self();
    if (g_held && pthread_equal(g_owner, me)) return;
    pthread_mutex_lock(&g_gate);
    g_owner = me;
    g_held = true;
}

void enableAudioInterrupt()
{
    using namespace xxl;
    if (t_inRender) return;
    if (g_held && pthread_equal(g_owner, pthread_self()))
    {
        g_held = false;
        pthread_mutex_unlock(&g_gate);
    }
}

/* ---- hardware.cpp (platforms/imx): startup / shutdown muting of the codec output ---- */
void muteAudioOutByKey() { xxl::g_muted.store(true); }
void unmuteAudioOutByKey() { xxl::g_muted.store(false); }
uint8_t isAudioOutMuted() { return xxl::g_muted.load() ? 1 : 0; }

/* ---- platform_math.cpp: CMSIS arm_sin_q15 (512-entry table, linear interpolation, input 0..0x7FFF = 0..2*pi) ---- */
int16_t sin_q15(int16_t x)
{
    static int16_t table[513];
    static bool init = false;
    if (!init)
    {
        for (int i = 0; i <= 512; i++)
        {
            double v = sin(2.0 * M_PI * i / 512.0) * 32768.0;
            table[i] = (int16_t)(v > 32767.0 ? 32767.0 : v < -32768.0 ? -32768.0 : floor(v + 0.5));
        }
        init = true;
    }
    const uint32_t u = (uint32_t)(uint16_t)x & 0x7FFFu;
    const int index = (int)(u >> 7);                 /* 512 entries over 15 bits */
    const int16_t fract = (int16_t)((u - ((uint32_t)index << 7)) << 8);
    const int16_t a = table[index], b = table[index + 1];
    int32_t s = ((int32_t)(0x7FFF - fract) * a) >> 16;
    s = (((int32_t)(int16_t)s << 16) + (int32_t)fract * b) >> 16;
    return (int16_t)(s << 1);
}
