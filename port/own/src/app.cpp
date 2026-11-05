#include "app.h"
#include "audio.h"
#include "grid.h"
#include "model.h"
#include "ui.h"
#include "xxl_scene.h"
#include "scene_layout.h"
#include "trackerxxl_voices.h"
#include <algorithm>
#include <functional>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace own;
namespace app
{
namespace
{
enum { S_PAT = 1, S_SEDIT, S_SLOAD, S_SPLAY, S_SREC, S_INSTR, S_SONG, S_PROJ, S_CONF, S_MASTER, S_PERF, S_IMG, S_COUNT };
const char* const kMod[S_COUNT] = { "global", "patternEditor", "sampleEditor", "sampleImporter", "samplePlayback", "sampleRecorder", "instrumentEditor", "songEditor", "projectEditor", "configEditor", "masterParams", "performanceMode", "imageViewer" };
struct Item { char name[32] = ""; char val[32] = ""; bool off = false; int bar = -1; bool sbar = false; bool blank = false; bool arrows = false; bool act = false; };

/* ------------------------------------------------------------------ state */
int g_scr = S_PAT, g_focus[S_COUNT] = {}, g_page[S_COUNT] = {};
bool g_shift = false, g_edit = false, g_inited = false;
int g_pat = 0, g_step = 0, g_trk = 0, g_par = 0, g_inst = 0, g_stepJump = 1, g_padOct = 0, g_padMode = 0;
bool g_preview = false, g_hostSyncOpt = true;
int g_lastNote = 60, g_lastFx = FX_VOL;
char g_toast[96] = ""; int g_toastLeft = 0;
int g_clock = 0;
bool g_dirty = true;
std::vector<Pattern> g_undo, g_redo;
Step g_clipStep; bool g_clipHas = false;
Pattern g_clipPat; bool g_clipPatHas = false;
Browser g_br;
bool g_brLoaded = false;
bool g_menu = false; int g_menuSel = 0, g_menuTop = 0; void menuOpen(); void menuMove(int d); void menuPick(int i);
int g_sloadRegion = 0, g_sloadSel = 0, g_sloadTop = 0, g_prevVol = 50;
std::vector<Sample> g_sedUndo; int g_sedUndoInstr = -1;
int g_effect = 1, g_fxp[4] = { 50, 50, 50, 50 };
int g_songSel = 0, g_songTop = 0; std::vector<std::vector<uint8_t>> g_songUndo;
int g_confBase = 1, g_confSub = 0, g_confRegion = 0;
/* Performance page: 12 columns, each an effect with 4 rows (row 0 = off, rows 1..3 = stored values); pads / taps pick the row, the track buttons choose the tracks it acts on */
struct PfState
{
    uint8_t fx[12] = { PF_VOL, PF_PAN, PF_TUNE, PF_LP, PF_HP, PF_BP, PF_DEL, PF_REV, PF_POS, PF_END, PF_PLAY, PF_REPEAT };
    int16_t val[12][4] = {}; uint8_t row[12] = {}; bool active[12] = {}; int16_t temp[12] = {};
    bool trk[8] = {}; bool edit = false; int place = 0;
} g_pf;
struct Config { int numbering = 1, padOct = 0, metro = 0, metroVol = 50, padVel = 100, pageStep = 0, hostSync = 1, brightness = 3, theme = 0, noteRec = 1; } g_cfg;
int g_rev[kInstr];
int g_confirm = -1; int g_confirmLeft = 0;
int g_projSel = 0, g_projTop = 0; std::vector<std::string> g_projList; std::vector<std::string> g_recent; bool g_projDirty = true;
int g_recSel = 0;
int g_inlist = 0;

void toast(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(g_toast, sizeof g_toast, fmt, ap); va_end(ap);
    g_toastLeft = kSr * 2; g_dirty = true;
}
bool confirm(int id, const char* msg)   /* second press within 3 s executes */
{
    if (g_confirm == id && g_confirmLeft > 0) { g_confirm = -1; return true; }
    g_confirm = id; g_confirmLeft = kSr * 3; toast("%s - press again to confirm", msg); return false;
}
Project& P() { return proj(); }
Pattern& pat() { return P().pat[g_pat]; }
Step& cur() { return pat().s[g_trk][g_step]; }
Instr& inst() { return P().ins[g_inst]; }
int stepLen() { return std::max(1, pat().len); }
std::string instName(int i)
{
    char b[48]; const Instr& I = P().ins[i];
    if (I.used) snprintf(b, sizeof b, "%d. %s", i + 1, I.kind == 1 ? (I.name[0] ? I.name : tv_voice_name(I.voice)) : I.name); else snprintf(b, sizeof b, "%d.", i + 1);
    return b;
}
void dirty() { g_dirty = true; }
void pushUndo() { g_undo.push_back(pat()); if (g_undo.size() > 24) g_undo.erase(g_undo.begin()); g_redo.clear(); }
void goStep(int s) { const int L = stepLen(); g_step = ((s % L) + L) % L; dirty(); }
void setPattern(int p) { g_pat = clampi(p, 0, kPat - 1); audio::setEditPattern(g_pat); g_step = std::min(g_step, stepLen() - 1); g_undo.clear(); g_redo.clear(); dirty(); }

/* ------------------------------------------------------------------ config file */
void cfgLoad()
{
    FILE* f = fopen((g_root + "/config.ini").c_str(), "r"); if (!f) return;
    char k[64]; int v;
    while (fscanf(f, "%63[^=]=%d\n", k, &v) == 2)
    {
        if (!strcmp(k, "numbering")) g_cfg.numbering = v; else if (!strcmp(k, "padOct")) g_cfg.padOct = v; else if (!strcmp(k, "metro")) g_cfg.metro = v;
        else if (!strcmp(k, "metroVol")) g_cfg.metroVol = v; else if (!strcmp(k, "padVel")) g_cfg.padVel = v; else if (!strcmp(k, "hostSync")) g_cfg.hostSync = v;
        else if (!strcmp(k, "brightness")) g_cfg.brightness = v; else if (!strcmp(k, "theme")) g_cfg.theme = v; else if (!strcmp(k, "noteRec")) g_cfg.noteRec = v; else if (!strcmp(k, "stepJump")) g_stepJump = clampi(v, 0, 16);
        else if (!strncmp(k, "pfFx", 4)) { const int s = atoi(k + 4); if (s >= 0 && s < 12) g_pf.fx[s] = (uint8_t)clampi(v, 0, PF_COUNT - 1); }
        else if (!strncmp(k, "pfV", 3)) { int s = 0, r = 0; if (sscanf(k + 3, "%d_%d", &s, &r) == 2 && s >= 0 && s < 12 && r >= 0 && r < 4) g_pf.val[s][r] = (int16_t)clampi(v, -100, 100); }
    }
    fclose(f);
}
void cfgSave()
{
    FILE* f = fopen((g_root + "/config.ini").c_str(), "w"); if (!f) return;
    fprintf(f, "numbering=%d\npadOct=%d\nmetro=%d\nmetroVol=%d\npadVel=%d\nhostSync=%d\nbrightness=%d\ntheme=%d\nnoteRec=%d\nstepJump=%d\n", g_cfg.numbering, g_cfg.padOct, g_cfg.metro, g_cfg.metroVol, g_cfg.padVel, g_cfg.hostSync, g_cfg.brightness, g_cfg.theme, g_cfg.noteRec, g_stepJump);
    for (int s = 0; s < 12; s++) { fprintf(f, "pfFx%d=%d\n", s, (int)g_pf.fx[s]); for (int r = 1; r < 4; r++) fprintf(f, "pfV%d_%d=%d\n", s, r, (int)g_pf.val[s][r]); }
    fclose(f);
}

/* ------------------------------------------------------------------ frame helpers */
int g_drawMi = 1;   /* the screen being drawn: the title bar, timer and popups exist once per screen (one tab each) */
int R(int mod, const char* fmt, int a = -1)
{
    if (!mod) mod = g_drawMi;
    char b[96]; if (a >= 0) { char f[64]; snprintf(f, sizeof f, fmt, a); snprintf(b, sizeof b, "%s.%s", kMod[mod], f); } else snprintf(b, sizeof b, "%s.%s", kMod[mod], fmt);
    return ui::row(b);
}
void bottom(int mod, const Item* it, int pages, int page, int dotsSlot = 7)
{
    for (int i = 0; i < 8; i++)
    {
        const int r = R(mod, "label[%d]", i); if (r < 0) continue;
        if (it[i].blank) continue;
        if (i == dotsSlot && pages > 1) { ui::label(r, it[i].name[0] ? it[i].name : "More", 0, nullptr, 0, 0); ui::dots(r, pages, page); continue; }
        ui::label(r, it[i].name, it[i].off ? 1 : 0, it[i].val[0] ? it[i].val : nullptr, it[i].off ? 2 : 0, (it[i].val[0] || it[i].act) ? (it[i].off ? ui::LN_RED : ui::LN_GREEN) : 0);
    }
}
void dbOfVol(int v, char* b) { audio::dbText(v, b); }
void titleBar(const char* title, const char* right)
{
    ui::label(R(0, "titleBar.bar"), "", 0);
    char tb[96]; snprintf(tb, sizeof tb, "%s", title);
    ui::label(R(0, "titleBar.titleLabel"), tb, 0);
    ui::label(R(0, "titleBar.rightLabel"), (g_confirmLeft > 0 && g_confirm >= 0) ? "Press again to confirm" : (right ? right : ""), 0);
    const int sec = (int)audio::timeSeconds();
    char t[64];
    if (audio::playing()) snprintf(t, sizeof t, "%dm:%02ds  P%d.%03d", sec / 60, sec % 60, audio::playPattern() + 1, audio::playStep() + 1);
    else snprintf(t, sizeof t, "%dm:%02ds  P%d.%03d", sec / 60, sec % 60, g_pat + 1, g_step + 1);
    ui::label(R(0, "songTimer.timerLabel"), t, 0);
    if (g_toastLeft > 0) ui::label(R(0, "popups.textPopup"), g_toast, 0);
}

/* ------------------------------------------------------------------ waveform (sample screens) */
struct WIdx { int g0, g1, mg, up[2][SCN_WCOLS], dn[2][SCN_WCOLS], mk[SCN_MCOLS]; } WI;
struct WCache { int instr = -1, rev = -1, zoom = -1, off = -1; int up[2][SCN_WCOLS], dn[2][SCN_WCOLS]; } WC;
void waveInit()
{
    WI.g0 = xxl_scene::index("wg0"); WI.g1 = xxl_scene::index("wg1"); WI.mg = xxl_scene::index("wmg");
    for (int s = 0; s < 2; s++) for (int c = 0; c < SCN_WCOLS; c++) { char k[24]; snprintf(k, sizeof k, "wu%d_%03d", s, c); WI.up[s][c] = xxl_scene::index(k); snprintf(k, sizeof k, "wd%d_%03d", s, c); WI.dn[s][c] = xxl_scene::index(k); }
    for (int c = 0; c < SCN_MCOLS; c++) { char k[16]; snprintf(k, sizeof k, "wk%03d", c); WI.mk[c] = xxl_scene::index(k); }
}
void waveHide()
{
    xxl_scene::begin();
    xxl_scene::setState(WI.g0, 0); xxl_scene::setState(WI.g1, 0); xxl_scene::setState(WI.mg, 0);
    for (int s = 0; s < 2; s++) for (int c = 0; c < SCN_WCOLS; c++) { xxl_scene::setState(WI.up[s][c], 0); xxl_scene::setState(WI.dn[s][c], 0); }
    for (int c = 0; c < SCN_MCOLS; c++) xxl_scene::setState(WI.mk[c], 0);
    xxl_scene::end();
}
/* sel: 0 none, 1 start, 2 end, 3 loop1, 4 loop2, 5 slice (selSlice) */
double w_start = 0, w_end = 1, w_l1 = 0, w_l2 = 1; int w_slices = 0;
void waveDraw(int instr, int zoomPct, int sel, int selSlice, bool loops, bool slices, double followFrac)
{
    const Sample& s = smp(instr); const Instr& I = P().ins[instr]; w_slices = I.slices;
    if (s.frames() < 2) { waveHide(); return; }
    const double vis = 100.0 / std::max(100, zoomPct);
    double off = std::max(0.0, std::min(1.0 - vis, followFrac - vis / 2));
    if (vis >= 1.0) off = 0;
    const int offI = (int)(off * 100000);
    if (WC.instr != instr || WC.rev != g_rev[instr] || WC.zoom != zoomPct || WC.off != offI)
    {
        WC.instr = instr; WC.rev = g_rev[instr]; WC.zoom = zoomPct; WC.off = offI;
        const double N = (double)s.frames();
        for (int ch = 0; ch < 2; ch++)
            for (int c = 0; c < SCN_WCOLS; c++)
            {
                int up = 0, dn = 0;
                if (ch < s.ch)
                {
                    const size_t a = (size_t)((off + vis * c / SCN_WCOLS) * N), b = std::min((size_t)((off + vis * (c + 1) / SCN_WCOLS) * N) + 1, s.frames());
                    const size_t stride = std::max<size_t>(1, (b - a) / 200);
                    int mx = 0, mn = 0; for (size_t i = a; i < b; i += stride) { const int v = s.d[ch][i]; mx = std::max(mx, v); mn = std::min(mn, v); }
                    up = (int)(mx * (double)SCN_WLV / 32767.0 + 0.5); dn = (int)(-mn * (double)SCN_WLV / 32767.0 + 0.5);
                }
                WC.up[ch][c] = std::min(SCN_WLV, up); WC.dn[ch][c] = std::min(SCN_WLV, dn);
            }
    }
    const bool st = s.ch > 1;
    int mk[SCN_MCOLS] = {};
    auto colOf = [&](double frac) { const double px = (frac - off) / vis * SCN_SRC_W; if (px < 0 || px >= SCN_SRC_W) return -1; return std::max(0, std::min(SCN_MCOLS - 1, (int)(px * SCN_MCOLS / SCN_SRC_W))); };
    double sa = w_start, sb = w_end; if (loops && I.mode >= PM_FWD && I.mode <= PM_PING) { sa = w_l1; sb = w_l2; }
    if (slices) { sa = -1; sb = -1; }
    if (sa >= 0)
    {
        int ca = (int)((sa - off) / vis * SCN_MCOLS), cb = (int)((sb - off) / vis * SCN_MCOLS);
        for (int c = std::max(0, ca); c <= std::min(SCN_MCOLS - 1, cb); c++) mk[c] = 1;
    }
    auto put = [&](double fr, int kindW, bool selected) { const int c = colOf(fr); if (c < 0) return; const bool sh = mk[c] == 1 || (mk[c] >= 2 && ((mk[c] - 2) & 1)); mk[c] = 2 + 2 * (kindW + (selected ? 1 : 0)) + (sh ? 1 : 0); };
    if (slices) { for (int i = 0; i < I.slices; i++) put(I.slice[i], SCN_MK_SLICE_W - 0, i == selSlice && sel == 5); }
    else
    {
        if (loops && I.mode >= PM_FWD && I.mode <= PM_PING) { put(w_l1, SCN_MK_LOOP1_W, sel == 3); put(w_l2, SCN_MK_LOOP2_W, sel == 4); }
        put(w_start, SCN_MK_START_W, sel == 1); put(w_end, SCN_MK_END_W, sel == 2);
    }
    const float pp = audio::lastPlayPos();
    if (pp >= 0 && audio::playing() | audio::previewing() | true) { const int c = colOf(pp); if (c >= 0 && mk[c] < 2) { const bool sh = mk[c] == 1; mk[c] = 2 + 2 * SCN_MK_PLAY + (sh ? 1 : 0); } }
    xxl_scene::begin();
    xxl_scene::setState(WI.g0, st ? 2 : 1); xxl_scene::setState(WI.g1, st ? 1 : 0); xxl_scene::setState(WI.mg, 1);
    for (int ch = 0; ch < 2; ch++)
        for (int c = 0; c < SCN_WCOLS; c++)
        {
            const bool on = ch == 0 || st;
            xxl_scene::setState(WI.up[ch][c], on ? WC.up[ch][c] : 0); xxl_scene::setState(WI.dn[ch][c], on ? WC.dn[ch][c] : 0);
        }
    for (int c = 0; c < SCN_MCOLS; c++) xxl_scene::setState(WI.mk[c], mk[c]);
    xxl_scene::end();
}

/* ------------------------------------------------------------------ helpers for items */
void setI(Item& it, const char* name, const char* fmt = nullptr, ...)
{
    snprintf(it.name, sizeof it.name, "%s", name); it.val[0] = 0;
    if (fmt) { va_list ap; va_start(ap, fmt); vsnprintf(it.val, sizeof it.val, fmt, ap); va_end(ap); }
}
inline int stepOf(bool coarse, int fine, int big) { return coarse ? big : fine; }
double secOf(double frac, int inst) { const Sample& s = smp(inst); return frac * (double)s.frames() / kSr; }
void secAdj(double& f, int d, bool coarse, int inst, double lo, double hi)
{
    const size_t N = smp(inst).frames(); if (N < 2) return;
    const double st = (coarse ? 0.01 : 0.001) * kSr / (double)N;
    f = std::max(lo, std::min(hi, f + d * st));
}
void allOffUi() { audio::allOff(); }
void clearPattern() { pushUndo(); Pattern& p = pat(); for (auto& t : p.s) for (auto& s : t) s = Step(); }
void transposePattern(int d)
{
    pushUndo(); Pattern& p = pat();
    for (int t = 0; t < kTracks; t++) for (int s = 0; s < p.len; s++) { Step& st = p.s[t][s]; if (st.note < 128) st.note = (uint8_t)clampi(st.note + d, 0, 127); }
}
void previewRow()
{
    for (int t = 0; t < kTracks; t++) { const Step& st = pat().s[t][g_step]; if (st.note < 128 && !P().mute[t]) audio::noteOn(t, st.note, st.instr != 255 ? st.instr : g_inst, 100); }
}
void playToggle(bool song)
{
    if (audio::playing()) { audio::stop(); return; }
    audio::setEditPattern(g_pat); audio::play(song, true);
}

/* ------------------------------------------------------------------ pattern editor */
void patItems(Item* it, int& pages)
{
    pages = 3; const int pg = g_page[S_PAT]; Project& p = P();
    if (pg == 0)
    {
        setI(it[0], "Pattern", "%d", g_pat + 1); setI(it[1], "Length", "%d", pat().len); setI(it[2], "Step jump", "%d", g_stepJump);
        setI(it[3], "Fill"); setI(it[4], "Preview", "%s", g_preview ? "On" : "Off");
        setI(it[5], "Undo"); it[5].off = g_undo.empty(); setI(it[6], "Redo"); it[6].off = g_redo.empty(); it[3].act = it[5].act = it[6].act = true;
    }
    else if (pg == 1)
    {
        setI(it[0], "Tempo", "%.1f", audio::hostSync() && audio::playing() ? audio::tempo() : (double)p.tempo); setI(it[1], "Swing", "%d%%", p.swing);
        setI(it[2], "Transpose", "0"); setI(it[3], "Pad octave", "%+d", g_padOct); setI(it[4], "Clear"); setI(it[5], "Copy pat."); setI(it[6], "Paste pat."); it[6].off = !g_clipPatHas; it[4].act = it[5].act = it[6].act = true;
    }
    else
    {
        char b[16]; audio::dbText(p.trackVol[g_trk], b);
        setI(it[0], "Track", "%d", g_trk + 1); setI(it[1], "Mute", "%s", p.mute[g_trk] ? "On" : "Off"); setI(it[2], "Track vol", "%s", b);
        setI(it[3], "Track pan", "%d", p.trackPan[g_trk]); setI(it[4], "Host sync", "%s", audio::hostSync() ? "On" : "Off");
        setI(it[5], "Play mode", "%s", audio::songMode() ? "Song" : "Pattern"); setI(it[6], "Instrument", "%d", g_inst + 1);
    }
    it[7].name[0] = 0;
}
void patAdjust(int slot, int d, bool coarse)
{
    const int pg = g_page[S_PAT]; Project& p = P();
    if (pg == 0)
        switch (slot)
        {
            case 0: setPattern(g_pat + d); break;
            case 1: pushUndo(); pat().len = clampi(pat().len + (coarse ? d * 8 : d), 1, kMaxLen); g_step = std::min(g_step, pat().len - 1); break;
            case 2: g_stepJump = clampi(g_stepJump + d, 0, 16); cfgSave(); break;
            case 4: g_preview = d > 0; break;
        }
    else if (pg == 1)
        switch (slot)
        {
            case 0: p.tempo = std::max(40.f, std::min(300.f, p.tempo + (coarse ? d * 10.f : d * 0.5f))); break;
            case 1: p.swing = clampi(p.swing + d, 50, 75); break;
            case 2: transposePattern(coarse ? d * 12 : d); break;
            case 3: g_padOct = clampi(g_padOct + d, -3, 3); g_cfg.padOct = g_padOct; cfgSave(); break;
        }
    else
        switch (slot)
        {
            case 0: g_trk = clampi(g_trk + d, 0, kTracks - 1); break;
            case 1: p.mute[g_trk] = d > 0; break;
            case 2: p.trackVol[g_trk] = clampi(p.trackVol[g_trk] + (coarse ? d * 5 : d), 0, 100); break;
            case 3: p.trackPan[g_trk] = clampi(p.trackPan[g_trk] + (coarse ? d * 5 : d), -50, 50); break;
            case 4: audio::setHostSync(d > 0); g_cfg.hostSync = d > 0; cfgSave(); break;
            case 5: if (!audio::playing()) { if (d > 0) {} } break;
            case 6: g_inst = clampi(g_inst + d, 0, kInstr - 1); break;
        }
    dirty();
}
void patPress(int slot)
{
    const int pg = g_page[S_PAT]; Project& p = P();
    if (slot == 7) { g_page[S_PAT] = (pg + 1) % 3; dirty(); return; }
    if (pg == 0)
    {
        if (slot == 3)
        {
            pushUndo(); const Step src = cur(); const int j = std::max(1, g_stepJump);
            for (int s = g_step; s < pat().len; s += j) pat().s[g_trk][s] = src;
            toast("Filled track %d", g_trk + 1);
        }
        else if (slot == 4) g_preview = !g_preview;
        else if (slot == 5 && !g_undo.empty()) { g_redo.push_back(pat()); pat() = g_undo.back(); g_undo.pop_back(); g_step = std::min(g_step, pat().len - 1); }
        else if (slot == 6 && !g_redo.empty()) { g_undo.push_back(pat()); pat() = g_redo.back(); g_redo.pop_back(); g_step = std::min(g_step, pat().len - 1); }
    }
    else if (pg == 1)
    {
        if (slot == 4 && confirm(1, "Clear pattern")) { clearPattern(); toast("Pattern cleared"); }
        else if (slot == 5) { g_clipPat = pat(); g_clipPatHas = true; toast("Pattern %d copied", g_pat + 1); }
        else if (slot == 6 && g_clipPatHas) { pushUndo(); const int len = pat().len; (void)len; pat() = g_clipPat; toast("Pattern pasted"); }
        else if (slot == 0) audio::tapTempo();
    }
    else
    {
        if (slot == 1) p.mute[g_trk] = !p.mute[g_trk];
        else if (slot == 4) { audio::setHostSync(!audio::hostSync()); g_cfg.hostSync = audio::hostSync(); cfgSave(); }
        else if (slot == 5) { audio::stop(); toast("Play: pattern; Shift+Play: song"); }
    }
    dirty();
}
/* the wheel / value knobs on the pattern grid */
void editField(int d, bool coarse)
{
    Step& s = cur();
    if (g_par == 0)
    {
        int idx = s.note < 128 ? s.note : s.note == N_FADE ? -1 : s.note == N_CUT ? -2 : s.note == N_OFF ? -3 : -4;
        if (idx == -4) { if (d < 0) return; idx = g_lastNote; if (s.instr == 255) s.instr = (uint8_t)g_inst; }
        else idx += coarse ? d * 12 : d;
        if (idx > 127) idx = 127; if (idx < -4) idx = -4;
        s.note = idx >= 0 ? (uint8_t)idx : idx == -1 ? N_FADE : idx == -2 ? N_CUT : idx == -3 ? N_OFF : N_NONE;
        if (idx >= 0) { g_lastNote = idx; if (s.instr == 255) s.instr = (uint8_t)g_inst; if (g_preview) audio::noteOn(g_trk, idx, s.instr, 100); }
    }
    else if (g_par == 1)
    {
        int v = s.instr == 255 ? -1 : s.instr; if (v < 0 && d < 0) return; if (v < 0) v = g_inst - d >= 0 ? g_inst - 1 : -1;
        v = clampi(v + (coarse ? d * 8 : d), -1, kInstr - 1); s.instr = v < 0 ? 255 : (uint8_t)v; if (v >= 0) g_inst = v;
    }
    else
    {
        const int k = g_par - 2; uint8_t& ty = s.fx[k]; int16_t& v = s.fv[k];
        if (ty == FX_NONE) { if (d < 0) return; ty = (uint8_t)g_lastFx; v = (int16_t)clampi(ty == FX_VOL ? 100 : ty == FX_TEMPO ? (int)P().tempo : ty == FX_RETRIG ? 2 : ty == FX_CHANCE ? 50 : ty == FX_PAN ? 0 : 50, fxMin(ty), fxMax(ty)); return; }
        v = (int16_t)clampi(v + (coarse ? d * 10 : d), fxMin(ty), fxMax(ty));
        if (v < fxMin(ty) && d < 0) {}
    }
}
void cycleFx(int dir)
{
    Step& s = cur(); const int k = g_par - 2; if (k < 0 || k > 1) return;
    int t = s.fx[k] + dir; if (t < 0) t = FX_COUNT - 1; if (t >= FX_COUNT) t = 0;
    s.fx[k] = (uint8_t)t; if (t) { g_lastFx = t; s.fv[k] = (int16_t)clampi(s.fv[k], fxMin(t), fxMax(t)); }
}
void patDraw()
{
    grid::View v; v.pat = &pat(); v.first = g_trk >= 4 ? 4 : 0; v.step = g_step; v.track = g_trk; v.param = g_par; v.curState = 1; v.curColor = g_edit ? 0 : 1;
    const int ps = audio::playPattern() == g_pat ? audio::playStep() : -1; v.playhead = ps;
    static bool inactive[kTracks]; for (int t = 0; t < kTracks; t++) inactive[t] = P().mute[t]; v.inactive = inactive; v.stepDiv = 4;
    grid::frame(v);
}

/* ------------------------------------------------------------------ instrument editor */
const char* const kFilt[4] = { "Disabled", "Low-pass", "High-pass", "Band-pass" };
const char* const kModes[8] = { "1-Shot", "Forward Loop", "Backward Loop", "Pingpong Loop", "Slice", "Beat Slice", "Wavetable", "Granular" };
void ctlName(int voice, int k, char* b, size_t n)
{
    const char* nm = tv_control_name(voice, k); if (!nm || !*nm) nm = tv_control_label(voice, k);
    snprintf(b, n, "%.14s", nm ? nm : ""); for (char* c = b; *c; c++) if (*c == '|') *c = ' ';
}
void ensureUsed(Instr& I) { if (!I.used) { I = Instr(); I.used = true; I.kind = 1; I.voice = 0; audio::loadPatch(I); snprintf(I.name, sizeof I.name, "%s", tv_voice_name(0)); } }
void setSynthVoice(Instr& I, int v) { I.voice = clampi(v, 0, tv_voice_count() - 1); I.starter = 0; audio::loadPatch(I); snprintf(I.name, sizeof I.name, "%s", tv_voice_name(I.voice)); audio::refreshInstrument(g_inst); }
int instPages() { const Instr& I = inst(); return !I.used ? 1 : I.kind == 1 ? 4 : 2; }
void instItems(Item* it, int& pages)
{
    Instr& I = inst(); pages = instPages(); const int pg = std::min(g_page[S_INSTR], pages - 1); char b[32];
    if (!I.used) { setI(it[0], "Type", "Empty"); setI(it[2], "SAMPLE LOAD", "%s", "for a sample"); setI(it[4], "Macro 1", "%s", "synth voice"); return; }
    if (I.kind == 0)
    {
        if (pg == 0)
        {
            audio::dbText(I.vol, b);
            setI(it[0], "Volume", "%s", b); it[0].bar = I.vol; setI(it[1], "Panning", "%d", I.pan); it[1].bar = (I.pan + 50); it[1].sbar = true;
            setI(it[2], "Tune", "%d", I.tune); it[2].bar = (I.tune + 24) * 100 / 48; setI(it[3], "Finetune", "%d", I.fine); it[3].bar = (I.fine + 100) / 2;
            setI(it[4], "Filter Type", "%s", kFilt[I.ftype]); setI(it[5], "Cutoff", "%d", I.cutoff); it[5].bar = I.cutoff; setI(it[6], "Resonance", "%d", I.res); it[6].bar = I.res;
            if (!I.ftype) { it[5].off = it[6].off = true; it[5].bar = it[6].bar = -2; }
        }
        else
        {
            setI(it[0], "Attack", "%d", I.atk); it[0].bar = I.atk; setI(it[1], "Decay", "%d", I.dec); it[1].bar = I.dec; setI(it[2], "Sustain", "%d", I.sus); it[2].bar = I.sus;
            setI(it[3], "Release", "%d", I.rel); it[3].bar = I.rel; setI(it[4], "Reverb send", "%d", I.rev); it[4].bar = I.rev; setI(it[5], "Delay send", "%d", I.del); it[5].bar = I.del;
            setI(it[6], "Type", "Sample");
        }
    }
    else
    {
        if (pg == 0)
        {
            audio::dbText(I.vol, b);
            snprintf(b + 16 - 0, 1, "%s", ""); char vb[32]; snprintf(vb, sizeof vb, "%.8s", tv_voice_short_name(I.voice));
            setI(it[0], "Voice", "%s", tv_voice_name(I.voice)); setI(it[1], "Patch", "%.14s", audio::patchName(I.voice, I.starter));
            char db[16]; audio::dbText(I.vol, db);
            setI(it[2], "Volume", "%s", db); it[2].bar = I.vol; setI(it[3], "Panning", "%d", I.pan); it[3].bar = I.pan + 50; it[3].sbar = true;
            setI(it[4], "Reverb send", "%d", I.rev); it[4].bar = I.rev; setI(it[5], "Delay send", "%d", I.del); it[5].bar = I.del; setI(it[6], "Type", "Synth");
        }
        else
        {
            for (int i = 0; i < 8; i++)
            {
                const int k = (pg - 1) * 8 + i; if (k >= TV_NUM_CONTROLS) { it[i].blank = true; continue; }
                char nm[32]; ctlName(I.voice, k, nm, sizeof nm); char tx[32]; tv_control_text(I.voice, k, I.ctl[k], tx, sizeof tx, 0);
                setI(it[i], nm, "%.12s", tx); it[i].bar = (int)(I.ctl[k] * 100 + 0.5f);
            }
        }
    }
}
void instAdjust(int slot, int d, bool coarse)
{
    Instr& I = inst(); const int pg = std::min(g_page[S_INSTR], instPages() - 1);
    if (!I.used)
    {
        if (slot == 0 && d != 0) { ensureUsed(I); I.kind = 1; I.voice = 0; audio::loadPatch(I); snprintf(I.name, sizeof I.name, "%s", tv_voice_name(I.voice)); audio::refreshInstrument(g_inst); dirty(); }
        return;
    }
    if (I.kind == 0)
    {
        if (pg == 0)
            switch (slot)
            {
                case 0: I.vol = clampi(I.vol + (coarse ? d * 5 : d), 0, 100); break;
                case 1: I.pan = clampi(I.pan + (coarse ? d * 5 : d), -50, 50); break;
                case 2: I.tune = clampi(I.tune + (coarse ? d * 12 : d), -24, 24); break;
                case 3: I.fine = clampi(I.fine + (coarse ? d * 10 : d), -100, 100); break;
                case 4: I.ftype = clampi(I.ftype + d, 0, 3); break;
                case 5: I.cutoff = clampi(I.cutoff + (coarse ? d * 5 : d), 0, 100); break;
                case 6: I.res = clampi(I.res + (coarse ? d * 5 : d), 0, 100); break;
            }
        else
            switch (slot)
            {
                case 0: I.atk = clampi(I.atk + (coarse ? d * 5 : d), 0, 100); break;
                case 1: I.dec = clampi(I.dec + (coarse ? d * 5 : d), 0, 100); break;
                case 2: I.sus = clampi(I.sus + (coarse ? d * 5 : d), 0, 100); break;
                case 3: I.rel = clampi(I.rel + (coarse ? d * 5 : d), 0, 100); break;
                case 4: I.rev = clampi(I.rev + (coarse ? d * 5 : d), 0, 100); break;
                case 5: I.del = clampi(I.del + (coarse ? d * 5 : d), 0, 100); break;
            }
    }
    else
    {
        if (pg == 0)
            switch (slot)
            {
                case 0: setSynthVoice(I, I.voice + d); break;
                case 1: I.starter = clampi(I.starter + d, 0, audio::patchCount(I.voice) - 1); audio::loadPatch(I); audio::refreshInstrument(g_inst); break;
                case 2: I.vol = clampi(I.vol + (coarse ? d * 5 : d), 0, 100); break;
                case 3: I.pan = clampi(I.pan + (coarse ? d * 5 : d), -50, 50); break;
                case 4: I.rev = clampi(I.rev + (coarse ? d * 5 : d), 0, 100); break;
                case 5: I.del = clampi(I.del + (coarse ? d * 5 : d), 0, 100); break;
            }
        else
        {
            const int k = (pg - 1) * 8 + slot; if (k >= TV_NUM_CONTROLS) return;
            const int st = tv_control_steps(I.voice, k);
            if (st > 1) { int o = (int)(I.ctl[k] * st); o = clampi(o + d, 0, st - 1); I.ctl[k] = (o + 0.5f) / st; }
            else I.ctl[k] = std::max(0.f, std::min(1.f, I.ctl[k] + d * (coarse ? 0.05f : 0.01f)));
        }
    }
    dirty();
}
void instPress(int slot)
{
    Instr& I = inst(); const int pages = instPages();
    if (slot == 7 && pages > 1) { g_page[S_INSTR] = (g_page[S_INSTR] + 1) % pages; g_focus[S_INSTR] = std::min(g_focus[S_INSTR], 6); dirty(); return; }
    const int pg = std::min(g_page[S_INSTR], pages - 1);
    const bool typeSlot = (!I.used && slot == 0) || (I.used && I.kind == 0 && pg == 1 && slot == 6) || (I.used && I.kind == 1 && pg == 0 && slot == 6);
    if (typeSlot)
    {
        if (!I.used || I.kind == 0) { if (!I.used) { ensureUsed(I); } else { I.kind = 1; I.voice = clampi(I.voice, 0, 24); audio::loadPatch(I); snprintf(I.name, sizeof I.name, "%s", tv_voice_name(I.voice)); } }
        else if (smp(g_inst).frames() > 1) { I.kind = 0; snprintf(I.name, sizeof I.name, "%s", smp(g_inst).name.c_str()); }
        else toast("No sample in this slot");
        audio::refreshInstrument(g_inst); dirty(); return;
    }
    g_focus[S_INSTR] = slot; dirty();
}
void instDraw()
{
    Item it[8]; int pages; instItems(it, pages); const int pg = std::min(g_page[S_INSTR], pages - 1); const Instr& I = inst();
    char t[64]; snprintf(t, sizeof t, "Instrument %s %d/%d", I.used && I.kind == 1 ? "Synth" : "Parameters", pg + 1, pages);
    titleBar(t, instName(g_inst).c_str());
    bottom(S_INSTR, it, pages, pg);
    for (int i = 0; i < 8; i++) { const int r = R(S_INSTR, "barControl[%d]", i); if (it[i].bar >= 0) ui::bar(r, it[i].bar); else if (it[i].bar == -2) ui::bar(r, 0, true); }
    ui::bg(R(S_INSTR, "bgLabel")); ui::frame(R(S_INSTR, "frameControl"), std::min(g_focus[S_INSTR], 7));
    if (I.used && I.kind == 0 && pg == 0) { const char* n[4] = { kFilt[0], kFilt[1], kFilt[2], kFilt[3] }; ui::list(R(S_INSTR, "filterModeListControl"), 4, n, nullptr, I.ftype, 0); }
    if (I.used && I.kind == 1 && pg == 0 && (g_focus[S_INSTR] == 0 || g_focus[S_INSTR] == 1))
    {
        static std::vector<std::string> store; static std::vector<const char*> ptr; store.clear(); ptr.clear();
        const bool voices = g_focus[S_INSTR] == 0; const int n = voices ? tv_voice_count() : audio::patchCount(I.voice);
        for (int i = 0; i < n; i++) store.push_back(voices ? tv_voice_name(i) : audio::patchName(I.voice, i));
        for (auto& s : store) ptr.push_back(s.c_str());
        const int rows = ui::rows(R(S_INSTR, "list1Control")); const int sel = voices ? I.voice : I.starter; const int top = std::max(0, std::min(sel - rows / 2, std::max(0, n - rows)));
        ui::list(R(S_INSTR, "list1Control"), n, ptr.data(), nullptr, sel, top);
    }
}

/* ------------------------------------------------------------------ sample loader */
bool brUp() { return !g_br.path.empty(); }
int brCount() { return (int)g_br.list.size() + (brUp() ? 1 : 0); }
const Entry* brEntry(int i) { if (brUp()) { if (i == 0) return nullptr; i--; } return i >= 0 && i < (int)g_br.list.size() ? &g_br.list[i] : nullptr; }
void brEnsure() { if (!g_brLoaded) { g_br.load(); g_brLoaded = true; } }
void brMove(int d)
{
    const int n = brCount(); if (!n) return;
    g_br.sel = clampi(g_br.sel + d, 0, n - 1);
    const int rows = std::max(1, ui::rows(R(S_SLOAD, "explorerListControl")));
    if (g_br.sel < g_br.top) g_br.top = g_br.sel; if (g_br.sel >= g_br.top + rows) g_br.top = g_br.sel - rows + 1;
}
std::string baseName(const std::string& n) { std::string b = n; const size_t d = b.rfind('.'); if (d != std::string::npos && d > 0) b.resize(d); if (b.size() > 30) b.resize(30); return b; }
bool loadInto(int slot, const std::string& path)
{
    Sample s; std::string err;
    if (!loadWav(path.c_str(), s, err)) { toast("%s", err.c_str()); return false; }
    const size_t used = sampleMemUsed() - smp(slot).bytes();
    if (used + s.bytes() > kSampleMemMax) { toast("Not enough memory"); return false; }
    smp(slot) = std::move(s); Instr I; I.used = true; I.kind = 0; snprintf(I.name, sizeof I.name, "%s", baseName(smp(slot).name).c_str());
    P().ins[slot] = I; g_rev[slot]++; audio::refreshInstrument(slot); g_inst = slot; return true;
}
int nextFree(int from) { for (int k = 0; k < kInstr; k++) { const int i = (from + k) % kInstr; if (!P().ins[i].used) return i; } return -1; }
void slotClear(int i) { smp(i) = Sample(); P().ins[i] = Instr(); g_rev[i]++; }
bool instUsedInPatterns(int i)
{
    for (int p = 0; p < kPat; p++) for (int t = 0; t < kTracks; t++) for (int s = 0; s < P().pat[p].len; s++) if (P().pat[p].s[t][s].instr == i) return true;
    return false;
}
void sloadPreview()
{
    static Sample pv;
    if (audio::previewing()) { audio::previewSample(nullptr, 0); return; }
    if (g_sloadRegion == 1) { if (P().ins[g_sloadSel].used) audio::noteOn(g_trk, 60, g_sloadSel, 100); return; }
    const Entry* e = brEntry(g_br.sel); if (!e || e->dir) return;
    std::string err; Sample s; if (!loadWav(e->path.c_str(), s, err)) { toast("%s", err.c_str()); return; }
    pv = std::move(s); audio::previewSample(&pv, g_prevVol);
}
void sloadEnterOrLoad(bool next)
{
    const Entry* e = brEntry(g_br.sel);
    if (!e && brUp()) { g_br.up(); return; }
    if (!e) return;
    if (e->dir) { g_br.enter(); return; }
    int slot = next ? nextFree(g_sloadSel) : g_sloadSel;
    if (slot < 0) { toast("No free instrument slot"); return; }
    if (loadInto(slot, e->path)) { g_sloadSel = slot; if (next) { brMove(1); g_sloadSel = nextFree(slot + 1) >= 0 ? nextFree(slot + 1) : slot; } g_sloadTop = std::max(0, std::min(g_sloadSel - 6, kInstr - 13)); toast("Loaded %s", P().ins[slot].name); }
}
void sloadImport()
{
    const Entry* e = brEntry(g_br.sel);
    if (!e) return;
    if (!e->dir) { sloadEnterOrLoad(true); return; }
    Browser b; b.path = e->path; b.scan(); int n = 0;
    for (auto& f : b.list) { if (f.dir) continue; const int slot = nextFree(0); if (slot < 0) break; if (!loadInto(slot, f.path)) break; n++; }
    toast("Imported %d samples", n);
}
void sloadItems(Item* it, int& pages)
{
    pages = 1; char b[16];
    setI(it[0], "Add"); it[0].act = true; setI(it[1], "Add next"); it[1].act = true; setI(it[2], "Preview"); it[2].act = true;
    setI(it[3], "Rename"); it[3].off = true; it[3].act = true; setI(it[4], "Import"); it[4].act = true; setI(it[5], "Delete unused"); it[5].act = true;
    audio::dbText(g_prevVol, b); setI(it[6], "Preview Vol", "%s", b); setI(it[7], "Memory", "%d%%", (int)(sampleMemUsed() * 100 / kSampleMemMax));
    for (int i : { 0, 1, 2, 4, 5 }) { it[i].val[0] = 0; }
    for (int i = 0; i < 8; i++) if (i >= 3 && i != 3 && i < 6) {}
}
void sloadAdjust(int slot, int d, bool coarse)
{
    if (slot == 6) { g_prevVol = clampi(g_prevVol + (coarse ? d * 5 : d), 0, 100); audio::setPreviewVol(g_prevVol); }
    else if (g_sloadRegion == 0) brMove(d * (coarse ? 8 : 1));
    else { g_sloadSel = clampi(g_sloadSel + d * (coarse ? 8 : 1), 0, kInstr - 1); const int rows = std::max(1, ui::rows(R(S_SLOAD, "instrumentListControl"))); if (g_sloadSel < g_sloadTop) g_sloadTop = g_sloadSel; if (g_sloadSel >= g_sloadTop + rows) g_sloadTop = g_sloadSel - rows + 1; g_inst = g_sloadSel; }
    dirty();
}
void sloadPress(int slot)
{
    switch (slot)
    {
        case 0: sloadEnterOrLoad(false); break;
        case 1: sloadEnterOrLoad(true); break;
        case 2: sloadPreview(); break;
        case 4: sloadImport(); break;
        case 5: if (confirm(2, "Delete unused instruments")) { int n = 0; for (int i = 0; i < kInstr; i++) if (P().ins[i].used && !instUsedInPatterns(i)) { slotClear(i); n++; } toast("Deleted %d", n); } break;
        case 6: g_focus[S_SLOAD] = 6; break;
    }
    dirty();
}
void sloadDraw()
{
    brEnsure(); Item it[8]; int pages; sloadItems(it, pages);
    titleBar("Sample Loader", nullptr);
    bottom(S_SLOAD, it, 1, 0, -1);
    const std::string folder = g_br.path.empty() ? std::string("Drives") : (g_br.path.rfind('/') != std::string::npos ? g_br.path.substr(g_br.path.rfind('/') + 1) : g_br.path);
    ui::label(R(S_SLOAD, "explorerLabelControl"), folder.empty() ? "/" : folder.c_str(), 0); ui::label(R(S_SLOAD, "instrumentLabelControl"), "Instruments", 0);
    static std::vector<std::string> a, bn; static std::vector<const char*> pa, pb; a.clear(); bn.clear(); pa.clear(); pb.clear();
    if (brUp()) a.push_back("/..");
    for (auto& e : g_br.list) a.push_back((e.dir ? "/" : "") + e.name);
    for (auto& s : a) pa.push_back(s.c_str());
    for (int i = 0; i < kInstr; i++) bn.push_back(instName(i));
    for (auto& s : bn) pb.push_back(s.c_str());
    ui::list(R(S_SLOAD, "explorerListControl"), (int)pa.size(), pa.data(), nullptr, g_br.sel, g_br.top);
    ui::list(R(S_SLOAD, "instrumentListControl"), kInstr, pb.data(), nullptr, g_sloadSel, g_sloadTop);
    ui::bar(R(S_SLOAD, "memoryBarControl"), (int)(sampleMemUsed() * 100 / kSampleMemMax)); ui::bar(R(S_SLOAD, "previewVolumeBarControl"), g_prevVol);
    ui::bg(R(S_SLOAD, "bgLabel")); ui::frame(R(S_SLOAD, "frameControl"), g_focus[S_SLOAD] == 6 ? 2 : g_sloadRegion);
}

/* ------------------------------------------------------------------ sample playback */
bool instIsSample() { const Instr& I = inst(); return I.used && I.kind == 0 && smp(g_inst).frames() > 1; }
void evenSlices(Instr& I, int n) { n = clampi(n, 1, 32); I.slices = n; for (int i = 0; i < n; i++) I.slice[i] = (double)i / n; }
double spPos(int mark) { const Instr& I = inst(); switch (mark) { case 1: return I.start; case 2: return I.end; case 3: return I.loop1; case 4: return I.loop2; } return 0; }
int spMark(int slot)
{
    const Instr& I = inst();
    if (I.mode == PM_SLICE || I.mode == PM_BEAT) return slot == 1 || slot == 2 ? 5 : 0;
    if (I.mode == PM_WAVETABLE || I.mode == PM_GRANULAR) return slot == 1 ? 1 : 0;
    return slot == 1 ? 1 : slot == 2 ? 3 : slot == 3 ? 4 : slot == 4 ? 2 : 0;
}
int g_slice = 0;
void spItems(Item* it, int& pages)
{
    pages = 1; Instr& I = inst(); char b[32];
    setI(it[0], "Preview", audio::samplePlayPos(g_trk, g_inst) >= 0 ? "Stop" : "-"); it[0].act = true;
    if (!instIsSample()) { for (int i = 1; i < 6; i++) it[i].blank = true; it[0].off = true; setI(it[6], "Play Mode", "%s", I.used && I.kind == 1 ? "Synth" : "-"); it[6].off = true; return; }
    const int inst_ = g_inst;
    auto sec = [&](double f, char* o) { snprintf(o, 32, "%.3fs", secOf(f, inst_)); };
    if (I.mode == PM_SLICE || I.mode == PM_BEAT)
    {
        setI(it[1], "Slice", "%d/%d", I.slices ? g_slice + 1 : 0, I.slices); sec(I.slices ? I.slice[g_slice] : 0, b); setI(it[2], "Position", "%s", b);
        setI(it[3], "Add"); it[3].act = true; setI(it[4], "Delete"); it[4].act = true; setI(it[5], "Auto slice", "%d", I.slices);
        for (int i : { 3, 4 }) it[i].val[0] = 0;
    }
    else if (I.mode == PM_WAVETABLE || I.mode == PM_GRANULAR)
    {
        sec(I.start, b); setI(it[1], "Position", "%s", b); it[2].blank = it[3].blank = it[4].blank = true; setI(it[5], "Zoom", "%.2f", I.zoom / 100.0);
    }
    else
    {
        const bool loop = I.mode >= PM_FWD && I.mode <= PM_PING;
        sec(I.start, b); setI(it[1], "Start", "%s", b); sec(I.loop1, b); setI(it[2], "Loop Start", "%s", b); sec(I.loop2, b); setI(it[3], "Loop End", "%s", b);
        sec(I.end, b); setI(it[4], "End", "%s", b); setI(it[5], "Zoom", "%.2f", I.zoom / 100.0); if (!loop) it[2].off = it[3].off = true;
    }
    setI(it[6], "Play Mode", "%s", ""); it[6].val[0] = 0; it[6].act = true;
}
void spAdjust(int slot, int d, bool coarse)
{
    Instr& I = inst(); if (slot == 6) { I.mode = clampi(I.mode + d, 0, PM_COUNT - 1); if ((I.mode == PM_SLICE || I.mode == PM_BEAT) && I.slices == 0) evenSlices(I, 8); if (I.mode >= PM_FWD && I.mode <= PM_PING && I.loop2 <= I.loop1) { I.loop1 = I.start; I.loop2 = I.end; } dirty(); return; }
    if (!instIsSample()) return;
    if (I.mode == PM_SLICE || I.mode == PM_BEAT)
    {
        if (slot == 1 && I.slices) g_slice = clampi(g_slice + d, 0, I.slices - 1);
        else if (slot == 2 && I.slices) { const double lo = g_slice ? I.slice[g_slice - 1] + 0.0005 : 0, hi = g_slice + 1 < I.slices ? I.slice[g_slice + 1] - 0.0005 : 0.999; secAdj(I.slice[g_slice], d, coarse, g_inst, lo, hi); }
        else if (slot == 5) evenSlices(I, I.slices + d);
        dirty(); return;
    }
    if (slot == 5) { I.zoom = clampi(I.zoom + (coarse ? d * 100 : d * 10), 100, 3200); dirty(); return; }
    if (I.mode == PM_WAVETABLE || I.mode == PM_GRANULAR) { if (slot == 1) secAdj(I.start, d, coarse, g_inst, 0, 0.999); dirty(); return; }
    switch (slot)
    {
        case 1: secAdj(I.start, d, coarse, g_inst, 0, I.end - 0.001); if (I.loop1 < I.start) I.loop1 = I.start; break;
        case 2: secAdj(I.loop1, d, coarse, g_inst, I.start, I.loop2 - 0.001); break;
        case 3: secAdj(I.loop2, d, coarse, g_inst, I.loop1 + 0.001, I.end); break;
        case 4: secAdj(I.end, d, coarse, g_inst, I.start + 0.001, 1.0); if (I.loop2 > I.end) I.loop2 = I.end; break;
    }
    dirty();
}
void spPress(int slot)
{
    Instr& I = inst();
    if (slot == 0) { if (audio::samplePlayPos(g_trk, g_inst) >= 0) audio::noteOff(g_trk); else if (I.used) { audio::noteOn(g_trk, I.mode == PM_SLICE || I.mode == PM_BEAT ? 36 + g_slice : 60, g_inst, 100); } }
    else if (slot == 3 && (I.mode == PM_SLICE || I.mode == PM_BEAT) && instIsSample() && I.slices < 32)
    {
        const double pos = I.slices ? std::min(0.999, I.slice[g_slice] + 0.02) : 0.5; int at = std::min(I.slices, g_slice + 1);
        for (int i = I.slices; i > at; i--) I.slice[i] = I.slice[i - 1]; I.slice[at] = pos; I.slices++; g_slice = at;
    }
    else if (slot == 4 && (I.mode == PM_SLICE || I.mode == PM_BEAT) && I.slices > 1) { for (int i = g_slice; i + 1 < I.slices; i++) I.slice[i] = I.slice[i + 1]; I.slices--; g_slice = std::min(g_slice, I.slices - 1); }
    else g_focus[S_SPLAY] = slot;
    dirty();
}
void spDraw()
{
    Item it[8]; int pages; spItems(it, pages); const Instr& I = inst();
    titleBar("Sample Playback", instName(g_inst).c_str());
    bottom(S_SPLAY, it, 1, 0, -1);
    static const char* n[8]; for (int i = 0; i < 8; i++) n[i] = kModes[i];
    ui::list(R(S_SPLAY, "playModeListControl"), 8, n, nullptr, I.mode, 0);
    ui::bg(R(S_SPLAY, "bgLabel")); ui::frame(R(S_SPLAY, "frameControl"), std::min(g_focus[S_SPLAY], 6));
    if (instIsSample())
    {
        const int mk = spMark(g_focus[S_SPLAY]); w_start = I.start; w_end = I.end; w_l1 = I.loop1; w_l2 = I.loop2;
        const bool sl = I.mode == PM_SLICE || I.mode == PM_BEAT;
        waveDraw(g_inst, sl ? 100 : I.zoom, sl ? 5 : mk, g_slice, true, sl, sl && I.slices ? I.slice[g_slice] : spPos(mk ? mk : 1));
    }
    else waveHide();
}

/* ------------------------------------------------------------------ sample editor */
const char* const kFx[11] = { "Normalizer", "Crop", "Reverse", "Fade In", "Fade Out", "Amplifier", "Overdrive", "Delay", "Bitcrusher", "Silence", "Invert" };
double g_selA = 0, g_selB = 1;
void effectApply()
{
    if (!instIsSample()) { toast("No sample"); return; }
    Sample& s = smp(g_inst); const size_t N = s.frames();
    size_t a = (size_t)(g_selA * N), b = (size_t)(g_selB * N); if (b > N) b = N; if (b <= a + 1) { toast("Selection too small"); return; }
    g_sedUndo.assign(1, s); g_sedUndoInstr = g_inst;
    const int ch = s.ch;
    switch (g_effect)
    {
        case 0: { int pk = 1; for (int c = 0; c < ch; c++) for (size_t i = a; i < b; i++) pk = std::max(pk, abs((int)s.d[c][i])); const double g = 32000.0 / pk; for (int c = 0; c < ch; c++) for (size_t i = a; i < b; i++) s.d[c][i] = (int16_t)clampi((int)lrint(s.d[c][i] * g), -32768, 32767); break; }
        case 1: for (int c = 0; c < ch; c++) { std::vector<int16_t> n(s.d[c].begin() + a, s.d[c].begin() + b); s.d[c] = n; } break;
        case 2: for (int c = 0; c < ch; c++) std::reverse(s.d[c].begin() + a, s.d[c].begin() + b); break;
        case 3: for (int c = 0; c < ch; c++) for (size_t i = a; i < b; i++) s.d[c][i] = (int16_t)(s.d[c][i] * (double)(i - a) / (b - a)); break;
        case 4: for (int c = 0; c < ch; c++) for (size_t i = a; i < b; i++) s.d[c][i] = (int16_t)(s.d[c][i] * (double)(b - i) / (b - a)); break;
        case 5: { const double g = pow(10.0, (g_fxp[0] - 50) * 0.48 / 20.0); for (int c = 0; c < ch; c++) for (size_t i = a; i < b; i++) s.d[c][i] = (int16_t)clampi((int)lrint(s.d[c][i] * g), -32768, 32767); break; }
        case 6: { const double dr = 1.0 + g_fxp[0] * 0.15; for (int c = 0; c < ch; c++) for (size_t i = a; i < b; i++) s.d[c][i] = (int16_t)lrint(tanh(s.d[c][i] / 32768.0 * dr) / tanh(dr) * 32767.0); break; }
        case 7: { const size_t dl = std::max<size_t>(1, (size_t)((g_fxp[0] + 1) * 5 * kSr / 1000)); for (int c = 0; c < ch; c++) { std::vector<int16_t> src(s.d[c]); for (size_t i = a + dl; i < b; i++) s.d[c][i] = (int16_t)clampi(src[i] + (int)(s.d[c][i - dl] * 0.5), -32768, 32767); } break; }
        case 8: { const int bits = clampi(1 + g_fxp[0] * 15 / 100, 1, 16); const int sh = 16 - bits; for (int c = 0; c < ch; c++) for (size_t i = a; i < b; i++) s.d[c][i] = (int16_t)((s.d[c][i] >> sh) << sh); break; }
        case 9: for (int c = 0; c < ch; c++) for (size_t i = a; i < b; i++) s.d[c][i] = 0; break;
        case 10: for (int c = 0; c < ch; c++) for (size_t i = a; i < b; i++) s.d[c][i] = (int16_t)clampi(-(int)s.d[c][i], -32768, 32767); break;
    }
    Instr& I = inst(); if (g_effect == 1) { I.start = 0; I.end = 1; I.loop1 = 0; I.loop2 = 1; g_selA = 0; g_selB = 1; }
    g_rev[g_inst]++; toast("%s applied", kFx[g_effect]); dirty();
}
void seItems(Item* it, int& pages)
{
    pages = 1; char b[32]; const Instr& I = inst();
    if (!instIsSample()) { it[0].blank = it[1].blank = it[2].blank = it[3].blank = it[4].blank = it[5].blank = true; setI(it[6], "Effect"); it[6].off = true; return; }
    snprintf(b, sizeof b, "%.3fs", secOf(g_selA, g_inst)); setI(it[0], "Start", "%s", b); snprintf(b, sizeof b, "%.3fs", secOf(g_selB, g_inst)); setI(it[1], "End", "%s", b);
    setI(it[2], "Zoom", "%.2f", I.zoom / 100.0); setI(it[3], "Undo"); it[3].act = true; it[3].off = g_sedUndo.empty() || g_sedUndoInstr != g_inst;
    const char* pn = g_effect == 5 ? "Gain" : g_effect == 6 ? "Drive" : g_effect == 7 ? "Time" : g_effect == 8 ? "Bits" : nullptr;
    if (pn) { setI(it[4], pn, "%d", g_fxp[0]); } else it[4].blank = true;
    setI(it[5], "Apply"); it[5].act = true; setI(it[6], "Effect"); it[6].act = true;
}
void seAdjust(int slot, int d, bool coarse)
{
    if (slot == 6) { g_effect = clampi(g_effect + d, 0, 10); dirty(); return; }
    if (!instIsSample()) return; Instr& I = inst();
    switch (slot)
    {
        case 0: secAdj(g_selA, d, coarse, g_inst, 0, g_selB - 0.001); break;
        case 1: secAdj(g_selB, d, coarse, g_inst, g_selA + 0.001, 1.0); break;
        case 2: I.zoom = clampi(I.zoom + (coarse ? d * 100 : d * 10), 100, 3200); break;
        case 4: g_fxp[0] = clampi(g_fxp[0] + (coarse ? d * 5 : d), 0, 100); break;
    }
    dirty();
}
void sePress(int slot)
{
    if (slot == 3 && !g_sedUndo.empty() && g_sedUndoInstr == g_inst) { smp(g_inst) = g_sedUndo[0]; g_sedUndo.clear(); g_rev[g_inst]++; toast("Undone"); }
    else if (slot == 5) effectApply();
    else g_focus[S_SEDIT] = slot;
    dirty();
}
void seDraw()
{
    Item it[8]; int pages; seItems(it, pages); const Instr& I = inst();
    titleBar("Sample Editor", instName(g_inst).c_str());
    bottom(S_SEDIT, it, 1, 0, -1);
    static const char* n[11]; for (int i = 0; i < 11; i++) n[i] = kFx[i];
    ui::list(R(S_SEDIT, "effectList"), 11, n, nullptr, g_effect, 0);
    ui::bg(R(S_SEDIT, "bgLabel"));
    if (instIsSample()) { w_start = g_selA; w_end = g_selB; w_l1 = 0; w_l2 = 1; const int f = g_focus[S_SEDIT]; waveDraw(g_inst, I.zoom, f == 0 ? 1 : f == 1 ? 2 : 0, 0, false, false, f == 1 ? g_selB : g_selA); }
    else waveHide();
}

/* ------------------------------------------------------------------ song */
void songClamp() { Project& p = P(); g_songSel = clampi(g_songSel, 0, p.songLen - 1); const int rows = std::max(1, ui::rows(R(0, "popups.popupListControl"))); if (g_songSel < g_songTop) g_songTop = g_songSel; if (g_songSel >= g_songTop + rows) g_songTop = g_songSel - rows + 1; }
void songPush() { Project& p = P(); std::vector<uint8_t> v(p.song, p.song + p.songLen); v.push_back((uint8_t)p.songLen); g_songUndo.push_back(v); if (g_songUndo.size() > 16) g_songUndo.erase(g_songUndo.begin()); }
void soItems(Item* it, int& pages)
{
    pages = 1; Project& p = P();
    setI(it[0], "Play\nPattern"); setI(it[1], "Play\nSong"); setI(it[2], "Undo"); it[2].off = g_songUndo.empty(); setI(it[3], "Add\nSlot"); setI(it[4], "Delete\nSlot", ""); it[4].off = p.songLen <= 1;
    for (int i : { 0, 1, 2, 3, 4 }) it[i].act = true;
    setI(it[5], "Pattern", "%d", p.song[g_songSel] + 1); it[6].blank = true; setI(it[7], "Tempo", "%.1f BPM", (double)p.tempo);
    for (char* c : { it[0].name, it[1].name, it[3].name, it[4].name }) for (char* q = c; *q; q++) if (*q == '\n') *q = ' ';
}
void soAdjust(int slot, int d, bool coarse)
{
    Project& p = P();
    if (slot == 5) { p.song[g_songSel] = (uint8_t)clampi(p.song[g_songSel] + d, 0, kPat - 1); }
    else if (slot == 7) p.tempo = std::max(40.f, std::min(300.f, p.tempo + (coarse ? d * 10.f : d * 0.5f)));
    else { g_songSel += d; songClamp(); }
    dirty();
}
void soPress(int slot)
{
    Project& p = P();
    switch (slot)
    {
        case 0: setPattern(p.song[g_songSel]); audio::stop(); audio::play(false, true); break;
        case 1: audio::stop(); audio::setEditPattern(p.song[0]); audio::play(true, true); break;
        case 2: if (!g_songUndo.empty()) { auto v = g_songUndo.back(); g_songUndo.pop_back(); p.songLen = v.back(); v.pop_back(); for (size_t i = 0; i < v.size(); i++) p.song[i] = v[i]; songClamp(); } break;
        case 3: if (p.songLen < kSong) { songPush(); for (int i = p.songLen; i > g_songSel + 1; i--) p.song[i] = p.song[i - 1]; p.song[g_songSel + 1] = p.song[g_songSel]; p.songLen++; g_songSel++; songClamp(); } else toast("Song is full"); break;
        case 4: if (p.songLen > 1) { songPush(); for (int i = g_songSel; i + 1 < p.songLen; i++) p.song[i] = p.song[i + 1]; p.songLen--; songClamp(); } break;
        default: g_focus[S_SONG] = slot;
    }
    dirty();
}
void soDraw()
{
    Item it[8]; int pages; soItems(it, pages); Project& p = P();
    titleBar("Song", nullptr); bottom(S_SONG, it, 1, 0, -1);
    static std::vector<std::string> a, v; static std::vector<const char*> pa, pv; static bool det[kSong]; a.clear(); v.clear(); pa.clear(); pv.clear();
    for (int i = 0; i < p.songLen; i++) { char b[48]; snprintf(b, sizeof b, "%2d  Pattern %d", i + 1, p.song[i] + 1); a.push_back(b); snprintf(b, sizeof b, "%d", p.pat[p.song[i]].len); v.push_back(b); det[i] = audio::playing() && audio::songMode() && audio::playSongSlot() == i; }
    for (auto& s : a) pa.push_back(s.c_str()); for (auto& s : v) pv.push_back(s.c_str());
    songClamp(); ui::list(R(0, "popups.popupListControl"), p.songLen, pa.data(), pv.data(), g_songSel, g_songTop, det);
    ui::bar(R(S_SONG, "barControl"), (int)((p.tempo - 40) * 100 / 260)); ui::bg(R(S_SONG, "bgLabel")); ui::frame(R(S_SONG, "frameControl"), 1);
}

/* ------------------------------------------------------------------ project / file */
void projRefresh()
{
    g_projList.clear(); std::string d = projectsDir();
    if (DIR* dir = opendir(d.c_str()))
    {
        while (dirent* e = readdir(dir)) { std::string n = e->d_name; if (n.size() > 5 && n.substr(n.size() - 5) == ".txlp") g_projList.push_back(n.substr(0, n.size() - 5)); }
        closedir(dir);
    }
    std::sort(g_projList.begin(), g_projList.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    g_projSel = clampi(g_projSel, 0, std::max(0, (int)g_projList.size() - 1)); g_projDirty = false;
}
void recentLoad() { g_recent.clear(); FILE* f = fopen((g_root + "/recent.txt").c_str(), "r"); char b[256]; while (f && fgets(b, sizeof b, f)) { size_t n = strlen(b); while (n && (b[n - 1] == '\n' || b[n - 1] == '\r')) b[--n] = 0; if (n) g_recent.push_back(b); } if (f) fclose(f); }
void recentAdd(const std::string& n)
{
    g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), n), g_recent.end()); g_recent.insert(g_recent.begin(), n); if (g_recent.size() > 8) g_recent.resize(8);
    FILE* f = fopen((g_root + "/recent.txt").c_str(), "w"); if (f) { for (auto& r : g_recent) fprintf(f, "%s\n", r.c_str()); fclose(f); }
}
bool projSave(const std::string& name)
{
    snprintf(P().name, sizeof P().name, "%s", name.c_str());
    if (!saveProject((projectsDir() + "/" + name + ".txlp").c_str())) { toast("Save failed"); return false; }
    recentAdd(name); g_projDirty = true; toast("Saved %s", name.c_str()); return true;
}
void projOpen(const std::string& name)
{
    std::string err; audio::stop(); audio::allOff();
    if (!loadProject((projectsDir() + "/" + name + ".txlp").c_str(), err)) { toast("%s", err.c_str()); return; }
    for (int i = 0; i < kInstr; i++) g_rev[i]++;
    snprintf(P().name, sizeof P().name, "%s", name.c_str()); recentAdd(name); g_pat = 0; g_step = 0; g_trk = 0; g_inst = 0; audio::setEditPattern(0); g_undo.clear(); g_redo.clear();
    audio::refreshInstrument(0); for (int i = 0; i < kInstr; i++) audio::refreshInstrument(i);
    toast("Opened %s", name.c_str()); g_scr = S_PAT;
}
void demoBeat()
{
    const char* f[3] = { "demo_kick.wav", "demo_snare.wav", "demo_hat.wav" }; bool any = false;
    for (int i = 0; i < 3; i++) if (loadInto(i, g_root + "/Samples/" + f[i])) any = true;
    if (!any) return;
    Pattern& p = P().pat[0]; p.len = 16;
    for (int st = 0; st < 16; st += 4) { p.s[0][st].note = 60; p.s[0][st].instr = 0; }
    for (int st = 4; st < 16; st += 8) { p.s[1][st].note = 60; p.s[1][st].instr = 1; }
    for (int st = 0; st < 16; st += 2) { p.s[2][st].note = 60; p.s[2][st].instr = 2; }
    g_inst = 0; g_sloadSel = 0;
}
void projNew()
{
    audio::stop(); audio::allOff(); projectInit(P()); for (int i = 0; i < kInstr; i++) { smp(i) = Sample(); g_rev[i]++; }
    g_pat = 0; g_step = 0; g_trk = 0; g_inst = 0; audio::setEditPattern(0); g_undo.clear(); g_redo.clear(); for (int i = 0; i < kInstr; i++) audio::refreshInstrument(i); demoBeat(); toast("New project"); g_scr = S_PAT;
}
std::string autoName() { projRefresh(); for (int n = 1; n < 1000; n++) { char b[32]; snprintf(b, sizeof b, "Project %d", n); if (std::find(g_projList.begin(), g_projList.end(), b) == g_projList.end()) return b; } return "Project"; }
void prItems(Item* it, int& pages)
{
    pages = 1; setI(it[0], "New"); setI(it[1], "Save"); setI(it[2], "Save As"); setI(it[3], "Delete"); it[4].blank = it[5].blank = true; setI(it[6], "Cancel"); setI(it[7], "Open");
    for (int i : { 0, 1, 2, 3, 6, 7 }) it[i].act = true; if (g_projList.empty()) it[3].off = it[7].off = true;
}
void prAdjust(int slot, int d, bool coarse) { (void)slot; g_projSel = clampi(g_projSel + d * (coarse ? 5 : 1), 0, std::max(0, (int)g_projList.size() - 1)); const int rows = std::max(1, ui::rows(R(S_PROJ, "fileListControl"))); if (g_projSel < g_projTop) g_projTop = g_projSel; if (g_projSel >= g_projTop + rows) g_projTop = g_projSel - rows + 1; dirty(); }
void prPress(int slot)
{
    switch (slot)
    {
        case 0: if (confirm(3, "New project (unsaved changes are lost)")) projNew(); break;
        case 1: projSave(P().name); break;
        case 2: projSave(autoName()); break;
        case 3: if (!g_projList.empty() && confirm(4, "Delete project")) { unlink((projectsDir() + "/" + g_projList[g_projSel] + ".txlp").c_str()); g_projDirty = true; toast("Deleted"); } break;
        case 6: g_scr = S_PAT; break;
        case 7: if (!g_projList.empty()) projOpen(g_projList[g_projSel]); break;
    }
    dirty();
}
void prDraw()
{
    if (g_projDirty) projRefresh(); static bool rl = false; if (!rl) { recentLoad(); rl = true; }
    Item it[8]; int pages; prItems(it, pages); titleBar("File - Open Project", P().name); bottom(S_PROJ, it, 1, 0, -1);
    ui::label(R(S_PROJ, "browserLabelControl"), "Projects", 0); ui::label(R(S_PROJ, "recentLabelControl"), "Recent", 0);
    static std::vector<const char*> pa, pr; pa.clear(); pr.clear(); for (auto& s : g_projList) pa.push_back(s.c_str()); for (auto& s : g_recent) pr.push_back(s.c_str());
    ui::list(R(S_PROJ, "fileListControl"), (int)pa.size(), pa.data(), nullptr, g_projSel, g_projTop); ui::list(R(S_PROJ, "recentListControl"), (int)pr.size(), pr.data(), nullptr, -1, 0);
    ui::bg(R(S_PROJ, "bgLabel")); ui::frame(R(S_PROJ, "frameControl"), 0);
}

