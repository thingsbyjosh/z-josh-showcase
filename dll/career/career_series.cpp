#include "career_series.h"
#include "career_content.h"   // DLC product overrides (Settings>Content) in the effective install
#include "career_weather.h"   // ParseWeatherList
#include "paths.h"            // mojibake fix 22/07: FOpen (UTF-8 -> wide)
#include <cstdio>
#include <cstdlib>
#include <cstdint>   // from the portal: INT32_MIN/INT32_MAX (LoadSeriesCars)
#include <cctype>
#include <cmath>
#include <ctime>
#include <mutex>
#include <algorithm>

namespace career {

// fwd: clears CalendarForSeriesInYear's cache (career_calendar.h/.cpp) -- declared here instead of
// #include-ing "career_calendar.h" so as not to drag its heavy includes (career_season.h/career_engine.h/
// career_results.h/career_econ.h) into this low-level module; same spirit as the SaveData forward-declare in
// career_calendar.h. Called from LoadSeries/ReplaceSeries/LoadSeriesCalendar/ReplaceCalendar (see the
// cache comment in career_calendar.cpp for why it's MANDATORY in all four places).
void InvalidateCalendarProjectionCache();

// fwd (career_fields.h): including it here would be circular — career_fields.h includes career_customai.h.
void ResetGridSizeCache();

// a PSV parser in the same spirit as career_customai.cpp's
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

// "25,18,15,..." -> vector<int>
static std::vector<int> ParseInts(const std::string& csv) {
    std::vector<int> out; std::string cur;
    for (char c : csv) { if (c == ',') { if (!cur.empty()) out.push_back(atoi(cur.c_str())); cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(atoi(cur.c_str()));
    return out;
}

// SERIES ----------
static std::vector<SeriesDef> g_series;

// access: LoadSeriesAccess has no table of its own (it mutates the already-populated g_series), so it needs its own
// module-level counter (declared here, next to g_series, so it's visible before LoadSeriesAccess runs).
// It's zeroed at the SAME point where the other loaders clear their table -- AFTER the fopen, never
// before -- so a load that fails to open the file leaves the previous count untouched, just like it leaves
// g_series untouched. It only counts rows that FOUND their series (a row naming a nonexistent series
// wasn't actually applied).
static int g_seriesAccessApplied = 0;
static int g_seriesMarketApplied = 0;
static int g_seriesScaleApplied = 0;    // 14/08: rows of series_scale.psv applied   // 13/08: rows of series_market.psv applied (diag/test)
static int g_seriesCarsApplied = 0;    // 19/09: rows of series_cars.psv applied (from the portal)

bool LoadSeries(const char* path) {
    ResetGridSizeCache();   // the series change -> the per-class size changes
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_series.clear();
    InvalidateCalendarProjectionCache();   // The series catalog changes -- see career_calendar.cpp
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 8) continue;
        if (t[0] == "series_id" || t[0].empty()) continue;   // header/empty
        SeriesDef s;
        s.id = t[0]; s.class_name = t[1]; s.display = t[2]; s.region = t[3];
        s.era = t[4]; s.profile_id = t[5]; s.points_id = t[6];
        s.rounds = atoi(t[7].c_str());
        s.branch_of = (t.size() > 8) ? t[8] : std::string();
        s.level = (t.size() > 9) ? atoi(t[9].c_str()) : 1;   // compat: 1 if the column is missing
        s.kind = (t.size() > 10 && !t[10].empty()) ? t[10] : std::string("champ");
        s.unlocks = (t.size() > 11) ? t[11] : std::string();
        s.gate_top = (t.size() > 12) ? atoi(t[12].c_str()) : 0;
        s.gate_feeder_class = (t.size() > 13) ? t[13] : std::string();
        s.gate_feeder_top   = (t.size() > 14) ? atoi(t[14].c_str()) : 0;
        g_series.push_back(s);
    }
    fclose(f);
    return true;
}

void ReplaceSeries(std::vector<SeriesDef> v) {
    g_series = std::move(v);
    InvalidateCalendarProjectionCache();   // The series catalog changes -- see career_calendar.cpp
}

bool LoadSeriesAccess(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_seriesAccessApplied = 0;   // AFTER the fopen, like the rest of the loaders (see the counter comment)
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 2) continue;
        if (t[0] == "series_id" || t[0].empty()) continue;   // header/empty
        // exact (SplitPipe already strips CR/LF)
        bool closed = (t[1] == "closed");
        for (auto& s : g_series) if (s.id == t[0]) { s.closed = closed; g_seriesAccessApplied++; break; }
    }
    fclose(f);
    return true;
}

// from the portal (19/09/2026) — the CAR RULE. Copies the LoadSeriesAccess pattern: separate file that mutates
// g_series, counter zeroed AFTER the fopen, only rows that found their series count.
bool LoadSeriesCars(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_seriesCarsApplied = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 2) continue;
        if (t[0] == "series_id" || t[0].empty()) continue;   // header/empty
        // veh_hash: 32-bit signed integer (AMS2 hashes can be negative). Non-numeric or 0 -> ignored.
        char* end = nullptr;
        const long long v = strtoll(t[1].c_str(), &end, 10);
        if (end == t[1].c_str() || v == 0 || v < INT32_MIN || v > INT32_MAX) continue;
        for (auto& s : g_series) if (s.id == t[0]) { s.car_veh_hash = (int)v; g_seriesCarsApplied++; break; }
    }
    fclose(f);
    return true;
}
int SeriesCarsRowsApplied() { return g_seriesCarsApplied; }

int OppTypeForCarRule(int carVehHash, int numClasses) {
    if (carVehHash == 0) return -1;         // no rule: auto by numClasses (same class / multiclass)
    return (numClasses <= 1) ? 0 : -1;      // single-class -> Identical; multiclass -> the rule doesn't reach the rivals
}

int OppTypeSlotValue(int numClasses, int oppType) {
    if (numClasses > 1) return kOppTypeMultiClass;   // multiclass: the per-class split requires it, oppType doesn't count
    if (oppType >= 0)   return oppType;              // explicit, RAW as always: test (0 classes) or single-vehicle (1)
    return kOppTypeSameClass;                        // auto: single-class or without a class set -> «Same class», as always
}

