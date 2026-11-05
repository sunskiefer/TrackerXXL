#include "model.h"
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace own
{
std::string g_root;
int g_log = 1;
static Project* g_proj = nullptr;
static Sample g_smp[kInstr];

Project& proj() { if (!g_proj) { g_proj = new Project(); projectInit(*g_proj); } return *g_proj; }
Sample& smp(int i) { return g_smp[i]; }
size_t sampleMemUsed() { size_t n = 0; for (auto& s : g_smp) n += s.bytes(); return n; }
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void projectInit(Project& p)
{
    strcpy(p.name, "Project");
    p.tempo = 130.f; p.swing = 50; p.songLen = 1; memset(p.song, 0, sizeof p.song);
    for (int i = 0; i < kPat; i++) { Pattern& pt = p.pat[i]; pt.len = 16; pt.name[0] = 0; for (auto& t : pt.s) for (auto& s : t) s = Step(); }
    for (auto& in : p.ins) in = Instr();
    for (int t = 0; t < kTracks; t++) { p.mute[t] = 0; p.trackVol[t] = 50; p.trackPan[t] = 0; }
    p.master = Master();
}

/* ---------------------------------------------------------------- note and cell text */
const char* noteName(int n, char* buf)
{
    static const char* nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (n == N_NONE) strcpy(buf, "---");
    else if (n == N_OFF) strcpy(buf, "OFF");
    else if (n == N_CUT) strcpy(buf, "CUT");
    else if (n == N_FADE) strcpy(buf, "FAD");
    else snprintf(buf, 8, "%s%d", nm[n % 12], n / 12 - 2);
    return buf;
}
const char* fxName(int t)
{
    static const char* n[FX_COUNT] = { "None", "Volume", "Panning", "Cutoff", "Resonance", "Tempo", "Offset", "Retrigger", "Reverse", "Chance" };
    return t >= 0 && t < FX_COUNT ? n[t] : "";
}
char fxChar(int t) { static const char c[FX_COUNT + 1] = "-VPCQTsRr%"; return t >= 0 && t < FX_COUNT ? c[t] : '-'; }
int fxMin(int t) { return t == FX_PAN ? -50 : t == FX_TEMPO ? 40 : t == FX_RETRIG ? 1 : 0; }
int fxMax(int t) { return t == FX_TEMPO ? 300 : t == FX_PAN ? 50 : t == FX_RETRIG ? 16 : t == FX_REVERSE ? 1 : 100; }
const char* cellText(const Step& st, int p, char* b)
{
    if (p == 0) return noteName(st.note, b);
    if (p == 1) { if (st.instr == 255) strcpy(b, "--"); else snprintf(b, 8, "%02d", st.instr + 1); return b; }
    const int k = p - 2;
    if (st.fx[k] == FX_NONE) { strcpy(b, "----"); return b; }
    if (st.fx[k] == FX_REVERSE) { strcpy(b, "r<<<"); return b; }
    snprintf(b, 8, "%c%3d", fxChar(st.fx[k]), (int)st.fv[k]);
    return b;
}

/* ---------------------------------------------------------------- WAV */
static uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t* p) { return p[0] | p[1] << 8; }
bool loadWav(const char* path, Sample& out, std::string& err)
{
    FILE* f = fopen(path, "rb");
    if (!f) { err = "cannot open"; return false; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 44 || sz > 200L * 1024 * 1024) { fclose(f); err = "bad size"; return false; }
    std::vector<uint8_t> d((size_t)sz);
    if (fread(d.data(), 1, (size_t)sz, f) != (size_t)sz) { fclose(f); err = "read error"; return false; }
    fclose(f);
    if (memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4)) { err = "not a WAV"; return false; }
    int fmt = 0, ch = 0, bits = 0; uint32_t sr = 0; const uint8_t* data = nullptr; size_t dlen = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size())
    {
        uint32_t len = rd32(&d[pos + 4]);
        const uint8_t* c = &d[pos + 8];
        if (!memcmp(&d[pos], "fmt ", 4) && pos + 8 + 16 <= d.size())
        { fmt = rd16(c); ch = rd16(c + 2); sr = rd32(c + 4); bits = rd16(c + 14); if (fmt == 0xFFFE && len >= 26) fmt = rd16(c + 24); }
        else if (!memcmp(&d[pos], "data", 4)) { data = c; dlen = std::min<size_t>(len, d.size() - pos - 8); break; }
        pos += 8 + len + (len & 1);
    }
    if (!data || ch < 1 || !sr || !bits || (fmt != 1 && fmt != 3)) { err = "unsupported WAV"; return false; }
    const int bps = bits / 8; if (bps < 1 || bps > 4) { err = "unsupported bits"; return false; }
    const size_t frames = dlen / (size_t)(bps * ch);
    if (!frames) { err = "empty"; return false; }
    auto get = [&](size_t i, int c) -> float
    {
        const uint8_t* p = data + (i * ch + c) * bps;
        if (fmt == 3 && bits == 32) { float v; memcpy(&v, p, 4); return v; }
        switch (bps)
        {
            case 1: return ((int)p[0] - 128) / 128.f;
            case 2: return (int16_t)rd16(p) / 32768.f;
            case 3: return (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.f;
            default: return (int32_t)rd32(p) / 2147483648.f;
        }
    };
    const int oc = ch >= 2 ? 2 : 1;
    const double ratio = (double)sr / kSr;
    const size_t of = (size_t)((double)frames / ratio);
    if (!of) { err = "empty"; return false; }
    Sample s; s.ch = oc;
    for (int c = 0; c < oc; c++)
    {
        s.d[c].resize(of);
        for (size_t i = 0; i < of; i++)
        {
            float v;
            if (sr == (uint32_t)kSr) v = get(i, c);
            else { const double x = i * ratio; const size_t i0 = (size_t)x; const double fr = x - i0; const size_t i1 = std::min(i0 + 1, frames - 1); v = (float)(get(i0, c) * (1 - fr) + get(i1, c) * fr); }
            v = v > 1 ? 1 : v < -1 ? -1 : v;
            s.d[c][i] = (int16_t)lrintf(v * 32767.f);
        }
    }
    const char* sl = strrchr(path, '/'); s.name = sl ? sl + 1 : path; s.path = path;
    out = std::move(s);
    return true;
}
bool saveWav(const char* path, const Sample& s)
{
    FILE* f = fopen(path, "wb"); if (!f) return false;
    const uint32_t fr = (uint32_t)s.frames(), ch = (uint32_t)s.ch, db = fr * ch * 2;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); }; auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + db); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(1); w16((uint16_t)ch); w32(kSr); w32(kSr * ch * 2); w16((uint16_t)(ch * 2)); w16(16);
    fwrite("data", 1, 4, f); w32(db);
    for (uint32_t i = 0; i < fr; i++) for (uint32_t c = 0; c < ch; c++) w16((uint16_t)s.d[c][i]);
    fclose(f); return true;
}

