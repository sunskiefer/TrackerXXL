/* TrackerXXL Linux Platform: internal declarations shared by the platform sources (no firmware headers here). */
#pragma once
#include "DisplayDriver.h"
#include "ConfigManager.h"
#include "Platform.h"
#include "../xxl_runtime.h"
#include "../display_hook.h"

#include <atomic>
#include <stdint.h>
#include <string>

namespace xxl
{
uint64_t nowNs();                       /* CLOCK_MONOTONIC, ns */
extern std::atomic<uint32_t> g_cnt_midiOutDropped;

/* ---- host -> firmware events (produced by the host thread, consumed by the UI thread) ---- */
enum EvType : uint8_t
{
    EV_BTN_PRESS,    /* a = firmware button index */
    EV_BTN_RELEASE,
    EV_BTN_TAP,      /* press now, release after CTL_TAP_MS */
    EV_PAD_PRESS,    /* a = firmware pad index 0..47 */
    EV_PAD_RELEASE,
    EV_PAD_TAP,
    EV_ENC,          /* a = signed encoder clicks */
    EV_STOP,         /* press PLAY only while the sequencer plays */
    EV_SYNC_MODE,    /* a = 0/1 */
    EV_RELEASE_ALL
};
struct Event
{
    uint8_t type;
    int16_t a;
};

/* ---- SwitchGroup (buttons / pads) ---- */
class SwitchGroupImpl final : public SwitchGroup
{
  public:
    explicit SwitchGroupImpl(int count) : count(count) {}
    void setHoldTime(int timeMs) override { holdMs = timeMs; }
    bool isButtonPressed(int idx) override { return idx >= 0 && idx < count && down[idx].load(std::memory_order_relaxed) != 0; }
    void setActionCallback(ActionType action, void (*callback)(uint8_t)) override
    {
        if ((unsigned)action < 3) cbs[action] = callback;
    }
    /* UI thread only */
    void press(int idx, uint32_t nowMs);
    void release(int idx);
    void poll(uint32_t nowMs);      /* hold detection + tap auto-release */
    void tap(int idx, uint32_t nowMs, uint32_t tapMs);
    void releaseAll();
    bool down_(int idx) const { return idx >= 0 && idx < count && down[idx].load(std::memory_order_relaxed) != 0; }

  private:
    static constexpr int kMax = 128;
    int count;
    int holdMs = 500;
    void (*cbs[3])(uint8_t) = { nullptr, nullptr, nullptr };
    std::atomic<uint8_t> down[kMax] = {};
    uint32_t pressedAt[kMax] = {};
    uint32_t releaseAt[kMax] = {};   /* tap auto-release time, 0 = none */
    bool holdFired[kMax] = {};
};

class LedsImpl final : public Leds
{
  public:
    void setLed(int idx, bool enabled, int brightness) override
    {
        if (idx >= 0 && idx < 128) level[idx].store(enabled ? (uint8_t)(brightness < 0 ? 0 : brightness > 255 ? 255 : brightness) : 0, std::memory_order_relaxed);
    }
    uint8_t get(int idx) const { return (idx >= 0 && idx < 128) ? level[idx].load(std::memory_order_relaxed) : 0; }

  private:
    std::atomic<uint8_t> level[128] = {};
};

/* ---- MIDI input queue shared by UartMidi/UsbMidi stubs: single producer, single consumer ---- */
struct MidiQueue
{
    static constexpr uint32_t kSize = 512; /* power of two */
    std::atomic<uint32_t> head{ 0 }, tail{ 0 };
    uint32_t slot[kSize];
    bool push(uint8_t status, uint8_t d1, uint8_t d2);
    bool pop(uint8_t& status, uint8_t& d1, uint8_t& d2);
    bool empty() const { return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire); }
};

class UsbMidiImpl final : public UsbMidi
{
  public:
    bool isReadAvailable() override { return !q.empty(); }
    void read() override;
    void sendRealTime(MessageType) override { g_cnt_midiOutDropped++; }
    void sendControlChange(uint8_t, uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendProgramChange(uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendAfterTouch(uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendPolyPressure(uint8_t, uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendPitchBend(int, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void setReceiveActions(MidiReceiveActions* a) override { if (a) actions = *a; }
    MidiQueue q;

  private:
    MidiReceiveActions actions = {};
};

class UartMidiImpl final : public UartMidi
{
  public:
    bool isReadAvailable() override { return false; } /* the jack input does not exist on the plugin; everything comes in as "USB" */
    void read() override {}
    void sendRealTime(MessageType) override { g_cnt_midiOutDropped++; }
    void sendControlChange(uint8_t, uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendProgramChange(uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendAfterTouch(uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendPolyPressure(uint8_t, uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendPitchBend(int, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override { g_cnt_midiOutDropped++; }
    void init() override {}
    void setReceiveActions(MidiReceiveActions* a) override { if (a) actions = *a; }
    void setReceiveNoteOnAction(void (*action)(uint8_t, uint8_t, uint8_t)) override { actions.noteOn = action; }

  private:
    MidiReceiveActions actions = {};
};

/* ---- the platform ---- */
FileSystem* newFileSystem(const std::string& root);
void deleteFileSystem(FileSystem*);
ConfigManager* newConfigManager(const std::string& root);
CrcCalculator* newCrcCalculator();
DisplayDriver* newNullDisplayDriver();

class PlatformImpl;
PlatformImpl* newPlatform(const std::string& root);
Platform* asPlatform(PlatformImpl*);
SwitchGroupImpl* platformButtons(PlatformImpl*);
SwitchGroupImpl* platformPads(PlatformImpl*);
LedsImpl* platformLeds(PlatformImpl*);
UsbMidiImpl* platformUsbMidi(PlatformImpl*);
Timer* platformTimer(PlatformImpl*);
/* runtime (Runtime.cpp) */
bool runtimeAcquire(const char* root);   /* false if another instance already owns the firmware */
void runtimeRelease();
bool runtimeStarted();
bool post(const Event& e);               /* host thread -> UI thread */
void midiToFirmware(uint8_t status, uint8_t d1, uint8_t d2);   /* into the firmware's (USB) MIDI input queue, read in the audio block */
void setPadCols(int cols);
void setHostSync(int on);
void renderBlock(int16_t* outLR);        /* 128 frames, interleaved int16 stereo */
void timerThreadStart();   /* MIDI-clock scheduler thread */
void timerThreadStop();
} // namespace xxl