// 13/08 — THE MARKET TAG. User's decision: «not a cut by year in code, but a specific
// tag». Same pattern as `LoadSeriesAccess` (separate file that mutates g_series), because the rows of
// series.psv have 10, 13 or 15 columns and adding one at the end would force filling in the short ones.
//
// A series WITHOUT a row here keeps the old behavior (its year against MODERN_ERA_CUTOFF), so this
// is additive: half-migrated data doesn't change anything by surprise.
bool LoadSeriesMarket(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_seriesMarketApplied = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 2) continue;
        if (t[0] == "series_id" || t[0].empty()) continue;   // header/empty
        const int tag = (t[1] == "viva") ? 1 : (t[1] == "epoca") ? 2 : 0;
        if (!tag) continue;                                  // unknown value -> as if there were no row
        for (auto& s : g_series) if (s.id == t[0]) { s.market_tag = tag; g_seriesMarketApplied++; break; }
    }
    fclose(f);
    return true;
}

int SeriesMarketRowsApplied() { return g_seriesMarketApplied; }

// 14/08 — the economic scale per SERIES. Same pattern as the market tag (separate file that
// mutates g_series) and for the same reason. Only the EXCEPTIONS: no row, 1.0.
bool LoadSeriesScale(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_seriesScaleApplied = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 2) continue;
        if (t[0] == "series_id" || t[0].empty()) continue;
        const double v = atof(t[1].c_str());
        // Guard: an absurd value doesn't silently ruin the whole economy.
        if (v <= 0.01 || v > 100.0) continue;
        for (auto& s : g_series) if (s.id == t[0]) { s.econ_scale = v; g_seriesScaleApplied++; break; }
    }
    fclose(f);
    return true;
}

int SeriesScaleRowsApplied() { return g_seriesScaleApplied; }

const SeriesDef* SeriesById(const std::string& id) {
    for (const auto& s : g_series) if (s.id == id) return &s;
    return nullptr;
}
std::vector<const SeriesDef*> SeriesForClass(const std::string& className) {
    std::vector<const SeriesDef*> out;
    for (const auto& s : g_series) if (s.class_name == className) out.push_back(&s);
    return out;
}
const std::vector<SeriesDef>& AllSeries() { return g_series; }
int SeriesCount() { return (int)g_series.size(); }
int SeriesAccessRowsApplied() { return g_seriesAccessApplied; }   // number of rows loaded (diagnostics/test)

// POINTS ----------
static std::map<std::string, PointsSystem> g_points;

bool LoadSeriesPoints(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_points.clear();
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 5) continue;
        if (t[0] == "points_id" || t[0].empty()) continue;
        PointsSystem ps;
        ps.points = ParseInts(t[1]);
        ps.fl_point = atoi(t[2].c_str());
        ps.fl_top_n = atoi(t[3].c_str());
        ps.pole_point = atoi(t[4].c_str());
        g_points[t[0]] = ps;
    }
    fclose(f);
    return true;
}
void ReplacePoints(std::map<std::string, PointsSystem> m) { g_points = std::move(m); }

const PointsSystem* PointsById(const std::string& id) {
    auto it = g_points.find(id);
    return it != g_points.end() ? &it->second : nullptr;
}
int PointsSystemCount() { return (int)g_points.size(); }   // number of rows loaded (diagnostics/test)

// CALENDAR ----------
static std::vector<CalRound> g_calendar;

int NormalizeRoundGridSize(int value) {
    return value >= 2 && value <= 60 ? value : 0;
}

static int ParseRoundGridSize(const std::string& raw) {
    if (raw.empty()) return 0;
    char* end = nullptr;
    const long value = strtol(raw.c_str(), &end, 10);
    if (!end || end == raw.c_str() || *end != '\0') return 0;   // rejects fractions and trailing garbage
    if (value < 2 || value > 60) return 0;
    return NormalizeRoundGridSize((int)value);
}

// derives the tag from the note's free text (zero migration): "what-if"/"whatif" -> whatif; "sust" -> sust
// (non-static: declared in career_series.h; the JSON adapter reuses this SAME fallback -- for parity)
std::string DeriveTagFromNote(const std::string& note) {
    std::string n; for (char c : note) n += (char)tolower((unsigned char)c);
    if (n.find("what-if") != std::string::npos || n.find("whatif") != std::string::npos) return "whatif";
    if (n.find("sust") != std::string::npos) return "sust";
    return "real";
}

bool LoadSeriesCalendar(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_calendar.clear();
    InvalidateCalendarProjectionCache();   // The base calendar changes -- see career_calendar.cpp
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.empty() || t[0] == "series_id" || t[0].empty()) continue;
        CalRound c;
        c.series_id = t[0]; c.round = atoi(t[1].c_str());
        int base;                                   // index of the first field after (round | doy date)
        if (t.size() >= 12) {                       // NEW format: doy + date present
            c.doy = atoi(t[2].c_str()); c.date = t[3]; base = 4;
        } else if (t.size() >= 10) {                // OLD format: no date
            c.doy = 0; c.date.clear(); base = 2;
        } else continue;                            // corrupt row
        c.track_hash = (long)strtoll(t[base].c_str(), nullptr, 10);
        c.track_name = t[base + 1]; c.fmt = atoi(t[base + 2].c_str());
        c.laps = atoi(t[base + 3].c_str()); c.durationMin = atoi(t[base + 4].c_str());
        c.hour = atoi(t[base + 5].c_str());
        c.weather = ParseWeatherList(t[base + 6]);   // "" = the player decides; "8" | "0,8,12,17" = authored
        c.note = t[base + 7];
        // cols base+8 / base+9 = practiceMin / qualifyMin (numeric, 0 = default).
        c.practiceMin = (t.size() > (size_t)(base + 8) && !t[base + 8].empty()) ? atoi(t[base + 8].c_str()) : 0;
        c.qualifyMin  = (t.size() > (size_t)(base + 9) && !t[base + 9].empty()) ? atoi(t[base + 9].c_str()) : 0;
        // As of Jul 28, col base+10 = pitkmh PER ROUND (0/absent = the profile's value wins). For tracks with their own
        // rules: Blackgate Speedway races with a pit limit lower than F1's general one (and a reduced distance via an
        // explicit exception to the 305 km minimum). It's a CIRCUIT fact, not a category one, so it doesn't belong in the profile.
        c.pitKmh      = (t.size() > (size_t)(base + 10) && !t[base + 10].empty()) ? atoi(t[base + 10].c_str()) : 0;
        // Final column base+11. Empty/0 and any invalid value inherit defensively.
        c.gridSize    = (t.size() > (size_t)(base + 11)) ? ParseRoundGridSize(t[base + 11]) : 0;
        c.tag = DeriveTagFromNote(c.note);   // tag always note-derived (no explicit tag column)
        g_calendar.push_back(c);
    }
    fclose(f);
    return true;
}
void ReplaceCalendar(std::vector<CalRound> v) {
    g_calendar = std::move(v);
    InvalidateCalendarProjectionCache();   // The base calendar changes -- see career_calendar.cpp
}
const std::vector<CalRound>& AllCalendarRounds() { return g_calendar; }

