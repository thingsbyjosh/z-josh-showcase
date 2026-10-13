#include "career_teamcars.h"
#include "paths.h"
#include "career_genome.h"   // TrimForUnit: a material unit's deterministic character
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

namespace career {

// per spec the game allows +-10%, but that's a 2.5s per-lap gap, more than what separates any
// real grid. It launches at +-2% (~1.4s at Coldvale Park and 1.9s at Northhurst Park) and widens once validated
// in-game. The PSV stores the HONEST value uncapped: raising the ceiling means changing this constant, not re-authoring.
const double kCarAmpCapPct = 2.0;
const double kScalarMin    = 0.900;   // 1.6.9.9's XML legal range
const double kScalarMax    = 1.100;
// vehicle_reliability is a RATIO, not a probability: real packs have been measured with values above
// 1.0 (up to ~1.21), and ReliabilityForArchetype already emits up to 1.20. This pair only catches an authoring
// typo; clamping to [0,1] would turn the class's most reliable car into a middling one.
const double kRelMin = 0.05;
const double kRelMax = 1.50;

namespace {

std::map<std::string, CarDispersion>          g_disp;   // key (series_id | class_name) -> class level
std::map<std::string, std::vector<TeamCarRow>> g_units;  // key (series_id | class_name) -> material units

// Local splitter (same criterion as career_classpace: this file carries nothing over from career_fields).
std::vector<std::string> SplitPipeLocal(const char* line) {
    std::vector<std::string> out; std::string cur;
    for (const char* p = line; *p; ++p) {
        if (*p == '|') { out.push_back(cur); cur.clear(); }
        else if (*p != '\r' && *p != '\n') cur += *p;
    }
    out.push_back(cur);
    return out;
}

std::string TrimSpace(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t')) b--;
    return s.substr(a, b - a);
}

// Reads a FULL line even if it exceeds the buffer (PSV notes run long). Without this, the tail of a
// long line would come back as the next "line" and get parsed as data. false = end of file.
bool ReadLine(FILE* f, std::string& out) {
    out.clear();
    char buf[1024];
    bool any = false;
    while (fgets(buf, sizeof(buf), f)) {
        any = true;
        out += buf;
        if (!out.empty() && (out[out.size()-1] == '\n')) break;
    }
    return any;
}

double ClampScalar(double v) {
    if (v < kScalarMin) return kScalarMin;
    if (v > kScalarMax) return kScalarMax;
    return v;
}

// 2026-07-28 -- SECOND AXIS: a material unit's STRAIGHT-vs-CORNER character. Until today the three scalars
// all came out of the SAME number (strength), so a strong car was strong everywhere and two cars
// with near-equal pace stayed near-equal on any track. This axis is ORTHOGONAL to strength: it doesn't
// make anyone better, it only decides WHERE they perform.
//
// The weights come from sensitivity MEASURED in-game on 2026-07-25 (spec, seconds per 1% of scalar):
//     weight  0.26 Northhurst Park / 0.24 Coldvale Park    power  0.16 / 0.09    drag  0.09 / 0.01
// A +1% power / -1% drag package is worth 0.25s at Northhurst Park and 0.10s at Coldvale Park. For the trade to
// be NEUTRAL on average it has to be paid for in weight, and 0.7% of weight costs 0.182 and 0.168:
//     Northhurst Park        +0.16 +0.09 -0.182 = +0.068 s
//     Coldvale Park  +0.09 +0.01 -0.168 = -0.068 s
// i.e. the same car gains on the straight EXACTLY what it loses in the twisty bits. Between the two extremes of a
// grid that's ~0.27s of swing from one circuit to another: enough for two neighbors to swap places
// between Northhurst Park and Coldvale Park, and too little to reshuffle the class.
//
// The amplitude is PER CLASS (the trim_pct column in series_car_dispersion.psv) and is NOT derived from amp_pct, on
// purpose: in a BoP series the rulebook equalizes LAP TIME, not character -- one car is still
// straight-line-biased and another still corner-biased. That's exactly where it's needed most, which is what the user asked for
// ("especially for drivers with very close pace"). No column -> 0 -> nothing changes. In a spec class it never
// reaches this point: the "zero" tier cuts it off earlier.
const double kCarTrimCapPct     = 2.0;   // conservative launch cap, same as kCarAmpCapPct
const double kCarTrimWeightCost = 0.7;   // weight the power+drag package pays to stay neutral

// A unit's character, deterministic by its key and stable across seasons: same car, same character.
// Uniform in [-1, +1]; the grid's mean is subtracted by the caller, same as with strength.
double TrimForUnit(const std::string& unitKey) {
    if (unitKey.empty()) return 0.0;
    return 2.0 * GenomeUniform(GenomeSeed(unitKey), 7) - 1.0;
}

double ClampRel(double v) {
    if (v < kRelMin) return kRelMin;
    if (v > kRelMax) return kRelMax;
    return v;
}

// UNIT resolution, in a single place ----------------------------------------------------------------
// Pace (scalars) and reliability are different axes, but the UNIT is the same: if the two sides
// resolved the team independently, on the very first edit a car would get one unit's pace and
// another's reliability. That's why the key and the index live here and both consume them.
// With unit_src=authored the unit declares its OWN liveries (the livery doesn't carry the team: "Eskdale Orn317");
// otherwise the unit IS the stem of the vanilla name.
std::string UnitKeyFor(const std::string& liveryName, bool authored) {
    return authored ? liveryName : LiveryStem(liveryName);
}

// key -> the row's INDEX (not a copy of one loose field: reliability needs the whole row).
// Repeated key -> the last one wins, same as the old strength-based index.
std::map<std::string, size_t> BuildUnitIndex(const std::vector<TeamCarRow>& rows, bool authored) {
    std::map<std::string, size_t> byKey;
    for (size_t i = 0; i < rows.size(); i++) {
        if (authored) { for (const auto& lv : rows[i].liveries) byKey[lv] = i; }
        else            byKey[rows[i].unit] = i;
    }
    return byKey;
}

// ';'-separated list -> vector, no empties, courtesy spaces trimmed.
std::vector<std::string> SplitSemi(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == ';') { std::string t = TrimSpace(cur); if (!t.empty()) out.push_back(t); cur.clear(); }
                       else cur += c; }
    std::string t = TrimSpace(cur); if (!t.empty()) out.push_back(t);
    return out;
}

}  // anon

