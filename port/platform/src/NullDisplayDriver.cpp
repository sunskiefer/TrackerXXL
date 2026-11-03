/* TrackerXXL: DisplayDriver with no FT812 behind it. Everything the firmware writes goes to counters and an optional Sink
 * (display_hook.h); the coprocessor is "busy" for setCoProLatencyUs() after each command list (pacing, default 2 ms); reads
 * return zeros.
 * The display -> skin bridge replaces nothing here: it registers a Sink and/or defines xxl_display_bridge_*(). */
#include "xxl_internal.h"
#include <string.h>
#include <unistd.h>

namespace xxl_display
{
static Stats g_stats;
static Sink g_sink = {};
static bool g_haveSink = false;
static uint32_t g_latencyUs = 2000;
static uint64_t g_busyUntilNs = 0;
void setCoProLatencyUs(uint32_t us) { g_latencyUs = us; }
const Stats& stats() { return g_stats; }
void setSink(const Sink* s)
{
    if (s) { g_sink = *s; g_haveSink = true; }
    else { g_sink = Sink(); g_haveSink = false; }
}
} // namespace xxl_display

namespace
{
using namespace xxl_display;

class NullDisplayDriver final : public DisplayDriver
{
  public:
    void setPowerState(bool) override {}
    void setBrightness(int) override {}
    void setRotation(int) override {}

    /* LOADIMAGE/GETPROPS read-back: no image decoder on the plugin; report an empty image. */
    void getImgProps(uint32_t& addr, uint32_t& width, uint32_t& height) override { addr = 0; width = 0; height = 0; }
    uint32_t getDisplayListOffset() override { return 0; }
    void IncCMDOffset(uint16_t) override {}
    void WriteData32(uint32_t data) override
    {
        g_stats.cmdWords++;
        if (g_haveSink && g_sink.onWord) g_sink.onWord(g_sink.user, data);
    }

    void beginCoProList() override { awaitCoProEmpty(); begin(); }
    void beginCoProListNoCheck() override { begin(); }
    void endCoProList() override
    {
        g_stats.coproLists++;
        g_busyUntilNs = xxl::nowNs() + (uint64_t)g_latencyUs * 1000ull;
        if (g_haveSink && g_sink.onCoProListEnd) g_sink.onCoProListEnd(g_sink.user);
    }
    void awaitCoProEmpty() override
    {
        const uint64_t now = xxl::nowNs();
        if (g_busyUntilNs > now) usleep((useconds_t)((g_busyUntilNs - now) / 1000ull));
    }
    uint8_t isCoProEmpty() override { return xxl::nowNs() >= g_busyUntilNs ? 1 : 0; }

    void writeDataRAMG(const uint8_t* d, uint32_t n, uint32_t addr) override { ramg(d, n, addr, 0); }
    void writeFontRAMG(const uint8_t* d, uint32_t n, uint32_t addr) override { ramg(d, n, addr, 1); }
    void readDataRAMG(uint8_t* d, uint32_t n, uint32_t) override { if (d) memset(d, 0, n); }
    void readBackDataRAMG(uint8_t* d, uint32_t n, uint32_t) override { if (d) memset(d, 0, n); }
    uint8_t sendString(const char*, uint32_t) override { return 1; }
    void writeDataToCMD(const uint8_t*, uint32_t) override {}

    void initStreamDma(uint16_t, uint16_t, uint16_t, uint8_t*, uint32_t) override {}
    bool updateStreamDma(bool) override { g_stats.streamFrames++; return true; }   /* "frame sent" */
    void stopStreamDma() override {}

  private:
    void begin()
    {
        if (g_haveSink && g_sink.onCoProListBegin) g_sink.onCoProListBegin(g_sink.user);
    }
    void ramg(const uint8_t* d, uint32_t n, uint32_t addr, int isFont)
    {
        g_stats.ramgWrites++;
        g_stats.ramgBytes += n;
        if (g_haveSink && g_sink.onRamg) g_sink.onRamg(g_sink.user, addr, d, n, isFont);
    }
};
} // namespace

namespace xxl
{
DisplayDriver* newNullDisplayDriver() { return new NullDisplayDriver(); }
}
