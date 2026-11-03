/* pattern editor: the cTracker (vertical pattern) -> pattern slots.  Mirrors core/libs/NewDisplay/tracker.cpp (and gen/scene_ref.py, the
 * reference the generated skin was previewed with):  15 rows, the cursor parked on row index 7, rows scroll around it every step,
 * columnsCount follows popupMode, rowCount is 8 while the Fill popup is up, row numbers wrap with the track-end line, the playhead is a
 * row band, the selection is a box + white text, inactive tracks are grey, empty "---" fields are dim.
 * NOT mapped: the horizontal pattern arrangement (cHorizontalTracker), patternInfo != 0 (grid at y 0, 28 px rows), partial-width playhead,
 * the selection outline frame. */
#include "bridge_internal.h"

namespace xxl_bridge
{
namespace
{
struct PatIdx
{
    int vis, line[16], cell[15][32], num[15], band[15], cur, curPos, curTxt, nameA[4], nameB[8], name[8], hit[15];
} I;
const int kNK = SCN_PAT_NK;
void copyN(char* d, const char* s, int n) { int i = 0; if (s) for (; s[i] && i < n; i++) d[i] = s[i]; d[i] = 0; }
}

void patInit()
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

static void hideAll()
{
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
}

/* the live vertical tracker control, if the pattern editor shows it */
static cTracker* liveTracker()
{
    for (int h = 0; h < controlsCount; h++)
    {
        cDisplayControl* c = display.controlsTable[h];
        if (c && c->id == controlIdTracker && visible(c)) return (cTracker*)c;
    }
    return nullptr;
}

void patFrame(int mod)
{
    cTracker* ct = mod == 1 ? liveTracker() : nullptr;
    if (!ct || !ct->tracks) { hideAll(); return; }
    const strTrackerPattern* t = ct->tracks;
    const int mode = ct->value;
    int mi = -1;
    for (int m = 0; m < SCN_PAT_MODES; m++) if (SCN_PAT_MODE_LIST[m] == mode) mi = m;
    if (mi < 0) { hideAll(); return; }                 /* a view mode that was not generated (core build has mode 0 only) */

    const int pm = t->popupMode;
    const int tw = SCN_PAT_TW[mi];
    int cc = SCN_PAT_COLS[mi] == 8 ? 8 : 4;
    if (pm & 2) cc = cc == 8 ? 6 : 3;
    if (pm & 4) cc = cc == 8 ? 4 : 2;
    const int npar = SCN_PAT_PARAMS[mi];
    const int rc = (pm & 1) ? 8 : 15;
    const int first = t->firstVisibleTrack, act = t->actualStep, L = t->patternLength > 0 ? t->patternLength : 1;
    const bool hex = ct->style & controlStyleHexNumbers, from1 = ct->style & controlStyleStartNumbersFrom1;

    S(I.vis, 1);
    int rown[15] = {};
    bool endlines = false;
    for (int i = 0; i < rc; i++)
    {
        if (t->track[0].row[i].note[0] == 0) continue;
        int row = act - 6 + i;
        if (row < 1) { endlines = true; row = L + (row % L); }
        else if (row > L) { endlines = true; row = (row - 1) % L + 1; }
        rown[i] = row;
    }
    for (int k = 0; k < SCN_PAT_NLINES && k < 16; k++)
    {
        bool on = false;
        for (int i = 0; i < cc; i++) if (27 + tw * i == SCN_PAT_LINE_X[k]) on = true;
        S(I.line[k], on ? 1 : 0);
    }
    const int sel2 = t->selectState == 2;
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    if (sel2)
    {
        x0 = std::min<int>(t->selectStartTrack, t->selectEndTrack); x1 = std::max<int>(t->selectStartTrack, t->selectEndTrack);
        y0 = std::min<int>(t->selectStartStep, t->selectEndStep); y1 = std::max<int>(t->selectStartStep, t->selectEndStep);
    }
    const int sp = t->selectedParam, mtrack = t->actualTrack - first;
    const bool markOn = t->selectState != 0;
    const char* cursorText = nullptr;
    const uint8_t typeMask = SCN_PAT_TYPEMASK[mi];
    for (int r = 0; r < 15; r++)
    {
        const bool ph = t->showPlayhead && t->playheadPosition > act - 8 && t->playheadPosition < act + 8 && (t->playheadPosition - act + 7) == r;
        const int kind = ph ? 2 : ((L > 1 && r < rc && rown[r] && ((rown[r] - 1) % (t->stepDevider ? t->stepDevider : 1)) == 0) ? 1 : 0);
        const int line = (endlines && r >= 1 && r < rc && rown[r] == 1) ? 1 : 0;
        S(I.band[r], kind * 2 + line);
        if (rown[r])
        {
            const int n = from1 ? rown[r] : rown[r] - 1;
            char buf[8];
            if (hex) snprintf(buf, sizeof buf, "%02X", n & 255); else snprintf(buf, sizeof buf, "%d", n);
            S(I.num[r], (pm & 6) ? 2 : 1); T(I.num[r], buf);
        }
        else { S(I.num[r], 0); T(I.num[r], ""); }
        for (int c = 0; c < 8; c++)
            for (int T_ = 0; T_ < 4; T_++)
            {
                const int slot = c * 4 + T_;
                if (I.cell[r][slot] < 0) continue;
                const int sid = SCN_PAT_VAR[mi][c][T_];
                if (r >= rc || c >= cc || !((typeMask >> T_) & 1) || sid < 0) { S(I.cell[r][slot], 0); T(I.cell[r][slot], ""); continue; }
                const strTrackerPattern::strTracks::strRow& f = t->track[first + c].row[r];
                const char* text = T_ == 0 ? f.note : T_ == 1 ? f.instr : T_ == 2 ? f.fx[0] : f.fx[1];
                bool sel = false;
                if (sel2 && sp == T_ && first + c >= x0 && first + c <= x1 && act - 7 + r >= y0 && act - 7 + r <= y1 && (npar != 1 || ((1 << sp) & mode)))
                    sel = true;
                int k;
                if (sel) k = 3 + std::max(0, std::min<int>(2, t->selectColor));
                else if (r == 7 && markOn && sp == T_ && c == mtrack) { S(I.cell[r][slot], 0); T(I.cell[r][slot], ""); cursorText = text; continue; }
                else if (t->inactive[first + c]) k = 2;
                else if (text[0] == '-') k = 1;
                else k = 0;
                S(I.cell[r][slot], 1 + kNK * sid + k);
                char buf[8]; copyN(buf, text, 5); T(I.cell[r][slot], buf);
            }
    }
    /* cursor box + the text drawn white on it */
    if (t->selectState && mtrack >= 0 && mtrack <= cc && mtrack < 8 && sp >= 0 && sp < 4 && SCN_PAT_POS[mi][mtrack][sp] >= 0)
    {
        const int pos = SCN_PAT_POS[mi][mtrack][sp];
        S(I.cur, 1 + pos * 6 + (t->selectState == 1 ? 3 : 0) + std::max(0, std::min<int>(2, t->selectColor)));
        S(I.curPos, 1 + pos);
        char buf[8]; copyN(buf, cursorText, 5);
        S(I.curTxt, cursorText ? 1 : 0); T(I.curTxt, buf);
    }
    else { S(I.cur, 0); S(I.curPos, 0); S(I.curTxt, 0); T(I.curTxt, ""); }
    /* track names (showTrackNames): a box as long as the text + the text */
    const bool classA = SCN_PAT_COLS[mi] == 4;
    const int maxLen = classA ? SCN_PAT_NAME_A : SCN_PAT_NAME_B;
    for (int i = 0; i < 8; i++)
    {
        if (t->showTrackNames && i < cc)
        {
            const char* nm = (first + i < strTrackerPattern::kTrackCount && t->tracksNames[first + i]) ? t->tracksNames[first + i] : "";
            char buf[24]; copyN(buf, nm, std::min(maxLen, 20));
            const int n = (int)strlen(buf);
            S(I.name[i], classA ? 1 : 2); T(I.name[i], buf);
            const int frame = (t->inactive[first + i] ? maxLen : 0) + n;
            if (classA) { if (i < 4) { S(I.nameA[i], frame); } }
            else S(I.nameB[i], frame);
            if (classA) S(I.nameB[i], 0); else if (i < 4) S(I.nameA[i], 0);
        }
        else
        {
            S(I.name[i], 0); T(I.name[i], "");
            S(I.nameB[i], 0); if (i < 4) S(I.nameA[i], 0);
        }
    }
}

/* a tap on the grid: phRR = row RR, value = zone 1..20 across the grid.  The cursor is moved with the firmware's own cursor keys:
 * Up/Down for the row difference (the cursor stays on row index 7 and the grid scrolls), Left/Right until the field under the finger. */
void patTouch(int idx, int value)
{
    int row = -1;
    for (int r = 0; r < 15; r++) if (I.hit[r] == idx) row = r;
    if (row < 0) return;
    xxl_scene::begin(); S(idx, 0); xxl_scene::end();
    cTracker* ct = liveTracker();
    if (!ct || !ct->tracks) return;
    const strTrackerPattern* t = ct->tracks;
    int mi = 0;
    for (int m = 0; m < SCN_PAT_MODES; m++) if (SCN_PAT_MODE_LIST[m] == ct->value) mi = m;
    const int tw = SCN_PAT_TW[mi], cols = SCN_PAT_COLS[mi] == 8 ? 8 : 4;
    const float xs = (value - 0.5f) / 20.0f * 798.0f - 26.0f;               /* source px inside the track area */
    int col = (int)(xs / tw);
    col = std::max(0, std::min(cols - 1, col));
    const int inCol = (int)(xs - col * tw);
    int param = -1, best = 1 << 30;
    for (int T_ = 0; T_ < 4; T_++)
    {
        if (!((SCN_PAT_TYPEMASK[mi] >> T_) & 1)) continue;
        const int pos = SCN_PAT_POS[mi][col][T_];
        if (pos < 0) continue;
        /* field start inside the track cell = the box offset; choose the last field starting at or left of the finger */
        static const int off4[4] = { 1, 44, 78, 132 };
        const int o = SCN_PAT_COLS[mi] == 4 || SCN_PAT_PARAMS[mi] == 4 ? off4[T_] : (SCN_PAT_PARAMS[mi] == 1 ? 24 : T_ * 40);
        const int d = inCol - o;
        if (d >= 0 && d < best) { best = d; param = T_; }
    }
    if (param < 0) param = 0;
    const int dRow = row - SCN_PAT_CURSOR_ROW;
    if (dRow > 0) queueTap(interfaceButtonDown, dRow);
    else if (dRow < 0) queueTap(interfaceButtonUp, -dRow);
    const int curCol = t->actualTrack - t->firstVisibleTrack;
    const int per = SCN_PAT_PARAMS[mi];
    /* firmware: left/right walks the displayed params of a track, then the next track */
    auto lin = [&](int c, int p) { int idxp = 0; for (int q = 0; q < p; q++) if ((SCN_PAT_TYPEMASK[mi] >> q) & 1) idxp++; return c * per + idxp; };
    const int dx = lin(col, param) - lin(curCol, t->selectedParam);
    if (dx > 0) queueTap(interfaceButtonRight, dx); else if (dx < 0) queueTap(interfaceButtonLeft, -dx);
}
}
