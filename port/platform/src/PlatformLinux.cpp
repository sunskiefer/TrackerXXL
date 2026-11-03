/* TrackerXXL: Linux implementation of the Polyend Platform (core/include/Platform.h), for the Akai Force / MPC OS VST2.
 * Hardware-less: codec, radio, battery, USB and the FT812 are inert; buttons, pads, encoder and MIDI are fed by the host
 * (src/Runtime.cpp); time is CLOCK_MONOTONIC; storage is a folder (src/FileSystemLinux.cpp). */
#include "xxl_internal.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace xxl
{
std::atomic<uint32_t> g_cnt_midiOutDropped{ 0 };

uint64_t nowNs()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t g_t0 = nowNs();

/* =============================================================================================== SwitchGroup */
void SwitchGroupImpl::press(int idx, uint32_t nowMs)
{
    if (idx < 0 || idx >= count || down[idx].load()) return;
    down[idx].store(1);
    pressedAt[idx] = nowMs;
    releaseAt[idx] = 0;
    holdFired[idx] = false;
    if (cbs[kPress]) cbs[kPress]((uint8_t)idx);
}

void SwitchGroupImpl::release(int idx)
{
    if (idx < 0 || idx >= count || !down[idx].load()) return;
    down[idx].store(0);
    releaseAt[idx] = 0;
    if (cbs[kRelease]) cbs[kRelease]((uint8_t)idx);
}

void SwitchGroupImpl::tap(int idx, uint32_t nowMs, uint32_t tapMs)
{
    if (idx < 0 || idx >= count) return;
    if (down[idx].load()) release(idx);   /* a second tap before the first released: release, then press again */
    press(idx, nowMs);
    releaseAt[idx] = nowMs + (tapMs ? tapMs : 1);
    if (releaseAt[idx] == 0) releaseAt[idx] = 1;
}

void SwitchGroupImpl::poll(uint32_t nowMs)
{
    for (int i = 0; i < count; i++)
    {
        if (!down[i].load(std::memory_order_relaxed)) continue;
        if (releaseAt[i] && (int32_t)(nowMs - releaseAt[i]) >= 0)
        {
            release(i);
            continue;
        }
        if (!holdFired[i] && (int32_t)(nowMs - pressedAt[i]) >= holdMs)
        {
            holdFired[i] = true;
            if (cbs[kHold]) cbs[kHold]((uint8_t)i);
        }
    }
}

void SwitchGroupImpl::releaseAll()
{
    for (int i = 0; i < count; i++) release(i);
}

/* =============================================================================================== MIDI */
bool MidiQueue::push(uint8_t status, uint8_t d1, uint8_t d2)
{
    const uint32_t h = head.load(std::memory_order_relaxed);
    if (h - tail.load(std::memory_order_acquire) >= kSize) return false;
    slot[h & (kSize - 1)] = ((uint32_t)status << 16) | ((uint32_t)d1 << 8) | d2;
    head.store(h + 1, std::memory_order_release);
    return true;
}

bool MidiQueue::pop(uint8_t& status, uint8_t& d1, uint8_t& d2)
{
    const uint32_t t = tail.load(std::memory_order_relaxed);
    if (t == head.load(std::memory_order_acquire)) return false;
    const uint32_t v = slot[t & (kSize - 1)];
    status = (uint8_t)(v >> 16);
    d1 = (uint8_t)(v >> 8);
    d2 = (uint8_t)v;
    tail.store(t + 1, std::memory_order_release);
    return true;
}

/* Same dispatch as the hardware UsbMidiImpl::read() (platforms/imx/source/UsbMidiImpl.cpp); channels are 1-based. */
void UsbMidiImpl::read()
{
    uint8_t st, d1, d2;
    if (!q.pop(st, d1, d2)) return;
    if (st >= 0xF0)
    {
        switch (st)
        {
            case 0xF8: case 0xFA: case 0xFB: case 0xFC:
                if (actions.realTime) actions.realTime(st);
                break;
            case 0xF2:
                if (actions.songPosition) actions.songPosition((uint16_t)((d1 & 0x7f) | ((d2 & 0x7f) << 7)));
                break;
            default: break;
        }
        return;
    }
    const uint8_t type = st & 0xF0, ch = (uint8_t)((st & 0x0F) + 1);
    switch (type)
    {
        case 0x80: if (actions.noteOff) actions.noteOff(ch, d1, d2); break;
        case 0x90: if (actions.noteOn) actions.noteOn(ch, d1, d2); break;
        case 0xB0: if (actions.controlChange) actions.controlChange(ch, d1, d2); break;
        default: break;
    }
}

/* =============================================================================================== inert hardware */
class CodecImpl final : public Sgtl5000
{
  public:
    void init() override {}
    void setInputType(InputType) override {}
    void setHeadphoneSource(HeadphoneSource) override {}
    void setLineInLevel(int) override {}
    void setMicGain(int) override {}
    void setHeadphoneMuted(bool) override {}
    void setMasterVolume(float) override {}
    void setAutoVolumeEnabled(bool) override {}
    void configureAutoVolume(AutoVolumeParams*) override {}
    void setEnhanceBassEnabled(bool) override {}
    void configureEnhanceBass(EnhanceBassParams*) override {}
    void setEqualizerType(EqualizerType) override {}
    void configureFiveBandEqualizer(float*) override {}
    void enableSurroundSound(bool) override {}
    void configureSurroundSound(SurroundSoundParams*) override {}
    void enableHardwareProcessor(bool) override {}
};

class RadioImpl final : public Radio
{
  public:
    void enableLineInput(bool) override {}
    void enable(bool) override {}
    bool setFrequency(float f) override { freq = f; return false; }
    void setSeekCallback(void (*)(void)) override {}
    void seek(SeekDirection) override {}
    void updateSeek() override {}
    bool updateRDS() override { return false; }
    float getFrequency() override { return freq; }
    float getBaseFrequency() override { return 87.5f; }
    float getSpacing() override { return 0.1f; }
    bool isinitialized() override { return false; }
    void clearRDS() override {}
    void setRegion(Region) override {}
    bool isRegionChanged() override { return false; }
    char* getStationName() override { return name; }

  private:
    float freq = 87.5f;
    char name[16] = { 0 };
};

class BatteryImpl final : public Battery
{
  public:
    void enableMonitoring() override {}
    int getLevel() override { return 100; }
    float getVoltage() override { return 4.2f; }
    bool getCharging() override { return false; }
    int getTimeTo() override { return 0; }
    bool getState() override { return true; }
    bool isEmpty() override { return false; }
};

class EncoderImpl final : public Encoder
{
  public:
    void setAcceleration(int a) override { acceleration = a; }
    void setResolution(int r) override { resolution = r; }
    int acceleration = 0, resolution = 0;
};

class UsbAudioImpl final : public UsbAudio
{
  public:
    void beginFillData() override {}
    void fillChannelData(void*, int, int) override {}
    void finishFillData() override {}
};

class UsbImpl final : public Usb
{
  public:
    void init(void) override {}
    void reset(void) override {}
    void update(void) override {}
    void setClassState(ClassType, bool) override {}
    bool getClassState(ClassType) override { return false; }
    bool isAttached() override { return false; }
};

/* =============================================================================================== Timer + MIDI-clock thread */
namespace
{
struct ClockSched
{
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    void (*cb)() = nullptr;
    uint64_t deadlineNs = 0;
    std::atomic<bool> run{ false };
    pthread_t th;
    bool started = false;
} g_clk;

void* clockThread(void*)
{
    while (g_clk.run.load())
    {
        void (*fire)() = nullptr;
        pthread_mutex_lock(&g_clk.m);
        if (g_clk.cb && nowNs() >= g_clk.deadlineNs)
        {
            fire = g_clk.cb;
            g_clk.cb = nullptr;
        }
        pthread_mutex_unlock(&g_clk.m);
        if (fire) fire();
        usleep(300);
    }
    return nullptr;
}
} // namespace

class TimerImpl final : public Timer
{
  public:
    uint32_t millis() override { return (uint32_t)((nowNs() - g_t0) / 1000000ull); }
    uint32_t micros() override { return (uint32_t)((nowNs() - g_t0) / 1000ull); }
    void delay(int ms) override { if (ms > 0) usleep((useconds_t)ms * 1000); }
    uint32_t clockCycles() override { return (uint32_t)(nowNs() - g_t0); }   /* "cycles" are nanoseconds */
    uint32_t clockCyclesPerUs() override { return 1000; }

    /* Same contract as the i.MX QTMR version: a still-pending callback fires immediately when a new one is scheduled. */
    void scheduleMidiClockUs(uint32_t delayUs, void (*callback)()) override
    {
        void (*prev)() = nullptr;
        pthread_mutex_lock(&g_clk.m);
        prev = g_clk.cb;
        g_clk.cb = nullptr;
        pthread_mutex_unlock(&g_clk.m);
        if (prev) prev();
        if (delayUs <= 2 || !g_clk.run.load())
        {
            if (callback) callback();
            return;
        }
        pthread_mutex_lock(&g_clk.m);
        g_clk.cb = callback;
        g_clk.deadlineNs = nowNs() + (uint64_t)delayUs * 1000ull;
        pthread_mutex_unlock(&g_clk.m);
    }
    void cancelMidiClock() override
    {
        pthread_mutex_lock(&g_clk.m);
        g_clk.cb = nullptr;
        pthread_mutex_unlock(&g_clk.m);
    }
};

void timerThreadStart()
{
    if (g_clk.started) return;
    g_clk.run.store(true);
    g_clk.started = pthread_create(&g_clk.th, nullptr, clockThread, nullptr) == 0;
    if (!g_clk.started) g_clk.run.store(false);
}

void timerThreadStop()
{
    if (!g_clk.started) return;
    g_clk.run.store(false);
    pthread_join(g_clk.th, nullptr);
    g_clk.started = false;
}

/* =============================================================================================== ConfigManager (EEPROM -> file) */
namespace
{
class ConfigManagerImpl final : public ConfigManager
{
  public:
    explicit ConfigManagerImpl(const std::string& root) : path(root + "/trackerConfig") {}
    bool readData(int address, void* data, int size) override
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        memset(data, 0, (size_t)size);
        if (fseek(f, address, SEEK_SET) == 0) (void)!fread(data, 1, (size_t)size, f);
        fclose(f);
        return true;
    }
    void writeData(int address, void* data, int size) override
    {
        FILE* f = fopen(path.c_str(), "r+b");
        if (!f) f = fopen(path.c_str(), "w+b");
        if (!f) return;
        if (fseek(f, address, SEEK_SET) != 0)
        {   /* writing past the end of a short file: pad with zeros */
            fseek(f, 0, SEEK_END);
            long end = ftell(f);
            for (long i = end; i < address; i++) fputc(0, f);
        }
        fwrite(data, 1, (size_t)size, f);
        fclose(f);
    }

  private:
    std::string path;
};

/* CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF) = FastCRC32 on the MK66, so project files stay interchangeable.
 * (The i.MX build returns 0; the core only stores/compares it.) */
class CrcCalculatorImpl final : public CrcCalculator
{
  public:
    uint32_t calculate32bitCrc(const uint8_t* data, const uint16_t len) override
    {
        static uint32_t table[256];
        static bool init = false;
        if (!init)
        {
            for (uint32_t i = 0; i < 256; i++)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[i] = c;
            }
            init = true;
        }
        uint32_t crc = 0xFFFFFFFFu;
        for (uint16_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }
};
} // namespace

