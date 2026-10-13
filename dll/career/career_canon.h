#pragma once
// career_canon — of the canonical rework (of the 2026-08-09 design).
//
// WHAT IT IS: the canonical model of (identity, state, seat, slot, contract) built ON TOP of
// today's data, without touching it. None of this decides anything yet: it is a READING of the current world
// expressed in the vocabulary we are heading to. Zero behavior changes, and that is the criterion of the
// phase, not a limitation.
//
// WHAT FOR: to be able to MEASURE. The lesson of the five attempts of 09/08 (kHeadOpen, ace cap, lowering
// the affinity, unifying the id, seeding by level) is that all of them were measured AFTER being written, one by
// one, and all five were reverted. The census below measures the same things BEFORE, on the real
// save, and with a number that can be compared across phases. Without this, the .5 and the D are validated by eye
// again.
//
// WHAT IT DOES **NOT** DO, and this is deliberate:
//   · It does not invent data. A canonical field that today has no source is declared absent (`Gaps`), not
//     filled in with a plausible value. The list of absences IS the work of phases B/C/D.
//   · It does not replace anyone. The engine keeps reading what it read; this layer observes it.
//   · It does not persist. It is built from the save and the loaded tables, and thrown away.
//
// Pure: no ImGui, no game, no SHM. Deterministic (ordered walks, no RNG).
#include <string>
#include <vector>
#include <map>
#include <set>

