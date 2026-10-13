#include "career_seats.h"
#include "career_pool_origin.h"   // 08/09: the curated origin category of the pool
#include "career_balance.h"
#include "career_series.h"    // SeriesById -> class_name + level (tier); CalendarForSeries/CalRound
#include "career_fields.h"    // PinnedRosterForSeries (occupants+livery) + RosterForTier (cohort occupants)
#include "career_growth.h"    // GrowthProgress — the RISE half of the curve, made personal
#include "career_worldseason.h" // BudgetFor + PriceFor — money decides WHO, never whether there is a slot
#include "career_market.h"    // IsEraMemorySeries (excludes historic/era seats)
#include "career_calendar.h"  // CareerCalendar (cursor_doy/year) for PreseasonWindowChurn
#include "career_content.h"   // SeriesPlayable (a series with no cars doesn't enter the window)
#include "career_data_json.h" // DriverIdForName -> discards team labels that are actually a driver's name
#include "career_dateutil.h"  // AbsDayFromYearDoy (the market pulse compares absolute days)
#include "career_contract.h"  // AcceptOffer -- the ONLY signing gate (guardrails included)
#include "career_registry.h" // ~~~02/09 (front 5): the RESERVE pool -- people with a card and without a grid
#include "career_candidate.h" // CohortCandidateFactors/TeamCandidateFit (the cohort competes for seats)
#include "career_rating.h"    // ~~~07/09: LevelAnchoredSeedPct -- the generated rookie is seeded with the SAME rule as everyone
#include "paths.h"            // mojibake fix 22/07: FOpen (UTF-8 -> wide)
#include "career_userlog.h"    // 0.7.0 port: notice of the 0.6.8 migration
#include "career_engine.h"     // 23/09 (): DropUnracedSeeds + SeedAllDrivers, the startup re-seeding
#include "career_text.h"       // 23/09: CleanNameSpaces — team names are loaded without hard spaces
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <utility>

// The living-seat-market team/seat/occupancy model. Pure engine (no ImGui,
// no game, no SHM). Built from EXISTING data: docs/livery_team_equiv.psv (livery -> real_team) +
// docs/series_rosters.psv (driver -> livery, via PinnedRosterForSeries) + RosterForTier (cohort occupants).
// Deterministic, no RNG (fnv1a where a stable derived value is needed). Team naming is best-effort with the
// DECIDED precedence (real_team -> alias -> livery_name -> "Privateer #"+car_number -> id
// "seat_privateer_entry"); ALL tiers populate regardless of name — the bottom (cohort) classes
// get derived labels, since that is where the player starts. PlayerDisplacesIncumbent / PlayerLosesSeat
// remain RED stubs, not yet implemented.
namespace career {

// The persisted vacancy sentinel (declared in career_seats.h). A seat_occupant entry
// holding this value marks the seat OPEN (the living-world market freed it) even when the static roster
// would derive an occupant. The "\x1f" unit separator never appears in a real driver id, so it cannot
// collide with a pool id.
const char* const kSeatOpen = "\x1f__open__";

// ~~~02/09 — DIAGNOSTIC: by which route each driver reaches each slot. Off except in `--world`.
// ~~~02/09 — switch. It is turned off by `test_a_cloned_rookie_enters_below_its_ceiling`, which measures properties
// of the generated CLONES and needs a sample of them: with the reserve in place the world stops inventing
// people (which is the goal) and that test's sample dries up -- 50 clones where it asked for more than 50.
bool g_reservaEnParrilla = true;
bool g_trazaRutas = false;
std::map<std::string, std::string> g_rutaDe;
std::map<std::string, long> g_rej7;   // ~~~02/09: why a level-7 aspirant guard dies
std::map<std::string, long> g_rejPromo;   // ~~~07/09: the same but at ALL levels (see the .h)
// Counts a promotion rejection by DESTINATION level and reason. Cheap and silent without `g_trazaRutas`.
// @@The PASS goes in the key, and it is not a formatting detail: the loop makes TWO passes -- the first with the
//  budget filter and the second without it-- so a rejection from pass 0 is NOT a final cause,
//  it only says that team could not afford it. Without separating them, the measure counts itself twice
//  for each slot that needs both, which is exactly the error that was already made with the displaced.
static void RejPromo(int tier, const char* motivo, int pasada) {
    if (!g_trazaRutas) return;
    char k[56]; snprintf(k, sizeof(k), "t%d/p%d-%s", tier, pasada, motivo);
    g_rejPromo[k]++;
}
static void Ruta(const std::string& id, int anio, const char* que, int de, int a) {
    if (!g_trazaRutas || id.empty()) return;
    char b[64];
    snprintf(b, sizeof(b), " T%d:%s%d>%d", anio, que, de, a);
    g_rutaDe[id] += b;
}


// Local deterministic hash (no RNG). Static to this TU to avoid ODR clash with career_market.cpp's fnv1a.
static unsigned long seatFnv1a(const std::string& s) {
    unsigned long h = 2166136261UL;
    for (char c : s) { h ^= (unsigned char)c; h *= 16777619UL; }
    return h;
}

// PSV split (keep empty fields — real_team/alias columns are legitimately blank). Mirrors the project idiom.
static std::vector<std::string> seatSplitPipe(const char* line) {
    std::vector<std::string> out; std::string cur;
    for (const char* p = line; *p; ++p) {
        char c = *p;
        if (c == '\r' || c == '\n') continue;
        if (c == '|') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

// livery_team_equiv.psv: (class_name, livery_name) -> { alias, real_team, car_number } -------------------
struct TeamEquiv { std::string alias, real_team, car_number; };
static std::map<std::string, TeamEquiv> g_teamEquiv;   // key = class_name + "\x1f" + livery_name

static std::string equivKey(const std::string& cls, const std::string& liv) {
    return cls + "\x1f" + liv;   // \x1f = unit separator (never appears in PSV team/livery text)
}

bool LoadLiveryTeamEquiv(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_teamEquiv.clear();
    char line[1024];
    // Header is a leading '#'-comment block (class_name|livery_id|...); every non-'#' line is data.
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = seatSplitPipe(line);
        if (t.size() < 6) continue;                 // need class|livery_id|livery_name|alias|real_team|car_number
        if (t[0].empty() || t[2].empty()) continue; // class_name + livery_name are the key
        TeamEquiv te;
        te.alias = CleanNameSpaces(t[3]); te.real_team = CleanNameSpaces(t[4]); te.car_number = t[5];
        g_teamEquiv[equivKey(t[0], t[2])] = te;     // last write wins (the PSV is keyed by class+livery)
    }
    fclose(f);
    return true;
}
int LiveryTeamEquivCount() { return (int)g_teamEquiv.size(); }

// Layer 0: livery_resolved.psv — the VERDICT per vanilla slot ------------------------------------
// It's the already-resolved answer for (class, livery) -> team, and it overrides everything below. It's produced by
// tools/livery_reconcile.py, merging in this order: what the human typed in the workbench, the
// recommended pack's slot, the curated tables, the driver-inferred layer, and finally, made-up teams.
//
// Why it comes AHEAD of TeamLabelFor's historic cascade: that cascade ends up returning the
// livery_name ("Dunfield Racing") or "Privateer #N" when it doesn't know, and those are NOT team
// names. A team is an entity that makes offers and outlives its drivers; naming it after the car — or
// after its driver — makes it meaningless the moment the world rotates. A deliberate design decision (Aug 3).
//
// Coverage measured on Aug 3: 2659 of 2808 liveries, versus livery_team_equiv.psv's 823 keys. The
// missing ones are road cars (Street/Coldstead_Motors/Hypercars), deliberately excluded.
// Aug 5: the row carries its OWN slot (veh_hash, livery_id) and is stored alongside the team. The
// PSV's header already says so -- "the key is (veh_hash, livery_id): the SLOT" -- but the reverse team->car cross-reference
// ignored that and looked up the slot in livery_vehicle_map, which is a dump and doesn't cover everything: 24 rows
// were left with no slot to cross-reference (16 Coldstead Motors teams, Aurora_Challenge, Aurora_Masters, and GT1) and fell back to
// fixed-random. Measured before trusting it: of the 2924 rows present in both tables, the slot
// matches in all 2924. There aren't two numbering schemes -- there was one incomplete table.
struct ResolvedSlot { std::string team; int vehHash = 0; int liveryNN = -1; };
static std::map<std::string, ResolvedSlot> g_liveryResolved;   // equivKey(class, livery) -> team + slot
// 23/09: and the DRIVER the pack paints in that slot (col 7), the other way round: class -> folded name -> livery.
// Used by the static era composition so as not to hand out the same livery to five people (Group A).
static std::map<std::string, std::map<std::string, std::string>> g_resolvedDriverLivery;

bool LoadLiveryResolved(const char* path) {
    // 16/09: cleared first. With the clear after the return, a failed load left the table of the previous load
    // in place, and the suite measured team names with the layer some other test had loaded: the result
    // depended on the order.
    g_liveryResolved.clear();
    g_resolvedDriverLivery.clear();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    char line[2048];
    // class_name|livery_name|veh_hash|livery_id|team|team_src|driver|driver_src|country|alt_team
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = seatSplitPipe(line);
        if (t.size() < 5) continue;
        if (t[0].empty() || t[1].empty() || t[4].empty()) continue;   // with no team it's worthless
        ResolvedSlot rs;
        rs.team     = CleanNameSpaces(t[4]);
        rs.vehHash  = atoi(t[2].c_str());
        rs.liveryNN = t[3].empty() ? -1 : atoi(t[3].c_str());
        g_liveryResolved[equivKey(t[0], t[1])] = rs;
        if (t.size() > 6 && !t[6].empty())                             // first row wins: the lowest slot
            g_resolvedDriverLivery[t[0]].emplace(FoldDriverName(t[6]), t[1]);
    }
    fclose(f);
    return true;
}
int LiveryResolvedCount() { return (int)g_liveryResolved.size(); }
std::string ResolvedLiveryForDriver(const std::string& className, const std::string& driverName) {
    auto c = g_resolvedDriverLivery.find(className);
    if (c == g_resolvedDriverLivery.end() || driverName.empty()) return std::string();
    auto d = c->second.find(FoldDriverName(driverName));
    return d != c->second.end() ? d->second : std::string();
}

// livery_vehicle_map.psv: (class_name, livery_name) -> { vehHash, liveryNN } -------------------
// The EXACT livery -> vehicle binding from the official dump (tools/gen_livery_vehmap.py). Keyed like
// g_teamEquiv (class + "\x1f" + livery) so the two tables cross deterministically in TeamSeatOptionsFor.
struct LiveryVehicle { int vehHash = 0; int liveryNN = -1; };
static std::map<std::string, LiveryVehicle> g_liveryVehicle;   // key = class_name + "\x1f" + livery_name

bool LoadLiveryVehicleMap(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_liveryVehicle.clear();
    char line[1024];
    // Header is a '#'-comment block + a "class_name|..." column line; data is every other non-'#' line.
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = seatSplitPipe(line);
        if (t.size() < 4) continue;                       // need class_name|livery_name|veh_hash|livery_id
        if (t[0].empty() || t[0] == "class_name" || t[1].empty()) continue;
        LiveryVehicle lv;
        lv.vehHash  = atoi(t[2].c_str());
        lv.liveryNN = atoi(t[3].c_str());
        if (lv.vehHash == 0) continue;                    // a mapping without a car is useless
        g_liveryVehicle[equivKey(t[0], t[1])] = lv;       // last write wins (the PSV has no dups)
    }
    fclose(f);
    return true;
}
int LiveryVehicleMapCount() { return (int)g_liveryVehicle.size(); }

// "real skins": accessor over g_liveryVehicle (same key shape as TeamSeatOptionsFor).
bool LiveryVehicleFor(const std::string& className, const std::string& liveryName,
                      int& outVehHash, int& outLiveryNN) {
    auto it = g_liveryVehicle.find(equivKey(className, liveryName));
    if (it == g_liveryVehicle.end()) return false;
    outVehHash = it->second.vehHash;
    outLiveryNN = it->second.liveryNN;
    return true;
}

// (a deliberate design decision, Aug 21: the driver of the player's livery does NOT score while the player has that
// car, not even on skipped rounds): the vanilla livery_name for a SLOT (veh_hash, NN) within a class —
// the INVERSE of the exact binding. It looks at livery_vehicle_map first (the official dump) and falls back to the
// resolved layer (livery_resolved) for rows the dump doesn't cover (measured Aug 5: where both tables
// exist, the slot matches 2924/2924 — there aren't two numbering schemes). "" = unknown slot (no exclusion:
// fail-open: better one extra driver in the simulation than wrongly erasing the right one).
std::string LiveryNameForSlot(const std::string& className, int vehHash, int liveryNN) {
    if (vehHash == 0 || liveryNN < 0) return std::string();
    const std::string pfx = className + '\x1f';
    for (const auto& kv : g_liveryVehicle) {
        if (kv.second.vehHash != vehHash || kv.second.liveryNN != liveryNN) continue;
        if (kv.first.compare(0, pfx.size(), pfx) != 0) continue;
        return kv.first.substr(pfx.size());
    }
    for (const auto& kv : g_liveryResolved) {
        if (kv.second.vehHash != vehHash || kv.second.liveryNN != liveryNN) continue;
        if (kv.first.compare(0, pfx.size(), pfx) != 0) continue;
        return kv.first.substr(pfx.size());
    }
    return std::string();
}

// feeder_team_map.psv: (class_name, livery_name) -> team_label -------------------
// Mirrors g_teamEquiv/equivKey (same "class\x1flivery" key shape). Currently only team_label is consumed
// (column 3 'kind' is discarded here; a later pass will reintroduce it for affiliations).
static std::map<std::string, std::string> g_feederTeam;   // key = class_name + "\x1f" + livery_name

// The real PARENT team for each feeder-team label (key = team_label). Populated by
// LoadFeederTeamMap, deriving the 'academy' rows whose team_label ends in " Academy"; a later pass will add a
// curated PSV that overrides/adds on top. See ParentTeamFor.
static std::map<std::string, std::string> g_teamParent;   // key = team_label -> real parent

static std::string feederKey(const std::string& cls, const std::string& liv) {
    return cls + "\x1f" + liv;   // \x1f = unit separator (never appears in PSV team/livery text)
}

// Derives the real parent of an 'academy' feeder-team label. If it ends EXACTLY with the
// " Academy" suffix (space+Academy, 8 chars), the parent is whatever precedes it (trailing spaces trimmed). If it
// doesn't end in that suffix (e.g. "Kingsmere Kor964"), no derivation is possible -> "" (left for manual curation).
static std::string deriveAcademyParent(const std::string& teamLabel) {
    static const std::string kSuffix = " Academy";
    if (teamLabel.size() <= kSuffix.size()) return std::string();
    if (teamLabel.compare(teamLabel.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) return std::string();
    std::string parent = teamLabel.substr(0, teamLabel.size() - kSuffix.size());
    while (!parent.empty() && parent.back() == ' ') parent.pop_back();
    return parent;
}

bool LoadFeederTeamMap(const char* path) {
    // Clear FIRST so a missing path deterministically empties the map (back-compat: FeederTeamFor falls back
    // to "" and callers keep the prior resolution). Mirrors LoadLiveryTeamPacks' clear-first shape.
    g_feederTeam.clear();
    g_teamParent.clear();   // Also a clean, deterministic reload of the parent table
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    char line[1024];
    // Header is a leading '#'-comment block (class_name|livery_name|kind|team_label); every non-'#' line is data.
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = seatSplitPipe(line);
        if (t.size() < 4) continue;                 // need class_name|livery_name|kind|team_label
        if (t[0] == "class_name") continue;         // guard in case a header line is ever added without a leading '#'
        const std::string label = CleanNameSpaces(t[3]);
        g_feederTeam[feederKey(t[0], t[1])] = label;  // last write wins
        if (t[2] == "academy") {                    // derives the parent for the academy rows
            std::string parent = deriveAcademyParent(label);
            if (!parent.empty()) g_teamParent[label] = parent;
        }
    }
    fclose(f);
    return true;
}
int FeederTeamMapCount() { return (int)g_feederTeam.size(); }

std::string FeederTeamFor(const std::string& className, const std::string& liveryName) {
    auto it = g_feederTeam.find(feederKey(className, liveryName));
    return it == g_feederTeam.end() ? std::string() : it->second;
}

// the real PARENT team for a feeder-team label, or "" if there's no known affiliation.
std::string ParentTeamFor(const std::string& teamLabel) {
    auto it = g_teamParent.find(teamLabel);
    return it == g_teamParent.end() ? std::string() : it->second;
}

// team_affiliations.psv: cantera_team -> parent_real_team (+ optional strength) -------
// Hand-CURATED affiliations that OVERRIDE/add on top of g_teamParent (the academy derivation above). They do NOT
// share a clear step with LoadFeederTeamMap (g_teamParent is NOT cleared here): the curated ones coexist with the
// derived ones and win per-row via override. g_affCount is its OWN counter (don't confuse it with g_teamParent.size(),
// which also includes the derived rows).
static int g_affCount = 0;
static std::map<std::string, double> g_affStrength;   // key = cantera_team -> strength (0..1), diag/

bool LoadTeamAffiliations(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_affCount = 0;
    g_affStrength.clear();   // g_affStrength belongs ONLY to this loader -> clean reload (avoids leaking
                             // stale strengths between loads; relevant on the reload triggered by a profile change).
    char line[1024];
    // Header is a leading '#'-comment block (cantera_team|parent_real_team|strength); every non-'#' line is data.
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = seatSplitPipe(line);
        if (t.size() < 2) continue;                 // need cantera_team|parent_real_team (strength optional)
        if (t[0].empty() || t[1].empty()) continue;
        const std::string cantera = CleanNameSpaces(t[0]);
        g_teamParent[cantera] = CleanNameSpaces(t[1]);   // override: curated rows win over the academy derivation
        if (t.size() >= 3 && !t[2].empty()) g_affStrength[cantera] = atof(t[2].c_str());   // default 1.0 if absent
        g_affCount++;
    }
    fclose(f);
    return true;
}
int TeamAffiliationCount() { return g_affCount; }

// strength 0..1 of a feeder team's affiliation to its parent.
double AffiliationStrengthFor(const std::string& canteraTeam) {
    if (ParentTeamFor(canteraTeam).empty()) return 0.0;          // no parent -> no affiliation
    auto it = g_affStrength.find(canteraTeam);
    return it != g_affStrength.end() ? it->second : 1.0;         // curated, or default 1.0 (there is a parent)
}

// LAYER 1: full livery CATALOG (docs/ams2_liveries.psv) -> class -> ordered car/manufacturer labels.
// The reliable FALLBACK team-naming source for REAL classes that lack manual livery_team_equiv data. Each label
// is the row's livery_name with a trailing " #<number>" stripped ("Ironmarch Sol974 " -> "Ironmarch Sol974").
// Dedup per class preserving first-seen (catalog file) order. Pure, deterministic, no RNG.
static std::map<std::string, std::vector<std::string>> g_liveryCatalog;   // class_name -> distinct car-labels

// Strip a trailing " #<number>" (one or more digits, optionally with a leading space before '#') from a
// livery_name, keeping the car/manufacturer/model text. "Ironmarch Sol974 " -> "Ironmarch Sol974";
// "Dunton Racing 10538 " -> "Dunton Racing 10538". A name with no trailing "#<digits>" is
// returned unchanged. Only the FINAL space-hash-digits run is removed (interior '#' or text is preserved).
static std::string stripTrailingCarNumber(const std::string& livery) {
    size_t end = livery.size();
    // trim trailing whitespace first (defensive; the PSV is clean but be robust).
    while (end > 0 && (livery[end-1] == ' ' || livery[end-1] == '\t')) --end;
    size_t i = end;
    // consume the trailing digit run.
    while (i > 0 && livery[i-1] >= '0' && livery[i-1] <= '9') --i;
    if (i == end || i == 0) return livery.substr(0, end);   // no digits, or string is all digits -> as-is
    if (livery[i-1] != '#') return livery.substr(0, end);   // digits not preceded by '#' -> not a car number
    --i;                                                    // step over the '#'
    // drop the single separating space (if any) before the '#'.
    while (i > 0 && (livery[i-1] == ' ' || livery[i-1] == '\t')) --i;
    return livery.substr(0, i);
}

bool LoadLiveryCatalog(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_liveryCatalog.clear();
    char line[1024];
    // Header is a leading '#'-comment block + a "class_name|xml_name|..." column line; data is every non-'#'
    // line whose first field is not the literal header "class_name".
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = seatSplitPipe(line);
        if (t.size() < 3) continue;                       // need class_name|xml_name|livery_name
        if (t[0].empty() || t[0] == "class_name") continue;
        if (t[2].empty()) continue;                       // no livery_name -> no label
        std::string label = stripTrailingCarNumber(t[2]);
        if (label.empty()) continue;
        std::vector<std::string>& v = g_liveryCatalog[t[0]];
        bool seen = false;
        for (const auto& e : v) if (e == label) { seen = true; break; }   // dedup preserving first-seen order
        if (!seen) v.push_back(label);
    }
    fclose(f);
    return true;
}
int LiveryCatalogClassCount() { return (int)g_liveryCatalog.size(); }

// LAYER 2: the PACKS team map (docs/livery_team_equiv_packs.psv) — class_name -> ordered DISTINCT
// REAL team labels extracted from the lantern_series_8801 NAMeS pack CustomAIDrivers XMLs (generated by
// tools/gen_team_equiv_packs.py: team = livery_name minus a trailing " #<number>"). This tier sits BELOW the
// manual livery_team_equiv (layer 1) and ABOVE the catalog car-labels (layer 1.5): a packs-covered class that
// LACKS manual data is upgraded from a catalog car-label ("BRACKENRIDGE RAV277") to a real team ("Inception Racing").
// NO FABRICATION (the team text is straight from the pack XML). The PSV rows are "class_name|real_team", one
// per distinct team per class, already sorted/dedup'd on generation; we preserve file (first-seen) order here
// (the file is sorted, so the result is deterministic either way). Pure, no RNG.
static std::map<std::string, std::vector<std::string>> g_teamPacks;   // class_name -> distinct real teams

bool LoadLiveryTeamPacks(const char* path) {
    // Clear FIRST so a missing/bogus path deterministically empties the map (lets a test isolate the catalog
    // layer by loading a non-existent packs path — distinct from LoadLiveryCatalog's open-then-keep shape).
    g_teamPacks.clear();
    if (!path) return false;
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    char line[1024];
    // Header is a leading '#'-comment block; every non-'#' line is "class_name|real_team".
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = seatSplitPipe(line);
        if (t.size() < 2) continue;                       // need class_name|real_team
        if (t[0].empty() || t[1].empty()) continue;
        std::vector<std::string>& v = g_teamPacks[t[0]];
        const std::string label = CleanNameSpaces(t[1]);   // 23/09: «N.Piquet Jr.» with a hard space did not match
        bool seen = false;
        for (const auto& e : v) if (e == label) { seen = true; break; }   // dedup preserving first-seen order
        if (!seen) v.push_back(label);
    }
    fclose(f);
    return true;
}
int LiveryTeamPacksClassCount() { return (int)g_teamPacks.size(); }

// CURATED classification (docs/pack_team_class.psv) of the pack labels. The
// pack generator produced "team = livery name", mixing up teams with DRIVERS and CARS. This map gives, per
// label, the CLEAN real team, or EMPTY if it's NOT a team (driver/car/category) -> the class falls back to fictional names.
static std::map<std::string, std::string> g_packTeamClass;   // equivKey(class,label) -> real_team ("" = NOT a team)
bool LoadPackTeamClass(const char* path) {
    g_packTeamClass.clear();                              // clear-first (mirror LoadLiveryTeamPacks)
    if (!path) return false;
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = seatSplitPipe(line);                    // "class|label|team" (empty team = NOT a team)
        if (t.size() < 2 || t[0].empty() || t[1].empty()) continue;
        g_packTeamClass[equivKey(t[0], CleanNameSpaces(t[1]))] = (t.size() >= 3) ? CleanNameSpaces(t[2]) : std::string();
    }
    fclose(f);
    return true;
}
int PackTeamClassCount() { return (int)g_packTeamClass.size(); }
// true if (class,label) is classified (out = clean team, or "" if NOT a team); false if it's not in the map.
static bool PackTeamClassGet(const std::string& cls, const std::string& label, std::string& out) {
    auto it = g_packTeamClass.find(equivKey(cls, CleanNameSpaces(label)));
    if (it == g_packTeamClass.end()) return false;
    out = it->second;
    return true;
}

// docs/team_relationships.psv -- team<->team graph (family|ally|rival|bitter) ----------
static std::map<std::string, std::vector<TeamRelEdge>> g_teamRel;  // team -> edges (both directions)
static int g_teamRelDropped = 0;                                    // diag: discarded edges
static const std::vector<TeamRelEdge> g_teamRelEmpty;               // stable ref for "no edges"

static bool relKindValid(const std::string& k) {
    return k == "family" || k == "ally" || k == "rival" || k == "bitter";
}

bool LoadTeamRelationships(const char* path) {
    g_teamRel.clear();
    g_teamRelDropped = 0;
    if (!path) return false;
    // set of known real teams = the non-empty values of g_packTeamClass
    std::set<std::string> known;
    for (const auto& kv : g_packTeamClass) if (!kv.second.empty()) known.insert(kv.second);
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = seatSplitPipe(line);                 // team_a|team_b|kind
        if (t.size() < 3) continue;
        if (t[0] == "team_a") continue;               // header
        const std::string a = CleanNameSpaces(t[0]); const std::string b = CleanNameSpaces(t[1]); const std::string& k = t[2];
        if (a.empty() || b.empty() || a == b || !relKindValid(k)) { g_teamRelDropped++; continue; }
        if (!known.count(a) || !known.count(b))                    { g_teamRelDropped++; continue; }
        g_teamRel[a].push_back({b, k});
        g_teamRel[b].push_back({a, k});               // symmetric
    }
    fclose(f);
    return true;
}
int TeamRelationsCount() { int n = 0; for (auto& kv : g_teamRel) n += (int)kv.second.size(); return n; }
const std::vector<TeamRelEdge>& TeamRelationsFor(const std::string& team) {
    auto it = g_teamRel.find(team);
    return it != g_teamRel.end() ? it->second : g_teamRelEmpty;
}

// prestige (proxy) = ally-degree (number of family/ally edges), and paths to higher-
// prestige teams reachable through allies in <=2 hops (for the signing UI). Pure, deterministic.
int TeamAllyDegree(const std::string& team) {
    int d = 0;
    for (const auto& e : TeamRelationsFor(team))
        if (e.kind == "family" || e.kind == "ally") ++d;
    return d;
}
std::vector<std::pair<std::string,int>> TeamPathways(const std::string& team, int topN) {
    const int selfDeg = TeamAllyDegree(team);
    std::map<std::string,int> reach;                          // destination -> ally-degree
    for (const auto& e : TeamRelationsFor(team)) {            // 1 ally hop
        if (e.kind != "family" && e.kind != "ally") continue;
        if (e.other == team) continue;
        reach[e.other] = TeamAllyDegree(e.other);
        for (const auto& e2 : TeamRelationsFor(e.other)) {    // 2 ally hops
            if (e2.kind != "family" && e2.kind != "ally") continue;
            if (e2.other == team) continue;
            if (!reach.count(e2.other)) reach[e2.other] = TeamAllyDegree(e2.other);
        }
    }
    std::vector<std::pair<std::string,int>> out;
    for (const auto& kv : reach) if (kv.second > selfDeg) out.push_back(kv);
    std::sort(out.begin(), out.end(), [](const std::pair<std::string,int>& a, const std::pair<std::string,int>& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    if ((int)out.size() > topN) out.resize(topN);
    return out;
}

// LAYER 1: the GENERATION exclusion set (docs/generation_classes.psv) — base/entry/spec/junior/kart
// classes that KEEP cohort homage labels (catalog car-labels are NOT applied to them). Owner-editable PSV.
static std::set<std::string> g_generationClasses;

bool LoadGenerationClasses(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_generationClasses.clear();
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        // one class_name per line; strip CR/LF + surrounding whitespace.
        std::string s;
        for (const char* p = line; *p; ++p) { char c = *p; if (c == '\r' || c == '\n') continue; s += c; }
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
        while (b > a && (s[b-1] == ' ' || s[b-1] == '\t')) --b;
        s = s.substr(a, b - a);
        if (s.empty()) continue;
        g_generationClasses.insert(s);
    }
    fclose(f);
    return true;
}
int  GenerationClassCount() { return (int)g_generationClasses.size(); }
bool IsGenerationClass(const std::string& className) { return g_generationClasses.count(className) != 0; }

// FICTIONAL team-name pool for uncurated cohort/generation seats ------------------
static std::vector<std::string> g_teamNamesPool;   // insertion order = PSV order (deterministic pick by index)

bool LoadTeamNamesPool(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) { g_teamNamesPool.clear(); return false; }   // missing file -> empty pool (legacy fallback), no throw
    g_teamNamesPool.clear();
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        // one fictional team name per line; strip CR/LF + surrounding whitespace (mirrors LoadGenerationClasses).
        std::string s;
        for (const char* p = line; *p; ++p) { char c = *p; if (c == '\r' || c == '\n') continue; s += c; }
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
        while (b > a && (s[b-1] == ' ' || s[b-1] == '\t')) --b;
        s = CleanNameSpaces(s.substr(a, b - a));
        if (s.empty()) continue;
        g_teamNamesPool.push_back(s);
    }
    fclose(f);
    return true;
}
int TeamNamesPoolCount() { return (int)g_teamNamesPool.size(); }

std::string FictionalTeamName(const std::string& seriesId, int seatIdx) {
    if (g_teamNamesPool.empty()) return std::string();   // not loaded -> caller keeps legacy homage/name fallback
    // key by (series|seat): distinct per SERIES (kills the cross-series collapse) AND per seat, stable over time.
    unsigned long h = seatFnv1a(seriesId + "#" + std::to_string(seatIdx));
    return g_teamNamesPool[h % g_teamNamesPool.size()];
}

// DECIDED precedence (deterministic, no RNG): real_team -> alias -> livery_name -> "Privateer #N"
// -> id "seat_privateer_entry". Some real_team cells carry the data-quality placeholder
// "Pendiente / no identificado" (not a real team) — treat those as ABSENT so the label falls through to the
// next concrete source (alias/livery_name) instead of surfacing the placeholder as a team name.
static bool isPlaceholderTeam(const std::string& s) {
    if (s.empty()) return true;
    // case-insensitive prefix check on the known placeholder ("Pendiente ...").
    static const char* kPend = "pendiente";
    size_t n = 0; for (; kPend[n]; ++n) {} if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = s[i]; if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (a != kPend[i]) return false;
    }
    return true;
}

std::string ResolvedTeamFor(const std::string& className, const std::string& liveryName) {
    auto r = g_liveryResolved.find(equivKey(className, liveryName));
    return (r != g_liveryResolved.end()) ? r->second.team : std::string();
}

std::string TeamLabelFor(const std::string& className, const std::string& liveryName) {
    // 0) livery_resolved.psv: the already-merged verdict. Overrides everything -- see LoadLiveryResolved.
    {
        auto r = g_liveryResolved.find(equivKey(className, liveryName));
        if (r != g_liveryResolved.end() && !r->second.team.empty()) return r->second.team;
    }
    auto it = g_teamEquiv.find(equivKey(className, liveryName));
    if (it != g_teamEquiv.end()) {
        const TeamEquiv& te = it->second;
        if (!isPlaceholderTeam(te.real_team)) return te.real_team;   // 1) real_team
        if (!te.alias.empty())                return te.alias;       // 2) alias
    }
    if (!liveryName.empty())                  return liveryName;     // 3) livery_name (always present here)
    // 4) "Privateer #"+car_number (when only a car number is known)
    if (it != g_teamEquiv.end() && !it->second.car_number.empty())
        return std::string("Privateer #") + it->second.car_number;
    return "seat_privateer_entry";   // 5) nameless fallback id (the overlay resolves it via TR())
}

// The ordered, DISTINCT team/car labels a CLASS fields, with the TWO-LAYER precedence documented in
// career_seats.h. LAYER 1 (manual): walks every g_teamEquiv key beginning with `className + "\x1f"`
// (g_teamEquiv is std::map -> key-sorted -> deterministic), resolving each entry with the SAME precedence as
// TeamLabelFor (real_team if !isPlaceholderTeam, else alias, else the livery_name from the key tail), skipping
// empty / "seat_privateer_entry", dedup first-seen. If LAYER 1 is non-empty it WINS (the ~28 curated classes).
// LAYER 2 (catalog fallback): if no manual data AND the class is NOT a generation class AND the full livery
// catalog has it, return the catalog car/manufacturer labels (already dedup'd first-seen in LoadLiveryCatalog).
// Else EMPTY -> the caller keeps its cohort fallback (generation classes + classes absent from both sources).
// NO FABRICATION (layer 2 is the real catalog car name). Pure, deterministic, no RNG.
// does the label look like a real TEAM (not a driver or a car)? Packs
// auto-generated ones (livery_team_equiv_packs.psv) produced "team = livery name", so they MIX teams
// ("3GT Racing") with drivers ("Elmham Racing") and cars ("Stonewood Racing"). POSITIVE test: contains a
// token typical of a team name. Whatever does NOT look like a team falls back to a FICTIONAL name. Deterministic, no RNG.
static bool LooksLikeTeam(const std::string& s) {
    static const char* kTok[] = {
        "Racing", "Motorsport", "Autosport", "Scuderia", "Engineering", "Squadra", "Corse", "Competic",
        "Competit", " Team", "Team ", "Grand Prix", " GP", "GP ", "Performance", "Equipe", "Motors",
        "Tecnica", "Junior Team", " by ", "Sportscar", "Speedworks", "Kompetition", "Kart Team"
    };
    for (const char* t : kTok) if (s.find(t) != std::string::npos) return true;
    return false;
}
// is this a usable REAL team label? it looks like a team AND isn't a known driver name (drivers.json).
static bool IsRealTeamLabel(const std::string& s) {
    return LooksLikeTeam(s) && DriverIdForName(s).empty();
}

std::vector<std::string> TeamLabelsForClass(const std::string& className) {
    std::vector<std::string> out;
    if (className.empty()) return out;
    // LAYER 1: manual real-team data (livery_team_equiv) — the curated classes. Highest precedence.
    const std::string prefix = className + "\x1f";   // key = class_name + "\x1f" + livery_name
    for (const auto& kv : g_teamEquiv) {             // std::map -> key-sorted -> deterministic order
        const std::string& key = kv.first;
        if (key.size() < prefix.size() || key.compare(0, prefix.size(), prefix) != 0) continue;
        const TeamEquiv& te = kv.second;
        const std::string liveryName = key.substr(prefix.size());   // the key tail is the livery_name
        std::string label; bool curated = false;
        if (!isPlaceholderTeam(te.real_team))      { label = te.real_team; curated = true; }   // 1) real_team (curated)
        else if (!te.alias.empty())                { label = te.alias; curated = true; }        // 2) alias (curated)
        else if (!liveryName.empty())              label = liveryName;                          // 3) livery_name (dubious)
        if (label.empty() || label == "seat_privateer_entry") continue;    // skip nameless fallbacks
        // CURATED real_team/alias are always respected; the livery_name fallback can
        // be a DRIVER or a CAR -> it's only accepted if it LOOKS like a real team. Otherwise the class falls back to a fictional name.
        if (!curated && !IsRealTeamLabel(label)) continue;
        // dedup preserving first-seen order (a class fields N distinct teams; a team may run 2 cars).
        bool seen = false;
        for (const auto& e : out) if (e == label) { seen = true; break; }
        if (!seen) out.push_back(label);
    }
    if (!out.empty()) return out;   // LAYER 1 covers the class -> manual real teams win.

    // Generation classes keep cohort labels regardless of which fallback would apply (catalog OR packs): the
    // base/entry/spec/junior/kart ladder is where the career WANTS invented rookie cohorts. Short-circuit here.
    if (IsGenerationClass(className)) return out;   // generation class -> empty -> cohort fallback (by design)

    // LAYER 2 (packs): REAL teams extracted from the NAMeS packs (docs/livery_team_equiv_packs.psv). Sits ABOVE
    // the catalog car-labels: a packs-covered class without manual data shows real teams ("Inception Racing")
    // instead of car-labels ("BRACKENRIDGE RAV277"). NO FABRICATION (the text is straight from the pack XML).
    {
        auto itp = g_teamPacks.find(className);
        if (itp != g_teamPacks.end() && !itp->second.empty()) {
            // The pack generator produced "team = livery name"; in many classes
            // the liveries are DRIVERS ("Elmham Racing") or CARS ("Stonewood Racing"). The
            // CURATED classification (g_packTeamClass) rules: clean real team, or EMPTY (driver/car) -> fictional.
            // Unclassified label (future data) -> the IsRealTeamLabel heuristic. Dedup preserving first-seen order.
            std::vector<std::string> realTeams;
            for (const auto& tlabel : itp->second) {
                std::string pick, curated;
                if (PackTeamClassGet(className, tlabel, curated)) { if (!curated.empty()) pick = curated; }  // curated (class,label) wins
                else if (IsRealTeamLabel(tlabel))       pick = tlabel;                              // heuristic fallback
                if (pick.empty()) continue;                                                        // not a team -> fictional
                bool seen = false; for (const auto& e : realTeams) if (e == pick) { seen = true; break; }
                if (!seen) realTeams.push_back(pick);
            }
            if (!realTeams.empty()) return realTeams;   // real teams (curated/clean), no drivers or cars
        }
    }

    // LAYER 1.5 (catalog) RETIRED as a TEAM source (a deliberate design decision): the
    // catalog labels are CAR names ("Stonewood Racing"), not teams -> players were seeing the car as the "team".
    // Now the class falls back to FICTIONAL names in SeatsForClass. g_liveryCatalog stays loaded for OTHER uses
    // (displaying the car); only its use as a TEAM LABEL is removed.
    return out;   // no curated real team -> empty -> fictional (cohort) in SeatsForClass
}

// the CURATED seat options for a (class, team-label) pair — see career_seats.h. Resolution (0) inverts
// livery_resolved.psv, the SAME layer 0 that names the team in TeamLabelFor (added Aug 5 — see the comment in
// the body: naming and seat MUST read the same table or you sign with one team and drive another's car).
// Resolution (1) inverts
// the equiv table BY LABEL using the SAME per-row precedence TeamLabelsForClass applies (real_team ->
// alias -> livery_name), so every label that surfaces in an offer/seat maps back to exactly the rows that
// produced it; each matching row's livery crosses into livery_vehicle_map for the car+NN. Resolution (2)
// covers raw-livery labels (pinned classes without equiv rows: TeamLabelFor returned the livery verbatim).
// Deterministic (both tables are std::map -> key-sorted). Liveries absent from the vehicle map are skipped.
std::vector<TeamSeatOption> TeamSeatOptionsFor(const std::string& className, const std::string& teamLabel) {
    std::vector<TeamSeatOption> out;
    if (className.empty() || teamLabel.empty() || teamLabel == "seat_privateer_entry") return out;
    const std::string prefix = className + "\x1f";
    // Layer 0 (Aug 5): the SAME resolved layer that names the team in TeamLabelFor, inverted. It has to go
    // here, and first, for a reason that isn't about taste: this is the INVERSE function of TeamLabelFor. If the name
    // comes from livery_resolved but its car is looked up in livery_team_equiv, the two halves of the same question read
    // different tables -- and the moment a class is in the resolved table but not in the equiv one (Ivory_Championship: 0 equiv rows,
    // 26 in the vehicle map) the team ends up with NO options and ResolveTeamSeat falls back to its "fixed-random": you sign
    // with 3422 and you drive 34's car. Found by testing it.
    // The slot comes from the RESOLVED row itself (see ResolvedSlot). livery_vehicle_map remains a backup for
    // older rows with no veh_hash: it's an incomplete dump and can't be the only path, or the 16 teams whose
    // livery isn't in the dump would again end up without their own car.
    for (const auto& kv : g_liveryResolved) {
        const std::string& key = kv.first;
        if (key.size() < prefix.size() || key.compare(0, prefix.size(), prefix) != 0) continue;
        if (kv.second.team != teamLabel) continue;
        int vh = kv.second.vehHash, nn = kv.second.liveryNN;
        if (vh == 0) {
            auto lv = g_liveryVehicle.find(key);
            if (lv == g_liveryVehicle.end()) continue;
            vh = lv->second.vehHash; nn = lv->second.liveryNN;
        }
        TeamSeatOption op;
        op.vehHash = vh; op.liveryNN = nn; op.livery_name = key.substr(prefix.size());
        out.push_back(op);
    }
    if (!out.empty()) return out;
    for (const auto& kv : g_teamEquiv) {
        const std::string& key = kv.first;
        if (key.size() < prefix.size() || key.compare(0, prefix.size(), prefix) != 0) continue;
        const TeamEquiv& te = kv.second;
        const std::string liveryName = key.substr(prefix.size());
        std::string label;
        if (!isPlaceholderTeam(te.real_team))      label = te.real_team;   // 1) real_team
        else if (!te.alias.empty())                label = te.alias;       // 2) alias
        else if (!liveryName.empty())              label = liveryName;     // 3) livery_name
        if (label != teamLabel) continue;
        auto lv = g_liveryVehicle.find(key);       // same key shape -> direct cross
        if (lv == g_liveryVehicle.end()) continue; // livery not in the vehicle map -> no curated option
        TeamSeatOption op;
        op.vehHash = lv->second.vehHash; op.liveryNN = lv->second.liveryNN; op.livery_name = liveryName;
        out.push_back(op);
    }
    if (!out.empty()) return out;
    // (2) the label IS a livery_name (pinned-roster class without equiv rows) -> 1:1 mapping.
    auto lv = g_liveryVehicle.find(equivKey(className, teamLabel));
    if (lv != g_liveryVehicle.end()) {
        TeamSeatOption op;
        op.vehHash = lv->second.vehHash; op.liveryNN = lv->second.liveryNN; op.livery_name = teamLabel;
        out.push_back(op);
    }
    if (!out.empty()) return out;
    // Saved contracts can retain an old feeder label (e.g. "Merrowdown Cor168"). Keep
    // its original vehicle identity instead of assigning an unrelated random car.
    // Current curated teams and exact livery names above keep precedence.
    for (const auto& kv : g_feederTeam) {
        if (kv.second != teamLabel || kv.first.compare(0, prefix.size(), prefix) != 0) continue;
        TeamSeatOption op;
        op.livery_name = kv.first.substr(prefix.size());
        auto resolved = g_liveryResolved.find(kv.first);
        if (resolved != g_liveryResolved.end() && resolved->second.vehHash != 0) {
            op.vehHash = resolved->second.vehHash;
            op.liveryNN = resolved->second.liveryNN;
        } else {
            auto vehicle = g_liveryVehicle.find(kv.first);
            if (vehicle == g_liveryVehicle.end()) continue;
            op.vehHash = vehicle->second.vehHash;
            op.liveryNN = vehicle->second.liveryNN;
        }
        if (op.vehHash != 0 && op.liveryNN >= 0) out.push_back(op);
    }
    return out;
}

std::vector<TeamSeatOption> FilterSeatOptionsByCar(const std::vector<TeamSeatOption>& opts, int ruleVehHash) {
    if (ruleVehHash == 0) return opts;
    std::vector<TeamSeatOption> out;
    for (const auto& o : opts) if (o.vehHash == ruleVehHash) out.push_back(o);
    return out;
}

// THE WORLD'S THREE POPULATIONS, and the middle one didn't exist.
//
//   MODERN    -- the living world: ages, rotates, retires, rookies come in.
//   MEMORY    -- IsEraMemorySeries: off the calendar, no seats.
//   PERIOD-ON-CALENDAR -- the player DOES race these (championship, prizes, progression), but their
//                drivers are anachronistic: the 1960s Keystone Masters 2528 is raced by Emerson Fittipaldi
//                and Carlos Pace; FALCON SERIES 847 by Irvine and Frentzen. They're fixed in place like the
//                memory-series drivers, but for a different reason -- those aren't on the calendar and these are.
//
// Until now the market only knew about the first two and threw the third into the churn: measured on
// Aug 4, the age cap was retiring Emerson Fittipaldi from Formula Vee at age 79, and Carlos Pace
// at 81, and the cascade was handing out their seats as if they were live vacancies.
//
// The data to tell them apart ALREADY EXISTED -- IsModernSeries, the era's year against MODERN_ERA_CUTOFF
// (2015) -- and nothing in the market was checking it. It's frozen by SEAT, not by driver, which is what's
// needed: Barrichello raced AURORA CUP 888 and is still active in Brazilian Stock Car, so his period
// seat stays frozen while his modern one stays live. 135 drivers are in both at once.
//
// The seats KEEP EXISTING (SeatsForClass still returns them): the series is raced, it has teams, and you
// can sign for it. What doesn't happen is the world moving them around.
static bool EsMercadoVivo(const SeriesDef& sd) {
    if (IsEraMemorySeries(sd)) return false;      // memory: no calendar, no seats
    if (IsEventOnlySeries(sd)) return false;      // 01/10: event-only: entirely outside the live world
    return IsModernSeries(sd.id);                 // period-on-calendar: seats YES, market NO
}

// 23/09 (): WHOEVER HAS JUST SIGNED IN THE WINDOW DOES NOT MOVE IN THAT SAME WINTER. The winner of a slot in the
// window is seated at the start of winter (SeatWindowWinnersForNewSeason) with a contract that starts the season
// that comes; if the promotion drag, the seal or the change of incumbent moved them ten lines later, the
// slot would again be left «won by someone who does not appear seated in it», which is exactly the card's failure.
// The three start their `yaMovido` from this set: for them, the newly signed has already moved this year.
static std::set<std::string> FichadosDeLaVentana(const SaveData& save, int worldYear) {
    std::set<std::string> out;
    if (save.window_year != worldYear + 1) return out;
    for (const auto& w : save.window_seats)
        if (w.seated && !w.winner_id.empty()) out.insert(w.winner_id);
    return out;
}


// The POOL id for a driver in a series' field. Same resolution the pinned branch uses
// (the id column if the data has one -> DriverIdForName by name+country -> a stable hash of the name), so the
// SAME driver in two series resolves to the SAME id and the world doesn't duplicate them. Empty if the row has no name.
// Aug 9 — the same string for a PINNED driver. It used to be hand-written inside SeatsForClass and
// nowhere else, so ELO seeding and grid composition resolved it differently (or didn't
// resolve it at all: `pd.id` comes back EMPTY because PSV column 2 is the CAR NUMBER). See the FieldOccupantId block.
std::string PinnedOccupantId(const PinnedDriver& pd) {
    if (!pd.id.empty()) return pd.id;
    if (pd.name.empty()) return std::string();
    const std::string poolId = DriverIdForName(pd.name, pd.country);
    if (!poolId.empty()) return poolId;
    return std::string("rd_") + std::to_string(seatFnv1a(pd.name));
}

std::string FieldOccupantId(const FieldDriver& f) {   // Aug 9: EXPORTED (see career_seats.h) — seeding and the grid now both use THIS
    if (!f.id.empty()) return f.id;
    if (f.name.empty()) return std::string();
    std::string poolId = DriverIdForName(f.name, f.country);
    if (!poolId.empty()) return poolId;
    return std::string("rd_") + std::to_string(seatFnv1a(f.name));
}

// THE SHORT-GRID TAIL. 41 of the 76 series without a pinned roster have fewer
// named people than seats, and that tail used to be covered by the COHORT: the same 28 drivers repeated across
// the whole ladder. Measured: 1.74 seats per head and 23 drivers in more than two tiers at once -- exactly what
// the closing criterion forbids.
//
// Now the cohort stays within its home tiers (bal::COHORT_HOME_MAX_TIER), which is what the
// Aug 2 rethink asked for -- "the cohort concentrates at the BOTTOM... if it plugs census gaps it stops being the
// player's generation and becomes filler" -- and above that, every series has its own people.
//
// DERIVED, not persisted: the id comes from (series, seat index), so it's the same on every launch
// and doesn't cost a single save byte. Same principle as the contract-expiry rule above -- a derived value
// can't drift out of sync or end up orphaned.
static std::string TailOccupantId(const std::string& seriesId, int idx) {
    return "fl_" + seriesId + "#" + std::to_string(idx);
}

// How many substitutes are tried for a slot whose occupant is already seated in another car (). The
// names of the queue are registered up to this multiple of the grid size, so the two figures
// have to move together or the substitute would be born without a name.
static const int kTailSubstituteTries = 3;

// — ONE SEAT FOR EACH CAR THAT RACES, and the livery of each, IN ORDER.
//
// It is the list that defines how many seats a series has and which car each one carries. It exists because
// `SeatsForClass` and `SeatLiveryForIndex` have to take it from the SAME place: if they disagree, the
// seat key stops meaning the same in both and the save seats people in another car.
//
// WHAT IT FIXES, measured on 10/08: 1,709 cars race in the 76 classes of the living world and there were only 1,339
// seats. 40 classes had fewer slots than cars — GT4 was left at 35 — so 370 cars went out on
// track without anybody having signed for them. Two causes, both here:
//   · without a pinned roster, slots were counted with `RosterForTier(tier).size()`, which is the whole cohort
//     (28 people). A class of 40 cars was left at 28;
//   · with a pinned roster, one slot per pinned row, and the pinned one almost never covers the catalog.
//
// IT MOVES NOBODY: the seats that already existed keep their index and their livery (pinned first in the
// file order, then the catalog). The new slots go AFTER, so no save key changes.
static std::vector<std::string> SeatLiveriesForSeries(const std::string& seriesId) {
    std::vector<std::string> out;
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return out;
    const std::vector<std::string> cat = LiveriesForClass(sd->class_name);
    // How many cars really race in this class. ALWAYS bounded by the liveries: a seat without a painted
    // car is not a seat, it is a gap (the phase 2b note about Garrowby Nox98).
    // 0.7.0 port: WITHOUT the player's cap on cars on track (, see career_fields.h). The slots belong to the
    // world; the cap trims the grid that is written, not who has a seat.
    int quiere = WorldGridSizeForClassName(sd->class_name);
    if (quiere <= 0 || quiere > (int)cat.size()) quiere = (int)cat.size();

    std::set<std::string> usadas;
    for (const auto& pd : PinnedRosterForSeries(seriesId)) {      // the pinned one rules and goes first
        if (pd.livery_name.empty() || !usadas.insert(pd.livery_name).second) continue;
        out.push_back(pd.livery_name);
    }
    // If the pinned one already carries more slots than cars the class asks for, they are respected: that file IS the championship.
    for (const auto& lv : cat) {
        if ((int)out.size() >= quiere) break;
        if (!usadas.insert(lv).second) continue;
        out.push_back(lv);
    }
    return out;
}

// The DERIVED occupant of seat `i`, without looking at the game. It is the precedence of the rethinking of
// 02/08 put in a single place: pinned -> field -> cohort (ONLY in their home tiers) -> the series'
// own people. It is shared by `SeatsForClass` and `EnsureTailNames`, which before each had it on its own and for
// that reason the second left without a name precisely the slots the first had just created.
static std::string DerivedSeatOccupant(const std::string& seriesId, int tier, int i,
                                       const std::vector<PinnedDriver>& pinned,
                                       const std::vector<FieldDriver>& field,
                                       const std::vector<RosterDriver>& cast,
                                       int packSlot = -1) {
    if (i < (int)pinned.size()) {
        const std::string id = PinnedOccupantId(pinned[i]);
        if (!id.empty()) return id;
    }
    // The field indexes from the first NON-pinned seat: in a series with a pinned roster the field is another
    // source for the slots that one does not cover, not a second distribution over the same ones.
    const int fi = i - (int)pinned.size();
    if (fi >= 0 && fi < (int)field.size()) {
        const std::string id = FieldOccupantId(field[fi]);
        if (!id.empty()) return id;
    }
    // 17/09 — THE GENERATION IS SPREAD; IT IS NOT CUT BY THE SEAT INDEX. Until today slot `i` got
    // `cast[i]`, and since the field ALWAYS covers the low indices, the cohort only reached the HIGH ones
    // — and they are the same in the eleven level-1 series. Measured with `--medidas` at T0: 103 cohort rows
    // spread among SIXTEEN people (Andre Costa in ten grids at once), with ELO 1075..1269, and the
    // first twelve of the cast without a car anywhere. Among them Lucía Vega, the head of your
    // generation: without a seat there are no results, without results the ELO does not move, and that is why she stayed stuck
    // at 1485 five seasons later while the world grew without her.
    //
    // The rotation spreads the SAME seats among the 28 people: it changes WHO, not HOW MANY, just as
    // Phase 0c did with the field. The offset comes from the series id, so it is stable between startups and
    // between the two routes that share this precedence (`SeatsForClass` and `EnsureTailNames`). And it is injective
    // within a series because the guard above keeps `i` below the size of the cast.
    if (tier <= bal::COHORT_HOME_MAX_TIER && !cast.empty() && i < (int)cast.size()) {
        const int giro = (int)(seatFnv1a(seriesId) % (unsigned long)cast.size());
        const std::string id = cast[(i + giro) % (int)cast.size()].id;
        if (!id.empty()) return id;
    }
    // — CONTENT THAT ALREADY EXISTS FIRST. Before inventing anybody, a REAL driver from the livery
    // packs that we already have on disk. Measured on 10/08: of the 245 slots left uncovered, 222
    // (90%) are covered by a real person from the catalog — all that was needed was to stop throwing them away.
    // `packSlot` is the ordinal of the UNCOVERED slot within the series, not the seat index: that way the
    // distribution is stable even if the field grows or shrinks ahead of it.
    if (packSlot >= 0) {
        const SeriesDef* sd = SeriesById(seriesId);
        if (sd) {
            if (const PackDriver* pk = PackDriverForClassSlot(sd->class_name, packSlot)) {
                const std::string id = PackDriverId(*pk);
                if (!id.empty()) return id;
            }
        }
    }
    return TailOccupantId(seriesId, i);
}

// Derive the team/seat/occupancy model for a series. Occupants come from the pinned roster
// (series_rosters.psv) when the series has one — each pinned driver carries its livery, which resolves to a
// team label. Otherwise (lower/cohort classes with no pinned roster) the occupants come from
// RosterForTier(level) and the label is the cohort homage/driver name (still non-empty -> all tiers
// populate). The persisted seat_occupant map ("<series_id>#<seat_idx>") overrides a seat's occupant.
void EnsureTailNames();   // defined next to the name generator, further down
void EnsureRosterNames(); // ditto: registers the name of the whole curated grid in the registry

// — the world checksum (see career_seats.h). 64-bit fnv1a, local on purpose: the 32-bit one
// from `career_market` is used for DRAWS and mixing them would invite someone to swap one for the other.
unsigned long long WorldSeatChecksum(const SaveData& save) {
    unsigned long long h = 1469598103934665603ULL;   // fnv1a-64
    auto mete = [&h](const std::string& s) {
        for (unsigned char ch : s) { h ^= ch; h *= 1099511628211ULL; }
        h ^= 0x1f; h *= 1099511628211ULL;             // separator: 'ab|c' cannot collide with 'a|bc'
    };
    for (const auto& sd : AllSeries()) {
        mete(sd.id);
        const std::vector<Seat> ss = SeatsForClass(sd.id, save);
        for (size_t i = 0; i < ss.size(); ++i) {
            mete(std::to_string(i));
            mete(ss[i].livery_name);
            mete(ss[i].occupant_id);
            mete(ss[i].team_label);
            mete(std::to_string(ss[i].tier));
            mete(ss[i].open ? "1" : "0");
        }
    }
    return h;
}

// 09/09 (performance): the DATA part of SeatsForClass, without the save. See career_seats.h (SeatsForClassUncached).
static std::vector<Seat> SeatsFromData(const std::string& seriesId) {
    std::vector<Seat> seats;
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return seats;                       // unknown series -> no seats
    if (IsEraMemorySeries(*sd)) return seats;    // no historic/era seats in the modern market
    if (IsEventOnlySeries(*sd)) return seats;    // 01/10: event-only: no seats (neither teams nor signings)

    int tier = (sd->level >= 1 && sd->level <= 7) ? sd->level : 1;
    const std::string& cls = sd->class_name;

    // the slots are those of SeatLiveriesForSeries — one seat per car that races. Before
    // they were «one per pinned row» or «one per cohort member», and both fell short.
    const std::vector<std::string> seatLiv = SeatLiveriesForSeries(seriesId);
    const std::vector<FieldDriver>  fieldAll = FieldForSeries(seriesId);
    const std::vector<RosterDriver> castAll  = RosterForTier(tier);
    // the occupants come out of the SAME list seen by the seeding and the bridge to the grid, substitutes
    // included. Three views of the world that did not say the same thing is where all this came from.
    const std::vector<std::string> ocupantes = DerivedSeatOccupants(seriesId);

    // Primary source: the pinned roster (real driver -> livery -> team). Order = standings order.
    std::vector<PinnedDriver> pinned = PinnedRosterForSeries(seriesId);
    if (!pinned.empty()) {
        for (size_t i = 0; i < seatLiv.size(); ++i) {
            Seat s;
            s.tier         = tier;
            s.livery_name  = seatLiv[i];       // the bridge: the pinned driver already came tied to THEIR livery
            s.team_label   = TeamLabelFor(cls, s.livery_name);
            // The occupant's id has to be the POOL's id, or the driver doesn't exist for the rest of the
            // engine. Here, "rd_" + hash(name) used to get minted when the PSV had no id -- and the PSV never
            // has one, because its column 2 is the CAR NUMBER. Result measured Aug 3: the seat for
            // Hamilton ended up as 'rd_1234567890' while the pool has him as 'rd_lewis_hamilton',
            // and the pool doesn't contain a SINGLE id shaped like rd_<number>. They never matched, so
            // BirthYearFor returned 0 for the 340 real-driver seats: with no age there's no procedural
            // retirement, and those seats never opened up.
            //
            // DriverIdForName is the resolution the rest of the engine already uses (career_engine, the
            // living-world harvest) and it disambiguates by country. If it doesn't resolve -- a pinned driver who
            // isn't in the pool -- it falls back to the old hash, which is at least stable and unique.
            //
            // the slots BEYOND the pinned roster (the pinned one almost never covers the catalog:
            // F1 2025 carries 19 rows for 20 cars) are covered by the same precedence as the rest of the world.
            // The ones that already existed do not move: for i < pinned.size() this returns exactly what it did before.
            s.occupant_id = (i < ocupantes.size()) ? ocupantes[i] : std::string();
            seats.push_back(s);
        }
    } else {
        // Fallback: lower/cohort class with no pinned roster -> occupants from the tier roster.
        // Partial: if the class HAS livery_team_equiv real-team data (the ~28 covered classes),
        // surface those REAL team labels (cycled deterministically by seat index — a team may field 2 cars),
        // resolved ONCE before the loop. This is a TEAM-LABEL-only change: the cohort driver stays the
        // occupant (occupant_id unchanged) and the seat count / open-occupancy logic is untouched, so
        // WeakestIncumbentRating / displacement / occupancy tracking are unaffected. Classes WITHOUT equiv
        // coverage (the bottom/generation classes) keep the cohort homage/name label (correct for them).
        std::vector<std::string> classTeams = TeamLabelsForClass(cls);
        std::vector<RosterDriver> cast = RosterForTier(tier);
        // WHO occupies the seat comes from THEIR SERIES' FIELD, not the tier roster.
        // RosterForTier returns the COHORT (28 drivers) and it's the same for the 13 series in a tier, so
        // until now the market believed 28 people occupied 1316 of the world's 1656 seats:
        // 47 seats per head, six of them present in all SEVEN tiers at once. With that, no
        // cascade is possible -- retiring one would empty half the ladder, and the closing criterion
        // ("no driver in more than 2 tiers") was unreachable by construction.
        // series_fields.psv already carries the named people for each series: 868 DISTINCT drivers covering
        // 880 of those 1316 seats. The Aug 2 rethink's precedence: real -> plausible generated
        // > cohort, with the cohort ONLY as a last resort, which is exactly what's left below.
        // The number of seats does NOT change (the cast still sets that): this changes WHO, not HOW MANY.
        std::vector<FieldDriver> field = FieldForSeries(seriesId);
        // The curated feeder-team map (docs/feeder_team_map.psv) resolved via
        // LiveriesForClass(cls) so seat i indexes livery i (same catalog order ComposeSeasonGrid uses).
        std::vector<std::string> liveries = LiveriesForClass(cls);
        // THE GRID CAN'T HAVE MORE SEATS THAN PAINTED CARS. Each (car, skin) is a
        // unit: a seat with no livery isn't a seat, it's a gap with no car. Garrowby Nox98 had 28 seats with
        // 26 liveries, and since the preseason opener ALWAYS frees the last one, the seats offered
        // to the player at the start were exactly the two with no car — which is why they came up with a made-up
        // team name instead of the one the user had tied to that livery.
        // Guard: a class with no livery catalog (empty list) is NOT trimmed, or it would end up with no grid.
        // the number of slots is NO LONGER set by the size of the cohort (28), which left a class of
        // 40 cars with 28 seats. It is set by how many cars race. See SeatLiveriesForSeries.
        const size_t nSeats = seatLiv.empty() ? cast.size() : seatLiv.size();
        for (size_t i = 0; i < nSeats; ++i) {
            const RosterDriver* rd = (i < cast.size()) ? &cast[i] : nullptr;
            Seat s;
            s.tier        = tier;
            // the bridge: seat i <-> livery i, which is the SAME index already used by the curated team and the
            // grid composition. This just makes explicit what the cascade already assumed.
            s.livery_name = (i < seatLiv.size()) ? seatLiv[i]
                          : ((i < liveries.size()) ? liveries[i] : std::string());
            // The field rules; the cohort covers what the field doesn't reach (41 of the 76
            // series have a field shorter than their seat grid). An empty id would leave the seat
            // reading as OPEN, so a nameless row also falls back to the cohort.
            s.occupant_id = (i < ocupantes.size()) ? ocupantes[i] : std::string();
            // FIRST, the curated livery->team map (seat i <-> livery i). Empty (map not
            // loaded, or the pair is missing) -> falls back to the prior precedence (class teams -> fictional).
            // Label-only: occupant_id untouched.
            // The feeder map wins over the class-teams data. Both datasets must remain
            // DISJOINT: gen_feeder_team_map.py only emits generation_classes (which lack curated real-team equiv data),
            // so today there's NO overlap. If they ever did overlap, the feeder wins ON PURPOSE (feeder-team curation
            // > generic catalog) -> keep them disjoint, or revisit this precedence here.
            std::string feederTeam = s.livery_name.empty() ? std::string() : FeederTeamFor(cls, s.livery_name);
            // THE TEAM TRAVELS WITH THE LIVERY, not the driver. Seat i carries livery i and the
            // team comes from THAT livery; when the seat opens up only the occupant changes, so Garrowby Nox98
            // #7 stays Tipco's no matter who's driving it. The full cascade (TeamLabelFor) does NOT apply here
            // because in a generation class it's deliberately cut short -- it would end up using car names.
            // What always has to come through is the hand-CURATED data, which is this.
            std::string curada = s.livery_name.empty() ? std::string() : ResolvedTeamFor(cls, s.livery_name);
            // Aug 5 -- THE CURATED LAYER WINS OVER THE FEEDER, and it used to be the other way around. The comment above
            // warned that the two datasets had to be DISJOINT, and that if they overlapped, the
            // feeder would win "on purpose". They DO overlap: the feeder covers 626 liveries from the SAME
            // generation classes that now have a row in livery_resolved.psv, so it was stomping on the hand-
            // curated data. Measured symptoms: Garrowby Nox98 #7 was coming up "Peregrine Racing" instead of the "Tipco" the
            // user set, and -- worse -- the feeder's `driver`-type rows put the
            // CAR's name ("Merrowdown Cor168") or the DRIVER's ("Pinedale Racing") as the team, which is exactly what
            // the resolved layer exists to fix. The resolved layer covers all 626, so putting it first doesn't
            // lose a single one.
            //
            // The feeder stays UNDERNEATH, it isn't deleted: it's still the safety net if the resolved layer fails to load.
            if (!curada.empty()) {
                s.team_label = curada;
            } else if (!feederTeam.empty()) {
                s.team_label = feederTeam;
            } else if (!classTeams.empty()) {
                // Covered class: a REAL team from the class's equiv data (deterministic by index).
                s.team_label = classTeams[i % classTeams.size()];
            } else {
                // Uncovered (cohort/generation) class: a DETERMINISTIC fictional team name from the pool,
                // keyed by (series|seat) so each series gets DISTINCT, STABLE names instead of
                // collapsing to a single shared cohort driver name ("Luz Verdano" xN). Pool EMPTY (not loaded) ->
                // keep the legacy homage/name fallback so nothing regresses (tests that don't load the pool are
                // byte-identical). Label-only: occupant_id (the cohort driver) is unchanged.
                std::string fict = FictionalTeamName(seriesId, (int)i);
                if (!fict.empty()) s.team_label = fict;
                else if (rd && !rd->homage.empty()) s.team_label = rd->homage;
                else if (rd && !rd->name.empty())   s.team_label = rd->name;
                else                                s.team_label = "seat_privateer_entry";
            }
            seats.push_back(s);
        }
    }

    return seats;
}
namespace {
struct SeatsDataMemo { long long firma = -1; unsigned long long epoch = 0; std::map<std::string, std::vector<Seat>> porSerie; };
SeatsDataMemo g_seatsData;
// The SAME signature as TalentoFirma/EnsureTailNames: it changes with any reload that matters (the tests reload the
// PSVs between cases), plus the affinity epoch (full load, ReplaceSeries, rollover).
long long SeatsDataFirma() {
    return (long long)AllSeries().size() * 1000003LL + (long long)FieldRowCount() * 131LL
         + (long long)PinnedRosterCount() * 17LL + (long long)RosterRowCount();
}
const std::vector<Seat>& SeatsFromDataMemo(const std::string& seriesId) {
    const long long firma = SeatsDataFirma();
    const unsigned long long ep = DataEpoch();   // 0.7.0 port: the data epoch (f0: AffinityEpoch, from contracts)
    if (g_seatsData.firma != firma || g_seatsData.epoch != ep) {
        g_seatsData.porSerie.clear(); g_seatsData.firma = firma; g_seatsData.epoch = ep;
    }
    auto it = g_seatsData.porSerie.find(seriesId);
    if (it == g_seatsData.porSerie.end()) it = g_seatsData.porSerie.emplace(seriesId, SeatsFromData(seriesId)).first;
    return it->second;
}
// The save's override layer, shared by the two versions.
void ApplySeatOverrides(std::vector<Seat>& seats, const std::string& seriesId, const SaveData& save);
}  // anon
std::vector<Seat> SeatsForClassUncached(const std::string& seriesId, const SaveData& save) {
    EnsureTailNames();
    std::vector<Seat> seats = SeatsFromData(seriesId);
    ApplySeatOverrides(seats, seriesId, save);
    return seats;
}
std::vector<Seat> SeatsForClass(const std::string& seriesId, const SaveData& save) {
    EnsureTailNames();                       // once per data load, not per call
    std::vector<Seat> seats = SeatsFromDataMemo(seriesId);   // copy of the data skeleton
    ApplySeatOverrides(seats, seriesId, save);
    return seats;
}
namespace { bool SerieConTabla068(const std::string& seriesId); }   // defined next to SeatIndex068

namespace {
void ApplySeatOverrides(std::vector<Seat>& seats, const std::string& seriesId, const SaveData& save) {
    // Occupancy reflection from the persisted save (the living-world market drives open/occupied state):
    // a "<series_id>#<seat_idx>" entry OVERRIDES that seat's occupant; absence leaves the derived occupant.
    // The kSeatOpen sentinel VACATES the seat (retirement/promotion freed it) -> occupant cleared,
    // the seat becomes open even though the static roster derived an occupant for it.
    //
    // 16/09 — where there CAN be orphans: only in an already-migrated save AND in one of the series that have a frozen
    // table from 0.6.8. In any other series a `serie#i` left in the save was written by 0.7.0 itself
    // because its slot does not resolve (there are 4 modern classes and 187 era ones like that), and that one has to keep being
    // read: if tomorrow its slot starts to resolve —a DLC, a livery curation— the key would start to
    // be looked up by slot and without this fallback the occupancy would be lost. It is computed ONCE per call, not per
    // seat, so as not to pay a `paths::Data` per car.
    const bool puedeHaberHuerfanas = save.seat_key_epoch != 0 && SerieConTabla068(seriesId);
    for (size_t i = 0; i < seats.size(); ++i) {
        // point 5, «keep reading the old format»: the per-slot key first and the legacy one
        // after. The load migration leaves the first, but a save that arrives by a path that did not
        // migrate has to keep seating its people — losing the occupancy is the only unacceptable failure
        // of this change, and it is paid for with one more lookup in a map.
        //
        // 16/09 (H1 of the migration review) — BUT NOT WHERE THERE CAN BE ORPHANS. In an already
        // migrated save and in a series with a 0.6.8 table, the legacy keys that remain are ORPHANS: they belong to
        // people who no longer race the series, and falling back to them applies them to WHOEVER OCCUPIES THAT INDEX TODAY. Measured:
        // 5 of the 13 fell on a new face — in FALCON SERIES 847 Wurz's on Alonso, Diniz's on
        // Marques and Mazzacane's on Burti; in F1 2021 Magnussen's on Tsunoda and Grosjean's
        // on Latifi — and if the mark is kSeatOpen, those drivers came out with the seat open. With this the
        // orphan is truly inert, which is what the per-PERSON migration promises.
        // The net of the UNmigrated save is kept whole, so is that of the series without a table, and when the
        // slot does not resolve `SeatKey` ALREADY returns the legacy key itself, so the first lookup
        // covers it just the same.
        auto it = save.seat_occupant.find(SeatKey(seriesId, (int)i));
        if (it == save.seat_occupant.end() && !puedeHaberHuerfanas)
            it = save.seat_occupant.find(SeatKeyLegacy(seriesId, (int)i));
        if (it != save.seat_occupant.end() && !it->second.empty()) {
            if (it->second == kSeatOpen) seats[i].occupant_id.clear();   // explicitly freed seat
            else                          seats[i].occupant_id = it->second;
        }
        seats[i].open = seats[i].occupant_id.empty();   // a seat with no occupant is open (offer-eligible)
    }
}
}  // anon

// Upgrade the rollover's tier-COUNT freed-seat tracking to PER-TEAM granularity. For
// every non-era-memory series, resolve the seats against the (already-updated) save and:
//   mark a retiree's SPECIFIC seat OPEN (kSeatOpen sentinel) — that team now has an open seat (offer source);
//   re-occupy one open seat at a promoted climber's DESTINATION tier (the AI->AI fill), so only genuinely-
//     still-open seats remain — consistent with PromoteFreedSeats (a freed seat the AI took is NOT an offer).
// Pure over the data tables; mutates ONLY save.seat_occupant. Deterministic (AllSeries() order, seats by index).
// Declared here because its definition lives further down (with the rest of that block) and this feature
// needs it beforehand: DemandForClass averages exactly the same aged rating used to decide the
// player's displacement. Two different projections of the same driver would be two different worlds.
static double agedRatingFor(const std::string& id, double skill, int rosterBirthYear,
                            const SaveData& save, int worldYear, int ageShift, int tier);

// the EFFECTIVE rating (capacity + results) is defined next to `agedRatingFor`, a thousand lines further down,
// but it is used by `ExpireSeatContracts`, which comes before. The STRUCTURE has to be whole here --
// `ExpireSeatContracts` declares one by value and a forward declaration is not enough for that--; the two
// functions are forward-declared. See the long note on `measureFormScale` for why the results have to be
// TYPED before subtracting them, which is the part that matters.
struct FormScale {
    double capMedia = 0.0, capSd = 1.0, eloMedia = 0.0, eloSd = 1.0;
    bool ok = false;
};
static FormScale measureFormScale(const std::string& seriesId, int tier, const SaveData& save, int worldYear);
static double effectiveRating(const std::string& id, double capacidad, const SaveData& save,
                              const FormScale& esc, bool acotado = true);

// how many extra years someone racing in this class holds on for (the class ceiling minus the flat
// threshold). Never negative: you leave a feeder school by moving up or down, not by retiring at 24. With no
// class_age_profile.psv loaded, it comes out 0 and the engine behaves as it did before this.
static int ClassAgeShift(const std::string& className) {
    const int techo = ClassAgeCeiling(className);
    return techo > bal::RETIRE_AGE_BASE ? techo - bal::RETIRE_AGE_BASE : 0;
}

// A (skill, birth year) record by occupant id, using the SAME THREE SOURCES and in the
// SAME ORDER SeatsForClass uses to pick who sits: pinned -> series field -> cohort.
//
// It used to be duplicated in WeakestIncumbentRating and TeamSalaryBand, and their own comments already warned
// that they had to look at "the same places". Once the field became a source of occupants, keeping
// two copies would have meant a team's salary band got calculated with drivers at a flat 0.85
// while the player's displacement used the real ones. One single function, three callers.
static std::map<std::string, std::pair<double,int>> OccupantMeta(const std::string& seriesId, int tier) {
    std::map<std::string, std::pair<double,int>> meta;   // id -> (skill 0..1, birth year)
    for (const auto& pd : PinnedRosterForSeries(seriesId)) {
        std::string id = pd.id.empty() ? DriverIdForName(pd.name, pd.country) : pd.id;
        if (id.empty()) id = std::string("rd_") + std::to_string(seatFnv1a(pd.name));
        meta[id] = std::make_pair(pd.skill, BirthYearFor(id));
    }
    for (const auto& f : FieldForSeries(seriesId)) {
        const std::string id = FieldOccupantId(f);
        if (!id.empty()) meta[id] = std::make_pair(f.skill > 0.0 ? f.skill : 0.85, BirthYearFor(id));
    }
    for (const auto& rd : RosterForTier(tier))
        meta[rd.id] = std::make_pair(rd.base_skill, rd.birth_year);
    return meta;
}

// A DRIVER'S ABSOLUTE TALENT, and why it's derived instead of stored.
//
// Simulating eight seasons surfaced the model's deepest flaw: Alonso was losing 157 rating points in
// ONE year, from 1620 to 1463. The age curve from 44 to 45 accounts for ten. The other 147 came from his skill
// it would re-derive from the seat he was in: he was Stonefield Motors's #2, moved to a midfield seat at a
// Northbourne Racing, and the model concluded he WAS worse. That's a drain -- you drop, you get worse, and next year you
// don't even hold that level, and you drop again.
//
// A driver's talent has to be THEIRS. It's computed once, from wherever their history placed them, and only
// age touches it afterward. And it's DERIVED from the static world (rosters + fields, with an empty save) instead of being persisted:
// same reason as with contracts above -- a derived value can't drift out of sync or end up
// orphaned. The static world never changes, so neither does the result.
//
// Of a driver's starting seats, the BEST one wins: a driver who starts in F1 and also in a GT car is an
// F1 driver who races GT, not the other way around.
static std::map<std::string, double>* g_talento = nullptr;
static std::map<std::string, double>* g_arranqueCoh = nullptr;   // see CohortCurveStart, at the end of BaseTalent
static long long g_talentoFirma = -1;

static long long TalentoFirma() {
    // Tests reload the PSVs between cases; without this the cache would go stale and a test would see the
    // previous test's talent. The signature is cheap and changes on any reload that matters.
    return (long long)AllSeries().size() * 1000003LL + (long long)FieldRowCount() * 131LL
         + (long long)PinnedRosterCount() * 17LL + (long long)RosterRowCount();
}

double BaseTalent(const std::string& driverId) {
    if (driverId.empty()) return 0.0;
    const long long firma = TalentoFirma();
    if (!g_talento || g_talentoFirma != firma) {
        delete g_talento;
        g_talento = new std::map<std::string, double>();
        g_talentoFirma = firma;
        const SaveData vacio;                      // the STATIC world: without the save's override layer
        // 17/09 — THE COHORT HAS NO «BEST STARTING CATEGORY», AND THAT IS WHY THIS FLATTENED IT.
        //
        // The rule above —the anchor comes from the best seat you start in— is right for the
        // 1,400 drivers of the world, whose starting place IS the datum that says what they are worth. The cohort is the
        // only population from which that CANNOT be read: `COHORT_HOME_MAX_TIER` seats them all at
        // level 1 by design, so its anchor came out 0 for all 28 and their absolute talent ended up in the
        // range of a karting driver. Measured: Lucia Vega ~183 against ~519 for an ace of the top category. With that
        // ceiling it is not that she took long to get to the top, it is that it was IMPOSSIBLE: the ceiling is what one is worth at
        // their peak, and hers was that of a debutant forever.
        //
        // What truly says where an `rk_*` ends up is its AUTHORED `tier_max` in series_roster.psv, which
        // is exactly the «career ceiling» field and the only population that carries it written by hand. It is also
        // what requires the ceiling to belong to the PERSON, and the archetype only sets the shape. And it does not
        // overlap with the seeding: the START remains anchored at COHORT_SEED_LEVEL (career_engine.cpp), which is
        // the other number of the two that the rookie fix asked for — potential and start, separated.
        std::map<std::string, int> techoCohorte;
        for (const auto& rd : RosterForTier(1))
            if (!rd.id.empty() && rd.tier_max >= 1 && rd.tier_max <= 7) techoCohorte[rd.id] = rd.tier_max;
        for (const auto& sd : AllSeries()) {
            if (IsEraMemorySeries(sd)) continue;
            const int tier = (sd.level >= 1 && sd.level <= 7) ? sd.level : 1;
            const double ancla = (tier - 1) * bal::RATING_TIER_STEP;
            const std::map<std::string, std::pair<double,int>> meta = OccupantMeta(sd.id, tier);
            for (const auto& s : SeatsForClass(sd.id, vacio)) {
                if (s.occupant_id.empty()) continue;
                auto m = meta.find(s.occupant_id);
                const double skill = (m == meta.end()) ? 0.85 : m->second.first;
                double dentro = (skill - bal::RATING_SKILL_FLOOR)
                              * (bal::RATING_SEED_TOP - bal::RATING_SEED_BOTTOM);
                if (dentro < 0.0) dentro = 0.0;
                double anclaSuya = ancla;
                const auto tc = techoCohorte.find(s.occupant_id);
                if (tc != techoCohorte.end()) anclaSuya = (tc->second - 1) * bal::RATING_TIER_STEP;
                double& v = (*g_talento)[s.occupant_id];
                if (anclaSuya + dentro > v) v = anclaSuya + dentro;   // their best starting category wins
            }
        }
        // 17/09 — AND THE START, WHICH IS THE OTHER HALF OF THE SAME PAIR.
        //
        // Raising the cohort's ceiling without touching anything else made it worth that ALREADY: `agedRatingFor` is
        // `ceiling x progress`, and without an entry in `driver_curve` the progress falls to the universal rise of its
        // age (~0.9 at 22). That is, a 22-year-old girl entered the world worth what an established
        // ace is, and besides she poisoned the bar of the entry classes — where the whole cohort
        // sits— until the generated rookies were left out. It is exactly the failure that was already fixed for
        // the rookies: the POTENTIAL number installing itself as a STARTING value.
        //
        // Here the same is done as there and for the same reason: potential and start are TWO numbers. The
        // cohort's start already exists and is not touched —it is `CohortAnchoredSeed`, the anchor of its seeding
        // level—, so the exit curve is the fraction that goes from one to the other. At T0 they are worth exactly
        // what they were worth; what changes is how far they can go. Derived and not stored, just like the
        // ceiling: that way a 0.6.8 game inherits it with no migration at all.
        delete g_arranqueCoh;
        g_arranqueCoh = new std::map<std::string, double>();
        std::vector<RosterDriver> elenco = RosterForTier(1);
        // The SAME order with which `SeedAllDrivers` seeds (career_engine.cpp): by base_skill and, on
        // a tie, by id. If it were ordered differently here, the start would not be that of its own seeding.
        std::sort(elenco.begin(), elenco.end(), [](const RosterDriver& a, const RosterDriver& b) {
            if (a.base_skill != b.base_skill) return a.base_skill > b.base_skill;
            return a.id < b.id;
        });
        for (size_t k = 0; k < elenco.size(); ++k) {
            if (elenco[k].id.empty()) continue;
            const auto t = g_talento->find(elenco[k].id);
            if (t == g_talento->end() || t->second <= 0.0) continue;
            const double arranque = CohortAnchoredSeed((int)k + 1, (int)elenco.size(), elenco[k].hot)
                                  - bal::RATING_SEED_BOTTOM;
            double p = arranque / t->second;
            if (p < 0.02) p = 0.02;                 // nobody starts at zero: it would still not exist
            if (p > 1.0)  p = 1.0;                  // nor above their own ceiling
            (*g_arranqueCoh)[elenco[k].id] = p;
        }
    }
    auto it = g_talento->find(driverId);
    return it == g_talento->end() ? 0.0 : it->second;
}

// The exit progress of an `rk_*` toward their ceiling (see the long note inside BaseTalent). 0 = not
// of the cohort, and then the usual universal rise by age rules.
double CohortCurveStart(const std::string& driverId) {
    if (driverId.empty() || driverId.rfind("rk_", 0) != 0) return 0.0;
    BaseTalent(driverId);                        // raises (or revalidates) the cache; they share a signature
    if (!g_arranqueCoh) return 0.0;
    const auto it = g_arranqueCoh->find(driverId);
    return it == g_arranqueCoh->end() ? 0.0 : it->second;
}

// THE ENGINE'S ONLY aged-rating projection. Rollover used to have its own
// (career_calendar.cpp), and once the ladder was anchored, they ended up disagreeing: the market measured
// a driver on one scale and retirement on another. One single function, two callers.
double DriverAgedRating(const std::string& id, double skill, int birthYear,
                        const SaveData& save, int worldYear, int ageShift, int tier) {
    return agedRatingFor(id, skill, birthYear, save, worldYear, ageShift, tier);
}

// An occupant's absolute rating IN a series: that series' tier anchor + their
// grid position, with the age curve applied on top. Public because it's needed outside the engine to
// track a career year by year (tools/cascade_probe.cpp), and because the UI will eventually display it.
double OccupantRating(const std::string& seriesId, const std::string& occupantId,
                      const SaveData& save, int worldYear) {
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd || occupantId.empty()) return 0.0;
    const std::map<std::string, std::pair<double,int>> meta = OccupantMeta(seriesId, sd->level);
    auto m = meta.find(occupantId);
    const double skill = (m == meta.end()) ? 0.85 : m->second.first;
    const int    birth = (m == meta.end()) ? 0    : m->second.second;
    return agedRatingFor(occupantId, skill, birth, save, worldYear, ClassAgeShift(sd->class_name), sd->level);
}

// A CATEGORY'S DEMAND FLOOR (see career_seats.h).
ClassDemand DemandForClass(const std::string& seriesId, const SaveData& save, int worldYear) {
    ClassDemand out;
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd || IsEraMemorySeries(*sd)) return out;
    const std::map<std::string, std::pair<double,int>> meta = OccupantMeta(seriesId, sd->level);
    const int shift = ClassAgeShift(sd->class_name);
    double suma = 0.0; int n = 0;
    for (const auto& seat : SeatsForClass(seriesId, save)) {
        if (seat.open || seat.occupant_id.empty()) continue;
        if (save.retired.count(seat.occupant_id)) continue;
        auto m = meta.find(seat.occupant_id);
        const double skill = (m == meta.end()) ? 0.85 : m->second.first;
        const int    birth = (m == meta.end()) ? 0    : m->second.second;
        suma += agedRatingFor(seat.occupant_id, skill, birth, save, worldYear, shift, sd->level);
        n++;
    }
    if (n <= 0) return out;                              // empty class: demands nothing from anyone
    out.mean = suma / (double)n;
    out.floor = out.mean - bal::CLASS_DEMAND_MARGIN;
    out.n = n;
    out.resolved = true;
    return out;
}

// EXPIRATIONS. Its OWN step, not a side effect of ApplyRolloverSeatChanges: there,
// `worldYear` already meant something else (the year used to distribute already-
// vacant seats by merit), and sneaking expiration in there would change the NUMBER of seats that function fills --
// exactly what its contract promises not to touch ("it only changes WHO ends up where"). Two old tests
// caught this, rightly so.
//
// Frees every seat whose contract expired when `worldYear` closes. Returns how many. Pure over
// the data tables; mutates ONLY save.seat_occupant and save.seat_contract_start. Deterministic.
// WHO RENEWS IS DECIDED BY PERFORMANCE, not a dice roll. The earlier work left this written down as a debt
// of its own: "WHO renews and who doesn't is a decision for later." A team renews whoever still performs at the
// level their category demands (DemandForClass), and lets go of whoever no longer does. AGE decides nothing here: what
// decides is the aging curve, which is what lowers their rating until it crosses the bar.
//
// `salientesOut` collects the ones who don't renew, with the seat and tier they're leaving. They are NOT
// retired: they're FREE AGENTS, and placing them lower is CascadeDemoteFreeAgents' job. Without that
// second half, the world loses them -- measured Aug 3: 155 drivers vanished over three seasons.
// ~~~29/08 — THE GRID IS REMEMBERED, NOT RECOMPUTED. (user decision, 29/08)
//
// The measured defect: `DerivedSeatOccupants` is static (it does not look at the save), but the LIST it comes from
//  moves every winter -- rookies enter, generated ones are pruned, the cast changes -- and that SHIFTS everyone
//  behind. Result over five production winters: **142-254 slots change occupant each
//  year and the market declares 3-5**. 98% of «who drives which car» was decided by a recomputation, not a
//  decision. That is why promotions came out at ZERO (when the ladder arrives, the slot already has an owner) and why
//  318 slots vacated by retirement produced 13 signings.
//
// This MATERIALIZES the occupancy: it writes into the save who is in each live slot that has no entry yet.
// From then on the derivation stops ruling and every movement has to go through the market, which is what
// makes a name mean something from one season to the next.
//
// It does NOT touch the slots that already have an entry (including the kSeatOpen sentinel: a declared gap remains
//  a gap), nor the era (it has no market), nor the player's -- those are resolved by their contract.
// Idempotent: running it twice changes nothing. Returns how many slots it has fixed.
int MaterializeSeatOccupancy(SaveData& save, const std::set<std::string>& delJugador) {
    int fijadas = 0;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (size_t i = 0; i < seats.size(); ++i) {
            if (seats[i].open || seats[i].occupant_id.empty()) continue;
            const std::string key = SeatKey(sd.id, (int)i);
            if (delJugador.count(key)) continue;
            if (save.seat_occupant.count(key)) continue;              // already fixed (or declared open)
            if (save.seat_occupant.count(SeatKeyLegacy(sd.id, (int)i))) continue;
            save.seat_occupant[key] = seats[i].occupant_id;
            fijadas++;
        }
    }
    return fijadas;
}

int ExpireSeatContracts(SaveData& save, int worldYear, std::vector<FreeAgent>* salientesOut,
                        const std::set<std::string>& delJugador) {
    if (worldYear <= 0) return 0;
    int vencidos = 0;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;                 // period series don't enter the market
        std::vector<Seat> seats = SeatsForClass(sd.id, save);
        // THIS category's bar, computed ONCE with the board as it stands at the start. If it were
        // recalculated per seat, the first one to leave would lower the average and save the next one: the order
        // of traversal would decide who stays, which is exactly what can't be allowed to happen.
        const ClassDemand exigencia = DemandForClass(sd.id, save, worldYear);
        const std::map<std::string, std::pair<double,int>> meta = OccupantMeta(sd.id, sd.level);
        const int shift = ClassAgeShift(sd.class_name);
        // the two scales of THIS series, measured once and with the board as it is at the start --
        // the same as the bar, and for the same reason: if they were re-measured per seat, the first one to leave
        // would move the next one's scale and the traversal order would decide who stays.
        const FormScale escala = measureFormScale(sd.id, sd.level, save, worldYear);
        for (size_t i = 0; i < seats.size(); ++i) {
            if (seats[i].occupant_id.empty()) continue;
            const std::string key = SeatKey(sd.id, (int)i);
            if (delJugador.count(key)) continue;             // your seat is resolved by YOUR contract, not this path
            if (!SeatContractExpires(save, key, seats[i].occupant_id, worldYear)) continue;
            // RENEWAL. A contract expiring doesn't mean the driver leaves: in
            // real-world racing, renewing is the norm and moving on is the exception.
            auto m = meta.find(seats[i].occupant_id);
            const double skill = (m == meta.end()) ? 0.85 : m->second.first;
            const int    birth = (m == meta.end()) ? 0    : m->second.second;
            // — AND HERE IS WHERE THE RESULTS COUNT. `agedRatingFor` is what PROMISES (aged authored
            // capacity); `effectiveRating` sets against it what they have DONE. Without this term the only
            // way of losing the seat was having a birthday, and that is why after twenty seasons there were 402 drivers
            // seated above their level: the world aged around them and nobody ever asked them
            // about their results.
            const double capacidad = agedRatingFor(seats[i].occupant_id, skill, birth, save, worldYear, shift, sd.level);
            // ~~~07/09 (D2b, Josh's decision): THE SEAT IS JUDGED BY THE RESULT. With D2a the capacity
            //  no longer drops for performing badly (the card grows along the curve and that is it), so the only way of
            //  going down that remains is results -- and here they came in bounded to +-70, a cap designed
            //  for when the capacity ALSO fell. No cap: whoever has been seasons below what
            //  they promise crosses the bar and goes down; whoever performs above stays even if their card says less.
            //  Confidence by races run stays in: a bad winter is not a finished race.
            const double suyo  = effectiveRating(seats[i].occupant_id, capacidad, save, escala, /*bounded=*/false);
            // With no resolvable bar (empty class) there's no reason to let anyone go.
            if (!exigencia.resolved || suyo >= exigencia.floor) {
                NoteSeatContractStart(save, key, worldYear);   // stays on, and their contract starts fresh
                continue;
            }
            if (salientesOut) {
                FreeAgent fa;
                fa.driver_id = seats[i].occupant_id;
                fa.from_seat = key;
                fa.from_series = sd.id;
                fa.from_tier = (sd.level >= 1 && sd.level <= 7) ? sd.level : 1;
                { int by = birth; auto b2 = save.birth_year.find(seats[i].occupant_id);
                  if (b2 != save.birth_year.end() && b2->second) by = b2->second;
                  if (by == 0) by = BirthYearFor(seats[i].occupant_id);
                  fa.age = DriverAge(by, worldYear); }
                fa.rating    = suyo;
                salientesOut->push_back(fa);
                RejPromo(fa.from_tier, "DESC-salen", 8);   // diag: how many give up the seat because of the bar
            }
            save.seat_occupant[key] = kSeatOpen;
            // The contract is over: its start year stops meaning anything. Clearing it also prevents
            // the map from growing without bound season after season.
            save.seat_contract_start.erase(key);
            vencidos++;
        }
    }
    return vencidos;
}

// A DRIVER'S DISCIPLINE AND A CATEGORY'S (see career_seats.h).
namespace {

int AffinityIndex(const std::string& fam) {
    if (fam == "touring")   return 0;
    if (fam == "openwheel") return 1;
    if (fam == "gt")        return 2;
    if (fam == "prototype") return 3;
    if (fam == "oval")      return 4;
    if (fam == "historic")  return 5;
    return -1;                              // no data: no door gets closed on them
}

// A SERIES' discipline: whichever the majority of its drivers have. It's derived from the
// authored data of its drivers instead of hand-classifying class names, so a new series inherits it
// automatically as soon as it has a grid, with no code or file changes needed.
std::map<std::string, int>* g_afinSerie = nullptr;
std::map<std::string, int>* g_afinClase = nullptr;   // 16/09: the same table, indexed by CLASS
long long g_afinFirma = -1;

// ~~~16/09 — THE TWO TABLES ARE BUILT IN THE SAME PASS, and it is not a decorative optimization.
//  `ClassAffinity` walked ALL of `AllSeries()` on each call, and that was fine while nobody called it
//  from a loop. On 16/09 (card 13) it came to live inside `DriverAffinity`, which is the guard of the
//  hottest loop of the engine: the cascade looks at the ~1,500 occupants of the world for each open slot, and in two
//  passes. Turning the card on turned a `find` into a sweep of 118 series PER CANDIDATE. With the daily
//  season data (more cast rows, more open slots) the suite stopped finishing: `test_renewal_rollover`
//  ate minutes without printing anything. With the per-class table it is a `find` again.
void ConstruirAfinidades() {
    const long long firma = (long long)AllSeries().size() * 1000003LL
                          + (long long)FieldRowCount() * 131LL
                          + (long long)PinnedRosterCount() * 17LL
                          + (long long)AuthoredProfileCount();
    if (g_afinSerie && g_afinClase && g_afinFirma == firma) return;
    delete g_afinSerie;
    delete g_afinClase;
    g_afinSerie = new std::map<std::string, int>();
    g_afinClase = new std::map<std::string, int>();
    g_afinFirma = firma;
    for (const auto& sd : AllSeries()) {
        int votos[bal::AFFINITY_COUNT] = {0,0,0,0,0,0};
        auto vota = [&](const std::string& nombre) {
            const AuthoredProfile* ap = AuthoredProfileFor(nombre);
            if (!ap) return;
            const int k = AffinityIndex(ap->affinity);
            if (k >= 0) votos[k]++;
        };
        for (const auto& pd : PinnedRosterForSeries(sd.id)) vota(pd.name);
        for (const auto& f  : FieldForSeries(sd.id))        vota(f.name);
        int mejor = -1, maxv = 0;
        for (int k = 0; k < bal::AFFINITY_COUNT; ++k) if (votos[k] > maxv) { maxv = votos[k]; mejor = k; }
        if (mejor >= 0) {
            (*g_afinSerie)[sd.id] = mejor;
            // `emplace` does NOT overwrite: the FIRST series with a vote wins in `AllSeries()` order, which is
            //  exactly who the previous sweep asked. The table says the same thing, not something similar.
            g_afinClase->emplace(sd.class_name, mejor);
        }
    }
}

int SeriesAffinity(const std::string& seriesId) {
    ConstruirAfinidades();
    auto it = g_afinSerie->find(seriesId);
    return it == g_afinSerie->end() ? -1 : it->second;
}

// ~~~08/09: the affinity of a CLASS = that of the first live series that runs it (all the series of a
//  class vote with the same grid, so it does not matter which). -1 if the class is not there or has no vote.
int ClassAffinity(const std::string& className) {
    if (className.empty()) return -1;
    ConstruirAfinidades();
    auto it = g_afinClase->find(className);
    return it == g_afinClase->end() ? -1 : it->second;
}
} // anon namespace

// ~~~16/09 (card 13): the MEASUREMENT hook. See career_seats.h. <0 = production.
int g_afinOrigenMedicion = -1;
bool AfinidadDeOrigenActiva() {
    return g_afinOrigenMedicion >= 0 ? g_afinOrigenMedicion != 0 : bal::POOL_ORIGIN_AFFINITY;
}

int DriverAffinity(const std::string& driverId, const std::string& driverName,
                   const std::string& seriesId) {
    // 1) what the data says about THEM. It's what keeps a GT driver a GT driver even while
    //    racing something else for a year.
    if (!driverName.empty()) {
        const AuthoredProfile* ap = AuthoredProfileFor(driverName);
        if (ap) { const int k = AffinityIndex(ap->affinity); if (k >= 0) return k; }
    }
    if (!driverId.empty()) {
        const PoolDriver* pd = DriverById(driverId);
        if (pd && !pd->name.empty()) {
            const AuthoredProfile* ap = AuthoredProfileFor(pd->name);
            if (ap) { const int k = AffinityIndex(ap->affinity); if (k >= 0) return k; }
        }
        // 1b) ~~~08/09 (Josh's decision via C2, E7): THE CURATED CATEGORY OF ORIGIN. If the data says
        //     which class this person is from, their affinity is that of that class -- ahead of the series in which
        //     they have landed, which is exactly what was deduced before and no longer.
        // MEASURED 08/09 with the curated file, three seeds: with this on, the generated aces that
        //  reach the top class fall from 23/19/19 to 17/14/14 and level 7 loses 30-80 points, because the class
        //  of origin BOUNDS the movements of 1,488 people and thins the upward flow; only with the
        //  settled one (below) does the ladder come out monotonic in the three seeds.
        // TURNED ON by Josh's decision on 16/09 (card 13): `bal::POOL_ORIGIN_AFFINITY`. The test
        //  bench turns it off with `career_test.exe --afin-origen 0` to measure again against the world from
        //  before; production does not read the hook.
        if (AfinidadDeOrigenActiva())
        if (const PoolOrigin* o = PoolOriginFor(driverId)) {
            const int k = ClassAffinity(o->home_class);
            if (k >= 0) return k;
        }
    }
    // 2) And if there's no data, the series they're racing in. This is where the COHORT and GENERATED ROOKIES fall, and
    //    that's fine: a young driver hasn't specialized yet. Their discipline is wherever
    //    they are right now, which is why they can cross worlds while moving up -- which is exactly
    //    what real people do coming out of karting. Once they've spent years in one, it'll be theirs.
    return seriesId.empty() ? -1 : SeriesAffinity(seriesId);
}

bool AffinityAllows(int desde, int hacia) {
    // With no data on either side, nothing is forbidden: affinity BOUNDS movement, it doesn't
    // invent it, and a series with no authored grid can't shut the door on anyone.
    if (desde < 0 || hacia < 0) return true;
    if (desde >= bal::AFFINITY_COUNT || hacia >= bal::AFFINITY_COUNT) return true;
    return bal::AFFINITY_OK[desde][hacia] != 0;
}

// ~~~16/09 — WE TRIED MAKING DISCIPLINE NOT VETO A PROMOTION, AND IT CAME OUT WORSE. DO NOT RETRY WITHOUT READING THIS.
//
// The starting point was good: with the origin affinity on (card 13), the aces that reach the
// top category fall from 8.7 to 5.7 on average over six seeds, and those that do not reach it do NOT stay one rung
// below --level 6 does not move-- but at the very bottom. It looked like the ace was missing the DOOR, so
// it was implemented that discipline would rule on STAYING and on LATERAL moves but never veto a level
// promotion (and at most would break a tie between two that are worth the same).
//
// Measured on the SAME six seeds. A = affinity off · B = on, what there is today · C = on without
// vetoing the promotion:
//
//                          A       B       C
//   aces to the top        8,7     5,7     3,7     <- C is WORSE than B, not better
//   in foreign discipline 36,0     3,8     7,7
//   empty seats          21,8    25,2    31,0
//   moved                 416     426     479
//   aces in levels 1-3      7      20      44     (sum of the six seeds)
//   aces in level 7        52      34      22
//
// THE MEASURED CAUSE IS NOT DISCIPLINE: IT IS THE CONGESTION OF THE INTERMEDIATE RUNGS. Opening the door does not
// open the way for the ace, it FILLS it with people. The cascade gives each slot to the best candidate in the whole world, so
// without the veto ALL the intermediate steps --not only the top one-- receive candidates from all
// disciplines. Movements go up 13% in the six seeds, the rungs the ace needs as a springboard
// change hands more often and, since they rise at most one level per season, in twenty they do not arrive. That is why the
// aces are not only missing at the top (52 -> 22) but pile up at the bottom (7 -> 44).
//
// If someone comes back for the aces, the lever is elsewhere: the maximum jump per season
// (`bal::CASCADE_PROMOTE_MAX_STEPS`) or giving priority to the high ceiling over the already settled veteran.
// Discipline, no.
//
// DECIDED by Josh on 16/09: B is published, that is, the veto stays exactly as it is a few lines below, in the
// three places where the cascade consults it. The experiment and its measurement hook were retired so as not to leave
// dormant code.

// THE PLAYER'S SEATS, by KEY. The cascade can't touch them: not aging, not a
// demotion, not a promotion, not a rookie. And they have to be looked up by key rather than occupant id, because the
// seat model does NOT know the player is there -- that seat keeps deriving the driver from whatever data
// used to occupy it. Without this, measured Aug 4, attrition was taking the player's seat away and giving it to a
// rookie while the player still had a valid contract.
// is the offer still standing? See career_seats.h.
// THE DAY THE PERIOD CLOSES that the sealed window belongs to. See career_seats.h.
// Returns an ABSOLUTE day. It's the hard cap: no offer goes past it, whatever its own deadline is.
static int OfferPeriodEndAbs(const SaveData& save, const CareerCalendar& cc, int firstYear) {
    const int fy = firstYear > 0 ? firstYear : kStartYear;
    if (save.window_year == fy) {
        // YEAR 1: the period opens on January 1st and CLOSES ON THE DAY OF YOUR FIRST RACE. A deliberate design
        // rule, because still signing drivers once the season has started feels wrong. It looks for the
        // earliest round among your entries that year; with no entries yet, the period closes
        // when its own decision week ends (no date gets invented out of nowhere).
        int primera = 0;
        for (const auto& e : cc.enrollments) {
            for (const auto& rr : CalendarForSeries(e.series_id))
                if (rr.doy > 0 && (primera == 0 || rr.doy < primera)) primera = rr.doy;
        }
        if (primera > 0) return career::dateutil::AbsDayFromYearDoy(fy, primera);
        return career::dateutil::AbsDayFromYearDoy(fy, market::MarketY1DecisionDoy(fy)) +
               bal::MK_DECISION_WEEK_DAYS;
    }
    // NORMAL YEAR: the period runs from September to the end of that year. The next day the clock is already in
    // season (MarketPhaseFor returns PADDOCK from January 1st), and "once the window closes there are no
    // more offers". The cap is therefore December 31st of the year it opened.
    const int anioVentana = save.window_year - 1;
    return career::dateutil::AbsDayFromYearDoy(anioVentana, career::dateutil::DaysInYear(anioVentana)) + 1;
}

int WindowOfferDeathAbs(const SaveData& save, const CareerCalendar& cc, int year, int doy, int firstYear) {
    if (save.window_year <= 0) return 0;          // with no sealed window there's no offer to hold
    // THIS window's decision week, derived the same way as in MarketCalendarTick and InDrought.
    const int fy = firstYear > 0 ? firstYear : kStartYear;
    int decYear, decDoy;
    if (save.window_year == fy) { decYear = fy;                   decDoy = market::MarketY1DecisionDoy(fy); }
    else                        { decYear = save.window_year - 1; decDoy = market::MarketDecisionDoy(decYear); }
    const int abreDecision = career::dateutil::AbsDayFromYearDoy(decYear, decDoy);
    const int hoy = career::dateutil::AbsDayFromYearDoy(year, doy);
    if (hoy < abreDecision) return 0;             // they haven't offered it to you yet: there's nothing to hold

    // the offer's own deadline is ITS OWN, but it's CAPPED by the period's close. The two limits coexist: the
    // first one to arrive wins. Without the cap, an overly generous deadline would leave offers alive in March,
    // which is exactly what got reported; without the deadline, within the period an offer would last as long as the
    // period itself, and deciding would stop being deciding.
    const int porPlazo  = abreDecision + bal::MK_OFFER_TTL_DAYS;
    const int porCierre = OfferPeriodEndAbs(save, cc, fy);
    const int muere = porPlazo < porCierre ? porPlazo : porCierre;
    return muere;
}
bool WindowOfferStillOpen(const SaveData& save, const CareerCalendar& cc, int year, int doy, int firstYear) {
    const int muere = WindowOfferDeathAbs(save, cc, year, doy, firstYear);
    return muere > 0 && career::dateutil::AbsDayFromYearDoy(year, doy) < muere;
}

// THE DROUGHT. See career_seats.h.
bool InDrought(const SaveData& save, int year, int doy, int firstYear) {
    if (save.window_year <= 0) return false;              // no sealed window: nothing that could have been lost
    // has THIS window's decision week already passed? Same derivation as the pulse (MarketCalendarTick): the
    // year 1 one is the compressed February one; for any other target season, it's the one in December of the
    // year BEFORE it. Derived, not stored, so it can never drift out of sync with the window.
    const int fy = firstYear > 0 ? firstYear : kStartYear;
    int decYear, decDoy;
    if (save.window_year == fy) { decYear = fy;                 decDoy = market::MarketY1DecisionDoy(fy); }
    else                        { decYear = save.window_year - 1; decDoy = market::MarketDecisionDoy(decYear); }
    if (career::dateutil::AbsDayFromYearDoy(year, doy) <
        career::dateutil::AbsDayFromYearDoy(decYear, decDoy)) return false;   // you haven't been judged yet

    // it looks at what you HAVE, not what you lost: an offered-but-unsigned seat still counts as an out, and
    // telling you you're out on the street while there's a contract on the table would be a lie.
    for (const auto& w : save.window_seats)
        if (w.state == WSEAT_OFFERED) return false;
    for (const auto& c : save.contracts) {
        if (!c.active) continue;
        if (c.signed_year + c.seasons_total - 1 >= save.window_year) return false;   // covers you for the target year
    }
    return true;
}

// 19/09: the window pool (defined further down, next to the winter fill they come from).
namespace {
std::set<std::string> OcupadosAhora(const SaveData& save);
std::string PozoParaPlazaDeVentana(const SaveData& save, const SeriesDef& sd,
                                   const std::set<std::string>& ocupados,
                                   const std::set<std::string>& usadosYa, int worldYear);
RosterDriver RosterDelPozo(const std::string& id);
void EntraDelPozo(SaveData& save, const std::string& key, const std::string& id, int nivel, int worldYear);
}   // anon

// 23/09 (, «One driver, one seat» card from the Tester Hub) — THE WINNER OF A SLOT SITS IN IT.
//
// The decision week named whoever took each slot you lost (winner_id/winner_name) and it stayed there: the
// nobody wrote `seat_occupant`, and in January the previous incumbent was still there with the contract renewed by the
// winter. Measured with the double (test profile, Quenby Rav327): 8 slots «won» by cohort people and
// none seated on 3 January. Only the offers that expired unsigned seated anyone, and on top of that in the
// team's FIRST slot (whether or not it was in play), without leaving their other slots to the winner and without anybody
// looking for a place for the incumbent who left.
//
// Now there is ONE path to seat a window winner, and it is used by the two places that distribute:
//  · winter (`SeatWindowWinnersForNewSeason`, in RolloverYearReport BEFORE the renewals), for the
//    autumn window: it seats those of the decision week and closes the offers you did not sign;
//  · the closing by date (`CloseExpiredWindowOffers`), for the year-1 window, which has no winter behind it.
// The winner leaves the slots they had (the gaps are inherited by the distribution afterwards, as in ), the incumbent
// they displace does not vanish (enters the cascade, or stays as a free agent if there is no winter in between) and
// the `seated` mark prevents seating them twice even if the world moves them later.
namespace {
// The slot the window put IN PLAY for (series, team): yours if it is your slot; if not, the one that is free or
// the one whose contract expires when the window year closes. `TeammateSeatIndex(..., -1)` returned the FIRST of the
// team, which in a two-car team could be the incumbent's with a live contract.
int IndiceEnJuegoDeVentana(const SaveData& save, const WindowSeat& w) {
    for (const auto& c : save.contracts)
        if (c.active && c.series_id == w.series_id && c.team_id == w.team_label && c.seat_idx >= 0)
            return c.seat_idx;
    const std::set<std::string> mios = PlayerSeatKeys(save);
    const std::vector<Seat> seats = SeatsForClass(w.series_id, save);
    const int ultimoAnio = save.window_year - 1;
    int primero = -1, abierto = -1, vence = -1;
    for (size_t i = 0; i < seats.size(); i++) {
        if (seats[i].team_label != w.team_label) continue;
        const std::string key = SeatKey(w.series_id, (int)i);
        if (mios.count(key)) continue;
        if (primero < 0) primero = (int)i;
        if (seats[i].open) { if (abierto < 0) abierto = (int)i; continue; }
        if (vence < 0 && !seats[i].occupant_id.empty() &&
            SeatContractExpires(save, key, seats[i].occupant_id, ultimoAnio))
            vence = (int)i;
    }
    return abierto >= 0 ? abierto : (vence >= 0 ? vence : primero);
}

// The incumbent who loses their slot: to winter like any free agent (same rating calculation as
// ExpireSeatContracts), or, with no winter in between, to the queue of free agents the fill draws from.
void TitularDesplazado(SaveData& save, const SeriesDef& sd, const std::string& key, const std::string& titular,
                       int worldYear, std::vector<FreeAgent>* salientesOut) {
    for (const auto& s2 : AllSeries()) {                      // does he have another slot left? then he comes out of nothing
        if (!EsMercadoVivo(s2)) continue;
        const std::vector<Seat> st = SeatsForClass(s2.id, save);
        for (size_t i = 0; i < st.size(); i++)
            if (!st[i].open && st[i].occupant_id == titular && SeatKey(s2.id, (int)i) != key) return;
    }
    if (salientesOut) {
        const std::map<std::string, std::pair<double,int>> meta = OccupantMeta(sd.id, sd.level);
        const auto m = meta.find(titular);
        const double skill = (m == meta.end()) ? 0.85 : m->second.first;
        int birth = (m == meta.end()) ? 0 : m->second.second;
        { const auto b2 = save.birth_year.find(titular);
          if (b2 != save.birth_year.end() && b2->second) birth = b2->second;
          if (birth == 0) birth = BirthYearFor(titular); }
        const FormScale escala = measureFormScale(sd.id, sd.level, save, worldYear);
        const double capacidad = agedRatingFor(titular, skill, birth, save, worldYear,
                                               ClassAgeShift(sd.class_name), sd.level);
        FreeAgent fa;
        fa.driver_id = titular; fa.from_seat = key; fa.from_series = sd.id;
        fa.from_tier = (sd.level >= 1 && sd.level <= 7) ? sd.level : 1;
        fa.age = DriverAge(birth, worldYear);
        fa.rating = effectiveRating(titular, capacidad, save, escala, /*bounded=*/false);
        salientesOut->push_back(fa);
    } else {
        int& idle = save.seasons_idle[titular];
        if (idle < 1) idle = 1;
        save.idle_from_series[titular] = sd.id;
    }
}

bool SientaGanadorDeVentana(SaveData& save, WindowSeat& w, int worldYear, std::vector<FreeAgent>* salientesOut) {
    if (w.winner_id.empty() || w.seated) return false;
    if (save.retired.count(w.winner_id)) { w.seated = 1; return false; }   // left before debuting it
    const SeriesDef* sd = SeriesById(w.series_id);
    if (!sd) return false;
    const int idx = IndiceEnJuegoDeVentana(save, w);
    if (idx < 0) return false;
    const std::string key = SeatKey(w.series_id, idx);
    const std::set<std::string> mios = PlayerSeatKeys(save);
    const std::vector<Seat> seats = SeatsForClass(w.series_id, save);
    const std::string titular = (idx < (int)seats.size() && !seats[idx].open) ? seats[idx].occupant_id
                                                                              : std::string();
    if (titular == w.winner_id) { w.seated = 1; return false; }
    // the winner leaves ALL their slots (like the aspirant of ): the gaps are inherited by the distribution afterwards
    for (const auto& s2 : AllSeries()) {
        if (!EsMercadoVivo(s2)) continue;
        const std::vector<Seat> st = SeatsForClass(s2.id, save);
        for (size_t i = 0; i < st.size(); i++) {
            if (st[i].open || st[i].occupant_id != w.winner_id) continue;
            const std::string k2 = SeatKey(s2.id, (int)i);
            if (k2 == key || mios.count(k2)) continue;
            save.seat_occupant[k2] = kSeatOpen;
            save.seat_contract_start.erase(k2);
        }
    }
    save.seat_occupant[key] = w.winner_id;
    NoteSeatContractStart(save, key, save.window_year);      // NEW contract for the season being signed for
    if (!titular.empty() && !mios.count(key)) TitularDesplazado(save, *sd, key, titular, worldYear, salientesOut);
    bool deLaCohorte = false;
    for (const auto& rd : RosterForTier(sd->level)) if (rd.id == w.winner_id) { deLaCohorte = true; break; }
    if (deLaCohorte) { save.seasons_idle.erase(w.winner_id); save.idle_from_series.erase(w.winner_id); }
    else EntraDelPozo(save, key, w.winner_id, sd->level, save.window_year);
    w.seated = 1;
    return true;
}

// The offers you did not sign, closed: the next of the shortlist (or the pool) takes them and sits.
int CerrarOfertasSinFirmar(SaveData& save, int year, double playerMoneyNorm, int worldYear,
                           std::vector<FreeAgent>* salientesOut) {
    // 19/09 (tester: «Lucia Vega» in 8 seats at once, in 2 series): `staying` was computed ONCE and
    // nobody added the one who had just won to it, so the cohort of the next slot was still the same,
    // TopRivalInRank returned the same person and seated them in ALL of them. Now it is a growing copy: whoever
    // takes a slot leaves the shortlist of the others. Those named in December by the decision week
    // of THIS window also come in: they already won their slot, and «so-and-so took it» twice would be the same lie.
    std::set<std::string> staying = StayingDrivers(save, year);
    for (const auto& w : save.window_seats)
        if (w.state == WSEAT_REJECTED && !w.winner_id.empty()) staying.insert(w.winner_id);
    std::set<std::string> ocupados; bool ocupadosListos = false;   // 19/09: only if it is necessary to go to the pool
    int cerradas = 0;
    for (auto& w : save.window_seats) {
        if (w.state != WSEAT_OFFERED) continue;
        // the one you DID sign doesn't get redistributed: the contract overrides the sealed key. Without this guard, the
        // world would take away the seat you just signed for, which is worse than leaving a seat hanging.
        bool firmada = false;
        for (const auto& c : save.contracts)
            if (c.active && c.series_id == w.series_id && c.team_id == w.team_label) { firmada = true; break; }
        if (firmada) continue;

        const SeriesDef* sd = SeriesById(w.series_id);
        if (!sd) continue;
        std::string teamCountry;
        for (const auto& s : SeatsForClass(w.series_id, save)) {
            if (s.team_label != w.team_label) continue;
            if (!s.occupant_id.empty())
                for (const auto& rd : RosterForTier(sd->level))
                    if (rd.id == s.occupant_id) { teamCountry = rd.country; break; }
            break;
        }
        // whoever gets it comes from THE SAME SHORTLIST that was competing with you for it. It isn't a new driver invented at
        // close time: it's whoever was behind you, which is why the news reads ("So-and-so got it") instead of
        // a name appearing out of nowhere.
        const std::vector<RosterDriver> cohorte = CohortMinusStaying(RosterForTier(sd->level), staying);
        const std::vector<SeatCandidate> rank = RankSeatCandidates(
            save, sd->class_name, w.team_label, teamCountry, playerMoneyNorm, year, cohorte, w.series_id);
        const SeatCandidate* rival = TopRivalInRank(rank);
        std::string ganador = rival ? rival->id : std::string();
        std::string nombre  = rival ? rival->name : std::string();
        // 19/09 (Josh's decision, «real drivers from the pool»): with the cohort exhausted the slot was left WITH
        // nobody (measured in the simulator with the game's load: 9 of 41 slots lost). Now it signs the
        // team's real pool driver, through the same chain as the winter fill; only if there is nobody there either
        // does it stay as it was.
        if (ganador.empty()) {
            if (!ocupadosListos) { ocupados = OcupadosAhora(save); ocupadosListos = true; }
            ganador = PozoParaPlazaDeVentana(save, *sd, ocupados, staying, save.window_year);
            if (!ganador.empty()) nombre = RosterDelPozo(ganador).name;
        }

        w.state = WSEAT_REJECTED;                  // it stopped being yours, and with the same status as if you'd lost it
        if (!ganador.empty()) {
            w.winner_id = ganador; w.winner_name = nombre;
            // ...and the world finds out: the seat gets a real occupant, not just a label. Without this
            // the seat would still read as "open" for next year's cascade, and the driver would be nowhere.
            SientaGanadorDeVentana(save, w, worldYear, salientesOut);
            staying.insert(ganador);     // 19/09: one driver, one seat -- no longer enters the shortlist of the next one
            if (ocupadosListos) ocupados.insert(ganador);
        }
        cerradas++;
    }
    return cerradas;
}
}   // anon

// THE OFFERS YOU DIDN'T TAKE GO TO SOMEONE ELSE. See career_seats.h.
int CloseExpiredWindowOffers(SaveData& save, const CareerCalendar& cc, int year, int doy,
                             double playerMoneyNorm, int firstYear) {
    if (save.window_year <= 0) return 0;
    if (WindowOfferStillOpen(save, cc, year, doy, firstYear)) return 0;   // the period is still open
    const int worldYear = save.current_world_year != 0 ? save.current_world_year : year;
    // 23/09 (): the YEAR-1 window has no winter behind it to seat those of the decision week, so
    // its closing does it. The autumn ones are seated by winter (SeatWindowWinnersForNewSeason): seating them here,
    // with the market already sealed, would move seats mid-season.
    if (save.window_year == firstYear)
        for (auto& w : save.window_seats)
            if (w.state == WSEAT_REJECTED) SientaGanadorDeVentana(save, w, worldYear, nullptr);
    return CerrarOfertasSinFirmar(save, year, playerMoneyNorm, worldYear, nullptr);
}

// 23/09 (): see career_seats.h.
int SeatWindowWinnersForNewSeason(SaveData& save, int worldYear, double playerMoneyNorm,
                                  std::vector<FreeAgent>* salientesOut) {
    if (worldYear <= 0 || save.window_year != worldYear + 1) return 0;
    int sentados = 0;
    for (auto& w : save.window_seats)
        if (w.state == WSEAT_REJECTED && SientaGanadorDeVentana(save, w, worldYear, salientesOut)) sentados++;
    // what is still OFFERED unsigned when 31 December ends will no longer be signed: it is distributed here, with the
    // incumbent who leaves entering the SAME cascade as those who do not renew.
    std::set<int> yaSentadas;
    for (size_t i = 0; i < save.window_seats.size(); i++) if (save.window_seats[i].seated) yaSentadas.insert((int)i);
    CerrarOfertasSinFirmar(save, worldYear + 1, playerMoneyNorm, worldYear, salientesOut);
    for (size_t i = 0; i < save.window_seats.size(); i++)
        if (save.window_seats[i].seated && !yaSentadas.count((int)i)) sentados++;
    return sentados;
}

// 23/09 (): see career_seats.h.
int WindowSeatIndexInPlay(const SaveData& save, const WindowSeat& w) { return IndiceEnJuegoDeVentana(save, w); }

// THE LIVING-WORLD -> GRID BRIDGE. See career_seats.h.
std::map<std::string, std::string> SeatOccupantsByLivery(const SaveData& save, const std::string& seriesId) {
    std::map<std::string, std::string> out;
    if (seriesId.empty()) return out;
    // NOBODY ELSE DRIVES YOUR SEAT. The seat model does NOT know you're there -- that's stated in the
    // PlayerSeatKeys contract: "that seat keeps deriving the driver from whatever data used to occupy it". Without this
    // guard, the bridge would grab that phantom driver and put them IN YOUR OWN CAR, and with even more force
    // than before: what used to be a passive inconsistency in the model would become the grid that actually gets written.
    // And the consequence is the right one: if you keep that livery, whoever had it does NOT race. One car, one
    // driver. This matters for real because AMS2 silently draws participants at random if there are more vehicles than seats.
    const std::set<std::string> mios = PlayerSeatKeys(save);
    const std::vector<Seat> asientos = SeatsForClass(seriesId, save);
    for (size_t i = 0; i < asientos.size(); i++) {
        const Seat& s = asientos[i];
        if (s.livery_name.empty() || s.occupant_id.empty()) continue;   // empty seat or no livery: nothing to bind
        if (mios.count(SeatKey(seriesId, (int)i))) continue;            // it's YOURS: nobody else sits there
        out[s.livery_name] = s.occupant_id;
    }
    return out;
}

// — the derived occupants, without a game. See career_seats.h.
std::vector<std::string> DerivedSeatOccupants(const std::string& seriesId) {
    EnsureTailNames();
    std::vector<std::string> out;
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd || IsEraMemorySeries(*sd)) return out;   // an era memory does not take part in the living world
    if (IsEventOnlySeries(*sd)) return out;          // 01/10: event-only neither
    const int tier = (sd->level >= 1 && sd->level <= 7) ? sd->level : 1;
    const std::vector<std::string>  liv    = SeatLiveriesForSeries(seriesId);
    const std::vector<PinnedDriver> pinned = PinnedRosterForSeries(seriesId);
    const std::vector<FieldDriver>  field  = FieldForSeries(seriesId);
    const std::vector<RosterDriver> cast   = RosterForTier(tier);
    const size_t n = liv.empty() ? cast.size() : liv.size();
    out.reserve(n);
    // — NOBODY DRIVES TWO CARS IN THE SAME RACE, and if the data says so, the slot goes to a SUBSTITUTE
    // instead of staying empty. The census counts 17 cases (`event_dup_driver`: the data carries the same person
    // twice in a series). Simply discarding the second left the car out of the XML — and AMS2 does not leave a
    // gap, it puts a generic in. Measured: 15 cars in 7 classes.
    //
    // The rule lives HERE and not in each consumer so that the three views of the world say the same thing: the
    // seats, the bridge to the grid and the ELO seeding. If the seeding did not see the substitute, that car
    // would go back to racing with no rating — the two talent scales again.
    std::set<std::string> ya;
    int packSlot = 0;                       // ordinal of UNCOVERED slot (see DerivedSeatOccupant)
    for (size_t i = 0; i < n; ++i) {
        const bool cubierta = (i < pinned.size()) ||
                              ((int)i - (int)pinned.size() >= 0 && (int)i - (int)pinned.size() < (int)field.size()) ||
                              (tier <= bal::COHORT_HOME_MAX_TIER && i < cast.size());
        std::string id = DerivedSeatOccupant(seriesId, tier, (int)i, pinned, field, cast,
                                             cubierta ? -1 : packSlot);
        if (!cubierta) packSlot++;
        if (!ya.insert(id).second) {
            // The substitute for THIS seat may already be seated (its own slot used them). It is searched in
            // jumps of a whole grid, which is deterministic and always finds a gap: without this the
            // slot was left empty and the car disappeared from the XML — measured, 2 in Ember_Cup.
            id.clear();
            for (int k = 0; k < kTailSubstituteTries; ++k) {
                const std::string s = TailOccupantId(seriesId, (int)i + k * (int)n);
                if (ya.insert(s).second) { id = s; break; }
            }
        }
        out.push_back(id);   // empty only if not even three substitutes: DECLARED free slot (the census sees it)
    }

    // — THE CURATED BINDING RULES OVER THE POSITION, and is applied HERE, in the distribution.
    //
    // Until 27/08 this distribution was purely POSITIONAL: slot `i` got occupant `i`. For
    // a series with a pinned roster that already pairs well by construction —`SeatLiveriesForSeries` puts
    // the pinned liveries FIRST and in their order—, but for a FIELD series nobody looked at
    // `driver_liveries.psv`, which is exactly the file that says which car belongs to whom. Measured in year 1 with an
    // empty save (`[faseF-descruce]`): of 541 rows with a curated binding, only **70 (13%)** were in their
    // car; the final cross-match by SURNAME (`RepairDriverLiveryPairing`) left them at 103 (19%); and
    // **486 (90%) were reachable by PERMUTATION**, that is, the right livery was on the grid,
    // only on the wrong driver.
    //
    // Why a PERMUTATION and not a new distribution: swapping leaves the multiset of occupants
    // EXACTLY the same, so neither the ELO seeding, nor the census, nor the «nobody drives two
    // cars» rule, nor the pack slot ordinal notice. The only thing that changes is who goes in which car,
    // which is what has to be fixed. A new distribution would touch all five things at once, and that already cost
    // five reverted attempts on 09/08.
    //
    // And it is deterministic: it is traversed by index, the first repeated livery wins, and a gap that has already
    // been satisfied by curation is PINNED — nobody takes it away afterwards.
    if (!liv.empty() && DriverLiveryCount() > 0) {
        std::map<std::string, std::string> quiere;      // livery_name -> id of whoever has it curated
        for (const auto& fd : field) {
            const std::string lv = DriverLiveryFor(seriesId, fd.name);
            if (lv.empty()) continue;
            const std::string fid = FieldOccupantId(fd);
            if (!fid.empty()) quiere.emplace(lv, fid);   // emplace: the first wins
        }
        // 28/08 — AND THE PACK ONES, which were in the worst shape. They enter by ORDINAL of uncovered
        // slot (`PackDriverForClassSlot`) and have NO associated livery in any file: the
        // pack corpus only carries identity, country and which classes they race in. Their only clue is the
        // NAME carried by the livery label — which is now data, derived once and offline
        // by `tools/gen_driver_liveries_from_labels.py`, instead of being read hot when
        // writing the XML (rule 4).
        //
        // They go AFTER the field on purpose: `emplace` lets the first win, and the field is more
        // curated than a deduction by surname.
        for (int k = 0; ; ++k) {
            const PackDriver* pk = PackDriverForClassSlot(sd->class_name, k);
            if (!pk) break;
            const std::string lv = DriverLiveryFor(seriesId, pk->name);
            if (lv.empty()) continue;
            const std::string pid = PackDriverId(*pk);
            if (!pid.empty()) quiere.emplace(lv, pid);
        }
        // Shielding: a livery from the PINNED roster is not touched. Its binding lives in column 9 of
        //  `series_rosters.psv` and is authoritative; `driver_liveries.psv` describes the FIELD series.
        //  Checked on 27/08: the two files cover DISJOINT sets —655 and 950 bindings, zero
        //  (series,driver) in common— so today this clash does not happen. It is shielded because a future data
        //  edit could create it, and the symptom would be a pinned driver expelled from their own car.
        std::set<std::string> librePinneada;
        for (const auto& pd : pinned) if (!pd.livery_name.empty()) librePinneada.insert(pd.livery_name);
        if (!quiere.empty()) {
            std::map<std::string, size_t> donde;         // id -> slot they occupy now
            for (size_t i = 0; i < out.size(); ++i) if (!out[i].empty()) donde.emplace(out[i], i);
            std::vector<bool> fijo(out.size(), false);
            for (size_t i = 0; i < out.size() && i < liv.size(); ++i) {
                if (librePinneada.count(liv[i])) continue;   // the pinned one's livery: untouchable
                const auto q = quiere.find(liv[i]);
                if (q == quiere.end()) continue;
                if (out[i] == q->second) { fijo[i] = true; continue; }   // was already fine
                const auto d = donde.find(q->second);
                if (d == donde.end()) continue;          // does not race on this grid: nothing to permute
                const size_t j = d->second;
                if (j == i || j >= out.size() || fijo[j]) continue;   // pinned by curation: not touched
                if (j < liv.size() && librePinneada.count(liv[j])) continue;   // a pinned one is not taken out
                std::swap(out[i], out[j]);
                if (!out[i].empty()) donde[out[i]] = i;
                if (!out[j].empty()) donde[out[j]] = j;
                fijo[i] = true;
            }
        }
    }
    return out;
}

// — the same distribution, ORDERED. See career_seats.h.
std::vector<SeatRow> SeatRowsForSeries(const SaveData& save, const std::string& seriesId) {
    std::vector<SeatRow> out;
    if (seriesId.empty()) return out;
    const std::set<std::string> mios = PlayerSeatKeys(save);
    const std::vector<Seat> asientos = SeatsForClass(seriesId, save);
    // The distribution that the DATA says, to know in which cars the living world has moved someone (SeatRow::moved).
    const std::vector<std::string> derivados = DerivedSeatOccupants(seriesId);
    out.reserve(asientos.size());
    std::set<std::string> yaSentado;
    for (size_t i = 0; i < asientos.size(); i++) {
        const Seat& s = asientos[i];
        if (s.livery_name.empty() || s.occupant_id.empty()) continue;   // free or without a car: it is not a row
        if (mios.count(SeatKey(seriesId, (int)i))) continue;            // yours: nobody else sits there
        std::string ocupante = s.occupant_id;
        bool movido = (i >= derivados.size()) || (derivados[i] != ocupante);
        // The same rule of  that the derived list applies, here for the duplicates the GAME carries
        // (the market can sign someone who already had another seat in the same series).
        if (!yaSentado.insert(ocupante).second) {
            const std::string suplente = TailOccupantId(seriesId, (int)i);
            if (!yaSentado.insert(suplente).second) continue;   // not even the substitute: it is declared in the census
            ocupante = suplente;
            movido = true;
        }
        out.push_back(SeatRow{ s.livery_name, ocupante, movido });
    }
    return out;
}

// THE LIVERIES YOU OCCUPY in this series. It's the exact complement of the guard above: that one
// stops the bridge from putting a phantom driver in your car, and this one tells grid composition that livery
// already has an owner and to look for another. Empty = you don't have a seat here (the normal case for almost every series).
std::set<std::string> PlayerLiveriesForSeries(const SaveData& save, const std::string& seriesId) {
    std::set<std::string> out;
    if (seriesId.empty()) return out;
    const std::set<std::string> mios = PlayerSeatKeys(save);
    if (mios.empty()) return out;
    const std::vector<Seat> asientos = SeatsForClass(seriesId, save);
    for (size_t i = 0; i < asientos.size(); i++) {
        if (asientos[i].livery_name.empty()) continue;
        if (mios.count(SeatKey(seriesId, (int)i))) out.insert(asientos[i].livery_name);
    }
    return out;
}

// 18/08 — THE SINGLE DOOR: compose from the game with nothing that can be forgotten. See career_seats.h
// for the history of the three writers and the three arguments that were left out.
std::vector<GridDriver> ComposeSeasonGridForSave(const SaveData& save, const std::string& seriesId,
                                                 int gridSize, double playerForm, int calYear) {
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return std::vector<GridDriver>();
    // The six that before had to be remembered, here and from a single source.
    const std::vector<SeatRow>    asientos = SeatRowsForSeries(save, seriesId);
    const std::set<std::string>   misLibreas = PlayerLiveriesForSeries(save, seriesId);
    // 18/08 — THE WORLD YEAR, resolved by the SAME rule as the rest of the layer (WindowSeatPerRace,
    // career_contract.cpp, career_calendar.cpp): `save.current_world_year` rules and the calendar year is
    // the fallback. The game carries the curve and the birth, but the year does not live in it until the first
    // rollover, so without the fallback the first season would compose with `worldYear = 0` — that is, without
    // aging the clone — and T1's grid would not be T2's for a reason that is not the model's.
    const int worldYear = save.current_world_year != 0 ? save.current_world_year : calYear;
    return ComposeSeasonGrid(seriesId, sd->level, playerForm, gridSize, /*useHomage=*/false,
                             save.rival_form, /*exclude=*/nullptr, &save.driver_ratings,
                             ClassRoleForSeries(seriesId),
                             asientos.empty()   ? nullptr : &asientos,
                             misLibreas.empty() ? nullptr : &misLibreas,
                             &save.driver_races,
                             &save.driver_curve, &save.birth_year, worldYear);
}

// SEAT-ALLOCATION FOOTPRINT. See career_seats.h.
unsigned long SeatOccupancyStamp(const SaveData& save) {
    // The world is walked the SAME way the grid gets composed (series -> livery -> occupant), so
    // the footprint changes exactly when the XML would change, and not one time more. `seat_occupant` alone wouldn't
    // work: it fills in gradually and doesn't include what the model derives on its own.
    unsigned long h = 2166136261UL;
    auto mezcla = [&h](const std::string& s) {
        for (unsigned char c : s) { h ^= c; h *= 16777619UL; }
        h ^= 0x2Fu; h *= 16777619UL;              // separator: "ab|c" and "a|bc" can never collide
    };
    for (const auto& sd : AllSeries()) {
        if (IsEraMemorySeries(sd)) continue;      // a memory series is frozen: it doesn't enter the living allocation
        // it walks the RAW allocation (SeatsForClass), not SeatOccupantsByLivery: the latter omits your seats, and
        // that omission alone used to move the footprint. See the note below.
        for (const auto& a : SeatsForClass(sd.id, save)) {
            if (a.livery_name.empty() || a.occupant_id.empty()) continue;
            mezcla(sd.id); mezcla(a.livery_name); mezcla(a.occupant_id);
        }
    }
    // the OWNER of each class («the XML fingerprint includes owner») goes in a SEPARATE
    // fingerprint, `ClassDeploymentStamp`, and not here. The reason is that the plan is only known to whoever has the
    // calendar in front (the app, in the pregen), so mixing it in here would make any query made
    // BEFORE the first pregen —right after opening the app— compare an empty plan against a sealed one and shout
    // «restart AMS2» forever. A fingerprint that cannot be evaluated cannot assert a change.
    // DECISION (Aug 6) — YOUR OWN SEATS DON'T COUNT. They used to, and the reasoning was sound on paper: when
    // you sign, whoever had that livery stops racing, so the grid changes. But in practice that
    // triggered the "restart AMS2" warning on the VERY FIRST round of contracts in a whole playthrough, which is
    // exactly the worst moment to ask the player to close the game.
    //
    // And what's gained isn't worth it: signing only touches YOUR livery, and the worst case is racing with one fewer car
    // on track — an explicit design decision: "fine by me if it races one car short all season".
    // It also self-heals: the next pregen (every time you open the app) composes without your livery from then on.
    //
    // What DOES still count is the world moving seats around, which is when WHO races changes, not just
    // how many. That's when the warning earns its interruption.
    return h ? h : 1UL;                            // 0 is reserved for "never written"
}

bool GridsAreStale(const SaveData& save) {
    if (save.grids_stamp != SeatOccupancyStamp(save)) return true;
    // and if the class has changed OWNER, even if not a single seat has moved — 9 of
    // the 11 shared classes compose different people depending on the series. The 0 of `ClassDeploymentStamp` is «I don't
    // know» (there is no plan set yet in this process) and it CANNOT be read as «it has changed»: otherwise the
    // notice would fire right after opening the app, which is exactly the worst moment to ask for the game to be closed.
    const unsigned long ahora = ClassDeploymentStamp();
    return ahora != 0UL && save.grids_owner_stamp != 0UL && ahora != save.grids_owner_stamp;
}

// YOUR TEAMMATE. See career_seats.h.
int TeammateSeatIndex(const SaveData& save, const std::string& seriesId,
                      const std::string& teamLabel, int yourSeatIdx) {
    if (seriesId.empty() || teamLabel.empty()) return -1;
    const std::vector<Seat> seats = SeatsForClass(seriesId, save);
    for (size_t i = 0; i < seats.size(); i++) {
        if ((int)i == yourSeatIdx) continue;              // that's you
        if (seats[i].team_label == teamLabel) return (int)i;
    }
    return -1;                                            // single-car team: no teammate
}

std::set<std::string> PlayerSeatKeys(const SaveData& save) {
    std::set<std::string> out;
    for (const auto& c : save.contracts) {
        if (!c.active || c.series_id.empty() || c.seat_idx < 0) continue;
        out.insert(SeatKey(c.series_id, c.seat_idx));
    }
    return out;
}

// DEMOTION (see career_seats.h). Second half of non-renewal: whoever doesn't get
// renewed looks for a spot further down, and ONLY if they can't find one do they cease to exist.
int CascadeDemoteFreeAgents(SaveData& save, std::vector<FreeAgent> agentes, int worldYear,
                            std::vector<std::string>* retiradosOut,
                            const std::set<std::string>& delJugador,
                            std::vector<Demotion>* bajadasOut,
                            bool retirarSinSitio, std::vector<FreeAgent>* sinSitioOut) {
    if (agentes.empty()) return 0;
    // The best pick first. With an allocation ordered by series, a tier-7 driver could end up
    // with nowhere to go because a tier-3 driver already grabbed the only seat that would have worked for them.
    std::sort(agentes.begin(), agentes.end(), [](const FreeAgent& a, const FreeAgent& b) {
        if (a.rating != b.rating) return a.rating > b.rating;
        return a.driver_id < b.driver_id;                    // stable tiebreak -> total order
    });

    // Openings per tier with their class's bar already resolved (once per series: there are ~90 series versus
    // hundreds of agents, not the other way around).
    struct Hueco { std::string key; int tier; double floor; bool libre; int afin; int techoEdad; bool escuela; std::string serie; };
    std::vector<Hueco> huecos;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        bool hayAbierto = false;
        for (const auto& s : seats) if (s.open) { hayAbierto = true; break; }
        if (!hayAbierto) continue;                           // full class: we don't even compute its bar
        const ClassDemand ex = DemandForClass(sd.id, save, worldYear);
        for (size_t i = 0; i < seats.size(); ++i) {
            if (!seats[i].open) continue;
            if (delJugador.count(SeatKey(sd.id, (int)i))) continue;   // your seat doesn't enter the allocation
            Hueco h;
            h.key   = SeatKey(sd.id, (int)i);
            h.tier  = (sd.level >= 1 && sd.level <= 7) ? sd.level : 1;
            h.floor = ex.resolved ? ex.floor : 0.0;          // empty class -> demands nothing
            h.libre = true;
            h.afin  = SeriesAffinity(sd.id);
            h.techoEdad = ClassAgeCeiling(sd.class_name);
            h.escuela   = IsSchoolClass(sd.class_name);
            h.serie     = sd.id;
            huecos.push_back(h);
        }
    }

    // Whoever ALREADY has another seat isn't a free agent. Without this the cascade was handing out
    // seats to people who hadn't actually lost theirs: measured, Hamilton kept his F1 seat and ALSO
    // got given one in Formula Vee, ending up in two tiers at once -- exactly what the
    // closing criterion forbids. A contract expiring in one of his series doesn't put him out on the street.
    std::set<std::string> yaSentado;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;
        for (const auto& s : SeatsForClass(sd.id, save))
            if (!s.open && !s.occupant_id.empty()) yaSentado.insert(s.occupant_id);
    }

    int colocados = 0;
    for (const auto& fa : agentes) {
        if (yaSentado.count(fa.driver_id)) continue;   // he still has another seat: not on the market
        // He wants to stay as high up as possible, but he can't climb back up: below where he
        // was, and at most CASCADE_DEMOTE_MAX_STEPS rungs down. You drop from F1 to GT, not to karting.
        const int suelo = fa.from_tier - bal::CASCADE_DEMOTE_MAX_STEPS;
        int mejor = -1;
        // ~~~07/09 (D5, Josh's decision): IF YOU DID NOT GIVE THE LEVEL, YOU DO NOT STAY AT THE LEVEL. This loop
        //  admitted the very rung they leave from and the selector keeps the highest that clears -- that is
        //  that at levels 1-3, where some seat is always left over, whoever lost the slot for not giving the
        //  bar swapped it for ANOTHER at the same level and the demotion did not exist. Measured over twenty seasons:
        //  4 demotions in the lower half of the ladder versus 84 in the upper; at 5-7 they fell because there
        //  aren't enough seats. Now ONE step below is looked at first (down to `suelo`) and only if there is no
        //  room there is their own looked at. Level 1 has nothing below and stays as it was: from the base there is
        //  no dropping. Everyone who leaves through here lost the seat on performance (ExpireSeatContracts only
        //  releases whoever is below the bar) or because the market seated someone better ().
        for (int pasada = 0; pasada < 2 && mejor < 0; ++pasada)
        for (size_t k = 0; k < huecos.size(); ++k) {
            const Hueco& h = huecos[k];
            if (!h.libre) continue;
            if (h.tier > fa.from_tier || h.tier < suelo) continue;   // outside their demotion band
            if (pasada == 0 && h.tier >= fa.from_tier) continue;     // D5: first BELOW where they were
            if (fa.rating < h.floor) continue;                       // doesn't hold this level EITHER
            // And it has to be a world they can actually go to. A GT driver doesn't drop into a
            // junior single-seater -- observed in the log: "KEYSTONE CUP 2200 -> Lantern Cup 2487".
            if (!AffinityAllows(DriverAffinity(fa.driver_id, std::string(), fa.from_series), h.afin)) continue;
            // And the category has to accept someone their age. Without this the cascade was sending a
            // professional into KARTING: it showed up on the very first pass over a real save, "Bruno Almeida ->
            // Ivory Cup 6423". Nobody goes back to a feeder school at thirty-five, and the data to
            // know that already existed -- the class's age ceiling (docs/career/class_age_profile.psv), which
            // is 24 for a feeder school. It's the same table that decides when each driver retires.
            if (h.techoEdad > 0 && fa.age > 0 && fa.age > h.techoEdad) continue;
            // You ENTER a feeder school, you don't go back to one. The age guard above isn't enough: the ones falling
            // into karting were 19-year-old rookies, well within its ceiling of 24. A feeder school is the
            // ENTRY point into the sport, not somewhere to land when things go badly -- you leave it either
            // by moving up or by leaving the sport entirely.
            if (h.escuela) continue;
            // the highest one that clears the bar; ties -> lower key, so the allocation doesn't depend on the order
            // the list happened to be built in
            if (mejor < 0 || h.tier > huecos[mejor].tier ||
                (h.tier == huecos[mejor].tier && h.key < huecos[mejor].key)) mejor = (int)k;
        }
        if (mejor < 0) {
            RejPromo(fa.from_tier, retirarSinSitio ? "DESC-sin-hueco-final" : "DESC-sin-hueco-1a", 8);
            // Nobody wants them within two steps down. It's the only real exit in the world:
            // you don't leave for turning old, you leave because you no longer make the level in any category you
            // could drop to.
            //
            // ~~~29/08 — BUT NOT IN THE FIRST PASS. This step runs BEFORE the promotions, and it's the
            //  promotions that open slots BELOW -- exactly where a demoted driver lands. Retiring them
            //  here is closing the door before the ones they're due open up. Measured with the harness already
            //  fixed: 17 people per winter, and rising, were leaving the sport because of that.
            if (!retirarSinSitio) {
                if (sinSitioOut) sinSitioOut->push_back(fa);
                continue;
            }
            // ~~~29/08 — AND EVEN SO THEY DON'T LEAVE YET. They have `seasons_idle` winters in a row without a seat;
            //  only when it exceeds `FREE_AGENT_MAX_IDLE` do they leave the sport. One bad winter is not a finished
            //  career, and the counter is also what orders the queue: whoever has just become free comes in
            //  before whoever has been out for two years.
            const int idle = ++save.seasons_idle[fa.driver_id];
            save.idle_from_series[fa.driver_id] = fa.from_series;
            if (idle <= bal::FREE_AGENT_MAX_IDLE) continue;   // still available
            if (FueraDelMercadoDePilotos(fa.driver_id)) continue;   // 23/09: not on the market -> not retired
            save.seasons_idle.erase(fa.driver_id);
            save.idle_from_series.erase(fa.driver_id);
            save.retired.insert(fa.driver_id);
            if (retiradosOut) retiradosOut->push_back(fa.driver_id);
            continue;
        }
        // Demotion gets RECORDED here, which is where it's actually known. The chronicle used to infer it by comparing
        // snapshots of the world, and it reported demotions that never happened: a driver with seats in several tiers who
        // loses the top one would "show up" at the bottom, where he already was. It surfaced on a real save, "Bruno Almeida
        // -> Ivory Cup 6423". Cross-checking with the source data would never have allowed that mistake.
        if (bajadasOut && huecos[mejor].tier < fa.from_tier) {
            Demotion d;
            d.driver_id = fa.driver_id; d.from_series = fa.from_series; d.to_series = huecos[mejor].serie;
            d.from_tier = fa.from_tier; d.to_tier = huecos[mejor].tier;
            bajadasOut->push_back(d);
        }
        RejPromo(fa.from_tier, huecos[mejor].tier < fa.from_tier ? "DESC-baja" : "DESC-mismo", 8);
        huecos[mejor].libre = false;
        yaSentado.insert(fa.driver_id);
        save.seasons_idle.erase(fa.driver_id);   // ~~~29/08: has a seat again
        save.idle_from_series.erase(fa.driver_id);
        save.seat_occupant[huecos[mejor].key] = fa.driver_id;
        NoteSeatContractStart(save, huecos[mejor].key, worldYear);   // a NEW contract for the new seat
        colocados++;
    }
    return colocados;
}

// LIFE HAPPENS (see career_seats.h). Departures that are neither age nor level.
int CascadeAttrition(SaveData& save, int worldYear, const std::set<std::string>& delJugador,
                     std::vector<std::string>* salenOut) {
    if (worldYear <= 0) return 0;
    // 17/09 — ONE LIFE, ONE DIE. The draw was PER SEAT (the hash carried the slot key), and in
    // this world one person holds several: measured at T0, Lucía Vega had seven slots and Andre Costa ten,
    // so each winter they rolled seven and ten dice for leaving the sport. With level 1's 80‰ that is a
    // 44% and a 57% dropout in the first year, against the 8% the design says — and that is how the whole
    // cohort bled out: 103 rows at T0, 45 at T1, 11 at T2. Nobody leaves motorsport three times.
    //
    // Now the die goes with the PERSON, and the level that grades it is their BEST seat, which is what the
    // constant's text says: «nobody leaves an F1 seat out of boredom». Whoever leaves, leaves
    // ALL their slots — before, they left the sport and kept showing up in the other six grids.
    struct Vida { int mejorTier = 0; std::vector<std::string> plazas; };
    std::map<std::string, Vida> gente;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;              // period series don't rotate
        const int tier = (sd.level >= 1 && sd.level <= 7) ? sd.level : 1;
        if (bal::ATTRITION_PER_MILLE[tier] <= 0) continue;
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (size_t i = 0; i < seats.size(); ++i) {
            if (seats[i].open || seats[i].occupant_id.empty()) continue;
            if (save.retired.count(seats[i].occupant_id)) continue;
            if (FueraDelMercadoDePilotos(seats[i].occupant_id)) continue;   // 23/09: off the market they don't «leave it»
            const std::string key = SeatKey(sd.id, (int)i);
            if (delJugador.count(key)) continue;              // your seat isn't touched by anyone else's life events
            Vida& v = gente[seats[i].occupant_id];
            if (tier > v.mejorTier) v.mejorTier = tier;
            v.plazas.push_back(key);
        }
    }
    int bajas = 0;
    for (const auto& kv : gente) {
        const int porMil = bal::ATTRITION_PER_MILLE[kv.second.mejorTier];
        if (porMil <= 0) continue;
        // The YEAR goes inside the hash: if it depended only on (seed, person), it would always hit the same one.
        const unsigned long dado = seatFnv1a(std::to_string(save.career_seed) + "|vida|" +
                                             std::to_string(worldYear) + "|" + kv.first);
        if ((int)(dado % 1000ul) >= porMil) continue;
        // He's leaving the sport: not a free agent looking for a spot further down, but someone quitting it.
        save.retired.insert(kv.first);
        for (const auto& key : kv.second.plazas) {
            save.seat_occupant[key] = kSeatOpen;
            save.seat_contract_start.erase(key);
        }
        if (salenOut) salenOut->push_back(kv.first);
        bajas++;
    }
    return bajas;
}

// THE PULL-UP PROMOTION (see career_seats.h). The missing rung of the cascade.
int CascadePromoteToOpenSeats(SaveData& save, int worldYear, std::vector<std::string>* subenOut,
                              const std::set<std::string>& delJugador) {
    // The census, done ONCE: who's who, with their career ceiling and current tier.
    const std::vector<RosterDriver> mundo = WorldOccupants(save);
    std::map<std::string, const RosterDriver*> porId;
    for (const auto& r : mundo) porId[r.id] = &r;

    // Where everyone sits right now (seat key), so their old spot can be emptied when they're moved.
    std::map<std::string, std::string> asientoDe, serieDe;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (size_t i = 0; i < seats.size(); ++i)
            if (!seats[i].open && !seats[i].occupant_id.empty()) {
                asientoDe[seats[i].occupant_id] = SeatKey(sd.id, (int)i);
                serieDe[seats[i].occupant_id]   = sd.id;
            }
    }

    int movidos = 0;
    std::set<std::string> yaMovido = FichadosDeLaVentana(save, worldYear);   // 23/09 ()
    // TOP TO BOTTOM, and that order IS the mechanism: filling a tier-7 seat with someone from
    // tier 6 opens a gap in tier 6, which, once its turn comes, pulls someone from tier 5, and so on down,
    // down to where the new generation picks up whatever's left. Done backwards, each tier would fill before
    // its own vacancy arrived and the pull-up chain wouldn't exist: which is exactly what used to happen, with
    // promotions tied only to retirements ("top to bottom, so every gap is inherited
    // by whoever's coming up behind").
    for (int tier = 7; tier >= 1; --tier) {
        // 14/08 — WITHIN A LEVEL, THE HIGHEST PAYER GOES FIRST. This was missing and was the piece that
        // kept the per-series scale from showing: level 7 is EIGHT series and they were walked in the order
        // of the catalog, so F1 got whatever was left. Money filtered candidates but gave no
        // PRIORITY, and a freshly arrived ace ended up in the first free slot that turned up, not the best one.
        //
        // Ordering by economic scale is also what really happens: whoever can pay most signs first.
        std::vector<const SeriesDef*> delTier;
        for (const auto& sd : AllSeries())
            if (EsMercadoVivo(sd) && sd.level == tier) delTier.push_back(&sd);
        std::sort(delTier.begin(), delTier.end(), [](const SeriesDef* a, const SeriesDef* b) {
            if (a->econ_scale != b->econ_scale) return a->econ_scale > b->econ_scale;
            return a->id < b->id;                            // tie -> id, to keep it deterministic
        });
        for (const SeriesDef* psd : delTier) {
            const SeriesDef& sd = *psd;
            const ClassDemand ex = DemandForClass(sd.id, save, worldYear);
            const int afinDestino = SeriesAffinity(sd.id);
            const std::vector<Seat> seats = SeatsForClass(sd.id, save);
            for (size_t i = 0; i < seats.size(); ++i) {
                if (!seats[i].open) continue;
                const std::string destino = SeatKey(sd.id, (int)i);
                if (delJugador.count(destino)) continue;      // nobody gets promoted into YOUR seat
                // The best candidate: someone racing BELOW this tier, whose ceiling reaches up to here and who
                // clears this category's bar. With not enough ceiling, they don't get promoted -- and a real
                // driver's ceiling IS their current category, so who actually moves up is the
                // cohort and the generated rookies, which is exactly who we wanted the world to fill up with.
                // 14/08 — AND HERE THE MONEY, which is where it really had to be. The budget was only
                // consulted in the final sweep (`SealLiveGrids`), which signs 4-30 slots per season;
                // this is the MAIN promotion channel and it was blind to money, so the per-series scale
                // showed in nothing. Measured: with the scale in place but only in the sweep, the F1 grid
                // gave exactly the same numbers.
                //
                // It goes as a PREFERENCE in two passes, not as a veto: first those the team can
                // pay are considered and, if there is nobody, the best there is. The rule that no slot stays empty
                // overrides the economy, same as in the sweep.
                const TeamBudget bolsillo = BudgetFor(sd.id, seats[i].team_label, save, worldYear);

                const RosterDriver* mejor = nullptr;
                double mejorNota = 0.0;
                for (int pasada = 0; pasada < 2 && !mejor; ++pasada)
                for (const auto& r : mundo) {
                    if (yaMovido.count(r.id) || save.retired.count(r.id)) continue;
                    if (pasada == 0 && bolsillo.per_race > 0 &&
                        PriceFor(r.id, save, worldYear, tier) > bolsillo.per_race) { RejPromo(tier, "presupuesto", pasada); continue; }
                    if (r.tier_max < tier) { if (g_trazaRutas && tier == 7) g_rej7["techo"]++; RejPromo(tier, "techo", pasada); continue; }
                    // 13/08: by `tier_now` (their HIGHEST slot today), not by `tier_min` (the lowest). With
                    // the floor, someone with several seats sneaked into a level they already occupied.
                    //
                    // 14/08 — THE LATERAL TRANSFER, BUT WITH MONEY DECIDING. On 13/08 I forbade it
                    // entirely to kill the champion exodus (Verstappen won three F1 titles and showed up in
                    // Keystone_Cup_6095), and the cure overshot: it turned the level-7 series you
                    // land in into a LIFE SENTENCE. An ace who moves up to HARBOR_CHALLENGE_8227 could never reach
                    // F1 no matter how much money F1 had — there was no door to open, and that is why the per-series
                    // scale, the realistic price and priority to the highest payer did not move F1's p90
                    // by a single point in five attempts.
                    //
                    // The correct rule is not «moving sideways is forbidden», it is «sideways ONLY UPWARD IN
                    // MONEY, and only if they can pay you»:
                    //
                    //   · never to a lower level by this route (that is what the demotion cascade is for);
                    //   · at the SAME level, only if the destination series pays MORE than where they run now — so
                    //     Verstappen doesn't go to Keystone_Cup_6095 (it pays x0.70 against F1's x2.50) but an ace from
                    //     HARBOR_CHALLENGE_8227 can end up in F1;
                    //   · and the team has to be able to REALLY afford it, in both passes: a lateral
                    //     transfer is not covering a slot, it is a signing, and without money there is no signing.
                    if (r.tier_now > 0 && r.tier_now >= tier) {
                        if (r.tier_now > tier) { RejPromo(tier, "hacia-abajo", pasada); continue; }   // never through here
                        const std::string& suSerie = serieDe.count(r.id) ? serieDe[r.id] : std::string();
                        const SeriesDef* actual = suSerie.empty() ? nullptr : SeriesById(suSerie);
                        const double alli = actual ? actual->econ_scale : 1.0;
                        if (sd.econ_scale <= alli) { RejPromo(tier, "lateral-no-paga-mas", pasada); continue; }
                        if (bolsillo.per_race <= 0 ||
                            PriceFor(r.id, save, worldYear, tier) > bolsillo.per_race) { RejPromo(tier, "lateral-sin-dinero", pasada); continue; }
                    }
                    if (r.tier_min >= tier && r.tier_now == 0) { if (g_trazaRutas && tier == 7) g_rej7["ya-esta"]++; RejPromo(tier, "ya-esta", pasada); continue; }
                    if (tier - r.tier_min > bal::CASCADE_PROMOTE_MAX_STEPS) { if (g_trazaRutas && tier == 7) g_rej7["salto"]++; RejPromo(tier, "salto", pasada); continue; }
                    const double nota = DriverAgedRating(r.id, r.base_skill, r.birth_year, save,
                                                         worldYear, r.age_shift, r.tier_min);
                    if (ex.resolved && nota < ex.floor) { if (g_trazaRutas && tier == 7) g_rej7["liston"]++; RejPromo(tier, "liston", pasada); continue; }
                    // And the discipline has to match. The cohort and the rookies don't bring
                    // their own discipline, so they inherit it from wherever they're racing, and can cross between worlds
                    // while they climb -- which is exactly what a kid does coming out of karting.
                    // 07/09: this guard had NO counter. It is the only one in the loop that could be
                    //  dropping people without a trace, and that is why it gets one here for the first time.
                    // 16/09: lifting this guard was tried when the candidate moves UP a level, and the world
                    //  came out worse on all four counts. The figures and the why, above, next to `AffinityAllows`.
                    if (!AffinityAllows(DriverAffinity(r.id, r.name, serieDe.count(r.id) ? serieDe[r.id] : std::string()),
                                        afinDestino)) { RejPromo(tier, "afinidad", pasada); continue; }
                    if (!mejor || nota > mejorNota ||
                        (nota == mejorNota && r.id < mejor->id)) { mejor = &r; mejorNota = nota; }
                }
                if (!mejor) { RejPromo(tier, "ZZ-plaza-sin-nadie", 9); continue; }   // nobody makes the grade
                RejPromo(tier, "ZZ-ASCIENDE", 9);
                if (g_trazaRutas) {
                    char rb[96];
                    snprintf(rb, sizeof(rb), " T%d:promo>%d[res%d liston%.0f nota%.0f]",
                             worldYear, tier, (int)ex.resolved, ex.floor, mejorNota);
                    g_rutaDe[mejor->id] += rb;
                }
                save.seat_occupant[destino] = mejor->id;
                NoteSeatContractStart(save, destino, worldYear);     // a NEW contract for the new seat
                // ~~~08/09 (Josh's decision, exit (a) of the deflation): ELO IS TIED TO THE LEVEL IN THE
                //  MOVEMENT. Measured with section (11) of the harness: level 5 lost 300 points in twenty
                //  seasons and it was NOT in the races (zero-sum, +0.08..+0.19 per head) but in the handover:
                //  whoever was promoted arrived with the ELO of the level below (~1250 since T6, BELOW the bar
                //  of the class, 1400-1420), and since ELO only moves in duels within the class, each level
                //  converged toward the ELO of its inflow. The anchored T0 seeding was a gift that
                //  eroded. Now whoever enters by promotion (or transfer) enters AT LEAST at the bar of
                //  the destination class, just as D4 does for whoever never raced; whoever already exceeds it is not touched.
                if (ex.resolved && ex.floor > 0.0) {
                    auto rt = save.driver_ratings.find(mejor->id);
                    if (rt != save.driver_ratings.end() && rt->second < ex.floor) {
                        RejPromo(tier, "ZZ-ELO-al-liston", 9);           // diag: how many enter below and get tied
                        rt->second = ex.floor;
                    }
                }
                auto vieja = asientoDe.find(mejor->id);
                // The vacancy it leaves can't be the player's seat either. The seat model believes that's
                // where the driver named by the data sits; promoting THAT driver would open up the
                // player's seat through the back door -- protecting the destination, not the source. It took three
                // attempts to find it: the other five writers were already covered.
                if (vieja != asientoDe.end() && !delJugador.count(vieja->second))
                    save.seat_occupant[vieja->second] = kSeatOpen;   // leaves a vacancy
                yaMovido.insert(mejor->id);
                if (subenOut) subenOut->push_back(mejor->id);
                movidos++;
            }
        }
    }
    return movidos;
}

// ===================================================================================================
// THE FINAL SWEEP: PERIOD DRIVERS ARE FIXED AND NO SLOT STAYS EMPTY (13/08)
// ===================================================================================================
// See the long note on `SealLiveGrids` in career_seats.h. Both rules are the user's.

bool EsPilotoDeEpoca(const std::string& driverId) {
    // Over the STATIC world (empty save) and only ONCE. If it were computed over the live save, someone who
    // slipped into a live series yesterday would no longer count as period and the sweep would never remove them: the rule
    // would self-destruct with its own first violation.
    static std::set<std::string> soloEpoca;
    static bool hecho = false;
    if (!hecho) {
        hecho = true;
        const SaveData vacio;
        std::set<std::string> enEpoca, enVivas;
        for (const auto& sd : AllSeries()) {
            const bool viva = EsMercadoVivo(sd);
            for (const auto& s : SeatsForClass(sd.id, vacio)) {
                if (s.occupant_id.empty()) continue;
                (viva ? enVivas : enEpoca).insert(s.occupant_id);
            }
        }
        for (const auto& id : enEpoca) if (!enVivas.count(id)) soloEpoca.insert(id);
    }
    return soloEpoca.count(driverId) != 0;
}


// ===================================================================================================
// — TEAMS REPLACE AN INCUMBENT WITH SOMEONE BETTER (see career_seats.h)
// ===================================================================================================
// ~~~02/09 — only to MEASURE the effect of the margin: <0 = the production one. No gameplay path touches it.
double g_margenCambioMedicion = -1.0;
double MargenDeCambio() { return g_margenCambioMedicion >= 0.0 ? g_margenCambioMedicion : bal::SWAP_MARGIN; }

// 16/09 — THE BREAKDOWN OF THE LAST PASS, SO IT CAN BE PRINTED WITHOUT RUNNING IT AGAIN.
// It exists because `test_fill_pass_distribution` called `ReplaceWeakIncumbents` a SECOND time after the
// rollover just to have its `SwapReport` in hand -- and this function does not report: it MOVES PEOPLE. That second
// pass changed occupants without an author and, carrying no `desplazadosOut`, left the outgoing incumbent out of
// all the distributions. Measured with `--medidas`: with the second call 51 people are lost in twenty
// winters; without it, 0. Same pattern as `LastGridDrops()`: it is stored, not recomputed.
static SwapReport g_lastSwap;
const SwapReport& LastSwapReport() { return g_lastSwap; }

SwapReport ReplaceWeakIncumbents(SaveData& save, int worldYear, const std::set<std::string>& delJugador,
                                 std::vector<FreeAgent>* desplazadosOut) {
    SwapReport rep;
    if (worldYear <= 0) { g_lastSwap = rep; return rep; }

    // Who is free to be signed: EVERYONE who holds a live slot, with their highest slot and their
    // seats. `WorldOccupants` already excludes the period, so nobody frozen enters from there.
    const std::vector<RosterDriver> mundo = WorldOccupants(save);
    std::map<std::string, std::vector<std::string>> asientosDe;
    std::map<std::string, std::string> serieDe;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (size_t i = 0; i < seats.size(); ++i)
            if (!seats[i].open && !seats[i].occupant_id.empty()) {
                asientosDe[seats[i].occupant_id].push_back(SeatKey(sd.id, (int)i));
                serieDe[seats[i].occupant_id] = sd.id;
            }
    }
    // Do they have ALL their contracts expired? If one is still live, they are not for sale.
    auto libre = [&](const std::string& id) {
        const auto it = asientosDe.find(id);
        if (it == asientosDe.end()) return true;                 // no seat: free agent
        for (const auto& k : it->second)
            if (!SeatContractExpires(save, k, id, worldYear)) return false;
        return true;
    };

    std::set<std::string> yaMovido = FichadosDeLaVentana(save, worldYear);   // 23/09 ()
    // Top to bottom and, within a level, THE HIGHEST PAYER FIRST: F1 picks before HARBOR_CHALLENGE_8227, which
    // is what concentrates talent where it can really be paid.
    for (int tier = 7; tier >= 1; --tier) {
        std::vector<const SeriesDef*> delTier;
        for (const auto& sd : AllSeries())
            if (EsMercadoVivo(sd) && sd.level == tier) delTier.push_back(&sd);
        std::sort(delTier.begin(), delTier.end(), [](const SeriesDef* a, const SeriesDef* b) {
            if (a->econ_scale != b->econ_scale) return a->econ_scale > b->econ_scale;
            return a->id < b->id;
        });
        for (const SeriesDef* psd : delTier) {
            const SeriesDef& sd = *psd;
            // And ONLY WHERE THERE IS A PROFESSIONAL MARKET. In a club cup or a school nobody signs anybody
            // by taking someone else's place: you race with whoever pays. Limiting it to the top also concentrates the
            // effect where it was needed -- the premier-category bottleneck -- without stirring the whole world.
            if (tier < bal::SWAP_MIN_TIER) continue;
            const int afinDestino = SeriesAffinity(sd.id);
            const int techoEdad = ClassAgeCeiling(sd.class_name);
            const std::vector<Seat> seats = SeatsForClass(sd.id, save);
            // And A CAP PER GRID AND SEASON. A team changing a driver is normal; half a grid
            // changing every winter is not a market, it is a raffle — and it also wipes out the familiar faces,
            // which is exactly what makes following a championship make sense. 15%: in a grid of 20
            // that is three changes, which is what you see in a genuinely busy year.
            // MEASURED AND CAPPED AT ONE. With 15% (three slots of twenty) F1 came out perfect but the
            // WORLD broke: real drivers fell from 883 to 388 over twenty seasons, those displaced
            // upward went from 102 to 534 and slots with no candidate at all from 20 to 216. The cause is
            // that a displaced driver falls to `CascadeDemoteFreeAgents`, which only drops TWO steps and if it doesn't
            // find room RETIRES them: displacing 270 people a year across the 91 series kills careers at a
            // rate the world cannot replenish. Losing a seat cannot be a sentence.
            int tope = 1;
            int hechos = 0;
            for (size_t i = 0; i < seats.size() && hechos < tope; ++i) {
                if (seats[i].open || seats[i].occupant_id.empty()) continue;
                const std::string key = SeatKey(sd.id, (int)i);
                if (delJugador.count(key)) continue;             // nobody negotiates your slot for you
                const std::string& titular = seats[i].occupant_id;
                if (save.retired.count(titular) || yaMovido.count(titular)) continue;
                if (!libre(titular)) continue;                   // their contract protects them
                rep.mirados++;

                const double suyo = OccupantRating(sd.id, titular, save, worldYear);
                if (suyo <= 0.0) continue;
                const TeamBudget bolsillo = BudgetFor(sd.id, seats[i].team_label, save, worldYear);

                const RosterDriver* mejor = nullptr;
                double mejorNota = 0.0;
                for (const auto& r : mundo) {
                    if (r.id == titular || yaMovido.count(r.id) || save.retired.count(r.id)) continue;
                    if (EsPilotoDeEpoca(r.id)) continue;
                    if (serieDe.count(r.id) && serieDe[r.id] == sd.id) continue;   // already races HERE
                    if (!libre(r.id)) { rep.caenPorContrato++; continue; }   // tied up elsewhere
                    // THE LADDER GUARDS, WHICH I FORGOT AND WERE SEEN ON THE FIRST PASS: without
                    // them a KARTING rookie got into F1 (measured, gn_2026 <- harbor_trophy_6546). An incumbent change
                    // is a normal signing, not a teleport: the SAME rules apply as
                    // in the promotion cascade.
                    if (r.tier_max < tier) { rep.caenPorEscalera++; continue; }         // their ceiling doesn't reach
                    if (r.tier_now > 0 && tier - r.tier_now > bal::CASCADE_PROMOTE_MAX_STEPS) { rep.caenPorEscalera++; continue; }
                    if (r.tier_now == 0 && tier - r.tier_min > bal::CASCADE_PROMOTE_MAX_STEPS) { rep.caenPorEscalera++; continue; }
                    const int edad = DriverAge(r.birth_year, worldYear);
                    if (techoEdad > 0 && edad > 0 && edad > techoEdad) continue;
                    if (!AffinityAllows(DriverAffinity(r.id, r.name, serieDe.count(r.id) ? serieDe[r.id] : std::string()),
                                        afinDestino)) continue;
                    const double nota = DriverAgedRating(r.id, r.base_skill, r.birth_year, save,
                                                         worldYear, r.age_shift, tier);
                    // BETTER BY A REAL MARGIN. With `DISPLACEMENT_MARGIN` (25 points, half a
                    // step) SIX OR SEVEN F1 slots were swapped per season: a third of the
                    // grid every winter, and no name meant anything. The right margin is what
                    // the class FORGIVES (`CLASS_DEMAND_MARGIN`): if the difference is smaller than the tolerance
                    // of the category, it is not worth breaking a seat for it.
                    if (nota < suyo + MargenDeCambio()) { rep.caenPorMargen++; continue; }
                    // And the team has to be able to AFFORD it: this is a signing, not an emergency filler.
                    if (bolsillo.per_race > 0 && PriceFor(r.id, save, worldYear, tier) > bolsillo.per_race) { rep.caenPorDinero++; continue; }
                    if (!mejor || nota > mejorNota || (nota == mejorNota && r.id < mejor->id)) {
                        mejor = &r; mejorNota = nota;
                    }
                }
                if (!mejor) { rep.noHayMejor++; continue; }

                // The swap. The challenger leaves ALL their slots (the gaps are inherited by the distribution that follows)
                // and the incumbent is left without this one: the cascade will find them room lower down, which is the
                // demotion channel. They are not retired here -- losing a seat is not leaving the sport.
                const auto viejas = asientosDe.find(mejor->id);
                if (viejas != asientosDe.end())
                    for (const auto& k : viejas->second) {
                        if (k == key || delJugador.count(k)) continue;
                        save.seat_occupant[k] = kSeatOpen;
                        save.seat_contract_start.erase(k);
                    }
                { const auto sd2 = serieDe.find(mejor->id);
                  const SeriesDef* o = (sd2 == serieDe.end()) ? nullptr : SeriesById(sd2->second);
                  Ruta(mejor->id, worldYear, "swap", o ? o->level : 0, tier); }
                save.seat_occupant[key] = mejor->id;
                NoteSeatContractStart(save, key, worldYear);
                asientosDe[mejor->id] = std::vector<std::string>{ key };
                serieDe[mejor->id] = sd.id;
                // ~~~29/08 — AND THE DISPLACED DRIVER LEAVES BY THE DOOR, not the window. They are handed to the
                //  demotion cascade like any free agent, unless they have another slot left.
                if (desplazadosOut) {
                    const auto suyas = asientosDe.find(titular);
                    bool leQuedaOtra = false;
                    if (suyas != asientosDe.end())
                        for (const auto& k : suyas->second) if (k != key) { leQuedaOtra = true; break; }
                    if (!leQuedaOtra) {
                        FreeAgent fa;
                        fa.driver_id = titular; fa.from_seat = key; fa.from_series = sd.id;
                        fa.from_tier = tier;    fa.rating = suyo;
                        for (const auto& r : mundo)
                            if (r.id == titular) { fa.age = DriverAge(r.birth_year, worldYear); break; }
                        desplazadosOut->push_back(fa);
                    }
                }
                asientosDe.erase(titular);
                serieDe.erase(titular);
                yaMovido.insert(mejor->id);
                yaMovido.insert(titular);                        // the same driver is not displaced twice in one year
                rep.cambios++;
                hechos++;
            }
        }
    }
    g_lastSwap = rep;
    return rep;
}

SealReport SealLiveGrids(SaveData& save, int worldYear, const std::set<std::string>& delJugador) {
    SealReport rep;
    if (worldYear <= 0) return rep;

    // 1) EVICTION. A period driver does not occupy a live slot. It is done BEFORE filling so that the gap
    //    it leaves enters the same distribution as the others, and does not wait for next year.
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (size_t i = 0; i < seats.size(); ++i) {
            if (seats[i].open || seats[i].occupant_id.empty()) continue;
            if (!EsPilotoDeEpoca(seats[i].occupant_id)) continue;
            const std::string key = SeatKey(sd.id, (int)i);
            if (delJugador.count(key)) continue;
            save.seat_occupant[key] = kSeatOpen;
            save.seat_contract_start.erase(key);
            rep.desalojados++;
        }
    }

    // 2) FILL. The census of the LIVE world, once: `WorldOccupants` already excludes the period, so from here none
    //    of the ones we just removed can come back in.
    const std::vector<RosterDriver> mundo = WorldOccupants(save);
    // ALL their live slots, not one. The first version kept ONE per driver and when moving them emptied only
    // that one; the others are DERIVED from the static data, so they reappeared and the driver accumulated a seat per
    // season. Measured: Hamilton ended up with FOUR live slots at once —F1, HARBOR_CHALLENGE_8227, NIMBUS_MASTERS_5890 and IVORY_CUP_12573—, which are
    // four calendars that clash with each other. A gap in the grid is ugly; a driver in four places on the
    // same Sunday is impossible.
    std::map<std::string, std::vector<std::string>> asientosDe;
    std::map<std::string, int> tierDe;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;
        const int tierSerie = (sd.level >= 1 && sd.level <= 7) ? sd.level : 1;
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (size_t i = 0; i < seats.size(); ++i)
            if (!seats[i].open && !seats[i].occupant_id.empty()) {
                asientosDe[seats[i].occupant_id].push_back(SeatKey(sd.id, (int)i));
                int& t = tierDe[seats[i].occupant_id];
                if (tierSerie > t) t = tierSerie;          // their HIGHEST slot rules
            }
    }

    std::set<std::string> yaMovido = FichadosDeLaVentana(save, worldYear);   // 23/09 ()
    // Top to bottom, like the cascade: filling a tier 7 slot opens a gap in 6, which pulls
    // from 5. The other way round, each tier would fill before its own vacancy arrived.
    for (int tier = 7; tier >= 1; --tier) {
        for (const auto& sd : AllSeries()) {
            if (!EsMercadoVivo(sd) || sd.level != tier) continue;
            const ClassDemand ex = DemandForClass(sd.id, save, worldYear);
            const int afinDestino = SeriesAffinity(sd.id);
            const int techoEdad = ClassAgeCeiling(sd.class_name);
            const std::vector<Seat> seats = SeatsForClass(sd.id, save);
            for (size_t i = 0; i < seats.size(); ++i) {
                if (!seats[i].open) continue;
                const std::string destino = SeatKey(sd.id, (int)i);
                if (delJugador.count(destino)) continue;

                // TWO PASSES, and the second is the user's rule: if nobody makes the grade, the best
                //   available GETS IN ANYWAY. A gap in the grid is worse than a weak driver — the gap shows on
                //   track and the weak driver is, simply, a weak driver.
                // — WHAT THIS TEAM CAN AFFORD THIS SEASON. Scale of its series x its position
                // last year, and it EXPIRES (there is no piggy bank: see `TeamBudget`). Money decides WHO gets in,
                // never whether anyone does — the rule that no slot stays empty always wins, and that is why
                // affordability is a PREFERENCE (passes 0 and 1) and not a veto (pass 2).
                const TeamBudget bolsillo = BudgetFor(sd.id, seats[i].team_label, save, worldYear);

                const RosterDriver* mejor = nullptr;
                double mejorNota = 0.0;
                int pasadaGanadora = 0;
                // pass 0: makes the level AND can afford it · 1: can afford it · 2: the best there is (the gap
                // does not stay, even if the team stretches above its budget)
                // ~~~16/09 — THERE IS NO FOURTH PASS HERE THAT RELAXES THE DISCIPLINE, AND IT WAS TRIED. The suspicion was
                // that affinity, required in all three, would leave the seat empty when the pool of its
                // discipline ran out; it was implemented and signed ZERO slots in twenty seasons. The reason is from further
                // up: the seats left empty are ALL level 1, and to a level-1 seat
                // this function can't send anyone — the candidate pool is `WorldOccupants` (people who
                // already have a seat) and the guard «their tier >= the destination's», two lines below, discards them
                // all because there is nobody below level 1. Those gaps are picked up by the second pass
                // of rookies, after the seal (career_calendar.cpp, `RolloverYearReport`).
                for (int pasada = 0; pasada < 3 && !mejor; ++pasada) {
                    for (const auto& r : mundo) {
                        if (yaMovido.count(r.id) || save.retired.count(r.id)) continue;
                        if (EsPilotoDeEpoca(r.id)) continue;            // rule 1, again: not through here either
                        // 14/08 — PENDING, AND MEASURED WHY. «A driver with a contract in force is not
                        // for sale» (user's request, and it is correct: otherwise, signing a kid for three years
                        // doesn't protect him from being taken the following winter). It was tried HERE and can't
                        // be applied yet: the candidate pool is `WorldOccupants`, i.e. those who DO have a
                        // seat, and twenty lines earlier `ExpireSeatContracts` has renewed the contract of
                        // everyone who passed the bar. The intersection comes out empty — measured: ZERO signings and
                        // between 24 and 57 slots uncovered per season, which also breaks the other rule, the one that
                        // no slot stays empty.
                        //
                        // What is missing first is the FREE AGENT POOL —the people WITHOUT a seat—, which today
                        // returns nobody. With that pool in hand, this guard is one line and then it does
                        // mean what it has to mean.
                        const auto it = tierDe.find(r.id);
                        if (it != tierDe.end() && it->second >= tier) continue;   // already races here or higher
                        if (r.tier_max < tier && pasada == 0) continue;           // their ceiling doesn't reach (1st pass)
                        if (!AffinityAllows(DriverAffinity(r.id, r.name, std::string()), afinDestino)) continue;
                        const int edad = DriverAge(r.birth_year, worldYear);
                        if (techoEdad > 0 && edad > 0 && edad > techoEdad) continue;
                        const double nota = DriverAgedRating(r.id, r.base_skill, r.birth_year, save,
                                                             worldYear, r.age_shift, it == tierDe.end() ? 1 : it->second);
                        if (pasada == 0 && ex.resolved && nota < ex.floor) continue;   // the bar, ONLY the 1st
                        // The pocket: in the first two passes only those who can be paid are considered.
                        if (pasada < 2 && bolsillo.per_race > 0 &&
                            PriceFor(r.id, save, worldYear, tier) > bolsillo.per_race) continue;
                        if (!mejor || nota > mejorNota || (nota == mejorNota && r.id < mejor->id)) {
                            mejor = &r; mejorNota = nota; pasadaGanadora = pasada;
                        }
                    }
                }
                if (!mejor) {
                    // there is NOBODY eligible or payable. That is not covered up, it is declared — it is the same
                    // signal as a short historical grid: content is missing and it has to be seen.
                    rep.sinNadie++;
                    continue;
                }
                { const auto td = tierDe.find(mejor->id);
                  const char* q = pasadaGanadora == 0 ? "sube-p0" : (pasadaGanadora == 1 ? "sube-p1" : "sube-p2");
                  Ruta(mejor->id, worldYear, q, td == tierDe.end() ? 0 : td->second, tier); }
                save.seat_occupant[destino] = mejor->id;
                NoteSeatContractStart(save, destino, worldYear);
                // And it leaves ALL the ones they had, not one: the gaps it opens enter this same distribution (it
                //   goes top to bottom), so no slot is lost along the way.
                const auto viejas = asientosDe.find(mejor->id);
                if (viejas != asientosDe.end())
                    for (const auto& k : viejas->second) {
                        if (k == destino || delJugador.count(k)) continue;
                        save.seat_occupant[k] = kSeatOpen;
                        save.seat_contract_start.erase(k);
                    }
                yaMovido.insert(mejor->id);
                tierDe[mejor->id] = tier;
                asientosDe[mejor->id] = std::vector<std::string>{ destino };
                rep.rellenados++;
                if (pasadaGanadora == 0)      rep.daElNivel++;
                else if (pasadaGanadora == 1) rep.bajoElListon++;
                else                          rep.sobreCoste++;
            }
        }
    }
    return rep;
}

// ROOKIES (see career_seats.h). The only way new drivers enter the world.

namespace {

// First and last names come from the pool itself, grouped by country. There's no curated names file, and
// none is needed: the 1,423 real drivers already carry names for every nationality racing in the game,
// so recombining them by country yields a Brazilian with a Brazilian name and a Finn with a Finnish name,
// for free, with no extra data to maintain. Any combination that matches someone who already
// exists is discarded, so as not to reinvent a real person.
struct BolsaNombres {
    std::map<std::string, std::vector<std::string>> pila, apellido;   // country -> list (deterministic order)
    std::set<std::string> existentes;
    bool cargada = false;
};
BolsaNombres g_bolsa;

void CargarBolsa() {
    if (g_bolsa.cargada) return;
    g_bolsa.cargada = true;
    for (const auto& id : PoolDriverIds()) {
        const PoolDriver* pd = DriverById(id);
        if (!pd || pd->name.empty()) continue;
        g_bolsa.existentes.insert(pd->name);
        const size_t sp = pd->name.find(' ');
        if (sp == std::string::npos || sp == 0 || sp + 1 >= pd->name.size()) continue;
        const std::string pais = pd->country.empty() ? std::string("INT") : pd->country;
        // Strip the generational suffix from the surname: keeping it produces an "Arthur Fittipaldi Jr" who has
        // no father anywhere. The bare surname is fine -- a new Fittipaldi on the grid
        // is plausible; an orphaned "Jr" is not.
        std::string ape = pd->name.substr(sp + 1);
        for (const char* suf : { " Jr", " Jr.", " Sr", " Sr.", " II", " III", " IV" }) {
            const size_t n = strlen(suf);
            if (ape.size() > n && ape.compare(ape.size() - n, n, suf) == 0) { ape.erase(ape.size() - n); break; }
        }
        if (ape.empty()) continue;
        g_bolsa.pila[pais].push_back(pd->name.substr(0, sp));
        g_bolsa.apellido[pais].push_back(ape);
    }
    // total ordering: two machines must generate the SAME rookie from the same seed
    for (auto& kv : g_bolsa.pila)     std::sort(kv.second.begin(), kv.second.end());
    for (auto& kv : g_bolsa.apellido) std::sort(kv.second.begin(), kv.second.end());
}

// A plausible nationality for a rookie in this series: that of someone already racing in it. Deterministic
// on `seed`. Empty if the series' grid has no countries.
std::string PaisDeLaParrilla(const std::string& seriesId, unsigned long semilla) {
    std::vector<std::string> paises;
    for (const auto& pd : PinnedRosterForSeries(seriesId)) if (!pd.country.empty()) paises.push_back(pd.country);
    for (const auto& f : FieldForSeries(seriesId))         if (!f.country.empty())  paises.push_back(f.country);
    if (paises.empty()) return std::string();
    std::sort(paises.begin(), paises.end());               // total ordering -> same result on every machine
    return paises[semilla % paises.size()];                // the series' REAL mix, with its proportions
}

// A plausible name for `country`, deterministic by `seed`. Empty if there's no material for that country.
std::string NombreNovato(const std::string& pais, unsigned long semilla) {
    CargarBolsa();
    auto p = g_bolsa.pila.find(pais);
    auto a = g_bolsa.apellido.find(pais);
    if (p == g_bolsa.pila.end() || a == g_bolsa.apellido.end() ||
        p->second.empty() || a->second.empty()) return std::string();
    // 32 attempts and, if none comes out free, the repeat is accepted. Before it was 8 and it returned EMPTY —
    // and an id with no name can't be written to the XML, so that car vanished from the grid and AMS2
    // put a generic in. With one seat per racing car the bag is requested 1,744 times instead of
    // ~400, and countries with few names run out: measured, 8 cars in 4 classes (GT4, KEYSTONE_CHAMPIONSHIP_6997, RX...).
    // Two drivers with the same name is a cosmetic, bounded defect; a generic car on the grid is
    // the failure this whole phase is here to remove. The id stays unique, so the engine doesn't confuse them.
    std::string primera;
    for (int intento = 0; intento < 32; ++intento) {
        const unsigned long h = seatFnv1a(pais + "|" + std::to_string(semilla) + "|" + std::to_string(intento));
        const std::string n = p->second[h % p->second.size()] + " " +
                              a->second[(h / 7919) % a->second.size()];
        if (primera.empty()) primera = n;
        if (!g_bolsa.existentes.count(n)) return n;
    }
    return primera;
}

// Power law for talent: the vast majority stay in their category, and a few go far.
// u uniform in [0,1) -> u^ROOKIE_TALENT_POWER concentrates the mass at the bottom. Returns 0..1.
double TalentoNovato(unsigned long semilla) {
    const double u = (double)(seatFnv1a("tal|" + std::to_string(semilla)) % 100000ul) / 100000.0;
    return std::pow(u, (double)bal::ROOKIE_TALENT_POWER);
}

} // anon namespace

// Their NAMES are registered once per data load. It's done here, not on the fly, because SeatsForClass
// is one of the engine's most-called functions and can't afford an insert per seat per call.
static long long g_colaFirma = -1;
// 18/09 — NO DRIVER CAN REACH THE SCREEN WITH THEIR ID AS A NAME. Josh's rule, no exceptions.
//
// The minted ids of the pinned set and the field (`rd_<fnv of the name>`) could only be read INSIDE their own
// series: the card that resolves names is built per series, and as soon as the market moves that person to another
// class, in the new one nobody knows them. Then the world census returned them nameless and the
// end-of-season report printed «rd_1045361494 · -1 yrs · Leaves the sport».
//
// Here the name of the WHOLE curated grid is registered in the name registry, with the SAME minting
// with which the id was created. From there `DriverDisplayName` resolves it from any series and from
// any screen — not only in the rollover.
//
// And WITHOUT requiring a country, which is the difference with `IndiceGlobalDeIdentidad`. That index asks for a country because
// its business is the flag (), and so it leaves out whoever has a name but no country. To SHOW A
// NAME the country isn't needed: whoever lacks one ends up without a flag, not without a name.
static long long g_nombresParrillaFirma = -1;
void EnsureRosterNames() {
    const long long firma = (long long)AllSeries().size() * 1000003LL + (long long)FieldRowCount() * 131LL
                          + (long long)PinnedRosterCount() * 17LL + (long long)RosterRowCount();
    if (g_nombresParrillaFirma == firma) return;
    g_nombresParrillaFirma = firma;
    for (const auto& sd : AllSeries()) {
        for (const auto& pd : PinnedRosterForSeries(sd.id)) {
            const std::string id = PinnedOccupantId(pd);
            if (!id.empty() && !pd.name.empty()) RegisterGeneratedName(id, pd.name, pd.country);
        }
        for (const auto& fd : FieldForSeries(sd.id)) {
            const std::string id = FieldOccupantId(fd);
            if (!id.empty() && !fd.name.empty()) RegisterGeneratedName(id, fd.name, fd.country);
        }
    }
    // The authored RESERVE, with the same minting the registry uses to register it.
    for (const auto& kv : AllAuthoredProfiles()) {
        const AuthoredProfile& ap = kv.second;
        if (ap.name.empty()) continue;
        FieldDriver fake; fake.name = ap.name;
        const std::string id = FieldOccupantId(fake);
        if (!id.empty()) RegisterGeneratedName(id, ap.name, ap.country);
    }
}

void EnsureTailNames() {
    EnsureRosterNames();   // they go together: both are "nobody is left without a name"
    const long long firma = (long long)AllSeries().size() * 1000003LL + (long long)FieldRowCount() * 131LL
                          + (long long)PinnedRosterCount() * 17LL + (long long)RosterRowCount();
    if (g_colaFirma == firma) return;
    g_colaFirma = firma;
    for (const auto& sd : AllSeries()) {
        if (IsEraMemorySeries(sd)) continue;
        const int tier = (sd.level >= 1 && sd.level <= 7) ? sd.level : 1;
        // can NO LONGER skip the cohort's home tiers. With one seat per racing car, a class
        // of level 1-2 with more cars than people in the cohort (28) also has a queue — and nameless, that
        // slot would read as EMPTY and AMS2 would put a generic in the car.
        // And the walk is no longer «from the end of the field to the end of the cohort»: it is that of the seats of
        // truth, with the SAME precedence that distributes them. If the two don't match, the slot is born nameless.
        const std::vector<std::string>  liv    = SeatLiveriesForSeries(sd.id);
        const std::vector<PinnedDriver> pinned = PinnedRosterForSeries(sd.id);
        const std::vector<FieldDriver>  field  = FieldForSeries(sd.id);
        const std::vector<RosterDriver> cast   = RosterForTier(tier);
        const size_t nSeats = liv.empty() ? cast.size() : liv.size();
        for (size_t i = 0; i < nSeats; ++i) {
            // The tail of ALL seats is named, not just of those that need it today. A seat
            // whose derived occupant is a driver with a card can end up needing its tail anyway: it is
            // the SUBSTITUTE for when that person is already seated in another car of the same race
            // (`event_dup_driver`, 17 cases). Without a name, the substitute can't be written and the car goes back
            // to being empty — which is exactly what was just fixed. Measured: 15 cars in 7 classes.
            // And up to `kTailSubstituteTries` grids further, which is where the derived list looks for a substitute
            // when this slot's driver is already seated. A nameless substitute can't be written.
            for (int k = 0; k < kTailSubstituteTries; ++k) {
                const std::string id = TailOccupantId(sd.id, (int)i + k * (int)nSeats);
                const unsigned long semilla = seatFnv1a(id);
                const std::string paisEfectivo = [&]{
                    const std::string p = PaisDeLaParrilla(sd.id, semilla);
                    return p.empty() ? std::string("BRA") : p;
                }();
                std::string nombre = NombreNovato(paisEfectivo, semilla);
                // the country travels WITH the name into the registry -- it was generated from THAT country's name pool, and without it
                // re-binding a grid slot left the previous occupant's flag behind (Olofsson stayed SWE in an ITA seat).
                if (!nombre.empty()) RegisterGeneratedName(id, nombre, paisEfectivo);
            }
        }
    }
}

// ~~~29/08 — Rookie pool switch, so the contribution can be MEASURED by turning it on and off
// on the same world. Same idiom as `g_varaGlobal`. Production = true.
bool g_novatosDelPozo = true;

// ~~~29/08 — BEFORE INVENTING ANYONE, CHECK WHETHER SOMEONE REAL IS WAITING.
//
// The defect, measured over five production winters: **433 slots go from a named person to
//  an INVENTED driver** (against 22 the other way). Of those, 278 are healthy rotation --the incumbent retired and a
//  new generation comes in-- but in ALL 433 the replacement is invented... while there are **1,549 real people without a
//  seat** in `docs/career/pack_drivers.psv`, people from the livery packs we already have on disk.
//
// It is literally what the asks for («consume the pack drivers first, and let the procedural reserve
// cover only what is left») and what the header of `gen_pack_drivers.py` says about its own reason for being:
// «there is no need to research drivers: you just have to stop throwing them away».
//
// It is searched in the seat's CLASS, and that solves two things for free that would otherwise have to be computed:
//  the discipline (it is cataloged there, so it runs that) and the level (ditto). No derived ceilings or
//  affinity matrices: if the data says this person races in this class, they fit this class.
// Deterministic: `PackDriverForClassSlot` goes by index, and is walked in order.
namespace {
// All ids that ALREADY hold a live slot. Computed once per pass: asking per seat would be
// quadratic over 1,700 slots.
std::set<std::string> OcupadosAhora(const SaveData& save) {
    std::set<std::string> out;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;
        for (const auto& st : SeatsForClass(sd.id, save))
            if (!st.open && !st.occupant_id.empty()) out.insert(st.occupant_id);
    }
    return out;
}

// The first real person cataloged in `className` who has no seat, isn't retired and hasn't already been
// taken by another slot in this same pass. Empty string = nobody left, and then one is invented.
// Searches ONE specific class. Empty string = nobody free cataloged there.
// ~~~08/09 (Josh's decision via C2, spec E7): THE CURATED CATEGORY OF ORIGIN RULES THE SEATING.
//  Until today the pool person entered by their PACK's class (PackDriverForClassSlot, in the order of the
//  file), and everything came from there: level, entry ELO (D4: the seat's) and affinity. With pool_origin.psv
//  loaded, for a seat of class X whoever has X as their category of origin is looked for FIRST (high
//  confidence first, id as tiebreak), and X's pack cohort comes after -- and from it whoever has a
//  curated origin at ANOTHER level does not enter (that person belongs elsewhere). Whoever has the note «era»/«retired» does not
//  enter the modern world, and neither does a real person older than ACTIVE_MAX_AGE (the legends of the
//  historical packs: 189 of the 1,488 are over 60). Without the file, it is exactly the seating from before.
std::map<std::string, std::vector<std::string> > g_origenPorClase;   // origin class -> pool ids, sorted
size_t g_origenFirma = 0;
void IndiceDeOrigen(const SaveData& save) {
    const size_t firma = save.driver_ratings.size() * 131u + (size_t)PoolOriginCount() * 7u + 1u;
    if (firma == g_origenFirma) return;
    g_origenFirma = firma;
    g_origenPorClase.clear();
    for (const auto& kv : save.driver_ratings) {
        const PoolOrigin* o = PoolOriginFor(kv.first);
        if (!o || o->era) continue;
        g_origenPorClase[o->home_class].push_back(kv.first);
    }
    for (auto& kv : g_origenPorClase)
        std::sort(kv.second.begin(), kv.second.end(), [](const std::string& a, const std::string& b) {
            const PoolOrigin* oa = PoolOriginFor(a); const PoolOrigin* ob = PoolOriginFor(b);
            const int ca = oa ? oa->confidence : 0, cb = ob ? ob->confidence : 0;
            if (ca != cb) return ca > cb;
            return a < b;
        });
}
bool DemasiadoMayorParaEntrar(const std::string& id, const SaveData& save, int worldYear) {
    if (worldYear <= 0) return false;
    int by = 0;
    { auto it = save.birth_year.find(id); if (it != save.birth_year.end()) by = it->second; }
    if (!by) if (const PoolDriver* pd = DriverById(id)) by = pd->birth_year;
    if (!by) by = BirthYearFor(id);
    if (!by) return false;                                  // without an age nothing is forbidden
    return DriverAge(by, worldYear) > bal::ACTIVE_MAX_AGE;
}

// 23/09 — FROM THE RESERVE ONLY WHOEVER STILL RACES ENTERS, AND WITH THEIR AGE. The reserve is real people
// (livery packs, authored corpus) who don't race in any grid today. Uncurated, the cascade seated
// a champion retired fifteen years ago just like an active driver, and with no birth year: they
// neither aged nor retired, and by T20 there were 753 of the reserve seated, 608 of them truly retired.
// Now only whoever docs/career/reserve_people.psv gives as ACTIVE in 2025 enters, with a real birth year and
// not exceeding ACTIVE_MAX_AGE. Whoever isn't from the reserve is not touched. Without the file loaded, the world from before.
}   // namespace -- visible from outside (career_seats.h): the suite tests it
bool ReservaEntraEnParrilla(const std::string& id, int worldYear) {
    if (ReservePeopleCount() == 0) return true;
    const canon::DriverIdentity* di = registry::ById(id);
    if (!di || di->population_role != canon::PopRole::Reserve) return true;
    if (di->reserve_status != RESERVE_ACTIVE || di->birth_year <= 0) return false;
    if (worldYear > 0 && DriverAge(di->birth_year, worldYear) > bal::ACTIVE_MAX_AGE) return false;
    return true;
}
// 23/09 — see career_seats.h. The reserve that curation does NOT give as active (retired, unknown or uncurated).
bool FueraDelMercadoDePilotos(const std::string& id) {
    if (id.empty() || ReservePeopleCount() == 0) return false;
    const canon::DriverIdentity* di = registry::ById(id);
    if (!di || di->population_role != canon::PopRole::Reserve) return false;
    return di->reserve_status != RESERVE_ACTIVE;
}
namespace {

std::string PersonaRealLibreEn(const std::string& className, const SaveData& save,
                               const std::set<std::string>& ocupados,
                               const std::set<std::string>& usadosYa,
                               int nivel = -1, int worldYear = 0) {
    // 1) whoever has THIS class as their curated category of origin
    if (PoolOriginCount() > 0) {
        IndiceDeOrigen(save);
        const auto it = g_origenPorClase.find(className);
        if (it != g_origenPorClase.end())
            for (const std::string& id : it->second) {
                if (usadosYa.count(id) || ocupados.count(id) || save.retired.count(id)) continue;
                if (DemasiadoMayorParaEntrar(id, save, worldYear)) continue;
                return id;
            }
    }
    // 2) the class's pack cohort, in file order (as always)
    for (int k = 0; ; ++k) {
        const PackDriver* pd = PackDriverForClassSlot(className, k);
        if (!pd) break;
        const std::string id = PackDriverId(*pd);
        if (id.empty() || usadosYa.count(id) || ocupados.count(id)) continue;
        if (save.retired.count(id)) continue;
        if (!ReservaEntraEnParrilla(id, worldYear)) continue;   // 23/09: retired or uncurated doesn't enter
        if (const PoolOrigin* o = PoolOriginFor(id)) {      // their curated origin: whoever is from their period doesn't enter
            if (o->era) continue;
            // MEASURED 08/09: excluding here whoever has a curated origin at ANOTHER level removed the 16 that entered
            //  level 6 every twenty seasons and lowered the aces in the premier class from 19-23 to 13 in the three
            //  seeds. The class's pack cohort still enters through its pack; the origin rules the
            //  order (step 1), not the prohibition. `nivel` is kept for the measured variant.
            (void)nivel;
        }
        return id;
    }
    return std::string();
}

// ~~~29/08 (2nd round) — AND IF THEIR CLASS HAS RUN OUT, THE SISTER CLASSES.
//
// Measured after the first version: the pool works but it DRIES UP. Inventions from level 3 upward
// dropped from 112 to 52 in five winters, and the 52 that remain are classes whose own pool ran out. A
// sister class --same LEVEL and same FAMILY (`FamilyForClass`, the project's authored answer to «what
// car is this»)-- is people who race the same thing in another championship: a GT3 NIMBUS_MASTERS_5890 driver is good for GT3
// LANTERN_TROPHY_5644. Neither the level nor the discipline is relaxed; only the EXACT championship stops being required.
// ~~~29/08 — THOSE WHO HAVE JUST BECOME FREE, FIRST.
//
// User's request: «people who end up without a contract are more likely to chain contracts than the
// free agents that already exist». Being in the paddock counts. `seasons_idle` orders the queue -- 1 winter
// out comes in before 2 -- and `idle_from_series` gives their level and their discipline, so a GT driver doesn't
// reappear in a junior single-seater. They return to their level or below, never above: this is a second
// chance, not a disguised promotion.
std::string AgenteLibreQueVuelve(int nivelDestino, int afinDestino, const SaveData& save,
                                 const std::set<std::string>& ocupados,
                                 const std::set<std::string>& usadosYa) {
    const std::string* mejor = nullptr; int mejorIdle = 0;
    for (const auto& kv : save.seasons_idle) {                 // std::map -> deterministic order by id
        if (kv.second <= 0 || usadosYa.count(kv.first) || ocupados.count(kv.first)) continue;
        if (save.retired.count(kv.first)) continue;
        const auto de = save.idle_from_series.find(kv.first);
        if (de == save.idle_from_series.end()) continue;        // without an origin it isn't known whether they fit
        const SeriesDef* sd = SeriesById(de->second);
        if (!sd || sd->level < nivelDestino) continue;          // doesn't return ABOVE where they were
        if (!AffinityAllows(DriverAffinity(kv.first, std::string(), de->second), afinDestino)) continue;
        if (!mejor || kv.second < mejorIdle) { mejor = &kv.first; mejorIdle = kv.second; }
    }
    return mejor ? *mejor : std::string();
}

// ~~~02/09 (front 5) — THE RESERVE: 253 people with a card who race in no grid.
//
// Measured over twenty seasons: **232 stayed idle** and only 15 had ever got a seat, while
// the world invented 929 drivers. And they weren't missing `tier_max` as the list said: as soon as they sit down
// the engine assigns it (`tier + CASCADE_CLIMB_STEP`). What they lacked was someone LOOKING at them. `BaseTalent` returns
// 0 for them because talent is derived from where you race, and they don't race -- but all 232 bring affinity,
// which is all that's needed to know whether they fit here. Real names: Petter Solberg,
// Thomas Erdos, Fabrizio Ghezzi.
//
// They are consulted RIGHT BEFORE inventing anyone, so it is purely additive: the only slot that changes
// hands is one that was going to be occupied by a driver out of nowhere.

std::string ReservaLibrePara(int afinDestino, const SaveData& save,
                             const std::set<std::string>& ocupados,
                             const std::set<std::string>& usadosYa, int worldYear) {
    for (const auto& id : registry::Ids()) {          // lexicographic order -> deterministic
        const canon::DriverIdentity* di = registry::ById(id);
        if (!di || di->population_role != canon::PopRole::Reserve) continue;
        if (usadosYa.count(id) || ocupados.count(id) || save.retired.count(id)) continue;
        if (!ReservaEntraEnParrilla(id, worldYear)) continue;   // 23/09: only the curated reserve that is ACTIVE
        // 08/09: the reserve was the path by which 60-72 legends with the note «era»/«retired» (no known
        //  age) ended up at level 1 every five winters. MEASURED: closing it leaves 180 fewer seats
        //  with a real name (1259 -> 1083 real with a seat) that become generated (298 -> 483), 4 > 5 in two
        //  seeds. DECIDED by Josh on 08/09: `bal::POOL_ORIGIN_LEGENDS_STAY_OUT` ON -- «if they are era or
//  retired they shouldn't be there». The cost in generated drivers will be covered when the slot market lets
//  the 413 people with a curated origin at levels 2-7 compete for theirs.
        if (bal::POOL_ORIGIN_LEGENDS_STAY_OUT)
        if (const PoolOrigin* o = PoolOriginFor(id)) if (o->era) continue;
        if (!AffinityAllows(DriverAffinity(id, di->display_name, std::string()), afinDestino)) continue;
        return id;
    }
    return std::string();
}

std::string PersonaRealLibreDe(const std::string& className, int nivel,
                               const SaveData& save,
                               const std::set<std::string>& ocupados,
                               const std::set<std::string>& usadosYa, int worldYear = 0) {
    const std::string propia = PersonaRealLibreEn(className, save, ocupados, usadosYa, nivel, worldYear);
    if (!propia.empty()) return propia;
    const CarFamily fam = FamilyForClass(className);
    std::set<std::string> vistas;                 // std::set -> deterministic order
    for (const auto& sd : AllSeries()) {
        if (sd.level != nivel || sd.class_name == className) continue;
        if (FamilyForClass(sd.class_name) != fam) continue;
        vistas.insert(sd.class_name);
    }
    for (const auto& cls : vistas) {
        const std::string id = PersonaRealLibreEn(cls, save, ocupados, usadosYa, nivel, worldYear);
        if (!id.empty()) return id;
    }
    return std::string();
}

// 19/09 (Josh's decision, «real drivers from the pool»). When in the signing window nobody from
// the cohort is left for a slot, the slot is neither left with nobody nor handed to you: the team signs a REAL person
// from the pool. It is not a new chain: it is THE SAME one that fills the winter gaps in CascadeGenerateRookies,
// in the same order and with the same guards -- whoever has just become free (their level or below, their
// affinity), the real person whose curated origin or pack is this class or a sister (without those of their period or those
// too old), and the reserve (affinity, no legends). `ocupados` = whoever already has a live seat;
// `usadosYa` = whoever has already taken a slot in this window. Empty string = there really is nobody.
std::string PozoParaPlazaDeVentana(const SaveData& save, const SeriesDef& sd,
                                   const std::set<std::string>& ocupados,
                                   const std::set<std::string>& usadosYa, int worldYear) {
    if (!g_novatosDelPozo) return std::string();
    const int afin = DriverAffinity(std::string(), std::string(), sd.id);
    std::string real = AgenteLibreQueVuelve(sd.level, afin, save, ocupados, usadosYa);
    if (real.empty()) real = PersonaRealLibreDe(sd.class_name, sd.level, save, ocupados, usadosYa, worldYear);
    if (real.empty() && g_reservaEnParrilla) real = ReservaLibrePara(afin, save, ocupados, usadosYa, worldYear);
    return real;
}

// 19/09: the card with which a pool person enters the shortlist for a slot (RankSeatCandidates). Name,
// country, age and archetype come from the pool or the registry; their ELO, from the world, like any rival's.
RosterDriver RosterDelPozo(const std::string& id) {
    RosterDriver r;
    r.id = id;
    if (const PoolDriver* pd = DriverById(id)) { r.name = pd->name; r.country = pd->country; r.birth_year = pd->birth_year; }
    if (const canon::DriverIdentity* di = registry::ById(id)) {
        if (r.name.empty()) r.name = di->display_name;
        if (r.country.empty()) r.country = di->country;
        if (!r.birth_year) r.birth_year = di->birth_year;
        r.archetype = di->archetype;
    }
    if (r.name.empty()) r.name = DriverDisplayName(id);
    if (!r.birth_year) r.birth_year = BirthYearFor(id);
    return r;
}

// 19/09: what the winter fill does with whoever leaves the pool, when it really seats them: they stop being
// waiting and, if they have never raced, their listing ELO is re-anchored to the level of the seat they occupy (D4, «you are worth where
// you sit»), with the same seeding rule as the rest of the world.
void EntraDelPozo(SaveData& save, const std::string& key, const std::string& id, int nivel, int worldYear) {
    save.seasons_idle.erase(id);
    save.idle_from_series.erase(id);
    const auto rc = save.driver_races.find(id);
    const bool nuncaCorrio = (rc == save.driver_races.end() || rc->second <= 0);
    if (!save.driver_ratings.count(id) || nuncaCorrio) {
        const unsigned long semilla = seatFnv1a(std::to_string(save.career_seed) + "|" +
                                                std::to_string(worldYear) + "|" + key);
        save.driver_ratings[id] = LevelAnchoredSeedPct(nivel, TalentoNovato(semilla));
        save.seat_seeded.insert(id);   // 23/09: boot doesn't change it for them until they race (ReseedUnracedForBoot)
    }
}
}   // anon

int CascadeGenerateRookies(SaveData& save, int worldYear, std::vector<std::string>* nuevosOut,
                           const std::set<std::string>& delJugador, bool elHuecoGana) {
    if (worldYear <= 0) return 0;
    int creados = 0;
    // the pool of real people, and whoever is already placed. Once per pass.
    const std::set<std::string> ocupados = OcupadosAhora(save);
    std::set<std::string> delPozo;      // those this pass has already seated
    // Bottom-up: each generation enters through the bottom door. Walking tiers in ascending order
    // (and series in AllSeries order) makes the distribution deterministic and concentrates rookies where
    // they belong -- by design, they enter from the bottom (tiers 1-2), filling what the cascade left open.
    //
    // ~~~07/09 — BUT THAT ORDER ONLY HOLDS FOR THOSE WHO ARE INVENTED, and this loop serves TWO populations
    //  with opposite needs:
    //    · the INVENTED rookie has to enter from the bottom. For them, ascending is right.
    //    · the REAL person leaving the pool has to enter WHERE THEY ARE WORTH. And with the ascending loop,
    //      level 1 had first refusal over the whole pool every winter: the first one out was
    //      taken by the first slot of the base category, whatever they were worth, before anyone looked at
    //      whether there was a gap higher up.
    //
    //  Measured over twenty seasons before separating it: of 1,312 entries into the world, 1,251 (87 %) were
    //  through level 1, with a p90 entry ELO of 1,805 -- category 6 people landing in
    //  Formula Vee. Of the 118 who ended up at level 1 above their band, 99 had ARRIVED already high
    //  (and since then LOST points, i.e. they didn't grow there: they arrived badly placed). That is why
    //  by T20 level 1 was worth more than 2, 3 and 4.
    //
    //  And the fix is not a guard or a market rule -- it is THE ORDER, which is what the
    //  promotion cascade a few functions above already says: «TOP TO BOTTOM, and that order IS the mechanism».
    //  Two phases over the SAME body, so the guards can't be duplicated or diverge again:
    //    phase 0 · top to bottom, ONLY the pool: whoever is worth level 6 meets a level-6 vacancy
    //             first, and only drops if there really is nothing better -- which is when it is realistic.
    //    phase 1 · bottom to top, as always: what remains is filled, and people are invented at the bottom.
    for (int fase = 0; fase < 2; ++fase)
    for (int tier = fase ? 1 : bal::ROOKIE_MAX_TIER;
         fase ? tier <= bal::ROOKIE_MAX_TIER : tier >= 1;
         tier += (fase ? 1 : -1)) {
        for (const auto& sd : AllSeries()) {
            if (!EsMercadoVivo(sd) || sd.level != tier) continue;
            const std::vector<Seat> seats = SeatsForClass(sd.id, save);
            const ClassDemand exigencia = DemandForClass(sd.id, save, worldYear);
            const int shiftClase = ClassAgeShift(sd.class_name);
            for (size_t i = 0; i < seats.size(); ++i) {
                if (!seats[i].open) continue;
                const std::string key = SeatKey(sd.id, (int)i);
                if (delJugador.count(key)) continue;          // no rookie inherits your seat
                // The rookie's seed: the race, the year, and the seat. Two different save files generate
                // different people; the same save, regenerated, produces the same one.
                // 23/09 (, «a winter leaves three empty slots in Formula Vee»): the seed is (race, year,
                //  seat), so TWO rookies for the same seat in the same year came out with the SAME id — and the
                //  second was skipped as «already exists». It really happens: the winter calls this function twice with
                //  the same `worldYear` (before and after the seal), the seal promotes the rookie who had just been born
                //  and leaves the slot free, and the second pass couldn't fill it again. Same with the
                //  boot recovery (RecoverEmptySeats) and that same year's winter. If the id already exists it means
                //  that person is no longer in THIS slot (it is open), so another is drawn, deterministically.
                const std::string baseSemilla = std::to_string(save.career_seed) + "|" +
                                                std::to_string(worldYear) + "|" + key;
                unsigned long semilla = seatFnv1a(baseSemilla);
                for (int otra = 1; otra < 16 &&
                     save.gen_drivers.count("gn_" + std::to_string(worldYear) + "_" + std::to_string(semilla)); ++otra)
                    semilla = seatFnv1a(baseSemilla + "|" + std::to_string(otra));
                // Nationality comes from WHO RACES HERE, not from sd.region. The first version used
                // the series' region ("BR", "EU", "INT") against the pool's country ("BRA", "GBR"): they didn't
                // match, the name pool collapsed to three, and "Robby Habul" came out FOUR times in
                // a batch of twelve. The series grid's countries are also more faithful than
                // any region: a Brazilian series with two Argentines still keeps them.
                const std::string pais = PaisDeLaParrilla(sd.id, semilla);
                std::string nombre = NombreNovato(pais, semilla);
                if (nombre.empty()) continue;               // with no pool loaded, nothing gets invented
                const std::string id = "gn_" + std::to_string(worldYear) + "_" + std::to_string(semilla);
                if (save.gen_drivers.count(id)) continue;    // already exists (idempotent)

                SaveData::GenDriver g;
                g.name = nombre;
                g.country = pais;
                // Talent ON THE ANCHORED SCALE: the anchor of where they enter, plus their place within that grid.
                double t01 = TalentoNovato(semilla);
                double curvaArranque = 0.0, ratingArranque = 0.0;   // ~~~02/09: see the clone block
                g.talent = (tier - 1) * bal::RATING_TIER_STEP + t01 * bal::RATING_CLASS_SPREAD;
                // 14/08 — NAMED ARCHETYPES: talent is CLONED from the world instead of invented.
                //
                // The formula above caps the ceiling at the anchor of its entry level plus ONE class
                // spread, and that made it impossible for a generated driver to be a star: a level-1 rookie topped out
                // at 1287 and a level-7 one at 1602, while F1 runs between 1524 and 1669. Measured: over
                // twenty seasons the HEAD of the F1 grid fell 127 points and the tail only 77 —
                // when an ace retired he was replaced by someone who structurally couldn't be one.
                //
                // Now talent comes from the percentile that falls to their archetype over the LIVE corpus of
                // real talents (see career_growth.h). Without an archetype table or without a corpus it returns 0 and
                // the usual formula stays: additive, and absent data changes nothing.
                {
                    const GrowthArchetype* arq = nullptr;
                    // ~~~02/09 — CLONED AGAINST THEIR OWN STEP. Before, the whole ladder was
                    //  sampled and a karting kid started with the median talent of a level 5-6:
                    //  measured, level 1 ended up worth more than 2, 3 and 4 at twenty seasons.
                    // POTENTIAL AND STARTING POINT ARE TWO DIFFERENT NUMBERS, and confusing them was the
                    //  flaw. `potencial` comes from the GLOBAL corpus: your Verstappen is still a Verstappen.
                    //  `arranque` comes from the corpus of THEIR step: they are born worth what their category is worth.
                    //  The rating is `SEED_BOTTOM + talent x curve`, so by storing the potential as
                    //  talent and starting the curve at arranque/potencial you get exactly the number of
                    //  their step TODAY -- and they grow to what they can be. The best of GARROWBY_HEX394 this year, an F1 driver
                    //  in eight if they rise.
                    const double potencial = RookieTalentCloned(semilla, &arq, 0);
                    const double clonado   = RookieTalentCloned(semilla, &arq, tier);
                    if (clonado > 0.0) {
                        g.talent = (potencial > clonado) ? potencial : clonado;
                        curvaArranque = clonado / g.talent;
                        if (curvaArranque > 1.0) curvaArranque = 1.0;
                        if (curvaArranque < 0.05) curvaArranque = 0.05;
                        ratingArranque = clonado;
                        // AND THE CEILING COMES FROM THE PERCENTILE, not from the absolute value. It is the half that makes
                        //  this not close the upper door again: a generational clone born in
                        //  GT3 is worth what GT3 is worth TODAY, and still has a level-7 ceiling -- they get there
                        //  by racing, which is how you get there. With the absolute value their ceiling sank with it.
                        t01 = RookiePercentile(semilla);
                        if (t01 < 0.0) t01 = 0.0;
                        if (t01 > 1.0) t01 = 1.0;
                    }
                }
                // Their CEILING comes from that same talent: the one who enters strong goes far, the rest plateau.
                // It's what lets a rookie end up in the elite within ten seasons -- and what makes
                // almost none of them do.
                // ~~~02/09 — ROUNDS, DOESN'T TRUNCATE. With `+0.0001` the ceiling only reached 7 if the
                //  percentile was EXACTLY 1.000: an ace born at level 1 with percentile 0.99 got
                //  int(0.99 x 6) = 5, i.e. ceiling 6. No generated driver below level 7 could have
                //  ceiling 7, EVER. That is why 32-40 of the 44 born with ace potential stopped exactly
                //  at level 6 and zero set foot in the premier category in twenty seasons -- and why neither
                //  the market margin, nor growth, nor touching the passes of the
                //  promotion fixed it: their ceiling WAS 6. Measured in level-7 slots: 289,493 rejections by ceiling
                //  against 11,428 by bar.
                g.tier_max = tier + (int)(t01 * (double)(7 - tier) + 0.5);
                if (g.tier_max < tier) g.tier_max = tier;
                if (g.tier_max > 7) g.tier_max = 7;

                const int edad = bal::ROOKIE_AGE_MIN +
                                 (int)(seatFnv1a("edad|" + id) % (unsigned long)(bal::ROOKIE_AGE_MAX - bal::ROOKIE_AGE_MIN + 1));
                save.birth_year[id] = worldYear - edad;       // the canonical age map, not a copy

                // A rookie enters where they TRULY belong by level, on their own talent. And this line
                // has had two wrong versions, both caught by reading the transfer report:
                //
                //  (a) with no check at all, they were born below their class's bar and died the
                //      following season -- "Sebastien Dolhem, 19, Pemberton_Zel121 NIMBUS_MASTERS_5890 PINNACLE_CHALLENGE_5480: no longer good
                //      enough for any category".
                //  (b) raising their talent UP TO the bar removed any reason to
                //      prefer anyone from below: 14 of the 17 tier-7 seats across three seasons
                //      went to debutants, with 18-year-olds signing for Hendrick
                //      Motorsports. That's not a prodigy, that's a broken ladder.
                //
                // 17/09 — AND `elHuecoGana` IS THE DECLARED EXCEPTION TO THIS PARAGRAPH. The bar comes from
                // `DemandForClass`, which averages those who OCCUPY the class TODAY; in a drained class —the
                // school of an old save, with three survivors averaging 1,464— the bar stays
                // at 1,394 and a level-2 rookie tops out at 1,336. That is, the class demands more than its
                // own step can produce, and the gap NEVER closes: measured in Josh's
                // save, Granite Challenge had 17 of its 20 cars not going out on track. With the flag set, the
                // bar is lifted and the gap wins — which is LITERALLY the rule the end-of-season
                // seal already applies («the gap ALWAYS WINS», see SealLiveGrids). Production sets it ONLY in
                // the recovery pass of an old save; the normal winter keeps the bar.
                //
                // So it's checked, not corrected: if their power-law roll doesn't reach the
                // bar for THIS category, the seat isn't theirs -- it stays open and goes to whoever
                // moves up, or the player. At the top the bar is extremely high, so only someone who rolled an
                // enormous roll crosses it: sudden hidden talent is still possible, and it stays
                // exceptional, which is what makes it a story.
                if (exigencia.resolved && !elHuecoGana) {
                    const double mult = AgeCurveMultiplier(edad, shiftClase);
                    // ~~~02/09: they are judged by what they are worth TODAY (their starting point), not by their potential.
                    const double hoy = (ratingArranque > 0.0) ? ratingArranque : g.talent;
                    if (bal::RATING_SEED_BOTTOM + hoy * mult < exigencia.floor) continue;
                }
                // ~~~07/09 — SEEDING, IN A SINGLE PLACE, AND THIS IS THE ROOT OF THE LADDER BEING
                //  INVERTED. The two branches below —the real person leaving the pool and the rookie who is
                //  invented— have to give THE SAME ELO for the same seat; the comment
                //  four lines below says so and it was true until 02/09. That day POTENTIAL was separated from
                //  STARTING POINT for the generated driver (`ratingArranque`), and the real person's branch stayed on
                //  `g.talent`... which from then on IS the potential, sampled from the GLOBAL corpus.
                //  The result, measured over twenty seasons: 1,251 people entered level 1
                //  through the pool with median ELO 1410 and p90 1805, when the whole level-1 band runs from 975 to
                //  1325. They were born above the best driver in their category, every winter, and the excess
                //  leaked to the veterans through the duels (+406 ELO to those who had been there since T0).
                //  That is why by T20 level 1 was worth more than 2, 3 and 4.
                //  And why it wasn't seen at the time: a fix that lives in TWO places and is applied in one
                //  stays INERT without anything turning red. That is why it is now ONE expression.
                int tierSiembra = tier; if (tierSiembra < 1) tierSiembra = 1; if (tierSiembra > 7) tierSiembra = 7;
                const double eloDeEntrada = (ratingArranque > 0.0)
                    ? LevelAnchoredSeedPct(tierSiembra, t01)          // the SAME rule as reals and cohort
                    : bal::RATING_SEED_BOTTOM + g.talent;             // no percentile: the usual formula
                // ~~~29/08 — THE POOL FIRST, AND ONLY IF NOBODY IS LEFT IS ONE INVENTED.
                //
                // It is decided HERE, with the talent already computed, so that the real person enters with THE SAME
                // seeding the generated one would have had: otherwise a seat would give one ELO or another depending on who
                // occupied it and there would be two scales in the same grid again.
                //
                // It passes the SAME level guard as the generated one (a few lines above): a real person does not
                //  enter a slot where a rookie wouldn't fit. Conservative on purpose.
                if (g_novatosDelPozo) {
                    // whoever has just become free goes AHEAD of the pool: they were in the paddock.
                    const int afinAqui = DriverAffinity(std::string(), std::string(), sd.id);
                    std::string real = AgenteLibreQueVuelve(tier, afinAqui, save, ocupados, delPozo);
                    if (real.empty())
                        real = PersonaRealLibreDe(sd.class_name, tier, save, ocupados, delPozo, worldYear);
                    if (real.empty() && g_reservaEnParrilla)  // and the RESERVE, before inventing anyone
                        real = ReservaLibrePara(afinAqui, save, ocupados, delPozo, worldYear);
                    if (!real.empty()) {
                        delPozo.insert(real);
                        save.seasons_idle.erase(real);        // has a seat again
                        save.idle_from_series.erase(real);
                        // ~~~07/09 (D4, Josh's decision): YOU ARE WORTH WHERE YOU SIT. This only seeded
                        //  whoever had no rating, and 1,488 people in the pool ALREADY had one without ever having
                        //  raced: `SeedAllDrivers` gave it to them for appearing in a series' `field` (2,733
                        //  listed for 2,153 seats). Measured: 89 of 89 of those who landed at the base
                        //  above their band carried that listing ELO (median 1,716), and that is why the
                        //  ladder inverted. A rating with not a single race behind it is a SEEDING, not
                        //  history: it is re-anchored to the seat they actually occupy. Whoever returns with races
                        //  run (`driver_races` > 0) keeps theirs -- that one IS history.
                        { const auto rc = save.driver_races.find(real);
                          const bool nuncaCorrio = (rc == save.driver_races.end() || rc->second <= 0);
                          if (!save.driver_ratings.count(real) || nuncaCorrio) {
                              save.driver_ratings[real] = eloDeEntrada;
                              save.seat_seeded.insert(real);   // 23/09: and boot doesn't recompute it for them
                          } }
                        save.seat_occupant[key] = real;
                        NoteSeatContractStart(save, key, worldYear);
                        if (nuevosOut) nuevosOut->push_back(real);
                        creados++;
                        continue;                  // nobody is invented for this slot
                    }
                }
                // 07/09: phase 0 ONLY places people from the pool. If nobody came out here, the slot is
                //  left open and phase 1 picks it up -- the usual one, bottom to top.
                if (fase == 0) continue;
                save.gen_drivers[id] = g;
                RegisterGeneratedName(id, g.name, g.country);   // so the engine knows their name (and flag) right away
                // 11/08 — AND THEIR ELO, which until today nobody set. A generated rookie entered their
                // seat WITHOUT a rating, and only got one if the player ended up racing against them (the
                // lazy seeding of the settle). Until then their grid talent came from the static
                // curve while their rivals in the SAME car came from RatingQuality(ELO): it is the two
                // scales of [[two_talent_scales_in_one_grid]] again, now in the LIVE world and growing
                // with every season. Measured with `career_test --world`: at 20 seasons 942 of the 1,744
                // occupants were generated, i.e. more than half the world racing on another scale.
                //
                // They are placed within the band of THEIR level, by their own talent — not by their position in a
                // list. It is what wants for everyone: the number belongs to the person.
                // `g.talent` is NOT 0..1: it is a talent ON THE ANCHORED SCALE (tier anchor + their place in
                // that grid), the same one `BaseTalent` uses. Treating it as a fraction multiplies by
                // 350 a number already worth ~1500 — tried and caught on the spot by `--world`, which produced
                // ELOs of 119,958 at 20 seasons. The correct conversion is the SAME one the
                // eligibility guard uses three lines above: RATING_SEED_BOTTOM + talent.
                // ~~~02/09: with potential and starting point separated, the entry ELO is that of THEIR step
                //  (`ratingArranque`), and the curve stores how much they have left to grow. Without a clone (no
                //  archetype table or no corpus) `ratingArranque` is 0 and the usual number stays.
                if (curvaArranque > 0.0) save.driver_curve[id] = curvaArranque;
                // ~~~07/09 — AND THEIR ELO COMES FROM THE SAME RULE AS EVERYONE ELSE'S.
                //  This was `RATING_SEED_BOTTOM + talento`: a flat line with base 1200, while the
                //  reals and the cohort are seeded with `LevelAnchoredSeed`, which anchors at the midpoint of
                //  THEIR level. Two rules for the same number, and it showed exactly where it hurts most: the
                //  level-1 band ends at 1325 and the rookie was born with a median of 1408, i.e. above the best
                //  driver in their category -- over half a class, every winter. The deviation measured over twenty
                //  seasons was +251 at level 1, +127 at 2, +33 at 3 and negative going up: flat
                //  versus ladder. That is why level 1 ended up worth more than 2, 3 and 4.
                //  And the excess didn't stay with the rookie: it leaked to the veterans through the duels.
                //  The ten who had been at level 1 since T0 gained +406 ELO without moving seat.
                //  It does NOT change what was decided on 02/09: the STARTING POINT is their step's and the POTENTIAL is still
                //  theirs (`g.talent` is not touched, and `driver_curve` stores how much they have left to grow).
                //  The only thing that changes is which rule expresses the starting point. Without a percentile (no archetype table
                //  or corpus) the usual formula stays: additive, and absent data doesn't
                //  change anything.
                if (!save.driver_ratings.count(id)) {
                    save.driver_ratings[id] = eloDeEntrada;
                    save.seat_seeded.insert(id);   // 23/09: without this the next boot left them without a rating
                }
                Ruta(id, worldYear, "nace", 0, tier);
                save.seat_occupant[key] = id;
                NoteSeatContractStart(save, key, worldYear);
                if (nuevosOut) nuevosOut->push_back(id);
                creados++;
            }
        }
    }
    return creados;
}

// 17/09 — THE CARS THAT DIDN'T GO OUT ON TRACK. See the header in career_seats.h.
//
// What was measured in Josh's save (200 races, 8 titles, coming from 0.6.8): 79 seats with the
// `kSeatOpen` mark set and no mechanism to fill them again. The grid is composed FROM the
// seats (), so an empty seat is a car that doesn't go out: Granite Challenge wrote 3 of 20 cars,
// Falcon_Cup_6341 10 of 24, Keystone Series 11 of 20, CINDER CUP 21 of 29 — 18 classes below their cap.
//
// WHY IT DIDN'T FIX ITSELF: the fill lives in the winter (`RolloverYearReport` -> cascade, seal,
// cascade), and a mid-season save doesn't get there until the year ends. Also 0.6.8
// had no seal, so those gaps are ACCUMULATED debt from earlier seasons, not just the last one.
//
// WHY THE NORMAL CASCADE ISN'T ENOUGH: in drained classes the `DemandForClass` bar is set by the
// survivors and sits above what that step can produce. Measured: with the bar on, 49 of the 79
// get filled; with both passes, 67 — and the 18 short classes go down to 1 (the one left is
// Falcon_Cup_10154, which has 29 seats for a curated grid size of 32: that is content already).
//
// IT MOVES NOBODY. It only writes `seat_occupant` of OPEN slots: no incumbent changes car,
// no identity changes and no seat key is reinterpreted. The seal (`SealLiveGrids`) reached the
// same result in the XML but moving 138 slots and evicting 12 people, which mid-season is
// a removal from the world that nobody asked for.
// 23/09 (): see career_seats.h.
int RecoverEmptySeatsForBoot(SaveData& save, int worldYear, const std::set<std::string>& delJugador) {
    const int llenadas = RecoverEmptySeats(save, worldYear, delJugador);
    if (llenadas > 0) ReseedUnracedForBoot(save);
    return llenadas;
}

// 23/09: see career_seats.h.
int DropUnracedSeedsForBoot(SaveData& save) {
    // the mark only makes sense while they haven't raced, are still in the world and have the number they were given
    for (auto it = save.seat_seeded.begin(); it != save.seat_seeded.end(); ) {
        const auto rc = save.driver_races.find(*it);
        const bool corrio = (rc != save.driver_races.end() && rc->second > 0);
        if (corrio || save.retired.count(*it) || !save.driver_ratings.count(*it)) it = save.seat_seeded.erase(it);
        else ++it;
    }
    return DropUnracedSeeds(save.driver_ratings, save.driver_races, save.seat_seeded);
}

int ReseedUnracedForBoot(SaveData& save) {
    const int borrados = DropUnracedSeedsForBoot(save);
    SeedAllDrivers(save.driver_ratings);
    return borrados;
}

bool HasDrainedClass(const SaveData& save) {
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        if (seats.size() < 3) continue;
        int abiertos = 0;
        for (const auto& s : seats) if (s.open) abiertos++;
        if (abiertos * 3 >= (int)seats.size()) return true;
    }
    return false;
}

int RecoverEmptySeats(SaveData& save, int worldYear, const std::set<std::string>& delJugador) {
    if (worldYear <= 0) return 0;
    int n = CascadeGenerateRookies(save, worldYear, nullptr, delJugador, /*elHuecoGana*/false);
    n += CascadeGenerateRookies(save, worldYear, nullptr, delJugador, /*elHuecoGana*/true);
    return n;
}

int PruneGenDrivers(SaveData& save) {
    if (save.gen_drivers.empty()) return 0;
    std::set<std::string> vivos;
    for (const auto& kv : save.seat_occupant)
        if (!kv.second.empty() && kv.second != kSeatOpen) vivos.insert(kv.second);
    int podados = 0;
    for (auto it = save.gen_drivers.begin(); it != save.gen_drivers.end(); ) {
        if (vivos.count(it->first)) { ++it; continue; }
        // Doesn't occupy a seat. If they're also retired, they're nobody anymore: out of the save.
        if (!save.retired.count(it->first)) { ++it; continue; }
        save.birth_year.erase(it->first);
        it = save.gen_drivers.erase(it);
        podados++;
    }
    return podados;
}

// THE WORLD CENSUS (see career_seats.h for why).
std::vector<RosterDriver> WorldOccupants(const SaveData& save) {
    std::map<std::string, RosterDriver> porId;      // std::map: deterministic traversal and output by id
    // The authored cohort, by id. All 28 rows carry tier_min = 1, so RosterForTier(1) brings them all.
    std::map<std::string, RosterDriver> elenco;
    for (const auto& rd : RosterForTier(1)) if (!rd.id.empty()) elenco[rd.id] = rd;
    for (const auto& sd : AllSeries()) {
        if (!EsMercadoVivo(sd)) continue;         // Watch out: an era that never rotates doesn't enter the census
        const int tier = (sd.level >= 1 && sd.level <= 7) ? sd.level : 1;
        const int shiftClase = ClassAgeShift(sd.class_name);

        // Records by id with the SAME PRECEDENCE SeatsForClass uses to pick the occupant: pinned
        // first, then the series field, then the cohort last. That way the skill attributed to
        // a driver comes from the same data that put them there.
        std::map<std::string, std::pair<double, std::string>> ficha;   // id -> (skill, name)
        for (const auto& pd : PinnedRosterForSeries(sd.id)) {
            std::string id = pd.id.empty() ? DriverIdForName(pd.name, pd.country) : pd.id;
            if (id.empty()) id = std::string("rd_") + std::to_string(seatFnv1a(pd.name));
            ficha[id] = std::make_pair(pd.skill, pd.name);
        }
        for (const auto& f : FieldForSeries(sd.id)) {
            const std::string id = FieldOccupantId(f);
            if (!id.empty()) ficha[id] = std::make_pair(f.skill > 0.0 ? f.skill : 0.85, f.name);
        }
        for (const auto& rd : RosterForTier(tier)) ficha[rd.id] = std::make_pair(rd.base_skill, rd.name);

        for (const auto& s : SeatsForClass(sd.id, save)) {
            if (s.open || s.occupant_id.empty()) continue;
            auto it = porId.find(s.occupant_id);
            if (it != porId.end()) {                 // already counted in another series: just widen their band
                if (tier < it->second.tier_min) it->second.tier_min = tier;
                if (tier > it->second.tier_max - bal::CASCADE_CLIMB_STEP)
                    it->second.tier_max = tier + bal::CASCADE_CLIMB_STEP;
                // races in multiple classes -> the MOST TOLERANT one wins. A single-seater veteran who
                // also holds an endurance seat keeps racing there once they no longer race here:
                // they stop existing for the world only once NO category is left, not the first one.
                if (shiftClase > it->second.age_shift) it->second.age_shift = shiftClase;
                if (tier > it->second.tier_now) it->second.tier_now = tier;   // their HIGHEST slot today
                continue;
            }
            RosterDriver r;
            r.id = s.occupant_id;
            // A generated rookie isn't in any series record -- their record IS the save.
            // Without this they'd enter with an empty name, no age, and the ceiling of their current tier -- meaning
            // they could never move up, and their own power-law roll would count for nothing.
            auto gd = save.gen_drivers.find(s.occupant_id);
            if (gd != save.gen_drivers.end()) {
                r.name = gd->second.name;
                r.country = gd->second.country;
                r.tier_min = tier;
                r.tier_max = gd->second.tier_max > tier ? gd->second.tier_max : tier;
                r.age_shift = shiftClase;
                r.tier_now  = tier;                  // the slot they occupy TODAY (see RosterDriver::tier_now)
                auto by2 = save.birth_year.find(s.occupant_id);
                r.birth_year = (by2 != save.birth_year.end()) ? by2->second : 0;
                porId[r.id] = r;
                continue;
            }
            auto fi = ficha.find(s.occupant_id);
            r.base_skill = (fi != ficha.end()) ? fi->second.first : 0.85;
            r.name       = (fi != ficha.end()) ? fi->second.second : std::string();
            if (r.name.empty()) { const PoolDriver* pd = DriverById(r.id); if (pd) r.name = pd->name; }
            // 18/09 — and if it still has no name, the FULL chain. `ficha` is built per SERIES, so whoever
            // the market brought from another class isn't known to it; and `DriverById` only looks at the pool. Without this
            // step, the world census returned nameless people and the end-of-season report printed
            // the raw id (`rd_1045361494`, `fl_aurora_series_9047#13`). `DriverDisplayName` covers both families:
            // the tail-generated ones through `EnsureTailNames` and the curated grid through `EnsureRosterNames`.
            if (r.name.empty()) {
                const std::string n = DriverDisplayName(r.id);
                if (!n.empty() && n != r.id) r.name = n;
            }
            r.birth_year  = BirthYearFor(r.id);      // the age seeded earlier, finally read back
            r.retire_year = RetireYearFor(r.id);     // icon with an authorized year -> ShouldRetire honors it
            r.tier_min    = tier;
            r.tier_max    = tier + bal::CASCADE_CLIMB_STEP;
            // 17/09 — THE COHORT'S AUTHORED CEILING GOT HERE AND WAS THROWN AWAY ON THE WAY.
            //
            // `career_balance.h` (CASCADE_CLIMB_STEP) says in so many words that «whoever DOES have an arc
            // upward is the cohort: its tier_max comes authored in series_roster.psv». It is true in the
            // rollover cast, and it was FALSE here — and this census is what feeds the real promotion cascade
            // (`CascadePromoteToOpenSeats`, `ReplaceWeakIncumbents`). An `rk_*` fell into this generic
            // branch and came out with `tier_max = tier_now + 1`, so Lucía Vega, with a 7 written by hand in
            // the data, entered the cascade with a ceiling of 2 and got stuck at level 3 at twenty seasons.
            // The generated rookies DO have their branch, three lines above, and read their ceiling from the save; your
            // generation, which is the ONLY one with a hand-authored ceiling, had none.
            //
            // Only the CEILING is restored. The FLOOR stays as in the rest of the census —the lowest slot they
            // occupy today—, because that is what the cascade understands by «where they climb from».
            { const auto ck = elenco.find(s.occupant_id);
              if (ck != elenco.end() && ck->second.tier_max > r.tier_max) r.tier_max = ck->second.tier_max; }
            r.age_shift   = shiftClase;
            r.tier_now    = tier;                    // the slot they occupy TODAY (see RosterDriver::tier_now)
            porId[r.id] = r;
        }
    }
    std::vector<RosterDriver> out;
    out.reserve(porId.size());
    for (auto& kv : porId) {
        if (kv.second.tier_max > 7) kv.second.tier_max = 7;
        if (kv.second.tier_min < 1) kv.second.tier_min = 1;
        out.push_back(kv.second);
    }
    return out;
}

void ApplyRolloverSeatChanges(SaveData& save,
                              const std::set<std::string>& retiredIds,
                              const std::map<std::string,int>& promotedToTier,
                              const std::vector<RosterDriver>& climbers,
                              int worldYear,
                              const std::set<std::string>& delJugador) {
    if (retiredIds.empty() && promotedToTier.empty()) return;   // nothing freed -> no open-seat changes

    // Pass 1 — RETIREMENTS: a seat whose CURRENT occupant retired this rollover is freed (per-team granularity).
    // A class with no retiree among its occupants gets no open seat (fully-occupied -> nothing recorded).
    if (!retiredIds.empty()) {
        for (const auto& sd : AllSeries()) {
            if (!EsMercadoVivo(sd)) continue;             // no historic/era seats in the modern market
            std::vector<Seat> seats = SeatsForClass(sd.id, save);
            for (size_t i = 0; i < seats.size(); ++i) {
                if (seats[i].occupant_id.empty() || !retiredIds.count(seats[i].occupant_id)) continue;
                // The PLAYER's seat is never freed, even if the driver the data places there has
                // retired -- that seat belongs to the player, not to them. The seat model doesn't know that, so
                // it has to be told explicitly, by key.
                if (delJugador.count(SeatKey(sd.id, (int)i))) continue;
                save.seat_occupant[SeatKey(sd.id, (int)i)] = kSeatOpen;   // free THIS team's seat
            }
        }
    }

    // Pass 2 -- PROMOTIONS: those promoted take the open seats in their tier. Resolve seats AFTER
    // pass 1, so the open set already reflects the retirements.
    //
    // -- the cohort competes TOO. This used to be "grab the first open seat in your tier",
    // walking `promotedToTier` in key order: the alphabetically lowest id got the best team, and
    // nothing about the world decided the outcome. Now every team with an open seat SCORES every candidate with the
    // same TeamCandidateFit it uses for you (with its personality, which is what makes one team prefer the young
    // driver and another the fast one) and assigns from highest score to lowest. Every candidate is considered for ALL seats in their tier,
    // so the same greedy fill still ends up filling exactly the same seats as before: only WHO changes.
    std::set<std::string> placedDriver;   // ids already placed (on merit) -- the rest falls back to the old distribution
    if (!climbers.empty()) {
        struct OpenSlot { std::string key, teamLabel, className; int tier; };
        std::vector<OpenSlot> slots;
        for (const auto& sd : AllSeries()) {
            if (!EsMercadoVivo(sd)) continue;
            std::vector<Seat> seats = SeatsForClass(sd.id, save);
            for (size_t i = 0; i < seats.size(); ++i)
                if (seats[i].open) {
                    if (delJugador.count(SeatKey(sd.id, (int)i))) continue;   // your seat is never redistributed
                    OpenSlot s;
                    s.key = SeatKey(sd.id, (int)i);
                    s.teamLabel = seats[i].team_label; s.className = sd.class_name; s.tier = sd.level;
                    slots.push_back(s);
                }
        }
        struct Bid { double fit; std::string slotKey, driverId; };
        std::vector<Bid> bids;
        for (const auto& kv : promotedToTier) {
            const RosterDriver* rd = nullptr;
            for (const auto& c : climbers) if (c.id == kv.first) { rd = &c; break; }
            if (!rd) continue;                               // without a record there's nothing to score -> falls back to the old distribution
            for (const auto& s : slots) {
                if (s.tier != kv.second) continue;
                // teamCountry "": an OPEN seat has no incumbent to draw a country from (the market
                // is derived from it); making one up would bias everyone's natMatch equally, so it's left neutral.
                CandidateFactors f = CohortCandidateFactors(*rd, save, s.className, s.teamLabel, "", worldYear);
                Bid b;
                b.fit = TeamCandidateFit(f, s.teamLabel, rd->id, worldYear);
                b.slotKey = s.key; b.driverId = rd->id;
                bids.push_back(b);
            }
        }
        // score descending; ties -> seat then driver, so the distribution doesn't depend on construction order
        std::sort(bids.begin(), bids.end(), [](const Bid& a, const Bid& b) {
            if (a.fit != b.fit) return a.fit > b.fit;
            if (a.slotKey != b.slotKey) return a.slotKey < b.slotKey;
            return a.driverId < b.driverId;
        });
        std::set<std::string> takenSlot;
        for (const auto& b : bids) {
            if (takenSlot.count(b.slotKey) || placedDriver.count(b.driverId)) continue;
            save.seat_occupant[b.slotKey] = b.driverId;      // the team keeps whoever fits it BEST
            // A NEW contract -> its start year is this one, and its expiry follows from that.
            // Without this, the newcomer would inherit the departing driver's expiry and would come back onto
            // the market the very next year.
            NoteSeatContractStart(save, b.slotKey, worldYear);
            takenSlot.insert(b.slotKey);
            placedDriver.insert(b.driverId);
        }
    }

    // Old distribution for anyone who couldn't be scored (no `climbers`, or a promoted driver not in the
    // list): the first open seat in their tier. It's there so nobody is left unplaced, not as the general rule.
    for (const auto& kv : promotedToTier) {
        const std::string& pid = kv.first;
        if (placedDriver.count(pid)) continue;
        int destTier = kv.second;
        if (destTier < 1 || destTier > 7) continue;
        bool placed = false;
        for (const auto& sd : AllSeries()) {
            if (placed) break;
            if (!EsMercadoVivo(sd)) continue;
            if (sd.level != destTier) continue;              // only the climber's destination tier
            std::vector<Seat> seats = SeatsForClass(sd.id, save);
            for (size_t i = 0; i < seats.size(); ++i) {
                if (seats[i].open) {
                    if (delJugador.count(SeatKey(sd.id, (int)i))) continue;   // your seat is never redistributed
                    save.seat_occupant[SeatKey(sd.id, (int)i)] = pid;   // AI re-fills the freed seat
                    NoteSeatContractStart(save, SeatKey(sd.id, (int)i), worldYear);   // A new contract
                    placed = true;
                    break;
                }
            }
        }
    }
}

// ELO displacement predicate. True iff the player's aged rating EXCEEDS the weakest
// in-reach incumbent's by at least bal::DISPLACEMENT_MARGIN — i.e. the player is clearly stronger than the
// team's backmarker and can "take his place". Pure, deterministic, no RNG (mirrors the PromoteFreedSeats /
// ShouldRetire pure-predicate shape). A low-rated player (rating below or merely within the margin of the
// weakest incumbent) gets NO displacement. The strict > with the margin makes "within the margin" non-
// displacing (a marginal edge over a backmarker is not enough to oust him — realism: you must clearly beat him).
// The AI's contract (see career_seats.h for why) ------------------------

std::string SeatKeyLegacy(const std::string& seriesId, int seatIdx) {
    return seriesId + "#" + std::to_string(seatIdx);
}

std::string SeatLiveryForIndex(const std::string& seriesId, int seatIdx) {
    if (seatIdx < 0) return std::string();
    // THE SAME list SeatsForClass uses, and that is why it comes out of the same function. Before there were two
    // rules written separately («pinned -> its livery», «if not -> catalog[i]») that agreed as long as the
    // pinned one was the only one that gave slots; as soon as a pinned series has cars beyond the
    // roster, one said "no livery" and the other gave it one — and the seat key split in two.
    const std::vector<std::string> liv = SeatLiveriesForSeries(seriesId);
    if (seatIdx < (int)liv.size()) return liv[seatIdx];
    // Outside the list: the usual behavior, which is what old saves read.
    if (PinnedRosterSizeFor(seriesId) > 0) return PinnedLiveryAt(seriesId, seatIdx);
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return std::string();
    return LiveryForClassAt(sd->class_name, seatIdx);
}

std::string SeatSlotKey(const std::string& seriesId, int seatIdx) {
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return std::string();
    const std::string liv = SeatLiveryForIndex(seriesId, seatIdx);
    if (liv.empty()) return std::string();
    int veh = 0, nn = -1;
    if (!LiveryVehicleFor(sd->class_name, liv, veh, nn)) return std::string();
    if (veh == 0 || nn < 0) return std::string();
    return seriesId + "|" + std::to_string(veh) + "|" + std::to_string(nn);
}

std::string SeatKey(const std::string& seriesId, int seatIdx) {
    // The stable one if there is one; if not, the usual. Never empty: a seat without a key would be a seat the
    // live world can't occupy.
    // 16/09 — the comment said «there are 4 modern classes and 187 period ones whose slot doesn't resolve today» and it is no longer
    // true: measured in the suite, the **2122** seats of the 118 series resolve a slot and **none** falls
    // to the legacy form. The fallback stays here because it is the point-5 safety net and because the data may have gaps
    // again, not because it is used today.
    std::string slot = SeatSlotKey(seriesId, seatIdx);
    return slot.empty() ? SeatKeyLegacy(seriesId, seatIdx) : slot;
}

// 0.7.0 port (F12) — IN A 0.6.8 SAVE, `<series>#<i>` IS A PERSON, NOT AN INDEX.
//
// The index i was row i of the pinned roster of 0.6.8. 0.7.0 reorders series_rosters.psv (the order of
// f0, which fixes the seeding and who races), so resolving that index against today's roster seated the
// market signing on another person. docs/career/seat_index_068.psv freezes, for the 7 series with a market
// whose order changed, who was at each index and with which livery (tools/gen_seat_index_068.py). It is read only here.
namespace {
// 17/09 — the table also carries NAME and COUNTRY, and until today they were thrown away when reading it. See `Identidades068`
// at the end of this block for why they are needed.
struct Asiento068 { std::string persona, librea, nombre, pais; };

// 16/09 (H2 of the review) — the table says, in its own header, HOW MANY series and how many rows it
// has to bring («# ESPERADO: series=N filas=M», written by gen_seat_index_068.py). If what is loaded doesn't add up,
// the table arrived incomplete or truncated at the install and MigrateSeatKeys does NOT migrate nor seal the epoch: sealing
// with a half table would leave the failure PERMANENT, because a later load with the whole file would no longer
// have legacy keys to migrate.
bool g_tabla068Completa = false;

const std::map<std::string, std::vector<Asiento068>>& SeatIndex068() {
    static std::string ruta;
    static std::map<std::string, std::vector<Asiento068>> tabla;
    const std::string r = paths::Data("career/seat_index_068.psv");
    if (r == ruta) return tabla;                  // one read per data root (the suite changes root)
    tabla.clear();
    g_tabla068Completa = false;
    FILE* f = paths::FOpen(r.c_str(), "rb");
    // The load failure is NOT memoized: without setting `ruta`, the next call tries again. Before it was
    // memoized, so a file that arrived late (or a data root that was mounted later) left the
    // table empty for the whole session.
    if (!f) return tabla;
    ruta = r;
    int esperaSeries = -1, esperaFilas = -1, filas = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') {
            if (esperaSeries < 0) {
                const char* p = strstr(line, "ESPERADO: series=");
                if (p) sscanf(p, "ESPERADO: series=%d filas=%d", &esperaSeries, &esperaFilas);
            }
            continue;
        }
        const std::vector<std::string> t = seatSplitPipe(line);
        if (t.size() < 4 || t[0].empty()) continue;
        std::vector<Asiento068>& v = tabla[t[0]];
        const size_t i = (size_t)atoi(t[1].c_str());
        if (v.size() <= i) v.resize(i + 1);
        v[i] = Asiento068{ t[2], t[3], t.size() > 4 ? t[4] : std::string(),
                                       t.size() > 5 ? t[5] : std::string() };
        filas++;
    }
    fclose(f);
    g_tabla068Completa = esperaSeries > 0 && esperaFilas > 0 &&
                         (int)tabla.size() == esperaSeries && filas == esperaFilas;
    return tabla;
}

// 16/09 — does this series have a frozen 0.6.8 table? ApplySeatOverrides asks it to know whether a
// legacy key of its own can be an orphan (and then not read it) or was written by 0.7.0 itself because its
// slot doesn't resolve (and then it has to keep being read).
bool SerieConTabla068(const std::string& seriesId) {
    const auto& t = SeatIndex068();
    return t.find(seriesId) != t.end();
}

// 0.6.8 index -> TODAY's livery of the same person (the k-th time it appears, in case the data brings it
// repeated) -> and if it is no longer in the roster, their livery from back then if the series still runs it. Empty if it doesn't resolve.
// `porPersona` (optional) says which of the two paths resolved: MigrateSeatKeys needs it to give priority
// to the person (see the double-pass note there).
std::string LiveryFrom068(const SeriesDef& sd, const std::vector<Asiento068>& viejos, int i,
                          bool* porPersona = nullptr) {
    if (porPersona) *porPersona = false;
    const std::string& persona = viejos[i].persona;
    int k = 0;
    for (int j = 0; j < i; j++) if (viejos[j].persona == persona) k++;
    for (const auto& pd : PinnedRosterForSeries(sd.id)) {
        if (persona.empty() || PinnedOccupantId(pd) != persona) continue;
        if (k-- == 0) { if (porPersona) *porPersona = true; return pd.livery_name; }
    }
    const std::vector<std::string> hoy = SeatLiveriesForSeries(sd.id);
    if (std::find(hoy.begin(), hoy.end(), viejos[i].librea) != hoy.end()) return viejos[i].librea;
    return std::string();
}

// The slot of that livery. Empty if it doesn't resolve: the key stays as a counted orphan, same as by index.
std::string SlotKeyFrom068(const SeriesDef& sd, const std::vector<Asiento068>& viejos, int i,
                           bool* porPersona = nullptr) {
    const std::string librea = LiveryFrom068(sd, viejos, i, porPersona);
    int veh = 0, nn = -1;
    if (librea.empty() || !LiveryVehicleFor(sd.class_name, librea, veh, nn) || veh == 0 || nn < 0) return std::string();
    return sd.id + "|" + std::to_string(veh) + "|" + std::to_string(nn);
}

// The player's contract (and the substitution) store the seat as a live INDEX, not as a key: PlayerSeatKeys,
// the non-renewal, TeammateSeatIndex and subride resolve it against today's list. In a 0.6.8 save
// that index is the one from back then; it is translated to the same person's position today. -1 if it doesn't resolve.
int SeatIndexFrom068(const SeriesDef& sd, const std::vector<Asiento068>& viejos, int i) {
    if (i < 0 || i >= (int)viejos.size()) return -1;
    const std::string librea = LiveryFrom068(sd, viejos, i);
    if (librea.empty()) return -1;
    const std::vector<std::string> hoy = SeatLiveriesForSeries(sd.id);
    const auto it = std::find(hoy.begin(), hoy.end(), librea);
    return (it == hoy.end()) ? -1 : (int)(it - hoy.begin());
}
}   // namespace

// 01/10 (hewo): here and not next to RecoverEmptySeats because it needs SeatIndex068() and g_tabla068Completa, which live
// in the anonymous namespace above.
bool SeatMigrationPending(const SaveData& save) {
    if (save.seat_key_epoch != 0) return false;            // already migrated (or born with today's keys)
    SeatIndex068();                                         // leaves g_tabla068Completa up to date for this data root
    if (g_tabla068Completa) return false;                   // with a table, the load already migrated or will migrate now
    // is there any legacy key ("series#n") left to migrate? With none, the 0 means "not needed".
    for (const auto& kv : save.seat_occupant)
        if (kv.first.find('#') != std::string::npos && kv.first.find('|') == std::string::npos) return true;
    return false;
}


// 17/09 — THE FROZEN TABLE ALSO KNOWS WHAT PEOPLE ARE CALLED, AND IT WAS THE ONLY PLACE THAT DID.
//
// It came out of measuring the real eight-title save: a car didn't reach the XML of Pinnacle_Masters, and it was
// `rd_kevin_magnussen`. His seat is written in the save —the market signed him there in its day— but
// 0.7.0 removed him from the live data, so none of the six steps of `PersonForOccupant` knew his
// name anymore: the row was thrown away whole and AMS2 put a generic in his place.
//
// The 0.6.8 table brings his name and country (`Kevin Magnussen|DNK`) because they froze them with everything
// else. They were read and thrown away on load. This keeps them and offers them as the LAST source of identity,
// which is exactly what they are: the census of who existed when that save was written.
//
// Its scope is the table's —9 series and 212 rows of 0.6.8— and not one more. It is not a new pool of
// people: it is the memory of the previous version, so that a person YOU have seated doesn't become
// a gap because today's data no longer carries them. Without a country they are not registered, for the same reason as the
// rest of the global index (AMS2 silently discards a `<country>` it doesn't know).
const std::map<std::string, std::pair<std::string, std::string> >& Identidades068() {
    static std::map<std::string, std::pair<std::string, std::string> > idx;
    static const std::map<std::string, std::vector<Asiento068> >* visto = nullptr;
    const std::map<std::string, std::vector<Asiento068> >& tabla = SeatIndex068();
    if (visto == &tabla && !idx.empty()) return idx;      // the table reloads itself when the root changes
    idx.clear();
    visto = &tabla;
    for (const auto& kv : tabla)
        for (const auto& a : kv.second) {
            if (a.persona.empty() || a.nombre.empty() || a.pais.empty()) continue;
            idx.insert(std::make_pair(a.persona, std::make_pair(a.nombre, a.pais)));
        }
    return idx;
}

// 23/09: see career_seats.h.
int RepairDowngradedSeatKeys(SaveData& save, int* recovered) {
    int tocadas = 0, recuperadas = 0;
    auto esSerieASecas = [](const std::string& k) {
        return !k.empty() && k.find('|') == std::string::npos && k.find('#') == std::string::npos &&
               SeriesById(k) != nullptr;
    };
    std::vector<std::string> malas;
    for (const auto& kv : save.seat_occupant) if (esSerieASecas(kv.first)) malas.push_back(kv.first);
    for (const auto& serie : malas) {
        const std::string pegado = save.seat_occupant[serie];
        save.seat_occupant.erase(serie);
        tocadas++;
        // the right slot is the one that, without its '|', is exactly the numeric prefix stuck to the id
        std::string buena, quien; int encajan = 0;
        const size_t n = SeatsForClass(serie, save).size();
        for (int i = 0; i < (int)n; i++) {
            const std::string k = SeatKey(serie, i);
            if (k.size() <= serie.size() + 1 || k.compare(0, serie.size() + 1, serie + "|") != 0) continue;
            std::string cifras;
            for (size_t c = serie.size() + 1; c < k.size(); c++) if (k[c] != '|') cifras += k[c];
            if (pegado.size() <= cifras.size() || pegado.compare(0, cifras.size(), cifras) != 0) continue;
            const std::string resto = pegado.substr(cifras.size());
            if (resto.empty() || (resto[0] >= '0' && resto[0] <= '9')) continue;   // the id never starts with a digit
            if (k == buena) continue;
            buena = k; quien = resto; encajan++;
        }
        if (encajan == 1 && !save.seat_occupant.count(buena)) { save.seat_occupant[buena] = quien; recuperadas++; }
    }
    for (auto it = save.seat_contract_start.begin(); it != save.seat_contract_start.end(); ) {
        if (esSerieASecas(it->first)) { it = save.seat_contract_start.erase(it); tocadas++; }
        else ++it;
    }
    if (recovered) *recovered = recuperadas;
    return tocadas;
}

int MigrateSeatKeys(SaveData& save, int* orphans) {
    int movidas = 0, huerfanas = 0;
    // 16/09 (M1 of the review) — EVERYTHING THAT MIGRATES, UNDER THE SAME LOCK. Before, the epoch lock
    // protected ONLY the contract and substitution index; the key walk ran on all
    // loads, with the 0.6.8 table in place. And 0.7.0 DOES write `series#i` keys on its own —
    // `SeatKey` falls back to the legacy form when the slot doesn't resolve —, so on the next load they
    // were reinterpreted as «the person in row i of 0.6.8». Today it self-stabilizes (if the slot didn't
    // resolve on write it doesn't resolve on migrate either), but all it takes is for the resolution to change between two
    // boots — a DLC, a livery curation — for a mark born in 0.7.0 to move to another person's
    // car. It's the same reasoning that already justified the contract's lock.
    if (save.seat_key_epoch != 0) { if (orphans) *orphans = 0; return 0; }
    // It is walked per SERIES and per index, not by parsing the keys: parsing would mean trusting the format,
    // and resolving by (series, index) uses exactly the same function that will later do the reads. That way the
    // migration and the lookup can't disagree.
    std::map<std::string, std::string> occ;
    std::map<std::string, int>         start;
    std::set<std::string> tratadas;
    const auto& tabla068 = SeatIndex068();

    // 16/09 (H2 of the review) — WITHOUT THE COMPLETE TABLE NOTHING IS MIGRATED, AND THE EPOCH IS NOT SEALED.
    // Before, if the file hadn't reached the install, the nine series were migrated BY INDEX against
    // the new roster — which is the original failure, the market signing landing on another person —, the
    // legacy keys were consumed and the save was left sealed: starting again with the file already present
    // repaired nothing, because there was no legacy left to migrate.
    // Now it exits without touching the save. Meanwhile nothing is lost: with the epoch still at 0,
    // ApplySeatOverrides keeps reading the legacy keys, which is exactly the 0.6.8 semantics. And the
    // first load with the whole file migrates properly from them.
    // The contract and substitution indices aren't touched either: translating them now and translating them again on the
    // good load would move them twice.
    if (!g_tabla068Completa) {
        UserLog("seats", "0.6.8 save: career/seat_index_068.psv is missing or incomplete (%d series loaded) -- "
                         "seat keys are NOT migrated and the save is NOT sealed, so it keeps working on the old "
                         "keys and a later load with the full file will migrate it correctly. Reinstall the data "
                         "if this keeps happening.", (int)tabla068.size());
        if (orphans) *orphans = 0;
        return 0;
    }

    for (const auto& sd : AllSeries()) {
        const auto it068 = tabla068.find(sd.id);
        const std::vector<Asiento068>* viejos = (it068 != tabla068.end()) ? &it068->second : nullptr;
        // The series catalog is walked until it stops giving liveries. The hard cap is a safety net: a
        // class with an absurd catalog can't hang the load of a save.
        struct Pendiente { std::string legacy, slot, persona; bool porPersona; int idx; };
        std::vector<Pendiente> pend;
        // 16/09 (M3) — if the save brings deviations of a series that is NOT in the table, it is migrated by
        // INDEX, which is exactly what the table exists to avoid. For the ~110 series whose roster didn't change
        // that is correct; if one of the nine ever shows up here, the table came incomplete and it has
        // to be seen in the player's log, not with a debugger.
        bool avisadaSinTabla = false;
        for (int i = 0; i < 512; i++) {
            const bool de068 = viejos && i < (int)viejos->size();
            if (!de068 && SeatLiveryForIndex(sd.id, i).empty()) break;   // end of this series' seats
            const std::string legacy = SeatKeyLegacy(sd.id, i);
            if (!save.seat_occupant.count(legacy) && !save.seat_contract_start.count(legacy)) continue;
            tratadas.insert(legacy);                               // seat with a persisted deviation
            if (!viejos && !avisadaSinTabla) {
                avisadaSinTabla = true;
                UserLog("seats", "0.6.8 save: series %s has saved seat deviations but no frozen 0.6.8 table, so "
                                 "they are migrated BY INDEX against today's roster", sd.id.c_str());
            }
            bool porPersona = true;                                // without a 0.6.8 table it resolves by live index
            const std::string slot = de068 ? SlotKeyFrom068(sd, *viejos, i, &porPersona) : SeatSlotKey(sd.id, i);
            pend.push_back(Pendiente{ legacy, slot, de068 ? viejos->at(i).persona : std::string(), porPersona, i });
        }
        // 16/09 (Josh's decision via team-lead) — A SAVED DEVIATION BELONGS TO THE PERSON, NOT THE CAR.
        // If the person in that row of 0.6.8 no longer runs the series, their key stays ORPHANED: kept
        // with its old key and not applied (, the path that already existed). It is NOT moved to «their livery from
        // back then», which is what it did before and did two bad things:
        //   · if that car is driven today by someone who DOES continue and brings their own key, the mark of the one who left
        //     OVERWROTE theirs -- measured when moving FALCON SERIES 847 from the 2000 grid to the 2001 one, 5 of 6 clashes
        //     fell on the wrong side (Zonta covered Villeneuve, Mazzacane Ralf Schumacher, Wurz Button,
        //     Diniz Frentzen, Herbert Häkkinen) and the map also shrank;
        //   · and if the car was free, the mark landed on a NEW FACE: Hamilton's on
        //     Mick Schumacher's Coldwick Racing and Magnussen's on a Greystead Motors that someone else drives today (F1 2021).
        // Neither is what the player saved. With the person as the only anchor the whole class of clashes
        // disappears, so the double pass isn't needed either: it only writes what it resolved by
        // person, and two different people don't share a livery.
        // The path by livery STAYS ALIVE in SeatIndexFrom068, which is something else: there the index is the
        // PLAYER's OWN seat, and its anchor is indeed the car they drive (and besides `porEquipo` re-derives it later
        // from the contract's label).
        for (const auto& p : pend) {
            const bool hayOcc   = save.seat_occupant.count(p.legacy) > 0;
            const bool hayStart = save.seat_contract_start.count(p.legacy) > 0;
            if (p.slot.empty() || !p.porPersona) {
                huerfanas++;
                // 16/09 (M3) — one line per orphan in the SUPPORT log, with series, index and the person who
                // occupied that seat in 0.6.8. Before only a counter came out, and via LogF. Without this, a
                // DATA regression —putting into the pool someone the table froze by hash, changing their
                // country, renaming a roster row— turns a good key into an orphan without leaving a trace.
                UserLog("seats", "0.6.8 save: seat %s#%d (%s) has no seat today; its saved deviation is kept "
                                 "under the old key but NOT applied", sd.id.c_str(), p.idx,
                        p.persona.empty() ? "no person in the frozen table" : p.persona.c_str());
                if (hayOcc)   occ[p.legacy]   = save.seat_occupant.at(p.legacy);
                if (hayStart) start[p.legacy] = save.seat_contract_start.at(p.legacy);
                continue;
            }
            if (hayOcc)   { occ[p.slot]   = save.seat_occupant.at(p.legacy);       movidas++; }
            if (hayStart) { start[p.slot] = save.seat_contract_start.at(p.legacy); }
        }
    }
    // What wasn't touched passes through as is: keys ALREADY migrated, and the remains of a series or an index that the
    // catalog of this save doesn't recognize — which also can't be relocated without inventing.
    for (const auto& kv : save.seat_occupant)       if (!tratadas.count(kv.first) && !occ.count(kv.first))   occ[kv.first]   = kv.second;
    for (const auto& kv : save.seat_contract_start) if (!tratadas.count(kv.first) && !start.count(kv.first)) start[kv.first] = kv.second;

    save.seat_occupant.swap(occ);
    save.seat_contract_start.swap(start);

    // 0.7.0 port (F12): the contract and substitution index, ONLY the first time (seat_key_epoch 0).
    // After that it is already today's index, and translating it again would move it to another car.
    // 16/09 (M1): the lock that used to be ONLY here was raised to the top of the function, so this
    // point isn't reached with the epoch sealed. The block stays for readability, not because it protects.
    {
        auto traducir = [&](const std::string& serie, int& idx) {
            const auto it = tabla068.find(serie);
            const SeriesDef* sd = SeriesById(serie);
            if (it == tabla068.end() || !sd || idx < 0) return;
            const int nuevo = SeatIndexFrom068(*sd, it->second, idx);
            if (nuevo >= 0) idx = nuevo;
            else UserLog("seats", "0.6.8 save: seat %s#%d of the player's contract has no seat today; kept as is",
                         serie.c_str(), idx);
        };
        // And the team rules over the person. The index came from the 0.6.8 signing by the team's LABEL
        // (AcceptOffer: the first seat with that team_label, or the free one), and in 0.6.8 those labels went one
        // livery raced: a F1 2025 «Fairfield_Motors» fell on Russell's car, and translated by person it stayed on
        // the Greystead Motors. If today's seat isn't the contract team's, it is re-derived just like on signing; without a
        // seat from that team, -1 (a foreign index protects and frees slots of another team).
        auto porEquipo = [&](const std::string& serie, const std::string& equipo, int& idx) {
            if (equipo.empty()) return;
            const std::vector<Seat> asientos = SeatsForClass(serie, save);
            if (idx >= 0 && idx < (int)asientos.size() && asientos[idx].team_label == equipo) return;
            int elegido = -1;
            for (size_t i = 0; i < asientos.size(); i++) {
                if (asientos[i].team_label != equipo) continue;
                if (elegido < 0) elegido = (int)i;
                if (asientos[i].open) { elegido = (int)i; break; }
            }
            if (elegido < 0 && idx >= 0)
                UserLog("seats", "0.6.8 save: team '%s' has no seat in %s today; the contract loses seat #%d",
                        equipo.c_str(), serie.c_str(), idx);
            idx = elegido;
        };
        for (auto& c : save.contracts) {
            if (!c.active || c.series_id.empty()) continue;
            traducir(c.series_id, c.seat_idx);
            porEquipo(c.series_id, c.team_id, c.seat_idx);
        }
        if (save.sub_ride.state != 0 && !save.sub_ride.series_id.empty()) {
            traducir(save.sub_ride.series_id, save.sub_ride.seat_idx);
            porEquipo(save.sub_ride.series_id, save.sub_ride.team_label, save.sub_ride.seat_idx);
        }
    }
    // Even if nothing moved: marks that the save is already in today's keys and indices. Without this, a
    // contract signed afterwards would be taken for a 0.6.8 one on the next load.
    // 16/09 (H2): we only get here WITH the complete table -- without it we exited above without touching the save,
    // precisely so as not to seal a half migration.
    save.seat_key_epoch = 1;
    if (orphans) *orphans = huerfanas;
    return movidas;
}

std::string SeriesOfSeatKey(const std::string& key) {
    size_t p = key.find('|');    // key by SLOT (): "<serie>|<veh_hash>|<librea>"
    if (p != std::string::npos) return key.substr(0, p);
    p = key.find('#');           // legacy key: "<serie>#<indice>"
    return p == std::string::npos ? key : key.substr(0, p);
}

int ContractExpiryYear(const std::string& seatKey, const std::string& occupantId, int startYear) {
    if (seatKey.empty() || occupantId.empty()) return 0;     // no seat or no occupant means no contract
    const int lo = bal::AI_CONTRACT_MIN_YEARS, hi = bal::AI_CONTRACT_MAX_YEARS;
    const int span = (hi >= lo) ? (hi - lo + 1) : 1;
    // The hash carries the seat's key INSIDE it, not just the driver: if it depended only on the driver,
    // someone holding two seats would lose both in the same year.
    unsigned long h = seatFnv1a(seatKey + "" + occupantId);
    return startYear + lo + (int)(h % (unsigned long)span);
}

int SeatContractStartYear(const SaveData& save, const std::string& seatKey) {
    auto it = save.seat_contract_start.find(seatKey);
    // Absent = has been occupied since the career started. That's the case for the 1,656 seats in a
    // new save, which is why none of them get persisted.
    return (it == save.seat_contract_start.end()) ? kStartYear : it->second;
}

bool SeatContractExpires(const SaveData& save, const std::string& seatKey,
                         const std::string& occupantId, int worldYear) {
    if (occupantId.empty() || occupantId == kSeatOpen) return false;   // empty seat: nothing to expire
    const int exp = ContractExpiryYear(seatKey, occupantId, SeatContractStartYear(save, seatKey));
    return exp > 0 && worldYear >= exp;
}

void NoteSeatContractStart(SaveData& save, const std::string& seatKey, int startYear) {
    if (seatKey.empty() || startYear <= 0) return;
    if (startYear == kStartYear) {
        // Matches the starting state: it's the default value, so it does NOT get written. A
        // new save stays byte-identical, which is this phase's compatibility rule.
        save.seat_contract_start.erase(seatKey);
        return;
    }
    save.seat_contract_start[seatKey] = startYear;
}

bool PlayerDisplacesIncumbent(double playerAgedRating, double weakestIncumbentRating) {
    return playerAgedRating > weakestIncumbentRating + bal::DISPLACEMENT_MARGIN;
}

// STARTUP signing window (per feedback from July 13): opens 'want' backmarker seats in entry-level series.
int PreseasonOpenSeats(SaveData& save, int maxLevel, int want) {
    if (want <= 0) return 0;
    if (maxLevel < 1) maxLevel = 1; if (maxLevel > 7) maxLevel = 7;
    std::vector<const SeriesDef*> cands;
    for (const auto& sd : AllSeries()) {
        if (sd.level > maxLevel) continue;
        if (sd.kind == "standalone") continue;
        if (IsEventOnlySeries(sd)) continue;             // 01/10
        if (!sd.branch_of.empty()) continue;
        if (IsEraMemorySeries(sd)) continue;
        cands.push_back(&sd);
    }
    if (cands.empty()) return 0;
    int opened = 0;
    size_t start = (size_t)(save.career_seed % (unsigned long)cands.size());   // different distribution per save
    for (size_t k = 0; k < cands.size() && opened < want; ++k) {
        const SeriesDef* sd = cands[(start + k) % cands.size()];
        std::vector<Seat> seats = SeatsForClass(sd->id, save);
        if (seats.empty()) continue;
        size_t last = seats.size() - 1;
        if (seats[last].open) continue;
        save.seat_occupant[SeatKey(sd->id, (int)last)] = kSeatOpen;
        opened++;
    }
    return opened;
}

// -- SILLY SEASON: deterministic seat releases mid-season (see career_seats.h).
int SillySeasonFreeSeats(SaveData& save, int year, int cursorDoy, int weeks, int maxLevel) {
    if (weeks <= 0) return 0;
    if (maxLevel < 1) maxLevel = 1; if (maxLevel > 7) maxLevel = 7;
    // GLOBAL cap on simultaneously open seats (also counts those freed by the rollover)
    int openNow = 0;
    for (const auto& kv : save.seat_occupant) if (kv.second == kSeatOpen) openNow++;
    // candidates: same exclusions as the market (no standalone / no branch / no era), reachable level
    std::vector<const SeriesDef*> cands;
    for (const auto& sd : AllSeries()) {
        if (sd.level > maxLevel) continue;
        if (sd.kind == "standalone") continue;
        if (IsEventOnlySeries(sd)) continue;             // 01/10
        if (!sd.branch_of.empty()) continue;
        if (IsEraMemorySeries(sd)) continue;
        cands.push_back(&sd);
    }
    if (cands.empty()) return 0;
    int opened = 0;
    const int weekEnd = cursorDoy / 7;                     // week index of the cursor, ALREADY advanced
    for (int w = 0; w < weeks && openNow + opened < bal::MK_SILLY_OPEN_CAP; ++w) {
        char key[64];
        snprintf(key, sizeof(key), "%lu|%d|%d", save.career_seed, year, weekEnd - w);
        unsigned long h = seatFnv1a(key);
        if ((int)(h % 100) >= bal::MK_SILLY_WEEK_PCT) continue;   // no movement this week
        // series chosen by the same hash; if its backmarker is already open, try the next ones (wrapping)
        for (size_t k = 0; k < cands.size(); ++k) {
            const SeriesDef* sd = cands[(h / 100 + k) % cands.size()];
            std::vector<Seat> seats = SeatsForClass(sd->id, save);
            if (seats.empty()) continue;
            size_t last = seats.size() - 1;
            if (seats[last].open) continue;                // already open -> next candidate
            save.seat_occupant[SeatKey(sd->id, (int)last)] = kSeatOpen;   // backmarker -> OPEN
            opened++;
            break;
        }
    }
    return opened;
}

// see career_seats.h. Releases THIS series' churn quota when the cursor falls within its
// signing window [openerDoy - MK_PRESEASON_LEAD, openerDoy). Deterministic (seatFnv1a by series|year, no
// RNG), idempotent (a seat that's already open, whether persisted or derived, is never recounted), bounded by
// the GLOBAL worldOpenCap. After the wrap, `delta` always falls in [0,365): delta=0 is the opener DAY itself (excluded,
// open right edge), delta in [1, LEAD] is the valid window (left edge opener-LEAD included),
// delta > LEAD falls outside. `seats[idx].open` ALREADY reflects the persisted sentinel (SeatsForClass applies it),
// so there's no need to re-read save.seat_occupant separately.
// Idempotency fix on the draft: the target set is the `quota` indices designated by the hash
// (k=0..quota-1), NOT "search until `quota` new openings are found". With the original `opened<quota` loop,
// a second call within the SAME window skipped indices already open (continue, without counting) and kept
// searching further -> it opened ANOTHER batch of `quota` seats every time (breaking the idempotency required by the
// test). Bounding `k` to `quota` instead of `seats.size()` fixes the same target set on every call:
// already open -> no-op; gaps -> filled until the quota is met (under the global cap).
int OpenPreseasonSeatsForSeries(SaveData& save, const std::string& seriesId, int openerDoy,
                                int cursorDoy, int year, int worldOpenCap) {
    if (openerDoy <= 0) return 0;                       // a series with no dates -> doesn't enter the master calendar
    int delta = openerDoy - cursorDoy;                  // days until this season's opener
    if (delta < 0) delta += 365;                        // wrap: a past opener means next year's
    if (delta <= 0 || delta > bal::MK_PRESEASON_LEAD) return 0;   // window [opener-LEAD, opener): delta in [1, LEAD]
    std::vector<Seat> seats = SeatsForClass(seriesId, save);
    if (seats.empty()) return 0;
    // the class's churn quota, capped at grid size
    int quota = (int)((seats.size() * bal::MK_CHURN_PCT + 50) / 100);   // round
    if (quota < bal::MK_CHURN_MIN) quota = bal::MK_CHURN_MIN;
    if (quota > (int)seats.size()) quota = (int)seats.size();
    // GLOBAL cap on simultaneously open seats
    int openNow = 0;
    for (const auto& kv : save.seat_occupant) if (kv.second == kSeatOpen) openNow++;
    // opens from the LAST index (backmarker) inward; startup order deterministic by series|year
    unsigned long h = seatFnv1a(seriesId + "|" + std::to_string(year));
    int opened = 0;
    for (size_t k = 0; k < (size_t)quota && openNow + opened < worldOpenCap; ++k) {
        size_t idx = seats.size() - 1 - ((h + k) % seats.size());   // FIXED target set (backmarkers-first)
        if (seats[idx].open) continue;                              // already in the target set -> quota already met there
        save.seat_occupant[SeatKey(seriesId, (int)idx)] = kSeatOpen;
        opened++;
    }
    return opened;
}

// driver -- replaces the weekly GLOBAL silly-season pass (SillySeasonFreeSeats) with a release
// ANCHORED to each class's calendar. Same exclusions as the market/silly-season (level<=maxLevel; no
// standalone/branch/era); resolves the day-of-year of round 1 for each series (via CalendarForSeriesInYear -- PROJECTED
// onto the current year, not the raw CalendarForSeries; kStartYear is the identity) and delegates to OpenPreseasonSeatsForSeries
// with cc's current cursor/year. Deterministic, idempotent (by transitivity of OpenPreseasonSeatsForSeries).
// Mutates ONLY save.seat_occupant. Returns the total number opened.
int PreseasonWindowChurn(SaveData& save, CareerCalendar& cc, int maxLevel) {
    if (maxLevel < 1) maxLevel = 1; if (maxLevel > 7) maxLevel = 7;
    int total = 0;
    for (const auto& sd : AllSeries()) {
        if (sd.level > maxLevel) continue;
        if (sd.kind == "standalone") continue;
        if (IsEventOnlySeries(sd)) continue;             // 01/10
        if (!sd.branch_of.empty()) continue;
        if (IsEraMemorySeries(sd)) continue;
        std::vector<CalRound> cal = CalendarForSeriesInYear(sd.id, cc.year);
        int openerDoy = 0;
        for (const auto& c : cal) if (c.round == 1) { openerDoy = c.doy; break; }
        total += OpenPreseasonSeatsForSeries(save, sd.id, openerDoy, cc.cursor_doy, cc.year, bal::MK_SILLY_OPEN_CAP);
    }
    return total;
}

// See career_seats.h for the full rationale.
int OpenTransferWindow(SaveData& save, int year, int doy, int absToday, int frontierPhase, int firstYear) {
    if (market::MarketPhaseFor(year, doy, firstYear) != market::MARKET_WINDOW) return 0;   //
    // the sealed year is the DESTINATION SEASON, not the clock's: that way year 1 can seal its
    // compressed February window (destination: this year) and then September's (destination: next year) without the
    // second one being skipped as "already sealed", leaving the player without a silly season in their first December.
    const int destino = market::WindowTargetYear(year, doy, firstYear);
    if (save.window_year == destino) return 0;                                  // already sealed: not re-sealed

    // candidates: (distance to the player's level, series, team) -- distance comes FIRST so the cap
    // keeps what's within your reach, not just whatever comes first in traversal order.
    struct Cand { int dist; int level; std::string series_id, team_label; bool tuya = false; };
    std::vector<Cand> cands;
    std::set<std::string> vistos;   // (series|team) already candidates: a team appears ONCE, not once per seat

    // YOUR CONTRACT GOES THROUGH THE SAME WINDOW. Until now, the entire world rotated except you:
    // the "in play" test asked about the contract DERIVED from the driver the data placed in that seat,
    // and in yours that driver is a ghost -- you're the one sitting there, and the seat model doesn't know it. So
    // your seat entered the window whenever someone else's imaginary contract happened to expire, or never did.
    // What expires on YOUR seat is YOUR contract: seasons_left. With a season still ahead, the seat is NOT in
    // play (nobody gets signed for a seat you're not about to leave); in your last season it is, and it's contested like any other.
    std::map<std::string, const Contract*> mios;      // SeatKey -> your contract for that seat
    for (const auto& c : save.contracts) {
        if (!c.active || c.series_id.empty() || c.seat_idx < 0) continue;
        mios[SeatKey(c.series_id, c.seat_idx)] = &c;
    }
    for (const auto& sd : AllSeries()) {
        if (sd.kind == "standalone" || !sd.branch_of.empty()) continue;
        if (IsEraMemorySeries(sd)) continue;      // a memory is frozen there: no signing happens
        if (IsEventOnlySeries(sd)) continue;      // 01/10: event-only: it has no seats
        if (!SeriesPlayable(sd)) continue;        // offering a seat that can't actually be raced is a trap
        if (sd.level < frontierPhase - bal::MK_OFFER_BAND_BELOW) continue;   // relevance band (A2 L1)
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (size_t i = 0; i < seats.size(); i++) {
            if (seats[i].team_label.empty()) continue;               // no team means no key to seal
            const std::string key = SeatKey(sd.id, (int)i);
            // in play = the seat is already open, or its contract expires at the close of this year (an expiry
            // derived value: it isn't persisted, so it can never fall out of sync with the seat)
            const auto mio = mios.find(key);
            const bool enJuego = (mio != mios.end())
                ? (mio->second->seasons_left <= 1)          // your seat: YOUR contract rules, not the ghost's
                : (seats[i].open || SeatContractExpires(save, key, seats[i].occupant_id, year));
            if (!enJuego) continue;
            const std::string llave = sd.id + "|" + seats[i].team_label;
            if (vistos.count(llave)) continue;
            vistos.insert(llave);
            Cand c;
            c.dist = sd.level > frontierPhase ? sd.level - frontierPhase : frontierPhase - sd.level;
            c.level = sd.level; c.series_id = sd.id; c.team_label = seats[i].team_label;
            c.tuya = (mio != mios.end());   // your own seat: the cap can't leave it out (see below)
            cands.push_back(c);
        }
    }
    // TOTAL, deterministic ordering: proximity, then level, then the key. Without the key as tiebreaker, the
    // set would depend on traversal order, and the window would change between runs.
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.dist != b.dist)   return a.dist < b.dist;
        if (a.level != b.level) return a.level > b.level;      // at equal distance, the higher category comes first
        if (a.series_id != b.series_id) return a.series_id < b.series_id;
        return a.team_label < b.team_label;
    });

    save.window_seats.clear();     // a new year reopens the whole window: it's a different silly season, not the same one
    save.window_year = destino;
    int sellados = 0;
    std::set<std::string> yaSellada;
    auto sellar = [&](const Cand& c) {
        WindowSeat w;
        w.series_id = c.series_id; w.team_label = c.team_label;
        w.state = WSEAT_FREE; w.opened_day = absToday; w.applied = 0;
        save.window_seats.push_back(w);
        yaSellada.insert(c.series_id + "|" + c.team_label);
        sellados++;
    };

    // FIRST AND FOREMOST, YOUR OWN SEAT. That's the rule of the
    // and no distribution can displace it: without your seat sealed there's no renewal to judge, so
    // meeting the quota wouldn't help you at all. They're one per contract, so they barely eat into the cap.
    for (const auto& c : cands) {
        if (sellados >= bal::MK_WINDOW_SEAT_CAP) break;
        if (!c.tuya) continue;
        sellar(c);
    }

    // AFTER THAT, THE QUOTA OF WHAT MUST BE EARNED. Ordering by proximity is good for "what's within your
    // reach", but it had an effect nobody intended: distance 0 is YOUR OWN LEVEL, and a seat at your level is never
    // contested (it's the on-ramp). Since more than 24 candidates come from your own level, the cap ran out right there and the
    // entire window ended up with not a single seat in play. Measured: 4 contested seats judged across six years, all four
    // in the first December; the rest of the time the world just said yes to everything.
    // A quota is reserved for the ASPIRATIONAL tier (above your level), and the rest still comes out through the
    // the usual order, so the on-ramp loses nothing: its guaranteed seats still come in afterward.
    // the quota is counted SEPARATELY from `sealed`: if they shared a counter, your own seats (sealed just
    // above) would eat into the reserved slot, and the window would end up with nothing to contest again.
    int aspiracionales = 0;
    for (const auto& c : cands) {
        if (aspiracionales >= bal::MK_WINDOW_ASPIRATIONAL_MIN) break;
        if (sellados >= bal::MK_WINDOW_SEAT_CAP) break;
        if (c.level <= frontierPhase) continue;                 // this isn't contested: it goes in the third pass
        if (yaSellada.count(c.series_id + "|" + c.team_label)) continue;
        sellar(c);
        aspiracionales++;
    }
    // ...and the rest, by proximity as always (skipping whatever already got in through the quota).
    for (const auto& c : cands) {
        if (sellados >= bal::MK_WINDOW_SEAT_CAP) break;
        if (yaSellada.count(c.series_id + "|" + c.team_label)) continue;
        sellar(c);
    }
    return sellados;
}

int OpenCareerStartSeats(SaveData& save) {
    // the whole entry ladder: a high `want` isn't a cap, it's "don't stop until tier 1 is exhausted"
    int n = PreseasonOpenSeats(save, 1, 999);
    // ...plus a few above. The second pass re-walks tier 1, but a seat that's already open is skipped
    // without counting, so the only thing that opens fresh is tier 2.
    n += PreseasonOpenSeats(save, bal::MK_PRESEASON_MAX_LEVEL, bal::MK_PRESEASON_ASPIRE_SEATS);
    return n;
}

// 09/09 (performance): see career_seats.h. The fingerprint and the memo, here; the count, in StayingDriversUncached.
namespace {
// Four live answers, not one: within one tick the current year and the next one are asked about
// (targetYear) with the same seats, and a single slot overwrote itself on every alternation.
struct StayingMemo { unsigned long long huella = 0; bool valid = false; unsigned long uso = 0; std::set<std::string> out; };
constexpr int kStayingSlots = 4;
StayingMemo g_stayingMemo[kStayingSlots];
unsigned long g_stayingTick = 0;
inline void stayingFnv(unsigned long long& h, const std::string& s) {
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    h ^= 0xFFu; h *= 1099511628211ULL;   // separator: "ab"+"c" != "a"+"bc"
}
unsigned long long StayingHuella(const SaveData& save, int worldYear) {
    unsigned long long h = 14695981039346656037ULL;
    stayingFnv(h, std::to_string(worldYear));
    stayingFnv(h, std::to_string(DataEpoch()));   // 0.7.0 port: the data epoch (f0: AffinityEpoch, from contracts)
    for (const auto& kv : save.seat_occupant) { stayingFnv(h, kv.first); stayingFnv(h, kv.second); }
    stayingFnv(h, "|contratos|");
    for (const auto& kv : save.seat_contract_start) { stayingFnv(h, kv.first); stayingFnv(h, std::to_string(kv.second)); }
    stayingFnv(h, "|retirados|");
    for (const auto& id : save.retired) stayingFnv(h, id);
    return h;
}
}  // anon
std::set<std::string> StayingDrivers(const SaveData& save, int worldYear) {
    const unsigned long long h = StayingHuella(save, worldYear);
    ++g_stayingTick;
    int libre = 0;
    for (int i = 0; i < kStayingSlots; i++) {
        if (g_stayingMemo[i].valid && g_stayingMemo[i].huella == h) { g_stayingMemo[i].uso = g_stayingTick; return g_stayingMemo[i].out; }
        if (!g_stayingMemo[i].valid) libre = i;
        else if (g_stayingMemo[i].uso < g_stayingMemo[libre].uso || !g_stayingMemo[libre].valid) { if (g_stayingMemo[libre].valid) libre = i; }
    }
    // the least used slot
    int victima = 0;
    for (int i = 1; i < kStayingSlots; i++) if (!g_stayingMemo[i].valid || g_stayingMemo[i].uso < g_stayingMemo[victima].uso) { victima = i; if (!g_stayingMemo[i].valid) break; }
    StayingMemo& m = g_stayingMemo[victima];
    m.out = StayingDriversUncached(save, worldYear);
    m.huella = h; m.valid = true; m.uso = g_stayingTick;
    return m.out;
}
std::set<std::string> StayingDriversUncached(const SaveData& save, int worldYear) {
    std::set<std::string> out;
    for (const auto& sd : AllSeries()) {
        if (IsEraMemorySeries(sd)) continue;          // a memory is frozen: its occupants don't sign
        const std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (size_t i = 0; i < seats.size(); i++) {
            if (seats[i].open || seats[i].occupant_id.empty()) continue;
            // if their contract does NOT expire at the close of this year, they stay: that driver already has a seat
            if (!SeatContractExpires(save, SeatKey(sd.id, (int)i), seats[i].occupant_id, worldYear))
                out.insert(seats[i].occupant_id);
        }
    }
    return out;
}

std::vector<RosterDriver> CohortMinusStaying(const std::vector<RosterDriver>& cohorte,
                                             const std::set<std::string>& staying) {
    std::vector<RosterDriver> out;
    out.reserve(cohorte.size());
    for (const auto& rd : cohorte) if (!staying.count(rd.id)) out.push_back(rd);
    return out;
}

WindowSeat* FindWindowSeat(SaveData& save, const std::string& seriesId, const std::string& teamLabel) {
    for (auto& w : save.window_seats)
        if (w.series_id == seriesId && w.team_label == teamLabel) return &w;
    return nullptr;
}
const WindowSeat* FindWindowSeat(const SaveData& save, const std::string& seriesId, const std::string& teamLabel) {
    for (const auto& w : save.window_seats)
        if (w.series_id == seriesId && w.team_label == teamLabel) return &w;
    return nullptr;
}

bool ApplyToWindowSeat(SaveData& save, const std::string& seriesId, const std::string& teamLabel) {
    WindowSeat* w = FindWindowSeat(save, seriesId, teamLabel);
    if (!w) return false;                      // not in the window: there's no seat to apply for
    // Tester feedback (Aug 27, [tester]): the window let you APPLY at ANY level -- [tester] applied to F1 several
    // tiers above their own level, won the contest, signed... and the calendar then locked their whole season (a dead
    // contract). And in a series with a pending superlicense, this was the third route for the exploit.
    // The +1 MARGIN is deliberate: ASPIRATIONAL seats one tier above are by design (the carrot of the
    // preseason, bal::MK_PRESEASON_ASPIRE_SEATS -- "those ARE contested, and you can lose them to the cohort");
    // what gets cut off is a jump of 2+ tiers, which can only end in an unsalvageable contract. Signing a
    // gated seat is also locked down in AcceptRejectReason ("superlicense"), the single gate.
    const SeriesDef* sdw = SeriesById(seriesId);
    if (sdw && sdw->level > save.prog.frontier_phase + 1) return false;
    if (sdw) {
        for (const auto& q : AllSeries()) {
            if (q.unlocks != seriesId) continue;
            if (save.unlocked_gates.count(seriesId) == 0) return false;
            break;
        }
    }
    if (w->applied) return false;              // you already applied: applying twice doesn't get you counted twice
    if (w->state == WSEAT_RENEWED || w->state == WSEAT_OFFERED || w->state == WSEAT_REJECTED)
        return false;                          // renewed or already resolved: that conversation doesn't exist
    w->applied = 1;
    w->state = WSEAT_DISPUTED;                 // OPEN/COURTED --you apply--> CONTESTED
    return true;
}

// see career_seats.h.
int ResolveDecisionWeek(SaveData& save, int year, int doy, double playerMoneyNorm, int firstYear,
                        const std::map<std::string, double>* ratios) {
    if (market::MarketPhaseFor(year, doy, firstYear) != market::MARKET_DECISION) return 0;
    // resolves the window that signs for the destination season of THIS point on the clock; if the sealed one
    // is a different one, this decision week isn't its own.
    if (save.window_year != market::WindowTargetYear(year, doy, firstYear)) return 0;

    // one driver, one seat: those already staying somewhere else don't contest this one. It's computed ONCE.
    // 19/09 (tester: «Lucia Vega» in 8 seats at once): ...and it GROWS. Whoever wins a slot leaves the shortlist of
    // the following ones; otherwise TopRivalInRank returned the same person for all of them and it was she who «took»
    // them all. It also starts with those this window already named in an earlier pulse of the week.
    std::set<std::string> staying = StayingDrivers(save, year);
    for (const auto& w : save.window_seats)
        if (w.state == WSEAT_REJECTED && !w.winner_id.empty()) staying.insert(w.winner_id);
    std::set<std::string> ocupados; bool ocupadosListos = false;   // 19/09: only if it is necessary to go to the pool
    int resueltas = 0;
    for (auto& w : save.window_seats) {
        const SeriesDef* sd = SeriesById(w.series_id);
        if (!sd) continue;

        // YOUR SEAT ISN'T CONTESTED, IT'S RENEWED OR IT SLIPS AWAY. The rest of the window is about
        // winning a shortlist; yours isn't, because the team already knows how you've done. What decides it is whether
        // you met the target set when you signed (obj_target_pos), measured with the SAME formula that
        // the rollover uses -- not a copy, the very same one (PlayerSeasonRatios -> ContractSeasonRatio).
        // That's why it's judged even when not in DISPUTED: you don't apply for your own seat.
        const Contract* mio = nullptr;
        for (const auto& c : save.contracts)
            if (c.active && c.series_id == w.series_id && c.team_id == w.team_label) { mio = &c; break; }
        const bool esTuya = (ratios != nullptr) && (mio != nullptr);

        // A VERDICT IS FINAL. The decision week lasts 7 days and the tick runs every time the
        // clock advances, so without this a seat already judged gets judged again on the next tick. It was uncovered by the
        // long-run simulator on its first extended run, and it showed up in the timeline: on day 349 the
        // seat is OFFERED to you and you sign it; on day 356 the same seat comes out DISCARDED, bearing the name of whoever
        // took it from you. You'd already signed it. In the real game it's less noticeable because the clock has to be advanced by hand
        // within that week, but the bug is the same, and it was live.
        if (w.state == WSEAT_OFFERED || w.state == WSEAT_REJECTED || w.state == WSEAT_RENEWED) continue;
        if (w.state != WSEAT_DISPUTED && !esTuya) continue;   // only what you fought for gets judged... or what's already yours

        // the seat's BAR comes from the series (market::SeatsForSeries derives it from the level), and the country comes from the
        // live seat's occupant. Neither is stored: both are derived, so they can never
        // fall out of sync with the world -- the same reason the earlier expiry value is derived too.
        int reqRep = 0; bool works = false;
        { const std::vector<market::MarketSeat> ms = market::SeatsForSeries(*sd);
          if (!ms.empty()) { reqRep = ms[0].reqRep; works = ms[0].works; } }   // the player's POTENTIAL seat

        // THE ON-RAMP (the feeder-seat rule, `SeatIsAspirational`): at your level and below there's ALWAYS room.
        // A seat that isn't aspirational is NOT contested -- it's offered to you. That's the rule that already governed the
        // Market and that this decision week was skipping, and it's what guarantees that your FIRST year
        // ends with contracts: backmarker teams in the entry tiers don't aim to sign the cream of
        // your generation, they want you. The ambitious seats still get fought over, which is where the drama comes from.
        if (!esTuya && !market::SeatIsAspirational(works, sd->level, save.prog.frontier_phase)) {
            w.state = WSEAT_OFFERED;
            w.winner_id.clear(); w.winner_name.clear();
            resueltas++;
            continue;
        }

        std::string teamCountry;
        for (const auto& s : SeatsForClass(w.series_id, save)) {
            if (s.team_label != w.team_label) continue;
            if (!s.occupant_id.empty())
                for (const auto& rd : RosterForTier(sd->level))
                    if (rd.id == s.occupant_id) { teamCountry = rd.country; break; }
            break;
        }

        const std::vector<RosterDriver> cohorte = CohortMinusStaying(RosterForTier(sd->level), staying);
        std::vector<SeatCandidate> rank = RankSeatCandidates(
            save, sd->class_name, w.team_label, teamCountry, playerMoneyNorm, year, cohorte, w.series_id);
        // 19/09 (Josh's decision, «real drivers from the pool»): with nobody from the cohort in the shortlist, the table
        // gave you the slot BY DEFAULT (PlayerWinsRankedSeat starts at 0 with no rivals). Now the team looks at
        // the real pool with the winter-fill chain, and that person enters the table as one more rival:
        // the slot is yours if you beat them, not because nobody was there. Only with nobody in the pool does it stay yours.
        if (!TopRivalInRank(rank)) {
            if (!ocupadosListos) { ocupados = OcupadosAhora(save); ocupadosListos = true; }
            const std::string real = PozoParaPlazaDeVentana(save, *sd, ocupados, staying, save.window_year);
            if (!real.empty())
                rank = RankSeatCandidates(save, sd->class_name, w.team_label, teamCountry, playerMoneyNorm, year,
                                          std::vector<RosterDriver>{ RosterDelPozo(real) }, w.series_id);
        }
        const SeatCandidate* rival = TopRivalInRank(rank);

        // the REQUIREMENT is checked HERE, not when applying: if you don't reach the bar, it rules you out no
        // matter how well you did in the comparison. It's what makes fighting for a seat you don't deserve yet
        // a season-long goal instead of a closed door.
        // For YOUR seat, the verdict is the target, not the shortlist. `rank` is already computed above and
        // is reused to put a NAME on whoever takes it: "you're not renewed, and they're going after him" is not a
        // phrase, it's this specific driver, the same one you'd have faced if you had contested it.
        if (esTuya) {
            double ratio = 0.5;      // with no measurement (a series with no table) -> ContractSeasonRatio's neutral value
            const auto itr = ratios->find(w.series_id);
            if (itr != ratios->end()) ratio = itr->second;
            // "the back-door exit": ABSENCES count toward renewal. Missing a round
            // under contract already dings your rating, and at MK_NOSHOW_BREACH_LIMIT the contract breaks on its own, but
            // in between, it cost you nothing seat-wise: you could meet the target while missing races and still get renewed
            // regardless. Now every absence eats into the ratio the team judges you by, because managing the
            // calendar is on you, and overcommitting has a cost.
            if (mio->missed_rounds > 0) {
                ratio -= mio->missed_rounds * bal::MK_RENEW_NOSHOW_PENALTY;
                if (ratio < 0.0) ratio = 0.0;
            }
            if (ratio >= bal::MK_RENEW_MET_RATIO) {
                w.state = WSEAT_OFFERED;                      // met the target -> renewal waiting for your signature
                w.winner_id.clear(); w.winner_name.clear();
            } else {
                w.state = WSEAT_REJECTED;                     // missed the target -> the seat goes to someone else
                w.reject_why = WSREJ_RENEW;
                if (rival) { w.winner_id = rival->id; w.winner_name = rival->name; staying.insert(rival->id); }
            }
            resueltas++;
            continue;
        }

        const bool llegaALaBarra = (reqRep <= 0) || (save.prog.rating >= (double)reqRep);
        const bool ganaLaTabla   = PlayerWinsRankedSeat(rank);

        // the margin of the contest, so we can MEASURE which side loses and by how much. It comes from the same
        // table that decides, not from a parallel calculation that could disagree with the verdict.
        { double pf = 0.0, best = 0.0;
          for (const auto& c : rank) { if (c.is_player) pf = c.fit; else if (c.fit > best) best = c.fit; }
          w.fit_margin = pf - best; }

        if (llegaALaBarra && ganaLaTabla) {
            w.state = WSEAT_OFFERED;
            w.reject_why = WSREJ_NONE;
            w.winner_id.clear(); w.winner_name.clear();   // it's yours: there's no "someone else got it" to report
        } else {
            w.state = WSEAT_REJECTED;
            w.reject_why = !llegaALaBarra ? (ganaLaTabla ? WSREJ_BAR : WSREJ_BOTH) : WSREJ_TABLE;
            if (rival) { w.winner_id = rival->id; w.winner_name = rival->name; staying.insert(rival->id); }
        }
        resueltas++;
    }
    return resueltas;
}

// see career_seats.h.
long WindowSeatPerRace(const SaveData& save, const std::string& seriesId, const std::string& teamLabel,
                       int year) {
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return 0;
    const int worldYear = save.current_world_year != 0 ? save.current_world_year : year;
    return career::SalaryFor(seriesId, teamLabel, sd->level, save.prog.rating, 50, save, worldYear);
}

// see career_seats.h.
bool SignWindowSeat(SaveData& save, CareerCalendar& cc, const std::string& seriesId,
                    const std::string& teamLabel, int year, std::string* reason, bool allowBreak) {
    auto fallo = [&](const char* r) { if (reason) *reason = r; return false; };
    const WindowSeat* w = FindWindowSeat(save, seriesId, teamLabel);
    // only what's OFFERED can be signed. A contested seat isn't yours yet, and a discarded one never will be; letting
    // either one be signed would turn the decision week into a formality with no verdict.
    if (!w || w->state != WSEAT_OFFERED) return fallo("no_offered");
    // and the offer HAS AN EXPIRY DATE. Without this, a seat offered during the December decision week
    // could still be signed in March -- signing for a season that had already been running for three months.
    // A tester found it while playing. The date comes from the CLOCK (cc), the world's clock, not whatever year the
    // caller passes: a caller that gets the year wrong must never be able to reopen a closed offer.
    if (!WindowOfferStillOpen(save, cc, cc.year, cc.cursor_doy)) return fallo("expired");
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return fallo("no_series");

    // the offer that corresponds to this seat. The id follows the SAME format as the market ("of_"+series),
    // because AcceptOffer derives the series from it -- using a different format here would leave it without a series.
    market::OfferDef od{};
    od.id      = "of_" + seriesId;
    od.clsId   = seriesId;
    od.team    = teamLabel;
    od.direct  = true;                 // they offered it to you after the contest: it's a signable seat
    od.reqRep  = 0;
    od.perRace = WindowSeatPerRace(save, seriesId, teamLabel, year);
    od.signBonus = 0;
    od.commitment = 1;
    od.contractKind = bal::CONTRACT_WORKS;

    // the guardrails are NOT checked here: AcceptOffer applies them, and it's the game's only signing gate.
    // What this does do is TRANSLATE its "false" into a reason, checking the same conditions it checks --
    // in that order, so exclusivity doesn't get blamed for what was actually a duplicate.
    // the contract is stamped for the DESTINATION SEASON, not the clock's year. The window closes in
    // December and signs for the FOLLOWING year: signing with the clock's year produced a contract for a season
    // that had already ended, and the rollover five days later killed it. Measured with the simulator: 22 signings
    // and 22 breaches, a permanent drought -- it made no difference whether you dominated or finished last, because nobody kept a seat.
    const int temporada = save.window_year > 0 ? save.window_year : year;

    // Does your CURRENT contract in this series already cover the season you're about to sign for? Then switching isn't
    // signing, it's BREAKING, and that has to be a choice, not something that just happens. Without `allowBreak` it exits with "would_break", and the
    // screen can show BreakContractFee -- the very function that will charge it -- before you click anything.
    // If the contract ends before that season, there's NO breach to speak of: your deal ends anyway, and this
    // is just a normal succession, the case that used to leave the career stuck upstream (see AcceptOffer).
    for (const auto& c : save.contracts) {
        if (!c.active || c.series_id != seriesId) continue;
        const int hasta = c.signed_year + (c.seasons_total > 0 ? c.seasons_total : 1) - 1;
        if (temporada < c.signed_year || temporada > hasta) continue;   // they don't overlap: succession, not a breach
        if (c.team_id == teamLabel) return fallo("already");            // your own seat: that's a renewal
        if (!allowBreak) return fallo("would_break");
        BreakContract(save, seriesId);   // a severance fee plus a REP hit, exactly once and through its one and only gate
        break;
    }

    if (AcceptOffer(od, save, cc, temporada)) { if (reason) reason->clear(); return true; }
    if (ContractForSeries(save, seriesId))      return fallo("already");
    // (fixed Aug 24): the translator now checks the SAME thing AcceptOffer does -- by destination season. With the gate
    // alone, a stale exclusivity that no longer blocks anything would have taken the blame for any other rejection.
    if (AnyExclusiveContractHeldFor(save, temporada)) return fallo("exclusive");
    if (od.buy_in > 0 && save.prog.credits < od.buy_in) return fallo("afford");
    // the canonical list of blockers as a final safety net: any new reason from AcceptRejectReason
    // ("superlicense" today, whatever comes tomorrow) reaches the screen without touching this translator.
    { std::string rr = AcceptRejectReason(od, save, temporada);
      if (!rr.empty()) return fallo(rr.c_str()); }
    return fallo("refused");
}

// see career_seats.h.
int SoundOutWindowSeats(SaveData& save) {
    int tanteadas = 0;
    // path 2 (proximity): candidates ordered by how close they are to you, ties broken by the key, so
    // the quota always goes to the same set regardless of who's running the sim. They're gathered in the same
    // pass that resolves path 1, and applied afterward: that way history never eats into the proximity quota.
    struct Cerca { int dist; std::string key; size_t idx; };
    std::vector<Cerca> cercanas;
    // the quota belongs to the window's TOTAL, not to this single call. Counting only what's courted today, every tick
    // would add three more, and within a few weeks half the window would be courted -- the cap would exist in the code
    // but not in the save. With this, the function is truly idempotent: calling it extra times changes nothing.
    //
    // And it counts ONLY the proximity ones, because history-based ones have no cap. The "why" isn't stored: it's
    // DERIVED by asking about the relationship again, the same way it was derived when courted. A seat already courted
    // that still has history behind it belongs to history; everything else, to the quota. One more field in the save would say the
    // same thing and could fall out of sync.
    int yaPorCercania = 0;
    for (const auto& w : save.window_seats)
        if (w.sounded && market::TeamRelationship(save, w.series_id, w.team_label) <= bal::REL_BASE)
            yaPorCercania++;

    for (size_t i = 0; i < save.window_seats.size(); i++) {
        WindowSeat& w = save.window_seats[i];
        if (w.state != WSEAT_FREE) continue;          // you're already contesting it, or the world already resolved it: it's not re-courted
        const SeriesDef* sd = SeriesById(w.series_id);
        if (!sd) continue;

        // path 1 -- HISTORY. TeamRelationship is DERIVED (former team, prior courting, snubs, press marks):
        // there's nothing to persist and nothing that can fall out of sync, and it starts exactly at REL_BASE when you
        // share no past. Above the base means they know you, and whoever knows you comes asking for you.
        if (market::TeamRelationship(save, w.series_id, w.team_label) > bal::REL_BASE) {
            w.state = WSEAT_SOUNDED; w.sounded = 1;
            tanteadas++;   // doesn't spend quota: the cap belongs to the proximity path, not to real reasons
            continue;
        }
        // path 2 -- only what is NOT aspirational: at your level and below, a team would sign you anyway
        // (the on-ramp), so courting you there is consistent. Anything ambitious still has to be fought for.
        if (market::SeatIsAspirational(false, sd->level, save.prog.frontier_phase)) continue;
        Cerca c;
        c.dist = save.prog.frontier_phase - sd->level;   // not aspirational -> never negative
        c.key  = w.series_id + "|" + w.team_label;
        c.idx  = i;
        cercanas.push_back(c);
    }

    std::sort(cercanas.begin(), cercanas.end(), [](const Cerca& a, const Cerca& b) {
        if (a.dist != b.dist) return a.dist < b.dist;    // closest to your level goes first
        return a.key < b.key;
    });
    for (size_t k = 0; k < cercanas.size() && yaPorCercania + (int)k < bal::MK_WINDOW_SOUND_CAP; k++) {
        WindowSeat& w = save.window_seats[cercanas[k].idx];
        w.state = WSEAT_SOUNDED; w.sounded = 1;
        tanteadas++;
    }
    return tanteadas;
}

// see career_seats.h.
int MarketCalendarTick(SaveData& save, int year, int doy, int frontierPhase, double playerMoneyNorm,
                       const std::map<std::string, double>* ratios) {
    // the year every career starts in (career_calendar.h). It isn't persisted because it doesn't need to be: it's a
    // game constant, so deriving it can never fall out of sync with anything.
    const int firstYear = kStartYear;
    const int absToday = career::dateutil::AbsDayFromYearDoy(year, doy);
    int tocadas = 0;

    // 1) RESOLVE what's pending, BEFORE opening anything (opening wipes the seats). The sealed window has ONE
    //    decision week, and which one it is can be known without storing it: year 1's is the compressed February one of that
    //    same year, and any other destination season's is December of the year BEFORE it.
    // THE VERDICT WAITS FOR THE MEASUREMENT RACE. The old offers board already did this (`calibrated` leaves the
    // Market empty until you race), and the Paddock has to respect the same rule for a reason that isn't about
    // symmetry: the decision week judges you on `prog.rating`, and before the measurement race that
    // number is just a SEED, not something you've proven. Resolving there would hand out seats based on
    // made-up data.
    //
    // It gates the VERDICT, not the window: the paddock opens and you can apply from day one -- looking at
    // the world and saying what you're aiming for needs no permission. And it self-heals: as soon as you run the measurement race, the
    // next tick resolves with the date it should have used, so nothing is lost by being late.
    if (save.window_year > 0 && save.prog.calibrated) {
        int decYear, decDoy;
        if (save.window_year == firstYear) {
            decYear = firstYear;  decDoy = market::MarketY1DecisionDoy(firstYear);
        } else {
            decYear = save.window_year - 1;  decDoy = market::MarketDecisionDoy(decYear);
        }
        // with the date it SHOULD have used, not today's: if you skipped the week, the verdict is the same as
        // if you'd lived through it. Past that day, it's still >= and resolves; before it, it's not due yet.
        if (absToday >= career::dateutil::AbsDayFromYearDoy(decYear, decDoy))
            tocadas += ResolveDecisionWeek(save, decYear, decDoy, playerMoneyNorm, firstYear, ratios);
    }

    // 2) ...and only now, open the window if the clock falls inside it. It does nothing if it was already sealed
    //    for this destination season, so the tick can run as often as it likes.
    const int selladas = OpenTransferWindow(save, year, doy, absToday, frontierPhase, firstYear);
    tocadas += selladas;
    // 3) ...and teams talk. Only on OPENING: courting is the window's first move, not a
    //    weekly drip. Running it on every tick would re-court seats as you freed them up, and
    //    "they want you" would end up as the default state of the whole board.
    if (selladas > 0) tocadas += SoundOutWindowSeats(save);
    return tocadas;
}

// The deterministic aged rating of an incumbent, mirroring the rollover's aged-rating
// projection so the displacement predicate compares apples to apples against the
// same living-world rating the retirement/promotion engine uses. skill (0..1) -> rating band, scaled by the
// age curve, plus the persisted rival_form delta (the season-to-season skill drift). Pure: reads the save's
// birth_year/rival_form maps by the driver id, mutates nothing. NO RNG. worldYear drives the age curve. A
// birth_year of 0 (unknown) yields the curve's base age multiplier (DriverAge handles 0 -> 0 -> neutral mult).
static double agedRatingFor(const std::string& id, double skill, int rosterBirthYear,
                            const SaveData& save, int worldYear, int ageShift, int tier) {
    // LEVEL ANCHOR + POSITION ON THE GRID.
    //
    // `skill` is NOT absolute: it comes from CurveSkill, which maps a driver's ORDER within THEIR grid onto the
    // AI range of their profile -- and no profile defines a range, so all 115 series use the same
    // 85-100. The karting ace and the F1 ace produced the same number, and the per-tier bar came out
    // INVERTED (measured: karting 1521, elite 1470). Comparing a driver's rating across classes didn't
    // mean anything, which is exactly what the cascade needs to do.
    //
    // Here the step is placed up front: the part of `skill` above the tail-end marker
    // becomes their position WITHIN their class, and the tier supplies the anchor.
    // Their talent is THEIRS: it comes from where their history put them, not from the seat they occupy today (BaseTalent).
    // Without this, moving down a team made you worse and doomed you to keep sliding -- measured: 157 points in one
    // year moving from Stonefield Motors to Northbourne Racing. Whoever has no history in the static world (a generated
    // rookie, the player) is scored by their seat, which is the only thing known about them.
    // A GENERATED rookie doesn't exist in the static world, so their talent can't be
    // derived: the save carries it, since that's where it was invented. It goes FIRST because BaseTalent would return 0 and
    // fall through to the seat-based calculation -- and then they'd improve or worsen just by switching teams, which is
    // exactly the leak this just closed.
    auto gd = save.gen_drivers.find(id);
    double base = (gd != save.gen_drivers.end()) ? gd->second.talent : BaseTalent(id);
    if (base <= 0.0) {
        const double dentro = (skill - bal::RATING_SKILL_FLOOR) * (bal::RATING_SEED_TOP - bal::RATING_SEED_BOTTOM);
        const int t = (tier >= 1 && tier <= 7) ? tier : 1;
        base = (dentro > 0.0 ? dentro : 0.0) + (t - 1) * bal::RATING_TIER_STEP;
    }
    int birthYear = rosterBirthYear;
    auto by = save.birth_year.find(id);
    if (by != save.birth_year.end() && by->second != 0) birthYear = by->second;
    // And if there's still no year, use the POOL's. A driver the market seats in a series
    // they don't belong to by data doesn't show up in that series' record, so they used to come out with
    // UNKNOWN age -- and an unknown age is a 1.0 multiplier: they NEVER aged. Measured: Hamilton
    // would land in Formula Vee and stay frozen at 1712.2 for eight straight seasons, immortal.
    if (birthYear == 0) birthYear = BirthYearFor(id);
    // The per-class character margin also applies HERE. The comment above requires that
    // this projection and the rollover's compare "apples to apples"; if the rollover ages a driver
    // from endurance racing with the shifted curve and this doesn't, the same driver would have two different ratings
    // depending on who's asking -- and the player's displacement would get decided with the wrong one.
    // — THE CURVE IS EACH ONE'S OWN. Before this was `AgeCurveMultiplier`, the SAME ramp for the 1,400
    // drivers in the world: you went up for turning old and went down for turning old, and that was it. Now the
    // RISING half is each one's personal progress toward their ceiling —doubled by what happens to them on
    // track— and the DECLINE half stays where it was, in the hands of age, because the spec wants it
    // irreversible. Without an entry in `driver_curve` the product is exactly the one from before.
    const int edad = DriverAge(birthYear, worldYear);
    double mult = GrowthProgress(save, id, edad, ageShift) * AgeDeclinePart(edad, ageShift);
    auto rf = save.rival_form.find(id);
    double fdelta = (rf == save.rival_form.end()) ? 0.0 : (double)rf->second;
    // Age bites into TALENT (anchor + position), not the band's floor. Multiplying
    // RATING_SEED_BOTTOM's 1200 would make an elite veteran lose 800 points and drop below
    // karting level: they'd retire instead of dropping a category, exactly the opposite of what happens.
    // So declining costs you TIERS -- a tier-7 ace at 75% on the curve ends up level with a
    // tier-5 ace, which is a career arc, not a cliff.
    return bal::RATING_SEED_BOTTOM + base * mult
           + fdelta * (bal::RATING_SEED_TOP - bal::RATING_SEED_BOTTOM);
}

// ===================================================================================================
// — RESULTS COUNT (12/08)
// ===================================================================================================
//
// `agedRatingFor` is authored capability x age curve, and does NOT read the results. So until today the
// only way to decay was to turn old -- exactly the opposite of the decision already made («the arc
// is by PERFORMANCE, not by age»). It was not an oversight: until `RunWorldSeason` existed there were no
// results to read, because the world's ELO only moved where the player had raced.
//
// AND THE TWO MAGNITUDES DON'T LIVE IN THE SAME PLACE. Measured over 1,298 occupants:
//
//     capability  mean 1393  sd 105
//     ELO        mean 1513  sd 318          mean difference +119, and TRIPLE the dispersion
//
// Subtracting them raw would have given the WHOLE world a +119 --cap included--, so nobody would have dropped
// below the bar ever and the fix would have come out INERT. It is literally the same failure it already had with
// `RatingQuality` (means at 0.41 and triple the dispersion): a fixed bias disguised as a deviation by shape.
//
// So ELO is STANDARDIZED over the class and projected onto the capability scale of that same class. And from
// there comes the property that avoids the ratchet: the average driver gets delta ~0, so the class bar
// which is its mean-- doesn't drift by itself season after season.
//
// The scale is MEASURED (not hard-coded) and measured ONCE PER SERIES, not per driver: it is the same pattern as
// `EffectivePace` with its `EloScale`, and for the same reason.

// The two scales of ONE series, measured over its own occupants. Once per series: it is the same decision
// that `EffectivePace` took with its `EloScale`, and for the same reason (measuring it per driver would mean walking the entire
// field once per driver).
//
// A minimum of people is required because an sd taken from three cars is not a scale, it is noise: with the field
// too short `ok=false` is returned and the world behaves as before.
static FormScale measureFormScale(const std::string& seriesId, int tier, const SaveData& save, int worldYear) {
    FormScale e;
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return e;
    const std::map<std::string, std::pair<double,int>> meta = OccupantMeta(seriesId, tier);
    const int shift = ClassAgeShift(sd->class_name);
    std::vector<double> caps, elos;
    for (const auto& seat : SeatsForClass(seriesId, save)) {
        if (seat.open || seat.occupant_id.empty()) continue;
        if (save.retired.count(seat.occupant_id)) continue;
        const auto ie = save.driver_ratings.find(seat.occupant_id);
        if (ie == save.driver_ratings.end() || ie->second <= 0.0) continue;
        auto m = meta.find(seat.occupant_id);
        const double skill = (m == meta.end()) ? 0.85 : m->second.first;
        const int    birth = (m == meta.end()) ? 0    : m->second.second;
        caps.push_back(agedRatingFor(seat.occupant_id, skill, birth, save, worldYear, shift, tier));
        elos.push_back(ie->second);
    }
    if (caps.size() < 6) return e;                 // field too short: there is no scale to measure
    auto media = [](const std::vector<double>& v) {
        double s = 0.0; for (double x : v) s += x; return s / (double)v.size();
    };
    auto desv = [&](const std::vector<double>& v, double m) {
        double a = 0.0; for (double x : v) a += (x - m) * (x - m);
        const double s = std::sqrt(a / (double)v.size());
        return s < 1e-6 ? 1e-6 : s;
    };
    e.capMedia = media(caps);  e.capSd = desv(caps, e.capMedia);
    e.eloMedia = media(elos);  e.eloSd = desv(elos, e.eloMedia);
    e.ok = true;
    return e;
}

// EFFECTIVE rating: capability perturbed by results, clamped and weighted by the confidence that the
// races they have run give. Without a resolvable scale or without races it returns the capability as is, which is the
// usual behavior.
static double effectiveRating(const std::string& id, double capacidad, const SaveData& save,
                              const FormScale& esc, bool acotado) {
    if (!esc.ok) return capacidad;
    const auto ie = save.driver_ratings.find(id);
    if (ie == save.driver_ratings.end() || ie->second <= 0.0) return capacidad;
    int carreras = 0;
    const auto ic = save.driver_races.find(id);
    if (ic != save.driver_races.end() && ic->second > 0) carreras = ic->second;
    if (carreras <= 0) return capacidad;      // has never raced: there is no result to set against their promise
    const double confianza = (double)carreras / (double)(carreras + bal::FORM_CONFIDENCE_WINDOW);
    const double z = (ie->second - esc.eloMedia) / esc.eloSd;
    const double resultadoEnEscalaDeCapacidad = esc.capMedia + z * esc.capSd;
    double delta = (resultadoEnEscalaDeCapacidad - capacidad) * confianza;
    // ~~~07/09 (D2b): the cap belongs to the question «how much are you worth» (market). For the question «do you stay or
    //  not» the result enters whole, weighted only by confidence -- see ExpireSeatContracts.
    if (acotado) {
        if (delta >  bal::FORM_RATING_POINTS_CAP) delta =  bal::FORM_RATING_POINTS_CAP;
        if (delta < -bal::FORM_RATING_POINTS_CAP) delta = -bal::FORM_RATING_POINTS_CAP;
    }
    return capacidad + delta;
}

// The WEAKEST in-reach incumbent's aged rating in a class, plus the incumbent's seat occupant
// id. Walks the SAME occupant set SeatsForClass produces (its resolved Seat.occupant_id per seat, so the
// pinned-roster / cohort-fallback + persisted-occupancy logic is reused, not duplicated), computes each
// occupant's aged rating from the matching source struct (pinned skill / RosterForTier base_skill), and
// returns the MINIMUM (the backmarker the player would displace). Open seats (occupant cleared / kSeatOpen)
// and retired occupants are skipped (no incumbent to displace there). Returns false via the return value when
// the class has no incumbent (era-memory / fully open / unresolved series). Pure, deterministic (seats by
// index). "in-reach" (the reachable-tier band) is applied by the CALLER (GenerateOffers); this returns the
// single weakest occupant so the predicate can decide displacement against the player's aged rating.
bool WeakestIncumbentRating(const std::string& seriesId, const SaveData& save, int worldYear,
                            double& outWeakest, std::string& outWeakestId) {
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return false;
    if (IsEraMemorySeries(*sd)) return false;   // Gotcha: no historic/era seats in the market

    const std::map<std::string, std::pair<double,int>> meta = OccupantMeta(seriesId, sd->level);
    const int shiftClase = ClassAgeShift(sd->class_name);

    bool found = false;
    double weakest = 0.0;
    std::string weakestId;
    for (const auto& seat : SeatsForClass(seriesId, save)) {
        if (seat.open || seat.occupant_id.empty()) continue;    // open seat -> nobody to displace
        if (save.retired.count(seat.occupant_id)) continue;     // retired -> not an incumbent
        auto m = meta.find(seat.occupant_id);
        double skill = (m == meta.end()) ? 0.85 : m->second.first;
        int    birth = (m == meta.end()) ? 0     : m->second.second;
        double rr = agedRatingFor(seat.occupant_id, skill, birth, save, worldYear, shiftClase, sd->level);
        if (!found || rr < weakest) { found = true; weakest = rr; weakestId = seat.occupant_id; }
    }
    if (!found) return false;
    outWeakest = weakest;
    outWeakestId = weakestId;
    return true;
}

// Variable salary: the team's salary band from the living-world ELO of the field.
// Mirrors WeakestIncumbentRating's meta lookup (pinned skill / RosterForTier base_skill+birth_year) +
// agedRatingFor (this file) — same two occupant sources, same aged-rating projection, so the band tracks the
// SAME rating the rollover/displacement logic uses. teamMean's percentile within [fieldMin..fieldMax] maps
// onto [SALARY_TEAM_MIN_CENTER..MAX_CENTER]; unresolved (unknown/era-memory series, or the team has no rated
// occupant this rollover) falls back to the field's mid-band center with resolved=false.
SalaryBand TeamSalaryBand(const std::string& seriesId, const std::string& teamLabel,
                          const SaveData& save, int worldYear) {
    SalaryBand out{ (bal::SALARY_TEAM_MIN_CENTER + bal::SALARY_TEAM_MAX_CENTER) * 0.5, 0, 0, false };
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return out;
    if (IsEraMemorySeries(*sd)) return out;   // Gotcha: no historic/era seats in the market

    // meta (skill/birth) by occupant id -- same three sources SeatsForClass uses (see OccupantMeta).
    const std::map<std::string, std::pair<double,int>> meta = OccupantMeta(seriesId, sd->level);
    const int shiftClase = ClassAgeShift(sd->class_name);

    // rating per occupant; accumulate the field's [min..max] and the target team's sum/count.
    double fieldMin = 0, fieldMax = 0; bool any = false;
    double teamSum = 0; int teamN = 0;
    for (const auto& seat : SeatsForClass(seriesId, save)) {
        if (seat.open || seat.occupant_id.empty()) continue;
        if (save.retired.count(seat.occupant_id)) continue;
        auto m = meta.find(seat.occupant_id);
        double skill = (m == meta.end()) ? 0.85 : m->second.first;
        int    birth = (m == meta.end()) ? 0    : m->second.second;
        double rr = agedRatingFor(seat.occupant_id, skill, birth, save, worldYear, shiftClase, sd->level);
        if (!any || rr < fieldMin) fieldMin = rr;
        if (!any || rr > fieldMax) fieldMax = rr;
        any = true;
        if (seat.team_label == teamLabel) { teamSum += rr; teamN++; }
    }
    if (!any || teamN == 0) return out;                       // no data -> mid-band center (resolved=false)
    double teamMean = teamSum / (double)teamN;
    double p = (fieldMax > fieldMin) ? (teamMean - fieldMin) / (fieldMax - fieldMin) : 0.5;   // 0..1 percentile
    if (p < 0) p = 0; if (p > 1) p = 1;
    double center = bal::SALARY_TEAM_MIN_CENTER + p * (bal::SALARY_TEAM_MAX_CENTER - bal::SALARY_TEAM_MIN_CENTER);
    out.centerMult = center;
    out.minMult = center - bal::SALARY_BAND_HALFWIDTH;
    out.maxMult = center + bal::SALARY_BAND_HALFWIDTH;
    out.resolved = true;
    return out;
}

// MOVE OUT the displaced incumbent (mirrors PromoteFreedSeats' move shape). When the player
// takes a backmarker's seat, that incumbent is demoted a tier / freed (and if old enough, retires). Pure over
// the data tables; mutates ONLY save (seat_occupant + retired). Deterministic. The displaced incumbent's seat
// in `seriesId` is marked OPEN (kSeatOpen) — the player's displacement offer now reads as that open seat; the
// incumbent is dropped to the free-agent pool (added to save.retired if aged past the retire window, else just
// freed from this seat — a demotion the AI->AI re-fill at the next rollover resolves, like PromoteFreedSeats).
void DisplaceIncumbent(SaveData& save, const std::string& seriesId, const std::string& incumbentId,
                       int worldYear) {
    if (seriesId.empty() || incumbentId.empty()) return;
    // Find the incumbent's seat index in this class and mark it OPEN (the player will occupy it via the offer).
    std::vector<Seat> seats = SeatsForClass(seriesId, save);
    for (size_t i = 0; i < seats.size(); ++i) {
        if (seats[i].occupant_id == incumbentId) {
            save.seat_occupant[SeatKey(seriesId, (int)i)] = kSeatOpen;
            break;
        }
    }
    // Move the incumbent out: if aged past the retire window, he retires; otherwise he is a free agent (demoted),
    // which the next rollover's PromoteFreedSeats re-fill resolves. Mirror the rollover's birth-year resolution.
    int birthYear = 0;
    auto by = save.birth_year.find(incumbentId);
    if (by != save.birth_year.end()) birthYear = by->second;
    int age = DriverAge(birthYear, worldYear);
    // 23/09: and whoever isn't on the driver market doesn't retire for this either (FueraDelMercadoDePilotos).
    if (birthYear != 0 && age >= bal::ACTIVE_MAX_AGE && !FueraDelMercadoDePilotos(incumbentId))
        save.retired.insert(incumbentId);
    // (else: demoted/free agent — no extra state; he simply no longer occupies this seat.)
}

// Bidirectional job security. The player LOSES the seat (not renewed / replaced) iff he
// UNDER-performed the contract objective (perf < bar) AND is NOT shielded by a negotiated protection clause
// (guaranteed_seat OR anti_replace, from accepted_deal). An on-target/over-performing player (perf >= bar)
// keeps the seat regardless of protection; a protected under-performer keeps it (the team honors the clause).
// Pure and deterministic (no RNG, no state) -- consumes the existing negotiated demands.
bool PlayerLosesSeat(double perf, double bar, bool guaranteedSeat, bool antiReplace) {
    return perf < bar && !guaranteedSeat && !antiReplace;
}

} // namespace career
