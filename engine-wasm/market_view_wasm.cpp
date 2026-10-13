// market_view_wasm.cpp -- the browser host's provider for query.market.
//
// PROVISIONAL -- SECOND IMPLEMENTATION OF THE MARKET VIEW.
//
// The authoritative assembly is career_overlay.cpp's SetMarketViewProvider lambda (it lives in the
// career_api_wiring.inc the overlay includes, with the market bridge functions it calls), which this
// repository does not publish. What follows is a copy written so the browser build can answer
// query.market at all, and copies drift: grep PROVISIONAL to find every one of them. The static_assert
// below makes a change to the view structure itself loud.
//
// Nothing here is new logic. The overlay's market bridges (MarketUiOffers, MarketUiOfferState,
// MarketSigningOpen, ProfilePlayerRatingUi, market::RepInfoFor) are declared in published headers but
// their definitions are not published, so each is rebuilt here from the published functions it is itself
// made of: market::MarketPhaseFor / DaysToMarketWindow (the market's hours), RepDisplayBand +
// RatingTierColor + bal::REP_BAND (the reputation badge -- RatingTierColor is documented as the mirror
// of the REP_TIERS colours), market::GenerateOffers (the offers themselves; pure), OfferConflicts,
// OfferFirstSeen / WeeksLeftFromFirstSeen / OfferDeathAbs, NegoNegotiable, DeriveOfferPresentation,
// ContractTypeInfo, SeatWatchRacesLeft. The serializer is the engine's own QueryMarket
// (career_api_queries.cpp) and is not touched or repeated here.
#include "career_api_queries.h"
#include "career_balance.h"
#include "career_calendar.h"
#include "career_candidate.h"
#include "career_cards.h"
#include "career_contract.h"
#include "career_dateutil.h"
#include "career_econ.h"
#include "career_engine.h"
#include "career_market.h"
#include "career_nego_thread.h"
#include "career_offer_meta.h"
#include "career_save.h"
#include "career_season_ui.h"   // MarketClassInfo
#include "career_series.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

// The guard that makes drift loud, for the same reason wasm_main.cpp guards HubView: MarketView is a
// plain serialization view, so a field added upstream would ship default-initialized through this demo
// without anything here having to mention it. Size, because C++17 cannot count members; exact for the
// one toolchain this file is built with (wasm32, libc++). When it fires: read the new MarketView (and
// MarketOfferView), fill the new field in MakeMarketView / OfferView below, THEN update the number. Do
// not just update the number.
static_assert(sizeof(capi::MarketView) == 168,
              "capi::MarketView changed upstream. This is a SECOND, provisional copy of the overlay's "
              "market assembly: fill the new/changed field in MakeMarketView below, THEN update this size.");
static_assert(sizeof(capi::MarketOfferView) == 372,
              "capi::MarketOfferView changed upstream. This is a SECOND, provisional copy of the overlay's "
              "market assembly: fill the new/changed field in OfferView below, THEN update this size.");

// The REP band's i18n TOKEN -- the key the app looks up as `market.tier.<token>` (Market.tsx). The
// overlay takes it from market::REP_TIERS[idx].key, whose definition is not published; the six tokens
// are the same vocabulary wasm_main.cpp's kTierToken carries for query.hub (and the engine's static
// TierToken / TierNameKey in career_api_queries.cpp). A key, never a localized label.
const char* const kTierToken[6] = { "unknown", "prospect", "known", "established", "star", "legend" };

capi::CareerStateProvider g_state;

// market::RepInfoFor, rebuilt from what it is made of. The band is RepDisplayBand (the engine's own
// truth, which RepInfoFor also calls); the floor of each band is bal::REP_BAND, which REP_TIERS[i].min
// is documented to mirror; the colour is RatingTierColor, documented as the mirror of REP_TIERS' colours.
// pct is RepInfoFor's: ROUNDED (+0.5), clamped 0..100, and 100 at the top band.
void FillReputation(capi::MarketView& v, int rep) {
    int idx = career::RepDisplayBand(rep);
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    v.repScore  = rep;
    v.repIdx    = idx;
    v.tierKey   = kTierToken[idx];
    v.tierColor = (ImU32)career::RatingTierColor(rep);
    v.hasNext   = idx < 5;
    int pct = 100;
    if (v.hasNext) {
        const int floorRep = career::bal::REP_BAND[idx], ceilRep = career::bal::REP_BAND[idx + 1];
        v.nextTierKey = kTierToken[idx + 1];
        v.nextTierMin = ceilRep;
        pct = ceilRep > floorRep ? (int)((rep - floorRep) * 100.0 / (ceilRep - floorRep) + 0.5) : 100;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
    }
    v.repPct = pct;
    // One dot per tier, in each tier's own colour (the overlay walks REP_TIERS); RatingTierColor at a band's
    // floor IS that band's colour.
    for (int i = 0; i < 6; i++) {
        capi::MarketTierDot d;
        d.color = (ImU32)career::RatingTierColor(career::bal::REP_BAND[i]);
        v.tierDots.push_back(d);
    }
}

