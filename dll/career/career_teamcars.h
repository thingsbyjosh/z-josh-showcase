#pragma once
// F2 (spec 2026-07-25 driver-model-living-grid): the CAR belongs to the TEAM.
// 1.6.9.9's scalars (weight/power/drag_scalar) do NOT belong to the driver: drivers sharing a material
// unit receive EXACTLY the same ones, so a great driver in a bad car ends up mid-pack.
//
// Two data files, both OPTIONAL (absent = the whole grid at 1.000 = usual behavior):
//   docs/career/series_car_dispersion.psv  key|tier|amp_pct|unit_kind|unit_src|rel_top|rel_bot|source
//   docs/career/team_cars.psv              key|season|unit|strength|reliability|liveries|note
//
// Modeled on career_classpace: idempotent loader, Replace* for the suite, one-shot guard-load and
// sane defaults with no crash if the file is missing.
#include <map>
#include <string>
#include <vector>
#include "career_customai.h"   // GridDriver (the scalars live on the grid entry)

namespace career {

// A class's car-dispersion level (how much the REAL series separates its material).
struct CarDispersion {
    std::string tier = "zero";       // high | mid | low | zero
    double amp_pct = 0.0;            // HONEST half-amplitude in % of scalar (the engine clamps it via kCarAmpCapPct)
    std::string unit_kind = "none";  // team | model | package | none (documentary)
    std::string unit_src  = "none";  // stem | authored | none  (where units come from)
    double rel_top = 0.98, rel_bot = 0.95;   // the class's reliability range (consumed by ApplyTeamCarReliability)
    // 2026-07-28 -- 9th column, OPTIONAL: the class's straight-vs-corner CHARACTER half-amplitude, in % of scalar.
    // It's a different axis from amp_pct: that one says how much the series separates its cars in PACE, this one how much it
    // separates them in WHERE they perform. Absent or 0 = no character (previous behavior, byte for byte). See the
    // long block in career_teamcars.cpp for where the weights come from and why they net to zero.
    double trim_pct = 0.0;
    bool found = false;              // true if it came from a PSV row
};

// A MATERIAL UNIT = one car = N drivers. Sharing is STRUCTURAL: there's a single number per row.
struct TeamCarRow {
    std::string unit;                 // with unit_src=stem, the EXACT stem of the vanilla livery
    int    season = 0;                // 0 = '*' (any season); a specific year WINS over '*'
    double strength = 0.0;            // -1.000 (worst material) .. +1.000 (best); 0.000 = middle
    double reliability = -1.0;        // <0 = no explicit override
    std::vector<std::string> liveries;   // only with unit_src=authored (the livery doesn't say who owns the car)
};

// load (idempotent; false if the file won't open, and the table stays empty) ----
bool LoadCarDispersion(const char* path);
bool LoadTeamCars(const char* path);
void ReplaceCarDispersion(std::map<std::string, CarDispersion> m);          // TEST-ONLY
void ReplaceTeamCars(std::map<std::string, std::vector<TeamCarRow>> m);     // TEST-ONLY
// One-shot guard-load from paths::Data (same style as EnsureClassPaceLoaded): called by the XML writer,
// which runs both after the overlay boots and from the offline generator and the suite. No-op if either
// table already has rows (a prior explicit Load*/Replace* wins) and doesn't retry if the file is missing.
void EnsureTeamCarsLoaded();
int  CarDispersionCount();   // number of keys loaded (diag/test)
int  TeamCarKeyCount();      // number of keys with unit rows (diag/test)

// Conservative launch cap (spec +-2%, not the +-10% the game allows) and the XML's legal range.
extern const double kCarAmpCapPct;
extern const double kScalarMin;
extern const double kScalarMax;

// vehicle_reliability sanity check. It's NOT a probability: it's a multiplier RATIO, and real packs have
// been measured with values above 1.0 (up to ~1.21), so clamping it to [0,1] would punish the reliable car.
// These two numbers only catch an authoring typo (a 9.5 for a 0.95), they don't calibrate anything.
extern const double kRelMin;
extern const double kRelMax;

// Split of the spread between driver and car (spec). Level only: the real amplitude comes from amp_pct.
double CarShareForTier(const std::string& tier);

// Key resolution: series_id row -> class_name row -> no row (found=false / empty vector).
CarDispersion CarDispersionFor(const std::string& seriesId, const std::string& className);
// The unit's rows for that season: '*' rows as the base, and the EXACT year's rows overriding unit by unit.
std::vector<TeamCarRow> TeamCarRowsFor(const std::string& seriesId, const std::string& className, int season);

// "Dundale Racing #6" -> "Dundale Racing";  "Ashwick Racing 10866 (NIMBUS_MASTERS_5890 2024)" -> "Ashwick Racing 10866".
// Strips, repeatedly and in any order, trailing parentheticals and a trailing " #<number>".
std::string LiveryStem(const std::string& liveryName);

// Applies car scalars to the grid, IN-PLACE, resolving the unit by livery_name.
// CRITICAL: must be called BEFORE RebindGridForSkins (career_skins.cpp), which rewrites livery_name to
// the installed skin's name. Resolved afterward, the stem stops being the team and becomes the
// driver's name: each car would fall into its own unit and dispersion would disappear SILENTLY, only for whoever
// has packs installed (doesn't reproduce in development). See gap A4 in the investigation.
// Degrades cleanly: no files, no class row, tier=zero, or unrecognized unit -> everything to 1.000.
// Returns how many cars end up with any scalar different from 1.000.
int ApplyTeamCarScalars(const std::string& seriesId, const std::string& className, int season,
                        std::vector<GridDriver>& grid);

// F2 (spec/investigation): vehicle_reliability BELONGS TO THE TEAM, not the driver. A material unit's
// reliability: the row's explicit override if it has one, otherwise LINEAR interpolation over the class's
// range by the unit's strength (strength -1 -> rel_bot, +1 -> rel_top, 0 -> the midpoint).
// It does NOT re-center over the composed grid, unlike the scalars: that re-centering exists so that
// a truncated grid doesn't inflate or sink the whole class's PACE, but reliability is an absolute value
// (how many times does this car finish?), and making it depend on who it races against would shift a class's
// DNF rate just by changing the number of rivals.
double ReliabilityForStrength(const CarDispersion& cd, double strength);

// Writes the TEAM's reliability onto the grid, IN-PLACE, resolving the unit EXACTLY the same way
// ApplyTeamCarScalars does (same VANILLA livery_name, same authored/stem), so unit teammates
// share the value by construction. Drivers whose unit doesn't resolve are left UNTOUCHED: they keep the
// reliability their archetype gave them (ReliabilityForArchetype), which is the usual behavior.
// Unlike the scalars, this does NOT look at tier: PACE dispersion and reliability are different
// axes (investigation), so a spec class with authored units still gets its reliability
// even though its scalars stay at exactly 1.000.
// Returns how many drivers received the team's reliability.
int ApplyTeamCarReliability(const std::string& seriesId, const std::string& className, int season,
                            std::vector<GridDriver>& grid);

// Spread split (spec): the measured spread from class_pace_curve.psv is the TOTAL gap, and it comes from
// two sources: what separates the drivers and what separates the cars. If F1 derives its k_class from the full
// spread AND F2 derives its amplitude from the full spread too, the grid comes out with DOUBLE the dispersion. This
// returns what's left for the DRIVER: the total spread minus what the car will actually contribute.
double DriverSpreadPct(const std::string& seriesId, const std::string& className, double spreadTotalPct);

}  // namespace career
