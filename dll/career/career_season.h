#pragma once
#include <string>
#include <vector>
#include "career_standings.h"   // Standings, ApplyRound, FinalTable, Save/LoadStandings
#include "career_engine.h"      // CareerProgress, RaceResult, SettleReport, Settle, XpThreshold, CheckPhaseUnlock
#include "career_results.h"     // ClassifiedEntry
#include "career_series.h"      // SeriesDef, SeriesById, CalendarForSeries, PointsById, AllSeries
#include "career_customai.h"    // LiveriesForClass

namespace career {

struct RoundOutcome { int round = 0, pos = 0, field = 0; bool dnfFlag = false; };   // your result per round (for playerForm)

struct SeasonState {
    std::string series_id, branch;
    int season_year = 1;
    int round_idx = 0;            // 0-based index of the NEXT round; == number of rounds when finished
    int status = 0;               // 0 active, 1 finished
    // season entry is deferred to the race COMMIT (not to Enroll). false =
    // still owed (charged when you press Rent/Race); true = already paid or covered by contract (waived).
    // Old saves (5-field E line) load as true = already paid under the old model (back-compat).
    bool inscription_paid = false;
    // item 5 (skin=team): car+skin identity LATCHED at the series' 1st race -> subsequent
    // rounds reuse it (skin locked, car pre-selected). -1/0 = not yet set. livery_ord = the ORDINAL
    // in CareerVehicleLiveryList for the livery_veh car (vehHash); the two travel together (an ordinal only makes sense
    // for its own car). Signing a new contract in the series RESETS it (you'll use the new team's livery).
    int  livery_ord = -1;
    int  livery_veh = 0;
    Standings standings;          // championship table
    std::vector<RoundOutcome> history;
};

struct RoundPlan {
    bool valid = false;
    int round = 0;
    long track_hash = 0;
    std::string track_name, class_name, profile_id;
    int fmt = 0, laps = 0, durationMin = 0, hour = 12, weather = 0, gridSize = 0;
};

struct SeasonOutcome { int player_pos = 0, field = 0; long money = 0; int xp = 0, rep = 0; };

SeasonState StartSeason(const std::string& seriesId, const std::string& branch);
RoundPlan PlanRound(const SeasonState& s);
// CAREER-wide form — average of prog.recent_form (finish quality across ALL series); 0.7 if empty.
double CareerForm(const CareerProgress& prog);
// SERIES-level playerForm. careerForm (default 0.7) calibrates a series with short history: empty history ->
// returns careerForm; 0 < size < K -> blends local/career weighted by size/K (less history = more career);
// size >= K -> pure local (no change). The 0.7 default leaves existing callers/tests untouched.
double PlayerForm(const SeasonState& s, double careerForm = 0.7);
// SettleRound now THREADS coverage/dealSalary (it used to drop them — the core gap)
// plus the new deductible/travel/resultBonus terms through to Settle. ALL new params are trailing-DEFAULTED
// (mirroring the noShowSeason precedent) so every pre-existing call site compiles byte-identical; the live
// overlay call sites that supply real profile-derived values are wired in separately.
SettleReport SettleRound(SeasonState& s, CareerProgress& prog, const RaceResult& r,
                         const std::vector<ClassifiedEntry>& classification, const std::string& flName,
                         long winnerPrize, long floorPrize, bool rented, int rentPct, long carValue,
                         long entryOverride = -1, bool factorySeat = false,   // passes through to Settle
                         long coverage = 0, long dealSalary = 0,              // previously DROPPED -> now threaded
                         long deductible = 0, long travel = 0, long resultBonus = 0,   // new terms
                         int raceLaps = bal::COST_REF_LAPS,   // LAP COUNT -> scales the player's own car's consumables (default = reference)
                         const WearTelemetry* wear = nullptr, // REAL wear (from shared memory) -> rescales consumables; nullptr = nominal (back-compat)
                         long repBonus = 0);   // passes through to Settle (the event's REP bonus)
bool SeasonComplete(const SeasonState& s);
// noShowSeason gates the REP floor. When the player was a no-show across the whole season
// (defined as: the contract's missed_rounds == the series' round count — NOT playerPos==fieldSize, which would wrongly
// penalize a partially-raced season), the +1 REP floor is LIFTED so a no-show season may land <=0 REP (the
// organic overlap penalty finally bites). DEFAULTED false -> all pre-existing call sites compile unchanged.
// seasonPurseOverride > 0 => PER-CLASS champion bonus (from SeriesEcon.champion); 0 => fallback to the per-tier table.
SeasonOutcome ChampionshipBonus(int playerPos, int fieldSize, int level, bool noShowSeason = false, long seasonPurseOverride = 0);
// nemesis — the (stable) driver_id of the HOT cast driver whose final position is CLOSEST to the
// player's. Maps each StandingRow.name -> driver_id (DriverIdForName) and filters by DriverById(id)->hot.
// Tiebreak: smaller |Δpos|, then better (lower) final position, then lexicographic driver_id. "" if there's
// no hot rival in the table, if playerPos is invalid (<=0), or if no name resolves to a hot id.
std::string ComputeNemesis(const std::vector<StandingRow>& finalTable, int playerPos);
// noShowSeason threads through to ChampionshipBonus's REP-floor gate. The PRODUCTION
// caller (with SaveData in hand) passes (contract.missed_rounds == series rounds); DEFAULTED false keeps the
// pure season tests + the simrunner caller compiling unchanged (a non-contracted season is never a no-show).
SeasonOutcome FinishSeason(SeasonState& s, CareerProgress& prog, const std::string& playerName,
                           bool noShowSeason = false);
std::vector<const SeriesDef*> AvailableSeries(int frontierPhase);
bool SaveSeason(const SeasonState& s, const char* path);
bool LoadSeason(SeasonState& s, const char* path);

} // namespace career
