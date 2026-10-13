#include "career_nego_thread.h"
#include "career_balance.h"
#include "career_series.h"      // SeriesById / SeriesDef.level
#include "career_market.h"      // seriesIdOfOffer isn't public: we derive it from o.id ("of_"+series)
#include "career_mail.h"        // MailHash (deterministic FNV-1a)
#include "career_contract.h"    // OfferFirstSeen (offer_lifecycle) + WeekTickResult/RunWorldTick
#include "career_season.h"      // CareerForm(prog) — same source as the why-bar
#include "career_userlog.h"     // career::EngineLogPath (the unified engine log, with a size cap)
#include "paths.h"              // mojibake fix 22/07: FOpen (UTF-8 -> wide)
#include <algorithm>
#include <cstdio>                // NegoLogF (fopen/append, portable across engine+overlay+test)
#include <cstdarg>

namespace career {

// A trace of negotiation-thread transitions, to the SAME log
// the engine uses (userdata/logs/zta_engine.log via career::EngineLogPath), the same one MktLogF (career_market.cpp) uses
// and EconLog (career_econ.cpp) — same idiom (portable fopen/append), works the same in the DLL and in
// career_test.exe. `grep "[nego]"` gives the full story of each thread (opens/replies/closes).
static void NegoLogF(const char* fmt, ...) {
    if (LogCaptureActive()) {   // 0.7.0 review (finding 7): clock forecast, it has not happened
        char buf[1024];
        va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
        LogCaptureEngineLine(buf);
        return;
    }
    FILE* f = paths::FOpen(EngineLogPath(), "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fclose(f);
}

static std::string negoSeriesIdOf(const market::OfferDef& o) {
    // OfferDef.id = "of_" + series_id (the whole market's idiom; see TeamRelationship in career_market.cpp)
    return (o.id.rfind("of_", 0) == 0) ? o.id.substr(3) : o.id;
}

int NegoImportance(const market::OfferDef& o, int seriesLevel) {
    if (seriesLevel < 1) seriesLevel = 1;
    int pts = bal::NEGO_IMP_LEVEL_PTS * seriesLevel;
    long econ = (o.signBonus + 10L * o.perRace) / bal::NEGO_IMP_ECON_DIV;
    if (econ > bal::NEGO_IMP_ECON_MAX) econ = bal::NEGO_IMP_ECON_MAX;
    if (econ < 0) econ = 0;
    pts += (int)econ;
    int k = (o.contractKind >= 0 && o.contractKind < 5) ? o.contractKind : 0;
    pts += bal::NEGO_IMP_KIND_PTS[k];
    if (pts < 0) pts = 0;
    if (pts > 100) pts = 100;
    return pts;
}

bool NegoNegotiable(const market::OfferDef& o) {
    const SeriesDef* sd = SeriesById(negoSeriesIdOf(o));
    return NegoImportance(o, sd ? sd->level : 1) >= bal::NEGO_IMPORTANCE_THRESH;
}

NegoPersonality NegoPersonalityFor(const std::string& teamId, int seriesLevel) {
    unsigned h = MailHash("negoper_" + teamId);
    NegoPersonality p;
    p.delay_days    = bal::NEGO_DELAY_MIN + (int)(h % (unsigned)bal::NEGO_DELAY_SPAN);
    p.patience_base = bal::NEGO_PAT_START_MIN + (int)((h >> 4) % (unsigned)bal::NEGO_PAT_START_SPAN);
    p.concession    = (int)((h >> 9) % 3u);
    p.tone          = (int)((h >> 12) % 3u);
    // style: ~60% normal, ~20% take-it-or-leave-it, ~20% haggler; top teams skew dry/tough
    unsigned s = (h >> 16) % 10u;
    p.style = (s < 6) ? 0 : (s < 8 ? 1 : 2);
    if (seriesLevel >= 5 && p.style == 2) p.style = 1;   // the elite doesn't haggle: they dictate
    return p;
}

int NegoPatienceStart(const NegoPersonality& p, double wantT) {
    if (wantT < 0.0) wantT = 0.0;
    if (wantT > 1.4) wantT = 1.4;
    return p.patience_base + (int)(wantT * bal::NEGO_PAT_WANT_GAIN);
}

int NegoPatienceBandOf(int patience, int patience_base) {
    if (patience_base < 1) patience_base = 1;
    int pct = patience * 100 / patience_base;
    return (pct >= 60) ? NEGO_PAT_RECEPTIVE : (pct >= 25 ? NEGO_PAT_TENSE : NEGO_PAT_EDGE);
}

void NegoEstimateBand(const std::string& offerId, int round, unsigned careerSeed, int trueBase,
                      int& lo, int& hi) {
    int w = bal::NEGO_BAND_START_W - round * bal::NEGO_BAND_NARROW_PER_ROUND;
    if (w < bal::NEGO_BAND_MIN_W) w = bal::NEGO_BAND_MIN_W;
    // center shifted with deterministic noise (the agent doesn't know its own mood or patience)
    unsigned h = MailHash(offerId + "_band_" + std::to_string(careerSeed));
    int shift = (int)(h % (unsigned)(w + 1)) - w / 2;
    int center = trueBase + shift / 2;
    lo = center - w / 2; hi = lo + w;
    if (lo < 0) { hi -= lo; lo = 0; }
    if (hi > bal::NEGO_MARGIN_MAX) { lo -= (hi - bal::NEGO_MARGIN_MAX); hi = bal::NEGO_MARGIN_MAX; if (lo < 0) lo = 0; }
}

// Pure resolution — NegoResolve + a greedy counteroffer ---

DealTerms NegoCounterFrom(const DealTerms& asked, int deficit, int concession) {
    DealTerms c = asked;
    if (concession >= 0 && concession <= 2) deficit -= bal::NEGO_CONCESSION_PCT[concession];
    // 1) an offensive sponsor: rejected first ("keep your sponsor")
    if (c.sponsorTierRank >= 0 && NegoSponsorDelta(c.sponsorTierRank) < 0) {
        deficit += NegoSponsorDelta(c.sponsorTierRank);   // a negative delta: dropping it RECOVERS margin
        c.sponsorTierRank = -1;
    }
    // 2) a greedy trim of the priciest demands (only the positive-cost ones; stable order by cost desc, id asc)
    while (deficit > 0) {
        int best = -1, bestCost = 0;
        for (int i = 0; i < bal::NEGO_DEMAND_COUNT; i++)
            if (c.demands[i] && bal::NEGO_DEMAND_COST[i] > bestCost) { best = i; bestCost = bal::NEGO_DEMAND_COST[i]; }
        if (best < 0) break;
        c.demands[best] = false; deficit -= bestCost;
    }
    // 3) if still short: drop the salary package one notch (90->50->10)
    while (deficit > 0 && c.salaryPct > bal::NEGO_SALARY_PKG[0]) {
        int cur = NegoSalaryCost(c.salaryPct);
        c.salaryPct = (c.salaryPct > bal::NEGO_SALARY_PKG[1]) ? bal::NEGO_SALARY_PKG[1] : bal::NEGO_SALARY_PKG[0];
        deficit -= (cur - NegoSalaryCost(c.salaryPct));
    }
    return c;
}

NegoReply NegoResolve(const Negotiation& n, const NegoWorldCtx& ctx) {
    NegoReply r;
    NegoPersonality per = NegoPersonalityFor(n.offer.team, 3);
    if (ctx.seatGone) { r.kind = NEGO_RE_WITHDRAW; r.patience_after = 0; return r; }
    double wantT = n.want_pct / 100.0;
    double trial = n.trial_pct / 100.0;
    NegoResult ev = NegoEvaluate(wantT, ctx.competing, trial, n.player_terms, ctx.relNow);
    // hidden modifiers: deterministic mood + recent form + low patience shrink the budget
    unsigned h = MailHash(n.offer.id + "_mood_" + std::to_string(n.round) + "_" + std::to_string(ctx.careerSeed));
    int mood = (int)(h % (unsigned)(2 * bal::NEGO_MOOD_SPAN + 1)) - bal::NEGO_MOOD_SPAN;
    int formPts = (int)((ctx.formNow - n.form_pct / 100.0) * bal::NEGO_FORM_GAIN);
    int band = NegoPatienceBandOf(n.patience, n.patience_base);
    int patShrink = (band == NEGO_PAT_EDGE) ? 8 : (band == NEGO_PAT_TENSE ? 4 : 0);
    int eff = ev.finalMargin + mood + formPts - patShrink;
    int deficit = -eff;
    // the personality's 'concession' softens the deficit BEFORE classifying AND before charging the
    // over-reach surcharge: the same lens for "you overreached" in patience and in response
    int concessionPts = (per.concession >= 0 && per.concession <= 2) ? bal::NEGO_CONCESSION_PCT[per.concession] : 0;
    int classifyDeficit = deficit - concessionPts;
    // patience cost for THIS round
    int cost = bal::NEGO_PAT_COST_ROUND + (classifyDeficit > bal::NEGO_COUNTER_BAND ? bal::NEGO_PAT_COST_OVERREACH : 0);
    if (ctx.daysToRound1 < bal::NEGO_URGENT_DAYS) cost *= 2;   // preseason urgency
    r.patience_after = n.patience - cost;
    bool exhausted = r.patience_after <= 0;
    // take-it-or-leave-it style: their first response is their last
    if (per.style == 1 && n.round >= 2) { r.kind = NEGO_RE_WITHDRAW; return r; }
    // exhausted patience ALWAYS withdraws, even if the proposal on the table was acceptable (time's up).
    if (exhausted) { r.kind = NEGO_RE_WITHDRAW; return r; }
    if (eff >= 0) {
        // haggler style: ALWAYS counters in round 1 (bazaar culture), even if the proposal was acceptable
        if (per.style == 2 && n.round == 1) {
            r.kind = NEGO_RE_COUNTER;
            r.counter = NegoCounterFrom(n.player_terms, 1, 0);   // a symbolic trim: one demand
            return r;
        }
        r.kind = NEGO_RE_ACCEPT;
        return r;
    }
    // Classification softened by generosity: classifyDeficit is already computed above and reused
    // for patience, classification, and building the counter (same lever, consistent).
    if (classifyDeficit > bal::NEGO_INSULT_BAND) { r.kind = NEGO_RE_WITHDRAW; return r; }
    if (classifyDeficit > bal::NEGO_COUNTER_BAND || per.style == 1) {
        r.kind = NEGO_RE_ULTIMATUM;
        r.counter = DealTerms{};                                  // the base offer, take it or leave it
        return r;
    }
    r.kind = NEGO_RE_COUNTER;
    r.counter = NegoCounterFrom(n.player_terms, deficit, per.concession);
    // a desperation sweetener: they want you a lot + round 1 close -> concede everything and raise the salary
    if (wantT >= bal::NEGO_DESPERATE_WANT && ctx.daysToRound1 < bal::NEGO_URGENT_DAYS) {
        r.counter = n.player_terms; r.sweetener = true;
    }
    return r;
}

// Translation templates + mail emitter for the NEGOTIATION family ---

bool EmitNegoMail(SaveData& save, const Negotiation& n, const char* beat, int day, int year,
                  const MailParams& extra, std::vector<std::string>* outIds) {
    std::string id = n.neg_id + "_" + beat + "_r" + std::to_string(n.round);
    for (const auto& e : save.emails) if (e.id == id) return false;      // dedup content-id
    NegoPersonality per = NegoPersonalityFor(n.offer.team, 3);
    GeneratedEmail ge;
    ge.id = id; ge.tag = 1; ge.day = day; ge.year = year; ge.family = NEGO_MAIL_FAMILY; ge.subtype = 0;
    ge.payload_key = beat;
    int vc = MailVariantCount(beat);
    ge.variant = vc > 0 ? (per.tone < vc ? per.tone : vc - 1) : 0;       // variant = TONE (not a hash)
    MailParams p;
    p.push_back({"team", n.offer.team});
    const SeriesDef* sd = SeriesById(negoSeriesIdOf(n.offer));
    p.push_back({"series", sd ? sd->display : n.offer.id});
    // {player} feeds the warm tones (_v1); empty -> MailInterpolate strips it cleanly
    std::string pf = MailFirstName(save.player.name);
    if (!pf.empty()) p.push_back({"player", pf});
    for (const auto& kv : extra) p.push_back(kv);
    ge.params = MailParamsEncode(p);
    ge.from_name = n.offer.team;
    for (auto& ch : ge.from_name) if (ch == '|') ch = '/';
    ge.from_role_key = "inbox_role_teamprincipal";
    ge.neg_id = n.neg_id;
    save.emails.push_back(ge);
    if (outIds) outIds->push_back(id);
    return true;
}

// Thread mutations (NegoStart/walk/closes) + snub on withdrawn ---

Negotiation* NegoFindByOffer(SaveData& save, const std::string& offerId) {
    for (auto& n : save.negotiations)
        if (n.offer.id == offerId && NegoThreadOpen(n.state)) return &n;
    return nullptr;
}
Negotiation* NegoFindById(SaveData& save, const std::string& negId) {
    for (auto& n : save.negotiations) if (n.neg_id == negId) return &n;
    return nullptr;
}
void NegoAppendHistory(Negotiation& n, const NegoRoundLog& row) {
    n.history.push_back(row);
    while ((int)n.history.size() > bal::NEGO_HISTORY_MAX) n.history.erase(n.history.begin());
}
static unsigned negoMaskOf(const DealTerms& t) {
    unsigned m = 0; for (int i = 0; i < bal::NEGO_DEMAND_COUNT; i++) if (t.demands[i]) m |= (1u << i);
    return m;
}

// respond_day = min(absDay + personality.delay_days, deadline-1); if it doesn't fit, a one-off grace that ALSO
// pushes the deadline out. Shared by NegoStart (new thread) and NegoReSend (re-counter from
// COUNTERED) so both paths honor exactly the same response window.
static int NegoComputeRespondDay(const NegoPersonality& per, int absDay, int& deadline_day) {
    int respond_day = absDay + per.delay_days;
    if (respond_day >= deadline_day) {
        deadline_day = absDay + bal::NEGO_TTL_GRACE_DAYS;
        respond_day = deadline_day - 1;
        if (respond_day <= absDay) respond_day = absDay + 1;
    }
    return respond_day;
}

bool NegoStart(SaveData& save, const market::OfferDef& offer, const DealTerms& terms,
               double wantT, int competing, double trialRatio, double formNow, int absDay, int year) {
    if (!NegoNegotiable(offer)) return false;
    if (NegoFindByOffer(save, offer.id)) return false;
    for (const auto& x : save.negotiations)
        if (x.state == NEGO_TH_ACCEPTED_PENDING) return false;   // there's a live handshake
    NegoPersonality per = NegoPersonalityFor(offer.team, 3);
    Negotiation n;
    // the absDay suffix -- without it, an offer reopened the SAME year after a closed thread
    // would produce the same neg_id as the old thread (same offer.id+year), and NegoFindById/negoh/mail
    // dedup would collide with the closed thread instead of seeing a new one.
    n.neg_id = "neg_" + offer.id + "_" + std::to_string(year) + "_" + std::to_string(absDay);
    n.offer = offer;
    n.state = NEGO_TH_WAITING; n.round = 1;
    n.want_pct = (int)(wantT * 100.0 + 0.5);
    n.form_pct = (int)(formNow * 100.0 + 0.5);
    n.trial_pct = (int)(trialRatio * 100.0 + 0.5);
    n.patience_base = NegoPatienceStart(per, wantT);
    n.patience = n.patience_base;
    int fs = absDay;
    OfferFirstSeen(save, offer.id, fs);                          // false -> fs stays absDay (full term)
    n.deadline_day = fs + bal::MK_OFFER_TTL_DAYS;
    n.respond_day = NegoComputeRespondDay(per, absDay, n.deadline_day);
    n.last_action_day = absDay;
    n.player_terms = terms;
    n.table_terms = DealTerms{};                                 // on the table: the neutral base offer
    NegoRoundLog rl; rl.day = absDay; rl.who = 0; rl.kind = 0;
    rl.demands_mask = negoMaskOf(terms); rl.salaryPct = terms.salaryPct;
    rl.sponsorTierRank = terms.sponsorTierRank;
    NegoAppendHistory(n, rl);
    // thread opening — -1 = no prior state (new thread)
    NegoLogF("[nego] %s state -1 -> %d (reason: start, patience %d)\r\n", n.neg_id.c_str(), n.state, n.patience);
    save.negotiations.push_back(n);
    return true;
}

bool NegoReSend(SaveData& save, Negotiation& n, const DealTerms& terms, int absDay) {
    (void)save;
    if (n.state != NEGO_TH_COUNTERED) return false;
    int oldSt = n.state;
    NegoPersonality per = NegoPersonalityFor(n.offer.team, 3);
    n.player_terms = terms;
    n.round++;
    n.state = NEGO_TH_WAITING;
    n.flags &= ~2;   // the reminder (bit1) is per-round: a new cycle can remind you again;
                     // the pressure (bit0) stays one-per-thread (re-firing it would squeeze the deadline)
    n.respond_day = NegoComputeRespondDay(per, absDay, n.deadline_day);
    n.last_action_day = absDay;
    NegoRoundLog rl; rl.day = absDay; rl.who = 0; rl.kind = 0;
    rl.demands_mask = negoMaskOf(terms); rl.salaryPct = terms.salaryPct;
    rl.sponsorTierRank = terms.sponsorTierRank;
    NegoAppendHistory(n, rl);
    NegoLogF("[nego] %s state %d -> %d (reason: resend, patience %d)\r\n", n.neg_id.c_str(), oldSt, n.state, n.patience);
    return true;
}

void NegoPlayerWalk(SaveData& save, Negotiation& n, int absDay) {
    if (!NegoThreadOpen(n.state)) return;
    bool hadRounds = n.round >= 1 && !n.history.empty();
    int oldSt = n.state;
    n.state = NEGO_TH_PLAYER_WALKED;
    NegoLogF("[nego] %s state %d -> %d (reason: player_walk, patience %d)\r\n", n.neg_id.c_str(), oldSt, n.state, n.patience);
    if (hadRounds) save.declined_offers.insert(n.offer.id);      // walking away after negotiating cools things down (snub)
    NegoRoundLog rl; rl.day = absDay; rl.who = 0; rl.kind = 4;   // 4 = walk (thread log)
    NegoAppendHistory(n, rl);
}

void NegoCloseElsewhere(SaveData& save, const std::string& signedOfferId, int absDay) {
    // fixes the "mail with an absurd date across a year boundary" bug: absDay already carries the real year (career_dateutil);
    // it's derived here ONLY to populate GeneratedEmail::year, without touching the usual persisted 'day' (doy).
    int emitYear, emitDoy; dateutil::YearDoyFromAbsDay(absDay, emitYear, emitDoy);
    for (auto& n : save.negotiations) {
        if (!NegoThreadOpen(n.state) || n.offer.id == signedOfferId) continue;
        bool handshake = (n.state == NEGO_TH_ACCEPTED_PENDING);
        int oldSt = n.state;
        n.state = NEGO_TH_CLOSED_ELSEWHERE;
        const char* motivo = handshake ? "signed_elsewhere + snub" : "signed_elsewhere";
        NegoLogF("[nego] %s state %d -> %d (reason: %s, patience %d)\r\n", n.neg_id.c_str(), oldSt, n.state, motivo, n.patience);
        if (handshake) save.declined_offers.insert(n.offer.id);  // a broken handshake IS a snub
        EmitNegoMail(save, n, "nego_close_elsewhere", absDay, emitYear, {}, nullptr);
    }
}

void NegoMarkSigned(SaveData& save, const std::string& negId) {
    Negotiation* n = NegoFindById(save, negId);
    if (n) {
        int oldSt = n->state;
        n->state = NEGO_TH_SIGNED;
        NegoLogF("[nego] %s state %d -> %d (reason: signed, patience %d)\r\n", n->neg_id.c_str(), oldSt, n->state, n->patience);
    }
}

// NegoTick — the negotiation thread's pulse, integrated into the world ---

void NegoCloseAllOpen(SaveData& save, int absDay, WeekTickResult& res) {
    // fixes the "mail with an absurd date across a year boundary" bug: absDay already carries the real year (career_dateutil); it's
    // derived ONLY to populate GeneratedEmail::year, without touching the usual persisted 'day' (absDay%365).
    int emitYear, emitDoy; dateutil::YearDoyFromAbsDay(absDay, emitYear, emitDoy);
    for (auto& n : save.negotiations) {
        if (!NegoThreadOpen(n.state)) continue;
        int oldSt = n.state;
        n.state = NEGO_TH_CLOSED_ROLLOVER;
        NegoLogF("[nego] %s state %d -> %d (reason: rollover, patience %d)\r\n", n.neg_id.c_str(), oldSt, n.state, n.patience);
        EmitNegoMail(save, n, "nego_close_rollover", absDay % 365, emitYear, {}, &res.generated_mail_ids);
    }
}

void NegoCloseAllOpen(SaveData& save, int absDay) {
    // AdvanceDays has no WeekTickResult in its signature (see the header comment): the mail is still persisted
    // the same way in save.emails via EmitNegoMail; this local result is discarded on purpose.
    WeekTickResult discard;
    NegoCloseAllOpen(save, absDay, discard);
}

void NegoTick(SaveData& save, const CareerCalendar& cc, const std::vector<market::GenOffer>& live,
              int absToday, WeekTickResult& res) {
    int doy = absToday % 365;
    auto weeksLeftOf = [&](const Negotiation& n) {
        int dl = n.deadline_day - absToday; return dl <= 0 ? 0 : (dl + 6) / 7; };
    for (auto& n : save.negotiations) {
        if (!NegoThreadOpen(n.state)) continue;
        bool stillLive = false;
        for (const auto& g : live) if (g.def.id == n.offer.id) { stillLive = true; break; }
        // signing reservation expired: you shook hands and never signed
        if (n.state == NEGO_TH_ACCEPTED_PENDING) {
            if (absToday > n.reserve_day) {
                int oldSt = n.state;
                n.state = NEGO_TH_WITHDRAWN;
                NegoLogF("[nego] %s state %d -> %d (reason: nego_reserve_expired, patience %d)\r\n",
                         n.neg_id.c_str(), oldSt, n.state, n.patience);
                save.withdrawn_offers.insert(n.offer.id);
                EmitNegoMail(save, n, "nego_reserve_expired", doy, cc.year, {}, &res.generated_mail_ids);
            }
            continue;
        }
        // the global deadline expired -> withdrawn (the world moved on). The mail's FLAVOR:
        // on that SAME TTL pulse the offer also disappears from the board as lapsed -> the board's
        // absence doesn't distinguish "someone else signed" from "it expired"; seatfilled ONLY if it's missing from the board and NOT lapsed.
        if (absToday > n.deadline_day) {
            int oldSt = n.state;
            n.state = NEGO_TH_WITHDRAWN;
            save.withdrawn_offers.insert(n.offer.id);
            const char* beat = (!stillLive && !save.lapsed_offers.count(n.offer.id))
                                   ? "nego_seatfilled" : "nego_withdraw";
            NegoLogF("[nego] %s state %d -> %d (reason: %s, patience %d)\r\n",
                     n.neg_id.c_str(), oldSt, n.state, beat, n.patience);
            EmitNegoMail(save, n, beat, doy, cc.year, {}, &res.generated_mail_ids);
            continue;
        }
        if (n.state == NEGO_TH_WAITING && absToday >= n.respond_day) {
            NegoWorldCtx ctx;
            ctx.careerSeed = save.career_seed;
            ctx.formNow = CareerForm(save.prog);
            ctx.relNow = market::TeamRelationship(save, negoSeriesIdOf(n.offer), n.offer.team);
            // lapsed != seat filled: a thread on grace can survive its own offer lapsing and
            // keep negotiating on its snapshot — only a NON-lapsed absence counts as seatGone.
            ctx.seatGone = !stillLive && !save.lapsed_offers.count(n.offer.id);
            for (const auto& x : save.negotiations)
                if (NegoThreadOpen(x.state) && x.neg_id != n.neg_id) ctx.competing++;
            for (const auto& g : live)
                if (g.def.direct && !g.held && g.def.id != n.offer.id) ctx.competing++;
            // urgency: days until the series' round 1 (via the deadline as a proxy if there's no date)
            ctx.daysToRound1 = n.deadline_day - absToday;
            NegoReply rep = NegoResolve(n, ctx);
            n.patience = rep.patience_after > 0 ? rep.patience_after : 0;
            NegoRoundLog rl; rl.day = absToday; rl.who = 1; rl.kind = rep.kind;
            rl.demands_mask = negoMaskOf(rep.counter); rl.salaryPct = rep.counter.salaryPct;
            rl.sponsorTierRank = rep.counter.sponsorTierRank;
            NegoAppendHistory(n, rl);
            int oldSt = n.state;                                 // always NEGO_TH_WAITING in this branch
            const char* beat = nullptr;
            switch (rep.kind) {
                case NEGO_RE_ACCEPT:
                    n.state = NEGO_TH_ACCEPTED_PENDING;
                    n.table_terms = n.player_terms; n.table_is_counter = false;
                    n.reserve_day = absToday + bal::NEGO_SIGN_RESERVE_DAYS;
                    beat = "nego_accept";
                    EmitNegoMail(save, n, beat, doy, cc.year,
                                 {{"reserve_w", std::to_string((bal::NEGO_SIGN_RESERVE_DAYS + 6) / 7)}},
                                 &res.generated_mail_ids);
                    break;
                case NEGO_RE_COUNTER:
                    n.state = NEGO_TH_COUNTERED;
                    n.table_terms = rep.counter; n.table_is_counter = true;
                    beat = rep.sweetener ? "nego_sweetener" : "nego_counter";
                    EmitNegoMail(save, n, beat, doy, cc.year, {}, &res.generated_mail_ids);
                    break;
                case NEGO_RE_ULTIMATUM:
                    n.state = NEGO_TH_ULTIMATUM;
                    n.table_terms = rep.counter; n.table_is_counter = true;
                    beat = "nego_ultimatum";
                    EmitNegoMail(save, n, beat, doy, cc.year, {}, &res.generated_mail_ids);
                    break;
                default:
                    n.state = NEGO_TH_WITHDRAWN;
                    save.withdrawn_offers.insert(n.offer.id);
                    beat = ctx.seatGone ? "nego_seatfilled" : "nego_withdraw";
                    EmitNegoMail(save, n, beat, doy, cc.year, {}, &res.generated_mail_ids);
            }
            NegoLogF("[nego] %s state %d -> %d (reason: %s, patience %d)\r\n",
                     n.neg_id.c_str(), oldSt, n.state, beat, n.patience);
            n.last_action_day = absToday;
            continue;
        }
        if (n.state == NEGO_TH_COUNTERED || n.state == NEGO_TH_ULTIMATUM) {
            // neglect: every week spent sitting on their counter drains patience; a one-time reminder (flag bit1)
            int idleWeeks = (absToday - n.last_action_day) / 7;
            if (idleWeeks >= 1) {
                n.patience -= idleWeeks * bal::NEGO_PAT_COST_NEGLECT;
                n.last_action_day += idleWeeks * 7;
                if (!(n.flags & 2)) {
                    n.flags |= 2;
                    EmitNegoMail(save, n, "nego_reminder", doy, cc.year, {}, &res.generated_mail_ids);
                }
                if (n.patience <= 0) {
                    int oldSt = n.state;
                    n.state = NEGO_TH_WITHDRAWN;
                    NegoLogF("[nego] %s state %d -> %d (reason: nego_withdraw, patience %d)\r\n",
                             n.neg_id.c_str(), oldSt, n.state, n.patience);
                    save.withdrawn_offers.insert(n.offer.id);
                    EmitNegoMail(save, n, "nego_withdraw", doy, cc.year, {}, &res.generated_mail_ids);
                    continue;
                }
            }
            // candidate pressure (once, bit0): with rival leverage, the deadline genuinely SHORTENS
            int rivals = 0;
            for (const auto& g : live) if (g.def.direct && !g.held && g.def.id != n.offer.id) rivals++;
            if (rivals > 0 && !(n.flags & 1) && (MailHash(n.neg_id + "_press") % 3u) == 0
                && absToday > n.last_action_day) {  // same-day guard: a re-tick with no forward progress can't fire pressure (idempotence)
                n.flags |= 1;
                int newDl = absToday + 10;
                if (newDl < n.deadline_day) n.deadline_day = newDl;
                EmitNegoMail(save, n, "nego_pressure", doy, cc.year,
                             {{"deadline_w", std::to_string(weeksLeftOf(n))}}, &res.generated_mail_ids);
            }
        }
    }
}

} // namespace career
