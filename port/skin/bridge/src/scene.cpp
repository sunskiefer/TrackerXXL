/* scene table storage: see include/xxl_scene.h */
#include "xxl_scene.h"
#include "scene_layout.h"

#include <algorithm>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{
constexpr int kTextMax = 64;          /* PARAM_TEXT_MAX in vst.json; the wrapper copies at most this */
struct Entry { const char* key; int idx; };
std::vector<Entry> g_sorted;
std::vector<int32_t> g_val;
std::vector<char*> g_text;            /* only for "<key>_t" parameters (nullptr otherwise) */
std::vector<uint8_t> g_isInput;
std::mutex g_mu;                      /* the bridge: held while it builds a frame */
std::mutex g_pm;                      /* the published copy: held for microseconds, so the audio thread never waits on a frame */
std::vector<int32_t> g_pval;
std::vector<char> g_ptext;            /* SCN_NPARAMS * kTextMax */
uint32_t g_prev = 1;
uint32_t g_rev = 1;
bool g_dirty = false;
std::vector<xxl_scene::Touch> g_touch;
bool g_ready = false;

uint16_t fold(const char* s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
    return (uint16_t)((h ^ (h >> 16)) & 0xFFFF);
}
bool endsWith(const char* s, const char* suf)
{
    const size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcmp(s + a - b, suf);
}
}

namespace xxl_scene
{
void init()
{
    if (g_ready) return;
    g_val.assign(SCN_NPARAMS, 0);
    g_pval.assign(SCN_NPARAMS, 0);
    g_ptext.assign((size_t)SCN_NPARAMS * kTextMax, 0);
    g_text.assign(SCN_NPARAMS, nullptr);
    g_isInput.assign(SCN_NPARAMS, 0);
    for (int i = 0; i < SCN_NPARAMS; i++)
    {
        const char* k = SCN_KEYS[i];
        g_sorted.push_back({ k, i });
        const size_t n = strlen(k);
        if (n > 2 && k[n - 2] == '_' && k[n - 1] == 't')
        {
            g_text[i] = (char*)calloc(kTextMax, 1);
        }
        if (i >= SCN_FIRST && ((k[0] == 'p' && k[1] == 'h' && n == 4) || (k[0] == 'l' && k[1] == 'h' && k[2] >= '0' && k[2] <= '9')))
            g_isInput[i] = 1;
    }
    std::sort(g_sorted.begin(), g_sorted.end(), [](const Entry& a, const Entry& b) { return strcmp(a.key, b.key) < 0; });
    g_ready = true;
}
int count() { return SCN_NPARAMS; }
int first() { return SCN_FIRST; }
int index(const char* key)
{
    size_t lo = 0, hi = g_sorted.size();
    while (lo < hi)
    {
        const size_t mid = (lo + hi) / 2;
        const int c = strcmp(g_sorted[mid].key, key);
        if (c == 0) return g_sorted[mid].idx;
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return -1;
}
/* the mutex is held by the bridge between begin() and end() */
bool setState(int idx, int value)
{
    if (idx < 0 || idx >= SCN_NPARAMS || g_val[idx] == value) return false;
    g_val[idx] = value;
    g_dirty = true;
    return true;
}
bool setText(int idx, const char* text)
{
    const int t = idx + 1;
    if (idx < 0 || t >= SCN_NPARAMS || !g_text[t]) return false;
    if (!text) text = "";
    if (!strncmp(g_text[t], text, kTextMax - 1) && strlen(text) < (size_t)kTextMax) return false;
    strncpy(g_text[t], text, kTextMax - 1);
    g_text[t][kTextMax - 1] = 0;
    int h = *g_text[t] ? fold(g_text[t]) : 0;
    if (h == g_val[t] && *g_text[t]) h = (h + 1) & 0xFFFF;   /* the value must move whenever the text does */
    g_val[t] = h;
    g_dirty = true;
    return true;
}
int getState(int idx) { return (idx >= 0 && idx < SCN_NPARAMS) ? g_val[idx] : 0; }
void begin() { g_mu.lock(); }
void end()
{
    if (g_dirty)
    {
        g_rev++; g_dirty = false;
        std::lock_guard<std::mutex> l(g_pm);
        g_pval = g_val;
        for (int i = 0; i < SCN_NPARAMS; i++)
            if (g_text[i]) memcpy(&g_ptext[(size_t)i * kTextMax], g_text[i], kTextMax);
        g_prev = g_rev;
    }
    g_mu.unlock();
}

int get(const char* key, char* buf, int len)
{
    if (!g_ready || len < 2) return 0;
    if (!strcmp(key, "display_rev"))
    {
        std::lock_guard<std::mutex> l(g_pm);
        return snprintf(buf, (size_t)len, "%u", g_prev);
    }
    const bool disp = endsWith(key, "_display");
    char base[64];
    if (disp)
    {
        const size_t n = strlen(key) - 8;
        if (n >= sizeof base) return 0;
        memcpy(base, key, n); base[n] = 0;
        key = base;
    }
    const int i = index(key);
    if (i < SCN_FIRST) return 0;                          /* platform controls answer for themselves */
    std::lock_guard<std::mutex> l(g_pm);
    if (disp)
    {
        if (!g_text[i]) return 0;
        return snprintf(buf, (size_t)len, "%.*s", kTextMax - 1, &g_ptext[(size_t)i * kTextMax]);
    }
    return snprintf(buf, (size_t)len, "%d", g_pval[i]);
}
bool set(const char* key, const char* val)
{
    if (!g_ready) return false;
    const int i = index(key);
    if (i < SCN_FIRST) return false;
    const int v = atoi(val ? val : "0");
    std::lock_guard<std::mutex> l(g_pm);          /* never the frame lock: a touch must not wait for the bridge */
    if (g_isInput[i])
    {
        g_pval[i] = v;
        if (v != 0 && g_touch.size() < 64) g_touch.push_back({ i, v });
    }
    return true;      /* display slots are the bridge's: a stray finger drag on a picture is ignored, the next frame overwrites it */
}
bool popTouch(Touch& t)
{
    std::lock_guard<std::mutex> l(g_pm);
    if (g_touch.empty()) return false;
    t = g_touch.front();
    g_touch.erase(g_touch.begin());
    return true;
}
}
