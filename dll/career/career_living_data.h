#pragma once
// career_living_data.h — Living World static data (ported from an early JS prototype).
// SPONSORS: tiers, traits (coverage vs flavor), my/available catalogs + the req gate.
// MEMORIES (//): the family-tree strip + per-era detail (legends, relic/crest/inherit).
// SAMPLE data, like the rest of the overlay; the LIVE economy/era-progress comes from the engine
// bridges (career::SponsorUi*/MemoryUi* in career_season_ui.h). Reuses Col/ImU32 + std types.
#include "imgui.h"
#include <string>
#include <vector>

namespace living {

// Sponsor tiers: local<regional<national<international<elite (data_living.jsx SPONSOR_TIERS) ----
struct SponsorTier { std::string key, es, en; ImU32 color; int rank; };
extern std::vector<SponsorTier> SPONSOR_TIERS;
const SponsorTier* SponsorTierByKey(const std::string& key);   // nullptr if unknown
const SponsorTier* SponsorTierByRank(int rank);                // save-derived MY stores tier as int rank

// (removal of the old sponsors system): TraitMeta/TRAIT_META/TraitByKey, this namespace's SponsorTrait,
// SponsorGoal, SponsorDef, MY_SPONSORS/AVAILABLE_SPONSORS, and ObjProgress/GoalProgress were DELETED —
// only the old catalog/gates/UI used them (screen_sponsors.cpp, already emptied to a placeholder). SPONSOR_TIERS
// (above) STAYS: the tiers/colors still use it.

// Era memories (//): the family-tree node + era detail ----
struct MemoryDef {
  std::string id; int gen;                         // gen 0 = oldest ancestor
  std::string ancestor;                            // FIRST NAME only -> + the player's surname = the lineage
  std::string relation_es, relation_en;
  std::string era, series_es, series_en;
  std::string clsId;                               // class (display; FindClass)
  long trackHash;                                  // REAL circuit hash (CareerTrackImage) -> a real map, not procedural
  std::string trackName, trackFlag;                // circuit display name (does NOT depend on SAMPLE_TRACKS, which doesn't cover period content)
  int doy;                                          // the memory's day of year: a ±1-week window + a dot marker + "view on calendar"
  int seatNo;
  std::string gridFlavor;                          // "pure" | "cohort"
  std::vector<std::string> legends;
  std::string objective_es, objective_en;
  std::string relic_es, relic_en, crest_es, crest_en;
  long inherit, rewardRepeat;
  ImU32 color;
};
extern std::vector<MemoryDef> MEMORIES;
const MemoryDef* MemoryById(const std::string& id);
const MemoryDef* MemoryByGen(int gen);

} // namespace living
