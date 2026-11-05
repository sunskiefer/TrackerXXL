#include "ui.h"
#include "own.h"
#include "xxl_scene.h"
#include "scene_layout.h"
#include "own_ctl.h"
#include <algorithm>
#include <vector>

namespace ui
{
namespace
{
constexpr int kMaxPool = 64, kRows = 14, kNCtl = (int)(sizeof OWN_CTL / sizeof OWN_CTL[0]);
struct Pools
{
    int lA[kMaxPool], lB[kMaxPool], lC[kMaxPool];
    int bS[kMaxPool], bV[kMaxPool];
    int sS[kMaxPool], sRow[kMaxPool][kRows], sVal[kMaxPool][kRows];
    int gS[kMaxPool], fS[kMaxPool];
    int hit[kMaxPool][SCN_NMODULES];
} P;
struct Want
{
    int lA[kMaxPool]; char tA[kMaxPool][64];
    int lB[kMaxPool]; char tB[kMaxPool][64];
    int lC[kMaxPool];
    int bS[kMaxPool], bV[kMaxPool];
    int sS[kMaxPool]; int sRow[kMaxPool][kRows]; char sTxt[kMaxPool][kRows][64]; char sVal[kMaxPool][kRows][64]; bool sHasVal[kMaxPool][kRows];
    int gS[kMaxPool], fS[kMaxPool];
} W;
struct NameIdx { const char* name; int row; };
std::vector<NameIdx> g_names;
std::vector<ListTap> g_taps;
bool g_inited = false;

int key1(const char* fmt, int a) { char k[48]; snprintf(k, sizeof k, fmt, a); return xxl_scene::index(k); }
int key2(const char* fmt, int a, int b) { char k[48]; snprintf(k, sizeof k, fmt, a, b); return xxl_scene::index(k); }
void copyText(char* d, const char* s)
{
    if (!s) { d[0] = 0; return; }
    strncpy(d, s, 63); d[63] = 0;
}
inline bool S(int idx, int v) { return xxl_scene::setState(idx, v); }
inline void T(int idx, const char* t) { xxl_scene::setText(idx, t); }
}

void init()
{
    if (g_inited) return;
    memset(&P, -1, sizeof P);
    for (int k = 0; k < SCN_LBL_POOL && k < kMaxPool; k++) { P.lA[k] = key1("lb%02da", k); P.lB[k] = key1("lb%02db", k); P.lC[k] = key1("lb%02dc", k); }
    for (int k = 0; k < SCN_BAR_POOL && k < kMaxPool; k++) { P.bS[k] = key1("br%02ds", k); P.bV[k] = key1("br%02dv", k); }
    for (int k = 0; k < SCN_LST_POOL && k < kMaxPool; k++)
    {
        P.sS[k] = key1("ls%02ds", k);
        for (int r = 0; r < kRows; r++) { P.sRow[k][r] = key2("ls%02dr%02d", k, r); P.sVal[k][r] = key2("ls%02dr%02dv", k, r); }
        for (int m = 0; m < SCN_NMODULES; m++) P.hit[k][m] = key2("lh%02dm%02d", k, m);
    }
    for (int k = 0; k < SCN_BGL_POOL && k < kMaxPool; k++) P.gS[k] = key1("bg%02d", k);
    for (int k = 0; k < SCN_FRM_POOL && k < kMaxPool; k++) P.fS[k] = key1("fr%02d", k);
    for (int i = 0; i < kNCtl; i++) g_names.push_back({ OWN_CTL[i].name, i });
    std::sort(g_names.begin(), g_names.end(), [](const NameIdx& a, const NameIdx& b) { return strcmp(a.name, b.name) < 0; });
    g_inited = true;
}

int row(const char* name)
{
    size_t lo = 0, hi = g_names.size();
    while (lo < hi)
    {
        const size_t mid = (lo + hi) / 2;
        const int c = strcmp(g_names[mid].name, name);
        if (c == 0) return g_names[mid].row;
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    static int warned = 0;
    if (warned < 20) { warned++; xxl::logf("ui: no control named %s", name); }
    return -1;
}
int module(int r) { return r >= 0 ? OWN_CTL[r].mi : 0; }
int rows(int r) { return r >= 0 ? OWN_CTL[r].rows : 0; }
int places(int r) { return r >= 0 ? OWN_CTL[r].nplaces : 0; }

void begin() { memset(&W, 0, sizeof W); }

void label(int r, const char* a, int va, const char* b, int vb, int line)
{
    if (r < 0) return;
    const OwnCtl& c = OWN_CTL[r];
    if (c.cls != 'L') return;
    const int k = c.pool;
    if (va >= c.nva) va = c.nva ? c.nva - 1 : 0;
    W.lA[k] = 1 + c.mi * 8 + va * 2;
    copyText(W.tA[k], a);
    if (b && *b)
    {
        if (vb >= c.nvb) vb = c.nvb ? c.nvb - 1 : 0;
        W.lB[k] = 1 + c.mi * 4 + vb;
        copyText(W.tB[k], b);
    }
    if (line > 0) W.lC[k] = 1 + c.mi * 16 + line;
}
void dots(int r, int count, int current)
{
    if (r < 0 || count < 2 || count > 3) return;
    const OwnCtl& c = OWN_CTL[r];
    if (c.cls != 'L') return;
    W.lC[c.pool] = 1 + c.mi * 16 + 4 + (count - 2) * 3 + std::min(current, count - 1);
}
void bar(int r, int pct, bool disabled)
{
    if (r < 0) return;
    const OwnCtl& c = OWN_CTL[r];
    if (c.cls != 'B') return;
    const int k = c.pool;
    if (disabled) { W.bS[k] = 1 + c.mi * 6 + 1; return; }
    pct = std::max(0, std::min(100, pct));
    const int f = (int)(pct / 100.0f * (SCN_BAR_FRAMES - 1) + 0.5f);
    W.bV[k] = std::max(0, std::min(SCN_BAR_FRAMES - 1, f));
    W.bS[k] = 1 + c.mi * 6;
}
void barSigned(int r, int v, int range)
{
    bar(r, (int)((v + range) * 100.0f / (2 * range) + 0.5f));
}
void list(int r, int n, const char* const* names, const char* const* vals, int sel, int top, const bool* detail)
{
    if (r < 0) return;
    const OwnCtl& c = OWN_CTL[r];
    if (c.cls != 'S') return;
    const int k = c.pool;
    W.sS[k] = 1 + c.mi * 4;
    const int nl = std::min<int>(c.rows, kRows);
    for (int row = 0; row < nl; row++)
    {
        const int idx = row + top;
        if (idx >= n) break;
        int kind = 0;
        if (detail && detail[idx]) kind = 1;
        else if (idx == sel) kind = 1;
        W.sRow[k][row] = 1 + c.mi * 6 + kind;
        copyText(W.sTxt[k][row], names[idx]);
        if (vals) { copyText(W.sVal[k][row], vals[idx]); W.sHasVal[k][row] = true; }
    }
}
void bg(int r)
{
    if (r < 0 || OWN_CTL[r].cls != 'G') return;
    W.gS[OWN_CTL[r].pool] = 1 + OWN_CTL[r].mi;
}
void frame(int r, int place)
{
    if (r < 0 || OWN_CTL[r].cls != 'F' || OWN_CTL[r].nplaces == 0) return;
    place = std::max(0, std::min<int>(place, OWN_CTL[r].nplaces - 1));
    W.fS[OWN_CTL[r].pool] = 1 + OWN_CTL[r].mi * 12 + 1 + place;
}

void end()
{
    /* list taps first: the skin reports lhKKmMM = row + 1 */
    xxl_scene::begin();
    for (int k = 0; k < SCN_LBL_POOL && k < kMaxPool; k++)
    {
        if (P.lA[k] >= 0) { S(P.lA[k], W.lA[k]); T(P.lA[k], W.lA[k] ? W.tA[k] : ""); }
        if (P.lB[k] >= 0) { S(P.lB[k], W.lB[k]); T(P.lB[k], W.lB[k] ? W.tB[k] : ""); }
        if (P.lC[k] >= 0) S(P.lC[k], W.lC[k]);
    }
    for (int k = 0; k < SCN_BAR_POOL && k < kMaxPool; k++) { S(P.bS[k], W.bS[k]); S(P.bV[k], W.bV[k]); }
    for (int k = 0; k < SCN_LST_POOL && k < kMaxPool; k++)
    {
        S(P.sS[k], W.sS[k]);
        for (int r = 0; r < kRows; r++)
        {
            if (P.sRow[k][r] >= 0) { S(P.sRow[k][r], W.sRow[k][r]); T(P.sRow[k][r], W.sRow[k][r] ? W.sTxt[k][r] : ""); }
            if (P.sVal[k][r] >= 0) { const bool on = W.sRow[k][r] && W.sHasVal[k][r]; S(P.sVal[k][r], on ? 1 : 0); T(P.sVal[k][r], on ? W.sVal[k][r] : ""); }
        }
    }
    for (int k = 0; k < SCN_BGL_POOL && k < kMaxPool; k++) S(P.gS[k], W.gS[k]);
    for (int k = 0; k < SCN_FRM_POOL && k < kMaxPool; k++) S(P.fS[k], W.fS[k]);
    xxl_scene::end();
}

/* taps: list rows (lhKKmMM).  Drains the scene's touch queue; pattern grid taps are left for the pattern code (popPatTap). */
static std::vector<std::pair<int, int>> g_patTaps;
static void drain()
{
    xxl_scene::Touch t;
    while (xxl_scene::popTouch(t))
    {
        bool done = false;
        for (int k = 0; k < SCN_LST_POOL && k < kMaxPool && !done; k++)
            for (int m = 0; m < SCN_NMODULES && !done; m++)
                if (P.hit[k][m] == t.idx)
                {
                    for (int i = 0; i < kNCtl; i++)
                        if (OWN_CTL[i].cls == 'S' && OWN_CTL[i].pool == k && OWN_CTL[i].mi == m) { g_taps.push_back({ i, t.value - 1 }); break; }
                    done = true;
                }
        if (!done) g_patTaps.push_back({ t.idx, t.value });
        xxl_scene::begin(); xxl_scene::setState(t.idx, 0); xxl_scene::end();
    }
}
bool popListTap(ListTap& t)
{
    drain();
    if (g_taps.empty()) return false;
    t = g_taps.front(); g_taps.erase(g_taps.begin());
    return true;
}
bool popOtherTap(int& idx, int& value)
{
    drain();
    if (g_patTaps.empty()) return false;
    idx = g_patTaps.front().first; value = g_patTaps.front().second; g_patTaps.erase(g_patTaps.begin());
    return true;
}
}
