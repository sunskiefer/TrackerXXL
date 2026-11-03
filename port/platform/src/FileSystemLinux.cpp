/* TrackerXXL: FileSystem on a folder (the "SD card" is <root>, e.g. /sdcard/TrackerXXL with Projects/ Samples/ Themes/ Fonts/).
 * Behaviour follows the Tracker+ FatFs implementation (platforms/imx/source/FileSystemImpl.cpp + sdmmc/sd/SD.cpp):
 *  - paths are card paths ("/Projects/a/b.mt"), mapped under <root>; lookup falls back to a case-insensitive match (FAT is
 *    case-insensitive, ext4 is not);
 *  - FAT has no "." / ".." entries and a HIDDEN attribute: names starting with '.' and folders created with mkdir(hidden=1)
 *    (recorded in <root>/.xxl_hidden) are hidden;
 *  - directory order is made deterministic (case-insensitive name order) because the lists are paged by skip_count;
 *  - fast-seek / async sector reads (SD-card DMA streaming) are not available: enableFastSeek() = false, the core then uses
 *    its synchronous path (VoiceStreamManager: hasValidClmt = false). */
#include "xxl_internal.h"
#include "util_string.h"

#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <set>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace xxl
{
namespace
{
struct Ent
{
    std::string name;
    bool isDir;
    bool hidden;
    uint32_t size;
};

struct File
{
    int fd = -1;
    std::string path;
};

class FileSystemLinux final : public FileSystem
{
  public:
    explicit FileSystemLinux(const std::string& r) : root(r)
    {
        while (root.size() > 1 && root.back() == '/') root.pop_back();
        mkdirs(root);
        for (const char* d : { "Projects", "Samples", "Themes", "Fonts" }) mkdirs(root + "/" + d);
        loadHidden();
    }

    // ---- card ----
    bool isSdCardInserted() override
    {
        struct stat st;
        return stat(root.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    void setMountEventCallback(CardStateChangeCallback f) override { mountCb = f; }

    // ---- paths / basic ops ----
    bool exists(const char* path) override
    {
        struct stat st;
        return statPath(path, st);
    }
    bool rename(const char* o, const char* n) override
    {
        std::string a = resolve(o), b = resolve(n, true);
        return ::rename(a.c_str(), b.c_str()) == 0;
    }
    bool mkdir(uint8_t hidden, const char* path, bool) override
    {
        std::string p = resolve(path, true);
        struct stat st;
        if (stat(p.c_str(), &st) == 0) return S_ISDIR(st.st_mode);   /* FR_EXIST is not an error for the core */
        if (!mkdirs(p)) return false;
        if (hidden) setHidden(rel(p), true);
        return true;
    }
    bool remove(const char* path) override
    {
        std::string p = resolve(path);
        struct stat st;
        if (stat(p.c_str(), &st) != 0) return false;
        int r = S_ISDIR(st.st_mode) ? ::rmdir(p.c_str()) : ::unlink(p.c_str());
        if (r == 0) setHidden(rel(p), false);
        return r == 0;
    }
    bool removeDirWithFiles(const char* path) override { return removeTree(resolve(path), true); }
    bool removeOnlyDirFiles(const char* path) override { return removeTree(resolve(path), false); }

    size_t getFileSize(const char* path) override
    {
        struct stat st;
        return (statPath(path, st) && S_ISREG(st.st_mode)) ? (size_t)st.st_size : 0;
    }
    size_t getFileSize(FileHandle h) override
    {
        File* f = (File*)h;
        struct stat st;
        return (f && f->fd >= 0 && fstat(f->fd, &st) == 0) ? (size_t)st.st_size : 0;
    }
    size_t getFileAvailable(FileHandle h) override
    {
        File* f = (File*)h;
        if (!f || f->fd < 0) return 0;
        struct stat st;
        if (fstat(f->fd, &st) != 0) return 0;
        off_t pos = lseek(f->fd, 0, SEEK_CUR);
        return (pos >= 0 && st.st_size > pos) ? (size_t)(st.st_size - pos) : 0;
    }

    // ---- file I/O ----
    std::tuple<bool, FileHandle> openFile(const char* filepath, FileOpenMode mode, FileHandle* handle) override
    {
        File* f;
        bool allocated = false;
        if (handle == nullptr) { f = new File(); allocated = true; }
        else if (*handle == nullptr) { f = new File(); *handle = f; }
        else { f = (File*)*handle; if (f->fd >= 0) { ::close(f->fd); f->fd = -1; } }

        std::string p = resolve(filepath, mode != kRead);
        int flags;
        switch (mode)
        {
            case kWriteOpen: flags = O_RDWR | O_CREAT; break;
            case kWriteAppend: flags = O_RDWR | O_CREAT; break;
            case kWriteCreateNew: flags = O_RDWR | O_CREAT | O_TRUNC; break;
            default: flags = O_RDONLY; break;
        }
        int fd = ::open(p.c_str(), flags | O_CLOEXEC, 0644);
        if (fd >= 0 && mode == kRead)
        {   /* a directory opens read-only on POSIX; FatFs f_open fails on it */
            struct stat st;
            if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { ::close(fd); fd = -1; }
        }
        if (fd < 0)
        {
            if (allocated) { delete f; f = nullptr; }
            return std::make_tuple(false, (FileHandle)f);
        }
        if (mode == kWriteAppend) lseek(fd, 0, SEEK_END);
        f->fd = fd;
        f->path = p;
        return std::make_tuple(true, (FileHandle)f);
    }
    int32_t readFile(void* dst, size_t len, FileHandle h) override
    {
        File* f = (File*)h;
        if (!f || f->fd < 0) return -1;
        size_t got = 0;
        while (got < len)
        {
            ssize_t n = ::read(f->fd, (char*)dst + got, len - got);
            if (n < 0) { if (errno == EINTR) continue; return -1; }
            if (n == 0) break;
            got += (size_t)n;
        }
        return (int32_t)got;
    }
    int32_t writeFile(const void* data, size_t len, FileHandle h) override
    {
        File* f = (File*)h;
        if (!f || f->fd < 0) return -1;
        size_t put = 0;
        while (put < len)
        {
            ssize_t n = ::write(f->fd, (const char*)data + put, len - put);
            if (n < 0) { if (errno == EINTR) continue; return -1; }
            put += (size_t)n;
        }
        return (int32_t)put;
    }
    bool seekFileCur(int off, FileHandle h) override
    {
        File* f = (File*)h;
        return f && f->fd >= 0 && (off == 0 || lseek(f->fd, off, SEEK_CUR) >= 0);
    }
    bool seekFile(uint32_t pos, FileHandle h) override
    {
        File* f = (File*)h;
        return f && f->fd >= 0 && lseek(f->fd, (off_t)pos, SEEK_SET) >= 0;
    }
    void closeFile(FileHandle h, bool deleteOnClose) override
    {
        File* f = (File*)h;
        if (!f) return;
        if (f->fd >= 0) { ::close(f->fd); f->fd = -1; }
        if (deleteOnClose) delete f;
    }
    void endFile(FileHandle h) override   /* f_truncate: cut the file at the current position */
    {
        File* f = (File*)h;
        if (f && f->fd >= 0)
        {
            off_t pos = lseek(f->fd, 0, SEEK_CUR);
            if (pos >= 0 && ftruncate(f->fd, pos) != 0) {}
        }
    }
    bool isFileOpen(FileHandle h) override { File* f = (File*)h; return f && f->fd >= 0; }

    bool enableFastSeek(FileHandle, uint32_t*, uint32_t, uint32_t* a, uint32_t* b) override
    {
        if (a) *a = 0;
        if (b) *b = 0;
        return false;
    }
    bool readSectorsAsync(uint8_t*, uint32_t, uint32_t) override { return false; }
    bool pollAsyncRead(bool* ok) override { if (ok) *ok = false; return true; }
    bool waitAsyncRead() override { return false; }
    bool isAsyncReadBusy() override { return false; }

    // ---- lists (SdDir equivalents) ----
    int createInstrumentsList(const char* path, char** list, uint8_t list_length, uint32_t* sizeOut) override
    {
        std::vector<Ent> es;
        if (!readDir(path, es)) return kListError;
        int n = 0;
        for (const Ent& e : es)
        {
            if (n >= list_length) break;
            if (e.hidden || e.isDir) continue;
            const int nameLen = (int)e.name.size();
            if (nameLen < 5 || strcasecmp(e.name.c_str() + nameLen - 4, ".pti") != 0) continue;
            const char* p = e.name.c_str();
            int idx1, idx2 = -1;
            if (*p >= '0' && *p <= '9') idx1 = *p - '0'; else continue;
            p++;
            if (*p >= '0' && *p <= '9') { idx2 = *p - '0'; p++; }
            if (*p != ' ') continue;
            p++;
            if (idx2 >= 0) idx1 = idx1 * 10 + idx2;
            if (idx1 < 1 || idx1 > 48) continue;
            idx1--;
            if (idx1 >= list_length || list[idx1] != nullptr) continue;
            int len = nameLen - (int)(p - e.name.c_str()) - 4;
            if (len < 0) len = 0;
            list[idx1] = new char[len + 1];
            strncpy(list[idx1], p, (size_t)len);
            list[idx1][len] = 0;
            if (sizeOut) *sizeOut += e.size;
            n++;
        }
        return n;
    }

    int countContainedFiles(const char* path) override
    {
        std::vector<Ent> es;
        if (!readDir(path, es)) return 0;
        uint8_t n = 0;   /* uint8_t like the original */
        const size_t dirLen = strlen(path);
        for (const Ent& e : es)
        {
            if (e.hidden) continue;
            if (!e.isDir)
            {
                const size_t l = e.name.size();
                if (l < 5 || strcasecmp(e.name.c_str() + l - 4, ".wav") != 0) continue;
                if (dirLen + l > 253) continue;
            }
            n++;
        }
        return n;
    }

    int createFilesList(const char* path, uint8_t start_line, char** list, uint8_t list_length, uint16_t max_used_memory,
                        enListFileFilter filter, uint16_t skip_count, uint16_t max_files_in_subdir,
                        enListFileFilter folderSizeFilter) override
    {
        std::vector<Ent> es;
        if (!readDir(path, es)) return kListError;
        for (uint8_t i = 0; i < list_length; i++)
            if (list[i] != nullptr) { delete list[i]; list[i] = nullptr; }
        uint8_t n = start_line;
        if (max_used_memory < 255) return n;
        uint16_t used = 0;
        const size_t dirLen = strlen(path);
        for (const Ent& e : es)
        {
            if (n >= list_length) break;
            if (used > max_used_memory - 255) break;
            if (!filterEntry(e, filter, dirLen)) continue;
            if (skip_count > 0) { skip_count--; continue; }
            uint16_t len = (uint16_t)e.name.size() + (e.isDir ? 2 : 1);
            list[n] = new char[len];
            used += len;
            if (e.isDir)
            {
                strcpy(list[n], "/");
                strcat(list[n], e.name.c_str());
                int16_t files = (int16_t)countInSub(std::string(path) + "/" + e.name, folderSizeFilter);
                if (max_files_in_subdir != 0 && files > max_files_in_subdir)
                {
                    uint16_t offset = 0;
                    len += 4 + 2 * 5;
                    while (files > 0 && n < list_length - 1)
                    {
                        if (list[n] != nullptr) { delete list[n]; used -= len; }
                        list[n] = new char[len];
                        used += len;
                        snprintf(list[n], len, "/%s (%d-%d)", e.name.c_str(), offset + 1, offset + max_files_in_subdir);
                        files -= max_files_in_subdir;
                        n++;
                        offset += max_files_in_subdir;
                    }
                    n--;
                }
            }
            else strcpy(list[n], e.name.c_str());
            n++;
        }
        return n;
    }

    void listDirContents(const char* dirpath, DirContentType type, std::function<bool(const char*)> nameFilter, char* namesBuffer,
                         int namesBufferSize, int maxNameLen, int& count, char** fileList) override
    {
        std::vector<Ent> es;
        if (!readDir(dirpath, es)) { count = 0; return; }
        int pos = 0;
        for (const Ent& e : es)
        {
            if (pos >= count) break;
            if (e.hidden) continue;
            int nameLen;
            if (type == kFolders)
            {
                if (!e.isDir || e.name == "__MACOSX") continue;
                if (nameFilter && !nameFilter(e.name.c_str())) continue;
                nameLen = (int)e.name.size() + 2;
            }
            else if (type == kFiles)
            {
                if (e.isDir || isMacMeta(e.name)) continue;
                if (nameFilter && !nameFilter(e.name.c_str())) continue;
                nameLen = (int)e.name.size() + 1;
            }
            else continue;
            if (nameLen > namesBufferSize) break;
            if (nameLen > maxNameLen) continue;
            fileList[pos] = namesBuffer;
            if (e.isDir) { *namesBuffer = '/'; namesBuffer++; }
            polystrncpy(namesBuffer, e.name.c_str(), maxNameLen + 1);
            namesBuffer += nameLen;
            namesBufferSize -= nameLen;
            pos++;
        }
        count = pos;
        std::sort(fileList, fileList + count, [](const char* a, const char* b) { return compareStrings(a, b); });
    }

    int readNbFiles(const char* romPath) override
    {
        std::vector<Ent> es;
        if (!readDir(romPath, es)) return 0;
        return (int)es.size();   /* FAT lists neither "." nor ".."; hidden entries are counted (readItem does not filter) */
    }

    bool nesDir_open(const char* path) override
    {
        nes.clear();
        nesPos = 0;
        return readDir(path, nes);
    }
    bool nesDir_readItem(char* out, uint8_t* isDir, uint8_t* isHid, uint32_t* size) override
    {
        if (nesPos >= nes.size()) return false;
        const Ent& e = nes[nesPos++];
        *isDir = e.isDir;
        *isHid = e.hidden;
        if (size) *size = e.size;
        strcpy(out, e.name.c_str());
        return true;
    }
    void nesDir_close() override { nes.clear(); nesPos = 0; }

  private:
    std::string root;
    CardStateChangeCallback mountCb = nullptr;
    std::set<std::string> hiddenSet;
    std::vector<Ent> nes;
    size_t nesPos = 0;

    // ---- helpers ----
    static bool mkdirs(const std::string& p)
    {
        if (p.empty()) return false;
        std::string cur;
        size_t i = 0;
        while (i <= p.size())
        {
            size_t j = p.find('/', i);
            if (j == std::string::npos) j = p.size();
            cur = p.substr(0, j);
            if (!cur.empty())
            {
                struct stat st;
                if (stat(cur.c_str(), &st) != 0 && ::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
            }
            i = j + 1;
        }
        struct stat st;
        return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    static bool isMacMeta(const std::string& n) { return n.size() >= 2 && n[0] == '.' && n[1] == '_'; }

    std::string rel(const std::string& full) const
    {
        return full.compare(0, root.size(), root) == 0 ? full.substr(root.size()) : full;
    }

    /* card path -> host path. With forWrite the last component may not exist yet (parent matched case-insensitively). */
    std::string resolve(const char* path, bool forWrite = false) const
    {
        std::string p = path ? path : "";
        for (auto& ch : p) if (ch == '\\') ch = '/';   /* Synth::copyWorkspacePatches builds backslash paths */
        std::string cur = root;
        size_t i = 0;
        while (i < p.size())
        {
            while (i < p.size() && p[i] == '/') i++;
            size_t j = p.find('/', i);
            if (j == std::string::npos) j = p.size();
            if (j == i) break;
            std::string comp = p.substr(i, j - i);
            i = j;
            if (comp == ".") continue;
            if (comp == "..")   /* never above the card root */
            {
                if (cur.size() > root.size()) cur.erase(cur.rfind('/'));
                continue;
            }
            std::string cand = cur + "/" + comp;
            struct stat st;
            if (stat(cand.c_str(), &st) != 0)
            {
                std::string hit;
                if (DIR* d = opendir(cur.c_str()))
                {
                    while (dirent* de = readdir(d))
                        if (strcasecmp(de->d_name, comp.c_str()) == 0) { hit = de->d_name; break; }
                    closedir(d);
                }
                if (!hit.empty()) cand = cur + "/" + hit;
            }
            cur = cand;
        }
        (void)forWrite;
        return cur;
    }
    bool statPath(const char* path, struct stat& st) const
    {
        std::string p = resolve(path);
        return stat(p.c_str(), &st) == 0;
    }

    void loadHidden()
    {
        FILE* f = fopen((root + "/.xxl_hidden").c_str(), "r");
        if (!f) return;
        char line[512];
        while (fgets(line, sizeof line, f))
        {
            size_t l = strlen(line);
            while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
            if (l) hiddenSet.insert(line);
        }
        fclose(f);
    }
    void saveHidden()
    {
        FILE* f = fopen((root + "/.xxl_hidden").c_str(), "w");
        if (!f) return;
        for (const std::string& s : hiddenSet) fprintf(f, "%s\n", s.c_str());
        fclose(f);
    }
    void setHidden(const std::string& relPath, bool on)
    {
        bool changed = on ? hiddenSet.insert(relPath).second : hiddenSet.erase(relPath) > 0;
        if (changed) saveHidden();
    }

    bool readDir(const char* path, std::vector<Ent>& out) const
    {
        const std::string full = resolve(path);
        DIR* d = opendir(full.c_str());
        if (!d) return false;
        const std::string relDir = rel(full);
        while (dirent* de = readdir(d))
        {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            struct stat st;
            const std::string child = full + "/" + de->d_name;
            if (stat(child.c_str(), &st) != 0) continue;
            Ent e;
            e.name = de->d_name;
            e.isDir = S_ISDIR(st.st_mode);
            e.size = (uint32_t)st.st_size;
            e.hidden = de->d_name[0] == '.' || hiddenSet.count(relDir + "/" + de->d_name) > 0;
            out.push_back(e);
        }
        closedir(d);
        std::sort(out.begin(), out.end(), [](const Ent& a, const Ent& b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
        return true;
    }

    int countInSub(const std::string& path, enListFileFilter filter) const
    {
        std::vector<Ent> es;
        if (!readDir(path.c_str(), es)) return 0;
        int c = 0;
        for (const Ent& e : es)
            if (filterEntry(e, filter, path.size())) c++;
        return c;
    }

    static bool endsWithNoCase(const std::string& s, const char* suf)
    {
        size_t l = strlen(suf);
        return s.size() >= l && strcasecmp(s.c_str() + s.size() - l, suf) == 0;
    }

    /* SdDir::filterFileEntry */
    bool filterEntry(const Ent& e, enListFileFilter filter, size_t dirLen) const
    {
        if (e.hidden) return false;
        if (e.isDir && filter != enListFileFilter_folder && filter != enListFileFilter_all) return false;
        switch (filter)
        {
            case enListFileFilter_all: return true;
            case enListFileFilter_folder: return e.isDir && e.name != "__MACOSX";
            case enListFileFilter_wave_inst:
            case enListFileFilter_wave:
            {
                if (e.name.size() < 5) return false;
                if (!endsWithNoCase(e.name, ".wav"))
                {
                    if (filter != enListFileFilter_wave_inst || !endsWithNoCase(e.name, ".pti")) return false;
                }
                if (dirLen + e.name.size() > 253) return false;
                return !isMacMeta(e.name);
            }
            case enListFileFilter_firmware:   /* firmware images do not apply to the plugin */
                return false;
            case enListFileFilter_mod: return endsWithNoCase(e.name, ".mod") || endsWithNoCase(e.name, ".it");
            case enListFileFilter_game: return !isMacMeta(e.name) && endsWithNoCase(e.name, ".nes");
        }
        return false;
    }

    bool removeTree(const std::string& p, bool removeSelf)
    {
        struct stat st;
        if (lstat(p.c_str(), &st) != 0) return false;
        if (!S_ISDIR(st.st_mode)) return ::unlink(p.c_str()) == 0;
        if (DIR* d = opendir(p.c_str()))
        {
            while (dirent* de = readdir(d))
            {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
                if (!removeTree(p + "/" + de->d_name, true)) { closedir(d); return false; }
            }
            closedir(d);
        }
        else return false;
        if (removeSelf) { setHidden(rel(p), false); return ::rmdir(p.c_str()) == 0; }
        return true;
    }
};
} // namespace

FileSystem* newFileSystem(const std::string& root) { return new FileSystemLinux(root); }
void deleteFileSystem(FileSystem* f) { delete static_cast<FileSystemLinux*>(f); }
} // namespace xxl
