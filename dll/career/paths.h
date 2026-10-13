#pragma once
// PathProvider (F2.A) -- resolves the project's 4 roots from config.json,
// with a FALLBACK to dev paths. Goal: remove the dev machine's absolute paths
// from the production build (career_overlay.cpp + dll/career/*.cpp)
// without changing dev behavior (with no config, each root falls back to its default).
//
// IMPORTANT: this header does NOT include <windows.h> (only <string>/<cstdio>) so it doesn't drag
// the legacy 'far' macro into .cpp files that include it (e.g. tools/career_test.cpp).
#include <cstdio>
#include <string>
#include <vector>

namespace paths {

enum class Root { Data = 0, UserData = 1, GameInstall = 2, GameUserDir = 3 };

// Resolves the 4 roots once from <DefaultUserRoot>\config.json (the "paths" member; rebrand 2026-07-20:
// %LOCALAPPDATA%\CareerEngineDemo, or the legacy \CareerEngineDemoLegacy if the machine hasn't migrated yet).
// If the file is missing, or "paths.<root>" is absent/empty, that root uses the dev fallback.
// Idempotent. The first call to Resolve/RootDir lazily triggers Init().
void Init();

// Injects explicit roots and marks it initialized (for tests). An empty string => that root's fallback.
void SetRootsForTest(const std::string& data, const std::string& userData,
                     const std::string& gameInstall, const std::string& gameUserDir);
// Returns to the uninitialized state (for tests).
void ResetForTest();

const std::string& RootDir(Root r);                    // root with no trailing slash
std::string Resolve(Root r, const std::string& rel);   // root + '\' + rel (normalizes '/'->'\', no double slash)

inline std::string Data(const std::string& rel)        { return Resolve(Root::Data, rel); }
inline std::string UserData(const std::string& rel)    { return Resolve(Root::UserData, rel); }
inline std::string GameInstall(const std::string& rel) { return Resolve(Root::GameInstall, rel); }
inline std::string GameUserDir(const std::string& rel) { return Resolve(Root::GameUserDir, rel); }

// Creates the directory tree for a file path's dirname (mkdir -p). Doesn't fail if it already exists.
void EnsureParentDir(const std::string& filePath);

// UTF-8 filesystem (mojibake fix 2026-07-22) ----------------------------------------------------------------
// Every engine path is a UTF-8 std::string (the app writes config.json in UTF-8). Windows/CRT's
// narrow APIs (fopen, CreateDirectoryA, stat...) interpret those bytes in the system's ANSI
// codepage: for accented usernames it would literally create "UsuÃ¡rio" and the
// engine would read/write into a tree parallel to the app's. EVERY disk access from the engine must go through
// these wrappers, which convert UTF-8 -> UTF-16 and call the wide (W) API.
std::wstring Widen(const std::string& utf8);    // UTF-8 -> UTF-16 (non-UTF-8 bytes fall back to ANSI: an old config)
std::string  Narrow(const std::wstring& w);     // UTF-16 -> UTF-8
std::FILE*   FOpen(const std::string& utf8Path, const char* mode);   // _wfopen (a drop-in for fopen)
bool PathExists(const std::string& utf8Path);   // exists (file OR directory)
bool DirExists(const std::string& utf8Path);    // exists and is a directory
bool FileExists(const std::string& utf8Path);   // exists and is NOT a directory
long long FileSize(const std::string& utf8Path);            // bytes; -1 if it doesn't exist
bool RemoveFile(const std::string& utf8Path);                // DeleteFileW (drop-in replacement for remove)
bool RenameFile(const std::string& utf8From, const std::string& utf8To);   // MoveFileW (does NOT overwrite the destination)
// — replaces a file with its .tmp version in ONE step. The save system's three files (career.sav,
// calendar, standings) used to RemoveFile(destination) then RenameFile: between the two calls the save did NOT
// EXIST, and if the rename failed (locked by antivirus, the search indexer, a cloud-sync client) the original was already
// deleted -> the save was lost outright, not half-written. MoveFileExW with REPLACE_EXISTING overwrites the destination in one
// single call; WRITE_THROUGH doesn't return until the metadata is on disk, so a power cut can't leave the gap.
// The name has a suffix on purpose: bare `ReplaceFile` is a windows.h MACRO (-> ReplaceFileW), and
// the .cpp (which does include windows.h) compiled the renamed symbol while this header's declaration didn't —
// a broken link from an invisible clash.
bool ReplaceFileAtomic(const std::string& utf8From, const std::string& utf8To);
bool CreateDir(const std::string& utf8Path);                 // one level; true if created or already existed
bool RemoveDirIfEmpty(const std::string& utf8Path);          // RemoveDirectoryW (only succeeds if empty)

// Directory listing via FindFirstFileW with UTF-8 names (drop-in replacement for the FindFirstFileA loops).
// The pattern supports wildcards ("dir\\*.json"); skips "." and "..".
struct FindEntry { std::string name; bool isDir; };
std::vector<FindEntry> ListDir(const std::string& utf8Pattern);

// One-shot migration of the mojibake "twin" tree the ANSI bug created on machines with an
// accented username: for the Data and UserData roots it computes the literal folder the ANSI ('A') APIs created
// (UTF-8 bytes read as ANSI), and if it exists and differs, merges its content into the correct
// root (never overwrites an existing file). Returns the number of files moved. No-op on
// ASCII machines or ones with ACP=UTF-8. Called by the DLL's boot BEFORE opening logs.
int MigrateMojibakeTwins();

// (tests/diagnostics) The LITERAL name the ANSI ('A') APIs would have created from these bytes (the
// UTF-8 bytes interpreted as ANSI), returned in UTF-8. Empty string if it matches the correct
// UTF-8 interpretation (pure ASCII path, or a machine with ACP=UTF-8): no twin is possible.
std::string AcpTwinUtf8(const std::string& utf8Path);

// Rebrand (07/20): default userdata root %LOCALAPPDATA%\CareerEngineDemo (previously \CareerEngineDemoLegacy).
// RESOLUTION is read-only (prefers the new folder if it exists; otherwise the legacy one; if neither,
// the new one). MIGRATION (renaming the whole folder) is explicit — called by the app and the DLL's boot,
// never by career_test or the path resolver.
const std::string& DefaultUserRoot();                                    // resolved default root (cached)
bool MigrateLegacyDir(const std::string& oldDir, const std::string& newDir);   // primitive: idempotent rename
bool MigrateLegacyUserRoot();                                            // CareerEngineDemoLegacy -> CareerEngineDemo under %LOCALAPPDATA%

} // namespace paths
