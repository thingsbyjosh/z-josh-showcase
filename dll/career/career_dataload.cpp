// career_dataload.cpp -- shared implementation for loading season data.
// See career_dataload.h for the contract and the rationale for this new translation unit.
//
// Includes: ONLY loader headers + paths.h. Never career_state.h, nor the overlay's theming header,
// nor the season UI header — the probe (tools/seatwatch_probe.cpp) doesn't link ImGui, and the file that
// at first glance looked like the obvious home for this function drags that dependency chain in. This translation unit exists precisely so that
// the shared list does NOT have to live there.
#include "career_dataload.h"
#include "career_growth.h"   // 14/08: named career archetypes

#include <cstdio>
#include <ctime>

#include "career_series.h"       // LoadSeries/LoadSeriesAccess/LoadSeriesPoints/LoadSeriesCalendar/LoadProfiles
                                  // + LoadInstalledContent/InstalledTrackCount/InstalledVehicleCount (they live here,
                                  // despite the name, not in career_content.h)
#include "career_fields.h"       // LoadFields/LoadRoster/LoadSeriesRosters/LoadDriverLiveries/LoadAuthoredProfiles
#include "career_pool_origin.h"   // the pool's category of origin (08/09)
#include "career_dda_offsets.h"  // career::dda::LoadClassDifficulty/ClassDifficultyRowCount (a namespace exception)
#include "career_classpace.h"    // LoadClassPace/ClassPaceCount
#include "career_econ.h"         // LoadSeriesEcon/SeriesEconCount + LoadVehiclePrices/VehiclePriceCount + LoadCircuitGeo/CircuitGeoCount
#include "career_data_json.h"    // LoadCareerDataFromJson (the useJson branch)
#include "career_content.h"      // LoadDlcProducts/DlcProductCount + LoadDlcOverrides/DisabledProducts/ForcedOnProducts
#include "career_seats.h"        // LoadLiveryTeamEquiv .. LoadTeamAffiliations (curated teams/liveries)
#include "career_registry.h"     // 23/09: the identity registry (the RESERVE of the ) when the load closes
#include "career_customai.h"     // LoadClassHash/ClassHashCount + LoadLiveries/LiveryClassCount
#include "career_calendar.h"     // LoadAudienceData
#include "career_attributes.h"   // LoadDriverAttributes/DriverAttributeCount (calibration set)
#include "paths.h"                // paths::Data/paths::UserData

// career_api_queries.h is deliberately NOT included here: of that entire header, this unit uses only ONE
// function, capi::LoadMemoryLore, and including the whole header drags in career_profile.h (career_api_queries.h:28),
// which includes imgui.h (career_profile.h:6). A forward declaration provides the function without the header.
//
// Honest caveat about what this DOES and does NOT achieve: imgui.h still reaches here
// via career_econ.h and career_calendar.h -> career_engine.h, and via career_seats.h -> career_save.h, all three
// via career_market_data.h:6, which includes it for a SINGLE typedef (ImU32). That's a condition of the whole engine,
// not something introduced here, and it can't be cut without touching career_save.h/career_engine.h. What
// actually matters and does hold: this translation unit depends on no ImGui SYMBOL, nor on Col::Disc
// (theme.cpp), so it LINKS in the probe, which compiles no ImGui .cpp at all. That was the real problem with
// career_data.cpp — a linker error, not an include one.
namespace capi { int LoadMemoryLore(const char* careerDir); }   // career_api_queries.h:125