namespace career {

struct SaveData;   // career_save.h — taken by const reference; we do not drag in its chain of includes

namespace canon {

//
// Vocabulary ()
//

// `origin` answers whether the PERSON is real or fictional. Immutable once an id is published.
enum class Origin { Real = 0, Fictional = 1, Count = 2 };

// `population_role` answers HOW THEY ENTER the world. It is an axis different from `origin`, and confusing them is
// precisely the mistake the engine has today: the cohort (rk_*) is fictional AND enters through its own door, but
// an authored fictional can be an incumbent just like a real one.
enum class PopRole { Incumbent = 0, Cohort = 1, Reserve = 2, Overflow = 3, Count = 4 };

// DERIVED state, never persisted.
enum class LifeState { Active = 0, FreeAgent = 1, Retired = 2, Dormant = 3, Count = 4 };

// `primary_affinity` / families. The order fixes the index of affinity_vector and family_experience.
enum class Family { OpenWheel = 0, GT = 1, Prototype = 2, Touring = 3, Oval = 4, Historic = 5, Count = 6 };

const char* OriginName(Origin o);
const char* RoleName(PopRole r);
const char* StateName(LifeState s);
const char* FamilyName(Family f);
// "openwheel|gt|prototype|touring|oval|historic" -> family. false if the string is not a known family
// (and then `out` is not touched): whoever asks must be able to tell "I don't know" from "it's single-seater".
bool FamilyFromName(const std::string& s, Family& out);

//
// Entities ()
//

// — static catalog, global and append-only.
struct DriverIdentity {
    std::string driver_id;              // stable and immutable key
    std::string display_name;
    std::string country;
    Origin  origin = Origin::Fictional;
    PopRole population_role = PopRole::Incumbent;
    int birth_year = 0;                 // 0 = no data (it happens today; it is seeded once)
    int authored_retire_year = 0;       // 0 = no mandatory historical retirement
    unsigned long long genome_seed = 0; // derived from the id (career_genome.h), never from the name
    Family primary_affinity = Family::OpenWheel;
    // false = the affinity is NOT in the data and was DERIVED from the initial seat ("main
    // family of its initial seat and a data warning"). Telling them apart matters: a derived affinity cannot
    // be used as evidence of anything, and today the whole cohort falls into this case.
    bool affinity_known = false;
    std::string archetype;              // ProfileForArch vocabulary; "" = neutral
    std::string era_scope;              // "modern" | "historic" | "both"
    // 23/09: REAL status of a reserve person according to docs/career/reserve_people.psv
    // (career::ReserveStatus: 0 uncurated, 1 active, 2 retired, 3 unknown). Only filled in for the reserve.
    int reserve_status = 0;
};

// — per-save state. Absence of state for a reserve means "dormant".
struct DriverCareerState {
    std::string driver_id;
    // separates `pace_rating` (latent capacity) from `results_rating` (observed ELO). TODAY THEY ARE THE SAME
    // NUMBER: `save.driver_ratings` is the only thing that exists, and both the XML's talent
    // (RatingQuality) and the world ranking come out of it. That fusion is the ; here it is reflected as is, with
    // both fields pointing to the same value, so that the day they split the census shows it.
    double pace_rating = 0.0;
    double results_rating = 0.0;
    int    results_races = 0;
    double reputation = 0.0;
    int debut_year = 0;
    int last_active_year = 0;
    LifeState state = LifeState::Dormant;
};

// — stable address of a paint within AMS2's content. The catalog numbering and the one
// of the game are converted ONLY inside the skins resolver: here nothing is added or subtracted by one.
struct LiverySlotId {
    int vehicle_hash = 0;
    int catalog_livery_id = -1;
    bool resolved() const { return vehicle_hash != 0 && catalog_livery_id >= 0; }
};

// — static catalog of possible slots.
struct SeatDefinition {
    // recommended key: series_id | vehicle_hash | catalog_livery_id. Gradually replaces
    // "series_id#seat_idx": the index changes when the catalog is reordered, the slot is the physical identity.
    std::string seat_id;
    // The bridge to today's world: the key "<series_id>#<idx>" with which save.seat_occupant stores the
    // occupancy. It exists ONLY during the migration (); when the save speaks seat_id, it goes away.
    std::string legacy_key;
    std::string series_id;
    std::string class_name;
    std::string team_id;                // today: the team label that TeamLabelFor resolves
    int tier = 1;                       // SeriesDef.level (1..7)
    LiverySlotId livery_slot;
    std::string livery_name;            // the vanilla livery the slot comes from (diag)
    int seat_number = 0;                // index within the class (SeatsForClass order)
    bool historical = false;            // era series / memory: does not rotate with the living world
};

// — minimal persistent occupancy state.
struct SeatAssignment {
    std::string seat_id;
    std::string driver_id;              // "" = vacant
    int contract_start_year = 0;
};

// — domain VIEW derived from SeatAssignment. It is not a duplicated record.
struct DriverContract {
    std::string driver_id;
    std::string seat_id;
    std::string series_id;
    std::string team_id;
    int tier = 1;
    int start_year = 0;
    bool primary = true;                // only one per driver; the rest are secondary
    bool player = false;                // the seat is occupied by the player (PlayerSeatKeys)
};

//
// The canonical world
//

struct World {
    int year = 0;
    std::map<std::string, DriverIdentity>    identities;   // driver_id -> identity
    std::map<std::string, DriverCareerState> states;       // driver_id -> state
    std::vector<SeatDefinition>              seats;        // deterministic order (series, index)
    std::map<std::string, SeatAssignment>    assignments;  // seat_id -> occupancy
    std::vector<DriverContract>              contracts;    // derived; order (driver_id, seat_id)

    // THE SEEDING AS THE ENGINE DOES IT, not a reimplementation. It comes out of calling
    // career::SeedAllDrivers on an empty map: same code as production, so what the census measures
    // is what really happens. An id ABSENT from this map is a driver who runs WITHOUT a seeded rating and
    // whose talent comes from the static curve — the two scales of [[two_talent_scales_in_one_grid]].
    std::map<std::string, double> engine_seed;

    // Index seat_id -> position in `seats`. With repeated seat_ids it points to the FIRST, just like
    // `assignments`, so that the two views of the same seat never disagree.
    std::map<std::string, size_t> seat_index;