/* ---------------------------------------------------------------- folders, project files */
static bool mk(const std::string& p) { struct stat st; if (!stat(p.c_str(), &st)) return true; return !mkdir(p.c_str(), 0755); }
std::string projectsDir() { std::string d = g_root + "/Projects"; mk(g_root); mk(d); return d; }
std::string samplesDir() { std::string d = g_root + "/Samples"; mk(g_root); mk(d); return d; }

static const char kMagic[8] = { 'T', 'X', 'L', 'P', '0', '2', 0, 0 };
bool saveProject(const char* path)
{
    Project& p = proj();
    FILE* f = fopen(path, "wb"); if (!f) return false;
    const uint32_t psz = (uint32_t)sizeof(Project);
    fwrite(kMagic, 1, 8, f); fwrite(&psz, 4, 1, f); fwrite(&p, sizeof p, 1, f);
    for (int i = 0; i < kInstr; i++)
    {
        const Sample& s = g_smp[i];
        const uint32_t n = (uint32_t)s.frames(), ch = (uint32_t)(n ? s.ch : 0), nl = (uint32_t)s.name.size();
        fwrite(&n, 4, 1, f); fwrite(&ch, 4, 1, f); fwrite(&nl, 4, 1, f); fwrite(s.name.data(), 1, nl, f);
        for (uint32_t c = 0; c < ch; c++) fwrite(s.d[c].data(), 2, n, f);
    }
    fclose(f); return true;
}
bool loadProject(const char* path, std::string& err)
{
    FILE* f = fopen(path, "rb"); if (!f) { err = "cannot open"; return false; }
    char m[8]; uint32_t psz = 0;
    if (fread(m, 1, 8, f) != 8 || memcmp(m, kMagic, 8) || fread(&psz, 4, 1, f) != 1 || psz != sizeof(Project)) { fclose(f); err = "not a TrackerXXL project"; return false; }
    Project* np = new Project();
    if (fread(np, sizeof *np, 1, f) != 1) { fclose(f); delete np; err = "short file"; return false; }
    std::vector<Sample> ns(kInstr); bool ok = true;
    for (int i = 0; i < kInstr && ok; i++)
    {
        uint32_t n, ch, nl;
        if (fread(&n, 4, 1, f) != 1 || fread(&ch, 4, 1, f) != 1 || fread(&nl, 4, 1, f) != 1 || nl > 4096 || ch > 2 || n > 100000000u) { ok = false; break; }
        std::string nm(nl, 0); if (nl && fread(&nm[0], 1, nl, f) != nl) { ok = false; break; }
        ns[i].name = nm; ns[i].ch = ch ? (int)ch : 1;
        for (uint32_t c = 0; c < ch; c++) { ns[i].d[c].resize(n); if (n && fread(ns[i].d[c].data(), 2, n, f) != n) { ok = false; break; } }
    }
    fclose(f);
    if (!ok) { delete np; err = "damaged file"; return false; }
    delete g_proj; g_proj = np;
    for (int i = 0; i < kInstr; i++) g_smp[i] = std::move(ns[i]);
    return true;
}

