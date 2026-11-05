/* model: project data, WAV loading, project files, the file browser. */
#pragma once
#include "own.h"

namespace own
{
extern std::string g_root;                       /* the plugin's data folder (MODULE_DIR) */
Project& proj();
Sample& smp(int i);
size_t sampleMemUsed();
void projectInit(Project& p);                    /* a fresh project: pattern 1 on screen, 8 tracks, 80 = 0 dB mixer */
bool loadWav(const char* path, Sample& out, std::string& err);
bool saveWav(const char* path, const Sample& s);
bool saveProject(const char* path);
bool loadProject(const char* path, std::string& err);
std::string projectsDir();
std::string samplesDir();

/* the explorer: a folder, or (path empty) the list of all drives */
struct Entry { std::string name, path; bool dir = false; size_t size = 0; };
struct Browser
{
    std::string path;                            /* empty = the drives */
    std::vector<Entry> list;
    int sel = 0, top = 0;
    void scan();
    void enter();                                /* the selected folder (or drive) */
    void up();                                   /* one level up; from the top of a drive to all drives */
    void load();                                 /* last folder from browser.ini */
    void remember() const;
};
int driveList(std::vector<std::string>& out);
}
