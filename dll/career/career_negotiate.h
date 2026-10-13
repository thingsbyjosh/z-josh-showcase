#pragma once
#include "career_balance.h"
// Contract negotiation maths. Pure: no ImGui, no game, no globals.
// Constants in career_balance.h (career::bal::NEGO_*). Mirrors temp/_ams2_design/screen_negotiate.jsx +
// data_living.jsx exactly, but the prototype's stand-in baseMargin is replaced by the real
// leverage formula give = k*(Want_T-theta)*(1+competing) blended with the trial completion ratio.
namespace career {

// The eight negotiation demands (index into bal::NEGO_DEMAND_COST). Stable order = prototype order.
// Grown from 4 to 8 demands. Slots 0..3 are UNCHANGED (prototype order); 4..7 wire to real systems
// (role/lead, damage-responsibility, calendar-freedom, easier-goals; index 7 is a REFUND like index 3).
enum NegoDemandId {
    NEGO_GUARANTEED_SEAT = 0,  // guaranteed seat to season's end       (+cost)
    NEGO_ANTI_REPLACE    = 1,  // anti-replacement protection           (+cost)
    NEGO_DURATION        = 2,  // extend duration to 2 seasons          (+cost)
    NEGO_HARDER_GOALS    = 3,  // accept tougher objectives (REFUNDS budget)
    NEGO_ROLE_LEAD       = 4,  // lead-driver role (raises objective/pressure)   (+cost)
    NEGO_DAMAGE_RESP     = 5,  // damage responsibility covered by team          (+cost)
    NEGO_CALENDAR_FREEDOM= 6,  // calendar/exclusivity freedom (less team support)(+cost)
    NEGO_EASIER_GOALS    = 7,  // softer objectives (REFUNDS budget, inverse of harder_goals)
};

// The player's chosen terms in a negotiation. demands[i]=true => demand i is being asked for.
// salaryPct 0..100 (0 = all coverage / cheap for team, 100 = max salary). sponsorTierRank = the rank
// (0..4) of the sponsor offered as a chip, or -1 for "none offered".
struct DealTerms {
    bool demands[bal::NEGO_DEMAND_COUNT] = {};   // 8-safe value-init (all false); was {false,false,false,false}
    int  salaryPct      = 50;   // 50 = neutral split (zero salary cost)
    int  sponsorTierRank = -1;  // 1 = no sponsor offered
};

// 0 = team would reject, 1 = weak (take it or leave it), 2 = strong (you dictate terms).
enum NegoOutcome { NEGO_REJECT = 0, NEGO_WEAK = 1, NEGO_STRONG = 2 };

// Full evaluation of a negotiation: the base margin (leverage budget surfaced), each spend line, and
// the resulting final margin + outcome. All %s are integers (the UI gauge shows whole %).
struct NegoResult {
    int baseMargin   = 0;   // surfaced leverage budget (before spending)
    int demandCost   = 0;   // total % spent on demands (negative = net refund)
    int salaryCost   = 0;   // % cost of the salary<->coverage choice (-25..+25)
    int sponsorDelta = 0;   // % delta from the offered sponsor chip (negative = offended)
    int finalMargin  = 0;   // baseMargin - demandCost - salaryCost + sponsorDelta, clamped 0..MARGIN_MAX low side not clamped
    NegoOutcome outcome = NEGO_REJECT;
    bool offends     = false;  // true if the offered sponsor is weak (negative delta)
    bool relDinged   = false;  // an over-reach reject with rel below the floor dinged the relationship signal
                               // (computed + shown + tested for now; the persisted per-team store is still pending)
};

// Total % cost of a set of demands (sum of bal::NEGO_DEMAND_COST[i] for each demands[i]==true).
// A negative total means the net effect REFUNDS budget (harder_goals outweighs the asks).
int NegoDemandCost(const DealTerms& terms);

// Salary<->coverage cost: round((salaryPct - 50) * 0.5). Positive = costs budget (you took more salary).
int NegoSalaryCost(int salaryPct);

// Sponsor chip delta by tier rank 0..4 (local..elite). rank<0 (none) => 0. Out-of-range clamps.
int NegoSponsorDelta(int sponsorTierRank);

// The leverage budget the team will concede: give = NEGO_K*(Want_T-NEGO_THETA)*(1+competing) + leverage_bonus.
// Returns a raw (unclamped, non-rounded) double. Want_T is the team's desire 0..~1.4 (>1 if boosted); competing =
// number of rival offers in hand (0,1,2,...). A team that barely wants you (Want_T<=theta) gives ~0.
// leverage_bonus: a flat % from the special card "nego.leverage" (+10 if SLOTTED, 0
// default) -- ADDED AFTER the formula's own gap*competing term, no-op at 0.
double NegoLeverageGive(double wantT, int competing, int leverage_bonus = 0);

// The BASE margin surfaced on the gauge BEFORE the player spends any budget: a blend dominated by the
// trial objective-completion ratio (4/4 >> 1/4), topped up by the leverage give. trialRatio in 0..1
// (e.g. 3 of 4 trial objectives met = 0.75); pass 1.0 when there is no trial (full credit). Clamped
// 0..NEGO_MARGIN_MAX, rounded to an int. leverage_bonus: forwarded to NegoLeverageGive, no-op at 0.
int NegoBaseMargin(double wantT, int competing, double trialRatio, int leverage_bonus = 0);

// Classify a FINAL margin into the gauge outcome: <0 reject, [0,NEGO_WEAK_THRESH) weak, else strong.
NegoOutcome NegoClassify(int finalMargin);

// Evaluate a whole negotiation: compute base margin from (wantT, competing, trialRatio), subtract the
// demand + salary spend, add the sponsor chip, classify. Fills every line of NegoResult. This is the one
// call the UI makes per frame as the player toggles demands / picks a salary package / picks a sponsor.
// rel (0..1, DEFAULTED) is the relationship signal: an over-reach reject with rel < bal::NEGO_RELDING_REL_FLOOR
// sets NegoResult.relDinged. The default rel=0.5 keeps every existing call site compiling unchanged
// and (== the floor) does NOT fire the ding — the live rel stub equals the floor this phase, so the ding is
// exercised only by the careerproof negotiate-reject PNG and the low-rel test case.
NegoResult NegoEvaluate(double wantT, int competing, double trialRatio, const DealTerms& terms, double rel = 0.5);

} // namespace career
