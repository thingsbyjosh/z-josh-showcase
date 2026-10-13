#include "save_migrate.h"
#include "paths.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // CopyFileW; declared locally in this translation unit -> the legacy 'far' macro doesn't affect the harness
#include <string>

namespace career {

// Copies src->dst ONLY if src exists and dst does NOT. Creates the destination dir. Returns 1 if it copied.
static int CopyIfAbsent(const std::string& src, const std::string& dst) {
    if (!paths::PathExists(src)) return 0;   // no source
    if (paths::PathExists(dst)) return 0;    // already migrated -> don't overwrite
    paths::EnsureParentDir(dst);
    // mojibake fix (07/22): CopyFileW (UTF-8 -> wide)
    return CopyFileW(paths::Widen(src).c_str(), paths::Widen(dst).c_str(), /*bFailIfExists*/TRUE) ? 1 : 0;
}

int MigrateLegacySavesOnce(const char* legacyDir) {
    std::string ld = legacyDir ? legacyDir : "";
    while (!ld.empty() && (ld.back() == '\\' || ld.back() == '/')) ld.pop_back();
    if (ld.empty()) return 0;

    int copied = 0;
    const char* fixed[] = { "career.sav", "career_season.sav", "career_seasons.sav" };
    for (const char* nm : fixed)
        copied += CopyIfAbsent(ld + "\\" + nm, paths::UserData(std::string("saves\\") + nm));

    // per-class standings: career_standings_*.sav
    // Mojibake fix: ListDir (UTF-8 -> wide)
    for (const auto& fe : paths::ListDir(ld + "\\career_standings_*.sav")) {
        if (!fe.isDir)
            copied += CopyIfAbsent(ld + "\\" + fe.name,
                                   paths::UserData(std::string("saves\\") + fe.name));
    }
    return copied;
}

} // namespace career