//
// load
//

bool LoadCarDispersion(const char* path) {
    g_disp.clear();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    std::string line;
    while (ReadLine(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        std::vector<std::string> t = SplitPipeLocal(line.c_str());
        if (t.size() < 8) continue;
        std::string key = TrimSpace(t[0]);
        if (key.empty() || key == "key") continue;             // header
        CarDispersion c;
        c.tier      = TrimSpace(t[1]);
        c.amp_pct   = atof(t[2].c_str());
        c.unit_kind = TrimSpace(t[3]);
        c.unit_src  = TrimSpace(t[4]);
        // rel_top/rel_bot: only override the sane default (0.98/0.95) if the column carries a USABLE number. A row
        // with the column empty or zero would leave the whole class with reliability 0 -- a car that never finishes --
        // and that isn't data, it's an authoring gap.
        { double v = atof(t[5].c_str()); if (v > 0.0) c.rel_top = v; }
        { double v = atof(t[6].c_str()); if (v > 0.0) c.rel_bot = v; }
        if (c.amp_pct < 0.0) c.amp_pct = 0.0;                  // bad data -> no dispersion, never negative
        // 9th column, OPTIONAL (trim_pct). Absent -> 0 = no character, which is the usual behavior:
        // so an 8-field row (and any synthetic PSV in the suite) still yields exactly the same thing.
        if (t.size() >= 9) { double v = atof(t[8].c_str()); if (v > 0.0) c.trim_pct = v; }
        c.found = true;
        g_disp[key] = c;
    }
    fclose(f);
    return true;
}

bool LoadTeamCars(const char* path) {
    g_units.clear();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    std::string line;
    while (ReadLine(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        std::vector<std::string> t = SplitPipeLocal(line.c_str());
        if (t.size() < 6) continue;
        std::string key = TrimSpace(t[0]);
        if (key.empty() || key == "key") continue;             // header
        TeamCarRow r;
        std::string season = TrimSpace(t[1]);
        r.season      = (season == "*" || season.empty()) ? 0 : atoi(season.c_str());
        r.unit        = TrimSpace(t[2]);
        r.strength    = atof(t[3].c_str());
        std::string rel = TrimSpace(t[4]);
        r.reliability = rel.empty() ? -1.0 : atof(rel.c_str());
        r.liveries    = SplitSemi(t[5]);
        if (r.unit.empty()) continue;
        if (r.strength < -1.0) r.strength = -1.0;              // the format promises [-1,+1]; that's honored here too
        if (r.strength >  1.0) r.strength =  1.0;
        g_units[key].push_back(r);
    }
    fclose(f);
    return true;
}

void ReplaceCarDispersion(std::map<std::string, CarDispersion> m) { g_disp = std::move(m); }
void ReplaceTeamCars(std::map<std::string, std::vector<TeamCarRow>> m) { g_units = std::move(m); }

void EnsureTeamCarsLoaded() {
    static bool tried = false;
    if (tried || !g_disp.empty() || !g_units.empty()) return;   // already loaded (or injected by a test) -> not overwritten
    tried = true;                                               // missing files -> everything to 1.000, no retries
    LoadCarDispersion(paths::Data("career\\series_car_dispersion.psv").c_str());
    LoadTeamCars     (paths::Data("career\\team_cars.psv").c_str());
}

int CarDispersionCount() { return (int)g_disp.size(); }
int TeamCarKeyCount()    { return (int)g_units.size(); }

//
// resolution
//

double CarShareForTier(const std::string& tier) {
    if (tier == "high") return 0.55;
    if (tier == "mid")  return 0.30;
    if (tier == "low")  return 0.12;
    return 0.0;                       // zero / unknown
}

CarDispersion CarDispersionFor(const std::string& seriesId, const std::string& className) {
    if (!seriesId.empty()) {
        auto it = g_disp.find(seriesId);          // a series can fine-tune its own row
        if (it != g_disp.end()) return it->second;
    }
    if (!className.empty()) {
        auto it = g_disp.find(className);
        if (it != g_disp.end()) return it->second;
    }
    return CarDispersion{};                       // found=false -> not a single write
}

std::vector<TeamCarRow> TeamCarRowsFor(const std::string& seriesId, const std::string& className, int season) {
    const std::vector<TeamCarRow>* src = nullptr;
    if (!seriesId.empty())  { auto it = g_units.find(seriesId);  if (it != g_units.end()) src = &it->second; }
    if (!src && !className.empty()) { auto it = g_units.find(className); if (it != g_units.end()) src = &it->second; }
    if (!src) return {};
    // '*' is the base, and the EXACT year overrides unit by unit: a partial block for one specific year can shift
    // a single season's order of strengths (the 2009 Brawn) without duplicating the whole block.
    std::vector<TeamCarRow> out;
    std::map<std::string, size_t> idx;
    for (const auto& r : *src) {
        if (r.season != 0) continue;
        idx[r.unit] = out.size();
        out.push_back(r);
    }
    if (season != 0) {
        for (const auto& r : *src) {
            if (r.season != season) continue;
            auto it = idx.find(r.unit);
            if (it != idx.end()) out[it->second] = r;
            else { idx[r.unit] = out.size(); out.push_back(r); }
        }
    }
    return out;
}

std::string LiveryStem(const std::string& liveryName) {
    std::string s = TrimSpace(liveryName);
    bool changed = true;
    while (changed) {
        changed = false;
        // 1) trailing parenthetical: "... (NIMBUS_MASTERS_5890 2024)" -> "..."
        if (!s.empty() && s[s.size()-1] == ')') {
            size_t open = s.rfind('(');
            if (open != std::string::npos && open > 0) { s = TrimSpace(s.substr(0, open)); changed = true; continue; }
        }
        // 2) trailing race number: "... #7" -> "..."  (the '#' can be attached or spaced)
        size_t i = s.size();
        while (i > 0 && s[i-1] >= '0' && s[i-1] <= '9') i--;
        if (i < s.size() && i > 0 && s[i-1] == '#') {
            std::string head = TrimSpace(s.substr(0, i-1));
            if (!head.empty()) { s = head; changed = true; continue; }
        }
    }
    return s;
}

//
// application
//

int ApplyTeamCarScalars(const std::string& seriesId, const std::string& className, int season,
                        std::vector<GridDriver>& grid) {
    for (auto& d : grid) { d.weight_scalar = 1.0; d.power_scalar = 1.0; d.drag_scalar = 1.0; }
    if (grid.empty()) return 0;
    EnsureTeamCarsLoaded();

    const CarDispersion cd = CarDispersionFor(seriesId, className);
    if (!cd.found) return 0;                       // class with no row -> usual behavior
    if (cd.tier == "zero") return 0;               // spec-series/kart/school: EXACTLY ZERO, not a single write
    double amp = cd.amp_pct;
    if (amp > kCarAmpCapPct) amp = kCarAmpCapPct;  // conservative launch cap (spec)
    if (amp <= 0.0) return 0;
    const double A = amp / 100.0;
    double trimPct = cd.trim_pct;
    if (trimPct > kCarTrimCapPct) trimPct = kCarTrimCapPct;    // same conservative criterion as amp_pct
    const double T = trimPct / 100.0;

    const std::vector<TeamCarRow> rows = TeamCarRowsFor(seriesId, className, season);
    if (rows.empty()) return 0;                    // class ranked but not yet authored -> 1.000

    const bool authored = (cd.unit_src == "authored");
    const std::map<std::string, size_t> byKey = BuildUnitIndex(rows, authored);

    std::vector<double> s(grid.size(), 0.0);
    std::vector<double> trim(grid.size(), 0.0);
    int covered = 0;
    for (size_t i = 0; i < grid.size(); i++) {
        const std::string key = UnitKeyFor(grid[i].livery_name, authored);
        auto it = byKey.find(key);
        if (it != byKey.end()) {
            s[i] = rows[it->second].strength; covered++;   // no row -> 0.000 (clean degradation)
            // character is seeded with the UNIT's name, not the driver's key. With
            // with unit_src=authored the key IS the livery ("Eskdale Orn317"), so seeding with it gave
            // two teammates a DIFFERENT character -- exactly what this module exists to prevent
            // (teammates share a car). test_team_car_scalars caught it on the first try.
            trim[i] = TrimForUnit(rows[it->second].unit);
        }
    }
    if (covered == 0) return 0;

    // RE-CENTERED on the grid as ACTUALLY composed: the mean of the cars actually written is subtracted, so
    // a partial file (or a grid truncated to 12 of 20) doesn't inflate or sink the whole class. The mean
    // is per CAR, not per unit: a three-car unit weighs three. That way mean(weight_scalar) = exactly 1.000.
    double mean = 0.0, meanTrim = 0.0;
    for (double v : s) mean += v;
    for (double v : trim) meanTrim += v;
    mean /= (double)s.size();
    meanTrim /= (double)trim.size();

    int applied = 0;
    for (size_t i = 0; i < grid.size(); i++) {
        const double v = s[i] - mean;
        const double c = trim[i] - meanTrim;
        // per spec/investigation, weight carries the FULL amplitude (it's the only scalar that pays off
        // everywhere: 0.26 s/% at Northhurst Park and 0.24 at Coldvale Park), and drag gets HALF (0.09 and 0.01: it evaporates
        // without top speed). Sign: more strength = less weight, more power, less drag.
        // 2026-07-28 -- on top of that comes CHARACTER (`c`), the second axis, which doesn't touch strength: it trades
        // power+drag against weight, so it nets to zero on average and only changes WHERE the car performs.
        grid[i].weight_scalar = ClampScalar(1.0 - v * A       + c * T * kCarTrimWeightCost);
        grid[i].power_scalar  = ClampScalar(1.0 + v * A       + c * T);
        grid[i].drag_scalar   = ClampScalar(1.0 - v * A * 0.5 - c * T);
        if (std::fabs(grid[i].weight_scalar - 1.0) > 5e-4 ||
            std::fabs(grid[i].power_scalar  - 1.0) > 5e-4 ||
            std::fabs(grid[i].drag_scalar   - 1.0) > 5e-4) applied++;
    }
    return applied;
}

//
// reliability (spec: the owner is the TEAM)
//

double ReliabilityForStrength(const CarDispersion& cd, double strength) {
    double s = strength;
    if (s < -1.0) s = -1.0;                       // the format promises [-1,+1]; that's honored here too
    if (s >  1.0) s =  1.0;
    double top = cd.rel_top, bot = cd.rel_bot;
    if (top < bot) { double t = top; top = bot; bot = t; }   // inverted row -> the best material isn't punished
    // LINEAR interpolation over the class's range: -1 (worst material) = rel_bot, +1 = rel_top, 0 = the midpoint.
    // It's only the DEFAULT: the reliability column in team_cars.psv overrides the correlation by hand wherever history
    // calls for it (the 2019 Stonefield Motors was fast and fragile), because pace and reliability are different axes.
    return ClampRel(bot + (top - bot) * (s + 1.0) * 0.5);
}

int ApplyTeamCarReliability(const std::string& seriesId, const std::string& className, int season,
                            std::vector<GridDriver>& grid) {
    if (grid.empty()) return 0;
    EnsureTeamCarsLoaded();

    const CarDispersion cd = CarDispersionFor(seriesId, className);
    if (!cd.found) return 0;                       // class with no row -> the archetype rules (previous behavior)
    // cd.tier is NOT checked: a spec class doesn't disperse PACE (scalars stay at exactly 1.000), but its cars still
    // have a class reliability, and it's the same for all of them because the car is the same. Different axes.
    const std::vector<TeamCarRow> rows = TeamCarRowsFor(seriesId, className, season);
    if (rows.empty()) return 0;                    // class ranked but not yet authored -> the archetype

    const bool authored = (cd.unit_src == "authored");
    const std::map<std::string, size_t> byKey = BuildUnitIndex(rows, authored);

    int applied = 0;
    for (auto& d : grid) {
        auto it = byKey.find(UnitKeyFor(d.livery_name, authored));
        if (it == byKey.end()) continue;           // unrecognized unit -> the archetype's value is kept
        const TeamCarRow& r = rows[it->second];
        // An explicit row override beats interpolation over the class range. Unit teammates land on
        // the SAME row, so they share the number by construction (no need to equalize them afterward).
        d.reliability = (r.reliability >= 0.0) ? ClampRel(r.reliability)
                                               : ReliabilityForStrength(cd, r.strength);
        applied++;
    }
    return applied;
}

double DriverSpreadPct(const std::string& seriesId, const std::string& className, double spreadTotalPct) {
    if (spreadTotalPct <= 0.0) return spreadTotalPct;
    EnsureTeamCarsLoaded();
    const CarDispersion cd = CarDispersionFor(seriesId, className);
    if (!cd.found || cd.tier == "zero") return spreadTotalPct;   // the car contributes nothing -> the driver contributes everything
    double carPart = cd.amp_pct;
    if (carPart > kCarAmpCapPct) carPart = kCarAmpCapPct;
    if (carPart <= 0.0) return spreadTotalPct;
    // What gets deducted is what the car will ACTUALLY contribute (already capped), not the theoretical car_share: with
    // the cap at 2.0 there are 12 'high' classes whose honest amp gets cut in half or more, and deducting the
    // full 55% would hand out a spread nobody actually writes. Raising the cap lets F1 give up more without touching anything.
    // Defensive floor: the split never takes more than car_share says (guards against a badly derived amp_pct).
    double rest = spreadTotalPct - carPart;
    const double floorPct = spreadTotalPct * (1.0 - CarShareForTier(cd.tier));
    if (rest < floorPct) rest = floorPct;
    return rest;
}

}  // namespace career
