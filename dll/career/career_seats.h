#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <utility>
#include "career_save.h"   // SaveData (seat occupancy lives on the persisted save)
#include "career_balance.h" // Variable salary: bal::SALARY_TEAM_MIN_CENTER/MAX_CENTER default for SalaryBand
#include "career_fields.h"  // RosterDriver (the candidates contesting the rollover's seats)

// The living-seat-market model: named/derived teams + seats + occupancy,
// layered over the existing aging/retirement/promotion engine (career_fields + RolloverYearReport).
// Pure: no ImGui, no game, no SHM. Mirrors the career_fields.{h,cpp} house style (POD struct + pure
// predicates, deterministic, no RNG — use fnv1a where a draw is needed). Tunables in career::bal
// (DISPLACEMENT_MARGIN, JOBSEC_PERF_BAR). Team naming is best-effort over docs/livery_team_equiv.psv +
// docs/series_rosters.psv (precedence: real_team -> alias -> livery_name ->
// "Privateer #"+car_number -> TR fallback). ALL tiers generate seats regardless of name.
//
// The bodies in career_seats.cpp implement the real named/derived team+seat+occupancy
// model over the existing PSVs + RosterForTier. PlayerDisplacesIncumbent / PlayerLosesSeat are still RED
// stubs, implemented separately below.
namespace career {

// fwd: PreseasonWindowChurn takes the live CareerCalendar (cursor_doy/year) by reference; the full type
// lives in career_calendar.h (not included here to avoid dragging its dependency chain into every TU that
// only wants seats). Mirrors career_simrunner.h's forward-declare shape.
struct CareerCalendar;

// One seat in a class: the team it belongs to (named or derived), its current occupant (driver id, "" =
// open), the seat's tier (1..7), and whether it is currently open (occupant retired/promoted/displaced).
struct Seat {
    std::string team_label;    // best-effort team name (real_team / alias / livery_name / "Privateer #N")
    std::string occupant_id;   // stable pool driver id occupying the seat ("" = open)
    int  tier = 1;             // ladder tier of the seat (1..7)
    bool open = false;         // true = a free seat (offer-eligible this rollover)
    // THE LIVING-WORLD -> GRID BRIDGE: this seat's LIVERY. It's the key both sides already
    // shared without saying so -- SeatsForClass binds seat<->livery (seat i carries livery i), and
    // ComposeSeasonGrid binds driver<->livery -- but since the seat didn't carry it, nobody could cross
    // the two halves. Without this field the market and the tarmac talk about different people: measured with the
    // simulator, in 66 of the dominant profile's races, the teammate the record assigned you was NOT in
    // the grid. Derived, never persisted: it comes from the class catalog just like the rest of the seat.
    std::string livery_name;
};

// THE LIVING-WORLD -> GRID BRIDGE. Who occupies each LIVERY in the series according to the living world, ready for
// grid composition to honor. `livery_name -> occupant_id`, skipping open seats and ones that
// don't resolve a livery.
//
// Why it exists: `ComposeSeasonGrid` pulls people from the static files and never once mentions
// `SeatsForClass` or `seat_occupant`, so the driver the market seats NEVER showed up in the
// race. It's the note the roadmap already flagged ("the cascade today is a market model that doesn't touch
// the tarmac"), and it shows the moment you measure it: the teammate the record assigns you wasn't in the
// grid in 66 of the dominant profile's races.
//
// A MAP is returned instead of letting `career_fields` look at the save directly: that layer is pure and
// knows nothing of seats or the save, and whoever has both in front of them is the caller. Same pattern as
// `driverRatings`, which already comes in as a parameter to the composition.
std::map<std::string, std::string> SeatOccupantsByLivery(const SaveData& save, const std::string& seriesId);

// — THE SAME BUT IN ORDER, which is what composition by seats needs. The map
// above says who drives which car; it doesn't say in what ORDER the seats are, and that order is the position in the
// series standings — the one that sets the class curve. Same two guards as the map: free seats are skipped,
// as are those that don't resolve a livery and YOURS (nobody else sits in your car).
std::vector<SeatRow> SeatRowsForSeries(const SaveData& save, const std::string& seriesId);

// — the DERIVED occupants of a series, in seat order and WITHOUT a save. It is the static world
// as seen by the seat layer: who sits in each car before the market moves anything.
//
// It exists for SEEDING. When we moved to one seat per racing car 370 new slots appeared, and those that
// neither the pinned set nor the field cover are occupied by the series' own people (`fl_*`) that `SeedAllDrivers` didn't
// walk: 223 grid rows without ELO, falling to the static curve while their rivals in the same car
// came from RatingQuality(ELO). It is the same disease that closed .5, only in the new people.
std::vector<std::string> DerivedSeatOccupants(const std::string& seriesId);

// The liveries occupied by THE PLAYER in that series. The composition swaps them for a free one so there aren't
// two cars with the same paint: the seat crossover only renames, so without this the livery
// you signed kept showing up in the AI grid too. Empty = you have no seat in that series.
std::set<std::string> PlayerLiveriesForSeries(const SaveData& save, const std::string& seriesId);

// 18/08 — COMPOSING A SERIES' GRID FROM THE SAVE, with nothing that can be forgotten.
//
// `ComposeSeasonGrid` takes FIFTEEN arguments and the last TEN have defaults. When the caller has
// a `SaveData` in hand, those ten are not optional: they are ALWAYS the same, derivable from the save and from
// the series. And forgetting one doesn't give an error, it gives a different, silent grid. History of the failure, all
// measured on `Juniper_Cup`:
//   · without `seats` the composition by seats doesn't kick in and four from the cohort come out with talent 0.30-0.38;
//   · without `driverRaces`, `EffectivePace` cuts short and returns `RatingQuality(ELO)` whole -> the authorship is
//     discarded (the same person with 0.8792 by one path and 0.9839 by the other);
//   · without `skipLiveries`, an AI car comes out with YOUR same paint and AMS2 chooses on its own which of the
//     two it leaves out;
//   · without the `driverCurve`/`birthYear`/`worldYear` trio (18/08) the clone enters by its CEILING instead of by
//     where it is, and the card channel disagrees with the ELO channel about the same man.
// 19/08 — the counts in this paragraph said «twelve and six» and went stale when the trio was added. Today they
// are fifteen and ten: EIGHT come whole from the save and the series (`rivalForm`, `driverRatings`, `role`,
// `seats`, `skipLiveries`, `driverRaces`, `driverCurve`, `birthYear`), `worldYear` comes from the save with the
// calendar year as a fallback (see below), and `exclude` goes to `nullptr` on purpose — the multiclass
// exclusion is decided by each writer, not by this gate.
// Three overlay writers had to remember all of them. Two fixed it on 17/08 and even so they
// left out `skipLiveries`; the third (`WriteChampGrid`) kept calling with nine arguments while its
// own comment said it composed «with the SAME production pipeline as the rest». With this gate
// there is nothing to remember: the level, the class role, the seats, your liveries, the ratings, the races,
// the growth curve, the births, the world year and the `rival_form` all come from in here.
//
// It lives in the SEATS layer and not in `career_fields` on purpose, for the same reason as
// `SeatRowsForSeries`: `career_fields` is pure and knows neither seats nor the save. The one that has both
// things in hand is this layer.
//
// `gridSize` does come in as a parameter: each writer resolves it with its own fallback (slots of the class,
// event opponents, number of liveries) and that decision isn't made here. So does `playerForm`, although today
// `ComposeSeasonGrid` no longer uses it — it is kept so as not to change the contract silently.
// Unknown series -> empty vector, same as `ComposeSeasonGrid`.
//
// 18/08 — `calYear` (THE CALENDAR YEAR) is the fifth, and it is a FALLBACK, not the source. The save brings
// the growth curve and the birth year of each one — everything needed for the clone to enter
// where it is and not by its ceiling — but the WORLD YEAR is not in `SaveData` until the first rollover
// writes `current_world_year`, so there was one datum of the three that this gate couldn't get from the
// save. It is resolved inside with the SAME rule the rest of the layer already uses
// (`save.current_world_year != 0 ? save.current_world_year : calYear`, see `WindowSeatPerRace`), so
// passing it can't contradict the save: it only covers the gap of the first season. 0 (the default) =
// as it was, and in an already-run save nothing changes either because the save's year rules.
std::vector<GridDriver> ComposeSeasonGridForSave(const SaveData& save, const std::string& seriesId,
                                                 int gridSize, double playerForm = 0.0, int calYear = 0);

// FINGERPRINT OF THE WORLD'S SEAT ASSIGNMENTS. Sums up, in one number, WHO drives WHAT car across the whole
// ladder -- exactly what decides the grid written out to the XML.
//
// It exists because AMS2 reads CustomAIDrivers at startup and never hot-reloads it, and because the app lives
// INSIDE the game: while you're playing, AMS2 is always open, so a signing made through the window can never
// reach the track until a restart. Comparing this fingerprint against `save.grids_stamp` tells you whether the XML
// files on disk have gone stale, with no "needs a restart" flag to store that someone has to remember
// to set and clear.
//
// Deterministic, no RNG. Includes the PLAYER's seats too (you signing a seat also changes the grid:
// whoever held it stops racing there).
unsigned long SeatOccupancyStamp(const SaveData& save);

// Do the grid XMLs written to disk reflect the world as it is RIGHT NOW? false = the seat assignments have
// changed since they were written, so what races on track isn't what the paddock says.
bool GridsAreStale(const SaveData& save);

// The persisted vacancy SENTINEL written into SaveData.seat_occupant to mark a SPECIFIC
// team seat as OPEN (the living-world market freed it via retirement/promotion). A seat_occupant entry
// holding this sentinel makes SeatsForClass flag that seat `open` even though the static roster would derive
// an occupant for it. Distinct from an ABSENT key (use the derived occupant) and an EMPTY value (no
// override). Deterministic, additive (write-when-present); back-compat saves never carry it. NOT a real
// driver id, so it never collides with a pool id (the "\x1f" unit separator never appears in an id).
extern const char* const kSeatOpen;   // "\x1f__open__"

// Load docs/livery_team_equiv.psv (class_name|livery_id|livery_name|alias|real_team|car_number|
// year|confidence|sheet) — the team-naming source. Clears its table; returns false if the file does not
// open. Mirrors LoadCircuitGeo's loader shape. Idempotent (last write wins per class+livery key).
bool LoadLiveryTeamEquiv(const char* path);
int  LiveryTeamEquivCount();   // nr of rows loaded (diag/test)

// LAYER 0 of team naming: docs/skins/livery_resolved.psv, the already-merged verdict for
// (class, livery) -> team. It overrides livery_team_equiv and the rest of TeamLabelFor's cascade,
// which without it ends up returning the car's name or "Privateer #N" -- neither of which is a team.
// Optional: if the file isn't there, the usual cascade still works exactly the same.
bool LoadLiveryResolved(const char* path);
int  LiveryResolvedCount();
// 23/09: the livery the pack paints with THIS driver's name in that class (col 7 of livery_resolved),
// or empty. It is the slot truth of pack_slot_identity_rule, read backwards.
std::string ResolvedLiveryForDriver(const std::string& className, const std::string& driverName);
// ONLY the curated layer -- the team the user tied to THAT livery, or empty. It exists apart from
// TeamLabelFor because that one drags the whole cascade behind it (car catalog, packs, "Privateer #N"), and in
// a generation class that cascade is cut off ON PURPOSE. What must always come through is the
// hand-curated data: if you said Garrowby Nox98 #7 belongs to Tipco, it belongs to Tipco.
std::string ResolvedTeamFor(const std::string& className, const std::string& liveryName);

// Load docs/ams2_liveries.psv (class_name|xml_name|livery_name|livery_id|display) into the
// FULL livery catalog — class_name -> ordered DISTINCT car/manufacturer labels, each derived from livery_name
// by stripping a trailing " #<number>" ("Ironmarch Sol974 " -> "Ironmarch Sol974"). Dedups per class
// preserving first-seen order. Clears its table; returns false if the file does not open. Pure, deterministic,
// no RNG. This is the reliable FALLBACK team-naming source for REAL classes that lack manual livery_team_equiv
// data (a GT3 offer shows "Ironmarch Sol974" instead of a cohort homage). Mirrors LoadLiveryTeamEquiv's shape.
bool LoadLiveryCatalog(const char* path);
int  LiveryCatalogClassCount();   // number of classes in the catalog (diag/test)

// Load docs/livery_team_equiv_packs.psv (class_name|real_team, '#'-comments) — the REAL teams
// extracted from the lantern_series_8801 NAMeS pack CustomAIDrivers XMLs (generated by tools/gen_team_equiv_packs.py:
// team = livery_name minus a trailing " #<number>"). class_name -> ordered DISTINCT real teams. This tier sits
// BELOW the manual livery_team_equiv (layer 1) and ABOVE the catalog car-labels: a packs-covered class lacking
// manual data is upgraded from a car-label ("BRACKENRIDGE RAV277") to a real team ("Inception Racing"). NO FABRICATION.
// Clears its map FIRST (a missing/bogus/null path deterministically empties it -> a test can isolate the
// catalog layer), then returns false if the file does not open. Pure, deterministic, no RNG.
bool LoadLiveryTeamPacks(const char* path);
// A CURATED classification (docs/pack_team_class.psv) mapping pack labels to real teams,
// clean or EMPTY (driver/car -> fictional). MUST be loaded after LoadLiveryTeamPacks. Hand-editable by the maintainer.
bool LoadPackTeamClass(const char* path);
int  PackTeamClassCount();   // number of labels classified (diag/test)
int  LiveryTeamPacksClassCount();   // number of packs-covered classes (diag/test)

// Team<->team relationship graph (docs/team_relationships.psv). Symmetric (both directions
// inserted). Names validated against pack_team_class's real teams; unknown/self/invalid-kind entries
// get discarded. TeamRelationship propagation consumes it later; here it's only loaded and exposed.
struct TeamRelEdge { std::string other; std::string kind; };  // kind: family|ally|rival|bitter
bool LoadTeamRelationships(const char* path);
int  TeamRelationsCount();
const std::vector<TeamRelEdge>& TeamRelationsFor(const std::string& team);

// Prestige (proxy) = ally-degree, plus paths toward big teams (for the signing UI). Pure functions.
int TeamAllyDegree(const std::string& team);   // number of family/ally edges for the team
std::vector<std::pair<std::string,int>> TeamPathways(const std::string& team, int topN); // reachable via allies <=2 hops with a higher degree than your own, ordered by degree desc (then name), top N

// Load docs/generation_classes.psv (one class_name per line, '#'-comments) into the
// GENERATION exclusion set — the base/entry/spec/junior/kart classes that KEEP cohort homage labels and must
// NOT receive catalog car-labels. Clears its set; returns false if the file does not open. The maintainer edits
// that PSV to move a class between "generation" (cohort) and "real" (catalog car-labels), no code change.
bool LoadGenerationClasses(const char* path);
int  GenerationClassCount();   // number of generation classes loaded (diag/test)
bool IsGenerationClass(const std::string& className);   // true -> keep cohort labels (catalog skipped)

// Load docs/team_names_pool.psv (one FICTIONAL team name per line, '#'-comments) — the
// deterministic name pool for UNCURATED cohort/generation seats. Without it those seats collapse to a single
// shared cohort DRIVER name as the "team" (e.g. "Luz Verdano" repeated across dozens of series, since
// RosterForTier returns the same cohort per tier and the seat-pick lands on the same index). Clears the pool
// FIRST; returns false if the file does not open (an empty pool -> FictionalTeamName returns "" -> the caller
// keeps the legacy homage/name fallback, NO regression). Pure, deterministic, no RNG.
bool LoadTeamNamesPool(const char* path);
int  TeamNamesPoolCount();   // number of names loaded (diag/test)

// A DETERMINISTIC fictional team name for an uncurated seat, keyed by (seriesId, seatIdx) via fnv1a so each
// SERIES gets DISTINCT, STABLE names (fixes the cross-series collapse to one cohort driver name). Returns "" when
// the pool is empty (not loaded) so the caller keeps its legacy homage/name fallback. Pure, no RNG.
std::string FictionalTeamName(const std::string& seriesId, int seatIdx);

// (team curation): load docs/livery_vehicle_map.psv (class_name|livery_name|veh_hash|livery_id,
// generated by tools/gen_livery_vehmap.py from the official dump) — the EXACT livery -> vehicle binding:
// veh_hash = the car model's vehHash, livery_id = the game's livery NN (== the <slug>_livery_NN.dds number
// the overlay's CareerVehicleLiveryList scans). Clears its table; returns false if the file does not open.
// Mirrors LoadLiveryTeamEquiv's loader shape. Pure, deterministic.
bool LoadLiveryVehicleMap(const char* path);
// the vanilla livery_name for a (veh_hash, NN) slot within a class -- the inverse of the exact binding,
// with a fallback to livery_resolved. "" = unknown (fail-open). Used by the exclusion of the DISPLACED driver
// by the player in simulated rounds (RacedGridForRound, career_overlay).
std::string LiveryNameForSlot(const std::string& className, int vehHash, int liveryNN);
int  LiveryVehicleMapCount();   // nr of rows loaded (diag/test)

// "real skins" accessor over g_liveryVehicle -- resolves the VANILLA (class_name, livery_name) to its
// exact (vehHash, liveryNN). false if the pair isn't in the map (unknown class/name). Pure, no I/O.
bool LiveryVehicleFor(const std::string& className, const std::string& liveryName,
                      int& outVehHash, int& outLiveryNN);

// one CURATED seat option for a real team — the actual car model + skin the team fields.
struct TeamSeatOption {
    int vehHash  = 0;          // the car model (veh_hash from livery_vehicle_map)
    int liveryNN = -1;         // the game livery id / DDS NN (the overlay maps NN -> ordinal at runtime)
    std::string livery_name;   // the vanilla livery this option came from (diag/trace)
};

// the CURATED seat options for a (class, team-label) pair — the real team's own car(s)+skin(s), so the
// offer/contract seat drives THE team's car instead of a fixed-random class pick. Resolution:
//   (1) walk the class's livery_team_equiv rows, resolving each row's label with the SAME precedence
//       TeamLabelsForClass uses (real_team -> alias -> livery_name); every row whose label == teamLabel
//       contributes its livery, mapped through livery_vehicle_map -> {vehHash, liveryNN}. A team fielding
//       2+ cars yields 2+ options (deterministic, key-sorted order).
//   (2) if (1) yields nothing, try teamLabel as a VERBATIM livery_name in livery_vehicle_map (pinned-roster
//       classes with no equiv data surface raw livery names as labels — those map 1:1 to their livery).
// EMPTY when the team has no curated livery (packs/catalog/cohort labels) -> the caller keeps its legacy
// fixed-random fallback. Pure, deterministic, no RNG. Liveries absent from the vehicle map are skipped.
std::vector<TeamSeatOption> TeamSeatOptionsFor(const std::string& className, const std::string& teamLabel);

// from the portal: with a car rule a team can only seat you in THAT car. Returns the options whose
// vehHash == ruleVehHash (same order). If none remains (the team has no curated livery of that car) it returns
// EMPTY so the caller falls to its fixed pick, restricted to the rule's car if installed; if not, to all
// the class, like VehiclesForRace (fail-open). ruleVehHash 0 = opts untouched.
std::vector<TeamSeatOption> FilterSeatOptionsByCar(const std::vector<TeamSeatOption>& opts, int ruleVehHash);

// Feeder teams: loads docs/feeder_team_map.psv (class_name|livery_name|kind|team_label) — the curated map
// of livery->team for feeder classes. Clears its table; false if it doesn't open (empty map -> back-compat: FeederTeamFor
// returns "" and resolution falls back to the previous behavior). Mirrors LoadLiveryTeamEquiv. Pure, deterministic.
bool LoadFeederTeamMap(const char* path);
int  FeederTeamMapCount();   // number of rows loaded (diagnostics/test)

// TEAM label for a feeder-class livery, from the map. "" if the (class,livery) pair isn't present.
std::string FeederTeamFor(const std::string& className, const std::string& liveryName);

// The real PARENT team of a feeder label (affiliation), or "" if independent.
// Populated by (1) DERIVING it from the 'academy' labels in feeder_team_map ("<X> Academy" -> "<X>") and, in a
// second pass, (2) a curated PSV that OVERRIDES/adds to it. Pure, deterministic.
std::string ParentTeamFor(const std::string& teamLabel);

// Loads docs/team_affiliations.psv (cantera_team|parent_real_team|strength) — curated affiliations
// that OVERRIDE/add to LoadFeederTeamMap's academy derivation (curated wins). Returns false
// if it doesn't open. IMPORTANT: it does NOT clear g_teamParent (it coexists with the derivation); it only overrides row by row.
// ORDERING INVARIANT: since g_teamParent is shared with LoadFeederTeamMap, this function must run AFTER
// LoadFeederTeamMap in the wiring, so the curated entries win over the derivation.
bool LoadTeamAffiliations(const char* path);
int  TeamAffiliationCount();   // number of curated affiliations loaded (diag/test)

// 0..1 strength of a feeder team's affiliation to its parent. 0.0 if the feeder has NO parent
// (ParentTeamFor empty); if it has a parent, the curated strength (column 3 of team_affiliations.psv) or 1.0 by default.
// TeamRelationship consumes it to scale how feeder->parent relationships propagate.
double AffiliationStrengthFor(const std::string& canteraTeam);

// Resolve a deterministic, best-effort TEAM LABEL for a (class_name, livery_name) pair using the
// loaded livery_team_equiv table, applying the DECIDED precedence: real_team -> alias -> livery_name ->
// "Privateer #"+car_number -> the stable id "seat_privateer_entry" (the overlay resolves that id via TR()).
// PURE ENGINE: returns a literal string for concrete cases and the bare id for the nameless fallback —
// NO TR() call here (engine/overlay split). The livery_name fallback guarantees a non-empty label whenever
// the livery is in the table; the "seat_privateer_entry" id is the last resort.
std::string TeamLabelFor(const std::string& className, const std::string& liveryName);

// The ordered, DISTINCT team/car labels a CLASS fields, with a LAYERED precedence:
//   (1) MANUAL real teams from livery_team_equiv.psv (every key class_name + "\x1f" + livery, resolved with
//       the same precedence as TeamLabelFor: real_team -> alias -> livery_name; skips empty /
//       "seat_privateer_entry"; dedup first-seen) — the ~28 manually-curated classes. If non-empty, returned.
//   GENERATION classes (docs/generation_classes.psv: karts/Onyx_Challenge/junior/spec entry) short-circuit to EMPTY
//   here regardless of any fallback (they keep cohort homage labels — SeatsForClass's cohort fallback).
//   (2) else PACKS real teams from livery_team_equiv_packs.psv (class -> the NAMeS pack teams, e.g.
//       DPI -> "Chip Kingsstead Racing Racing"/"Blackhurst Racing 19230 Racing Thornford Racing 3076"/...). Sits ABOVE the catalog.
//   (2.5) else, IF the full livery CATALOG (docs/ams2_liveries.psv) has it, the catalog CAR/MANUFACTURER labels
//       (livery_name minus a trailing " #<number>", dedup first-seen) — e.g. GT4 -> "Ironmarch Ax304"/...
//   (3) else EMPTY — the class keeps its cohort homage labels (SeatsForClass's cohort fallback handles that).
// NO FABRICATION: the packs/catalog labels come straight from the data (pack XML / catalog); never invented.
// Pure, deterministic (g_teamEquiv + g_teamPacks + g_liveryCatalog are key-sorted/insertion-ordered), no RNG.
// Test-visible. Authoring real_team into the manual equiv for a class -> layer 1 overrides packs+catalog.
std::vector<std::string> TeamLabelsForClass(const std::string& className);

// As of Aug 9, THE ONE-AND-ONLY id resolution for a field driver, now shared by all three layers.
// Chain: the id column if the data carries one -> DriverIdForName (name+country, accent-proof) -> a
// stable hash of the name. It used to be `static` in career_seats.cpp and only the seats used it; ELO seeding
// (career_engine::SeedAllDrivers) and grid composition (career_fields) resolved it on their own and would
// GIVE UP when the name wasn't in the pool. Result measured on Aug 9: **740 of 2,253 drivers with no
// seeded rating**, falling back to the static curve while their rivals on the SAME grid came out of
// RatingQuality(elo) -- two talent scales in the same car, medians separated by 0.435 (a full
// bar and a half) and **70 of 100 classes mixing them**. In Ivory_Cup_10072 the top 8 were exactly the 8
// unseeded ones: the ladder wasn't measuring talent, it was measuring whether you had an ELO value or not.
// This connects directly to how driver-affinity noise ends up deciding the width of the grid — measured separately.
std::string FieldOccupantId(const FieldDriver& f);
std::string PinnedOccupantId(const PinnedDriver& pd);   // the same applies to the pinned roster (its PSV has NO id column: column 2 is the car number)

// The seats for a class/series, with occupancy resolved from the persisted save (seat_occupant
// map) + the roster/livery data. ALL tiers populate (naming best-effort). Pure: reads the save
// + the loaded PSV tables, mutates nothing. Occupancy reflection: a persisted seat_occupant entry keyed
// "<series_id>#<seat_idx>" overrides that seat's occupant_id (the living-world market drives this).
std::vector<Seat> SeatsForClass(const std::string& seriesId, const SaveData& save);

// — WORLD CHECKSUM. A single number over the WHOLE seat model: for each series
// and each slot, its livery, its occupant, its team, its tier and whether it is open.
//
// What for: livery packs are PRESENTATION and can't move the world. That was said in the spec and
// wasn't checked. With this, «installing or removing a pack changes nothing» is an equality between two
// numbers, not a field-by-field inspection — and it covers what a `team_label` comparison doesn't see:
// occupants, seat liveries, tiers and open slots.
//
// Deterministic and stateless: it walks `AllSeries()` in file order and `SeatsForClass` is pure. Two
// calls with the same save and the same PSVs always give the same number.
unsigned long long WorldSeatChecksum(const SaveData& save);

// Apply the rollover's living-world seat moves to the persisted occupancy map, upgrading
// the tier-COUNT freed-seat tracking (RolloverYearReport's freedByTier) to PER-TEAM granularity. For every
// non-era-memory series, SeatsForClass is resolved against `save`; then:
//   retiredIds: a seat whose current occupant retired this rollover is marked OPEN (kSeatOpen sentinel) —
//     that SPECIFIC team seat is now an offer source. A fully-occupied class (no retiree among its
//     occupants) gets NO open seat.
//   promotedToTier: a climber who was promoted into a freed seat (id -> destination tier from
//     PromoteFreedSeats) RE-OCCUPIES the lowest-index open seat at THAT tier (the AI->AI fill), so only
//     genuinely-still-open seats remain open — keeping the open set consistent with PromoteFreedSeats.
// Pure over the data tables; mutates ONLY save.seat_occupant (additive, write-when-present). Deterministic
// (series iterated in AllSeries() order, seats by index). Era-memory series are skipped.
//
// (the cohort competes too): `climbers` + `worldYear` turn pass 2 into a CONTEST.
// Previously each promoted driver grabbed "the first open seat in their tier", so the alphabetically lowest id
// got the best team: map iteration order was deciding careers. Now every team with an open seat scores every
// candidate with the SAME model it uses for you (TeamCandidateFit + its personality), and seats go out on merit.
// The NUMBER of seats filled doesn't change -- only WHO ends up where. Without `climbers` (or a promoted driver not
// in the list) it falls back to the old distribution, so nobody is left unplaced.
void ApplyRolloverSeatChanges(SaveData& save,
                              const std::set<std::string>& retiredIds,
                              const std::map<std::string,int>& promotedToTier,
                              const std::vector<RosterDriver>& climbers = std::vector<RosterDriver>(),
                              int worldYear = 0,
                              const std::set<std::string>& delJugador = std::set<std::string>());

// THE AI'S CONTRACT ------------------------------------------------------------
//
// WHY THIS IS NEEDED. Until now a seat only stored WHO occupies it (save.seat_occupant), not
// UNTIL WHEN. Without an expiry, a seat could only open up if its occupant retired or the
// player displaced them: there was no other path, which is why the world never rotated. Measured on Aug 3
// on a fresh save: 1,656 seats, 0 open.
//
// WHY DERIVED, NOT PERSISTED. It's not about size -- that argument doesn't hold up: the save
// is really 64 KB, and 1,656 explicit contracts would add ~29 KB more, which nobody would care about. The reason
// is that a derived value CAN NEVER FALL OUT OF SYNC with the seat it depends on. If a seat changes
// owner, its expiry recalculates itself; there's no way for an orphaned contract to end up
// pointing at someone who isn't there anymore.
//
// Only what can't be derived gets persisted: the year a driver ENTERED a seat, and only
// when that happens mid-save (save.seat_contract_start). Absent = started with the career.

// The year `occupantId`'s contract in `seatKey` EXPIRES, having started in `startYear`.
// Staggered 1..3 years by a deterministic hash of (seat, occupant): if they all lasted
// the same length, the entire world would expire at once every N years, which isn't a silly season, it's a reset.
// The hash includes the SEAT, not just the driver, so the same driver holding two seats doesn't lose
// both at once.
int ContractExpiryYear(const std::string& seatKey, const std::string& occupantId, int startYear);

// The start year of the contract currently in `seatKey`: the persisted one if there is one, otherwise the year
// the career starts (kStartYear) -- in other words, "they've been there since the beginning".
int SeatContractStartYear(const SaveData& save, const std::string& seatKey);

// Does this contract expire AT THE CLOSE of `worldYear`? True once the expiry year has been reached.
bool SeatContractExpires(const SaveData& save, const std::string& seatKey,
                         const std::string& occupantId, int worldYear);

// Records that `occupantId` enters `seatKey` in `startYear`. Writes ONLY if it differs from the
// career's start year, which is what keeps persistence limited to the deviations.
void NoteSeatContractStart(SaveData& save, const std::string& seatKey, int startYear);

// The seat key exactly as save.seat_occupant uses it: "<series_id>#<index>".
//
// — IT IS THE ONLY WAY TO BUILD IT, and until today it wasn't: the same hand-written concatenation lived in
// eleven places (`sd.id + "#" + std::to_string(i)`) spread across career_seats, career_calendar and the overlay.
// They agreed by habit, not by contract, so the key format couldn't be changed without hunting them down
// one by one. Now they all go through here. What comes next —the key by SLOT of , which survives
// reordering the catalog— is a change in THIS function and a migration, not a sweep through the repository.
std::string SeatKey(const std::string& seriesId, int seatIdx);

// the key by SLOT -------------------------------------------------
//
// THE PROBLEM. `"<serie>#<indice>"` stores a POSITION in a list, and the list gets reordered: all it takes is for a
// pack to add a livery, a roster row to be curated or the catalog order to change for
// seat 7 to become another car. The save would keep saying that So-and-so occupies 7 — and So-and-so would appear
// driving someone else's car, without anything warning. The physical slot (vehicle + livery) doesn't move, and
// it is also the identity the skins system already uses.
//
// The livery of seat `idx`, without looking at the save: from the pinned roster if the series has one, and if not from the
// class catalog (seat i <-> livery i, the same index the grid composition uses).
std::string SeatLiveryForIndex(const std::string& seriesId, int seatIdx);

// The STABLE key "<serie>|<veh_hash>|<catalog_livery_id>"; **empty** if the slot doesn't resolve (class with no
// livery map, livery not in livery_vehicle_map). Empty is NOT an error: it means "this series
// can't have a stable key yet", and the seat keeps the legacy one.
std::string SeatSlotKey(const std::string& seriesId, int seatIdx);

// The LEGACY key, exactly as saves wrote it until today. It is kept to read old saves and to
// diagnose; it is NOT written if there is a slot.
std::string SeatKeyLegacy(const std::string& seriesId, int seatIdx);

// — migrates the keys of `seat_occupant` and `seat_contract_start` from index to slot. Idempotent.
//
// A key that doesn't resolve is NOT touched and NOT reassigned: it is explicit —«if the index can't be
// resolved, the load must not seat the driver in another car»—, so it stays as a counted orphan. And
// since `SeatKey` also returns the legacy one when there is no slot, that seat keeps working the same.
// Returns how many keys it moved; `orphans` (optional) how many stayed.
int MigrateSeatKeys(SaveData& save, int* orphans = nullptr);

// 23/09 — THE KEYS BROKEN BY AN EARLIER VERSION. 0.6.x reads `seatocc`/`seatstart` splitting at the FIRST
// '|' and gluing the rest without separators, so a 0.7.0 save opened (and saved) with 0.6.8/0.6.9
// comes back with `seatocc juniper_series|178530063571rd_4012492869` — key = the bare series, occupant = slot + id
// glued together — and `seatstart juniper_series|2147483647` (the overflowed year). Seen in a test save: 73 lines
// like that. Also all the slots of a series fall on the SAME key, so only the last survives: the rest was
// lost in 0.6.x and can't be recovered (the migration card already warns that going back isn't safe).
// This recovers what remains: the good key is rebuilt by trying the series' slots against the glued numeric
// prefix (only if ONE fits), and bare-series keys are always deleted, because they don't point to
// any seat and only dirty the save. Needs the catalog loaded (goes with MigrateSeatKeys). Idempotent.
// Returns how many entries it touched (recovered + deleted).
int RepairDowngradedSeatKeys(SaveData& save, int* recovered = nullptr);

// 17/09 — the identities that the 0.6.8 table froze: `person_id` -> (name, country). It is the LAST
// source of identity of the engine and exists for a specific case: someone whom 0.7.0 removed from the live data
// and who is still SEATED in a save. Measured in the real eight-title save: `rd_kevin_magnussen` in
// Pinnacle_Masters — a car that didn't reach the XML because nobody knew his name anymore.
// Scope: the 9 series and 212 rows of the table, not one more; it is not a new pool of people, it is the memory of
// the previous version. Whoever brings no country doesn't enter (). Consumed by `IndiceGlobalDeIdentidad`.
const std::map<std::string, std::pair<std::string, std::string> >& Identidades068();

// The SERIES of a seat key, whatever the format costs. It exists for the same reason: the inverse parse
// («what's before the '#'») was copied by hand in two places of career_calendar, and both would go
// silent —without breaking, which is worse— the day the key stops carrying '#'. Returns the whole key if it
// recognizes no separator, which is the safe answer: a nonexistent series seats nobody.
std::string SeriesOfSeatKey(const std::string& key);

// OPENS THE SIGNING WINDOW and seals its keys. The seats in play are the ones whose
// contract expires at the close of `year` (the derived expiry), in playable series, excluding era-memory and standalone
// or branch series, and within the player's relevance band (no karting for a GT3 driver).
//
// Seals only the IDENTITY (series|team) and the state; everything else keeps being derived live. A team appears ONCE
// even if both its seats empty out: the key is the team, not the vacancy.
//
// Capped at MK_WINDOW_SEAT_CAP by PROXIMITY to the player's level, not by blindly truncating: with 1-to-3-year
// contracts, a third of the world expires every year, and that calls for a window that's "few and readable".
//
// It does nothing outside the window or if the year was already sealed -- that way the board can refresh
// as many times as it likes without a contested seat reverting to OPEN. A new year reopens it entirely.
// Deterministic, no RNG. Returns how many keys it sealed (0 = not due yet).
int OpenTransferWindow(SaveData& save, int year, int doy, int absToday, int frontierPhase,
                       int firstYear = 0);

// THE SEATS A CAREER IS BORN WITH. Opens a seat across the ENTIRE entry ladder
// (tier 1) plus `MK_PRESEASON_ASPIRE_SEATS` from tier 2 as a carrot. An entry category always takes in
// new people -- that's what makes it an entry category -- so which ones isn't randomized: they all get in. The tier-2 ones
// sit above your current stage, so those ARE contested, and you lose them to the cohort.
// Idempotent (a seat already open isn't recounted). Deterministic. Returns how many it opened.
int OpenCareerStartSeats(SaveData& save);

// ONE DRIVER, ONE SEAT. The ids that are ALREADY staying put next season: they occupy a
// seat whose contract isn't expiring, so they can't also be contesting another one against you.
//
// It exists because the cohort occupies SEVERAL seats at once (the probe measures 1.61 seats per occupant), and the
// market used to rank all of them against the player for every seat: the same three drivers made up the shortlist for all
// fourteen contests, and the rookie lost 100% of their first season. It's not that they were too good --
// being monsters from day one is intentional design (`CAST_SEED_RATING_HI`, decided on Jul 2) -- it's that
// they were everywhere at once, which doesn't happen in real life.
//
// DERIVED from contract expiry: nothing is persisted, so it can never fall out of sync.
// It's computed ONCE and passed into the rankings; internally it walks the entire series list.
std::set<std::string> StayingDrivers(const SaveData& save, int worldYear);
// 09/09 (performance, probe of ): StayingDrivers carries a MEMO. It cost 26 ms per call (it walks all the
//  live series composing their seats) and was called 8,555 times per suite: 226 s, the whole bottleneck of the Sus
//  inputs are the catalog (DataEpoch, which data loading and ReplaceSeries bump), seat_occupant, seat_contract_start,
//  retired and worldYear: a content FINGERPRINT (never the save's address: a new one lands on the same stack) decides
//  whether the last answer is valid; the seats change (signing, cascade, rollover) and it is recomputed. This is the count WITHOUT
//  memo: TESTS ONLY (equivalence and sabotage); the engine always calls StayingDrivers.
std::set<std::string> StayingDriversUncached(const SaveData& save, int worldYear);
// 09/09 (performance): SeatsForClass composes its DATA part (liveries, rosters, fields, team labels:
//  everything except the save's overrides layer) ONCE per series and per data load, and each call copies that
//  skeleton and applies seat_occupant. It is the engine's most-called function (StayingDrivers, BudgetFor, the cascade, the
//  slots...). The memo's signature is the same one BaseTalent and EnsureTailNames use (reloads between tests) plus
//  DataEpoch. This is the version WITHOUT memory: TESTS ONLY (equivalence); the engine always calls SeatsForClass.
std::vector<Seat> SeatsForClassUncached(const std::string& seriesId, const SaveData& save);

// Removes from `cohort` anyone already staying elsewhere. Pure; preserves insertion order.
std::vector<RosterDriver> CohortMinusStaying(const std::vector<RosterDriver>& cohorte,
                                             const std::set<std::string>& staying);

// The sealed seat entry for that seat in the live window, or nullptr. A seat = (series, team).
WindowSeat* FindWindowSeat(SaveData& save, const std::string& seriesId, const std::string& teamLabel);
const WindowSeat* FindWindowSeat(const SaveData& save, const std::string& seriesId, const std::string& teamLabel);

// APPLYING to a seat without having been courted for it. You can go after any seat that's
// in play, and it moves to CONTESTED. What you don't get is preferential treatment: you're added to the END of the shortlist, not
// ranked in by score -- the team already had its candidates, and you're the one who volunteered. Being courted still
// means something, because the team put you on the list BEFORE seeing the rest of your season.
//
// This does NOT check requirements, and that's deliberate: the requirement is checked in DECEMBER. You can fight for a
// seat whose reputation you don't have yet, and if you don't reach it by December, that seat rules you out. That turns a
// wall into a season-long goal instead of a closed door.
//
// false if the seat isn't in the window, you already applied, or it's renewed or already resolved.
bool ApplyToWindowSeat(SaveData& save, const std::string& seriesId, const std::string& teamLabel);

// THE DECISION WEEK. Everything resolves at once: every CONTESTED seat comes out
// OFFERED (it's yours if you sign it) or DISCARDED, and the discarded one keeps the NAME of whoever took it --
// without that, the December screen can't tell you why, which is half of what it's there to show.
//
// Here, and only here, is the seat's REQUIREMENT checked: if you don't reach its bar, it rules you out no
// matter how well you did in the comparison. That's what turns the wall into a season-long goal.
//
// Seats you never fought for (OPEN / COURTED) are NOT touched: the world resolved them without you, and they're not your
// yours. Only what you contested gets judged, which is what runs as a parallel observation.
//
// It does nothing outside the decision week, or if the sealed window isn't this year's. Idempotent by
// construction: after resolving, none remain CONTESTED. Deterministic. Returns how many it resolved.
// 19/09: whoever takes a slot can't take another one in the same window (they leave the next shortlist).
// 19/09: without a cohort, a real person from the pool enters the table as a rival: the slot is no longer yours by
// default, only if there really is nobody (see CloseExpiredWindowOffers).
// `ratios` = series -> the player's contract season ratio (career_calendar.h,
// PlayerSeasonRatios). With it, YOUR seat isn't contested: the team already knows how you've done, so it either
// renews (>= MK_RENEW_MET_RATIO) or slips away. nullptr = no renewal verdict (previous behavior).
int ResolveDecisionWeek(SaveData& save, int year, int doy, double playerMoneyNorm, int firstYear = 0,
                        const std::map<std::string, double>* ratios = nullptr);

// SIGNING AN OFFERED SEAT. What closes the loop: without this, the decision week left
// seats stuck in OFFERED with no way to take them, so "you come out of February with contracts to sign" was still
// a lie, for lack of that last step.
//
// This does NOT reimplement signing: it builds the offer that corresponds to the seat and calls `AcceptOffer`, which is
// where the real guardrails already live -- no duplicate series, the EXCLUSIVITY lock, and
// buy-in affordability. Duplicating those rules here would have created a second gate with different criteria, which is
// how you end up able to sign for two exclusive teams at once.
//
// And it's NOT "pick one": holding multiple contracts is deliberate. You can sign every seat you're offered
// as long as the guardrails allow it; the game doesn't impose an artificial cap -- the calendar, the
// money, and exclusivity do.
//
// `reason` (optional) receives WHY it couldn't be done, so the screen can say so instead of doing nothing:
// "no_offered" (the seat isn't offered) · "no_series" · "already" (you already have a contract in that series) ·
// "exclusive" (an exclusive contract blocks it) · "afford" (you can't reach the buy-in) ·
// "would_break" (the seat is in a series where YOUR CURRENT CONTRACT already covers that season: it would have to be broken).
//
// `allowBreak` is the answer to "make me the offer and let ME decide whether to break it". By default, nothing
// gets broken: signing fails with "would_break", and the screen can show the exact amount (the same
// BreakContractFee it will later charge) and ask first. With allowBreak=true it terminates and signs on the spot, with
// the severance fee and reputation hit that BreakContract already charges. Never by accident: if a single click
// could cost you a severance fee with no warning, the guardrail would be worse than having none at all.
bool SignWindowSeat(SaveData& save, CareerCalendar& cc, const std::string& seriesId,
                    const std::string& teamLabel, int year, std::string* reason = nullptr,
                    bool allowBreak = false);

// The PER-RACE salary that seat offers, derived (career::SalaryFor over series x team x your
// rating). It isn't persisted in the sealed key: the window stores identity, not figures -- if the world shifts
// underneath it, the figure recalculates itself. 0 if the seat or series don't resolve.
long WindowSeatPerRace(const SaveData& save, const std::string& seriesId, const std::string& teamLabel,
                       int year);

// COURTING: having the team come after YOU. Until now the only way into a seat
// was applying (`ApplyToWindowSeat`), so nothing ever produced the COURTED state, and half
// of a silly season's appeal -- getting a call -- didn't exist in the game.
//
// Two paths, and the distinction matters:
//   1. HISTORY. A team you share a past with (`market::TeamRelationship` above the baseline:
//      former team, prior courting, press marks) courts you with NO CAP. A real reason doesn't compete for quota.
//   2. PROXIMITY. Plus up to `MK_WINDOW_SOUND_CAP` seats at your level or below -- the ones a team
//      would offer you anyway (the on-ramp). Kept LOW: being sought after has to stay rare, or it
//      stops being news. Without this second path, your FIRST year wouldn't have a single approach, because a
//      rookie has no history with anyone -- the same gap that already swallowed the year-1 cycle.
//
// Only moves OPEN seats: one you're already contesting, or one the world already resolved, doesn't get re-courted. Idempotent,
// Deterministic, no RNG. Returns how many it courted.
int SoundOutWindowSeats(SaveData& save);

// THE MARKET'S HEARTBEAT. Checks the clock and does whatever's due: opens the window if we're inside it,
// resolves the decision week if it has already come due. ONE single place, so there aren't two callers with two different rules.
//
// Until now, nothing in production called OpenTransferWindow or ResolveDecisionWeek: the window existed,
// it was tested, and it never opened. This is what wires it into the clock (career_contract.cpp, RunWorldTick).
//
// RESOLVES FIRST, OPENS AFTER, and that's deliberate: opening WIPES the seats (`window_seats.clear()`, a new
// year is a different silly season). The other way around, a clock jump from November to the following October would wipe out
// a window with contested seats without ever judging them.
//
// THE WORLD DOESN'T WAIT FOR YOU. The decision week resolves even if you SKIPPED it: the player can jump
// straight from January 1 to the day of their first race (AdvanceToNextEvent), sailing right past February
// 15 without ever landing on it. If we only resolved on landing inside it, that save would be stuck with its
// window sealed and contested forever. It's judged using the DATE IT SHOULD HAVE USED (not today's), so the
// verdict is the same whether you were watching or not -- which is exactly what it means for the world to live without you.
//
// Idempotent and deterministic. Returns how many seats it touched (opened + resolved); 0 = nothing was due.
int MarketCalendarTick(SaveData& save, int year, int doy, int frontierPhase, double playerMoneyNorm,
                       const std::map<std::string, double>* ratios = nullptr);

// A CATEGORY'S PERFORMANCE FLOOR. What it takes to keep a seat here.
//
// A driver doesn't leave their seat for having a birthday: they leave when they no longer match the level of the people they're
// racing against. That, not an age threshold, is what triggers the cascade -- age is the CAUSE (the aging curve
// lowers their rating), not the criterion itself.
//
// The floor is RELATIVE TO THE CLASS on purpose: a mediocre F1 driver is still a star in
// Formula Vee, so the same rating passes at the bottom and fails at the top. That's where the demotion comes from, without needing
// a destination table: you drop to the first tier whose bar you actually clear.
//
// mean = average aged rating of the class's live occupants. floor = mean − CLASS_DEMAND_MARGIN.
// resolved=false (an empty, era-memory, or unknown class) means "demands nothing": nobody drops because of it.
struct ClassDemand {
    double mean = 0.0, floor = 0.0;
    int n = 0;                 // occupants that went into the average
    bool resolved = false;
};
ClassDemand DemandForClass(const std::string& seriesId, const SaveData& save, int worldYear);

// The ABSOLUTE rating of an occupant in a series: the tier anchor (bal::RATING_TIER_STEP) plus
// their position within that grid, with the age curve applied on top. It's the scale on which comparing a
// GT3 driver to an F1 driver means something; without the anchor, both aces came out with the same number.
// 0 if the series or the occupant don't resolve.
double OccupantRating(const std::string& seriesId, const std::string& occupantId,
                      const SaveData& save, int worldYear);

// A driver's ABSOLUTE TALENT: the anchor of the tier their history placed them in, plus how far above
// the tail-end of that grid they finish. It's DERIVED from the static world (rosters + fields, empty
// save), so it isn't persisted and can never fall out of sync -- same reason as the derived contract expiry.
// 0 = has no history in the static world (a generated rookie, the player).
//
// It exists because simulating eight seasons showed skill was being re-derived from the seat currently occupied:
// Alonso would lose 157 points just for moving from Stonefield Motors to Northbourne Racing, and that dooms you to keep sliding.
double BaseTalent(const std::string& driverId);

// 17/09 — the EXIT progress of a cohort member toward their ceiling, derived (not stored).
// Its ceiling comes from its authored `tier_max` and its start from `CohortAnchoredSeed`, which are the two numbers
// that already existed; this is the fraction that goes from one to the other. Returns 0 for everyone who isn't `rk_*`,
// and then `GrowthProgress` keeps the universal rise by age as always.
double CohortCurveStart(const std::string& driverId);

// THE engine's aged-rating projection, the same one used by both the market and the rollover.
// Each used to have its own, and once the ladder was anchored, they ended up measuring on different scales.
double DriverAgedRating(const std::string& id, double skill, int birthYear,
                        const SaveData& save, int worldYear, int ageShift, int tier);

// A driver who does NOT get renewed. They're not retired: they're seatless until the cascade
// finds them one further down. `rating` is their aged rating on the way out, and it's what decides the redistribution.
struct FreeAgent {
    std::string driver_id, from_seat, from_series;   // from_series: their discipline comes from there
    int from_tier = 0;
    int age = 0;                                     // their age: a school doesn't admit a professional
    double rating = 0.0;
};

// ~~~29/08 — Pins in the save who occupies each LIVE slot that doesn't already have an entry, so that the occupancy
// stops being recomputed every winter (measured: 142-254 slots changed occupant per year and the market
// declared 3-5). Idempotent; doesn't touch period, nor the player's slots, nor those already pinned. Returns how many.
int MaterializeSeatOccupancy(SaveData& save,
                             const std::set<std::string>& delJugador = std::set<std::string>());

// Frees every seat whose contract expires at the close of `worldYear`; returns how many.
// A SEPARATE step, not part of ApplyRolloverSeatChanges: there, `worldYear` means the year
// used to distribute already-open seats on merit, and folding the expiry logic in there changed the NUMBER of
// seats that function fills -- exactly what its contract promises not to touch. It runs BEFORE it in
// the rollover: first whatever must expire does, then everything open gets distributed at once.
//
// Who gets renewed is decided by PERFORMANCE against their category's bar (DemandForClass),
// not a die roll. `salientesOut` (optional) collects who's leaving, so CascadeDemoteFreeAgents can
// place them elsewhere; without that second step, the world just loses them.
int ExpireSeatContracts(SaveData& save, int worldYear, std::vector<FreeAgent>* salientesOut = nullptr,
                        const std::set<std::string>& delJugador = std::set<std::string>());

// THE DEMOTION. Places free agents in the HIGHEST seat they can still
// hold, never moving back up, and at most bal::CASCADE_DEMOTE_MAX_STEPS tiers below where
// they were ("you drop from F1 to GT/prototypes, not to karting"). Anyone who finds no room in that
// band gets retired -- and that's the world's ONLY real exit: you don't leave for having a birthday, you leave
// because you no longer match the level of any category you could drop to.
//
// Returns how many were relocated; `retiradosOut` (optional) collects who wasn't. Mutates save.seat_occupant,
// save.seat_contract_start, and save.retired. Deterministic: free agents are sorted by rating (the best
// pick first), tiebroken by id, and open seats by tier, tiebroken by key.
// A completed DEMOTION: who dropped, from which series, and to which. It's data, not inference.
struct Demotion { std::string driver_id, from_series, to_series; int from_tier = 0, to_tier = 0; };

// ~~~29/08 — `retirarSinSitio=false` leaves ALIVE whoever finds no gap and returns them in `sinSitioOut`,
// to retry after the promotions. Measured: 17 people per winter (and rising) were being retired
// because there was no open slot THAT winter two steps down that fit them. The promotions run
// AFTER and open slots right below, which is where a demoted driver lands.
int CascadeDemoteFreeAgents(SaveData& save, std::vector<FreeAgent> agentes, int worldYear,
                            std::vector<std::string>* retiradosOut = nullptr,
                            const std::set<std::string>& delJugador = std::set<std::string>(),
                            std::vector<Demotion>* bajadasOut = nullptr,
                            bool retirarSinSitio = true,
                            std::vector<FreeAgent>* sinSitioOut = nullptr);

// ROOKIES. Feeds new people in from the bottom into the seats the cascade left open, up to
// bal::ROOKIE_MAX_TIER ("they enter from the bottom, tiers 1-2"). It's the world's ONLY entry point: without
// it, ~15 leave per season, nobody comes in, and over ten seasons the population drops from 940 to 862, with
// 140 open seats that never get filled again.
//
// Every rookie carries a plausible name for their country (recombined from the pool itself: real first name + surname
// from that nationality, discarding any combination that already exists), age 17-22, and talent by
// POWER LAW -- most stay where they start and a few go far. That same talent also determines
// their CEILING, so a rookie can end up in the elite within ten seasons, and almost none of them do.
//
// Deterministic by (career_seed, year, seat): two different saves generate different people; the same
// save, regenerated, generates the same ones. Returns how many; `nuevosOut` (optional) collects their ids.
// LIFE HAPPENS. Apart from age and level, every season some people simply
// walk away: they run out of money, get injured, find a job, lose interest. They leave the seat open and never
// come back -- they're not a free agent looking for a seat further down, they've left the sport entirely.
//
// It happens FAR more at the bottom than the top (bal::ATTRITION_PER_MILLE per tier): in entry-level categories
// half the grid is amateurs paying out of pocket; in the elite, nobody walks away from an F1 seat out of
// A deliberate design call (Aug 4): an exit path was needed that was neither the clock nor
// performance, especially in the lower categories.
//
// Deterministic by a hash of (career seed, year, seat) -- "almost random" to whoever's playing,
// reproducible to whoever's debugging. `protegido` (optional) is the id that's never touched: the player's.
// DISCIPLINE. Index 0..5 (touring, openwheel, gt, prototype, oval, historic), or -1 if it's
// unknown. It comes from the driver's authored data (docs/career/f3_authored_profiles.psv, affinity_family, 1,634
// of 1,634 rows filled in that the engine wasn't even reading) and, failing that, from the series they race in.
//
// In that second case, the COHORT and GENERATED ROOKIES fall through, and that's fine: a young driver
// hasn't specialized yet. Their discipline is wherever they currently are, which is why they can cross over
// while moving up -- exactly what real people do coming out of karting.
int DriverAffinity(const std::string& driverId, const std::string& driverName,
                   const std::string& seriesId);

// Is it plausible to move from discipline `desde` to `hacia`? (bal::AFFINITY_OK). With no data on either
// side, it returns true: affinity CONSTRAINS moves, it doesn't invent them.
bool AffinityAllows(int desde, int hacia);

int CascadeAttrition(SaveData& save, int worldYear,
                     const std::set<std::string>& delJugador = std::set<std::string>(),
                     std::vector<std::string>* salenOut = nullptr);

// THE CASCADE PROMOTION. Walks open seats FROM TOP TO BOTTOM and fills each
// one with the best candidate from the tier below who clears that category's bar. That order IS the
// mechanism: filling a tier-7 seat opens a gap in tier 6, which pulls from tier 5, and so on down to where the
// new generation picks up whatever's left ("top to bottom, so every gap is inherited by
// whoever comes next").
//
// What was missing: promotions were tied ONLY to retirements (PromoteFreedSeats over freedByTier), so
// a vacancy from an expired contract pulled nobody up and stayed open forever -- measured: 124
// open seats and climbing, over ten seasons.
//
// A REAL driver never gets promoted (their ceiling is their current category): it's the cohort and
// the generated rookies who move up, since they're who the world is meant to fill up with.
int CascadePromoteToOpenSeats(SaveData& save, int worldYear, std::vector<std::string>* subenOut = nullptr,
                              const std::set<std::string>& delJugador = std::set<std::string>());

// ~~~29/08 — do rookies consume REAL PEOPLE from the pool before inventing? Production = true; the
// measurement turns it off to know what it contributes over the same world.
extern bool g_novatosDelPozo;

// 17/09 — `elHuecoGana`: lifts the class's BAR (`DemandForClass`) so that no open slot
// is left with nobody. It exists because of a save coming from 0.6.8 with 79 empty seats that its market
// never filled again: when a class drains, the bar is set by the survivors —the strongest— and
// rises above what that step can produce, so the gap becomes PERMANENT. It is the same
// rule the end-of-season seal already applies; the normal winter does NOT use it. See `RecoverEmptySeats`.
int CascadeGenerateRookies(SaveData& save, int worldYear, std::vector<std::string>* nuevosOut = nullptr,
                           const std::set<std::string>& delJugador = std::set<std::string>(),
                           bool elHuecoGana = false);

// 17/09 — THE RECOVERY OF THE SEATS AN OLD SAVE BRINGS EMPTY. A single pass, meant
// to run ONCE per save (mark `SaveData::seatfill`), with two attempts in the order the winter already uses:
// first with the bar on —which is the one that decides well— and then, only over what is still open,
// with the gap winning. It does NOT move anyone who already has a seat: it only writes into open slots.
// Returns how many slots were filled.
int RecoverEmptySeats(SaveData& save, int worldYear,
                      const std::set<std::string>& delJugador = std::set<std::string>());

// 23/09 (, «Close the app, open it again: nothing moves») — THE PRE-GENERATION RECOVERY, NOW STABLE.
// Pre-generation re-seeds on EVERY boot the ratings of whoever has never raced (DropUnracedSeeds +
// SeedAllDrivers) BEFORE recovering the empty seats, and the recovery seeds those it seats with its own
// rule. So the SECOND boot re-seeded those same ones with the other: measured with the migration of a 0.6.8
// save, 67 AI ratings changed (2001→1704) on reopening, and on the third nothing. This does the recovery and,
// if it has seated anyone, the same re-seed the next boot would do: the final state is the one the
// next opening already finds done. Returns how many slots it filled.
int RecoverEmptySeatsForBoot(SaveData& save, int worldYear,
                             const std::set<std::string>& delJugador = std::set<std::string>());
// 01/10 (hewo, 0.7.0): the recovery above ran BEFORE migrating the 0.6.8 seat keys. The app's startup
// loads the save before the installer leaves career/seat_index_068.psv in the data directory:
// without a table the migration touches nothing (MigrateSeatKeys), the recovery filled 100 places over the old keys,
// set `seatfill` and did not run again; when Play was pressed the migration did happen and 106 places were left open (Granite Challenge
// ran with 8 cars out of 20). True while the save still has unmigrated legacy keys for lack of a table:
// the recovery has to wait for the load that migrates.
bool SeatMigrationPending(const SaveData& save);
// 01/10: is there any DRAINED class (a third or more of its seats open)? The market leaves one or two vacancies
// per class, never a third; only the inheritance of an old save leaves that. It is the condition for the second
// 0.7.1 recovery pass not to eat the legitimate vacancies of a healthy save.
bool HasDrainedClass(const SaveData& save);

// 23/09 — THE BOOT RE-SEED, in a single place (used by pre-generation and RecoverEmptySeatsForBoot).
// Deletes the rating of whoever has never raced and re-seeds it with the current rule (DropUnracedSeeds +
// SeedAllDrivers): this way a change in the seeding rule reaches whoever has no history yet. Returns how many it deleted.
// 23/09 — EXCEPT whoever the market has seated (SaveData::seat_seeded): the ELO of their seat (D4, «you are worth where you
// sit») and that of the rookie who is born are kept until they race. Before, the next boot changed it to the
// listing ELO (measured: 134 ratings changed on reopening after a winter, and 12 rookies were left without a rating).
// Decision of 23/09: it is not recomputed.
int ReseedUnracedForBoot(SaveData& save);
// The delete half, without re-seeding: used by pre-generation, which seeds after its period migrations.
int DropUnracedSeedsForBoot(SaveData& save);

// TWO USER RULES (13/08), and both are met here, at the END of the rollover:
//
//   1. PERIOD DRIVERS ARE FIXED: they neither enter nor leave. A historical grid is content, not a
//      youth academy. Measured: Vettel —whose home is `beacon_championship_2774` and `granite_cup_3307`— ended up with a seat in `hypercars`,
//      which is a live series. The other five legendary ones didn't move, so the leak is rare but it exists.
//   2. NO SLOT STAYS EMPTY. «We should always fill even if it means pulling up drivers who aren't due, that is
//      lowering requirements, but the gaps can't stay.» F1 stayed at 18 of 20 for
//      three seasons in a row.
//
// It goes as a FINAL SWEEP and not as a patch inside each mechanism ON PURPOSE: there are six different places
// that write occupant, and a rule spread over six guards breaks the day the seventh appears. Here
// both rules are true BY CONSTRUCTION when the rollover ends, whoever is looking.
//
// Order: first EVICT (a period driver frees their live slot) and then FILL, so that gap enters
// the same distribution. The fill lowers the bar in two passes —whoever makes the level, and if not whoever doesn't
// but can— and whatever is left empty is declared (that is already a lack of content, and it has to be seen).
//
// It does NOT touch the player's seats. Deterministic, no RNG.
// 14/08 — WHAT IF NO CANDIDATE FITS THEIR BUDGET? A user's question, and the answer is in
// the breakdown: the demand is lowered in three steps, and the gap ALWAYS WINS.
//
//   `daElNivel`   the team signs someone who makes the class level AND can pay. The normal case.
//   `bajoElListon` nobody who makes the level fits their pocket → signs the best one who can pay,
//                  even if they don't make the level. A weak driver, not a gap.
//   `sobreCoste`  not even that: NOBODY of those who can pay will do → it STRETCHES above its budget and
//                  signs the best there is. It is what makes money decide WHO and not WHETHER.
//   `sinNadie`    there isn't even one eligible candidate in the whole world. That is no longer economics: it is lack
//                  of content, and it is DECLARED () instead of being covered up with an invented driver.
// (15/08) — TEAMS REPLACE AN INCUMBENT WITH SOMEONE BETTER. It is the missing half, and without it
// everything else in got nowhere.
//
// Measured: sealing the gaps (`SealLiveGrids`) guaranteed that no slot stays empty — and incidentally
// that none OPENS. F1 had ONE entry in twenty seasons, when Sainz retired. Aces tripled
// (18 -> 58 born, 36 reaching level 7) and the F1 grid didn't change a single digit: there was no
// door. The aces piled up in the other seven level-7 series.
//
// It is literally what was written from day one: «the main job of is for teams to
// FIRE, not to sign». `DisplaceIncumbent` already existed, but it only ran when the PLAYER takes a
// seat; the world never did it to itself.
//
// AND THIS IS WHERE THE CONTRACT MEANS SOMETHING, which is what the user asked for and had nowhere to bite: an
// incumbent with a contract IN FORCE is not touched. Only whoever has it expired can be replaced — and the same on
// the other side, you can't sign someone who is tied up elsewhere. Signing a kid for three years
// now really protects him.
//
// The four guards, and all four are needed:
//   1. the challenger has to be BETTER BY A MARGIN (`DISPLACEMENT_MARGIN`), not by a point — otherwise the
//      whole grid churns every winter and nothing means anything;
//   2. both contracts expired (the incumbent's and the challenger's);
//   3. the team has to be able to AFFORD it — it is a signing, not an emergency filler;
//   4. and the class's affinity and age ceiling, as in all the other routes.
//
// The displaced one does NOT disappear: they are left without that seat and the cascade/`SealLiveGrids` look for room
// lower down, which is exactly the demotion channel we've spent the whole week trying to open.
// ~~~01/09 — WHY IT ISN'T SWAPPED. `cambios` said how many slots moved (8 a year across 91 series) and
// nothing about why the others didn't. Each counter is a guard of the , and together they say whether the market is
// held back by the MARGIN (nobody is sufficiently better), by MONEY (they are and can't be paid) or by the
// LADDER (ceiling/steps/affinity/age).
struct SwapReport {
    int cambios = 0, mirados = 0;
    int noHayMejor = 0;     // the slot was looked at and no challenger passed all the guards
    int caenPorMargen = 0, caenPorDinero = 0, caenPorEscalera = 0, caenPorContrato = 0;
};
// 16/09 — THE BREAKDOWN OF THE LAST PASS, without running it again. `ReplaceWeakIncumbents` is NOT a
// query: it moves occupants and, without `desplazadosOut`, leaves the outgoing incumbent out of all the distributions.
// Whoever only wants to PRINT its part —a diagnostic, a test— has to read it from here and not call it again
// . It was added because `test_fill_pass_distribution` called it a second time just for that: measured with
// `--medidas`, that second pass made 51 people disappear in twenty winters (0 without it).
// Same pattern as `LastGridDrops()`.
const SwapReport& LastSwapReport();
// ~~~29/08 — `desplazadosOut` returns the incumbent a team replaces with someone better. Without this they
// were silently left out of the world: the comment said «the cascade will find them room lower down» and the
// cascade didn't see them, because it only receives those whose contract expires. Measured: 10 lost in five winters,
// named people (Tsunoda, Aitken, Stenhouse) who weren't retired or in any grid.
// ~~~02/09 — DIAGNOSTIC: the route by which each driver reaches each slot (is born / is promoted by which
// pass / is taken by the / drops). Off by default; `--world` turns it on. Without it there is no way
// to know why 40 of 44 born with ace potential end up parked at level 6.
extern bool g_reservaEnParrilla;   // ~~~02/09: the RESERVE fills gaps before inventing anyone
extern bool g_trazaRutas;
extern std::map<std::string, std::string> g_rutaDe;
extern std::map<std::string, long> g_rej7;
// ~~~07/09 — WHY WHOEVER DESERVES IT ISN'T PROMOTED, at ALL levels and not just 7.
// `g_rej7` only counts at `tier == 7` (it was added for the , which was about the premier category). Measured on
// 07/09: at twenty seasons there are 303 people who deserve to rise and don't, and 99 of the 118 at
// level 1 above their band ARRIVED already high -- i.e. the problem is not how they are seeded but
// that nobody moves them. Without counting the rejections by level that is invisible: the cascade says nothing when
// rejects someone, and the AFFINITY guard didn't even have a counter, so it could be killing
// candidates without a trace. Key: "tN/reason". Only filled with `g_trazaRutas`.
extern std::map<std::string, long> g_rejPromo;

extern double g_margenCambioMedicion;   // ~~~02/09: <0 = the production one (measurement only)
double MargenDeCambio();

// ~~~16/09 (card 13): the hook with which the TEST BENCH turns off the affinity of the pool's class of origin
// to measure against the world from before. <0 = whatever production says (bal::POOL_ORIGIN_AFFINITY, today
// ON) · 0 = off · >0 = on. `career_test.exe --afin-origen 0|1` writes it before loading
// anything; in the game nobody touches it.
extern int g_afinOrigenMedicion;
bool AfinidadDeOrigenActiva();

SwapReport ReplaceWeakIncumbents(SaveData& save, int worldYear,
                                 const std::set<std::string>& delJugador = std::set<std::string>(),
                                 std::vector<FreeAgent>* desplazadosOut = nullptr);

struct SealReport {
    int desalojados = 0, rellenados = 0, sinNadie = 0;
    int daElNivel = 0, bajoElListon = 0, sobreCoste = 0;
};
SealReport SealLiveGrids(SaveData& save, int worldYear,
                         const std::set<std::string>& delJugador = std::set<std::string>());

// A driver whose world is ONLY period: appears in historical grids and in no live one. It is computed ONCE
// over the STATIC world (empty save), so it doesn't depend on what the market has done today — if
// it did, someone who slipped into a live series yesterday would stop counting as period and would stay there.
bool EsPilotoDeEpoca(const std::string& driverId);

// The seats the player occupies RIGHT NOW, by key. They have to be captured at the START of the
// rollover: by the time the cascade runs, the renewals block has already cleared out the contracts that
// were ending, leaving this map empty. Measured on Aug 4 -- attrition was taking the seat away from the
// player and handing it to a rookie while the player was still protected by clause.
//
// By KEY, not occupant id, because the seat model doesn't know the player is sitting there: that
// seat still derives whichever driver the data says occupies it.
std::set<std::string> PlayerSeatKeys(const SaveData& save);

// YOUR TEAMMATE -- the other seat with YOUR SAME label in the same series.
// Returns their index, or -1 if your team only fields one car (the norm outside the big categories).
//
// Derived from SeatsForClass, NEVER persisted: if the world moves your teammate mid-season, the
// answer updates itself. Same reason a contract's expiry isn't stored either.
//
// This is needed for clauses like "beat your teammate": without it you can't even state who
// they are. And it only makes sense where the team fields two cars, which is exactly what this returns.
//
// yourSeatIdx = the seat you occupy (Contract::seat_idx), excluded so it never returns you to yourself.
// Pass -1 if asking about a team you're not on: it then returns their FIRST seat.
// With 3+ cars it returns the first other one by index (SeatsForClass's total ordering -> deterministic).
int TeammateSeatIndex(const SaveData& save, const std::string& seriesId,
                      const std::string& teamLabel, int yourSeatIdx);

// THE DROUGHT -- the window passed and you were left with nothing.
//
// It's not game over, which is why it has its own name instead of being the absence of something: it's THE FLOOR. You rent a drive,
// race a season at your own expense in a worse car with no salary, and in September you get looked at again. It's the safety net
// that lets the rest of the system be harsh without being unfair, so the screen has to be able to say so
// out loud instead of leaving the tab blank.
//
// Derived, NEVER persisted. true only when: (1) there's a sealed window, (2) its decision week has ALREADY passed
// per the clock passed in (same derivation as MarketCalendarTick), (3) there's no OFFERED seat left
// unsigned, and (4) no active contract reaches the destination season.
//
// It looks at what you HAVE, not what you lost: saying "you're out on the street" with a contract on the table
// would be a lie, and losing five contests while holding a live contract isn't a drought, it's a bad season.
bool InDrought(const SaveData& save, int year, int doy, int firstYear = 0);

// IS AN OFFERED SEAT STILL STANDING? Found while testing: seats were still
// signable in MARCH. And it wasn't just cosmetic -- SignWindowSeat never checked the date, only that the state was OFFERED,
// so an offer from December's decision week could be signed in January, February, or March,
// until the September window wiped the board. In other words: signing for a season that had already been
// running for three months.
//
// THE RULE, as decided: **an offer never lives outside its PERIOD**. Every offer has its
// own deadline, but that deadline is CAPPED by the close of the transfer period: "every offer should have a deadline
// but never exceed the transfer period's maximum date; once the window closes there are no more
// offers".
//
// Both limits coexist, and whichever comes first wins:
//   · DEADLINE -- `bal::MK_OFFER_TTL_DAYS` (28) from when its decision week opens. It's the SAME constant that
//               already governed market offers ("expires 4 weeks after appearing"): one rule, not two.
//   · CLOSE   -- the end of the period the window belongs to. In a normal year, December 31 of the year
//               it opened in (from January 1 the clock is already in-season). In YEAR 1, it's the day of
//               YOUR FIRST RACE: the period opens January 1 and closes once you start racing,
//               because it would be strange to keep signing once the season is already underway.
//
// Without the cap, a long deadline left offers signable in MARCH -- found while testing, and it wasn't cosmetic:
// SignWindowSeat only checked that the state was OFFERED, so you could sign for a season that had already
// been running for three months. Without the deadline, within the period an offer would last as long as the period itself,
// and deciding would stop being deciding.
//
// Before the decision week opens: false, there's nothing to sustain yet. Everything is derived from the
// window's and calendar's dates, with no new state to persist or that could fall out of sync.
bool WindowOfferStillOpen(const SaveData& save, const CareerCalendar& cc, int year, int doy,
                          int firstYear = 0);
// 18/09 (Josh): the SAME deadline as a date, to paint how many days the offered slot has left. The first
// absolute day on which it is no longer signed (the first of the two limits above). 0 = there is no sealed window or
// its decision week hasn't opened the day yet (year, doy).
int WindowOfferDeathAbs(const SaveData& save, const CareerCalendar& cc, int year, int doy, int firstYear = 0);

// OFFERS YOU DIDN'T TAKE GO TO SOMEONE ELSE. As decided: "every offer you haven't
// taken gets assigned to other drivers", and "when they close, they get filled".
//
// Until now, an OFFERED and unsigned seat stayed in that state until the September window
// wiped the board, and NOBODY occupied the seat. Three consequences, and none were cosmetic:
//   · the world was left with an empty seat nobody drove;
//   · the screen kept showing a dead offer as if it were a way out;
//   · and `InDrought` counted it as "you have something on the table", so the drought never got declared.
//
// Once the period closes (WindowOfferStillOpen already false), every offered and UNSIGNED seat moves to
// DISCARDED with the name of whoever takes it, and that driver actually occupies the seat (seat_occupant).
// Whoever takes it comes from the SAME SHORTLIST that was contesting it with you: it's whoever was behind you, not a name
// invented at closing time, which is why the news actually reads as real.
//
// The one you DID sign is untouched: the contract overrides the sealed key.
// 19/09 (tester: «Lucia Vega» in 8 seats at once): ONE DRIVER, ONE SEAT. Whoever takes a slot leaves
// the shortlist of the following ones (and those already named by this window's decision week don't enter either), and
// their contract starts at `window_year` (NoteSeatContractStart), not inherited from the previous occupant.
// 19/09 (Josh's decision, «real drivers from the pool»): with nobody from the cohort, the slot is signed by a REAL person
// from the pool (the same chain as the winter fill: the newly freed, the origin/pack of the class or sister, the
// reserve), never someone with a live seat nor a winner of this window. Only with nobody does it close nameless.
// Returns how many it closed. Idempotent (once closed, they're no longer OFFERED).
int CloseExpiredWindowOffers(SaveData& save, const CareerCalendar& cc, int year, int doy,
                             double playerMoneyNorm, int firstYear = 0);

// 23/09 (, «One driver, one seat»): THE WINDOW WINNER GETS SEATED, in WINTER. The decision week
// only named whoever took the slot and nobody seated them: in January the previous incumbent was still there. This runs in
// RolloverYearReport BEFORE the renewals (with `worldYear` = the year being closed and the destination window
// worldYear+1): it seats each decision-week winner in the slot that was AT STAKE, closes and distributes
// the offers still unsigned, the winner leaves their other slots and the displaced incumbent goes into
// `salientesOut` (the same cascade as those who don't renew). Idempotent: the `WindowSeat::seated` mark prevents
// seating twice. Returns how many it seated. Year 1's window, with no winter, is seated by CloseExpiredWindowOffers.
int SeatWindowWinnersForNewSeason(SaveData& save, int worldYear, double playerMoneyNorm,
                                  std::vector<FreeAgent>* salientesOut);
// The slot (index) the window put at stake for that team: yours if it is your slot; if not, the free one or the
// one that expires when the window's year closes; if not, the team's first. -1 if the team has no slots.
int WindowSeatIndexInPlay(const SaveData& save, const WindowSeat& w);

// Save pruning: deletes generated drivers who no longer occupy a seat AND are retired. Without
// this the map would grow with the clock instead of with the living world. Returns how many were removed.
int PruneGenDrivers(SaveData& save);

// THE WORLD CENSUS. One RosterDriver for every driver who genuinely occupies a seat right
// now, ready for the age/retirement/promotion machinery (which only speaks RosterDriver, nothing else).
//
// It exists because the rollover used to build its cast from RosterForTier(1..7), and that's ONLY the cohort: 28
// drivers. The 900-odd real occupants on the board never got included, so ShouldRetire never even looked at them,
// EVER -- measured on Aug 3 on a fresh career: 0 retirements in three seasons even though ages had
// already been assigned to everyone. The age data was there, but nobody was reading it.
//
// For every occupant:
//   skill: whatever the data behind their seat says (pinned -> field -> roster), not a flat 0.85.
//   birth_year / retire_year: from the pool (whatever was seeded earlier). The save takes priority if it carries a deviation, and
//     the rollover handles that, not this function.
//   tier_min: the lowest tier where they currently hold a seat.
//   tier_max: the highest one PLUS bal::CASCADE_CLIMB_STEP -- their career ceiling. Without that margin,
//     PromoteFreedSeats can't promote anyone (it requires tier_max >= destination tier), and the cascade
//     ends up crippled: only whoever's already at the top would ever move up.
// Deterministic (AllSeries() in order, seats by index, output sorted by id). Mutates nothing.
std::vector<RosterDriver> WorldOccupants(const SaveData& save);

// 23/09 — «IF THEY ARE NOT ON THE DRIVER MARKET THEY SHOULD NEVER RETIRE» (Josh). Who is not on the market:
// the registry's RESERVE that docs/career/reserve_people.psv doesn't give as active (retired, «unknown» or uncurated). Those
// people don't enter through the cascade (ReservaEntraEnParrilla) and only sit because a pack paints them on the car of a
// live timeless series (Frank Gardner in Keystone Challenge, Don Yenko in the 70s touring cars). The market didn't bring them and the
// market doesn't take them away: winter doesn't retire them by age, nor by level, nor because «they leave it», and the report doesn't
// count them. Whoever isn't from the reserve, or the ACTIVE reserve, is market and retires like everyone. Without the
// reserve file loaded, nobody is outside (the world from before).
bool FueraDelMercadoDePilotos(const std::string& id);

// Displacement predicate -- true iff the player's aged rating EXCEEDS the weakest in-reach
// incumbent's by at least bal::DISPLACEMENT_MARGIN (the player can take the seat). Pure, deterministic.
bool PlayerDisplacesIncumbent(double playerAgedRating, double weakestIncumbentRating);

// ("no contracts ever show up") -- SILLY SEASON: the world also frees seats MID-
// season, not only at the rollover (a new career used to spend its first year with 0 open seats ->
// GenerateOffers skipped every series -> an empty Market and no offer inbox messages). For each of the
// 'weeks' advanced, it rolls a deterministic fnv1a(career_seed|year|weekIdx) draw: with probability MK_SILLY_WEEK_PCT
// it opens the BACKMARKER seat (last index) of a reachable series (level<=maxLevel; no standalone/branch/
// era-memory), chosen by the same hash. Global cap MK_SILLY_OPEN_CAP on simultaneously open seats.
// Mutates ONLY save.seat_occupant. Returns the number of seats opened by this call.
int SillySeasonFreeSeats(SaveData& save, int year, int cursorDoy, int weeks, int maxLevel);

// STARTUP signing window (per feedback from July 13): a NEW career is born in preseason -> opens
// `want` backmarker seats in entry-level series (level<=maxLevel; same exclusions
// as the market), with a deterministic offset from career_seed so each career distributes them differently.
// De facto idempotent (a seat already open isn't counted twice). Returns the number opened.
int PreseasonOpenSeats(SaveData& save, int maxLevel, int want);

// if the cursor is within THIS series' signing window
// [openerDoy - MK_PRESEASON_LEAD, openerDoy), it releases its backmarker-seat churn quota (from the last
// index inward), without reopening already-open seats, bounded by worldOpenCap (simultaneously open seats,
// GLOBALLY). Quota = max(MK_CHURN_MIN, round(grid * MK_CHURN_PCT/100)). Deterministic (seatFnv1a by series|year),
// idempotent. openerDoy<=0 (a series with no dates) -> 0. Returns the number opened. Mutates only save.seat_occupant.
int OpenPreseasonSeatsForSeries(SaveData& save, const std::string& seriesId, int openerDoy,
                                int cursorDoy, int year, int worldOpenCap);

// driver -- for every reachable series (level<=maxLevel; no standalone/branch/era-memory) resolves its
// round-1 day-of-year (CalendarForSeries) and calls OpenPreseasonSeatsForSeries with cc's current cursor. Replaces the
// weekly GLOBAL silly-season pass with a release ANCHORED to each class's calendar. Returns the total number opened.
int PreseasonWindowChurn(SaveData& save, CareerCalendar& cc, int maxLevel);

// The WEAKEST incumbent's aged rating in a class, plus the occupant's id (out-params). Walks
// the occupant set SeatsForClass resolves (pinned roster / cohort fallback + persisted occupancy), computes
// each occupant's aged rating the SAME way the rollover does (career_calendar.cpp aged-rating projection:
// skill -> rating band x age curve + rival_form delta), and returns the minimum (the backmarker the player
// would displace). Skips open seats + retired occupants. Returns false when the class has no incumbent
// (era-memory / fully open / unresolved series). Pure (reads save + data tables, mutates nothing). The
// caller (GenerateOffers) applies the reachable-tier "in-reach" band; this returns the single weakest seat.
bool WeakestIncumbentRating(const std::string& seriesId, const SaveData& save, int worldYear,
                            double& outWeakest, std::string& outWeakestId);

// Variable salary: the band of salary MULTIPLIERS a team's living-world strength
// buys in `seriesId`. centerMult/minMult/maxMult are multipliers over career::SeriesSalaryAnchor(level) (the
// per-level credits/race anchor); `resolved`=false when the team has no ratable occupants (the
// caller falls back to the field's mid-band center).
struct SalaryBand {
    double centerMult = (bal::SALARY_TEAM_MIN_CENTER + bal::SALARY_TEAM_MAX_CENTER) * 0.5;
    double minMult = 0;
    double maxMult = 0;
    bool   resolved = false;
};

// Variable salary: the salary band (multipliers over the anchor) for team `teamLabel` in `seriesId`,
// derived from the LIVING ELO of the field — the team's mean occupant aged-rating is placed as a percentile
// within [worst..best] of the series' current occupants, mapped onto [SALARY_TEAM_MIN_CENTER..MAX_CENTER];
// the band is center ± SALARY_BAND_HALFWIDTH. Mirrors WeakestIncumbentRating's meta lookup (pinned
// skill/RosterForTier base_skill+birth_year) + agedRatingFor (same file) — no duplicated roster loading.
// resolved=false (mid-band center) when the series is unknown/era-memory or the team has no occupant with a
// rating this rollover. Pure, deterministic (no RNG), reads save + the loaded PSV tables, mutates nothing.
SalaryBand TeamSalaryBand(const std::string& seriesId, const std::string& teamLabel,
                          const SaveData& save, int worldYear);

// MOVE OUT the displaced incumbent (mirrors PromoteFreedSeats' move shape). When the
// player takes a backmarker's seat, that incumbent's seat in `seriesId` is marked OPEN (kSeatOpen) so the
// player's displacement offer reads as that open seat, and the incumbent is dropped to the free-agent pool
// (retires if aged past the active-age window, else demoted/freed — the next rollover's AI->AI re-fill
// resolves it). Mutates ONLY save (seat_occupant + retired). Pure over the data tables, deterministic.
void DisplaceIncumbent(SaveData& save, const std::string& seriesId, const std::string& incumbentId,
                       int worldYear);

// Job-security predicate -- true iff the player LOSES the seat (not renewed / replaced): perf below
// the bar AND not protected by a guaranteed seat or anti-replacement clause. Pure, deterministic.
bool PlayerLosesSeat(double perf, double bar, bool guaranteedSeat, bool antiReplace);

} // namespace career