std::vector<CalRound> CalendarForSeries(const std::string& seriesId) {
    std::vector<CalRound> out;
    for (const auto& c : g_calendar) if (c.series_id == seriesId) out.push_back(c);
    std::sort(out.begin(), out.end(), [](const CalRound& a, const CalRound& b){ return a.round < b.round; });
    return out;
}

// PROFILES ----------
std::string ConfigProfile::get(const std::string& key, const std::string& def) const {
    auto it = fields.find(key);
    return it != fields.end() ? it->second : def;
}

static std::map<std::string, ConfigProfile> g_profiles;

bool LoadProfiles(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    g_profiles.clear();
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.size() < 2) continue;
        if (t[0] == "profile_id" || t[0].empty()) continue;
        ConfigProfile p; p.id = t[0];
        // t[1] = "k=v;k=v;..."
        std::string body = t[1];
        body += ';';   // sentinel to close the last pair
        std::string k, v; bool inVal = false;
        for (char c : body) {
            if (c == ';') {
                if (!k.empty()) p.fields[k] = v;
                k.clear(); v.clear(); inVal = false;
            } else if (c == '=' && !inVal) { inVal = true; }
            else { if (inVal) v += c; else k += c; }
        }
        g_profiles[p.id] = p;
    }
    fclose(f);
    return true;
}
void ReplaceProfiles(std::map<std::string, ConfigProfile> m) { g_profiles = std::move(m); }

const ConfigProfile* ProfileById(const std::string& id) {
    auto it = g_profiles.find(id);
    return it != g_profiles.end() ? &it->second : nullptr;
}
int ConfigProfileCount() { return (int)g_profiles.size(); }   // number of rows loaded (diagnostics/test)

std::string ProfileField(const std::string& profileId, const std::string& key) {
    const ConfigProfile* p = ProfileById(profileId);
    return p ? p->get(key) : std::string();
}

int StartTypeForSeries(const std::string& seriesId) {
    const SeriesDef* s = SeriesById(seriesId);
    if (!s) return 0;
    // Only "roll" forces a rolling start; any other value (standing/absent/unknown) -> grid start (safe default).
    return ProfileField(s->profile_id, "start") == "roll" ? 1 : 0;
}

// Reads a NUMERIC key from the profile; "" (absent or no profile) -> -1 (don't force).
static int ProfileInt(const std::string& profileId, const std::string& key) {
    std::string v = ProfileField(profileId, key);
    return v.empty() ? -1 : atoi(v.c_str());
}
// Per-CATEGORY default for the pit limit (km/h) when the event has no real per-track data (read at
// runtime from PitSpeedLimit_HighKPH) and no "pitkmh" override in the profile. Physically, most circuits sit at 60;
// modern F1 at 80; karts lower. The real per-track value, when available, overrides this one.
static int PitKmhDefaultForClass(const std::string& className) {
    std::string c = className; for (char& ch : c) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
    auto has = [&](const char* k){ return c.find(k) != std::string::npos; };
    if (has("kart"))                                              return 50;   // karts: slow pit lane
    if (has("ultimate") || has("reiza") || has("hitech") || has("v10")) return 80;   // modern F1 / top single-seater
    return 60;   // GT, touring, stock car, prototypes, junior formulas, historics: standard LANTERN_SERIES_1872 limit
}
RaceRules RaceRulesForSeries(const std::string& seriesId) {
    RaceRules r;   // everything defaults to -1 (no series/profile -> force nothing)
    const SeriesDef* s = SeriesById(seriesId);
    if (!s) return r;
    r.formationLap = ProfileInt(s->profile_id, "formlap");
    r.mandatoryPit = ProfileInt(s->profile_id, "pitreq");
    r.refuelling   = ProfileInt(s->profile_id, "refok");
    r.minTyres     = ProfileInt(s->profile_id, "mintyres");
    r.pitSpeedKmh  = ProfileInt(s->profile_id, "pitkmh");          // data-driven override from the profile (if it exists)
    if (r.pitSpeedKmh < 0) r.pitSpeedKmh = PitKmhDefaultForClass(s->class_name);   // per-category fallback
    return r;
}

// As of Jul 28: adjusts the series' BASE rule to the round's ACTUAL format. See the long comment in the header.
// The asymmetry is deliberate: endurance ADDS a stop (without it the race is impossible) and sprint REMOVES it
// (a mandatory stop in a support race exists in no rulebook). Never the other way around.
RaceRules RaceRulesForRound(const RaceRules& base, int fmt, int pitKmhRound) {
    RaceRules r = base;
    if (fmt == 2) {                 // endurance: hours of racing -> you stop and refuel, no question
        r.mandatoryPit = 1;
        r.refuelling   = 1;         // even if the category forbade it in its era: a 6-hour race with no refueling doesn't happen
        if (r.minTyres < 0) r.minTyres = 0;   // the stop is mandatory; changing tires is strategy
    } else if (fmt == 0) {          // sprint: short support race -> no mandatory stop
        if (r.mandatoryPit == 1) { r.mandatoryPit = 0; r.minTyres = 0; }
    }
    if (pitKmhRound > 0) r.pitSpeedKmh = pitKmhRound;   // the circuit overrides the category
    return r;
}

