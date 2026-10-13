#include "career_attributes.h"
#include "career_fields.h"    // FoldDriverName + PackDriver/PackDriverId — the SAME id resolution as the seat
#include "career_data_json.h" // DriverIdForName/PoolDriverIds/DriverById: the pool's canonical identity
#include "career_genome.h"    // GenomeSeed/GenomeNormal: deterministic draw by identity, zero save bytes
#include "paths.h"            // FOpen (UTF-8 -> wide)
#include "career_text.h"      // 23/09: CleanNameSpaces — the name, without hard spaces
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>

namespace career {

static std::vector<std::string> SplitPipe(const char* line) {
    std::vector<std::string> out; std::string cur;
    for (const char* p = line; *p; ++p) {
        char c = *p;
        if (c == '\r' || c == '\n') continue;
        if (c == '|') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

static std::vector<DriverAttributes>       g_attrs;
static std::map<std::string, size_t>       g_attrById;     // pool id -> index
static std::map<std::string, size_t>       g_attrByFold;   // folded name -> index

// THE POOL IS MATCHED BY EXACT STRING, AND THAT SPLITS PEOPLE IN TWO. `DriverIdForName` looks up the name as
// is, so «Aurélien Panis» and «Aurelien Panis» are not the same person to it; and when it fails, the fallback id
// is a hash of the RAW name, so the crack propagates instead of closing. Measured on the composed
// grid: 18 rows where the name finds the anchor and the id doesn't, in both directions —
// «Sebastien Bourdais» carries the pool id in the anchor and a hash in the grid, so today the engine ALREADY
// has that person split into two identities, with or without it.
//
// Here it is closed only for the anchor lookup, which is what this file can fix without touching anyone's
// identity: a FOLDED pool index with which to register each anchor also under the canonical id
// its name leads to. Changing `PackDriverId` to fold would be the truly correct thing, but it moves keys
// that are written in the saves and is not a decision for this chapter.
static std::map<std::string, std::vector<std::string>>* PoolByFold() {
    static std::map<std::string, std::vector<std::string>> idx;
    static bool hecho = false;
    if (!hecho) {
        hecho = true;
        for (const auto& id : PoolDriverIds()) {
            const PoolDriver* pd = DriverById(id);
            if (!pd) continue;
            const std::string f = FoldDriverName(pd->name);
            if (!f.empty()) idx[f].push_back(id);
        }
    }
    return &idx;
}

// The canonical id a folded name leads to, breaking ties by country when there are namesakes (the registry
// has 27 names with 54 identities: «Joao Barbosa» are really two people, one BRA and one PRT).
static std::string PoolIdByFoldedName(const std::string& name, const std::string& country) {
    const std::string f = FoldDriverName(name);
    if (f.empty()) return std::string();
    const auto& idx = *PoolByFold();
    const auto it = idx.find(f);
    if (it == idx.end() || it->second.empty()) return std::string();
    if (it->second.size() == 1) return it->second[0];
    if (!country.empty())
        for (const auto& id : it->second) {
            const PoolDriver* pd = DriverById(id);
            if (pd && pd->country == country) return id;
        }
    return std::string();   // namesake with no tiebreak: NOT guessed — assigning another person's attributes
}                           //   is worse than not having them

//
// The fifteen knobs
//

static const char* kKnobNames[KNOB_COUNT] = {
    "race_skill", "qualifying_skill", "aggression", "defending", "stamina", "consistency",
    "start_reactions", "wet_skill", "tyre_management", "fuel_management", "blue_flag_conceding",
    "weather_tyre_changes", "vehicle_reliability", "avoidance_of_mistakes", "avoidance_of_forced_mistakes"
};

const char* KnobName(int k) { return (k >= 0 && k < KNOB_COUNT) ? kKnobNames[k] : ""; }

bool KnobIsPositional(int k) {
    return k == KNOB_RACE || k == KNOB_QUAL || k == KNOB_RELIABILITY;
}

bool KnobIsCharacter(int k) {
    return k == KNOB_AGGRESSION || k == KNOB_FUEL || k == KNOB_BLUEFLAG || k == KNOB_WXTYRE;
}

//
// Load
//

// `avg(5)` -> 5 · `perturb` -> 1 · anything we don't recognize -> 1. How much evidence is behind a card
// matters for calibration: an average of ten sources weighs more than a noisy measurement.
static int PesoEvidencia(const std::string& s) {
    if (s.size() > 5 && s.compare(0, 4, "avg(") == 0 && s[s.size() - 1] == ')') {
        const int n = atoi(s.c_str() + 4);
        return n > 0 ? n : 1;
    }
    return s.empty() ? 0 : 1;
}

static void CalibrateModel();   // defined further below, next to the model it calibrates

bool LoadDriverAttributes(const char* path) {
    g_attrs.clear(); g_attrById.clear(); g_attrByFold.clear();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;                       // file missing = world without anchors; nothing regresses
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 5 + KNOB_COUNT) continue;
        if (t[0].empty() || t[0] == "name") continue;
        DriverAttributes d;
        d.name = CleanNameSpaces(t[0]); d.country = t[1]; d.evidence = t[2]; d.src_classes = t[3]; d.present_in = t[4];
        d.evidence_weight = PesoEvidencia(d.evidence);
        for (int k = 0; k < KNOB_COUNT; k++) {
            const std::string& s = t[5 + k];
            d.v[k] = s.empty() ? -1.0f : (float)atof(s.c_str());
        }
        const size_t idx = g_attrs.size();
        g_attrs.push_back(d);
        // IT IS INDEXED WITH `PackDriverId`, NOT WITH A BARE `DriverIdForName`. When a person isn't in the
        // pool, that function returns "" but the seat DOES give them an id: the fallback `rd_<fnv>` hash. Measured,
        // 169 of the 346 anchors only exist in our world as pack filler, so indexing only by
        // the pool left half the set out without anything saying so. A `PackDriver` is built
        // to reuse the real function instead of copying its hash, which is how they drift apart over time.
        PackDriver como; como.name = d.name; como.country = d.country;
        const std::string id = PackDriverId(como);
        if (!id.empty()) g_attrById.emplace(id, idx);
        // And also under the id its FOLDED name leads to, which is different when the spelling differs.
        const std::string idPlegado = PoolIdByFoldedName(d.name, d.country);
        if (!idPlegado.empty()) g_attrById.emplace(idPlegado, idx);
        const std::string fold = FoldDriverName(d.name);
        if (!fold.empty()) g_attrByFold.emplace(fold, idx);   // the first wins: a repeated card is ONE person
    }
    fclose(f);
    CalibrateModel();   // the model comes from the freshly loaded anchors, not from hand-written numbers
    return true;
}

int DriverAttributeCount() { return (int)g_attrs.size(); }

//
// The two-factor model, calibrated from the anchors on load
//

static AttrModel g_model;

// The eight QUALITY knobs that define the axis. Qualifying is left out on purpose: measured, its loading
// on it is 0.08, and including it would drag pace into the factor that must be orthogonal to pace.
static const int kCraftKnobs[] = {
    KNOB_CONSISTENCY, KNOB_DEFENDING, KNOB_WET, KNOB_TYRE, KNOB_RELIABILITY, KNOB_MISTAKES,
    KNOB_STAMINA, KNOB_STARTS
};
static const int kCraftKnobCount = (int)(sizeof(kCraftKnobs) / sizeof(kCraftKnobs[0]));

// FALLBACK constants, measured over the 346 on 11/08. Only used if the anchors file is absent: without
// them, a missing optional dataset would leave the model at zeros and the whole world would come out identical.
static void FallbackModel(AttrModel& m) {
    static const float mean[KNOB_COUNT] = { 0.849f, 0.858f, 0.610f, 0.530f, 0.742f, 0.745f, 0.809f, 0.689f,
                                            0.779f, 0.670f, 0.918f, 0.702f, 0.785f, 0.536f, 0.512f };
    static const float rr[KNOB_COUNT]   = { 1.000f, 0.829f, 0.223f, 0.133f, 0.129f, 0.465f, 0.467f, 0.290f,
                                            0.254f, 0.263f, -0.061f, 0.123f, 0.262f, 0.113f, 0.176f };
    static const float sd[KNOB_COUNT]   = { 0.088f, 0.080f, 0.167f, 0.196f, 0.115f, 0.143f, 0.086f, 0.173f,
                                            0.109f, 0.142f, 0.132f, 0.135f, 0.161f, 0.171f, 0.169f };
    static const float load[KNOB_COUNT] = { 0.000f, 0.080f, 0.300f, 0.680f, 0.710f, 0.670f, 0.660f, 0.630f,
                                            0.610f, 0.300f, 0.200f, 0.300f, 0.660f, 0.870f, 0.600f };
    m.mean_pace = 0.849f; m.sd_pace = 0.088f;
    for (int k = 0; k < KNOB_COUNT; k++) {
        m.mean[k] = mean[k];
        m.r_pace[k] = rr[k];
        m.beta[k] = rr[k] * sd[k] / m.sd_pace;
        m.sd_resid[k] = sd[k] * std::sqrt(1.0f - rr[k] * rr[k] > 0.0f ? 1.0f - rr[k] * rr[k] : 0.0f);
        m.craft_load[k] = load[k];
    }
    m.beta[KNOB_RACE] = 1.0f; m.sd_resid[KNOB_RACE] = 0.0f; m.craft_load[KNOB_RACE] = 0.0f;
    m.r_pace[KNOB_RACE] = 1.0f;
    m.calibrated = false;
}

// Mean and standard deviation of a sample; sd is never zero (it's used as a divisor).
static void MediaSd(const std::vector<float>& v, float& m, float& s) {
    m = 0.0f; s = 1e-6f;
    if (v.empty()) return;
    for (float x : v) m += x;
    m /= (float)v.size();
    double acc = 0.0;
    for (float x : v) acc += (double)(x - m) * (x - m);
    const float d = (float)std::sqrt(acc / v.size());
    if (d > 1e-6f) s = d;
}

static float Corr(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.size() < 4) return 0.0f;
    float ma, sa, mb, sb; MediaSd(a, ma, sa); MediaSd(b, mb, sb);
    double acc = 0.0;
    for (size_t i = 0; i < a.size(); i++) acc += (double)(a[i] - ma) * (b[i] - mb);
    const float r = (float)(acc / a.size()) / (sa * sb);
    return r > 1.0f ? 1.0f : (r < -1.0f ? -1.0f : r);
}

