#include "career_econ.h"
#include "career_balance.h"
#include "career_archetype.h"   // ContractArchetype enum (ARCH_*) for the type mapping
#include "career_negotiate.h"   // DealTerms -- the negotiated package the presentation consumes
#include "paths.h"              // EconLog: the same overlay log (userdata/logs), resolved once
#include "career_userlog.h"     // the ledger is mirrored to the user's SUPPORT log (support\zta_support.log)
#include "career_customai.h"    // VehiclesForClass -- the class's REAL car catalog
#include "career_save.h"        // 0.7.0: SaveData/CarOwnership/ClassPrefix — car sale
#include "career_series.h"      // 0.7.0: SeriesById — class of a contract's series (car sale)
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <string>
#include <map>
namespace career {

// Money LEDGER (declared in career_econ.h). Portable fopen/append into the overlay log, with
// a local timestamp: "what the player did, and when". Silent if the log isn't writable (never breaks the engine).
void EconLog(const char* reason, long delta, long after) {
    // Rebrand: the engine log is now unified with a size cap (zta_engine.log; EngineLogPath rotates on first use)
    time_t t = time(nullptr); struct tm tmv {};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char line[256];
    snprintf(line, sizeof(line), "[money] %02d:%02d:%02d %s %+ld -> %ld\r\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, reason, delta, after);
    if (LogCaptureActive()) {   // 0.7.0 review (finding 7): clock forecast, it has not happened
        LogCaptureEngineLine(line);
    } else {
        FILE* f = paths::FOpen(EngineLogPath(), "a");
        if (f) { fputs(line, f); fclose(f); }
    }
    UserLog("money", "%s %+ld -> %ld", reason, delta, after);   // mirrored to the support log
}

long FloorForPhase(int phase) {
    if (phase < 1) phase = 1; if (phase > 7) phase = 7;
    // per-phase participation floor: covers at least some of your consumables even if you finish last
    static const long F[7] = { 500, 1000, 3000, 8000, 15000, 25000, 40000 };
    return F[phase - 1];
}
long EntryForPhase(int phase) {
    if (phase < 1) phase = 1; if (phase > 7) phase = 7;
    return bal::ENTRY_BY_PHASE[phase - 1];   // {500,1000,2000,4000,8000,15000,20000}
}

// The guaranteed spend per grid action (see career_econ.h). The car's cost mirrors Settle:
//   rent = round(carValue*rentPct/100); run-cost = round(carValue*RUN_COST_PCT). Never < 0.
long RaceEntryOutlay(EntryAction action, long inscriptionDue, long entry, long carValue, int rentPct) {
    if (inscriptionDue < 0) inscriptionDue = 0;
    if (entry < 0) entry = 0;
    if (carValue < 0) carValue = 0;
    if (rentPct < 0) rentPct = 0;
    long carCost = 0;
    switch (action) {
        case EntryAction::Rent:   carCost = (long)(carValue * (rentPct / 100.0) + 0.5); break;
        case EntryAction::Buy:    carCost = (long)(carValue * bal::RUN_COST_PCT + 0.5) + carValue; break;
        case EntryAction::OwnCar: carCost = (long)(carValue * bal::RUN_COST_PCT + 0.5); break;
    }
    return inscriptionDue + entry + carCost;
}

// AMENDMENT: the per-race cost of an OWNED car, broken down (see career_econ.h). Consumables
// scale with LAPS (factor = laps/COST_REF_LAPS; 1.0 at the reference race length, >1 for longer races),
// insurance is flat. total = sum of the rounded components (identical to what the UI panel shows).
RaceCostBreakdown OwnedRaceCosts(long carValue, int laps) {
    RaceCostBreakdown b;
    if (carValue < 0) carValue = 0;
    if (laps < 0) laps = 0;
    double f = (double)laps / (double)bal::COST_REF_LAPS;                        // per-LAP factor (no upper cap)
    b.insurance = (long)(carValue * bal::COST_INSURANCE_PCT + 0.5);              // flat (per event)
    b.tyres     = (long)(carValue * bal::COST_TYRES_PCT  * f + 0.5);             // consumables scale with laps
    b.fuel      = (long)(carValue * bal::COST_FUEL_PCT   * f + 0.5);
    b.fluids    = (long)(carValue * bal::COST_FLUIDS_PCT * f + 0.5);
    b.total     = b.tyres + b.fuel + b.fluids + b.insurance;
    return b;
}

// (real wear): scales the nominal consumables using the shared-memory telemetry (see career_econ.h).
// A component with no data (< 0) stays at its nominal value (fail-open: without shared memory the economy is EXACTLY as before).
RaceCostBreakdown ApplyWearToCosts(const RaceCostBreakdown& nominal, const WearTelemetry& tel) {
    RaceCostBreakdown b = nominal;
    auto mult = [](double actual, double ref) {
        if (ref <= 0.0) return 1.0;                                     // degenerate reference -> no scaling
        double m = actual / ref;
        if (m < bal::WEAR_MULT_MIN) m = bal::WEAR_MULT_MIN;             // floor (careful driving)
        if (m > bal::WEAR_MULT_MAX) m = bal::WEAR_MULT_MAX;             // ceiling (brutal punishment)
        return m;
    };
    if (tel.tyreWear >= 0.0f) b.tyres = (long)(nominal.tyres * mult(tel.tyreWear, bal::WEAR_REF_TYRE) + 0.5);
    if (tel.fuelUsed >= 0.0f) b.fuel  = (long)(nominal.fuel  * mult(tel.fuelUsed, bal::WEAR_REF_FUEL) + 0.5);
    // fluids + insurance: untouched (damage is already billed via the deductible; insurance is flat per event).
    b.total = b.tyres + b.fuel + b.fluids + b.insurance;
    return b;
}

// NOMINAL lap-equivalent (see career_econ.h). Lap races -> laps; timed races -> durationMin/minutes-per-lap.
// See career_econ.h. Integer rounding, no cmath. E.g. (ref=20 laps): 20 laps->x1, 10->x2, 5->x4, 1->x20.
// FIX (from a report: "a 50%-length race lands on a x3 consumption multiplier and the AI runs out of fuel").
// The formula scaled against a FIXED 20-lap reference instead of against this race's own distance,
// which breaks the one physical constraint that actually matters here: the FUEL TANK. AMS2 sizes the tank (and
// the AI's strategy) for ONE full race, so the total fuel you can ask for is bounded by
// `laps_actually_run x mult <= full_race_distance`. With the fixed reference, that was violated constantly:
//   - a 14-lap race at 50% = 7 laps actually run, round(20/7)=x3 -> asks for 21 laps of fuel in a 14-lap tank.
//   - It didn't even need shortening: a 12-lap series AT 100% gave round(20/12)=x2, i.e. two
//     races' worth of fuel in a one-race tank. Nobody makes it -- not the AI, not the player.
// Now the multiplier is this race's FULL distance divided by the distance actually run,
// which is what the scaling meant to say from the start ("strategy should matter the same even if you shorten the race"),
// and it also guarantees, by construction, that the total never exceeds one race's worth. It depends ONLY on the length %:
// 100%->x1, 75%->x1, 50%->x2, 25%->x4. The per-axis caps (tires x7, fuel x5) can only push it lower.
// FLOOR, not round-up: conservative when in doubt. Rounding up is exactly what would ask for more
// fuel than fits, and running short only makes the strategy matter a little less.
int WearMultForLaps(int effLaps, int fullLaps) {
    if (effLaps <= 0) return 1;
    if (fullLaps <= 0) fullLaps = effLaps;   // full distance unknown -> don't scale (conservative)
    int m = fullLaps / effLaps;              // FLOOR: never ask for more fuel than the tank holds
    return (m < 1) ? 1 : m;
}
// hub (31/08): the ANNOUNCEMENT is capped wherever the write is capped. Yesterday we capped the write
// at x5 and this one was left at x7, so pre-race promised a x6/x7 the game doesn't offer, which then never got written.
int WearMultTyre(int effLaps, int fullLaps) { int m = WearMultForLaps(effLaps, fullLaps); return m > 5 ? 5 : m; }
int WearMultFuel(int effLaps, int fullLaps) { int m = WearMultForLaps(effLaps, fullLaps); return m > 5 ? 5 : m; }
// With tire scaling switched ON, wear ALWAYS follows the distance-based
// multiplier scale -- the player's global wear slider is NOT honored (that's what the toggle is for, set it to
// OFF, the "inherited" setting for this family: the mod leaves it alone). The game's own mapping
// (from reverse-engineering the save format): a HIGHER index = LESS wear -- 0=x7 .. 6=x1 (Authentic), 7=x0.5, 8=OFF.
// m<=1 (reference length or longer) -> Authentic; m>=2 -> the multiplier's absolute value. The player's global
// wear slider is restored once the race ends (that contract doesn't change).
int TireWearIdxForMult(int m) {
    if (m <= 1) return 6;       // x1 Authentic: the distance-based scale, at its starting point
    // hub (30/08 decision): ONLY the multipliers the game offers. Its tire scale goes up
    // to x5; indices 1 and 0 (x6/x7) exist in the reader but not in the menu, and the tester's screenshot
    // shows AMS2 announcing "x7" -- a value the player can neither choose nor verify. A 5% race asked for x7
    // and fell outside the scale. Cap at x5 (idx 2): at very short distances wear no longer compensates the
    // full distance, but everything we write is something the game knows how to do.
    if (m > 5) m = 5;
    return 7 - m;
}
// Index of FuelUsage to WRITE with scaling ON. Mapping confirmed (7 values,
// verified against the game's own comparison logic): 0/1=x1, 6=x2, 5=x3, 4=x4, 3=x5 -- there's no x6/x7 for fuel. m<=1 -> x1
// (index 1 is written; 0 is also x1 -- one is picked and kept consistent); m>=2 -> 8-m, capped at x5.
int FuelUsageIdxForMult(int m) {
    // hub (30/08): index 1 is NOT "Authentic" -- the tester's screenshot shows it as **"Slow"**, meaning
    // consumption BELOW the real one, which we write on every reference-length race (100%, 75%, 50%). The
    // reverse-engineering treated them as equivalent ("0/1 = x1") and the screen says otherwise: the screen wins. Authentic is 0.
    if (m <= 1) return 0;
    if (m > 5) m = 5;
    return 8 - m;
}
const int kDurationPcts[] = { 5, 10, 25, 50, 75, 100, 0 };   // (0 = end)

int RaceLapEquivalent(int laps, int durationMin) {
    if (laps > 0) return laps;                                                // race by LAPS
    if (durationMin > 0) {                                                    // race by TIME -> lap-equivalent
        int n = (int)(durationMin / bal::COST_MIN_PER_LAP + 0.5);
        return n < 1 ? 1 : n;
    }
    return 1;                                                                 // no data -> minimum 1
}

// Per-category economy: the PSV table + indices by node_id and by class_name ----
static std::map<std::string, SeriesEcon> g_econ;          // node_id -> row
static std::map<std::string, std::string> g_classToNode;  // catalog class_name -> node_id
// Per-CAR price override: a REAL differentiated value (market/auction or pedigree) keyed by vehHash. Cars that
// aren't here fall back to class-base + JitterPrice. Only applies when the car is actually BOUGHT (not a factory seat) -- in a
// normal race the contract classes stay at 0; it mostly bites in special events. See docs/ams2_vehicle_prices.psv.
static std::map<int, long> g_vehPrice;                    // vehHash -> price override (0 = no override)

// split that KEEPS empty fields (PSV columns)
static std::vector<std::string> splitKeep(const std::string& s, char d) {
    std::vector<std::string> r; std::string cur;
    for (char c : s) { if (c == d) { r.push_back(cur); cur.clear(); } else cur += c; }
    r.push_back(cur);
    return r;
}
// split that DROPS empty fields (the 'classes' column's CSV list)
static std::vector<std::string> splitDrop(const std::string& s, char d) {
    std::vector<std::string> r;
    for (const auto& t : splitKeep(s, d)) if (!t.empty()) r.push_back(t);
    return r;
}
// strips a leading UTF-8 BOM if present
static void stripBom(std::string& s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s.erase(0, 3);
}

bool LoadSeriesEcon(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_econ.clear(); g_classToNode.clear();
    char ln[1024]; bool header = false;
    while (fgets(ln, sizeof(ln), f)) {
        std::string s(ln);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        stripBom(s);
        if (s.empty() || s[0] == '#') continue;
        if (!header) { header = true; continue; }   // header row
        // format: node_id|phase|discipline|buy|rent|entry|win_prize|model|icon[|classes[|champion]]
        std::vector<std::string> q = splitKeep(s, '|');
        if (q.size() < 9) continue;
        SeriesEcon e;
        e.node_id = q[0];
        e.phase = atoi(q[1].c_str()); e.disc = q[2];
        e.comprar = atol(q[3].c_str()); e.alquiler = atol(q[4].c_str());
        e.entry = atol(q[5].c_str()); e.premio = atol(q[6].c_str());
        e.model = q[7]; e.icon = q[8];
        if (q.size() > 9 && !q[9].empty()) e.classes = splitDrop(q[9], ',');
        if (q.size() > 10) e.champion = atol(q[10].c_str());   // per-class champion bonus (col 10, optional)
        for (const auto& c : e.classes)
            if (!c.empty() && !g_classToNode.count(c)) g_classToNode[c] = e.node_id;
        g_econ[e.node_id] = e;
    }
    fclose(f);
    return true;
}
void ReplaceEcon(std::map<std::string, SeriesEcon> econ, std::map<std::string, std::string> classToNode) {
    g_econ = std::move(econ);
    g_classToNode = std::move(classToNode);
}

const SeriesEcon* FindEcon(const std::string& nodeId) {
    auto it = g_econ.find(nodeId);
    return it == g_econ.end() ? nullptr : &it->second;
}
const SeriesEcon* FindEconByClassName(const std::string& className) {
    auto it = g_classToNode.find(className);
    return it == g_classToNode.end() ? nullptr : FindEcon(it->second);
}
int SeriesEconCount() { return (int)g_econ.size(); }

// Per-car price (override) ----
// PSV format: veh_hash|price|note (note = the real-world justification, ignored by the engine). BOM/comments/
// header handled the same as LoadSeriesEcon. price<=0 or a short row -> ignored (falls back to class base+jitter).
bool LoadVehiclePrices(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_vehPrice.clear();
    char ln[1024]; bool header = false;
    while (fgets(ln, sizeof(ln), f)) {
        std::string s(ln);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        stripBom(s);
        if (s.empty() || s[0] == '#') continue;
        if (!header) { header = true; continue; }   // header row
        std::vector<std::string> q = splitKeep(s, '|');
        if (q.size() < 2) continue;
        long price = atol(q[1].c_str());
        if (price <= 0) continue;
        g_vehPrice[(int)strtol(q[0].c_str(), nullptr, 10)] = price;   // vehHash, signed 32-bit
    }
    fclose(f);
    return true;
}
long VehiclePrice(int vehHash) {
    auto it = g_vehPrice.find(vehHash);
    return it == g_vehPrice.end() ? 0 : it->second;
}
int VehiclePriceCount() { return (int)g_vehPrice.size(); }

// The "smarter formula" promised earlier, after a report about the Eskdale Gry320's price -- see bal::PRICE_SPREAD_CAP.
// The PSV keeps the REAL (curated) value and the BUY price honors it in full; this only feeds the
// per-race ECONOMIC value (CarItem.runValue: rent/consumables/insurance/settle).
long CompressPriceToClassBand(long vp, long classBase) {
    if (vp <= 0 || classBase <= 0) return vp;
    double r = (double)vp / (double)classBase;
    if (r <= bal::PRICE_SPREAD_CAP) return vp;
    long out = (long)llround((double)classBase * (bal::PRICE_SPREAD_CAP + std::log(r / bal::PRICE_SPREAD_CAP)));
    return ((out + 500) / 1000) * 1000;   // commercial rounding: a showroom price, not a formula output
}

// For the "from X credits" showcase price: the per-car minimum, using EXACTLY the same formula as CarsByClass
// (career_data.cpp): the real override if there is one (VehiclePrice>0), otherwise JitterPrice(base, vehHash) --
// JitterPrice already returns the ABSOLUTE jittered price (not a delta to add to the base).
long CheapestCarPriceForClass(const std::string& className) {
    const SeriesEcon* ec = FindEconByClassName(className);
    long base = ec ? ec->comprar : 0;
    long best = 0;
    for (const auto& v : VehiclesForClass(className)) {
        long vp = VehiclePrice(v.vehHash);
        // The real BUY price, uncompressed -- "from X credits" is what you'd actually pay for the car.
        long price = vp > 0 ? vp : JitterPrice(base, v.vehHash);
        if (price > 0 && (best == 0 || price < best)) best = price;
    }
    return best > 0 ? best : base;   // no priced cars -> base
}

long JitterPrice(long base, int vehHash) {
    if (base == 0) return 0;                          // factory / "UNDER CONTRACT": never touch
    uint32_t u = (uint32_t)vehHash;                   // reinterpret signed->unsigned, stable
    int step = (int)(u % 17u) - 8;                    // integer in [-8,+8], 17 symmetric buckets
    return (long)llround((double)base * (1.0 + step / 100.0));
}

// Living World: income_per_race = base * (0.7 + 0.6*q(R)) * (1 - coverage*k_cov).
long IncomePerRace(long base, double qR, double coverage) {
    if (qR < 0.0) qR = 0.0; if (qR > 1.0) qR = 1.0;
    if (coverage < 0.0) coverage = 0.0; if (coverage > 1.0) coverage = 1.0;
    double mult = (bal::INCOME_Q_FLOOR + bal::INCOME_Q_SPAN * qR) * (1.0 - coverage * bal::K_COV);
    return (long)llround((double)base * mult);
}

// Living World: your out-of-pocket share = max(rawCost - covered, 0).
long CoverageShare(long rawCost, long covered) {
    if (rawCost <= 0) return 0;
    long share = rawCost - covered;
    return share > 0 ? share : 0;
}

// Living World: one-off qualifier/dream-race entry cost by phase (heavier than EntryForPhase).
long QualifierEntryCost(int phase) {
    if (phase < 1) phase = 1; if (phase > 7) phase = 7;
    return bal::QUALIFIER_ENTRY_BY_PHASE[phase - 1];
}

// Per-contract-type coverage profile. ------------------------------
// The ~6 contract TYPES are DERIVED, not stored: ContractTypeIndex maps a contract's
// kind (career::bal::CONTRACT_* 0..4), refined by its narrative archetype (career::ContractArchetype 0..9),
// onto a type index 0..5; ContractCoverageProfile then snapshots that type's row from the named
// career::bal::COVERAGE_* tables (no magic numbers -- every field reads from bal::). Pure and deterministic:
// no RNG, no globals, no game state. Out-of-range inputs degrade SAFELY to the privateer type (0).
// Index: 0 Self-funded - 1 Pay-per-event - 2 Co-pay - 3 Covered/semi-pro - 4 Paid-works - 5 Factory.

// Map (kind, archetype) -> type 0..5. The ARCHETYPE is the primary, most-specific signal
// -- each of the 9 archetypes carries the economics intent the type
// table keys on. The kind only REFINES the gray-zone archetypes (OPEN_VACANCY / BRIDGE) -- a WORKS kind on a
// generic vacancy is a covered pro seat, a PAY_DRIVER kind on the same vacancy is a paid-rental. Clamps both
// inputs; an unknown archetype falls to the privateer type (0) -- the safe default.
int ContractTypeIndex(int kind, int archetype) {
    if (archetype < 0 || archetype > 9) return 0;          // unknown archetype -> privateer (safe degrade) [0..9: +ACADEMY_PROMOTION]
    switch (archetype) {
        case ARCH_SELF_FUNDED_ENTRY: return 0;             // Self-funded -- no contract / fully self-funded
        case ARCH_WILDCARD:          return 1;             // Pay-per-event -- one-race rental (the race is the test)
        case ARCH_CUSTOMER_SEAT:     return 2;             // Co-pay -- customer co-pays part of the seat
        case ARCH_FACTORY_PROGRAM:   return 5;             // Factory/works -- full factory seat
        case ARCH_DIRECT_OFFER:                             // works seat the team approached you for
        case ARCH_JUNIOR_PROGRAM:                          // junior development program (team-backed)
        case ARCH_ACADEMY_PROMOTION:                        // Feeder-team promotion by the parent squad (a real works seat)
        case ARCH_RENEWAL:           return 4;             // Paid-works -- salaried works/conditional seat
        case ARCH_OPEN_VACANCY:                             // gray-zone vacancy -- KIND refines it
        case ARCH_BRIDGE_CONTRACT:                          // short bridge contract -- KIND refines it
        default: break;
    }
    // gray-zone (OPEN_VACANCY / BRIDGE): the contract KIND decides where it lands on the coverage ladder.
    if (kind == bal::CONTRACT_WORKS)            return 4;  // works kind -> Paid-works (salaried)
    if (kind == bal::CONTRACT_CONDITIONAL)      return 4;  // development/conditional -> Paid-works
    if (kind == bal::CONTRACT_PAY_DRIVER)       return 1;  // pay-driver -> Pay-per-event (rental)
    if (kind == bal::CONTRACT_CUSTOMER)         return 3;  // customer vacancy -> Covered/semi-pro
    if (kind == bal::CONTRACT_TRIAL)            return 3;  // short trial seat -> Covered/semi-pro
    return 3;                                              // safe mid-ladder for an unknown kind
}

// The resolved coverage profile for a contract, snapshotting the type's row from career::bal.
// EVERY field reads from a named COVERAGE_* / RESULT_BONUS table at ContractTypeIndex(...) -- no literal here.
CoverageProfile ContractCoverageProfile(int kind, int archetype) {
    int t = ContractTypeIndex(kind, archetype);
    if (t < 0 || t >= bal::COVERAGE_TYPE_COUNT) t = 0;     // defensive: clamp to a valid type row
    CoverageProfile p;
    p.seat_entry_cov    = bal::COVERAGE_SEAT_ENTRY[t];
    p.consumables_cov   = bal::COVERAGE_CONSUMABLES[t];
    p.damage_cov        = bal::COVERAGE_DAMAGE[t];
    p.travel_cov        = bal::COVERAGE_TRAVEL[t];
    p.salary_dir        = bal::COVERAGE_SALARY_DIR[t];
    p.damage_cap        = bal::COVERAGE_DAMAGE_CAP[t];
    p.exclusive         = bal::COVERAGE_EXCLUSIVE[t];
    p.damage_share_base = bal::COVERAGE_DAMAGE_SHARE_BASE[t];
    p.fixes_car         = bal::COVERAGE_FIXES_CAR[t];
    return p;
}

// The offer/contract PRESENTATION facets -- type label + the per-category covered/you-pay
// breakdown that drives the "Includes" checklist. Reads the SAME ContractCoverageProfile fractions used at
// Settle time (presentation == reality); when `terms` carries a more-coverage package
// the shown fractions are lifted toward full. Pure, deterministic; no globals. `terms == nullptr` (Market
// "as-is" / no negotiation) uses the bare type profile. The deductible/damage stays the player's regardless
// -- `damageCovered` reflects only the REPAIR coverage, never the deductible (the deductible is always the player's).
OfferPresentationFacets DeriveOfferPresentation(int kind, int archetype, const DealTerms* terms) {
    OfferPresentationFacets f;
    f.typeLabel = ContractTypeIndex(kind, archetype);          // clamped 0..5 by ContractTypeIndex itself
    CoverageProfile p = ContractCoverageProfile(kind, archetype);
    f.salaryDir = p.salary_dir;
    // start from the bare type fractions (the exact Settle inputs)
    double se = p.seat_entry_cov, co = p.consumables_cov, da = p.damage_cov, tr = p.travel_cov;
    // A more-coverage negotiated package (salaryPct BELOW the neutral 50) raises the shown
    // coverage toward full, proportional to how far below neutral the player went. salaryPct ABOVE 50 (more
    // salary, less coverage) does NOT lower the shown coverage below the type floor -- the type still covers
    // what it covers; the player merely traded for salary. lift in [0,1]; out = frac + (1-frac)*lift.
    if (terms) {
        int pct = terms->salaryPct;
        if (pct < 0) pct = 0; if (pct > 100) pct = 100;        // defensive clamp (DealTerms is 0..100)
        if (pct < 50) {                                        // "More coverage" side
            double lift = ((50.0 - (double)pct) / 50.0) * bal::COVERAGE_PRESENT_NEGO_GAIN;
            if (lift < 0.0) lift = 0.0; if (lift > 1.0) lift = 1.0;
            auto raise = [&](double v){ double o = v + (1.0 - v) * lift; return o > 1.0 ? 1.0 : o; };
            se = raise(se); co = raise(co); da = raise(da); tr = raise(tr);
        }
    }
    f.seatEntryFrac = se; f.consumablesFrac = co; f.damageFrac = da; f.travelFrac = tr;
    const double thr = bal::COVERAGE_PRESENT_THRESHOLD;
    f.seatEntryCovered    = se >= thr;
    f.consumablesCovered  = co >= thr;
    f.damageCovered       = da >= thr;
    f.travelCovered       = tr >= thr;
    return f;
}

// The player's per-race deductible for a contract TYPE. ALWAYS the player's -- never
// reduced by the team/sponsor coverage (maximum realism). More-covered types carry a SMALLER deductible
// purely via a lower COVERAGE_DAMAGE_SHARE_BASE[type] (1.0 privateer .. 0.1 factory), not via any coverage
// offset. An optional damage-responsibility reducer (damageShareReducer in [0,1], 1.0 = none) LOWERS
// it when present. Every magnitude is a named bal:: constant -- no literal here. Clamps typeIndex + the 0..1
// inputs; a degenerate/zero severity yields 0 (a clean race is free); never returns < 0.
long DeductibleForRace(int typeIndex, double damageShareSeverity, double damageShareReducer) {
    if (typeIndex < 0 || typeIndex >= bal::COVERAGE_TYPE_COUNT) typeIndex = 0;   // clamp to a valid type row
    if (damageShareSeverity < 0.0) damageShareSeverity = 0.0;                    // clamp severity to [0,1]
    if (damageShareSeverity > 1.0) damageShareSeverity = 1.0;
    if (damageShareReducer  < 0.0) damageShareReducer  = 0.0;                    // clamp reducer to [0,1]
    if (damageShareReducer  > 1.0) damageShareReducer  = 1.0;
    double d = (double)bal::DEDUCTIBLE_BASE
             * bal::COVERAGE_DAMAGE_SHARE_BASE[typeIndex]
             * (bal::DEDUCTIBLE_DAMAGE_SCALE * damageShareSeverity)
             * damageShareReducer;
    if (d < 0.0) d = 0.0;                                                        // never below 0
    return (long)llround(d);
}

// The season insurance PREMIUM for one held contract, charged once per season at rollover.
// Priced by phase (faster cars cost more to insure) x the type's damage_share_base (privateer pays the full
// base; a covered type is cheaper) x a past-season cleanliness multiplier (a clean season pays LESS, a
// crash-prone one MORE). cleanliness is 0..100 (CareerProgress.cleanliness); mult lerps from CLEAN_MULT_MIN
// (spotless) to CLEAN_MULT_MAX (crash-prone) by (1 - cleanliness/100). Every magnitude is a named bal::
// constant. Clamps phase to 1..7, typeIndex to a valid row, cleanliness to [0,100]; never returns < 0.
long InsurancePremiumForContract(int phase, int typeIndex, double cleanliness) {
    if (phase < 1) phase = 1; if (phase > 7) phase = 7;
    if (typeIndex < 0 || typeIndex >= bal::COVERAGE_TYPE_COUNT) typeIndex = 0;
    if (cleanliness < 0.0) cleanliness = 0.0; if (cleanliness > 100.0) cleanliness = 100.0;
    double dirty = 1.0 - cleanliness / 100.0;                                    // 0 = spotless .. 1 = crash-prone
    double cleanMult = bal::INSURANCE_CLEAN_MULT_MIN
                     + (bal::INSURANCE_CLEAN_MULT_MAX - bal::INSURANCE_CLEAN_MULT_MIN) * dirty;
    double p = (double)bal::INSURANCE_PREMIUM_BY_PHASE[phase - 1]
             * bal::COVERAGE_DAMAGE_SHARE_BASE[typeIndex]
             * cleanMult;
    if (p < 0.0) p = 0.0;
    return (long)llround(p);
}

// Circuit geo (country/continent proxy) -- docs/circuits_ref.psv keyed by track hash. ----------
static std::map<long, CircuitGeo> g_circuitGeo;   // track hash -> { country, continent }
// the circuit's DLC pack (col 9 `dlc` of circuits_ref.psv, the ground truth for the track catalog).
// "" = base content. It only gives the pack's NAME for the "requires DLC X" message; whether it's INSTALLED
// is decided by career::TrackInstalled (the live catalog), not this map.
static std::map<long, std::string> g_circuitDlc;

// Parse circuits_ref.psv (header `hash|name|country|continent|type|grade|era|note|slug|dlc`). Mirrors
// LoadSeriesEcon: BOM strip, '#'/blank skip, header skip, split-keep, field-count guard. Cols 0/2/3 used.
bool LoadCircuitGeo(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_circuitGeo.clear();
    char ln[1024]; bool header = false;
    while (fgets(ln, sizeof(ln), f)) {
        std::string s(ln);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        stripBom(s);
        if (s.empty() || s[0] == '#') continue;
        // FIX: circuits_ref.psv's header is a '#' comment line, so the first non-comment line
        // is ALREADY data (Ashton Park was silently dropped from the geo table because of this). Only skip a line if it doesn't look like data.
        if (!header) { header = true; if (!(s[0] == '-' || isdigit((unsigned char)s[0]))) continue; }
        std::vector<std::string> q = splitKeep(s, '|');
        if (q.size() < 4) continue;                  // need at least hash|name|country|continent
        if (q[0].empty()) continue;
        long h = atol(q[0].c_str());
        CircuitGeo g; g.country = q[2]; g.continent = q[3];
        // fields used for track substitution (name/type/grade/era/slug); a non-numeric era -> 0
        g.name = q[1];
        if (q.size() > 4) g.type  = q[4];
        if (q.size() > 5) g.grade = q[5];
        // 19/09 (player report: Wildton Ring six times in the Thornwick Motors Ax797). "modern" is the
        // era of 153 of the 230 circuits and `atoi` read it as year 0: for a 2025 series any historic
        // circuit with a year (2001, 24 years away) ended up "closer" than ALL the modern ones (2025 years away), so the
        // substitution of missing tracks always went to the historic one. "modern" becomes a real year.
        if (q.size() > 6) g.eraYear = (q[6] == "moderno") ? kModernCircuitEraYear : atoi(q[6].c_str());
        if (q.size() > 8) g.slug  = q[8];
        if (q.size() > 10) g.flags = q[10];          // optional trailing column: "nosub" (see ResolveTrackSubstitute)
        g_circuitGeo[h] = g;                         // last write wins (PSV is unique by hash)
        // col 9 `dlc` (pack slug; empty = base content). It used to be ignored.
        if (q.size() >= 10 && !q[9].empty()) g_circuitDlc[h] = q[9];
    }
    fclose(f);
    return true;
}
// the circuit's DLC pack ("" = base/unknown).
std::string TrackDlcPack(long trackHash) {
    auto it = g_circuitDlc.find(trackHash);
    return it == g_circuitDlc.end() ? std::string() : it->second;
}
bool FindCircuitGeo(long trackHash, CircuitGeo& out) {
    auto it = g_circuitGeo.find(trackHash);
    if (it == g_circuitGeo.end()) return false;      // unknown circuit -> caller uses the safe cross-continent tier
    out = it->second;
    return true;
}
int CircuitGeoCount() { return (int)g_circuitGeo.size(); }
// full view, for track substitution (career_content) and query.content detection.
const std::map<long, CircuitGeo>& AllCircuitGeo() { return g_circuitGeo; }

// Nationality ISO code -> circuits_ref-style {country, continent} (Spanish names, so the first-leg
// origin compares correctly against FindCircuitGeo output). Codes mirror screen_identity.cpp's kCountries.
// Pure + deterministic; an unknown/empty code returns an empty geo (the caller degrades to cross-continent).
CircuitGeo NationalityGeo(const std::string& natCode) {
    struct NG { const char* code; const char* country; const char* continent; };
    static const NG T[] = {
        {"GB","Reino Unido","Europa"},   {"DE","Alemania","Europa"},   {"FR","Francia","Europa"},
        {"IT","Italia","Europa"},        {"ES","España","Europa"},     {"NL","Países Bajos","Europa"},
        {"BE","Bélgica","Europa"},       {"AT","Austria","Europa"},    {"CH","Suiza","Europa"},
        {"FI","Finlandia","Europa"},     {"SE","Suecia","Europa"},     {"DK","Dinamarca","Europa"},
        {"NO","Noruega","Europa"},       {"IE","Irlanda","Europa"},    {"PL","Polonia","Europa"},
        {"HU","Hungría","Europa"},       {"RO","Rumanía","Europa"},    {"RU","Rusia","Europa"},
        {"US","EE.UU.","Norteamérica"},  {"CA","Canadá","Norteamérica"},{"BR","Brasil","Sudamérica"},
        {"AR","Argentina","Sudamérica"}, {"MX","México","Norteamérica"},{"CO","Colombia","Sudamérica"},
        {"AU","Australia","Oceanía"},    {"NZ","Nueva Zelanda","Oceanía"},{"JP","Japón","Asia"},
        {"MC","Blackgate Speedway","Europa"},        {"ID","Indonesia","Asia"},    {"ZA","Sudáfrica","África"},
    };
    CircuitGeo g;
    for (const auto& n : T) if (natCode == n.code) { g.country = n.country; g.continent = n.continent; break; }
    return g;   // empty when unknown -> caller treats as the safe cross-continent tier
}

// The travel cost of an A->B leg for a contract TYPE, by country/continent proxy
// (no coordinates, deliberately). Tier the GROSS leg cost: same country -> TRAVEL_SAME_COUNTRY; same continent,
// different country -> TRAVEL_SAME_CONTINENT; else (cross-continent OR an unknown continent/country) ->
// TRAVEL_CROSS_CONTINENT (the safe, most-expensive default on malformed data). The team's
// covered fraction (COVERAGE_TRAVEL[typeIndex]) is then applied here so a more-covered type pays ~0 travel:
//   out = round(base * (1.0 - COVERAGE_TRAVEL[typeIndex])).  Every magnitude is a named bal:: constant -- no
// literal here. Clamps typeIndex to a valid type row; never returns < 0. The season's FIRST leg (no previous
// circuit) uses the player's nationality as the origin country/continent -- resolved by the caller (the overlay
// passes the nationality home for the from* args when last_circuit_hash == 0).
long TravelLegCost(int typeIndex, const std::string& fromContinent, const std::string& fromCountry,
                   const std::string& toContinent, const std::string& toCountry) {
    if (typeIndex < 0 || typeIndex >= bal::COVERAGE_TYPE_COUNT) typeIndex = 0;   // clamp to a valid type row
    // Tier the leg cost by the country/continent proxy. An empty/unknown continent or country can't be matched to
    // "same", so it falls through to the cross-continent (most-expensive) tier -- the safe default.
    long base;
    bool knownFrom = !fromContinent.empty() && !fromCountry.empty();
    bool knownTo   = !toContinent.empty()   && !toCountry.empty();
    if (knownFrom && knownTo && fromCountry == toCountry) {
        base = bal::TRAVEL_SAME_COUNTRY;                 // A->B within the same country (cheap)
    } else if (knownFrom && knownTo && fromContinent == toContinent) {
        base = bal::TRAVEL_SAME_CONTINENT;               // same continent, different country (medium)
    } else {
        base = bal::TRAVEL_CROSS_CONTINENT;              // cross-continent OR unknown data (expensive/safe)
    }
    // FIX: the base is a REALISTIC PERSONAL cost (~independent of the tier); the difference by
    // tier comes from the contract's COVERAGE (a factory team covers your travel).
    double cov = bal::COVERAGE_TRAVEL[typeIndex];        // the team's covered fraction of this leg
    if (cov < 0.0) cov = 0.0; if (cov > 1.0) cov = 1.0;  // defensive clamp (tunable should already be 0..1)
    double out = (double)base * (1.0 - cov);
    if (out < 0.0) out = 0.0;                            // never below 0
    return (long)llround(out);
}

// Multi-contract SEASON aggregation. --------------------------------------
// Group the season's per-race SettleReports by their owning contract key and sum each contract's economy on
// its OWN bucket (no cross-contamination: contract A's coverage/deal/etc. never lands on contract B). The
// season grand net is the straight sum of every race's net -- which, because each race's net is added to
// exactly one contract bucket, is identically the sum of the per-contract nets (the invariant the season/
// Results UI relies on). Stable order = first-seen contract_key (a small linear index, no map ordering
// surprises). Pure, deterministic; no globals, no RNG.
SeasonEconomyReport AggregateSeasonEconomy(const std::vector<SeasonRaceEconomy>& perRace) {
    SeasonEconomyReport out;
    std::map<std::string, size_t> idx;   // contract_key -> index into out.per_contract (first-seen order)
    for (const auto& e : perRace) {
        auto it = idx.find(e.contract_key);
        size_t i;
        if (it == idx.end()) {                       // first race seen for this contract -> new bucket
            ContractSeasonTotal t; t.contract_key = e.contract_key;
            i = out.per_contract.size();
            idx[e.contract_key] = i;
            out.per_contract.push_back(t);
        } else {
            i = it->second;
        }
        ContractSeasonTotal& t = out.per_contract[i];
        const SettleReport& r = e.report;
        t.races++;
        t.prize        += r.prize;
        t.deal_salary  += r.deal_salary;
        t.result_bonus += r.result_bonus;
        t.coverage     += r.coverage;
        t.costs        += r.costs;
        t.deductible   += r.deductible;
        t.travel       += r.travel;
        t.net          += r.credits_delta;           // this contract's season net = sum of ITS race nets
        out.season_net += r.credits_delta;           // season grand net = sum of EVERY race net
    }
    return out;
}

// 0.7.0 (thread “The ability to sell cars”): selling purchased cars (decl. in career_econ.h) ----
long CarResaleValue(long basePrice, double km) {
    if (basePrice <= 0) return 0;
    if (!(km > 0.0)) km = 0.0;                                  // negatives and NaN count as a car that has not debuted
    double wornShare = km >= bal::CAR_RESALE_KM_FULL ? 1.0 : km / bal::CAR_RESALE_KM_FULL;
    double v = (double)basePrice * bal::CAR_RESALE_PCT * (1.0 - bal::CAR_RESALE_KM_LOSS * wornShare);
    return (long)llround(v / 10.0) * 10;
}

double RaceDistanceKm(int laps, double trackLengthM) {
    if (laps <= 0 || !(trackLengthM > 0.0)) return 0.0;
    return (double)laps * trackLengthM / 1000.0;
}

void SettleOwnedCarUse(SaveData& d, const std::string& carId, int laps, double trackLengthM) {
    if (carId.empty()) return;
    for (auto& c : d.owned) {
        if (c.class_id != carId) continue;
        c.races++;
        c.wear += 0.02f;
        c.km += RaceDistanceKm(laps, trackLengthM);
        break;                                                  // the first unit, same as before in the overlay
    }
}

const char* CarSaleBlockToken(int block) {
    switch (block) {
    case CAR_SALE_OK:           return "ok";
    case CAR_SALE_NOT_OWNED:    return "not_owned";
    case CAR_SALE_PENDING_RACE: return "pending_race";
    case CAR_SALE_CONTRACT:     return "contract";
    case CAR_SALE_GIFT:         return "gift";
    case CAR_SALE_NO_VALUE:     return "no_value";
    default:                    return "not_owned";
    }
}

namespace {
int LiveryUnit(int livery) { return livery < 0 ? 0 : livery; }   // 1 (not chosen) = the stock livery = 0

int FindOwnedUnit(const SaveData& d, const std::string& carId, int livery) {
    for (size_t i = 0; i < d.owned.size(); i++)
        if (d.owned[i].class_id == carId && LiveryUnit(d.owned[i].livery) == LiveryUnit(livery)) return (int)i;
    return -1;
}

std::string SeriesClassDefault(const std::string& seriesId) {
    const SeriesDef* s = SeriesById(seriesId);
    return s ? s->class_name : std::string();
}
} // namespace

int CarSaleCheck(const SaveData& d, const std::string& carId, int livery, long basePrice,
                 const std::string& pendingCarId, SeriesClassFn seriesClass) {
    if (FindOwnedUnit(d, carId, livery) < 0) return CAR_SALE_NOT_OWNED;
    if (carId == kStarterMemoryCarId && LiveryUnit(livery) == 0) return CAR_SALE_GIFT;
    if (!pendingCarId.empty() && pendingCarId == carId) return CAR_SALE_PENDING_RACE;
    // Active contract of the SAME class where the seat is not free for you (types 0-2: you pay part or all of the
    // rental, so your car is what saves you that expense). It only blocks if it is your ONLY car of that class.
    const std::string cls = ClassPrefix(carId);
    int sameClass = 0;
    for (const auto& c : d.owned) if (ClassPrefix(c.class_id) == cls) sameClass++;
    if (sameClass <= 1) {
        for (const auto& ct : d.contracts) {
            if (!ct.active) continue;
            if (ContractCoverageProfile(ct.kind, ct.archetype).seat_entry_cov >= 1.0) continue;
            std::string seriesCls = seriesClass ? seriesClass(ct.series_id) : SeriesClassDefault(ct.series_id);
            // a multiclass series may declare several classes separated by commas
            size_t start = 0;
            while (start <= seriesCls.size()) {
                size_t comma = seriesCls.find(',', start);
                std::string one = seriesCls.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!one.empty() && one == cls) return CAR_SALE_CONTRACT;
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
    }
    if (basePrice <= 0) return CAR_SALE_NO_VALUE;
    return CAR_SALE_OK;
}

bool CarOpSell(SaveData& d, const std::string& carId, int livery, long basePrice, const std::string& pendingCarId,
               SeriesClassFn seriesClass, int* outBlock, long* outPaid) {
    const int block = CarSaleCheck(d, carId, livery, basePrice, pendingCarId, seriesClass);
    if (outBlock) *outBlock = block;
    if (outPaid) *outPaid = 0;
    if (block != CAR_SALE_OK) return false;
    const int idx = FindOwnedUnit(d, carId, livery);
    const long paid = CarResaleValue(basePrice, d.owned[(size_t)idx].km);
    d.owned.erase(d.owned.begin() + idx);
    d.prog.credits += paid;
    EconLog("car_sell", paid, d.prog.credits);
    if (outPaid) *outPaid = paid;
    return true;
}
}
