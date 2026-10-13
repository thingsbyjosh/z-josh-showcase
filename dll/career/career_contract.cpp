#include "career_contract.h"
#include "career_dateutil.h"       // real-time model: DaysInYear/AbsDayFromYearDoy (leap-aware)
#include "career_balance.h"       // bal::REP_BROKEN_CONTRACT
#include "career_econ.h"          // ContractCoverageProfile (exclusivity + fixes_car at sign)
#include "career_archetype.h"     // renewal fix (21/07): commitment=SEASONS only on renewal
#include "career_market.h"        // market::GenOffer, GenerateOffers, OfferGenInput (only in the .cpp)
#include "career_series.h"        // CalendarForSeries, CalRound
#include "career_season.h"        // CareerForm(prog) -- the ONE form source shared with the why-bar
#include "career_mail.h"          // Living mail (2026-07-13 spec): MailParams/MailHash/MailVariantCount/MailManagerName
#include "career_cards.h"         // Living mail: CardById (the sponsor mail's sender brand)
#include "career_seats.h"         // SillySeasonFreeSeats (seats freed up mid-season)
#include "career_nego_thread.h"   // Negotiation redesign: NegoTick/NegoCloseAllOpen -- the thread's pulse
#include "career_data_json.h"     // DriverById(duel_rival_id)->base_aggression -- the duel's tone
#include "career_events.h"        // SpecialEventById/SpecialEventRegistryCount -- invite_event expiration
#include <algorithm>              // std::sort, std::unique
#include <set>
#include <cstdlib>                 // atol (numeric params: ends_at)
#include <ctime>                   // time(nullptr) -- invite_event's real expiration
#include <chrono>                 // clock diagnostic (ZTA_CLOCK_PROFILE)
#include <cstdarg>                // ditto
#include <cstdio>                 // ditto

