#include "career_market.h"
#include "career_balance.h"       // bal::REP_BAND, REP_TIER1, FACTORY_SEAT_SALARY
#include "career_engine.h"        // career::ReqRepRating (reqRep re-anchored onto the rating scale)
#include "career_rating.h"        // career::RatingQuality (q(R) feeding want-scores)
#include "career_archetype.h"     // career::ArchetypeForOffer (pure offer->archetype resolver)
#include "career_econ.h"          // career::ContractCoverageProfile (the salary_dir gate on the salary)
#include "career_save.h"          // career::SaveData (open-seat occupancy source)
#include "career_seats.h"         // career::SeatsForClass (the open-seat gate)
#include "career_candidate.h"     // career::PlayerWinsSeat (player-vs-cohort seat contest)
#include "career_content.h"       // SeriesPlayable (series with no installed cars, kept out of the market/promotions)
#include "career_calendar.h"      // career::CalendarForSeriesInYear (opener PROJECTED onto the current year, in.year)
#include "career_dateutil.h"      // AbsDayFromYearDoy (cooldown after a missed trial)
#include "json_min.h"             // jsonm::ParseFile (classes.json)
#include "paths.h"                // PathProvider: classes.json under data/, log under userdata/logs
#include "career_userlog.h"       // career::EngineLogPath (the unified engine log, with a size cap)
#include <algorithm>
#include <cmath>                  // std::fmod, std::abs (deterministic HSV->RGB color)
#include <map>
#include <vector>                 // posAllyNeighbors
#include <utility>                 // std::pair/std::make_pair (posAllyNeighbors)
#include <cstdio>                 // diagnostic LogF (fopen/append, portable across engine+overlay+test)
#include <cstdarg>

namespace career {

// Salary anchor (credits/race) by series LEVEL. A compressed geometric curve (bal::SALARY_*).
long SeriesSalaryAnchor(int level) {
    int lv = level < 1 ? 1 : (level > 7 ? 7 : level);
    double v = (double)bal::SALARY_ANCHOR_BASE;
    for (int i = 1; i < lv; ++i) v *= bal::SALARY_ANCHOR_GROWTH;
    return (long)(v + 0.5);
}

// Variable salary: the final number = clamp(anchor(level) * position-in-band, FLOOR, CAP).
// NOTE: TeamSalaryBand returns minMult=maxMult=0 when it can't resolve (a team with no rated occupants; only
// centerMult is meaningful then) -- so the position is derived from centerMult ± SALARY_BAND_
// HALFWIDTH instead of the raw min/maxMult. This also holds when it DOES resolve: there min=center-HALF /
// max=center+HALF anyway (TeamSalaryBand builds them that way), so it's byte-identical in that case.
long SalaryFor(const std::string& seriesId, const std::string& teamLabel, int level,
               double rating, int salaryPct, const SaveData& save, int worldYear) {
    long anchor = SeriesSalaryAnchor(level);
    SalaryBand band = TeamSalaryBand(seriesId, teamLabel, save, worldYear);
    double q = RatingQuality(rating);                                   // 0..1
    double lo = band.centerMult - bal::SALARY_BAND_HALFWIDTH;           // band centered on centerMult
    double hi = band.centerMult + bal::SALARY_BAND_HALFWIDTH;
    double pctShift = ((double)salaryPct - (double)bal::NEGO_SALARY_MID) / 100.0;   // -0.5..+0.5
    double m = lo + q * (hi - lo) + pctShift * bal::SALARY_BAND_HALFWIDTH;
    if (m < lo - bal::SALARY_BAND_HALFWIDTH) m = lo - bal::SALARY_BAND_HALFWIDTH;   // bounded clamp
    if (m > hi + bal::SALARY_BAND_HALFWIDTH) m = hi + bal::SALARY_BAND_HALFWIDTH;
    long pay = (long)((double)anchor * m + 0.5);
    if (pay < bal::SALARY_FLOOR) pay = bal::SALARY_FLOOR;
    if (pay > bal::SALARY_CAP)   pay = bal::SALARY_CAP;
    return pay;
}

// Inbox redesign, press: rel_marks — a DECAYED sum of the relationship marks recorded toward `team`.
// weight = 0.5^age (age = currentYear - mark.year); age<0 -> treated as age=0 (full weight — a mark "from
// this year or later" isn't penalized by the clock); age>3 -> dropped entirely (too old to keep weighing,
// not just faded to near-zero). currentYear<=0 (a world with no year set, save.current_world_year at its default)
// -> ALL marks weigh x1 (with no age curve there's no decay to apply). PURE, deterministic, no RNG.
double RelMarksSum(const SaveData& save, const std::string& team, int currentYear) {
    double sum = 0.0;
    for (const auto& m : save.rel_marks) {
        if (m.team != team) continue;
        int age = (currentYear <= 0) ? 0 : (currentYear - m.year);
        if (age < 0) age = 0;                 // a "future"/current-year mark -> full weight
        if (age > 3) continue;                // no longer weighs anything (dropped, not just faded)
        sum += (m.delta_x1000 / 1000.0) * std::pow(0.5, (double)age);
    }
    return sum;
}

} // namespace career