// IT IS CALIBRATED ON LOAD, the numbers are not hard-coded. If more measured packs come in tomorrow, the model moves
// with them instead of staying behind describing a corpus that no longer exists.
static void CalibrateModel() {
    FallbackModel(g_model);                       // floor: if something can't be measured, the fallback stays
    if (g_attrs.size() < 50) return;              // insufficient sample to regress anything meaningful

    std::vector<float> pace;
    std::vector<size_t> conPace;
    for (size_t i = 0; i < g_attrs.size(); i++)
        if (g_attrs[i].has(KNOB_RACE)) { pace.push_back(g_attrs[i].v[KNOB_RACE]); conPace.push_back(i); }
    if (pace.size() < 50) return;
    MediaSd(pace, g_model.mean_pace, g_model.sd_pace);

    // 1) regression of each knob on pace -> mean, slope and residual
    std::vector<std::vector<float>> resid(KNOB_COUNT);
    std::vector<std::vector<size_t>> quien(KNOB_COUNT);
    for (int k = 0; k < KNOB_COUNT; k++) {
        if (k == KNOB_RACE) { g_model.mean[k] = g_model.mean_pace; g_model.beta[k] = 1.0f; g_model.r_pace[k] = 1.0f;
                              g_model.sd_resid[k] = 0.0f; g_model.craft_load[k] = 0.0f; continue; }
        std::vector<float> x, y; std::vector<size_t> idx;
        for (size_t j = 0; j < conPace.size(); j++) {
            const size_t i = conPace[j];
            if (!g_attrs[i].has(k)) continue;
            x.push_back(pace[j]); y.push_back(g_attrs[i].v[k]); idx.push_back(i);
        }
        if (x.size() < 30) continue;              // few data: the fallback constant stays
        float mx, sx, my, sy; MediaSd(x, mx, sx); MediaSd(y, my, sy);
        const float r = Corr(x, y);
        g_model.mean[k] = my;
        g_model.r_pace[k] = r;      // the target `career_traits` is calibrated against
        g_model.beta[k] = r * sy / sx;
        for (size_t t = 0; t < y.size(); t++) resid[k].push_back(y[t] - (my + g_model.beta[k] * (x[t] - mx)));
        float mr, sr; MediaSd(resid[k], mr, sr);
        g_model.sd_resid[k] = sr;
        quien[k] = idx;
    }

    // 2) the CRAFT factor: mean of the STANDARDIZED residuals of the eight quality knobs. It is standardized before
    //    averaging because otherwise `defending` (sd 0.196) would dominate `starts` (0.086) for no reason.
    std::map<size_t, std::vector<float>> zPorPersona;
    for (int c = 0; c < kCraftKnobCount; c++) {
        const int k = kCraftKnobs[c];
        if (resid[k].empty()) continue;
        float mr, sr; MediaSd(resid[k], mr, sr);
        for (size_t t = 0; t < resid[k].size(); t++)
            zPorPersona[quien[k][t]].push_back((resid[k][t] - mr) / sr);
    }
    std::vector<float> craft; std::vector<size_t> craftQuien;
    for (const auto& kv : zPorPersona) {
        if ((int)kv.second.size() < 6) continue;  // with fewer than six knobs the factor isn't reliable
        float s = 0.0f; for (float z : kv.second) s += z;
        craft.push_back(s / kv.second.size()); craftQuien.push_back(kv.first);
    }
    if (craft.size() < 50) return;
    float mc, sc; MediaSd(craft, mc, sc);
    for (size_t t = 0; t < craft.size(); t++) {
        craft[t] = (craft[t] - mc) / sc;          // to N(0,1), the scale on which it is drawn
        g_attrs[craftQuien[t]].craft = craft[t];
        g_attrs[craftQuien[t]].craft_known = true;
    }

    // 3) the loading of EACH knob on the factor — including those that don't define it, to know how much falls to them
    std::map<size_t, float> craftDe;
    for (size_t t = 0; t < craft.size(); t++) craftDe[craftQuien[t]] = craft[t];
    for (int k = 0; k < KNOB_COUNT; k++) {
        if (k == KNOB_RACE || resid[k].empty()) continue;
        std::vector<float> a, b;
        for (size_t t = 0; t < resid[k].size(); t++) {
            const auto it = craftDe.find(quien[k][t]);
            if (it == craftDe.end()) continue;
            a.push_back(resid[k][t]); b.push_back(it->second);
        }
        if (a.size() < 30) continue;
        float l = Corr(a, b);
        if (l < 0.0f) l = 0.0f;                   // a negative loading on a quality axis means nothing
        if (l > 0.98f) l = 0.98f;                 // always leaves each axis some life of its own
        g_model.craft_load[k] = l;
    }
    g_model.calibrated = true;
}

