// dll/career/career_traits.h
// A FACTOR model of the driver. Each trait = common-talent loading + sub-factor shared by
// linked pairs + archetype's authored bias + the driver's OWN variance (genome) - aggression
// counterweight. The difference from a scalar model ([competitor] hangs everything off a single number with
// correlations of 0.83 to 0.94) is that the independent variance is high enough to bring correlations
// down to ~0.5, which is what's measured in real grids and what turns "everyone is the same
// driver" into "everyone has peaks and valleys".
#pragma once
#include <cstdint>
#include "career_genome.h"

namespace career {

struct TraitVector {
    double race_skill = 0, qualifying = 0, aggression = 0, defending = 0;
    double stamina = 0, consistency = 0, start_reactions = 0, wet_skill = 0;
    double tyre_management = 0, fuel_management = 0, blue_flag = 0, weather_tyre = 0;
    double avoid_mistakes = 0, avoid_forced = 0;
};

// Archetype-authored bias: where peaks and valleys are DESIGNED on purpose.
// Applied to the trait before clamping, PUSHING THROUGH THE SLACK remaining up to the ceiling (or the
// floor if negative), never linearly: a linear +0.20 on a trait already at 0.86 pins
// EVERY driver of that archetype at 1.00, and we'd lose exactly what this model exists to fix.
// Everything at 0 = a neutral archetype.
struct TraitBias {
    double race_skill = 0, qualifying = 0, aggression = 0, defending = 0;
    double stamina = 0, consistency = 0, start_reactions = 0, wet_skill = 0;
    double tyre_management = 0, avoid_mistakes = 0, avoid_forced = 0;
    // 2026-07-27 -- these two are NOT shifts like the ones above: they're the archetype's ABSOLUTE TARGET
    // (the metronome saves fuel, the charger burns it; the ice-cold driver nails the switch to wets, the rookie doesn't).
    // <=0 means "no archetype" and TraitsFor falls back to the old flat value, so behavior with no
    // archetype is identical to before. These are the two parameters that decide WHEN each driver pits: they used to be
    // flat at 0.65/0.70 across the whole grid and made everyone pit on the same lap.
    double fuel_target = 0, weather_tyre_target = 0;
    // 26/08 — DEFENDING MOVES TO AN ABSOLUTE TARGET, for the same reason as those two.
    // Measured: with the shift, the model only produced RAW defending between 0.14 and 0.41 (inverted
    // from the GT3 XML through DefendingInBand). That's why raising the ceiling from 0.50 to 0.95 on 25/08 moved
    // the values but fixed nothing: there was no range to distribute. Nobody starts from their grade — everyone
    // starts from the same place and moves a little, because the bias is an asymptotic push from a
    // common anchor. On disk: defending written from 0.24 to 0.50 against 0.22-0.90 in the human grids, and NOT ONE
    // of 2,991 drivers defending like a normal one.
    // <=0 means "no archetype" and falls to the usual path. That leaves the suite's statistical tests UNTOUCHED,
    // which compose with NEUTRAL bias, and confines the change to the real grids.
    double defending_target = 0;
    // 26/08 — AGGRESSION, likewise, and for a DIFFERENT reason than defending. Here the grades were already
    // fine: they go from 0.42 to 0.95 with `solido` at 0.60, and the 324 real ones go from 0.39 to 0.91 with
    // median 0.56. The problem was only MECHANICAL -- the model centers EVERYONE at 0.50 and then pushes
    // asymptotically, so the low tail slams into the clipping floor and the grades don't arrive.
    // Measured on disk: minimum pinned at EXACTLY 0.200 (the floor) in the four classes looked at.
    double aggression_target = 0;
    // 26/08 (afternoon) — THE REMAINING SEVEN, same pattern. Five had their grades BADLY ANCHORED and were
    // fixed with the same method as defending (preserve the order, bring level and range to the
    // 324 real ones); `start_reactions` and `wet_skill` were already fine and weren't touched.
    // The biggest bias was in the ERRORS: `solido` avoided mistakes at 0.72 when a real driver is
    // at 0.53. We'd been authoring an AI much cleaner than people -- exactly the opposite of the
    // user's request of 28/07, «in general that the AI makes mistakes».
    double consistency_target = 0, avoid_mistakes_target = 0, avoid_forced_target = 0;
    double start_reactions_target = 0, wet_skill_target = 0, tyre_management_target = 0;
    double stamina_target = 0;
};

// `craftOverride` = that person's CRAFT axis, in standard deviations, when we have it MEASURED
// (`career_attributes::MeasuredCraftFor`). nullptr —the normal case— and the genome draws it like everything else.
// It's the slot through which the 346 anchors enter the SAME model as the rest instead of living alongside it on
// another scale: their fifteen raw knobs are relative to THEIR pack, but their CRAFT is the person's and travels.
TraitVector TraitsFor(uint64_t seed, double talent, const TraitBias& bias,
                      const double* craftOverride = nullptr);

// Car families. A driver can be excellent in single-seaters and mid-pack in tin-tops: that's normal
// in the real world, and it's a source of identity no reference generator models.
enum class CarFamily { OpenWheel, GT, Prototype, Touring, Oval, Historic, COUNT };

// GLOBAL SCALE of the per-family AFFINITY. **PRODUCTION = 0.0 since 17/08** (see the value and the
// full why in career_traits.cpp). It was born as a bench switch for the ; today it's the lever
// with which the decision is reverted by changing ONE number, and that's why the mechanism has been left whole
// (zero sum, `kAffCap`, the body of `AffinityFor` and the subtraction in career_fields.cpp).
//
// CAREFUL — 1.0 IS NOT «going back to production», it's UNDOING the 17/08 decision. This comment said
// exactly that for a day and it's the trap this note closes.
//
// The reason for turning it off, measured on the ORDER criterion: with affinity, F1's worst driver
// shifted 10 places relative to his card and one of the six best fell out of the lead group; without
// it, 0 and 0. The whole F1 grid measures 0.11 of authored talent and the affinity cap was 0.12: a
// push the size of the field isn't texture, it's a lottery (customai_affinity_noise_decides_ace).
//
// AND THE OLD «-20%» MEASUREMENT IS EXPIRED. That note said that lowering affinity NARROWS the
// grid (mean width of 114 series 0.389 -> 0.309) and that's why it had to be left alone; it was measured BEFORE
// the noise cap existed (`ClampGridNoise`, bal::NOISE_SPAN_FRAC). With the cap in place the
// OPPOSITE happens: removing affinity WIDENS (Juniper_Cup 0.2510 -> 0.3000), because with less noise there's
// less to clip. See task 6 of the 2026-08-17 grid-order plan.
extern double g_affinityScale;

double AffinityFor(uint64_t seed, CarFamily f);
double TalentInFamily(uint64_t seed, double talent, CarFamily f);

// RACECRAFT BOUNDED BY DISCIPLINE (2026-08-09) -- Curve's ruleset, measured against our output.
// Why it exists: our grid was producing exactly the combination that prevents overtaking. Measured across the 100
// production XML (2,253 drivers): aggression with 37% BELOW 0.20 and **263 drivers at exactly
// 0.000** (a driver with aggression 0 never attacks), while at the same time defense with 56% ABOVE
// 0.50 (median 0.537). Little attack + lots of defense = a procession, and that's independent of any
// spread debate. Blue flags sat flat at 0.950 (100% outside its own rule).
// The trait model still decides ORDER and character within the range; this just sets the
// floor and ceiling, which is what the ruleset fixes. It's applied in ApplyDriverModel, which is where the
// car's family is known.
struct RacecraftBounds {
    double aggr_lo, aggr_hi, def_lo, def_hi, blue;
};
// 25/08 — THE CEILING GOES; THE FLOOR STAYS (user's decision).
// The floor has a measured reason and stays: a driver with aggression 0.000 NEVER ATTACKS, and that isn't
// character, it's a broken driver (there were 263 like that in production). But the CEILING did the opposite of what
// its own comment promised: it said «the model still decides the character within the bracket»
// and what came out was the whole grid stuck to an edge — median aggression 0.200 on a floor
// of 0.20, and a `guerrero` with 0.95 defending flattened at 0.50 next to a `metronomo`.
// The archetype travels with the driver and is COMPLETE: if the model says someone is a 9 out of 10
// at defending, the XML must write a 9. The ceiling stays high only to avoid the exact 1.000, which the
// community reports AMS2 treats as a special case.
inline RacecraftBounds RacecraftFor(CarFamily f) {
    // Single-seaters: less contact tolerated, they yield to being lapped sooner.
    if (f == CarFamily::OpenWheel) return { 0.20, 0.95, 0.20, 0.95, 0.77 };
    return { 0.20, 0.95, 0.20, 0.95, 0.73 };   // non-open-wheel (GT, prototype, touring, oval, historic)
}

// AGGRESSION is only bounded: its natural center is 0.50 (not anchored high, see TraitsFor), so it lands
// inside the range without bunching up -- measured, median 0.454 and not a single 0.000.
inline double AggressionInBand(double v, const RacecraftBounds& rb) {
    return v < rb.aggr_lo ? rb.aggr_lo : (v > rb.aggr_hi ? rb.aggr_hi : v);
}
// DEFENSE is RESCALED, not just bounded. It's anchored HIGH like a "more is better" trait, and a plain clamp
// left more than half the grid pinned right at the cap (measured: median 0.500 = the maximum). That's not
// the rule: the rule is a range with people spread OUT within it. The model's plausible range
// [0.05, 0.95] is mapped to [lo, hi] preserving order and spacing; the clamp remains as a net for the tails.
// Measured after the change: min 0.204, median 0.371, max 0.500.
// Lives here and not in the .cpp on purpose: the suite validates the wiring by comparing against THIS function, so
// production and tests can't diverge (same reason RaceSkillAnchored delegates to RaceSkillShaped).
inline double DefendingInBand(double v, const RacecraftBounds& rb) {
    const double u = (v - 0.05) / 0.90;
    const double r = rb.def_lo + (rb.def_hi - rb.def_lo) * u;
    return r < rb.def_lo ? rb.def_lo : (r > rb.def_hi ? rb.def_hi : r);
}

}   // namespace career
