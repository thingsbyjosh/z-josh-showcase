// career_canon.cpp — the adapters of the world of TODAY read in the vocabulary of
//
// House rule for this whole file: **nothing is invented**. Where the data does not exist, the field
// stays at its default and the absence is declared (Gaps() / a Warning), because the value of this layer lies in
// its census being believable. An adapter that fills gaps with plausible values measures its own
// imagination, and that is exactly the mistake that cost the five attempts of 09/08.
#include "career_canon.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>

#include "career_save.h"
#include "career_series.h"
#include "career_fields.h"
#include "career_seats.h"
#include "career_data_json.h"
#include "career_registry.h"    // the single identity catalog; the census is just one more consumer
#include "career_engine.h"
#include "career_genome.h"
#include "career_customai.h"    // GridDriver / ComposeSeasonGrid: the grid that really runs
#include "career_classpace.h"   // ClassPace.talent_ref_span: the curated bar per class
#include "career_gridcurve.h"   // kTalentRefSpan + the sanitizing of the bar (same as ShapeForClass)

namespace career {
namespace canon {

//
// Vocabulary
//

const char* OriginName(Origin o) {
    switch (o) { case Origin::Real: return "real"; default: return "fictional"; }
}
const char* RoleName(PopRole r) {
    switch (r) {
        case PopRole::Incumbent: return "incumbent";
        case PopRole::Cohort:    return "cohort";
        case PopRole::Reserve:   return "reserve";
        default:                 return "overflow";
    }
}
const char* StateName(LifeState s) {
    switch (s) {
        case LifeState::Active:    return "active";
        case LifeState::FreeAgent: return "free_agent";
        case LifeState::Retired:   return "retired";
        default:                   return "dormant";
    }
}
const char* FamilyName(Family f) {
    switch (f) {
        case Family::OpenWheel: return "openwheel";
        case Family::GT:        return "gt";
        case Family::Prototype: return "prototype";
        case Family::Touring:   return "touring";
        case Family::Oval:      return "oval";
        default:                return "historic";
    }
}
bool FamilyFromName(const std::string& s, Family& out) {
    if (s == "openwheel") { out = Family::OpenWheel; return true; }
    if (s == "gt")        { out = Family::GT;        return true; }
    if (s == "prototype") { out = Family::Prototype; return true; }
    if (s == "touring")   { out = Family::Touring;   return true; }
    if (s == "oval")      { out = Family::Oval;      return true; }
    if (s == "historic")  { out = Family::Historic;  return true; }
    return false;
}

// "Unknown affinity: main family of its initial seat and a data warning". The character of the
// class (docs/career/class_age_profile.psv) is the closest thing to a family that exists today in the engine, and the
// translation is LOSSY on purpose: "endurance" covers GT and prototype with the same word. That is why the
// identity that comes out of here carries affinity_known=false — a derivative cannot be used as evidence.
static Family FamilyFromClassCharacter(const std::string& character) {
    if (character == "turismo")     return Family::Touring;
    if (character == "resistencia") return Family::Prototype;
    if (character == "historico")   return Family::Historic;
    return Family::OpenWheel;   // school | promotion | single-seater | unknown
}

//
// World: accessors
//

const SeatDefinition* World::SeatById(const std::string& seat_id) const {
    auto it = seat_index.find(seat_id);
    return it == seat_index.end() ? nullptr : &seats[it->second];
}
const DriverIdentity* World::Identity(const std::string& driver_id) const {
    auto it = identities.find(driver_id);
    return it == identities.end() ? nullptr : &it->second;
}
const DriverCareerState* World::State(const std::string& driver_id) const {
    auto it = states.find(driver_id);
    return it == states.end() ? nullptr : &it->second;
}

//
// The adapter
//

namespace {

// Name/country of whoever really runs, crossed by the SAME id resolution that the seats use
// (FieldOccupantId / PinnedOccupantId). Without this, an occupant whose id is a name hash — because the
// name is not in drivers.json — would be left without identity and the census would count it as a data error
// when in fact the name does exist, just in another file.
struct NameHint { std::string name, country, archetype; };

void CollectNameHints(std::map<std::string, NameHint>& hints) {
    for (const auto& sd : AllSeries()) {
        for (const auto& fd : FieldForSeries(sd.id)) {
            std::string id = FieldOccupantId(fd);
            if (id.empty() || hints.count(id)) continue;
            hints[id] = NameHint{ fd.name, fd.country, std::string() };
        }
        for (const auto& pd : PinnedRosterForSeries(sd.id)) {
            std::string id = PinnedOccupantId(pd);
            if (id.empty() || hints.count(id)) continue;
            hints[id] = NameHint{ pd.name, pd.country, pd.archetype };
        }
    }
    for (int t = 1; t <= 7; t++)
        for (const auto& r : RosterForTier(t)) {
            if (r.id.empty() || hints.count(r.id)) continue;
            hints[r.id] = NameHint{ r.name, r.country, r.archetype };
        }
}

// Where each occupant sits today: the class of their FIRST seat (deterministic order of AllSeries) and whether
// any of their seats is an era seat. It is the source of the derived `era_scope` and of the affinity of
struct SeatFootprint {
    std::string first_class;
    bool any_modern = false, any_historical = false;
    int  seats = 0;
};

}   // namespace

World BuildWorld(const SaveData& save) {
    World w;
    w.year = save.current_world_year;

    // (1) THE ENGINE'S SEEDING, without reimplementing. An empty map goes in and comes out with what production
    // would seed in this same world: LevelAnchoredSeed for the fields, CastSeedFromBase for the cohort,
    // and NOTHING for whoever the id resolver does not find. That "nothing" is half of the grid knot,
    // so measuring it with the real code and not with a copy is half the value of the phase.
    SeedAllDrivers(w.engine_seed);

    std::map<std::string, NameHint> hints;
    CollectNameHints(hints);

    // (2) SEATS AND OCCUPANCY. One seat per (series, index), with its slot resolved when the livery map
    // knows it. The seat_id of is series|vehicle|livery; when the slot does not resolve we
    // keep the legacy key so as not to lose the seat, and Validate reports it.
    const std::set<std::string> playerKeys = PlayerSeatKeys(save);
    std::map<std::string, SeatFootprint> footprint;

    for (const auto& sd : AllSeries()) {
        const bool historical = !IsModernSeries(sd.id);
        std::vector<Seat> seats = SeatsForClass(sd.id, save);
        for (int i = 0; i < (int)seats.size(); i++) {
            SeatDefinition d;
            // Careful with this: `SeatKeyLegacy`, not `SeatKey`. Since the , `SeatKey` returns the key by
            // slot, so comparing it with the seat_id would always give equal and the census would stop seeing the
            // seats covered by a repeated slot — it happened: 19 errors became 1. The instrument
            // has to look at BOTH formats, or it stops seeing exactly the change it is measuring.
            d.legacy_key  = SeatKeyLegacy(sd.id, i);
            d.series_id   = sd.id;
            d.class_name  = sd.class_name;
            d.tier        = sd.level;
            d.seat_number = i;
            d.historical  = historical;
            d.livery_name = seats[i].livery_name;
            d.team_id     = seats[i].team_label;

            int vh = 0, nn = -1;
            if (!d.livery_name.empty() && LiveryVehicleFor(sd.class_name, d.livery_name, vh, nn)) {
                d.livery_slot.vehicle_hash     = vh;
                d.livery_slot.catalog_livery_id = nn;
            }
            // The seat_id is composed by the ENGINE (career_seats::SeatKey), not by the census: the key format
            // lives in a single place since the , and the census is a consumer like any other.
            d.seat_id = SeatKey(sd.id, i);

            SeatAssignment a;
            a.seat_id = d.seat_id;
            const std::string& occ = seats[i].occupant_id;
            if (!seats[i].open && !occ.empty() && occ != std::string(kSeatOpen)) a.driver_id = occ;
            auto cs = save.seat_contract_start.find(d.seat_id);
            if (cs == save.seat_contract_start.end()) cs = save.seat_contract_start.find(d.legacy_key);
            if (cs != save.seat_contract_start.end()) a.contract_start_year = cs->second;

            // The vector keeps ALL the seats (Validate needs to see the repeated seat_ids); the occupancy
            // map keeps the first, which is the catalog's deterministic order.
            if (!w.seat_index.count(d.seat_id)) w.seat_index[d.seat_id] = w.seats.size();
            w.seats.push_back(d);
            if (!w.assignments.count(a.seat_id)) w.assignments[a.seat_id] = a;
        }
    }

    // The seat footprint is taken from the ASSIGNMENTS, not from the walk above, so that "who has a
    // seat" is ONE count and not two. With repeated seat_ids (there are: 19 today) the map keeps the
    // first, and counting by the vector gave seven more occupants than counting by the map. Two censuses of the same
    // world that do not add up with each other are no good for comparing anything.
    for (const auto& kv : w.assignments) {
        if (kv.second.driver_id.empty()) continue;
        const SeatDefinition* d = w.SeatById(kv.first);
        if (!d) continue;
        SeatFootprint& fp = footprint[kv.second.driver_id];
        if (fp.seats == 0) fp.first_class = d->class_name;
        fp.seats++;
        if (d->historical) fp.any_historical = true; else fp.any_modern = true;
    }

    // (3) IDENTITIES. Three entry doors, in order of authority: the curated pool, the drivers the
    // save invented, and "what is left": whoever occupies a seat without being in either of the two.
    // are NO longer re-derived here. They are served by `career_registry`, which is the single catalog (). Before
    // this block went back to reading the pool and translating `kind` to the two axes on its own — a second
    // opinion on who is who. The census consumes the catalog like any other consumer; otherwise it
    // would measure a world that only exists inside the census.
    registry::Load();
    for (const std::string& id : registry::Ids())
        if (const DriverIdentity* di = registry::ById(id)) w.identities[id] = *di;

    // Those the SAVE invented. They are not in any table — they cannot be — so they are registered IN
    // the registry, not separately: calls them procedural overflow and they are people of the world like any other.
    for (const auto& kv : save.gen_drivers) {
        DriverIdentity di;
        di.driver_id       = kv.first;
        di.display_name    = kv.second.name;
        di.country         = kv.second.country;
        di.origin          = Origin::Fictional;
        di.population_role = PopRole::Overflow;
        if (const DriverIdentity* reg = registry::Register(di)) w.identities[kv.first] = *reg;
    }

    // And the tail occupants (`fl_*`), which are not a driver but a named generated GAP. They exist
    // because 41 series have fewer named people than seats; the retires them with the reserve.
    for (const auto& kv : w.assignments) {
        const std::string& id = kv.second.driver_id;
        if (id.empty() || w.identities.count(id)) continue;
        DriverIdentity di;
        di.driver_id = id;
        auto h = hints.find(id);
        if (h != hints.end()) { di.display_name = h->second.name; di.country = h->second.country;
                                di.archetype = h->second.archetype; }
        di.origin          = Origin::Fictional;
        di.population_role = PopRole::Overflow;
        if (const DriverIdentity* reg = registry::Register(di)) w.identities[id] = *reg;
    }

    // (3b) What can only be known by LOOKING AT THE WORLD, and that is why it is not in the catalog: where
    // each one sits and what this particular save says about them.
    for (auto& kv : w.identities) {
        DriverIdentity& di = kv.second;
        if (di.display_name.empty()) di.display_name = DriverDisplayName(di.driver_id);

        // The save rules over the catalog for the birth year: it is the canonical map that the
        // rest of the engine already reads (ActiveModernSet, the age curve).
        auto by = save.birth_year.find(di.driver_id);
        if (by != save.birth_year.end() && by->second > 0) di.birth_year = by->second;

        // AFFINITY. The DATA one already comes set by the registry (authored corpus). Here only the fallback
        // for whoever lacks it: the family of their initial seat, marked as deduced.
        if (!di.affinity_known) {
            auto fp = footprint.find(di.driver_id);
            di.primary_affinity = FamilyFromClassCharacter(
                fp == footprint.end() ? std::string() : ClassCharacter(fp->second.first_class));
        }

        // ERA_SCOPE. wants it in the catalog; today it does not exist in any file, so it is derived from
        // the seats they occupy. It is declared in Gaps() so nobody mistakes it for authorship.
        auto fp = footprint.find(di.driver_id);
        if (fp == footprint.end())                                   di.era_scope = "";
        else if (fp->second.any_modern && fp->second.any_historical) di.era_scope = "both";
        else if (fp->second.any_historical)                          di.era_scope = "historic";
        else                                                         di.era_scope = "modern";
    }

    // (4) STATE. separates latent capacity from observed ELO; TODAY IT IS A SINGLE NUMBER and both fields
    // point to it. When the separates them, this adapter is the place where it shows.
    for (const auto& kv : w.identities) {
        const std::string& id = kv.first;
        DriverCareerState st;
        st.driver_id = id;
        auto r = save.driver_ratings.find(id);
        auto s = w.engine_seed.find(id);
        double rating = 0.0;
        bool   touched = false;                       // does the save know anything about this driver?
        if (r != save.driver_ratings.end()) { rating = r->second; touched = true; }
        else if (s != w.engine_seed.end())    rating = s->second;
        st.pace_rating    = rating;
        st.results_rating = rating;
        auto rc = save.driver_races.find(id);
        if (rc != save.driver_races.end()) { st.results_races = rc->second; if (rc->second > 0) touched = true; }

        const bool retired  = save.retired.count(id) > 0;
        const bool hasSeat  = footprint.count(id) > 0;
        // with the only translation that today's world allows: without a static reserve () there is no
        // real "dormant", so the word is reserved for the catalog that the save has NEVER
        // touched — which is what the reserve will be. A name in drivers.json with no seat, no ELO and no
        // races is not a free agent: it is a card that nobody has taken out of the drawer.
        if (retired)       st.state = LifeState::Retired;
        else if (hasSeat)  st.state = LifeState::Active;
        else if (touched)  st.state = LifeState::FreeAgent;
        else               st.state = LifeState::Dormant;
        w.states[id] = st;
    }

    // (5) CONTRACTS: derived view, one per occupied seat. The main one is the highest tier (tie -> the
    // lowest seat_id), which is the reasonable reading of over a world that does not yet distinguish.
    std::map<std::string, std::vector<size_t>> bySeatDriver;
    for (const auto& kv : w.assignments) {
        const SeatAssignment& a = kv.second;
        if (a.driver_id.empty()) continue;
        const SeatDefinition* sd = w.SeatById(a.seat_id);
        if (!sd) continue;
        DriverContract c;
        c.driver_id  = a.driver_id;
        c.seat_id    = a.seat_id;
        c.series_id  = sd->series_id;
        c.team_id    = sd->team_id;
        c.tier       = sd->tier;
        c.start_year = a.contract_start_year;
        c.player     = playerKeys.count(sd->legacy_key) > 0;
        c.primary    = false;
        w.contracts.push_back(c);
    }
    std::sort(w.contracts.begin(), w.contracts.end(), [](const DriverContract& a, const DriverContract& b) {
        if (a.driver_id != b.driver_id) return a.driver_id < b.driver_id;
        return a.seat_id < b.seat_id;
    });
    for (size_t i = 0; i < w.contracts.size(); ) {
        size_t j = i, best = i;
        while (j < w.contracts.size() && w.contracts[j].driver_id == w.contracts[i].driver_id) {
            if (w.contracts[j].tier > w.contracts[best].tier) best = j;
            j++;
        }
        w.contracts[best].primary = true;
        i = j;
    }
    return w;
}

//
// Validators
//

std::vector<Finding> Validate(const World& w) {
    std::vector<Finding> out;
    auto add = [&](Severity sev, const char* code, const std::string& subj, const std::string& detail) {
        out.push_back(Finding{ sev, code, subj, detail });
    };

    // · two seats with the same seat_id. With the slot resolved it should never happen; when
    // it does, two different cars share a physical address and the one that arrives second disappears.
    std::map<std::string, int> seatSeen;
    for (const auto& s : w.seats) seatSeen[s.seat_id]++;
    for (const auto& s : w.seats) {
        if (seatSeen[s.seat_id] > 1 && s.legacy_key != s.seat_id)
            add(Severity::Error, "seat_id_dup", s.seat_id,
                s.legacy_key + " comparte identidad de ranura con otro asiento");
        // · modern seat with no slot. In an era series it is not an error: its grid is static.
        if (!s.livery_slot.resolved() && !s.historical)
            add(Severity::Error, "seat_no_slot", s.legacy_key,
                s.class_name + " / librea '" + s.livery_name + "' no resuelve (vehículo, librea)");
    }

    // · active occupant without identity, and "every occupant references an existing ID".
    for (const auto& kv : w.assignments) {
        const SeatAssignment& a = kv.second;
        if (a.driver_id.empty()) continue;
        const DriverIdentity* di = w.Identity(a.driver_id);
        if (!di) { add(Severity::Error, "occupant_no_identity", a.driver_id, "ocupa " + a.seat_id); continue; }
        // An occupant whose "name" is its own id is a driver that the UI would show as "fl_1160919266".
        if (di->display_name == di->driver_id)
            add(Severity::Warning, "identity_nameless", di->driver_id, "ocupa " + a.seat_id + " sin nombre en ninguna tabla");
    }

    // · dangling contract, and "at most one main per driver".
    std::map<std::string, int> primaries, seatsPerDriver;
    for (const auto& c : w.contracts) {
        if (!w.SeatById(c.seat_id))  add(Severity::Error, "contract_dangling", c.driver_id, "asiento inexistente " + c.seat_id);
        if (!w.Identity(c.driver_id)) add(Severity::Error, "contract_dangling", c.driver_id, "piloto inexistente");
        if (c.primary) primaries[c.driver_id]++;
        seatsPerDriver[c.driver_id]++;
        // "the player never shares their slot with an AI". Today they ALWAYS share it: the seats
        // model does not know you are there, so the seat keeps driving the driver from the data. The
        // closes it; meanwhile let it be counted, because it is the reason your teammate may
        // not be the one running next to you.
        if (c.player)
            add(Severity::Warning, "player_seat_phantom_occupant", c.seat_id,
                "el asiento del jugador deriva además al piloto " + c.driver_id);
    }
    for (const auto& kv : primaries)
        if (kv.second > 1) add(Severity::Error, "two_primaries", kv.first, std::to_string(kv.second) + " contratos principales");

    // · no duplicate driver within the same event (here: the same series).
    std::set<std::string> dupSeen;
    for (const auto& c : w.contracts) {
        std::string k = c.series_id + "\x1f" + c.driver_id;
        if (!dupSeen.insert(k).second)
            add(Severity::Error, "event_dup_driver", c.driver_id, "dos asientos en " + c.series_id);
    }

    // / · "no cohort driver starts above tier 2". Today the cohort sits in
    // all seven tiers on purpose, so this comes out red by design: it is the precondition that measures the
    // .5, not a failure to fix loose. See [[cohort_above_home_tier_in_grids]].
    for (const auto& c : w.contracts) {
        const DriverIdentity* di = w.Identity(c.driver_id);
        if (di && di->population_role == PopRole::Cohort && c.tier > bal::COHORT_HOME_MAX_TIER)
            add(Severity::Warning, "cohort_above_tier2", c.driver_id,
                "tier " + std::to_string(c.tier) + " en " + c.series_id);
    }

    // THE TWO SCALES. An unseeded occupant runs with the talent of the static curve while their
    // grid neighbor comes out of RatingQuality(ELO): medians separated by 0.435 in 70 of 100 classes.
    // It is not a cosmetic warning — it is the root cause that the closes.
    for (const auto& kv : w.assignments) {
        const std::string& id = kv.second.driver_id;
        if (id.empty()) continue;
        if (!w.engine_seed.count(id))
            add(Severity::Warning, "occupant_unseeded", id, "ocupa " + kv.first + " sin rating sembrado");
    }

    // · fallbacks applied, which must be countable.
    for (const auto& kv : w.identities) {
        const DriverIdentity& di = kv.second;
        if (!di.affinity_known) add(Severity::Warning, "affinity_derived", di.driver_id,
                                    std::string("familia deducida del asiento: ") + FamilyName(di.primary_affinity));
        if (di.birth_year <= 0)  add(Severity::Warning, "birth_year_missing", di.driver_id, "sin año de nacimiento");
    }

    std::sort(out.begin(), out.end(), [](const Finding& a, const Finding& b) {
        if (a.sev != b.sev)   return (int)a.sev < (int)b.sev;
        if (a.code != b.code) return a.code < b.code;
        return a.subject < b.subject;
    });
    return out;
}

//
// Census
//

static double Median(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

Census TakeCensus(const World& w, const std::vector<Finding>& findings) {
    Census c;
    const registry::Stats rs = registry::LastStats();
    c.identity_from_pool       = rs.from_pool;
    c.identity_from_field      = rs.from_field;
    c.identity_authored        = rs.authored;
    c.identity_reclassified    = rs.reclassified;
    c.identity_name_collisions = rs.name_collisions;
    c.identity_authored_shared = rs.authored_shared;
    for (const auto& kv : w.identities) {
        const DriverIdentity& di = kv.second;
        c.drivers++;
        c.by_origin[(int)di.origin]++;
        c.by_role[(int)di.population_role]++;
        c.by_family[(int)di.primary_affinity]++;
        if (!di.affinity_known) c.affinity_derived++;
        if (di.birth_year <= 0) c.birth_year_missing++;
        const DriverCareerState* st = w.State(di.driver_id);
        if (st) c.by_state[(int)st->state]++;
    }

    std::set<std::string> classes, classesMixing;
    std::map<std::string, std::pair<int,int>> perClass;          // class -> (seeded, unseeded)
    std::map<std::string, std::vector<double>> perClassSeeds;    // class -> NON-cohort seedings
    std::set<std::string> occupants;

    for (size_t i = 0; i < w.seats.size(); i++) {
        const SeatDefinition& s = w.seats[i];
        c.seats_total++;
        if (s.historical) c.seats_historical++;
        if (!s.livery_slot.resolved()) c.seats_slot_unresolved++;
        int t = s.tier; if (t < 0) t = 0; if (t > 7) t = 7;
        c.seats_by_tier[t]++;
        classes.insert(s.class_name);
        // A seat whose seat_id is already held by another is COVERED: it shares a physical address, so only
        // one occupancy can exist for both. It is counted apart instead of counting the same occupant twice,
        // which is what made "occupants" and "active" give different numbers.
        auto ix = w.seat_index.find(s.seat_id);
        if (ix == w.seat_index.end() || ix->second != i) { c.seats_dup_id++; continue; }
        auto a = w.assignments.find(s.seat_id);
        const bool occupied = a != w.assignments.end() && !a->second.driver_id.empty();
        if (!occupied) { c.seats_open++; continue; }
        c.seats_occupied++;
        c.occupied_by_tier[t]++;

        const std::string& id = a->second.driver_id;
        occupants.insert(id);
        const bool seeded = w.engine_seed.count(id) > 0;
        auto& pc = perClass[s.class_name];
        if (seeded) pc.first++; else pc.second++;
        const DriverIdentity* di = w.Identity(id);
        const bool cohort = di && di->population_role == PopRole::Cohort;
        if (seeded && !cohort) perClassSeeds[s.class_name].push_back(w.engine_seed.at(id));
    }
    c.classes_with_seats = (int)classes.size();
    for (const auto& kv : perClass) if (kv.second.first > 0 && kv.second.second > 0) c.classes_mixing_scales++;

    c.occupants = (int)occupants.size();
    for (const auto& id : occupants) if (!w.engine_seed.count(id)) c.occupants_unseeded++;

    // The ELO range WITHIN a class is what the XML conversion turns into a fan: if it mixes
    // people anchored at level 2 with people anchored at level 7, the fan of that grid is not the one that is curated.
    for (auto& kv : perClassSeeds) {
        if (kv.second.size() < 2) continue;
        auto mm = std::minmax_element(kv.second.begin(), kv.second.end());
        double range = *mm.second - *mm.first;
        if (range > c.class_seed_range_max) { c.class_seed_range_max = range; c.class_seed_range_worst = kv.first; }
    }

    std::vector<double> real, cohort;
    for (const auto& kv : w.engine_seed) {
        const DriverIdentity* di = w.Identity(kv.first);
        if (di && di->population_role == PopRole::Cohort) cohort.push_back(kv.second);
        else                                              real.push_back(kv.second);
    }
    if (!real.empty())   { auto mm = std::minmax_element(real.begin(), real.end());
                           c.seed_real_lo = *mm.first; c.seed_real_hi = *mm.second; c.seed_real_p50 = Median(real); }
    if (!cohort.empty()) { auto mm = std::minmax_element(cohort.begin(), cohort.end());
                           c.seed_cohort_lo = *mm.first; c.seed_cohort_hi = *mm.second; c.seed_cohort_p50 = Median(cohort); }

    c.contracts = (int)w.contracts.size();
    std::map<std::string, int> perDriver;
    for (const auto& ct : w.contracts) perDriver[ct.driver_id]++;
    for (const auto& kv : perDriver) if (kv.second > 1) c.drivers_multi_seat++;
    for (const auto& ct : w.contracts) {
        const DriverIdentity* di = w.Identity(ct.driver_id);
        if (di && di->population_role == PopRole::Cohort && ct.tier > bal::COHORT_HOME_MAX_TIER) c.cohort_above_tier2++;
    }

    for (const auto& f : findings) (f.sev == Severity::Error ? c.errors : c.warnings)++;
    return c;
}

//
// Census of the grid — the other population, the one that really runs
//

// · THE COHORT IS COUNTED BY IDENTITY, NOT BY `kind`. And this was an instrument failure.
//
// `GridDriver.kind` is "roster" for the youth academy... and also for each series' own people (`fl_*`),
// because the composer had no third label and "roster" really meant "is not a real driver
// from the data". It is literally the finding of the "`kind=real` meant 'is not from the cohort'"
// applied to the other side. Measured consequence: the census gave 418 cohort rows above their level in the
// seats path, when the cohort there can only enter through its home tiers. It was counting the tail.
static bool EsCohorteId(const std::string& id) {
    if (id.empty()) return false;
    static std::set<std::string> cohorte;
    static size_t firma = 0;
    const size_t f = RosterForTier(1).size() * 1000 + RosterForTier(7).size();
    if (cohorte.empty() || firma != f) {
        cohorte.clear(); firma = f;
        for (int t = 1; t <= 7; t++) for (const auto& rd : RosterForTier(t)) if (!rd.id.empty()) cohorte.insert(rd.id);
    }
    return cohorte.count(id) > 0;
}
static bool EsCohorte(const GridDriver& d) { return EsCohorteId(d.driver_id); }

GridCensus TakeGridCensus(const World& w, const SaveData& save) {
    GridCensus g;
    std::vector<double> tSeeded, tUnseeded, us, usDerivada, usReales;
    std::set<std::string> visto;

    for (const auto& s : AllSeries()) {
        const ClassPace cp = ClassPaceFor(s.class_name);
        if (!cp.found) continue;                              // without a curated fan there is no bar to measure
        if (!visto.insert(s.class_name).second) continue;     // one series per class, the first one
        const int gs = GridSizeForClassName(s.class_name);
        if (gs < 8) continue;

        // THE LIVE PATH, with the same parameters as production (PregenAllAi): real size and level,
        // the seeded ratings and the seat bridge. Measuring without any of the three measures another grid —
        // it has already fooled three different measurements, and that is why all three go here.
        // 18/08 — KNOWN DISCREPANCY, DELIBERATELY NOT FIXED. FIVE arguments are missing here, not
        // two: the original note listed `skipLiveries` and `driverRaces` and fell short by three when the
        // 18/08 the aging trio was added. The COMPLETE list, so nobody reads it believing that
        // it is already whole (19/08 — if another is added tomorrow, this paragraph is updated or lies again):
        //   · `skipLiveries` — an AI car can come out with the player's paint;
        //   · `driverRaces`  — without it `EffectivePace` returns the whole `RatingQuality(ELO)` instead of the
        //                      perturbed authored capacity;
        //   · `driverCurve`, `birthYear`, `worldYear` — the 18/08 trio. Without them a CLONE enters by its
        //                      CEILING instead of by where it is: this census measures grids WITHOUT aging
        //                      while production does age them, and the difference grows with the cloned share.
        // And `role` is fixed to `SingleClass` instead of `ClassRoleForSeries`, which here does not change the number
        // because the census does not read blue flags.
        // That is: this census measures a grid that is NOT exactly the one written to the XML.
        //
        // It is NOT touched here on purpose: the numbers of this instrument are written in two specs and in the
        // thresholds of several tests. Changing it without re-deriving everything it fed is the mistake this
        // project has already made three times. It has its own card; until then, this note is what stops
        // someone from reading the line above — "the same parameters as production" — and believing it whole.
        const std::vector<SeatRow> filas = SeatRowsForSeries(save, s.id);   // the ORDERED bridge
        std::vector<GridDriver> grid = ComposeSeasonGrid(
            s.id, s.level, 0.0, gs, false, EmptyRivalForm(), nullptr, &w.engine_seed,
            ClassRole::SingleClass, filas.empty() ? nullptr : &filas);
        if ((int)grid.size() < 8) continue;

        g.classes++;
        int seeded = 0, unseeded = 0;
        double tAce = -1e9, tLast = 1e9;
        double tAceR = -1e9, tLastR = 1e9;   // the ace and the backmarker WITHOUT counting the cohort
        bool aceIsCohort = false, hayCohorte = false;

        for (const auto& d : grid) {
            g.rows++;
            // AS THE COMPOSER SEES IT: `ratingSkill(fd.id, ...)` with the RAW id of the row, which in the
            // PSV comes empty. That is the criterion that decides which scale this car's talent comes from,
            // so it is the one that has to be counted. (The talent is already computed when the composition
            // changes occupant, so looking at the id from afterwards does not falsify this count.)
            const bool hasElo = !d.driver_id.empty() && w.engine_seed.count(d.driver_id) > 0;
            if (!hasElo) {
                g.rows_unseeded++; unseeded++;
                // And the size of the fix: does its ELO exist under the id the shared resolution would give?
                std::string alt = DriverIdForName(d.name, d.country);
                if (!alt.empty() && w.engine_seed.count(alt)) g.rows_seedable_by_name++;
            } else seeded++;

            if (d.talent >= 0.0) {
                (hasElo ? tSeeded : tUnseeded).push_back(d.talent);
                if (d.talent > tAce) { tAce = d.talent; aceIsCohort = EsCohorte(d); }
                if (d.talent < tLast) tLast = d.talent;
            }
            std::string id = d.driver_id.empty() ? DriverIdForName(d.name, d.country) : d.driver_id;
            const DriverIdentity* di = id.empty() ? nullptr : w.Identity(id);
            const bool cohort = EsCohorte(d) || (di && di->population_role == PopRole::Cohort);
            // 11/08: against the CONSTANT, not against a literal 2 — the cohort's home is tier 1 from today and
            // an instrument that measures against a hand-written number stops measuring as soon as it moves.
            if (cohort) { g.cohort_rows++; hayCohorte = true; if (s.level > bal::COHORT_HOME_MAX_TIER) g.cohort_rows_above_tier2++; }
            else if (d.talent >= 0.0) {
                if (d.talent > tAceR)  tAceR  = d.talent;
                if (d.talent < tLastR) tLastR = d.talent;
            }
        }
        if (hayCohorte) g.classes_with_cohort++;
        if (seeded > 0 && unseeded > 0) g.classes_mixing_scales++;
        if (aceIsCohort) g.classes_cohort_leads++;

        // the EFFECTIVE bar (derived from the level x class trim), the same one the XML writer
        // receives. Measuring with the legacy column would be measuring a bar that is no longer used.
        double ref = TalentRefSpanFor(s.class_name, s.level);
        if (!(ref > 0.05 && ref < 2.0)) ref = kTalentRefSpan;
        if (tAce > tLast && ref > 0.0) {
            const double u = (tAce - tLast) / ref;
            us.push_back(u);
            if (u > 2.0) g.classes_u_over_2++;
            if (u > g.u_max) { g.u_max = u; g.u_worst_class = s.class_name; }
        }
        // AND THE MEASURE THAT DECIDES THE REST OF PHASE D: the same count with the PURE DERIVED bar, without
        // the curated trim. If the derivation describes well what the seeding produces, this has to hover around 1
        // in every class. What deviates from 1 is what the trim is correcting — and there you can see at a
        // glance how much of the dispersion is "this class is truly different" and how much is "the bar was
        // divorced from the scale".
        const double refPuro = DerivedTalentRefSpan(s.level);
        if (tAce > tLast && refPuro > 0.0) {
            const double ud = (tAce - tLast) / refPuro;
            usDerivada.push_back(ud);
            if (ud > g.ud_max) { g.ud_max = ud; g.ud_worst_class = s.class_name; }
        }
        if (tAceR > tLastR && refPuro > 0.0) {
            const double ur = (tAceR - tLastR) / refPuro;
            usReales.push_back(ur);
            if (ur > g.ur_max) g.ur_max = ur;
        }
    }

    if (!us.empty()) { std::sort(us.begin(), us.end()); g.u_min = us.front(); g.u_p50 = us[us.size() / 2]; }
    if (!usDerivada.empty()) {
        std::sort(usDerivada.begin(), usDerivada.end());
        g.ud_min = usDerivada.front(); g.ud_p50 = usDerivada[usDerivada.size() / 2];
    }
    if (!usReales.empty()) {
        std::sort(usReales.begin(), usReales.end());
        g.ur_min = usReales.front(); g.ur_p50 = usReales[usReales.size() / 2];
    }
    g.talent_p50_seeded   = Median(tSeeded);
    g.talent_p50_unseeded = Median(tUnseeded);
    if (!tSeeded.empty() && !tUnseeded.empty())
        g.talent_gap = std::fabs(g.talent_p50_unseeded - g.talent_p50_seeded);
    return g;
}

//
// Dumps
//

static std::string Fmt(const char* f, ...) {
    char buf[512];
    va_list ap; va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

std::string DumpDriver(const World& w, const std::string& driver_id) {
    const DriverIdentity* di = w.Identity(driver_id);
    if (!di) return std::string();
    std::string s = Fmt("driver %s\n  name      %s (%s)\n  origin    %s / %s\n",
                        di->driver_id.c_str(), di->display_name.c_str(), di->country.c_str(),
                        OriginName(di->origin), RoleName(di->population_role));
    s += Fmt("  born      %d%s   retire %d\n", di->birth_year, di->birth_year ? "" : " (sin dato)",
             di->authored_retire_year);
    s += Fmt("  affinity  %s%s   arch '%s'   era %s\n", FamilyName(di->primary_affinity),
             di->affinity_known ? "" : " (deducida)", di->archetype.c_str(),
             di->era_scope.empty() ? "-" : di->era_scope.c_str());
    s += Fmt("  genome    %llu\n", (unsigned long long)di->genome_seed);
    if (const DriverCareerState* st = w.State(driver_id))
        s += Fmt("  state     %s   pace %.1f   elo %.1f (%d carreras)\n",
                 StateName(st->state), st->pace_rating, st->results_rating, st->results_races);
    auto sd = w.engine_seed.find(driver_id);
    s += sd == w.engine_seed.end() ? "  seed      NINGUNA (cae a la curva estatica)\n"
                                   : Fmt("  seed      %.1f\n", sd->second);
    for (const auto& c : w.contracts) {
        if (c.driver_id != driver_id) continue;
        s += Fmt("  %s %s  t%d  %s  desde %d%s\n", c.primary ? "PRIMARY" : "  second",
                 c.series_id.c_str(), c.tier, c.team_id.c_str(), c.start_year,
                 c.player ? "  [asiento del jugador]" : "");
    }
    return s;
}

std::string DumpSeat(const World& w, const std::string& seat_id) {
    const SeatDefinition* s = w.SeatById(seat_id);
    if (!s) for (const auto& d : w.seats) if (d.legacy_key == seat_id) { s = &d; break; }
    if (!s) return std::string();
    std::string out = Fmt("seat %s\n  legacy    %s\n  series    %s   clase %s   tier %d%s\n",
                          s->seat_id.c_str(), s->legacy_key.c_str(), s->series_id.c_str(),
                          s->class_name.c_str(), s->tier, s->historical ? "   (epoca)" : "");
    out += Fmt("  team      %s\n  livery    '%s' -> ", s->team_id.c_str(), s->livery_name.c_str());
    out += s->livery_slot.resolved()
           ? Fmt("veh %d / librea %d\n", s->livery_slot.vehicle_hash, s->livery_slot.catalog_livery_id)
           : std::string("SIN RANURA\n");
    auto a = w.assignments.find(s->seat_id);
    if (a == w.assignments.end() || a->second.driver_id.empty()) out += "  occupant  (libre)\n";
    else {
        const DriverIdentity* di = w.Identity(a->second.driver_id);
        out += Fmt("  occupant  %s  %s  desde %d\n", a->second.driver_id.c_str(),
                   di ? di->display_name.c_str() : "(SIN IDENTIDAD)", a->second.contract_start_year);
    }
    return out;
}

std::string CensusText(const Census& c) {
    std::string s;
    s += "== CENSO CANONICO (Fase A) ==\n";
    s += Fmt("pilotos            %d   (real %d / ficticio %d)\n", c.drivers,
             c.by_origin[(int)Origin::Real], c.by_origin[(int)Origin::Fictional]);
    s += Fmt("  rol              incumbent %d  cohorte %d  reserva %d  overflow %d\n",
             c.by_role[0], c.by_role[1], c.by_role[2], c.by_role[3]);
    s += Fmt("  estado           activo %d  agente libre %d  retirado %d  dormido %d\n",
             c.by_state[0], c.by_state[1], c.by_state[2], c.by_state[3]);
    s += Fmt("  familia          ow %d  gt %d  proto %d  turismo %d  oval %d  historico %d\n",
             c.by_family[0], c.by_family[1], c.by_family[2], c.by_family[3], c.by_family[4], c.by_family[5]);
    s += Fmt("  sin dato         afinidad deducida %d   sin ano de nacimiento %d\n",
             c.affinity_derived, c.birth_year_missing);
    s += Fmt("  procedencia      pool %d  solo-corre %d  con ficha autorada %d\n",
             c.identity_from_pool, c.identity_from_field, c.identity_authored);
    s += Fmt("  homonimos        %d nombres repetidos -> %d identidades COMPARTEN ficha autorada\n",
             c.identity_name_collisions, c.identity_authored_shared);
    s += Fmt("  ★RECLASIFICADOS  %d   (estaban como `kind=real` y el corpus los declara INVENTADOS)\n",
             c.identity_reclassified);
    s += Fmt("asientos           %d   ocupados %d   libres %d   de epoca %d\n",
             c.seats_total, c.seats_occupied, c.seats_open, c.seats_historical);
    s += Fmt("  sin ranura       %d   tapados por seat_id repetido %d\n",
             c.seats_slot_unresolved, c.seats_dup_id);
    s += "  por tier         ";
    for (int t = 1; t <= 7; t++) s += Fmt("t%d %d/%d  ", t, c.occupied_by_tier[t], c.seats_by_tier[t]);
    s += "\n";
    s += Fmt("contratos          %d   con 2+ asientos %d   cohorte por encima de t2 %d\n",
             c.contracts, c.drivers_multi_seat, c.cohort_above_tier2);
    s += "-- las dos escalas (lo que decide el abanico de la parrilla) --\n";
    s += Fmt("ocupantes          %d   SIN SEMBRAR %d\n", c.occupants, c.occupants_unseeded);
    s += Fmt("clases con asiento %d   MEZCLANDO escalas %d\n", c.classes_with_seats, c.classes_mixing_scales);
    s += Fmt("siembra por nivel  [%.0f .. %.0f] p50 %.0f\n", c.seed_real_lo, c.seed_real_hi, c.seed_real_p50);
    s += Fmt("siembra cohorte    [%.0f .. %.0f] p50 %.0f\n", c.seed_cohort_lo, c.seed_cohort_hi, c.seed_cohort_p50);
    s += Fmt("rango dentro de una clase (sin cohorte)  max %.0f  en %s\n",
             c.class_seed_range_max, c.class_seed_range_worst.c_str());
    s += Fmt("hallazgos          %d errores   %d avisos\n", c.errors, c.warnings);
    return s;
}

std::string GridCensusText(const GridCensus& g) {
    std::string s;
    s += "== CENSO DE PARRILLA (la poblacion que CORRE, no la que tiene asiento) ==\n";
    s += Fmt("clases             %d   filas %d\n", g.classes, g.rows);
    s += Fmt("  SIN SEMBRAR      %d   (de esas, %d tienen ELO bajo el id que la resolucion compartida daria)\n",
             g.rows_unseeded, g.rows_seedable_by_name);
    s += Fmt("  clases mezclando escalas   %d de %d\n", g.classes_mixing_scales, g.classes);
    s += Fmt("cohorte            %d filas   por encima de t2 %d   lidera %d clases\n",
             g.cohort_rows, g.cohort_rows_above_tier2, g.classes_cohort_leads);
    s += Fmt("talento p50        con ELO %.3f   sin ELO %.3f   brecha %.3f\n",
             g.talent_p50_seeded, g.talent_p50_unseeded, g.talent_gap);
    s += Fmt("vara u (as-farolillo / vara EFECTIVA)  min %.2f  p50 %.2f  max %.2f (%s)  clases u>2: %d\n",
             g.u_min, g.u_p50, g.u_max, g.u_worst_class.c_str(), g.classes_u_over_2);
    s += Fmt("   con la vara DERIVADA PURA (sin trim)  min %.2f  p50 %.2f  max %.2f (%s)\n",
             g.ud_min, g.ud_p50, g.ud_max, g.ud_worst_class.c_str());
    s += Fmt("   idem SIN la cohorte en la cuenta      min %.2f  p50 %.2f  max %.2f   (%d clases la llevan)\n",
             g.ur_min, g.ur_p50, g.ur_max, g.classes_with_cohort);
    return s;
}

std::string FindingsText(const std::vector<Finding>& f, int maxPerCode) {
    std::string s;
    std::map<std::string, int> shown, total;
    for (const auto& x : f) total[x.code]++;
    for (const auto& x : f) {
        int& n = shown[x.code];
        if (maxPerCode > 0 && n >= maxPerCode) continue;
        n++;
        s += Fmt("%-7s %-28s %-34s %s\n", x.sev == Severity::Error ? "ERROR" : "aviso",
                 x.code.c_str(), x.subject.c_str(), x.detail.c_str());
    }
    if (maxPerCode > 0)
        for (const auto& kv : total)
            if (kv.second > maxPerCode) s += Fmt("        %-28s ... y %d mas\n", kv.first.c_str(), kv.second - maxPerCode);
    return s;
}

std::vector<std::string> Gaps() {
    // Each line is a field of that TODAY has nowhere to come from, with the phase that brings it. The list is
    // short on purpose: if it grows, the adapter is inventing less than it should.
    return {
        "DriverIdentity.potential_rating      — no existe techo de desarrollo; hoy la capacidad no crece (Fase D)",
        "DriverIdentity.development_profile   — no hay perfiles early/normal/late/veteran (Fase D)",
        "DriverIdentity.affinity_vector[6]    — solo hay familia PRINCIPAL, y para la cohorte ni eso (Fase D)",
        "DriverIdentity.starting_pace_rating  — hoy la siembra es el ELO mismo; separar capacidad de resultados es la Fase D",
        "DriverIdentity.era_scope             — se DEDUCE de los asientos ocupados; no esta autorado (Fase B)",
        "DriverCareerState.family_experience  — no se acumula experiencia por familia (Fase D)",
        "DriverCareerState.debut/last_active  — no se registran; el mundo no sabe cuando debuto nadie (Fase B)",
        "DriverCareerState.reputation         — la REP del jugador existe; la de la IA no (Fase D)",
        "SeatDefinition.material_unit_id      — el escalar de coche es por equipo, no por asiento (Fase F)",
        "TeamDefinition                       — no hay entidad equipo: team_id es hoy una ETIQUETA de texto (Fase C/F)",
        "PopRole::Reserve                     — no hay reserva precreada; todo novato entra como overflow (Fase G)",
        "f3_authored_profiles.psv sin pais    — se indexa solo por NOMBRE, asi que dos homonimos comparten"
        " ficha (talento, arquetipo, afinidad). Dar identidad al corpus, no heuristica (Fase B/D)",
    };
}

}   // namespace canon
}   // namespace career
