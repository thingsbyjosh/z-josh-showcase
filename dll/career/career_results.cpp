#include "career_results.h"
#include <algorithm>
#include <cctype>

namespace career {

bool IsRealDriver(const std::string& name) {
    // trims whitespace and compares lowercase against the known artefacts
    size_t a = 0, b = name.size();
    while (a < b && (unsigned char)name[a] <= ' ') a++;
    while (b > a && (unsigned char)name[b - 1] <= ' ') b--;
    std::string t;
    for (size_t i = a; i < b; i++) t += (char)tolower((unsigned char)name[i]);
    if (t.empty()) return false;                          // nameless slot -> doesn't score
    if (t == "safety car" || t == "pace car") return false;
    return true;
}

std::string TrackDisplayJoin(const std::string& loc, const std::string& var) {
    // See career_results.h (Aug 24 fix): comparison is done using only lowercase alphanumerics, so that
    // "Wildham-Francorchamps" matches "Wildham_Francorchamps_2022" even though hyphens/underscores differ.
    auto norm = [](const std::string& s){
        std::string o; o.reserve(s.size());
        for (unsigned char c : s) if (isalnum(c)) o += (char)tolower(c);
        return o;
    };
    auto pretty = [](const std::string& s){
        std::string o = s;
        for (char& c : o) if (c == '_') c = ' ';
        return o;
    };
    if (var.empty()) return loc;
    if (loc.empty()) return pretty(var);
    const std::string nl = norm(loc), nv = norm(var);
    if (nv == nl) return loc;                                        // redundant variation -> location only
    if (nv.size() > nl.size() && nv.compare(0, nl.size(), nl) == 0)  // the variation embeds the location
        return pretty(var);                                          //   > "Kingsmoor_Park" => "Blackmoor Park Intl 2019"
    return loc + " " + pretty(var);                                  // normal case -> "Stoneton Bay"
}

bool RaceIsOver(const std::vector<ParticipantSnap>& grid) {
    // 19/09 (report «everyone who was behind comes out lapped»): the leader's checkered flag does NOT close the
    // race for the rest. It was being settled at that instant and the snapshot caught the whole grid halfway through its
    // last lap: one lap fewer, half-finished times and even crossed positions that scored. Now, besides
    // the leader at the line, no ACTIVE real driver may still be in RACING. It looks at "nobody running" and not
    // "all in 3/4/5/6" on purpose: an odd state (0/1) with the leader already at the line will never advance, and
    // waiting for it would leave the race unsettled until leaving the session. The Safety Car does not count.
    bool leaderHome = false;
    for (const auto& p : grid) {
        if (!p.active || !IsRealDriver(p.name)) continue;
        if (p.pos == 1) { if (p.raceState != RS_FINISHED) return false; leaderHome = true; }
        if (p.raceState == RS_RACING) return false;
    }
    return leaderHome;
}

std::vector<ClassifiedEntry> Classify(const std::vector<ParticipantSnap>& grid) {
    std::vector<ParticipantSnap> act;
    int maxLaps = 0;
    for (const auto& p : grid)
        if (p.active && IsRealDriver(p.name)) { act.push_back(p); if (p.laps > maxLaps) maxLaps = p.laps; }   // discards Safety Car / empty slots
    // valid pos first (ascending); unset pos (0) goes last, by laps descending.
    std::sort(act.begin(), act.end(), [&](const ParticipantSnap& a, const ParticipantSnap& b) {
        long ka = a.pos > 0 ? a.pos : 100000L + (maxLaps - a.laps);
        long kb = b.pos > 0 ? b.pos : 100000L + (maxLaps - b.laps);
        return ka < kb;
    });
    std::vector<ClassifiedEntry> out;
    int rank = 0;
    for (const auto& p : act) {
        ClassifiedEntry e;
        e.name = p.name; e.carClass = p.carClass; e.carName = p.carName; e.laps = p.laps;
        e.driver_id = p.driver_id;   // Propagates the stable identity resolved in ShmReadGrid
        // Retiring counts as a DNF (a player reported on Aug 6 that penalties weren't being applied). Here, state 5
        // (RETIRED) was being counted as a finish, and the championship points hang off this classification
        // (ScoreRound skips DNFs), the rating duel (career_engine: "DNF does not contest"), and the
        // grid the app displays. So you'd pit out, your result said DNF, and the classification still
        // gave you the points for that position: measured — 13 points for a race abandoned in P4.
        // AMS2 marks a retiree as state 5, and state 6 is almost never seen, so this was the normal case, not an edge case.
        // The player's own settle logic already used this criterion (r.finished = state != DNF/RETIRED/DSQ);
        // this brings the classification in line with it, for rivals just as for you.
        e.dnf      = (p.raceState == RS_DNF || p.raceState == RS_DSQ || p.raceState == RS_RETIRED);
        e.finished = (p.raceState == RS_FINISHED);
        e.pos = ++rank;
        out.push_back(e);
    }
    return out;
}

namespace {
// trim + lowercase (same criterion as IsRealDriver). File-local.
std::string normClass(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    std::string t;
    for (size_t i = a; i < b; i++) t += (char)tolower((unsigned char)s[i]);
    return t;
}
} // namespace

int DistinctClassCount(const std::vector<ClassifiedEntry>& global) {
    std::vector<std::string> seen;
    for (const auto& e : global) {
        std::string c = normClass(e.carClass);
        if (c.empty()) continue;
        bool dup = false;
        for (const auto& x : seen) if (x == c) { dup = true; break; }
        if (!dup) seen.push_back(c);
    }
    return (int)seen.size();
}

ClassResult ClassifyByClass(const std::vector<ClassifiedEntry>& global,
                            const std::string& playerClass,
                            const std::string& playerName) {
    ClassResult out;
    const std::string want = normClass(playerClass);
    if (want.empty()) return out;
    int rank = 0;
    for (const auto& e : global) {
        if (normClass(e.carClass) != want) continue;
        ClassifiedEntry c = e;
        c.pos = ++rank;
        if (!playerName.empty() && c.name == playerName) out.pos = rank;
        out.entries.push_back(std::move(c));
    }
    out.field = (int)out.entries.size();
    return out;
}

} // namespace career
