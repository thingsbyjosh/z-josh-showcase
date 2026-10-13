#include "career_scoring.h"
#include "career_balance.h"

namespace career {

std::vector<RoundScore> ScoreRound(const std::vector<ClassifiedEntry>& cls,
                                   const PointsSystem& sys, double sessionScale,
                                   const std::string& flName) {
    std::vector<RoundScore> out;
    for (const auto& e : cls) {
        RoundScore r;
        r.name = e.name; r.driver_id = e.driver_id; r.pos = e.pos; r.dnf = e.dnf;
        int base = 0;
        if (!e.dnf && e.pos >= 1 && e.pos <= (int)sys.points.size())
            base = sys.points[e.pos - 1];
        r.points = (int)(base * sessionScale + 0.5);
        if (!flName.empty() && e.name == flName && !e.dnf && sessionScale > 0.0 &&
            (sys.fl_top_n == 0 || e.pos <= sys.fl_top_n)) {
            r.points += sys.fl_point;
            r.fl = true;
        }
        out.push_back(r);
    }
    return out;
}

int PtsFor(int pos) {
    if (pos >= 1 && pos <= 10) return bal::CHAMP_POINTS[pos - 1];
    return 0;
}

} // namespace career
