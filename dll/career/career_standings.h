#pragma once
#include <string>
#include <vector>
#include <map>
#include "career_scoring.h"

namespace career {

// A driver's result in a round (persistable).
struct DriverRound { int round = 0, pos = 0, points = 0; bool fl = false, dnf = false; };

// A championship's standings (one class's one season).
struct Standings {
    std::string node_id, points_system;
    int season_year = 1, round = 0;
    std::map<std::string, std::vector<DriverRound>> drivers;   // name -> rounds
    // F4 (, deaths): reserved -- legacy persistence with no detector. E6 used to log on-track player<->rival
    // contacts here (LogContact/NemesisFromContact, retired: they never had a production caller, only
    // their own test). F4 replaces that "contact-based nemesis" with the CLASSIFIED duel (career_contract.cpp,
    // family 5). The field + its save lines ('c|...') STAY so as not to break the round-trip for standings
    // already persisted with contacts written.
    std::map<std::string, int> contacts;
    // 18/09 (Josh's decision) — WHO EACH ROW IS. The table accumulates by NAME, and the name doesn't identify:
    // there are 29 names that carry two people (Joao Barbosa the Brazilian and the Portuguese). Resolving the name afterwards
    // with DriverIdForName kept the first, so the standings and the nemesis could point at the
    // wrong person. The results ALREADY carry the id (RoundScore::driver_id), and it's stored here. Persisted
    // in separate `i|name|id` lines: an old reader ignores them.
    std::map<std::string, std::string> ids;
};

// A row of the sorted final table.
struct StandingRow { std::string name; int total = 0, wins = 0, played = 0;
                     std::string id; };   // 18/09: driver id if the table knows it ("" if not)

// E6: row for the Championships screen (port of the prototype's `computeStandings`). ptsPerRound[i] =
// PtsFor(round i's position) or 0 if this driver didn't run that round; rounds are sorted by their
// round number (1..N). total/wins/podiums computed with the DISPLAY table PtsFor (not points_id):
// it's what the UI renders. rank is 1-based after sorting by total desc, wins desc, name asc (deterministic).
struct ChampRow {
    std::string name;
    int total = 0, wins = 0, podiums = 0, rank = 0;
    std::vector<int> ptsPerRound;   // size = number of rounds the table has run; in round order
    std::string id;                 // 18/09: driver id if the table knows it ("" if not)
};

// E6: championship DISPLAY standings (what screen_championships.jsx consumes). Walks s.drivers,
// scores each round with PtsFor(pos) (25..1 table), sorts and assigns rank. roundsPlayed = max round seen.
std::vector<ChampRow> ChampStandings(const Standings& s);

// E6 (per-period points): PERIOD-CORRECT final table. Re-scores each round with the PointsSystem from
// s.points_system (via PointsById) -- an era system (e.g. 9-6-4-3-2-1) scores differently than the
// display table PtsFor. If points_system is unknown (PointsById == nullptr) it uses the already-stored
// DriverRound.points (doesn't silently zero it out). dropWorst same as FinalTable. Order: total desc, wins desc, name.
std::vector<StandingRow> StandingsTable(const Standings& s, int dropWorst = 0);

// Applies one round's scores (adds a DriverRound per driver; advances s.round).
void ApplyRound(Standings& s, int round, const std::vector<RoundScore>& scores);

// Sorted final table: total desc, tiebreak by wins, then by name (deterministic).
// dropWorst = number of worst results dropped per driver (0 = none).
std::vector<StandingRow> FinalTable(const Standings& s, int dropWorst = 0);

// (2026-08-24): rename a driver by MERGING if the new name already exists. The real case is the PLAYER
// changing their nickname (Steam/AMS2) mid-season: the table accumulates by name and the change would split their
// points across two drivers. Merge: rounds are concatenated; if both names scored the SAME round (a rare
// save state), the BEST result is kept (more points; on a tie, the better position -- same criterion as the dedup
// above). contacts are summed. oldName absent or equal to newName -> no-op (false). Returns true if it changed.
bool RenameDriverInStandings(Standings& s, const std::string& oldName, const std::string& newName);
// 02/10 ([tester]): the same merge, but only if the two rows can be the SAME person: they share
// no round (nobody scores twice in the same one) and, if both carry an id, it is the same. It is the one used by the
// card->nick reconciliation, which does not know for sure that the old row is the player: an AI rival who
// is named like their card ran the same rounds as them, and that gives it away. false = nothing was touched.
bool MergePlayerKeyInStandings(Standings& s, const std::string& oldName, const std::string& newName);

// Persistence (line format with '|'-separated fields, round-trip testable). Atomic write.
bool SaveStandings(const Standings& s, const char* path);
bool LoadStandings(Standings& s, const char* path);   // false if it doesn't exist or is corrupt

} // namespace career
