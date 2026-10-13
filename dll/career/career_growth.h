#pragma once
// ===================================================================================================
// — GROWTH AND DECLINE: THE CURVE IS EACH ONE'S OWN (12/08)
// ===================================================================================================
//
// Until today everyone shared the SAME curve (`AgeCurveMultiplier`): x0.82 at 18, ramp to x1.00 at
// 27, plateau until 32 and decline after. That 18% of rise is ~270 rating points — close to a
// whole ladder level —, so growing was already worth a lot, but it distinguished NOBODY from anybody. And in the
// other direction it had a worse consequence: the only way to decay in this world was to have a birthday.
//
// What puts it in its place, with the decisions already taken by the user on 11/08:
//
//   DECISION 1 — THE CEILING IS THE PERSON'S, THE SHAPE IS THE ARCHETYPE'S. Each one has his ceiling; the
//     archetype says HOW and WHEN he approaches it, not where it ends. It's FIFA's POT model. And here the
//     ceiling doesn't even need inventing: `BaseTalent(id)` IS ALREADY what a driver is worth at his peak, because the
//     age curve multiplies it. What was missing wasn't the ceiling — it was the road to it.
//
//   DECISION 2 — THE CIRCUMSTANCE BENDS THE CURVE WITHOUT LOWERING THE CEILING. Three years in a bad car really
//     stall you; if a good seat arrives at 26, you rise toward your ceiling again. TIME is lost, not the
//     ceiling: that's why progress can GO BACK and rise again, while `BaseTalent` is never touched.
//     The irreversible part is already set by age (`AgeDeclinePart`), so the circumstance doesn't need to be
//     permanent to hurt.
//
// AND THIS IS WHAT UNLOCKS RELEGATION. Measured on 12/08: with the universal curve, the condition for
// losing the seat boiled down to "your authored talent was already below your class's average" — i.e.
// half the grid was IMMOVABLE however it drove, because the maximum punishment for results (70
// points) is exactly the class's tolerance and the two cancel out. With personal progress, a driver
// who has been getting thrashed for years really moves away from his ceiling, crosses the bar and drops a category.
//
// AND THE LOOP CLOSES BY ITSELF, which is the nice part: on dropping a category he's compared with a weaker
// field, his circumstance becomes neutral and he stops falling. No artificial brake is needed — the
// relegation IS the brake. (Even so there's a hard floor, in case a strange world leaves someone with no place to
// drop to.)
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace career {

struct SaveData;

// The five ways of evolving (, user decision 11/08). The archetype is the STARTING
// TENDENCY, not a sentence: the real curve responds to what happens to him, and this only says where it tends
// when nothing pushes it.
enum class GrowthShape {
    Meteorico = 0,   // rises very fast and young
    Progresivo,      // rises steadily throughout the career
    Tardio,          // starts weak and explodes around 26
    Meseta,          // reaches his ceiling early and stays
    SeApaga,         // promise that never pans out and declines
};

// A driver's starting shape. Deterministic by his id: the same driver always has the same tendency
// in the same save and in all of them. It isn't drawn per season — that would be noise, not an arc.
//
// It's DERIVED instead of curated because there's no data: `PoolDriver.growth_archetype` exists but carries inert
// legacy values (`anchor_high`/`anchor_low`/`variable`, retired on 02/07). The day there's curation,
// it comes in through here without touching anything else.
GrowthShape ShapeFor(const std::string& driverId);
const char* ShapeName(GrowthShape s);

// How far he'd advance toward his ceiling this year if NOTHING pushed him: the pure shape, in progress units
// (0..1). It's what the circumstance multiplies.
double ShapeStep(GrowthShape s, int age);

// A driver's shape ACCORDING TO HIS LEVEL: the archetype whose percentile bracket contains his
// talent is looked up. It's what makes the PSV's `forma` column mean something — without this, a "generacional" could
// come out meseta. It stores nothing: it's derived, so there's no save to migrate. Without a table, it falls back to `ShapeFor`.
GrowthShape ShapeForTalent(const std::string& driverId, double talent);

// A driver's PROGRESS toward his ceiling, 0..1. Absent = the universal rise at his age, which is what makes
// an old save (or a world that hasn't run yet) give EXACTLY the same number as before.
double GrowthProgress(const SaveData& save, const std::string& id, int age, int ageShift);

// 18/08 — The same computation, reachable from the COMPOSITION. `ComposeSeasonGrid` receives slices of the
// save (ratings, races) and never the whole save; this overload respects that contract. The version with
// SaveData delegates here, so they can't give different numbers.
double GrowthProgress(const std::map<std::string, double>& curve, const std::string& id, int age, int ageShift);

// ===================================================================================================
// NAMED CAREER ARCHETYPES (14/08, user decision)
// ===================================================================================================
//
// "Take archetypes of very well-known drivers and give them a probability of showing up. Almost named
// archetypes: this one is a Verstappen, this one is a Hamilton... and then an Ocon, of those you have more."
//
// It replaces inventing a rookie's talent with a formula bounded by his entry level — which made
// a generated one UNABLE to be a star even with the best roll, and that's why the head of the F1 grid
// degraded 127 points in twenty seasons while the tail only lost 77.
//
// The band is a PERCENTILE of the LIVE corpus of real talents, not a pinned number: that way this clones the
// shape of the world that exists and doesn't go stale when a pack comes in. And it's a BRACKET, not a value, so as
// not to break ("the ceiling is the PERSON's; the archetype sets the shape, not the end"): two Verstappens
// aren't identical and scouting still has uncertainty.
struct GrowthArchetype {
    std::string id, referencia, nota;
    double pct_min = 0.0, pct_max = 1.0;
    GrowthShape forma = GrowthShape::Progresivo;
    int peso = 0;
};

bool LoadGrowthArchetypes(const char* path);   // false if the file doesn't open; leaves the table empty
int  GrowthArchetypeCount();
const std::vector<GrowthArchetype>& GrowthArchetypes();

// The archetype a rookie gets, by weight and deterministic by `seed`. Empty table -> nullptr, and the
// generator falls back to its usual formula (additive: without the PSV nothing changes).
const GrowthArchetype* ArchetypeForRookie(unsigned long seed);

// A ROOKIE'S TALENT, CLONED FROM THE WORLD. Returns a talent on the SAME anchored scale as
// `BaseTalent` (level anchor + his spot within it), taken from the percentile his archetype assigns him over the
// live corpus. `outArch` receives the chosen archetype (may be nullptr).
//
// Returns <= 0 if there's no corpus or table: then the caller uses its previous formula.
// ~~~02/09 — `tier` = the tier where he is BORN. >0 reads the archetype's percentile over the people of that
// tier (a clone of a generational in GT3 is the best in GT3, not an F1 driver parked there); 0 or a
// tier with fewer than 50 cards falls back to the global corpus, which is the usual behavior.
double RookieTalentCloned(unsigned long seed, const GrowthArchetype** outArch, int tier = 0);
const std::vector<double>& TalentCorpusForTier(int tier);

// The live corpus of real talents, SORTED. It's built once and cached; it's where the
// percentiles come from. Exposed for diagnostics.
const std::vector<double>& TalentCorpus();
void InvalidateTalentCorpus();   // the data has changed (tests): rebuild it

// 18/08 — THE TWIN OF `RookieTalentCloned` ON THE OTHER SCALE.
// `TalentCorpus`/`RookieTalentCloned` live in RATING (BaseTalent, anchored by level). The grid
// composition, on the other hand, reads the 0..1 card (`AuthoredProfile::talent`), and an invented driver had none:
// it fell back to the CURVE point of his seat, i.e. to the car's pace. These three close that gap.
// The percentile is SHARED with the rating clone on purpose: the same person takes the same spot on the
// two scales, which is how you avoid someone being born with a star's ELO and a filler card.
const std::vector<double>& ProfileTalentCorpus();          // 0..1 cards of the authored world, sorted
extern double g_sueloSubidaMedicion;   // ~~~02/09: factor floor while you rise (<0 = production)
// ~~~07/09: how much the circumstance weighs (`GROWTH_BEND`), to MEASURE what happens to the world if
// development stops reading results and grows by the plain curve -- the FIFA model, where the card
// grows by potential and age and it's the CLUB that sells you if you don't perform. <0 = production (sweep only).
extern double g_growthBendMedicion;
extern std::string g_trazaGrowth;   // ~~~02/09: id to trace in AdvanceGrowth (empty = nothing)
double RookiePercentile(unsigned long seed);               // his spot within his archetype's bracket
double RookieProfileTalentCloned(unsigned long seed);      // 0.0 = no corpus -> the caller decides
// 18/08 — THE TWIN OF `InvalidateTalentCorpus`, and not as decoration: when plugging `RookieProfileTalentCloned` into
// `career_fields.cpp::PersonForOccupant` (the cascade that resolves ANY occupant), the first suite test to
// touch a generated one builds this cache -- with whatever data is loaded AT THAT MOMENT, which
// may be a tiny fixture or none. Without invalidating, it stays that way for the rest of the process: the dedicated test
// (`test_generated_driver_gets_a_profile_of_its_own`) and any other that reloads data afterwards see a
// foreign corpus. `canon_load_prod()` calls it after each load.
void InvalidateProfileTalentCorpus();

struct GrowthReport {
    int movidos = 0;      // drivers whose progress changed
    int subieron = 0, bajaron = 0;
    int techo = 0;        // those already at their ceiling (progress 1.0)
    double medio = 0.0;   // world's average progress
};

// Advances the arc of the WHOLE world by one season. The circumstance comes from the results that
// `RunWorldSeason` has just produced: what he has done (ELO) is compared with what he promises (his capability), standardized
// over his own series — the same projection used by the decision on whether he stays, and for the same reason (the two
// magnitudes don't live on the same scale).
//
// `skip` = drivers not to touch (the player). Deterministic, no RNG. Mutates only `save.driver_curve`.
GrowthReport AdvanceGrowth(SaveData& save, int worldYear,
                           const std::set<std::string>& skip = std::set<std::string>());

}  // namespace career
