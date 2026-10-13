#pragma once
#include <string>
#include <vector>
#include "career_results.h"

namespace career {

// Period-correct points system (data will come from ams2_points.psv).
struct PointsSystem {
    std::vector<int> points;   // highest to lowest: points[0] = points for 1st
    int fl_point = 0;          // point for fastest lap
    int fl_top_n = 0;          // 0 = no condition; N = only if the fastest lap finished in the top-N
    int pole_point = 0;        // reserved (not used yet)
};

// A driver's score for one round.
struct RoundScore {
    std::string name;
    int  pos = 0;
    int  points = 0;
    bool fl = false;
    bool dnf = false;
    std::string driver_id;     // stable id propagated from ClassifiedEntry (rides alongside; the
                               // standings key is still the name — ADDITIVE, placed last so it doesn't break positional inits)
};

// Scores a classification. sessionScale: 1.0 feature, 0.5 sprint, 0.0 quali/practice.
// flName = driver with fastest lap ("" = none). DNF/DSQ score 0.
std::vector<RoundScore> ScoreRound(const std::vector<ClassifiedEntry>& classification,
                                   const PointsSystem& sys, double sessionScale,
                                   const std::string& flName = std::string());

// Championship points by position (1-based) — ported from the prototype's `ptsFor` function.
// Returns bal::CHAMP_POINTS[pos-1] for pos in 1..10, 0 out of range (incl. pos<=0). This table is for
// DISPLAY/bonus; period-correct per-series scoring uses PointsSystem (points_id) in ScoreRound.
int PtsFor(int pos);

} // namespace career
