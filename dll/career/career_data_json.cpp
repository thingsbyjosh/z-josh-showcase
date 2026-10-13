#include "career_data_json.h"
#include "json_min.h"
#include "career_series.h"
#include "career_fields.h"
#include "career_econ.h"
#include "career_scoring.h"   // PointsSystem
#include "paths.h"            // PathProvider: drivers.json in data/
#include "career_registry.h"  // 23/09: the curated age of the reserve
#include "career_text.h"      // 23/09: CleanNameSpaces — the names from the JSON, without hard spaces

#include <map>
#include <vector>
#include <string>
#include <cstdio>            // fprintf (a defensive warning if an id doesn't resolve)
#include <cmath>             // floor (gridSize must be an integer)

namespace career {

// Production flag: defaults to OFF (behavior unchanged).
bool g_useJsonCareerData = false;

namespace {

using jsonm::Value;
using jsonm::ValuePtr;

// "sprint|feature|endurance" -> 0/1/2 (CalRound.fmt mapping). Empty/unknown -> 0, IDENTICAL to the
// PSV loader (atoi("")==0=sprint); the JSON always carries a valid value, this is just the mirror of that behavior.
int FormatStrToInt(const std::string& s) {
    if (s == "feature")   return 1;
    if (s == "endurance") return 2;
    return 0;   // "sprint", empty, and everything else (= atoi from the PSV)
}

// branch_of: a JSON null or missing field -> "" (same as the PSV when the column is empty).
std::string OptString(const Value* obj, const std::string& key) {
    if (!obj) return std::string();
    const Value* v = obj->member(key);
    return (v && v->isString()) ? v->str : std::string();
}

} // anon namespace

bool LoadCareerDataFromJson(const char* careerDir) {
    ResetGridSizeCache();   // classes.json supplies the grid_size values
    if (!careerDir) return false;
    std::string base = careerDir;
    if (!base.empty() && (base.back() == '\\' || base.back() == '/')) base.pop_back();
    auto pathOf = [&](const char* leaf) { return base + "\\" + leaf; };

    std::string err;

    // drivers.json -> g_roster (kind==roster only). The rd_* identities are CANONICAL
    // (one id per person, canonical country) BUT it's no longer injected into field/roster: each entry carries its own
    // LITERAL name/country. The pool only feeds the recurring rk_* cast. ----------
    ValuePtr drivers = jsonm::ParseFile(pathOf("drivers.json").c_str(), &err);
    if (!drivers || !drivers->isObject()) return false;

    std::vector<RosterDriver> roster;
    for (const auto& kv : drivers->obj) {
        const std::string& id = kv.first;
        const Value* d = kv.second.get();
        if (!d || !d->isObject()) continue;

        // RosterDriver is ONLY for kind=="roster" (recurring rk_* cast); the rd_* entries are real identities.
        if (d->memberString("kind") == "roster") {
            RosterDriver r;
            r.id          = id;
            r.name        = CleanNameSpaces(d->memberString("name"));
            r.homage      = CleanNameSpaces(d->memberString("homage"));
            r.country     = d->memberString("country");
            r.archetype   = d->memberString("archetype");
            r.tier_min    = d->memberInt("tier_min", 1);
            r.tier_max    = d->memberInt("tier_max", 7);
            r.hot         = d->memberBool("hot", false);
            r.modern_only = d->memberBool("modern_only", false);   // excluded from period series
            const Value* sb = d->member("stats_base");
            if (sb && sb->isObject()) {
                r.base_skill  = sb->memberNumber("skill", 0.85);
                r.aggression  = sb->memberNumber("aggression", 0.60);
                r.consistency = sb->memberNumber("consistency", 0.85);
            }
            roster.push_back(r);
        }
    }

    // points.json -> g_points (PointsSystem) ----------
    ValuePtr points = jsonm::ParseFile(pathOf("points.json").c_str(), &err);
    if (!points || !points->isObject()) return false;
    std::map<std::string, PointsSystem> pointsMap;
    for (const auto& kv : points->obj) {
        const Value* p = kv.second.get();
        if (!p || !p->isObject()) continue;
        PointsSystem ps;
        const Value* table = p->member("table");
        if (table && table->isArray())
            for (const auto& el : table->arr) ps.points.push_back(el ? el->asInt(0) : 0);
        ps.fl_point   = p->memberInt("fl_point", 0);
        ps.fl_top_n   = p->memberInt("fl_top_n", 0);
        ps.pole_point = p->memberInt("pole_point", 0);
        pointsMap[kv.first] = ps;
    }

    // profiles.json -> g_profiles (ConfigProfile.fields, all string->string) ----------
    ValuePtr profiles = jsonm::ParseFile(pathOf("profiles.json").c_str(), &err);
    if (!profiles || !profiles->isObject()) return false;
    std::map<std::string, ConfigProfile> profMap;
    for (const auto& kv : profiles->obj) {
        const Value* pr = kv.second.get();
        if (!pr || !pr->isObject()) continue;
        ConfigProfile cp;
        cp.id = kv.first;
        for (const auto& fk : pr->obj) {
            const Value* fv = fk.second.get();
            if (!fv) continue;
            // values are strings in the JSON (the same code path as the PSV's "k=v"); for robustness,
            // if a number/bool shows up instead, it's serialized to its literal form.
            if (fv->isString())      cp.fields[fk.first] = fv->str;
            else if (fv->isNumber()) { char b[32]; snprintf(b, sizeof(b), "%g", fv->num); cp.fields[fk.first] = b; }
            else if (fv->isBool())   cp.fields[fk.first] = fv->b ? "1" : "0";
            // null -> the key is omitted (doesn't exist in the profile), same as a missing k=v.
        }
        profMap[cp.id] = cp;
    }

    // econ.json -> g_econ + g_classToNode (SeriesEcon) ----------
    ValuePtr econ = jsonm::ParseFile(pathOf("econ.json").c_str(), &err);
    if (!econ || !econ->isObject()) return false;
    std::map<std::string, SeriesEcon> econMap;
    std::map<std::string, std::string> classToNode;
    for (const auto& kv : econ->obj) {
        const Value* e = kv.second.get();
        if (!e || !e->isObject()) continue;
        SeriesEcon se;
        se.node_id  = kv.first;
        se.phase    = e->memberInt("phase", 1);
        se.disc     = e->memberString("disc");
        se.comprar  = e->memberLong("comprar", 0);
        se.alquiler = e->memberLong("alquiler", 0);
        se.entry    = e->memberLong("entry", 0);
        se.premio   = e->memberLong("premio", 0);
        se.champion = e->memberLong("champion", 0);   // per-class champion bonus (0 = falls back to the tier default)
        se.model    = e->memberString("model");
        se.icon     = e->memberString("icon");
        const Value* cls = e->member("classes");
        if (cls && cls->isArray())
            for (const auto& c : cls->arr) if (c && c->isString() && !c->str.empty()) se.classes.push_back(c->str);
        for (const auto& c : se.classes)
            if (!c.empty() && !classToNode.count(c)) classToNode[c] = se.node_id;   // first-wins (= PSV)
        econMap[se.node_id] = se;
    }

    // seasons/*.json -> g_series, g_calendar, g_fields, g_pinned ----------
    std::vector<SeriesDef>   series;
    std::vector<CalRound>    calendar;
    std::vector<FieldDriver> fields;
    std::vector<PinnedDriver> pinned;

    // Mojibake fix: ListDir (UTF-8 -> wide)
    std::vector<paths::FindEntry> seasonEntries = paths::ListDir(base + "\\seasons\\*.json");
    if (seasonEntries.empty()) return false;
    bool anySeason = false;
    for (const auto& fe : seasonEntries) {
        if (fe.isDir) continue;
        std::string file = base + "\\seasons\\" + fe.name;
        ValuePtr s = jsonm::ParseFile(file.c_str(), &err);
        if (!s || !s->isObject()) return false;
        anySeason = true;

        const std::string sid = s->memberString("series_id");

        // SeriesDef -----
        SeriesDef sd;
        sd.id         = sid;
        sd.class_name = s->memberString("class");
        sd.display    = s->memberString("display");
        sd.region     = s->memberString("region");
        sd.era        = s->memberString("era");
        sd.profile_id = s->memberString("profile");
        sd.points_id  = s->memberString("points");
        sd.branch_of  = OptString(s.get(), "branch_of");   // null/missing -> ""
        sd.rounds     = s->memberInt("rounds", 0);
        sd.level      = s->memberInt("tier", 1);
        { std::string k = s->memberString("kind"); sd.kind = k.empty() ? std::string("champ") : k; }
        // gate.* (optional object; missing -> defaults: unlocks "", top 0, feeder_class "", feeder_top 0)
        const Value* gate = s->member("gate");
        if (gate && gate->isObject()) {
            sd.unlocks          = OptString(gate, "unlocks");       // null -> ""
            sd.gate_top         = gate->memberInt("top", 0);
            sd.gate_feeder_class = OptString(gate, "feeder_class");
            sd.gate_feeder_top  = gate->memberInt("feeder_top", 0);
        }
        series.push_back(sd);

        // CalRound (one per calendar[]; guests[] do NOT add rows) -----
        const Value* cal = s->member("calendar");
        if (cal && cal->isArray()) {
            for (const auto& rp : cal->arr) {
                if (!rp || !rp->isObject()) continue;
                CalRound c;
                c.series_id   = sid;
                c.round       = rp->memberInt("round", 0);
                c.doy         = rp->memberInt("doy", 0);
                c.date        = rp->memberString("date");
                c.track_hash  = rp->memberLong("circuit_hash", 0);   // signed int32 verbatim
                c.track_name  = rp->memberString("track_name");
                c.fmt         = FormatStrToInt(rp->memberString("format", ""));   // missing -> "" -> 0 (= PSV)
                c.laps        = rp->memberInt("laps", 0);
                c.durationMin = rp->memberInt("durationMin", 0);
                c.hour        = rp->memberInt("hour", 12);
                // "weather" is a LIST of 0..4 integers (empty = the player decides). An earlier JSON
                // carried an integer (0 = no data, >0 = authored across the 4 slots): it is accepted with its old semantics.
                if (const Value* wx = rp->member("weather")) {
                    if (wx->isArray()) {
                        // range-filter 0..17, same as ParseWeatherList (career_weather.cpp) and the PSV: a
                        // value outside the catalog is discarded instead of slipping into CalRound.weather.
                        for (const auto& e : wx->arr) {
                            if (!e || !e->isNumber() || c.weather.size() >= 4) continue;
                            int v = e->asInt(0);
                            if (v >= 0 && v <= 17) c.weather.push_back(v);
                        }
                    } else if (wx->isNumber() && wx->asInt(0) > 0) {
                        c.weather.push_back(wx->asInt(0));
                    }
                }
                c.note        = rp->memberString("note");
                // missing/empty tag -> DeriveTagFromNote(note), the SAME fallback as the PSV loader (parity)
                { std::string t = rp->memberString("tag"); c.tag = t.empty() ? DeriveTagFromNote(c.note) : t; }
                c.pitKmh      = rp->memberInt("pitKmh", 0);   // Per-round pit-box override (parity with the PSV's base+10 column)
                c.practiceMin = rp->memberInt("practiceMin", 0);
                c.qualifyMin  = rp->memberInt("qualifyMin", 0);
                // Optional and nullable. memberInt would truncate 18.5 to 18: the original number is validated so that
                // JSON and PSV reject the same fractional entry and fall back to inheritance.
                { const Value* gs = rp->member("gridSize");
                  if (gs && gs->isNumber() && gs->num >= 2.0 && gs->num <= 60.0 &&
                      std::floor(gs->num) == gs->num)
                      c.gridSize = NormalizeRoundGridSize((int)gs->num); }
                calendar.push_back(c);
            }
        }

        // FieldDriver (one per field[]; name/country = the entry's LITERAL values; sentinel) -----
        const Value* fld = s->member("field");
        if (fld && fld->isArray()) {
            for (const auto& fp : fld->arr) {
                if (!fp || !fp->isObject()) continue;
                FieldDriver d;
                d.series_id = sid;
                d.id        = fp->memberString("driver");   // stable id from the pool (a reference into drivers.json)
                d.order     = fp->memberInt("order", 0);
                d.iconic    = fp->memberBool("iconic", false);
                d.note      = fp->memberString("note");
                // skill/aggr == null (or missing) -> 0.0 (a sentinel meaning "compute from the curve/default").
                const Value* sk = fp->member("skill");
                d.skill = (sk && sk->isNumber()) ? sk->num : 0.0;
                const Value* ag = fp->member("aggr");
                d.aggr  = (ag && ag->isNumber()) ? ag->num : 0.0;
                // name/country are the entry's per-series LITERAL value (verbatim from the PSV),
                // NOT the pool's canonical identity (idMap) -> the grid/XML stay byte-faithful to the PSV.
                // The `driver` id still references the de-fragmented pool (kept for future use).
                const Value* nm = fp->member("name");
                if (nm && nm->isString()) { d.name = CleanNameSpaces(nm->str); d.country = fp->memberString("country"); }
                else fprintf(stderr, "[career_data_json] WARN: field without literal 'name' (series %s, driver '%s')\n",
                             sid.c_str(), fp->memberString("driver").c_str());
                fields.push_back(d);
            }
        }

        // PinnedDriver (one per roster[]; a missing/null numeric field -> defaults; a missing `real` -> true) -----
        const Value* ros = s->member("roster");
        if (ros && ros->isArray()) {
            for (const auto& pp : ros->arr) {
                if (!pp || !pp->isObject()) continue;
                PinnedDriver p;   // struct defaults: skill 0.85, aggr 0.60, cons 0.85, real true
                p.series_id  = sid;
                p.id         = pp->memberString("driver");   // stable id from the pool (a reference into drivers.json)
                p.car_no     = pp->memberString("car_no");
                p.archetype  = pp->memberString("archetype");
                p.livery_name = pp->memberString("livery");   // verbatim, case-sensitive
                p.binding    = pp->memberString("binding");
                { const Value* v = pp->member("skill"); if (v && v->isNumber()) p.skill = v->num; }
                { const Value* v = pp->member("aggr");  if (v && v->isNumber()) p.aggr  = v->num; }
                { const Value* v = pp->member("cons");  if (v && v->isNumber()) p.cons  = v->num; }
                { const Value* v = pp->member("real");  if (v && v->isBool())   p.real  = v->b; }
                // name/country are the entry's LITERAL values (verbatim PSV), not the pool's identity.
                const Value* nm = pp->member("name");
                if (nm && nm->isString()) { p.name = CleanNameSpaces(nm->str); p.country = pp->memberString("country"); }
                else fprintf(stderr, "[career_data_json] WARN: roster without literal 'name' (series %s, driver '%s')\n",
                             sid.c_str(), pp->memberString("driver").c_str());
                pinned.push_back(p);
            }
        }
    }
    if (!anySeason) return false;

    // The identity pool is built lazily from drivers.json (independent of
    // this adapter); we don't populate it here so identity still resolves in PSV mode too.

    // atomic swap of ALL globals (identical to the PSV loaders) ----------
    ReplaceSeries(std::move(series));
    LoadSeriesAccess(paths::Data("series_access.psv").c_str());   // re-applies the access axis onto the freshly-replaced g_series
    LoadSeriesMarket(paths::Data("series_market.psv").c_str());   // 13/08: and the LIVE MARKET tag, for the same reason
    LoadSeriesCars(paths::Data("series_cars.psv").c_str());       // portal: and the car rule, for the same reason
    ReplaceCalendar(std::move(calendar));
    ReplaceFields(std::move(fields));
    ReplacePinned(std::move(pinned));
    ReplaceRoster(std::move(roster));
    ReplacePoints(std::move(pointsMap));
    ReplaceProfiles(std::move(profMap));
    ReplaceEcon(std::move(econMap), std::move(classToNode));
    return true;
}

//
// A queryable IDENTITY pool. Loaded ONCE from drivers.json (a hardcoded path, same as
// the rest of the project), lazy and INDEPENDENT of g_useJsonCareerData -> identity works in PSV mode too.
//
namespace {
std::vector<PoolDriver>                 g_poolDrivers;     // in id order (the JSON key)
std::map<std::string, size_t>           g_poolById;        // id -> index into g_poolDrivers
std::map<std::string, std::vector<size_t>> g_poolByName;   // name -> indices (collisions are possible)
bool g_poolLoaded = false;

// Builds the index only once. If drivers.json can't be opened, it's still marked loaded (empty index):
// identity lookups simply return nullptr/"" — ADDITIVE, it never breaks the PSV flow.
void EnsurePoolLoaded() {
    if (g_poolLoaded) return;
    g_poolLoaded = true;   // already marked (a single attempt, avoids re-parsing on failure)

    std::string err;
    ValuePtr drivers = jsonm::ParseFile(paths::Data("career\\drivers.json").c_str(), &err);   // data/ root
    if (!drivers || !drivers->isObject()) return;

    // obj preserves the parser's insertion order -> deterministic iteration (first id wins on collision).
    for (const auto& kv : drivers->obj) {
        const std::string& id = kv.first;
        const Value* d = kv.second.get();
        if (!d || !d->isObject()) continue;
        if (g_poolById.count(id)) continue;   // duplicate id (shouldn't happen): the first one wins
        PoolDriver pd;
        pd.id      = id;
        pd.name    = CleanNameSpaces(d->memberString("name"));
        pd.country = d->memberString("country");
        pd.kind    = d->memberString("kind");
        pd.birth_year  = d->memberInt("birth_year", 0);    // 0 if missing (real or cast)
        pd.retire_year = d->memberInt("retire_year", 0);
        // Growth fields — only for the recurring cast (kind=="roster"). The real rd_* entries
        // don't carry them; they stay at their defaults (growth_archetype "", stats 0, tier 1-7, hot false).
        if (pd.kind == "roster") {
            pd.growth_archetype = d->memberString("growth_archetype");
            pd.tier_min = d->memberInt("tier_min", 1);
            pd.tier_max = d->memberInt("tier_max", 7);
            pd.hot      = d->memberBool("hot", false);
            const Value* sb = d->member("stats_base");
            if (sb && sb->isObject()) {
                pd.base_skill       = sb->memberNumber("skill", 0.0);
                pd.base_aggression  = sb->memberNumber("aggression", 0.0);
                pd.base_consistency = sb->memberNumber("consistency", 0.0);
            }
        }
        size_t idx = g_poolDrivers.size();
        g_poolDrivers.push_back(std::move(pd));
        g_poolById[id] = idx;
        if (!g_poolDrivers[idx].name.empty()) g_poolByName[g_poolDrivers[idx].name].push_back(idx);
    }
}
} // anon namespace

// The ENTIRE pool, in deterministic id order. Needed by the rookie generator, which
// recombines real first/last names by country instead of shipping a curated names file —
// so a Brazilian rookie gets a Brazilian name, and there's no extra data to keep up to date.
std::vector<std::string> PoolDriverIds() {
    EnsurePoolLoaded();
    std::vector<std::string> out;
    out.reserve(g_poolById.size());
    for (const auto& kv : g_poolById) out.push_back(kv.first);   // g_poolById is a std::map -> already ordered
    return out;
}

// The names of drivers INVENTED by the living world.
static std::map<std::string, std::string> g_generados;
// and their COUNTRY. The registry used to keep only the name and threw away the country the person was generated with:
// grid re-seating (career_fields.cpp, rebindFromSeats) would set the new name while leaving the
// PREVIOUS occupant's flag — a Swedish name recombined from the SWE pool showing up under an Italian flag (the Anders Olofsson case,
// Group C class). Kept as a separate map so as not to touch g_generados's shape; a legacy entry with no country simply isn't here.
static std::map<std::string, std::string> g_generadosPais;

void RegisterGeneratedName(const std::string& id, const std::string& name, const std::string& country) {
    if (id.empty() || name.empty()) return;
    g_generados[id] = name;
    if (!country.empty()) g_generadosPais[id] = country;
}
void ClearGeneratedNames() { g_generados.clear(); g_generadosPais.clear(); }
int  GeneratedNameCount() { return (int)g_generados.size(); }

std::string DriverDisplayName(const std::string& id) {
    if (id.empty()) return std::string();
    const PoolDriver* pd = DriverById(id);
    if (pd && !pd->name.empty()) return pd->name;
    auto it = g_generados.find(id);
    if (it != g_generados.end() && !it->second.empty()) return it->second;
    return id;                                  // last resort: a visible id beats a blank
}

// A mirror of DriverDisplayName for the FLAG. Pool first (real/cast drivers carry their country in their
// record); generated drivers come from the registry. "" if nobody knows — the caller decides (re-seating keeps whatever
// was already there, which is the usual behavior).
std::string DriverDisplayCountry(const std::string& id) {
    if (id.empty()) return std::string();
    const PoolDriver* pd = DriverById(id);
    if (pd && !pd->country.empty()) return pd->country;
    auto it = g_generadosPais.find(id);
    return (it != g_generadosPais.end()) ? it->second : std::string();
}

const PoolDriver* DriverById(const std::string& id) {
    EnsurePoolLoaded();
    if (id.empty()) return nullptr;
    auto it = g_poolById.find(id);
    return (it == g_poolById.end()) ? nullptr : &g_poolDrivers[it->second];
}

std::string GrowthArchetypeFor(const std::string& id) {
    const PoolDriver* pd = DriverById(id);
    return pd ? pd->growth_archetype : std::string();
}

int BirthYearFor(const std::string& id) {
    const PoolDriver* pd = DriverById(id);
    if (pd && pd->birth_year > 0) return pd->birth_year;
    // 23/09: and if it is not in the pool, the registry's. That is where the REAL age of the curated reserve lives
    //  (docs/career/reserve_people.psv): without this a seated pack person had no age for anyone, did not
    //  age or retire, and by T10 there were 846 seats with no known age.
    if (const canon::DriverIdentity* di = registry::ById(id)) return di->birth_year;
    return 0;
}
int RetireYearFor(const std::string& id) {
    const PoolDriver* pd = DriverById(id);
    return pd ? pd->retire_year : 0;
}

int PoolDriversWithBirthYear() {
    EnsurePoolLoaded();
    int n = 0;
    for (const auto& d : g_poolDrivers) if (d.birth_year > 0) ++n;
    return n;
}

bool PoolBirthYearsAllWithin(int lo, int hi) {
    EnsurePoolLoaded();
    for (const auto& d : g_poolDrivers)
        if (d.birth_year > 0 && (d.birth_year < lo || d.birth_year > hi)) return false;
    return true;
}

int PoolDriversAgedAtLeast(int anio, int edadMin) {
    EnsurePoolLoaded();
    int n = 0;
    for (const auto& d : g_poolDrivers)
        if (d.birth_year > 0 && (anio - d.birth_year) >= edadMin) ++n;
    return n;
}

std::string DriverIdForName(const std::string& name, const std::string& country) {
    EnsurePoolLoaded();
    if (name.empty()) return std::string();
    auto it = g_poolByName.find(CleanNameSpaces(name));
    if (it == g_poolByName.end() || it->second.empty()) return std::string();
    const std::vector<size_t>& idxs = it->second;
    if (idxs.size() == 1) return g_poolDrivers[idxs[0]].id;   // single match: direct
    // name collision: disambiguates by country if given; otherwise the first one deterministically (id order).
    if (!country.empty())
        for (size_t k : idxs) if (g_poolDrivers[k].country == country) return g_poolDrivers[k].id;
    // Unresolved ambiguity: defensive warning + first deterministic id (idxs is already in insertion order).
    // .5: goes to stdout, not stderr. Since seeding resolves ALL fields by name, this
    // warning fires on every startup — and any byte on stderr makes build_career_test.ps1 abort with a
    // NativeCommandError, so a DATA warning brought down the whole suite without any test failing.
    // The serious count of homonyms is now kept by `career_registry` (27 names, 54 identities affected).
    printf("[career_data_json] WARN: name '%s' ambiguous (%d ids), returning the first ('%s')\n",
           name.c_str(), (int)idxs.size(), g_poolDrivers[idxs[0]].id.c_str());
    return g_poolDrivers[idxs[0]].id;
}

} // namespace career