ConfigManager* newConfigManager(const std::string& root) { return new ConfigManagerImpl(root); }
CrcCalculator* newCrcCalculator() { return new CrcCalculatorImpl(); }

/* =============================================================================================== Platform */
class PlatformImpl final : public Platform
{
  public:
    explicit PlatformImpl(const std::string& root)
        : buttons(128), pads(48), fs(newFileSystem(root)), config(newConfigManager(root)), crc(newCrcCalculator()), display(newNullDisplayDriver())
    {
        sysInfo.buildId = "TrackerXXL 0.1 (Linux ARMv7 plugin platform)";
    }
    void setPowerMode(bool) override {}               /* no power rail to switch; a host plugin cannot power itself off */
    void resetMCU() override {}                       /* nothing to reset */
    void runBootloader(char*) override {}             /* firmware updates are done by installing a new plugin */
    void setPowerButtonCallback(PowerButtonCallback cb) override { powerCb = cb; }
    bool getPowerButtonState() override { return false; }
    bool readForceStartUp() override { return true; }

    Sgtl5000* getCodecAudio() override { return &codec; }
    Radio* getRadio() override { return &radio; }
    SwitchGroup* getButtons() override { return &buttons; }
    SwitchGroup* getPads() override { return &pads; }
    Leds* getLeds() override { return &leds; }
    Battery* getBattery() override { return &battery; }
    Timer* getTimer() override { return &timer; }
    Encoder* getEncoder() override { return &encoder; }
    DisplayDriver* getDisplayDriver() override { return display; }
    UsbAudio* getUsbAudio() override { return &usbAudio; }
    UartMidi* getUartMidi() override { return &uart; }
    UsbMidi* getUsbMidi() override { return &usbMidi; }
    ConfigManager* getConfigManager() override { return config; }
    FileSystem* getFileSystem() override { return fs; }
    CrcCalculator* getCrcCalculator() override { return crc; }
    SystemInfo* getSystemInfo() override { return &sysInfo; }
    Usb* getUsb() override { return &usb; }

    SwitchGroupImpl buttons, pads;
    LedsImpl leds;
    UsbMidiImpl usbMidi;

  private:
    CodecImpl codec;
    RadioImpl radio;
    BatteryImpl battery;
    TimerImpl timer;
    EncoderImpl encoder;
    UsbAudioImpl usbAudio;
    UartMidiImpl uart;
    UsbImpl usb;
    FileSystem* fs;
    ConfigManager* config;
    CrcCalculator* crc;
    DisplayDriver* display;
    PowerButtonCallback powerCb = nullptr;
    SystemInfo sysInfo;
};

PlatformImpl* newPlatform(const std::string& root) { return new PlatformImpl(root); }
Platform* asPlatform(PlatformImpl* p) { return p; }
SwitchGroupImpl* platformButtons(PlatformImpl* p) { return &p->buttons; }
SwitchGroupImpl* platformPads(PlatformImpl* p) { return &p->pads; }
LedsImpl* platformLeds(PlatformImpl* p) { return &p->leds; }
UsbMidiImpl* platformUsbMidi(PlatformImpl* p) { return &p->usbMidi; }
Timer* platformTimer(PlatformImpl* p) { return p->getTimer(); }
} // namespace xxl
