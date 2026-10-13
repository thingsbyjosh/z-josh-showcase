#pragma once
#include <vector>
// Living World — unified ELO=REP rating maths. Pure: no ImGui, no game, no globals.
// Constants in career_balance.h (career::bal::RATING_* / K_* / AI_*).
namespace career {

// Expected score of `rSelf` vs one opponent `rOpp`: 1/(1+10^((rOpp-rSelf)/DIV)).
double RatingExpected(double rSelf, double rOpp);

// AI%-fold: the opponent's EFFECTIVE rating for the duel, shifted by the difficulty slider.
// aiPct<=0 => no fold (returns rOpp), matching RepFor's "0 = no scaling" convention.
double EffectiveOpp(double rOpp, int aiPct);

// Rating change for ONE driver after a race. beat[i] = did this driver finish ahead of
// opponent i. k = total K for the race (spread across the N-1 duels). aiPct folds the opps.
double RatingDelta(double myRating, const std::vector<double>& oppRatings,
                   const std::vector<bool>& beat, int aiPct, double k);

// Whole-field deltas from a finishing order (index 0 = winner; lower index = better finish).
std::vector<double> RatingUpdateRace(const std::vector<double>& ratingsInFinishOrder,
                                     int aiPct, double k);

// q(R) in 0..1 (logistic): the "quality" feeding AI skill params and the market.
double RatingQuality(double r);

// Letter band index 0..5 (0=F .. 5=A) for display.
int RatingBand(double r);

// Dynamic K: high while provisional (first K_PROVISIONAL_RACES races), low once settled; the
// player keeps a floor of K_PLAYER_MIN so results always move the needle.
double RatingK(int racesCompleted, bool isPlayer);

// Seed a real driver's starting rating from their career_fields `order` (1=best) in a field of
// `fieldSize`, +bump if iconic. order 1 -> SEED_TOP(+bump); order fieldSize -> SEED_BOTTOM.
double RatingSeedFromOrder(int order, int fieldSize, bool iconic);

// Living World — level-anchored seed on the single global ladder. Unlike
// RatingSeedFromOrder (which spreads the same 1900..1200 band INSIDE every field -> every series'
// #1 ≈ 1900, the "wall"), this places the in-field order-spread AROUND a per-level midpoint so a
// higher-prestige series (higher ladder `level` 1..7) anchors HIGHER, while adjacent levels OVERLAP
// at the edges (a top driver in a lower series can out-rate a backmarker one tier up — the "hero in
// a bad car"). Reuses RatingSeedFromOrder's order-fraction t and the iconic bump. `level` is clamped
// to 1..7. Pure: no globals/IO.
double LevelAnchoredSeed(int level, int order, int fieldSize, bool iconic);

// ~~~07/09 — THE SAME CURVE BY PERCENTILE, for whoever does not come from a classification. `pct01`: 1 = the best
// of its level, 0 = the last. It is needed by the GENERATED rookie (`career_seats.cpp`), which until today was the only
// population in the world seeded with another rule -- `RATING_SEED_BOTTOM + talent`, with no level anchor.
// Measured before fixing it, over 20 seasons: a level-1 rookie was born with a median ELO of 1408 when the
// whole band of its level ends at 1325, that is, ABOVE the best driver of its category; the deviation was
// +251 at level 1, +127 at 2, +33 at 3 and NEGATIVE from there up. That shape —flat against a ladder—
// is the signature of two different rules, and it made level 1 worth more than 2, 3
// and 4 after twenty seasons. The comment on `CohortAnchoredSeed` says it and it goes for everyone: «a single scale for everyone, or the
// talent of a grid goes back to depending on who populates it»."
double LevelAnchoredSeedPct(int level, double pct01, bool iconic = false);

// Seed for the recurring cast (rk_*), independent of tier: the cohort settles across all
// tiers 1..7 with a single global rating, so its seed comes from its authored base_skill (the hierarchy
// from the PSV), linearly mapped CAST_SEED_BASE_LO..HI -> RATING_ROOKIE_BASE..CAST_SEED_RATING_HI (clamped).
double CastSeedFromBase(double baseSkill);

// .5 (, user decision 2026-08-10) — the cohort, ANCHORED TO ITS HOME LEVEL.
//
// Replaces `CastSeedFromBase` in the seeding. That one mapped the authored `base_skill` onto 1150..2250
// WITHOUT a level term, and that put the academy's apex (2140) above the band of the high
// classes: as soon as the real drivers find their ELO —which fixes the other half of C.5— the cohort went on to
// lead 48 of 100 grids, Ivory_Cup_10072 included. Measured with `career_test --seedlab`.
//
// The authored `base_skill` is NOT thrown away: it still rules the ORDER within the cast (who is the apex and who is
// last). What changes is the SCALE: that order is placed around the midpoint of its home tier
// (`bal::COHORT_HOME_MAX_TIER`), like any other driver in the world. They start low and rise through
// results, which is by design.
//
// `order` 1 = the best of the cast by base_skill; `castSize` = how many there are; `hot` keeps the iconic bump.
double CohortAnchoredSeed(int order, int castSize, bool hot);

} // namespace career