void WeatherSlotsForEvent(int calWeather, const std::string& profileWx, int out[4]) {
    (void)profileWx;                              // AGREED RULE: ALL races are random unless the calendar sets an EXPLICIT weather.
    int v;
    if (calWeather >= 0) v = calWeather;          // an explicit weather value from the real event WINS (rain/fog/17; weather=0 arrives as -1)
    else v = 17;                                  // GLOBAL DEFAULT = random; the profile's "dry" wx setting no longer forces sun.
    for (int i = 0; i < 4; i++) out[i] = v;
}

// 4 random weather slots across the whole session is too many. The number of slots (1..4) scales to the
// session's DURATION, with randomness within the band: <=15 min (short quali) 1-2; <=60 min 1-3; <=120 min 1-4;
// >120 min always 4. sessionMin<=0 (LAPS mode / unknown duration) = typical sprint -> band 1-3.
int WeatherSlotCountForSession(int sessionMin) {
    if (sessionMin > 120) return 4;
    static unsigned rng = 0;
    if (!rng) rng = (unsigned)time(nullptr) | 1u;                 // lazy seed, never 0
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;          // xorshift32
    int lo = 1, hi;
    if      (sessionMin > 60)                    hi = 4;
    else if (sessionMin > 15 || sessionMin <= 0) hi = 3;
    else                                         hi = 2;
    return lo + (int)(rng % (unsigned)(hi - lo + 1));
}

// COMPLETE TABLE of time progression (VM550 s27), MEASURED in-game on 1.6.9.95 on 12/09/2026
// walking the selector from top to bottom and reading the slot at each step. The four in the middle
// (x15/x20/x25/x40) were ASSUMED in the docs and are now measured; x60 didn't exist in
// any table of ours. See docs/2026-06-24-version-migration-vm-slots-RUNBOOK.md.
// (The slot's value 0 is NOT confirmed: the game doesn't let you select "off". Don't use it.)
static const int kTimeProgMult[11] = { 1, 2, 5, 10, 15, 20, 25, 30, 40, 50, 60 };

int TimeProgMultiplier(int idx) {
    if (idx < 1 || idx > 11) return 0;
    return kTimeProgMult[idx - 1];
}

// RULE (Josh's decision, 12/09): the smallest step that COVERS the requested ratio -- rounding
// UP, never below. The goal is that in the shortened race ALL the hours of the real race
// are covered: if you fall short, you miss part of the day. "When in doubt, one step more."
// Accepted consequence: at high percentages you overshoot (at 75% -> x2, you see 1.5 days); in exchange
// you NEVER see less of the day than was due.
static int TimeProgForRatio(double ratio) {
    if (!(ratio > 1.0)) return 1;                        // not shortened (or NaN) -> real time
    for (int i = 0; i < 11; ++i)
        if (kTimeProgMult[i] >= ratio - 1e-9) return i + 1;
    return 11;                                           // above x60 there is no step: it saturates
}

// s27 index for a duration %. pct<=0 or >=100 -> 1 (Real time; explicit, so as not to inherit the
// progression of the previous meeting). Used by the UI for the reading "24h - x20 - 5%".
int PctToTimeProg(int pct) {
    if (pct <= 0 || pct >= 100) return 1;
    return TimeProgForRatio(100.0 / pct);
}

// Applies the duration % to the event's copy. TIME mode (durationMin>0): scales the minutes
// (floor, minimum 1) and returns the time-acceleration index to force. LAPS mode (durationMin<=0): scales the laps
// (lround, minimum 1) and returns 0 (don't force time-acceleration). pct outside [25,100] -> 100. See career_series.h.
int ApplyDurationPercent(int pct, int& durationMin, int& laps) {
    // A race's minimum is set by the MODE, not a % threshold: 3 laps (laps mode) or 8 minutes (time mode).
    // The UI DISABLES the % buttons that would fall below that (see DUR_MIN_LAPS/DUR_MIN_MIN); this is only a
    // safety backstop. The old floor, "pct<25 -> 100", was the "10% -> full race" bug (200-lap HARBOR_CHALLENGE_8227).
    if (pct <= 0 || pct > 100) pct = 100;      // just a technical range
    if (durationMin <= 0) {                     // laps-mode race
        if (laps > 0) {
            laps = (int)std::lround(laps * pct / 100.0);
            if (laps < 3) laps = 3;             // minimum 3 laps
        }
        return 0;                               // don't force time-acceleration
    }
    const int full = durationMin;               // REAL duration of the round, before shortening
    durationMin = durationMin * pct / 100;      // floor (integer)
    if (durationMin < 8) durationMin = 8;       // minimum 8 min
    // EFFECTIVE ratio, not the requested %: if the 8 min floor trims the shortening, the clock must not
    // speed up more than the race has REALLY shrunk (a 20 min one at 25% ends at 8, not 5).
    return TimeProgForRatio((double)full / (double)durationMin);
}

// VALIDATION ----------
std::vector<std::string> ValidateSeriesData() {
    std::vector<std::string> errs;
    for (const auto& s : g_series) {
        if (!PointsById(s.points_id)) errs.push_back("series '" + s.id + "': unknown points_id '" + s.points_id + "'");
        if (!ProfileById(s.profile_id)) errs.push_back("series '" + s.id + "': unknown profile_id '" + s.profile_id + "'");
        if (!s.branch_of.empty() && !SeriesById(s.branch_of)) errs.push_back("series '" + s.id + "': branch_of does not exist '" + s.branch_of + "'");
        if (!s.unlocks.empty() && !SeriesById(s.unlocks))
            errs.push_back("series '" + s.id + "': unlocks does not exist '" + s.unlocks + "'");
        if (!s.unlocks.empty() && s.gate_top <= 0)
            errs.push_back("series '" + s.id + "': qualifier '" + s.id + "' without valid gate_top");
        if (!s.gate_feeder_class.empty() && s.gate_feeder_top <= 0)
            errs.push_back("series '" + s.id + "': gate_feeder_class without valid gate_feeder_top");
        if (s.gate_feeder_class.empty() && s.gate_feeder_top > 0)
            errs.push_back("series '" + s.id + "': gate_feeder_top without gate_feeder_class");
    }
    for (const auto& c : g_calendar) {
        if (!SeriesById(c.series_id)) errs.push_back("orphan calendar: series '" + c.series_id + "' (round " + std::to_string(c.round) + ")");
        if ((c.laps <= 0) == (c.durationMin <= 0)) errs.push_back("calendar '" + c.series_id + "' round " + std::to_string(c.round) + ": laps XOR time wrong");
    }
    // doy/date stay consistent when the round has a date (doy>0 <=> date non-empty; doy in range)
    for (const auto& c : g_calendar) {
        if (c.doy != 0 && (c.doy < 1 || c.doy > 366))
            errs.push_back("doy out of range in " + c.series_id + " R" + std::to_string(c.round));
        if ((c.doy > 0) != (!c.date.empty()))
            errs.push_back("doy/date inconsistent in " + c.series_id + " R" + std::to_string(c.round));
        if (c.practiceMin < 0 || c.practiceMin > 240)
            errs.push_back("cal '" + c.series_id + "' R" + std::to_string(c.round) + ": practiceMin out of [0,240]");
        if (c.qualifyMin < 0 || c.qualifyMin > 240)
            errs.push_back("cal '" + c.series_id + "' R" + std::to_string(c.round) + ": qualifyMin out of [0,240]");
    }
    return errs;
}

