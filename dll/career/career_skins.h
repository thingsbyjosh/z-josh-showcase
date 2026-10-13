#pragma once
#include <string>
#include <vector>
#include "career_customai.h"   // "real skins" Task 3: GridDriver (boot grid to rebind)
namespace career {
struct InstalledSkin { std::string vehicle_slug; int livery_id; std::string name; std::string preview_rel; };
// Scans <overridesDir>/<car>/<car>.xml (the ACTIVE one only: stem==folder, ignores *_dist.xml and
// per-round <car>_*.xml variants). Extracts each <LIVERY_OVERRIDE LIVERY="id" NAME="name">. Populates the internal
// (slug,id)->name registry. Returns the number of overrides found. Idempotent (clears first). dir is a parameter (testable).
// aliasPath (optional, docs/livery_override_alias.psv): translates FOLDER name -> car slug, because
// the folder isn't always named after the car (see the long comment in the .cpp). Empty = no translation.
int ScanInstalledSkins(const std::string& overridesDir, const std::string& aliasPath = std::string());
// Name of the installed skin for (slug, livery_id), or "" if none. Case-sensitive, byte for byte.
// liveryId is in GAME numbering (see GameLiveryIdFromCatalogNN): the same as the LIVERY="NN" attribute in the override
// XML, since that XML is exactly where this registry comes from.
std::string SkinNameFor(const std::string& vehicleSlug, int liveryId);

// 2026-07-28 -- THERE ARE TWO LIVERY NUMBERINGS AND THEY DON'T MATCH. Measured, not assumed:
//   . the GAME's starts at 51. Three independent sources agree on it: the
//     `<slug>_livery_NN.dds` filename, the override XML's `LIVERY="NN"` attribute (which is what AMS2
//     actually parses), and the live vehicle registry the `liverycat` command dumps.
//   . the CATALOG's starts at 50. It's the one in docs/livery_vehicle_map.psv and docs/ams2_liveries.psv, both
//     GENERATED from the dedicated server's dump (enum-gg). It runs exactly ONE below.
// Checked by cross-referencing the live dump against the map: 2632 of 2657 pairs come out +1 off (the ~25 remaining
// are classes whose livery list changed after the dump, i.e. a stale catalog, which is a separate problem).
// It matters because g_skins is indexed with the GAME numbering and LiveryVehicleFor returns the CATALOG one: mixing them
// resolves the neighboring slot. It gets converted HERE, at the single point where the two touch.
inline int GameLiveryIdFromCatalogNN(int catalogNN) { return catalogNN + 1; }
// 2026-08-25 -- THE INVERSE, and it's genuinely needed: `CareerVehicleLiveryList` returns the
// GAME numbering and `LiveryNameForSlot` indexes the CATALOG one, so the ordinal -> NN -> livery_name path crosses
// them. Measured ON TRACK by forcing the livery (not deduced): with the player in game slot 52, AMS2
// benched the driver from `#4` -- line 1 of the XML -- instead of `#7`, which is what the catalog
// returned when read unconverted. Swept afterward across 6 cars from different classes: NN 52 -> ordinal 1 in every one.
inline int CatalogNNFromGameLiveryId(int gameLiveryId) { return gameLiveryId - 1; }
// "real skins" Task 4: is there an installed skin for (slug, liveryId)? = !SkinNameFor(...).empty(), without exposing
// the name -- used by cmd.skins.status (career_api_queries.cpp) to check per-entry presence.
bool SkinSlotPresent(const std::string& vehicleSlug, int liveryId);
int InstalledSkinCount();          // diag/test
void ResetSkinsForTest();          // clears the registry

// "real skins" ALL detected installed slots, with their preview_rel (path
// RELATIVE to the override folder, from <PREVIEWIMAGE PATH="...">, "" if the override doesn't declare one). Ground
// truth from disk (independent of the catalog) -- cmd.skins.installed uses it so the app can convert DDS to PNG.
std::vector<InstalledSkin> InstalledSkinSlots();

// TEST-ONLY: injects directly into the g_skins registry without going through ScanInstalledSkins/XML files.
// Used by RebindLiveryName's tests to simulate an installed skin without building a temp dir.
// optional previewRel seeds the slot's preview. Do NOT call from production code.
void SetSkinForTest(const std::string& vehicleSlug, int liveryId, const std::string& name,
                    const std::string& previewRel = std::string());

// "real skins" Task 2: the EFFECTIVE name of a VANILLA livery. Chain: (className, vanillaName) ->
// LiveryVehicleFor (career_seats) -> (vehHash, liveryNN) -> SlugForVehicle (career_customai) -> slug ->
// SkinNameFor (Task 1) -> installed skin's NAME, if any. If ANY step of the chain fails
// (unmapped pair, vehHash with no slug, or no skin installed for that slot) it returns vanillaName UNCHANGED
// -- it never breaks. Pure: only uses maps already loaded (g_liveryVehicle + vehicle catalog + g_skins).
std::string RebindLiveryName(const std::string& className, const std::string& vanillaName);

// "real skins" Task 3: applies RebindLiveryName to every driver in the grid, in place. Touches nothing else
// (name/car/skill/class untouched) -- only rewrites livery_name. No-op if no skins were scanned.
// 2026-08-09: chains RepairDriverLiveryPairing (below) on completion.
// 27/08: `seriesId` (optional) lets you query the curated binding `driver_liveries.psv`, which is per
// SERIES. Without it the previous behavior is kept exactly: all rows take part in the uncrossing.
void RebindGridForSkins(const std::string& className, std::vector<GridDriver>& grid,
                        const std::string& seriesId = std::string());

// 2026-08-09 -- if a car's label NAMES a driver from this grid, the car is THEIRS.
// Uncrosses driver<->livery as a PERMUTATION within the grid: the set of liveries written doesn't
// change (no paint gets duplicated and no car is left without a driver), only who drives which changes.
// Matches by ASCII-folded LAST NAME and only when the pairing is unique in both directions; a driver with no
// pairing keeps their livery if nobody claims it. No-op if no label identifies anyone.
// The why (two curation passes over the same slot, the pack's is the one that shows) is in the .cpp.
// 27/08 () — `intocable` marks the rows that NEITHER offer their paint NOR lose it, same as
// `seat_moved`. `nullptr` = the usual behavior (all take part), which is what the unit tests
// of this function use.
//
// The rule that fills it (`RebindGridForSkins`): **untouchable = is in its CURATED car and its label has NOT
// been repainted by a pack**. All three halves matter and each came from a 27/08 measurement:
//   · if it's curated and NOBODY has repainted the car, the pairing is already right and the surname
//     heuristic can only spoil it — measured: it moved 38 liveries and broke 16 curated pairings;
//   · if it's NOT curated, the heuristic is all there is and it keeps acting (there's vanilla that names
//     drivers: `Millstead Racing #6` in Stock Car). Restricting it to packs lost 21 hits;
//   · if a PACK has repainted the slot, the pack rules even if there was curation: what the player sees is
//     the paint, and that's the case this function was written for (09/08, «Tsunoda at Fairfield_Motors»).
void RepairDriverLiveryPairing(std::vector<GridDriver>& grid,
                               const std::vector<bool>* intocable = nullptr);

// cc8Eelp0: how many pairings in this grid CANNOT be anchored to a SLOT (vehHash, NN).
// Packs overwrite numbered slots and change their label, so the slot is the stable
// identity and the name is what's volatile. When LiveryVehicleFor doesn't know a livery_name's slot, we write
// that name blindly: if the user has a pack for that car it may no longer exist, and AMS2 discards the
// override for the ENTIRE car silently. Returns how many go in blind and, if asked, up to 6 examples.
// Has no opinion (0) if no override is installed at all: without packs, vanilla names are correct by definition.
int AuditGridLiveryBinding(const std::string& className, const std::vector<GridDriver>& grid,
                           std::vector<std::string>* unresolved = nullptr);
}
