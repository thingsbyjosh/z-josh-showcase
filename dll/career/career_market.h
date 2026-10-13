#pragma once
// career_market — computes the DYNAMIC OFFER for the driver market.
//
// A PURE module (no ImGui beyond the OfferDef/ImU32 type from career_market_data.h, no game
// state, no mutable globals): it reads the player's state (REP/XP/gates) + the season-JSON data already
// loaded in the engine (career_series / career_fields / classes.json), and RETURNS the offers the
// player can see. DETERMINISTIC: same inputs -> same offers. Testable in career_test.
//
// This is ONLY the offer COMPUTATION. Persistence (Contract, accepted/rejected) and the
// UI wiring come later.
//
// Design spec: the career contracts/market design doc (seats / offers).

#include <string>
#include <vector>
#include <set>
#include "career_series.h"        // career::SeriesDef
#include "career_market_data.h"   // market::OfferDef (+ ImU32)
#include "career_save.h"          // career::SeatContender (the sealed shortlist travels in the contest report)

namespace career {
struct SaveData;   // open-seat occupancy source (forward decl, pointer param)

// Salary anchor (credits/race) by series LEVEL. A compressed geometric curve (bal::SALARY_*).
long SeriesSalaryAnchor(int level);

// Variable salary: the FINAL salary number = clamp(anchor(level) * position-in-band,
// SALARY_FLOOR, SALARY_CAP). position-in-band = q(rating) placed within [center-HALFWIDTH..center+HALFWIDTH]
// (TeamSalaryBand(seriesId, teamLabel, save, worldYear).centerMult), shifted by salaryPct (0..100, 50=
// neutral) within a bounded margin. Deterministic.
long SalaryFor(const std::string& seriesId, const std::string& teamLabel, int level,
               double rating, int salaryPct, const SaveData& save, int worldYear);

// Inbox redesign, press: a DECAYED sum of the relationship marks (SaveData::rel_marks, career_save.h)
// recorded toward `team`. weight = 0.5^age, age = currentYear - mark.year; age<0 -> treated as age=0 (full
// weight — a mark "from this year or later" isn't penalized by the clock); age>3 -> dropped (no longer weighs).
// currentYear<=0 (a world with no year set) -> ALL marks weigh x1 (a documented fallback, the same mechanism
// TeamRelationship uses when save.current_world_year==0). PURE, deterministic, no RNG. Consumed by
// market::TeamRelationship (career_market.cpp) for the fold of the
double RelMarksSum(const SaveData& save, const std::string& team, int currentYear);

} // namespace career

