#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include "career_customai.h"   // GridDriver, LiveriesForClass, XmlStemForClass, BuildGridXml, BackupAndWriteAi
#include "career_series.h"     // SeriesDef, SeriesById, ProfileById, AllSeries
#include "career_traits.h"     // TraitVector/TraitBias/CarFamily (the driver's factor model)

namespace career {
struct CareerProgress;   // career_engine.h -- WriteAllClassGrids writes each class at the save's effective slider ()

// A real driver from a series' field (a row in series_fields.psv).
struct FieldDriver {
    std::string series_id, name, country, note;
    std::string id;           // Stable pool id (the `driver` field of the JSON entry); "" in PSV
    int order = 0;            // rank from real-world results (1 = best); defines the mapping onto the curve
    bool iconic = false;
    double skill = 0.0;       // explicit from the PSV (>0), or 0 = compute from the curve in FieldForSeries
    double aggr  = 0.0;       // explicit from the PSV (>0), or 0 = default
};

// A driver with a PINNED livery in a series (a row in series_rosters.psv).
// The file's order is the real season's standings order (truncating = truncating by standings).
struct PinnedDriver {
    std::string series_id, car_no, name, country, archetype, livery_name, binding;
    std::string id;           // Stable pool id (the `driver` field of the JSON entry); "" in PSV
    double skill = 0.85, aggr = 0.60, cons = 0.85;
    bool real = true;
};

// A recurring rival from the roster (a row in series_roster.psv).
struct RosterDriver {
    std::string id, name, homage, country, archetype;
    int tier_min = 1, tier_max = 7;
    double base_skill = 0.85, aggression = 0.60, consistency = 0.85;
    bool hot = false;
    int birth_year = 0;   // 0 = unknown -> procedural age/retirement; >0 = real data
    int retire_year = 0;  // >0 = AUTHORIZED retirement (icons); 0 = procedural by age/rating
    bool modern_only = false;   // Jul 2 feedback: NEVER seated in period series (IsModernSeries==false)
    // Years this driver holds on BEYOND the procedural threshold, based on the nature of what they
    // race (docs/career/class_age_profile.psv). 0 = open-wheel/junior school, the usual threshold. It's filled in
    // by WorldOccupants from the class where they hold a seat; a PSV row leaves it at 0 and nothing changes.
    int age_shift = 0;
    // 13/08 — THE HIGHEST LEVEL IT OCCUPIES RIGHT NOW. It was needed and did not exist: `tier_min` is the
    // LOWEST one it races at and `tier_max` is its race ceiling (widened by CASCADE_CLIMB_STEP), so
    // neither of the two serves to ask "is he already up here?". Without that question, a driver with several
    // seats was a candidate to "move up" to a level he ALREADY occupied — a lateral transfer in disguise — and since
    // the allocation goes by rating, whoever took the free slot was the best of the tier, i.e. the champion.
    // Measured: Verstappen won three F1 titles in a row and on the third showed up in Keystone_Cup_6095.
    // 0 = occupies no live slot (the cohort, an unseated rookie).
    int tier_now = 0;
};

// AUTHORED profile for a real driver in the field (a row in docs/career/f3_authored_profiles.psv).
// It exists because until now EVERY real driver in the field entered with archetype "" (the neutral "solid" profile) and
// consistency 0.85 flat: 2,085 grid rows with the same personality. That's the "drivers are
// robots" feedback, literally, in three push_backs of ComposeSeasonGrid.
//
// `talent` is ABSOLUTE, not normalized per class: per-class aces range from 0.58 to 1.00, and only 7 drivers
// out of 1,402 hit the ceiling. That's correct and does NOT clash with the XML's anchored conversion, which recomputes the
// ace against whoever's in THAT grid: absolute scale underneath, per-class anchor on top.
struct AuthoredProfile {
    std::string archetype;   // ProfileForArch vocabulary (apex/metronome/...); never "" (validated at load time)
    double talent = 0.0;     // 0..1 absolute (median 0.86). Replaces CurveSkill, which was POSITIONAL.
    bool is_real = true;
    // A driver's DISCIPLINE: touring | openwheel | gt | prototype | oval | historic.
    // It had been in the PSV from the start, with 1,634 of 1,634 rows filled in, and the engine wasn't reading it. It's what
    // was needed so a GT driver wouldn't end up in a junior open-wheel car when dropping a category.
    std::string affinity;
    // the name AS the corpus writes it. The table is indexed by folded key
    // ("alexzanardi"), and the identity registry needs to be able to register the 572 authored fictional ones
    // with a name that can be shown — the key won't do for that.
    std::string name;
    // ~~~04/09 () — THE YEAR THE CARD DESCRIBES, taken from the `era` column on load.
    // The card of a real driver is "what he is TODAY" (user decision, 18/08), but that "today" has a
    // date: Pemberton Sol523 is carded in 1991, Alonso in 2006, the current ones in 2025. Without this datum the engine
    // treated the card as timeless and a 19-year-old came in already at his final value and didn't improve in
    // twenty seasons (measured: his ELO moves +0.031, i.e. 0.39 sigma). With it, the arc is ANCHORED:
    // at the reference age the multiplier is exactly 1 and the number is the usual one.
    // 0 = the column carried no year (40% of the corpus) -> previous behavior, bit for bit.
    int era_year = 0;
    // 16/09 — THE COUNTRY (column 9 of the PSV), and it is needed so these people can RACE.
    //
    // `career_registry` registers as RESERVE everyone who has a card here and does NOT race in any
    // of today's grids, minting their id with `FieldOccupantId` over a `FieldDriver` **without country** (a hash
    // of the name). Then the market seats them — that's what the reserve is for — and the composition could not
    // write their row because it didn't even know their name: 71 rows lost at five seasons and 115 at
    // twenty, measured with `--sinnombre`.
    //
    // And the name alone isn't enough: AMS2 SILENTLY discards a `<country>` its table doesn't know and the
    // driver comes out with the wrong flag (, with the flags test put there precisely for that). So
    // resolving the name without a flag would change "doesn't appear" into "appears wrong". That's why the global index ONLY
    // accepts those who bring a country: whoever doesn't bring one still doesn't appear, on purpose and counted.
    //
    // Empty = either this person resolves through another route (pool, pack, field or pinned, which already carry their flag),
    // or their country was left pending a decision.
    std::string country;
};

// — A REAL DRIVER FROM THE LIVERY PACKS WE ALREADY HAVE.
//
// A row of docs/career/pack_drivers.psv (generated by tools/gen_pack_drivers.py from the catalog of the
// 17 packs). It exists because when giving a seat to every car that races, 245 slots appeared that neither the pinned
// roster nor the field cover, and they were occupied by INVENTED people — while 1,704 real people were on
// disk unused. `tools/skins_to_rosters.py` trims them to the number of liveries when writing the roster.
//
// It brings IDENTITY and not pace, on purpose: 1,981 of the catalog's 2,330 rows take their `race_skill` from a
// POSITIONAL curve (`stat_source=curve`), and using that as absolute capability is the error it forbids.
// The pace of these slots is set by their class's curve, just like that of anyone without an authored card.
struct PackDriver {
    std::string class_name, name, country, pack;
};

// Load (clears its table; false if it can't open) ----
bool LoadFields(const char* path);
bool LoadPackDrivers(const char* path);   // docs/career/pack_drivers.psv
int  PackDriverCount();                   // number of rows loaded (diagnostics/test)

// The k-th available pack driver for a class (k from 0), or nullptr if the class doesn't have that many.
// The file ORDER is significant: uncovered slot k is filled with row k, always the same one.
const PackDriver* PackDriverForClassSlot(const std::string& className, int k);
// And by id, so the composition knows who he is when it only receives the seat's occupant.
const PackDriver* PackDriverById(const std::string& id);
// The stable id of a pack driver: the SAME string as `FieldOccupantId` (pool by name+country -> hash).
std::string PackDriverId(const PackDriver& pd);
// 23/09 — THE CURATED RESERVE (docs/career/reserve_people.psv). The reserve is REAL people (livery packs
// and authored corpus) who don't race in any of today's grids, and they brought neither age nor whether they are still
// active: they entered the world without a birth year, didn't age and never retired. This file curates it
// person by person, with a source: real year, status in 2025 and last season. "unknown" is a
// valid answer and is NOT guessed. Columns: name|country|birth_year|status|last_season|last_series|confidence|source.
enum ReserveStatus { RESERVE_UNCURATED = 0, RESERVE_ACTIVE = 1, RESERVE_RETIRED = 2, RESERVE_UNKNOWN = 3 };
struct ReservePerson {
    std::string name, country;
    int birth_year = 0;           // 0 = not found with certainty
    int status = RESERVE_UNKNOWN;
    int last_season = 0;
};
bool LoadReservePeople(const char* path);   // false if it doesn't open (then nobody in the reserve is curated)
int  ReservePeopleCount();
// By FOLDED name; with namesakes the country decides, and with no country match nobody is returned (two
// different people don't share an age).
const ReservePerson* ReservePersonFor(const std::string& name, const std::string& country);
bool LoadRoster(const char* path);
bool LoadSeriesRosters(const char* path);   // docs/series_rosters.psv (driver->livery binding)
bool LoadAuthoredProfiles(const char* path);   // docs/career/f3_authored_profiles.psv (key = folded name)

// Authored profile for a driver, looked up by NAME, or nullptr if they don't have one (then the usual
// neutral default applies and behavior is identical to before). The key is the FOLDED name: lowercase, no
// accents and no punctuation, because the field writes "Agustín Canapino" while the authored data has "Agustin Canapino"
// — without folding, 51 real drivers lost their profile over a single accent mark.
const AuthoredProfile* AuthoredProfileFor(const std::string& name);
int AuthoredProfileCount();   // number of profiles loaded (diag/test)

// — the name FOLDING by which the authored corpus and the identity pool are cross-referenced
// (lowercase, no accents, no punctuation). It was `static` in here; it's exposed because the identity
// registry has to cross by the SAME key. Two different foldings are two different people: without
// this, 51 real drivers were left without their profile over an accent mark.
std::string FoldDriverName(const std::string& name);

// — the ENTIRE authored corpus, indexed by folded key. The identity registry walks it:
// `AuthoredProfileFor` only answers for one, and classifying the world requires seeing them all (1,672 rows, of
// which 572 declare `is_real=0` — the fictional ones that the catalog today labels as real).
const std::map<std::string, AuthoredProfile>& AllAuthoredProfiles();

// 18/08 — The 0..1 talents of ALL the authored cards, so they can be sampled. It is the reserve from
// which an invented driver's rating comes (career_growth::ProfileTalentCorpus). No guaranteed order: whoever
// consumes it should sort.
std::vector<double> AllAuthoredProfileTalents();

// Parallel ingest: the JSON adapter REPLACES the global table with data that's already
// built, identical to the PSV loader's. Only swaps the global; doesn't touch the PSV path. ----
void ReplaceFields(std::vector<FieldDriver> v);
void ReplaceRoster(std::vector<RosterDriver> v);
void ReplacePinned(std::vector<PinnedDriver> v);

// Pure curve: order 1..fieldSize -> race_skill 0-1, from the profile's AI range (lo-hi on AMS2's ~0-120 scale).
double CurveSkill(int order, int fieldSize, int aiLo, int aiHi);

// The series' real field with skills already resolved (profile curve + icon bump + overrides), ordered by order.
std::vector<FieldDriver> FieldForSeries(const std::string& seriesId);
int FieldRowCount();   // number of series_fields.psv rows loaded -- diag/test (one per field driver, not per series)

// Active roster for a tier (1-7), ordered: hot drivers first, then by higher base_skill.
std::vector<RosterDriver> RosterForTier(int tier);
int RosterRowCount();   // number of series_roster.psv rows loaded (diag/test)

// A class's CHARACTER (docs/career/class_age_profile.psv, generated by
// tools/pool_seed_ages.py --perfil-clases). What you race outranks the tier: a
// junior open-wheel school is full of teenagers, while an endurance GT has 55-year-old gentleman drivers.
// ClassAgeCeiling returns the class's age ceiling, or 0 if it's not in the table (-> the engine
// falls back to its usual flat threshold, so failing to load the file breaks nothing).
//
// Exists because the retirement threshold was a flat 40 for everyone while the initial seeding was handing out
// ages up to 52 based on class character. Measured on Aug 3: 245 retirements out of 940 in the FIRST
// season — the engine was kicking out, all at once, every veteran the seeding had just created.
bool LoadClassAgeProfile(const char* path);
int  ClassAgeCeiling(const std::string& className);
int  ClassAgeProfileCount();   // number of classes loaded (diagnostics/test)

// A class's CHARACTER exactly as the classifier emitted it (school | development |
// open-wheel | touring | endurance | historic); "" if it's not in the table.
std::string ClassCharacter(const std::string& className);

// Is it a SCHOOL class (karting, entry-level formulas)? You ENTER a school class, you don't return to one:
// it's an entry point into the sport, not a place to land when things go badly. Without this rule the
// cascade was sending already-debuted drivers back to karting — it showed up on the first pass
// over a real save, "Bruno Almeida -> Ivory Cup 6423".
bool IsSchoolClass(const std::string& className);

// A series' pinned roster in the file's ORDER (= real standings). Empty if the series
// isn't in series_rosters.psv (then ComposeSeasonGrid uses the series_fields curve-based flow).
std::vector<PinnedDriver> PinnedRosterForSeries(const std::string& seriesId);
int PinnedRosterCount();   // total number of rows loaded (diag/test)

// — the livery of seat `idx` of a pinned series, and how many pinned seats it has, WITHOUT
// copying the whole roster. The per-slot seat key () asks this once per seat, and
// `PinnedRosterForSeries` returns a copy after sweeping the ~2,000 rows: with that, computing the world's
// fingerprint would go from a sweep to a sweep squared. "" / 0 if the series has no pinned roster.
std::string PinnedLiveryAt(const std::string& seriesId, int idx);
int PinnedRosterSizeFor(const std::string& seriesId);

// .5 — WHERE `ComposeSeasonGrid` GETS THE ID IT USES TO LOOK UP THE ELO. **ON in production**
// since 10/08; the flag survives so the bench (`career_test --seedlab`) can measure the before.
//
// It used to pass the row's RAW id (`fd.id`/`pd.id`), which in the PSVs comes EMPTY: 792 of 2,253 rows
// fell back to the static curve while their rivals in the same car came out of RatingQuality(ELO) — a gap of
// 0.461 in 72 of 100 classes. With the shared resolution (`FieldOccupantId`/`PinnedOccupantId`, the same one
// as the seats and the seeding): 60 rows and 11 classes.
//
// It can NOT be turned back OFF without putting the cohort back to 2140: the two halves went in together
// on purpose. With this one alone, the farm system leads 48 of 100 grids.
extern bool g_seedIdResolveOccupant;

// BENCH SWITCH (-> decides the ) — the cohort ONLY at its home tiers, also in
// the GRID. The SEATS already respect `bal::COHORT_HOME_MAX_TIER`; `ComposeSeasonGrid` doesn't, and that's why the
// farm system shows up in 97 of 100 grids. OFF in production: truly turning it off is the (building the
// series from contracts), not a flag. Here it serves to MEASURE how much of the grids' width is put by
// that placement, which is what decides whether the fan-out fixes it or not.
extern bool g_benchCohortHomeOnly;

// CLOSED (27/08) — `g_gridFromSeats` NO LONGER EXISTS. It was the "the grid is built
// from the seats" switch, turned on in production on 10/08, and its declared destiny was to disappear and be the
// only path. It is as of today: with seats the grid is composed from them, without seats the static
// composition (the era memory) rules, and there is NO third way that re-binds occupants afterwards.
//
// What it fixed, so it isn't reopened: with both layers alive, the seats respected
// `bal::COHORT_HOME_MAX_TIER` and the grid didn't, so the farm system appeared in 458 rows above its
// level. Measured with `--seedlab` right before removing it, going back still gave 310 of those rows and 1
// class out of 98 where the cohort LEADS, against 0 and 0 on the seats path.
//
// And just removing the cohort wasn't enough: removing it outright left the grids at 1,961 rows of 2,253 — 292
// fewer cars, meaning half the improvement of that bench was measuring fewer people. Those seats have an owner
// (`TailOccupantId`: the series' own people) and the seats path brings them.

// BENCH SWITCH (/) — the form PERTURBS the capability instead of replacing it.
//
// TURNED ON 12/08, and what unblocked it was discovering that the block was MEASURED WRONG.
//
// It had been off because of this number: "year 1, race_skill dispersion 0.243 -> 0.092, a -62%". But that was
// measured on the TALENT scale, i.e. BEFORE the conversion. And the conversion anchors the ace at 1.00 and scales by
// the global yardstick, so a narrower talent fan expands again. Measured on the scale that
// matters — the XML's <race_skill>, the only thing AMS2 reads:
//
//   year 1, DELIVERED width:   before 0.497  ->  with the formula 0.495   (100%)
//
// It narrows nothing. The "one change, not two" that this seemed to need wasn't needed: the grid doesn't hang
// from the raw talent fan, it hangs from the conversion.
//
// What IS measured and green with the switch on (see the test for ):
//   • year 1: r(authored talent, race_skill) 0.43 -> 0.76, i.e. authoring finally governs
//   • 400 races: r with the ELO 0.67 > r with the authoring 0.59, i.e. the evidence ends up ruling
//
// 18/08 — WHAT OFF MEANS, which until today was the opposite of what the name promises. Turning it off made
// `EffectivePace` return the WHOLE `RatingQuality(ELO)`, i.e. the OLD behavior: it didn't remove the
// form, it swapped it for SUBSTITUTION. With that you couldn't measure "without form", which is the baseline that
// the next phase asks for. Now OFF returns the AUTHORED value as is: zero perturbation.
//   • ON  (production) = authored capability + form delta bounded to bal::FORM_RATING_CAP
//   • OFF (bench)      = authored capability, plain
// The old behavior is still reachable, and through the door that was already declared: NOT passing
// `driverRaces` (see ComposeSeasonGrid's parameter). They are two different questions and now they have two
// different answers.
extern bool g_formPerturbsCapacity;
// ~~~07/09: how much of the AGE CURVE reaches the six quality knobs (consistency, wet, tyres,
// endurance, errors, forced errors). 0 = production: the knobs only follow the curve through their
// load on talent (0.5-0.67). 1 = they scale fully with the arc. 08/09 (D3): in production
// `bal::TRAIT_CURVE_WEIGHT` (0.5) rules; this hook only overrides it to measure when it is >= 0.
extern double g_traitCurveMedicion;
// The factor that multiplies the six quality knobs for an `arc` (talent's age multiplier):
// `(1 - weight) + weight x arc`, with the production weight or the hook's. It is 1 outside (0, 1) or with weight 0.
double TraitCurveFactor(double arco);

// "any exclusion must shrink the grid in a DECLARED way". Why the last composition by
// seats left out a slot. A car that drops out of the XML doesn't disappear: AMS2 puts a generic in, so
// a silent guard is worse than the problem it avoids. Diagnosis of the LAST call, not state.
struct GridDropReport {
    int dup_driver = 0;    // the occupant already drives another car in this grid
    int dup_livery = 0;    // that car is already driven by someone else (two seats with the same slot)
    int player_car = 0;    // it's your car: you drive it, not the AI
    int multiclass = 0;    // already races in a faster class of the same weekend ()
    int unresolved = 0;    // we don't even have a name for that id: the row can't be written
    int over_size  = 0;    // seats above the class's grid size
    int total() const { return dup_driver + dup_livery + player_car + multiclass + unresolved + over_size; }
};
const GridDropReport& LastGridDrops();

// — WHO WRITES A CLASS'S XML. The design's `ClassDeploymentPlan`.
//
// AMS2 loads ONE CustomAIDrivers per CLASS at startup, and 11 ladder classes are run by two or more series
// (the US/EU branches, and the classes that appear in two championships). Until today the owner was chosen by
// `BuildGenOrder`: the MAIN one (`branch_of` empty) always, and the branch never wrote. says
// literally that this is wrong — "the owning series is not chosen by the first incidental element nor
// always by the main branch".
//
// And it is NOT cosmetic. Measured on 10/08: of those 11 classes, **9 compose DIFFERENT people depending on the series**, and
// Onyx_Cup_4824 differs in **36 drivers**. That is, whoever races in the branch races against the other one's grid.
//
// The rule of is "the app selects as owner the series that is preparing for the next
// start". That is known by the caller (it has the calendar and your contracts in front), not this layer: it comes in
// as a preference and here it is only applied. Without a preference, the usual rule.
struct ClassDeployment {
    std::string class_name;        // one XML per class
    std::string owner_series_id;   // the series whose grid is written into it
    std::string reason;            // "next_race" (the preference ruled) | "main" (the usual rule)
};

// The class -> series preference that the app sets before pre-generating. Empty = previous behavior.
void SetClassOwnerPreference(std::map<std::string, std::string> classToSeries);
int  ClassOwnerPreferenceCount();

// The CURRENT plan: one entry per generable class, with its owner and why. It's what would be written
// right now, so it serves as diagnostic and as fingerprint (an owner change forces regenerating and restarting).
std::vector<ClassDeployment> CurrentClassDeployments();

// "the XML fingerprint includes owner". Summarizes the current plan in one number: changing owner
// changes the grid as much as a signing does, and the seat distribution can be identical and not notice.
//
// It goes SEPARATE from `SeatOccupancyStamp` and returns **0 when no preference is set**, which is what makes it
// safe: the plan is only known by whoever has the calendar in front (the app, when pre-generating), so a
// query made before the first pregen can't compare anything. 0 means "I don't know, and not knowing must never
// be read as 'it changed'" — otherwise the AMS2 restart warning would fire right after opening the app.
unsigned long ClassDeploymentStamp();

// A CLASS'S GRID SIZE — THE SINGLE SOURCE OF TRUTH.
//
// Until today there were TWO numbers that didn't talk to each other: the AI XML wrote EVERY livery in the class (66 for
// Coldstead Motors), and the race computed its own seat count with a `min(liveries, 26)` in screen_calendar.
// They only matched by accident, and in 37 of 110 classes they didn't. When there are more names than seats, AMS2 doesn't use
// them all: it picks, and that choice isn't ours, so the championship's rivals would change without warning.
//
// MEASURED IN-GAME (Aug 6, player test with the Oakfield Motors Lum695): the game FIRST takes the liveries the
// CustomAI names. With 4 seats and 5 names, four of ours showed up; with 23 seats and 5 names,
// it filled the rest with generics. So as long as seats <= names, everyone on track is ours. Matching the
// two numbers kills both the auto-fill and the draw in one go.
//
// The number comes from `grid_size` in docs/career/classes.json, which now carries the REAL grid size
// of the championship each class represents (it used to just mirror the livery count: 101 of 101
// identical, so it said nothing). It's ALWAYS capped by the liveries: you can't field a car that isn't
// painted. A class with no entry or 0 -> falls back to the catalog, same as before.
// (reported by a player on Discord): "above 32 cars it runs badly for me". A cap on cars ON TRACK set by the
// player, global to the install (config.json "max_grid"; 0/absent = no cap). It's applied INSIDE
// GridSizeForClassName, the single source of truth for the size: this way the AI XML and the race's seat
// count get trimmed TOGETHER. Trimming further down (GridSizeForSeries) would leave the XML untouched, and with more
// vehicles than seats, AMS2 draws the grid at random.
// A cap < 2 is ignored (a grid of one isn't a race): treated as "no cap".
void SetMaxGridSize(int n);
int  MaxGridSize();

int GridSizeForClassName(const std::string& className);
// Port 0.7.0 — the same number WITHOUT the player's cap on cars on track (). The SEATS use it: the cap
// is a performance setting and trims what goes out on track, not how many slots the class has in the world. With
// the cap inside, lowering "max_grid" to 20 left without a seat anyone who ran from 21 onward and the market stopped
// seeing them; raising it again brought them back. The XML is still trimmed: the by-seats grid stops at `gridSize`.
int WorldGridSizeForClassName(const std::string& className);

// ...AND THE REAL CAP, WHICH THE DRIVERS ALSO SET. Measured on Aug 6: 13 series compose FEWER
// cars than their `grid_size` asks for, because there aren't enough people to fill them — GT4 asks for 45 and can only
// field 26, GT3 asks for 40 and fields 27. And a short XML against a race asking for the full number
// gives back exactly the auto-fill this whole thing is meant to remove.
//
// The authority is THE COMPOSITION ITSELF: it's asked for the class size and we count what it returns. A
// parallel formula ("pinned + cohort + field") would be faster and would drift out of sync the first time
// someone touched the seat assignment; this can't lie because it's the same code that writes it.
// Unknown series -> 0.
int GridSizeForSeries(const std::string& seriesId);

// Invalidates the "names each class's XML is going to carry" table that bounds GridSizeForSeries. It's called by
// the loaders whose inputs change (series, fields, roster, pinned, liveries, classes.json). Same
// spirit and same reason as InvalidateCalendarProjectionCache: an un-invalidated cache starts lying the moment a
// test loads another fixture, and it lies silently.
void ResetGridSizeCache();
// 16/09 — invalidates the GLOBAL IDENTITY INDEX (career_fields.cpp). Called from the same
// places as `ResetGridSizeCache` (every series/field/pinned loader) and from `LoadAuthoredProfiles`,
// which is the index's other source. Without this, reloading data in the same session -- the suite does it all
// the time -- would leave the index with pointers to names from the previous load.
void ResetGlobalIdentityIndex();

// Publishes how many names the pregen left in each class's XML. Called by whoever writes them
// (PregenAllAi) with what it JUST wrote, because the real number depends on the living world's seat
// map, and this layer is pure: it doesn't know about the save. This way, the seats the race asks for can never exceed
// the names the file carries -- and exceeding it means generics, silently.
void SetObservedXmlNames(std::map<std::string, int> perClass);
int  ObservedXmlNamesCount();   // diag/test

// AND THE THIRD CASE: THE MULTICLASS WEEKEND. Here it's not one series racing, it's several on the SAME
// grid, and the number the race asks for is the whole meeting's — the IVORY_CUP_12573 fields 36 cars, not the 56
// its three classes would add up to separately. `seriesIds` = the series present (deduped by class).
//
//   MeetingClassShare = cars per class = curated total / number of classes, CAPPED by whichever class can
//                       field the fewest. That cap isn't cosmetic: Garage 56 has 2 liveries, and as the IVORY_CUP_12573's fourth
//                       class it was capping the whole weekend at 8 cars (2 per class), which is why it was pulled from the group.
//   MeetingGridSize   = share * number of classes = what's actually going to be on track.
//
// The equal split across classes is already handled by the game's own grid builder (it divides the total by n);
// this only feeds it the correct total. A single series -> its normal size. A group with no curated total -> the sum.
int MeetingClassShare(const std::vector<std::string>& seriesIds);
int MeetingGridSize(const std::vector<std::string>& seriesIds);

// AND THE FOURTH CAP, THE TRACK'S: WHAT FITS ON THIS ROUND'S CIRCUIT.
//
// 16/09. A series has ONE size, but it races on different circuits, and not all have the same slots:
// having measured the 230 installed tracks (docs/ams2_track_slots.psv), **52 of the calendar's 1,238 races
// asked for more cars than fit** -- Stock USA 40 at Coldton Bay (32 slots), the 60s touring cars thirty
// at Northhurst Park 1971 (26), GT3 forty at Westvale Speedway (26). Nobody noticed because the engine didn't have a single
// capacity datum.
//
// The fix is NOT trimming the series' number: taking Northhurst Park 1971 out of the Keystone_Masters_11138 calendar, or lowering their
// whole grid, over FOUR cars on one date out of fifteen, would be worse than the problem. What gets bounded is the
// ROUND. And it's not a new concept: the size already varies per round (a multiclass round aligns the meeting's part
// and a standalone one the whole class); this is one more cap, of the same kind as the player's `max_grid`.
//
//   RoundClassShare = the cars of THIS class in this round, bounded by (track capacity / no. of classes).
//   RoundGridSize   = share * no. of classes = what will really be on track in that round.
//
// `cita` = the series present (the one the caller already resolves), `trackHash` = the round's circuit.
// Unknown capacity (trackHash 0, circuit outside the PSV) -> NOTHING is touched: the usual value is returned.
// A base size < 2 (unresolved series) comes out AS IS, with no cap and no floor, so the caller keeps
// applying its historical floor of 16 -- putting a 2 in here would turn it into a two-car race.
int RoundClassShare(const std::vector<std::string>& cita, const std::string& seriesId, int trackHash,
                    int requestedTotal = 0);
int RoundGridSize(const std::vector<std::string>& cita, const std::string& seriesId, int trackHash,
                  int requestedTotal = 0);

// How many of the field get replaced by the roster, based on tier (capped at gridSize).
int ReplaceCount(int tier, int gridSize);

// FORMAL derivation of "freed seats". The recurring cast does NOT cut in ahead
// of the real field: the model is "roster = the lowest-reputation real teams". The real field keeps
// the BEST seats (top-by-`order`, order 1 = best) and the cast DISPLACES the worst real drivers (the ones with the
// highest `order`). Derived ONLY from the field[]'s `order` (no corpus needed). Deterministic and livery-stable.
//   FreedSeatCount(series, grid) = number of seats the cast occupies = ReplaceCount(tier, grid), capped.
//   RealFieldCount(series, grid) = number of real drivers kept (top-by-order) = grid - FreedSeatCount, with a
//                                  floor = min(8, fieldSize) (don't let the cast displace below the
//                                  minimum real field; consistent with the field>=8 invariant).
int FreedSeatCount(const std::string& seriesId, int gridSize);
int RealFieldCount(const std::string& seriesId, int gridSize);

// Composes the grid (exactly gridSize entries if there's data; feasibility permitting):
//  a series with a pinned roster (series_rosters.psv): each driver carries THEIR OWN livery from the PSV every
//    round; the roster's cast occupies only free liveries, in deterministic order.
//  everything else: curve-based field + cast graft, livery by fixed position (stable across rounds).
// Shared empty map for ComposeSeasonGrid's default (a const-ref parameter doesn't allow
// a temporary with stable linkage as a default in C++17; this global const guarantees parity = baseline).
const std::map<std::string, float>& EmptyRivalForm();

// exclude (multiclass): driver names to OMIT from this class because they're already in a faster class on the
// SAME multiclass weekend (avoids the same driver appearing 2x in the combined grid). Gaps get filled with the
// next non-excluded candidates (cast/field). null/empty = no exclusion (original behavior).
// driverRatings (optional): live per-AI-driver Elo rating from SaveData::driver_ratings, keyed by
// stable pool driver_id. When present and a driver has a stored rating, their race_skill is derived
// from RatingQuality(rating) (0..1) instead of the static CurveSkill / base_skill. Drivers with no
// stored rating fall back to the existing curve (back-compat: nullptr == old behavior exactly).
// The driver model enters grid composition -------------------------------------
// A catalog class's car family. Affinity (career_traits) makes the same driver perform
// differently depending on what they're driving, so the partition has to separate DISCIPLINES, not labels. Rules, in
// this order: (1) Historic = pre-wing/pre-slick machinery (vintage formulas, 60s/70s touring cars): driving them
// is a different craft, so they're their own family; (2) OpenWheel = single-seaters ("F-"/"FE-") and karts; (3) Oval = the
// US stock cars (drafting and traffic, no braking: a discipline apart); (4) Prototype = Pemberton_Zel121/LMP/DPi/Group C/Hypercar/
// P1..P4; (5) GT = GT1..GARROWBY_HEX394/MERROWDOWN_MYR373/Valemont_Kor951 and GT one-make series; (6) everything else, Touring.
// Fine per-series granularity is CURATED data; what this map guarantees is that the partition is
// stable (a class always lands in the same family) and that it genuinely separates disciplines.
CarFamily FamilyForClass(const std::string& className);

// Archetype bias for the factor model, DERIVED from the authored profile (career_customai::ProfileForArch):
// the archetype's offset from the neutral profile, not its absolute value (the level comes from talent
// and the genome). Empty/unknown archetype -> zero bias = pure model.
TraitBias BiasForArchetype(const std::string& archetype);
// 26/08 — the COMPLETE bias: archetype + PSV authored datum + scale cap. It's what
// ApplyDriverModel uses, and exists separately so the suite can reproduce the wiring WITHOUT copying the logic.
TraitBias BiasForDriver(const std::string& archetype, double authoredAggr, double authoredCons);

// A class's role within the weekend (`blue_flag_conceding` is "owned by: class role").
enum class ClassRole { SingleClass, FastestInMulticlass, SlowerInMulticlass };
// The blue flag value written to the XML based on role. The official guide recommends 0 for the SLOW class in a
// multiclass event: that way it holds its line instead of jerking aside every time a car from the
// faster class arrives (which in a real multiclass race is constantly, and produces AI that throws itself into the gravel).
double BlueFlagForRole(ClassRole role);

// A series' class ROLE, derived from the SAME FASTEST-FIRST order and the SAME
// curated co-occurrence map WriteAllClassGrids uses. It exists because the blue flag was getting lost when REWRITING a
// class per event: WriteSeasonGridForEvent/WriteCalendarGridForEvent were composing with no role, so the SLOW class
// in a multiclass event would revert to 0.95 and keep pulling aside non-stop in the next session. The data is NOT recomputed
// ad hoc in the caller: it's requested here, where the derivation lives. Unknown series or single-class -> SingleClass.
// Pure over the catalog (series + calendar + liveries); no caching, so the test suite can swap the data.
ClassRole ClassRoleForSeries(const std::string& seriesId);

// The ladder series that WRITES a class (same dedup rule as the BATCH generator:
// the MAIN series wins over a US/EU branch). Needed by the path that only knows the class — the overlay's
// fallback grid, which arrives by vehHash — so it can compose with the production pipeline instead of
// the tribute grid. nullptr if no ladder series runs that class.
const SeriesDef* SeriesForClassName(const std::string& className);

// Completes a TRIBUTE grid (career::AssembleGrid) with the TEAM's equipment data, which is
// the only thing from the production pipeline that path can receive without a series behind it. It applies the car
// scalars and the unit's reliability, resolving by the VANILLA livery_name just like ComposeSeasonGrid.
// What it does NOT do, and why: it doesn't synthesize `talent`. The only absolute number a tribute grid
// carries is the authored `skill` from ams2_rivals.psv (0.84..0.98, SD ~0.035), and BuildGridXml's anchored
// conversion expects talent on the model's scale (SD 0.081 within a class). Fed in as-is, the
// grid would come out with HALF its class's spread; rescaling it would mean inventing a scale that data
// never had, because the range remap only ever used its ORDER. With no talent, BuildGridXml keeps that remap,
// which is exactly the behavior this path already had. Only classes OUTSIDE the ladder hit it: the ones
// in the ladder already compose through the production pipeline (SeriesForClassName).
// Stable with season 0 ('*'): a class with no series has no era to resolve.
void ApplyHomageGridModel(const std::string& className, std::vector<GridDriver>& grid);

// — THE SEAT AS SEEN BY THE GRID. It's the living world -> grid bridge, ORDERED: a
// `livery -> occupant` map says who drives what, but not in what order the seats are, and the order is
// the series' standings position (the one that sets the class's curve). It's passed as a vector and not as
// `career::Seat` because this layer is pure and can't include `career_seats.h` — which in turn includes
// it. It's produced by `career::SeatRowsForSeries`, which is the one that has the save in front.
struct SeatRow {
    std::string livery_name;    // the car: the key the two layers already shared
    std::string occupant_id;    // who drives it according to the living world. Never empty on the bridge.
    // "There's ANOTHER person here than the one who put the curated datum": the market signed, someone retired, new
    // people came in. It's consumed by the livery uncrossing in `career_skins`, which respects that mark strictly —
    // the day the market moves a driver to another team, his signing has to be visible, and without the mark the
    // uncrossing would return him to his usual car and hide it.
    //
    // It's decided HERE and not in the composition because here both sides are: the occupant DERIVED from the datum
    // and the one the save has seated. Before, `rebindFromSeats` deduced it by comparing NAMES, which was the
    // only thing it could do with an empty `pd.id`; since there is one identity per person and ids are compared.
    bool moved = false;
};

// seats (optional) — THE LIVING WORLD'S SEATS. With them, the grid is COMPOSED from whoever the
// market has seated in each car instead of taking people from the static files; without them the
// static composition rules. Until 27/08 it also came in as a `livery_name -> driver_id` MAP for the bridge
// `rebindFromSeats`, which re-bound the grid AFTER composing it; that bridge was retired when closing the .
//
// Until now this function never once mentioned `SeatsForClass`, `seat_occupant`, or `gen_drivers`, so
// the market and the tarmac were talking about different people: a rookie who signed with a team wouldn't show up in the
// race, and the teammate your contract assigned you wasn't on the grid in 66 of the dominant profile's
// races (measured with the simulator). This is the task the roadmap had set aside separately.
//
// Passed in as a MAP, deliberately not by looking at the save: this layer is pure and knows nothing about seats or the save.
// Same pattern as `driverRatings`. nullptr = EXACTLY the previous behavior.
//
// — `exclude` are DRIVER_IDs, no longer names. A multiclass weekend can't seat the
// same gentleman in two cars, and until here that was checked by comparing NAME STRINGS against the ones the
// fast class had placed. Three ways to fail, and all three were alive:
//   • the composition's own dedup adds a suffix (" 2", " 3"), so the second Fulano of a grid
//     entered the registry as "Fulano 2" and no longer excluded anyone;
//   • the swap to a tribute changes the name and NOT the id, so the same driver was registered with one name and
//     checked with another;
//   • and 27 namesakes in the corpus (54 rows) excluded each other while being different people.
// The id is the identity, and since there is exactly one per person. An empty id does NOT exclude —
// excluding by empty would throw out of the grid everyone who doesn't resolve.
std::vector<GridDriver> ComposeSeasonGrid(const std::string& seriesId, int tier,
                                          double playerForm, int gridSize, bool useHomage,
                                          const std::map<std::string, float>& rivalForm = EmptyRivalForm(),
                                          const std::set<std::string>* exclude = nullptr,
                                          const std::map<std::string, double>* driverRatings = nullptr,
                                          ClassRole role = ClassRole::SingleClass,
                                          const std::vector<SeatRow>* seats = nullptr,
                                          const std::set<std::string>* skipLiveries = nullptr,
                                          // /how many races each driver has. It is the CONFIDENCE with which
                                          // his ELO can perturb his authored capability. nullptr = full
                                          // confidence, which is the previous behavior (`driver_races` didn't come in).
                                          const std::map<std::string, int>* driverRaces = nullptr,
                                          // 18/08 — THE CLONE ENTERS WHERE HE IS, NOT AT HIS CEILING. The card of
                                          // a GENERATED occupant (`SeatPerson::cloned`) is sampled from the corpus of
                                          // AUTHORED cards, which are levels of MATURE people today; giving it as
                                          // is to a 19-year-old makes him come in performing like the adult he was
                                          // cloned from. With the three present, `ComposeGridFromSeats` treats it as a
                                          // CEILING and multiplies it by the TWO halves of the age curve (age =
                                          // worldYear - birthYear): `GrowthProgress` (the RISE, personal if there is one)
                                          // by `AgeDeclinePart` (what age takes away past 32, ).
                                          // They are exactly the two that the ELO channel already applies. A driver with an
                                          // AUTHORED card isn't touched by a single bit: the `cloned` flag is never set on him.
                                          // If any of the three is missing -> multiplier 1, the behavior
                                          // as always.
                                          const std::map<std::string, double>* driverCurve = nullptr,
                                          const std::map<std::string, int>* birthYear = nullptr,
                                          int worldYear = 0);

// 18/08 — THE SEED OF A GENERATED OCCUPANT (`gn_<year>_<seed>`, `fl_<series>#<i>`). It lives inside the
// id itself (see the long comment in career_fields.cpp next to the definition) and is what decides, via
// `RookieProfileTalentCloned`, that person's CLONED card — the ceiling this file applies to the
// progress. Exposed (no longer `static`) because the harness needs it to reproduce the same ceiling and verify
// that nobody comes in above it.
unsigned long SeedFromGeneratedId(const std::string& id);

// Bounds the noise of an already-composed grid: limits `talent - authored_talent` to
// `bal::NOISE_SPAN_FRAC` of THAT grid's authored fan. Idempotent. Fan <= 0 -> touches nothing.
//
// "With authoring" is `authored_talent > 0`, the SAME boundary used by everything that reads this field
// (ApplyDriverModel only writes it when the authored one is > 0). The other rows are left untouched and don't
// count for the fan: a 0.0 slipped in would drag the floor to zero and the limit to ~0.25, which is
// as good as turning off the clamp for the whole grid without anyone noticing. See the body.
void ClampGridNoise(std::vector<GridDriver>& grid);

// (REMOVED Jul 2) AdvanceRivalSeason(Aged) — the cast's scheduled SKILL growth (deltas of
// rival_form toward targets tied to the player's tier via a growth archetype) belonged to the
// ABSOLUTE difficulty model and no longer exists. The cast's ability is INNATE and constant (authored base_skill +
// CastSeedFromBase); what grows is their FAME/recognition = the ELO rating evolving from results
// (SettleRatingGrid) and its REP view. rival_form remains a legacy hook, always empty in production.

// HYBRID retirement decision, pure and deterministic (no RNG; "noise" = a stable hash of the id).
//  AUTHORIZED: retire_year>0 and currentYear >= retire_year -> retires (icons with a real year).
//  PROCEDURAL by age: age >= RETIRE_AGE_BASE + spread(id) (deterministic spread 0..RETIRE_AGE_SPREAD).
//  PROCEDURAL by rating: agedRating < RETIRE_RATING_FLOOR AND age >= RETIRE_MIN_AGE (a young slump doesn't retire).
// unknown age (birthYear<=0) -> only the retire_year path can retire them (no age, no procedural path).
bool ShouldRetire(const RosterDriver& d, int currentYear, double agedRating);

// A retirement frees a seat -> promotes the best eligible CANDIDATE one tier up. Pure and
// deterministic. Input: 'retiredByTier' = how many seats opened up in each tier (index 1..7);
// 'cast' = the full roster (the union of tiers); 'agedRating' = rating already aged, keyed by id (to rank
// candidates). Returns the promoted IDs with their destination tier, ORDERED (tier asc, then rating desc).
// A candidate is eligible if their tier_max reaches the destination tier and their agedRating >= PROMOTE_MIN_RATING; a
// driver who's already retired (not in 'cast') never gets promoted, nor is the same seat filled twice. No RNG.
struct Promotion { std::string id; int to_tier = 0; };
std::vector<Promotion> PromoteFreedSeats(const std::map<int,int>& retiredByTier,
                                         const std::vector<RosterDriver>& cast,
                                         const std::map<std::string,double>& agedRating);

// (real->generated drift) — the [0,1] fraction of a series' field that's STILL real after 'seasons'
// of advancement, given a retirement/replacement rate. Simple, deterministic model: each season a
// RETIRE_REPLACE_RATE fraction of the remaining real drivers gets replaced by generated ones (geometric decay).
// seasons<=0 -> 1.0 (world intact). startRealCount<=0 -> 0.0. Pure. Feeds the "fully generated" endgame and diagnostics.
double WorldIsRealForClass(int startRealCount, int seasons, double retireReplaceRate);

// A driver's age in the world's current year. birthYear<=0 -> -1 (unknown, the caller uses the
// procedural-by-id path). currentYear < birthYear -> 0 (defensive). Pure, stateless.
int DriverAge(int birthYear, int currentYear);

// The skill multiplier for AGE (1.0 = peak). Rises from AGE_RISE_FLOOR at AGE_RISE_FROM up to 1.0 at
// AGE_PEAK_LO; plateau [PEAK_LO, PEAK_HI]; declines gently to AGE_DECLINE_KNEE, then accelerates; floor AGE_MULT_FLOOR.
// age<0 (unknown age) -> 1.0 (neutral: doesn't age). 'gtShift' moves the peak (GT/endurance drivers peak
// later): effective age = age - gtShift (open-wheel gtShift=0). Pure, deterministic, no RNG.
double AgeCurveMultiplier(int age, int gtShift);

// — the TWO HALVES of that curve, and their product is EXACTLY it (a test pins it).
// `AgeRisePart` is what brings you closer to your ceiling: the half that circumstance BENDS ("time is lost, not
// the ceiling", ). `AgeDeclinePart` is what age takes away past 32: the irreversible part, which the
// spec deliberately leaves in the clock's hands. See career_growth.h.
double AgeRisePart(int age, int gtShift);
double AgeDeclinePart(int age, int gtShift);

// Real driver->livery pairing (docs/driver_liveries.psv: series_id|driver|livery_name). Only affects
// FIELD classes (pinned ones already bind via series_rosters.psv): the real field keeps its curve-derived skill and
// ONLY its livery gets fixed to its real-life team's. Liveries CAN repeat (two drivers, same livery = ok).
bool LoadDriverLiveries(const char* path);
std::string DriverLiveryFor(const std::string& seriesId, const std::string& driver);   // "" if there's no binding
// number of (series, driver) -> livery BINDINGS loaded (diag/test). NOT the number of series with a binding: the
// structure is series -> driver -> livery, and counting only the outer map would give series, not PSV rows.
int DriverLiveryCount();
void ReplaceDriverLiveries(std::map<std::string, std::map<std::string, std::string>> m);  // parallel ingest/test

// Materializes it: resolves class->stem and writes the XML (backing up the player's).
// 09/09 (): `aiPct` = the class's effective slider (0 = unknown, the table as is). See BuildGridXml.
bool WriteSeasonGridXml(const std::string& seriesId, const std::vector<GridDriver>& grid, const std::string& customAiDir,
                        int aiPct = 0);

// Pass 3 — the BATCH generator: pre-writes the XML for EVERY ladder class at once (the boot-time read
// AMS2 requires it: it reads CustomAIDrivers at startup, no hot reload). It iterates the MAIN series first
// (empty branch_of), then the branches, deduped by class stem -> a shared class (US/EU) gets written by its
// main series. gridSize = the class's livery count (the full grid). Uses the given rivalForm (cross-season
// growth) so the cast persists at its current level. Returns the number of classes written.
// If 'out' != null, collects {seriesId, grid} per class written (ABSOLUTE skills, for diagnostics/CSV; the XML
// on disk carries the NORMALIZED race_skill with top=1.0 that BuildGridXml produces).
// driverRatings (optional): live per-AI Elo ratings (SaveData::driver_ratings) forwarded
// into ComposeSeasonGrid so the BOOT grids reflect each driver's seeded rating, not the flat static curve. The
// per-event regen paths already pass it; this closes the boot-path gap (nullptr == old flat-curve behavior).
// seatsBySeries (optional) — THE LIVING-WORLD -> GRID BRIDGE on the path that ENDS UP IN THE XML
// AMS2 reads: `series_id -> (livery_name -> driver_id)`, i.e. career::SeatOccupantsByLivery's map, per series.
// Without it, everything the market moves during the season stays in the model and never hits the tarmac.
// nullptr = EXACTLY the previous behavior.
int WriteAllClassGrids(const std::string& customAiDir,
                       const std::map<std::string, float>& rivalForm = EmptyRivalForm(),
                       bool useHomage = false,
                       std::vector<std::pair<std::string, std::vector<GridDriver>>>* out = nullptr,
                       const std::map<std::string, double>* driverRatings = nullptr,
                       const std::map<std::string, std::vector<SeatRow>>* seatsBySeries = nullptr,
                       const std::map<std::string, std::set<std::string>>* playerLiveriesBySeries = nullptr,
                       // /races per driver = the CONFIDENCE with which his ELO can displace
                       // his authored capability. nullptr = ELO SUBSTITUTION (the previous behavior).
                       const std::map<std::string, int>* driverRaces = nullptr,
                       // 18/08 — THE CLONE ENTERS WHERE HE IS, ALSO IN THE XML THAT AMS2 READS. The three
                       // travel as is to `ComposeSeasonGrid` (see the long comment there): without them, the
                       // generated 19-year-old occupant comes out written to the file with the card of the adult he was
                       // cloned from, and on top of that the world that runs (`RunWorldSeason`, which does pass them) would decide
                       // the results with another pace. If any of the three is missing -> multiplier 1, i.e.
                       // the previous behavior bit for bit.
                       const std::map<std::string, double>* driverCurve = nullptr,
                       const std::map<std::string, int>* birthYear = nullptr,
                       int worldYear = 0,
                       const CareerProgress* prog = nullptr);   // 09/09 (): the effective slider per class

// Integrity check over the real data (empty = clean).
std::vector<std::string> ValidateFieldsData();

// Living World: the ACTIVE-MODERN subset of the world ----------------------------------
// Modern/historic split: a series is MODERN-ladder unless its content is locked to a real bygone era.
// The discriminator is SeriesDef.era ("2024","1991",…): a leading 4-digit year >= MODERN_ERA_CUTOFF (or no
// parseable year — fictional/timeless Reiza formulas, karts) => modern; an older year (1991, 2004, 2005…) =>
// historic memory. Pure (reads only the series table). Unknown series id => false (not modern).
bool IsModernSeries(const std::string& seriesId);

// The ACTIVE-MODERN driver set: every stable driver_id that (a) appears in a MODERN-ladder series' field
// this season AND (b) is alive (DriverAge from birthYear/currentYear is not past the retire window) AND (c) is
// not in the `retired` set. This is the substrate the Profile world-ranking renders (the ~1336-rated pool is
// the full substrate; only this active subset shows on the modern board). `birthYear` = SaveData.birth_year
// overrides (id -> year; empty/0 falls back to the roster's own birth_year). `currentYear` = the world clock
// (0 = age unknown -> treated as alive). `seriesFilter` (optional) restricts to a single series' field (the
// screen's series filter); empty = the whole modern world. Pure: no globals mutated, no I/O.
std::set<std::string> ActiveModernSet(const std::map<std::string, int>& birthYear,
                                      const std::set<std::string>& retired,
                                      int currentYear,
                                      const std::string& seriesFilter = std::string());

} // namespace career
