// career_aiverify — did OUR AI grid actually get applied?
//
// The problem this solves: AMS2 matches each <driver> in the CustomAIDrivers XML
// to a car by its livery_name attribute. If that name does NOT match a livery that's ACTUALLY installed,
// the game DISCARDS that car's entire override and drops in its generic driver with default
// abilities. No error, no warning, no log. All it takes is the user installing a skin pack that
// renames liveries, and our entire AI layer (talent, pace, reliability, DDA) stops existing while the
// race just keeps running as if nothing happened.
//
// The check is straightforward because we have both halves:
//   what we WROTE: the <name> tags of each class's XML (the writer records them, and they can
//     also be re-read from disk, which is exactly what AMS2 read at boot);
//   what's ON TRACK: mParticipantInfo[i].mName from shared memory (ShmReadGrid).
// If the names on the live grid aren't ours, the override fell through.
//
// Deliberately a RATIO, not an equality check: the player and the Safety Car show up on the grid
// and aren't ours, and there are classes where the game legitimately fills a gap with a generic driver.
#pragma once
#include <string>
#include <vector>

namespace career {

// Records the driver names we just wrote into a class's XML. Called by
// WriteSeasonGridXml; it accumulates (does NOT replace), because a mid-session regeneration doesn't erase the
// names AMS2 actually booted with — comparing only against the latest write
// would give a false positive after a regeneration.
void AiVerifyRegisterNames(const std::string& className, const std::vector<std::string>& driverNames);

// Re-reads the <name> tags from the XML files already present in the CustomAIDrivers directory. It's the ground truth of what
// AMS2 read at boot, and it makes the check work even when this session hasn't
// regenerated anything. Returns how many new names it added. Idempotent (accumulates, doesn't duplicate).
int AiVerifyLoadFromDisk(const std::string& customAiDir);

// Verdict on a live grid.
struct AiOverrideCheck {
    bool   evaluated = false;   // false = not enough evidence -> NO verdict either way
    int    counted   = 0;       // participants judged (excluding player and Safety Car)
    int    ours      = 0;       // ...of which, names we actually wrote
    int    foreign   = 0;       // ...of which, names we didn't
    double oursPct   = 0.0;     // ours / counted, in [0,1]
    bool   broken    = false;   // the override was NOT applied (ratio below the threshold)
    std::vector<std::string> foreignSample;   // up to 4 foreign names, so the log is actionable
};

// Compares the live grid's names against what was recorded. `playerName` is excluded (it can come in
// empty). Safety Car / Pace Car / unnamed slots are excluded via IsRealDriver.
// Gives no verdict (evaluated=false) if there are no recorded names, or fewer than kMinCounted
// participants remain to judge: a 2-car grid isn't enough for an honest ratio.
AiOverrideCheck AiVerifyCheckGrid(const std::vector<std::string>& trackNames,
                                  const std::string& playerName);

// Minimum judgeable participants to issue a verdict, and the ratio of OUR names
// below which we consider the override to have fallen through. 0.50 leaves plenty of room for legitimate generic
// filler (which is one or two cars) without letting a total failure (100%) slip through.
extern const int    kAiVerifyMinCounted;
extern const double kAiVerifyMinOursPct;

size_t AiVerifyKnownCount();   // diag/test: recorded names
void   AiVerifyResetForTest();
}  // namespace career