// MarketSigningOpen: the signing window, or the decision week that closes it.
bool SigningOpen(int year, int doy) {
    const int phase = market::MarketPhaseFor(year, doy, career::kStartYear);
    return phase == market::MARKET_WINDOW || phase == market::MARKET_DECISION;
}

// MarketUiOfferState: 2 = declined, 1 = accepted (an active contract for the offer's series; the offer id
// is "of_" + series_id), 0 = on the table.
int OfferState(const career::SaveData& save, const std::string& offerId) {
    if (save.declined_offers.count(offerId) > 0) return 2;
    std::string sid = offerId;
    const std::string pfx = "of_";
    if (sid.rfind(pfx, 0) == 0) sid = sid.substr(pfx.size());
    return career::ContractForSeries(save, sid) ? 1 : 0;
}

// The open negotiation thread for an offer. NegoFindByOffer is the published lookup but takes a mutable
// save, and a query must not need one; this is its loop, read-only.
const career::Negotiation* OpenThreadFor(const career::SaveData& save, const std::string& offerId) {
    for (const auto& n : save.negotiations)
        if (n.offer.id == offerId && career::NegoThreadOpen(n.state)) return &n;
    return nullptr;
}

// MarketUiOffers, minus the parts that are not a query's business. The two gates come first and in the
// overlay's order: outside the signing window the board is EMPTY (the market has hours), and before the
// measurement race a rookie has no offers at all (the onboarding gate). Past both, the offers are
// market::GenerateOffers over the same input the overlay builds.
//
// Left out on purpose: the overlay's MarketUiOffers also OPENS and CLOSES seat watches, mails the team's
// verdict and saves the career, from inside this read (its own comment flags those mutations as misplaced
// in a query). The save this host reads through the career-state seat is const, and a query that moves
// the world is exactly what that comment warns against; so the contest report is not requested, and the
// watches already on the save are shown as they stand.
std::vector<market::GenOffer> LiveOffers(const career::SaveData& save, int rep, int frontier, int year, int doy) {
    if (!SigningOpen(year, doy)) return {};
    if (!save.prog.calibrated) return {};
    market::OfferGenInput in;
    in.rep            = rep;
    in.frontier_phase = frontier;
    in.unlocked_gates = &save.unlocked_gates;
    in.year           = year;
    in.cursor_doy     = doy;
    in.rating         = save.prog.rating;
    in.form           = career::CareerForm(save.prog);
    in.rating_races   = save.prog.rating_races;
    in.credits        = save.prog.credits;
    in.career_seed    = save.career_seed;
    in.cleanliness    = save.prog.cleanliness;
    {
        const double mn = (double)save.prog.credits / career::bal::MK_PAYDRIVER_MONEY_REF;
        in.money_norm = mn < 0.0 ? 0.0 : (mn > 1.0 ? 1.0 : mn);
    }
    in.world_year   = save.current_world_year > 0 ? save.current_world_year : year;
    in.quality_bump = career::CardSpecialActive(save.cards, "market.offer_quality") ? 1 : 0;
    return market::GenerateOffers(in, &save, nullptr);
}