/* ------------------------------------------------------------------ config */
struct CE { std::string name, val; std::function<void(int)> adj; std::vector<std::string> choices; int cur = 0; };
int g_midiCh = 0;
const char* const kGroups[4] = { "Project Settings", "General", "MIDI", "Metronome" };
std::vector<CE> confEntries(int g)
{
    std::vector<CE> v; char b[64]; Project& p = P();
    auto onoff = [](int& x, int d) { x = d > 0 ? 1 : 0; };
    switch (g)
    {
        case 0:
            v.push_back({ "Project name", p.name, nullptr });
            snprintf(b, sizeof b, "%.1f", (double)p.tempo); v.push_back({ "Tempo", b, [&p](int d) { p.tempo = std::max(40.f, std::min(300.f, p.tempo + d * 0.5f)); } });
            snprintf(b, sizeof b, "%d%%", p.swing); v.push_back({ "Swing", b, [&p](int d) { p.swing = clampi(p.swing + d, 50, 75); } });
            snprintf(b, sizeof b, "%d", pat().len); v.push_back({ "Pattern length", b, [](int d) { pushUndo(); pat().len = clampi(pat().len + d, 1, kMaxLen); g_step = std::min(g_step, pat().len - 1); } });
            break;
        case 1:
            v.push_back({ "Host sync", audio::hostSync() ? "On" : "Off", [onoff](int d) { audio::setHostSync(d > 0); g_cfg.hostSync = d > 0; }, { "Off", "On" }, audio::hostSync() ? 1 : 0 });
            snprintf(b, sizeof b, "%+d", g_padOct); v.push_back({ "Pad octave", b, [](int d) { g_padOct = clampi(g_padOct + d, -3, 3); g_cfg.padOct = g_padOct; } });
            snprintf(b, sizeof b, "%s", g_cfg.padVel >= 127 ? "Full" : "Medium"); v.push_back({ "Pad velocity", b, [](int d) { g_cfg.padVel = d > 0 ? 127 : 100; }, { "Medium", "Full" }, g_cfg.padVel >= 127 ? 1 : 0 });
            v.push_back({ "Pads record notes", g_cfg.noteRec ? "On" : "Off", [](int d) { g_cfg.noteRec = d > 0; }, { "Off", "On" }, g_cfg.noteRec });
            snprintf(b, sizeof b, "%d", g_stepJump); v.push_back({ "Step jump", b, [](int d) { g_stepJump = clampi(g_stepJump + d, 0, 16); } });
            break;
        case 2:
            snprintf(b, sizeof b, "%s", g_midiCh ? std::to_string(g_midiCh).c_str() : "Omni"); v.push_back({ "MIDI in channel", b, [](int d) { g_midiCh = clampi(g_midiCh + d, 0, 16); } });
            v.push_back({ "MIDI notes play", "Current instrument", nullptr });
            break;
        case 3:
            v.push_back({ "Metronome", g_cfg.metro ? "On" : "Off", [](int d) { g_cfg.metro = d > 0; audio::setMetronome(g_cfg.metro, g_cfg.metroVol); }, { "Off", "On" }, g_cfg.metro });
            snprintf(b, sizeof b, "%d", g_cfg.metroVol); v.push_back({ "Metronome volume", b, [](int d) { g_cfg.metroVol = clampi(g_cfg.metroVol + d, 0, 100); audio::setMetronome(g_cfg.metro, g_cfg.metroVol); } });
            break;
    }
    return v;
}
void cfItems(Item* it, int& pages)
{
    pages = 1; auto e = confEntries(g_confBase); g_confSub = clampi(g_confSub, 0, std::max(0, (int)e.size() - 1));
    setI(it[0], kGroups[g_confBase]); it[1].blank = true; it[2].blank = true;
    if (!e.empty()) setI(it[3], e[g_confSub].name.c_str()); else it[3].blank = true;
    it[4].blank = it[5].blank = it[6].blank = true; setI(it[7], "Enter"); it[7].off = true;
    for (int i : { 0, 3 }) if (!it[i].blank) it[i].act = true;
}
void cfAdjust(int slot, int d, bool coarse)
{
    (void)coarse;
    if (g_confRegion == 0 || slot == 0) { g_confBase = clampi(g_confBase + d, 0, 3); g_confSub = 0; }
    else { auto e = confEntries(g_confBase); if (g_confSub < (int)e.size() && e[g_confSub].adj) { e[g_confSub].adj(d); cfgSave(); } }
    dirty();
}
void cfPress(int slot) { if (slot == 0) g_confRegion = 0; else if (slot == 3) g_confRegion = 1; dirty(); }
void cfDraw()
{
    Item it[8]; int pages; cfItems(it, pages); titleBar("Config", nullptr); bottom(S_CONF, it, 1, 0, -1);
    static std::vector<const char*> g; g.clear(); for (int i = 0; i < 4; i++) g.push_back(kGroups[i]);
    ui::list(R(S_CONF, "configBasemenuListControl"), 4, g.data(), nullptr, g_confBase, 0);
    auto e = confEntries(g_confBase); static std::vector<std::string> n, v; static std::vector<const char*> pn, pv; n.clear(); v.clear(); pn.clear(); pv.clear();
    for (auto& x : e) { n.push_back(x.name); v.push_back(x.val); } for (auto& s : n) pn.push_back(s.c_str()); for (auto& s : v) pv.push_back(s.c_str());
    ui::list(R(S_CONF, "configSubmenuListControl"), (int)pn.size(), pn.data(), pv.data(), g_confSub, 0);
    if (g_confSub < (int)e.size() && !e[g_confSub].choices.empty()) { static std::vector<const char*> c; c.clear(); for (auto& s : e[g_confSub].choices) c.push_back(s.c_str()); ui::list(R(S_CONF, "configListControl"), (int)c.size(), c.data(), nullptr, e[g_confSub].cur, 0); }
    ui::bg(R(S_CONF, "bgLabel")); ui::frame(R(S_CONF, "frameControl"), g_confRegion);
}

