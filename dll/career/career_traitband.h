// dll/career/career_traitband.h
// — THE HUMAN BAND. Takes each trait we write to the band in which the people who author grids
// write it, knob by knob.
//
// Where it comes from: 324 pack cards whose fifteen knobs were MEASURED (docs/career/driver_attributes.psv)
// against the 2,991 drivers our pipeline writes. Comparison of 26/08, by percentiles:
//
//   knob                     them p05/p50/p95       us p05/p50/p95
//   consistency              0,54 / 0,74 / 0,94     0,15 / 0,58 / 0,91
//   tyre_management          0,66 / 0,79 / 0,92     0,19 / 0,56 / 0,93
//   wet_skill                0,44 / 0,69 / 0,93     0,05 / 0,60 / 1,00
//   start_reactions          0,67 / 0,81 / 0,95     0,37 / 0,73 / 0,96
//   defending                0,22 / 0,48 / 0,90     0,25 / 0,36 / 0,50
//   aggression               0,39 / 0,56 / 0,91     0,20 / 0,41 / 0,62
//   blue_flag_conceding      0,86 / 0,96 / 0,98     0,00 / 0,73 / 1,00
//
// THREE DEFECTS, and all three are the same fix seen from different sides:
//
// 1. TAILS NOBODY WOULD WRITE. In the skills our p05 sinks far below theirs:
//    consistency 0.15 against 0.54, tyres 0.19 against 0.66, wet 0.05 against 0.44. No person
//    authors anyone that broken; we have 5 % of the field there.
//
// 2. FLAT WHERE THE FIGHTING IS. The other way round in the two combat knobs: their defending goes from 0.22 to 0.90 and
//    ours from 0.25 to 0.50 — **not one of our 2,991 drivers defends like a normal human**. In
//    aggression the whole band is shifted down (p95 0.62 against 0.91). Hence the races
//    come out as processions: nobody attacks and nobody defends.
//
// 3. THE BLUES ARE **NOT** A DEFECT, and this is the correction of a wrong diagnosis of mine.
//    The comparison flags them in red —them 0.86-0.98, us 0.00-1.00— and it looks like we write
//    drivers who don't let the leader through. **It's deliberate**: `BlueFlagForRole` sets 0.00 for the
//    SLOW class of a multiclass on the official guide's recommendation, so a GT lapped by an LANTERN_MASTERS_7489
//    holds its line instead of moving aside constantly and ending up alone in the gravel. It's their ROLE in the
//    race, not their character. `blue_flag_conceding` stays OUT of the mapping, and there's a test that guards it.
//
// THE RULE THAT COMES OUT OF THAT: the human band is **evidence of the usable range**, not a target to
// copy blindly. Where we know what the knob does, what we know rules — and of these fifteen knobs we
// know quite a lot. The other two defects stand on MECHANISM, not on resemblance:
//   · the consistency floor, because Reiza documents that at 0.0 the weekend roll removes
//     up to 85 % of the skill and lasts the WHOLE race: there the driver is broken, it isn't character;
//   · the start-reaction floor, because the engine itself declares 0.65 as the realistic one in
//     `ApplyAiPersonality` — a function the production path no longer calls;
//   · and the defending/aggression width, because they're the two knobs that produce wheel-to-wheel:
//     with 0.25 of range there's no fight possible and the race comes out a procession.
//
// Why the reference is them and not real telemetry (user's decision, 26/08): this is a
// game, not a template simulator. What we're after is for our grids to look like the ones
// someone who knows writes, not like a timesheet.
//
// EACH KNOB HAS ITS OWNER, and confusing them is what produced the false blues defect
// (split decided by the user, 26/08):
//
//   owner        knobs                                          who decides it
//   -------------------------------------------    -------------------------------------
//   DRIVER (12)  aggression, defending, stamina, consistency,    their character -> THIS layer
//                start_reactions, wet_skill, tyre_management,
//                fuel_management, weather_tyre_changes,
//                avoidance_of_mistakes, avoidance_of_forced,
//                qualifying_skill
//   CLASS/ROLE   blue_flag_conceding                            BlueFlagForRole: lapped vs lapper
//   POSITIONAL   race_skill                                     relative to THEIR grid, not capability
//   TEAM         the car scalars (drag, weight, power)          aren't among these fifteen knobs
//
// `qualifying_skill` is the DRIVER's and `race_skill` isn't: qualifying is a skill of the person
// —there are one-lap specialists— whereas the race pace that gets written is a position
// within their grid. ⚠️Note: the header of `driver_attributes.psv` declares BOTH as positional;
// this decision overrides it on purpose and is noted in case it needs revisiting.
// `vehicle_reliability` doesn't appear above: today it's derived from the archetype and the design one is assigned
// to the team. Known discrepancy, not touched here.
//
// The band in this file is applied ONLY to the driver's twelve.
#pragma once

namespace career {
namespace traitband {

// The twelve traits that are written and have a measured band. The order doesn't matter outside here.
enum Trait {
    kAggression = 0, kDefending, kStamina, kConsistency, kStartReactions, kWetSkill,
    kTyreManagement, kFuelManagement, kBlueFlag, kWeatherTyre, kAvoidMistakes, kAvoidForced,
    kQualifying,   // 26/08: it's the DRIVER's (user's decision) -- see the owners table below
    kTraitCount
};

// Maps a value of OURS to the one a person would write. Interpolates through the three percentile anchors
// (p05, p50, p95) and saturates at the observed extremes, so:
//   · the median is preserved (it doesn't shift the whole field),
//   · the bottom tail rises up to where theirs reaches,
//   · a narrow knob like defending gets WIDENED up to its p95.
// Out of range it saturates; it never returns the exact 0.000 or 1.000, which the community reports as
// AMS2 special cases.
double ToHumanBand(Trait t, double ours);

// The three human anchors of a trait, so the suite compares against the data and not against a
// number copied by hand into the test.
void HumanAnchors(Trait t, double& p05, double& p50, double& p95);

// 26/08 — the same three percentiles but of OUR distribution, which is the other half of the rule of
// three. It exists so the suite pins the CONTRACT and not the calibration: a test that writes «0.50 defending
// has to come out 0.90» expires as soon as the table is re-measured -- it happened four times, the same day. With this
// the test says «our p95 comes out at THEIR p95», which is true however the calibration is redone.
void OurAnchors(Trait t, double& p05, double& p50, double& p95);

} // namespace traitband
} // namespace career
