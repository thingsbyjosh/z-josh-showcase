#include "career_memories.h"
#include "career_balance.h"
#include "career_rating.h"
#include "json_min.h"
#include "career_text.h"   // 23/09: CleanNameSpaces — the legend's name, without hard spaces
#include <algorithm>
#include <cctype>

namespace career {

// Translation-unit-local memory table (populated by LoadMemories). The accessors are defined
// here because MemoryStatusOf walks AllMemories() for the chain-predecessor gate. LoadMemories
// fills g_memories; until then it is empty (hand-built memories in tests use the fallback path).
static std::vector<Memory> g_memories;

const std::vector<Memory>& AllMemories() { return g_memories; }

const Memory* MemoryById(const std::string& id) {
    for (const auto& m : g_memories) if (m.id == id) return &m;
    return nullptr;
}

int MemoryCount() { return (int)g_memories.size(); }

// Load the memory table from a JSON file. Uses the repo's json_min parser
// exactly as career_data_json.cpp does (jsonm::ParseFile -> walk the "memories" array). false if the
// file won't open or won't parse, or it has no "memories" array. Replaces g_memories on success.
bool LoadMemories(const char* path) {
    if (!path) return false;
    std::string err;
    jsonm::ValuePtr root = jsonm::ParseFile(path, &err);
    if (!root || !root->isObject()) return false;
    const jsonm::Value* arr = root->member("memories");
    if (!arr || !arr->isArray()) return false;
    g_memories.clear();
    for (const auto& ep : arr->arr) {
        const jsonm::Value* o = ep.get();
        if (!o || !o->isObject()) continue;
        Memory m;
        m.id          = o->memberString("id");
        m.gen         = o->memberInt("gen");
        m.ancestor    = o->memberString("ancestor");
        m.relation_es = o->memberString("relation_es");
        m.relation_en = o->memberString("relation_en");
        m.era         = o->memberString("era");
        m.series_es   = o->memberString("series_es");
        m.series_en   = o->memberString("series_en");
        m.cls         = o->memberString("cls");
        m.seat_no     = o->memberInt("seat_no");
        m.grid_flavor = o->memberString("grid_flavor");
        m.objective_es= o->memberString("objective_es");
        m.objective_en= o->memberString("objective_en");
        const jsonm::Value* legs = o->member("legends");
        if (legs && legs->isArray())
            for (const auto& lp : legs->arr) {
                const jsonm::Value* L = lp.get();
                if (!L || !L->isObject()) continue;
                MemoryLegend leg;
                leg.name    = CleanNameSpaces(L->memberString("name"));
                leg.country = L->memberString("country");
                m.legends.push_back(leg);
            }
        const jsonm::Value* rds = o->member("rounds");
        if (rds && rds->isArray())
            for (const auto& rp : rds->arr) {
                const jsonm::Value* R = rp.get();
                if (!R || !R->isObject()) continue;
                MemoryRound r;
                r.track_name = R->memberString("track");
                r.track_hash = R->memberLong("track_hash");
                r.doy        = R->memberInt("doy");
                r.round      = R->memberInt("round", 1);
                m.rounds.push_back(r);
            }
        m.reward.relic_es      = o->memberString("relic_es");
        m.reward.relic_en      = o->memberString("relic_en");
        m.reward.crest_es      = o->memberString("crest_es");
        m.reward.crest_en      = o->memberString("crest_en");
        m.reward.inherit       = o->memberLong("inherit");
        m.reward.reward_repeat = o->memberLong("reward_repeat");
        if (o->has("unlock_gen_override")) m.unlock_gen_override = o->memberInt("unlock_gen_override");
        g_memories.push_back(m);
    }
    return true;
}

// first-pass classification (criterion: content locked to a real bygone era -> memory candidate).
// Keys are normalized class keys (lowercase, [a-z0-9] only). Dual-context classes (group c / lantern_masters_7489 /
// gt1) appear here as memory candidates AND also sit in the modern calendar (handled at call sites).
static std::string normClass(const std::string& s) {
    std::string o;
    for (char c : s) { if (std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c); }
    return o;
}

std::string EraForClass(const std::string& classKey) {
    const std::string k = normClass(classKey);
    // 60s-70s
    static const char* e6070[] = { "fvintageg1","fvintageg2","fclassicgen1","onyx_trophy_8637","ivory_championship",
        "copaclassic","fretrog1","stanmore_ax671" };
    // 80s
    static const char* e80[] = { "fclassicgen2","fclassicgen3","groupa","groupc","m1ashwood_motors","nimbus_championship_8678",
        "fretrog2","fretrog3" };
    // 90s-00s
    static const char* e9000[] = { "gt190s","f11991","fedge1995","stockcar1999","drift_championship_55212004","gt12005",
        "gt22005","lmp12005","lantern_masters_74892005","lantern_masters_7489" };
    for (const char* s : e6070)  if (k == normClass(s)) return "60s70s";
    for (const char* s : e80)    if (k == normClass(s)) return "80s";
    for (const char* s : e9000)  if (k == normClass(s)) return "90s00s";
    return "";   // modern ladder / fictional / timeless -> not a memory
}

bool IsMemoryClass(const std::string& classKey) { return !EraForClass(classKey).empty(); }

double MemoryUnlockRating(const Memory& m) {
    int idx = (m.unlock_gen_override >= 0) ? m.unlock_gen_override : m.gen;
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;                          // table has 3 buckets (gen 0..2+)
    return bal::MEM_UNLOCK_RATING[idx];
}

MemoryStatus MemoryStatusOf(const Memory& m, const std::map<std::string,int>& clearedCount,
                            double playerRating) {
    auto it = clearedCount.find(m.id);
    if (it != clearedCount.end() && it->second > 0) return MemoryStatus::Completed;
    if (m.gen > 0) {
        bool prevCleared = false;
        // prefer the loaded table's gen N-1 memory
        for (const auto& p : AllMemories())
            if (p.gen == m.gen - 1) {
                auto pit = clearedCount.find(p.id);
                if (pit != clearedCount.end() && pit->second > 0) { prevCleared = true; }
            }
        // fallback (hand-built / partial table): ANY cleared memory satisfies the chain
        if (!prevCleared)
            for (const auto& kv : clearedCount) if (kv.second > 0) { prevCleared = true; break; }
        if (!prevCleared) return MemoryStatus::Locked;
    }
    if (playerRating < MemoryUnlockRating(m)) return MemoryStatus::Locked;
    return MemoryStatus::Available;
}

bool MemoryRoundOpen(const Memory& m, const MemoryRound& r, const std::map<std::string,int>& clearedCount,
                     double playerRating, int cursorDoy) {
    // level half (status must be Available — Completed memories are replayable but the caller decides
    // that; for "raceable right now" we require unlocked, i.e. NOT Locked)
    if (MemoryStatusOf(m, clearedCount, playerRating) == MemoryStatus::Locked) return false;
    // date half (shared general calendar must have reached the round's doy)
    return cursorDoy >= r.doy;
}

bool MemoryGrantsContent(int playedCount) { return playedCount == 0; }

long MemoryPayout(const Memory& m, int playedCount, double objectiveScore) {
    if (playedCount <= 0) return m.reward.inherit;            // first clear: full inheritance
    double s = objectiveScore;
    if (s < 0.0) s = 0.0; else if (s > 1.0) s = 1.0;
    double mult = bal::MEM_REPLAY_MIN_MULT
                + s * (bal::MEM_REPLAY_MAX_MULT - bal::MEM_REPLAY_MIN_MULT);
    return (long)(m.reward.reward_repeat * mult + 0.5);       // round to nearest credit
}

// map a 0..1 position (0 = best seat) to a rating in [hi..lo], then to AMS2 race_skill 0..1.
static double seatRating(double t, double hi, double lo) {
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    return hi - t * (hi - lo);
}
// AMS2 race_skill 0..1 from a rating via the unified quality map (q(R)); keeps the period field on the
// same scale the modern grid uses (career_rating::RatingQuality), so legends read as fast, fillers slow.
static double skillFromRating(double r) { return RatingQuality(r); }

std::vector<GridDriver> ComposeMemoryGrid(const Memory& m, const std::vector<std::string>& liveries) {
    const int N = (int)liveries.size();
    std::vector<GridDriver> out;
    out.reserve(N);
    // 1) named legends fill the top works seats (rating MEM_LEGEND_TOP..BOTTOM by their order)
    const int nLeg = std::min((int)m.legends.size(), N);
    for (int i = 0; i < nLeg; i++) {
        GridDriver g;
        g.livery_name = liveries[i];
        g.name        = m.legends[i].name;
        g.country     = m.legends[i].country;
        g.kind        = "real";
        double t = (nLeg <= 1) ? 0.0 : (double)i / (double)(nLeg - 1);
        g.race_skill  = skillFromRating(seatRating(t, bal::MEM_LEGEND_TOP, bal::MEM_LEGEND_BOTTOM));
        g.qualifying  = g.race_skill;
        out.push_back(g);
    }
    // 2) remaining seats = privateer cohort (incl. the ancestor). "pure" wants no fillers BUT if there
    //    are more liveries than legends we still must fill every livery -> the ancestor + minimal fillers.
    const int remaining = N - nLeg;
    for (int j = 0; j < remaining; j++) {
        GridDriver g;
        g.livery_name = liveries[nLeg + j];
        g.kind        = "roster";
        double t = (remaining <= 1) ? 0.0 : (double)j / (double)(remaining - 1);
        g.race_skill  = skillFromRating(seatRating(t, bal::MEM_PRIVATEER_TOP, bal::MEM_PRIVATEER_BOT));
        g.qualifying  = g.race_skill;
        if (j == 0) {                                  // the ancestor takes the strongest privateer seat
            g.name    = m.ancestor;
            g.country = "";
        } else {
            g.name    = "Privateer #" + std::to_string(m.seat_no + j);
            g.country = "";
        }
        out.push_back(g);
    }
    return out;
}

} // namespace career
