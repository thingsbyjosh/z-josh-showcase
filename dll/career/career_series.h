#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include "career_scoring.h"   // career::PointsSystem (reused, not duplicated)

namespace career {

// A real series (championship). One AMS2 class can host several (US/EU branches, eras).
struct SeriesDef {
    std::string id;          // unique series_id, e.g. "nimbus_challenge_4988"
    std::string class_name;  // catalog class (career_customai's key), e.g. "Onyx_Cup_4824"
    std::string display;     // "Valemont Fal38 — NIMBUS_MASTERS_5890 CINDER_SERIES_5357"
    std::string region;      // "US" | "EU" | "BR" | "INT"
    std::string era;         // "2024", "1991", "1988"…
    std::string profile_id;  // FK to ConfigProfile
    std::string points_id;   // FK to PointsSystem
    std::string branch_of;   // parent series_id if this is a branch (or "")
    int rounds = 0;
    int level = 1;           // tier 1-7 (from the prototype's level); gating + tier for ComposeSeasonGrid
    std::string kind = "champ";   // "champ" (default) | "standalone" (one-off event / qualifier)
    std::string unlocks;          // series_id of the championship this opens as a qualifier for; "" = not a gate
    int gate_top = 0;             // top-N threshold to pass the gate (only if unlocks != "")
    std::string gate_feeder_class;  // Feeder class for the SEASON path ("" = no season path)
    int gate_feeder_top = 0;        // top-N for the feeder season (>0 if a class is set)
    bool closed = false;            // access: true = CLOSED grid (team contract only); false = OPEN (privateer)
    // from the portal (19/09/2026): CAR RULE. 0 = the whole class (as always). Any other value = veh_hash
    // (docs/ams2_vehclass.psv) of the ONLY car the series runs (docs/series_cars.psv): the player can only use
    // that car (pre-race/purchase/rental/team seat) and the rivals go on «Identical» (VM498 s31 = 0) if the
    // race is single-class. A SUBSET of cars is not offered by AMS2: it stays out.
    int car_veh_hash = 0;
    // 13/08 (user's decision) — DOES THIS SERIES HAVE A LIVE MARKET? EXPLICIT tag, from
    // docs/series_market.psv. 0 = no row (falls to the old criterion by year) · 1 = LIVE · 2 = PERIOD.
    //
    // It replaces deciding it with `era >= MODERN_ERA_CUTOFF`, because a threshold by year doesn't distinguish «old
    // because there is already a new one» from «old because it's the only one of its discipline». Measured: with the cut
    // at 2015, the 2015 and 2019 F1 were live market and Vettel competed for seats in the current F1; and
    // raising the cut to 2023 would have frozen the WHOLE Brazilian endurance ladder —four rungs,
    // levels 3 to 6— and MERROWDOWN_MYR373, which have no modern successor.
    int market_tag = 0;
    // 14/08 — THE ECONOMIC SCALE OF THIS SERIES, multiplier over that of its LEVEL (1.0 = no row
    // in docs/career/series_scale.psv). It is needed because level 7 is EIGHT series and they all paid the
    // same: with no economic advantage, F1 didn't attract aces any more than HARBOR_CHALLENGE_8227 or NIMBUS_MASTERS_5890, and of the 16
    // stars who reach the top in twenty seasons TWO fell to it.
    double econ_scale = 1.0;
};

// One round of a series' calendar (the format reuses ams2_calendar.psv).
struct CalRound {
    std::string series_id, track_name, note, date;   // date = "Jun 15" (display); "" if the series has no dates yet
    int round = 0;
    int doy = 0;             // day of year 1..366; 0 = a series with no dates (excluded from the master calendar)
    long track_hash = 0;     // the REAL AMS2 hash (like CalEvent in career_state.h)
    int fmt = 0;             // 0=sprint 1=feature 2=endurance
    int laps = 0, durationMin = 0;   // laps XOR durationMin
    int hour = 12;
    // from the portal (19/09/2026): AUTHORED weather of the round as a LIST of 1..4 slots from the catalog (0..17,
    // 0 = clear). EMPTY = «the player decides» (their Settings mode). Before it was an int with 0 = «no data», so
    // a round couldn't author clear. PSV cell: "" | "8" | "0,8,12,17" (career::ParseWeatherList).
    std::vector<int> weather;
    std::string tag = "real";   // "real" (default) | "whatif" | "sust"; derived from the note if the column is missing
    int pitKmh = 0;        // As of Jul 28, a per-round override for the pit limit (0 = the profile wins). A CIRCUIT fact (Blackgate Speedway).
    int practiceMin = 0;   // per-round override: 0 = use profile default; >0 = explicit minutes
    int qualifyMin  = 0;   // per-round override: 0 = use profile default; >0 = explicit minutes
    int gridSize = 0;      // total of the event: 0 = inherited; 2..60 = explicit exception
};

// Contract shared by both loaders. Values outside 2..60 degrade to inherited (0).
int NormalizeRoundGridSize(int value);

// Realistic config profile (free key->value fields; the engine reads whichever keys it needs).
struct ConfigProfile {
    std::string id;
    std::map<std::string, std::string> fields;
    std::string get(const std::string& key, const std::string& def = std::string()) const;
};

// Loading (each loader clears its own table; false if the file doesn't open) ----
bool LoadSeries(const char* path);
bool LoadSeriesPoints(const char* path);
bool LoadSeriesCalendar(const char* path);
bool LoadProfiles(const char* path);
// access: loads docs/series_access.psv (series_id|access; '#'-comments + header). Sets SeriesDef.closed=true
// for every row with access=="closed"; everything else stays OPEN (default). MUST be called AFTER LoadSeries/
// ReplaceSeries (it operates on the already-populated g_series). Idempotent. false if the file doesn't open.
bool LoadSeriesAccess(const char* path);
// 13/08 — the MARKET TAG per series (docs/series_market.psv): "series_id|viva|epoca". It replaces the
// cut by year of `IsModernSeries`. No row -> old behavior. MUST be loaded after LoadSeries.
bool LoadSeriesMarket(const char* path);
int  SeriesMarketRowsApplied();
// 14/08 — the ECONOMIC SCALE per series (docs/career/series_scale.psv): "series_id|escala|motivo".
// Only exceptions are listed; no row, 1.0. MUST be loaded after LoadSeries.
bool LoadSeriesScale(const char* path);
int  SeriesScaleRowsApplied();

// Parallel ingest: the JSON adapter REPLACES the global table with data that's already
// built, identical to the PSV loader's. They don't touch the PSV path; they only swap the global. ----
void ReplaceSeries(std::vector<SeriesDef> v);
void ReplacePoints(std::map<std::string, PointsSystem> m);
void ReplaceCalendar(std::vector<CalRound> v);
void ReplaceProfiles(std::map<std::string, ConfigProfile> m);

// Lookups ----
const SeriesDef* SeriesById(const std::string& id);                 // nullptr if it doesn't exist
std::vector<const SeriesDef*> SeriesForClass(const std::string& className);
const std::vector<SeriesDef>& AllSeries();
int SeriesCount();
// access: number of rows in series_access.psv that FOUND their series in g_series and had the field
// closed applied to it, in the last load that opened the file (a row naming a nonexistent series doesn't count). There's
// no table of its own (LoadSeriesAccess mutates g_series), so the counter lives in a separate module. A load
// that fails to open the file leaves the previous count untouched, just like it leaves g_series untouched.
int SeriesAccessRowsApplied();   // number of rows loaded (diagnostics/test)
// from the portal: the CAR RULE per series (docs/series_cars.psv): "series_id|veh_hash|note". Same pattern
// as LoadSeriesAccess (separate file that mutates g_series; no row = the whole class; MUST go after LoadSeries).
// Optional: without the file it doesn't block startup. Rows with a nonexistent series or non-numeric/0 veh_hash don't count.
bool LoadSeriesCars(const char* path);
int  SeriesCarsRowsApplied();
// from the portal: type of rivals the car rule imposes. 0 (Identical, VM498 s31) only if there is a fixed car
// AND the race is single-class (numClasses <= 1); -1 (auto by numClasses) otherwise. In a multiclass weekend Identical
// would empty the other classes, so the rule only reaches the player and the rivals stay on auto.
int OppTypeForCarRule(int carVehHash, int numClasses);
// Task 7, fix round 1: VALUE that CareerApplyEvent writes into VM498 s31 (OpponentsType). It is a raw INDEX,
// verified live on 17/06: 0 Identical, 1 Same class, 2 Multiclass. A single rule for both paths of the
// overlay: multiclass (numClasses > 1) -> Multiclass always; otherwise oppType >= 0 rules (the contract test,
// with numClasses 0, and the single-vehicle, with numClasses 1, ask for Identical); otherwise Same class. Before the path with
// numClasses > 0 ignored oppType and the car rule never reached the rivals.
constexpr int kOppTypeIdentical = 0, kOppTypeSameClass = 1, kOppTypeMultiClass = 2;
int OppTypeSlotValue(int numClasses, int oppType);
const PointsSystem* PointsById(const std::string& id);              // nullptr if not
int PointsSystemCount();   // number of rows loaded (diagnostics/test)
std::vector<CalRound> CalendarForSeries(const std::string& seriesId);   // sorted by round
const std::vector<CalRound>& AllCalendarRounds();   // the FULL calendar table (appending events)

// FIX (Aug 1, reported as: "I won the Oakfield Motors round and got 13 points instead of 25"). A round's points factor:
// 0.5 for SPRINT, 1.0 for everything else. But the half-point only means something if the championship ALSO
// runs rounds of another format -- there, the sprint is worth less than the long race, just like real life. In a
// championship where EVERY round is sprint (Oakfield Motors Lum695, Kingsmere, trucks, karts: 13 series in the
// catalog, versus 3 that do mix formats) there's nothing to compare against: those races just ARE the championship, and halving
// them all wouldn't change any standings, it would just make a win worth 13. They score in full.
// Used by BOTH paths that score a round (the player racing, and the round simulated without them): if they diverged,
// the player's standings and the AI's would stop adding up the same way.
double RoundPointsScale(const std::vector<CalRound>& cal, int roundIdx);
// Same rule keyed by (series, round number), for callers without the calendar in hand -- e.g. the
// query.calendar feed, which goes slot by slot. Unknown series or round -> 1.0 (it never invents a discount).
double RoundPointsScaleFor(const std::string& seriesId, int round);
const ConfigProfile* ProfileById(const std::string& id);           // nullptr if not
int ConfigProfileCount();   // number of rows loaded (diagnostics/test)
// The value of a profile field ("" if the profile or key doesn't exist). E.g. ProfileField("keystone_cup_6095","start")="roll".
std::string ProfileField(const std::string& profileId, const std::string& key);
// Start type of the event, by series: 1=rolling ONLY if its profile has start=roll;
// 0=standing start for everything else (no series/profile/key, or an unknown value).
int StartTypeForSeries(const std::string& seriesId);
// REALISTIC race rules (pit stop/refueling/formation lap/min-tires),
// read from the series' ConfigProfile (keys formlap/pitreq/refok/mintyres, researched by analogy
// with the REAL category). Each field: -1 = don't force (game default); 0/1 = force off/on; mintyres is an integer.
// A missing field or no profile -> -1. launchSlot consumes these to fill in the handoff's CareerEventParams.
struct RaceRules { int formationLap = -1; int mandatoryPit = -1; int refuelling = -1; int minTyres = -1; int pitSpeedKmh = -1; };
RaceRules RaceRulesForSeries(const std::string& seriesId);
// As of Jul 28, PER-ROUND OVERRIDE. The profile describes the category's BASE rule, but some championships have
// calendars that mix incompatible formats: 60s historic touring runs 12 laps at Blackmoor Park AND
// Wildham 24h in the same season, under a single club-sprint profile. One value per series can't be
// correct for both, so the round's ACTUAL FORMAT (CalRound.fmt) adjusts the base rule:
//   · fmt=2 (endurance): an hours-long race can't be run without a pit stop. It forces a mandatory stop and
//     allows refueling, with free tire choice (the stop exists; changing tires is strategy).
//   · fmt=0 (sprint): a 15-lap support race has no mandatory stop. If the base rule imposed one,
//     it's removed. Never the other way: a sprint never gains rules, it only loses them.
//   · fmt=1 (feature): the base rule stands, untouched.
// pitKmhRound > 0 (column base+10 of the calendar) always overrides the profile's pit limit: it's a
// CIRCUIT fact (Blackgate Speedway), not a category one. 0 = no override.
RaceRules RaceRulesForRound(const RaceRules& base, int fmt, int pitKmhRound);
// Event weather, ALWAYS 4 slots in out[4]. AMS2's enum (fully cataloged as of Jul 17; see
// app/src/events/eventHelpers.ts): 0 clear, 1-4 cloudy, 5 drizzle, 6 rain, 7 storm, 8 electrical
// storm, 12 hazy, 13 haze with rain, 14 dense fog, 15 dense fog+rain, 16 haze, 17 random.
// Priority: the calendar's data (calWeather>=0 -> 4x that value; 17 included) OVERRIDES the profile;
// with no data (calWeather<0): wx=dry -> 4x sun(0); dynamic/unknown/empty -> 4x random(17).
void WeatherSlotsForEvent(int calWeather, const std::string& profileWx, int out[4]);
// Number of weather slots (1..4), scaled to the session's duration, with randomness within the band:
// <=15 min 1-2 · <=60 min 1-3 · <=120 min 1-4 · >120 min fixed at 4. sessionMin<=0 (laps/unknown) -> 1-3.
int WeatherSlotCountForSession(int sessionMin);

// INSTALLED CONTENT (tracks/cars). The overlay populates the sets from the game's LIVE
// catalogs (a TrackCat/VehCat walk); the engine consults them to gate the calendar/pre-race screen. FAIL-OPEN RULE:
// an empty set (not yet populated / walk unavailable) => EVERYTHING counts as installed (zero false blocks).
void SetInstalledContent(const std::set<int>& trackHashes, const std::set<int>& vehHashes);
// per side, so the scan doesn't wipe out the side that hasn't resolved yet (or what was loaded from disk).
void SetInstalledTracks(const std::set<int>& trackHashes);
void SetInstalledVehicles(const std::set<int>& vehHashes);
// persistence of the detected catalog (PSV `kind|hash`, at the install path:
// paths::UserData("cache\\content_installed.psv")). Without this, DLC detection reset to zero on every
// launch, and the DLC screen was stuck waiting for a catalog nobody ever saved. Load requires the same
// threshold as the scan (>=50 per side) so it doesn't accept a half-written file; false = not applied.
bool SaveInstalledContent(const char* path);
bool LoadInstalledContent(const char* path);
// the EFFECTIVE sets also subtract products disabled in Settings>Content (career_content's
// overrides); the Raw variants see ONLY the live catalog (for query.content's detection).
bool TrackInstalled(int trackHash);     // effective: Raw && !TrackDisabledByOverride
bool VehicleInstalled(int vehHash);     // effective: Raw && !VehicleDisabledByOverride
bool TrackInstalledRaw(int trackHash);      // pure set (empty = fail-open true)
bool VehicleInstalledRaw(int vehHash);      // pure set (empty = fail-open true)
int  InstalledTrackCount();             // diagnostic (0 = not populated)
int  InstalledVehicleCount();
// Time progression index (VM550 **s27**, 1..11) for a duration %: the smallest step
// that COVERS speeding up 100/pct, so that in the shortened race ALL the hours of the
// real race are covered. Rounding UP by design decision (12/09): falling short makes you miss
// part of the day. pct<=0 or >=100 -> 1 (Real time).
// Table MEASURED in-game on 1.6.9.95 (12/09): 1=real 2=x2 3=x5 4=x10 5=x15 6=x20 7=x25 8=x30 9=x40
// 10=x50 11=x60. (The slot stopped being s21 in the 1.6.9.8 reloc; the old name misled.)
int PctToTimeProg(int pct);
int TimeProgMultiplier(int idx);   // s27 idx (1..11) -> multiplier (1,2,5,...,60); 0 out of range
// Applies the duration % to the event's copy. A race by TIME (durationMin>0): scales
// the minutes and RETURNS the s27 index (>0, forces the acceleration). Race by LAPS
// (durationMin<=0): scales the laps and RETURNS 0 (doesn't force time-acceleration). pct outside [25,100] -> 100.
int ApplyDurationPercent(int pct, int& durationMin, int& laps);

// Cross-validation: list of errors (empty = consistent) ----
std::vector<std::string> ValidateSeriesData();

// Resolves a session's minutes (practice/quali): if override_ is in (0,240], it's used;
// otherwise it returns fixedDefault (the engine's hardcoded value, e.g. 30 min / 15 min).
int SessionMinutes(int override_, int fixedDefault);

// Qualifying minutes derived from the racing CLASS (HARBOR_CHALLENGE_8227's quali isn't a Kingsmere's). A heuristic
// by discipline over the catalog's class_name. The write site (CareerApplyEvent) resolves the class via
// vehHash (ClassForVehicle). Replaces the flat, uniform 15 the calendar used to generate.
int QualiMinutesForClass(const std::string& className);

// Discipline derived from the series (band color + multiclass pairing rule):
// "entry" | "endurance" | "touring" | "single" | "gt" | "offroad".
std::string DiscForSeries(const SeriesDef* sd);

// CURATED multiclass: groups of series that share ONE grid (multiclass) when they co-locate
// (same circuit+day). Co-location alone does NOT merge them; only members of the SAME group. It's curated by
// series_id (not class_name) because the same class races at different meetings (Pemberton_Zel121 in both IVORY_CUP_12573 and NIMBUS_MASTERS_5890).
int MulticlassGroupOf(const std::string& series_id);                          // the group's index, or -1
bool SameMulticlassGroup(const std::string& a, const std::string& b);         // same group (index >= 0)
const std::vector<std::vector<std::string>>& MulticlassGroups();              // getter (save/restore in tests)
void ReplaceMulticlassGroups(std::vector<std::vector<std::string>> groups);   // setter (injection in tests)

// CARS ON TRACK for a multiclass weekend, CURATED PER GROUP. A meeting doesn't grid the sum of its
// classes (the IVORY_CUP_12573 would be 56 cars: 18 Pemberton_Zel121 + 22 LANTERN_MASTERS_7489 + 16 MERROWDOWN_MYR373, but the real IVORY_CUP_12573 is 36); it grids the
// CHAMPIONSHIP's size, split evenly among the classes present. The game already does the split with
// our fix to the grid builder (`mcdist`), which divides total/n; what was missing was the total.
// It's tied to the SERIES, not the group's index: ReplaceMulticlassGroups can reorder them, and an index-keyed
// table would hand the IVORY_CUP_12573's total to a different group. 0 = a series with no curated group -> the caller falls back to the sum.
int MulticlassTotalForSeries(const std::string& series_id);

// "Era memory" predicate (SINGLE SOURCE, shared by the Era Memories screen and the driver
// Market): true if the series belongs to a past era -- its `era` is a 4-digit YEAR before 2000 (ONYX TROPHY 560,
// Greyham Motors Lum998, Group A/C, HARBOR_CHALLENGE_8227 87/92, GARROWBY 95/98, ...). These get REVIVED as your ancestor (Era Memories), so
// the modern Market EXCLUDES them: you never get offered a normal-career contract for a pre-2000 grid (Market
// ⊕ Memories are complementary by the SAME criterion). Qualifiers (kind=="standalone") are filtered
// separately by each caller. The ONLY path to offer one of these classes is an EXHIBITION contract
// (HISTORIC_EXHIBITION), a separate generation path, not the normal GenerateOffers.
bool IsEraMemorySeries(const SeriesDef& sd);

// 01/10 ([tester], 0.7.1): EVENT-ONLY series (kind=="event"). It exists so that a special portal event can
// run a class that is NOT in the career (Hypercars, road Coldstead Motors, Pinewood Motors Dax366). Entirely outside the live world:
// no seats, no offers, no season simulation, no series rail or tree, and no grid XML of ours
// (the AI of that class is set by the game). Only CareerArmEvent arms it, like any other series of an event.
bool IsEventOnlySeries(const SeriesDef& sd);

// Derives the tag from the note's free text: "what-if"/"whatif"->whatif; "sust"->sust; everything else->real.
// Exposed so the JSON adapter (career_data_json) uses the SAME fallback as the PSV loader
// when the tag column/key is missing or empty (for parity: don't replicate the logic).
std::string DeriveTagFromNote(const std::string& note);

} // namespace career
