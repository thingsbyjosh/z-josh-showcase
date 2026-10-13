#pragma once
// One-shot migration of legacy saves (a flat folder, e.g. a temp directory) to userdata/saves.
// Idempotent: only copies a file if the destination does NOT yet exist (never overwrites a newer save).
namespace career {

// Copies career.sav / career_season.sav / career_seasons.sav / career_standings_*.sav from legacyDir
// to paths::UserData("saves\\..."). Returns the number of files copied. Safe to call on every boot.
int MigrateLegacySavesOnce(const char* legacyDir = "C:\\temp");

} // namespace career
