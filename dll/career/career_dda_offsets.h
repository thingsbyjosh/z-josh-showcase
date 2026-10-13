#pragma once
// Difficulty offsets aggregated from COMMUNITY telemetry and
// packaged into each release's data bundle. Two granularities:
//   per CLASS: how well or poorly AMS2's AI is tuned for that class
//   per CLASS + TRACK: AI quirks at a specific circuit
// The track offset is COMMUNITY-only: a single player doesn't repeat a combo often enough to learn
// anything reliable, but thousands of players do. The class offset also carries a local delta on top (career_difficulty.h).
// RULE: an absent, empty, or unreadable table = every offset defaults to zero. The system has to work fine without it.
#include <map>
#include <string>

namespace career { namespace dda {

// A safety cap on the data: a corrupted dump can't shift difficulty by more than this.
constexpr int kOffsetCap = 10;

// PSV: class_id|track_hash|offset  (track_hash 0 = the whole class's offset). '#' = comment.
// false if it couldn't be opened; in that case the table is left EMPTY (it doesn't keep the previous one).
bool LoadClassDifficulty(const char* path);

// Replaces the entire table (tests and cold start).
void ReplaceClassDifficulty(std::map<std::string, std::map<long, int>> m);

int ClassOffsetFor(const std::string& classId);                    // the track_hash 0 row
int TrackOffsetFor(const std::string& classId, long trackHash);     // 0 if there's no row
// TOTAL number of PSV rows loaded (diag/test): the sum of the inner maps, not the number of classes — the
// file carries one row per (class, track) PLUS one row for the whole class (track_hash 0). It's the number that
// feeds the data root's fingerprint; confusing it with "number of classes" would compare apples to oranges across versions.
// class_difficulty.psv being absent is the normal state today (see the RULE above): it returns 0 without crashing.
int ClassDifficultyRowCount();

}}  // namespace career::dda