namespace career {

// local helper: an offer's series_id (its id is "of_"+series_id; falls back to the raw id if it doesn't match).
static std::string seriesIdOfOffer(const market::OfferDef& offer) {
    std::string sid = offer.id;
    const std::string pfx = "of_";
    if (sid.rfind(pfx, 0) == 0) sid = sid.substr(pfx.size());
    // v0.4.0 rival offers: id "of_<serie>#rv<i>" -> the series is whatever comes before the '#' (series ids never
    // carry a '#'; the suffix only distinguishes the rival team bidding within the SAME series).
    size_t h = sid.find('#');
    if (h != std::string::npos) sid = sid.substr(0, h);
    return sid;
}

// Is any active contract EXCLUSIVE? Resolves each contract's profile by its
// (kind, archetype) -- the flag's source of truth (ContractCoverageProfile(...).exclusive); the
// persisted Contract.exclusive flag is a mirror (set by AcceptOffer at signing) and also counts, in case a save
// carries the flag but the mapping has since changed. Bounded iteration over save.contracts (DoS-safe). Pure.
bool AnyExclusiveContractHeld(const SaveData& save) {
    for (const auto& c : save.contracts) {
        if (!c.active) continue;
        if (c.exclusive) return true;                                  // persisted flag (mirror)
        if (ContractCoverageProfile(c.kind, c.archetype).exclusive) return true;   // the type's profile
    }
    return false;
}

// (24/08): the PER-SEASON variant -- the one AcceptOffer queries. Same lesson as the duplicate for
// series+season below (v0.6.3): an exclusive contract that ENDS this season can't veto signing for
// the NEXT one. With window offers pointing at the new year, a single exclusive contract left EVERY
// market Accept dead (Decline still worked: it doesn't sign anything), and the player could never
// switch teams. It only vetoes an exclusive whose range [signed_year, signed_year+seasons-1] covers the
// TARGET season. signed_year<=0 (a legacy save with no stamp) always vetoes -- conservative, like the old gate.
bool AnyExclusiveContractHeldFor(const SaveData& save, int targetYear) {
    for (const auto& c : save.contracts) {
        if (!c.active) continue;
        const bool excl = c.exclusive || ContractCoverageProfile(c.kind, c.archetype).exclusive;
        if (!excl) continue;
        if (c.signed_year <= 0) return true;
        const int hasta = c.signed_year + (c.seasons_total > 0 ? c.seasons_total : 1) - 1;
        if (targetYear >= c.signed_year && targetYear <= hasta) return true;
    }
    return false;
}

// See the doc in career_contract.h. team_display/per_race come from the contract for the SERIES
// actually raced (raceSeriesId); final_series follows ITS OWN rule (seasons_left==1), without breaking if they're different
// contracts. There's no early 'break': it walks ALL active contracts because the two lookups are independent.
void FillSettleContractCtx(const SaveData& save, const std::string& raceSeriesId, EmailGenContext& ctx) {
    for (const auto& c : save.contracts) {
        if (!c.active) continue;
        if (!raceSeriesId.empty() && c.series_id == raceSeriesId) {
            ctx.team_display = c.team_id;
            ctx.per_race = c.per_race;
        }
        if (c.seasons_left == 1) ctx.final_series = c.series_id;
    }
}

// See career_contract.h. The rescue path for saves that signed using the clock's year.
int RepairWindowSignedContracts(SaveData& save, CareerCalendar& cc) {
    // The season that's actually due to be signed today. If it matches the clock's year there's nothing to repair:
    // either we're outside the window (where signing with the clock's year is correct) or it's the
    // compressed year-1 cycle, which signs for the current year on purpose. Neither case was a bug.
    const int destino = market::SigningSeasonFor(cc.year, cc.cursor_doy, kStartYear);
    if (destino == cc.year) return 0;
    int reparados = 0;
    for (auto& c : save.contracts) {
        if (!c.active) continue;
        if (c.signed_year != cc.year) continue;   // already carries the right season (or is from a different era)
        // the three alibis for a LEGITIMATE contract in the current season ---
        if (c.premium_year != 0) continue;        // paid a premium at a rollover: it existed before this window
        if (c.missed_rounds != 0) continue;       // accumulates absences: the season has been running under it
        if (const SeasonState* e = FindEnrollment(cc, c.series_id))
            if (e->round_idx != 0 || !e->history.empty()) continue;   // has races behind it
        // this is the buggy contract: it gets stamped with the season it was actually signed for ---
        c.signed_year = destino;
        // ...and the early enrollment in the DEAD season is withdrawn. It's untouched (we just
        // verified that), so nothing is lost; leaving it would make every clock advance count its
        // already-past rounds as absences, which is half the damage. The rollover enrolls the correct season.
        for (size_t i = 0; i < cc.enrollments.size(); i++)
            if (cc.enrollments[i].series_id == c.series_id) {
                cc.enrollments.erase(cc.enrollments.begin() + (long)i);
                break;
            }
        reparados++;
    }
    return reparados;
}

// See career_contract.h. THIS function IS AcceptOffer's block of gates (which calls it), so it
// can't diverge from it; returns the key of the first gate that trips ("" = would sign).
std::string AcceptRejectReason(const market::OfferDef& offer, const SaveData& save, int year) {
    // the offer's series_id: the id is "of_"+series_id (career_market.cpp). We derive it by stripping the prefix;
    // if the format doesn't match, we fall back to using the id as-is (defensive, shouldn't happen with real offers).
    std::string seriesId = seriesIdOfOffer(offer);
    if (seriesId.empty()) return "refused";      // an offer with no series -> nothing to sign
    // MULTI-CONTRACT: you can hold several contracts at once (different series). Only DUPLICATING the one for
    // the SAME series is refused (you're already signed there). Other series can be signed freely (real life juggles them).
    //
    // ...but the duplicate check is PER SERIES **AND SEASON**, not just per series. It used to be enough to have a
    // live contract in a series to block you from signing ANYTHING in it, and that killed the career at the top end:
    // measured with the simulator, the player reached F1 in 2029, and in December 2030 they were offered 24 seats
    // which were the 8 seats in one series, 11 in another, and 4 in a third -- exactly their three series. All
    // 24 failed with "already", in January their contracts expired, and they were left with nothing: zero races in 2031.
    // They couldn't move from Halcyon to Stonefield Motors because the gate never checked whether their contract was ending.
    // What's actually impossible is holding TWO seats in the same series in THE SAME SEASON; signing in
    // December for next year while finishing out the current one is, literally, how a transfer works.
    for (const auto& c : save.contracts) {
        if (!c.active || c.series_id != seriesId) continue;
        const int hasta = c.signed_year + (c.seasons_total > 0 ? c.seasons_total : 1) - 1;
        if (year >= c.signed_year && year <= hasta) return "already";   // they overlap: a genuine duplicate
    }
    // EXCLUSIVITY GATE: if you already hold an EXCLUSIVE contract covering THIS season, that
    // team demands exclusivity -> you CANNOT sign another (a fine-grained gate at signing time; the save isn't touched). The
    // open/low-commitment (non-exclusive) types can still stack with no cap. Evaluated BEFORE
    // the contract is built.
    // (24/08): PER SEASON, the same lesson as the duplicate check above. The bare gate used to veto signing
    // ANYTHING while any exclusive contract was alive, even if it ended this year and the offer was for the
    // NEXT one: in December every market Accept died silently (Decline still worked), and the
    // player could never switch teams.
    if (AnyExclusiveContractHeldFor(save, year)) return "exclusive";
    // PAY-DRIVER SOLVENCY GATE: a pay-driver seat carries a
    // BUY-IN the player pays up front. It is charged AT SIGNING and is solvency-gated -- if the player cannot
    // afford it the sign is BLOCKED (no contract, no negative credits), mirroring Enroll's `credits < ins ->
    // return false`. Checked BEFORE any state mutation so a blocked sign leaves the save untouched.
    if (offer.buy_in > 0 && save.prog.credits < offer.buy_in) return "afford";
    // Third route (tester feedback, Aug 27): the Paddock WINDOW was signing series with a pending superlicense.
    // The market already holds its offers back (gateLock), but THIS list is the single signing gate: the lock
    // lives here and covers market, paddock and negotiation all at once. A local copy of seriesIsGated (private
    // to career_market.cpp -- same note as subSeriesGated further down in this file).
    {
        bool gated = false;
        for (const auto& q : AllSeries()) if (q.unlocks == seriesId) { gated = true; break; }
        if (gated && save.unlocked_gates.count(seriesId) == 0) return "superlicense";
    }
    return std::string();
}

bool AcceptOffer(const market::OfferDef& offer, SaveData& save, CareerCalendar& cc, int year,
                 const DealTerms* terms) {
    // the gates live in AcceptRejectReason (same order, same semantics) so the API can
    // tell the player WHY the sign was refused without duplicating the predicates here.
    if (!AcceptRejectReason(offer, save, year).empty()) return false;
    std::string seriesId = seriesIdOfOffer(offer);

    // building the contract: works (kind 0) iff the offer is direct (a closed seat + enough REP);
    // otherwise, customer (kind 1). v1: 1 season. The economics (bonus/per_race) come from the offer.
    Contract ct;
    ct.series_id     = seriesId;
    ct.team_id       = offer.team;           // the offer's synthetic team label
    // THE SEAT YOU ACTUALLY SIGN. It used to be hardcoded to 0, so the engine believed
    // you occupied seat 0 of the series no matter who you signed with. It's not cosmetic: PlayerSeatKeys protects
    // SeatKey(series, seat_idx) from the living world's cascade, and the rollover FREES that same index when you're
    // not renewed (career_calendar.cpp:1432). With 0 hardcoded, the world was protecting ANOTHER team's seat, and
    // failing to renew you opened the seat for ANOTHER team, while your own team kept deriving its usual driver
    // as if you weren't there -- which is exactly what PlayerSeatKeys' comment assumes.
    // It's resolved by the SAME team label you sign with; if the team runs two cars, whichever one
    // is FREE wins. No match -> -1 (no seat): a wrong index is worse than none,
    // because it protects and frees seats that belong to someone else. Both signing paths (the market and the Paddock window) go
    // through here, so it only needs to be fixed once.
    ct.seat_idx      = -1;
    { const std::vector<Seat> asientos = SeatsForClass(seriesId, save);
      for (size_t i = 0; i < asientos.size(); i++) {
          if (asientos[i].team_label != ct.team_id) continue;
          if (ct.seat_idx < 0) ct.seat_idx = (int)i;              // the first one by index: total ordering
          if (asientos[i].open) { ct.seat_idx = (int)i; break; }  // but whichever one is FREE wins
      } }
    // The contract type is decided by the offer (works/customer/pay_driver/trial/conditional). We only
    // fall back to the direct/open heuristic when the offer carries the inherited default (0) -> back-compat with
    // old offers that don't fill in contractKind.
    ct.kind          = offer.contractKind != 0 ? offer.contractKind : (offer.direct ? 0 : 1);
    ct.archetype     = offer.archetype;      // carry the narrative archetype onto the contract (mirror ct.kind)
    // persists the NEGOTIATED package onto the contract so signing reflects what was negotiated (otherwise,
    // Submit == Accept-as-is == a plain Accept would all be the same). nullptr = the BASE offer (a neutral deal) for a simple accept.
    if (terms) ct.accepted_deal = *terms;
    // renewal fix (21/07): an over-performance renewal advertises 2 seasons (commitment=2,
    // BuildRenewalOffer), but this used to ALWAYS sign 1 (v1) -> the offer promised a length the contract
    // never delivered. Only for renewals does commitment mean SEASONS; in market offers it means the calendar's
    // ROUNDS (career_market.cpp o.commitment=(int)cal.size()), and there it still signs for 1. Clamped 1..3.
    int seasons = 1;
    if (offer.archetype == ARCH_RENEWAL && offer.commitment >= 1)
        seasons = offer.commitment > 3 ? 3 : offer.commitment;
    ct.seasons_total = seasons;
    ct.seasons_left  = seasons;
    ct.sign_bonus    = offer.signBonus;
    ct.per_race      = offer.perRace;
    ct.signed_year   = year;
    ct.active        = true;
    ct.promote_to    = offer.promote_to;   // conditional-contract target ("" otherwise)
    // Snapshot the resolved coverage profile for THIS contract's type (kind+archetype). The
    // exclusivity flag is mirrored onto the contract (persisted) so a future sign is gated even after a save
    // round-trip; the buy-in is RECORDED from the offer terms but NOT debited here (that debit happens
    // it). For a works/brand seat (profile.fixes_car) the contract carries the team's car so the car-set infra
    // applies it and the garage hides the picker; privateer/rental leave it empty (player chooses).
    CoverageProfile prof = ContractCoverageProfile(ct.kind, ct.archetype);
    ct.exclusive     = prof.exclusive;     // Persist the type's exclusivity flag (the gate's source)
    ct.buy_in        = offer.buy_in;       // Record the pay-driver buy-in (debited just below)
    // v0.4.0 SEASON objectives (21/07 feedback): the team sets its demands AT SIGNING, based on the contract's real
    // TYPE (same index as the economics). The rollover judges renewal/dismissal AGAINST this objective.
    { int ti = ContractTypeIndex(ct.kind, ct.archetype);
      if (ti < 0 || ti >= bal::COVERAGE_TYPE_COUNT) ti = 0;
      ct.obj_target_pos = bal::SEASON_OBJ_TARGET[ti]; }
    if (prof.fixes_car) {                   // A works/brand contract FIXES the car -> the team's car is used
        // The team's car for a fixed-car seat is the class spec the team runs ("<clsId>_spec" mirrors the
        // garage's synthesized owned-car id). Empty clsId degrades to the series id so team_car is non-empty
        // (works/brand => never a player picker). team_livery left empty -> the launch flow fills the livery.
        ct.team_car = !offer.clsId.empty() ? offer.clsId : seriesId;
    }                                       // privateer/rental: team_car/team_livery stay empty -> player selects
    // obj_done/obj_total stay 0 here; the trial objective set is assigned by the season/objective layer.

    // enrolls the series with the entry fee WAIVED (the team covers it). Doesn't require a balance or charge an entry fee.
    //
    // ...but ONLY if the contract is for the CURRENT season. The signing window closes in DECEMBER, and
    // it signs for the FOLLOWING year: enrolling there creates a new season at round 0 (StartSeason) whose
    // rounds have ALL already passed, and from that point every clock advance counts them as absences
    // (DnfPassedRounds -> applyNoShowSting), so the freshly signed contract would break itself within three rounds.
    // The simulator exposed it, where the bug disguises itself in reverse: since it races before
    // advancing, those dead rounds got RACED instead of failed -- 31 races on day 349 of 2025, two
    // whole seasons in a single clock step, with their money and rating attached.
    // There's no need to compensate for it anywhere: the rollover already re-enrolls these contracts (its
    // `signed_year >= cc.year + 1` -> reEnrollSeries branch) and it also clears cc.enrollments first, so this
    // early enrollment was getting thrown away anyway.
    if (year <= cc.year) Enroll(cc, seriesId, save.prog, /*waiveInscription=*/true);
    // Signing a NEW contract in this series RESETS the car+skin latch -> "you'll be running your
    // new team's livery". The 1st race under the contract re-latches (with works/customer, the seat decides,
    // with privateer the player re-chooses). Without this you'd keep the previous team/era's skin.
    if (SeasonState* e = FindEnrollment(cc, seriesId)) { e->livery_ord = -1; e->livery_veh = 0; }
    // the new contract also cancels the privateer's durable "marriage" in this series
    // ("you're married to that team UNLESS you land another contract in the discipline").
    save.privateer_pick.erase(seriesId);

    // DEBIT the pay-driver buy-in at signing (solvency already checked above -> never goes
    // negative). For every non-pay seat offer.buy_in is 0 -> no-op (byte-identical). The buy-in is the player
    // paying for an above-pace seat he brought budget to (the Latifi path).
    save.prog.credits -= offer.buy_in;
    if (offer.buy_in != 0) EconLog("contract_buyin", -offer.buy_in, save.prog.credits);   //
    // signing bonus -> goes to the player's credits
    save.prog.credits += offer.signBonus;
    if (offer.signBonus != 0) EconLog("sign_bonus", offer.signBonus, save.prog.credits);   //

    save.contracts.push_back(ct);            // multi-contract: adds (doesn't replace)
    return true;
}

// v0.4.0 silly season: signing a RENEWAL offer. With the series' contract still ACTIVE (the
// early "#early" one, signed mid-season) the contract is EXTENDED in place -- a new one isn't created:
// +seasons (commitment, renewals only; clamped 1..3), a new per_race figure (FROZEN at signing,
// consistent with the earlier fix), the signing bonus paid out, and the season objective re-set by type. With no
// active contract (the rollover's final renewal: the seat has already been freed) it falls through to AcceptOffer, the normal path.
bool AcceptRenewal(const market::OfferDef& ren, SaveData& save, CareerCalendar& cc, int year,
                   const DealTerms* terms) {
    std::string sid = seriesIdOfOffer(ren);
    if (Contract* c = ContractForSeriesMut(save, sid)) {
        int add = 1;
        if (ren.archetype == ARCH_RENEWAL && ren.commitment >= 1)
            add = ren.commitment > 3 ? 3 : ren.commitment;
        c->seasons_left  += add;
        c->seasons_total += add;
        c->per_race   = (ren.perRace > 0) ? ren.perRace : c->per_race;
        c->sign_bonus = ren.signBonus;
        save.prog.credits += ren.signBonus;
        if (ren.signBonus != 0) EconLog("renew_early_bonus", ren.signBonus, save.prog.credits);   //
        { int ti = ContractTypeIndex(c->kind, c->archetype);
          if (ti < 0 || ti >= bal::COVERAGE_TYPE_COUNT) ti = 0;
          c->obj_target_pos = bal::SEASON_OBJ_TARGET[ti]; }
        if (terms) c->accepted_deal = *terms;
        return true;
    }
    return AcceptOffer(ren, save, cc, year, terms);
}

// WHAT LEAVING COSTS, derived and not persisted -> the UI can warn about the EXACT amount before
// you click, and it can never fall out of sync with what actually gets charged (it's the same function).
// empty seriesId = the sum of breaking ALL of them, which is what the hub's generic button does.
long BreakContractFee(const SaveData& save, const std::string& seriesId) {
    long total = 0;
    for (const auto& c : save.contracts) {
        if (!c.active) continue;
        if (!seriesId.empty() && c.series_id != seriesId) continue;
        long fee = c.per_race * bal::MK_BREAK_RACES_OWED;
        if (fee <= 0) {
            // a seat that does NOT pay you (customer/pay-driver): it's anchored to its tier's entry cost, the same
            // anchor as the privateer split, so both exits cost the same currency.
            const SeriesDef* sd = SeriesById(c.series_id);
            int lvl = sd ? sd->level : 1;
            if (lvl < 1) lvl = 1; if (lvl > 7) lvl = 7;
            fee = (long)(bal::ENTRY_BY_PHASE[lvl - 1] * bal::MK_BREAK_ENTRY_MULT);
        }
        const int quedan = c.seasons_left > 0 ? c.seasons_left : 1;
        total += fee * quedan;
    }
    return total;
}

void BreakContract(SaveData& save, const std::string& seriesId) {
    // the amount is computed BEFORE anything is deleted: afterward the contracts are gone and it would come out as 0.
    const long fee = BreakContractFee(save, seriesId);
    // the relationship marker too, ALSO before deleting (afterward there's no way to know which team you left): the
    // abandoned team COOLS OFF (rel_marks -> folded into TeamRelationship for its future offers). The same
    // filter as the severance payment: only active contracts, and only the one for a given series if one was requested.
    for (const auto& c : save.contracts) {
        if (!c.active || c.team_id.empty()) continue;
        if (!seriesId.empty() && c.series_id != seriesId) continue;
        save.rel_marks.push_back({ c.team_id, save.current_world_year, bal::MK_BREAK_MARK_X1000 });
    }
    bool broke = false;
    if (seriesId.empty()) {                  // terminate ALL active contracts (the hub's generic button)
        if (!save.contracts.empty()) { save.contracts.clear(); broke = true; }
    } else {                                 // terminate the one for THAT series
        for (size_t i = 0; i < save.contracts.size(); i++)
            if (save.contracts[i].active && save.contracts[i].series_id == seriesId) {
                save.contracts.erase(save.contracts.begin() + i); broke = true; break; }
    }
    if (broke) {                             // a single REP penalty for the action
        save.prog.rep += bal::REP_BROKEN_CONTRACT;
        if (save.prog.rep < 0) save.prog.rep = 0;
        // ...and the SEVERANCE payment. It's charged as far as the wallet allows and stops at 0: leaving
        // the player in the red would trap them in a career they can't play, and the real punishment
        // is ending up with no seat and no money, not a debt that can never be paid off.
        if (fee > 0) {
            save.prog.credits -= fee;
            if (save.prog.credits < 0) save.prog.credits = 0;
        }
    }
}

// Discord «Canceling a contract doesn't affect the race schedule»: see career_contract.h. The one above deletes the
// contract but knows nothing about the calendar, and everything the calendar counts as "your rounds" comes out of
// cc.enrollments: the clash warning, the market's season clash, the jump's absences and the DNF when
// passing the date. Terminating by hand has to release that too.
int BreakContract(SaveData& save, CareerCalendar& cc, const std::string& seriesId) {
    // series with an active contract that sustains the CLOCK's season. A signing for the coming one does not
    // sustain it: AcceptOffer only enrolls with year <= cc.year, so it is the same boundary. signed_year <= 0
    // (legacy save without a stamp) counts as of this season, just as in the signing's brakes.
    auto sostenidas = [&]() {
        std::set<std::string> out;
        for (const auto& c : save.contracts) {
            if (!c.active) continue;
            if (!seriesId.empty() && c.series_id != seriesId) continue;
            if (c.signed_year > cc.year) continue;
            out.insert(c.series_id);
        }
        return out;
    };
    const std::set<std::string> antes = sostenidas();
    BreakContract(save, seriesId);   // amount, REP and relationship mark: a single door
    const std::set<std::string> despues = sostenidas();
    int retiradas = 0;
    for (const auto& sid : antes) {
        if (despues.count(sid)) continue;   // another active contract still sustains this season
        for (size_t i = 0; i < cc.enrollments.size(); i++) {
            if (cc.enrollments[i].series_id != sid) continue;
            // a closed season is a record: the rollover archives it in completed_seasons and already paid its
            // bonus. Breaking a two-year contract in November cannot erase the one you just finished.
            if (cc.enrollments[i].status == 1 || SeasonEnded(cc.enrollments[i])) break;
            // With rounds already run behind it, it is not kept either: what was run has already been settled (money, rating, ELO) and what
            // remains ahead is precisely what clashed and what would be counted as absences.
            cc.enrollments.erase(cc.enrollments.begin() + (long)i);
            retiradas++;
            break;
        }
    }
    return retiradas;
}

bool ContractFactorySeat(const SaveData& save, const std::string& seriesId) {
    const Contract* c = ContractForSeries(save, seriesId);
    return c && c->kind == 0;                // works (factory team): the team pays you
}

// ============================ market dynamics ============================

std::vector<std::string> OfferConflicts(const market::OfferDef& offer, const CareerCalendar& cc) {
    std::vector<std::string> out;
    std::string seriesId = seriesIdOfOffer(offer);
    if (seriesId.empty()) return out;

    // (tester feedback): this used to project each of the offer's rounds against ConflictsFor, which only looks at
    // the PENDING round of each enrollment. At the start of a career every enrollment is on round 1
    // (different days), so signing three series that clash mid-season didn't warn about anything, and the
    // clash would only show up later on the calendar. Now the FULL SEASONS are compared (SeasonClashesFor, the same
    // SlotsConflict rule the calendar uses), including each round's time and lap count: without the time, two
    // races on the same day at the SAME circuit used to be judged with a midnight window and never caught.
    for (const SeasonClash& sc : SeasonClashesFor(cc, seriesId)) out.push_back(sc.series_id);
    return out;
}

std::vector<std::string> OfferConflicts(const market::OfferDef& offer, const CareerCalendar& cc, const SaveData& save) {
    const int destino = market::SigningSeasonFor(cc.year, cc.cursor_doy, kStartYear);
    if (destino <= cc.year) return OfferConflicts(offer, cc);   // signed THIS season: their enrollments rule
    std::vector<std::string> out;
    const std::string seriesId = seriesIdOfOffer(offer);
    if (seriesId.empty()) return out;
    auto huecos = [destino](const std::string& sid) {
        std::vector<CalSlot> v;
        for (const auto& c : CalendarForSeriesInYear(sid, destino)) {
            if (c.doy <= 0) continue;
            CalSlot sl; sl.series_id = sid; sl.doy = c.doy; sl.track_hash = c.track_hash;
            sl.laps = c.laps; sl.durationMin = c.durationMin; sl.hour = c.hour; sl.valid = true;
            v.push_back(sl);
        }
        return v;
    };
    const std::vector<CalSlot> mios = huecos(seriesId);
    if (mios.empty()) return out;
    std::set<std::string> vistas;
    for (const auto& c : save.contracts) {
        if (!c.active || c.series_id.empty() || c.series_id == seriesId) continue;
        const int ultimo = c.signed_year + (c.seasons_total > 0 ? c.seasons_total : 1) - 1;
        if (c.signed_year > destino || ultimo < destino) continue;   // does not cover the season being signed
        if (!vistas.insert(c.series_id).second) continue;
        bool choca = false;
        for (const auto& o : huecos(c.series_id)) {
            for (const auto& m : mios) if (SlotsConflict(m, o)) { choca = true; break; }
            if (choca) break;
        }
        if (choca) out.push_back(c.series_id);
    }
    return out;
}

void RecordCourting(SaveData& save, const std::vector<market::GenOffer>& offers, int year) {
    for (const auto& go : offers) {
        if (!go.held) continue;                   // only HELD offers leave a courtship trail
        std::string seriesId = seriesIdOfOffer(go.def);
        if (seriesId.empty()) continue;
        const std::string& teamId = go.def.team;
        bool exists = false;                      // idempotent: don't duplicate (team_id, series_id)
        for (const auto& c : save.courting)
            if (c.team_id == teamId && c.series_id == seriesId) { exists = true; break; }
        if (exists) continue;
        Courting c;
        c.team_id         = teamId;
        c.series_id       = seriesId;
        c.first_seen_year = year;
        c.met_rep     = true;          // a HELD offer means you already clear the REP soft floor
        c.tier_locked = go.tier_locked;// XP-phase / superlicense still blocks it (the retained-offer state)
        save.courting.push_back(c);
    }
}

// offer_lifecycle: stamps the first time an offer is seen (idempotent).
// An already-LAPSED offer doesn't get re-stamped. Without this guard, GenerateOffers keeps
// returning the id after it expires (the series is still in its window), and the next AdvanceWeek pulse would
// re-stamp it with a new date -> it would end up in both lapsed_offers AND offer_first_seen at once (an inconsistent state:
// the OfferFirstSeen/weeksLeft accessor would report it "alive" while the display filter kept hiding it).
// It stays expired until the season purge (RolloverYear), just like declined_offers.
void MarkOfferSeen(career::SaveData& s, const std::string& id, int absDay) {
    if (id.empty()) return;
    if (s.lapsed_offers.count(id)) return;    // a lapsed offer doesn't get re-stamped (it stays expired until the season purge)
    s.offer_first_seen.emplace(id, absDay);   // emplace: doesn't overwrite if it already exists
}
// offer_lifecycle: moves offers older than MK_OFFER_TTL_DAYS into lapsed.
void LapseExpiredOffers(career::SaveData& s, int absDay) {
    for (auto it = s.offer_first_seen.begin(); it != s.offer_first_seen.end(); ) {
        // only UNDECIDED offers expire; a declined one stays visible (state=2) until the season purge
        if (s.declined_offers.count(it->first)) { ++it; continue; }
        if (absDay - it->second >= career::bal::MK_OFFER_TTL_DAYS) {
            s.lapsed_offers.insert(it->first);
            it = s.offer_first_seen.erase(it);
        } else ++it;
    }
}
// offer_lifecycle: a read-only accessor for save.offer_first_seen (for weeksLeft in the UI).
bool OfferFirstSeen(const career::SaveData& s, const std::string& id, int& out) {
    auto it = s.offer_first_seen.find(id);
    if (it == s.offer_first_seen.end()) return false;
    out = it->second;
    return true;
}

// offer_lifecycle: weeks (ceil) until an offer stamped at firstSeen expires, given today's absolute day.
// Range [0, ceil(MK_OFFER_TTL_DAYS/7)]. For an offer NOT yet stamped, the caller passes firstSeen=absToday (the full window).
int WeeksLeftFromFirstSeen(int firstSeen, int absToday) {
    int left = career::bal::MK_OFFER_TTL_DAYS - (absToday - firstSeen);
    return left > 0 ? (left + 6) / 7 : 0;
}

// 18/09 (Josh): see career_contract.h. An offer's deadline, those on their last day and the warning.
int OfferDeathAbs(const career::SaveData& s, const std::string& offerId, int absToday) {
    auto it = s.offer_first_seen.find(offerId);
    int muere = (it != s.offer_first_seen.end() ? it->second : absToday) + career::bal::MK_OFFER_TTL_DAYS;
    for (const auto& n : s.negotiations) {
        if (n.offer.id != offerId || !career::NegoThreadOpen(n.state)) continue;
        const int tope = (n.state == career::NEGO_TH_ACCEPTED_PENDING ? n.reserve_day : n.deadline_day) + 1;
        if (tope < muere) muere = tope;
    }
    int y = 0, d = 0;
    career::dateutil::YearDoyFromAbsDay(absToday, y, d);
    const int cierre = market::DaysToMarketClose(y, d, career::kStartYear);
    if (cierre > 0 && absToday + cierre < muere) muere = absToday + cierre;
    return muere;
}
std::vector<std::string> OffersOnLastDay(const career::SaveData& s, int absDay) {
    std::vector<std::string> out;
    int y = 0, d = 0;
    career::dateutil::YearDoyFromAbsDay(absDay, y, d);
    if (market::MarketPhaseFor(y, d, career::kStartYear) == market::MARKET_PADDOCK) return out;   // closed: cannot be signed
    const bool filtrar = (s.live_offers_day == absDay);
    for (const auto& kv : s.offer_first_seen) {
        const std::string& id = kv.first;
        if (s.declined_offers.count(id) || s.lapsed_offers.count(id) ||
            s.withdrawn_offers.count(id) || s.expired_offers.count(id)) continue;
        if (filtrar && !s.live_offer_ids.count(id)) continue;          // left the market without expiring
        if (OfferDeathAbs(s, id, absDay) - absDay != 1) continue;      // tomorrow it can no longer be signed
        std::string sid = id.rfind("of_", 0) == 0 ? id.substr(3) : id;
        const size_t h = sid.find('#');
        const bool early = (h != std::string::npos && sid.compare(h + 1, 5, "early") == 0);
        if (h != std::string::npos) sid = sid.substr(0, h);
        if (!early && career::ContractForSeries(s, sid)) continue;    // you already signed in that series
        out.push_back(id);
    }
    return out;
}
int ReopenOfferMailsOnLastDay(career::SaveData& s, int absDay) {
    int n = 0;
    for (const auto& id : OffersOnLastDay(s, absDay))
        for (auto& e : s.emails) {
            if (e.family != 0 || e.answered || e.subtype == career::bal::MK_MAIL_SUBTYPE_COURTING) continue;
            if (e.payload_key != id || e.id.rfind("renew_", 0) == 0) continue;
            if (e.read) { e.read = 0; n++; }
        }
    return n;
}

// if the player's BIRTHDAY (month, day from save.player) falls within (fromDoy, toDoy] of cc.year,
// emits Atenea's birthday greeting (deduped per year inside EmitBirthday). Feb 29 in a non-leap year resolves
// to Mar 1 (DoyFromYmd clamps via month addition). Call it after moving the cursor, BEFORE the year rollover.
static void EmitBirthdayIfCrossed(const CareerCalendar& cc, SaveData& save,
                                  const std::string& playerName, int fromDoy, int toDoy) {
    const auto& pl = save.player;
    if (pl.birth_year == 0 || pl.birth_month <= 0 || pl.birth_day <= 0) return;
    int b = career::dateutil::DoyFromYmd(cc.year, pl.birth_month, pl.birth_day);
    if (b > fromDoy && b <= toDoy) career::EmitBirthday(save, playerName, cc.year, b);
}

void AdvanceDays(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                 const std::string& playerName, int days) {
    if (days <= 0) return;
    int remaining = days;
    while (remaining > 0) {
        int room = career::dateutil::DaysInYear(cc.year) - cc.cursor_doy;   // days until Dec 31 (leap-aware)
        if (remaining <= room) {                    // fits within this year -> advance and finish
            int from = cc.cursor_doy;
            cc.cursor_doy += remaining;
            remaining = 0;
            EmitBirthdayIfCrossed(cc, save, playerName, from, cc.cursor_doy);   //
            DnfPassedRounds(cc, playerName, &save);
            for (auto& e : cc.enrollments)
                if (SeasonEnded(e)) FinalizeSeason(e, prog, playerName, NoShowSeasonForSave(save, e.series_id));
        } else {                                    // crosses the boundary: consumes 'room' up to year end, then rolls over
            int from = cc.cursor_doy;
            cc.cursor_doy = career::dateutil::DaysInYear(cc.year);
            remaining -= room;
            EmitBirthdayIfCrossed(cc, save, playerName, from, cc.cursor_doy);   // (the stretch up to Dec 31)
            DnfPassedRounds(cc, playerName, &save);
            for (auto& e : cc.enrollments)
                if (SeasonEnded(e)) FinalizeSeason(e, prog, playerName, NoShowSeasonForSave(save, e.series_id));
            // Negotiation redesign: bulk-closes open threads WHEN CROSSING the year (one mail per thread);
            // AdvanceDays doesn't carry a WeekTickResult in its signature -> an overload without 'res' (see career_nego_thread.h).
            career::NegoCloseAllOpen(save, career::NegoAbsDay(cc.year, cc.cursor_doy));
            RolloverYear(cc, prog, save, playerName);
            save.negotiations.clear();   // the new year's market starts clean; persisted mails keep the history
        }
    }
}

WeekTickResult RunWorldTick(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                            const std::string& playerName, bool advanced) {
    WeekTickResult res;

    // 1.5) churn ANCHORED to each class's signing window (replaces the silly-season
    //      weekly GLOBAL draw). Every reachable series releases its own churn quota when the
    //      cursor enters [round1.doy - MK_PRESEASON_LEAD, round1.doy) -- instead of a global weekly draw
    //      disconnected from the calendar. Reachable level = frontier_phase+1 (offers close to the player); a
    //      global MK_SILLY_OPEN_CAP cap (inside PreseasonWindowChurn/OpenPreseasonSeatsForSeries).
    if (advanced) {
        // 18/09 (Josh's decision): "for now the only window for moves will be until 31
        // December, everything stays sealed there". The preseason opening opened tail seats in the four
        // weeks before the first race of each category — that is, in January-March, with the market already
        // closed: nobody could sign those gaps until the following winter, and each opening triggered the
        // restart-AMS2 warning. Now it only opens WITHIN a signing period (September-December; in
        // year 1, its January-February start window, which is where your first contract comes from).
        if (market::MarketPhaseFor(cc.year, cc.cursor_doy, kStartYear) != market::MARKET_PADDOCK) {
            int freed = PreseasonWindowChurn(save, cc, save.prog.frontier_phase + 1);
            (void)freed;
        }
        // v0.4.0 silly season: once <= EARLY_RENEW_ROUNDS_LEFT rounds remain in your contract's last season,
        // the team opens the EARLY renewal (a provisional offer based on your current standing + a mail). Idempotent.
        EarlyRenewalTick(cc, save, playerName);
    }

    // 1.6) THE MARKET'S PULSE -- opens the signing window when the clock enters it, and
    //      resolves the decision week when it falls due (even if you skipped straight past it with a jump). It runs OUTSIDE the
    //      `if (advanced)` on purpose: a pulse with no time advance (loading the save, creating it) also has to
    //      leave the paddock consistent with the date, or the tab would open empty until someone touched the
    //      clock. Idempotent, so running it extra times costs nothing.
    { double mnw = (double)save.prog.credits / bal::MK_PAYDRIVER_MONEY_REF;
      if (mnw < 0.0) mnw = 0.0; else if (mnw > 1.0) mnw = 1.0;
      // The pulse carries your contracts' season ratio, because the calendar holds the
      // championship table and the window is what judges you. Without this your seat enters the window (it already
      // does) but you'd be judged like a stranger, ignoring the objective you were set when you signed.
      const std::map<std::string, double> ratios = career::PlayerSeasonRatios(cc, save, playerName);
      career::MarketCalendarTick(save, cc.year, cc.cursor_doy, save.prog.frontier_phase, mnw, &ratios);
      // And when the period closes, the offers you did NOT take get picked up by someone else. It runs AFTER the
      // pulse on purpose: the world resolves and distributes first, and only what was left on the table un-
      // signed gets closed. The other way around would close offers the pulse itself had just created.
      career::CloseExpiredWindowOffers(save, cc, cc.year, cc.cursor_doy, mnw, career::kStartYear); }

    // 2) regenerate offers with the new cursor (same input as the market UI).
    market::OfferGenInput in;
    in.rep            = save.prog.rep;
    in.frontier_phase = save.prog.frontier_phase;
    in.unlocked_gates = &save.unlocked_gates;
    in.year           = cc.year;
    in.cursor_doy     = cc.cursor_doy;
    in.rating         = save.prog.rating;   // Forward the real rating (eligibility = the rating scale)
    in.form           = CareerForm(save.prog);   // The ONE CareerForm value (the why-bar and the soft term share it)
    in.rating_races   = save.prog.rating_races;  // Provisional-spike discount input
    in.credits        = save.prog.credits;       // DORMANT signability gate input (afford)
    in.career_seed    = save.career_seed;   // Thread the career seed (the scarcity system consumes it)
    in.cleanliness    = save.prog.cleanliness;   // Real cleanliness EMA -> the gray-zone trial trigger
    in.world_year     = (save.current_world_year != 0) ? save.current_world_year : cc.year;  // Incumbent age curve
    // money_norm = the player's pay-driver budget, 0..1 (the same clamp as the POST-RACE pulse,
    // career_overlay) -> pay-driver seats (the Latifi kind) are evaluated the same way in both pulses.
    { double mn = (double)save.prog.credits / bal::MK_PAYDRIVER_MONEY_REF;
      in.money_norm = mn < 0.0 ? 0.0 : (mn > 1.0 ? 1.0 : mn); }
    // Unified with the POST-RACE pulse (career_overlay) -- both now use the same
    // living-world GenerateOffers(in,&save). The world arrives already SEEDED at this point (the always-open-seat mechanism +
    // the signing-window churn both anchor seat_occupant before this pulse runs), so the
    // legacy disconnect that motivated the original note no longer applies: the WEEK-ADVANCE board stays
    // consistent with the live market (same team labels, same money_norm as the post-race pulse).
    std::vector<market::GenOffer> live = market::GenerateOffers(in, &save);

    // 2.5) offer_lifecycle: stamps the firstSeen of every LIVE offer in this pulse and expires
    //      (28-day TTL, bal::MK_OFFER_TTL_DAYS) whichever have been visible too long. BEFORE the detection of
    //      expired offers/mail emission below, so a freshly-expired offer doesn't get re-announced in this
    //      same pulse. absToday = the real Gregorian abs_day (AbsDayFromYearDoy; career_dateutil's scale, NOT year*365).
    // Only NON-held offers get stamped. A held offer (blocked by XP/rep) is a persistent goal
    // meant to stay visible "further down" indefinitely -- if it were stamped here, it would expire after 28 days and
    // vanish from the board even though the player never got a chance to accept it. The 4-week window only starts
    // once the offer becomes actionable (stops being held) and this loop stamps it for the first time.
    const int absToday = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);
    for (const auto& go : live) if (!go.held) MarkOfferSeen(save, go.def.id, absToday);  // held = a persistent goal, it doesn't expire; the TTL starts once it becomes actionable
    // 18/09: what the market offers TODAY, so the "last day" warning does not count offers that left.
    save.live_offer_ids.clear();
    for (const auto& go : live) if (!go.held) save.live_offer_ids.insert(go.def.id);
    save.live_offers_day = absToday;
    LapseExpiredOffers(save, absToday);
    career::NegoTick(save, cc, live, absToday, res);   // the negotiation thread's pulse (2026-07-20 spec)

