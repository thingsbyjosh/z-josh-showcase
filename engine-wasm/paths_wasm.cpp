// paths_wasm.cpp -- PathProvider (F2.A) for the WASM/browser build.
//
// paths.cpp (dll/career/paths.cpp, private tree) resolves paths on real Windows, where the
// narrow (A) file APIs mangle non-ASCII bytes -- that machinery (Widen/Narrow via
// MultiByteToWideChar, the *W Win32 calls) exists ONLY to work around that Windows quirk.
//
// Emscripten's virtual filesystem (MEMFS/IDBFS) is POSIX and UTF-8 throughout: a UTF-8
// std::string is *already* the correct byte sequence for fopen/stat/mkdir. There is no ANSI
// code page to mismatch, so this file is deliberately simpler than the original, not a port of it:
// every disk wrapper below calls the POSIX function directly.
#include "paths.h"

// Every POSIX call below is included from the header POSIX actually specifies for it, not
// pulled in transitively -- MinGW-w64's Windows-compatibility headers expose things like rmdir()
// through non-standard chains (e.g. <io.h>) that a real POSIX/musl sysroot (Emscripten) will not.
//   mkdir, stat, S_ISDIR/S_IFMT/S_IFDIR/S_IFREG -> <sys/stat.h>
//   rmdir                                       -> <unistd.h>
//   opendir/readdir/closedir, struct dirent     -> <dirent.h>
//   fopen, remove, rename                       -> <cstdio> (ISO C, not POSIX-specific)
//   errno                                       -> <cerrno>
//   strcmp                                      -> <cstring>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// mkdir is 2-argument POSIX (path, mode) everywhere this file is meant to run (Emscripten, Linux).
// The one documented exception is native Windows (MSVC's <direct.h> _mkdir/mkdir, 1-argument,
// no permission bits) -- kept here only so this same file compiles as a native smoke test under
// MinGW g++ in Git Bash, the closest local POSIX-ish proxy for Emscripten.
#if defined(_WIN32)
#include <direct.h>
static int PortableMkdir(const std::string& p) { return ::mkdir(p.c_str()); }
#else
static int PortableMkdir(const std::string& p) { return ::mkdir(p.c_str(), 0777); }
#endif

#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif

