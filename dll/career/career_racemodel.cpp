// dll/career/career_racemodel.cpp
#include "career_racemodel.h"
#include <algorithm>
#include <cmath>

namespace career {
namespace {
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double unit() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }  // [0,1)
    double gauss() {
        double u1 = unit(), u2 = unit();
        if (u1 < 1e-12) u1 = 1e-12;
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }
};
struct Row { int idx; double score; bool dnf; bool mech; };
} // anon

RaceOutcome SimulateRace(const std::vector<RaceEntrant>& es, int weather,
                         std::uint64_t seed, const RaceModelConfig& cfg) {
    const int n = (int)es.size();
    RaceOutcome out; out.field = n; out.dnf_of.assign(n, DnfKind::Finished);
    if (n == 0) return out;
    Rng rng(seed);
    const bool wet = (weather >= 2);  // 0 dry, 1 variable, 2 rain, 3 random (treated as wet)
    std::vector<Row> rows; rows.reserve(n);
    for (int i = 0; i < n; i++) {
        const RaceEntrant& e = es[i];
        double skill = e.skill + (e.is_player ? cfg.player_offset : 0.0);
        if (wet) skill = skill * (1.0 - cfg.wet_skill_weight) + e.wet_skill * cfg.wet_skill_weight;
        double sigma = cfg.base_sigma + cfg.consistency_sigma * (1.0 - e.consistency);
        if (wet) sigma *= cfg.wet_sigma_mult;
        double score = skill + rng.gauss() * sigma;
        bool mech = rng.unit() < cfg.mech_dnf_per_unreliability * (1.0 - e.reliability);
        bool inc  = rng.unit() < cfg.incident_dnf_per_aggression * e.aggression;
        rows.push_back({ i, score, (mech || inc), (mech && !inc) });  // if both -> incident (error)
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.dnf != b.dnf) return !a.dnf;   // finishers first
        return a.score > b.score;            // then by pace descending
    });
    out.order.reserve(n);
    for (int rank = 0; rank < n; rank++) {
        const Row& r = rows[rank];
        out.order.push_back(r.idx);
        if (r.dnf) out.dnf_of[r.idx] = r.mech ? DnfKind::MechanicalDnf : DnfKind::ErrorDnf;
        if (es[r.idx].is_player) out.player_pos = rank + 1;
    }
    return out;
}
} // namespace career