    // 3) detect EXPIRED offers: a series that GenerateOffers no longer emits because it's fallen OUTSIDE the time window.
    //    MUST use the SAME season-relative window as GenerateOffers: if this used
    //    the old absolute cutoff while GenerateOffers used the rolling window, offers would flicker between the
    //    Market and the expired_offers set (a seat "comes back" into the window but would already be marked expired).
    //    id = "of_"+series_id (same as GenerateOffers). We only register new ones (it doesn't re-fire if it was already there).
    //    CalendarForSeriesInYear(sd.id, cc.year) -- the opener PROJECTED onto the current year, not the raw one (kStartYear
    //    is the identity year; it diverges from season 2 onward).
    for (const auto& sd : AllSeries()) {
        if (IsEventOnlySeries(sd)) continue;             // 01/10: never offered, nothing to expire
        std::vector<CalRound> cal = CalendarForSeriesInYear(sd.id, cc.year);
        int round1doy = 0;
        for (const auto& c : cal) if (c.round == 1) { round1doy = c.doy; break; }
        int delta = round1doy - cc.cursor_doy;            // days until the opener (mirrors GenerateOffers)
        if (delta < 0) delta += 365;                      // year wraparound (identical to GenerateOffers)
        bool inWindow = (delta <= bal::MK_OFFER_HORIZON_DOY);
        if (round1doy > 0 && cc.cursor_doy > 0 && !inWindow) {   // outside the window -> no longer offered -> expired
            std::string oid = "of_" + sd.id;
            if (save.expired_offers.insert(oid).second)   // .second=true iff NEW -> avoids re-notifying
                res.expired_offer_ids.push_back(oid);
        }
    }

    // 4) record the courtship trail for live held offers (a persistent "they remember you" memory).
    RecordCourting(save, live, cc.year);

    // 5) AdvanceWeek's pulse for the mail generator. Emits OFFER mails (new direct offers) plus any
    //    band/last-season milestone that applies given the current state; persists into save.emails and returns
    //    the new ids via res.generated_mail_ids (the UI drains/notifies from there). Deterministic, no RNG or timer.
    EmailGenContext ctx;
    ctx.year     = cc.year;
    ctx.day      = cc.cursor_doy;
    ctx.rep_band = RepDisplayBand(save.prog.rep);
    for (const auto& c : save.contracts)                 // your own contract in its LAST season -> a milestone
        if (c.active && c.seasons_left == 1) { ctx.final_series = c.series_id; break; }
    // Living mail: params available at this pulse (with no race result -> race_pos/track
    // stay at 0/"" -> those placeholders get stripped out during interpolation, so nothing breaks).
    ctx.player  = career::MailFirstName(playerName);
    ctx.credits = save.prog.credits;
    for (const auto& c : save.contracts)
        if (c.active && c.seasons_left == 1) { ctx.team_display = c.team_id; break; }
    // The next actionable event -- ONLY this pulse (AdvanceWeek/AdvanceDays) fills it in; the
    // post-Settle site (career_overlay.cpp) leaves it at its default (days_away=-1) on purpose (the rule: a
    // same-day settle shouldn't warn about finances/circuit history).
    ctx.next_event = NextEventLookahead(save, cc, save.prog.frontier_phase);
    // THE THIRD GATE ON THE MARKET'S SCHEDULE, which had been left unlocked. The board
    // (MarketUiOffers) and the POST-RACE pulse (career_overlay) no longer offer anything outside the
    // signing window, but this pulse -- the one that ADVANCES THE CLOCK -- kept emitting offer mails all
    // twelve months of the year. Measured on a real save: nine offers stamped on day 53 (February 22) of 2027,
    // with the market closed since January. That leaves the player with mailed offers that the Market doesn't show
    // and that MarketUiAccept will reject on schedule grounds -- and it poisons expiry along the way, because a mail that's
    // born outside the window has no window it belongs to.
    // Only MAIL EMISSION is cut off. `live` is still used as-is for the TTL, the negotiation-thread pulse,
    // and the courtship trail: clearing it entirely would close threads for "the seat's gone" that are still
    // alive, which is a bigger, different kind of damage than the one being fixed here.
    const bool mercadoAbierto =
        market::MarketPhaseFor(cc.year, cc.cursor_doy, kStartYear) != market::MARKET_PADDOCK;
    const std::vector<market::GenOffer> liveParaCorreo =
        mercadoAbierto ? live : std::vector<market::GenOffer>{};
    std::vector<market::EmailDef> newMail = GenerateCareerEmails(save, liveParaCorreo, ctx);
    for (const auto& m : newMail) res.generated_mail_ids.push_back(m.id);

    // 6) A deterministic weekly draw for a ONE-OFF SUBSTITUTE -- after the mail
    //    generator (same gates: no active/offered substitute, no unanswered family-7 invitation). Emits its
    //    own mail directly into save.emails (the mailExists/emitMail idiom); the new id (if any) gets
    //    threaded into res.generated_mail_ids via outMailIds, the same as newMail above.
    SubRideWeeklyDraw(save, cc, playerName, &res.generated_mail_ids);

    // 7) 18/09 (Josh): "when the offers are about to expire, if they have not been explicitly rejected, the
    //    notification must be shown again". On the eve of an offer ceasing to be signable, its mail
    //    goes back to unread (the mailbox puts it on top with the last-day mark). After step 5, which
    //    is the one that closes the mails of offers that have gone. Only with the day advanced: a re-pulse of the
    //    same day must not mark as unread again a mail you already opened today.
    if (advanced) ReopenOfferMailsOnLastDay(save, absToday);

    return res;
}

// adds the two vectors of a pulse into the accumulator; defined next to the clock with stops, further below
static void c1Append(WeekTickResult& acc, const WeekTickResult& t);

// 18/09 — advances `days` days WITH ONE PULSE PER DAY, and returns the sum of the pulses.
//
// Before, these two paths moved the cursor in one go and gave ONE pulse at the end. For a one-week jump it
// was barely noticeable; for a long one it was a real failure: `AdvanceToDate` swallowed a hundred days in one bite and
// pulsed once, so a signing window that fell ENTIRELY inside the jump was never sealed —
// the world skipped its own silly season without saying anything. Each day that passes now gets its pulse, just
// like in the clock with stops. AdvanceWeek, AdvanceToDate and the simulator use it.
WeekTickResult AdvanceDaysPulsed(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                                 const std::string& playerName, int days) {
    WeekTickResult acc;
    if (days <= 0) return RunWorldTick(cc, prog, save, playerName, false);
    for (int i = 0; i < days; i++) {
        AdvanceDays(cc, prog, save, playerName, 1);
        c1Append(acc, RunWorldTick(cc, prog, save, playerName, true));
    }
    return acc;
}

WeekTickResult AdvanceWeek(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                           const std::string& playerName, int weeks) {
    // will retire this wrapper (and the "+7"); the pulse inside is already daily.
    return AdvanceDaysPulsed(cc, prog, save, playerName, weeks > 0 ? 7 * weeks : 0);
}

// Real-time model: the abs_day of the NEXT actionable round ahead of the cursor (the same filter
// as WeeksToNextEvent: valid, not raced/dnf/locked, and either enrolled or open going forward). It lands at the START
// of the weekend (event_doy - the series' footprint.lead), not on race day. If none remain this
// year, it RE-PROJECTS next year's calendar (dates shift per year, via RealignDoy) and takes the
// earliest actionable opener from that projection -- it does not reuse this year's doys (a bug fixed here).
// bug reported (05/08, "next event"): landing an event for schedule jumps. It lands
// at the START of the weekend (race day minus the footprint's lead, by design), but CLAMPED: never before
// 'floorAbsDay' (the clock never runs backward), and never past race day itself.
//
// Without the clamp, an event less than 'lead' days from the cursor would return a target <= today, and AdvanceToDate
// (days <= 0 -> no advance) left the "next event" button as a silent NO-OP: the player clicks it and the clock
// doesn't move, with no explanation. Measured on the production calendar: it happened on 281 of the 351 days of the year
// that had an event ahead, and on 138 of them the target landed BEFORE today (a major-formula event tomorrow -> lead 2 -> yesterday).
// It's the same footprint arithmetic as the next-day DNF bug (SlotOccupiedDays in career_calendar.cpp),
// just seen from the clock's side instead of the clash detector's.
//
// The lower bound is ALWAYS reachable: all three selectors require doy > cursor_doy (or a later year),
// so race day is never earlier than today+1 and the jump always advances at least one day. If the weekend had already
// started, the correct target is race day itself: you're already inside the weekend, all that's left is to race.
static int landEventAbsDay(int raceAbsDay, int lead, int floorAbsDay) {
    int landed = raceAbsDay - lead;
    if (landed < floorAbsDay) landed = floorAbsDay;    if (landed > raceAbsDay)  landed = raceAbsDay;
    return landed;
}

// the jump can only take you to an event you could actually RACE. A CLOSED-grid series
// (SeriesDef.closed) with no ACTIVE contract passes every slot flag -- it's not locked by
// phase or gate -- but the calendar's launch flow rejects it (CanEnterSeries), so "Next available"
// used to land on unreachable events. It uses the SAME predicate as the launch flow; save==nullptr = fail-open (projections and
// callers with no save keep the previous behavior). Already enrolled always passes: enrolling already required access.
static bool SlotJumpEnterable(const SaveData* save, const CalSlot& s) {
    if (s.enrolled || !save) return true;
    return CanEnterSeries(*save, s.series_id);
}

int NextEventAbsDay(const CareerCalendar& cc, int frontierPhase, const std::set<std::string>* gates,
                    const SaveData* save) {
    const std::set<std::string> contracted = save ? ContractedSeries(*save) : std::set<std::string>();   // 01/10 jse67
    std::vector<CalSlot> slots = BuildMasterCalendar(cc, frontierPhase, gates, nullptr, &contracted);
    const int NONE = 0x7fffffff;
    int bestThisYear = NONE;
    std::string bestSid;
    for (const auto& s : slots) {
        if (!s.valid || s.locked) continue;
        if (s.raced || s.dnf) continue;
        if (!(s.enrolled || !s.past)) continue;
        if (!SlotJumpEnterable(save, s)) continue;   //
        if (s.doy > cc.cursor_doy && s.doy < bestThisYear) { bestThisYear = s.doy; bestSid = s.series_id; }
    }
    const int today = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);
    // THE DECISION WEEK IS A SCHEDULE EVENT, and it was the one this jump didn't know about. It's the day
    // the window's seats get handed out, so for a seatless driver it's THE date of the year.
    // Without this the career would close itself out: with no contract there are no pending races left, so the jump fell
    // through to the year+1 fallback and landed on January's opener, FLYING PAST December 15, with the
    // window's seats never handed out. The player was left without a seat and no way to get one, and the next year
    // the same thing happened. Reported 09/08 in 2031 ("I tried advancing to December and I have no offers at all").
    // The markers come from MarketMarksForYear, the SAME source that draws them on the calendar, so the jump
    // can never fall out of sync with what the player sees, and year 1 (which has TWO decision weeks) comes for free.
    int decisionDoy = 0;
    for (const auto& mk : market::MarketMarksForYear(cc.year, career::kStartYear)) {
        if (mk.kind != market::MARK_DECISION_WEEK) continue;
        if (mk.doy <= cc.cursor_doy) continue;                        // already happened
        if (decisionDoy == 0 || mk.doy < decisionDoy) decisionDoy = mk.doy;
    }
    // LANDING days are compared, not race days: a race lands `lead` days earlier (you arrive
    // on Thursday for a Sunday race), but the decision week doesn't -- it's a date you have to be present for, not a weekend you
    // travel to ahead of time. Comparing the raw doys would pick wrong whenever the two fall close together.
    const int raceAbs = (bestThisYear != NONE)
        ? landEventAbsDay(career::dateutil::AbsDayFromYearDoy(cc.year, bestThisYear),
                          FootprintForSeries(bestSid).lead, today + 1)
        : -1;
    const int decisionAbs = (decisionDoy > 0)
        ? career::dateutil::AbsDayFromYearDoy(cc.year, decisionDoy) : -1;
    if (decisionAbs > 0 && (raceAbs < 0 || decisionAbs < raceAbs)) return decisionAbs;
    if (raceAbs > 0) return raceAbs;
    // year+1 fallback: re-projects next year's calendar (dates VARY per year) and takes the
    // earliest actionable (not locked) opener.
    CareerCalendar nextCc = cc; nextCc.year = cc.year + 1; nextCc.cursor_doy = 0;
    std::vector<CalSlot> nextSlots = BuildMasterCalendar(nextCc, frontierPhase, gates, nullptr, &contracted);
    int bestNext = NONE; std::string bestNextSid;
    for (const auto& s : nextSlots) {
        if (!s.valid || s.locked) continue;
        if (!SlotJumpEnterable(save, s)) continue;   // (also in the year+1 projection)
        if (s.doy > 0 && s.doy < bestNext) { bestNext = s.doy; bestNextSid = s.series_id; }
    }
    if (bestNext != NONE) {
        int lead = FootprintForSeries(bestNextSid).lead;
        return landEventAbsDay(career::dateutil::AbsDayFromYearDoy(cc.year + 1, bestNext), lead, today + 1);
    }
    return -1;
}