capi::MarketOfferView OfferView(const market::GenOffer& g, const char* category, const career::SaveData& save,
                                const career::CareerCalendar& cal, int playerRating) {
    const market::OfferDef& o = g.def;
    capi::MarketOfferView ov;
    ov.id = o.id;
    ov.category = category;
    ov.state = OfferState(save, o.id);
    ov.held = g.held;
    const bool direct = o.direct && !g.held;
    ov.direct = direct;
    ov.team = o.team; ov.from = o.from; ov.title = o.title; ov.titleEn = o.title_en;
    ov.teamColor = o.color;
    // The class, as the overlay resolves it: MarketClassInfo (career_data.cpp) over the UI's own class
    // catalog, falling back to the raw class id and the offer's own colour for a class it does not know.
    {
        std::string cn; unsigned int dc = 0;
        if (career::MarketClassInfo(o.clsId, cn, dc)) { ov.className = cn; ov.discColor = (ImU32)dc; }
        else { ov.className = o.clsId; ov.discColor = o.color; }
    }
    ov.clsId = o.clsId; ov.scope = o.scope; ov.commitment = o.commitment;
    const market::ContractType& ct = market::ContractTypeInfo(o.contractKind);
    ov.contractKind = o.contractKind; ov.contractKindIcon = ct.icon; ov.contractKindColor = ct.color;
    ov.archetype = o.archetype;
    ov.rivalId = g.rival_id; ov.rivalName = g.rival_name;
    {
        const int absToday = career::dateutil::AbsDayFromYearDoy(cal.year, cal.cursor_doy);
        int fs;
        if (career::OfferFirstSeen(save, o.id, fs)) ov.weeksLeft = career::WeeksLeftFromFirstSeen(fs, absToday);
        else ov.weeksLeft = career::WeeksLeftFromFirstSeen(absToday, absToday);   // not sealed yet -> the full term
        if (ov.state == 0) ov.daysLeft = career::OfferDeathAbs(save, o.id, absToday) - absToday;
    }
    ov.negotiable = career::NegoNegotiable(o);
    if (const career::Negotiation* th = OpenThreadFor(save, o.id)) {
        ov.negState = th->state;
        const int absToday = career::NegoAbsDay(cal.year, cal.cursor_doy);
        ov.negRespondDays = (th->state == career::NEGO_TH_WAITING && th->respond_day > absToday)
                                ? th->respond_day - absToday : 0;
    }
    ov.requiresTryout = o.requiresTryout; ov.canAfford = o.canAfford; ov.reqRep = o.reqRep;
    ov.displacement = o.displacement; ov.buyIn = o.buy_in;
    if      (o.reqRep >= 500) ov.expectationKey = "market.expect.win";
    else if (o.reqRep >= 280) ov.expectationKey = "market.expect.top5";
    else if (o.reqRep >= 120) ov.expectationKey = "market.expect.experience";
    else                      ov.expectationKey = "market.expect.finish";
    ov.expectation = o.expect; ov.expectationEn = o.expect_en;
    const career::OfferPresentationFacets f = career::DeriveOfferPresentation(o.contractKind, o.archetype, nullptr);
    ov.typeLabel = f.typeLabel;
    ov.seatEntryCovered = f.seatEntryCovered; ov.consumablesCovered = f.consumablesCovered;
    ov.damageCovered = f.damageCovered; ov.travelCovered = f.travelCovered;
    {
        const int tl = f.typeLabel < 0 ? 0 : (f.typeLabel > 5 ? 5 : f.typeLabel);
        char pk[24];
        std::snprintf(pk, sizeof(pk), "ctype_pitch_%d", tl);
        ov.pitchKey = pk;
    }
    ov.pitchPrestige = (o.reqRep >= 280);
    ov.whyRating = o.whyRating > 0 ? o.whyRating : 55;
    ov.whyForm   = o.whyForm   > 0 ? o.whyForm   : 70;
    ov.whyRel    = o.whyRel    > 0 ? o.whyRel    : (direct ? 70 : 50);
    ov.signBonus = o.signBonus; ov.perRace = o.perRace; ov.seatName = o.seatName;
    ov.lockReason = g.lock_reason; ov.lockReasonEn = g.lock_reason_en;
    // The fixed car+livery preview (fixesCar) goes through the overlay's CalUiOfferSeat, which needs
    // ResolveTeamSeat -- not defined anywhere this tree publishes (checked: no header declares it, no
    // published source defines it). fixesCar stays false, which the app reads as "the player chooses":
    // it draws no car rather than a guessed one.
    const bool repOk = o.direct || (playerRating >= o.reqRep);
    if (!g.held && ov.state == 0 && repOk) {
        for (const auto& sid : career::OfferConflicts(o, cal, save)) {
            capi::MarketConflictView mc;
            const career::SeriesDef* sd = career::SeriesById(sid);
            mc.seriesName = sd ? sd->display : sid;
            ov.conflicts.push_back(mc);
        }
    }
    return ov;
}

