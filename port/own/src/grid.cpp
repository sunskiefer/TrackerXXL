#include "grid.h"
#include "ui.h"
#include "xxl_scene.h"
#include "scene_layout.h"
#include <algorithm>

namespace grid
{
namespace
{
struct Idx
{
    int vis, line[16], cell[15][32], num[15], band[15], cur, curPos, curTxt, nameA[4], nameB[8], name[8], hit[15];
} I;
constexpr int kNK = SCN_PAT_NK;
int key1(const char* fmt, int a) { char k[48]; snprintf(k, sizeof k, fmt, a); return xxl_scene::index(k); }
int key2(const char* fmt, int a, int b) { char k[48]; snprintf(k, sizeof k, fmt, a, b); return xxl_scene::index(k); }
inline bool S(int idx, int v) { return xxl_scene::setState(idx, v); }
inline void T(int idx, const char* t) { xxl_scene::setText(idx, t); }
}

void init()
{
    memset(&I, -1, sizeof I);
    I.vis = xxl_scene::index("pvis");
    for (int k = 0; k < SCN_PAT_NLINES && k < 16; k++) I.line[k] = key1("pl%02d", k);
    for (int r = 0; r < 15; r++)
    {
        for (int s = 0; s < 32; s++) I.cell[r][s] = key2("pc%02d%02d", r, s);
        I.num[r] = key1("pn%02d", r); I.band[r] = key1("pb%02d", r); I.hit[r] = key1("ph%02d", r);
    }
    I.cur = xxl_scene::index("pcur"); I.curPos = xxl_scene::index("pcp"); I.curTxt = xxl_scene::index("pct");
    for (int i = 0; i < 4; i++) I.nameA[i] = key1("ptA%d", i);
    for (int i = 0; i < 8; i++) { I.nameB[i] = key1("ptB%d", i); I.name[i] = key1("pt%d", i); }
}

void hide()
{
    xxl_scene::begin();
    S(I.vis, 0);
    for (int k = 0; k < 16; k++) S(I.line[k], 0);
    for (int r = 0; r < 15; r++)
    {
        for (int s = 0; s < 32; s++) if (I.cell[r][s] >= 0) { S(I.cell[r][s], 0); T(I.cell[r][s], ""); }
        S(I.num[r], 0); T(I.num[r], ""); S(I.band[r], 0);
    }
    S(I.cur, 0); S(I.curPos, 0); S(I.curTxt, 0); T(I.curTxt, "");
    for (int i = 0; i < 4; i++) S(I.nameA[i], 0);
    for (int i = 0; i < 8; i++) { S(I.nameB[i], 0); S(I.name[i], 0); T(I.name[i], ""); }
    xxl_scene::end();
}

void frame(const View& v)
{
    using namespace own;
    const Pattern& pat = *v.pat;
    const int L = pat.len > 0 ? pat.len : 1;
    const int tw = SCN_PAT_TW[0], cc = 4;
    const int act = v.step;
    xxl_scene::begin();
    S(I.vis, 1);
    int rown[15]; bool wrapped[15]; bool endlines = false;
    for (int r = 0; r < 15; r++)
    {
        const int x = act - 7 + r;
        wrapped[r] = x < 0 || x >= L;
        if (wrapped[r]) endlines = true;
        rown[r] = ((x % L) + L) % L + 1;                    /* 1-based step number */
    }
    for (int k = 0; k < SCN_PAT_NLINES && k < 16; k++)
    {
        bool on = false;
        for (int i = 0; i < cc; i++) if (27 + tw * i == SCN_PAT_LINE_X[k]) on = true;
        S(I.line[k], on ? 1 : 0);
    }
    int prow = -1;
    if (v.playhead >= 0)
    {
        int rel = v.playhead - act;
        if (rel > L / 2) rel -= L; else if (rel < -L / 2) rel += L;
        if (rel >= -7 && rel <= 7) prow = 7 + rel;
    }
    const char* cursorText = nullptr;
    char buf[8]; static char curBuf[8];
    for (int r = 0; r < 15; r++)
    {
        const int kind = (r == prow) ? 2 : ((L > 1 && ((rown[r] - 1) % (v.stepDiv ? v.stepDiv : 1)) == 0) ? 1 : 0);
        const int line = (endlines && r >= 1 && rown[r] == 1) ? 1 : 0;
        S(I.band[r], kind * 2 + line);
        snprintf(buf, sizeof buf, "%d", rown[r]);
        S(I.num[r], 1); T(I.num[r], buf);
        for (int c = 0; c < 8; c++)
            for (int T_ = 0; T_ < 4; T_++)
            {
                const int slot = c * 4 + T_;
                if (I.cell[r][slot] < 0) continue;
                const int sid = SCN_PAT_VAR[0][c][T_];
                if (c >= cc || sid < 0) { S(I.cell[r][slot], 0); T(I.cell[r][slot], ""); continue; }
                const int trk = v.first + c;
                const Step& st = pat.s[trk][rown[r] - 1];
                char tb[8]; cellText(st, T_, tb);
                int k;
                if (r == 7 && v.curState && v.param == T_ && trk == v.track) { S(I.cell[r][slot], 0); T(I.cell[r][slot], ""); cursorText = tb[0] ? strcpy(curBuf, tb) : nullptr; continue; }
                else if (v.inactive && v.inactive[trk]) k = 2;
                else if (tb[0] == '-') k = 1;
                else k = 0;
                S(I.cell[r][slot], 1 + kNK * sid + k);
                T(I.cell[r][slot], tb);
            }
    }
    const int mtrack = v.track - v.first;
    if (v.curState && mtrack >= 0 && mtrack < cc && v.param >= 0 && v.param < 4 && SCN_PAT_POS[0][mtrack][v.param] >= 0)
    {
        const int pos = SCN_PAT_POS[0][mtrack][v.param];
        S(I.cur, 1 + pos * 6 + 3 + std::max(0, std::min(2, v.curColor)));
        S(I.curPos, 1 + pos);
        S(I.curTxt, cursorText ? 1 : 0); T(I.curTxt, cursorText ? cursorText : "");
    }
    else { S(I.cur, 0); S(I.curPos, 0); S(I.curTxt, 0); T(I.curTxt, ""); }
    /* track names: "Track n", class A (4 columns, up to 18 characters) */
    for (int i = 0; i < 8; i++)
    {
        if (i < cc)
        {
            char nm[24]; snprintf(nm, sizeof nm, "Track %d", v.first + i + 1);
            const int n = (int)strlen(nm);
            S(I.name[i], 1); T(I.name[i], nm);
            const int frame = ((v.inactive && v.inactive[v.first + i]) ? SCN_PAT_NAME_A : 0) + n;
            S(I.nameA[i], frame);
            S(I.nameB[i], 0);
        }
        else { S(I.name[i], 0); T(I.name[i], ""); S(I.nameB[i], 0); if (i < 4) S(I.nameA[i], 0); }
    }
    xxl_scene::end();
}

bool decodeTap(int idx, int value, const View& v, int& row, int& track, int& param)
{
    (void)v;
    row = -1;
    for (int r = 0; r < 15; r++) if (I.hit[r] == idx) row = r;
    if (row < 0) return false;
    const int tw = SCN_PAT_TW[0], cols = 4;
    const float xs = (value - 0.5f) / 20.0f * 798.0f - 26.0f;
    int col = (int)(xs / tw);
    col = std::max(0, std::min(cols - 1, col));
    const int inCol = (int)(xs - col * tw);
    static const int off4[4] = { 1, 44, 78, 132 };
    int best = 1 << 30; param = 0;
    for (int T_ = 0; T_ < 4; T_++) { const int d = inCol - off4[T_]; if (d >= 0 && d < best) { best = d; param = T_; } }
    track = v.first + col;
    return true;
}
}