// Time-skip helper: the EXACT SAME filter as NextEventAbsDay (above), filtered down to ONE specific series.
// Any filter change in NextEventAbsDay must be mirrored here (the same warning NextEventLookahead already leaves
// further below for its own copy of the filter).
int NextEventAbsDayForSeries(const CareerCalendar& cc, const std::string& seriesId, int frontierPhase,
                             const std::set<std::string>* gates, const SaveData* save) {
    const std::set<std::string> contracted = save ? ContractedSeries(*save) : std::set<std::string>();   // 01/10 jse67
    std::vector<CalSlot> slots = BuildMasterCalendar(cc, frontierPhase, gates, nullptr, &contracted);
    const int NONE = 0x7fffffff;
    int bestThisYear = NONE;
    for (const auto& s : slots) {
        if (s.series_id != seriesId) continue;
        if (!s.valid || s.locked) continue;
        if (s.raced || s.dnf) continue;
        if (!(s.enrolled || !s.past)) continue;
        if (!SlotJumpEnterable(save, s)) continue;   //
        if (s.doy > cc.cursor_doy && s.doy < bestThisYear) bestThisYear = s.doy;
    }
    const int today = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);
    if (bestThisYear != NONE) {
        int lead = FootprintForSeries(seriesId).lead;
        return landEventAbsDay(career::dateutil::AbsDayFromYearDoy(cc.year, bestThisYear), lead, today + 1);
    }
    // year+1 fallback: the identical pattern to NextEventAbsDay (re-projects next year's calendar),
    // but filtered to the same series.
    CareerCalendar nextCc = cc; nextCc.year = cc.year + 1; nextCc.cursor_doy = 0;
    std::vector<CalSlot> nextSlots = BuildMasterCalendar(nextCc, frontierPhase, gates, nullptr, &contracted);
    int bestNext = NONE;
    for (const auto& s : nextSlots) {
        if (s.series_id != seriesId) continue;
        if (!s.valid || s.locked) continue;
        if (!SlotJumpEnterable(save, s)) continue;   // (also in the year+1 projection)
        if (s.doy > 0 && s.doy < bestNext) bestNext = s.doy;
    }
    if (bestNext != NONE) {
        int lead = FootprintForSeries(seriesId).lead;
        return landEventAbsDay(career::dateutil::AbsDayFromYearDoy(cc.year + 1, bestNext), lead, today + 1);
    }
    return -1;
}

// Time-skip helper: a variant for a date picker. The SAME slot filter as NextEventAbsDay
// (valid/locked/raced/dnf/enrolled-or-not-past), but the SELECTION criterion is the LANDED abs_day
// (doy - footprint.lead) compared against targetAbsDay -- NOT the raw doy compared against cursor_doy. The lead varies
// by series, so EVERY candidate has to be landed before comparing/sorting (a larger raw doy can
// land EARLIER than a smaller one if its lead is bigger); you can't sort by raw doy and only land at the
// end the way NextEventAbsDay does. Any filter change in NextEventAbsDay must be mirrored here too.
//
// BUGFIX (in-game: "I pick a date in the selector and it says there's no race to jump to"): the
// UI's <input type="date"> (Calendar.tsx) does NOT impose a maximum -- the player can pick ANY date.
// The fallback used to ONLY re-project cc.year+1 (a single year of margin); with a targetAbsDay beyond that year
// no candidate would land >= targetAbsDay and the function returned -1 (the UI showed "no race available").
// Fix: after exhausting cc.year+1, it keeps re-projecting year by year (the same loose fallback filter: valid/!locked/
// doy>0, with no history to check in a projection) until it finds the first candidate or exhausts
// kMaxYearsForward years. It starts at max(cc.year+1, the year containing targetAbsDay): if the target already falls in a
// year far ahead, there's no need to re-check the years in between (none of them can land >= a target from
// a later year: AbsDayFromYearDoy(y, doy) < AbsDayFromYearDoy(y+1, 1) for any doy in y).
int NextEventAbsDayOnOrAfter(const CareerCalendar& cc, int targetAbsDay, int frontierPhase,
                             const std::set<std::string>* gates, const SaveData* save) {
    const std::set<std::string> contracted = save ? ContractedSeries(*save) : std::set<std::string>();   // 01/10 jse67
    std::vector<CalSlot> slots = BuildMasterCalendar(cc, frontierPhase, gates, nullptr, &contracted);
    const int NONE = 0x7fffffff;
    int best = NONE;
    for (const auto& s : slots) {
        if (!s.valid || s.locked) continue;
        if (s.raced || s.dnf) continue;
        if (!(s.enrolled || !s.past)) continue;
        if (!SlotJumpEnterable(save, s)) continue;   //
        int lead = FootprintForSeries(s.series_id).lead;
        int landed = career::dateutil::AbsDayFromYearDoy(cc.year, s.doy) - lead;
        if (landed >= targetAbsDay && landed < best) best = landed;
    }
    if (best != NONE) return best;

    // multi-year fallback: re-projects year by year (the identical loose filter as the old year+1 fallback: valid/
    // !locked/doy>0), landing each candidate against the same targetAbsDay, until it finds the first one or
    // hits the cap. kMaxYearsForward = 8: generous enough for any date the UI's <input type="date">
    // picker could offer in a real career (a career in this game lasts, in practice, only a
    // few years of live clock time), without leaving the loop unbounded against a corrupt/absurd targetAbsDay.
    const int kMaxYearsForward = 8;
    int targetYear = 0, targetDoyUnused = 0;
    career::dateutil::YearDoyFromAbsDay(targetAbsDay, targetYear, targetDoyUnused);
    int fromYear = (targetYear > cc.year + 1) ? targetYear : (cc.year + 1);
    for (int y = fromYear; y <= cc.year + kMaxYearsForward; ++y) {
        CareerCalendar yearCc = cc; yearCc.year = y; yearCc.cursor_doy = 0;
        std::vector<CalSlot> ySlots = BuildMasterCalendar(yearCc, frontierPhase, gates, nullptr, &contracted);
        int bestY = NONE;
        for (const auto& s : ySlots) {
            if (!s.valid || s.locked) continue;
            if (s.doy <= 0) continue;
            if (!SlotJumpEnterable(save, s)) continue;   // (also in the multi-year projection)
            int lead = FootprintForSeries(s.series_id).lead;
            int landed = career::dateutil::AbsDayFromYearDoy(y, s.doy) - lead;
            if (landed >= targetAbsDay && landed < bestY) bestY = landed;
        }
        if (bestY != NONE) return bestY;
    }
    return -1;
}

// A capacity variant of NextEventAbsDay that only counts
// series the player is ENROLLED in (s.enrolled) -- it's used to tell apart "next
// AVAILABLE event" (NextEventAbsDay, any open series) from "next event you're enrolled in". The SAME
// landing logic and the SAME year+1 fallback as NextEventAbsDay (above); the only change: "s.enrolled" gets added to the
// filter (in both years, this-year and next-year -- a series you're not enrolled in can never be the target).
// with NO enrollment at all -> -1, it does NOT fall back to NextEventAbsDay: such a fallback would blur exactly the distinction
// this function exists to keep apart ("available" vs. "enrolled"), and -1 is already the "no target" value used by
// NextEventAbsDayForSeries/NextEventAbsDayOnOrAfter for their own "nothing to offer" case (the same convention across
// the whole family).
int NextEventAbsDayEnrolled(const CareerCalendar& cc, int frontierPhase, const std::set<std::string>* gates) {
    std::vector<CalSlot> slots = BuildMasterCalendar(cc, frontierPhase, gates, nullptr);
    const int NONE = 0x7fffffff;
    int bestThisYear = NONE;
    std::string bestSid;
    for (const auto& s : slots) {
        if (!s.enrolled) continue;
        if (!s.valid || s.locked) continue;
        if (s.raced || s.dnf) continue;
        if (s.doy > cc.cursor_doy && s.doy < bestThisYear) { bestThisYear = s.doy; bestSid = s.series_id; }
    }
    const int today = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);
    if (bestThisYear != NONE) {
        int lead = FootprintForSeries(bestSid).lead;
        return landEventAbsDay(career::dateutil::AbsDayFromYearDoy(cc.year, bestThisYear), lead, today + 1);
    }
    // year+1 fallback: the identical pattern to NextEventAbsDay (re-projects year+1 with cursor=0); cc.enrollments
    // travels intact into the copy (nextCc = cc), so "enrolled" still flags the SAME series you're enrolled in
    // in next year's projection (the real season rollover, if it clears season_state, happens somewhere
    // else in the engine -- not here).
    CareerCalendar nextCc = cc; nextCc.year = cc.year + 1; nextCc.cursor_doy = 0;
    std::vector<CalSlot> nextSlots = BuildMasterCalendar(nextCc, frontierPhase, gates, nullptr);
    int bestNext = NONE; std::string bestNextSid;
    for (const auto& s : nextSlots) {
        if (!s.enrolled) continue;
        if (!s.valid || s.locked) continue;
        if (s.doy > 0 && s.doy < bestNext) { bestNext = s.doy; bestNextSid = s.series_id; }
    }
    if (bestNext != NONE) {
        int lead = FootprintForSeries(bestNextSid).lead;
        return landEventAbsDay(career::dateutil::AbsDayFromYearDoy(cc.year + 1, bestNext), lead, today + 1);
    }
    return -1;
}

// The minimum guaranteed outlay to race a given slot -- the SAME semantics as Prerace/settle
// (screen_vehicle.cpp: InscriptionDue+EntryFeeFor for the series + RaceEntryOutlay). 'cc' is the calendar whose
// enrollments InscriptionDue queries (this year's, or the year+1 fallback projection). A series with no row in
// the catalog (class_name doesn't resolve) -> 0, fail-safe (doesn't trigger noise). rentPct uses the reference
// percentage (bal::RENT_PCT) -- there's no SPECIFIC car chosen at this lookahead stage, only "the cheapest in the class"
// (the same fallback career_simrunner.cpp uses for AI/sim with no car selected).
static long NextEventMinOutlay(const SaveData& save, const CareerCalendar& cc, const CalSlot& slot) {
    const SeriesDef* sd = SeriesById(slot.series_id);
    if (!sd) return 0;
    long inscDue  = InscriptionDue(cc, slot.series_id);
    long entryFee = EntryFeeFor(slot.series_id);
    long carValue = CheapestCarPriceForClass(sd->class_name);
    // "The SAME semantics as Prerace/settle" calls for the ECONOMIC value -- the cheapest car
    // in reality can be a collector's item (era classes: the cheapest F-Retro car at 1.85M against a 260k baseline), and the
    // affordability estimate was throwing false warnings. It's clamped to the class band instead, like the runValue that actually gets charged.
    const SeriesEcon* ecn = FindEconByClassName(sd->class_name);
    if (ecn && ecn->comprar > 0) carValue = CompressPriceToClassBand(carValue, ecn->comprar);
    int  rentPct  = (int)(bal::RENT_PCT * 100.0 + 0.5);
    return OwnsCarOfClass(save, sd->class_name)
        ? RaceEntryOutlay(EntryAction::OwnCar, inscDue, entryFee, carValue, rentPct)
        : RaceEntryOutlay(EntryAction::Rent,   inscDue, entryFee, carValue, rentPct);
}

// The EXACT SAME filter as NextEventAbsDay (above) to pick the slot, but keeping the
// full slot (not just its series_id) so it can expose track_name/track_hash + the minimum outlay. Any
// filter change in NextEventAbsDay must be mirrored here (or the two would disagree about what "the next
// event" even is).
NextEventInfo NextEventLookahead(const SaveData& save, const CareerCalendar& cc, int frontierPhase) {
    NextEventInfo info;
    const std::set<std::string>* gates = &save.unlocked_gates;
    const int NONE = 0x7fffffff;
    int todayAbsDay = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);

    const std::set<std::string> contracted = ContractedSeries(save);   // 01/10 jse67: the contract lifts the lock
    std::vector<CalSlot> slots = BuildMasterCalendar(cc, frontierPhase, gates, nullptr, &contracted);
    int bestDoy = NONE; int bestIdx = -1;
    for (size_t i = 0; i < slots.size(); i++) {
        const CalSlot& s = slots[i];
        if (!s.valid || s.locked) continue;
        if (s.raced || s.dnf) continue;
        if (!(s.enrolled || !s.past)) continue;
        if (!SlotJumpEnterable(&save, s)) continue;   // the schedule doesn't announce events you can't actually race
        if (s.doy > cc.cursor_doy && s.doy < bestDoy) { bestDoy = s.doy; bestIdx = (int)i; }
    }
    if (bestIdx >= 0) {
        const CalSlot& best = slots[bestIdx];
        int lead = FootprintForSeries(best.series_id).lead;
        int eventAbsDay = career::dateutil::AbsDayFromYearDoy(cc.year, bestDoy) - lead;
        info.days_away  = eventAbsDay - todayAbsDay;
        info.track      = best.track_name;
        info.track_hash = best.track_hash;
        info.min_outlay = NextEventMinOutlay(save, cc, best);
        return info;
    }

    // year+1 fallback (identical to NextEventAbsDay): re-projects next year's calendar and takes the
    // earliest actionable (not locked) opener.
    CareerCalendar nextCc = cc; nextCc.year = cc.year + 1; nextCc.cursor_doy = 0;
    std::vector<CalSlot> nextSlots = BuildMasterCalendar(nextCc, frontierPhase, gates, nullptr, &contracted);
    int bestNextDoy = NONE; int bestNextIdx = -1;
    for (size_t i = 0; i < nextSlots.size(); i++) {
        const CalSlot& s = nextSlots[i];
        if (!s.valid || s.locked) continue;
        if (!SlotJumpEnterable(&save, s)) continue;   // (also in the year+1 projection)
        if (s.doy > 0 && s.doy < bestNextDoy) { bestNextDoy = s.doy; bestNextIdx = (int)i; }
    }
    if (bestNextIdx >= 0) {
        const CalSlot& best = nextSlots[bestNextIdx];
        int lead = FootprintForSeries(best.series_id).lead;
        int eventAbsDay = career::dateutil::AbsDayFromYearDoy(cc.year + 1, bestNextDoy) - lead;
        info.days_away  = eventAbsDay - todayAbsDay;
        info.track      = best.track_name;
        info.track_hash = best.track_hash;
        info.min_outlay = NextEventMinOutlay(save, nextCc, best);
    }
    return info;   // with no actionable event (not even in year+1) -> days_away stays at -1 (default)
}

// Real temporal model advances the clock to a target abs_day, with one pulse per day traveled
// (see AdvanceDaysPulsed). Target in the past/today -> does not go back (only re-pulse, advanced=false).
WeekTickResult AdvanceToDate(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                             const std::string& playerName, int targetAbsDay) {
    int today = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);
    return AdvanceDaysPulsed(cc, prog, save, playerName, targetAbsDay - today);
}

// Real-time model: advances to the schedule's next event (NextEventAbsDay with
// the save's frontier/gates). No pending event -> just a re-pulse.
WeekTickResult AdvanceToNextEvent(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                                  const std::string& playerName) {
    int target = NextEventAbsDay(cc, save.prog.frontier_phase, &save.unlocked_gates, &save);   //
    if (target < 0) return RunWorldTick(cc, prog, save, playerName, false);
    return AdvanceToDate(cc, prog, save, playerName, target);
}

// ============================ C1 · Clock with stops (spec ) ============================

