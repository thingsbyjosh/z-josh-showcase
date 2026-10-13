#pragma once
// (DLC v2): sellable DLC products, user overrides (Settings > Content),
// series playability by installed content, and substitution of missing tracks.
// Data: docs/dlc_products.psv (track-pack slug / class_name -> the store's sellable product, 2026).
// Inherited FAIL-OPEN rule: with no populated live catalog, nothing gets blocked or substituted.
#include <string>
#include <set>
#include <vector>
#include "career_series.h"   // SeriesDef (per-class playability)

namespace career {

// A sellable product (post-reorg, Aug 2025; per the store's DLC catalog research).
struct DlcProduct {
    std::string id;                        // "premium_track_pack"
    std::string display;                   // "Premium Track Pack"
    std::vector<std::string> trackSlugs;   // the game's track-pack slugs (circuits_ref col 9)
    std::vector<std::string> classes;      // class_name from series.psv whose CORE class belongs to this pack
    // CAR-BY-CAR content (vehicle_name from ams2_vehclass.psv). Membership by CLASS doesn't
    // work for classes shared across several packs (Hypercars: Coldstead Motors Pt1/Pt2 + Highbourne Motors Pt1/Pt2), nor
    // for packs of STANDALONE cars inside base classes (Brazilian Racing Legends in F-Retro/F-Classic,
    // Highbourne Motors Pt2's Eskdale Gry320 in Onyx Cup 12409). When a product lists specific cars, THEY decide:
    // they're what determines if it's detected, and what gets taken down when it's disabled.
    std::vector<std::string> cars;
};

// product catalog (docs/dlc_products.psv) ----
bool LoadDlcProducts(const char* path);    // clears and loads; false if it can't open
int  DlcProductCount();
const std::vector<DlcProduct>& AllDlcProducts();
std::string DlcProductForTrackPack(const std::string& packSlug);   // product_id ("" if base/unknown)
std::string DlcProductForClass(const std::string& className);      // product_id ("")
std::string DlcProductForCarName(const std::string& carName);      // product_id for the specific car ("")
std::string DlcProductForVehicle(int vehHash);                     // same, by vehHash ("")
std::string DlcProductDisplay(const std::string& productId);       // display name ("" if it doesn't exist)

// user overrides (Settings > Content toggles; owned by the INSTALL, not the save) ----
// (30/07 report: "I see a lot marked not detected that are still ON"): TRI-STATE. As long as the player hasn't
// touched the toggle, DETECTION drives the value: pack seen in the live catalogs = on, not seen = off.
// The override is only born when it's moved by hand (detection failing, a pack giving them trouble), and from then on it wins
// in BOTH directions until it's set back to AUTO. There used to be no AUTO: the absence of an override meant ON,
// so packs the player didn't own showed up switched on, and the screen looked like it was telling them they owned everything.
enum class DlcOverride { Auto = 0, On, Off };
DlcOverride ProductOverride(const std::string& productId);
void SetProductOverride(const std::string& productId, DlcOverride v);
void ClearProductOverrides();                                   // everything set to AUTO
void SetDisabledProducts(const std::set<std::string>& ids);     // explicit OFF override for these ids
bool ProductDisabled(const std::string& productId);             // explicit OFF (AUTO doesn't count: see the note below)
const std::set<std::string>& DisabledProducts();
void SetForcedOnProducts(const std::set<std::string>& ids);     // explicit ON override for these ids
const std::set<std::string>& ForcedOnProducts();
// PSV persistence, `product_id|off` / `product_id|on` (paths::UserData("cache\dlc_overrides.psv") in production).
// No row = AUTO. An old install only has `off` rows, which are still read the same way.
bool LoadDlcOverrides(const char* path);   // applies the overrides read (missing file = fine, everything stays AUTO)
bool SaveDlcOverrides(const char* path);

// (24/08): real OWNERSHIP via Steam ----
// AMS2 ships DLC files to everyone (ownership only gates its own UI), and our launch
// flow was bypassing that gate from memory: any installed circuit loaded, owned or not. dlc_products.psv
// gains kind=steam rows: key = the DLC's Steam appid (validated against the store on 24/08), product_id = the
// product it grants. SEVERAL rows per product (the Premium Track Pack is also granted by owning each circuit
// sold standalone). The overlay -- inside the game's own process, with its Steam API already initialized -- queries
// appid by appid and reports back here; this module stays pure. Per-product semantics:
//   Unknown  = none of the product's appids answered (no curated rows / Steam API unavailable) -> fail-open
//   Owned    = ANY of the product's appids owned (buying a standalone circuit grants its share of the pack)
//   NotOwned = there are responses and none are owned -> the product's content is vetoed under AUTO
// The manual override (Settings > Content) still wins in BOTH directions: On lifts the veto (family
// sharing, unusual accounts), Off already blocked on its own side. It vetoes TRACKS and DECLARED cars (kind=car,
// where the product is unambiguous); vetoing by CLASS is deliberately left out -- classes shared across
// packs (MERROWDOWN_MYR373, Hypercars) would mean not owning one pack takes down cars from another pack you do own.
enum class Ownership { Unknown = 0, Owned, NotOwned };
const std::vector<std::pair<unsigned, std::string>>& SteamAppidRows();   // (appid, product_id) pairs for the overlay's sweep
void SetSteamAppOwned(unsigned appid, bool owned);                       // the overlay's report (accumulates)
void ClearSteamOwnership();                                              // tests / re-sweep
void SetOwnershipVetoBypass(bool on);   // cmd.debug.contentLog: NotOwned stops vetoing (the manual OFF still rules)
bool OwnershipVetoBypass();
Ownership ProductOwnership(const std::string& productId);
bool TrackVetoedBySteam(long trackHash);    // the track's pack -> a NotOwned product (and no On override)
bool VehicleVetoedBySteam(int vehHash);     // only DECLARED cars (kind=car); a shared class = fail-open

// effective filter: content taken down by a disabled product ----
// Only an EXPLICIT OFF takes anything down. A product under AUTO that isn't detected doesn't need to take anything down: its content isn't
// in the live catalogs to begin with, so it already falls away on its own (and detection is stricter than the catalog, so
// taking things down under AUTO would take out cars that actually are installed).
bool TrackDisabledByOverride(long trackHash);   // via TrackDlcPack(hash) -> product
bool VehicleDisabledByOverride(int vehHash);    // via ClassForVehicle(vehHash) -> product (class granularity)

// series playability: a class with NO cars installed at all => the whole series is locked ----
bool SeriesPlayable(const SeriesDef& sd);                 // fail-open: empty catalog/unknown class => true
std::string SeriesMissingProduct(const SeriesDef& sd);    // the missing product_id ("" if playable/unknown)

// substitution for a missing track (priority set by the user) ----
// same circuit, a different installed era (the closest one to the series' era) > same circuit, any
// variant > a viable circuit by score (REQUIRED type, grade, |era|, country, continent). Deterministic.
struct TrackSub {
    long hash = 0;                 // the chosen substitute
    std::string name;              // display name (circuits_ref col name)
    long origHash = 0;             // the original track from the curated calendar
    std::string origName;
    std::string productId;         // the missing product ("" if unknown)
    std::string productDisplay;
};
// 19/09 (player report: Wildton Ring six times in the Thornwick Motors Ax797): `exclude` = circuits
// that the season ALREADY runs or that are already a substitute for another round. First it looks for venues that none
// of them uses (another layout of the same venue is also repeating), then at least one different track; if
// with those out none is left, it repeats rather than block the round (a playable round on a
// repeated circuit is better than a lock). nullptr = no season memory (the usual).
bool ResolveTrackSubstitute(long missingHash, const std::string& seriesEra, TrackSub& out,
                            const std::set<long>* exclude = nullptr);

// content availability for a special EVENT ----
// Events don't go through the calendar, so they inherit neither the substitution nor the series lock: an
// event is ITS circuit and ITS car, changing them defeats the point. This only reports what's missing, so it can warn BEFORE
// setting up (the event card), instead of sending the game a track or class it doesn't have.
// Fail-open just like the rest: with no populated live catalogs, everything is available.
struct EventContentState {
    bool trackOk = true;
    std::string trackProduct;                   // display name of the missing product ("" if unknown)
    bool classOk = true;                        // the class the player DRIVES
    std::string classProduct;
    std::vector<std::string> droppedClasses;    // series_id for SECONDARY classes with no cars: off the grid
    bool playable() const { return trackOk && classOk; }
};
// playerSeriesId and eventClasses are series_id values (SpecialEventDef::classes); trackHash 0 = an unresolved track.
EventContentState EventContentFor(long trackHash, const std::string& playerSeriesId,
                                  const std::vector<std::string>& eventClasses);

// per-product status for query.content (Settings > Content) ----
struct DlcProductState {
    std::string id, display;
    std::string kind;      // "tracks" | "cars" | "mixed"
    bool detected = false; // some of the product's content is present in the LIVE catalogs (Raw variants)
    DlcOverride ovr = DlcOverride::Auto;   // what wins: detection (Auto) or the player's choice
    Ownership owned = Ownership::Unknown;  // Steam's response (Unknown = no data, asserts nothing)
    bool enabled = true;   // the EFFECTIVE, already-resolved status: On/Off if there's an override; under AUTO, detection + ownership
};
std::vector<DlcProductState> DlcProductStates();

} // namespace career