std::string DiscForSeries(const SeriesDef* sd) {
    if (!sd) return "single";
    // As of Aug 24: explicit curation by series_id, checked BEFORE the heuristic. The matcher below
    // only sees profile_id+class_name (feeding it the whole series_id would break nimbus_challenge_4988: "nimbus_masters_5890" => endurance),
    // and with generic profiles ("grid_nopit_fuel_60") 51 series fell back to the "single" default: the calendar
    // was painting Brackenridge Wyn710, Oakfield Motors Lum695, Onyx Cup 12409, Group C, Juniper Trophy... as SINGLE-SEATERS. Only the
    // ones the heuristic misclassifies get curated; the rest (every formula under the default) keeps being derived.
    // "endurance" carries behavior beyond just a label (audience fmt=2, the multiclass sportscar gate) --
    // and as of Aug 25 it's intentional: the Brazilian prototypes ARE in kCuratedGroups (P1..P4
    // share the calendar's 15 weekends and race TOGETHER, split evenly).
    static const struct { const char* id; const char* disc; } kDiscCurated[] = {
        { "onyx_masters_12204",            "touring" }, { "aurora_trophy_11261",           "touring" },
        { "granite_championship_11220",         "touring" }, { "harbor_series_11343",           "touring" },
        { "granite_cup_5808",           "touring" }, { "lantern_cup_8145",           "touring" },
        { "group_a",            "touring" },   // (coldstead_motors_g3 used to be curated here; the street series was pulled from the roster on Aug 28)
        { "harbor_masters_9416",        "touring" }, { "juniper_series",         "touring" },
        { "granite_series",       "touring" },
        { "juniper_cup_3635",        "entry"   },
        { "keystone_trophy_3266",             "single"  }, { "granite_cup_3307",             "single"  },   // "F-V8" was falling into the "v8" keyword (touring)
        // Tester feedback (Aug 27): the Caterhams are the ENTRY rung of the ladder -- painting them GT was pulling them
        // out of their family in the calendar. And Greywick Motors is de facto rallycross, not entry-level karting.
        { "granite_masters","entry"   }, { "onyx_challenge",      "entry"   },
        { "greywick_motors",          "offroad" },
        { "keystone_challenge_3676",        "gt"      }, { "granite_trophy_10359",       "gt"      },
        { "juniper_trophy_10400",  "gt"      }, { "granite_cup_12081",        "gt"      },
        { "granite_challenge",        "gt"      }, { "granite_trophy_7817",          "gt"      },
        { "ember_challenge_7038",          "endurance" }, { "group_c",          "endurance" },
        { "onyx_championship_8842",              "endurance" }, { "juniper_masters_8924",            "endurance" },
        { "ember_series_8965",              "endurance" }, { "onyx_championship_9006",            "endurance" },
        { "aurora_masters",             "endurance" }, { "pinnacle_masters",           "endurance" },   // Endurance Brasil multiclass (29/08)
        { "cinder_series",             "endurance" }, { "nimbus_championship",           "endurance" },
        { "rallycross",         "offroad" }, { "beacon_series_10441","offroad" },
        // package a69f1ccb (23/09): portal series whose class the heuristic doesn't catch (KEYSTONE_MASTERS_11138/KEYSTONE_TROPHY_11302,
        // "group a"/"group c" with a space match no kw). Same discipline as the main one of their class.
        { "JUNIPER_MASTERS_10974",   "touring" }, { "JUNIPER_SERIES_11015",  "touring" },
        { "HARBOR_MASTERS_11056",         "touring" }, { "BEACON_SERIES_5398",         "touring" },
        { "Harbor_Challenge_3881",          "touring" }, { "ONYX_CHAMPIONSHIP_3922",          "endurance" },
    { "Onyx_Challenge_11999", "endurance" },   // [tester]: Masters Group C historic series
    };
    for (const auto& r : kDiscCurated) if (sd->id == r.id) return r.disc;
    std::string s = sd->profile_id + " " + sd->class_name;
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    auto has = [&](const char* k){ return s.find(k) != std::string::npos; };
    if (has("kart") || has("academy") || has("oakfield_motors") || has("blackley_motors")) return "entry";
    if (has("endur") || has("ivory_cup_12573") || has("nimbus_masters_5890") || has("pemberton_zel121") || has("lmp") || has("dpi") || has("group_c") || has("proto") || has("hypercar") || has("merrowdown_myr373")) return "endurance";
    if (has("stock") || has("harbor_challenge_8227") || has("copa") || has("greyham_motors") || has("truck") || has("touring") || has("v8")) return "touring";
    if (has("formula") || has("f1") || has("fhybrid") || has("ultimate") || has("indy") || has("garrowby") || has("vee") || has("inter") || has("fusa") || has("single")) return "single";
    if (has("gt")) return "gt";
    if (has("rally") || has("dirt") || has("offroad") || has("cross")) return "offroad";
    return "single";
}