const char* DayEventKindName(DayEventKind k) {
    static const char* N[kDayEventKinds] = { "race_mine", "race_other", "mail", "offer", "offer_expired", "relation",
                                             "decision", "rollover", "year_end", "cap", "until", "noshow",
                                             "seats_last_day", "seats_lost", "offer_last_day" };
    return (k >= 0 && k < kDayEventKinds) ? N[k] : "unknown";
}
static std::string c1SeatKey(const WindowSeat& ws) { return ws.series_id + '\x1F' + ws.team_label; }
// the offered seat you already signed is not lost: same criterion as CloseExpiredWindowOffers
static bool c1SeatSigned(const SaveData& save, const WindowSeat& ws) {
    for (const auto& c : save.contracts)
        if (c.active && c.series_id == ws.series_id && c.team_id == ws.team_label) return true;
    return false;
}
void DayEventParam(std::string& params, const char* key, const std::string& value) {
    if (!params.empty()) params += '\x1F';
    params += key; params += '='; params += value;
}
DaySnapshot SnapshotDay(const SaveData& save, const CareerCalendar& cc) {
    DaySnapshot s;
    s.expired_ids = save.expired_offers; s.lapsed_ids = save.lapsed_offers;
    s.rel_marks = save.rel_marks.size();
    // by IDENTITY, not by size: GenerateCareerEmails PRUNES at the end, so a day that delivers one and prunes
    // another leaves the counter pinned and the day's mail was lost. With ids, deliver-and-prune counts.
    for (const auto& m : save.emails) s.email_ids.insert(m.id);
    for (const auto& o : save.pending_renewals) s.renewal_ids.insert(o.id);
    for (const auto& kv : save.offer_first_seen) s.offers_seen.insert(kv.first);
    for (const auto& ws : save.window_seats)
        if (ws.state == WSEAT_OFFERED) { s.offered_seats++; s.offered_seat_keys.insert(c1SeatKey(ws)); }
    for (const auto& e : cc.enrollments) s.history_len[e.series_id] = e.history.size();
    return s;
}
static DayEvent makeEvent(int absDay, DayEventKind k, bool stops, const std::string& sid = std::string()) {
    DayEvent e; e.abs_day = absDay; e.kind = k; e.stops = stops; e.series_id = sid;
    e.label_key = std::string("time.ev.") + DayEventKindName(k);
    return e;
}
// C1 (final round): a series' footprint, memoized in the context. FootprintForSeries rescans
// CalendarForSeries (1348 rows, std::string comparison per row) every time it is called; the daily
// loop asked for it per slot and per enrollment, 365 times per season. Here it is paid once per series.
static int ctxLead(const DayContext& ctx, const std::string& seriesId) {
    auto it = ctx.lead.find(seriesId);
    if (it != ctx.lead.end()) return it->second;
    const int lead = FootprintForSeries(seriesId).lead;
    ctx.lead.emplace(seriesId, lead);
    return lead;
}
DayContext BuildDayContext(const CareerCalendar& cc, const SaveData& save) {
    DayContext ctx;
    const std::set<std::string> contracted = ContractedSeries(save);   // 01/10 jse67
    ctx.slots = BuildMasterCalendar(cc, save.prog.frontier_phase, &save.unlocked_gates, nullptr, &contracted);
    ctx.landing.assign(ctx.slots.size(), -1);
    for (size_t i = 0; i < ctx.slots.size(); i++) {
        const CalSlot& s = ctx.slots[i];
        const int lead = ctxLead(ctx, s.series_id);   // also for filtered slots: their series may be enrolled
        if (!s.valid || s.locked || s.enrolled || s.raced || s.dnf || s.past) continue;   // same filter as NextEventAbsDay
        ctx.landing[i] = career::dateutil::AbsDayFromYearDoy(cc.year, s.doy) - lead;
    }
    return ctx;
}
std::vector<DayEvent> StopReasons(const DaySnapshot& before, const SaveData& save, const CareerCalendar& cc,
                                  const DayContext& ctx, int absDay, const StopRules& rules) {
    std::vector<DayEvent> out;
    // 1) MY races: landing (doy - lead) or race day, if the round is still pending
    for (const auto& e : cc.enrollments) {
        CalSlot p = PendingSlot(cc, e.series_id);
        if (!p.valid || p.raced || p.dnf) continue;
        const int raceAbs = career::dateutil::AbsDayFromYearDoy(cc.year, p.doy);
        const int landed  = raceAbs - ctxLead(ctx, e.series_id);
        if (absDay != landed && absDay != raceAbs) continue;
        DayEvent ev = makeEvent(absDay, DEK_RACE_MINE, rules.myRaces, e.series_id);
        const SeriesDef* sd = SeriesById(e.series_id);
        DayEventParam(ev.params, "series", sd ? sd->display : e.series_id);
        DayEventParam(ev.params, "track", p.track_name);
        DayEventParam(ev.params, "round", std::to_string(p.round));
        out.push_back(ev);
    }
    // 2) OTHER enterable races (the filter is already resolved in ctx.landing: compare integers), landing
    //    today. SlotJumpEnterable (CanEnterSeries) is only asked for the FOUR or FIVE slots that fall today,
    //    not for the ~1300 of the whole year, and only when the day has any.
    for (size_t i = 0; i < ctx.slots.size(); i++) {
        if (ctx.landing[i] != absDay) continue;
        const CalSlot& s = ctx.slots[i];
        if (!SlotJumpEnterable(&save, s)) continue;
        DayEvent ev = makeEvent(absDay, DEK_RACE_OTHER, rules.anyRace, s.series_id);
        const SeriesDef* sd = SeriesById(s.series_id);
        DayEventParam(ev.params, "series", sd ? sd->display : s.series_id);
        DayEventParam(ev.params, "track", s.track_name);
        out.push_back(ev);
    }
    // 3) mails arrived: NEW ids (not the size — the prune of GenerateCareerEmails swallowed the event)
    { int n = 0;
      for (const auto& m : save.emails) if (!before.email_ids.count(m.id)) n++;
      if (n > 0) { DayEvent ev = makeEvent(absDay, DEK_MAIL, rules.mail); DayEventParam(ev.params, "n", std::to_string(n)); out.push_back(ev); } }
    // 4) new offers: new stamps in offer_first_seen, new OFFERED seats, new renewals (by id)
    // Review (finding 2): the stamps only count with the signing market open. RunWorldTick stamps the
    // live offers all twelve months (the 28-day deadline has to run), but outside the window the Market
    // is empty (MarketSigningOpen) and no mail announces them (`mercadoAbierto`, same door): the clock
    // stopped in April for offers that the player can neither see nor sign. Renewals and offered seats do not
    // depend on the window and are always counted.
    const bool fichajesAbiertos =
        market::MarketPhaseFor(cc.year, cc.cursor_doy, kStartYear) != market::MARKET_PADDOCK;
    { int n = 0;
      if (fichajesAbiertos)
          for (const auto& kv : save.offer_first_seen) if (!before.offers_seen.count(kv.first)) n++;
      size_t offered = 0; for (const auto& ws : save.window_seats) if (ws.state == WSEAT_OFFERED) offered++;
      if (offered > before.offered_seats) n += (int)(offered - before.offered_seats);
      for (const auto& o : save.pending_renewals) if (!before.renewal_ids.count(o.id)) n++;
      if (n > 0) { DayEvent ev = makeEvent(absDay, DEK_OFFER, rules.offers); DayEventParam(ev.params, "n", std::to_string(n)); out.push_back(ev); } }
    // 5) expired (by window or by TTL): they are listed in the feed but NEVER stop the clock. The spec
    // lists as reasons of the "offers" rule the formal offer, the reply, the renewal verdict,
    // the window opening/closing and the invitation — "an offer or a reply arrives"; expiry is not
    // any of those things (the player answered nothing), so stops is fixed to false, not rules.offers.
    // Review (finding 4b): only those the player had in front of them. RunWorldTick marks "of_<series>" as
    // expired for EACH series that leaves the offers horizon, seen or not, and the feed said "97 offers
    // expired". It counts the expired one whose offer was stamped as seen before the day, and every one expired by
    // deadline (TTL), which by construction had been seen. An offer that does both on the same day, once.
    { std::set<std::string> gone;
      for (const auto& id : save.expired_offers)
          if (!before.expired_ids.count(id) && before.offers_seen.count(id)) gone.insert(id);
      for (const auto& id : save.lapsed_offers)
          if (!before.lapsed_ids.count(id)) gone.insert(id);
      if (!gone.empty()) {
          DayEvent ev = makeEvent(absDay, DEK_OFFER_EXPIRED, false);
          DayEventParam(ev.params, "n", std::to_string(gone.size()));
          out.push_back(ev);
      } }
    // 6) relationship: new mark. Several on the SAME day fit in a single event: `team` is the last (the one the
    // screen names) and `n` how many there were, so the feed does not lie saying "one" when there were three.
    if (save.rel_marks.size() > before.rel_marks) {
        DayEvent ev = makeEvent(absDay, DEK_RELATION, rules.relation);
        DayEventParam(ev.params, "team", save.rel_marks.back().team);
        DayEventParam(ev.params, "n", std::to_string(save.rel_marks.size() - before.rel_marks));
        out.push_back(ev);
    }
    // 7) market decision week (C2 retires it with the global window)
    if (market::DaysToDecisionWeek(cc.year, cc.cursor_doy, career::kStartYear) == 0)
        out.push_back(makeEvent(absDay, DEK_DECISION, rules.offers));
    // 8) Review (finding 4a, ): OFFERED seats that are lost. CloseExpiredWindowOffers gives to someone else each
    // offered and unsigned seat when its deadline expires, and it did so silently: the player reached February with
    // the offers "rejected" without seeing it coming. Two warnings:
    //  · on the eve of the close, if any is left unsigned: it stops with the offers rule, because it can still be signed;
    //  · the day they are lost: a feed line that does not stop (there is no remedy any more, like an expired one).
    { int pendientes = 0;
      for (const auto& ws : save.window_seats)
          if (ws.state == WSEAT_OFFERED && !c1SeatSigned(save, ws)) pendientes++;
      if (pendientes > 0) {
          int ty = 0, td = 0;
          career::dateutil::YearDoyFromAbsDay(absDay + 1, ty, td);
          if (WindowOfferStillOpen(save, cc, cc.year, cc.cursor_doy, kStartYear) &&
              !WindowOfferStillOpen(save, cc, ty, td, kStartYear)) {
              DayEvent ev = makeEvent(absDay, DEK_SEATS_LAST_DAY, rules.offers);
              DayEventParam(ev.params, "n", std::to_string(pendientes));
              out.push_back(ev);
          }
      }
      int perdidas = 0;
      for (const auto& ws : save.window_seats)
          if (ws.state == WSEAT_REJECTED && before.offered_seat_keys.count(c1SeatKey(ws))) perdidas++;
      if (perdidas > 0) {
          DayEvent ev = makeEvent(absDay, DEK_SEATS_LOST, false);
          DayEventParam(ev.params, "n", std::to_string(perdidas));
          out.push_back(ev);
      } }
    // 9) 18/09 (Josh's decision): a NORMAL market offer also warns on the eve of expiring. Until now
    // only the window's seats did it (above); the 28-day offer expired silently even if you had
    // "offers" ticked. The deadline is that of OfferDeathAbs (TTL, negotiation thread or market close, whichever
    // comes first), and who is on their last day is decided by OffersOnLastDay: the same list that marks
    // their mails as unread again, so the stop and the mailbox warning cannot count different things.
    if (fichajesAbiertos) {
        const int ultimas = (int)OffersOnLastDay(save, absDay).size();
        if (ultimas > 0) {
            DayEvent ev = makeEvent(absDay, DEK_OFFER_LAST_DAY, rules.offers);
            DayEventParam(ev.params, "n", std::to_string(ultimas));
            out.push_back(ev);
        }
    }
    return out;
}

static void c1Append(WeekTickResult& acc, const WeekTickResult& t) {
    acc.expired_offer_ids.insert(acc.expired_offer_ids.end(), t.expired_offer_ids.begin(), t.expired_offer_ids.end());
    acc.generated_mail_ids.insert(acc.generated_mail_ids.end(), t.generated_mail_ids.begin(), t.generated_mail_ids.end());
}
// no-shows of the day: rounds whose history grew with dnfFlag (DnfPassedRounds marked them inside AdvanceDays)
// RolloverYear empties cc.enrollments; today it is unreachable here because the hard stop of 31 Dec always arrives
// before crossing with pending rounds.
static void c1CollectNoShows(const DaySnapshot& before, const CareerCalendar& cc, int absDay, AdvanceReport& r) {
    for (const auto& e : cc.enrollments) {
        auto it = before.history_len.find(e.series_id);
        const size_t was = (it == before.history_len.end()) ? 0 : it->second;
        for (size_t i = was; i < e.history.size(); i++) {
            if (!e.history[i].dnfFlag) continue;
            JumpNoShow ns; ns.series_id = e.series_id; ns.round = e.history[i].round; ns.year = cc.year;
            const SeriesDef* sd = SeriesById(e.series_id);
            if (sd) { ns.class_name = sd->class_name; ns.series_display = sd->display; }
            for (const auto& cr : CalendarForSeriesInYear(e.series_id, cc.year))
                if (cr.round == ns.round) { ns.track_name = cr.track_name; ns.date = cr.date; ns.doy = cr.doy; break; }
            ns.abs_day = ns.doy > 0 ? career::dateutil::AbsDayFromYearDoy(cc.year, ns.doy) : absDay;
            r.no_shows.push_back(ns);
            DayEvent ev; ev.abs_day = absDay; ev.kind = DEK_NOSHOW; ev.stops = false; ev.series_id = e.series_id;
            ev.label_key = "time.ev.noshow";
            DayEventParam(ev.params, "series", ns.series_display); DayEventParam(ev.params, "track", ns.track_name);
            DayEventParam(ev.params, "round", std::to_string(ns.round));
            r.crossed.push_back(ev);
        }
    }
}
// Clock stopwatch (diagnostic) -------------------------------------------------------------
// Inert: if the environment variable ZTA_CLOCK_PROFILE does not point to a file, it does absolutely nothing.
// It exists because the cost of a long advance cannot be seen from outside: the preview covers its own logs
// (LogCapture) and the only observable thing is "it takes a while". This says WHICH DAY and in WHICH PART.
namespace {
const char* clockProfPath() {
    static const char* p = std::getenv("ZTA_CLOCK_PROFILE");
    return (p && *p) ? p : nullptr;
}
void clockProf(const char* fmt, ...) {
    const char* p = clockProfPath();
    if (!p) return;
    FILE* f = std::fopen(p, "ab");
    if (!f) return;
    va_list ap; va_start(ap, fmt); std::vfprintf(f, fmt, ap); va_end(ap);
    std::fclose(f);
}
}  // anon
bool ClockProfOn() { return clockProfPath() != nullptr; }
void ClockProf(const char* fmt, ...) {
    const char* p = clockProfPath();
    if (!p) return;
    FILE* f = std::fopen(p, "ab");
    if (!f) return;
    va_list ap; va_start(ap, fmt); std::vfprintf(f, fmt, ap); va_end(ap);
    std::fclose(f);
}
namespace {
inline long long msDesde(const std::chrono::steady_clock::time_point& t0) {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
}
}  // anon

namespace {
bool (*g_previewAbortProbe)() = nullptr;   // registered by the API layer (SetPreviewAbortProbe)
bool (*g_advanceAbort)() = nullptr;        // active ONLY inside PreviewAdvance; the real advance never sees it
}  // anon
void SetPreviewAbortProbe(bool (*probe)()) { g_previewAbortProbe = probe; }

AdvanceReport AdvanceUntilStop(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                               const std::string& playerName, const StopRules& rules, int untilAbsDay) {
    AdvanceReport r;
    r.from_abs_day = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);
    if (untilAbsDay > 0 && untilAbsDay <= r.from_abs_day) {   // does not go back: only re-pulse
        c1Append(r.ticks, RunWorldTick(cc, prog, save, playerName, false));
        // the report has the SAME shape as any other stop: the app paints `crossed` without branching on
        // "the destination has already passed", which was the only case in which the feed arrived empty with stop_reason set.
        DayEvent ev; ev.abs_day = r.from_abs_day; ev.kind = DEK_UNTIL; ev.stops = true; ev.label_key = "time.ev.until";
        r.crossed.push_back(ev);
        r.stop_abs_day = r.from_abs_day; r.stop_reason = DEK_UNTIL; r.days = 0;
        return r;
    }
    // The year's context is frozen here and only rebuilt on crossing the year: a phase or door unlock
    // happening IN THE MIDDLE of the advance does not reopen slots until January (assumed cost of ruling R-T2, in exchange for not
    // rebuilding the master calendar 365 times).
    DayContext ctx = BuildDayContext(cc, save);
    const bool cronom = clockProfPath() != nullptr;
    if (cronom) clockProf("== avance desde abs %d (hasta %d), reglas %d%d%d%d%d\n", r.from_abs_day, untilAbsDay,
                          (int)rules.offers, (int)rules.myRaces, (int)rules.anyRace, (int)rules.mail, (int)rules.relation);
    for (int d = 1; d <= kAdvanceCapDays; d++) {
        // 18/09: a preview is left half-done as soon as newer clock work is waiting (see
        // SetPreviewAbortProbe). It is checked BEFORE simulating the day: what was already simulated is thrown away whole.
        if (g_advanceAbort && g_advanceAbort()) {
            if (cronom) clockProf("   previsión ABANDONADA en el día %d: hay trabajo de reloj más nuevo en la cola\n", d);
            r.aborted = true; r.days = d - 1;
            return r;
        }
        const auto tDia = std::chrono::steady_clock::now();
        const DaySnapshot before = SnapshotDay(save, cc);
        const long long msSnap = cronom ? msDesde(tDia) : 0;
        const int yearBefore = cc.year;
        const auto tAdv = std::chrono::steady_clock::now();
        AdvanceDays(cc, prog, save, playerName, 1);
        const long long msAdv = cronom ? msDesde(tAdv) : 0;
        const int absDay = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);
        // 18/09 — THE WORLD PULSE IS DAILY. Before, it was anchored to `d % 7`, the day WITHIN this
        // call, and that had two problems. The deep one: the clock was born moving by weeks and no longer
        // does — the player advances day by day, so the world does too. The measurable one: the anchor was not a date
        // but a position in the jump, so that the same Tuesday 3 March carried a pulse or not depending on
        // when you had started to advance.
        //
        // What it really gains is the deadlines. An offer's TTL starts counting the day the pulse
        // sees it for the first time (`MarkOfferSeen`), not the day it is born: with a weekly pulse an offer lost up to
        // six days of life and expired up to six days late. The three negotiation deadlines likewise — "they
        // answer you in three days" answered on the seventh.
        //
        // The other ten phases do not change: they are pure functions of the date and were already shielded against running
        // extra (see the idempotence comment in `RunWorldTick`). The only one that did depend on the rhythm
        // was the one-off substitution draw, and that is why it is now anchored to the calendar — see
        // `SubRideWeeklyDraw`.
        const auto tTick = std::chrono::steady_clock::now();
        c1Append(r.ticks, RunWorldTick(cc, prog, save, playerName, true));
        const long long msTick = cronom ? msDesde(tTick) : 0;
        if (cc.year != yearBefore) {
            r.crossed_year = true;
            ctx = BuildDayContext(cc, save);   // new year: calendar, landings and footprints, again just ONE
            DayEvent ev; ev.abs_day = absDay; ev.kind = DEK_ROLLOVER; ev.stops = false; ev.label_key = "time.ev.rollover";
            DayEventParam(ev.params, "year", std::to_string(cc.year));
            r.crossed.push_back(ev);
        }
        c1CollectNoShows(before, cc, absDay, r);
        const auto tStop = std::chrono::steady_clock::now();
        std::vector<DayEvent> evs = StopReasons(before, save, cc, ctx, absDay, rules);
        if (cronom) clockProf("dia %3d abs %5d: total %6lld ms (snapshot %lld, avance %lld, pulso %lld, paradas %lld)\n",
                              d, absDay, msDesde(tDia), msSnap, msAdv, msTick, msDesde(tStop));
        // 23/09 (Tester Hub ) — WITH A DESTINATION, THE RULES DO NOT STOP BEFORE ARRIVING. The rules belong to the
        // BLIND advance (no destination): with the factory ones (offers + my races) "go to date" would stop at the first
        // offer or at your race, and landing on the requested day is exactly what that action promises. These stay the same:
        // events are listed with their `stops` (the dialog warns about your own races that are skipped), on the day
        // of arrival the rule is still the REASON if it matches (R-T3-R5), and 31 December and the cap are
        // HARD stops that do not depend on the rules.
        const bool reglasParan = !(untilAbsDay > 0 && absDay < untilAbsDay);
        bool stop = false; DayEventKind why = DEK_CAP;
        for (const auto& e : evs) if (e.stops && !stop && reglasParan) { stop = true; why = e.kind; }
        r.crossed.insert(r.crossed.end(), evs.begin(), evs.end());
        if (cc.cursor_doy >= career::dateutil::DaysInYear(cc.year)) {   // 31 December: hard stop
            DayEvent ev; ev.abs_day = absDay; ev.kind = DEK_YEAR_END; ev.stops = true; ev.label_key = "time.ev.year_end";
            r.crossed.push_back(ev); stop = true; why = DEK_YEAR_END;
        }
        if (untilAbsDay > 0 && absDay >= untilAbsDay) {
            DayEvent ev; ev.abs_day = absDay; ev.kind = DEK_UNTIL; ev.stops = true; ev.label_key = "time.ev.until";
            r.crossed.push_back(ev); if (!stop) why = DEK_UNTIL; stop = true;
        }
        if (!stop && d == kAdvanceCapDays) {
            DayEvent ev; ev.abs_day = absDay; ev.kind = DEK_CAP; ev.stops = true; ev.label_key = "time.ev.cap";
            r.crossed.push_back(ev); stop = true; why = DEK_CAP;
        }
        if (!stop) continue;
        // 18/09 — HERE THERE WAS A "CLOSING RE-PULSE" AND IT IS NO LONGER NEEDED. It existed because the pulse was
        // anchored to `d % 7`: in an advance of fewer than seven days ("stop here", any race) the pulse
        // with advance NEVER arrived, and the preseason shake-up and the early renewal were left
        // unrun. With a daily pulse the stop day already brings its own, so the world arrives fresh at the
        // landing by construction and there is no second pulse to reconcile with the day's stops.
        r.stop_abs_day = absDay; r.stop_reason = why; r.days = d;
        return r;
    }
    r.stop_abs_day = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);   // never abs_day 0 by accident
    return r;   // unreachable: the cap always stops inside the loop
}
uint64_t AdvanceSeal(const CareerCalendar& cc, const SaveData& save) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](const std::string& bytes) { for (unsigned char c : bytes) { h ^= c; h *= 1099511628211ULL; } };
    mix(SerializeCareer(save));
    mix(std::string(1, '\x1E'));
    mix(SerializeCalendar(cc));
    return h;
}
AdvanceReport PreviewAdvance(const CareerCalendar& cc, const SaveData& save, const std::string& playerName,
                             const StopRules& rules, int untilAbsDay, AdvancePreviewKeep* keep) {
    const bool cron = ClockProfOn();
    auto t = std::chrono::steady_clock::now();
    const uint64_t seal = keep ? AdvanceSeal(cc, save) : 0;   // BEFORE simulating: the seal is of the starting state
    if (cron) { ClockProf("  [preview] sello %lld ms\n", msDesde(t)); t = std::chrono::steady_clock::now(); }
    CareerCalendar c = cc; SaveData s = save;   // the COPY: same idiom as cmd.debug.simWorld
    if (cron) { ClockProf("  [preview] copia %lld ms\n", msDesde(t)); t = std::chrono::steady_clock::now(); }
    LogCapture capture;                          // finding 7: nothing of what is simulated reaches the logs
    // 18/09: the abandon probe, ONLY during this simulation (and removed even if it exits by exception)
    struct Sonda { Sonda() { g_advanceAbort = g_previewAbortProbe; } ~Sonda() { g_advanceAbort = nullptr; } } sonda;
    AdvanceReport rep = AdvanceUntilStop(c, s.prog, s, playerName, rules, untilAbsDay);
    if (rep.aborted) {
        // what was simulated was partial: it is neither saved to adopt nor are its captured logs ever replayed
        if (cron) ClockProf("  [preview] abandonada tras %d dias (%lld ms)\n", rep.days, msDesde(t));
        return rep;
    }
    if (cron) { ClockProf("  [preview] simulacion %lld ms (%d dias, %d eventos cruzados)\n",
                          msDesde(t), rep.days, (int)rep.crossed.size()); t = std::chrono::steady_clock::now(); }
    if (keep) {
        keep->valid = true; keep->seal = seal; keep->untilAbsDay = untilAbsDay;
        keep->playerName = playerName; keep->rules = rules; keep->report = rep;
        keep->cc = std::move(c); keep->save = std::move(s);
        if (cron) { ClockProf("  [preview] guardar keep %lld ms\n", msDesde(t)); t = std::chrono::steady_clock::now(); }
        keep->logs = capture.Take();
        if (cron) ClockProf("  [preview] capturar logs %lld ms (%d bytes)\n", msDesde(t), (int)keep->logs.size());
    }
    return rep;
}
bool AdoptPreviewedAdvance(CareerCalendar& cc, CareerProgress& prog, SaveData& save, const std::string& playerName,
                           const StopRules& rules, int untilAbsDay, AdvancePreviewKeep& keep, AdvanceReport& out) {
    if (!keep.valid || keep.untilAbsDay != untilAbsDay || keep.playerName != playerName || !(keep.rules == rules))
        return false;
    if (AdvanceSeal(cc, save) != keep.seal) return false;   // the state moved since the preview: it is simulated
    cc = std::move(keep.cc);
    save = std::move(keep.save);
    if (&prog != &save.prog) prog = save.prog;
    LogReplay(keep.logs);
    out = std::move(keep.report);
    keep = AdvancePreviewKeep{};
    return true;
}

