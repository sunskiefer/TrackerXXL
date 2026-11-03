/* TrackerXXL display hook: the seam between the Linux Platform (null FT812 driver) and the display -> skin bridge.
 *
 * The Polyend firmware draws by pushing FT812 coprocessor commands through DisplayDriver. On the plugin there is no FT812:
 * src/NullDisplayDriver.cpp implements DisplayDriver as a sink that executes nothing, always reports the coprocessor empty
 * (so display.update() never stalls) and counts the traffic. The display agent fills this seam, at control level, without
 * touching the Platform:
 *
 *   1. define the three xxl_display_bridge_* functions below (the platform provides weak no-op defaults, so a strong
 *      definition in any linked object replaces them; nothing in port/platform/src has to change);
 *   2. optionally register a Sink to see the raw command stream (word-level FT812 display-list/coprocessor words and
 *      RAM_G uploads: fonts, bitmaps);
 *   3. read input/LED state with the xxl_runtime.h accessors, and the firmware's own control objects through the core headers
 *      (everything the bridge needs is reachable from core: mtInterface, mtProject, sequencer, display.getControls...).
 *
 * Threading: bridge_init / bridge_update / bridge_shutdown run on the firmware UI thread (the same thread that runs
 * Tracker::update()), so they may call into the firmware without locks. A Sink callback runs on the thread that issued the
 * command (the UI thread for everything display.cpp does). The host-facing side (get_param/set_param/render) is a different
 * thread: publish results through atomics or a lock of your own and read them from get_param().
 */
#pragma once
#include <stdint.h>

class Platform;

namespace xxl_display
{
struct Stats
{
    uint32_t coproLists;   /* endCoProList() calls                                    */
    uint32_t cmdWords;     /* WriteData32() calls (FT812 command / display-list words) */
    uint32_t ramgWrites;   /* writeDataRAMG + writeFontRAMG calls                      */
    uint32_t ramgBytes;
    uint32_t streamFrames; /* updateStreamDma() calls                                  */
};
const Stats& stats();

struct Sink
{
    void (*onCoProListBegin)(void* user);
    void (*onCoProListEnd)(void* user);
    void (*onWord)(void* user, uint32_t word);                                              /* WriteData32 */
    void (*onRamg)(void* user, uint32_t addr, const uint8_t* data, uint32_t size, int isFont);
    void* user;
};
/* Pass nullptr to remove. The Sink struct is copied. */
void setSink(const Sink* sink);

/* Pacing: the FT812 needs time to execute a command list, and the firmware's display.update() is written to wait for it
 * (isCoProEmpty()). The null driver imitates that so the UI thread does not redraw thousands of times a second into nothing:
 * after each endCoProList() the coprocessor reports "busy" for this long. Default 2000 us (~3 lists per screen refresh =
 * ~150 refreshes/s upper bound); 0 = never busy (the bridge wants every frame as fast as the firmware produces it). */
void setCoProLatencyUs(uint32_t us);
} // namespace xxl_display

/* Defined weakly (no-ops) by the platform; the display bridge overrides them. */
extern "C++" {
void xxl_display_bridge_init(Platform* platform);   /* once, after Tracker::init(), on the UI thread */
void xxl_display_bridge_update();                   /* every UI loop iteration, after Tracker::update() */
void xxl_display_bridge_shutdown();                 /* when the runtime stops (process exit / hard teardown) */
}