/* ------------------------------------------------------------------ master / mixer */
const char* const kSat[5] = { "Off", "Soft", "Medium", "Hard", "Fold" };
const char* const kRates[14] = { "1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1" };
const char* const kMTitle[6] = { "Master 1/6", "Track Mixer 2/6", "Track Panning 3/6", "Master Reverb 4/6", "Master Delay 5/6", "Master Limiter 6/6" };
void maItems(Item* it, int& pages)
{
    pages = 6; Master& m = P().master; const int pg = g_page[S_MASTER]; char b[32];
    if (pg == 0)
    {
        audio::dbText(m.volume, b); setI(it[0], "Volume", "%s", b); it[0].bar = m.volume; setI(it[1], "Bass", "%d", m.bass); it[1].bar = m.bass; setI(it[2], "Space", "%d", m.space); it[2].bar = m.space;
        setI(it[3], "EQ Low", "%d", m.eqLo); it[3].bar = (m.eqLo + 50); it[3].sbar = true; setI(it[4], "EQ Mid", "%d", m.eqMid); it[4].bar = m.eqMid + 50; setI(it[5], "Saturation", "%s", kSat[clampi(m.saturation, 0, 4)]);
        setI(it[6], "EQ High", "%d", m.eqHi); it[6].bar = m.eqHi + 50;
    }
    else if (pg == 1 || pg == 2)
    {
        for (int i = 0; i < 8; i++)
        {
            if (pg == 1) { char d[16]; audio::dbText(P().trackVol[i], d); setI(it[i], d, "%s", P().mute[i] ? "Muted" : "Mute"); }
            else setI(it[i], "Pan", "%d", P().trackPan[i]);
        }
        it[7].name[0] = 0;
    }
    else if (pg == 3) { setI(it[0], "Size", "%d", m.revSize); it[0].bar = m.revSize; setI(it[1], "Damping", "%d", m.revDamp); it[1].bar = m.revDamp; setI(it[2], "Predelay", "%d", m.revPre); it[2].bar = m.revPre; setI(it[3], "Diffusion", "%d", m.revDiff); it[3].bar = m.revDiff; setI(it[4], "Mix", "%d", m.revMix); it[4].bar = m.revMix; }
    else if (pg == 4)
    {
        setI(it[0], "PingPong", "%s", m.delPing ? "On" : "Off"); setI(it[1], "Sync", "%s", m.delSync ? "On" : "Off"); setI(it[2], "Rate", "%s", kRates[clampi(m.delTime, 0, 13)]); it[2].off = !m.delSync;
        setI(it[3], "Time", "%d ms", (m.delTime + 1) * 35); it[3].off = m.delSync; it[3].bar = m.delSync ? -2 : std::min(100, m.delTime * 100 / 28);
        setI(it[4], "Feedback", "%d", m.delFb); it[4].bar = m.delFb; setI(it[5], "Mix", "%d", m.delMix); it[5].bar = m.delMix;
    }
    else { setI(it[0], "Threshold", "%d", m.limThr); it[0].bar = m.limThr; setI(it[1], "Attack", "%d", m.limAtk); it[1].bar = m.limAtk; setI(it[2], "Release", "%d", m.limRel); it[2].bar = m.limRel; }
}
void maAdjust(int slot, int d, bool coarse)
{
    Master& m = P().master; const int pg = g_page[S_MASTER]; const int s = coarse ? d * 5 : d;
    if (pg == 0) switch (slot) { case 0: m.volume = clampi(m.volume + s, 0, 100); break; case 1: m.bass = clampi(m.bass + s, 0, 100); break; case 2: m.space = clampi(m.space + s, 0, 100); break; case 3: m.eqLo = clampi(m.eqLo + s, -50, 50); break; case 4: m.eqMid = clampi(m.eqMid + s, -50, 50); break; case 5: m.saturation = clampi(m.saturation + d, 0, 4); break; case 6: m.eqHi = clampi(m.eqHi + s, -50, 50); break; }
    else if (pg == 1) { if (slot < 8) P().trackVol[slot] = clampi(P().trackVol[slot] + s, 0, 100); }
    else if (pg == 2) { if (slot < 8) P().trackPan[slot] = clampi(P().trackPan[slot] + s, -50, 50); }
    else if (pg == 3) switch (slot) { case 0: m.revSize = clampi(m.revSize + s, 0, 100); break; case 1: m.revDamp = clampi(m.revDamp + s, 0, 100); break; case 2: m.revPre = clampi(m.revPre + s, 0, 100); break; case 3: m.revDiff = clampi(m.revDiff + s, 0, 100); break; case 4: m.revMix = clampi(m.revMix + s, 0, 100); break; }
    else if (pg == 4) switch (slot) { case 0: m.delPing = d > 0; break; case 1: m.delSync = d > 0; m.delTime = clampi(m.delTime, 0, m.delSync ? 13 : 28); break; case 2: case 3: m.delTime = clampi(m.delTime + d, 0, m.delSync ? 13 : 28); break; case 4: m.delFb = clampi(m.delFb + s, 0, 100); break; case 5: m.delMix = clampi(m.delMix + s, 0, 100); break; }
    else switch (slot) { case 0: m.limThr = clampi(m.limThr + s, 0, 100); break; case 1: m.limAtk = clampi(m.limAtk + s, 0, 100); break; case 2: m.limRel = clampi(m.limRel + s, 0, 100); break; }
    dirty();
}
void maPress(int slot)
{
    const int pg = g_page[S_MASTER];
    if (slot == 7 && pg < 6 && !(pg == 1 || pg == 2)) { g_page[S_MASTER] = (pg + 1) % 6; g_focus[S_MASTER] = 0; dirty(); return; }
    if (pg == 1) { if (slot < 8) { P().mute[slot] = !P().mute[slot]; g_focus[S_MASTER] = slot; } }
    else if (pg == 2) { if (slot < 8) { P().trackPan[slot] = 0; g_focus[S_MASTER] = slot; } }
    else if (pg == 4 && slot <= 1) { Master& m = P().master; if (slot == 0) m.delPing = !m.delPing; else { m.delSync = !m.delSync; m.delTime = clampi(m.delTime, 0, m.delSync ? 13 : 28); } g_focus[S_MASTER] = slot; }
    else g_focus[S_MASTER] = slot;
    dirty();
}
void maDraw()
{
    Item it[8]; int pages; maItems(it, pages); const int pg = g_page[S_MASTER];
    titleBar(kMTitle[pg], nullptr);
    if (pg == 1 || pg == 2) { for (int i = 0; i < 8; i++) { const int r = R(S_MASTER, "label[%d]", i); ui::label(r, it[i].name, 0, it[i].val, 0, ui::LN_GREEN); } }
    else bottom(S_MASTER, it, pages, pg);
    if (pg == 1 || pg == 2) { for (int i = 0; i < 8; i++) { char n[16]; snprintf(n, sizeof n, "Track %d", i + 1); ui::label(R(S_MASTER, "trackNameLabel[%d]", i), n, 0); ui::bar(R(S_MASTER, "trackVolumeBar[%d]", i), pg == 1 ? P().trackVol[i] : (P().trackPan[i] + 50)); } }
    for (int i = 0; i < 8; i++) { if (pg == 1) {} else if (it[i].bar >= 0 && pg != 2) ui::bar(R(S_MASTER, "barControl[%d]", i), it[i].bar); else if (it[i].bar == -2) ui::bar(R(S_MASTER, "barControl[%d]", i), 0, true); }
    if (pg == 0) { static const char* n[5]; for (int i = 0; i < 5; i++) n[i] = kSat[i]; ui::list(R(S_MASTER, "saturationList"), 5, n, nullptr, clampi(P().master.saturation, 0, 4), 0); }
    if (pg == 4)
    {
        static const char* oo[2] = { "On", "Off" }; Master& m = P().master;
        ui::list(R(S_MASTER, "delayPingpongEnableList"), 2, oo, nullptr, m.delPing ? 0 : 1, 0); ui::list(R(S_MASTER, "delaySyncEnableList"), 2, oo, nullptr, m.delSync ? 0 : 1, 0);
        static const char* rt[14]; for (int i = 0; i < 14; i++) rt[i] = kRates[i]; if (m.delSync) ui::list(R(S_MASTER, "delaySyncRateList"), 14, rt, nullptr, clampi(m.delTime, 0, 13), 0);
    }
    ui::bg(R(S_MASTER, "bgLabel")); ui::frame(R(S_MASTER, "frameControl"), std::min(g_focus[S_MASTER], 7));
}

