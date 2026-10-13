#pragma once
#include <string>

// ===================================================================================================
// THE POOL'S CATEGORY OF ORIGIN (Josh's decision, 08/09, via the contracts session: spec , E7)
// ===================================================================================================
//
// The ~1,500 people with a rating and no seat (the "pool") used to enter the world through the grid in which
// they appeared listed, and from there came their level, their entry ELO and their affinity. Josh decided that that is not
// DEDUCES: it is CURATED per person -- «which category does it belong to, where would it be today» -- and is read for two things:
// seating it (career_seats: the person enters through THEIR class, and only through it or its family at the same level) and its
// affinity (career_seats::DriverAffinity: that of its class of origin, ahead of that of the series where
// it lands). The curation is done by agents (C2 workflow) over the starter list in
// docs/medicion/pool_origin_input.psv, and arrives as docs/career/pool_origin.psv.
//
// SIBLING FILE of driver_profiles.psv, same key: name_hash = PoolOriginHash(driver_id) (FNV-1a-32).
// 0.7.0 port: in f0 that function was called `DriverProfileHash` and lived in `career_profiles`, a module
// that 0.7.0 retired for having not a single consumer in production. The hash is THE SAME (the curation was
// done with it) and it moves here, which is where it now actually has one.
// It is NOT a column of pack_drivers.psv (generated; regenerating it already deletes 114 rows).
//   name_hash|name|home_class|confidence|note
//   home_class = EXACT class_id (class_name from docs/series.psv) · confidence = alta|media|baja (optional)
//   note = free text (optional); if it contains «era», «retirado/a» or «leyenda» the person does NOT sit in the
//   modern world (stays for its era).
// Absent = the world as before (D4: you are worth where you sit). Optional on purpose: nothing breaks if it is missing.
// ===================================================================================================

namespace career {

struct PoolOrigin {
    std::string home_class;   // exact class_id from the vocabulary (class_name from series.psv)
    std::string name;         // as the curator wrote it (diag only; the key is the hash)
    std::string note;
    int  confidence = 0;      // 0 no data · 1 low · 2 medium · 3 high
    bool era = false;         // the note leaves it in its era: it does not enter the modern world
};

// Loads docs/career/pool_origin.psv (key name_hash). false if it does not open (empty table, no crash).
// Lines with < 3 fields, header (col0 == "name_hash") or non-numeric key are skipped. Replaces what was loaded.
bool LoadPoolOrigin(const char* path);
int  PoolOriginCount();

// 32-bit FNV-1a over the driver_id: the key `pool_origin.psv` was curated with.
unsigned PoolOriginHash(const std::string& driverId);

// The category of origin of an id (via PoolOriginHash(id)), or nullptr if it is not curated.
const PoolOrigin* PoolOriginFor(const std::string& driverId);
const PoolOrigin* PoolOriginForHash(unsigned nameHash);

// 0.7.0 port: `PoolOriginLevel` (the ladder level of the class of origin) used to live here. In f0 it is read by
// `career_plaza` (contracts) and `career_simrunner` (measurement harness), and neither of them comes in: here it
// would be left without a single production consumer. It comes back with whoever needs it; it is four lines over
// `AllSeries()`.

}  // namespace career