// ============================ mail generator ============================

namespace {

// Does a mail with this id already exist in save.emails? (content-derived dedup). Season milestone
// ids EMBED the year -> the same event next season carries a different id and DOES re-fire.
bool mailExists(const career::SaveData& save, const std::string& id) {
    for (const auto& e : save.emails) if (e.id == id) return true;
    return false;
}

// persists a new mail as GeneratedEmail (id+keys+payload_key, NEVER the body) and adds it to
// the output list. day/tag come from the EmailDef (the renderer re-resolves EN/ES from payload_key when drawing it).
// Living mail: a deterministic variant chosen AT GENERATION time (persisted; no rand) + params + sender.
// forceVariant >= 0 OVERRIDES the hash -- used by emissions whose data CONTRACT requires a specific
// variant (e.g. season_close/ms_finalseason, which lack the params their other variants need; MailBodyKey already
// clamps to [0,count-1], so an out-of-range forceVariant can't break anything). -1 (default) = the usual hash.
void emitMail(career::SaveData& save, std::vector<market::EmailDef>& out,
              const market::EmailDef& def, int family, int subtype, const std::string& payloadKey, int day, int year,
              const career::MailParams& params, const std::string& fromName, const std::string& fromRoleKey,
              int forceVariant = -1) {
    career::GeneratedEmail ge;
    ge.id          = def.id;
    ge.tag         = def.tag;
    ge.day         = day;
    ge.year        = year;   // fix for "mail with an absurd date when crossing the year": the calendar's year AT EMISSION TIME
    ge.family      = family;        // 0=OFFER 1=PROGRESS/MILESTONE (there is no "you let it slide" family)
    ge.read        = 0;
    ge.answered    = 0;
    ge.choice      = 0;
    ge.subtype     = subtype;
    ge.payload_key = payloadKey;
    if (forceVariant >= 0) {
        ge.variant = forceVariant;
    } else {
        int vc = career::MailVariantCount(payloadKey);
        // Mixes the CAREER's seed into the hash -- with only def.id, a constant id (one-shot:
        // ms_firstpodium/ms_firstwin/ob_first_race/ob_first_goal) would ALWAYS resolve the same variant for EVERY
        // player, forever. Mixing in save.career_seed varies it between careers while staying deterministic
        // within the SAME career (the persisted variant doesn't change once generated).
        ge.variant = vc > 0 ? (int)(career::MailHash(def.id + std::to_string(save.career_seed)) % (unsigned)vc) : 0;
    }
    ge.params      = career::MailParamsEncode(params);
    ge.from_name   = fromName;                        // raw display name (person/brand); '|' -> '/' (pipe character)
    for (auto& ch : ge.from_name) if (ch == '|') ch = '/';
    ge.from_role_key = fromRoleKey;
    save.emails.push_back(ge);
    out.push_back(def);
}

// local helper: copies 'p', replacing (or adding) key 'k' with 'v' -- used to specialize 'base' per emission
// (e.g. {series} for ONE specific offer, or {pos} for ONE specific title fight) without mutating the shared base.
career::MailParams withParam(career::MailParams p, const std::string& k, const std::string& v) {
    for (auto& kv : p) if (kv.first == k) { kv.second = v; return p; }
    p.push_back({k, v});
    return p;
}

// prunes the inbox at generation time: a total cap + a per-family table,
// always pruning the OLDEST (by day) mail that's READ + not pending (career::MailActionablePending --
// unread or an OFFER/decision awaiting a response is NEVER pruned). If everything left over is unprunable, it
// stays above the cap (that's correct: housekeeping, not a hard cap).
void pruneInbox(career::SaveData& save) {
    auto countFamily = [&](int fam)->int { int n=0; for (const auto& e : save.emails) if (e.family==fam) n++; return n; };
    auto famCap  = [&](int fam)->int  { return (fam >= 0 && fam < 9) ? career::bal::MK_INBOX_FAMILY_CAPS[fam] : -1; };
    auto famOver = [&](int fam)->bool { int c = famCap(fam); return c >= 0 && countFamily(fam) > c; };
    auto overCap = [&]()->bool {
        if ((int)save.emails.size() > career::bal::MK_INBOX_MAX_MAILS) return true;
        for (int f = 0; f < 9; f++) if (famOver(f)) return true;
        return false;
    };
    while (overCap()) {
        int victim = -1;
        for (size_t i = 0; i < save.emails.size(); i++) {
            const auto& e = save.emails[i];
            if (career::MailActionablePending(e, save)) continue;   // never prunes pending/unread mail
            // respects the exceeded cap: if only one family is over, it prunes from THAT family.
            bool over = ((int)save.emails.size() > career::bal::MK_INBOX_MAX_MAILS) || famOver(e.family);
            if (!over) continue;
            if (victim < 0 || save.emails[i].day < save.emails[victim].day) victim = (int)i;
        }
        if (victim < 0) break;                                // nothing prunable -> stays above the cap
        save.emails.erase(save.emails.begin() + victim);
    }
}

} // anonymous namespace

// The duel template NEVER asserts what the engine doesn't actually know -- duel_won and
// race_clean force the coherent half of the variants (0/1 vs 2/3); the hash only picks within that half.
// Pure (no SaveData/RNG) so the test can run it as a direct matrix (beef x aggr x duelWon x raceClean).
void RivalDuelTemplate(int beefLevel, double aggr, bool duelWon, bool raceClean, unsigned hash,
                       std::string& keyOut, int& variantOut) {
    if (beefLevel >= bal::MK_BEEF_LEVEL_BEEF) {
        keyOut = "rival_duel_beef";
        // v0/v1 = "dirty race" ONLY with !raceClean; v2/v3 = clean rivalry, no contact implied.
        variantOut = (raceClean ? 2 : 0) + (int)(hash % 2);
    } else if (aggr >= bal::MK_BEEF_HEATED_AGGR) {
        keyOut = "rival_duel_heated";
        // v0/v1 assume the player passed ("you passed me") -> ONLY with duelWon; v2/v3 = the rival stayed ahead.
        variantOut = (duelWon ? 0 : 2) + (int)(hash % 2);
    } else {
        keyOut = "rival_duel_calm";
        // the calm v1 line implies the rival LOST the duel -> a lost duel forces v0 (neutral) instead
        variantOut = duelWon ? (int)(hash % 2u) : 0;
    }
}

