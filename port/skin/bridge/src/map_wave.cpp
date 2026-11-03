/* sample waveform + markers (picture columns): every cSpectrum / cPoints / cSlicePoints / cProgressCursor / cLineIndicator on screen.
 *   waveform  : WCOLS columns, each an upper and a lower level 0..WLV  (spectrum.cpp: bar from centre-upper*k to centre-lower*k, k = h / 109)
 *   markers   : MCOLS columns, each one frame = what is drawn in it (points.cpp / slice.cpp / progressCursor.cpp / lineIndicator.cpp)
 * Simplifications (see PORTING_NOTES.md): the zoomed "line" waveform (spectrumType 1) is drawn as bars; marker position is quantised to
 * 4 source px; waveform to 6 source px; the start-end shading and markers use the Tracker colours. */
#include "bridge_internal.h"

namespace xxl_bridge
{
namespace
{
struct WIdx { int g0, g1, mg, up[2][SCN_WCOLS], dn[2][SCN_WCOLS], mk[SCN_MCOLS]; } I;
int g_wu[2][SCN_WCOLS], g_wd[2][SCN_WCOLS], g_mk[SCN_MCOLS];
inline int mkFrame(int kind, bool shade) { return 2 + kind * 2 + (shade ? 1 : 0); }
}

void waveInit()
{
    I.g0 = xxl_scene::index("wg0"); I.g1 = xxl_scene::index("wg1"); I.mg = xxl_scene::index("wmg");
    for (int s = 0; s < 2; s++)
        for (int c = 0; c < SCN_WCOLS; c++)
        {
            char k[24];
            snprintf(k, sizeof k, "wu%d_%03d", s, c); I.up[s][c] = xxl_scene::index(k);
            snprintf(k, sizeof k, "wd%d_%03d", s, c); I.dn[s][c] = xxl_scene::index(k);
        }
    for (int c = 0; c < SCN_MCOLS; c++) { char k[16]; snprintf(k, sizeof k, "wk%03d", c); I.mk[c] = xxl_scene::index(k); }
}

static int geomOf(const short (*tab)[3], int n, int x0, int y, int h)
{
    for (int g = 0; g < n; g++) if (tab[g][0] == x0 && tab[g][1] == y && tab[g][2] == h) return g + 1;
    return 0;
}

void waveFrame(int mod)
{
    int geom[2] = { 0, 0 }, mg = 0;
    memset(g_wu, 0, sizeof g_wu); memset(g_wd, 0, sizeof g_wd); memset(g_mk, 0, sizeof g_mk);
    const bool sampleScreen = mod >= 2 && mod <= 5;     /* sampleEditor, sampleImporter, samplePlayback, sampleRecorder */
    if (sampleScreen)
    {
        int slot = 0;
        for (int h = 0; h < controlsCount; h++)
        {
            cDisplayControl* c = display.controlsTable[h];
            if (!c || !visible(c) || c->id != controlIdSpectrum) continue;
            cSpectrum* sp = (cSpectrum*)c;
            if (!sp->spectrum || sp->spectrum->spectrumType == 3) continue;       /* 3 = loading spinner */
            /* the control that sits at y 225 is the second channel */
            const int s = (c->posY >= 225) ? 1 : 0;
            const int g = s == 0 ? geomOf(SCN_WGEOM0, 4, c->posX, c->posY, c->height) : geomOf(SCN_WGEOM1, 2, c->posX, c->posY, c->height);
            if (!g) continue;
            geom[s] = g; slot = s;
            const int w = std::max<int>(1, std::min<int>(600, (int)sp->spectrum->width ? (int)sp->spectrum->width : (int)c->width));
            for (int col = 0; col < SCN_WCOLS; col++)
            {
                int hi = 0, lo = 0;
                const int a = col * SCN_SRC_W / SCN_WCOLS, b = (col + 1) * SCN_SRC_W / SCN_WCOLS;
                for (int i = a; i < b && i < w && i < 600; i++)
                {
                    hi = std::max<int>(hi, sp->spectrum->upperData[i]);
                    lo = std::min<int>(lo, sp->spectrum->lowerData[i]);
                    if (sp->spectrum->spectrumType == 1) { lo = std::min<int>(lo, sp->spectrum->upperData[i]); hi = std::max<int>(hi, 0); }
                }
                /* data units are source px at k = 1 for h 109: half height = 54.6 units */
                g_wu[s][col] = std::max(0, std::min(SCN_WLV, (int)(hi * SCN_WLV / 54.6f + 0.5f)));
                g_wd[s][col] = std::max(0, std::min(SCN_WLV, (int)(-lo * SCN_WLV / 54.6f + 0.5f)));
            }
        }
        (void)slot;
        /* markers */
        int start = -1, end = -1, l1 = -1, l2 = -1, sel = 0, zc = 0, ptype = 0, x0 = -1, play = -1, gran = -1, selSlice = -1;
        int slices[64], nslice = 0;
        for (int h = 0; h < controlsCount; h++)
        {
            cDisplayControl* c = display.controlsTable[h];
            if (!c || !visible(c)) continue;
            if (c->id == controlIdPoints && ((cPoints*)c)->points)
            {
                const strTrackerPoints* p = ((cPoints*)c)->points;
                start = p->startPoint; end = p->endPoint; l1 = p->loopPoint1; l2 = p->loopPoint2; sel = p->selected; zc = p->zerocrossed; ptype = p->pointsType;
                x0 = c->posX;
            }
            else if (c->id == controlIdSlicePoints && ((cSlicePoints*)c)->points)
            {
                const strSlicePoints* p = ((cSlicePoints*)c)->points;
                nslice = std::min<int>(p->sliceCount, 64);
                for (int i = 0; i < nslice; i++) slices[i] = p->slice[i];
                selSlice = p->selected;
                if (x0 < 0) x0 = c->posX;
            }
            else if (c->id == controlIdProgressCursor && c->value > 0 && c->value < c->width) { play = c->value; if (x0 < 0) x0 = c->posX; }
            else if (c->id == controlIdLineIndicator && c->value >= 0 && c->value <= c->width) { gran = c->value; if (x0 < 0) x0 = c->posX; }
        }
        if (x0 >= 0) mg = x0 >= 100 ? 2 : 1;
        if (mg)
        {
            auto colOf = [](int px) { return std::max(0, std::min(SCN_MCOLS - 1, px * SCN_MCOLS / SCN_SRC_W)); };
            auto put = [&](int px, int kind) { if (px >= 0 && px < SCN_SRC_W) { const int c = colOf(px); if (!g_mk[c] || (g_mk[c] & 1) == 0) g_mk[c] = kind; } };
            /* shade: start..end (type 0) or loop1..loop2 (type > 0 with a loop point) */
            int sa = -1, sb = -1;
            if (ptype == 0) { sa = start >= 0 ? start : 0; sb = end >= 0 ? end : SCN_SRC_W; }
            else if (l1 >= 0 || l2 >= 0) { sa = l1 >= 0 ? l1 : 0; sb = l2 >= 0 ? l2 : SCN_SRC_W; }
            for (int c = 0; c < SCN_MCOLS; c++)
                if (sa >= 0 && c * 4 + 3 >= sa && c * 4 <= sb) g_mk[c] = 1;      /* shade only */
            auto mk = [&](int px, int kindBase, int bit, int zbit)
            {
                if (px < 0 || px >= SCN_SRC_W) return;
                const int c = colOf(px);
                const bool sh = g_mk[c] == 1 || (g_mk[c] >= 2 && (g_mk[c] - 2) % 2 == 1);
                const int k = (sel & bit) ? ((zc & zbit) ? kindBase + 2 : kindBase + 1) : kindBase;
                g_mk[c] = mkFrame(k, sh);
            };
            if (ptype > 0) { mk(l1, SCN_MK_LOOP1_W, selectLoop1, loop1Zerocrossed); mk(l2, SCN_MK_LOOP2_W, selectLoop2, loop2Zerocrossed); }
            mk(start, SCN_MK_START_W, selectStart, startZerocrossed);
            mk(end, SCN_MK_END_W, selectEnd, endZerocrossed);
            for (int i = 0; i < nslice; i++)
                if (slices[i] >= 0 && slices[i] < SCN_SRC_W)
                {
                    const int c = colOf(slices[i]);
                    g_mk[c] = mkFrame(i == selSlice ? SCN_MK_SLICE_R : SCN_MK_SLICE_W, g_mk[c] == 1);
                }
            auto line = [&](int px, int kind)
            {
                if (px < 0) return; const int c = colOf(px);
                if (g_mk[c] >= 2 && g_mk[c] < 2 + 2 * SCN_MK_PLAY) return;      /* a point has priority over the cursor */
                g_mk[c] = mkFrame(kind, g_mk[c] == 1 || (g_mk[c] >= 2 && (g_mk[c] - 2) % 2 == 1));
            };
            line(gran, SCN_MK_GRANULAR);
            line(play, SCN_MK_PLAY);
        }
    }
    S(I.g0, geom[0]); S(I.g1, geom[1]); S(I.mg, mg);
    for (int s = 0; s < 2; s++)
        for (int c = 0; c < SCN_WCOLS; c++) { S(I.up[s][c], geom[s] ? g_wu[s][c] : 0); S(I.dn[s][c], geom[s] ? g_wd[s][c] : 0); }
    for (int c = 0; c < SCN_MCOLS; c++) S(I.mk[c], mg ? g_mk[c] : 0);
}
}
