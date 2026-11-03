/* shared between the bridge sources */
#pragma once
#include "bridge_core.h"
#include "xxl_scene.h"
#define SCN_WANT_CTL 1
#include "scene_layout.h"

namespace xxl_bridge
{
struct Counters { uint32_t frames, writes, textWrites, touches; };
extern Counters g_cnt;

/* the visible control at table slot i, or nullptr */
cDisplayControl* control(int hControlHandle);
bool visible(const cDisplayControl* c);
int  activeModule();                              /* 1.. = SCN_MODULES index of the screen on display, 0 = none known */

/* one frame of writes; every function writes through these so "changed" is tracked in one place */
inline bool S(int idx, int v) { return xxl_scene::setState(idx, v); }
inline void T(int idx, const char* t) { xxl_scene::setText(idx, t); }

/* pooled controls: label / bar / list / frame / bg-label (map_ctl.cpp) */
void ctlInit();
void ctlFrame(int module);
void ctlTouch(int idx, int value);                /* a list row was tapped */
/* pattern editor (map_pattern.cpp) */
void patInit();
void patFrame(int module);
void patTouch(int idx, int value);
/* waveform + markers (map_wave.cpp) */
void waveInit();
void waveFrame(int module);

/* firmware button taps, one press per bridge tick, release on the next */
void queueTap(int fwButton, int count);
void tapTick();

int key1(const char* fmt, int a);
int key2(const char* fmt, int a, int b);
}