/* ------------------------------------------------------------------ performance, recorder, images */
const char* const kPfName[PF_COUNT][2] = { { "", "" }, { "Volume", "vel" }, { "Pan", "" }, { "Tune", "" }, { "LP", "cutoff" }, { "HP", "cutoff" }, { "BP", "cutoff" }, { "Delay", "send" }, { "Reverb", "send" },
    { "Sample", "start" }, { "Sample", "end" }, { "Play", "dir" }, { "Step", "repeat" }, { "Bit", "depth" }, { "Over", "drive" } };
const int kPfMin[PF_COUNT] = { 0, -100, -100, -48, -100, -100, -100, -100, -100, -100, -100, 0, 0, -12, -100 };
const int kPfMax[PF_COUNT] = { 0, 100, 100, 48, 100, 100, 100, 100, 100, 100, 100, 1, 15, 12, 100 };
const char* const kPfRep[16] = { "---", "16", "12", "8", "6", "4", "3", "2", "1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16" };
int pfRowVal(int s, int r) { return (r > 0 && r == g_pf.row[s]) ? g_pf.temp[s] : g_pf.val[s][r]; }
void pfText(int s, int r, char* b, size_t n)
{
    const int fx = g_pf.fx[s]; const int v = r == 0 ? 0 : pfRowVal(s, r);
    if (fx == PF_NONE) snprintf(b, n, "");
    else if (r == 0) snprintf(b, n, "off");
    else if (fx == PF_PLAY) snprintf(b, n, "%s", v ? "<<<" : ">>>");
    else if (fx == PF_REPEAT) snprintf(b, n, "%s", kPfRep[clampi(v, 0, 15)]);
    else if (v == 0) snprintf(b, n, "---"); else snprintf(b, n, "%+d", v);
}
void pfClearSlotFx(int s) { const int fx = g_pf.fx[s]; if (fx) for (int t = 0; t < kTracks; t++) audio::perfClear(t, fx); }
void pfApplySlot(int s)
{
    const int fx = g_pf.fx[s]; if (!fx) return;
    for (int o = 0; o < 12; o++)   /* one slot per effect; the three filters block each other */
    {
        if (o == s) continue; const int ofx = g_pf.fx[o];
        if (ofx == fx || (fx >= PF_LP && fx <= PF_BP && ofx >= PF_LP && ofx <= PF_BP)) { g_pf.row[o] = 0; g_pf.temp[o] = 0; g_pf.active[o] = false; }
    }
    g_pf.temp[s] = g_pf.row[s] > 0 ? g_pf.val[s][g_pf.row[s]] : 0;
    for (int t = 0; t < kTracks; t++) { if (g_pf.trk[t] && g_pf.temp[s] != 0) audio::perfSet(t, fx, g_pf.temp[s]); else audio::perfClear(t, fx); }
}
void pfApplyTrack(int t)
{
    for (int s = 0; s < 12; s++) { const int fx = g_pf.fx[s]; if (!fx) continue; if (g_pf.trk[t] && g_pf.row[s] > 0 && g_pf.temp[s] != 0) audio::perfSet(t, fx, g_pf.temp[s]); else audio::perfClear(t, fx); }
}
void pfPad(int s, int r, bool down)
{
    if (s < 0 || s >= 12) return; dirty();
    if (down) { g_pf.place = s; g_pf.row[s] = g_pf.fx[s] ? (uint8_t)clampi(r, 0, 3) : 0; g_pf.active[s] = true; pfApplySlot(s); }
    else { pfApplySlot(s); g_pf.active[s] = false; }
}
void pfAdjust(int s, int d, bool coarse, bool store)
{
    const int fx = g_pf.fx[s]; if (!fx || s < 0 || s >= 12) return; dirty();
    if (g_pf.row[s] == 0) g_pf.row[s] = 1;
    const int r = g_pf.row[s]; const int step = (coarse && kPfMax[fx] - kPfMin[fx] > 30 ? 5 : 1) * d;
    g_pf.temp[s] = (int16_t)clampi(pfRowVal(s, r) + step, kPfMin[fx], kPfMax[fx]);
    if (store) { g_pf.val[s][r] = g_pf.temp[s]; cfgSave(); }
    for (int t = 0; t < kTracks; t++) { if (g_pf.trk[t] && g_pf.temp[s] != 0) audio::perfSet(t, fx, g_pf.temp[s]); else audio::perfClear(t, fx); }
}
void pfChangeFx(int s, int d)
{
    pfClearSlotFx(s);
    int fx = (int)g_pf.fx[s] + d; if (fx >= PF_COUNT) fx = 0; if (fx < 0) fx = PF_COUNT - 1;
    g_pf.fx[s] = (uint8_t)fx; for (int r = 0; r < 4; r++) g_pf.val[s][r] = 0; g_pf.row[s] = 0; g_pf.temp[s] = 0; g_pf.active[s] = false; cfgSave(); dirty();
}
void pfWheel(int d, bool coarse)
{
    if (g_pf.edit) { pfAdjust(g_pf.place, d, coarse, true); return; }
    for (int s = 0; s < 12; s++) if (g_pf.active[s]) pfAdjust(s, d, coarse, false);
}
void pfTrackTap(int t)
{
    dirty();
    if (g_shift) { P().mute[t] = !P().mute[t]; if (P().mute[t]) audio::noteOff(t); return; }
    g_pf.trk[t] = !g_pf.trk[t]; pfApplyTrack(t);
}
void pfItems(Item* it, int& pages)
{
    pages = 1;
    for (int i = 0; i < 8; i++) { char n[16]; snprintf(n, sizeof n, "Track %d", i + 1); setI(it[i], n, "%s", ""); if (P().mute[i]) snprintf(it[i].val, sizeof it[i].val, "MUTED"); else snprintf(it[i].val, sizeof it[i].val, "Patt: %d", g_pat + 1); it[i].act = g_pf.trk[i]; it[i].off = P().mute[i] != 0; }
}
void pfPress(int slot) { pfTrackTap(slot); }
void pfDraw()
{
    Item it[8]; int pages; pfItems(it, pages); titleBar("Performance FX", g_pf.edit ? "Edit" : nullptr); bottom(S_PERF, it, 1, 0, -1);
    ui::bg(R(S_PERF, "bgLabel"));
    for (int s = 0; s < 12; s++)
    {
        const int fx = g_pf.fx[s];
        ui::label(R(S_PERF, "fxName[%d]", s), kPfName[fx][0], (g_pf.edit && g_pf.place == s) ? 1 : 0, kPfName[fx][1], 0, 0);
        static char tx[12][4][12]; const char* p[4]; for (int r = 0; r < 4; r++) { pfText(s, r, tx[s][r], sizeof tx[s][r]); p[r] = tx[s][r]; }
        ui::list(R(S_PERF, "col[%d]", s), fx ? 4 : 0, p, nullptr, g_pf.row[s], 0);
    }
}
void srDraw()
{
    Item it[8]; for (int i = 0; i < 8; i++) { it[i].blank = true; } setI(it[0], "Record"); it[0].off = true; setI(it[7], "Back"); it[7].act = true;
    titleBar("Sample Recorder", nullptr); bottom(S_SREC, it, 1, 0, -1);
    static const char* n[2] = { "Line in", "Radio" }; ui::list(R(S_SREC, "sourceListControl"), 2, n, nullptr, 0, 0);
    ui::label(R(S_SREC, "radioRdsLabel"), "Recording is not available in the plugin", 0); ui::bg(R(S_SREC, "bgLabel"));
}
void imDraw()
{
    Item it[8]; for (int i = 0; i < 8; i++) it[i].blank = true; titleBar("Image Viewer", nullptr); bottom(S_IMG, it, 1, 0, -1);
    ui::label(R(S_IMG, "infoTextBox"), "No images", 0); ui::bg(R(S_IMG, "bgLabel"));
}

