#include "career_cards.h"
#include "career_balance.h"
#include "career_econ.h"   // EconLog -- a [money] ledger entry on every credits mutation (buy/reroll/upgrade/sell)
#include <cmath>   // Cards v2 plumbing: llround -- bonus/prize deltas can now be negative (fees)
#include <algorithm>   // 19/09: stable_sort of the prize raises (decreasing stacking)

namespace career {

const CardDef* CardById(const std::string& id) {
    for (const auto& c : CARD_DEFS) if (c.id == id) return &c;
    return nullptr;
}

long CardCost(const CardDef& d) {
    int r = d.rarity; if (r < 0) r = 0; if (r > 4) r = 4;
    return bal::CARD_COST_BY_RARITY[r];
}

int CardMaxLevel(const CardDef& d) { return 1 + d.upgrade.levels; }

double CardFxV(const CardDef& d, int level) {
    if (level >= 3 && d.upgrade.levels >= 2) return d.upgrade.v3;
    if (level >= 2 && d.upgrade.levels >= 1) return d.upgrade.v2;
    return d.v;
}

bool CardUpgradeNext(const SignedCard& sc, CardUpgradeInfo& out) {
    const CardDef* d = CardById(sc.card_id);
    if (!d || d->upgrade.levels <= 0 || sc.level >= CardMaxLevel(*d)) return false;
    out.to_level = sc.level + 1;
    out.need = d->upgrade.after * sc.level;
    out.cost = d->upgrade.cost * sc.level;
    out.have = sc.races_active < out.need ? sc.races_active : out.need;
    out.ready = sc.races_active >= out.need;
    return true;
}

long CardSellValue(const SignedCard& sc) {
    long base = sc.invested;
    if (base <= 0) { const CardDef* d = CardById(sc.card_id); base = d ? CardCost(*d) : 0; }
    double v = (double)base * bal::CARD_SELL_PCT;
    return (long)(v / 10.0 + 0.5) * 10;
}

bool CardSpecialActive(const std::vector<SignedCard>& cards, const char* specialId) {
    for (const auto& sc : cards) {
        if (sc.slot < 0) continue;                       // benched doesn't count
        const CardDef* d = CardById(sc.card_id);
        if (d && d->special_id == specialId) return true;
    }
    return false;
}

int CardShopMaxBuys(const std::vector<SignedCard>& cards) {
    return bal::CARD_MAX_BUYS + (CardSpecialActive(cards, "shop.extra_buy") ? 1 : 0);
}
int CardShopOfferCount(const std::vector<SignedCard>& cards) {
    return bal::CARD_SHOP_SIZE + (CardSpecialActive(cards, "shop.extra_offer") ? 1 : 0);
}
long CardShopRerollCost(int rerolls, const std::vector<SignedCard>& cards) {
    bool freeFirst = CardSpecialActive(cards, "shop.free_reroll");
    if (rerolls == 0 && freeFirst) return 0;
    int paid = freeFirst ? rerolls - 1 : rerolls;
    if (paid < 0) paid = 0;
    long c = bal::CARD_REROLL_BASE;
    for (int i = 0; i < paid; i++) c *= bal::CARD_REROLL_GROWTH;
    return c;
}

int CardSlotsUnlocked(int phase) {
    if (phase < 1) phase = 1; if (phase > 7) phase = 7;
    return bal::CARD_SLOTS_BY_PHASE[phase];
}

static unsigned CardHashFnv(const std::string& s) {
    unsigned h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

// mulberry32 -- the same generator as the design prototype (app/design/data_cards.jsx)
static double CardRngNext(unsigned& a) {
    a += 0x6D2B79F5u;
    unsigned t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (double)((t ^ (t >> 14))) / 4294967296.0;
}

std::vector<std::string> CardShopDraw(const std::vector<SignedCard>& owned, int phase,
                                      const std::string& seedStr, int count) {
    unsigned rng = CardHashFnv(seedStr);
    if (phase < 1) phase = 1; if (phase > 7) phase = 7;
    const int* w = bal::CARD_WEIGHTS_BY_PHASE[phase];
    auto isTaken = [&](const std::string& id, const std::vector<std::string>& offers) {
        for (const auto& sc : owned) if (sc.card_id == id) return true;
        for (const auto& o : offers)  if (o == id) return true;
        return false;
    };
    std::vector<std::string> offers;
    for (int n = 0; n < count; n++) {
        std::string pick;
        for (int attempt = 0; attempt < 12 && pick.empty(); attempt++) {
            double roll = CardRngNext(rng) * 100.0; int acc = 0; int rarity = 0;
            for (int i = 0; i < 5; i++) { acc += w[i]; if (roll < (double)acc) { rarity = i; break; } }
            std::vector<const CardDef*> pool;
            for (const auto& c : CARD_DEFS)
                if (c.rarity == rarity && !isTaken(c.id, offers)) pool.push_back(&c);
            if (!pool.empty()) pick = pool[(size_t)(CardRngNext(rng) * (double)pool.size()) % pool.size()]->id;
        }
        if (pick.empty()) {                              // fallback: any rarity
            std::vector<const CardDef*> any;
            for (const auto& c : CARD_DEFS) if (!isTaken(c.id, offers)) any.push_back(&c);
            if (any.empty()) break;
            pick = any[(size_t)(CardRngNext(rng) * (double)any.size()) % any.size()]->id;
        }
        offers.push_back(pick);
    }
    return offers;
}

CardFlags CardFlagsFrom(const RaceResult& r, const RaceTelemetry& tel, bool isEnduranceEvent) {
    CardFlags f;
    f.dnf    = !r.finished;
    f.win    = r.finished && r.pos == 1;
    f.podium = r.finished && r.pos <= 3;
    f.top8   = r.finished && r.pos <= 8;
    f.top10  = r.finished && r.pos <= 10;
    f.clean  = r.finished && r.clean;
    f.endur  = r.finished && isEnduranceEvent;
    f.damagefree = r.finished && tel.maxComponentDamage < (float)bal::SPONSOR_DAMAGE_THRESHOLD;
    f.tyrewear   = r.finished && tel.maxTyreWear        < (float)bal::SPONSOR_TYREWEAR_MAX;
    f.fueluse    = r.finished && tel.fuelRemainFrac     > (float)bal::SPONSOR_FUEL_MIN_FRAC;
    return f;
}

static bool CardTriggerHits(const CardDef& d, const CardFlags& f) {
    const std::string& t = d.trigger;
    if (t == "passive")    return true;
    if (t == "win")        return f.win;
    if (t == "podium")     return f.podium;
    if (t == "top8")       return f.top8;
    if (t == "clean")      return f.clean;
    if (t == "damagefree") return f.damagefree;
    if (t == "tyrewear")   return f.tyrewear;
    if (t == "fueluse")    return f.fueluse;
    if (t == "endur")      return f.endur;
    return false;
}

// Catalog v2: a card's effects = a main fx + extras, using the SAME hook vocabulary. The card
// fires AS A UNIT (a single CardTriggerHits gate upstream in the caller) -- this helper only enumerates WHAT
// to apply once it's already known to have fired. The main effect scales with level (CardFxV); the extras have NO
// upgrade of their own, they carry their fixed v.
struct CardEffect { int hook; double v; };
static std::vector<CardEffect> CardAllEffects(const CardDef& d, int level) {
    std::vector<CardEffect> out;
    out.reserve(1 + d.extras.size());
    out.push_back({ d.hook, CardFxV(d, level) });
    for (const auto& e : d.extras) out.push_back({ e.hook, e.v });
    return out;
}

CardsSettleReport CardsSettle(std::vector<SignedCard>& cards, const CardFlags& f,
                              long prizeBase, const CardCosts& costs) {
    CardsSettleReport rep;
    // 1) walk ACTIVE cards: income + xp + rep + mark triggers (main + extras, as a unit) ---
    std::vector<bool> firedIdx(cards.size(), false);
    for (size_t i = 0; i < cards.size(); i++) {
        const SignedCard& sc = cards[i];
        if (sc.slot < 0) continue;
        const CardDef* d = CardById(sc.card_id);
        if (!d || !CardTriggerHits(*d, f)) continue;
        bool any = false;
        for (const auto& e : CardAllEffects(*d, sc.level)) {
            switch (e.hook) {
            case CARD_HOOK_INCOME_BONUS:
                // llround: v can be NEGATIVE (a per-race fee, Catalog v2) -- (long)(v+0.5) truncated
                // negatives incorrectly (e.g. -2.5 -> -2 instead of -3).
                rep.bonus += (long)llround(e.v); any = true;
                rep.income.push_back({ sc.card_id, sc.level, false, e.v, d->trigger });
                break;
            case CARD_HOOK_PRIZE_MULT:
                // 19/09: it is NO longer multiplied here (it chained: +15 M in one race). It is applied below, in order.
                any = true;
                rep.income.push_back({ sc.card_id, sc.level, true, e.v, d->trigger });
                break;
            case CARD_HOOK_XP_MULT:
                rep.xp_mult *= e.v; any = true;
                break;
            case CARD_HOOK_REP_MULT:
                rep.rep_mult *= e.v; any = true;
                break;
            case CARD_HOOK_COST_TYRES: case CARD_HOOK_COST_FUEL: case CARD_HOOK_COST_TRAVEL:
            case CARD_HOOK_COST_ENTRY: case CARD_HOOK_COST_RENT:
                any = true;                                     // computed below
                break;
            default: break;                                     // specials: no-settle
            }
        }
        if (any) firedIdx[i] = true;
    }
    // 2-3) additives -> multipliers. llround (not a truncated "+0.5"): bonus can be negative (a fee) and
    // prize_final can drop below the base prize -- but never below 0 (you never "owe" money to Settle).
    // 19/09 (tester: +15 M in ONE race = 226,600 × 2.5 × 1.6 × 3 × 2 × 2.8). The multipliers that RAISE
    // stack with diminishing returns (Josh's decision): they are sorted by their increment (v − 1) from largest to
    // smallest and the i-th counts × CARD_PRIZE_STACK_DECAY^i. A single card pays what it prints; the cuts
    // (v < 1) multiply as always. The prize is built STEP BY STEP, one income line per step, and each
    // line keeps what its step moved (already rounded): that way the lines add up EXACTLY to the final prize and
    // Results shows what each card contributed, not an approximate split. Order of the steps: positive bonuses,
    // fees (so the floor at 0 only trims the fees), raises from largest to smallest, cuts.
    rep.prize_base  = prizeBase;
    {
        std::vector<size_t> bonos, cuotas, subidas, rebajas;
        for (size_t k = 0; k < rep.income.size(); k++) {
            const CardIncomeLine& in = rep.income[k];
            if (!in.mult)           (llround(in.v) >= 0 ? bonos : cuotas).push_back(k);
            else if (in.v > 1.0)    subidas.push_back(k);
            else if (in.v < 1.0)    rebajas.push_back(k);   // v == 1: moves nothing, its line stays at 0
        }
        std::stable_sort(subidas.begin(), subidas.end(),
                         [&](size_t a, size_t b) { return rep.income[a].v > rep.income[b].v; });
        double acum = (double)prizeBase;   // the exact prize after each step
        long pagado = prizeBase;           // ...rounded and not below 0: what is really paid
        auto paso = [&](size_t k, double nuevo) {
            acum = nuevo;
            long r = (long)llround(acum);
            if (r < 0) r = 0;
            rep.income[k].amount = r - pagado;
            pagado = r;
        };
        for (size_t k : bonos)  paso(k, acum + (double)llround(rep.income[k].v));
        for (size_t k : cuotas) paso(k, acum + (double)llround(rep.income[k].v));
        const double base = acum > 0.0 ? acum : 0.0;   // the base of the multipliers (prize + fixed, >= 0)
        acum = base;
        double extra = 0.0, peso = 1.0;
        for (size_t k : subidas) {
            extra += (rep.income[k].v - 1.0) * peso;
            peso  *= bal::CARD_PRIZE_STACK_DECAY;
            paso(k, base * (1.0 + extra));             // the first: base × v, identical to a single card
        }
        double recorte = 1.0;
        for (size_t k : rebajas) { recorte *= rep.income[k].v; paso(k, acum * rep.income[k].v); }
        rep.mult        = (1.0 + extra) * recorte;
        rep.prize_final = pagado;
    }
    long delta = rep.prize_final - prizeBase;
    // 4) capped per-match discounts, cost never negative (main + extras of every card) ---
    struct Bucket { const char* key; long base; int hook; };
    const Bucket buckets[] = {
        { "entry",  costs.entry,  CARD_HOOK_COST_ENTRY  },
        { "tyres",  costs.tyres,  CARD_HOOK_COST_TYRES  },
        { "fuel",   costs.fuel,   CARD_HOOK_COST_FUEL   },
        { "travel", costs.travel, CARD_HOOK_COST_TRAVEL },
    };
    for (const auto& b : buckets) {
        if (b.base <= 0) continue;
        double disc = 0.0;
        for (const auto& sc : cards) {
            if (sc.slot < 0) continue;
            const CardDef* d = CardById(sc.card_id);
            if (!d || !CardTriggerHits(*d, f)) continue;
            for (const auto& e : CardAllEffects(*d, sc.level)) if (e.hook == b.hook) disc += e.v;
        }
        if (disc > bal::CARD_DISCOUNT_CAP) disc = bal::CARD_DISCOUNT_CAP;
        long fin = (long)((double)b.base * (1.0 - disc) + 0.5);
        if (fin < 0) fin = 0;
        rep.costs.push_back({ b.key, b.base, disc, fin });
        delta += b.base - fin;                                 // savings
    }
    rep.rent_base  = costs.rent;
    rep.rent_final = costs.rent;
    if (costs.rent > 0) {
        double disc = 0.0;
        for (const auto& sc : cards) {
            if (sc.slot < 0) continue;
            const CardDef* d = CardById(sc.card_id);
            if (!d || !CardTriggerHits(*d, f)) continue;
            for (const auto& e : CardAllEffects(*d, sc.level)) if (e.hook == CARD_HOOK_COST_RENT) disc += e.v;
        }
        if (disc > bal::CARD_DISCOUNT_CAP) disc = bal::CARD_DISCOUNT_CAP;
        rep.rent_disc = disc;
        rep.rent_final = (long)((double)costs.rent * (1.0 - disc) + 0.5);
        if (rep.rent_final < 0) rep.rent_final = 0;
        delta += costs.rent - rep.rent_final;
    }
    // 5) lifetime tick (active cards only) + 6) clauses ---
    std::vector<SignedCard> next;
    for (size_t i = 0; i < cards.size(); i++) {
        SignedCard sc = cards[i];
        if (sc.slot < 0) { next.push_back(sc); continue; }     // bench frozen
        const CardDef* d = CardById(sc.card_id);
        if (!d) { next.push_back(sc); continue; }
        sc.races_active++;
        if (firedIdx[i]) rep.fired.push_back(sc.card_id);
        if (d->life_kind == CARD_LIFE_RACES && sc.races_left > 0) sc.races_left--;
        if (d->life_kind == CARD_LIFE_USES && firedIdx[i] && sc.uses_left > 0) sc.uses_left--;
        bool drop = false;
        if (d->clause.kind == 1 && f.dnf) {                    // dnf_fine
            rep.fines.push_back({ sc.card_id, d->clause.fine });
            rep.fines_total += d->clause.fine;
        }
        if (d->clause.kind == 2) {                             // notop10_lose
            sc.clause_miss = (f.dnf || !f.top10) ? sc.clause_miss + 1 : 0;
            if (sc.clause_miss >= d->clause.races) { rep.lost.push_back(sc.card_id); drop = true; }
        }
        if (!drop && ((d->life_kind == CARD_LIFE_RACES && sc.races_left <= 0) ||
                      (d->life_kind == CARD_LIFE_USES  && sc.uses_left  <= 0))) {
            rep.expired.push_back(sc.card_id); drop = true;
        }
        if (!drop) next.push_back(sc);
    }
    cards.swap(next);
    delta -= rep.fines_total;
    rep.credits_delta = delta;
    return rep;
}

// Pure wallet and shop operations
static SignedCard* CardFind(SaveData& d, const std::string& id) {
    for (auto& sc : d.cards) if (sc.card_id == id) return &sc;
    return nullptr;
}

bool CardOpBuy(SaveData& d, const std::string& id) {
    if (!d.card_shop.open) return false;
    bool offered = false;
    for (const auto& o : d.card_shop.offers) if (o == id) offered = true;
    for (const auto& b : d.card_shop.bought_ids) if (b == id) return false;
    if (!offered) return false;
    if (d.card_shop.bought >= CardShopMaxBuys(d.cards)) return false;
    const CardDef* def = CardById(id);
    if (!def) return false;
    long price = CardCost(*def);
    if (d.prog.credits < price) return false;
    d.prog.credits -= price;
    EconLog("card_buy", -price, d.prog.credits);   //
    SignedCard sc; sc.card_id = id; sc.invested = price;
    if (def->life_kind == CARD_LIFE_RACES) sc.races_left = def->life_n;
    if (def->life_kind == CARD_LIFE_USES)  sc.uses_left  = def->life_n;
    int slots = CardSlotsUnlocked(d.prog.frontier_phase);
    sc.slot = -1;
    for (int i = 0; i < slots; i++) {
        bool used = false;
        for (const auto& x : d.cards) if (x.slot == i) used = true;
        if (!used) { sc.slot = i; break; }
    }
    d.cards.push_back(sc);
    d.card_shop.bought++;
    d.card_shop.bought_ids.push_back(id);
    return true;
}

bool CardOpReroll(SaveData& d) {
    if (!d.card_shop.open) return false;
    long cost = CardShopRerollCost(d.card_shop.rerolls, d.cards);
    if (d.prog.credits < cost) return false;
    d.prog.credits -= cost;
    EconLog("card_reroll", -cost, d.prog.credits);   //
    d.card_shop.rerolls++;
    char seed[128];
    snprintf(seed, sizeof(seed), "%s:%d:%d", d.card_shop.race_id.c_str(),
             d.card_shop.seed_base, d.card_shop.rerolls);
    d.card_shop.offers = CardShopDraw(d.cards, d.prog.frontier_phase, seed,
                                      CardShopOfferCount(d.cards));
    d.card_shop.bought_ids.clear();
    return true;
}

bool CardOpShopClose(SaveData& d) { d.card_shop = CardShopState{}; return true; }

bool CardOpSlot(SaveData& d, const std::string& id, int slot) {
    SignedCard* sc = CardFind(d, id);
    if (!sc) return false;
    if (slot < 0 || slot >= CardSlotsUnlocked(d.prog.frontier_phase)) return false;
    for (const auto& x : d.cards) if (&x != sc && x.slot == slot) return false;   // occupied
    sc->slot = slot;
    return true;
}
bool CardOpBench(SaveData& d, const std::string& id) {
    SignedCard* sc = CardFind(d, id);
    if (!sc) return false;
    sc->slot = -1;
    return true;
}

bool CardOpUpgrade(SaveData& d, const std::string& id) {
    SignedCard* sc = CardFind(d, id);
    if (!sc) return false;
    CardUpgradeInfo up;
    if (!CardUpgradeNext(*sc, up) || !up.ready) return false;
    if (d.prog.credits < up.cost) return false;
    d.prog.credits -= up.cost;
    EconLog("card_upgrade", -up.cost, d.prog.credits);   //
    sc->level = up.to_level;
    sc->invested += up.cost;
    return true;
}

bool CardOpSell(SaveData& d, const std::string& id) {
    for (size_t i = 0; i < d.cards.size(); i++) {
        if (d.cards[i].card_id != id) continue;
        { long v = CardSellValue(d.cards[i]); d.prog.credits += v; EconLog("card_sell", v, d.prog.credits); }   //
        d.cards.erase(d.cards.begin() + i);
        // the "seen" marker can never exceed the real wallet (otherwise the next
        // purchase would get absorbed by the inflated marker and there'd be no badge).
        if (d.cards_seen > (int)d.cards.size()) d.cards_seen = (int)d.cards.size();
        return true;
    }
    return false;
}

// visiting the Sponsors tab -> the entire current wallet is marked "seen".
bool CardOpSeen(SaveData& d) {
    d.cards_seen = (int)d.cards.size();
    return true;
}

// atomic multi-slot reassignment (drag & drop). Validates against a copy and commits all-or-nothing.
bool CardOpArrange(SaveData& d, const std::vector<std::pair<std::string, int>>& moves) {
    if (moves.empty()) return false;
    int slots = CardSlotsUnlocked(d.prog.frontier_phase);
    std::vector<SignedCard> next = d.cards;
    for (const auto& mv : moves) {
        SignedCard* sc = nullptr;
        for (auto& x : next) if (x.card_id == mv.first) { sc = &x; break; }
        if (!sc) return false;                                  // nonexistent id -> nothing is applied
        if (mv.second < -1 || mv.second >= slots) return false; // outside the gating (-1 = benched)
        sc->slot = mv.second;
    }
    for (size_t i = 0; i < next.size(); i++)                    // no duplicated active slot after applying
        for (size_t j = i + 1; j < next.size(); j++)
            if (next[i].slot >= 0 && next[i].slot == next[j].slot) return false;
    d.cards.swap(next);
    return true;
}

// Season rollover: everything persists; clauses restart their counters.
void CardOpSeasonReset(SaveData& d) {
    for (auto& sc : d.cards) sc.clause_miss = 0;
}

// See career_cards.h. Verifies rarity BEFORE deleting (a defense against a
// sponsorTierRank that no longer matches the card -- e.g. you sold it and bought a different one between the
// negotiation opening and its submission). Never touches d.prog.credits.
bool CardOpCede(SaveData& d, const std::string& id, int expectedRarity) {
    for (size_t i = 0; i < d.cards.size(); i++) {
        if (d.cards[i].card_id != id) continue;
        const CardDef* def = CardById(id);
        if (!def || def->rarity != expectedRarity) return false;
        d.cards.erase(d.cards.begin() + i);
        return true;
    }
    return false;
}

} // namespace career