std::vector<market::EmailDef> GenerateCareerEmails(SaveData& save,
                                                   const std::vector<market::GenOffer>& offers,
                                                   const EmailGenContext& ctx) {
    std::vector<market::EmailDef> out;

    // Living mail: the shared params base for every emission in this pulse -- each emission
    // can specialize it (withParam) for its own {series}/{pos} without mutating this shared base.
    career::MailParams base;
    if (!ctx.player.empty())         base.push_back({"player",  ctx.player});
    if (!ctx.series_display.empty()) base.push_back({"series",  ctx.series_display});
    if (!ctx.track.empty())          base.push_back({"track",   ctx.track});
    if (ctx.race_pos > 0)            base.push_back({"pos",     std::to_string(ctx.race_pos)});
    base.push_back({"credits", career::MailFmtCredits(ctx.credits)});
    if (!ctx.team_display.empty())   base.push_back({"team",    ctx.team_display});

    // Family A: OFFER -- one per freshly-surfaced DIRECT offer (not held, not declined). ----
    // The id is content-derived ("offer_"+offer.id, and offer.id is already "of_"+series) -> stable + deduped. The payload_key
    // is the TR template key the renderer re-resolves to EN/ES (we NEVER persist the body). The
    // voice is the offer's team principal (sender persona: go.def.team signs it, the
    // LIVE team label from career_market.cpp; go.def.from there is the CLASS, NOT a person/team -- signing with it
    // used to produce "GT3 . Team Principal" instead of the team's real name). Role "inbox_role_teamprincipal".
    // offer=true + a CTA to the Market.
    for (const auto& go : offers) {
        // A held offer "keeps watching you" -- a courtship mail, 1 per team+year, with NO actions (flavor only).
        // payload_key = the template key (not the offer id): the sync auto-resolves it and it's eligible for pruning on its own.
        if (go.held) {
            std::string cid = "court_" + go.def.team + "_" + std::to_string(ctx.year);
            if (!mailExists(save, cid)) {
                market::EmailDef m; m.id = cid; m.from = go.def.team; m.tag = 1; m.day = ctx.day;
                std::string sid2 = seriesIdOfOffer(go.def);
                const career::SeriesDef* sd2 = career::SeriesById(sid2);
                // The courtship mail is signed by the team COURTING you, not your current team.
                // base.{team} = your own team (ctx.team_display); it has to be overwritten with go.def.team (the suitor),
                // or the courtship mail's body names the wrong team.
                career::MailParams p = withParam(withParam(base, "series", sd2 ? sd2->display : sid2),
                                                 "team", go.def.team);
                emitMail(save, out, m, /*family=*/0, /*subtype=*/career::bal::MK_MAIL_SUBTYPE_COURTING,
                         /*payloadKey=*/"courting", ctx.day, ctx.year, p,
                         /*fromName=*/go.def.team, /*fromRoleKey=*/"inbox_role_teamprincipal");
            }
            continue;
        }
        if (!go.def.direct) continue;                             // only signable DIRECT offers (held = informational lane, courtship above)
        bool declined = save.declined_offers.count(go.def.id) > 0;
        if (declined) continue;                                   // declined -> no mail
        // The renewal already covers that seat with better narrative terms (your own team
        // renews you, not some generic suitor) -- if save.pending_renewals already carries an OfferDef for this
        // same id, career_calendar.cpp's "renew_" mail delivers it; don't duplicate it with an offer here.
        bool hasRenewal = false;
        for (const auto& ren : save.pending_renewals)
            if (ren.id == go.def.id) { hasRenewal = true; break; }
        if (hasRenewal) continue;
        std::string id = "offer_" + go.def.id;                    // content-derived (1 per series); stable dedup
        if (mailExists(save, id)) continue;                       // already emitted -> don't duplicate
        market::EmailDef m;
        m.id = id; m.from = go.def.team; m.org = go.def.from; m.tag = 1; m.day = ctx.day; m.type = 0;
        m.offer = true; m.ctaLabel = "mkt_view_market"; m.ctaLabel_en = "mkt_view_market";
        // payload_key = the offer (its title/pitch are re-resolved by the renderer from OfferDef via the series id).
        // The OFFER mail carries the contract TYPE in its subject so the inbox communicates
        // the deal type at a glance (the per-type "Incluye" + the generated pitch live on the Market offer card,
        // re-resolved from OfferDef). subtype encodes the taxonomy type index (0..5) — consumes the type profile,
        // adds no new requirements. The body stays content-derived (never persisted).
        int typeIdx = career::DeriveOfferPresentation(go.def.contractKind, go.def.archetype, nullptr).typeLabel;
        m.subject = m.subject_en = go.def.title;                  // flavor text lives in memory; only payload_key is persisted
        // {series} for THIS specific offer (may differ from the series of the last race carried in ctx.base):
        // Resolved from the catalog's real display name (SeriesById, same as the rest of this
        // file), not go.def.title_en (the live generator builds it as "Seat at " + display -> that leaked English
        // text into a Spanish-language mail: "Contract offer — Seat at GT3 Europe"). Falls back to the raw id if the
        // catalog fails to resolve (defensive; shouldn't happen with real offers).
        std::string offerSeriesId = seriesIdOfOffer(go.def);
        const career::SeriesDef* offerSd = career::SeriesById(offerSeriesId);
        career::MailParams offerParams = withParam(base, "series", offerSd ? offerSd->display : offerSeriesId);
        // An offer with a tryout attached sets a param that MailActionsFor reads to disable the Accept button
        // (direct signing doesn't apply; the tryout takes priority). It can still be declined.
        if (go.def.requiresTryout) offerParams = withParam(offerParams, "tryout", "1");
        // "negotiable" is frozen AT EMISSION time, from the same source as query.market (NegoNegotiable,
        // career_nego_thread.cpp) -> QueryInbox reads it to decide whether to offer the Negotiate CTA (see below).
        offerParams = withParam(offerParams, "negotiable", career::NegoNegotiable(go.def) ? "1" : "0");
        // family 0 = OFFER; subtype = the contract TYPE index (0..5) so the inbox can label the offer's type.
        emitMail(save, out, m, /*family=*/0, /*subtype=*/typeIdx, /*payloadKey=*/go.def.id, ctx.day, ctx.year,
                 offerParams, /*fromName=*/go.def.team, /*fromRoleKey=*/"inbox_role_teamprincipal");
    }

    // Family B: PROGRESS/MILESTONE — Atenea Montero (manager, "inbox_role_manager") signs
    // every milestone mail: it's your manager writing to you, not an anonymous "Paddock".
    // Each milestone has a content-derived id; season-scoped ones embed the year (so they re-fire next season).
    // forceVariant is optional and additive (default -1 = the usual hash) — used by emissions whose
    // data contract requires a specific variant (see ms_finalseason below).
    auto emitMilestone = [&](const std::string& id, int subtype, int tag,
                             const std::string& from, const std::string& org,
                             const std::string& payloadKey, const career::MailParams& params,
                             int forceVariant = -1) {
        if (id.empty() || mailExists(save, id)) return;           // content-id dedup (one-shot or per-season-year)
        market::EmailDef m;
        m.id = id; m.from = from; m.org = org; m.tag = tag; m.day = ctx.day; m.type = 0;
        emitMail(save, out, m, /*family=*/1, subtype, payloadKey, ctx.day, ctx.year,
                 params, career::MailManagerName(), "inbox_role_manager", forceVariant);
    };

    // onboarding arc: stage 1 -> ob_first_race on the first settle; stage 2 -> ob_first_goal
    //      on the next resultless tick; stage 3 = arc complete (goes silent). Fixed ids (one-shot). The
    //      onboarding arc is NEVER dropped -> it sits outside the counter for the
    //      per-pulse cap, always BEFORE this family's regular milestones. ----
    if (save.mailnarr.onboard_stage == 1 && ctx.has_race_result)
        { emitMilestone("ob_first_race", /*subtype=*/7, /*tag=*/1, career::MailManagerName(), "manager", "ob_first_race", base);
          if (mailExists(save, "ob_first_race")) save.mailnarr.onboard_stage = 2; }
    else if (save.mailnarr.onboard_stage == 2 && !ctx.has_race_result)
        { emitMilestone("ob_first_goal", /*subtype=*/8, /*tag=*/1, career::MailManagerName(), "manager", "ob_first_goal", base);
          if (mailExists(save, "ob_first_goal")) save.mailnarr.onboard_stage = 3; }

    // TEAMOPS — family 8, same style as emitMilestone, but the sender is decided by the
    //      CONTRACT (not fixed to Atenea): with an active contract (ctx.team_display non-empty) the team
    //      principal signs ("inbox_role_teamprincipal"); without a contract, Atenea stays on duty ("inbox_role_manager").
    auto emitOps = [&](const std::string& id, int subtype, const std::string& payloadKey,
                       const career::MailParams& params) {
        if (id.empty() || mailExists(save, id)) return;           // content-id dedup (one-shot or per-season-year)
        bool hasTeam = !ctx.team_display.empty();
        const std::string fromName = hasTeam ? ctx.team_display : career::MailManagerName();
        const std::string fromRole = hasTeam ? "inbox_role_teamprincipal" : "inbox_role_manager";
        market::EmailDef m;
        m.id = id; m.from = fromName; m.org = hasTeam ? "team" : "manager"; m.tag = 1; m.day = ctx.day; m.type = 0;
        emitMail(save, out, m, /*family=*/8, subtype, payloadKey, ctx.day, ctx.year, params, fromName, fromRole);
    };

    // TREASURY alert — family 8 subtype 2. Unlike TEAMOPS above,
    //      this one is never dropped by the per-pulse cap -> it's emitted HERE, before the capped block, without going
    //      through tryOps/p2Sent. Atenea (manager) ALWAYS signs it, regardless of the contract: it's a
    //      driver-treasury notice, not a team message (which is why it does NOT reuse emitOps, which picks team/manager
    //      based on ctx.team_display). Fires if the next actionable event (ctx.next_event, filled in ONLY by the
    //      other code path) lands within MK_MAILOPS_TREASURY_DAYS and its guaranteed minimum payout
    //      (RaceEntryOutlay) exceeds the current balance. The id embeds the EVENT's abs_day (not the tick's) -> one
    //      alert per event (it won't repeat until the next event changes).
    if (ctx.next_event.days_away >= 0 && ctx.next_event.days_away <= bal::MK_MAILOPS_TREASURY_DAYS
        && ctx.next_event.min_outlay > 0 && ctx.credits < ctx.next_event.min_outlay) {
        int eventAbsDay = career::dateutil::AbsDayFromYearDoy(ctx.year, ctx.day) + ctx.next_event.days_away;
        std::string id = "ops_treasury_" + std::to_string(eventAbsDay);
        if (!mailExists(save, id)) {
            // Atenea ALWAYS signs (see comment above) -> the params carry no
            // {team} even if 'base' carries one (last-season contract). Belt-and-braces alongside the rewording
            // of the ops_treasury* strings: it starts from 'base' without the "team" entry instead of inheriting it.
            career::MailParams treasuryBase;
            for (const auto& kv : base) if (kv.first != "team") treasuryBase.push_back(kv);
            career::MailParams p = withParam(withParam(treasuryBase, "track", ctx.next_event.track),
                                             "cost", career::MailFmtCredits(ctx.next_event.min_outlay));
            market::EmailDef m;
            m.id = id; m.from = career::MailManagerName(); m.org = "manager"; m.tag = 1; m.day = ctx.day; m.type = 0;
            emitMail(save, out, m, /*family=*/8, /*subtype=*/2, /*payloadKey=*/"ops_treasury", ctx.day, ctx.year,
                     p, career::MailManagerName(), "inbox_role_manager");
        }
    }

    // PRESS on settle — doesn't consume the shared per-pulse cap (same as the treasury alert above). The
    //      journalist (MailPressName / inbox_role_press, tag=2, the race's official voice) requests an interview after a
    //      milestone; the player picks a TONE (humble/spicy/corporate) and that moves rep/marks/beef in
    //      ApplyPressAnswer, NEVER the rating. Hard rule: at most ONE unanswered interview at a time
    //      (PressInterviewPending) — if one is already pending, the new one is DROPPED (not deferred). These coexist with
    //      Atenea's milestones (ms_firstwin celebrates; the press asks questions). The headline's rival: save.nemesis_id
    //      if non-empty, otherwise the highest-level beef; resolved via DriverById (nullptr -> no rival). With a rival ->
    //      params rival+rival_id and variant hash%2 (v0/v1); without a rival -> forceVariant 0 (v0 is rival-free, the emitter
    //      forces it because v1 weaves in {rival}). rival_team is OMITTED: there's no cheap seat->team resolver in
    //      career_seats.h (the rival's team cooldown is left without data; ApplyPressAnswer already tolerates that). ----
    auto emitPress = [&](const std::string& id, int subtype, const std::string& payloadKey,
                         const career::MailParams& params) {
        if (mailExists(save, id)) return;                          // content-id dedup (one-shot / per-season-year)
        if (career::PressInterviewPending(save)) return;           // one interview at a time
        career::MailParams p = params;
        std::string rivalId = save.nemesis_id;
        if (rivalId.empty()) {                                     // no nemesis -> the highest-level beef
            int best = 0;
            for (const auto& kv : save.beef) if (kv.second.level > best) { best = kv.second.level; rivalId = kv.first; }
        }
        int forceVariant = 0;                                      // v0 rival-free by default
        if (!rivalId.empty()) {
            const career::PoolDriver* pd = career::DriverById(rivalId);
            if (pd) {
                p = withParam(withParam(p, "rival", pd->name.empty() ? pd->id : pd->name), "rival_id", rivalId);
                forceVariant = (int)(career::MailHash(id) % 2u);   // a rival is present -> hash%2 (v0/v1)
            }
        }
        market::EmailDef m;
        m.id = id; m.from = career::MailPressName(); m.org = "press"; m.tag = 2; m.day = ctx.day; m.type = 0;
        emitMail(save, out, m, /*family=*/6, subtype, payloadKey, ctx.day, ctx.year, p,
                 career::MailPressName(), "inbox_role_press", forceVariant);
    };

    // First win: id press_fw_<year>, subtype 0. Params {player}{series}{track} (already in base) + rival.
    if (ctx.first_win)
        emitPress("press_fw_" + std::to_string(ctx.year), /*subtype=*/0, "press_q_firstwin", base);

    // Title fight: a real signal from standings (press_standing_pos/press_rounds_left). Gate:
    //     P1..MAXPOS with at least 1 and at most ROUNDS rounds remaining (an imminent, contested finish). id
    //     press_tf_<series>_<year>, subtype 1, params + {pos} = standing in the CHAMPIONSHIP (not the finishing position of the last
    //     race in base). {series} comes from the series in play (press_series), in case it differs from the last one raced.
    if (ctx.press_standing_pos >= 1 && ctx.press_standing_pos <= bal::MK_PRESS_TITLEFIGHT_MAXPOS
        && ctx.press_rounds_left >= 1 && ctx.press_rounds_left <= bal::MK_PRESS_TITLEFIGHT_ROUNDS) {
        career::MailParams p = withParam(base, "pos", std::to_string(ctx.press_standing_pos));
        if (!ctx.press_series.empty())
            if (const career::SeriesDef* sd = career::SeriesById(ctx.press_series))
                p = withParam(p, "series", sd->display);
        emitPress("press_tf_" + ctx.press_series + "_" + std::to_string(ctx.year), /*subtype=*/1,
                  "press_q_titlefight", p);
    }

    // A cap shared per pulse: at most MK_MAIL_P2_PER_PULSE
    //      new "flavor" mails per pulse, shared between Atenea's milestones (family 1), TEAMOPS (family 8) and
    //      the on-track rivalry (family 5, RIVAL) — 2 total, not 2+2+2. Whatever doesn't fit is DROPPED (not deferred
    //      to the next pulse). The counter only counts NEW emissions (save.emails grew) -> an id that's already
    //      deduplicated does NOT consume quota, so repeating the same pulse (or an already-emitted season id) doesn't
    //      block the others. Exact priority (the rivalry mail is inserted after the clean streak):
    //      win > podium > REP band > final season > hot streak > cold streak > damage
    //      bill > clean streak > on-track rivalry > track history (the last four go at the end —
    //      damage bill/clean streak stayed as TEAMOPS family 8; the rivalry mail was added as family 5/RIVAL;
    //      track history stays family 1/Atenea). The late-stage "title fight" mail
    //      was removed from this list — the generator no longer fires it. ----
    int p2Sent = 0;
    auto tryMilestone = [&](const std::string& id, int subtype, const std::string& key,
                            const career::MailParams& params, int forceVariant = -1) {
        if (p2Sent >= bal::MK_MAIL_P2_PER_PULSE) return;
        size_t before = save.emails.size();
        emitMilestone(id, subtype, /*tag=*/1, career::MailManagerName(), "manager", key, params, forceVariant);
        if (save.emails.size() > before) p2Sent++;
    };
    auto tryOps = [&](const std::string& id, int subtype, const std::string& key,
                      const career::MailParams& params) {
        if (p2Sent >= bal::MK_MAIL_P2_PER_PULSE) return;
        size_t before = save.emails.size();
        emitOps(id, subtype, key, params);
        if (save.emails.size() > before) p2Sent++;
    };

    // (1) first podium / first win: race-scoped one-shots (no year -> only once per career).
    //     subtype 2 = first_podium, 3 = first_win.
    if (ctx.first_win)    tryMilestone("ms_firstwin",    /*subtype=*/3, "ms_firstwin",    base);
    if (ctx.first_podium) tryMilestone("ms_firstpodium", /*subtype=*/2, "ms_firstpodium", base);

    // Block (2), the late-stage "title fight" (ms_titlefight, pos<=MAXPOS &&
    // rounds_left<=ROUNDS_LEFT), is RETIRED here — the generator never fires it anymore. It's replaced by a
    // real press system; the ms_titlefight_* templates stay in strings.cpp ONLY to render
    // mails already persisted in old saves (subtype 4 stays reserved, career_save.h:119).

    // (2) crossing a REP band: one id PER band reached (one-shot per band, not per year) -> moving up a
    //     band triggers the bulletin; if it drops back and rises again, the id already exists and won't re-fire (correct: it was already
    //     announced). subtype 1 = repband. Band 0 = starting point (rep 0), NOT an achievement —
    //     with >=0 the very first pulse of a new career fired "People are starting to ask about you" before you'd done anything.
    if (ctx.rep_band >= 1)
        tryMilestone("ms_repband_" + std::to_string(ctx.rep_band), /*subtype=*/1, "ms_repband", base);

    // (3) final season of your own contract: seasons_left==1. subtype 5 = finalseason.
    // Up to 2 teams from the courting radar (save.courting) sign off on Atenea's notice — v0/v2
    // name them ({team_a}/{team_b}). Data contract: v0/v2 REQUIRE both to be non-empty
    // (MailInterpolate blanks out placeholders with no data -> a broken "Two names: , ."); v1 is the fallback without
    // team names, written without those tokens (verified in strings.cpp). With <2 suitors, v1 is FORCED instead
    // of leaving it to the hash — with exactly 1, team_a is still passed (v1 ignores it, harmless, in case
    // it's ever used).
    // {series}/{team} for THIS milestone can NOT come from 'base' — 'base' carries
    // series_display/team_display from the context of the race just run, or, on the
    // AdvanceWeek tick (RunWorldTick), where series_display is NEVER filled in (stays ""). finalseason
    // fires from ctx.final_series (the contract with seasons_left==1), which can be a series different from the
    // one raced under a multi-contract career -> {series}/{team} must be resolved from ITS OWN contract, not someone else's.
    if (!ctx.final_series.empty()) {
        career::MailParams fsParams = base;
        if (const career::SeriesDef* fsSd = career::SeriesById(ctx.final_series))
            fsParams = withParam(fsParams, "series", fsSd->display);
        for (const auto& c : save.contracts)
            if (c.active && c.series_id == ctx.final_series) { fsParams = withParam(fsParams, "team", c.team_id); break; }
        bool hasTeamA = !save.courting.empty();
        bool hasTeamB = save.courting.size() >= 2;
        if (hasTeamA) fsParams = withParam(fsParams, "team_a", save.courting[0].team_id);
        if (hasTeamB) fsParams = withParam(fsParams, "team_b", save.courting[1].team_id);
        int forceVariant = (hasTeamA && hasTeamB) ? -1 : /*ms_finalseason_v1*/ 1;
        tryMilestone("ms_finalseason_" + ctx.final_series + "_" + std::to_string(ctx.year),
                     /*subtype=*/5, "ms_finalseason", fsParams, forceVariant);
    }

    // (4) streaks: hot streak = MK_MAIL_STREAK_PODIUMS consecutive podiums; cold streak =
    //     MK_MAIL_STREAK_BAD consecutive bottom-third-or-DNF finishes (save.mailnarr, updated by MailNarrativeOnSettle
    //     on the post-settle pulse). Ids embed the year -> one-shot per season (they re-fire next season,
    //     same pattern as finalseason). subtype 9 = hotstreak, 10 = coldstreak. hotstreak's {track} takes the
    //     last notable circuit (podium/win) of the current streak.
    // Streaks fire ONLY on the pulse that carries a result (settle). Without this gate, a streak that already crossed
    // the threshold but got bumped by the 2-per-pulse cap could re-attempt firing on a later resultless
    // AdvanceWeek tick -> {track} arrives empty ("and the one at was the worst"). The settle pulse is the only
    // source of {track} for these two beats.
    if (ctx.has_race_result && save.mailnarr.consec_podiums >= bal::MK_MAIL_STREAK_PODIUMS)
        tryMilestone("ms_hotstreak_" + std::to_string(ctx.year), /*subtype=*/9, "ms_hotstreak",
                     withParam(base, "track", save.mailnarr.last_notable_track));
    if (ctx.has_race_result && save.mailnarr.consec_bottom >= bal::MK_MAIL_STREAK_BAD)
        tryMilestone("ms_coldstreak_" + std::to_string(ctx.year), /*subtype=*/10, "ms_coldstreak", base);

    // (5) Damage bill — TEAMOPS, LOWEST priority except for the clean streak (right below). Only on
    //     the post-settle pulse (ctx.has_race_result); fires if THIS race's deductible passes the threshold —
    //     the GREATER of a credits floor (without a salaried contract) and a % of the per-race payout (with
    //     a contract). Tone scales with cleanliness (EMA): calm if it's going well, annoyed, or a serious warning.
    //     subtype 0 = ops_damage. The id EMBEDS year+day -> one bill per race pulse (won't retry the same
    //     day if it got squeezed out by the shared cap; if the race was clean the deductible is near 0 and never reaches the threshold).
    if (ctx.has_race_result) {
        long floorCr = bal::MK_MAILOPS_DEDUCT_MIN_CR;
        long pctCr   = ctx.per_race * bal::MK_MAILOPS_DEDUCT_PCT / 100;
        long threshold = pctCr > floorCr ? pctCr : floorCr;
        if (ctx.deductible >= threshold) {
            // cleanliness < 0 means no data yet (no EMA) -> defaults to a NEUTRAL tone,
            // not the harshest one. Without this check, -1 fell below MK_MAILOPS_CLEAN_WARN and forced the
            // "warn" tone (the most aggressive) for exactly the case where we DON'T know if the race was dirty.
            std::string tone = ctx.cleanliness < 0                          ? "ops_damage_calm"
                              : ctx.cleanliness >= bal::MK_MAILOPS_CLEAN_IRKED ? "ops_damage_calm"
                              : ctx.cleanliness >= bal::MK_MAILOPS_CLEAN_WARN  ? "ops_damage_irked"
                                                                                : "ops_damage_warn";
            tryOps("ops_damage_" + std::to_string(ctx.year) + "_" + std::to_string(ctx.day), /*subtype=*/0, tone,
                  withParam(base, "cost", career::MailFmtCredits(ctx.deductible)));
        }
    }

    // (6) Clean streak — TEAMOPS, LOWEST priority. Reaches the threshold of consecutive
    //     incident-free races (save.mailnarr.consec_clean, updated by MailNarrativeOnSettle) -> the team
    //     acknowledges it. subtype 1 = ops_cleanstreak. The id EMBEDS the year -> one-shot per season (it re-fires
    //     next season if the streak holds, same pattern as hotstreak/coldstreak).
    // Same has_race_result gate as hotstreak/coldstreak/ops_damage above — without it,
    // a streak that already crossed the threshold but got squeezed out by the shared cap on the settle pulse could re-attempt
    // firing on a later AdvanceWeek tick with no race ("whatever doesn't fit is dropped, not deferred").
    if (ctx.has_race_result && save.mailnarr.consec_clean >= bal::MK_MAIL_CLEAN_STREAK)
        tryOps("ops_cleanstreak_" + std::to_string(ctx.year), /*subtype=*/1, "ops_cleanstreak", base);

    // (6.5) On-track rivalry — family 5 (RIVAL), NOT TEAMOPS: the rival driver themself signs it in the first
    //     person (from_name/from_role_key = "inbox_role_rival"), not Atenea or the team principal. It shares the
    //     same shared cap (bal::MK_MAIL_P2_PER_PULSE), with priority right after the clean streak (it occupies the slot
    //     left free before track history). Gate: a pulse with a result (ctx.has_race_result)
    //     AND there was a rival from the adjacent cast this race (ctx.duel_rival_id, filled in ONLY by the
    //     head-to-head snapshot in the overlay). With no name resolved (a DriverById miss in the pool), it's NOT emitted:
    //     a mail signed by nobody would break the first-person voice. tag=2 (career_save.h documents 0=sponsor/
    //     1=team/2=official) — the rival speaks as the race's OFFICIAL/third-party voice, not the player's team
    //     (this corrects the original tag=1, mistakenly inherited from the other person-senders).
    //     Its own cooldown of MK_RIVAL_DUEL_COOLDOWN_RACES races per rival (save.beef[id].last_race_idx, counted
    //     in race_log race count, not days/years) — the same id doesn't write every race, only when
    //     "a while" has passed since the last run-in with them. The tone FAMILY scales with the rival's base aggression
    //     (DriverById->base_aggression >= MK_BEEF_HEATED_AGGR -> "heated"; otherwise "calm"; with no id
    //     resolved in the pool -> calm, aggr=0.0) until accumulated beef (level >= MK_BEEF_LEVEL_BEEF) forces
    //     the "you again" template (rival_duel_beef), which OVERRIDES the aggression-based tone.
    //     The template no longer claims things the engine doesn't know — within each family, the VARIANT
    //     is not freely hashed; RivalDuelTemplate (a pure selector, verified against a template matrix test) uses
    //     duel_won/race_clean to force a coherent HALF of the variants (heated: v0/v1 ONLY if duel_won —
    //     "you passed me" —, v2/v3 otherwise; beef: v2/v3 ONLY if race_clean, v0/v1 "dirty race" otherwise; calm is
    //     direction-neutral, hash%2 is free) and MailHash(id) only picks WITHIN that half (forceVariant in
    //     emitMail). The rivalry ALWAYS feeds the beef counter when it actually fires (level++ capped at 5, last_year,
    //     last_race_idx) — even in calm tone: it's the metric for "how many times you've crossed paths on track", not
    //     of anger. The id EMBEDS the race count (race_log.size() at emission time) -> it re-fires after the
    //     cooldown with a different id (normal content-id dedup). With no name/rival/cooldown -> nothing
    //     (silent, not deferred).
    if (ctx.has_race_result && !ctx.duel_rival_id.empty()) {
        career::Beef& bf = save.beef[ctx.duel_rival_id];
        int raceCount = (int)save.race_log.size();
        bool cooldownOk = bf.last_race_idx < 0 || raceCount - bf.last_race_idx >= bal::MK_RIVAL_DUEL_COOLDOWN_RACES;
        if (cooldownOk && !ctx.duel_rival_name.empty() && p2Sent < bal::MK_MAIL_P2_PER_PULSE) {
            std::string id = "rival_duel_" + ctx.duel_rival_id + "_" + std::to_string(raceCount);
            if (!mailExists(save, id)) {
                const career::PoolDriver* pd = career::DriverById(ctx.duel_rival_id);
                double aggr = pd ? pd->base_aggression : 0.0;
                std::string payloadKey; int variant;
                RivalDuelTemplate(bf.level, aggr, ctx.duel_won, ctx.race_clean, career::MailHash(id),
                                  payloadKey, variant);
                market::EmailDef m;
                m.id = id; m.from = ctx.duel_rival_name; m.org = "rival"; m.tag = 2; m.day = ctx.day; m.type = 0;
                emitMail(save, out, m, /*family=*/5, /*subtype=*/0, payloadKey, ctx.day, ctx.year, base,
                         ctx.duel_rival_name, "inbox_role_rival", variant);
                p2Sent++;
                bf.level = (bf.level < 5) ? bf.level + 1 : 5;   // cap 5 (inline)
                bf.last_year = ctx.year;
                bf.last_race_idx = raceCount;
            }
        }
    }

    // (7) Track history — Atenea (milestone family 1), LOWEST priority, after the clean
    //     streak. The next actionable event (ctx.next_event, filled in only by the other code path) lands within
    //     7 days AND you've already raced that SAME circuit (track_hash) with a podium (pos 1..3) before -> the paddock
    //     remembers it. With multiple podiums at that circuit it takes the BEST position (and ITS year); ties in position
    //     are resolved by order of appearance in race_log. subtype 13 = track_history. The id EMBEDS the
    //     EVENT's abs_day (not the tick's) -> one story per event (won't repeat until the next event at that
    //     circuit). With no podium history for that hash -> nothing (there's no "first time back" story to tell).
    if (ctx.next_event.days_away >= 0 && ctx.next_event.days_away <= 7) {
        int bestPos = 0, bestYear = 0;
        for (const auto& rl : save.race_log) {
            if (rl.track_hash != ctx.next_event.track_hash) continue;
            if (rl.pos < 1 || rl.pos > 3) continue;
            if (bestPos == 0 || rl.pos < bestPos) { bestPos = rl.pos; bestYear = rl.year; }
        }
        if (bestPos > 0) {
            int eventAbsDay = career::dateutil::AbsDayFromYearDoy(ctx.year, ctx.day) + ctx.next_event.days_away;
            career::MailParams p = withParam(withParam(withParam(base, "track", ctx.next_event.track),
                                             "pos", std::to_string(bestPos)),
                                             "year", std::to_string(bestYear));
            tryMilestone("track_history_" + std::to_string(eventAbsDay), /*subtype=*/13, "track_history", p);
        }
    }

    // An OFFER mail whose offer is NO longer live (signed/expired/withdrawn)
    // or was rejected -> gets answered=1: it stops being an "eternally pending" mail and becomes eligible for pruning. The link
    // between mail and offer is payload_key == OfferDef.id (that's how Family A emits it, above).
    for (auto& e : save.emails) {
        // The renewal doesn't live in GenerateOffers (it's born in the rollover, career_calendar.cpp) ->
        // this sync would mark it answered=1 on the very next pulse (choice 0, no resolvedKey), killing
        // the action right after delivering it, before the player could use it. It expires on its own at the next rollover,
        // which clears pending_renewals — and that same rollover closes any "renew_" from the old year still
        // unanswered (CloseStaleRenewalMails, career_calendar.cpp).
        if (e.id.rfind("renew_", 0) == 0) continue;
        if (e.family != 0 || e.answered) continue;
        bool live = false;
        const market::OfferDef* liveDef = nullptr;
        for (const auto& go : offers)
            if (go.def.id == e.payload_key && go.def.direct && !go.held) { live = true; liveDef = &go.def; break; }
        if (!live || save.declined_offers.count(e.payload_key) > 0) { e.answered = 1; continue; }
        // Backfills "negotiable" onto OFFER mails emitted before this param existed
        // (heals the player's current save in one pulse). Same source as query.market (NegoNegotiable) —
        // only touches mails missing the key; one already emitted with it is never rewritten.
        career::MailParams p = career::MailParamsDecode(e.params);
        bool hasNegotiable = false;
        for (const auto& kv : p) if (kv.first == "negotiable") { hasNegotiable = true; break; }
        if (!hasNegotiable && liveDef) {
            p.push_back({"negotiable", career::NegoNegotiable(*liveDef) ? "1" : "0"});
            e.params = career::MailParamsEncode(p);
        }
    }

    // Sync of EXPIRATIONS for the INVITE family (7) — same pattern as the OFFER sync above
    // (closes on its own with choice 0, no resolution). Two subtypes, two different clocks:
    //  · subtype 1 (event): the invite expires with the event's REAL window (ends_at, a UTC epoch stored
    //    in params at emission time) or if the event was already claimed/removed from the registry. The registry ONLY lives in the
    //    overlay (career_events.cpp) — an EMPTY registry (SpecialEventRegistryCount()==0) is ambiguous (did it really
    //    expire, or did the process/test just never load it?), so "removed from the registry" only counts when the
    //    registry is populated; otherwise the real clock (ends_at) remains the only source of closure.
    //  · subtype 0 (sub): unanswered + today is past the round's absday -> closes + frees sub_ride (the
    //    "one substitution at a time" gate). Already ACCEPTED (state 2, mail already answered with choice 1) and the absday passed
    //    > "race missed, no penalty": the mail is left as-is (untouched), only sub_ride is cleared (the
    //    real settle is the normal path; this is the safety catch if the player let the
    //    date pass without racing). Mail<->sub_ride correspondence is verified via a reconstructed id, the same
    //    defensive pattern as ApplySubRideAnswer (career_mail.cpp): it never touches anything but the ACTIVE substitution.
    {
        const int todayAbs = career::dateutil::AbsDayFromYearDoy(ctx.year, ctx.day);
        for (auto& e : save.emails) {
            if (e.family != 7 || e.answered) continue;
            if (e.subtype == 1) {
                std::string eventId, endsAtStr;
                for (const auto& kv : career::MailParamsDecode(e.params)) {
                    if (kv.first == "event_id") eventId = kv.second;
                    else if (kv.first == "ends_at") endsAtStr = kv.second;
                }
                bool claimed = !eventId.empty() && save.claimed_events.count(eventId) > 0;
                bool goneFromRegistry = !eventId.empty() && career::SpecialEventRegistryCount() > 0
                                        && career::SpecialEventById(eventId) == nullptr;
                bool pastDeadline = false;
                if (!endsAtStr.empty()) {
                    long endsAt = atol(endsAtStr.c_str());
                    if (endsAt > 0 && endsAt < (long)time(nullptr)) pastDeadline = true;
                }
                if (claimed || goneFromRegistry || pastDeadline) { e.answered = 1; e.choice = 0; }
            } else if (e.subtype == 0 && save.sub_ride.state == 1) {
                std::string expectedId = "subride_" + save.sub_ride.series_id + "_"
                                        + std::to_string(save.sub_ride.event_absday);
                if (e.id == expectedId && todayAbs > save.sub_ride.event_absday) {
                    e.answered = 1; e.choice = 0;
                    save.sub_ride = SubRide{};
                }
            }
        }
        // state 2 (ACCEPTED) with the round already past: "race missed" -> clears sub_ride without touching the mail
        // (already answered=1/choice=1 from ApplySubRideAnswer) or penalizing the player. It lives outside the loop
        // above because it doesn't depend on any unanswered mail — it's a direct cleanup of save.sub_ride.
        if (save.sub_ride.state == 2 && todayAbs > save.sub_ride.event_absday) save.sub_ride = SubRide{};
    }

    // Retention: housekeeping of the INBOX at generation time, NOT the cap on OFFERS (the Market's offer
    //      set is still uncapped). Prunes the oldest READ+non-actionable mail; never unread or pending. ----
    pruneInbox(save);

    return out;
}

