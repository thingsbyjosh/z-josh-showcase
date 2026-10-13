#include "career_negotiate.h"
#include "career_balance.h"
#include <cmath>
#include <algorithm>

namespace career {

int NegoDemandCost(const DealTerms& terms) {
    int sum = 0;
    // NOTE: multiply-by-predicate (not `if(demands[i]) sum+=cost`). MSVC 19.51 /O2 auto-vectorizes the
    // branch+index form by FUSING the bool[] guard with the int[] cost array and byte-loads the negative
    // element (-18 -> 238), giving a wrong total. The arithmetic form computes the same sum correctly.
    for (int i = 0; i < bal::NEGO_DEMAND_COUNT; i++)
        sum += (terms.demands[i] ? 1 : 0) * bal::NEGO_DEMAND_COST[i];
    return sum;
}

int NegoSalaryCost(int salaryPct) {
    // round-half-away-from-zero, matching JS Math.round on the prototype's symmetric +/- values.
    double v = (double)(salaryPct - bal::NEGO_SALARY_MID) * bal::NEGO_SALARY_K;
    return (int)std::floor(std::fabs(v) + 0.5) * (v < 0.0 ? -1 : 1);
}

int NegoSponsorDelta(int sponsorTierRank) {
    if (sponsorTierRank < 0) return 0;          // none offered
    int r = sponsorTierRank;
    if (r > 4) r = 4;                            // clamp to elite
    return bal::NEGO_SPONSOR_DELTA[r];
}

double NegoLeverageGive(double wantT, int competing, int leverage_bonus) {
    double gap = wantT - bal::NEGO_THETA;
    if (gap < 0.0) gap = 0.0;                           // no negative budget
    if (competing < 0) competing = 0;
    return bal::NEGO_K * gap * (1.0 + (double)competing) + (double)leverage_bonus;
}

int NegoBaseMargin(double wantT, int competing, double trialRatio, int leverage_bonus) {
    if (trialRatio < 0.0) trialRatio = 0.0;
    if (trialRatio > 1.0) trialRatio = 1.0;
    double give    = NegoLeverageGive(wantT, competing, leverage_bonus);
    double trialPts = trialRatio * bal::NEGO_TRIAL_SPAN;
    double m = bal::NEGO_TRIAL_W * trialPts + (1.0 - bal::NEGO_TRIAL_W) * give;
    if (m < 0.0) m = 0.0;
    if (m > (double)bal::NEGO_MARGIN_MAX) m = (double)bal::NEGO_MARGIN_MAX;
    return (int)std::floor(m + 0.5);
}

NegoOutcome NegoClassify(int finalMargin) {
    if (finalMargin < 0)                       return NEGO_REJECT;
    if (finalMargin < bal::NEGO_WEAK_THRESH)   return NEGO_WEAK;
    return NEGO_STRONG;
}

NegoResult NegoEvaluate(double wantT, int competing, double trialRatio, const DealTerms& terms, double rel) {
    NegoResult r;
    r.baseMargin   = NegoBaseMargin(wantT, competing, trialRatio);
    r.demandCost   = NegoDemandCost(terms);
    r.salaryCost   = NegoSalaryCost(terms.salaryPct);
    r.sponsorDelta = NegoSponsorDelta(terms.sponsorTierRank);
    r.offends      = (terms.sponsorTierRank >= 0 && r.sponsorDelta < 0);
    // Mirrors the prototype: margin = base - demandCost - salaryCost + sponsorDelta.
    int m = r.baseMargin - r.demandCost - r.salaryCost + r.sponsorDelta;
    if (m > bal::NEGO_MARGIN_MAX) m = bal::NEGO_MARGIN_MAX;   // high-side clamp only; can go negative (reject)
    r.finalMargin = m;
    r.outcome     = NegoClassify(m);
    // An over-reach that gets REJECTED while rel is below the floor dings the relationship signal.
    // Computed/shown/tested only for now (there's no persisted per-team store yet). The default rel==floor
    // does NOT fire (0.5 < 0.5 is false), so the live screen never dings until the per-team rel store lands.
    r.relDinged    = (r.outcome == NEGO_REJECT && rel < bal::NEGO_RELDING_REL_FLOOR);
    return r;
}

} // namespace career
