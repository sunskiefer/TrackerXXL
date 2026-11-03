/* TrackerXXL display -> skin bridge: replaces the FT812 rendering at the CONTROL level.
 *
 * The firmware still creates and updates its controls exactly as on the Tracker (display.createControl<...>, setControlText, ...); the
 * platform's null FT812 driver swallows the draw commands.  Every ~33 ms this file reads the controls (type, rect, text, colour role,
 * state, list / bar / pattern / waveform data) from the firmware's own objects and writes the numbered scene slots the skin shows.
 * Only slots whose value changed are written (xxl_scene), so MPC repaints only what moved.
 *
 * Seam (port/platform/display_hook.h): defines xxl_display_bridge_init / update / shutdown (strong definitions replace the weak ones).
 * Host side: EngineGlue's get_param / set_param call xxl_scene::get / set first (see docs/PORTING_NOTES.md, S5). */
#include "bridge_internal.h"

namespace xxl_bridge
{
Counters g_cnt = {};
static ModuleBase* const* modules()
{
    static ModuleBase* const m[] = { &patternEditor, &sampleEditor, &sampleImporter, &samplePlayback, &sampleRecorder, &instrumentEditor,
                                     &songEditor, &projectEditor, &configEditor, &masterParams, &performanceMode, &imageViewer };
    return m;
}
static_assert(SCN_NMODULES == 13, "module list differs from the generated one (SCN_MODULES): regenerate with build_skin.py and no --modules filter");

cDisplayControl* control(int h)
{
    if (h < 0 || h >= controlsCount) return nullptr;
    return display.controlsTable[h];
}
bool visible(const cDisplayControl* c) { return c && (c->style & controlStyleShow); }
int activeModule()
{
    ModuleBase* on = mtInterface.onScreenModule;
    for (int i = 0; i < SCN_NMODULES - 1; i++)
        if (modules()[i] == on) return i + 1;
    return 0;
}
int key1(const char* fmt, int a) { char k[48]; snprintf(k, sizeof k, fmt, a); return xxl_scene::index(k); }
int key2(const char* fmt, int a, int b) { char k[48]; snprintf(k, sizeof k, fmt, a, b); return xxl_scene::index(k); }

/* ---- taps: cursor movement for touch.  A tap = press now, release on the next bridge tick (SwitchGroup semantics). */
static std::vector<int> g_taps;
static int g_release = -1;
void queueTap(int fwButton, int count) { for (int i = 0; i < count && g_taps.size() < 64; i++) g_taps.push_back(fwButton); }
void tapTick()
{
    if (g_release >= 0) { onButtonRelease((uint8_t)g_release); g_release = -1; return; }
    if (!g_taps.empty())
    {
        const int b = g_taps.front(); g_taps.erase(g_taps.begin());
        onButtonPush((uint8_t)b);
        g_release = b;
    }
}
}

using namespace xxl_bridge;

static uint64_t nowMs()
{
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static uint64_t g_last = 0;
static bool g_inited = false;

extern "C++" void xxl_display_bridge_init(Platform*)
{
    xxl_scene::init();
    ctlInit(); patInit(); waveInit();
    g_inited = true;
}

extern "C++" void xxl_display_bridge_update()
{
    if (!g_inited) return;
    const uint64_t t = nowMs();
    if (t - g_last < 33) return;                      /* 30 Hz: MPC's screen thread pays for every repaint */
    g_last = t;
    g_cnt.frames++;

    tapTick();
    xxl_scene::Touch tc;
    while (xxl_scene::popTouch(tc))
    {
        g_cnt.touches++;
        patTouch(tc.idx, tc.value);
        ctlTouch(tc.idx, tc.value);
    }

    const int mod = activeModule();
    xxl_scene::begin();
    ctlFrame(mod);
    patFrame(mod);
    waveFrame(mod);
    xxl_scene::end();
}

extern "C++" void xxl_display_bridge_shutdown()
{
    g_inited = false;
}