/* ------------------------------------------------------------------ dispatch */
int g_patBottom = -1, g_lastScr = -1, g_lastEditInst = -1, g_held[64];
void items(Item* it, int& pages)
{
    for (int i = 0; i < 8; i++) it[i] = Item();
    switch (g_scr)
    {
        case S_PAT: patItems(it, pages); break; case S_INSTR: instItems(it, pages); break; case S_SLOAD: sloadItems(it, pages); break;
        case S_SPLAY: spItems(it, pages); break; case S_SEDIT: seItems(it, pages); break; case S_SONG: soItems(it, pages); break;
        case S_PROJ: prItems(it, pages); break; case S_CONF: cfItems(it, pages); break; case S_MASTER: maItems(it, pages); break; case S_PERF: pfItems(it, pages); break;
        default: pages = 1;
    }
}
void adjustScr(int slot, int d, bool coarse)
{
    switch (g_scr)
    {
        case S_PAT: patAdjust(slot, d, coarse); break; case S_INSTR: instAdjust(slot, d, coarse); break; case S_SLOAD: sloadAdjust(slot, d, coarse); break;
        case S_SPLAY: spAdjust(slot, d, coarse); break; case S_SEDIT: seAdjust(slot, d, coarse); break; case S_SONG: soAdjust(slot, d, coarse); break;
        case S_PROJ: prAdjust(slot, d, coarse); break; case S_CONF: cfAdjust(slot, d, coarse); break; case S_MASTER: maAdjust(slot, d, coarse); break;
    }
}
void gotoScreen(int s, int page = -1)
{
    if (g_scr == s && page < 0) { return; }
    if (audio::previewing()) audio::previewSample(nullptr, 0);
    g_scr = s; if (page >= 0) g_page[s] = page; g_patBottom = -1; g_confirm = -1;
    if (s == S_SLOAD) { g_sloadSel = g_inst; g_sloadTop = std::max(0, std::min(g_inst - 6, kInstr - 13)); }
    if (s == S_SEDIT && g_lastEditInst != g_inst) { g_selA = 0; g_selB = 1; g_lastEditInst = g_inst; }
    if (s == S_PROJ) g_projDirty = true;
    dirty();
}
void cursorMove(int dir)   /* 0 up 1 down 2 left 3 right */
{
    dirty();
    switch (g_scr)
    {
        case S_PAT:
            g_patBottom = -1;
            if (dir == 0) goStep(g_step - (g_shift ? 4 : 1)); else if (dir == 1) goStep(g_step + (g_shift ? 4 : 1));
            else if (dir == 2) { if (--g_par < 0) { if (g_trk > 0) { g_trk--; g_par = 3; } else g_par = 0; } }
            else { if (++g_par > 3) { if (g_trk < kTracks - 1) { g_trk++; g_par = 0; } else g_par = 3; } }
            if (g_preview && dir < 2) previewRow();
            break;
        case S_INSTR: if (dir < 2) { g_inst = clampi(g_inst + (dir ? 1 : -1), 0, kInstr - 1); g_page[S_INSTR] = std::min(g_page[S_INSTR], instPages() - 1); } else g_focus[S_INSTR] = clampi(g_focus[S_INSTR] + (dir == 3 ? 1 : -1), 0, 7); break;
        case S_SLOAD:
            if (dir < 2) sloadAdjust(g_focus[S_SLOAD] == 6 ? 0 : 0, dir ? 1 : -1, false);
            else if (dir == 2) { if (g_sloadRegion == 1) g_sloadRegion = 0; else g_br.up(); g_focus[S_SLOAD] = 0; }
            else { if (g_sloadRegion == 0) { const Entry* e = brEntry(g_br.sel); if (e && e->dir) g_br.enter(); else if (!e && brUp()) g_br.up(); else g_sloadRegion = 1; } g_focus[S_SLOAD] = 0; }
            break;
        case S_SPLAY: case S_SEDIT: if (dir < 2) { g_inst = clampi(g_inst + (dir ? 1 : -1), 0, kInstr - 1); g_selA = 0; g_selB = 1; g_slice = 0; g_lastEditInst = g_inst; } else g_focus[g_scr] = clampi(g_focus[g_scr] + (dir == 3 ? 1 : -1), 0, 6); break;
        case S_SONG: if (dir < 2) { g_songSel += dir ? 1 : -1; songClamp(); } else g_focus[S_SONG] = clampi(g_focus[S_SONG] + (dir == 3 ? 1 : -1), 0, 7); break;
        case S_PROJ: if (dir < 2) prAdjust(0, dir ? 1 : -1, false); break;
        case S_PERF: if (!g_pf.edit) break; if (dir == 2) g_pf.place = std::max(0, g_pf.place - 1); else if (dir == 3) g_pf.place = std::min(11, g_pf.place + 1); else pfChangeFx(g_pf.place, dir == 0 ? 1 : -1); break;
        case S_CONF:
            if (dir == 2) g_confRegion = 0; else if (dir == 3) g_confRegion = 1;
            else if (g_confRegion == 0) { g_confBase = clampi(g_confBase + (dir ? 1 : -1), 0, 3); g_confSub = 0; }
            else { const int n = (int)confEntries(g_confBase).size(); g_confSub = clampi(g_confSub + (dir ? 1 : -1), 0, std::max(0, n - 1)); }
            break;
        case S_MASTER: if (dir < 2) g_page[S_MASTER] = ((g_page[S_MASTER] + (dir ? 1 : 5)) % 6); else g_focus[S_MASTER] = clampi(g_focus[S_MASTER] + (dir == 3 ? 1 : -1), 0, 7); break;
    }
}
/* screen menu: a tap on the title bar opens a list of all screens (replaces the separate CONTROLS page) */
const char* const kMenu[12] = { "Pattern", "Instrument", "Sample Load", "Sample Play", "Sample Edit", "Sample Rec", "Song", "Project", "Config", "Master", "Performance", "Images" };
const int kMenuScr[12] = { S_PAT, S_INSTR, S_SLOAD, S_SPLAY, S_SEDIT, S_SREC, S_SONG, S_PROJ, S_CONF, S_MASTER, S_PERF, S_IMG };
void menuOpen() { g_menu = true; g_menuSel = 0; for (int i = 0; i < 12; i++) if (kMenuScr[i] == g_scr) g_menuSel = i; g_menuTop = 0; dirty(); }
void menuMove(int d)
{
    g_menuSel = clampi(g_menuSel + d, 0, 11); const int rows = std::max(1, ui::rows(R(0, "popups.popupListControl")));
    if (g_menuSel < g_menuTop) g_menuTop = g_menuSel; if (g_menuSel >= g_menuTop + rows) g_menuTop = g_menuSel - rows + 1; dirty();
}
void menuPick(int i) { if (i < 0 || i > 11) return; g_menu = false; gotoScreen(kMenuScr[i]); dirty(); }
void menuDraw() { ui::list(R(0, "popups.popupListControl"), 12, kMenu, nullptr, g_menuSel, g_menuTop); }
void doWheel(int d, bool coarse)
{
    if (!d) return; dirty();
    if (g_menu) { menuMove(d); return; }
    if (g_scr == S_PERF) { pfWheel(d > 0 ? 1 : -1, coarse); return; }
    if (g_scr == S_PAT)
    {
        if (g_patBottom >= 0) { patAdjust(g_patBottom, d, coarse); return; }
        if (g_edit) { pushUndo(); editField(d, coarse); if (g_preview && g_par == 0 && cur().note < 128) {} }
        else goStep(g_step + d * (coarse ? 4 : 1));
        return;
    }
    if (g_scr == S_SONG && g_focus[S_SONG] != 5 && g_focus[S_SONG] != 7) { soAdjust(0, d > 0 ? 1 : -1, false); return; }
    int slot = g_focus[g_scr];
    if (g_scr == S_SLOAD && slot != 6) slot = 0;
    if (g_scr == S_PROJ || g_scr == S_CONF) slot = g_scr == S_CONF && g_confRegion == 1 ? 3 : 0;
    adjustScr(slot, d, coarse);
}
void edit_playNote(int note, int vel)
{
    if (g_scr == S_PAT && g_edit && g_cfg.noteRec)
    {
        pushUndo(); Step& s = cur(); s.note = (uint8_t)clampi(note, 0, 127); s.instr = (uint8_t)g_inst; g_lastNote = note;
        if (g_stepJump > 0) goStep(g_step + g_stepJump);
        g_patBottom = -1;
    }
    audio::noteOn(g_trk, note, g_inst, vel); dirty();
}
}   // anon