    const SeatDefinition* SeatById(const std::string& seat_id) const;
    const DriverIdentity* Identity(const std::string& driver_id) const;
    const DriverCareerState* State(const std::string& driver_id) const;
};

// Builds the canonical world from the save + the already-loaded tables (AllSeries/SeatsForClass/the identity
// pool/the authored profiles). It does NOT load files: the caller must have done the season data load,
// just as the engine does. Pure except for that dependency on already-populated globals.
World BuildWorld(const SaveData& save);

//
// Cross validators (hard errors · fallbacks with warning · invariants)
//

enum class Severity { Error = 0, Warning = 1 };

struct Finding {
    Severity sev = Severity::Warning;
    std::string code;      // stable and searchable: "seat_no_slot", "two_primaries", ...
    std::string subject;   // the driver_id / seat_id / class it refers to
    std::string detail;
};

// All the findings, sorted (severity, code, subject). An Error is a content failure that
// requires blocking the write of the affected XML; a Warning is a safe fallback that has been applied.
std::vector<Finding> Validate(const World& w);

//
// Census (· )
//

struct Census {
    int drivers = 0;
    int by_origin[(int)Origin::Count] = {0};
    int by_role[(int)PopRole::Count] = {0};
    int by_state[(int)LifeState::Count] = {0};
    int by_family[(int)Family::Count] = {0};
    int affinity_derived = 0;      // affinity that does NOT come from the data
    int birth_year_missing = 0;
    // — where each identity comes from and how many change sides. `reclassified` is THE number of the
    // phase: people that the catalog had as `kind=real` and that the authored corpus declares invented ().
    int identity_from_pool = 0, identity_from_field = 0, identity_authored = 0;
    int identity_reclassified = 0, identity_name_collisions = 0, identity_authored_shared = 0;

    int seats_total = 0;
    int seats_occupied = 0;
    int seats_open = 0;
    int seats_slot_unresolved = 0; // seat without slot -> hard error
    int seats_dup_id = 0;          // seats COVERED for sharing a seat_id with another
    int seats_historical = 0;
    int seats_by_tier[8] = {0};    // index = tier 1..7 (0 unused)
    int occupied_by_tier[8] = {0};

    int contracts = 0;
    int drivers_multi_seat = 0;    // more than one seat (one main + secondaries)
    int cohort_above_tier2 = 0;    // / "no cohort driver starts above tier 2"

    // THE TWO SCALES, which is the measure this phase exists for.
    int occupants = 0;             // distinct drivers occupying a seat right now
    int occupants_unseeded = 0;    // ...and have NO seeded rating -> they fall to the static curve
    int classes_with_seats = 0;
    int classes_mixing_scales = 0; // classes where seeded and unseeded coexist IN THE SAME grid
    double seed_real_lo = 0.0, seed_real_hi = 0.0, seed_real_p50 = 0.0;    // level-anchored seeding
    double seed_cohort_lo = 0.0, seed_cohort_hi = 0.0, seed_cohort_p50 = 0.0; // CastSeedFromBase
    // ELO range WITHIN a class, which is what the grid turns into a fan. The worst case measured
    // on 09/08 was 802 points without counting the cohort, with LEVEL_SEED_SPREAD at 350.
    double class_seed_range_max = 0.0;
    std::string class_seed_range_worst;

