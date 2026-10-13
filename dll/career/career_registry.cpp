// career_registry.cpp — the construction of the v2 identity catalog ().
#include "career_registry.h"

#include <algorithm>

#include "career_series.h"
#include "career_fields.h"
#include "career_seats.h"       // FieldOccupantId / PinnedOccupantId: the id minting that ALREADY exists
#include "career_data_json.h"   // the drivers.json pool
#include "career_genome.h"

namespace career {
namespace registry {

namespace {

std::map<std::string, canon::DriverIdentity> g_reg;
// folded key -> ids that share it, in registration order. A repeated name is normal in a corpus of
// 1,700 people (two Bertrand Gachots, two Stéphane Ortellis), and that is why the tie-break is by country.
std::map<std::string, std::vector<std::string>> g_byName;
Stats g_stats;
bool  g_loaded = false;

canon::DriverIdentity& Slot(const std::string& id) {
    canon::DriverIdentity& di = g_reg[id];
    if (di.driver_id.empty()) { di.driver_id = id; di.genome_seed = GenomeSeed(id); }
    return di;
}

void IndexName(const canon::DriverIdentity& di) {
    if (di.display_name.empty()) return;
    const std::string key = FoldDriverName(di.display_name);
    if (key.empty()) return;
    std::vector<std::string>& v = g_byName[key];
    if (std::find(v.begin(), v.end(), di.driver_id) == v.end()) v.push_back(di.driver_id);
}

// The authored corpus is the AUTHORITY over `origin` and, when it carries them, over archetype and family. It is applied
// the same to whoever comes from the pool and to whoever only exists because they race: the question "did this person exist?" does not
// depend on which file they appear in.
// Returns true if the card existed (for the `authored` count).
bool ApplyAuthored(canon::DriverIdentity& di, bool* outReclassified) {
    const AuthoredProfile* ap = AuthoredProfileFor(di.display_name);
    if (!ap) return false;
    // THE AMBIGUITY OF `kind` IS UNDONE HERE. `is_real=0` means invented character, and the catalog
    // had it as real in 385 cases. «the 572 known fictional ones are not labelled as real».
    const canon::Origin nuevo = ap->is_real ? canon::Origin::Real : canon::Origin::Fictional;
    if (outReclassified && di.origin == canon::Origin::Real && nuevo == canon::Origin::Fictional)
        *outReclassified = true;
    di.origin = nuevo;
    if (!ap->archetype.empty()) di.archetype = ap->archetype;
    canon::Family fam = canon::Family::OpenWheel;
    if (canon::FamilyFromName(ap->affinity, fam)) { di.primary_affinity = fam; di.affinity_known = true; }
    return true;
}

}   // namespace

int Load() {
    Clear();

    // (1) The pool: those that already have their own id -----------------------------------------------------
    for (const std::string& id : PoolDriverIds()) {
        const PoolDriver* pd = DriverById(id);
        if (!pd) continue;
        canon::DriverIdentity& di = Slot(id);
        di.display_name = pd->name;
        di.country      = pd->country;
        // the two axes, and for the first time truly separated. `kind` only decides the ROLE —which is what it
        // really knows—; the origin will be stated by the authored corpus one step below.
        di.population_role = (pd->kind == "roster") ? canon::PopRole::Cohort : canon::PopRole::Incumbent;
        di.origin          = (pd->kind == "real")   ? canon::Origin::Real    : canon::Origin::Fictional;
        di.birth_year           = pd->birth_year;
        di.authored_retire_year = pd->retire_year;
        di.archetype            = pd->growth_archetype;   // inert legacy; data, not rule
        g_stats.from_pool++;
    }

    // (2) Those that only exist because they RACE --------------------------------------------------------
    // Their id comes from FieldOccupantId/PinnedOccupantId, which is the minting already used by the seats and the
    // seeding. Minting one of our own here would create the second identity that this phase is here to eliminate.
    for (const auto& sd : AllSeries()) {
        for (const auto& fd : FieldForSeries(sd.id)) {
            const std::string id = FieldOccupantId(fd);
            if (id.empty()) continue;
            if (g_reg.count(id)) continue;
            canon::DriverIdentity& di = Slot(id);
            di.display_name = fd.name;
            di.country      = fd.country;
            di.origin       = canon::Origin::Fictional;      // until the corpus says otherwise
            di.population_role = canon::PopRole::Incumbent;   // has a place on a grid: is a starter
            g_stats.from_field++;
        }
        for (const auto& pd : PinnedRosterForSeries(sd.id)) {
            const std::string id = PinnedOccupantId(pd);
            if (id.empty()) continue;
            if (g_reg.count(id)) continue;
            canon::DriverIdentity& di = Slot(id);
            di.display_name = pd.name;
            di.country      = pd.country;
            di.origin       = pd.real ? canon::Origin::Real : canon::Origin::Fictional;
            di.population_role = canon::PopRole::Incumbent;
            di.archetype    = pd.archetype;
            g_stats.from_field++;
        }
    }
    // (2b) 16/09 — THE LIVERY PACKS ARE ALSO IDENTITIES, and the index did not know them ------
    //
    // `IdForName` is the only thing that stops step (3) from minting a SECOND identity for someone who already
    // exists. Its index was built from the pool, the field and the pinned — not from the packs. And since the id is
    // minted from the RAW name (`PackDriverId` and `FieldOccupantId` fall back to an FNV hash of the name when it is not
    // in the pool), a person who only lives in a pack ended up with TWO ids as soon as the corpus
    // wrote them with another spelling: `pack_drivers.psv` has «Cacá Bueno» and the corpus wrote «Caca Bueno», which
    // fold the same but hash differently. Four like that: Cacá Bueno, Guto Negrão, Xandy Negrão and José Carlos
    // Dias. The ghost sat down, nobody knew them, and their car did not go out on track.
    //
    // IT IS POPULATION-NEUTRAL, and it was measured BEFORE writing it: of the 1,441 people in the packs, 1,440
    // WERE already RESERVE identities — step (3) registered them from their corpus card — and only 1
    // was also in the pool. So this adds nobody to and removes nobody from the pool of the  it changes the id of
    // the same ones, from the hash of the corpus spelling to that of the pack spelling, which is the one that carries COUNTRY.
    //
    // That is why the role is `Reserve` and not `Incumbent`: a pack driver is, by construction of
    // `gen_pack_drivers.py`, someone who is NOT in `series_rosters.psv` nor in `series_fields.psv`, which is
    // exactly what  calls reserve. Marking them as starters would shrink the pool from 1,632 to 192 and the
    //  would end up inventing whoever it takes from there today.
    //
    // DISCARDED ALTERNATIVE (decision of 16/09): mint the id from the FOLDED name instead of the raw one. It closes
    // the same gap and also closes it for good, but **it moves keys that are already written in saved
    // games** (`seat_occupant`, `driver_ratings`, `retired`…), so it would require migrating saves. Not in this
    // version.
    for (const auto& sd : AllSeries()) {
        for (int k = 0; ; ++k) {
            const PackDriver* pk = PackDriverForClassSlot(sd.class_name, k);
            if (!pk) break;
            const std::string id = PackDriverId(*pk);
            if (id.empty() || g_reg.count(id)) continue;
            canon::DriverIdentity& di = Slot(id);
            di.display_name    = pk->name;
            di.country         = pk->country;           // the pack DOES carry a flag; the corpus did not have it
            di.origin          = canon::Origin::Real;   // until the corpus says otherwise (step 3)
            di.population_role = canon::PopRole::Reserve;
            g_stats.from_pack++;
        }
    }

    // The cohort, in case some roster row was not in the pool.
    for (int t = 1; t <= 7; t++)
        for (const auto& r : RosterForTier(t)) {
            if (r.id.empty()) continue;
            canon::DriverIdentity& di = Slot(r.id);
            if (di.display_name.empty()) { di.display_name = r.name; di.country = r.country;
                                           di.archetype = r.archetype; g_stats.from_field++; }
            di.origin          = canon::Origin::Fictional;   // the cohort is invented by definition ()
            di.population_role = canon::PopRole::Cohort;
            if (di.birth_year == 0)           di.birth_year = r.birth_year;
            if (di.authored_retire_year == 0) di.authored_retire_year = r.retire_year;
        }

    // The name index is built HERE and not at the end: step (3) asks it who
    // lacks a card, and a half-built index would answer that nobody is in it.
    for (const auto& kv : g_reg) IndexName(kv.second);

    // (3) The authored corpus rules over the ORIGIN ---------------------------------------------------
    for (auto& kv : g_reg) {
        bool recl = false;
        if (ApplyAuthored(kv.second, &recl)) g_stats.authored++;
        if (recl) g_stats.reclassified++;
    }
    // And the authored fictional ones that do NOT race on any grid today are not lost either: without registering
    // them, a character who enters through the market tomorrow would be born with no card and with a guessed `origin`. The id
    // is the same one FieldOccupantId would give them, so if they appear in a field tomorrow, they recognize each other.
    for (const auto& kv : AllAuthoredProfiles()) {
        const AuthoredProfile& ap = kv.second;
        if (ap.name.empty()) continue;
        if (!IdForName(ap.name).empty()) continue;              // already there, via pool or grid
        FieldDriver fake; fake.name = ap.name;                  // same minting, no country (the corpus does not carry it)
        const std::string id = FieldOccupantId(fake);
        if (id.empty() || g_reg.count(id)) continue;
        canon::DriverIdentity& di = Slot(id);
        di.display_name    = ap.name;
        di.origin          = ap.is_real ? canon::Origin::Real : canon::Origin::Fictional;
        // ~~~29/08 — RESERVE, NOT STARTER. This loop registers precisely the people who **do not race on any
        // grid today** (the comment above says so), and it was marking them `Incumbent`, i.e. starter.
        // Someone without a seat is not a starter: they are the RESERVE, which is the role already defined
        // (`PopRole::Reserve`) and which until today **was not assigned anywhere** -- the counter existed in
        // `Stats` and was not even printed, so the gap was not visible.
        //
        // Why it matters beyond the label: the  wants a FREE-AGENT POOL and today the pool
        // of candidates is `WorldOccupants`, i.e. ONLY those who already have a seat (see the 14/08 note in
        // `career_seats.cpp`). With nobody marked as reserve, the rule «a driver under a valid contract is not
        // for sale» cannot be applied: the intersection comes out empty. This is the first step, and it does not
        // change behavior by itself -- nobody branches on `Incumbent`, only on `Cohort`.
        di.population_role = canon::PopRole::Reserve;
        di.archetype       = ap.archetype;
        canon::Family fam = canon::Family::OpenWheel;
        if (canon::FamilyFromName(ap.affinity, fam)) { di.primary_affinity = fam; di.affinity_known = true; }
        g_stats.authored++;
        IndexName(di);
    }

    // (3b) 23/09 — THE CURATED RESERVE: real age and whether still active ---------------------------------
    // Until today the reserve entered the world without a birth year (no file carried it): it did not age, did not
    // retire, and a champion retired in 2010 was seated the same as an active driver. Each person in the
    // reserve receives here what docs/career/reserve_people.psv says, curated with a source; whoever is not there or comes out as
    // «unknown» is left uncurated, and the cascade does not seat them (career_seats, ReservaEntraEnParrilla).
    for (auto& kv : g_reg) {
        canon::DriverIdentity& di = kv.second;
        if (di.population_role != canon::PopRole::Reserve) continue;
        const ReservePerson* rp = ReservePersonFor(di.display_name, di.country);
        if (!rp) continue;
        di.reserve_status = rp->status;
        if (di.birth_year == 0 && rp->birth_year > 0) di.birth_year = rp->birth_year;
        if (di.country.empty() && !rp->country.empty()) di.country = rp->country;
        g_stats.reserve_curated++;
    }

    // (4) Count ------------------------------------------------------------------------------------
    const std::map<std::string, AuthoredProfile>& corpus = AllAuthoredProfiles();
    for (const auto& kv : g_byName) {
        if (kv.second.size() <= 1) continue;
        g_stats.name_collisions++;
        // All the identities that share the name also share the card: there is no way to
        // split it, because the corpus does not know whose it is.
        if (corpus.count(kv.first)) g_stats.authored_shared += (int)kv.second.size();
    }
    for (const auto& kv : g_reg) {
        const canon::DriverIdentity& di = kv.second;
        g_stats.total++;
        (di.origin == canon::Origin::Real ? g_stats.real : g_stats.fictional)++;
        switch (di.population_role) {
            case canon::PopRole::Cohort:   g_stats.cohort++;    break;
            case canon::PopRole::Reserve:  g_stats.reserve++;   break;
            case canon::PopRole::Overflow: g_stats.overflow++;  break;
            default:                       g_stats.incumbent++; break;
        }
        if (di.affinity_known) g_stats.affinity_known++;
    }
    g_loaded = true;
    return g_stats.total;
}

void Clear() { g_reg.clear(); g_byName.clear(); g_stats = Stats(); g_loaded = false; }
int  Count()  { return (int)g_reg.size(); }
bool Loaded() { return g_loaded; }
Stats LastStats() { return g_stats; }

const canon::DriverIdentity* ById(const std::string& id) {
    auto it = g_reg.find(id);
    return it == g_reg.end() ? nullptr : &it->second;
}

std::string IdForName(const std::string& name, const std::string& country) {
    if (name.empty()) return std::string();
    auto it = g_byName.find(FoldDriverName(name));
    if (it == g_byName.end() || it->second.empty()) return std::string();
    if (it->second.size() == 1 || country.empty()) return it->second.front();
    // Two people with the same name: country rules. With no match, the first in registration order —
    // deterministic, and the same criterion as DriverIdForName, so there are not two answers.
    for (const std::string& id : it->second) {
        const canon::DriverIdentity* di = ById(id);
        if (di && di->country == country) return id;
    }
    return it->second.front();
}

const canon::DriverIdentity* ByName(const std::string& name, const std::string& country) {
    const std::string id = IdForName(name, country);
    return id.empty() ? nullptr : ById(id);
}

std::vector<std::string> Ids() {
    std::vector<std::string> v; v.reserve(g_reg.size());
    for (const auto& kv : g_reg) v.push_back(kv.first);   // std::map -> already in order
    return v;
}

const canon::DriverIdentity* Register(const canon::DriverIdentity& in) {
    if (in.driver_id.empty()) return nullptr;
    auto it = g_reg.find(in.driver_id);
    if (it != g_reg.end()) return &it->second;   // append-only: the catalog rules over the save
    canon::DriverIdentity di = in;
    di.genome_seed = GenomeSeed(di.driver_id);   // the genome ALWAYS from the id, never from what the registration brings
    canon::DriverIdentity& slot = g_reg[di.driver_id] = di;
    IndexName(slot);
    return &slot;
}

}   // namespace registry
}   // namespace career