// CURATED multiclass (see header). Hardcoded default (same spirit as DiscForSeries: curating
// series metadata in C++). Derived from the calendar's real co-locations, with realism applied:
// distinct championships that only share a circuit+weekend (IVORY_CUP_12573 vs NIMBUS_MASTERS_5890 at Ashwick Ring, Valemont Kor951, support races for
// Highgate, Group C) stay single-class. Invariant: every series_id belongs to <=1 group (test_multiclass_groups_data).
//
// `total` = CARS ON TRACK for the meeting, split evenly among the classes present. It's the
// CHAMPIONSHIP's size, not the sum of its classes: the IVORY_CUP_12573 grids 36, not the 56 you'd get adding 18 Pemberton_Zel121 + 22 LANTERN_MASTERS_7489
// + 16 MERROWDOWN_MYR373. The game's own grid-builder fix already divides total/n; what
// was missing was the total. Approved Aug 6: 36/3=12, 30/3=10, 28/4=7, 22/2=11, 34/2=17, 26/2=13. Aug 25: 32/4=8.
struct MulticlassGroupDef { int total; std::vector<std::string> members; };
static const std::vector<MulticlassGroupDef> kCuratedGroups = {
    // Aug 6 -- GARAGE 56 IS OUT. It used to sit here as the IVORY_CUP_12573's fourth class, and with an even split
    // across the multiclass weekend it sank the entire group: it's a SINGLE guest car from Highgate 2023 with only 2
    // liveries, so the split got capped at 2 per class and the IVORY_CUP_12573 came out with 8 cars instead of 36.
    // The class still exists and can be raced on its own; what it can't do is split the grid as if it were
    // a full championship.
    // 19/09 (Josh: «IVORY_CUP_12573 with GT3 makes more sense») — THE IVORY_CUP_12573 MOVES TO THE 2024 FORMAT: Hypercar + LMGT3, 18 per class
    // (36/2), which is the real grid and fits all its tracks (the tightest, Fairworth Speedway, has 40 slots).
    // LANTERN_MASTERS_7489 and MERROWDOWN_MYR373 leave the event: the real IVORY_CUP_12573 dropped LANTERN_MASTERS_7489 in 2024 (except Highgate) and MERROWDOWN_MYR373 in 2023.
    { 36, {"ivory_trophy_7202","beacon_championship_5070"} },                           // IVORY_CUP_12573 Hypercar+LMGT3
    // 19/09 (Josh: «as faithful as possible to the series in real life») — NIMBUS_MASTERS_5890's LANTERN_MASTERS_7489 is lantern_trophy_7694 (class LANTERN_MASTERS_7489, liveries
    // and grid of NIMBUS_MASTERS_5890 2024), not beacon_series_7653 (Drift_Series_7735, European grid of 2018-19): they were crossed.
    { 30, {"harbor_challenge_7161","lantern_trophy_7694","nimbus_challenge_4988"} },               // NIMBUS_MASTERS_5890 PINNACLE_CHALLENGE_5480+LANTERN_MASTERS_7489+CINDER_SERIES_5357
    // 19/09 — the 2019 IVORY_CUP_12573, PERIOD: LANTERN_MASTERS_7489 + MERROWDOWN_MYR373, the two classes of that year that AMS2 has (not its LMP1). 10 per
    // class: the Drift_Series_7735 class has only 10 liveries and the equal split is capped by the shortest.
    { 20, {"beacon_series_7653","merrowdown_myr373"} },                                     // IVORY_CUP_12573 2019: LANTERN_MASTERS_7489+MERROWDOWN_MYR373
    { 28, {"ivory_championship_7448","keystone_championship_7612","cinder_series_4291","ivory_cup_4455"} },  // PINNACLE_MASTERS_7243/Highgate 2005: LMP1+LANTERN_MASTERS_7489+GT1+GT2
    { 22, {"onyx_championship_5603","pinnacle_masters_4414"} },                               // ONYX CUP 1995: GT1+GT2
    { 34, {"onyx_cup_4742","onyx_cup_5193"} },                                // Drift Cup style: GT3+GT4
    { 26, {"pinnacle_series_4783","dpi"} },                                   // Harbor Series 5931 (2017-22): DPi+CINDER_SERIES_5357
    // Endurance Brasil «On Tour»: the multiclass with its OWN championship. Four ebr_* branches co-dated with
    // their own 8-round BR calendar, in gaps between the home family's weekends. The scoring
    // chaos (only 8 of the 14-16 of each class enter each meeting) is accepted by design.
    { 32, {"aurora_masters","pinnacle_masters","cinder_series","nimbus_championship"} },                 // Drift Championship: P1+P2+P3+P4
    // Endurance Brasil «at home» — NEW DECISION BY JOSH, 16/09, WHICH CHANGES THE ONE FROM 29/08.
    //
    // The one from 29/08 said BOTH EXPERIENCES: these four (onyx_championship_8842, juniper_masters_8924, ember_series_8965, onyx_championship_9006) were NEVER merged
    // — each lined up its whole grid of 14-16, «which is what people enjoy» — and the multiclass
    // lived only in the ebr_*. It was not an oversight and it stood for three weeks.
    //
    // It changes because when measuring the REAL slots of the tracks (16/09) it turned out the four share the
    // exact same FIFTEEN rounds —same track and same day, and nobody else runs those dates—, i.e. they were already one
    // event in everything but the split, and that their circuits hold the 40 cars in 10 of the 15. Josh
    // approved it with those figures in front of him: 40 on ten dates, 32 at Northvale_Park/Northstead/Pinestead_Ring/Longbourne_Ring and 28 at
    // Millwood Ring, which is where the track cap trims. Total 40 = ten per class; liveries don't cap
    // (the shortest class, P2, has 15 and the split asks for 10).
    { 40, {"onyx_championship_8842","juniper_masters_8924","ember_series_8965","onyx_championship_9006"} },                     // Endurance Brasil at home: P1+P2+P3+P4
    // package a69f1ccb (2026-09-23). Total = SUM of the curated grids of the classes in the portal's seven groups; the split is capped by the shortest class and each round is trimmed to the slots
    // of ITS track ([tester], 25/09: "max grid possible per track"). The first cut (the tightest track on the calendar, VIR 22, Northford
    // 26...) capped ALL the rounds by the worst one; that number stays in each comment only as a measurement.
    // EMBER_TROPHY_106462026TC (EMBER_TROPHY_10646 TC AND GT): sum of classes 50 (KEYSTONE_MASTERS_11138=30, KEYSTONE_CHALLENGE_11179=20); tightest track Redcombe Park = 22 slots
    { 50, {"JUNIPER_MASTERS_10974","JUNIPER_SERIES_11015"} },
    // package a69f1ccb (2026-09-23): IVORY_CHAMPIONSHIP_5685 (EMBER_TROPHY_10646 TC AND GT): sum of classes 80 (Falcon_Masters_9867=40, Ivory_Championship_9908=40); tightest track Marchwood = 32 slots; total 32 (multiple of 2)
    { 80, {"Harbor_Trophy_5726","Falcon_Series_5767"} },
    // package a69f1ccb (2026-09-23): F1-1967-69HISTORIC (Formula Historics): sum of classes 40 (Falcon_Masters=20, Aurora_Masters=20); tightest track Northvale = 26 slots; total 26 (multiple of 2)
    { 40, {"Keystone_Championship_1257","Onyx_Masters_1298"} },
    // package a69f1ccb (2026-09-23): TheMasters (Formula Historics): sum of classes 72 (Lantern_Cup=22, Ivory_Series=24, Ivory_Cup=26); tightest track Kingsley Ring = 32 slots; total 30 (multiple of 3)
    { 72, {"Aurora_Challenge_11917","Aurora_Masters_11958","Lantern_Masters_11876"} },
    // package a69f1ccb (2026-09-23): BritishGT2012 (Drift Cup): sum of classes 105 (GT3=34, GT4=45, Ember_Cup_5439=26); tightest track Westvale Speedway = 26 slots; total 24 (multiple of 3)
    { 105, {"Drift_Trophy","Drift_Series","Drift_Masters"} },
    // package a69f1ccb (2026-09-23): BritishGT04 (Drift Cup): sum of classes 28 (GRANITE_MASTERS_4373=12, HARBOR_MASTERS_5562=16); tightest track Westvale Speedway = 26 slots; total 26 (multiple of 2)
    { 28, {"Nimbus_Cup","Falcon_Challenge"} },
    // package a69f1ccb (2026-09-23): onyx_cup_4824_KEYSTONE_TROPHY (Drift Cup modern): sum of classes 85 (Onyx_Cup_4824=40, GT4=45); tightest track Westvale Speedway = 26 slots; total 26 (multiple of 2)
    { 85, {"Cinder_Trophy_4865","Ember_Trophy_4906"} },
    // package b8f9ce3f (2026-09-28): Mastersenduro96_06 (The Master's GT classes): total = sum of the classes (the split is capped by the shortest class and each round by its track); measured: sum of classes 95 (IVORY_CHAMPIONSHIP_7407=12, CINDER_CHAMPIONSHIP_7571=12, HARBOR_MASTERS_5562=16, PINNACLE_CHALLENGE_4250=16, GRANITE_MASTERS_4373=12, GT1=27); tightest track Fairworth Speedway = 40 slots; total 36 (multiple of 6)
    { 95, {"Drift_Trophy_8022","Falcon_Series_8063","Falcon_Challenge_7981","Falcon_Cup_7899","Cinder_Challenge_7940","Ivory_Trophy_7858"} },
};

