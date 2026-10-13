#pragma once
#include <string>
#include <vector>
#include <map>
#include "career_engine.h"   // SettleReport -- the per-race economy the season aggregator sums
namespace career {
// Money LEDGER: one "[money] HH:MM:SS <reason> +/-delta -> balance" line per EVERY mutation
// of the real prog.credits, into the SAME overlay log (the MktLogF pattern: portable fopen/append -- works the same in the
// DLL and in career_test). ALWAYS call it after a mutation, with the resulting balance. `grep "[money]"` = the full
// financial timeline for that user (a past bug was hard to see precisely because this didn't exist).
void EconLog(const char* reason, long delta, long after);

// Cross-cutting economy-engine parameters by PHASE. Prices/rentals/prizes
// per category live in the car data and the event; here is only what scales by phase.
long FloorForPhase(int phase);   // participation floor (a minimum prize even if you finish last)
long EntryForPhase(int phase);   // entry fee per race

// The GUARANTEED spend to get on the grid, per car action.
// The "can't afford it" gate requires credits >= this value; the PRIZE doesn't count (you could finish last), so
// you never end up negative after Settle. The car's cost mirrors Settle's formula EXACTLY:
//   Rent   -> inscriptionDue + entry + round(carValue * rentPct/100)         (rent = the same cost Settle charges)
//   Buy    -> inscriptionDue + entry + round(carValue * RUN_COST_PCT) + carValue   (run-cost + capital)
//   OwnCar -> inscriptionDue + entry + round(carValue * RUN_COST_PCT)        (run-cost for a car you already own)
// Factory seat / calibration race: the caller passes inscriptionDue=entry=0 (and skips the gate). Never < 0.
enum class EntryAction { Rent, Buy, OwnCar };
long RaceEntryOutlay(EntryAction action, long inscriptionDue, long entry, long carValue, int rentPct);

// AMENDMENT: the per-race cost of an OWNED car, broken into REAL components (bal::COST_*).
// Consumables (tires/fuel/fluids) scale with the race's LAPS (factor =
// laps/COST_REF_LAPS; a 40-lap race costs more than an 18-lap one), insurance is flat. total = sum of the 4 (each
// component rounded) -> the UI panel and Settle use EXACTLY the same number. carValue<0 -> 0; laps<0 -> 0.
// Down the line each component can be tuned/scaled independently (level/wear), with per-component coverage.
struct RaceCostBreakdown { long tyres = 0, fuel = 0, fluids = 0, insurance = 0, total = 0; };
RaceCostBreakdown OwnedRaceCosts(long carValue, int laps);

// (real wear): wear telemetry at the END of the race, read from the $pcars2$ shared memory by the
// overlay (the engine never touches shared memory directly: it's handed the values). Everything in 0..1. A NEGATIVE value = NO DATA (shared memory off,
// a broken read, or the exit fuel level couldn't be latched) -> that component stays at its NOMINAL value.
struct WearTelemetry {
    float tyreWear = -1.0f;   // the WORST tire's wear at the end (0 = new, 1 = worn out). mTyreWear
    float fuelUsed = -1.0f;   // fraction of the TANK used during the race (start - finish), not liters
};

// scales the CONSUMABLES from the nominal breakdown using the race's REAL wear. The nominal value already priced the
// LENGTH; this prices the relative PUNISHMENT against a normal race of that same length:
//   tires *= clamp(tyreWear / bal::WEAR_REF_TYRE, WEAR_MULT_MIN, WEAR_MULT_MAX)
//   fuel *= clamp(fuelUsed / bal::WEAR_REF_FUEL, WEAR_MULT_MIN, WEAR_MULT_MAX)
//   fluids + insurance: UNTOUCHED (damage is already billed via the deductible -> don't charge twice).
// A component with no data (value < 0) stays NOMINAL. With telemetry from a reference-length race
// (tyreWear == WEAR_REF_TYRE, fuelUsed == WEAR_REF_FUEL) it returns EXACTLY the nominal value (multiplier 1.0).
// total is RECOMPUTED as the sum of the 4 components. PURE, deterministic, no RNG, no shared memory.
RaceCostBreakdown ApplyWearToCosts(const RaceCostBreakdown& nominal, const WearTelemetry& tel);

// The race's NOMINAL length in "lap-equivalent" units, used to scale consumables. A
// lap-based race (laps>0) -> laps. A timed race (laps<=0 and durationMin>0) -> round(durationMin / COST_MIN_PER_LAP): a
// proxy using a typical minutes-per-lap value (a track-LENGTH-based estimate would be more accurate, but there's no
// length data available today). It does NOT apply the session-length % (that's a separate tweak, it doesn't change the economy). Returns >= 1.
int RaceLapEquivalent(int laps, int durationMin);

// the WEAR/CONSUMPTION multiplier the mod writes into the game's global options for a
// race of N EFFECTIVE laps (the ones actually run, after the length % is applied). It's inversely proportional
// to the length -- round(COST_REF_LAPS / laps) -- so the EFFECTIVE wear (laps x mult) stays
// roughly constant and tire strategy matters the same in a short race as in a long one.
// 1 = no scaling (reference-length race or longer): there, the player's own setting is respected.
// These used to live as overlay statics (GameplayWearMult and its two indices); they moved down into the engine so
// the pre-race screen can ANNOUNCE what it's about to apply without duplicating the formula.
// FIX: BOTH parameters are required. effLaps = the laps actually run (already with the length %
// applied); fullLaps = THIS race's FULL distance. The multiplier is fullLaps/effLaps, FLOORED,
// so that `effLaps x mult` never exceeds one full race's worth -- which is what fits in the tank and what
// the AI plans for. It used to scale against a fixed 20-lap constant and ask for impossible amounts of fuel; see the
// long comment in career_econ.cpp. fullLaps<=0 -> no scaling (conservative).
int WearMultForLaps(int effLaps, int fullLaps);   // unclamped (the cap depends on the axis)
int TireWearIdxForMult(int m);                    // index to WRITE with scaling ON (m<=1 -> Authentic; the scale always wins)
int FuelUsageIdxForMult(int m);                   // same for fuel (m<=1 -> x1; capped at x5)
int WearMultTyre(int effLaps, int fullLaps);      // tires: support x1..x7
int WearMultFuel(int effLaps, int fullLaps);      // fuel: only up to x5
// the length %s the pre-race screen offers. This is the CANONICAL list: the pre-race query publishes a
// multiplier for each one and the app just renders whatever comes back, so if its row of buttons ever drifted out of sync it would
// show "no data" instead of a made-up number. Ends with 0 (a sentinel) so it can be walked without knowing its size.
extern const int kDurationPcts[];

// Economy by CATEGORY (docs/ams2_series_econ.psv, emitted by tools/gen_econ.py) ----
// The table used to live in the UI layer (career_data.cpp), indexed ONLY by the ladder's node_id, but the live
// calendar flow looks it up by the catalog's class_name ("GT3" vs. the key "gt3") -> it NEVER resolved
// and always fell back to the level-based default. Now it lives in the engine (testable) and the join is EXPLICIT: the
// PSV's 'classes' column lists the catalog class_names (= series.psv::class_name) each row covers.
struct SeriesEcon {
    std::string node_id;               // the ladder's node_id (or its own id for series without a ladder node)
    int  phase = 1;                    // ladder phase/level (a gate; the economy itself is per category)
    std::string disc, model, icon;     // model: pocket|sponsor|factory|heritage  icon: an iconic prize purse, or 
    long comprar = 0, alquiler = 0, entry = 0, premio = 0;
    long champion = 0;                 // PER-CLASS champion bonus (0 = falls back to the tier table in career_season)
    std::vector<std::string> classes;  // the catalog class_names it covers (the calendar flow's join)
};
bool LoadSeriesEcon(const char* path);                      // parses the PSV (clears the table first); false if it can't open
// The parallel JSON ingest path REPLACES the global tables (economy by node_id +
// the class_name->node_id index) with data identical to the PSV loader's. It only swaps them in; it never touches the PSV.
void ReplaceEcon(std::map<std::string, SeriesEcon> econ, std::map<std::string, std::string> classToNode);
const SeriesEcon* FindEcon(const std::string& nodeId);      // by node_id; nullptr if absent
const SeriesEcon* FindEconByClassName(const std::string& className);  // by catalog class_name (the 'classes' column)
int SeriesEconCount();                                       // number of rows loaded (diagnostics/test)

// Intra-class variation by car, deterministic and stable (keyed by vehHash).
// The class base stays the anchor (the jitter averages to ~0). Factory cars / price 0 stay at 0.
long JitterPrice(long base, int vehHash);

// Per-CAR price override: a REAL differentiated value keyed by vehHash (docs/ams2_vehicle_prices.psv).
// VehiclePrice returns 0 if the car has no override -> the caller falls back to base+JitterPrice.
bool LoadVehiclePrices(const char* path);                   // parses the PSV (clears the table first); false if it can't open
long VehiclePrice(int vehHash);                             // the price override, or 0 if absent
int  VehiclePriceCount();                                   // number of overrides loaded (diagnostics/test)

// Compresses a value against its class base (bal::PRICE_SPREAD_CAP) -- feeds the
// per-race ECONOMIC value (CarItem.runValue), NEVER the buy price (a collector car's price is left alone).
// Inside the band (<= cap x base): untouched. Above it: base*(cap + ln(r/cap)), rounded to the nearest 1,000.
// vp<=0 or base<=0 -> vp untouched (no class means no band). Monotonic: preserves ordering.
long CompressPriceToClassBand(long vp, long classBase);

// For the "from X credits" showcase price: the REAL price of the class's cheapest car, not the base value from the
// economy row. Walks career::VehiclesForClass(className) (career_customai.h) and prices EACH car EXACTLY
// like CarsByClass (career_data.cpp) does: VehiclePrice(vehHash) if there's an override (>0), otherwise
// JitterPrice(base, vehHash) (JitterPrice already returns the ABSOLUTE jittered price, not a delta) -- base =
// FindEconByClassName(className)->buy price. Returns the minimum of those prices; with no cars (or an unloaded
// catalog) -> the base value. Pure except for reading the global tables (LoadSeriesEcon/LoadVehiclePrices/
// LoadVehClass), deterministic.
long CheapestCarPriceForClass(const std::string& className);

// Deal coverage <-> income (Living World) ----
// income_per_race = base * (0.7 + 0.6*q(R)) * (1 - coverage*k_cov).  q(R) = RatingQuality (career_rating).
// base   = the deal's base income for the seat/tier (caller supplies, e.g. SponsorTierBase or series base).
// qR     = quality in 0..1 from the unified rating (RatingQuality); clamped here defensively.
// coverage = fraction of the deal's costs covered by team/sponsors in 0..1 (more coverage => lower income).
// The trade: full coverage (entry-safe) => lower per-race income; you-carry-risk => higher income.
long IncomePerRace(long base, double qR, double coverage);

// Your-share of a race cost after sponsor/team coverage (Living World net = income - your-share).
// rawCost = the race's coverable cost (consumables/entry the deal assigns to coverage); covered = the credits the
// signed sponsors actually cover this race (sum of SponsorTraitCoverThisRace). Returns max(rawCost-covered,0).
long CoverageShare(long rawCost, long covered);

// A Living World money sink: the entry cost of a superlicense qualifier / dream-exhibition shot.
// A one-off credits charge by phase (heavier than the per-round EntryForPhase). Clamps phase to 1..7.
long QualifierEntryCost(int phase);

// The resolved coverage profile for a contract TYPE. -------------------------
// Pure POD. Fractions in 0..1 (the TEAM's covered share of that cost category). Resolved at runtime by
// ContractCoverageProfile(kind, archetype) -- the ~6 contract types are DERIVED, not stored.
// All magnitudes come from career::bal (COVERAGE_*); this struct just snapshots one type's row.
struct CoverageProfile {
    double seat_entry_cov   = 0.0;   // fraction of (rent/seat-fee + entry) the team covers
    double consumables_cov  = 0.0;   // fraction of tyres+fuel+maintenance the team covers
    double damage_cov       = 0.0;   // fraction of repair cost covered (NEVER the deductible -- that's always the player's)
    double travel_cov       = 0.0;   // fraction of the A->B leg covered
    int    salary_dir       = 0;     // 1 = you pay (buy-in path), 0 = none, +1 = team pays salary
    long   damage_cap       = 0;     // co-pay: a credits cap on the damage you eat before the team covers the rest (0 = no cap)
    bool   exclusive        = false; // Holding this blocks signing another
    double damage_share_base= 1.0;   // your deductible share (1.0 privateer .. 0.1 factory)
    bool   fixes_car        = false; // this contract type fixes the car (no selection)
};

// Map a contract (career::bal::CONTRACT_* kind 0..4 + career::ContractArchetype 0..9) onto its
// type index 0..5 (0 Self-funded .. 5 Factory/works). Pure, deterministic; clamps inputs.
int ContractTypeIndex(int kind, int archetype);

// The resolved coverage profile for a contract, from its kind+archetype. Pure, deterministic; reads
// only career::bal COVERAGE_* tables (no RNG, no globals). Out-of-range inputs clamp to a safe type.
CoverageProfile ContractCoverageProfile(int kind, int archetype);

// THE single authoritative test for "does this contract FIX the car?" (works/
// customer/co-pay/factory -- every type 1..5; only the pure self-funded privateer, type 0, lets the player
// pick). DERIVED from the PERSISTED kind+archetype, so it is stable across a save/load. This is the source of
// truth the prerace seat-LOCK (CareerSeatLockForSeries), the seat RESOLUTION (CalUiContractSeat) and the offer
// PREVIEW (CalUiOfferSeat) all gate on. NEVER gate this decision on Contract::team_car -- that field is a
// presentation-only hint that is NOT serialized (career_save.cpp), so it comes back EMPTY after any reload;
// gating on its emptiness used to silently drop the fixed car on every save/load (a real bug, GARROWBY_HEX394/juniper_cup_3635).
inline bool ContractFixesCar(int kind, int archetype) { return ContractCoverageProfile(kind, archetype).fixes_car; }

// Pure OFFER/contract PRESENTATION facets. -----------------------------------
// Each offer surfaces a TYPE LABEL + an "Includes" CHECKLIST (what the team covers vs. what YOU
// pay, per cost category). These are DERIVED, not stored -- the facets read the SAME ContractCoverageProfile
// fractions used at Settle, so the presentation can NEVER diverge from the economic reality.
// Consuming the negotiated terms: a more-coverage salary package (DealTerms.salaryPct BELOW the
// neutral 50 -- the "More coverage" pick) RAISES the shown coverage; this slice adds NO new demand math.
// Per-category booleans: covered = the team shoulders the bulk of that category for this type (fraction >= the
// COVERAGE_PRESENT_THRESHOLD). The four player-facing categories are: seat/entry, consumables
// (tyres+fuel+maintenance), damage repair, and travel. salaryDir mirrors CoverageProfile.salary_dir (-1 you
// pay a buy-in, 0 none, +1 the team pays a salary). typeLabel is the ContractTypeIndex 0..5 (clamped) so the
// UI maps it to the 6 taxonomy labels via TR(). Pure POD; no globals, no ImGui.
struct OfferPresentationFacets {
    int  typeLabel        = 0;     // ContractTypeIndex 0..5 (0 Self-funded .. 5 Factory/works)
    bool seatEntryCovered = false; // team covers the bulk of (rent/seat-fee + entry)
    bool consumablesCovered = false; // team covers the bulk of tyres+fuel+maintenance
    bool damageCovered    = false; // team covers the bulk of repair cost (the deductible is ALWAYS yours)
    bool travelCovered    = false; // team covers the bulk of the A->B leg
    int  salaryDir        = 0;     // 1 you pay a buy-in, 0 no salary, +1 the team pays a salary
    // the raw covered fractions (0..1) the booleans were derived from -- for a richer "70% covered" UI if wanted
    double seatEntryFrac  = 0.0, consumablesFrac = 0.0, damageFrac = 0.0, travelFrac = 0.0;
};
struct DealTerms;   // fwd (career_negotiate.h): the negotiated package the presentation CONSUMES
// Derive the offer's presentation facets (type label + per-category covered/you-pay) from its
// kind+archetype, reading the SAME ContractCoverageProfile used at Settle (presentation == reality). When
// `terms` is non-null and carries a more-coverage package (salaryPct below neutral), the shown coverage is
// RAISED proportionally toward full (this only consumes existing demand math, it adds none). Pure,
// deterministic; clamps inputs; `terms == nullptr` (sign "as-is" / Market base) uses the bare type profile.
OfferPresentationFacets DeriveOfferPresentation(int kind, int archetype, const DealTerms* terms);

// The player's per-race deductible for a contract TYPE (always the player's, never covered by the team).
//   deductible = round(DEDUCTIBLE_BASE * COVERAGE_DAMAGE_SHARE_BASE[typeIndex]
//                      * (DEDUCTIBLE_DAMAGE_SCALE * damageShareSeverity) * damageShareReducer).
// damageShareSeverity (0..1) = SHM component-damage severity this race; damageShareReducer (0..1, default 1.0)
// = the OPTIONAL damage-responsibility reducer (1.0 = none). Clamps typeIndex + the 0..1 inputs.
long DeductibleForRace(int typeIndex, double damageShareSeverity, double damageShareReducer = 1.0);

// The season insurance PREMIUM for one held contract, priced by phase x the type's damage_share_base
// x a past-season cleanliness multiplier (a clean season pays LESS, a crash-prone season MORE). Charged once
// per held contract at rollover (RolloverYearReport, guarded by Contract.premium_year). cleanliness is the
// 0..100 EMA on CareerProgress; phase is the contract's series phase 1..7; typeIndex is ContractTypeIndex(...).
//   premium = round(INSURANCE_PREMIUM_BY_PHASE[phase-1] * COVERAGE_DAMAGE_SHARE_BASE[typeIndex]
//                  * lerp(CLEAN_MULT_MIN .. CLEAN_MULT_MAX, 1 - cleanliness/100)).
// Pure, deterministic; reads only career::bal. Clamps phase/typeIndex/cleanliness; never < 0.
long InsurancePremiumForContract(int phase, int typeIndex, double cleanliness);

// The travel cost of an A->B leg for a contract TYPE, by country/continent proxy (no coordinates).
// Same country -> TRAVEL_SAME_COUNTRY; same continent, different country -> TRAVEL_SAME_CONTINENT; else
// TRAVEL_CROSS_CONTINENT. FIX: REALISTIC PERSONAL travel magnitudes (~independent of the tier);
// the difference by tier comes from the contract's COVERAGE (COVERAGE_TRAVEL[typeIndex]), applied INSIDE (net of
// coverage). Empty from* (first leg) -> the origin is the caller's responsibility (nationality). Pure, deterministic;
// clamps typeIndex.
long TravelLegCost(int typeIndex, const std::string& fromContinent, const std::string& fromCountry,
                   const std::string& toContinent, const std::string& toCountry);

// A circuit's geographic proxy (country + continent) for the travel leg cost, keyed by track hash.
// Source = docs/circuits_ref.psv (header `hash|name|country|continent|...`, cols 1/3/4). Pure POD.
// extra fields for track substitution (name/type/grade/slug/eraYear from circuits_ref).
// Additive: previous consumers (the travel-cost code) only use country/continent.
// 19/09: the year with which the "modern" era of circuits_ref.psv is read (it used to fall to 0 and any historic
// circuit with a year beat it in the substitution; see LoadCircuitGeo).
constexpr int kModernCircuitEraYear = 2020;
struct CircuitGeo {
    std::string country, continent;
    std::string name, type, grade, slug;   // cols 1/4/5/8 of circuits_ref.psv
    int eraYear = 0;                       // col 6 parsed as a year ("modern" = kModernCircuitEraYear; 0 = other non-numeric: "historic")
    std::string flags;                     // col 10 (optional): "nosub" = never use as an automatic substitute
};
// Parse circuits_ref.psv into the hash->geo table (clears the table first); false if it can't open. Mirrors
// LoadSeriesEcon's split-keep + BOM strip + field-count guard. Unparsable/short rows are skipped.
bool LoadCircuitGeo(const char* path);
// Resolve a track hash -> its country/continent. Returns false (and leaves out untouched) when the hash is
// unknown / the table is empty -> the caller treats an unknown circuit as the safe cross-continent tier.
bool FindCircuitGeo(long trackHash, CircuitGeo& out);
int  CircuitGeoCount();   // nr of rows loaded (diag/test)
// full hash->geo view, for track substitution (career_content) and query.content.
const std::map<long, CircuitGeo>& AllCircuitGeo();
// the circuit's DLC pack (the `dlc` column of circuits_ref.psv). "" = base/unknown content.
// Only NAMES the pack (for "requires DLC X"); whether it's INSTALLED is answered by career::TrackInstalled.
std::string TrackDlcPack(long trackHash);

// The player's HOME geo for the season's FIRST leg, from the identity-screen nationality ISO code
// (the codes in screen_identity.cpp kCountries: "ES"/"BR"/"US"/...). Returns the matching circuits_ref-style
// {country, continent} (Spanish names, to compare against FindCircuitGeo output). An empty/unknown code yields
// an empty geo -> the first leg degrades to the safe cross-continent tier. Pure, deterministic.
CircuitGeo NationalityGeo(const std::string& natCode);

// Multi-contract SEASON aggregation. ------------------------------------------
// The no-cap multi-contract reality means a season can touch several held contracts, each race
// resolving to ITS owning contract's type (the live path already does that per race -- career_overlay.cpp).
// This is the pure building block for the season-total UI/Results: it groups the per-race SettleReports by
// their owning contract and sums each contract's economy SEPARATELY (no cross-contamination of coverage
// between contracts), then exposes the whole-season total = the straight sum of every race's net. Pure,
// deterministic; no globals. A SettleReport's contract owner is supplied alongside it as a stable key
// (the live caller passes the race's series_id / "<series>#<seat>" key).
struct SeasonRaceEconomy {
    std::string contract_key;   // owning contract key (e.g. series_id); groups races to their contract
    SettleReport report;        // the per-race settle breakdown (already type-resolved at Settle time)
};
// One contract's season subtotal: the summed money terms across all its races + the net (= sum of nets).
struct ContractSeasonTotal {
    std::string contract_key;
    int   races        = 0;
    long  prize        = 0;     // summed gross prize across this contract's races
    long  deal_salary  = 0;     // summed per-race salary (the type's salary)
    long  result_bonus = 0;     // summed low-type result bonuses
    long  coverage     = 0;     // summed team coverage applied
    long  costs        = 0;     // summed race costs (rent/consumables)
    long  deductible   = 0;     // summed insurance deductibles (always the player's)
    long  travel       = 0;     // summed travel-leg costs (net of the type's covered fraction)
    long  net          = 0;     // this contract's season net = sum of its per-race credits_delta
};
// The whole-season aggregate: a per-contract subtotal vector + the season grand net.
struct SeasonEconomyReport {
    std::vector<ContractSeasonTotal> per_contract;   // one entry per distinct contract_key (stable order)
    long season_net = 0;                             // = sum of every race's net (== sum of per-contract nets)
};
// Aggregate a season's per-race economy by owning contract. Each contract's subtotal sums ONLY its
// own races (coverage/deal/etc. never leak across contracts); season_net = the straight sum of all per-race
// nets and ALWAYS equals the sum of the per-contract nets (the invariant the Results/season UI relies on).
// Stable order = first-seen contract_key. Pure, deterministic.
SeasonEconomyReport AggregateSeasonEconomy(const std::vector<SeasonRaceEconomy>& perRace);

// 0.7.0 (thread “The ability to sell cars”): selling purchased cars ----
// Resale price: CAR_RESALE_PCT of the base price at 0 km, which mileage lowers in a straight line down to
// CAR_RESALE_KM_LOSS of itself at CAR_RESALE_KM_FULL (see the table in career_balance.h). Rounded to tens.
long   CarResaleValue(long basePrice, double km);
// Distance of a race: completed laps x track length (meters) / 1000. Without either of the two -> 0.
double RaceDistanceKm(int laps, double trackLengthM);
struct SaveData;   // career_save.h
// Use of the owned car when settling the race: +1 race, +0.02 wear and the race's km. Replaces the
// loose loop in the overlay so that the accumulation enters the harness. A car that is not yours or an empty id -> nothing.
void SettleOwnedCarUse(SaveData& d, const std::string& carId, int laps, double trackLengthM);
enum CarSaleBlock {
    CAR_SALE_OK = 0,
    CAR_SALE_NOT_OWNED,       // you do not have that unit (car + livery)
    CAR_SALE_PENDING_RACE,    // it is the car of the launched race, not yet settled
    CAR_SALE_CONTRACT,        // only car of the class of an active contract that does not cover the whole seat for you
    CAR_SALE_GIFT,            // the gift from 67 (kStarterMemoryCarId): it cannot be sold
    CAR_SALE_NO_VALUE,        // base price 0 (factory car or no price)
};
const char* CarSaleBlockToken(int block);   // "ok" | "not_owned" | "pending_race" | "contract" | "gift" | "no_value"
// The unit is car + livery; -1 (not chosen) and 0 are the same. pendingCarId = car of the race pending
// settlement ("" if none). seriesClass resolves the class of a series (nullptr -> SeriesById); it is a parameter
// so that the harness does not depend on the loaded world.
using SeriesClassFn = std::string (*)(const std::string& seriesId);
int  CarSaleCheck(const SaveData& d, const std::string& carId, int livery, long basePrice,
                  const std::string& pendingCarId, SeriesClassFn seriesClass = nullptr);
// Sells: adds CarResaleValue, removes the unit and leaves the [money] car_sell line. If it cannot be done, it touches nothing.
bool CarOpSell(SaveData& d, const std::string& carId, int livery, long basePrice, const std::string& pendingCarId,
               SeriesClassFn seriesClass = nullptr, int* outBlock = nullptr, long* outPaid = nullptr);
}