/* ---------------------------------------------------------------- explorer */
int driveList(std::vector<std::string>& out)
{
    static const char* pseudo[] = { "proc", "sysfs", "tmpfs", "devtmpfs", "devpts", "cgroup", "cgroup2", "overlay", "debugfs", "tracefs", "securityfs", "configfs", "pstore", "mqueue", "hugetlbfs", "fusectl", "autofs", "bpf", "ramfs", "rootfs", "squashfs", nullptr };
    out.clear();
    FILE* f = fopen("/proc/mounts", "r"); char line[1024];
    while (f && fgets(line, sizeof line, f) && out.size() < 32)
    {
        char src[256], mp[512], fs[64];
        if (sscanf(line, "%255s %511s %63s", src, mp, fs) != 3) continue;
        char* w = mp;
        for (char* r = mp; *r; r++) { if (r[0] == '\\' && r[1] == '0' && r[2] == '4' && r[3] == '0') { *w++ = ' '; r += 3; } else *w++ = *r; }
        *w = 0;
        bool skip = false; for (int i = 0; pseudo[i]; i++) if (!strcmp(fs, pseudo[i])) skip = true;
        if (skip) continue;
        if (strncmp(mp, "/media/", 7) && strncmp(mp, "/mnt/", 5) && strncmp(mp, "/run/media/", 11) && strncmp(mp, "/sdcard", 7) && strncmp(mp, "/storage/", 9)) continue;
        if (std::find(out.begin(), out.end(), mp) == out.end()) out.push_back(mp);
    }
    if (f) fclose(f);
    struct stat st;
    if (!stat("/sdcard", &st) && S_ISDIR(st.st_mode) && std::find(out.begin(), out.end(), "/sdcard") == out.end()) out.push_back("/sdcard");
    if (!g_root.empty() && std::find(out.begin(), out.end(), g_root) == out.end()) out.push_back(g_root);
    return (int)out.size();
}
static bool isAudio(const std::string& n)
{
    const size_t l = n.size(); if (l < 5) return false;
    return !strcasecmp(n.c_str() + l - 4, ".wav") || !strcasecmp(n.c_str() + l - 5, ".wave");
}
void Browser::scan()
{
    list.clear();
    if (path.empty())
    {
        std::vector<std::string> v; driveList(v);
        for (auto& d : v) { Entry e; e.dir = true; e.path = d; const char* b = strrchr(d.c_str(), '/'); e.name = d == g_root ? "TrackerXXL" : (b && b[1] ? b + 1 : d); list.push_back(e); }
    }
    else if (DIR* d = opendir(path.c_str()))
    {
        while (dirent* de = readdir(d))
        {
            if (de->d_name[0] == '.') continue;
            Entry e; e.name = de->d_name; e.path = path + (path.back() == '/' ? "" : "/") + de->d_name;
            struct stat st; if (stat(e.path.c_str(), &st)) continue;
            e.dir = S_ISDIR(st.st_mode); e.size = (size_t)st.st_size;
            if (e.dir || isAudio(e.name)) list.push_back(e);
        }
        closedir(d);
        std::sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) { if (a.dir != b.dir) return a.dir; return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    }
    if (sel >= (int)list.size()) sel = std::max(0, (int)list.size() - 1);
    top = std::max(0, std::min(top, std::max(0, (int)list.size() - 1)));
}
void Browser::enter()
{
    if (sel < 0 || sel >= (int)list.size() || !list[sel].dir) return;
    path = list[sel].path; sel = 0; top = 0; scan(); remember();
}
void Browser::up()
{
    if (path.empty()) return;
    std::vector<std::string> v; driveList(v);
    std::string p = path; while (p.size() > 1 && p.back() == '/') p.pop_back();
    const std::string from = p;
    if (p == "/" || std::find(v.begin(), v.end(), p) != v.end()) path.clear();
    else { size_t s = p.rfind('/'); path = (s == std::string::npos || s == 0) ? "" : p.substr(0, s); }
    sel = 0; top = 0; scan();
    for (int i = 0; i < (int)list.size(); i++) if (list[i].path == from) { sel = i; break; }
    top = std::max(0, sel - 6); remember();
}
void Browser::remember() const
{
    FILE* f = fopen((g_root + "/browser.ini").c_str(), "w");
    if (f) { fprintf(f, "%s\n", path.c_str()); fclose(f); }
}
void Browser::load()
{
    path.clear();
    FILE* f = fopen((g_root + "/browser.ini").c_str(), "r"); char b[1024];
    if (f && fgets(b, sizeof b, f)) { size_t n = strlen(b); while (n && (b[n - 1] == '\n' || b[n - 1] == '\r')) b[--n] = 0; struct stat st; if (n && !stat(b, &st) && S_ISDIR(st.st_mode)) path = b; }
    if (f) fclose(f);
    if (path.empty()) { std::string s = samplesDir(); struct stat st; if (!stat(s.c_str(), &st)) path = s; }
    scan();
}
}