static std::vector<std::vector<std::string>> makeGroupMembers() {
    std::vector<std::vector<std::string>> g;
    for (const auto& d : kCuratedGroups) g.push_back(d.members);
    return g;
}
static std::vector<std::vector<std::string>> g_multiclassGroups = makeGroupMembers();

// the total is tied to the SERIES, not the group index, on purpose: the tests inject groups via
// ReplaceMulticlassGroups, and a parallel by-index table used to hand "the IVORY_CUP_12573 has 36 cars" to a group that was
// no longer the IVORY_CUP_12573. An injected group not present in the curation returns 0 = the caller falls back to the sum.
int MulticlassTotalForSeries(const std::string& series_id) {
    for (const auto& g : kCuratedGroups)
        for (const auto& m : g.members) if (m == series_id) return g.total;
    return 0;
}

int MulticlassGroupOf(const std::string& series_id) {
    for (size_t g = 0; g < g_multiclassGroups.size(); ++g)
        for (const auto& m : g_multiclassGroups[g])
            if (m == series_id) return (int)g;
    return -1;
}

bool SameMulticlassGroup(const std::string& a, const std::string& b) {
    int ga = MulticlassGroupOf(a);
    return ga >= 0 && ga == MulticlassGroupOf(b);
}

const std::vector<std::vector<std::string>>& MulticlassGroups() { return g_multiclassGroups; }

void ReplaceMulticlassGroups(std::vector<std::vector<std::string>> groups) {
    g_multiclassGroups = std::move(groups);
}

// Era-memory predicate (see header): true if `era` is a 4-digit year < 2000. SAME criterion as
// screen_memories (era.size()==4 && 1900<=y<2000); "1960s" (5 chars)/"modern"/"club"/"2024" -> false.
bool IsEraMemorySeries(const SeriesDef& sd) {
    if (sd.era.size() != 4) return false;
    int y = atoi(sd.era.c_str());
    return y >= 1900 && y < 2000;
}

bool IsEventOnlySeries(const SeriesDef& sd) {
    return sd.kind == "event";
}

int SessionMinutes(int override_, int fixedDefault) {
    return (override_ > 0 && override_ <= 240) ? override_ : fixedDefault;
}

// INSTALLED content sets (populated by the overlay from the game's live catalogs, on
// its own THREAD -> mutex). FAIL-OPEN: an empty set => everything counts as installed (never block without a populated catalog).
static std::mutex g_contentMx;
static std::set<int> g_installedTracks;
static std::set<int> g_installedVehs;
void SetInstalledContent(const std::set<int>& trackHashes, const std::set<int>& vehHashes) {
    std::lock_guard<std::mutex> lk(g_contentMx);
    g_installedTracks = trackHashes;   // unconditional replacement: {} = back to fail-open (the caller validates)
    g_installedVehs   = vehHashes;
}
// SEPARATELY. The live scan resolves tracks and cars at different times (each container gets
// populated when the game builds its picker), and with a combined call, whichever side wasn't ready yet
// used to get written as {} -- wiping out what we already knew (including what was loaded from disk) and reverting to
// fail-open, where the DLC screen treats EVERYTHING as installed.
void SetInstalledTracks(const std::set<int>& trackHashes) {
    std::lock_guard<std::mutex> lk(g_contentMx);
    g_installedTracks = trackHashes;
}
void SetInstalledVehicles(const std::set<int>& vehHashes) {
    std::lock_guard<std::mutex> lk(g_contentMx);
    g_installedVehs = vehHashes;
}

