#include "paths.h"
#include "json_min.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace paths {

// Fallbacks for when config.json is missing or a root isn't defined.
//   Data / GameInstall: dev repo/Steam paths (in production the config ALWAYS provides these).
//   UserData: %LOCALAPPDATA%\CareerEngineDemo (rebrand 2026-07-20; legacy \CareerEngineDemoLegacy kept until migrated) -- this is NOT a dev
//     path, it's the REAL userdata location on any machine (config.json already lives there): a universal fallback.
//     the old temp folder remains ONLY as a legacy migration source (save_migrate.cpp), never as a destination.
static const char* kDevFallback[4] = {
    "",                                                       // Data -> DevDataRoot() (0a: root of the tree IN USE)
    "",                                                       // UserData -> DefaultUserData()
    "",                                                   // GameInstall (not fixed; see config.json)
    "",                                                       // GameUserDir (the DLL doesn't use it today)
};

static std::string g_root[4];
static bool g_init = false;

// Rebrand 2026-07-20: the branded folder. Resolution is READ-ONLY (prefers the new one if it exists; otherwise
// the legacy one, so machines that haven't migrated don't lose data; if neither exists, the new one). The actual migration
// (rename) lives in MigrateLegacyUserRoot() and is triggered explicitly by the app/DLL -- never by this function,
// so that career_test never renames real data on the machine.
static const char* kBrandDir  = "CareerEngineDemo";
static const char* kLegacyDir = "CareerEngineDemoLegacy";

