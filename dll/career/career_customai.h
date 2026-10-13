#pragma once
#include <string>
#include <vector>
// Custom AI: livery catalog + XML construction + grid assembly + backup/restore.
// Pure logic + I/O with paths passed as parameters (testable in career_test with temp paths).

namespace career {

// A grid driver, tied to a specific livery (a <driver> entry in the XML).
// driver_id/kind are stable IDENTITY (not serialized to the game's XML — see BuildGridXml,
// which ignores these fields). driver_id survives the tribute-name swap (name changes, driver_id does NOT).
struct GridDriver {
    std::string livery_name, name, country;
    std::string driver_id;   // stable id from the pool (empty in PSV mode: PSV loaders don't mint ids)
    std::string kind;        // "real" (field/pinned) or "roster" (recurring cast); "" if unknown
    double race_skill = 0.90, qualifying = 0.90, aggression = 0.60,
           defending = 0.80, consistency = 0.85, reliability = 0.95;
    // ABSOLUTE AI personality (0-1; does NOT scale with the global slider, unlike race_skill/aggression).
    // Filled in by ApplyAiPersonality(archetype, base_skill). Neutral defaults (a driver with no archetype).
    double stamina = 0.78, start_reactions = 0.80, wet_skill = 0.72, tyre_management = 0.78,
           avoidance_of_mistakes = 0.60, avoidance_of_forced_mistakes = 0.55,
           fuel_management = 0.65, blue_flag_conceding = 0.95, weather_tyre_changes = 0.70;
    // The driver's ABSOLUTE talent (<0 = no data -> BuildGridXml falls back to the usual range-remap).
    // Placed at the END on purpose: putting it earlier would break the positional initializers already in the test suite.
    double talent = -1.0;
    // Car scalars, from version 1.6.9.9. They belong NOT to the driver but to their EQUIPMENT UNIT (team/model/
    // package), so teammates carry IDENTICAL values. Set by career_teamcars::ApplyTeamCarScalars
    // on the VANILLA livery_name, and BuildGridXml only emits them if any value departs from 1.000 — with no
    // team data the XML comes out byte-for-byte as before. Like talent, these go at the END because of the
    // test suite's positional initializers.
    double weight_scalar = 1.0, power_scalar = 1.0, drag_scalar = 1.0;
    // 09/08 — "the LIVING WORLD changed this car's occupant". RepairDriverLiveryPairing reads it
    // to NOT TOUCH that row: the driver<->paint uncrossing repairs the AUTHORED curation (which was expressed
    // against vanilla liveries), not the market's decisions. Without this mark, the day the market moves
    // Verstappen to another team the uncrossing would return him to his paint and the signing would not be seen.
    //
    // 27/08 () — WHO SETS IT, which has changed. It had two producers: `rebindFromSeats` (the bridge
    // of the static path) and the SEATS layer. The bridge was retired, so now it is set ONLY by the
    // seats layer (`ComposeGridFromSeats`: `g.seat_moved = sr.moved`), which is the one that sees both sides and compares
    // IDs instead of names. A grid without seats —the era memory— never has it set, which is
    // right: there is no market there that has moved anything.
    //
    // And why it is still here: its only consumer is `RepairDriverLiveryPairing`, which is precisely what the
    // asks to retire. This field goes with F, not with E.
    // It goes AT THE END, like talent and the scalars, because of the suite's positional initializations.
    bool seat_moved = false;
    // 17/08 — THE AUTHORED TALENT, UNPERTURBED. `talent` can arrive with TWO perturbations inside,
    // the shape (EffectivePace, cap FORM_RATING_CAP 0.10) and the per-family affinity (AffinityFor, cap
    // kAffCap 0.12), and both are ABSOLUTE against a spread of CURATED CARDS of 0.11 in F1. Storing the starting
    // value is what lets the COMBINED noise be bounded in a single place
    // (career_fields::ClampGridNoise) instead of recalibrating the two functions separately.
    // 18/08 — TODAY ONLY ONE IS ALIVE: affinity was left at scale 0 in production on 17/08
    // (career_traits.cpp::g_affinityScale), so the only thing separating `talent` from this field is the shape.
    // The affinity mechanism is still fully there and comes back with a number, which is why the clamp still bounds the
    // effect of both.
    // 18/08 — WHAT THIS NUMBER IS, EXACTLY, FOR A ROW WITHOUT A CARD: the CURVE point of its slot,
    // not anyone's authorship (career_fields.cpp, `base = p.talent > 0 ? p.talent : curvePoint`). That is why the
    // spread of this field in an F1 grid measures 0.3000 and not the 0.11 of the curated cards, and why the
    // ClampGridNoise limit comes out 0.0750 and not 0.0275. Anyone who reads “authored” as “a
    // person wrote it” is wrong for most rows. The conversion phase reads this same field.
    // <0 = no data, and “has data” is `> 0` in every reader. NOT every grid row carries it: the one that comes out of
    // `ComposeSeasonGrid` does (ApplyDriverModel always writes it), but `AssembleGrid` (tribute
    // roster) and `ComposeMemoryGrid` (period memories) do not go through there and leave the -1.0.
    // It goes AT THE END, like talent and the scalars, because of positional initializations.
    double authored_talent = -1.0;
};

// Personality profile AUTHORED per archetype (grounded in real packs + the tooltips from
// Reiza). Exposed here because grid composition no longer calls ApplyAiPersonality: it derives the
// archetype BIAS for the factor model and car reliability from this same table. Keeping a
// second, parallel table would drift out of sync at the first edit, so there's just this one.
// Empty/unknown archetype -> the neutral "solid" profile (which also serves as the REFERENCE against which
// everyone else's offset is measured).
// 25/08 — `aggr` ENTERS THE ARCHETYPE (user decision). It was deliberately left out, waiting for a
// per-driver authored datum in the PSV... that was never created: NO PSV in the project has an
// aggressiveness column, so `authoredAggr` never arrived, the bias stayed at zero and the model produced
// values below the clip floor. Result measured on the Highbourne Motors: median 0.200, which is
// EXACTLY the floor -- the whole grid stuck to the edge, with no character and not attacking.
// It goes at the end of the struct so as not to move the thirteen positions that already existed.
struct ArchProfile { double def, cons, avoidM, avoidF, start, wet, tyre, stam, rel; bool erratic;
                     double fuel, blue, weather; double aggr; };
// `found` (optional) reports whether the name actually MATCHED or the neutral profile was returned because it wasn't recognized. Without it,
// a misspelled archetype is indistinguishable from a legitimately neutral one.
ArchProfile ProfileForArch(const std::string& archetype, bool* found = nullptr);

// Is the name in the table? The test suite runs this against EVERY PSV with an archetype column, so a
// typo can never again leave a driver with no character in silence.
bool IsKnownArchetype(const std::string& archetype);

// Car reliability derived from the archetype (the same number ApplyAiPersonality sets). The
// factor model doesn't cover this parameter — it's assigned to the TEAM by design — so it's exposed separately so
// the grid composed from the genome doesn't lose it along the way. It's the DEFAULT in classes with
// authored equipment units, where career_teamcars::ApplyTeamCarReliability overrides it (the car belongs to the team, and
// teammates share its luck); elsewhere it's still the number that reaches the XML.
double ReliabilityForArchetype(const std::string& archetype, double base_skill);

// Fills in a driver's ABSOLUTE personality from their archetype + base_skill (profiles
// grounded in real packs). Accepts PSV cast names (apex/metronomo/cargador/...) AND
// pinned codes (AP/MT/CH/AM); an empty/unknown archetype (field) -> the neutral "solid" profile. Does NOT touch
// race_skill/qualifying/aggression (RELATIVE: set by the pipeline + the global slider, not scaled here).
// ComposeSeasonGrid no longer calls this (traits come from the genome now), but it's still live and validated by the
// test suite: it's used by the code paths that don't compose a season grid.
void ApplyAiPersonality(GridDriver& g, const std::string& archetype, double base_skill);

// Applies the age multiplier to a GridDriver's race_skill/qualifying for the grid
// REGENERATED by the rollover (the AI field "gets older"). ageMult is in [0,1.x]; it only scales the
// RELATIVE parameters (race_skill, qualifying, defending) — the ABSOLUTE personality values are NOT touched. Clamped to [0.05,1.0].
// Pure, stateless. ageMult==1.0 -> no change (the current year's grid).
void AgeAdjustedSkill(GridDriver& g, double ageMult);

// Roster rival definition (assigned to liveries in AssembleGrid). Loaded from ams2_rivals.psv.
struct RivalDef {
    std::string name, country;
    double skill = 0.85, aggression = 0.60, consistency = 0.85;
};

// Livery catalog (docs/ams2_liveries.psv: class_name|xml_name|livery_name) ----
bool LoadLiveries(const char* path);                          // parses the PSV; false if it can't be opened
std::vector<std::string> LiveriesForClass(const std::string& className);   // all livery_name values for the class
// the livery `idx` of the class, without copying the whole vector. Needed by the per-slot seat key
// () once per seat; with the copy, computing the world fingerprint became quadratic. "" when out of range.
std::string LiveryForClassAt(const std::string& className, int idx);
std::string XmlStemForClass(const std::string& className);    // xml_name (the CustomAIDrivers file's stem); "" if not found
int LiveryClassCount();                                        // number of classes loaded (diagnostics/test)

// Resolves a KEY to the catalog's class_name (= the key used by LiveriesForClass/XmlStemForClass).
// The key can be the internal class_name ("GT3", "Beacon_Trophy") or the ladder's display name
// ("GT3", "Kingsmere Uma713", "Formula Vee Brazil Gen 1"). Tries: exact class_name, exact
// display, and normalized (lowercase + [a-z0-9] only, absorbs "Gen 1" vs "Gen1"). "" if nothing matches.
std::string ResolveClass(const std::string& key);

// vehHash -> class_name (docs/ams2_vehclass.psv: veh_hash|class_name|vehicle_name) ----
// The Custom Race handoff ALWAYS carries the car's real vehHash; this resolves the catalog's
// class with 100% reliability, without depending on the name/display (which doesn't always match).
bool LoadVehClass(const char* path);                 // parses the PSV; false if it can't be opened
std::string ClassForVehicle(int vehHash);            // the catalog's class_name, or "" if the vehHash isn't found
// car name <-> vehHash (column 3 of the same PSV). Used by dlc_products.psv to declare
// a pack's content CAR BY CAR, which is the only honest granularity for classes shared across
// multiple packs. The comparison ignores spaces and case; 0 / "" if it's not in the catalog.
int VehicleHashByName(const std::string& name);
std::string VehicleNameForHash(int vehHash);
int VehClassCount();                                // number of catalog vehicles loaded (diag/test)
void ResetVehClassForTest();                         // clears the catalog (tests the boot load wiring)

// track_hash -> track image slug (docs/ams2_track_images.psv: track_hash|slug) ----
// The slug names the circuit map's .dds (gui/trackmaps3d/<slug>.dds, RGB32/BGRA8). The
// track_hash is the handoff's CalSlot.track_hash (signed int). slug<->hash is CURATED (1 row per
// line): the hash is NOT derivable from the slug (different namespaces) — an auto-matcher plus 49 hand-curated entries.
bool LoadTrackImages(const char* path);      // parses the PSV (pipe-delimited, header, # comments); false if it can't be opened
std::string SlugForTrack(int trackHash);     // the map's slug, or "" if the trackHash isn't found
std::string SlugForTrackEnsured(int trackHash);// SlugForTrack + a guarded load of the PSV (paths::Data) if it isn't loaded yet
int TrackImageCount();                       // number of circuits loaded (diag/test)
int TrackHashForSlug(const std::string& slug);   // reverse slug->hash lookup on the SAME curated catalog; 0 if not found
bool LoadTrackCatalogLive(const char* path);        // parses the LIVE catalog of installed items; false if it cannot be opened (TrackHashForSlugLive does this on its own with guard-load)
int TrackHashForSlugLive(const std::string& slug);  // slug->hash from the LIVE catalog of installed circuits (ams2_track_catalog_live.psv); covers ANY installed circuit; 0 if not found

// HOW MANY CARS FIT ON A TRACK (docs/ams2_track_slots.psv: hash|slug|grid_slots|pit_boxes) ----
// 16/09. Until today the engine had NOT A SINGLE capacity datum: an event asked for 48 cars at Northford (which
// has 26 slots) and nobody noticed. The numbers come from the .aiw files of the installed game
// (tools/build_track_slots.py): `grid_slots` = [GRID] entries (starting grid slots) and `pit_boxes` =
// GarPos entries (garage slots). Capacity is the SMALLER of the two — you cannot line up more cars than
// fit on the grid nor put more in the pit lane than have a garage — and that min() is done here, not in
// the file, so the PSV stays raw evidence and where each number comes from can be reviewed.
bool LoadTrackSlots(const char* path);       // parses the PSV; false if it can't be opened
int  TrackGridCapacity(int trackHash);       // cars that fit; 0 = track with no known slots (guard-load 1 attempt)
int  TrackSlotsCount();                      // number of tracks with slots loaded (diag/test)

// vehHash -> image slug (docs/ams2_vehicle_catalog.csv: slug,id,manufacturer_hash,class_guess) ----
// The slug names the folder/files for the DDS images extracted from the game
// (gui/vehicleimages/vehicleimages_<slug>/<slug>_livery_NN.dds). 'id' = the internal vehHash (signed 32-bit).
bool LoadVehicleCatalog(const char* path);     // parses the CSV (comma-delimited, header, UTF-8 BOM); false if it can't be opened
std::string SlugForVehicle(int vehHash);       // image slug, or "" if the vehHash isn't found
std::string SlugForVehicleEnsured(int vehHash);// SlugForVehicle + a guarded load of the catalog (paths::Data) if it isn't loaded yet
int VehicleCatalogCount();                     // number of cars loaded (diag/test)

// class_name -> runtime class hash (docs/ams2_class_hash.psv: class_name|hash) ----
// hash = the class's JSON "value"; feeds the in-memory multiclass injection (the set of classes in the
// grid). Accepts the exact or normalized class_name (spaces/"Gen 1"/case).
bool LoadClassHash(const char* path);                       // parses the PSV; false if it can't be opened
unsigned ClassHashForName(const std::string& className);    // the class's hash, or 0 if not found
// number of classes in the EXACT map (diag/test). Does NOT sum exact + normalized: the normalized map is a
// fallback index over the SAME data (ClassHashForName's fallback); summing them would count each row twice.
int ClassHashCount();

// A car model within a class (real name + vehHash). The vehicle screen lists these.
struct VehicleEntry { int vehHash = 0; std::string name; };
std::vector<VehicleEntry> VehiclesForClass(const std::string& className);   // all car models in the class
// from the portal: the cars the player can use in a race. ruleVehHash 0 = the whole class
// (VehiclesForClass). With a rule, only that car if it is in the class list; if it is not (uninstalled, from another
// class, wrong hash) FAIL-OPEN to the whole class and a line in the log. The guarantee of “never an empty
// prerace” holds for a KNOWN class; an UNKNOWN class (VehiclesForClass already returns {}) has no “whole
// class” to fall back to: it returns {} with its own log line, distinct from the fail-open one. Each line is emitted ONCE
// per (class, rule) and process: CarsByClass calls it on every prerace query.
std::vector<VehicleEntry> VehiclesForRace(const std::string& className, int ruleVehHash);
// Task 7 fix round 2: the rule that is ACTUALLY applied. ruleVehHash if it is not 0 and is in VehiclesForClass(className)
// (installed filter, with its fail-open); 0 if not (not installed, from another class, wrong hash, unknown class).
// It is the SAME condition with which VehiclesForRace leaves the list at that single car, so the player and the rivals
// see the same rule: launches use it for pr.ruleVehHash AND for OppTypeForCarRule. With the raw rule,
// an uninstalled car left the player with the whole class and the rivals on Identical cloning whichever one they chose.
int EffectiveCarRule(const std::string& className, int ruleVehHash);
// Does the class still have ANY installed car? FAIL-OPEN: an empty live catalog or a class with no rows in
// ams2_vehclass -> true. raw=true queries the pure live catalog (detection); raw=false applies overrides.
bool ClassHasInstalledVehicle(const std::string& className, bool raw);
// The same question WITHOUT fail-open, for DETECTING (not blocking): with no live catalog, an unknown
// class, or no cars installed -> false. See the implementation comment.
bool ClassHasInstalledVehicleStrict(const std::string& className);

// AERO-KIT variants for a class (docs/ams2_class_variants.psv) ----
// AMS2 looks up the CustomAIDrivers XML by the EXACT token of the LIVE class, and an aero kit changes that
// token: at Northhurst Park the Falcon Championship runs as Juniper_Cup_LD (measured in-game) and F-USA's oval tracks
// as F-USA_*_SO/_SS/_SW — without an XML under THAT exact name, the entire grid silently falls back to generic drivers.
// The writer leaves a copy of the base class's XML for every variant of its stem (same cars, same
// liveries => identical content); a variant the game never asks for is just an inert file.
bool LoadClassVariants(const char* path);                          // base_stem|variant_stem
int  ClassVariantCount();                                          // total variants loaded
const std::vector<std::string>& VariantStemsFor(const std::string& baseStem);   // empty if it has none

// XML construction (pure) ----
std::string XmlEscape(const std::string& s);
// country alias (IOC/FIFA or other common 3-letter codes) -> the canonical ISO-3166-1 alpha-3 code, which is
// what AMS2's nationality table understands. A code the game doesn't recognize is silently
// dropped and the driver ends up with the wrong flag (reported: Vettel/Hulkenberg with GER). Same table as
// COUNTRY_SYNONYMS in tools/psv_to_season_json.py (there it only mints ids; here it's applied to the LITERAL
// value emitted, which is what the game actually sees) + JAP/NDL as seen in community packs. An unknown code is returned
// as-is (valid ISO codes pass through untouched; the test suite guards the vocabulary, not this function).
std::string NormalizeCountryAlias(const std::string& code);
// The race_skill band derived from the class's target SPREAD (spreadPct = % of the last-place car vs the
// leader; medianPct = % of the median car). Floor and shape come from career_gridcurve. The no-spread wrapper uses
// club-tier defaults for call sites that haven't been updated yet.
// BOTH PERCENTAGES ARE THE DRIVER'S SHARE, not the raw PSV column: the caller first subtracts
// what the car contributes (`DriverSpreadPct` + the median's `shrink`). Counting it twice is the only failure
// in this chain that the harness does not catch by construction — with the live yardstick the extreme lands wherever it is told,
// whatever it is told —, and that is why it has its own test.
// 15/08 — `isOval`: in OVAL classes the whole grid comes out with race_skill 1.0 (decision of the
// user, information from the community/Reiza: AMS2's AI does not run well on ovals below 1). It is the
// LAST thing applied, so talent still decides the order and everything else (quali, aggressiveness,
// reliability, which car each one drives). Default false = behavior as always.
//
// 24/08 (task 5 of the “the gap” plan) — THE SIGNATURE LOSES THREE ARGUMENTS: `refSpan` (the talent yardstick of
// the class), `openWheel` (it chose the band by Curve discipline) and `curveTrimPct` (its per-class trim).
// All three fed the per-PLACE distribution, which has been retired entirely; the width is now set by the spread of
// the CLASS and the extremes, the grid being written. WATCH THE ORDER if an old call shows up:
// `isOval` has moved up to fourth place, so an older `BuildGridXml(g, s, m, vara)` would compile taking the
// yardstick as “this is an oval”. The callers were reviewed one by one; if you add one, write the bool.
// ~~~09/09 (): `aiPct` = the EFFECTIVE slider this class will run at (career_engine::EffectiveAiForClass);
//  the % -> race_skill conversion is rescaled with it (career_gridcurve::TransferScaleForSlider) so that the class
//  delivers its spread at any difficulty. 0 = unknown = the table as is (what there was).
std::string BuildGridXml(const std::vector<GridDriver>& drivers, double spreadPct, double medianPct,
                         bool isOval = false, int aiPct = 0);
inline std::string BuildGridXml(const std::vector<GridDriver>& drivers) { return BuildGridXml(drivers, 6.0, 2.4); }

// Assembly: assigns a roster driver to EVERY livery (TOTAL override of the class) ----
// liveries.size() entries; cycles the roster if there are more liveries than rivals (with a suffix for unique names).
std::vector<GridDriver> AssembleGrid(const std::vector<std::string>& liveries,
                                     const std::vector<RivalDef>& roster);

// Files (dir = the CustomAIDrivers folder; stem = the class's xml_name). Testable with a temp dir. ----
// TOMBSTONES: the <stem>.xml.careerbak file marks the slot as OURS, and its size says what was there before —
// non-empty is the user's original XML, EMPTY (0 bytes) is a tombstone: there was nothing of theirs there.
// Writes our <stem>.xml, leaving that mark the FIRST time the slot is touched (a real backup if the
// user had a file, a tombstone if not). Once the mark is already there, it only overwrites the .xml: it never
// backs up again, so a file of ours can never end up passing itself off as the user's original.
// If the backup (RenameFile) fails while the user has a real file, it ABORTS without writing -> false, and
// `path` is left intact: we'd rather have a class without our grid than a user without their file.
bool BackupAndWriteAi(const std::string& dir, const std::string& stem, const std::string& xml);
// Undoes the above, and ONLY where the mark exists: with no .careerbak next to it, the .xml isn't ours and isn't touched.
// With a tombstone, deletes our .xml and the tombstone; with a real backup, restores the user's. Idempotent: no
// marks are left, so repeating the call (the uninstaller's retry) does nothing.
bool RestoreAi(const std::string& dir, const std::string& stem);

// Rival roster (docs/ams2_rivals.psv: name|country|skill|aggression|consistency|real) ----
bool LoadRivals(const char* path);              // parses the PSV -> roster; false if it can't be opened
const std::vector<RivalDef>& Roster();          // the loaded roster, or DefaultRoster() if nothing loaded
int RivalCount();                               // number of rivals loaded (diag/test)

// Bootstrap roster (tribute cast) — fallback if ams2_rivals.psv isn't available.
const std::vector<RivalDef>& DefaultRoster();

} // namespace career