const AttrModel& AttributeModel() { return g_model; }

bool MeasuredCraftFor(const std::string& driverId, const std::string& name, float& out) {
    const DriverAttributes* d = AttributesFor(driverId, name);
    if (!d || !d->craft_known) return false;
    out = d->craft;
    return true;
}

float CraftFor(const std::string& driverId, const std::string& name) {
    if (const DriverAttributes* d = AttributesFor(driverId, name))
        if (d->craft_known) return d->craft;      // measured: what is known isn't drawn
    // Deterministic draw by identity. Band 7001 is this axis's own so as not to step on the draws
    // the genome already makes with the same seed.
    const std::string sem = driverId.empty() ? name : driverId;
    if (sem.empty()) return 0.0f;
    return (float)GenomeNormal(GenomeSeed(sem), 7001);
}


const DriverAttributes* AttributesForId(const std::string& driverId) {
    if (driverId.empty()) return nullptr;
    const auto it = g_attrById.find(driverId);
    return it == g_attrById.end() ? nullptr : &g_attrs[it->second];
}

const DriverAttributes* AttributesForIndex(int i) {
    return (i >= 0 && i < (int)g_attrs.size()) ? &g_attrs[i] : nullptr;
}

const DriverAttributes* AttributesForName(const std::string& name) {
    const std::string fold = FoldDriverName(name);
    if (fold.empty()) return nullptr;
    const auto it = g_attrByFold.find(fold);
    return it == g_attrByFold.end() ? nullptr : &g_attrs[it->second];
}

const DriverAttributes* AttributesFor(const std::string& driverId, const std::string& name) {
    if (const DriverAttributes* d = AttributesForId(driverId)) return d;
    return AttributesForName(name);
}

} // namespace career