capi::MarketView MakeMarketView() {
    capi::MarketView v;
    if (!g_state) return v;
    const capi::CareerStateView st = g_state();
    if (!st.loaded || !st.save || !st.calendar) return v;   // loaded=false -> the serializer says not_ready
    const career::SaveData& save = *st.save;
    const career::CareerCalendar& cal = *st.calendar;
    v.loaded = true;

    // The market's hours, so the screen can say "closed until September" instead of showing an empty list
    // that reads as a failure.
    v.marketOpen   = SigningOpen(cal.year, cal.cursor_doy);
    v.daysToWindow = market::DaysToMarketWindow(cal.year, cal.cursor_doy, career::kStartYear);

    const int rep = save.prog.rep;
    FillReputation(v, rep);
    for (const auto& kv : save.class_stats) {
        v.wins += kv.second.wins; v.podiums += kv.second.podiums; v.races += kv.second.races;
    }

    // The contract banner, rebuilt structurally like the overlay's: one active contract names its team,
    // series and kind; several list their series. team_id already holds the team LABEL (career_contract.cpp).
    {
        std::vector<const career::Contract*> act;
        for (const auto& c : save.contracts) if (c.active) act.push_back(&c);
        v.contract.active = !act.empty();
        v.contract.count = (int)act.size();
        if (act.size() == 1) {
            const career::Contract& c = *act[0];
            v.contract.teamName = c.team_id;
            const career::SeriesDef* d = career::SeriesById(c.series_id);
            v.contract.seriesName = d ? d->display : c.series_id;
            v.contract.kind = c.kind;
        } else {
            for (const career::Contract* c : act) {
                const career::SeriesDef* d = career::SeriesById(c->series_id);
                v.contract.seriesNames.push_back(d ? d->display : c->series_id);
            }
        }
    }

    // ProfilePlayerRatingUi: the persisted rating, rounded; a career never seeded reads as the rookie base.
    v.playerRating = (int)(save.prog.rating + 0.5);
    if (v.playerRating <= 0) v.playerRating = (int)career::bal::RATING_ROOKIE_BASE;

    // The offers, in the overlay's two buckets: direct ones (not held, not declined) first, then the open
    // board sorted by required reputation.
    const std::vector<market::GenOffer> gen = LiveOffers(save, rep, save.prog.frontier_phase, cal.year, cal.cursor_doy);
    std::vector<const market::GenOffer*> directs, opens;
    for (const auto& g : gen) {
        const bool isDirect = g.def.direct && !g.held;
        if (isDirect) { if (OfferState(save, g.def.id) != 2) directs.push_back(&g); }
        else opens.push_back(&g);
    }
    std::sort(opens.begin(), opens.end(),
              [](const market::GenOffer* a, const market::GenOffer* b) { return a->def.reqRep < b->def.reqRep; });
    v.seekingYouCount = (int)directs.size();
    v.openSeatsCount  = (int)opens.size();

    // Seats watching the player: separate from the offers on purpose, because they are not the player's yet.
    for (const auto& w : save.seat_watches) {
        if (w.state != 0) continue;
        capi::MarketWatchView wv;
        wv.seriesId = w.series_id;
        const career::SeriesDef* sd = career::SeriesById(w.series_id);
        wv.seriesName = sd ? sd->display : w.series_id;
        wv.teamName = w.team_label;
        wv.rivalName = w.rival_name;
        wv.racesLeft = career::SeatWatchRacesLeft(save, w);
        v.watching.push_back(wv);
    }

    for (const market::GenOffer* gp : directs) v.offers.push_back(OfferView(*gp, "direct", save, cal, v.playerRating));
    for (const market::GenOffer* gp : opens)   v.offers.push_back(OfferView(*gp, "open", save, cal, v.playerRating));
    return v;
}

}  // namespace

namespace wasmhost {

void InstallMarketView(capi::CareerStateProvider state) {
    g_state = std::move(state);
    capi::SetMarketViewProvider(&MakeMarketView);
}

}  // namespace wasmhost