namespace market {

// The market calendar. See career_market.h. Fully PURE: it only looks at the clock.
int MarketWindowOpenDoy(int year) {
    return career::dateutil::DoyFromYmd(year, career::bal::MK_WINDOW_OPEN_MONTH, career::bal::MK_WINDOW_OPEN_DAY);
}
int MarketDecisionDoy(int year) {
    return career::dateutil::DoyFromYmd(year, career::bal::MK_DECISION_MONTH, career::bal::MK_DECISION_DAY);
}
int MarketY1DecisionDoy(int year) {
    return career::dateutil::DoyFromYmd(year, career::bal::MK_Y1_DECISION_MONTH, career::bal::MK_Y1_DECISION_DAY);
}
int MarketPhaseFor(int year, int doy, int firstYear) {
    const int maxDoy = career::dateutil::DaysInYear(year);
    if (doy < 1) doy = 1; else if (doy > maxDoy) doy = maxDoy;   // clamps: never invents a fourth phase
    // year 1 runs an EARLY compressed cycle. You start in preseason, so the window is already open
    // from day one, and its decision week is February's. Once that passes, the year FALLS BACK to the normal calendar
    // (which is why there's no early `return` for the rest): in September the year-2 window opens as usual.
    if (firstYear > 0 && year == firstYear) {
        const int y1 = MarketY1DecisionDoy(year);
        if (doy < y1) return MARKET_WINDOW;
        if (doy < y1 + career::bal::MK_DECISION_WEEK_DAYS) return MARKET_DECISION;
    }
    if (doy >= MarketDecisionDoy(year))   return MARKET_DECISION;
    if (doy >= MarketWindowOpenDoy(year)) return MARKET_WINDOW;
    return MARKET_PADDOCK;
}
int WindowTargetYear(int year, int doy, int firstYear) {
    // year 1's compressed cycle: you sign for THIS year, the one about to start
    if (firstYear > 0 && year == firstYear &&
        doy < MarketY1DecisionDoy(year) + career::bal::MK_DECISION_WEEK_DAYS) return year;
    return year + 1;   // the rest of the time you sign for next season
}

// See career_market.h. The rule the player's own signing paths apply before calling AcceptOffer.
int SigningSeasonFor(int year, int doy, int firstYear) {
    const int fase = MarketPhaseFor(year, doy, firstYear);
    // Outside the window there are no signings: the only thing that signs there is the definitive renewal, which lands in
    // January with the clock already in the new season. Returning the clock's own year leaves it EXACTLY as it was.
    if (fase != MARKET_WINDOW && fase != MARKET_DECISION) return year;
    return WindowTargetYear(year, doy, firstYear);
}

// Days until a date in the year; if it already passed, until the SAME date next year. Never negative, so the
// calendar warning can fire without special-casing the year boundary.
static int daysToDateOfYear(int year, int doy, int targetDoy) {
    if (doy <= targetDoy) return targetDoy - doy;
    using namespace career::dateutil;
    return (DaysInYear(year) - doy) + targetDoy;
}
int DaysToMarketWindow(int year, int doy, int firstYear) {
    // the same gap as DaysToDecisionWeek: in year 1 the window is open FROM DAY ONE (you start in
    // preseason), so counting 243 days to September would send the player off to wait for a door
    // that's already open right in front of them. It closes once its February week passes, and only then does September's count.
    if (firstYear > 0 && year == firstYear && doy < MarketY1DecisionDoy(year)) return 0;
    const int abre = MarketWindowOpenDoy(year);
    if (doy >= abre && doy < MarketDecisionDoy(year)) return 0;   // you're already inside it
    return daysToDateOfYear(year, doy, abre);
}
int DaysToMarketClose(int year, int doy, int firstYear) {
    if (MarketPhaseFor(year, doy, firstYear) == MARKET_PADDOCK) return 0;
    int y = year, d = doy;
    for (int n = 1; n <= 400; n++) {   // the longest window lasts four months: 400 days is plenty
        if (++d > career::dateutil::DaysInYear(y)) { y++; d = 1; }
        if (MarketPhaseFor(y, d, firstYear) == MARKET_PADDOCK) return n;
    }
    return 0;
}
int DaysToDecisionWeek(int year, int doy, int firstYear) {
    // YEAR 1 DECIDES IN FEBRUARY, NOT DECEMBER. This countdown didn't know about `firstYear` while
    // MarketPhaseFor and MarketMarksForYear did, so in the first season it pointed at the
    // December week: on January 1st it told the player "348 days to go" for a date that was actually
    // only 45 days out. And it's not a cosmetic number — it's the countdown that decides whether there's time to
    // prove something before the seats get handed out.
    if (firstYear > 0 && year == firstYear) {
        const int y1 = MarketY1DecisionDoy(year);
        if (doy <= y1) return y1 - doy;      // the compressed one hasn't arrived yet: THAT'S the one that counts
        // once February's week passes, year 1 falls back to the normal calendar (same as MarketPhaseFor): December's is next
    }
    return daysToDateOfYear(year, doy, MarketDecisionDoy(year));   // the day itself gives 0; once past, it counts to the next one
}

// see career_market.h.
std::vector<MarketMark> MarketMarksForYear(int year, int firstYear) {
    using namespace career::dateutil;
    std::vector<MarketMark> out;
    // year 1's compressed cycle runs EARLY: the window is already open on day one and its decision
    // week falls in February. These are proper dates, not a special case drawn on the side.
    if (firstYear > 0 && year == firstYear) {
        MarketMark abre; abre.kind = MARK_WINDOW_OPEN; abre.doy = 1;
        abre.days = MarketY1DecisionDoy(year) - 1;
        if (abre.days < 1) abre.days = 1;
        out.push_back(abre);
        MarketMark dec; dec.kind = MARK_DECISION_WEEK; dec.doy = MarketY1DecisionDoy(year);
        dec.days = career::bal::MK_DECISION_WEEK_DAYS;
        out.push_back(dec);
    }
    // ...and after that the usual calendar, which year 1 also has (in September the year-2 window
    // opens just like in any other year).
    MarketMark abre; abre.kind = MARK_WINDOW_OPEN; abre.doy = MarketWindowOpenDoy(year);
    abre.days = MarketDecisionDoy(year) - abre.doy;
    if (abre.days < 1) abre.days = 1;
    out.push_back(abre);
    MarketMark dec; dec.kind = MARK_DECISION_WEEK; dec.doy = MarketDecisionDoy(year);
    // December's mark runs to year end: there's no season behind it to roll the clock back into
    dec.days = DaysInYear(year) - dec.doy + 1;
    if (dec.days < 1) dec.days = 1;
    out.push_back(dec);
    return out;
}

int DecisionWeekSkippedBetween(int fromAbsDay, int toAbsDay, int firstYear) {
    using namespace career::dateutil;
    if (toAbsDay <= fromAbsDay) return 0;
    // both the departure AND arrival years are checked: a jump can cross December 31st and with it the
    // December decision week, which is the one that hurts most to skip.
    int desde = 0, hasta = 0, tmpDoy = 0;
    YearDoyFromAbsDay(fromAbsDay, desde, tmpDoy);
    YearDoyFromAbsDay(toAbsDay,   hasta, tmpDoy);
    for (int y = desde; y <= hasta; y++) {
        for (const auto& m : MarketMarksForYear(y, firstYear)) {
            if (m.kind != MARK_DECISION_WEEK) continue;
            const int ini = AbsDayFromYearDoy(y, m.doy);
            const int fin = ini + m.days - 1;
            // "JUMPING OVER" means landing BEYOND the last day: if the destination falls inside the
            // week, you haven't skipped it — you arrived in time, which is exactly the case that needs no warning.
            if (fromAbsDay <= fin && toAbsDay > fin) return ini;
        }
    }
    return 0;
}

// =============================================================================================
// classes.json: grid_kind / grid_size per class. LAZY, load-ONCE (static guard),
// INDEPENDENT of g_useJsonCareerData (same pattern as career_data_json's EnsurePoolLoaded).
// Hardcoded path, same as the rest of the project. Missing class -> grid_kind "open", grid_size 0.
// =============================================================================================
namespace {

struct ClassMeta { std::string grid_kind = "open"; int grid_size = 0; };
std::map<std::string, ClassMeta> g_classMeta;
bool g_classesLoaded = false;

void EnsureClassesLoaded() {
    if (g_classesLoaded) return;
    g_classesLoaded = true;   // marks it loaded (a single attempt; if it fails to open, the map stays empty -> defaults)

    std::string err;
    jsonm::ValuePtr root = jsonm::ParseFile(paths::Data("career\\classes.json").c_str(), &err);   // data/ root
    if (!root || !root->isObject()) return;

    for (const auto& kv : root->obj) {
        const std::string& cls = kv.first;
        const jsonm::Value* v = kv.second.get();
        if (!v || !v->isObject()) continue;
        ClassMeta m;
        std::string gk = v->memberString("grid_kind", "open");
        m.grid_kind = (gk == "closed") ? "closed" : "open";   // only "closed" counts as closed; everything else is open
        m.grid_size = v->memberInt("grid_size", 0);
        g_classMeta[cls] = m;
    }
}

} // anon namespace

std::string GridKindForClass(const std::string& className) {
    EnsureClassesLoaded();
    auto it = g_classMeta.find(className);
    return (it == g_classMeta.end()) ? std::string("open") : it->second.grid_kind;
}

int GridSizeForClass(const std::string& className) {
    EnsureClassesLoaded();
    auto it = g_classMeta.find(className);
    return (it == g_classMeta.end()) ? 0 : it->second.grid_size;
}

// Diplomacy-diffusion helpers (team<->team relationship graph) --------------------------------------
static double allyKindWeight(const std::string& kind) {   // ALLY weight by kind (0 if not an ally)
    if (kind == "family") return career::bal::REL_KIND_FAMILY;
    if (kind == "ally")   return career::bal::REL_KIND_ALLY;
    return 0.0;
}
static double rivalKindWeight(const std::string& kind) {  // RIVAL magnitude by kind (0 if not a rival)
    if (kind == "rival")  return career::bal::REL_KIND_RIVAL;
    if (kind == "bitter") return career::bal::REL_KIND_BITTER;
    return 0.0;
}
// x's positive ALLY neighbors with their weight: peer (family/ally) + x->parent affiliation (weight from AffiliationStrengthFor).
static std::vector<std::pair<std::string,double>> posAllyNeighbors(const std::string& x) {
    std::vector<std::pair<std::string,double>> out;
    for (const auto& e : career::TeamRelationsFor(x)) {
        double w = allyKindWeight(e.kind);
        if (w > 0.0) out.push_back(std::make_pair(e.other, w));
    }
    const std::string parent = career::ParentTeamFor(x);
    if (!parent.empty()) out.push_back(std::make_pair(parent, career::AffiliationStrengthFor(x)));
    return out;
}
// AllyReach: best ALLY reach between from and to, up to REL_DIPLOMACY_MAX_HOPS hops (peer
// family/ally + junior-team->parent affiliation). 1 hop = the kind's weight (or the affiliation's); 2 hops = the product
// of the two weights damped by REL_DIPLOMACY_DECAY. 0 if there's no path, from==to, or either name is empty.
double AllyReach(const std::string& from, const std::string& to) {
    using namespace career::bal;
    if (from.empty() || to.empty() || from == to) return 0.0;
    double best = 0.0;
    const auto n1s = posAllyNeighbors(from);
    for (const auto& n1 : n1s) {                                  // 1 hop
        if (n1.first == to) { if (n1.second > best) best = n1.second; }
    }
    if (REL_DIPLOMACY_MAX_HOPS >= 2) {
        for (const auto& n1 : n1s) {                              // 2 hops: from -> m -> to
            if (n1.first == to || n1.first == from) continue;
            for (const auto& n2 : posAllyNeighbors(n1.first)) {
                if (n2.first != to) continue;
                double w = n1.second * n2.second * REL_DIPLOMACY_DECAY;
                if (w > best) best = w;
            }
        }
    }
    return best;
}
// RivalWeight: DIRECT rivalry magnitude (1 hop, kind rival/bitter) between from and to.
// 0 if there's no direct rival edge, from==to, or either name is empty.
double RivalWeight(const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from == to) return 0.0;
    double best = 0.0;
    for (const auto& e : career::TeamRelationsFor(from))
        if (e.other == to) { double w = rivalKindWeight(e.kind); if (w > best) best = w; }
    return best;
}

