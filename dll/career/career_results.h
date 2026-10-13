#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace career {

// Race states (shared memory $pcars2$ v14, dll/SharedMemory.h).
enum RaceState { RS_RACING = 2, RS_FINISHED = 3, RS_DSQ = 4, RS_RETIRED = 5, RS_DNF = 6 };

// A participant read from shared memory (mirrors the relevant fields of mParticipantInfo[i]).
struct ParticipantSnap {
    std::string name;          // mParticipantInfo[i].mName
    std::string carClass;      // mCarClassNames[i] ("" if unused)
    int  pos = 0;              // mRacePosition (1 = leader; 0 = unset)
    int  laps = 0;             // mLapsCompleted
    int  raceState = 0;        // mRaceStates[i]
    bool active = true;        // mIsActive
    std::string carName;       // mCarNames[i] (exact in-game model; basis for livery_name). Placed last: doesn't break positional inits.
    std::string driver_id;     // Stable id resolved from name (DriverIdForName); "" if not in the pool
    float best_lap = -1.0f;    // mFastestLapTimes[i] (s); <=0 = no valid lap. Used by the DDA difficulty system.
    float last_lap = -1.0f;    // mLastLapTimes[i] (s); <=0 = no completed lap. Used by the lap-to-lap variance test harness.
};

// A row of the final classification (order 1..N).
struct ClassifiedEntry {
    std::string name, carClass;
    int  pos = 0;              // classified rank (1..N among active entries)
    int  laps = 0;
    bool finished = false;     // FINISHED or RETIRED (classified)
    bool dnf = false;          // DNF or DSQ (doesn't score)
    std::string carName;       // mCarNames[i] (exact in-game model). Placed last: doesn't break positional inits.
    std::string driver_id;     // Stable id propagated from ParticipantSnap (used by the nemesis/rival_form logic)
};

// A driver's pace summary for one race (Jul 27). Best lap doesn't work for comparing
// fine-grained pace — measured on track: ±0.3 s of noise across 13 laps vs 0.08-0.25 s of signal between neighbors —
// and the mean gets wrecked by any pit stop or contact. The median withstands both without filtering
// anything by hand, and the standard deviation shows whether a driver is consistent or streaky.
// This is summarized here rather than sending the whole series, deliberately (decided Jul 27): three numbers per
// driver instead of one lap per line.
struct LapPaceStats {
    int    n = 0;              // laps with a credible time
    double medianS = 0.0;      // median (robust to pit stops and spins)
    double sdS = 0.0;          // population standard deviation of the same sample
};

// Summary of a series of lap times. Copies the vector because it sorts for the median. n=0 -> everything zero.
// Gaps (<=0) are ignored: the series stores a zero for laps whose time wasn't credible so that the
// index still matches the lap number, and a gap isn't treated as a 0 s lap.
inline LapPaceStats ComputeLapPace(std::vector<float> laps) {
    LapPaceStats s;
    laps.erase(std::remove_if(laps.begin(), laps.end(), [](float v) { return v <= 0.0f; }), laps.end());
    if (laps.empty()) return s;
    std::sort(laps.begin(), laps.end());
    s.n = (int)laps.size();
    const size_t m = laps.size() / 2;
    s.medianS = (laps.size() % 2) ? (double)laps[m] : 0.5 * ((double)laps[m - 1] + (double)laps[m]);
    double sum = 0.0;
    for (float v : laps) sum += v;
    const double mean = sum / (double)laps.size();
    double acc = 0.0;
    for (float v : laps) { const double d = (double)v - mean; acc += d * d; }
    s.sdS = sqrt(acc / (double)laps.size());
    return s;
}

// (Jul 27) Does the lap-by-lap data for this race fit in the payload? The series lets you reconstruct the
// gap between drivers on every lap (cumulative) and see stint degradation, but it grows with grid size
// × laps: a sprint is 4 KB, and a 24-hour Thornmere race (32 cars, ~820 laps) would be 184 KB, which doesn't even hit the
// endpoint's cap, nor should it come close to the platform's limits.
// The rule is deliberately all-or-nothing: half a series gives false cumulative numbers with no warning, so when
// it doesn't fit, only the summary is sent (three numbers per driver), and the payload states which mode it's in.
constexpr size_t kPaceSeriesMaxSamples = 6000;   // ~42 KB per series; covers a 4-hour race with a full grid
inline bool PaceSeriesFits(size_t totalSamples) { return totalSamples > 0 && totalSamples <= kPaceSeriesMaxSamples; }

// Is this a "real" participant (a driver), not a session artefact (Safety Car / Pace Car), nor a
// empty slot? Artefacts must not be classified or score points (they'd pollute the championship standings).
bool IsRealDriver(const std::string& name);

// (Aug 24) Readable track name built from shared memory (mTrackLocation + mTrackVariation).
// The variation sometimes arrives as an internal id ("Kingsmoor_Park"), and naively concatenating it produced
// "Blackmoor Park Kingsmoor_Park" in emails, the TIMES screen, and the race_log. Rule: an empty variation, or one equal to the
// location, means location-only; if the variation embeds the location (compared using only
// lowercase alphanumerics) -> the variation alone, with '_'->' '; otherwise -> "location variation" with '_'->' '.
std::string TrackDisplayJoin(const std::string& loc, const std::string& var);

// Did the race finish? = the REAL leader (mRacePosition==1, not an artifact) is in FINISHED and no other
// active real driver is still in RACING (19/09: each has closed their last lap, retired or is out).
bool RaceIsOver(const std::vector<ParticipantSnap>& grid);

// Classifies active participants in position order (1..N). Those with unset pos (0) go last,
// by laps descending. Each entry ends up with pos = its classified rank, plus the finished/dnf flags.
std::vector<ClassifiedEntry> Classify(const std::vector<ParticipantSnap>& grid);

// Per-class result from a global classification (multiclass races, as of Jul 27 2026): the
// entries in the player's class re-ranked 1..N (relative order preserved, DNFs last, same as they come from
// Classify), plus the player's class position/size.
struct ClassResult {
    std::vector<ClassifiedEntry> entries;
    int pos = 0;     // the player's position within their class (0 if unresolved: no match / player absent)
    int field = 0;   // number of classified entries in the class
};

// number of distinct non-empty classes in the classification. >=2 => multiclass race.
int DistinctClassCount(const std::vector<ClassifiedEntry>& global);

// Filters `global` to the player's class (case-insensitive match with trim, same criterion as the signature
// used by settle with s.playerCarClass). An empty playerClass, or no matching entries, -> empty entries and
// pos=0 (the caller decides whether to fall back to the global classification).
ClassResult ClassifyByClass(const std::vector<ClassifiedEntry>& global,
                            const std::string& playerClass,
                            const std::string& playerName);

} // namespace career