// ============================ Sponsor-card mail family ============================
// DEDICATED family, reuses the SAME mailExists/emitMail dedup+persist idiom as GenerateCareerEmails above.
// family=2 (sponsor voice — the inbox already renders this family; no new inbox family needed). payload_key
// is a TR template key (bilingual-by-TR, body never persisted). id = "card_"+kind+"_"+cardId+"_"+year+"_"+day
// > dedup content-id (the SAME event the SAME day doesn't duplicate; a different day/kind/card is a fresh mail).
// kind: 0 = purchase (buy, sp_mail_card_buy) · 1 = clause triggered (sp_mail_card_clause) ·
//       2 = expiration/loss (sp_mail_card_expire). Returns the NEW EmailDef(s) (empty if deduped).
std::vector<market::EmailDef> GenerateCardEmails(SaveData& save, const std::string& cardId,
                                                 int year, int day, int kind) {
    std::vector<market::EmailDef> out;
    if (cardId.empty()) return out;
    std::string payloadKey;
    switch (kind) {
        case 0:  payloadKey = "sp_mail_card_buy";    break;
        case 1:  payloadKey = "sp_mail_card_clause";  break;
        default: payloadKey = "sp_mail_card_expire"; break;   // kind 2 (and anything else) -> expiration/loss
    }
    std::string id = "card_" + std::to_string(kind) + "_" + cardId + "_"
                    + std::to_string(year) + "_" + std::to_string(day);
    if (mailExists(save, id)) return out;              // content-id dedup (same pattern as elsewhere)
    // The brand signs with its real catalog name (CardById); {brand}/{player}
    // feed the template. cardId falls back if the id doesn't resolve (defensive; shouldn't happen).
    const career::CardDef* cd = career::CardById(cardId);
    std::string brand = cd ? cd->name : cardId;
    career::MailParams cp = { {"brand", brand} };
    std::string pf = career::MailFirstName(save.player.name);   // GenerateCardEmails doesn't receive ctx -> from the save
    if (!pf.empty()) cp.push_back({"player", pf});
    market::EmailDef m;
    m.id = id; m.from = cardId; m.org = "sponsor"; m.tag = 0; m.day = day; m.type = 0;
    m.subject = m.subject_en = payloadKey;            // flavor in memory; the persisted thing is payload_key
    emitMail(save, out, m, /*family=*/2, /*subtype=*/kind, payloadKey, day, year,
             cp, /*fromName=*/brand, /*fromRoleKey=*/"inbox_sponsor");   // family 2 = sponsor (shared voice)
    pruneInbox(save);                                  // same housekeeping as GenerateCareerEmails
    return out;
}

// ============================ One-off substitution ============================

// Next ACTIONABLE round on 'sid''s calendar from 'cc''s cursor — the first one with doy > cursor_doy
// IN THIS YEAR (doesn't re-project into next year, same scope as the rest of the weekly draw). Returns false if the
// season for that series is already exhausted this year (all its rounds are behind).
// CalendarForSeriesInYear(sid, cc.year) -- dates PROJECTED onto 'cc''s current year (the year is already
// available in scope, passed in by the caller); CalendarForSeries (raw) only matches in kStartYear.
static bool subNextRound(const std::string& sid, const CareerCalendar& cc, CalRound& out) {
    std::vector<CalRound> cal = CalendarForSeriesInYear(sid, cc.year);
    int best = 0x7fffffff; int bi = -1;
    for (size_t i = 0; i < cal.size(); i++) {
        if (cal[i].doy <= 0 || cal[i].doy <= cc.cursor_doy) continue;   // no date, or already past/today
        if (cal[i].doy < best) { best = cal[i].doy; bi = (int)i; }
    }
    if (bi < 0) return false;
    out = cal[bi];
    return true;
}

// Same criterion as career_market.cpp::seriesIsGated (private to that unit, not exported) — a series is
// GATED iff it appears as the DESTINATION (unlocks) of some qualifier.
static bool subSeriesGated(const std::string& sid) {
    for (const auto& q : AllSeries()) if (q.unlocks == sid) return true;
    return false;
}

bool SubRideWeeklyDraw(SaveData& save, const CareerCalendar& cc, const std::string& playerName,
                       std::vector<std::string>* outMailIds) {
    // One substitution at a time: no draw with an active/offered sub_ride, and none with an
    // unanswered family-7 (INVITE) invitation — the player still has a pending decision on the table.
    if (save.sub_ride.state != 0) return false;
    for (const auto& e : save.emails) if (e.family == 7 && !e.answered) return false;

    // 18/09 — THE DRAW REMAINS WEEKLY EVEN THOUGH THE PULSE IS DAILY, and it is anchored to the CALENDAR.
    //
    // It is the only phase of the pulse that depended on the rhythm instead of the date: the gate is decided per week
    // (`career_seed|year|week`), so with a daily pulse the same week was drawn SEVEN times. The
    // gate would not open more often — the hash is the same all seven days — but within a winning
    // week there would be seven attempts to find a candidate instead of one, and the real substitution rate
    // would go above the 4 % design (MK_SUB_WEEK_PCT).
    //
    // The cure is to anchor it to the calendar and not to the pulse: it is drawn on the day that OPENS each weekly bucket. Besides
    // restoring the single attempt, it makes it reproducible — before it fell on day `d % 7` of the jump, that is, it
    // depended on when you had started to advance.
    // The bucket is `cursor_doy / 7`, and the day that OPENS it is `7*bucket` — except bucket 0, which starts on day 1
    // because doy runs from 1. That leaves the 53 buckets of the year with one attempt each, and the hash KEY
    // does not change: the measured seeds are still valid.
    if (cc.cursor_doy % 7 != 0 && cc.cursor_doy != 1) return false;
    int week = cc.cursor_doy / 7;
    std::string key = std::to_string(save.career_seed) + "|" + std::to_string(cc.year) + "|" + std::to_string(week);
    unsigned h = career::MailHash(key);
    if ((int)(h % 100u) >= bal::MK_SUB_WEEK_PCT) return false;   // no substitution this week

    // candidates: reachable (same standalone/branch/era filter as SillySeasonFreeSeats), level within the
    // player's orbit, gate unlocked, rating within a WORKS seat's fit band, next round with
    // enough lead time and not clashing with the player's OWN next event.
    const int fp = save.prog.frontier_phase;
    const int todayAbs = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);
    const int ownNextAbs = NextEventAbsDay(cc, fp, &save.unlocked_gates, &save);   //
    std::vector<const SeriesDef*> cands;
    std::vector<CalRound> candRound;
    for (const auto& sd : AllSeries()) {
        if (sd.level < fp - 1 || sd.level > fp + 1) continue;
        if (sd.kind == "standalone") continue;
        if (IsEventOnlySeries(sd)) continue;             // 01/10
        if (!sd.branch_of.empty()) continue;
        if (IsEraMemorySeries(sd)) continue;
        if (subSeriesGated(sd.id) && !save.unlocked_gates.count(sd.id)) continue;
        double reqRep = career::ReqRepRating(sd.level, /*works=*/true);
        if (save.prog.rating < reqRep - bal::MK_FIT_UNDER_MARGIN || save.prog.rating > reqRep + bal::MK_FIT_OVER_MARGIN) continue;
        CalRound rd;
        if (!subNextRound(sd.id, cc, rd)) continue;
        int roundAbs = career::dateutil::AbsDayFromYearDoy(cc.year, rd.doy);
        if (roundAbs - todayAbs < bal::MK_SUB_LEAD_MIN_DAYS) continue;
        if (ownNextAbs >= 0 && roundAbs == ownNextAbs) continue;
        cands.push_back(&sd);
        candRound.push_back(rd);
    }
    if (cands.empty()) return false;

    // series picked by the SAME draw hash (same pattern as SillySeasonFreeSeats): wraps around until it finds a
    // candidate with a real-name OCCUPIED seat (SeatsForClass) — without that there's nobody to "substitute for".
    for (size_t k = 0; k < cands.size(); k++) {
        size_t idx = (h / 100u + k) % cands.size();
        const SeriesDef* sd = cands[idx];

        // With your own ACTIVE contract in this series there's nobody to substitute for — you're already racing there.
        if (ContractForSeries(save, sd->id)) continue;

        std::vector<Seat> seats = SeatsForClass(sd->id, save);
        int seatIdx = -1; std::string teamLabel;
        for (size_t si = 0; si < seats.size(); si++) {
            if (seats[si].open || seats[si].team_label.empty()) continue;
            seatIdx = (int)si; teamLabel = seats[si].team_label; break;
        }
        if (seatIdx < 0) continue;   // no named occupied seat -> next candidate

        int absday = career::dateutil::AbsDayFromYearDoy(cc.year, candRound[idx].doy);
        // mail from the seat's team principal (family 7 INVITE, subtype 0) — same emitMail/
        // mailExists pattern as the rest of the generator; dedup by content-derived id (series+round).
        std::string id = "subride_" + sd->id + "_" + std::to_string(absday);
        // A dedup hit does NOT pin sub_ride — without this, an already-used id would leave a
        // sub_ride.state=1 with no mail ever emitted, and the "one substitution at a time" gate above
        // would block every future draw forever (a softlock). The next candidate is tried instead.
        if (mailExists(save, id)) continue;

        long fee = bal::MK_SUB_FEE_BASE * (long)sd->level;
        // Also freezes the chosen seat's TEAM label (teamLabel, already in hand) into the
        // sub_ride -> SubRideSettle records the warmth without recomputing the seat. 6th field of the persisted line.
        save.sub_ride = SubRide{ sd->id, seatIdx, absday, fee, /*state=*/1, teamLabel };   // pinea SOLO junto con el correo real

        career::MailParams p;
        std::string pf = career::MailFirstName(playerName);
        if (!pf.empty()) p.push_back({"player", pf});
        p.push_back({"team", teamLabel});
        p.push_back({"series", sd->display});
        p.push_back({"track", candRound[idx].track_name});
        p.push_back({"fee", career::MailFmtCredits(fee)});
        market::EmailDef m;
        m.id = id; m.from = teamLabel; m.org = "team"; m.tag = 1; m.day = cc.cursor_doy; m.type = 0;
        std::vector<market::EmailDef> unused;
        emitMail(save, unused, m, /*family=*/7, /*subtype=*/0, /*payloadKey=*/"invite_sub", cc.cursor_doy, cc.year,
                 p, /*fromName=*/teamLabel, /*fromRoleKey=*/"inbox_role_teamprincipal",
                 /*forceVariant=*/(int)(h % 2u));
        // pruneInbox is NOT a static local of GenerateCareerEmails — it's a function of the
        // translation unit (anonymous namespace, career_contract.cpp) defined earlier in the same file,
        // so it's accessible unqualified here (same pattern as GenerateCareerEmails/GenerateCardEmails).
        pruneInbox(save);
        if (outMailIds) outMailIds->push_back(id);   // RunWorldTick threads it through res.generated_mail_ids
        return true;
    }
    return false;   // none of the candidates (with a contract not your own) had a live seat -> no substitution this week
}

} // namespace career
