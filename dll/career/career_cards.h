#pragma once
// career_cards.h -- sponsor-card system (roguelike deck-building style). 2026-07-11 spec /
// Pure engine: no ImGui, no overlay globals. Catalog lives in career_cards_data.cpp.
#include <string>
#include <utility>
#include <vector>
#include "career_save.h"  // SignedCard, CardShopState
#include "career_engine.h"  // RaceResult

namespace career {

enum CardLife { CARD_LIFE_PERM = 0, CARD_LIFE_RACES = 1, CARD_LIFE_USES = 2 };

enum CardHook {
    CARD_HOOK_PRIZE_MULT = 0,   // income.prize_mult  (multiplies the prize if it fires)
    CARD_HOOK_INCOME_BONUS,     // income.bonus       (flat credits if it fires)
    CARD_HOOK_COST_TYRES,       // cost.*             (v = discount fraction 0..1, passive)
    CARD_HOOK_COST_FUEL,
    CARD_HOOK_COST_TRAVEL,
    CARD_HOOK_COST_ENTRY,
    CARD_HOOK_COST_RENT,
    CARD_HOOK_XP_MULT,          // xp.mult
    CARD_HOOK_REP_MULT,         // rep.mult -- multiplies the REP gained at settle (Catalog v2, allows <1)
    CARD_HOOK_SPECIAL,          // effect keyed by record (special_id)
    CARD_HOOK_COUNT
};

// Catalog v2 (2026-07-11): an EXTRA effect on a card -- same hook vocabulary as the main
// fx (hook+v), with no clause/upgrade of its own. 0-2 per card; usually the negative counterweight
// (mult <1 or a negative bonus = a per-race fee). CARD_HOOK_SPECIAL never shows up here (specials only
// live in the main fx, via special_id).
struct CardFx { int hook = CARD_HOOK_SPECIAL; double v = 0.0; };

// Triggers: "passive" or the settle's v2 vocabulary:
// win | podium | top8 | clean | damagefree | tyrewear | fueluse | endur

struct CardClause {             // kind 0 = no clause
    int  kind  = 0;             // 1 = dnf_fine . 2 = notop10_lose
    long fine  = 0;             // fine, in credits (dnf_fine)
    int  races = 0;             // race window (notop10_lose)
};
struct CardUpgrade {
    int    levels = 0;          // EXTRA levels (0 = not upgradable; max 2)
    int    after  = 0;          // need = after * current_level (ACTIVE races)
    long   cost   = 0;          // cost = cost * current_level
    double v2 = 0.0, v3 = 0.0;  // effect magnitude at N2 / N3
};
struct CardDef {
    std::string id, name;
    std::string tag_es, tag_en;     // brand tagline
    int rarity    = 0;              // 0..4 = local/regional/national/international/elite
    int life_kind = CARD_LIFE_PERM;
    int life_n    = 0;              // races or uses (0 if permanent)
    std::string trigger;            // "passive" | kind v2
    int    hook = CARD_HOOK_SPECIAL;
    double v    = 0.0;              // magnitude N1 (0.0 if special)
    CardUpgrade upgrade;
    CardClause  clause;
    std::string special_id;         // "" | shop.free_reroll | shop.extra_offer | shop.extra_buy
                                    //    | market.offer_quality | nego.leverage
    std::string special_es, special_en;   // special-effect text (only if special)
    // Catalog v2: 0-2 EXTRA effects (same trigger as the main fx -- the card fires AS A
    // UNIT). Field ADDED AT THE END on purpose: the 27 aggregate initializers in career_cards_data.cpp
    // don't list it -> they default to {} (empty) untouched.
    std::vector<CardFx> extras;
};

extern std::vector<CardDef> CARD_DEFS;              // career_cards_data.cpp (the original 27 at launch)

// 0.7.0: every card has to EARN money in its best realistic case (see
// test_card_best_case_pays, career_test.cpp). Those not bought for money -- their main effect is XP,
// REP or a special -- or those that lose money ON PURPOSE go in this list, each with its reason. A
// card that loses money and is not here breaks the harness.
struct CardRoiNote { const char* id; const char* reason; };
extern const std::vector<CardRoiNote> CARD_ROI_BY_DESIGN;   // career_cards_data.cpp
const CardDef* CardById(const std::string& id);     // nullptr if it doesn't exist
long CardCost(const CardDef& d);                    // bal::CARD_COST_BY_RARITY[rarity]

struct CardUpgradeInfo { int to_level = 0; long cost = 0; int need = 0, have = 0; bool ready = false; };

int    CardMaxLevel(const CardDef& d);
double CardFxV(const CardDef& d, int level);
bool   CardUpgradeNext(const SignedCard& sc, CardUpgradeInfo& out);
long   CardSellValue(const SignedCard& sc);
bool   CardSpecialActive(const std::vector<SignedCard>& cards, const char* specialId);
int    CardShopMaxBuys(const std::vector<SignedCard>& cards);
int    CardShopOfferCount(const std::vector<SignedCard>& cards);
long   CardShopRerollCost(int rerolls, const std::vector<SignedCard>& cards);
int    CardSlotsUnlocked(int phase);
std::vector<std::string> CardShopDraw(const std::vector<SignedCard>& owned, int phase,
                                      const std::string& seedStr, int count);

// Pure wallet and shop operations (they mutate SaveData& d, return a bool for success)
bool CardOpBuy(SaveData& d, const std::string& id);
bool CardOpReroll(SaveData& d);
bool CardOpShopClose(SaveData& d);
bool CardOpSlot(SaveData& d, const std::string& id, int slot);
bool CardOpBench(SaveData& d, const std::string& id);
bool CardOpUpgrade(SaveData& d, const std::string& id);
bool CardOpSell(SaveData& d, const std::string& id);
// marks the ENTIRE wallet as seen (cards_seen = cards.size()); the tab's badge
// counts the cards above that marker (bought and not yet viewed) + upgrades that are ready.
bool CardOpSeen(SaveData& d);
// (drag&drop from the design mockup): ATOMIC reassignment of several slots at once (swaps and
// insertions). moves = (card_id -> destination slot; -1 = benched). Validates EVERYTHING against a copy
// (existing ids, slots within the gating, no duplicate slot after applying) and only then commits.
bool CardOpArrange(SaveData& d, const std::vector<std::pair<std::string, int>>& moves);
void CardOpSeasonReset(SaveData& d);
// erase-on-cede EXTRACTED from MarketUiAccept's negotiation give-up path into a pure,
// testable op. Looks up `id`; if it exists AND its rarity matches `expectedRarity` (a defense against a tierRank
// that's gone out of sync on the client), it DELETES it from the wallet and returns true. It NEVER pays/refunds credits (a
// give-up isn't a sale -> don't use CardOpSell here). Mismatched rarity / nonexistent id -> false, wallet
// left untouched. `expectedRarity` < 0 ("none offered") is the CALLER's responsibility: don't call this op then.
bool CardOpCede(SaveData& d, const std::string& id, int expectedRarity);

// (retiring the old sponsor system): RaceTelemetry used to live in career_sponsor.h (now deleted) --
// it's MOVED here byte-identical, it's the SAME telemetry that used to feed SponsorObjectiveRaceHit and now feeds
// CardFlagsFrom (2026-07-11 spec). Value-typed race telemetry the overlay fills from ShmSnap at
// Settle time (semantics to watch for):
//  maxTyreWear:        worst-corner mTyreWear (0=fresh, 1=fully worn) -- "low wear" tests BELOW SPONSOR_TYREWEAR_MAX
//  maxComponentDamage: worst of aero/engine/brake/susp (0..1) -- "damage-free" tests BELOW SPONSOR_DAMAGE_THRESHOLD
//  fuelRemainFrac:     mFuelLevel fraction remaining at finish (0..1) -- "fuel to spare" tests ABOVE SPONSOR_FUEL_MIN_FRAC
struct RaceTelemetry { float maxTyreWear = 0.0f; float maxComponentDamage = 0.0f; float fuelRemainFrac = 1.0f; };

struct CardFlags {
    bool win = false, podium = false, top8 = false, top10 = false, clean = false,
         damagefree = false, tyrewear = false, fueluse = false, endur = false, dnf = false;
};
CardFlags CardFlagsFrom(const RaceResult& r, const RaceTelemetry& tel, bool isEnduranceEvent);

struct CardCosts { long entry = 0, tyres = 0, fuel = 0, travel = 0, rent = 0; };
// 19/09: `amount` = what THAT line really contributes to the prize (₡, can be negative). The income lines
// add up exactly to prize_final − prize_base, so Results no longer splits by eye (see CardsSettle).
struct CardIncomeLine { std::string id; int level = 1; bool mult = false; double v = 0.0; std::string trigger; long amount = 0; };
struct CardCostDisc   { std::string key; long base = 0; double disc = 0.0; long final_ = 0; };
struct CardsSettleReport {
    long bonus = 0; double mult = 1.0, xp_mult = 1.0, rep_mult = 1.0;   // mult = EFFECTIVE factor of the prize (19/09)
    long prize_base = 0;       // prize BEFORE bonus/mult (echoes the prizeBase parameter; used to reconstruct the per-card delta)
    long prize_final = 0;
    long rent_base = 0;        // rental cost BEFORE the card discount (echoes costs.rent)
    long rent_final = 0; double rent_disc = 0.0;
    long fines_total = 0;
    std::vector<CardIncomeLine> income;
    std::vector<CardCostDisc>   costs;              // only entries with a base > 0
    std::vector<std::pair<std::string, long>> fines; // (card_id, fine)
    std::vector<std::string> fired, expired, lost;   // card_ids
    long credits_delta = 0;    // NET delta to apply ON TOP OF the base Settle
};
CardsSettleReport CardsSettle(std::vector<SignedCard>& cards, const CardFlags& f,
                              long prizeBase, const CardCosts& costs);

} // namespace career