/* ================================================================== public */
void init(const char* root)
{
    if (g_inited) return;
    g_root = root ? root : "";
    memset(g_rev, 0, sizeof g_rev); memset(g_held, -1, sizeof g_held);
    xxl_scene::init(); ui::init(); grid::init(); waveInit();
    cfgLoad(); g_padOct = g_cfg.padOct; audio::init(); audio::setHostSync(g_cfg.hostSync != 0); audio::setMetronome(g_cfg.metro, g_cfg.metroVol);
    audio::setPreviewVol(g_prevVol); audio::setEditPattern(0);
    xxl::logf("app: init root=%s", g_root.c_str());
    demoBeat();
    g_inited = true; g_lastScr = -1; dirty();
}
void shutdown() { if (!g_inited) return; audio::shutdown(); g_inited = false; }
void setPadMode(int m) { g_padMode = m; }
void tick(int frames)
{
    if (g_toastLeft > 0) { g_toastLeft -= frames; if (g_toastLeft <= 0) dirty(); }
    if (g_confirmLeft > 0) { g_confirmLeft -= frames; if (g_confirmLeft <= 0) g_confirm = -1; }
    g_clock += frames;
}
void padDown(int k, int vel)
{
    if (k < 0 || k > 63) return;
    const int row = k / 8, col = k & 7; const int note = clampi(48 + g_padOct * 12 + (row % 4) * 12 + (row < 4 ? 0 : 4) + col, 0, 127);
    static int n = 0; if (n < 60) { n++; xxl::logf("pad %d -> note %d", k, note); }
    g_held[k] = note; edit_playNote(note, g_cfg.padVel >= 127 ? 127 : std::max(1, vel));
}
void padUp(int k)
{
    if (k < 0 || k > 63 || g_held[k] < 0) return; g_held[k] = -1;
    const Instr& I = inst(); if (I.kind == 1 || (I.used && I.mode >= PM_FWD && I.mode <= PM_PING)) audio::noteOff(g_trk);
}
void midiNote(int note, int vel, bool on)
{
    static int n = 0; if (n < 60) { n++; xxl::logf("midi note %d %d %d", note, vel, (int)on); }
    if (g_scr == S_PERF) { const int idx = note - 48; if (idx >= 0 && idx < 48) pfPad(idx % 12, idx / 12, on && vel > 0); return; }
    if (on && vel > 0) edit_playNote(note, vel); else { const Instr& I = inst(); if (I.kind == 1 || (I.used && I.mode >= PM_FWD && I.mode <= PM_PING)) audio::noteOff(g_trk); }
}
void stopButton() { if (audio::playing()) audio::stop(); else { audio::allOff(); g_step = 0; } dirty(); }
void buttonPress(int n) { if (n == 29) { g_shift = true; dirty(); } }
void buttonRelease(int n) { if (n == 29) { g_shift = false; dirty(); } }
void wheelTab(int s) { if (s >= 1 && s < S_COUNT && s != g_scr) { g_menu = false; gotoScreen(s); } }
void wheel(int c) { doWheel(c > 0 ? 1 : -1, g_shift); if (std::abs(c) > 1) for (int i = 1; i < std::min(std::abs(c), 2); i++) doWheel(c > 0 ? 1 : -1, g_shift); }
void macroLabel(int scr, int idx, char* name, char* val, int len)
{
    name[0] = val[0] = 0; if (idx < 0 || idx > 7 || scr < 1 || scr >= S_COUNT) return;
    const int keep = g_scr; g_scr = scr;
    struct Restore { int k; ~Restore() { g_scr = k; } } restore{ keep };
    Item it[8]; int pages = 1; items(it, pages);
    const char* n = it[idx].blank ? "" : it[idx].name; const char* v = it[idx].blank ? "" : it[idx].val;
    if (g_scr == S_SLOAD) { if (idx == 0) { n = "Samples"; v = g_br.path.empty() ? "Drives" : (g_br.path.rfind('/') != std::string::npos ? g_br.path.c_str() + g_br.path.rfind('/') + 1 : g_br.path.c_str()); } else if (idx == 1) { static std::string in; in = instName(g_inst); n = "Instruments"; v = in.c_str(); } }
    else if (g_scr == S_PROJ) { static const char* pn[8] = { "Projects", "New", "Save", "Save As", "Delete", "Open", "", "" }; n = pn[idx]; v = (idx == 0 && !g_projList.empty()) ? g_projList[clampi(g_projSel, 0, (int)g_projList.size() - 1)].c_str() : ""; }
    else if (g_scr == S_CONF) { if (idx == 0) { n = "Groups"; v = kGroups[std::max(0, std::min(g_confBase, 3))]; } else if (idx == 1) { auto e = confEntries(g_confBase); n = "Setting"; static char sv[40]; snprintf(sv, sizeof sv, "%s", g_confSub < (int)e.size() ? e[g_confSub].name.c_str() : ""); v = sv; } else if (idx == 2) { auto e = confEntries(g_confBase); n = "Value"; static char sv2[40]; snprintf(sv2, sizeof sv2, "%s", g_confSub < (int)e.size() ? e[g_confSub].val.c_str() : ""); v = sv2; } else { n = ""; v = ""; } }
    else if (g_scr == S_PERF) { static char pv[16], nb[32]; const int fx = g_pf.fx[idx]; if (fx) snprintf(nb, sizeof nb, "%s %s", kPfName[fx][0], kPfName[fx][1]); else nb[0] = 0; n = nb; pfText(idx, std::max(1, (int)g_pf.row[idx]), pv, sizeof pv); v = pv; }
    else if (g_scr == S_SREC || g_scr == S_IMG) { n = ""; v = ""; }
    snprintf(name, (size_t)len, "%s", n && *n ? n : "-"); snprintf(val, (size_t)len, "%s", v ? v : "");
}
void bottomPress(int n);
void macro(int b, int c)
{
    const int tabScr = b / 8, idx = b % 8;
    static int logged = 0; if (logged < 200) { logged++; xxl::logf("macro tab %d idx %d %d (screen was %d)", tabScr, idx, c, g_scr); }
    if (tabScr >= 1 && tabScr < S_COUNT && tabScr != g_scr) { g_menu = false; gotoScreen(tabScr); }
    const int d = c > 0 ? 1 : -1; const int n = std::min(std::abs(c), 3); if (idx < 0 || idx > 7) return;
    dirty();
    if (!g_menu)
    {
        /* a bottom-bar button that is an action (Add slot, Play song, Undo, Apply ...): turning its macro presses it, once per turn */
        Item itm[8]; int pgs = 1; items(itm, pgs);
        if (g_scr == S_PROJ) { static const int mp[8] = { -1, 0, 1, 2, 3, 7, -1, -1 }; if (idx > 0 && mp[idx] >= 0) { prPress(mp[idx]); return; } }
        else if ((g_scr == S_SONG || g_scr == S_SEDIT || g_scr == S_SPLAY || g_scr == S_SLOAD) && !((g_scr == S_SEDIT || g_scr == S_SPLAY) && idx == 6) && !(g_scr == S_SLOAD && (idx == 0 || idx == 1 || idx == 6)) && !itm[idx].blank && itm[idx].act && !itm[idx].val[0]) { bottomPress(idx); return; }
    }
    for (int i = 0; i < n; i++)
    {
        if (g_menu) { menuMove(d); continue; }
        switch (g_scr)
        {
            case S_PAT: if (idx == 7) { g_page[S_PAT] = (g_page[S_PAT] + d + 3) % 3; g_patBottom = -1; } else { g_patBottom = idx; patAdjust(idx, d, g_shift); } break;
            case S_SLOAD: if (idx == 0) { g_sloadRegion = 0; sloadAdjust(0, d, g_shift); } else if (idx == 1) { g_sloadRegion = 1; sloadAdjust(0, d, g_shift); } else if (idx == 6) sloadAdjust(6, d, g_shift); break;
            case S_SONG: soAdjust(idx, d, g_shift); break;
            case S_PROJ: prAdjust(0, d, g_shift); break;
            case S_CONF: if (idx == 0) { g_confRegion = 0; cfAdjust(0, d, g_shift); } else if (idx == 1) { const int ne = (int)confEntries(g_confBase).size(); g_confRegion = 1; g_confSub = clampi(g_confSub + d, 0, std::max(0, ne - 1)); dirty(); } else if (idx == 2) { g_confRegion = 1; cfAdjust(3, d, g_shift); } break;
            case S_PERF: pfAdjust(idx, d, g_shift, true); g_pf.place = idx; break;
            case S_SREC: case S_IMG: break;
            default: g_focus[g_scr] = idx; adjustScr(idx, d, g_shift); break;
        }
    }
}
void knob(int c, bool coarse) { for (int i = 0; i < std::min(std::abs(c), 3); i++) doWheel(c > 0 ? 1 : -1, coarse); }
void bottomPress(int n)
{
    xxl::logf("bottom %d on screen %d", n, g_scr);
    if (g_scr == S_PAT) { g_patBottom = n; patPress(n); return; }
    if (g_scr == S_PERF) { pfPress(n); return; }
    if (g_scr == S_SREC) { if (n == 7) gotoScreen(S_PAT); return; }
    g_focus[g_scr] = n;
    switch (g_scr)
    {
        case S_INSTR: instPress(n); break; case S_SLOAD: sloadPress(n); break; case S_SPLAY: spPress(n); break; case S_SEDIT: sePress(n); break;
        case S_SONG: soPress(n); break; case S_PROJ: prPress(n); break; case S_CONF: cfPress(n); break; case S_MASTER: maPress(n); break;
    }
}
void wheelEnter()
{
    dirty();
    switch (g_scr)
    {
        case S_SLOAD: sloadEnterOrLoad(false); break;
        case S_PROJ: if (!g_projList.empty()) projOpen(g_projList[g_projSel]); break;
        case S_CONF: cursorMove(3); break;
        case S_INSTR: case S_SPLAY: case S_SEDIT: case S_SONG: case S_MASTER: cursorMove(3); break;
        case S_PERF: g_pf.edit = !g_pf.edit; break;
        case S_PAT: g_edit = !g_edit; g_patBottom = -1; break;
    }
}
void buttonTap(int n)
{
    if (n >= 200) { const int s = n - 200; if (s >= 1 && s < S_COUNT && s != g_scr) { g_menu = false; gotoScreen(s); } wheelEnter(); dirty(); return; }
    if (n >= 100) { const int s = (n - 100) / 8, i = (n - 100) % 8; if (s >= 1 && s < S_COUNT && s != g_scr) { g_menu = false; gotoScreen(s); } if (!g_menu) bottomPress(i); dirty(); return; }
    static int logged = 0; if (logged < 80) { logged++; xxl::logf("btn %d screen %d", n, g_scr); }
    dirty();
    if (n == 33) { if (g_menu) g_menu = false; else menuOpen(); return; }
    if (n == 34) { wheelEnter(); return; }   /* the data wheel pressed */
    if (g_menu) { if (n >= 8 && n <= 22) g_menu = false; else if (n == 26) { menuMove(-1); return; } else if (n == 31) { menuMove(1); return; } else if (n == 28 || n == 32 || n == 23) { menuPick(g_menuSel); return; } else if (n == 30) { g_menu = false; return; } else if (n < 8) return; }
    if (n >= 0 && n < 8) { bottomPress(n); return; }
    switch (n)
    {
        case 8: g_page[S_INSTR] = std::min(1, instPages() - 1); g_focus[S_INSTR] = 0; gotoScreen(S_INSTR, g_page[S_INSTR]); break;
        case 9: gotoScreen(S_PERF); break;
        case 10: gotoScreen(S_PROJ); break;
        case 11: gotoScreen(S_CONF); break;
        case 12: gotoScreen(S_MASTER, 0); break;
        case 13: gotoScreen(S_SLOAD); break;
        case 14: gotoScreen(S_SPLAY); break;
        case 15: gotoScreen(S_SEDIT); break;
        case 16: break;   /* the recorder is not part of the plugin */
        case 17: gotoScreen(S_SONG); songClamp(); break;
        case 18: g_par = 0; gotoScreen(S_PAT); g_scr = S_PAT; break;
        case 19: gotoScreen(S_INSTR, 0); break;
        case 20: gotoScreen(S_MASTER, 1); break;
        case 21: gotoScreen(S_MASTER, 3); break;
        case 22: gotoScreen(S_PAT); break;
        case 23: playToggle(g_shift); break;
        case 24: if (g_scr == S_PAT) { if (g_shift) { if (g_clipHas) { pushUndo(); cur() = g_clipStep; goStep(g_step + g_stepJump); } } else { g_clipStep = cur(); g_clipHas = true; toast("Step copied"); } } break;
        case 25: if (g_scr == S_PAT) { pushUndo(); if (g_par >= 2) cycleFx(g_shift ? -1 : 1); else { for (int s = pat().len - 1; s > g_step; s--) pat().s[g_trk][s] = pat().s[g_trk][s - 1]; cur() = Step(); } } break;
        case 26: cursorMove(0); break;
        case 27:
            if (g_scr == S_PAT) { pushUndo(); Step& s = cur(); if (g_shift) s = Step(); else if (g_par == 0) s.note = N_NONE; else if (g_par == 1) s.instr = 255; else { s.fx[g_par - 2] = 0; s.fv[g_par - 2] = 0; } }
            else if (g_scr == S_SLOAD && g_sloadRegion == 1) { slotClear(g_sloadSel); }
            break;
        case 28: if (g_scr == S_PERF) g_pf.edit = !g_pf.edit; else { g_edit = !g_edit; g_patBottom = -1; } break;
        case 30: cursorMove(2); break; case 31: cursorMove(1); break; case 32: cursorMove(3); break;
    }
}


