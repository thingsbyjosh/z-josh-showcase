#pragma once
#include <string>
#include <vector>
#include <set>                // set of a team's occupants (cohort brand affinity)
#include <map>                // driver -> classified position (the duel against your teammate)
#include "career_save.h"      // SaveData, ClassStats, PlayerIdentity, CareerProgress
#include "career_fields.h"    // RosterDriver
#include "career_balance.h"   // bal::CANDFIT_*

namespace career {

// a candidate's 8 normalized 0..1 factors for a seat (fixed catalog order:
// skill, clean, seriesExp, brandAff, rep, natMatch, youth, budget). Pure POD.
struct CandidateFactors { double f[career::bal::CANDFIT_FACTOR_COUNT] = {0,0,0,0,0,0,0,0}; };

// Team personality seeded from its label (deterministic): fnv1a(teamLabel) % CANDFIT_PERSONALITY_COUNT.
int TeamPersonality(const std::string& teamLabel);

// THE RIVALRY WITH YOUR TEAMMATE.
//
// It's the rival that actually matters, and the engine used to treat them like a stranger: `rivalryHeat`, the grudge system, and
// the on-track duel never looked at team affiliation, and the on-track duel also picked the adjacent driver by
// position, which is almost never the one on the other side of your garage. Same car, same team: it's the only comparison
// on the whole grid that's truly clean, and losing it has no excuse.
//
// This settles it after a race. It does NOT duplicate h2h (AccumulateH2H already handles that, and has been
// logging the duel against them without anyone knowing which one it was for a while): what this does is turn that
// result into GRUDGE, which is what the rest of the game already knows how to read (the shortlist reserves a slot for whoever has
// history with you, and the inbox talks about them).
struct TeammateDuel {
    std::string id, name;      // "" = your team runs a single car, or there's no contract in that series
    bool raced = false;        // both classified: there's a duel to tell
    bool lost  = false;        // beat you
    int  w = 0, l = 0;         // the accumulated h2h against them (informational, not touched here)
    int  beef = 0;             // their grudge level AFTER this race
    // WHY there was no duel, so it can be measured instead of guessed. 0 = there was one.
    // 1 you DNF'd . 2 no contract in that series . 3 the team runs a single car . 4 their seat is empty
    // . 5 your teammate was NOT on that race's grid
    int  why = 0;
};

// `posById` = driver -> classified position for THIS race (0 or absent = didn't classify). `playerPos` <= 0
// (a DNF) -> no duel, the same criterion AccumulateH2H already uses: a DNF isn't a loss against
// anyone. MUTATES save.beef; doesn't touch h2h.
TeammateDuel SettleTeammateDuel(SaveData& save, const std::string& seriesId, int playerPos,
                                const std::map<std::string, int>& posById, int worldYear);

// WHAT THE SEAT WANTS, SAID OUT LOUD.
//
// The candidacy model already decided using this -- the per-personality weights in CANDFIT_W and the candidate's
// 8 factors -- but none of it surfaced from the engine: `career_api_queries.cpp` never named TeamPersonality once,
// so the seat detail screen could only show the reputation bar, and the player had no way to
// know why one team wanted them and another didn't. This makes that calculation visible, and the rule is that it has to be
// THE SAME calculation, not a copy: these two functions read CANDFIT_W and PlayerCandidateFactors, so if a weight
// gets recalibrated tomorrow, the screen updates on its own.
//
// STABLE token for the team's personality, so the UI can translate it instead of receiving pre-baked text:
// meritocratic | young_academy | veteran | money | patriotic | brand_loyal.
const char* TeamPersonalityToken(const std::string& teamLabel);

// A factor the team values, with its weight and with YOUR score on it. Both on a 0..100 scale so they cross the
// API without decimals: `weight` is what matters to THEM, `you` is what YOU bring.
struct SeatAsk {
    const char* key = "";   // stable token: skill|clean|seriesExp|brandAff|rep|natMatch|youth|budget
    int weight = 0;         // 0..100 -- that factor's weight for THIS team (CANDFIT_W)
    int you    = 0;         // 0..100 -- your score on that factor (PlayerCandidateFactors)
};

// The `topN` factors that matter MOST to the team, from highest to lowest weight (ties -> catalog order, which
// is total and deterministic). topN <= 0 or larger than the catalog -> all 8. Pure: doesn't touch the save.
std::vector<SeatAsk> SeatAsksFor(const SaveData& save, const std::string& className,
                                 const std::string& teamLabel, const std::string& teamCountry,
                                 double playerMoneyNorm, int worldYear, int topN);

// A candidate's fit score for THIS team's seat: sum of weights[personality]xfactor + deterministic
// noise (fnv1a(teamLabel|candidateId|year)). candidateId identifies the candidate for the noise term
// (the driver's name/id; "player" for the player). Pure, stateless.
double TeamCandidateFit(const CandidateFactors& fac, const std::string& teamLabel,
                        const std::string& candidateId, int year);

// The PLAYER's extraction: the real factors read from the save. money_norm is computed by the caller (credits/ref).
CandidateFactors PlayerCandidateFactors(const SaveData& save, const std::string& className,
                                        const std::string& teamLabel, const std::string& teamCountry,
                                        double money_norm, int worldYear);

// The ids of the drivers who currently occupy a seat at `teamLabel` in `className`.
// This is the honest source for a cohort driver's brand affinity: no history is invented, it's read
// who's driving for that team right now. Empty label or class -> empty set (never "everyone").
std::set<std::string> TeamOccupantIds(const SaveData& save, const std::string& className,
                                      const std::string& teamLabel);

// The COHORT's extraction (simulated where there's no data): skill from the live rating/base_skill, clean by archetype,
// seriesExp from incumbency, rep as a proxy for rating, natMatch by country, youth by birth_year.
// `teamOccupants` is the TeamOccupantIds set for THIS team. nullptr -> brandAff 0.0, which
// is the previous behavior (old callers and tests that don't pass it stay unchanged). Pure.
// `seriesId` anchors the rival's skill TO THE TIER OF THE SEAT being contested (OccupantRating from the
// earlier work). Without it, the cohort fell back to a FLAT mapping (1200 + base_skill x 700) that ignores category: a
// karting driver with base_skill 0.97 scored 1879, an elite rating, because that 0.97 is their rank WITHIN karting
// and not an absolute level. Empty -> flat mapping, the previous behavior.
// 09/09 (performance, probe of the ): what does NOT change from one candidate to another and was computed per candidate.
//  `incumbents` = all the seat occupants in the world (the VALUES of save.seat_occupant), built ONCE
//  per triple in RankSeatCandidates; before, each candidate swept the whole map (2,150 seats) to know whether
//  they were an incumbent, and with hundreds of candidates per triple and hundreds of triples per winter it was the bottleneck (the T4 of
//  contracts raised the probe by 34 %). nullptr -> the old sweep, same result.
struct CohortFactorsCache { const std::set<std::string>* incumbents = nullptr; };
CandidateFactors CohortCandidateFactors(const RosterDriver& rd, const SaveData& save,
                                        const std::string& className, const std::string& teamLabel,
                                        const std::string& teamCountry, int worldYear,
                                        const std::set<std::string>* teamOccupants = nullptr,
                                        const std::string& seriesId = std::string(),
                                        const CohortFactorsCache* cache = nullptr);

// 28/07 -- a CANDIDATE for the seat, with their score. `id` is the roster id ("player" for the player) and `name`
// is the display name. This exists because the whole contest used to be computed and then thrown away: `PlayerWinsSeat` returned a yes/no and
// with it went exactly what's needed to show WHO you're actually competing against.
struct SeatCandidate {
    std::string id, name;
    double fit = 0.0;
    bool is_player = false;
};

// The FULL seat contest, best to worst score. Includes the player. Ties -> broken by id, so
// the order is deterministic and doesn't depend on how the cohort happens to be sorted. Pure, stateless.
// `seriesId` (optional) anchors the cohort's skill to the seat's tier -- see
// CohortCandidateFactors. Empty = previous behavior.
std::vector<SeatCandidate> RankSeatCandidates(const SaveData& save, const std::string& className,
                                              const std::string& teamLabel, const std::string& teamCountry,
                                              double playerMoneyNorm, int worldYear,
                                              const std::vector<RosterDriver>& cohortDrivers,
                                              const std::string& seriesId = std::string());

// The VERDICT on a table that's ALREADY been computed: true if the player keeps the seat. This exists so that any place
// that also needs the rival's NAME can pull it from the same table instead of ranking twice
// or reimplementing the criterion -- if the two were separate, what the UI shows and what the engine decides could disagree.
bool PlayerWinsRankedSeat(const std::vector<SeatCandidate>& rank);

// The best candidate who is NOT the player, or nullptr if you were competing alone. The table comes pre-sorted, so it's the
// first non-player entry. A pointer into the `rank` element: valid as long as the vector lives.
const SeatCandidate* TopRivalInRank(const std::vector<SeatCandidate>& rank);

// does the player win the seat against the best cohort candidate? true if
// playerFit >= bestCohortFit - MK_SEAT_CONTEST_MARGIN. cohortDrivers = the reachable candidates (RosterForTier).
// A thin wrapper over RankSeatCandidates: the SAME verdict as always, computed once.
bool PlayerWinsSeat(const SaveData& save, const std::string& className, const std::string& teamLabel,
                    const std::string& teamCountry, double playerMoneyNorm, int worldYear,
                    const std::vector<RosterDriver>& cohortDrivers);

// "you're being watched" (see SeatWatch in career_save.h) --------------------------------
// The THREE outcomes of a contest. Until now there were only two (you get it / silence); WATCH carves out a third case
// WITHIN what used to be silence, so the set of seats you win TODAY doesn't change.
enum SeatContestOutcome { SEAT_CONTEST_WON = 0, SEAT_CONTEST_WATCH = 1, SEAT_CONTEST_LOST = 2 };

// Classifies a table that's ALREADY been ranked. WON is exactly PlayerWinsRankedSeat (the same criterion, not a copy).
int ClassifyRankedSeat(const std::vector<SeatCandidate>& rank);

// The player's races in this observation's class (0 if they've never raced in it).
int SeatWatchRacesNow(const SaveData& save, const SeatWatch& w);
// Races left in the window (0 = exhausted). Derived from races_at_open, NEVER from a separate counter.
int SeatWatchRacesLeft(const SaveData& save, const SeatWatch& w);
// Is it time to resolve it? Either the races ran out OR the day cap was hit (with no contract in the class you won't race, so it wouldn't count down).
bool SeatWatchDue(const SaveData& save, const SeatWatch& w, int absToday);

// Logs the races run since the last check-in. `rivalAhead` = whether your rival is currently ahead of you
// (the same table that decides the seat). Adds to the streak if they're ahead; RESETS it the moment you beat them once.
// Returns true if it logged anything (no new races -> no-op). It doesn't close anything: that's decided by the rule.
bool SeatWatchNoteRaces(const SaveData& save, SeatWatch& w, bool rivalAhead);

// What to do with a LIVE observation given today's verdict. Lives here and not in the bridge because it's THE rule
// for the period, and the bridge isn't covered by the test harness.
enum SeatWatchAction { SEATWATCH_KEEP = -1, SEATWATCH_CLOSE_LOST = 0, SEATWATCH_CLOSE_WON = 1 };
int SeatWatchActionNow(const SaveData& save, const SeatWatch& w, int outcomeToday, int absToday);

// The live observation for this seat, or nullptr. A seat = (series, team).
SeatWatch* FindSeatWatch(SaveData& save, const std::string& seriesId, const std::string& teamLabel);
const SeatWatch* FindSeatWatch(const SaveData& save, const std::string& seriesId, const std::string& teamLabel);

// The MK_SEAT_WATCH_SHORTLIST best candidates who aren't you, in order. This is the list that gets sealed when the contest opens.
// With `save`, the FIRST slot is reserved for whoever has history with you (beef, or h2h above
// MK_RIVAL_TERNA_MIN_H2H encounters) and is ALREADY in the table -- it gets reordered, never injected as someone
// who wasn't a candidate. It goes first because SeatWatchOpen takes contenders[0] as the observation's rival.
// nullptr or no rivalry -> the usual score-based shortlist.
std::vector<SeatContender> ShortlistFromRank(const std::vector<SeatCandidate>& rank,
                                             const SaveData* save = nullptr);

// Opens the observation if there wasn't a live one for that seat already. true if it opened one (so it can be announced).
// `contenders` is the SEALED list of candidates: while the observation is live, the seat is only
// contested between them and you. The first entry in the list is the main rival (the one that's shown).
bool SeatWatchOpen(SaveData& save, const std::string& seriesId, const std::string& className,
                   const std::string& teamLabel, const std::vector<SeatContender>& contenders, int absToday);
// Closes it: won -> state 1, otherwise state 2 (which starts the cooldown). No-op if it was already closed.
void SeatWatchClose(SeatWatch& w, bool won, int absToday);

// The missing return leg of the rivalry loop. LOSING the seat raises the beef against whoever
// took it from you (+MK_BEEF_SEAT_LOST, capped at 5); winning it doesn't touch it, because the grudge is generated by losing, not by
// competing. With no rival logged (you were competing alone) it does nothing and does NOT create an empty entry. It lives here and not in the
// bridge that closes the observation, because it's THE rule, and the bridge isn't covered by the test harness -- the same criterion as
// SeatWatchActionNow. It's called ALONGSIDE SeatWatchClose, not inside it, so as not to change the signature for callers.
void BeefOnSeatLost(SaveData& save, const SeatWatch& w, bool won);

// Is this seat IN COOLDOWN from a lost observation? While it is, no offer goes out: otherwise the same
// team would reopen the contest on you the very next cycle, and losing it would have cost nothing.
bool SeatWatchOnCooldown(const SaveData& save, const std::string& seriesId, const std::string& teamLabel,
                         int absToday);

// Drops the closed ones that no longer matter (their cooldown has passed): without this they'd grow without bound, season after
// season. Returns how many it removed.
int SeatWatchPrune(SaveData& save, int absToday);

} // namespace career