namespace career {

namespace {

// Adds an entry to the report. Centralizes the struct's shape so the 33 call sites below don't repeat
// field-by-field construction (and so a future new struct field only needs adding in ONE place).
void Push(SeasonDataReport& report, std::string name, std::string rel, std::string root,
          bool opened, bool hasCount, int rows, std::string reason = std::string()) {
    SeasonDataset d;
    d.name = std::move(name);
    d.rel = std::move(rel);
    d.root = std::move(root);
    d.opened = opened;
    d.has_count = hasCount;
    d.rows = rows;
    d.reason = std::move(reason);
    report.datasets.push_back(std::move(d));
}

} // namespace (anonymous)

void LoadSeasonData(const SeasonDataOpts& opts, SeasonDataReport& report) {
    report.datasets.clear();
    report.special_events.clear();

    if (opts.useJson) {
        // The JSON branch replaces the first 12 PSV entries with ONE (career_json) — it fills the
        // SAME engine globals (Series/Points/Calendar/Profiles/Fields/Roster/Pinned/Econ) as the
        // PSV branch. rows uses the SAME accessor as the PSV branch's 'series' entry (SeriesCount()): it's the
        // most direct measure of "how many series the load brought in", and the adapter itself leaves it populated.
        bool okJson = LoadCareerDataFromJson(paths::Data("career").c_str());
        Push(report, "career_json", "career", "data", okJson, /*hasCount=*/true, okJson ? SeriesCount() : 0);
    } else {
        // (1) series
        {
            bool ok = LoadSeries(paths::Data("series.psv").c_str());
            Push(report, "series", "series.psv", "data", ok, true, ok ? SeriesCount() : 0);
        }
        // (2) series_access — access: MUST come after LoadSeries (career_series.h:57-60): it operates on
        // an already-populated g_series. The order of this list is a CONTRACT, not a style choice.
        {
            bool ok = LoadSeriesAccess(paths::Data("series_access.psv").c_str());
            Push(report, "series_access", "series_access.psv", "data", ok, true, ok ? SeriesAccessRowsApplied() : 0);
        }
        // from the portal (19/09) — the CAR RULE (single-vehicle), behind access and for the same reason: it mutates
        // g_series already populated. Without the file every series runs its whole class: optional, does not block startup.
        {
            bool ok = LoadSeriesCars(paths::Data("series_cars.psv").c_str());
            Push(report, "series_cars", "series_cars.psv", "data", ok, false, ok ? SeriesCarsRowsApplied() : 0);
        }
        // 13/08 — the MARKET TAG, right behind the access one and for the same reason: both mutate
        // g_series already populated. Without the file, each series falls back to its year against MODERN_ERA_CUTOFF, which is the
        // behavior as always — that is why it is NOT mandatory and its absence does not block startup.
        {
            bool ok = LoadSeriesMarket(paths::Data("series_market.psv").c_str());
            Push(report, "series_market", "series_market.psv", "data", ok, false, ok ? SeriesMarketRowsApplied() : 0);
        }
        // 14/08 — the per-series ECONOMIC SCALE. Behind the market tag: both mutate g_series.
        {
            bool ok = LoadSeriesScale(paths::Data("career\\series_scale.psv").c_str());
            Push(report, "series_scale", "career/series_scale.psv", "data", ok, false,
                 ok ? SeriesScaleRowsApplied() : 0);
        }
        // 14/08 — the named CAREER ARCHETYPES (). Without the file, the rookie generator falls back
        // to its bounded formula as always, so it is optional and does not block startup.
        {
            bool ok = LoadGrowthArchetypes(paths::Data("career\\growth_archetypes.psv").c_str());
            Push(report, "growth_archetypes", "career/growth_archetypes.psv", "data", ok, false,
                 ok ? GrowthArchetypeCount() : 0);
        }
        // (3) series_points
        {
            bool ok = LoadSeriesPoints(paths::Data("series_points.psv").c_str());
            Push(report, "series_points", "series_points.psv", "data", ok, true, ok ? PointsSystemCount() : 0);
        }
        // (4) series_calendar
        {
            bool ok = LoadSeriesCalendar(paths::Data("series_calendar.psv").c_str());
            Push(report, "series_calendar", "series_calendar.psv", "data", ok, true,
                 ok ? (int)AllCalendarRounds().size() : 0);
        }
        // (5) series_config_profiles
        {
            bool ok = LoadProfiles(paths::Data("series_config_profiles.psv").c_str());
            Push(report, "series_config_profiles", "series_config_profiles.psv", "data", ok, true,
                 ok ? ConfigProfileCount() : 0);
        }
        // (6) series_fields
        {
            bool ok = LoadFields(paths::Data("series_fields.psv").c_str());
            Push(report, "series_fields", "series_fields.psv", "data", ok, true, ok ? FieldRowCount() : 0);
        }
        // (7) series_roster
        {
            bool ok = LoadRoster(paths::Data("series_roster.psv").c_str());
            Push(report, "series_roster", "series_roster.psv", "data", ok, true, ok ? RosterRowCount() : 0);
        }
        // (7b) class_age_profile — the CHARACTER of each class (age ceiling). Without it, retirement
        // falls back to a flat threshold of 40 for everyone and empties out endurance categories all at once.
        {
            bool ok = LoadClassAgeProfile(paths::Data("career/class_age_profile.psv").c_str());
            Push(report, "class_age_profile", "career/class_age_profile.psv", "data", ok, true,
                 ok ? ClassAgeProfileCount() : 0);
        }
        // (8) series_rosters — driver->livery binding (takes priority over fields)
        {
            bool ok = LoadSeriesRosters(paths::Data("series_rosters.psv").c_str());
            Push(report, "series_rosters", "series_rosters.psv", "data", ok, true, ok ? PinnedRosterCount() : 0);
        }
        // (9) driver_liveries — real driver->livery pairing (field classes)
        {
            bool ok = LoadDriverLiveries(paths::Data("driver_liveries.psv").c_str());
            Push(report, "driver_liveries", "driver_liveries.psv", "data", ok, true, ok ? DriverLiveryCount() : 0);
        }
        // (9b) class_variants — aero-kit variants (Falcon_Championship_*_LD/HD, F-USA_*_SO/SS/SW...):
        // the writer leaves a copy of the XML per variant, or the live class on those tracks is entirely generic.
        {
            bool ok = LoadClassVariants(paths::Data("ams2_class_variants.psv").c_str());
            Push(report, "class_variants", "ams2_class_variants.psv", "data", ok, true, ok ? ClassVariantCount() : 0,
                 ok ? "" : "sin variantes de aero-kit: Northhurst Park (Falcon Championship LD) y los ovalos F-USA correran con IA generica");
        }
        // class_difficulty — COMMUNITY difficulty offsets. Currently absent from the data root —
        // that's the normal state until there's enough aggregated telemetry volume (a rule from the
        // dda header itself). The annotation EXPLAINS the zero, it doesn't exempt it from the warning.
        {
            bool ok = dda::LoadClassDifficulty(paths::Data("class_difficulty.psv").c_str());
            Push(report, "class_difficulty", "class_difficulty.psv", "data", ok, true,
                 ok ? dda::ClassDifficultyRowCount() : 0,
                 "ausente del data root hoy: todos los offsets a cero es el estado normal sin telemetria agregada");
        }
        // class_pace — per-class qualifying spreads; absent = club-tier defaults (never a crash)
        {
            bool ok = LoadClassPace(paths::Data("career/class_pace_curve.psv").c_str());
            Push(report, "class_pace", "career/class_pace_curve.psv", "data", ok, true, ok ? ClassPaceCount() : 0);
        }
        // series_econ — economy per category (calendar prize/entry)
        {
            bool ok = LoadSeriesEcon(paths::Data("ams2_series_econ.psv").c_str());
            Push(report, "series_econ", "ams2_series_econ.psv", "data", ok, true, ok ? SeriesEconCount() : 0);
        }
    }

    // The 21 common ones: loaded the same way by BOTH paths (deliberately outside the if/else). ----

    // authored_profiles — real-field personality; absent = every real driver enters neutral
    {
        bool ok = LoadAuthoredProfiles(paths::Data("career/f3_authored_profiles.psv").c_str());
        Push(report, "authored_profiles", "career/f3_authored_profiles.psv", "data", ok, true,
             ok ? AuthoredProfileCount() : 0);
    }
    // #13b pack_drivers -- the REAL drivers of the livery packs we already have, for
    // the slots that neither the pinned roster nor the field cover. Absent = those slots go back to made-up
    // people (`fl_*`), which is the world as before: optional on purpose, breaks nothing by being missing.
    {
        bool ok = LoadPackDrivers(paths::Data("career/pack_drivers.psv").c_str());
        Push(report, "pack_drivers", "career/pack_drivers.psv", "data", ok, false, ok ? PackDriverCount() : 0);
    }
    // #13c reserve_people -- 23/09: the RESERVE curated person by person (real birth year, active or
    // retired in 2025, last season, with source). The registry () applies it and with it the cascade decides
    // whom it may seat. Absent = nobody in the reserve is curated and the cascade behaves as before.
    {
        bool ok = LoadReservePeople(paths::Data("career/reserve_people.psv").c_str());
        Push(report, "reserve_people", "career/reserve_people.psv", "data", ok, true, ok ? ReservePeopleCount() : 0);
    }
    // #13d pool_origin -- 08/09 (Josh's decision via C2, spec E7): the curated CATEGORY OF ORIGIN of the
    // pool people, sibling file of driver_profiles (same key). Read by the pool settling and by
    // affinity (career_seats). Absent = the world as before (D4): optional on purpose.
    {
        bool ok = LoadPoolOrigin(paths::Data("career/pool_origin.psv").c_str());
        Push(report, "pool_origin", "career/pool_origin.psv", "data", ok, false, ok ? PoolOriginCount() : 0);
    }
    // #13c driver_attributes -- THE CALIBRATION SET. The 346 pack cards whose fifteen
    // controls were MEASURED against real results (the other 1,981 come from a positional curve and are not valid
    // as absolute capability, ). It is not the population “covers 10-28%” — it is the yardstick against which the
    // averages are calibrated. Absent = the world as before, with no measured attributes: optional on purpose.
    {
        bool ok = LoadDriverAttributes(paths::Data("career/driver_attributes.psv").c_str());
        Push(report, "driver_attributes", "career/driver_attributes.psv", "data", ok, false,
             ok ? DriverAttributeCount() : 0);
    }
    // vehicle_prices — per-car price (override); absent = base+jitter
    {
        bool ok = LoadVehiclePrices(paths::Data("ams2_vehicle_prices.psv").c_str());
        Push(report, "vehicle_prices", "ams2_vehicle_prices.psv", "data", ok, true, ok ? VehiclePriceCount() : 0);
    }
    // special_events — loads the registry (the overlay publishes it with its own set-registry, this
    // function does NOT do that: that's the caller's policy). Deliberately WITHOUT a measurable count: it depends on
    // time(nullptr), and would inject a non-deterministic value into an output that has to be byte-for-byte identical
    // across runs (a fingerprint/baseline). `opened` isn't separately measurable for this function either
    // (it returns a vector, not a bool): it's left as true (the loader ran).
    {
        report.special_events =
            LoadSpecialEvents(paths::Data("special_events.json").c_str(), (long long)time(nullptr));
        Push(report, "special_events", "special_events.json", "data", /*opened=*/true, /*hasCount=*/false, 0,
             "recuento dependiente del reloj (time(nullptr)): nunca entra como numero medible");
    }
    // circuit_geo — hash -> country/continent (A->B travel cost)
    {
        bool ok = LoadCircuitGeo(paths::Data("circuits_ref.psv").c_str());
        Push(report, "circuit_geo", "circuits_ref.psv", "data", ok, true, ok ? CircuitGeoCount() : 0);
    }
    // dlc_products — catalog of purchasable DLC products
    {
        bool ok = LoadDlcProducts(paths::Data("dlc_products.psv").c_str());
        Push(report, "dlc_products", "dlc_products.psv", "data", ok, true, ok ? DlcProductCount() : 0);
    }
    // vehclass -- vehHash catalog -> class/name. It's the OTHER half of the DLC veto's data: the guard for
    // events (EventContentFor -> SeriesPlayable -> ClassHasInstalledVehicle) is fail-open with the class
    // outside this catalog, and the PSV used to load lazily only from UI paths (grid, tree, market).
    // A cold boot straight into a special event = "unknown" class = the guard waves it through and the arm
    // went in with a class whose entire content is unowned DLC packs (bug 0.6.5, 0/32 count). Loading it
    // here populates it BEFORE any guard consults it; the lazy guard-loads above become idempotent no-ops.
    {
        bool ok = LoadVehClass(paths::Data("ams2_vehclass.psv").c_str());
        Push(report, "vehclass", "ams2_vehclass.psv", "data", ok, true, ok ? VehClassCount() : 0);
    }
    // dlc_overrides — user overrides (Settings>Content); ABSENT on a clean install: the
    // annotation explains the zero, it doesn't exempt it from the warning.
    {
        bool ok = LoadDlcOverrides(paths::UserData("cache/dlc_overrides.psv").c_str());
        int rows = ok ? (int)(DisabledProducts().size() + ForcedOnProducts().size()) : 0;
        Push(report, "dlc_overrides", "cache/dlc_overrides.psv", "userdata", ok, true, rows,
             "cache de UserData: no existe hasta que el usuario toca Ajustes>Contenido (instalacion limpia)");
    }
    // content_installed — content catalog detected in previous sessions; same reasoning as
    {
        bool ok = LoadInstalledContent(paths::UserData("cache/content_installed.psv").c_str());
        int rows = ok ? (InstalledTrackCount() + InstalledVehicleCount()) : 0;
        Push(report, "content_installed", "cache/content_installed.psv", "userdata", ok, true, rows,
             "cache de UserData: nace vacia hasta que el escaneo vivo de una sesion anterior la escribio");
    }
    // livery_team_equiv — livery -> real_team (naming for the seat market)
    {
        bool ok = LoadLiveryTeamEquiv(paths::Data("livery_team_equiv.psv").c_str());
        Push(report, "livery_team_equiv", "livery_team_equiv.psv", "data", ok, true, ok ? LiveryTeamEquivCount() : 0);
    }
    // livery_resolved — LAYER 0 of naming: the already-merged verdict (human + packs + curated +
    // inferred + generated). It takes priority over the older cascade. OPTIONAL: without it, the historical cascade still works.
    {
        // 16/09: the installer leaves it at the data root (tauri.conf.json: docs/skins/livery_resolved.psv ->
        // ./livery_resolved.psv), and that is where production looks for it. With a data root that is the tree's docs\ — the
        // suite (canon_load_prod), the game double, dev without config.json — the file is in skins\ and the layer
        // did not load: team names fell back to the old cascade without warning. Production path first.
        std::string rel = "livery_resolved.psv";
        if (!paths::FileExists(paths::Data(rel)) && paths::FileExists(paths::Data("skins/livery_resolved.psv")))
            rel = "skins/livery_resolved.psv";
        bool ok = LoadLiveryResolved(paths::Data(rel).c_str());
        Push(report, "livery_resolved", rel, "data", ok, true, ok ? LiveryResolvedCount() : 0,
             ok ? "" : "sin capa resuelta: el naming de equipos cae a livery_team_equiv y sus respaldos");
    }
    // livery_vehicle_map — livery -> exact car+NN (curated real-team seat)
    {
        bool ok = LoadLiveryVehicleMap(paths::Data("livery_vehicle_map.psv").c_str());
        Push(report, "livery_vehicle_map", "livery_vehicle_map.psv", "data", ok, true, ok ? LiveryVehicleMapCount() : 0);
    }
    // livery_catalog — class -> catalog car/manufacturer labels (fallback). Reads the SAME
    // file as (liveries) with a DIFFERENT loader filling a DIFFERENT table: it's not a duplicate to
    // delete, it's a dataset with two consumers (the rule is: no repeated loader+file pair, never "no
    // repeated file").
    {
        const char* rel = "ams2_liveries.psv";
        bool ok = LoadLiveryCatalog(paths::Data(rel).c_str());
        Push(report, "livery_catalog", rel, "data", ok, true, ok ? LiveryCatalogClassCount() : 0);
    }
    // livery_team_packs — class -> REAL teams from the lantern_series_8801 packs
    {
        bool ok = LoadLiveryTeamPacks(paths::Data("livery_team_equiv_packs.psv").c_str());
        Push(report, "livery_team_packs", "livery_team_equiv_packs.psv", "data", ok, true,
             ok ? LiveryTeamPacksClassCount() : 0);
    }
    // pack_team_class — curated classification (team/driver/car) of pack labels
    {
        bool ok = LoadPackTeamClass(paths::Data("pack_team_class.psv").c_str());
        Push(report, "pack_team_class", "pack_team_class.psv", "data", ok, true, ok ? PackTeamClassCount() : 0);
    }
    // team_relationships — team<->team relationship graph
    {
        bool ok = LoadTeamRelationships(paths::Data("team_relationships.psv").c_str());
        Push(report, "team_relationships", "team_relationships.psv", "data", ok, true, ok ? TeamRelationsCount() : 0);
    }
    // generation_classes — base/entry/spec/kart classes that KEEP cohort labels
    {
        bool ok = LoadGenerationClasses(paths::Data("generation_classes.psv").c_str());
        Push(report, "generation_classes", "generation_classes.psv", "data", ok, true, ok ? GenerationClassCount() : 0);
    }
    // team_names_pool — FICTIONAL names for classes with no curated team
    {
        bool ok = LoadTeamNamesPool(paths::Data("team_names_pool.psv").c_str());
        Push(report, "team_names_pool", "team_names_pool.psv", "data", ok, true, ok ? TeamNamesPoolCount() : 0);
    }
    // feeder_team_map — F1 feeder categories: curated livery->team map (feeder classes)
    {
        bool ok = LoadFeederTeamMap(paths::Data("feeder_team_map.psv").c_str());
        Push(report, "feeder_team_map", "feeder_team_map.psv", "data", ok, true, ok ? FeederTeamMapCount() : 0);
    }
    // team_affiliations — F2 feeder categories: curated affiliations (override, after the feeder map)
    {
        bool ok = LoadTeamAffiliations(paths::Data("team_affiliations.psv").c_str());
        Push(report, "team_affiliations", "team_affiliations.psv", "data", ok, true, ok ? TeamAffiliationCount() : 0);
    }
    // class_hash — class_name -> hash (calendar multiclass; kept in sync with the catalog, both paths)
    {
        bool ok = LoadClassHash(paths::Data("ams2_class_hash.psv").c_str());
        Push(report, "class_hash", "ams2_class_hash.psv", "data", ok, true, ok ? ClassHashCount() : 0);
    }
    // audience — curated per-round audience (card bar + ELO/rep scale). Receives the
    // 'career' DIRECTORY; the report names the actual file it resolves inside it. The function returns the
    // count directly (there's no separate open bool: `opened` isn't measurable here).
    {
        int n = LoadAudienceData(paths::Data("career").c_str());
        Push(report, "audience", "career/audience.json", "data", /*opened=*/true, /*hasCount=*/true, n);
    }
    // memory_lore — historical lore for period series (Memories). Same pattern as the others.
    {
        int n = capi::LoadMemoryLore(paths::Data("career").c_str());
        Push(report, "memory_lore", "career/memory_lore.json", "data", /*opened=*/true, /*hasCount=*/true, n);
    }
    // liveries — livery catalog for the grid XML. Reads the SAME file as
    // (livery_catalog) with a DIFFERENT loader filling a DIFFERENT table: two consumers, not a
    // duplicate (same note as above).
    {
        const char* rel = "ams2_liveries.psv";
        bool ok = LoadLiveries(paths::Data(rel).c_str());
        Push(report, "liveries", rel, "data", ok, true, ok ? LiveryClassCount() : 0);
    }
    // registry -- 23/09. NOBODY LOADED THE IDENTITY REGISTRY IN THE GAME. Its only caller
    // was `canon::BuildWorld`, which in turn has none in dll/; the measurement harnesses loaded it by hand
    // (`career_test --world`, `career_sim`, ashmere_motors_ladder, ashmere_motors_pool). So the RESERVE — “people with a
    // card who do not run on any grid” — existed in every measurement and in NO game:
    // `ReservaLibrePara` (the seat cascade, `g_reservaEnParrilla = true`) walked an empty registry,
    // `RosterDelPozo` lost the archetype and `DriverFamily` let things through without knowing the family. Same fix as
    // f0 (47b9af7c, LoadSeasonData ). It goes LAST because the registry is built from the above (the
    // pool, the packs, the rosters and the fields): it is an index over that data, not one more datum.
    // The same rule as the rest of the report, “rows = opened ? accessor() : 0”. The registry opens no file
    //  of its own, so what counts as “opened” is that the `drivers.json` of THIS root exists. Without that, with an
    //  empty root the report would state thousands of identities taken from the pool that the previous load left cached.
    {
        const bool abre = paths::FileExists(paths::Data("career/drivers.json"));
        const int n = registry::Load();
        Push(report, "registry", "career/drivers.json", "data", abre, /*hasCount=*/true, abre ? n : 0);
    }
    // Port 0.7.0 (F22) — THE DATA UNDERNEATH IS DIFFERENT: the epoch is closed. The memos of career_seats
    //  (StayingDrivers and the data part of SeatsForClass) carry the epoch in their key, and here
    //  liveries, rosters, fields and team labels have just changed. Without this they would answer with the previous world, which
    //  in the suite is five team-name tests (measured in the port lab) and in the game an
    //  old grid after any hot reload. See career_calendar.h.
    BumpDataEpoch();
}

bool SeasonDataIsClean(const SeasonDataReport& report) {
    for (const auto& d : report.datasets) {
        // Any entry with no measurable number OR zero rows dirties the ENTIRE report — no dataset
        // gets a pass: the reason annotation EXPLAINS the zero, it doesn't excuse it.
        if (!d.has_count || d.rows <= 0) return false;
    }
    return true;
}

std::string SeasonDataWarnings(const SeasonDataReport& report) {
    std::string out;
    for (const auto& d : report.datasets) {
        bool affected = !d.has_count || d.rows <= 0;
        if (!affected) continue;
        out += d.name;
        out += " (";
        out += d.rel;
        out += ")";
        if (!d.reason.empty()) {
            out += " -- ";
            out += d.reason;
        }
        out += "\n";
    }
    return out;
}

// fingerprint of the data root. PURE — no disk, no clock, no globals, just
// formats `report`. See the full contract in career_dataload.h.
std::string FormatDataRootFingerprint(const SeasonDataReport& report) {
    // Which branch ran is INFERRED from the report itself (never from a new parameter or a global):
    // the JSON branch replaces the first 12 PSV entries with a single "career_json" (LoadSeasonData,
    // earlier in this same file); the PSV branch leaves "series" as the first entry. Two runs
    // "over the same entries" with the flag flipped would produce different output without this line.
    const bool isJson = !report.datasets.empty() && report.datasets.front().name == "career_json";

    std::string out = "=== HUELLA DEL DATA ROOT ===\n";
    {
        char header[64];
        snprintf(header, sizeof(header), "rama=%s datasets=%d\n", isJson ? "JSON" : "PSV",
                 (int)report.datasets.size());
        out += header;
    }

    // One line per dataset, IN LOAD ORDER (the contract in tools/fixtures/season_dataset_order.txt),
    // with fixed-width columns so a line-by-line diff reads without noise. A labeled root (never the
    // absolute root the PathProvider resolves); a relative path with forward slashes (the report already carries it
    // that way).
    for (const auto& d : report.datasets) {
        // The zero's reason (when there is one) as a separate suffix: avoids passing a temporary std::string directly
        // into a variadic snprintf argument.
        const std::string reasonSuffix = d.reason.empty() ? std::string() : (" -- " + d.reason);
        char line[512];
        if (d.has_count) {
            // A genuine zero rows (rows==0, has_count==true) prints with the real NUMBER — it's never confused
            // with the branch below, which is the only one that prints the NO-NUMBER mark.
            snprintf(line, sizeof(line), "  %-24s %-9s %-34s filas=%-6d%s\n",
                     d.name.c_str(), d.root.c_str(), d.rel.c_str(), d.rows, reasonSuffix.c_str());
        } else {
            // has_count==false (today, only special_events): no measurable number — NEVER a zero that could be
            // confused with a genuinely empty dataset.
            snprintf(line, sizeof(line), "  %-24s %-9s %-34s filas=%-6s%s\n",
                     d.name.c_str(), d.root.c_str(), d.rel.c_str(), "SIN-NUMERO", reasonSuffix.c_str());
        }
        out += line;
    }

    // Final status line: the same criterion as SeasonDataIsClean, so the mark can never
    // drift out of sync with the lines that justify it.
    out += SeasonDataIsClean(report) ? "estado = LIMPIA\n" : "estado = NO LIMPIA\n";
    return out;
}

} // namespace career
