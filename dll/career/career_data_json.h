#pragma once
// career_data_json — an adapter that reads the season-JSON model (docs/career/*.json +
// docs/career/seasons/*.json) and FILLS the SAME engine globals that the
// PSV loaders fill (career_series/career_fields/career_econ), matching the schema mapping identically. That way the
// validated ComposeSeasonGrid -> CustomAI XML -> Settle -> calendar/standings chain stays
// byte-for-byte identical, just fed from JSON instead.
//
// The g_useJsonCareerData flag (default OFF) decides in production whether this path or the 7
// PSV Load* calls are used. Exposed so career_test can flip it on and compare for parity.
#include <string>
#include <vector>

namespace career {

// Global flag: production uses JSON only if it's true. Default OFF (behavior unchanged).
extern bool g_useJsonCareerData;

// Reads the 6 JSON sources from <careerDir>/ (drivers/points/profiles/econ + classes ignored) and
// <careerDir>/seasons/*.json, and REPLACES the engine's globals (Series/Points/Calendar/Profiles/
// Fields/Roster/Pinned/Econ) with data identical to the PSV's. Returns true if everything loaded.
// careerDir with NO trailing slash (e.g. TESTDATA("career")).
bool LoadCareerDataFromJson(const char* careerDir);

// A queryable IDENTITY pool (source of truth for name/country/kind per driver_id) ----
// Loaded from docs/career/drivers.json LAZILY and ONCE (a static guard), INDEPENDENT of
// g_useJsonCareerData: identity resolves in PSV mode too (production). It's ADDITIVE: it doesn't touch
// any engine global or the existing PSV/JSON path.
struct PoolDriver {
    std::string id, name, country, kind;
    // Cast fields (only populated for kind=="roster" — the recurring rk_* cast; the real rd_*
    // entries don't carry them and stay at their defaults). growth_archetype is INERT LEGACY
    // (scheduled skill growth was removed; it's still parsed for back-compat/lore).
    std::string growth_archetype;   // "anchor_high"|"anchor_low"|"variable" ("" for real drivers) — INERT
    double base_skill = 0.0, base_aggression = 0.0, base_consistency = 0.0;   // stats_base.{skill,aggression,consistency}
    int tier_min = 1, tier_max = 7;
    bool hot = false;
    int birth_year = 0;    // JSON "birth_year" (0 = unknown -> procedural)
    int retire_year = 0;   // JSON "retire_year" (0 = procedural)
};

// Canonical identity by id; nullptr if the id isn't in the pool. The pointer is stable for the
// life of the process (the index is static and never rebuilt after the first load).
const PoolDriver* DriverById(const std::string& id);

// The driver's growth archetype (a string from the pool); "" if the id isn't found or isn't in the cast.
// INERT LEGACY: nothing in production consumes it anymore (skill growth was removed).
std::string GrowthArchetypeFor(const std::string& id);

// A driver's authored birth/retirement year (0 if the id isn't found or the data is missing).
int BirthYearFor(const std::string& id);
int RetireYearFor(const std::string& id);

// Pool diagnostics for the test harness: how many drivers already carry a birth year, and whether all
// of those fall within a sane range. Without ages, the AgeCurveMultiplier curve never
// gets exercised, so COVERAGE is an invariant, not a curiosity.
int  PoolDriversWithBirthYear();
bool PoolBirthYearsAllWithin(int lo, int hi);
// How many drivers in the pool already have at least 'minAge' in 'year'. It's the invariant that decides whether
// the world can turn over: if nobody's approaching the retirement threshold, no seat ever opens up.
int  PoolDriversAgedAtLeast(int anio, int edadMin);

// All the pool's ids, in deterministic order. Used by the rookie generator to
// recombine REAL first/last names by country instead of shipping a curated
// names file: a Brazilian rookie gets a Brazilian name, with no extra data to keep up to date.
std::vector<std::string> PoolDriverIds();

// DISPLAY NAME for any driver, including ones INVENTED by the living world.
//
// A generated rookie isn't in drivers.json, so DriverById returns nullptr and the sixteen places
// in the engine that resolve id->name used to fall back to the RAW ID: the world ranking would have shown the player
// a "gn_2025_1160919266". No green test ever caught it — the harness compares data, it doesn't read screens.
//
// These are registered SEPARATELY from the pool on purpose. Folding them in would change PoolDriversWithBirthYear and other
// counts mid test run, and a driver invented in one playthrough has no reason to exist
// in the next one: the registry is cleared on save load and repopulated with that save's own entries.
// country is optional — the registry used to keep only the name, and grid re-seating would leave the
// previous occupant's flag (a name from the SWE pool under an ITA flag, the Olofsson case). All three call sites that register names
// (EnsureTailNames, CascadeGenerateRookies, save loading) have the country on hand and pass it; a
// legacy caller with no country still compiles and simply contributes no flag.
void RegisterGeneratedName(const std::string& id, const std::string& name,
                           const std::string& country = std::string());
void ClearGeneratedNames();
int  GeneratedNameCount();

// Pool -> the generated-drivers registry -> the id as a last resort (never returns empty unless the id itself is empty).
std::string DriverDisplayName(const std::string& id);
// the flag via the same path (pool -> registry); "" if nobody knows it.
std::string DriverDisplayCountry(const std::string& id);

// Reverse name->id lookup. If several ids share a name, disambiguates by country (if given); otherwise,
// returns the first one deterministically (id order). "" if the name isn't in the pool.
std::string DriverIdForName(const std::string& name, const std::string& country = std::string());

} // namespace career