/* a tapped list row: select it; a second tap on the selected row opens it */
void listTap(const char* name, int idx)
{
    const std::string n = name; dirty();
    if (n.rfind("performanceMode.col[", 0) == 0) { const int s = atoi(n.c_str() + 20); gotoScreen(S_PERF); pfPad(s, idx, true); pfPad(s, idx, false); return; }
    if (n.find(".popups.popupListControl") != std::string::npos)
    {
        if (g_menu) { const int i = g_menuTop + idx; if (i == g_menuSel) menuPick(i); else { g_menuSel = clampi(i, 0, 11); } }
        else if (g_scr == S_SONG) { g_songSel = clampi(g_songTop + idx, 0, P().songLen - 1); }
        return;
    }
    if (n == "sampleImporter.explorerListControl")
    {
        const int i = clampi(g_br.top + idx, 0, std::max(0, brCount() - 1)); g_sloadRegion = 0; g_focus[S_SLOAD] = 0;
        if (i != g_br.sel) { g_br.sel = i; return; }
        const Entry* e = brEntry(g_br.sel);
        if (e && e->dir) g_br.enter(); else if (!e && brUp()) g_br.up(); else if (e) sloadEnterOrLoad(false);
        return;
    }
    if (n == "sampleImporter.instrumentListControl") { g_sloadRegion = 1; g_sloadSel = clampi(g_sloadTop + idx, 0, kInstr - 1); g_inst = g_sloadSel; return; }
    if (n == "projectEditor.fileListControl")
    {
        const int i = clampi(g_projTop + idx, 0, std::max(0, (int)g_projList.size() - 1));
        if (i == g_projSel && !g_projList.empty()) projOpen(g_projList[g_projSel]); else g_projSel = i;
        return;
    }
    if (n == "configEditor.configSubmenuListControl" || n == "configEditor.configListControl")
    {
        auto e = confEntries(g_confBase); const int i = clampi(idx, 0, std::max(0, (int)e.size() - 1)); g_confRegion = 1;
        if (i == g_confSub && i < (int)e.size() && e[i].adj) { if (!e[i].choices.empty()) e[i].adj(e[i].cur ? -1 : 1); else cfAdjust(3, 1, false); cfgSave(); } else g_confSub = i;
        return;
    }
    if (n == "projectEditor.recentListControl") { if (idx >= 0 && idx < (int)g_recent.size()) projOpen(g_recent[idx]); return; }
    if (n == "samplePlayback.playModeListControl") { P().ins[g_inst].mode = clampi(idx, 0, 7); g_focus[S_SPLAY] = 0; return; }
    if (n == "sampleEditor.effectList") { g_effect = clampi(idx, 0, 10); return; }
    if (n == "masterParams.saturationList") { P().master.saturation = clampi(idx, 0, 4); return; }
    if (n == "masterParams.delayPingpongEnableList") { P().master.delPing = idx == 0; return; }
    if (n == "masterParams.delaySyncEnableList") { Master& m = P().master; m.delSync = idx == 0; m.delTime = clampi(m.delTime, 0, m.delSync ? 13 : 28); return; }
    if (n == "masterParams.delaySyncRateList") { P().master.delTime = clampi(idx, 0, 13); return; }
    if (n == "instrumentEditor.filterModeListControl") { P().ins[g_inst].ftype = clampi(idx, 0, 3); return; }
    if (n == "instrumentEditor.list1Control")
    {
        Instr& I = P().ins[g_inst]; const bool voices = g_focus[S_INSTR] == 0; const int cnt = voices ? tv_voice_count() : audio::patchCount(I.voice);
        const int rows = ui::rows(ui::row("instrumentEditor.list1Control")); const int sel = voices ? I.voice : I.starter; const int top = std::max(0, std::min(sel - rows / 2, std::max(0, cnt - rows)));
        const int i = clampi(top + idx, 0, std::max(0, cnt - 1));
        if (voices) setSynthVoice(I, i); else { I.starter = i; audio::loadPatch(I); audio::refreshInstrument(g_inst); }
        return;
    }
    if (n == "configEditor.configBasemenuListControl") { g_confRegion = 0; g_confBase = clampi(idx, 0, 3); g_confSub = 0; return; }
}
void frame()
{
    if (!g_inited) return;
    /* touch */
    ui::ListTap lt; while (ui::popListTap(lt)) { const int m = ui::module(lt.row); if (m >= 1 && m < S_COUNT && m != g_scr && m != S_PERF) gotoScreen(m); listTap(ui::ctlName(lt.row), lt.index); }
    int ti, tv_; while (ui::popOtherTap(ti, tv_))
    {
        int row, trk, par; grid::View v; v.first = g_trk >= 4 ? 4 : 0;
        if (g_scr == S_PAT && grid::decodeTap(ti, tv_, v, row, trk, par)) { g_patBottom = -1; g_step = ((g_step + (row - 7)) % stepLen() + stepLen()) % stepLen(); g_trk = clampi(trk, 0, kTracks - 1); g_par = par; dirty(); }
    }
    /* follow the play position */
    int pc; if (audio::popPatternChange(pc) && audio::songMode() && audio::playing()) { g_pat = pc; }
    if (audio::playing() && g_scr == S_PAT && audio::playPattern() == g_pat) g_step = audio::playStep() < stepLen() ? audio::playStep() : g_step;
    ui::begin();
    if (g_lastScr != g_scr)
    {
        g_lastScr = g_scr;
    }
    /* every screen is its own tab: all of them are drawn each frame, the current one last (it owns the shared waveform) */
    {
        const int cur = g_scr;
        static const int order[] = { S_INSTR, S_SLOAD, S_SPLAY, S_SEDIT, S_SONG, S_PROJ, S_CONF, S_MASTER, S_PERF, S_PAT };
        for (int k = 0; k < 11; k++)
        {
            const int s = k < 10 ? order[k] : cur; if (k < 10 && s == cur) continue;
            g_scr = s; g_drawMi = s;
            switch (s)
            {
                case S_PAT: { Item it[8]; int pages; patItems(it, pages); titleBar((std::string("Pattern ") + std::to_string(g_pat + 1)).c_str(), instName(g_inst).c_str());
                    bottom(S_PAT, it, pages, g_page[S_PAT]); ui::bg(R(S_PAT, "bgLabel")); if (g_patBottom >= 0 && g_patBottom < 7) ui::frame(R(S_PAT, "frameControl"), g_patBottom); patDraw(); break; }
                case S_INSTR: instDraw(); break; case S_SLOAD: sloadDraw(); break; case S_SPLAY: spDraw(); break; case S_SEDIT: seDraw(); break; case S_SONG: soDraw(); break;
                case S_PROJ: prDraw(); break; case S_CONF: cfDraw(); break; case S_MASTER: maDraw(); break; case S_PERF: pfDraw(); break;  
            }
        }
        g_scr = cur; g_drawMi = cur;
    }
    if (g_menu) menuDraw();
    ui::end();
    g_dirty = false;
}
}
