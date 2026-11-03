/* pooled controls: every cLabel / cTextBox / cTextPopup / cSimpleText ('L'), cBar ('B'), cList ('S'), cBgLabel ('G'), cFrame ('F') the firmware
 * has on screen -> the pool slots generated in scene_layout.h (SCN_CTL).  A pool slot is shared by the controls of different screens; the
 * state value contains the screen (module) index, so IndexedEnabling shows exactly the widget of the screen on display. */
#include "bridge_internal.h"

namespace xxl_bridge
{
namespace
{
constexpr int kMaxPool = 64, kRows = 14;
struct Pools
{
    int lA[kMaxPool], lB[kMaxPool], lC[kMaxPool];
    int bS[kMaxPool], bV[kMaxPool];
    int sS[kMaxPool], sRow[kMaxPool][kRows], sVal[kMaxPool][kRows];
    int gS[kMaxPool], fS[kMaxPool];
    int hit[kMaxPool][SCN_NMODULES];
} P;
/* desired values of this frame (default 0 = hidden) */
struct Want
{
    int lA[kMaxPool]; char tA[kMaxPool][64];
    int lB[kMaxPool]; char tB[kMaxPool][64];
    int lC[kMaxPool];
    int bS[kMaxPool], bV[kMaxPool];
    int sS[kMaxPool]; int sRow[kMaxPool][kRows]; char sTxt[kMaxPool][kRows][64]; char sVal[kMaxPool][kRows][64]; bool sHasVal[kMaxPool][kRows];
    int gS[kMaxPool], fS[kMaxPool];
} W;
/* last values written (state only; texts are compared inside xxl_scene) */
void copyText(char* d, const char* s)
{
    if (!s) { d[0] = 0; return; }
    strncpy(d, s, 63); d[63] = 0;
}
int variantA(const ScnCtl& r, const uint32_t* col, bool bg)
{
    for (int v = 0; v < r.nva; v++)
        if (r.va[v][0] == (col[0] & 0xFFFFFF) && (!bg || r.va[v][1] == 0 || r.va[v][1] == (col[1] & 0xFFFFFF))) return v;
    return 0;
}
}

void ctlInit()
{
    memset(&P, -1, sizeof P);
    for (int k = 0; k < SCN_LBL_POOL && k < kMaxPool; k++)
    {
        P.lA[k] = key1("lb%02da", k); P.lB[k] = key1("lb%02db", k); P.lC[k] = key1("lb%02dc", k);
    }
    for (int k = 0; k < SCN_BAR_POOL && k < kMaxPool; k++) { P.bS[k] = key1("br%02ds", k); P.bV[k] = key1("br%02dv", k); }
    for (int k = 0; k < SCN_LST_POOL && k < kMaxPool; k++)
    {
        P.sS[k] = key1("ls%02ds", k);
        for (int r = 0; r < kRows; r++) { P.sRow[k][r] = key2("ls%02dr%02d", k, r); P.sVal[k][r] = key2("ls%02dr%02dv", k, r); }
        for (int m = 0; m < SCN_NMODULES; m++) P.hit[k][m] = key2("lh%02dm%02d", k, m);
    }
    for (int k = 0; k < SCN_BGL_POOL && k < kMaxPool; k++) P.gS[k] = key1("bg%02d", k);
    for (int k = 0; k < SCN_FRM_POOL && k < kMaxPool; k++) P.fS[k] = key1("fr%02d", k);
}

static int handleOf(int i)
{
    switch (i)
    {
#include "bridge_handles.inc"
        default: return -1;
    }
}

/* rows whose handle is not a plain member are found by control id + creation order inside their screen */
static cDisplayControl* byOrder(const ScnCtl& r, int rowIndex)
{
    int nth = 0;
    for (int i = 0; i < rowIndex; i++)
        if (SCN_CTL[i].mi == r.mi && SCN_CTL[i].id == r.id && handleOf(i) < 0) nth++;
    for (int h = 0, seen = 0; h < controlsCount; h++)
    {
        cDisplayControl* c = display.controlsTable[h];
        if (c && c->id == r.id && c->posX == r.x && c->posY == r.y) { if (seen++ == nth) return c; }
    }
    return nullptr;
}

void ctlFrame(int mod)
{
    memset(&W, 0, sizeof W);
    for (int i = 0; i < SCN_NCTL; i++)
    {
        const ScnCtl& r = SCN_CTL[i];
        if (r.mi != 0 && r.mi != mod) continue;
        const int h = handleOf(i);
        cDisplayControl* c = h >= 0 ? control(h) : byOrder(r, i);
        if (!visible(c) || c->id != r.id) continue;
        const uint32_t style = c->style;
        const uint32_t* col = c->colors;
        switch (r.cls)
        {
            case 'L':
            {
                const int k = r.pool;
                const bool bg = style & controlStyleBackground;
                const bool cy = style & controlStyleCenterY;
                int vflag = 0;
                if (cy && c->value > 0 && c->value < 2 && c->font && c->height > c->font->height) vflag = 1;
                if (c->id != controlIdLabel && c->id != controlIdSimpleText) vflag = 0;
                const int vi = col ? variantA(r, col, bg) : 0;
                W.lA[k] = 1 + r.mi * 8 + vi * 2 + vflag;
                copyText(W.tA[k], c->text);
                if (c->id == controlIdLabel)
                {
                    cLabel* lab = (cLabel*)c;
                    if (c->value > 0 && lab->text2 && lab->text2[0])
                    {
                        int vb = 0;
                        if (col) for (int v = 0; v < r.nvb; v++) if (r.vb[v] == (col[3] & 0xFFFFFF)) { vb = v; break; }
                        W.lB[k] = 1 + r.mi * 4 + vb;
                        copyText(W.tB[k], lab->text2);
                    }
                    if ((style & controlStyleSpecialFx) && lab->data)
                    {
                        const uint32_t lc = lab->data->line.color & 0xFFFFFF;
                        const int opt = lc == (SCN_COL_RED & 0xFFFFFF) ? 1 : lc == (SCN_COL_GREEN & 0xFFFFFF) ? 2 : 3;
                        W.lC[k] = 1 + r.mi * 16 + opt;
                    }
                    else if ((style & controlStyleSpecialFx2) && lab->data && lab->data->dotIndicator.count >= 2 && lab->data->dotIndicator.count <= 3)
                        W.lC[k] = 1 + r.mi * 16 + 4 + (lab->data->dotIndicator.count - 2) * 3 + std::min<int>(lab->data->dotIndicator.current, lab->data->dotIndicator.count - 1);
                }
                break;
            }
            case 'B':
            {
                const int k = r.pool;
                int vi = 0;
                if (col) for (int v = 0; v < r.nvars; v++) if (r.bar[v][0] == (col[0] & 0xFFFFFF) && r.bar[v][3] == (col[3] & 0xFFFFFF)) { vi = v; break; }
                const int v = c->value;
                if ((style & controlStyleValue_0_100) && v < 0) { W.bS[k] = 1 + r.mi * 6 + vi * 2 + 1; break; }
                float frac;
                if (style & controlStyleValueLeftRight_100_100) frac = (v + 100) / 200.0f;
                else if (style & controlStyleValueLeftRight_24_24) frac = (v + 24) / 48.0f;
                else frac = v / 100.0f;
                int f = (int)(frac * (SCN_BAR_FRAMES - 1) + 0.5f);
                W.bV[k] = std::max(0, std::min(SCN_BAR_FRAMES - 1, f));
                W.bS[k] = 1 + r.mi * 6 + vi * 2;
                break;
            }
            case 'S':
            {
                const int k = r.pool;
                int length = 0, lines = 0, start = 0, pos = 0, disBar = 0;
                char** names = nullptr; char** vals = nullptr; const uint8_t* selTab = nullptr;
                if (c->id == controlIdParamValueList)
                {
                    cParamValueList* l = (cParamValueList*)c;
                    if (!l->list) break;
                    length = l->list->length; lines = l->list->linesCount; start = l->list->start; pos = l->textListPos; disBar = l->disableBar;
                    names = l->list->params; vals = l->list->values;
                }
                else if (c->id == controlIdImgList)
                {
                    NewList* l = (NewList*)c;
                    if (!l->data) break;
                    length = l->data->length; lines = l->data->linesCount; start = l->data->start; pos = l->listPos; disBar = l->disableBar;
                    names = l->data->params; vals = l->data->values; selTab = l->data->selectTab;
                }
                else
                {
                    cList* l = (cList*)c;
                    const strList* sl = l->list;
                    if (!sl || !sl->data) break;
                    length = sl->length; lines = sl->linesCount; start = sl->start; pos = l->textListPos; disBar = l->disableBar;
                    names = sl->data; selTab = sl->selectTab;
                }
                if (!names) break;
                int vi = 0;
                if (col) for (int v = 0; v < r.nvars; v++) if (r.vars[v][0] == (col[3] & 0xFFFFFF) && r.vars[v][1] == (col[4] & 0xFFFFFF)) { vi = v; break; }
                W.sS[k] = 1 + r.mi * 4 + vi;
                const int nl = std::min<int>(std::min<int>(length, lines), std::min<int>(r.rows, kRows));
                for (int row = 0; row < nl; row++)
                {
                    const int idx = row + pos;
                    if (idx >= length) break;
                    int kind = 0;
                    if (selTab && selTab[idx]) kind = SCN_DETAIL_LOW ? 1 : 2;
                    else if (!disBar && idx == start) kind = 1;
                    W.sRow[k][row] = 1 + r.mi * 6 + vi * 3 + kind;
                    copyText(W.sTxt[k][row], names[idx]);
                    if (vals) { copyText(W.sVal[k][row], vals[idx]); W.sHasVal[k][row] = true; }
                }
                break;
            }
            case 'G':
                W.gS[r.pool] = 1 + r.mi;
                break;
            case 'F':
            {
                const strFrameData* fd = ((cFrame*)c)->data;
                if (!fd || fd->placesCount == 0 || r.nplaces == 0) break;
                int v = std::max(0, std::min<int>(c->value, std::min<int>(fd->placesCount, r.nplaces) - 1));
                W.fS[r.pool] = 1 + r.mi * 12 + 1 + v;
                break;
            }
        }
    }
    /* write the changes */
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
}

/* a tapped list row (lhKKmMM = list pool K of screen M, value = row + 1): move the list's selection there with Up / Down taps */
void ctlTouch(int idx, int value)
{
    for (int k = 0; k < SCN_LST_POOL && k < kMaxPool; k++)
        for (int m = 0; m < SCN_NMODULES; m++)
            if (P.hit[k][m] == idx)
            {
                const int row = value - 1;
                /* find the live list of that pool slot on the screen */
                for (int i = 0; i < SCN_NCTL; i++)
                {
                    const ScnCtl& r = SCN_CTL[i];
                    if (r.cls != 'S' || r.pool != k || r.mi != m) continue;
                    const int h = handleOf(i);
                    cDisplayControl* c = h >= 0 ? control(h) : byOrder(r, i);
                    if (!visible(c)) continue;
                    int pos = 0, cur = 0, len = 0;
                    if (c->id == controlIdParamValueList) { cParamValueList* l = (cParamValueList*)c; if (!l->list) continue; pos = l->textListPos; cur = l->list->start; len = l->list->length; }
                    else if (c->id == controlIdImgList) { NewList* l = (NewList*)c; if (!l->data) continue; pos = l->listPos; cur = l->data->start; len = l->data->length; }
                    else { cList* l = (cList*)c; if (!l->list) continue; pos = l->textListPos; cur = l->list->start; len = l->list->length; }
                    const int target = pos + row;
                    if (target >= len) continue;
                    if (target > cur) queueTap(interfaceButtonDown, target - cur);
                    else if (target < cur) queueTap(interfaceButtonUp, cur - target);
                }
                xxl_scene::begin(); S(idx, 0); xxl_scene::end();
                return;
            }
}
}
