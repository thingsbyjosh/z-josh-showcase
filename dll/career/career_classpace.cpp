#include "career_classpace.h"
#include "career_balance.h"    // the seeding constants the bar is DERIVED from
#include "career_gridcurve.h"  // kTalentRefSpan: the last resort when there is neither trim nor legacy column
#include "career_rating.h"     // RatingQuality: the rating -> talent conversion
#include "paths.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace career {

// Switch of the global-bar experiment (see TalentRefSpanFor).
bool g_varaGlobal = true;
namespace {
std::map<std::string, ClassPace> g_pace;

// Local splitter: doesn't depend on career_fields.cpp's SplitPipe, so this file doesn't drag anything in.
std::vector<std::string> SplitPipeLocal(const char* line) {
    std::vector<std::string> out; std::string cur;
    for (const char* p = line; *p; ++p) {
        if (*p == '|') { out.push_back(cur); cur.clear(); }
        else if (*p != '\r' && *p != '\n') cur += *p;
    }
    out.push_back(cur);
    return out;
}
}  // anon

bool LoadClassPace(const char* path) {
    g_pace.clear();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#' || line[0]=='\r' || line[0]=='\n' || line[0]=='\0') continue;
        std::vector<std::string> t = SplitPipeLocal(line);
        if (t.size() < 5) continue;
        if (t[0].empty() || t[0] == "class_id") continue;   // header
        ClassPace c;
        c.spread_dry = atof(t[1].c_str());
        c.median_dry = atof(t[2].c_str());
        c.spread_wet = atof(t[3].c_str());
        c.median_wet = atof(t[4].c_str());
        // col 5 = source (text). col 6 (28/07, OPTIONAL) = the class's talent span. Read at the end
        // on purpose: an old, 6-column PSV still loads the same way and falls back to the global span.
        if (t.size() >= 7) c.talent_ref_span = atof(t[6].c_str());
        // the TRIM, a multiplier over the derived bar. It is also read at the end
        // and for the same reason: a 7-column PSV still loads the same and falls to the legacy column.
        if (t.size() >= 8) c.talent_ref_trim = atof(t[7].c_str());
        // 24/08: col 9 was `curve_trim` (the Curve trim). It was retired along with the per-position split and
        // is no longer read — it was not written in any row. A row that brings it still loads: it is extra, it does not
        // get in the way.
        c.found = true;
        g_pace[t[0]] = c;
    }
    fclose(f);
    return true;
}

void ReplaceClassPace(std::map<std::string, ClassPace> m) { g_pace = std::move(m); }

void EnsureClassPaceLoaded() {
    static bool tried = false;
    if (tried || !g_pace.empty()) return;   // already loaded (or injected by a test) -> not overwritten
    tried = true;                           // missing file -> club-level defaults, no per-grid retry
    LoadClassPace(paths::Data("career\\class_pace_curve.psv").c_str());
}

int ClassPaceCount() { return (int)g_pace.size(); }

ClassPace ClassPaceFor(const std::string& classId) {
    auto it = g_pace.find(classId);
    if (it != g_pace.end()) return it->second;
    return ClassPace{};   // defaults, found=false
}

double DerivedTalentRefSpan(int level) {
    if (level < 1) level = 1;
    if (level > 7) level = 7;
    const double mid  = bal::LEVEL_SEED_MIDPOINT[level - 1];
    const double half = bal::LEVEL_SEED_SPREAD * 0.5;
    // The TALENT fan that the seeding produces at this level: the first of the grid is anchored half a
    // spread above the midpoint and the last half below (LevelAnchoredSeed), and RatingQuality
    // converts the two extremes to the 0..1 scale in which talent lives.
    const double span = RatingQuality(mid + half) - RatingQuality(mid - half);
    return span > 0.0 ? span : kTalentRefSpan;
}

// THE WIDTH KNOB, applied at the ONLY point the bar passes through on its way to the XML.
//
// The bar is a DIVISOR: it converts talent gap into pace gap, so a shorter bar delivers a
// WIDER grid. That is why it is divided, and why the scale is intuitive the opposite way to how it is applied:
// `GRID_SPREAD_SCALE = 2.0` means "twice as wide", not "the bar doubled".
//
// It goes here and not in the two XML writers because both call this function: a single place, or they diverge
// again — which is exactly what happened to `ratingSkill` with its two copies, and left an inert fix.
static inline double ConAnchuraGlobal(double vara) {
    const double k = bal::GRID_SPREAD_SCALE;
    return (k > 0.01 && k < 100.0) ? vara / k : vara;   // guard: an absurd value does not break the world
}

double TalentRefSpanFor(const std::string& className, int level) {
    // EXPERIMENT 12/08 — A SINGLE GLOBAL BAR. Dividing by each CLASS's own bar is saying "I don't care
    // how much these people truly spread apart, only their position within their own group", and it erases the character
    // of the category right after computing it (measured: Ember_Series, CINDER CUP and GT3 end up delivering the same).
    // With a global scale, a class whose people are truly spread out delivers a wide grid and
    // one whose people are bunched together delivers it tight: the character is described by WHO RUNS, not by a curated number.
    if (g_varaGlobal) return ConAnchuraGlobal(kTalentRefSpan);
    auto it = className.empty() ? g_pace.end() : g_pace.find(className);
    // Without a level there is no possible derivation (a class OUTSIDE the ladder, reached by any
    // Custom Race): then what is curated in absolute terms rules, which is the only thing there is. A level is
    // not invented: guessing it would give a level 1 bar to a class that can be anything.
    if (level <= 0) {
        if (it != g_pace.end() && it->second.talent_ref_span > 0.0)
            return ConAnchuraGlobal(it->second.talent_ref_span);
        return ConAnchuraGlobal(kTalentRefSpan);
    }
    const double derivada = DerivedTalentRefSpan(level);
    if (it == g_pace.end()) return ConAnchuraGlobal(derivada);
    // Precedence, and the order matters: the TRIM rules because it is what follows the scale. The legacy column
    // survives one cycle so that an unconverted class does not change behavior the day the
    // seeding is touched -- but it is precisely the one that falls out of sync, so it never beats the trim.
    if (it->second.talent_ref_trim > 0.0) return ConAnchuraGlobal(derivada * it->second.talent_ref_trim);
    if (it->second.talent_ref_span > 0.0) return ConAnchuraGlobal(it->second.talent_ref_span);
    return ConAnchuraGlobal(derivada);
}

}  // namespace career
