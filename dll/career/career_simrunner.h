#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include "career_racemodel.h"
#include "career_season.h"
#include "career_save.h"

namespace career {

struct SimRoundResult {
    int  round = 0;
    int  player_pos = 0;
    int  field = 0;
    long credits_delta = 0;
    int  xp_gained = 0;
    int  rep_delta = 0;
    double rating_before = 0.0, rating_after = 0.0;
    std::string winner_name;
    // phase 7: the ON-TRACK DNF, exactly as the race model returned it (DnfKind). It's split out
    // because player_pos already reads 0 on a DNF, and that 0 doesn't say whether the car broke or someone
    // took you out -- and those are different things to whoever reads the simulation.
    int  dnf_kind = 0;   // DnfKind: 0 finished, 1 mechanical, 2 incident, 3 absence
    // phase 8: the duel with your teammate IN THIS race. `mate_raced` false = your team runs a single car,
    // they didn't race, or you retired (and a DNF isn't a loss to anyone).
    bool mate_raced = false;
    bool mate_lost  = false;
    int  mate_beef  = 0;
    int  mate_why   = 0;   // TeammateDuel::why -- why there WASN'T a duel (0 = there was one)
    bool season_complete = false;
};

struct SimSeasonResult {
    std::string series_id;
    int rounds_run = 0;
    std::string champion;
    int player_pos = 0;
    long money = 0;
    int xp = 0;
    int rep = 0;
    std::vector<SimRoundResult> rounds;
};

struct SimYearsResult {
    int from_year = 0, to_year = 0;
    int retirements = 0;
    int promotions = 0;
    std::vector<std::string> retired_names;
};

// REAL grid composed for a series (rookie cohort + real drivers mixed by tier, same as
// SimulateRound), for inspection WITHOUT simulating. The static `field` from the JSON is just the pool of real drivers;
// this is the grid that actually races (the cohort is grafted in at runtime, it doesn't live in the JSON).
struct SimGridResult {
    bool found = false;
    std::string series_id, display, class_name, region, era;
    int tier = 0;
    int grid_size = 0;
    int real_count = 0, roster_count = 0;
    std::vector<GridDriver> drivers;   // in grid order
};
// Canonical baseline: fresh SaveData -> static curve (no live ratings), deterministic from the data.
SimGridResult SimComposeGrid(const std::string& seriesId);

// fwd: CareerCalendar is defined in career_calendar.h (included in the .cpp)
struct CareerCalendar;

// Task 1: sandbox init + snapshot
void SimInit(SaveData& save, CareerCalendar& cc, const std::string& playerName);

struct SimState {
    int  year = 0, doy = 0;
    long credits = 0, xp_total = 0;
    int  rep = 0;
    double rating = 0.0;
    int  rating_band = 0;
    int  enrollments = 0, retired_count = 0;
    std::vector<std::string> retired;
};
SimState SimSnapshot(const SaveData& save, const CareerCalendar& cc);

std::uint64_t RoundSeed(std::uint64_t base, const std::string& seriesId, int year, int round);

// phase 7 -- THE PLAYER'S LEVEL IS CHOSEN, not deduced. A user request: for a debug tool what's
// worth testing are the CONSEQUENCES of performing that way (what the world does if you dominate, fight, or sink),
// not modeling how anyone drives. And deducing it from ELO fed back on itself: worse rating -> slower -> worse
// rating, a spiral with no way out (measured: rating stuck on the floor after 6 years).
// The profile is placed on the FIELD'S OWN SCALE, so it means the same in karting as in F1.
enum SimPlayerPerf {
    SIMPERF_DOMINANT = 0,   // dominant: wins almost everything
    SIMPERF_STRONG   = 1,   // upper tier: wins some, podium often
    SIMPERF_MID      = 2,   // the midfield (default)
    SIMPERF_POOR     = 3,   // at the back, but within the field
};

SimRoundResult SimulateRound(SeasonState& season, SaveData& save, const std::string& playerName,
                             std::uint64_t baseSeed, const RaceModelConfig& cfg = RaceModelConfig(),
                             int perf = SIMPERF_MID);

SimSeasonResult SimulateSeason(SaveData& save, const std::string& seriesId,
                               const std::string& playerName, std::uint64_t baseSeed,
                               const RaceModelConfig& cfg = RaceModelConfig());

SimYearsResult SimulateYears(CareerCalendar& cc, SaveData& save, const std::string& playerName,
                             int years, std::uint64_t baseSeed);

// Task 12 (sponsor-cards spec 2026-07-11, anti-inflation guard): simulates `seasons` seasons of pure
// economy (self-contained loop, see the design note in career_simrunner.cpp), and if useCards, applies after each
// race a deterministic shop + greedy policy (buys the first affordable offer) + CardsSettle.
// Dumps the final accumulated credits into *outCredits (no-op if outCredits is nullptr).
void SimRunCredits(int seasons, bool useCards, long* outCredits);

// PHASE 7 -- THE WHOLE WORLD, WEEK BY WEEK.
//
// Why it exists: the simulator was SPLIT INTO TWO HALVES THAT NEVER TOUCHED, which meant it couldn't be used
// for the one thing that actually matters, which is seeing what happens as time passes.
//
//   . SimulateYears runs races and ages the world, but jumps year to year via RolloverYearReport WITHOUT
//     TOUCHING THE CLOCK. Since the entire silly season hangs off RunWorldTick -> MarketCalendarTick, that means the
//     signing window NEVER opens: no approaches, no decision week, no renewals, no drought.
//   . The CLI's `advance-week` does move the clock through the app's own path, so the whole world happens...
//     but NOBODY RACES. The player accumulates missed_rounds, at three the contract auto-breaks, and within a few
//     weeks the career is dead: everything you measure after that is garbage.
//
// This stitches the two halves together. The rule that governs it, and everything else hangs off it:
//
//   RUN BEFORE ADVANCING. If a signed-up round's weekend is left behind unrun, DnfPassedRounds
//   counts it as an absence. Reversing this order is exactly today's bug, and it doesn't fail loudly:
//   it fails by leaving a simulation that looks like it works while the career quietly dies.
//
// The clock ALWAYS advances via AdvanceWeek -- the same path the app triggers -- never through its own loop.
// It's the user's rule: results are simulated, but time passes the way it would while actually playing.
//
// Events are NOT emitted by the engine: they're DERIVED by diffing the save before and after each week. That way this
// adds no coupling to anything, and a future change to the window or contracts shows up here without touching anything.
struct SimWorldEvent {
    int year = 0, doy = 0;
    std::string kind;        // token, NEVER UI text: race|window|applied|sounded|offered|rejected|
                             // renewed|signed|broken|drought|rollover
    std::string series_id, team, detail;
    int  num = 0;            // meaning depends on kind (race position, sealed seats, year...
                             // and for 'rejected', the WindowSeatReject: why you were passed over)
    long money = 0;          // balance after the event, so the economy can be read along the timeline
};

enum SimSignPolicy {
    SIMSIGN_NEVER  = 0,      // doesn't apply or sign: it exists to see how long it takes the world to shut you out
    SIMSIGN_GREEDY = 1,      // applies to EVERY open seat and signs every OFFERED one (default).
                             // applying isn't optional, it's mandatory: ResolveDecisionWeek only judges what's
                             // IN DISPUTE, and a seat isn't disputed until you apply for it.
};

struct SimWorldResult {
    int days = 0;
    int fromYear = 0, fromDoy = 0, toYear = 0, toDoy = 0;
    int races = 0, windows = 0, signings = 0, applications = 0, breaks = 0, droughtDays = 0, rollovers = 0;
    // phase 7 (diagnostics): the discard's BREAKDOWN. A bare total was worthless -- measured on 2026-08-05,
    // dominating produced 109 discards and struggling produced 104, and from that it was concluded "disputes don't respond to
    // performance" without knowing they could be two OPPOSITE causes giving the same number: the dominant one gets knocked out by
    // the standings table (the cohort), the struggling one gets knocked out by the bar (the requirement). Two different fixes.
    int rejBar = 0, rejTable = 0, rejBoth = 0, rejRenew = 0;
    // ABSENCES AND BREACHES. Applying to EVERY open seat (the greedy policy) fills the calendar with
    // series whose weekends clash with each other: the overlapping round doesn't run, counts as an absence, and at three
    // the contract breaks on its own. A bare `breaks` count didn't distinguish that from a season's normal end.
    int noShows = 0;          // contracted rounds lost (the calendar clash, CHARGED)
    int clashSkipped = 0;     // rounds left unrun from clashing with another date (the clash, OBSERVED)
    int signFails = 0;        // OFFERED seats that couldn't be signed (the reason lives in the event)
    // phase 8: the duel with your teammate, across the whole racing career. `mateBeefMax` is the peak
    // grudge reached: it rises when they beat you and falls when you beat them, so the max counts if there was ever
    // a real rivalry at some point, even if it's dormant today.
    int mateW = 0, mateL = 0, mateBeefMax = 0;
    int mateWhy[6] = {0,0,0,0,0,0};   // tally by reason for NO duel (index = TeammateDuel::why)
    int dnfMech = 0;          // ON-TRACK DNFs from mechanical failure
    int dnfCrash = 0;         // ON-TRACK DNFs from an incident (the "collision DNF")
    int maxConcurrent = 0;    // active contracts at once at the peak -- how many fronts were open
    int breaksExpired = 0;    // the contract expired when the year rolled over (normal, doesn't hurt)
    int breaksNoShow  = 0;    // auto-break from accumulating absences (the calendar clash, charged)
    int breaksOther   = 0;    // any other cause
    double fitMarginSum = 0.0;   // sum of (player fit - best cohort fit) over judged disputes
    int    fitMarginN   = 0;     // how many, so it can be averaged without a zero lying about it
    std::vector<SimWorldEvent> timeline;
    SimState endState;
};

// Simulates FROM ONE DATE TO ANOTHER. It's requested by date, not by a number of weeks, because that's how the
// question is framed ("from here to March 2028"), and because a week counter forces doing the math by hand every
// time. Internally it advances in jumps of at most 7 days, and the last jump is the remainder, so it lands
// EXACTLY on the requested day instead of overshooting. If the target date has already passed or is today, it does nothing.
//
// MUTATES cc and save: whoever doesn't want to lose their save should work on a COPY (which is what the app's
// debug command does). Deterministic via baseSeed.
SimWorldResult SimulateWorld(CareerCalendar& cc, SaveData& save, const std::string& playerName,
                             int toYear, int toDoy, std::uint64_t baseSeed,
                             int signPolicy = SIMSIGN_GREEDY, int perf = SIMPERF_MID);

} // namespace career