    int errors = 0;
    int warnings = 0;
};

Census TakeCensus(const World& w, const std::vector<Finding>& findings);

// THE GRID CENSUS, which is a DIFFERENT world from the seats one and both have to be measured.
//
// The first census uncovered it: over seats, the cohort does NOT go beyond tier 2 (seats respect the cap)
// and only 29 of 77 classes mix scales. Over the COMPOSED GRID —the one written to the XML and the one that
// runs— the cohort comes out ahead in level 5 classes and the mix reaches 70 of 100. It is not a
// contradiction: they are two different populations, because `ComposeSeasonGrid` takes people from the static
// files without going through the seats model. That divergence IS the finding, and the closes it.
//
// Composing ~100 grids is expensive, so it goes apart from the cheap census and is requested when you want to measure.
struct GridCensus {
    int classes = 0;                  // classes with a composable grid (>= 8 cars)
    int rows = 0;                     // grid rows in total
    // IT IS MEASURED AS THE COMPOSER SEES IT, not as the seeder sees it, and the difference is not subtle: the
    // seeding resolves the id by name and the composer does NOT (it passes the raw `fd.id`, empty in the PSVs). Measuring
    // with the good resolution gives 153 rows without ELO where the composer sees 792. An instrument that measures the
    // world we would like is worse than not measuring.
    int rows_unseeded = 0;            // rows that the COMPOSER resolves to the static curve
    // ...and of those, how many HAVE a seeded ELO waiting for them, only under an id the composer does not use.
    // It is the exact size of the available fix: the shared resolution already exists (FieldOccupantId).
    int rows_seedable_by_name = 0;
    int classes_mixing_scales = 0;    // classes where seeded and unseeded coexist IN THE SAME car
    int cohort_rows = 0;
    int cohort_rows_above_tier2 = 0;  // cohort running in a class of level > 2 (/ )
    int classes_cohort_leads = 0;     // classes where the grid's highest TALENT belongs to the cohort

    // The two talent medians, which is the gap of [[two_talent_scales_in_one_grid]]: whoever has ELO
    // comes out of RatingQuality(elo) and whoever does not, from the static curve. Measured on 09/08: 0.407 against 0.842.
    double talent_p50_seeded = 0.0, talent_p50_unseeded = 0.0, talent_gap = 0.0;

    // "the bar is derived, not curated". u = (ace talent - backmarker talent) / the class's ref_span.
    // If the bar followed the scale, u would hover around 1 in all of them; measured, it goes from 0.69 to 3.97. It is THE
    // number of the , and the one that decided that the five attempts of 09/08 were patches.
    double u_min = 0.0, u_max = 0.0, u_p50 = 0.0;
    int classes_u_over_2 = 0;
    std::string u_worst_class;
    // The same count with the PURE DERIVED bar, without the curated trim. If the derivation describes well what
    // the seeding produces, this hovers around 1 in all classes; what deviates is what the trim corrects.
    // It is the number that separates "this class is truly different" from "the bar was divorced from the scale".
    double ud_min = 0.0, ud_max = 0.0, ud_p50 = 0.0;
    std::string ud_worst_class;
    // The same count but ONLY among those who are not from the cohort. If taking it out makes the width drop
    // suddenly, the excess is not put there by the talent model: it is put there by the academy running in classes far
    // above their rating band — which is placement (), not performance ().
    double ur_min = 0.0, ur_max = 0.0, ur_p50 = 0.0;
    int classes_with_cohort = 0;
};

GridCensus TakeGridCensus(const World& w, const SaveData& save);
std::string GridCensusText(const GridCensus& g);

//
// Dumps ("the engine/harness must be able to dump")
//

// driver_id -> identity / state / contracts. Empty if the id is not in the world.
std::string DumpDriver(const World& w, const std::string& driver_id);
// seat_id -> team / slot / occupant. Also accepts the legacy key "<series>#<idx>".
std::string DumpSeat(const World& w, const std::string& seat_id);
// The census in plain text, one line per axis. It is the format that is compared across phases.
std::string CensusText(const Census& c);
// The findings, one per line, already sorted. `maxPerCode` bounds the repetition (0 = all).
std::string FindingsText(const std::vector<Finding>& f, int maxPerCode);

// has fields that TODAY HAVE NO SOURCE in the engine. Returning them as a list, and not filling them in by eye,
// is what keeps the census honest: each line is the work of a later phase, with the phase written down.
std::vector<std::string> Gaps();

}   // namespace canon
}   // namespace career
