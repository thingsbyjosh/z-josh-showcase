#pragma once
// career_registry — ONE identity per person (, , `career_driver_registry`).
//
// THE PROBLEM IT CLOSES. Today a person lives split across five partial representations —
// `PoolDriver`, `FieldDriver`, `PinnedDriver`, `RosterDriver`, `AuthoredProfile`— and none is the
// authority. Worse: the catalog only knows how to tell `real` from `roster`, so **`kind=real` today means
// "not from the cohort", not "is a real person"**. Measured over the repo corpus: of the 572 drivers
// that `f3_authored_profiles.psv` declares INVENTED (`is_real=0`), **385 are in `drivers.json` marked
// `kind=real`**. With that ambiguity, different rules cannot be imposed on real drivers, cohort, reserve and
// generated ones — which is everything that  and  ask for
//
// WHAT IT DOES. It walks the sources in a declared order of authority and produces one `canon::DriverIdentity`
// per person, with `origin` and `population_role` as TWO distinct axes ():
//
//   1. `drivers.json`                 the catalog that carries the IDs. Name, country, birth, retirement, kind.
//   2. `f3_authored_profiles.psv`     THE AUTHORITY OVER `origin`: its `is_real` column is the only place
//                                     in the repo that tells a real person from a character. Also archetype
//                                     and family. Joined by FOLDED name (FoldDriverName).
//   3. pinned fields / rosters        the people who race and are not in the pool. Their id comes from
//                                     `FieldOccupantId`/`PinnedOccupantId`, NOT from a minting of its own:
//                                     minting another id would be creating the second identity that this phase exists
//                                     to eliminate.
//
// WHAT IT DOES **NOT** DO, and it has to be written down (, scaffolding with an expiry date):
//   · It does not change where the grid COMPOSITOR resolves the id. Doing so is the fix measured on 09/08
//     that lowers the unseeded rows from 792 to 153 — and which, on its own, puts the cohort ahead of the
//     real drivers (Ivory_Cup_10072: Giaffone/Girolami/Rossi -> Vega/Mori/Rocha). That comes in **.5**, when
//     the cohort is anchored to the level, and not before.
//   · It does not unify the seeding SCALE (`CastSeedFromBase` still has no level term). Also C.5.
//   · It does not touch `PoolDriver` or its consumers: they coexist while they migrate.
//
// Pure except that it requires the tables to be already loaded (the pool, the authored corpus, series/fields/rosters), just
// like the rest of the engine. Deterministic: ordered traversals, no RNG, no clock.
#include <string>
#include <vector>
#include <map>
#include "career_canon.h"   // canon::DriverIdentity — the type belongs to  and is not duplicated here

namespace career {
namespace registry {

// Builds (or rebuilds) the registry from the loaded tables. Idempotent: calling it twice with the
// same data leaves the same registry. Returns the number of identities. Registrations made with Register() are
// LOST on reload, which is correct: a driver invented in one game does not exist in the next.
int Load();

int Count();
bool Loaded();
void Clear();

// Identity by stable id; nullptr if it is not there.
const canon::DriverIdentity* ById(const std::string& id);

// THE name -> id resolution, and the only one. Joins by folded name, breaks ties by country when several
// share a name, and returns "" if nobody fits. It is the same chain that `FieldOccupantId` walks by hand
// today in three different layers.
std::string IdForName(const std::string& name, const std::string& country = std::string());
const canon::DriverIdentity* ByName(const std::string& name, const std::string& country = std::string());

// All the ids, in deterministic (lexicographic) order.
std::vector<std::string> Ids();

// Hot registration for whoever does not exist in any table: the living world's rookies (`gn_*`) and the
// tail occupants (`fl_*`). Append-only within the life of the registry; an id already present is NOT overwritten
// (the catalog rules over what the game invents). Returns the final identity, never nullptr.
const canon::DriverIdentity* Register(const canon::DriverIdentity& di);

// Count of the last load. It is the snapshot the census compares between phases; `reclassified` is the number
// that this phase exists to move.
struct Stats {
    int total = 0;
    int real = 0, fictional = 0;
    int incumbent = 0, cohort = 0, reserve = 0, overflow = 0;
    int from_pool = 0;          // came with their own id from drivers.json
    int from_field = 0;         // only exist because they race (id minted by FieldOccupantId)
    int from_pack = 0;          // ~~~16/09: only live in a livery pack (id minted by PackDriverId)
    int authored = 0;           // have a card in the authored corpus
    int reclassified = 0;       // were marked `kind=real` and the corpus says they are INVENTED
    int affinity_known = 0;     // affinity from the data, not deduced
    int name_collisions = 0;    // folded names that point to more than one id (broken by country)
    // And the consequence of those namesakes, which is a CONTENT failure and not a code one: the authored corpus
    // is indexed only by name —it has no country column—, so two different people who share a
    // name receive the SAME card: same talent, same archetype, same affinity. This counter tells
    // how many identities this is happening to. What closes it is giving the corpus identity, not a heuristic.
    int authored_shared = 0;
    int reserve_curated = 0;    // 23/09: reservists with a row in docs/career/reserve_people.psv
};
Stats LastStats();

}   // namespace registry
}   // namespace career