// UTF-8 filesystem (mojibake fix 2026-07-22) ----------------------------------------------------------------
// Convention: EVERY engine path is UTF-8 in memory; writing to disk converts to UTF-16 and uses the W API.
// With bytes that AREN'T valid UTF-8 (an old config.json edited in ANSI) it falls back to the
// ANSI interpretation, which is what those bytes meant when they were written.

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    UINT cp = CP_UTF8; DWORD flags = MB_ERR_INVALID_CHARS;
    if (n <= 0) { cp = CP_ACP; flags = 0; n = MultiByteToWideChar(cp, flags, utf8.c_str(), (int)utf8.size(), nullptr, 0); }
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(cp, flags, utf8.c_str(), (int)utf8.size(), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::FILE* FOpen(const std::string& utf8Path, const char* mode) {
    wchar_t wmode[16]; int i = 0;
    for (; mode[i] && i < 15; i++) wmode[i] = (wchar_t)(unsigned char)mode[i];
    wmode[i] = L'\0';
    return _wfopen(Widen(utf8Path).c_str(), wmode);
}

static DWORD AttrsOf(const std::string& p) { return GetFileAttributesW(Widen(p).c_str()); }

bool PathExists(const std::string& p) { return AttrsOf(p) != INVALID_FILE_ATTRIBUTES; }
bool DirExists(const std::string& p)  { DWORD a = AttrsOf(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
bool FileExists(const std::string& p) { DWORD a = AttrsOf(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

long long FileSize(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(Widen(p).c_str(), GetFileExInfoStandard, &fad)) return -1;
    return ((long long)fad.nFileSizeHigh << 32) | (long long)fad.nFileSizeLow;
}

bool RemoveFile(const std::string& p)  { return DeleteFileW(Widen(p).c_str()) != 0; }
bool RenameFile(const std::string& a, const std::string& b) { return MoveFileW(Widen(a).c_str(), Widen(b).c_str()) != 0; }
// -- see paths.h for why. A short retry because the locks that break this (antivirus,
// an indexer, cloud sync opening the file right after it's written) last milliseconds:
// without it, a perfectly healthy save would fail from bad timing luck. The destination NEVER disappears.
bool ReplaceFileAtomic(const std::string& a, const std::string& b) {
    const std::wstring from = Widen(a), to = Widen(b);
    const DWORD flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
    for (int i = 0; i < 5; i++) {
        if (MoveFileExW(from.c_str(), to.c_str(), flags)) return true;
        const DWORD e = GetLastError();
        if (e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION && e != ERROR_LOCK_VIOLATION) return false;
        Sleep(20 * (i + 1));
    }
    return MoveFileExW(from.c_str(), to.c_str(), flags) != 0;
}
bool CreateDir(const std::string& p) {
    if (CreateDirectoryW(Widen(p).c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}
bool RemoveDirIfEmpty(const std::string& p) { return RemoveDirectoryW(Widen(p).c_str()) != 0; }

std::vector<FindEntry> ListDir(const std::string& utf8Pattern) {
    std::vector<FindEntry> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(Widen(utf8Pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        out.push_back({ Narrow(fd.cFileName), (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 });
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

static std::string DefaultUserData() {
    wchar_t lad[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", lad, MAX_PATH) > 0) {
        std::string base = Narrow(lad);
        std::string neo = base + "\\" + kBrandDir;
        std::string old = base + "\\" + kLegacyDir;
        if (!PathExists(neo) && PathExists(old)) return old;   // unmigrated machine: still on the legacy path
        return neo;
    }
    return std::string("C:\\") + kBrandDir;   // unlikely last resort (no %LOCALAPPDATA%)
}

const std::string& DefaultUserRoot() {
    static std::string s = DefaultUserData();
    return s;
}

bool MigrateLegacyDir(const std::string& oldDir, const std::string& newDir) {
    if (PathExists(newDir)) return false;                    // never overwrite an existing destination
    if (!DirExists(oldDir)) return false;
    return MoveFileExW(Widen(oldDir).c_str(), Widen(newDir).c_str(), 0) != 0;   // same volume -> a cheap rename
}

bool MigrateLegacyUserRoot() {
    wchar_t lad[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", lad, MAX_PATH) == 0) return false;
    std::string base = Narrow(lad);
    return MigrateLegacyDir(base + "\\" + kLegacyDir, base + "\\" + kBrandDir);
}

// 0a (0.7.0 port, batch 0 · preparation) — DEVELOPMENT fallback for Data. It used to be an absolute path
// pinned to the main checkout's docs folder: any binary without config.json (career_test.exe, the
// seatwatch probe...) running from ANOTHER worktree was unknowingly reading that checkout's data, not
// its own. It's resolved just ONCE (like DefaultUserRoot(), above), relative to the
// process CWD: build_career_test.ps1 does Push-Location to the repo root before launching, so
// this ALWAYS lands in the tree it's run from, whatever its name. In production this
// branch isn't exercised: config.json always rules.
static std::string DevDataRoot() {
    static std::string s = [] {
        wchar_t cwd[MAX_PATH];
        DWORD n = GetCurrentDirectoryW(MAX_PATH, cwd);
        std::string base = (n > 0 && n < MAX_PATH) ? Narrow(std::wstring(cwd, n)) : std::string(".");
        return base + "\\docs";
    }();
    return s;
}

static std::string FallbackRoot(int i) {
    if (i == (int)Root::UserData) return DefaultUserData();
    if (i == (int)Root::Data) return DevDataRoot();
    return kDevFallback[i];
}

static std::string TrimTrailingSlash(std::string s) {
    while (!s.empty() && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    return s;
}

// Rebrand 2026-07-20: tolerance for configs written BEFORE the folder rename -- if a root points to
// ...\CareerEngineDemoLegacy\... which no longer exists but its twin ...\CareerEngineDemo\... does, it's corrected in memory
// (the app persistently rewrites config.json; here we just avoid breaking).
static void RebrandStaleRoot(std::string& p) {
    static const std::string kLegacyMarker = std::string("\\") + kLegacyDir;
    size_t at = p.find(kLegacyMarker);
    if (at == std::string::npos) return;
    if (PathExists(p)) return;                              // the old path is still alive -> honor it
    std::string alt = p;
    alt.replace(at, kLegacyMarker.size(), std::string("\\") + kBrandDir);
    if (PathExists(alt)) p = alt;
}

void Init() {
    if (g_init) return;
    for (int i = 0; i < 4; i++) g_root[i].clear();
    // config.json ALWAYS lives at %LOCALAPPDATA%'s default root (the app writes it there),
    // which after the rebrand resolves to CareerEngineDemo (or to the legacy CareerEngineDemoLegacy if this machine hasn't migrated).
    std::string cfgPath = DefaultUserRoot() + "\\config.json";
    if (auto cfg = jsonm::ParseFile(cfgPath.c_str())) {
        if (const jsonm::Value* p = cfg->member("paths")) {
            g_root[0] = p->memberString("data");
            g_root[1] = p->memberString("userdata");
            g_root[2] = p->memberString("gameInstall");
            g_root[3] = p->memberString("gameUserDir");
        }
    }
    for (int i = 0; i < 4; i++) {
        g_root[i] = g_root[i].empty() ? FallbackRoot(i) : TrimTrailingSlash(g_root[i]);
        RebrandStaleRoot(g_root[i]);
    }
    g_init = true;
}

void SetRootsForTest(const std::string& data, const std::string& userData,
                     const std::string& gameInstall, const std::string& gameUserDir) {
    const std::string* in[4] = { &data, &userData, &gameInstall, &gameUserDir };
    for (int i = 0; i < 4; i++)
        g_root[i] = in[i]->empty() ? FallbackRoot(i) : TrimTrailingSlash(*in[i]);
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
    for (char& c : sub) if (c == '/') c = '\\';
    while (!sub.empty() && sub.front() == '\\') sub.erase(sub.begin());
    if (sub.empty()) return base;
    return base + "\\" + sub;
}

void EnsureParentDir(const std::string& filePath) {
    std::string p = filePath;
    for (char& c : p) if (c == '/') c = '\\';
    size_t start = (p.size() >= 2 && p[1] == ':') ? 2 : 0;   // skip "X:"
    for (size_t i = start + 1; i < p.size(); i++) {
        if (p[i] == '\\') {
            std::string dir = p.substr(0, i);
            if (dir.size() > 2) CreateDirectoryW(Widen(dir).c_str(), nullptr);
        }
    }
}

// migrating the mojibake twin (2026-07-22) --------------------------------------------------------
// Before the fix, the A APIs interpreted the config's UTF-8 bytes as ANSI: on a machine with an
// accented username, the engine created a parallel tree with the corrupted name (Usuário -> UsuÃ¡rio).
// This computes that literal folder (the SAME bytes, converted the way the A API did) and
// merges its contents into the correct root so the user gets their saves/data back without touching anything.

// The LITERAL wide path the A APIs produced from these bytes (ACP conversion, the way Windows did it).
static std::wstring AcpTwinW(const std::string& bytes) {
    if (bytes.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, bytes.c_str(), (int)bytes.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.c_str(), (int)bytes.size(), &w[0], n);
    return w;
}

std::string AcpTwinUtf8(const std::string& utf8Path) {
    std::wstring twin = AcpTwinW(utf8Path);
    if (twin.empty() || twin == Widen(utf8Path)) return std::string();
    return Narrow(twin);
}

// Recursively moves from -> to without overwriting existing destinations (the correct tree wins). Returns
// the number of files moved. Source dirs are removed only if they ended up empty.
static int MergeMoveTreeW(const std::wstring& from, const std::wstring& to) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((from + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int moved = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring src = from + L"\\" + fd.cFileName;
        std::wstring dst = to + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryW(dst.c_str(), nullptr);
            moved += MergeMoveTreeW(src, dst);
            RemoveDirectoryW(src.c_str());   // best-effort: only removed if it ended up empty
        } else {
            if (MoveFileExW(src.c_str(), dst.c_str(), 0) != 0) moved++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return moved;
}

int MigrateMojibakeTwins() {
    if (!g_init) Init();
    int total = 0;
    // Only Data and UserData: the only roots where the engine CREATES files. GameInstall/GameUserDir
    // belong to the game (read-only for the engine), and an accidental merge there would be dangerous.
    const Root roots[2] = { Root::Data, Root::UserData };
    for (Root r : roots) {
        const std::string& root = g_root[(int)r];
        if (root.empty()) continue;
        std::wstring good = Widen(root);
        std::wstring twin = AcpTwinW(root);
        if (twin.empty() || twin == good) continue;   // pure ASCII or ACP=UTF-8: no twin is possible
        DWORD a = GetFileAttributesW(twin.c_str());
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) continue;
        // ensure the destination exists (the correct tree may not exist yet on a freshly affected machine)
        for (size_t i = 3; i < good.size(); i++)
            if (good[i] == L'\\') CreateDirectoryW(good.substr(0, i).c_str(), nullptr);
        CreateDirectoryW(good.c_str(), nullptr);
        total += MergeMoveTreeW(twin, good);
        RemoveDirectoryW(twin.c_str());   // best-effort: may remain if there were name collisions
    }
    return total;
}

} // namespace paths
