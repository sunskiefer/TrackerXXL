/* offline test harness (x86, development only, not shipped): drives app/audio like the plugin and dumps the scene for skin/gen/preview.py */
#include "../src/app.h"
#include "../src/audio.h"
#include "../src/model.h"
#include "xxl_scene.h"
#include "scene_layout.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
namespace xxl { void logf(const char* fmt, ...) { va_list ap; va_start(ap, fmt); fprintf(stderr, "[log] "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n"); va_end(ap); } }
static std::vector<int16_t> g_out;
static void blocks(int n)
{
    int16_t b[256];
    for (int i = 0; i < n; i++) { audio::render(b, 128); app::tick(128); g_out.insert(g_out.end(), b, b + 256); if (i % 11 == 10) app::frame(); }
    app::frame();
}
static void dump(const char* path)
{
    FILE* f = fopen(path, "w"); fputs("{", f); bool first = true; char v[128], t[128];
    for (int i = SCN_FIRST; i < SCN_NPARAMS; i++)
    {
        const char* k = SCN_KEYS[i]; size_t n = strlen(k);
        if (n > 2 && !strcmp(k + n - 2, "_t")) continue;
        if (!xxl_scene::get(k, v, sizeof v)) continue;
        const int val = atoi(v); std::string kt = std::string(k) + "_t";
        const bool hasText = xxl_scene::index(kt.c_str()) >= 0;
        if (!val && !hasText) continue;
        std::string tx;
        if (hasText) { std::string kd = kt + "_display"; if (xxl_scene::get(kd.c_str(), t, sizeof t)) tx = t; }
        if (!val && tx.empty()) continue;
        std::string esc; for (char c : tx) { if (c == '"' || c == '\\') esc += '\\'; esc += c; }
        fprintf(f, "%s\"%s\": %s", first ? "" : ",\n", k, hasText ? ("[" + std::to_string(val) + ", \"" + esc + "\"]").c_str() : std::to_string(val).c_str());
        if (hasText) fprintf(f, ",\n\"%s\": [%d, \"%s\"]", kt.c_str(), val, esc.c_str());
        first = false;
    }
    fputs("}\n", f); fclose(f);
}
int main(int argc, char** argv)
{
    const char* root = argc > 1 ? argv[1] : "/tmp/txl_root";
    app::init(root);
    char line[512];
    while (fgets(line, sizeof line, stdin))
    {
        char c[64], a[256] = ""; int n = sscanf(line, "%63s %255[^\n]", c, a); if (n < 1) continue;
        const int x = atoi(a);
        if (!strcmp(c, "tap")) app::buttonTap(x); else if (!strcmp(c, "press")) app::buttonPress(x); else if (!strcmp(c, "release")) app::buttonRelease(x);
        else if (!strcmp(c, "wheel")) app::wheel(x); else if (!strcmp(c, "knob")) app::knob(x, false); else if (!strcmp(c, "pad")) app::padDown(x, 100); else if (!strcmp(c, "padup")) app::padUp(x);
        else if (!strcmp(c, "stop")) app::stopButton(); else if (!strcmp(c, "blocks")) blocks(x ? x : 1);
        else if (!strcmp(c, "frame")) app::frame(); else if (!strcmp(c, "dump")) { app::frame(); dump(a); }
        else if (!strcmp(c, "hostpos")) { double ppq, bpm; int pl, fr; sscanf(a, "%lf %lf %d %d", &ppq, &bpm, &pl, &fr); audio::hostPos(ppq, bpm, pl, fr); }
        else if (!strcmp(c, "peak")) { int16_t mx = 0; for (auto s : g_out) mx = std::max<int16_t>(mx, abs(s)); printf("peak %d samples %zu\n", mx, g_out.size() / 2); fflush(stdout); g_out.clear(); }
        else if (!strcmp(c, "setstep")) { int p, t, s, note, ins; sscanf(a, "%d %d %d %d %d", &p, &t, &s, &note, &ins); own::Step& st = own::proj().pat[p].s[t][s]; st.note = note; st.instr = ins; }
        else if (!strcmp(c, "loadwav")) { int slot; char path[200]; sscanf(a, "%d %199s", &slot, path); own::Sample s; std::string err; if (own::loadWav(path, s, err)) { own::smp(slot) = std::move(s); own::Instr I; I.used = true; snprintf(I.name, sizeof I.name, "%s", own::smp(slot).name.c_str()); own::proj().ins[slot] = I; printf("loaded %d %zu frames\n", slot, own::smp(slot).frames()); } else printf("load failed %s\n", err.c_str()); }
        else if (!strcmp(c, "echo")) { printf("%s\n", a); fflush(stdout); }
    }
    app::shutdown();
    return 0;
}