// =============================================================================================
// team RELATIONSHIP 0..1 for a series, derived from PERSISTED history. PURE +
// deterministic (no RNG, no new save field). Feeds wi.rel in GenerateOffers -> BOTH the why-bar (o.whyRel =
// rel*100) AND the soft matching (Want_T's L_t*rel term). PRECEDENCE (career_balance.h REL_*):
//   1. NO shared history -> EXACTLY bal::REL_BASE (0.50). CRITICAL: keeps test_mk_want_t == 0.5825 byte-for-byte.
//   2. active contract on this series (ContractForSeries) -> bal::REL_CURRENT_TEAM (precedence; returned now).
//   3. a pending renewal exists (pending_renewals carries of_<series_id>) -> at least bal::REL_RENEWAL (precedence).
//   4. otherwise: start at REL_BASE + ex-team (TENURE-SCALED: min(REL_EX_TEAM_PER_SEASON * seasonsHere,
//      REL_EX_TEAM_CAP), where seasonsHere = #completed_seasons in this series, or 1 via race_log fallback;
//      +REL_EX_TEAM_PODIUM if best final_pos <= REL_PODIUM_POS) + courted (courting has this series) - snubbed
//      (declined_offers or expired_offers has of_<series_id>), then CLAMP to [REL_MIN, REL_MAX].
// Realism: shared history WARMS a team to you; MORE seasons warm it MORE (diminishing returns, capped — a
// long-tenured driver is "family"); snubbing COOLS it. Current-team / renewal outrank history.
//
// Refinement — CROSS-SERIES BRAND AFFINITY (offerTeam, default ""): when the OFFER's team (the offered seat's
// team_label, passed by GenerateOffers) is an org you have history with in a DIFFERENT series, warm by
// REL_BRAND_AFFINITY. "Org footprint in your history" = any completed_seasons[].team_id OR active contracts[].
// team_id (BOTH in a series != this one) OR any courting[].team_id (the org courted you anywhere) matching
// offerTeam (case-sensitive exact; the labels are canonical). This is ADDITIVE (alongside ex-team/courted/
// snubbed, before the clamp); it does NOT override the current-team / pending-renewal precedence returns (a
// brand match doesn't make you their actual current driver). offerTeam == "" -> NO brand term -> byte-identical
// to the prior path (so test_mk_want_t stays 0.5825 and the no-history default stays exactly REL_BASE).
// NOTE: completed_seasons.team_id only populates for seasons archived after this field was added, so brand-affinity STRENGTHENS
// as a career progresses (expected; not retroactive).
//
// Inbox redesign, press — REL_MARKS: small warmth/chill "marks" the player's press tone (and other
// mail families) leave on a specific team (career::SaveData::rel_marks, keyed by TEAM LABEL like offerTeam).
// When offerTeam is non-empty, career::RelMarksSum(save, offerTeam, save.current_world_year) sums every mark
// aimed at that org, ALREADY DECAYED (weight ×0.5 per year of age, dropped past 3 years old). ADDITIVE
// (alongside ex-team/courted/snubbed/brand/diplomacy, before the clamp) and it also escapes precedence-1 on
// its own (a mark with no other shared history still warms/chills above/below REL_BASE). offerTeam == "" ->
// NO marks term -> byte-identical to the previous path.
double TeamRelationship(const career::SaveData& save, const std::string& series_id, const std::string& offerTeam) {
    using namespace career::bal;
    if (series_id.empty()) return REL_BASE;                       // no series -> neutral (defensive)
    const std::string offerId = "of_" + series_id;               // declined/expired/renewal id keying

    // precedence 2: you hold an active contract on this series -> they're YOUR team. ---
    if (career::ContractForSeries(save, series_id) != nullptr) return REL_CURRENT_TEAM;

    // precedence 3: a pending renewal for this series -> they're trying to keep you. ---
    for (const auto& ren : save.pending_renewals)
        if (ren.id == offerId) return REL_RENEWAL;

    // Refinement: CROSS-SERIES BRAND AFFINITY — does the offer's org appear in your history ELSEWHERE? ---
    // Evaluated here (after the precedence returns, with the additive terms below) so it never overrides being
    // the team's actual current driver / a pending renewal. offerTeam == "" short-circuits (no brand term).
    bool brandAffinity = false;
    if (!offerTeam.empty()) {
        // (a) drove for this org in ANOTHER series (an archived season whose team_id == offerTeam, different series).
        for (const auto& cs : save.completed_seasons)
            if (cs.series_id != series_id && cs.team_id == offerTeam) { brandAffinity = true; break; }
        // (b) you CURRENTLY drive for this org in another series (a multi-series org you race for elsewhere).
        if (!brandAffinity)
            for (const auto& c : save.contracts)
                if (c.active && c.series_id != series_id && c.team_id == offerTeam) { brandAffinity = true; break; }
        // (c) the org courted you (anywhere — a courting footprint keyed by team_id).
        if (!brandAffinity)
            for (const auto& co : save.courting)
                if (co.team_id == offerTeam) { brandAffinity = true; break; }
    }

    // DIPLOMACY DIFFUSION. Generalizes the single-hop junior-team->parent propagation to multi-hop
    //     over the ally graph (peer family/ally + affiliations). The reputation of the teams in the
    //     player's history (archived seasons + active contracts) flows along ally edges to offerTeam (<=2
    //     hops, decayed, capped); DIRECT rivals cool it. offerTeam == "" -> 0 (byte-compat). ---
    double warmthDiplomacy = 0.0, coolDiplomacy = 0.0;
    if (!offerTeam.empty()) {
        auto accumulate = [&](const std::string& srcTeam) {
            if (srcTeam.empty() || srcTeam == offerTeam) return;   // the mark itself is already covered by brandAffinity
            warmthDiplomacy += AllyReach(srcTeam, offerTeam)   * career::bal::REL_DIPLOMACY_PER_SEASON;
            coolDiplomacy   += RivalWeight(srcTeam, offerTeam) * career::bal::REL_DIPLOMACY_PER_SEASON;
        };
        for (const auto& cs : save.completed_seasons) accumulate(cs.team_id);
        for (const auto& c : save.contracts) if (c.active) accumulate(c.team_id);
        if (warmthDiplomacy > career::bal::REL_DIPLOMACY_CAP) warmthDiplomacy = career::bal::REL_DIPLOMACY_CAP;
        if (coolDiplomacy   > career::bal::REL_RIVAL_COOL_CAP) coolDiplomacy   = career::bal::REL_RIVAL_COOL_CAP;
    }
    const bool hasDiplomacyHistory = (warmthDiplomacy > 0.0 || coolDiplomacy > 0.0);

    // Inbox redesign, press: rel_marks — one-off warmth/chill notes recorded toward a
    //     TEAM (career::RelMark, career_save.h; written by press replies and other mail families).
    //     Keyed by TEAM LABEL just like offerTeam -> the term ONLY applies when offerTeam is non-empty
    //     (same gate as brandAffinity/diplomacy; offerTeam=="" -> 0, byte-compat with the unrefined path).
    //     TeamRelationship doesn't receive a year -> we use save.current_world_year (RelMarksSum already has the fallback: if
    //     it's 0/negative, marks weigh x1 -- see career::RelMarksSum). Already comes DECAYED (weight x0.5/year, and
    //     dropped past 3 years). ---
    double relMarks = offerTeam.empty() ? 0.0 : career::RelMarksSum(save, offerTeam, save.current_world_year);
    const bool hasRelMarks = (relMarks != 0.0);

    // additive history: ex-team / courted / snubbed (only matters once we reach here). ---
    // CompletedSeason.team_id is now PERSISTED (the team you drove for each archived season).
    // This is NOT yet read here — the future per-org cross-series brand-affinity term wires it once
    // real-team naming lands; today's ex-team warmth stays SERIES-keyed (tenure in this series), unchanged.
    // ex-team / shared history: this series appears in the driver's record OR the race-time log. TENURE-SCALED — count
    // the seasons raced (one completed_seasons entry per series+year). Also peek at the BEST final_pos in
    // completed_seasons for a small warmer when you finished well there.
    int  seasonsHere  = 0;
    bool finishedWell = false;
    for (const auto& cs : save.completed_seasons) {
        if (cs.series_id == series_id) {
            ++seasonsHere;
            if (cs.final_pos > 0 && cs.final_pos <= REL_PODIUM_POS) finishedWell = true;
        }
    }
    // race_log fallback: if there are NO completed_seasons but you've raced the series, count it as ONE season
    // of tenure (a partial / in-progress season — enough shared history to warm above neutral, but not stacked).
    if (seasonsHere == 0) {
        for (const auto& rl : save.race_log)
            if (rl.series_id == series_id) { seasonsHere = 1; break; }
    }
    const bool sharedHistory = (seasonsHere > 0);
    // courted: a courting footprint for this series (an offer they remembered you with).
    bool courted = false;
    for (const auto& c : save.courting)
        if (c.series_id == series_id) { courted = true; break; }
    // snubbed: you declined OR let their offer expire OR the team withdrew the deal mid-negotiation
    // (the negotiation redesign: NegoPlayerWalk/NegoCloseElsewhere feed withdrawn_offers the same way).
    const bool snubbed = (save.declined_offers.count(offerId) > 0) ||
                         (save.expired_offers.count(offerId) > 0) ||
                         (save.withdrawn_offers.count(offerId) > 0);

    // precedence 1: NO shared history of ANY kind -> EXACTLY REL_BASE (back-compat; test_mk_want_t==0.5825). ---
    // Refinement: a CROSS-SERIES brand match counts as "shared history" here too — a pure brand match (no
    // series-keyed tenure/courting/snub) must still warm above neutral, so it cannot be short-circuited to base.
    // a rel_mark alone (no other shared history) must also escape REL_BASE — a press-tone warmth/chill with
    // an org you've never raced for is still a real, if small, signal.
    if (!sharedHistory && !courted && !snubbed && !brandAffinity && !hasDiplomacyHistory && !hasRelMarks) return REL_BASE;

    double rel = REL_BASE;
    if (sharedHistory) {
        double exTeam = REL_EX_TEAM_PER_SEASON * (double)seasonsHere;  // tenure-scaled warmth
        if (exTeam > REL_EX_TEAM_CAP) exTeam = REL_EX_TEAM_CAP;        // diminishing returns -> cap
        rel += exTeam;
        if (finishedWell) rel += REL_EX_TEAM_PODIUM;                   // podium sweetener on top (still clamped below)
    }
    if (courted)       rel += REL_COURTED;
    if (snubbed)       rel -= REL_SNUBBED;
    if (brandAffinity) rel += REL_BRAND_AFFINITY;                // cross-series org footprint warmth
    rel += warmthDiplomacy;          // ally warmth (multi-hop, capped)
    rel -= coolDiplomacy;            // direct-rival chill (capped)
    rel += relMarks;                 // press warmth/chill marks (already decayed, keyed to offerTeam)
    if (rel < REL_MIN) rel = REL_MIN;                            // clamp [REL_MIN, REL_MAX]
    if (rel > REL_MAX) rel = REL_MAX;
    return rel;
}

// =============================================================================================
// two-sided matching (PURE): RivBoost + Want_T (team wants driver).
// =============================================================================================
double RivBoost(const WantInputs& in) {
    double b = career::bal::MK_RIV_O * in.nemesisFrac;
    if (in.sameCat)  b += career::bal::MK_RIV_O;
    if (in.sameTeam) b += career::bal::MK_RIV_O;
    return b;
}

double Want_T(const WantInputs& in) {
    using namespace career::bal;
    double q          = career::RatingQuality(in.rating);          // 0..1 logistic
    double prestigeFit= 1.0 - std::abs(q - in.prestige / 100.0);
    if (prestigeFit < 0.0) prestigeFit = 0.0;
    double merit      = MK_W_Q * q + MK_W_P * prestigeFit;
    double want       = MK_L_TEAM * in.rel + (1.0 - MK_L_TEAM) * merit;
    if (in.contractKind == CONTRACT_PAY_DRIVER)
        want *= 1.0 + MK_K_PAY * in.money_norm;                    // money lowers the bar (needy teams)
    want *= 1.0 + MK_W_SPONSOR * in.sponsorFit;                    // fills sponsorFit; 0 stub today
    want *= 1.0 + RivBoost(in);
    // Soft terms layered onto the offer score. Each is a NO-OP at WantInputs defaults (form=MK_FORM_NEUTRAL,
    //      tier_gap=0, rating_races=K_PROVISIONAL_RACES) so the byte-identical core + test_mk_want_t==0.5825 hold.
    // form: trending-up drivers court better; one shared CareerForm source with the why-bar.
    want *= 1.0 + MK_W_FORM * (in.form - MK_FORM_NEUTRAL);
    // jump: a tier-gap beyond the freebie penalises desire (a big leap is a harder sell). Clamp >= 0.
    int gap = in.tier_gap - MK_JUMP_FREE;
    if (gap > 0) {
        double jumpPen = 1.0 - MK_W_JUMP * (double)gap;
        if (jumpPen < 0.0) jumpPen = 0.0;
        want *= jumpPen;
    }
    // cleanliness: a dirty driver courts worse. MK_W_CLEAN is now LIVE (0.10),
    // but the penalty ONLY bites BELOW CLEAN_TEAMINTEREST_FLOOR (55) — proportional to how far below. At/above the
    // floor (incl. the WantInputs default 100.0) the deficit is 0 -> factor == 1.0 (NO-OP, keeps test_mk_want_t).
    // Clamp the factor >= 0 (mirrors the jump-penalty clamp above).
    {
        double deficit = CLEAN_TEAMINTEREST_FLOOR - in.cleanliness;
        if (deficit > 0.0) {
            double cleanPen = 1.0 - MK_W_CLEAN * (deficit / CLEAN_TEAMINTEREST_FLOOR);
            if (cleanPen < 0.0) cleanPen = 0.0;
            want *= cleanPen;
        }
    }
    // provisional-spike discount: damp desire while the rating is still provisional.
    if (in.rating_races < K_PROVISIONAL_RACES) want *= MK_PROVISIONAL_DISCOUNT;
    return want;
}

// teamInterest: hard GATE_fit on top of the tested Want_T soft core. 0 on gate-fail, else Want_T.
// PURE wrapper — it does NOT re-implement any Want_T math; the soft score stays inside Want_T. The caller
// (GenerateOffers) already prunes gate-failing seats via `continue`, so want_t never carries a 0 into the
// ordering; this wrapper is the named, testable contract for "no fit -> no interest".
double teamInterest(const WantInputs& in, bool gateFit) {
    if (!gateFit) return 0.0;
    return Want_T(in);
}

// gateAfford: PURE signability check. Fee-waived seats (works/customer/signed — the team or the
// budget covers any buy-in) are always affordable. Otherwise the player needs credits >= cost * MK_AFFORD_MARGIN.
// This NEVER drops an offer or zeroes teamInterest — the caller computes it and stamps OfferDef.canAfford so the
// UI can soft-mark the Accept button. Currently DORMANT.
bool gateAfford(long credits, long cost, bool feeWaived) {
    if (feeWaived) return true;
    if (cost <= 0) return true;                                   // nothing to pay up front -> affordable
    return (double)credits >= (double)cost * career::bal::MK_AFFORD_MARGIN;
}

double PrestigeAttract(double prestige) {
    double a = (prestige - career::bal::MK_PRESTIGE_LO) / career::bal::MK_PRESTIGE_SPAN;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    return a;
}