namespace market {

// THE MARKET CALENDAR. The year has three stretches, and the Market is a
// a different thing in each:
//   PADDOCK   (Jan 1 - Aug 31)  who drives where, your contracts, and how you're doing against their targets. No seats open.
//   WINDOW    (Sep 1 - Dec 14)  the paddock PLUS the seats opening up for next year.
//   DECISION  (Dec 15 - year end)  everything resolves at once: teams decide and you choose among whoever wants you.
// By DATE and GLOBAL, not relative to each series' opener: with multiple contracts on different calendars, a
// per-series window would be unreadable. All of this is PURE and deterministic: it only looks at the clock.
enum MarketPhase { MARKET_PADDOCK = 0, MARKET_WINDOW = 1, MARKET_DECISION = 2 };

// The day of the year (1-based) the window opens / the decision week falls on, RESOLVED for that specific year:
// in a leap year the same September 1st falls on a higher doy, so this can't be a constant.
int MarketWindowOpenDoy(int year);
int MarketDecisionDoy(int year);
// The day of year 1's COMPRESSED decision week (February 15th).
int MarketY1DecisionDoy(int year);

// Which stretch of the year `doy` falls in. An out-of-range doy clamps to the year's edges (it never invents a phase).
//
// `firstYear` = the year the career started. If `year` is that one, the year runs an EARLY compressed
// cycle: the window is open from day one (you start in preseason) and its decision week falls in
// February, so you enter your first season with seats already contested. Once that week passes, the year follows the
// normal calendar, so in September the year-2 window opens like in any other year.
// 0 (default) = no special case, which is the usual behavior for old callers.
int MarketPhaseFor(int year, int doy, int firstYear = 0);

// The SEASON being signed for at this point on the clock. Normally next
// year — you sign in September for the following season — but in year 1's compressed cycle you
// sign for THIS year, because your first season starts right after the February week.
//
// This is what lets year 1 seal TWO windows without clashing: February's (target: this year) and
// September's (target: next year). Without distinguishing the target, the second would be mistaken for "the same window
// already sealed", and the player would never see their first December's silly season.
int WindowTargetYear(int year, int doy, int firstYear = 0);

// THE SEASON A CONTRACT SIGNED TODAY GETS STAMPED WITH. It's the missing translation between the CLOCK and the
// CONTRACT, and its absence broke the whole career at the end: the market only opens in the window (Sep 1 -
// Dec 21), where you sign for NEXT year, but the player's own signing paths (market, mail,
// negotiation) were passing AcceptOffer the clock's year — the one that's ending. With that, signing in
// December enrolled you in the DEAD season (all its rounds had already passed), and the rollover a few days
// later didn't recognize the contract as "not started yet" (it checks signed_year >= cc.year+1), so it burned its
// only season and killed it without re-enrolling you: you ended up with no seat for the new year. Reported by three
// players ("it just signs me up to do the events for that same year that has already passed").
// Outside the window the only thing that signs is the definitive renewal, which lands in January with the clock already in its
// place -> the clock's year is correct there and this is a no-op. The Paddock path (SignWindowSeat) never
// suffered from this because it already translated on its own via save.window_year; this is the SAME rule, derived from the clock.
int SigningSeasonFor(int year, int doy, int firstYear = 0);

// Days remaining until the window opens / until the decision week. 0 if you're already inside or on it. Once
// the date has passed, it counts to NEXT YEAR's, so it never returns a negative — which is what the
// calendar warning ("you're skipping the offer week") needs, so it can fire just like the skip-a-race warning.
// `firstYear`: in year 1 the window is open from day one, so it returns 0 instead of
// counting toward September. 0 = the usual behavior.
int DaysToMarketWindow(int year, int doy, int firstYear = 0);
// `firstYear` = the year the career started. In the FIRST season the decision week that arrives
// is February's compressed one, not December's: without this the countdown pointed 348 days at a
// date that was only 45 days out. Same parameter, same criterion as MarketPhaseFor. 0 = the usual behavior.
int DaysToDecisionWeek(int year, int doy, int firstYear = 0);
// 18/09: days left until the market CLOSES (up to the first closed day). 0 = it's already closed today.
// With the market open, the close is also the last deadline for every live offer: the next day nothing is
// signed (Josh 18/09: "the only window for moves is going to be until December 31st"). Same
// `firstYear` as MarketPhaseFor: year 1 closes at the end of its February decision week.
int DaysToMarketClose(int year, int doy, int firstYear = 0);

// THE MARKET DATES FOR A YEAR, so the calendar can DRAW them. Until now the
// market had a calendar only the engine knew about: the player saw eight weeks of preseason
// sitting empty with a decisive date inside and no sign it was there. A gap with nothing drawn
// reads as "nothing happening here", which is exactly the opposite, and "jump to next race" would skip right
// over it without the player ever noticing.
//
// DERIVED from the clock, not persisted: same inputs, same dates. Sorted by doy.
// `firstYear` = the year the career started; if `year` is that one, the two dates of the
// compressed cycle come first, on top of the usual ones — year 1 has TWO windows and two decision weeks.
enum MarketMarkKind { MARK_WINDOW_OPEN = 0, MARK_DECISION_WEEK = 1 };
struct MarketMark {
    int kind = MARK_WINDOW_OPEN;
    int doy  = 0;   // first day (1-based) of the mark
    int days = 1;   // how long it lasts; December's decision week runs to year end
};
std::vector<MarketMark> MarketMarksForYear(int year, int firstYear = 0);

// Does the [fromAbsDay, toAbsDay] span jump clean over a decision week without landing in it?
// Returns the abs_day of the FIRST day of that week, or 0 if none is skipped. It's the warning missing from the
// clock jump: "jump to next race" crosses February 15th without stopping, and the player finds out
// when there's nothing left to do about it. Pure; it looks at most at the departure and arrival years.
int DecisionWeekSkippedBetween(int fromAbsDay, int toAbsDay, int firstYear = 0);

// GenerateOffers' diagnostic trace. OFF by default: it opens and closes the log file on EVERY
// line, and offers regenerate every ~1.7s, including WHILE DRIVING -- measured at 2128 file
// open/close cycles within a 15-minute race, which is the stutter testers reported.
// config.json "market_log": true turns it back on for diagnosis.
void SetMarketLogging(bool on);

// two-sided matching (PURE) ----------------------------------------------------------
// Inputs to the want-scores. rating = the unified ELO=REP number (the per-driver rating; until
// that lands the caller passes the player's rating and AI ratings come from RatingSeedFromOrder).
// rel = relationship[driver,team] in 0..1 (default 0.5). prestige = team/series prestige 0..100.
// money_norm = pay-driver budget in 0..1 (0 = none). sponsorFit/nemesisFrac in 0..1. sameTeam/
// sameCat feed rivBoost. sameContinent = driver shares the series' continent.
struct WantInputs {
    double rating       = 1150.0;
    double rel          = 0.5;
    double prestige     = 50.0;
    double money_norm   = 0.0;     // pay_driver only
    double sponsorFit   = 0.0;     // the real overlap gets filled in later; 0 stub today
    double nemesisFrac  = 0.0;     // nemesis/total in this candidate set
    bool   sameTeam     = false;   // rivBoost +o
    bool   sameCat      = false;   // rivBoost +o
    bool   sameContinent= true;    // Want_D continent penalty off when true
    int    contractKind = 0;       // career::bal::CONTRACT_* (payAdjust only for pay_driver)
    // Soft terms layered onto team desire. All default to a NO-OP so the byte-identical Want_T core
    //     math and the existing want_t regression (test_mk_want_t == 0.5825) stay green. ---
    double form         = 0.7;     // CareerForm 0..1; default == MK_FORM_NEUTRAL -> form factor 1.0 (no-op)
    int    tier_gap     = 0;       // seat.level - player tier; default 0 -> jump penalty 0 (no-op)
    int    rating_races = 10;      // default == K_PROVISIONAL_RACES (settled) -> no provisional discount (no-op)
    // Cleanliness soft term. MK_W_CLEAN goes live; the penalty ONLY bites
    //     below bal::CLEAN_TEAMINTEREST_FLOOR. Default 100.0 (a fully clean driver) -> the factor is exactly 1.0
    //     so the byte-identical Want_T core + the test_mk_want_t == 0.5825 regression stay green. ---
    double cleanliness  = 100.0;   // 0..100; >= CLEAN_TEAMINTEREST_FLOOR -> no-op
};

// team RELATIONSHIP 0..1 for a series, derived from PERSISTED history (PURE, deterministic,
// NO RNG, NO new save field). This is the single source for wi.rel in GenerateOffers: it feeds BOTH the
// "WHY THEY WANT YOU -> RELATIONSHIP" why-bar (o.whyRel = rel*100) AND the soft matching (Want_T's L_t*rel term),
// replacing the old flat 0.5 stub. PRECEDENCE (bal::REL_*):
//   1. NO shared history          -> EXACTLY bal::REL_BASE (0.50)  [keeps test_mk_want_t == 0.5825 byte-identical]
//   2. active contract (ContractForSeries) -> bal::REL_CURRENT_TEAM (precedence; returned directly)
//   3. pending renewal (pending_renewals has of_<series_id>) -> at least bal::REL_RENEWAL (precedence)
//   4. otherwise: start at REL_BASE, then ADD ex-team (completed_seasons/race_log; +podium extra if best
//      final_pos <= REL_PODIUM_POS) + courted (courting) - snubbed (declined_offers/expired_offers)
//      + brand-affinity (see offerTeam below), then CLAMP to [bal::REL_MIN, bal::REL_MAX].
//      Shared history warms a team; snubbing cools it.
// offerTeam (default ""): the OFFERED SEAT'S team_label. When non-empty AND that org appears in
// your history in a DIFFERENT series (completed_seasons[].team_id or active contracts[].team_id in another
// series, OR any courting[].team_id), add bal::REL_BRAND_AFFINITY ("the team has entries in OTHER series where
// you've raced"). ADDITIVE (alongside the terms, before the clamp); does NOT override the current-team /
// renewal precedence. offerTeam == "" -> NO brand term -> byte-identical to the prior path (the
// no-history default stays EXACTLY REL_BASE -> test_mk_want_t stays 0.5825).
// Inbox redesign, press — rel_marks: when offerTeam is non-empty, ADD career::RelMarksSum(save,
// offerTeam, save.current_world_year) — press-tone (and other mail-family) warmth/chill marks aimed at that
// org, already decayed. Escapes precedence-1 on its own (a lone mark still moves off REL_BASE); ADDITIVE
// before the clamp; offerTeam == "" -> no marks term (byte-identical to the prior path).
double TeamRelationship(const career::SaveData& save, const std::string& series_id,
                        const std::string& offerTeam = "");

// Diplomacy-diffusion helpers (team<->team relationship graph). Pure.
double AllyReach(const std::string& from, const std::string& to);   // decayed ally reach (peer family/ally + affiliation), <=REL_DIPLOMACY_MAX_HOPS hops; 0 if no path or from==to
double RivalWeight(const std::string& from, const std::string& to); // DIRECT rivalry magnitude (1 hop); 0 if not a rival

// rivBoost = MK_RIV_O*nemesisFrac, plus +MK_RIV_O each for sameCat / sameTeam. Pure helper.
double RivBoost(const WantInputs& in);

// Junior team: is this an ASPIRATIONAL seat (where the cohort contest actually BITES)? At your level and
// below (on-ramp) there's ALWAYS room -> false -> no contest (never starvation when starting out). Pure, stateless.
//
// The `seatWorks` field is dropped, deliberately. That field (the class's grid_kind)
// was doing TWO jobs at once: how you get in (you apply and pay, or you get signed) and whether you have to win the seat
// away from someone. In reality the second thing doesn't depend on the category but on how far above you it sits: at the
// entry level anyone will take you as you show up, and it's the good seats you have to be signed for. Formula Vee
// coming out "closed" and Karting "open" didn't correspond to anything real, and left the rookie unable to
// enter a category of their own level. `seatWorks` stays in the signature because it still decides legitimate
// things (the reputation bar, and whether the contract is factory or customer), but it no longer closes the door.
inline bool SeatIsAspirational(bool /*seatWorks*/, int seriesLevel, int frontierPhase) {
    return seriesLevel > frontierPhase;
}

// Want_T (step 2): how much the TEAM wants this driver. 0..~1.5 (boosts can exceed 1).
//   merit  = w_q*q(R) + w_p*prestigeFit ; prestigeFit = 1 - |q(R) - prestige/100|
//   Want_T = L_t*rel + (1-L_t)*merit ; *= payAdjust (pay_driver) ; *= (1+w_s*sponsorFit) ; *= (1+rivBoost)
//   soft terms, all NO-OP at WantInputs defaults: *= (1 + w_form*(form - FORM_NEUTRAL)) ;
//   *= (1 - w_jump*max(0, tier_gap - JUMP_FREE)) clamped >=0 ; *= PROVISIONAL_DISCOUNT while rating_races < K.
double Want_T(const WantInputs& in);

// teamInterest: a thin PURE wrapper over the tested Want_T core. Returns 0.0 when the
// hard GATE_fit fails (gateFit==false), else delegates verbatim to Want_T. The soft score lives inside
// Want_T; this wrapper ONLY layers the hard gate on top so GenOffer.want_t stays the ordering carrier.
// teamInterest ORDERS offers (never truncates — NO-CAP).
double teamInterest(const WantInputs& in, bool gateFit);

// gateAfford: a PURE SIGNABILITY check — does the player have the liquid credits to
// SIGN this offer? It gates the Accept button (UI soft-mark), NOT generation: the offer is ALWAYS generated
// and shown; gateAfford NEVER drops it nor zeroes teamInterest. Fee-waived seats (works/customer/signed —
// the team or the budget covers the buy-in) are always affordable (neutral). cost = the buy-in/inscription
// the player must pay up front (pay_driver buy_in today; 0 for fee-waived). Currently DORMANT.
// money_norm=0 no pay seats generate, so cost is 0 everywhere and this returns true. Wired in but not yet exercised.
//   affordable = feeWaived || (double)credits >= (double)cost * MK_AFFORD_MARGIN
bool gateAfford(long credits, long cost, bool feeWaived);

// prestigeAttract = clamp((prestige - MK_PRESTIGE_LO)/MK_PRESTIGE_SPAN, 0, 1). Pure helper.
double PrestigeAttract(double prestige);

// Want_D (step 3): how much the DRIVER wants this seat (places AI; gates whether the player is
// even courted). continentPen repels foreign low-prestige series.
//   Want_D = [L_d*rel + (1-L_d)*q(R)] * prestigeAttract * continentPen * (1 + rivBoost)
double Want_D(const WantInputs& in);

// money terms by contract type. coverage in 0..1 (a future deal axis; 0 today). per_race and
// sign_bonus are for works/customer; buy_in is for pay_driver (you pay; worse fit -> you pay more).
struct OfferTermsOut { long per_race = 0; long sign_bonus = 0; long buy_in = 0; };

// rating = the unified rating; level = series tier 1..7; contractKind = career::bal::CONTRACT_*.
//   works/customer: per_race  = FACTORY_SEAT_SALARY*(level/7) * (0.7 + 0.6*q(R)) * (1 - coverage*k_cov)
//                   sign_bonus = per_race * k_bonus ;  customer gets per_race scaled to 0 salary share
//   pay_driver:     buy_in     = MK_PAYDRIVER_BUYIN[level-1] * (1 - q(R))
OfferTermsOut OfferTerms(double rating, int level, int contractKind, double coverage);

// Variable salary: like OfferTerms, but with series/team/save context -> the salary
// comes from career::SalaryFor (variable by series x team x rating), NOT OfferTerms' flat level/7 curve.
// buy_in (pay_driver) stays IDENTICAL to OfferTerms.
// The salary is only emitted if the contract's profile pays it
// (ContractCoverageProfile(contractKind, archetype).salary_dir > 0 == works/factory), not by KIND. That way a
// trial/works with an unsalaried archetype no longer carries a PHANTOM per_race that the settle (which
// pays by salary_dir) never actually pays out -> pre-race screen == settlement. That's why it now takes the archetype.
// OfferTerms stays as-is for back-compat with callers that lack this context (legacy tests).
OfferTermsOut OfferTermsCtx(double rating, int level, int contractKind, int archetype, const std::string& seriesId,
                            const std::string& teamLabel, const career::SaveData& save, int worldYear);

// tryout gate — a high-standard works team (Want_T >= MK_TRYOUT_WANT_T) at tier >= MK_TRYOUT_MIN_LEVEL
// demands an evaluation event before the offer opens. Pure predicate; the tryout EVENT itself (target lap)
// is the qualifier reused in the UI. pay_driver / customer never require a tryout (it's a talent gate).
// LEGACY: the simple level/want gate. SUPERSEDED by RequiresTrial (the gray-zone trigger-set); kept
// only so the test_mk_tryout regression / any straggler call still link. New code MUST use RequiresTrial.
bool TryoutRequired(double wantT, int level, int contractKind);

// Gray-zone trial trigger-set — REPLACES TryoutRequired as the offer gate. PURE.
// A trial fires ONLY in the gray zone and NEVER on the excluded archetypes/conditions:
//   requiresTrial = true if ANY gray-zone condition:
//     jumping 2+ category levels      (targetLevel - frontierPhase >= 2)
//     disputed seat                   (a rival candidate also wants it; disputedSeat)
//     top team AND no recent result   (topTeam && !recentStrongResult)
//     over-budget ask in negotiation  (overReach)
//     JUNIOR_PROGRAM / FACTORY_PROGRAM one-time entry  (archetype trialRule == ARCH_TRIAL_ONETIME)
//     coming from accidents           (ComingFromAccidents(cleanliness), cleanliness < CLEAN_ACCIDENT_TRIGGER_FLOOR)
//   NEVER (overrides every gray-zone condition):
//     SELF_FUNDED_ENTRY / WILDCARD / RENEWAL archetype  (trialRule == ARCH_TRIAL_NEVER)
//     team approached YOU for a recent title/result     (approachedForResult)
//     low category                                       (targetLevel < TRIAL_MIN_LEVEL)
// archetype = career::ContractArchetype (drives the never / one-time-entry branches via ArchetypeInfo.trialRule).
bool RequiresTrial(int archetype, int targetLevel, int frontierPhase, double cleanliness,
                   bool disputedSeat, bool topTeam, bool recentStrongResult,
                   bool overReach, bool approachedForResult);

// The "coming from accidents" gray-zone trial trigger. Pure predicate
// over the player's 0..100 cleanliness EMA: true when cleanliness is below bal::CLEAN_ACCIDENT_TRIGGER_FLOOR
// (45). The trial trigger-set CONSUMES this (one of its gray-zone OR conditions); placed here, pure, so
// the trigger-set and tests share one source of truth. No game/SHM/ImGui dependency.
bool ComingFromAccidents(double cleanliness);

// Decide the contract TYPE for a seat (one of five types). Pure, deterministic.
//   works  : closed (factory) seat the team offers you (direct, REP-met).
//   customer: open seat (self-funded), you applied.
//   pay_driver: an upper-tier backmarker open seat where you bring budget (money_norm>0 and you're
//               under the rating bar for it) -> the Latifi path.
//   trial  : a short works seat at a stretch tier when you're trending up but not proven (q below bar).
//   conditional: a feeder/development seat with a promotion clause (open seat at a low tier with a
//                clear ladder target). Encoded by a non-empty promote_to series.
// works/closedSeat = seat.works ; q = q(rating) ; bar = q-threshold the seat normally wants (q(reqRep-equiv)).
int ContractKindForSeat(bool closedSeat, double q, double seatBarQ, double money_norm, bool hasPromoteTarget);

// classes.json: grid type per class (lazily loaded, once) ----
// "open"  = an open seat (you apply, "driver wanted"); reqRep low or 0.
// "closed"= a sealed/factory seat (THEY come looking for you to sign); reqRep HIGH, never reqRep=0.
// Reads the classes.json data file with the jsonm parser. Missing class -> "open".
std::string GridKindForClass(const std::string& className);
int         GridSizeForClass(const std::string& className);   // 0 if the class isn't in classes.json

// A seat derived from a series (the corpus teams aren't usable directly -> derive one) ----
// works=true  -> a team seat (CLOSED grid): "they want you", by signing only (reqRep high).
// works=false -> an open ad (OPEN grid): "driver wanted"; reqRep low (level<=2 -> 0).
struct MarketSeat {
    std::string series_id;
    std::string class_name;
    int  reqRep = 0;       // the ENGINE's REP threshold (REP_BAND scale), NOT the Market's old scale
    bool works = false;    // true = closed grid (factory/signing seat)
    int  level = 1;        // the series' tier 1..7 (a copy of sd.level)
};

// 1 seat per series (v1: the player's POTENTIAL seat). Deterministic.
// reqRep:  CLOSED -> REP_BAND[clamp(level-1,0,5)] scaled x1.5 (min REP_TIER1=30; signing demands REP).
//          OPEN -> level<=2 ? 0 : REP_BAND[clamp(level-3,0,5)] (a modest threshold that grows with the tier).
std::vector<MarketSeat> SeatsForSeries(const career::SeriesDef& sd);

// Input for GenerateOffers: player state + calendar cursor ----
struct OfferGenInput {
    int rep = 0;
    int frontier_phase = 1;                          // the XP-tree gate: a series is enterable if level<=frontier_phase
    const std::set<std::string>* unlocked_gates = nullptr;  // series_ids with an earned superlicense (can be null)
    int year = 0;                                    // (reserved; doesn't affect v1, for future dated campaigns)
    int cursor_doy = 0;                              // the current day of year; a series already underway (round-1 doy<cursor) = expired
    double rating = 1150.0;   // the player's unified rating (prog.rating; defaults to rookie base)
    double money_norm = 0.0;  // the player's pay-driver budget 0..1 (filled in later; 0 today -> no pay seats)
    // Canonical append order (rating_races, credits, career_seed) AFTER money_norm. teamInterest
    // adds rating_races (provisional-spike discount) + credits (dormant GATE_afford) + form (the shared why-bar
    // / soft-score CareerForm value). Scarcity consumes career_seed. All appended AFTER the
    // existing positional fields so the 6-field positional init at career_test.cpp stays valid (defaults fill
    // the trailing fields). Do NOT reorder the existing fields.
    double form = 0.7;               // the player's CareerForm 0..1 (==MK_FORM_NEUTRAL default = no-op)
    int    rating_races = 0;         // races toward settling; <K_PROVISIONAL_RACES -> provisional discount
    long   credits = 0;              // liquid credits for the DORMANT signability gate (afford)
    unsigned long career_seed = 0;   // the career's deterministic scarcity seed (0 = legacy/shared)
    // the player's 0..100 cleanliness EMA (prog.cleanliness), threaded into the
    // gray-zone trial trigger-set (via ComingFromAccidents). APPENDED AFTER career_seed (canonical append order)
    // so the existing positional inits in career_test/callers stay valid. NO-OP default 100.0 (a fully clean
    // driver -> ComingFromAccidents false) keeps the accident branch inert for callers that don't set it.
    double cleanliness = 100.0;      // 0..100; >= CLEAN_ACCIDENT_TRIGGER_FLOOR -> accident trigger off (no-op)
    // The living-world year that drives the incumbent age curve for ELO displacement (the
    // aged-rating projection mirrors RolloverYearReport). APPENDED AFTER cleanliness (canonical append order)
    // so existing positional inits stay valid. 0 (default) -> no displacement scan (the legacy/no-save path and
    // any caller that doesn't set it is byte-identical). The overlay threads save.current_world_year (else year).
    int    world_year  = 0;
    // The sponsor-card system: level bonus from the special card "market.offer_quality" (1 if the
    // card is SLOTTED, 0 if not / no card). APPENDED AFTER world_year (canonical append order) so the
    // existing positional inits (career_test.cpp) stay valid. 0 (default) -> no-op, byte-identical with no card.
    int    quality_bump = 0;
};

// A generated offer: the UI's OfferDef + HELD state (blocked by XP/gate) ----
// held=true: you have the REP but the series is blocked by XP phase or superlicense. The UI shows it
// locked with lock_reason. held=false: the offer is already signable.
struct GenOffer {
    market::OfferDef def;
    bool        held = false;
    std::string lock_reason;       // "" if !held; if held: "requires " / "requires superlicense"
    std::string lock_reason_en;    // English mirror of the lock reason
    double      want_t = 0.0;       // team's desire for the player (debug/ordering/negotiation handoff)
    bool        tier_locked = false; // courting state: REP-met but XP/gate blocks it (== held)
    int         archetype = 0;       // resolved ContractArchetype (== def.archetype; transient, regen'd each call)
    // — WHO you won this seat against. The contest already ranked every aspirant
    // to decide whether to offer you the seat, but it came out as a plain yes/no and the rival's name was thrown away. It's
    // only filled in for seats that are genuinely contested (the works ramp or above your phase, and not
    // pay-driver); everywhere else you're not competing against anyone and they stay empty. Transient, regenerated on every call.
    std::string rival_id;            // "" if there was no contest, or there was one but no cohort aspirant existed
    std::string rival_name;          // the rival's display name
    double      rival_fit = 0.0;     // their rating; 0.0 with no rival
    double      player_fit = 0.0;    // yours, on the SAME table (comparable to each other, not to other series)
};

// Generates ALL visible offers given the player's state. Deterministic. Sorted by series_id.
// Rules:
//   direct (def.direct=true): a works (closed) seat AND rep>=reqRep ("they want you").
//   ad (def.direct=false): an open seat with reqRep==0, or an open seat with rep>=reqRep.
//   held: rep>=reqRep BUT (level>frontier_phase) or (a gated series whose gate isn't open) -> emitted locked.
//   NOT offered: !metRep and it's not an open ad (you need the REP for them to want you).
//   expired: a series whose round-1 has doy>0 and doy<cursor_doy (the season is already underway) -> OMITTED.
//
// OPEN-SEAT gating: when `save` is non-null, the living seat market drives generation —
// a series only offers when it has at least one OPEN seat (career::SeatsForClass(sd, *save) with an open seat,
// freed by retirement/promotion in RolloverYearReport). A fully-occupied class produces NO offer; offers
// emerge across ALL tiers (the lower/cohort classes included). Era-memory series stay excluded.
// When `save` is null (legacy callers / no living-world state yet), the behavior is the ORIGINAL static-per-
// series generation (back-compat: every existing call site + test is byte-identical). The reqRep/fit/scarcity/
// teamInterest gates remain filters ON the open-seat set; NO concurrent-offer cap.
// — the REPORT for each seat that's genuinely CONTESTED (the works ramp or above your phase,
// and not pay-driver). GenerateOffers is PURE and can't open or close a trial in the save, so what it
// does is report what it saw: the caller decides what to persist. Seats that aren't contested do NOT appear here.
struct SeatContestReport {
    std::string series_id, class_name, team_label;
    std::string rival_id, rival_name;      // "" if there was no aspirant facing them
    double player_fit = 0.0, rival_fit = 0.0;
    int    outcome = 0;                    // career::SeatContestOutcome (WON / WATCH / LOST)
    // The aspirants that would be sealed in if this opened a trial. Already computed here
    // because that's where the table lives; the caller only persists it (GenerateOffers never touches the save).
    std::vector<career::SeatContender> shortlist;
};

std::vector<GenOffer> GenerateOffers(const OfferGenInput& in, const career::SaveData* save,
                                     std::vector<SeatContestReport>* contestsOut = nullptr);

// Legacy single-arg form (static-per-series generation) — kept for back-compat with every
// existing call site/test (byte-identical to the earlier behavior; no open-seat gating).
std::vector<GenOffer> GenerateOffers(const OfferGenInput& in);

// Convenience reference overload (open-seat gated) — same as the pointer form with &save.
std::vector<GenOffer> GenerateOffers(const OfferGenInput& in, const career::SaveData& save);

// Junior-team promotion destination series — the LOWEST level > canteraLevel where `parent` fields a real
// car (TeamLabelsForClass includes it). Returns the series_id, or "" if the parent has no seat in a higher tier.
std::string PromotionDestinationForParent(const std::string& parent, int canteraLevel);
// Junior team, after the GenerateOffers loop: if the player beats their cohort in an affiliated junior team,
// the parent team issues a promotion offer (ARCH_ACADEMY_PROMOTION) into `out`. Deterministic, additive, a no-op with no affiliation.
void AppendAcademyPromotions(const OfferGenInput& in, const career::SaveData& save, std::vector<GenOffer>& out);

} // namespace market