namespace paths {

// Fixed default roots for the browser build. There is no config.json in the browser: the host
// page (JS boot) mounts the virtual filesystem and decides where each root lives before the
// engine ever touches disk. These four are only the fallback for a build/test that never mounts
// anything else (career_test-style usage without SetRootsForTest).
static const char* kDefaultRoot[4] = {
    "/world",  // Data
    "/save",   // UserData
    "/cache",  // GameInstall
    "/log",    // GameUserDir
};

static std::string g_root[4];
static bool g_init = false;

static std::string TrimTrailingSlash(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

void Init() {
    if (g_init) return;
    for (int i = 0; i < 4; i++) g_root[i] = kDefaultRoot[i];
    g_init = true;
}

void SetRootsForTest(const std::string& data, const std::string& userData,
                     const std::string& gameInstall, const std::string& gameUserDir) {
    const std::string* in[4] = { &data, &userData, &gameInstall, &gameUserDir };
    for (int i = 0; i < 4; i++)
        g_root[i] = in[i]->empty() ? std::string(kDefaultRoot[i]) : TrimTrailingSlash(*in[i]);
    g_init = true;
}

void ResetForTest() {
    for (int i = 0; i < 4; i++) g_root[i].clear();
    g_init = false;
}

const std::string& RootDir(Root r) {
    if (!g_init) Init();
    return g_root[(int)r];
}

std::string Resolve(Root r, const std::string& rel) {
    const std::string& base = RootDir(r);   // ensures Init()
    std::string sub = rel;
    for (char& c : sub) if (c == '\\') c = '/';
    while (!sub.empty() && sub.front() == '/') sub.erase(sub.begin());
    if (sub.empty()) return base;
    return base + "/" + sub;
}

void EnsureParentDir(const std::string& filePath) {
    std::string p = filePath;
    for (char& c : p) if (c == '\\') c = '/';
    for (size_t i = 1; i < p.size(); i++) {
        if (p[i] == '/') {
            std::string dir = p.substr(0, i);
            // Best-effort, same as the original's silent CreateDirectoryW: the result (created,
            // already existed, or some other failure) is never inspected -- a later fopen() on
            // the full path is what actually reports whether this succeeded.
            if (!dir.empty()) PortableMkdir(dir);
        }
    }
}

// UTF-8 <-> wstring -----------------------------------------------------------------------------
// No ANSI code page on a POSIX/UTF-8 filesystem, so this is a plain UTF-8 <-> UTF-32-per-wchar_t
// codec (correct on Emscripten/Linux, where wchar_t is 4 bytes). Nothing in this file routes
// through it -- every disk wrapper below is UTF-8 end to end -- it exists only because paths.h
// declares it as part of the public surface some callers still link against.
std::wstring Widen(const std::string& utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = (unsigned char)utf8[i];
        unsigned long cp = 0;
        int extra = 0;
        if (c < 0x80)             { cp = c;        extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { i++; continue; }   // invalid lead byte: skip it
        if (i + (size_t)extra >= utf8.size()) break;
        bool valid = true;
        for (int k = 1; k <= extra; k++) {
            unsigned char cc = (unsigned char)utf8[i + (size_t)k];
            if ((cc & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!valid) { i++; continue; }
        out.push_back((wchar_t)cp);
        i += (size_t)extra + 1;
    }
    return out;
}

std::string Narrow(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t wc : w) {
        unsigned long cp = (unsigned long)wc;
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::FILE* FOpen(const std::string& utf8Path, const char* mode) {
    return std::fopen(utf8Path.c_str(), mode);
}

static bool StatOf(const std::string& p, struct stat& st) {
    return ::stat(p.c_str(), &st) == 0;
}

bool PathExists(const std::string& p) { struct stat st{}; return StatOf(p, st); }
bool DirExists(const std::string& p)  { struct stat st{}; return StatOf(p, st) && S_ISDIR(st.st_mode); }
bool FileExists(const std::string& p) { struct stat st{}; return StatOf(p, st) && !S_ISDIR(st.st_mode); }

long long FileSize(const std::string& p) {
    struct stat st{};
    if (!StatOf(p, st)) return -1;
    return (long long)st.st_size;
}

bool RemoveFile(const std::string& p) { return ::remove(p.c_str()) == 0; }

bool RenameFile(const std::string& a, const std::string& b) {
    if (PathExists(b)) return false;   // MoveFileW without REPLACE_EXISTING refuses to overwrite
    return ::rename(a.c_str(), b.c_str()) == 0;
}

// POSIX rename() is already atomic and already replaces an existing destination in one syscall --
// exactly the guarantee ReplaceFileAtomic exists to provide on Windows via MoveFileExW. No retry
// loop needed: Emscripten's virtual filesystem has no antivirus/indexer/cloud-sync to race against.
bool ReplaceFileAtomic(const std::string& a, const std::string& b) {
#if defined(_WIN32)
    // Windows CRT rename() -- unlike true POSIX rename() -- refuses to overwrite an existing
    // destination. This branch only exists so this file also behaves correctly when smoke-tested
    // natively under MinGW; it is not atomic. The real target (Emscripten) never takes it.
    ::remove(b.c_str());
#endif
    return ::rename(a.c_str(), b.c_str()) == 0;
}

bool CreateDir(const std::string& p) {
    if (PortableMkdir(p) == 0) return true;
    return errno == EEXIST;
}

bool RemoveDirIfEmpty(const std::string& p) { return ::rmdir(p.c_str()) == 0; }

// Minimal glob matcher (only '*' and '?') so ListDir doesn't need fnmatch.h, which isn't
// available on every POSIX-ish toolchain this file is smoke-tested against (MinGW).
static bool WildcardMatch(const char* pat, const char* str) {
    if (*pat == '\0') return *str == '\0';
    if (*pat == '*') {
        if (WildcardMatch(pat + 1, str)) return true;
        return *str != '\0' && WildcardMatch(pat, str + 1);
    }
    if (*pat == '?') return *str != '\0' && WildcardMatch(pat + 1, str + 1);
    return *str != '\0' && *pat == *str && WildcardMatch(pat + 1, str + 1);
}

std::vector<FindEntry> ListDir(const std::string& utf8Pattern) {
    std::vector<FindEntry> out;
    std::string pattern = utf8Pattern;
    for (char& c : pattern) if (c == '\\') c = '/';
    size_t slash = pattern.find_last_of('/');
    std::string dir  = (slash == std::string::npos) ? std::string(".") : pattern.substr(0, slash);
    std::string glob = (slash == std::string::npos) ? pattern : pattern.substr(slash + 1);
    // A pattern whose tail carries no wildcard (a bare directory, or one with a trailing '/')
    // means "everything in this directory" -- an empty glob would otherwise only match an empty
    // name, i.e. nothing at all.
    if (glob.empty()) glob = "*";

    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        const char* name = ent->d_name;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;
        if (!WildcardMatch(glob.c_str(), name)) continue;
        struct stat st{};
        bool isDir = StatOf(dir + "/" + name, st) && S_ISDIR(st.st_mode);
        out.push_back({ std::string(name), isDir });
    }
    closedir(d);
    return out;
}

// Mojibake twins (ANSI-vs-UTF-8 folder pairs) can only exist on Windows' narrow file APIs.
// A POSIX/UTF-8 virtual filesystem never creates the corrupted twin in the first place: no-op.
int MigrateMojibakeTwins() { return 0; }

std::string AcpTwinUtf8(const std::string& /*utf8Path*/) { return std::string(); }

const std::string& DefaultUserRoot() {
    static const std::string s = kDefaultRoot[(int)Root::UserData];
    return s;
}

bool MigrateLegacyDir(const std::string& oldDir, const std::string& newDir) {
    if (PathExists(newDir)) return false;   // never overwrite an existing destination
    if (!DirExists(oldDir)) return false;
    return ::rename(oldDir.c_str(), newDir.c_str()) == 0;
}

// The legacy/rebrand folder split is a Windows userdata-migration concern; a freshly mounted
// browser virtual filesystem never has a legacy folder to migrate from.
bool MigrateLegacyUserRoot() { return false; }

} // namespace paths