double Want_D(const WantInputs& in) {
    using namespace career::bal;
    double q   = career::RatingQuality(in.rating);
    double pa  = PrestigeAttract(in.prestige);
    double same= in.sameContinent ? 1.0 : 0.0;
    double continentPen = 1.0 - (1.0 - same) * (1.0 - pa) * MK_CONTINENT_PEN;
    double core= MK_L_DRIVER * in.rel + (1.0 - MK_L_DRIVER) * q;
    return core * pa * continentPen * (1.0 + RivBoost(in));
}

OfferTermsOut OfferTerms(double rating, int level, int contractKind, double coverage) {
    using namespace career::bal;
    OfferTermsOut out;
    int lv = level < 1 ? 1 : (level > 7 ? 7 : level);
    double q = career::RatingQuality(rating);
    if (contractKind == CONTRACT_PAY_DRIVER) {
        double bi = (double)MK_PAYDRIVER_BUYIN[lv - 1] * (1.0 - q);   // worse fit (low q) -> higher buy-in
        out.buy_in = (long)(bi + 0.5);
        return out;                                                   // pay_driver pays no salary
    }
    if (contractKind == CONTRACT_CUSTOMER) return out;               // self-funded: no salary, no bonus
    // works / trial / conditional: salaried (trial/conditional reuse the works pay shape)
    double base    = (double)FACTORY_SEAT_SALARY * (double)lv / 7.0;
    double perRace = base * (0.7 + 0.6 * q) * (1.0 - coverage * MK_K_COV);
    out.per_race   = (long)(perRace + 0.5);
    out.sign_bonus = (long)((double)out.per_race * MK_K_BONUS + 0.5);
    return out;
}

// Variable salary: like OfferTerms, but the salary comes from career::SalaryFor (variable
// by series x team x rating via TeamSalaryBand + SeriesSalaryAnchor) instead of the flat level/7 curve.
// buy_in (pay_driver) stays byte-identical to OfferTerms.
// The salary is decided by the contract's PROFILE (salary_dir of the type derived from
// kind+archetype), NOT the kind directly. The settle only pays dealSalary if salary_dir>0 (works/factory types,
// emitting per_race for an unsalaried trial/works type used to create a phantom figure that
// the pre-race screen showed but the settle never paid out. Now both read the SAME source -> what's shown == what's paid.
OfferTermsOut OfferTermsCtx(double rating, int level, int contractKind, int archetype, const std::string& seriesId,
                            const std::string& teamLabel, const career::SaveData& save, int worldYear) {
    using namespace career::bal;
    OfferTermsOut out;
    int lv = level < 1 ? 1 : (level > 7 ? 7 : level);
    double q = career::RatingQuality(rating);
    if (contractKind == CONTRACT_PAY_DRIVER) {
        double bi = (double)MK_PAYDRIVER_BUYIN[lv - 1] * (1.0 - q);   // worse fit (low q) -> higher buy-in
        out.buy_in = (long)(bi + 0.5);
        return out;                                                   // pay_driver pays no salary
    }
    // salary ONLY if the type's profile pays it (salary_dir>0). Matches the settle logic
    // (`if (p.salary_dir > 0) t.dealSalary = c->per_race`). customer/trial/self-funded (salary_dir<=0) -> 0.
    if (career::ContractCoverageProfile(contractKind, archetype).salary_dir > 0) {
        out.per_race   = career::SalaryFor(seriesId, teamLabel, lv, rating, /*salaryPct*/NEGO_SALARY_MID, save, worldYear);
        out.sign_bonus = (long)((double)out.per_race * MK_K_BONUS + 0.5);
    }
    return out;
}

bool TryoutRequired(double wantT, int level, int contractKind) {
    using namespace career::bal;
    if (contractKind != CONTRACT_WORKS && contractKind != CONTRACT_CONDITIONAL) return false;
    return wantT >= MK_TRYOUT_WANT_T && level >= MK_TRYOUT_MIN_LEVEL;
}

// "coming from accidents" — true when the player's cleanliness EMA has dropped below
// the accident floor. The gray-zone trial trigger-set reads this as one of its OR conditions.
bool ComingFromAccidents(double cleanliness) {
    return cleanliness < career::bal::CLEAN_ACCIDENT_TRIGGER_FLOOR;
}

// Gray-zone trial trigger-set — the offer gate that REPLACES TryoutRequired. PURE.
// Order of evaluation matters: the NEVER branches override every gray-zone condition (design a self-funded /
// wildcard / renewal seat is never a trial; a team that approached you for a recent result skips it; a low
// category courts freely). Then a one-time-entry archetype (junior/factory program) ALWAYS requires the entry
// trial. Otherwise the gray-zone OR-set decides.
bool RequiresTrial(int archetype, int targetLevel, int frontierPhase, double cleanliness,
                   bool disputedSeat, bool topTeam, bool recentStrongResult,
                   bool overReach, bool approachedForResult) {
    using namespace career::bal;
    const career::ArchetypePreset& ap = career::ArchetypeInfo(archetype);

    // NEVER branches (hard overrides) ----
    // SELF_FUNDED_ENTRY / WILDCARD / RENEWAL all carry trialRule == ARCH_TRIAL_NEVER (career_archetype.cpp table).
    if (ap.trialRule == career::ARCH_TRIAL_NEVER) return false;
    // Team approached YOU for a recent title/result -> no trial (you've already proven it on track).
    if (approachedForResult) return false;
    // Low category: the gray-zone gate is for big jumps / contested / top seats; entry tiers court freely.
    if (targetLevel < TRIAL_MIN_LEVEL) return false;

    // ONE-TIME-ENTRY (junior / factory program): the entry trial always applies ----
    if (ap.trialRule == career::ARCH_TRIAL_ONETIME) return true;

    // GRAY-ZONE OR-set (ARCH_TRIAL_GRAYZONE) ----
    const bool bigJump      = (targetLevel - frontierPhase) >= 2;          // 2+ category-level jump
    const bool topNoResult  = topTeam && !recentStrongResult;             // marquee seat, unproven
    const bool dirty        = ComingFromAccidents(cleanliness);           // coming from accidents (below the trigger floor)
    return bigJump || disputedSeat || topNoResult || overReach || dirty;
}

int ContractKindForSeat(bool closedSeat, double q, double seatBarQ, double money_norm, bool hasPromoteTarget) {
    using namespace career::bal;
    if (closedSeat) {
        // a pay-driver seat is also allowed on CLOSED (works) seats — checked BEFORE the quality-bar branch so
        // a below-pace, funded player buys in instead of getting a bare works/trial slot. Excluded when
        // hasPromoteTarget (a ladder-rung series, e.g. ivory_trophy_7202 -> harbor_challenge_7161, stays trial/conditional instead of
        // being hijacked into a buy-in — mirrors the open-seat branch below, which checks hasPromoteTarget FIRST)
        // and gated by the competence FLOOR (MK_PAYDRIVER_SKILL_FLOOR) so this function enforces it directly
        // instead of relying on the upstream payDriverEligible/fitPass emission gate by coincidence.
        if (!hasPromoteTarget && money_norm > 0.0 && q < seatBarQ && q >= seatBarQ - MK_PAYDRIVER_SKILL_FLOOR) return CONTRACT_PAY_DRIVER;
        // factory seat: clear the quality bar -> works; under it but offered (stretch) -> trial
        return (q >= seatBarQ) ? CONTRACT_WORKS : CONTRACT_TRIAL;
    }
    // open seat
    if (hasPromoteTarget)               return CONTRACT_CONDITIONAL;   // development ladder
    if (money_norm > 0.0 && q < seatBarQ) return CONTRACT_PAY_DRIVER;  // you buy the opportunity
    return CONTRACT_CUSTOMER;                                          // plain self-funded apply
}

// =============================================================================================
// SeatsForSeries — derives the player's POTENTIAL seat. 1 per series in v1.
// =============================================================================================
namespace {

int clampIdx(int i, int lo, int hi) { return i < lo ? lo : (i > hi ? hi : i); }

} // anon namespace

std::vector<MarketSeat> SeatsForSeries(const career::SeriesDef& sd) {
    using namespace career::bal;
    std::vector<MarketSeat> out;

    MarketSeat seat;
    seat.series_id  = sd.id;
    seat.class_name = sd.class_name;
    seat.level      = sd.level;

    const bool closed = (GridKindForClass(sd.class_name) == "closed");
    seat.works = closed;

    // reqRep re-anchored onto the RATING scale. Closed (works) seats demand more
    // (never 0); open ads at low tiers are free. ReqRepRating returns a rating threshold; store it as int.
    seat.reqRep = (int)(career::ReqRepRating(sd.level, closed) + 0.5);

    out.push_back(seat);
    return out;
}