// the live catalog used to be scanned ONLY in memory, so every launch started out not knowing what
// content the player owned, and the DLC screen was stuck on "waiting for the game's catalog" (which was
// also a false promise: "just open AMS2 once" was pointless if nothing got saved). It's now persisted at
// install time and reloaded at startup; the session's scan overwrites it once it resolves, so
// uninstalling a DLC self-corrects on the next session that launches the game.
bool SaveInstalledContent(const char* path) {
    if (!path) return false;
    paths::EnsureParentDir(path);
    FILE* f = paths::FOpen(path, "wb");
    if (!f) return false;
    fputs("# content_installed.psv -- catalogo VIVO detectado en el juego (kind|hash). Lo reescribe cada escaneo.\n", f);
    std::lock_guard<std::mutex> lk(g_contentMx);
    for (int h : g_installedTracks) fprintf(f, "track|%d\n", h);
    for (int h : g_installedVehs)   fprintf(f, "veh|%d\n", h);
    fclose(f);
    return true;
}
bool LoadInstalledContent(const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;                       // absent = never scanned: fail-open, same as before
    std::set<int> trk, veh;
    char ln[128];
    while (fgets(ln, sizeof(ln), f)) {
        if (ln[0] == '#') continue;
        const char* bar = strchr(ln, '|');
        if (!bar) continue;
        int h = atoi(bar + 1);
        if (!h) continue;
        if (!strncmp(ln, "track", 5)) trk.insert(h);
        else if (!strncmp(ln, "veh", 3)) veh.insert(h);
    }
    fclose(f);
    // the scan's threshold (>=50) applies here too: a half-written file shouldn't be able to pass itself off as a catalog
    if (trk.size() < 50 && veh.size() < 50) return false;
    std::lock_guard<std::mutex> lk(g_contentMx);
    if (trk.size() >= 50) g_installedTracks = trk;
    if (veh.size() >= 50) g_installedVehs   = veh;
    return true;
}
bool TrackInstalledRaw(int trackHash) {
    std::lock_guard<std::mutex> lk(g_contentMx);
    return g_installedTracks.empty() || g_installedTracks.count(trackHash) > 0;
}
bool VehicleInstalledRaw(int vehHash) {
    std::lock_guard<std::mutex> lk(g_contentMx);
    return g_installedVehs.empty() || g_installedVehs.count(vehHash) > 0;
}
// effective = the live catalog MINUS products the player disabled (Settings>Content).
// The override also applies under fail-open (empty set): disabling a pack simulates its absence even with no scan.
// MINUS also whatever ISN'T OWNED per Steam -- AMS2 ships DLC files to everyone and lists
// their tracks in the live walk, so "installed" never implied "owned", and the career was running circuits without
// being purchased (the engine's own launch path bypasses the game's own gate). With this, the calendar's track
// substitution kicks in automatically for rounds using unowned packs. Without a Steam scan or
// curated app ids, the veto is a no-op (fail-open as always).
bool TrackInstalled(int trackHash) {
    return TrackInstalledRaw(trackHash) && !TrackDisabledByOverride(trackHash) && !TrackVetoedBySteam(trackHash);
}
bool VehicleInstalled(int vehHash) {
    return VehicleInstalledRaw(vehHash) && !VehicleDisabledByOverride(vehHash) && !VehicleVetoedBySteam(vehHash);
}
int InstalledTrackCount()   { std::lock_guard<std::mutex> lk(g_contentMx); return (int)g_installedTracks.size(); }
int InstalledVehicleCount() { std::lock_guard<std::mutex> lk(g_contentMx); return (int)g_installedVehs.size(); }

// Qualifying minutes PER CLASS. The calendar data (qualifyMin) is uniformly generated at 15 minutes, so
// qualifying always came out at 15 regardless of class. Here we derive a reasonable duration from the
// class's discipline (via keywords in class_name). Easy to tune. PRACTICE is still driven by data (45/60/90/180).
int QualiMinutesForClass(const std::string& className) {
    std::string c; c.reserve(className.size());
    for (char ch : className) c.push_back((char)((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch));
    auto has = [&](const char* k) { return c.find(k) != std::string::npos; };
    // Club / entry-level (short quali): Kingsmere, karts, Formula Vee/Trainer, Blackley_Motors/Greycombe_Motors/Uno.
    if (has("kingsmere") || has("kart") || has("vee") || has("trainer") ||
        has("blackley_motors") || has("greycombe_motors") || has("uno")) return 10;
    // Endurance / prototypes (long quali, superpole-style): Group C, LMP/Pemberton_Zel121, Hypercar, P1..P4, DPi, prototypes.
    if (has("group c") || has("groupc") || has("lmp") || has("pemberton_zel121") || has("hypercar") ||
        has("proto") || has("dpi") || has("p1") || has("p2") || has("p3") || has("p4")) return 30;
    // Stock cars / ovals (long group): Stock Car, HARBOR_CHALLENGE_8227, Harcourt Jun934.
    if (has("stock") || has("harbor_challenge_8227") || has("beacon_series_10605") || has("harcourt_jun934")) return 25;
    // GT (GT1/GT3/GT4/MERROWDOWN_MYR373/Valemont_Kor951): GT sprint.
    if (has("gt")) return 20;
    // Single-seaters (Falcon_Cup/F-Classic/Falcon_Championship/CINDER_CUP/F-4/F-V10/F-V12/Indy...): standard formula quali.
    if (has("f-") || has("formula") || has("indy")) return 20;
    // Touring cars / one-make cups: TCR, Copa, Cup, Clio, TSI, hot hatch.
    if (has("tc") || has("touring") || has("copa") || has("cup") || has("clio") || has("tsi") || has("hatch")) return 15;
    return 15;  // default: 15 min (generic sprint)
}

} // namespace career
