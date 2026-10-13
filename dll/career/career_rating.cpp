#include "career_rating.h"
#include "career_balance.h"
#include <cmath>

namespace career {

double RatingExpected(double rSelf, double rOpp) {
    return 1.0 / (1.0 + std::pow(10.0, (rOpp - rSelf) / bal::RATING_DIV));
}

double EffectiveOpp(double rOpp, int aiPct) {
    if (aiPct <= 0) return rOpp;
    return rOpp + bal::AI_RATING_PER_PCT * (double)(aiPct - 100);
}

double RatingDelta(double myRating, const std::vector<double>& oppRatings,
                   const std::vector<bool>& beat, int aiPct, double k) {
    const size_t n = oppRatings.size();
    if (n == 0 || beat.size() != n) return 0.0;   // empty or mismatched inputs -> no change
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) {
        double e = RatingExpected(myRating, EffectiveOpp(oppRatings[i], aiPct));
        double s = beat[i] ? 1.0 : 0.0;
        sum += (s - e);
    }
    return k * sum / (double)n;
}

std::vector<double> RatingUpdateRace(const std::vector<double>& ratingsInFinishOrder,
                                     int aiPct, double k) {
    const size_t n = ratingsInFinishOrder.size();
    std::vector<double> out(n, 0.0);
    for (size_t i = 0; i < n; i++) {
        std::vector<double> opp;
        std::vector<bool> beat;
        for (size_t j = 0; j < n; j++) {
            if (j == i) continue;
            opp.push_back(ratingsInFinishOrder[j]);
            beat.push_back(i < j);   // lower finish index = finished ahead
        }
        out[i] = RatingDelta(ratingsInFinishOrder[i], opp, beat, aiPct, k);
    }
    return out;
}

double RatingQuality(double r) {
    return 1.0 / (1.0 + std::exp(-(r - bal::RATING_Q_R0) / bal::RATING_Q_S));
}

int RatingBand(double r) {
    constexpr int kBands = (int)(sizeof(bal::RATING_BAND_MIN) / sizeof(bal::RATING_BAND_MIN[0]));
    int b = 0;
    for (int i = 0; i < kBands; i++) if (r >= bal::RATING_BAND_MIN[i]) b = i;
    return b;
}

double RatingK(int racesCompleted, bool isPlayer) {
    double k = (racesCompleted < bal::K_PROVISIONAL_RACES) ? bal::K_PROVISIONAL : bal::K_SETTLED;
    if (isPlayer && k < bal::K_PLAYER_MIN) k = bal::K_PLAYER_MIN;
    return k;
}

double RatingSeedFromOrder(int order, int fieldSize, bool iconic) {
    if (fieldSize < 1) fieldSize = 1;
    double t = (fieldSize <= 1) ? 0.0 : (double)(order - 1) / (double)(fieldSize - 1);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double r = bal::RATING_SEED_TOP - t * (bal::RATING_SEED_TOP - bal::RATING_SEED_BOTTOM);
    if (iconic) r += bal::RATING_ICONIC_BUMP;
    return r;
}

// ~~~07/09 — THE SAME SPREAD, BUT BY PERCENTILE. It is extracted from `LevelAnchoredSeed` without changing a single
// comma so that whoever does NOT come from any classification can use it: a generated rookie has no place in
// a list, it has a percentile. And it had to be able to use it, because until today it was seeded with ANOTHER rule --
// `RATING_SEED_BOTTOM + talent`, flat-- and that is why it was born outside the band of its own level.
// `pct01` = 1 the best of its level, 0 the last. It is the inverse of the `t` below, which runs the other way.
double LevelAnchoredSeedPct(int level, double pct01, bool iconic) {
    if (level < 1) level = 1;
    if (level > 7) level = 7;
    if (pct01 < 0.0) pct01 = 0.0;
    if (pct01 > 1.0) pct01 = 1.0;
    // The level's fan is spread AROUND its midpoint: the best (pct 1) falls half a fan above
    // and the last (pct 0) half below. Neighboring levels OVERLAP because the fan is wider
    // than the biggest jump between midpoints -- the hero in a bad car.
    double r = bal::LEVEL_SEED_MIDPOINT[level - 1] + (pct01 - 0.5) * bal::LEVEL_SEED_SPREAD;
    if (iconic) r += bal::RATING_ICONIC_BUMP;
    return r;
}

double LevelAnchoredSeed(int level, int order, int fieldSize, bool iconic) {
    // Same order-fraction t as RatingSeedFromOrder (verbatim): 0 = field #1, 1 = field last.
    if (fieldSize < 1) fieldSize = 1;
    double t = (fieldSize <= 1) ? 0.0 : (double)(order - 1) / (double)(fieldSize - 1);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    // 07/09: the body now lives in LevelAnchoredSeedPct. `t` runs the other way from the percentile (t=0 is the
    // first), so it is inverted when passed. Byte for byte the same number as before.
    return LevelAnchoredSeedPct(level, 1.0 - t, iconic);
}

double CohortAnchoredSeed(int order, int castSize, bool hot) {
    // The same curve as the rest of the world: the order within the cast is spread around the midpoint
    // of its home tier. Reusing LevelAnchoredSeed is not a convenience — it is THE point of the phase: a single
    // scale for everyone, or the talent of a grid goes back to depending on who populates it.
    // 11/08: the anchor is `COHORT_SEED_LEVEL`, NOT the home tier. See career_balance.h — where they sit and
    // how much they are worth are two different things, and with a single constant the cohort ended up being the average driver of its
    // own category instead of the people the champions will come from.
    return LevelAnchoredSeed(bal::COHORT_SEED_LEVEL, order, castSize, hot);
}

double CastSeedFromBase(double baseSkill) {
    double t = (baseSkill - bal::CAST_SEED_BASE_LO) / (bal::CAST_SEED_BASE_HI - bal::CAST_SEED_BASE_LO);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return bal::RATING_ROOKIE_BASE + t * (bal::CAST_SEED_RATING_HI - bal::RATING_ROOKIE_BASE);
}

} // namespace career