// =============================================================================================
// GenerateOffers — combines seats + player state into offers.
// =============================================================================================
namespace {

// Deterministic disc color per class (display tint; the UI tries FindClass first and falls back to this).
// FNV-1a of the class_name -> a fixed hue, pleasant S/V. Deterministic across runs/compilers.
unsigned long fnv1a(const std::string& s) {
    unsigned long h = 2166136261UL;
    for (unsigned char c : s) { h ^= c; h *= 16777619UL; }
    return h;
}
ImU32 deterministicColor(const std::string& className) {
    unsigned long h = fnv1a(className);
    // Simple HSV->RGB with S=0.55, V=0.85; hue = h%360.
    double H = (double)(h % 360), S = 0.55, V = 0.85;
    double C = V * S, X = C * (1 - std::abs(std::fmod(H / 60.0, 2.0) - 1)), m = V - C;
    double r = 0, g = 0, b = 0;
    if      (H < 60)  { r = C; g = X; }
    else if (H < 120) { r = X; g = C; }
    else if (H < 180) { g = C; b = X; }
    else if (H < 240) { g = X; b = C; }
    else if (H < 300) { r = X; b = C; }
    else              { r = C; b = X; }
    auto to8 = [&](double v) -> unsigned { int q = (int)((v + m) * 255.0 + 0.5); return (unsigned)(q < 0 ? 0 : (q > 255 ? 255 : q)); };
    // IM_COL32(r,g,b,a) = a<<24 | b<<16 | g<<8 | r  (ImGui's in-memory ABGR).
    return (ImU32)((255u << 24) | (to8(b) << 16) | (to8(g) << 8) | to8(r));
}

// Is series sid GATED? An EXACT replica of the calendar's gating logic (gatedBy): a series is
// gated iff it appears as the TARGET (`unlocks`) of some qualifier series. unlockedGates stores the
// series_id of the destination CHAMPIONSHIP (sd.id), not of the qualifier.
bool seriesIsGated(const std::string& sid) {
    for (const auto& q : career::AllSeries())
        if (q.unlocks == sid) return true;
    return false;
}

// The series this one UNLOCKS (its `unlocks` target), if any -> the conditional-contract promotion target.
std::string SeriesUnlockTarget(const std::string& sid) {
    for (const auto& q : career::AllSeries())
        if (q.id == sid && !q.unlocks.empty()) return q.unlocks;
    return std::string();
}

// Diagnostic: GenerateOffers writes one trace line to the same log the overlay uses
// (userdata/logs/zta_engine.log via career::EngineLogPath). Portable (fopen append) so it
// works in BOTH the overlay DLL and the pure career_test.exe build — the live checkpoint reads this log to
// PROVE the bridge omission (rating reads the 1150 rookie default BEFORE the bridge fix, the real value + N>0 after).
// OFF BY DEFAULT — a MEASURED cause of in-race stutter, reported by testers.
// This trace was born for a one-off diagnosis (a bridge that was omitting in.rating) that got
// resolved long ago, but the trace stayed on. And it isn't "one log line": it OPENS AND CLOSES the file on
// EVERY call, and GenerateOffers emits one line per direct offer plus the summary.
// MEASURED in a VR tester's log, within ONE 15-minute race:
//     1596 [offer-direct] + 532 [offers]  =  2128 open-write-close cycles
// i.e. more than two SYNCHRONOUS file operations per second while driving, because offers
// regenerate every ~1.7s. Of 4892 log lines from that race, 4250 were this.
// The mechanism stays, the noise doesn't: config.json "market_log": true re-enables it for diagnosis.
static volatile bool g_mktLog = false;
void MktLogF(const char* fmt, ...) {
    if (!g_mktLog) return;
    if (career::LogCaptureActive()) {   // 0.7.0 review (finding 7): clock forecast, it has not happened
        char buf[1024];
        va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
        career::LogCaptureEngineLine(buf);
        return;
    }
    FILE* f = paths::FOpen(career::EngineLogPath(), "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fclose(f);
}

} // anon namespace

// The switch is exposed outside the anonymous namespace (inside, it would have internal linkage and the DLL couldn't
// call it). career_overlay turns it on at startup if config.json has "market_log": true.
void SetMarketLogging(bool on) { g_mktLog = on; }

// Junior-team promotion destination series — the LOWEST level > canteraLevel where `parent` fields
// a real car (TeamLabelsForClass includes it). "" if the parent has no seat in a higher tier (realistic —
// not every parent team has a seat above).
std::string PromotionDestinationForParent(const std::string& parent, int canteraLevel) {
    if (parent.empty()) return std::string();
    const career::SeriesDef* best = nullptr;
    for (const auto& sd : career::AllSeries()) {
        if (sd.level <= canteraLevel) continue;
        if (!career::SeriesPlayable(sd)) continue;   // never promote into an unplayable series (its content pack is missing)
        const auto labels = career::TeamLabelsForClass(sd.class_name);
        bool has = false; for (const auto& t : labels) if (t == parent) { has = true; break; }
        if (!has) continue;
        if (!best || sd.level < best->level) best = &sd;   // the lowest tier above the junior team
    }
    return best ? best->id : std::string();
}

// After the main GenerateOffers loop, if the player beats their cohort in an
// affiliated junior team (ParentTeamFor non-empty + PlayerWinsSeat), the parent team issues a PROMOTION offer
// (a real works seat) to the higher tier where it fields a real car. Deterministic (PlayerWinsSeat
// is pure), additive, a no-op for contracts with no parent or no higher destination.
void AppendAcademyPromotions(const OfferGenInput& in, const career::SaveData& save, std::vector<GenOffer>& out) {
    for (const auto& c : save.contracts) {
        if (!c.active) continue;
        const std::string parent = career::ParentTeamFor(c.team_id);
        if (parent.empty()) continue;                                    // an independent junior team -> no promotion
        const career::SeriesDef* cantera = nullptr;
        for (const auto& sd : career::AllSeries()) if (sd.id == c.series_id) { cantera = &sd; break; }
        if (!cantera) continue;
        if (!career::PlayerWinsSeat(save, cantera->class_name, c.team_id, "",
                                    in.money_norm, in.world_year ? in.world_year : in.year,
                                    career::RosterForTier(cantera->level)))
            continue;                                                    // the cohort beats you -> you don't get promoted yet
        const std::string destId = PromotionDestinationForParent(parent, cantera->level);
        if (destId.empty()) continue;
        const career::SeriesDef* dest = nullptr;
        for (const auto& sd : career::AllSeries()) if (sd.id == destId) { dest = &sd; break; }
        if (!dest) continue;
        // A promotion shares its offer id ("of_"+destId) with the rest of the
        // market -- a withdrawn/expired one doesn't reappear as a Promotion either (the same cut as the main loop
        // above, replicated here because this appendix builds its GenOffer outside that loop).
        const std::string suppressId = "of_" + destId;
        if (save.lapsed_offers.count(suppressId) > 0 || save.withdrawn_offers.count(suppressId) > 0) continue;
        GenOffer go; OfferDef& o = go.def;
        o.id = "of_" + destId; o.direct = true; o.team = parent; o.from = dest->class_name;
        o.clsId = dest->class_name; o.color = deterministicColor(dest->class_name);
        o.scope = "season"; o.contractKind = career::bal::CONTRACT_WORKS; o.reqRep = 0;
        o.seatName = dest->display; o.commitment = 0;
        // Variable salary: full context on hand (destId/parent/save) -> OfferTermsCtx (variable
        // salary via career::SalaryFor) instead of OfferTerms' flat level/7 curve.
        OfferTermsOut terms = OfferTermsCtx(in.rating, dest->level, career::bal::CONTRACT_WORKS,
                                             career::ARCH_ACADEMY_PROMOTION, dest->id, parent,
                                             save, in.world_year ? in.world_year : in.year);   // works+academy -> type 4 (salaried)
        o.perRace = terms.per_race; o.signBonus = terms.sign_bonus;
        o.title = "Ascenso a " + dest->display; o.title_en = "Promotion to " + dest->display;
        o.pitch    = parent + " te asciende desde su academia.";
        o.pitch_en = parent + " promotes you from its academy.";
        o.whyRating = (int)(career::RatingQuality(in.rating) * 100.0 + 0.5);
        o.archetype = career::ARCH_ACADEMY_PROMOTION;
        // an academy promotion does NOT skip the superlicense — a gated destination (IVORY_CUP_12573/NIMBUS_MASTERS_5890 PINNACLE_CHALLENGE_5480/F1)
        // arrives COURTED ("requires superlicense"), the same gateLock as GenerateOffers' main loop.
        const bool gateLock = seriesIsGated(destId) &&
                              !(in.unlocked_gates && in.unlocked_gates->count(destId) > 0);
        go.held = gateLock; go.tier_locked = gateLock;
        if (gateLock) { go.lock_reason = "requiere superlicencia"; go.lock_reason_en = "requires superlicense"; }
        go.archetype = o.archetype; go.want_t = 1.0;
        // dedup = REPLACE-or-APPEND: a generic held offer for the same destination gets replaced by the promotion.
        bool replaced = false;
        for (auto& g : out) if (g.def.id == o.id) { g = std::move(go); replaced = true; break; }
        if (!replaced) out.push_back(std::move(go));
    }
}

std::vector<GenOffer> GenerateOffers(const OfferGenInput& in, const career::SaveData* save,
                                     std::vector<SeatContestReport>* contestsOut) {
    std::vector<GenOffer> out;
    using namespace career;
    using namespace career::bal;

    // the cursor's absolute day — consumed by the cooldown after a missed trial. With cursor_doy 0
    // (callers that don't set it) it comes out as day 1 of the year, which is harmless: it's only compared against resolved_day, and
    // with no trials in the save there's nothing to compare against.
    const int absToday = career::dateutil::AbsDayFromYearDoy(in.year, in.cursor_doy > 0 ? in.cursor_doy : 1);

    // Diagnostic state: count expired-omitted series + capture the first seat's reqRep so the
    // single trace line below proves the empty-board cause (rating-blind bridge / absolute-doy expiry).
    int diagExpired = 0;
    int diagFirstReqRep = -1;

    // ONE DRIVER, ONE SEAT. Whoever is already staying put next season can't be
    // contesting another seat against you. Resolved ONCE for the whole board: internally it walks the entire series
    // list, and doing it per seat would be quadratic. With no `save` there's no living world -> an empty, inert set.
    const std::set<std::string> staying =
        save ? career::StayingDrivers(*save, in.world_year ? in.world_year : in.year) : std::set<std::string>();

    for (const auto& sd : AllSeries()) {
        if (IsEventOnlySeries(sd)) continue;   // 01/10: event-only: with no calendar it never leaves the window -> it would always offer
        // Excludes ERA MEMORIES: a bygone-era series (pre-2000;
        // ONYX TROPHY 560, Greyham Motors Lum998, Group A/C, HARBOR_CHALLENGE_8227 87/92, …) is REVIVED as your ancestor in Era Memories — it's NOT a
        // modern career seat. Kept out of the driver Market (the SAME predicate the
        // Memories screen uses to list them -> Market and Memories are complementary). The ONLY way to offer one of these
        // classes is an EXHIBITION contract, a separate generation path; the normal
        // team-offer loop simply skips it here. (A single chokepoint: covers Market/mail/expiry.)
        if (IsEraMemorySeries(sd)) continue;

        // a series that's UNPLAYABLE with the installed content (its class ended up with no car — the content pack is missing).
        // Kept out of the market: offering a seat that can't be raced is a trap. FAIL-OPEN inside
        // SeriesPlayable (an empty live catalog = everything playable). Same chokepoint as Era Memories.
        if (!SeriesPlayable(sd)) continue;

        // RELEVANCE BAND: with a living world (save), NEVER court the player for
        //      a series more than MK_OFFER_BAND_BELOW tiers BELOW their phase — a GT3 driver doesn't get courted
        //      by karting teams. A uniform FLOOR that applies to ALL paths (open ads, DISPLACEMENT, and
        //      freed seats): a seat far below is spam, not an opportunity. Runs BEFORE the
        //      displacement scan (otherwise a strong player would displace the backmarker of every low series and the band
        //      would never bite). Floor only: series ABOVE the phase already come out 'held'/courting further below.
        //      save null (legacy) -> inert (previous static behavior untouched). A relevance filter, NOT a
        //      cap: it doesn't truncate a set, it discards series that are irrelevant by tier.
        if (save && sd.level < in.frontier_phase - career::bal::MK_OFFER_BAND_BELOW) continue;

        // 18/09 — THE WINDOW FIRST, so as not to pay for what will be thrown away. With the daily pulse this function
        // runs every day and was 84 % of the pulse (32 of 38 ms): it computed the shift and the seats of
        // ALL series to later discard most of them by window. Here the window is computed first
        // (it's cheap: the projected calendar of the year), and an OPEN series that falls outside it is dropped right away.
        // The result doesn't change at all: for an OPEN series outside the window neither the shift nor the
        // seats could cut it earlier (that cut is only for CLOSED ones), so its only destiny was the
        // window's `continue`, after the lapsed/withdrawn suppression, which is looked at here in the same order
        // so that the `diagExpired` diagnostic counter comes out identical.
        // D-OFF04-02 (G2): SEASON-RELATIVE offer window behind MK_OFFER_HORIZON_DOY (see the block below).
        // CalendarForSeriesInYear(sd.id, in.year) -- opener PROJECTED onto the current year, not the raw CalendarForSeries
        // (base kStartYear); every real caller already fills in.year (career_contract.cpp's RunWorldTick,
        // career_overlay.cpp, tools/career_sim.cpp, tools/careerproof.cpp).
        std::vector<CalRound> cal = CalendarForSeriesInYear(sd.id, in.year);
        int round1doy = 0;
        for (const auto& c : cal) if (c.round == 1) { round1doy = c.doy; break; }
        int delta = round1doy - in.cursor_doy;            // days until this season's opener
        if (delta < 0) delta += 365;                      // wrap: a past opener = "next year's opener"
        bool inWindow = (delta <= MK_OFFER_HORIZON_DOY);  // surface seats opening within the horizon
        const bool fueraDeVentana = (round1doy > 0 && in.cursor_doy > 0 && !inWindow);
        if (fueraDeVentana && !sd.closed) {
            if (save) {
                const std::string suppressId = "of_" + sd.id;
                if (save->lapsed_offers.count(suppressId) > 0 || save->withdrawn_offers.count(suppressId) > 0) continue;
            }
            diagExpired++; continue;
        }

        // OPEN-SEAT gate: the living seat market drives generation. When a
        //      save is supplied, a series only offers if it has at least one OPEN seat (a team seat freed by
        //      retirement/promotion in RolloverYearReport -> career::SeatsForClass(sd, *save) with seat.open).
        //      A fully-occupied class produces NO offer (the static "every series always offers" flood is gone).
        //      Offers emerge across ALL tiers (the lower/cohort classes where the player starts included), and
        //      regardless of team name (derived-label seats offer too — SeatsForClass populates every tier).
        //      Era-memory seats never reach here (excluded above). When `save` is null (legacy callers), this
        //      gate is INERT -> the original static-per-series behavior (back-compat). The reqRep/fit/scarcity/
        //      teamInterest gates below stay as FILTERS on the open-seat set; NO concurrent-offer cap (NO-CAP).
        // ELO DISPLACEMENT: a class can ALSO open to the player when it is fully
        //   occupied but the player's AGED rating beats the WEAKEST in-reach incumbent's by DISPLACEMENT_MARGIN
        //   (the player can "take his place"). "in-reach" = within the player's reachable tier band (the same
        //   frontier-phase gate the courting path uses: sd.level <= frontier_phase). When displacement holds we
        //   let the series THROUGH the open-seat gate and flag it so a DISPLACEMENT offer (and the incumbent
        //   move-out) is surfaced below — even if SeatsForClass shows no open seat. A low-rated player triggers
        //   none (the predicate is strict-greater past the margin). Needs save (the occupancy + aged-rating set)
        //   and world_year (the incumbent age curve); world_year==0 (legacy/no-save) -> displacement scan off.
        bool displaceSeries = false;
        std::string displacedIncumbentId;
        if (save && in.world_year != 0 && sd.level <= in.frontier_phase) {
            double weakest = 0.0; std::string weakestId;
            if (career::WeakestIncumbentRating(sd.id, *save, in.world_year, weakest, weakestId)
                && career::PlayerDisplacesIncumbent(in.rating, weakest)) {
                displaceSeries = true;
                displacedIncumbentId = weakestId;
            }
        }
        // 18/09: the seats are only looked at where they decide something — in a CLOSED one that isn't opened by
        // displacement. Before, they were computed in all 118 series every day (18 of this function's 29 ms)
        // to be used only in the 14 closed ones.
        if (save && sd.closed && !displaceSeries) {
            bool anyOpenSeat = false;
            for (const auto& st : career::SeatsForClass(sd.id, *save)) if (st.open) { anyOpenSeat = true; break; }
            // an OPEN series (open grid, customer car) ALWAYS advertises a seat
            // within its window (it doesn't require a freed seat; there's always room for one more privateer). Only
            // CLOSED series (factory seat, sd.closed==true) still require an open seat or displacement
            // (unchanged for them). `sd` is already the full SeriesDef (AllSeries()) -> direct access.
            // A fully-occupied CLOSED class produces NO offer UNLESS the player can DISPLACE a backmarker.
            if (!anyOpenSeat && !displaceSeries && sd.closed) continue;
        }
        // Negotiation-thread redesign: a LAPSED offer (TTL, offer_lifecycle) or
        //      WITHDRAWN (the team pulled it after a broken negotiation -- NegoPlayerWalk/NegoCloseElsewhere)
        //      does NOT reappear as "New": it's cut BEFORE the seat loop (the offer id is "of_"+series,
        //      shared by every seat in the series). declined_offers/expired_offers are deliberately left OUT
        //      -- those stay VISIBLE on the board with a "declined"/"expired" badge (see
        //      MarketUiOfferState in career_overlay.cpp and the comment in career_contract.cpp: "a
        //      declined offer stays visible (state=2) until the season purge"); lapsed/withdrawn, by contrast,
        //      disappear entirely. dll/career_overlay.cpp::MarketUiOffers already applied this same filter to
        //      lapsed_offers as a post-filter in the overlay layer (outside the test build); it's replicated here, in
        //      the pure, testable engine, and withdrawn_offers is added with the identical disappearance semantics.
        if (save) {
            const std::string suppressId = "of_" + sd.id;
            if (save->lapsed_offers.count(suppressId) > 0 || save->withdrawn_offers.count(suppressId) > 0) continue;
        }
        // SEASON-RELATIVE offer window behind MK_OFFER_HORIZON_DOY ----
        // Before: an absolute cutoff of round1doy < cursor -> OMIT, which emptied the board past ~doy 80 for
        // any save progressed toward the rollover. Now it rolls forward: delta = days until this
        // season's round 1; if it already passed (delta<0) we treat it as "next YEAR's round 1" (+365), and the seat
        // only drops out if its opening lands FARTHER than the forward horizon (it rolls forward instead
        // of disappearing). The same fix is mirrored in AdvanceWeek, or offers flicker.
        //
        // ORTHOGONALITY: this TEMPORAL axis (intra/inter-season) is INDEPENDENT of the
        // per-(seat, season) SCARCITY draw. They compose as two separate `continue`s in the
        // seat loop; they never overlap or double-count (one is the when, the other is how many seats).
        // CalendarForSeriesInYear(sd.id, in.year) -- opener PROJECTED onto the current year, not the raw CalendarForSeries
        // (base kStartYear); every real caller already fills in.year (career_contract.cpp's RunWorldTick,
        // career_overlay.cpp, tools/career_sim.cpp, tools/careerproof.cpp).
        // (computed above, before the displacement and the seats; only CLOSED ones get here outside the window)
        if (fueraDeVentana) { diagExpired++; continue; }   // outside the window

        bool displaceConsumed = false;   // Emit ONE displacement offer per series (the first seat)
        for (const auto& seat : SeatsForSeries(sd)) {
            if (diagFirstReqRep < 0) diagFirstReqRep = seat.reqRep;   // first seat's reqRep (rating scale) for the diagnostic
            // This seat carries the displacement only if the series is displaceable AND we haven't
            // emitted the displacement offer yet (one displacement per series, on the first seat).
            const bool displaceThisSeat = displaceSeries && !displaceConsumed;
            // The eligibility gate is on the RATING scale, NOT engine-REP-points.
            // seat.reqRep is rating-scale (career::ReqRepRating, 1150..2243); comparing in.rep (0..450) hid every
            // closed "they're courting you" direct offer. GATE_fit extends THIS same comparison.
            const bool metRep   = (in.rating >= seat.reqRep);
            const bool openAd0  = (!seat.works && seat.reqRep == 0);   // a fully open ad
            const bool xpLocked = (sd.level > in.frontier_phase);
            const bool gated    = seriesIsGated(sd.id);
            const bool gateOpen = in.unlocked_gates && in.unlocked_gates->count(sd.id) > 0;
            const bool gateLock = gated && !gateOpen;

            // SCARCITY: a further gate, a deterministic per-(seat, SEASON) availability draw.
            //      `scarce = fnv1a(series_id|year|career_seed) % 100 >= rate -> continue`. Season-stable: it
            //      re-rolls ONLY at the year boundary (`in.year` in the key), never per-week, never with RNG.
            //      Asymmetric: works seats are scarce (MK_SCARCITY_WORKS_RATE_PCT ~25, ~1-in-4 seasons),
            //      open ads common (MK_SCARCITY_OPEN_RATE_PCT ~70, ~7-in-10), branched on the existing seat.works.
            //      A scarcity-closed seat is SILENT: pure absence, no mail, no held card — just the
            //      `continue`. HELD/COURTED seats are EXEMPT: a team that remembers you
            //      doesn't vanish. `go.held` is computed below at the OfferDef build (after fitPass), so recompute
            //      the held predicate INLINE here (metRep && (xpLocked || gateLock)) and skip scarcity for it.
            //
            // ORTHOGONALITY: this scarcity axis is the SEASON axis (how many seats exist this
            // year) and is INDEPENDENT of the round1doy timing window above (the intra/inter-season WHEN
            // axis). They compose as two separate `continue`s and never double-count. Scarcity is also
            // distinct from the RETIRED MK_OFFER_TOP_K: it prunes the set the gates yield, it does NOT cap it.
            // An OPEN-SEAT-driven offer (save != null) is a CONCRETE vacancy the living
            // world freed (retirement/promotion) — it is EXEMPT from the probabilistic season scarcity draw
            // (a seat that just opened doesn't vanish to a dice roll), exactly like a held/courted seat. The
            // scarcity axis only prunes the LEGACY static-per-series flood (save == null).
            const bool wouldBeHeld = metRep && (xpLocked || gateLock);
            if (!wouldBeHeld && !save) {
                unsigned long sh   = fnv1a(sd.id + "|" + std::to_string(in.year) + "|" + std::to_string(in.career_seed));
                const int     rate = seat.works ? MK_SCARCITY_WORKS_RATE_PCT : MK_SCARCITY_OPEN_RATE_PCT;
                if ((int)(sh % 100) >= rate) continue;   // scarcity-closed this season -> silent absence
            }

            // GATE_fit: the HARD gate, on the rating scale, EXTENDING the base
            //      eligibility comparison. A CLOSED (works) seat courts you only when your rating sits inside
            //      the fit band [reqRep - MK_FIT_UNDER_MARGIN, reqRep + MK_FIT_OVER_MARGIN]: the under-margin is
            //      a thin-field soft floor (a near-miss is still courted); the over-margin is "a backmarker
            //      won't court an over-qualified driver". Open reqRep==0 ads always pass (free ad); other open
            //      ads keep the plain in.rating >= reqRep eligibility. XP/superlicense locks stay HELD below
            //      (NOT hard-fails). gateFit==false for a closed seat => the seat is OMITTED (no card at all).
            // pay-driver: an ABOVE-PACE open seat (the player is UNDER its rating bar) opens
            //   as a BUY-IN when the player brings budget (money_norm > 0) — the Latifi path. Normally an open
            //   seat the player is under-rated for is NOT emitted (fitPass needs metRep); the buy-in OVERRIDES
            //   that for an OPEN, non-promotion seat. Mirrors ContractKindForSeat's pay_driver condition exactly
            //   (open seat, no promote target, money_norm>0, q<seatBarQ) so the kind resolved below is pay_driver.
            //   money_norm==0 -> false everywhere -> no pay seats (byte-identical to before this feature).
            const double payQ        = career::RatingQuality(in.rating);
            const double paySeatBarQ = seat.reqRep > 0 ? career::RatingQuality((double)seat.reqRep) : 0.0;
            // the pay-driver (Latifi) path now also applies on WORKS seats (closed grid), but with a FLOOR
            // on competence (payQ no more than MK_PAYDRIVER_SKILL_FLOOR below the bar). Open seats
            // still have no floor (previous behavior). money_norm==0 -> false everywhere (dormant, byte-identical to before).
            const bool   payFloorOK = payQ >= paySeatBarQ - bal::MK_PAYDRIVER_SKILL_FLOOR;
            const bool   payDriverEligible = SeriesUnlockTarget(sd.id).empty() && in.money_norm > 0.0
                                             && payQ < paySeatBarQ && (!seat.works || payFloorOK);

            // Displacement: a backmarker seat the player can OUST his way into is offered
            //   EVEN WHEN the player is over-qualified (the over-margin would normally reject it — "a backmarker
            //   won't court an over-qualified driver"). Displacement is precisely "you're better than the
            //   incumbent, so you take his place", so it BYPASSES the fit band for this series. Only the first
            //   (lowest-index) seat carries the displacement so a class emits one displacement offer, not N.
            const bool fitPass = (displaceThisSeat || payDriverEligible) ? true : (seat.works
                ? (in.rating >= (double)seat.reqRep - MK_FIT_UNDER_MARGIN &&
                   in.rating <= (double)seat.reqRep + MK_FIT_OVER_MARGIN)
                : (openAd0 || metRep));

            // is the offer emitted? direct (works + inside the fit band) | ad (open with reqRep==0 or metRep).
            const bool emitDirect = (seat.works && fitPass);
            const bool emitOpen   = (!seat.works && fitPass);
            if (!emitDirect && !emitOpen) continue;   // outside the fit band and not an open ad -> not offered

            GenOffer go;
            OfferDef& o = go.def;

            // held: fit/REP met but blocked by XP/gate. Closed = fit band passed; open = metRep. ----
            go.held = (seat.works ? fitPass : metRep) && (xpLocked || gateLock);
            // hardening: the gate ALWAYS wins — no forced-emission path
            // (displacement, pay-driver, or a gated class miscatalogued as 'open' with metRep=false)
            // can leave a series with a pending superlicense actionable. Latent with today's data
            // (the three gated series are all closed), but new data must never be able to reopen the exploit.
            if (gateLock) go.held = true;
            go.tier_locked = go.held;   // soft floor: REP-met but XP/gate blocks it == courting/retained
            if (go.held) {
                if (xpLocked) {
                    o.id.clear();
                    go.lock_reason    = "requiere Fase " + std::to_string(sd.level);
                    go.lock_reason_en = "requires Phase " + std::to_string(sd.level);
                } else {  // gateLock
                    go.lock_reason    = "requiere superlicencia";
                    go.lock_reason_en = "requires superlicense";
                }
            }

            // OfferDef fields ----
            o.id        = "of_" + sd.id;                 // deterministic id
            o.direct    = seat.works && fitPass;         // "they're courting you" only if closed + inside the fit band (GATE_fit)
            o.team      = sd.display;                    // synthetic team label (deterministic per series)
            // When generation is OPEN-SEAT-driven (save != null), surface the LIVING
            // TEAM LABEL from the seat/occupancy model — the per-team name from TeamLabelFor (real_team -> alias ->
            // livery_name -> "Privateer #" -> the "seat_privateer_entry" fallback id the overlay resolves via TR).
            // This is what makes NAMED teams appear at the top tiers and DERIVED labels (incl. the privateer-entry
            // fallback) at the lower/cohort tiers in the market. For a displacement offer prefer the OCCUPIED seat
            // being taken (the incumbent's team); otherwise the first OPEN seat. Pure (reads the same save+PSVs).
            // The OFFERED SEAT's team label (the team making the offer) — passed to
            // TeamRelationship below as offerTeam for the cross-series brand-affinity term. Resolved from the
            // SAME single seat-pick used for o.team; "" when no clean single seat-team is available (no save,
            // or no labeled seat) -> NO brand term (safe, deterministic).
            std::string offerTeamLabel;
            if (save) {
                std::vector<career::Seat> classSeats = career::SeatsForClass(sd.id, *save);
                const career::Seat* pick = nullptr;
                if (displaceThisSeat) {
                    for (const auto& cs : classSeats) if (!cs.open && !cs.team_label.empty()) { pick = &cs; break; }
                }
                if (!pick) for (const auto& cs : classSeats) if (cs.open && !cs.team_label.empty()) { pick = &cs; break; }
                if (!pick) for (const auto& cs : classSeats) if (!cs.team_label.empty()) { pick = &cs; break; }
                if (pick) { o.team = pick->team_label; o.from = sd.display; offerTeamLabel = pick->team_label; }
            }

            // CONTEST — the cohort competes for the seat; the player only receives the
            //      offer if they're the STRONGEST candidate (by TeamCandidateFit, with a tolerance
            //      margin). If the cohort wins, the AI keeps the seat -> no offer,
            //      a SILENT absence (the "you missed it" mail family is NEVER emitted). Runs right
            //      AFTER resolving offerTeamLabel (above) and BEFORE building the rest of the OfferDef /
            //      pushing it to `out` (further below), so the `continue` skips it ENTIRELY. Scope: this
            //      SEAT loop iteration (`for (const auto& seat : SeatsForSeries(sd))`), not the whole series —
            //      another seat in the same series can still be offered. Only applies with a living world (save) and
            //      a real resolved teamLabel; without it, the previous behavior is untouched.
            //
            // Junior-team CONTEST with an ON-RAMP. With the cohort's brand affinity hardened
            //      the contest is no longer tautological in junior teams -> it can also run there. But the
            //      ON-RAMP limits it to ASPIRATIONAL seats: a works seat, or a series ABOVE the
            //      player's phase. At your level and below there's ALWAYS room (on-ramp, never starvation) ->
            //      no contest. Replaces the old classHasRealTeam restriction (now junior teams do contest);
            //      the ramp protects the on-ramp across ALL classes (real and junior-team). pay-driver stays exempt.
            const bool aspirationalSeat = SeatIsAspirational(seat.works, sd.level, in.frontier_phase);
            if (save && !offerTeamLabel.empty() && aspirationalSeat && !payDriverEligible) {
                std::string teamCountry;   // proxy: the seat incumbent's country (if any)
                for (const auto& cs : career::SeatsForClass(sd.id, *save))
                    if (cs.team_label == offerTeamLabel && !cs.occupant_id.empty()) {
                        // the occupant's country via their RosterDriver (looked up in RosterForTier(sd.level))
                        for (const auto& rd : career::RosterForTier(sd.level))
                            if (rd.id == cs.occupant_id) { teamCountry = rd.country; break; }
                        break;
                    }
                // ranked ONCE, and the verdict and the rival both come from that SAME table. It used to
                // call PlayerWinsSeat, which ranked internally and returned only yes/no: who you
                // were competing against was computed and thrown away. The verdict is bit-identical (PlayerWinsSeat is now a
                // wrapper over PlayerWinsRankedSeat), so this does NOT change which offers come out.
                // With no trial open you compete against the ENTIRE reachable cohort
                // (who enters to contest the seat). With one open, the field is SEALED and the seat
                // is only decided among whoever entered and you — otherwise a driver who wasn't even there when it opened
                // could end up taking it, and the earlier step would have counted for nothing.
                std::vector<career::RosterDriver> cohorte =
                    career::CohortMinusStaying(career::RosterForTier(sd.level), staying);
                const career::SeatWatch* liveWatch = career::FindSeatWatch(*save, sd.id, offerTeamLabel);
                if (liveWatch && !liveWatch->contenders.empty()) {
                    std::vector<career::RosterDriver> sellados;
                    for (const auto& sc : liveWatch->contenders)
                        for (const auto& rd : cohorte) if (rd.id == sc.id) { sellados.push_back(rd); break; }
                    if (!sellados.empty()) cohorte.swap(sellados);   // empty = everyone retired -> a living cohort
                }
                // the SERIES is passed in to anchor the cohort's skill to this seat's tier.
                const std::vector<career::SeatCandidate> rank = career::RankSeatCandidates(
                    *save, sd.class_name, offerTeamLabel, teamCountry, in.money_norm,
                    in.world_year ? in.world_year : in.year, cohorte, sd.id);
                const int outcome = career::ClassifyRankedSeat(rank);
                const career::SeatCandidate* riv = career::TopRivalInRank(rank);
                if (riv) { go.rival_id = riv->id; go.rival_name = riv->name; go.rival_fit = riv->fit; }
                for (const auto& c : rank) if (c.is_player) { go.player_fit = c.fit; break; }
                // LOSING a trial closes the seat for a while. Without this the same team would
                // reopen the contest next cycle, and failing the trial would have cost nothing.
                // It's a READ of the save (GenerateOffers stays pure); the caller is the one who opens and closes it.
                if (career::SeatWatchOnCooldown(*save, sd.id, offerTeamLabel, absToday)) continue;
                if (contestsOut) {
                    SeatContestReport rp;
                    rp.series_id = sd.id; rp.class_name = sd.class_name; rp.team_label = offerTeamLabel;
                    if (riv) { rp.rival_id = riv->id; rp.rival_name = riv->name; rp.rival_fit = riv->fit; }
                    rp.player_fit = go.player_fit; rp.outcome = outcome;
                    // The shortlist reserves its first slot for whoever has history with you.
                    rp.shortlist = career::ShortlistFromRank(rank, save);
                    contestsOut->push_back(rp);
                }
                // WATCH and LOST behave the SAME as before (silence): the band only carves out a third case
                // WITHIN that silence — it never takes the offer away from anyone who already had it.
                if (outcome != career::SEAT_CONTEST_WON)
                    continue;   // the cohort wins the aspirational seat -> don't offer it to the player
            }

            o.from      = sd.class_name;                 // origin = the class
            o.color     = deterministicColor(sd.class_name);
            o.clsId     = sd.class_name;                 // disc dot: class_name; the UI falls back to o.color if it doesn't resolve
            o.scope     = "season";                      // v1: a full season
            o.reqRep    = seat.reqRep;
            o.seatName  = sd.display;                     // car/seat (placeholder: the series display name)

            // contract type + terms + tryout + "why" bars ----
            double q       = career::RatingQuality(in.rating);
            // seat quality bar: the q-equivalent of the seat's reqRep. NOTE: SeatsForSeries now stores
            // reqRep on the RATING scale (career::ReqRepRating), not the old engine-REP-point scale, so the bar is
            // simply q(reqRep). (The earlier design mapped the REP_BAND index/5; with rating-scale reqRep that band
            // saturates to 1.0 for every top seat -> everyone a 'trial'. q(reqRep) is the faithful, corrected bar.)
            double seatBarQ= seat.reqRep > 0 ? career::RatingQuality((double)seat.reqRep) : 0.0;
            // conditional only when this series feeds another (unlocks target) AND it's an open low seat.
            std::string promoteTo = SeriesUnlockTarget(sd.id);
            int kind = ContractKindForSeat(seat.works, q, seatBarQ, in.money_norm, !promoteTo.empty());

            // build the want-inputs for the player against this seat (defaults where that data is absent).
            WantInputs wi;
            wi.rating       = in.rating;
            // REAL relationship from persisted history (no longer the flat 0.5 stub). This
            // single value feeds BOTH the why-bar (o.whyRel = wi.rel*100 below) AND the soft matching (Want_T's
            // L_t*rel term). Guarded for the no-save overload: with no save we keep the neutral 0.5 (byte-identical
            // to the legacy single-arg path; a no-history save also returns exactly bal::REL_BASE == 0.5).
            // Pass the offered seat's team label as offerTeam -> cross-series brand-affinity warmth
            // when this org appears in your history in ANOTHER series ("" when unresolved -> no brand term).
            wi.rel          = save ? TeamRelationship(*save, sd.id, offerTeamLabel) : 0.5;
            wi.prestige     = (double)sd.level * 100.0 / 7.0;   // tier as a prestige proxy for now
            wi.money_norm   = in.money_norm;
            wi.contractKind = kind;
            // Soft inputs to the want-score ----
            wi.form         = in.form;                   // the SAME CareerForm value the why-bar shows
            wi.tier_gap     = sd.level - in.frontier_phase;   // jump term: tiers above the player's frontier (0 if at/below)
            if (wi.tier_gap < 0) wi.tier_gap = 0;
            wi.rating_races = in.rating_races;           // provisional-spike discount while < K_PROVISIONAL_RACES
            wi.cleanliness  = in.cleanliness;            // real cleanliness EMA -> Want_T soft penalty below the floor (NO-OP at default 100.0)
            // teamInterest = hard GATE_fit (closed seats already pruned to fitPass above; open ads pass
            // the gate by construction) x the Want_T soft score. The carrier stays GenOffer.want_t; ORDER only.
            double wantT    = teamInterest(wi, fitPass);

            double coverage = 0.0;                       // the future deal-coverage vector fills this in; 0 today
            // Resolve the archetype BEFORE the terms -> OfferTermsCtx gates the salary
            // by salary_dir(kind,archetype) (not by kind), so the emitted per_race == what the settle actually pays. The
            // signals ArchetypeForOffer reads (contractKind/direct/promote_to) are already available: o.direct
            // was set above and kind/promoteTo are local -> they're copied onto o.* here (previously done after the terms).
            o.contractKind = kind;
            o.promote_to   = promoteTo;
            o.archetype    = career::ArchetypeForOffer(go);   // Narrative archetype, from the signals already fixed above
            go.archetype   = o.archetype;
            // The sponsor-card system: market.offer_quality bumps the level fed to OfferTerms by +1 (one tier
            // better). OfferTerms already clamps lv to 1..7 internally -- no need to clamp here too.
            // Variable salary: with a living world (save != null) the series/team/save context is on hand
            // (sd.id/offerTeamLabel/*save, already resolved above) -> OfferTermsCtx (variable salary via
            // career::SalaryFor). With no save (legacy static-per-series, save == null) it stays on OfferTerms
            // (byte-identical to before this change; no legacy no-save test changes).
            OfferTermsOut terms = save
                ? OfferTermsCtx(in.rating, sd.level + in.quality_bump, kind, o.archetype, sd.id, offerTeamLabel, *save,
                                in.world_year ? in.world_year : in.year)
                : OfferTerms(in.rating, sd.level + in.quality_bump, kind, coverage);
            o.perRace   = terms.per_race;
            o.signBonus = terms.sign_bonus;
            // Surface the pay-driver buy-in onto the offer (MK_PAYDRIVER_BUYIN[level-1]*(1-q),
            // computed by OfferTerms only for CONTRACT_PAY_DRIVER; 0 otherwise). AcceptOffer reads it onto
            // Contract.buy_in and DEBITS it at signing (solvency-gated). With money_norm=0 no pay seats arise
            // (ContractKindForSeat never returns pay_driver) -> buy_in stays 0 everywhere (byte-identical).
            o.buy_in    = terms.buy_in;

            // SIGNABILITY soft-mark — COMPUTED but NEVER drops the offer or zeroes
            //      want_t. Only pay_driver carries an up-front buy_in; works/customer/trial/conditional are
            //      fee-waived (the team or self-funded entry, neutral). DORMANT while money_norm=0: no pay seats
            //      generate -> cost 0 -> always affordable. No `continue`, no want_t mutation here.
            const bool feeWaived = (kind != CONTRACT_PAY_DRIVER);
            const long affordCost = (kind == CONTRACT_PAY_DRIVER) ? terms.buy_in : 0;
            o.canAfford = gateAfford(in.credits, affordCost, feeWaived);

            // o.contractKind/o.promote_to/o.archetype were already set ABOVE (before the terms)
            // to gate the salary by salary_dir. Only the "why" bars and want_t are left here.
            o.whyRating     = (int)(q * 100.0 + 0.5);
            o.whyRel        = (int)(wi.rel * 100.0 + 0.5);
            o.whyForm       = (int)(in.form * 100.0 + 0.5);   // ONE CareerForm source, shared with the soft term (wi.form)
            go.want_t       = wantT;

            // The gray-zone trial trigger-set REPLACES the old TryoutRequired gate. Reads
            // the resolved archetype (never/one-time-entry via ArchetypeInfo.trialRule) + offer-generation signals.
            // Signals available at generation time: sd.level (target), in.frontier_phase, in.cleanliness, and the
            // "top team" proxy (tier >= TRIAL_TOPTEAM_LEVEL). The remaining gray-zone signals are negotiation/runtime
            // (disputed seat, recent strong result, over-reach, approached-for-result) and are not known here ->
            // default false at generation; the trial that fires here is the talent/jump gate. (recentStrongResult is
            // passed true here ONLY when this is a direct works approach — being courted directly implies you proved it — so a
            // top direct offer does not over-trigger purely on tier.)
            const bool topTeam = (sd.level >= bal::TRIAL_TOPTEAM_LEVEL);
            o.requiresTryout = RequiresTrial(o.archetype, sd.level, in.frontier_phase, in.cleanliness,
                                             /*disputedSeat=*/false, topTeam, /*recentStrongResult=*/o.direct,
                                             /*overReach=*/false, /*approachedForResult=*/false);

            o.commitment = (int)cal.size();              // number of rounds = season commitment

            // bilingual titles/pitch (a simple template from the display name) ----
            o.title    = "Plaza en " + sd.display;
            o.title_en = "Seat at " + sd.display;
            // if the seat carries a REAL team (not the privateer fallback or a synthetic display name), the offer
            // signs with its own name — "Halcyon F1 Team wants you for X". Otherwise it falls back to the generic phrase.
            const bool realTeam = !offerTeamLabel.empty() && offerTeamLabel != "seat_privateer_entry"
                                  && offerTeamLabel != sd.display;
            if (seat.works) {
                if (realTeam) {
                    o.pitch    = offerTeamLabel + " te quiere para " + sd.display + ".";
                    o.pitch_en = offerTeamLabel + " wants you for " + sd.display + ".";
                } else {
                    o.pitch    = "El equipo te quiere para " + sd.display + ".";
                    o.pitch_en = "The team wants you for " + sd.display + ".";
                }
            } else {
                o.pitch    = "Plaza abierta en " + sd.display + ". Preséntate.";
                o.pitch_en = "Open seat in " + sd.display + ". Apply now.";
            }

            // Displacement: this offer is the player ousting a backmarker — re-label the
            // pitch as "take his place" and mark it as a concrete (non-held) actionable seat. The incumbent
            // move-out is applied at the rollover/accept site via career::DisplaceIncumbent (GenerateOffers is
            // pure — it cannot mutate the save). Consume so only one displacement offer surfaces per series.
            if (displaceThisSeat) {
                // displacement skips the fit band, NOT the superlicense — with that gate closed the
                // offer stays COURTED (the "requires superlicense" lock is already set above), not actionable.
                if (!gateLock) { go.held = false; go.tier_locked = false; }   // in-reach by construction (sd.level <= frontier_phase)
                o.direct       = true;                     // the team comes to YOU because you outpace its backmarker
                o.displacement = true;                     // The Market renders seat_displacement; accept-time DisplaceIncumbent keys off it
                o.pitch    = "Superas a su piloto en " + sd.display + ". Ocupa su asiento.";
                o.pitch_en = "You outpace their driver at " + sd.display + ". Take his seat.";
                displaceConsumed = true;
            }

            out.push_back(std::move(go));
        }
    }

    // Junior-team academy-promotion offers (junior team -> parent). After the loop, before ranking,
    // so they participate in the want_t ordering. Only with a living world (save).
    if (save) AppendAcademyPromotions(in, *save, out);

    // strongest interest (highest want_t) first, deterministic id tiebreak.
    std::sort(out.begin(), out.end(), [](const GenOffer& a, const GenOffer& b) {
        if (a.want_t != b.want_t) return a.want_t > b.want_t;   // strongest interest first
        return a.def.id < b.def.id;                              // deterministic tiebreak
    });

    // One trace line proving the input bridge state. BEFORE the bridge fix the bridges omit
    // in.rating -> this logs rating=1150 (rookie default) with N=0; AFTER, rating is the real value and N>0.
    int diagDirects = 0, diagActionable = 0;
    for (const auto& g : out) if (g.def.direct) {
        diagDirects++;
        if (!g.held) diagActionable++;
        // DEBUG only (stripped with the debug-save block): per-direct -> team/reqRep/held + reason, to
        // diagnose why a generated direct offer does NOT come out actionable as "THEY WANT YOU" (held => "THEY'RE COURTING YOU").
        MktLogF("[offer-direct] team='%s' reqRep=%d held=%d tryout=%d reason=%s\r\n",
                g.def.team.c_str(), g.def.reqRep, g.held ? 1 : 0, g.def.requiresTryout ? 1 : 0,
                g.held ? (g.lock_reason_en.empty() ? "held" : g.lock_reason_en.c_str()) : "actionable");
    }
    MktLogF("[offers] rep=%d rating=%.1f frontier=%d cursor=%d reqRep0=%d N=%d directs=%d actionable=%d expired=%d\r\n",
            in.rep, in.rating, in.frontier_phase, in.cursor_doy, diagFirstReqRep,
            (int)out.size(), diagDirects, diagActionable, diagExpired);

    return out;
}

// Legacy single-arg form -> the original static-per-series generation (save == nullptr).
std::vector<GenOffer> GenerateOffers(const OfferGenInput& in) { return GenerateOffers(in, (const career::SaveData*)nullptr); }

// Convenience reference overload (open-seat gated) -> forwards &save to the pointer form.
std::vector<GenOffer> GenerateOffers(const OfferGenInput& in, const career::SaveData& save) { return GenerateOffers(in, &save); }

} // namespace market
