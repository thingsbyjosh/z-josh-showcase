#pragma once
#include <string>
#include <vector>
#include <map>
#include "career_customai.h"   // GridDriver (period grid)
// Era memories + legacy. Pure: no ImGui, no game, no globals beyond the
// loaded memory table. Constants in career_balance.h (career::bal::MEM_*).
namespace career {

// One round of an era memory: a period championship event on a real date (date-gated).
struct MemoryRound {
    std::string track_name;          // canonical track key (TRACKS / ams2 catalog)
    long track_hash = 0;             // CalSlot.track_hash equivalent (handoff)
    int  doy  = 0;                   // day-of-year (1..366) on the SHARED general calendar
    int  round = 1;
};

// A named legend of the period grid — fills a works seat.
struct MemoryLegend { std::string name, country; };

// First-clear vs replay rewards: first = relic + crest + inheritance; replay = money only.
struct MemoryReward {
    std::string relic_es, relic_en;  // diegetic relic (ancestor's car/livery), shown once
    std::string crest_es, crest_en;  // family crest unlocked once
    long inherit = 0;                // first-clear cash inheritance
    long reward_repeat = 0;          // base for the annual money-only replay (scaled by performance)
};

// An era memory = one ancestor's bounded period campaign (narrative frame).
struct Memory {
    std::string id;                  // "mem_aldo" ...
    int gen = 0;                     // 0 = oldest ancestor; status chains gen N on gen N-1
    std::string ancestor;            // family identity that takes the ancestor seat
    std::string relation_es, relation_en;
    std::string era;                 // "1968"
    std::string series_es, series_en;
    std::string cls;                 // class key (era classification; e.g. "groupc")
    int seat_no = 0;                 // ancestor's car number (a privateer seat)
    std::string grid_flavor;         // "pure" (legends only) | "cohort" (legends + privateers)
    std::string objective_es, objective_en;
    std::vector<MemoryLegend> legends;
    std::vector<MemoryRound>  rounds;
    MemoryReward reward;
    int unlock_gen_override = -1;    // 1 => use gen for the MEM_UNLOCK_RATING bar; else this index
};

// Status of a memory node (mirrors the prototype statusOf in data_living.jsx).
enum class MemoryStatus { Locked, Available, Completed };

// Era classification: which bygone era a class belongs to (memory candidate) ----
// "60s70s" | "80s" | "90s00s" | "" (modern ladder / fictional / timeless -> NOT a memory).
std::string EraForClass(const std::string& classKey);
// true if the class is locked to a real bygone era (era-memory candidate).
bool IsMemoryClass(const std::string& classKey);

// Memory table (loaded from the memories data file) ----
bool LoadMemories(const char* path);                 // parses the JSON; false if it won't open/parse
const std::vector<Memory>& AllMemories();            // the loaded table (empty until LoadMemories)
const Memory* MemoryById(const std::string& id);     // nullptr if not loaded / unknown
int MemoryCount();                                   // diag/test

// Node status / gating (double-gate) ----
// Level-unlock bar for a memory: the player rating threshold from MEM_UNLOCK_RATING[gen-bucket].
double MemoryUnlockRating(const Memory& m);
// statusOf (prototype): Completed if done[id]>0; else Available if the chain predecessor is cleared AND
// the player meets the level bar; else Locked. `done` = memory_id -> times cleared (save). gen 0 needs
// only the level bar. `playerRating` = the unified career rating (career_rating).
MemoryStatus MemoryStatusOf(const Memory& m, const std::map<std::string,int>& clearedCount,
                            double playerRating);
// Is THIS round raceable right now? Requires the memory unlocked (level) AND the general calendar
// cursor has reached the round's doy (date-gate). Pure; the calendar passes cursorDoy.
bool MemoryRoundOpen(const Memory& m, const MemoryRound& r, const std::map<std::string,int>& clearedCount,
                     double playerRating, int cursorDoy);

// Rewards: first clear vs annual replay ----
// playedCount = times this memory was cleared before this run (0 => first clear). objectiveScore in
// 0..1 = how well the objective was met (1 = crushed). Returns the cash to grant. First clear pays the
// full inheritance; replays pay reward_repeat scaled in [MEM_REPLAY_MIN..MAX_MULT] by objectiveScore.
long MemoryPayout(const Memory& m, int playedCount, double objectiveScore);
// First clear ONLY grants the relic + crest + inheritance (content, never XP/rating).
bool MemoryGrantsContent(int playedCount);   // true iff playedCount == 0

// Period grid: legends in works seats + privateer cohort; the ancestor is a privateer ----
// Builds the era field as GridDriver rows for the memory's class liveries (override the modern grid).
// `liveries` = LiveriesForClass(memory's class) (caller resolves). The ancestor (seat_no/ancestor name)
// takes a privateer slot; "pure" memories add no privateers (legends only, ancestor among them).
std::vector<GridDriver> ComposeMemoryGrid(const Memory& m, const std::vector<std::string>& liveries);

} // namespace career
