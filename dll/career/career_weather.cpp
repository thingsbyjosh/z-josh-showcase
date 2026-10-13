#include "career_weather.h"
#include "career_genome.h"   // GenomeSeed / GenomeUniform: the same deterministic draw as the rest of the engine
#include "career_series.h"   // WeatherSlotCountForSession ()
#include <algorithm>

namespace career {
namespace {

// the two AXES -------------------------------------------------------------------------------------
// SEVERITY axis, already ordered by the catalog itself: from clear to thunderstorm. The order IS the
// data: the generator only moves along it, so a jump from sun to storm is impossible by construction
// once the step is capped.
const int kAxisSky[] = { WX_CLEAR, WX_PARTLY, WX_MEDIUM, WX_MOSTLY, WX_OVERCAST,
                         WX_DRIZZLE, WX_RAIN, WX_STORM, WX_THUNDER };
constexpr int kAxisSkyN = (int)(sizeof(kAxisSky) / sizeof(kAxisSky[0]));

// FOG runs on its own axis (spec C2). It isn't one more degree of cloudiness: it's a different phenomenon, and chaining
// "overcast -> thick fog -> drizzle" would give a session unlike any real one. The two BRIDGES (13
// misty rain, 15 thick fog and rain) live INSIDE this axis, placed by severity, which is
// what gives a fog session its real shape: the fog closes in and on top of that it starts raining.
const int kAxisFog[] = { WX_MIST, WX_FOGGY, WX_MIST_RAIN, WX_HEAVY_FOG, WX_HEAVYFOG_RAIN };
constexpr int kAxisFogN = (int)(sizeof(kAxisFog) / sizeof(kAxisFog[0]));

// design constants (spec C2) ----------------------------------------------------------------
// The spec fixes the SHAPE (arc, max jump, rare escape) and leaves the numbers to tuning. All named and
// kept here, so raising a season's rain is changing one line, not diving into the generator.
constexpr int kMaxStep       = 2;    // max jump between adjacent slots, in axis steps
constexpr double kStep2Share = 0.25; // ...and of those, how many are TWO-level jumps (the rest are one)
constexpr int kEscapeStep    = 4;    // a sudden front breaks the cap and skips this
constexpr int kEscapePctX100 = 700;  // 7.00% of sessions carry an escape (spec: "on the order of 5 to 10%")
constexpr int kFogPctX100    = 1200; // 12.00% of sessions are FOG (the other axis)

// Genome stream indices. Fixed: changing them changes the weather for the ENTIRE calendar.
enum WxGen { G_FAMILY = 0, G_SHAPE, G_START, G_ESCAPE, G_STEP0, G_STEP1, G_STEP2, G_ESCWHERE };

// The session's arc shape, drawn BEFORE anything else. A real race has an arc; it doesn't oscillate. This is
// what kills native random's "sun, rain, sun, rain".
enum class Shape { Worsen, Improve, Steady, Peak };

// Factory weights. Criterion: dry dominates (that's what happens on a real calendar), rain shows up for
// real but isn't the norm, and extremes are rare. Fog is drawn separately (kFogPctX100), so
// here its weights only split things up WITHIN a session that already came out foggy.
std::map<int, int> MakeDefaults() {
    std::map<int, int> w;
    w[WX_CLEAR]    = 20; w[WX_PARTLY]  = 18; w[WX_MEDIUM] = 15; w[WX_MOSTLY] = 12; w[WX_OVERCAST] = 9;
    // wet is weighted LOW at the start on purpose: the session's arc already brings it in on its own. With 7/6/3/2 sessions
    // with some rain came out in 40% of races, measured; the target is ~1 in 4, which is
    // what a real season looks like.
    w[WX_DRIZZLE]  = 4;  w[WX_RAIN]    = 3;  w[WX_STORM]  = 2;  w[WX_THUNDER] = 1;
    w[WX_MIST]     = 5;  w[WX_FOGGY]   = 4;  w[WX_MIST_RAIN] = 2; w[WX_HEAVY_FOG] = 2; w[WX_HEAVYFOG_RAIN] = 1;
    return w;
}

int WeightOf(const std::map<int, int>& w, int wx) {
    auto it = w.find(wx);
    return it == w.end() ? 0 : (it->second < 0 ? 0 : it->second);
}

// The axis that's actually walked: ONLY the allowed states, in their severity order.
// It used to walk the full axis and then jump to the nearest allowed state, and that had a bug
// only visible by measuring: a gap TRAPS the walk. With "clear" and "thunderstorm" checked and nothing
// in between, a step of 1 or 2 lands on a forbidden state, the nearest allowed one is the STARTING one (it's
// 1-2 away, the other 6-7), and the session gets stuck right where it started: 963 of 1000 measured sessions
// showed not a single change. Both weathers came up across the calendar, but the race itself never rotated.
// Compressing the axis fixes it at the root and also says the right thing: the moment the player trims the
// palette, the plausible neighbors are the neighbors of THEIR palette. The max jump is measured there.
std::vector<int> AllowedAxis(const int* axis, int n, const std::map<int, int>& w) {
    std::vector<int> out;
    for (int i = 0; i < n; i++) if (WeightOf(w, axis[i]) > 0) out.push_back(axis[i]);
    return out;
}

// Weighted draw over an already-filtered axis. Returns the INDEX, or -1 if empty.
int PickIndexWeighted(const std::vector<int>& axis, const std::map<int, int>& w, double u) {
    int total = 0;
    for (int v : axis) total += WeightOf(w, v);
    if (total <= 0) return -1;
    int t = (int)(u * total);
    if (t >= total) t = total - 1;      // u==1.0 exact must not overflow
    for (size_t i = 0; i < axis.size(); i++) {
        t -= WeightOf(w, axis[i]);
        if (t < 0) return (int)i;
    }
    return (int)axis.size() - 1;         // unreachable except by rounding
}

// Shape distribution, TUNED WITH THE MEASURING TOOL, not by eye. With the first pass (30/25/30/15 and a uniform
// 1-2 step) sessions with some wet weather came out in 49% of races: too much for a real calendar,
// where rain shows up but isn't the norm. What was driving it wasn't the starting weather (which comes out dry
// 80% of the time) but the WALK: with 4 slots and steps up to 2, any session starting overcast
// ended up raining. Stable dominates by a wide margin, which is what actually happens.
constexpr double kShapeWorsen  = 0.22;
constexpr double kShapeImprove = 0.42;   // cumulative
constexpr double kShapeSteady  = 0.84;   // cumulative
Shape PickShape(double u) {
    if (u < kShapeWorsen)  return Shape::Worsen;    // worsening: an incoming front
    if (u < kShapeImprove) return Shape::Improve;   // improving: a drying track
    if (u < kShapeSteady)  return Shape::Steady;    // stable: by far the most common
    return Shape::Peak;                             // spike: worsens then eases
}

}  // anon

bool WeatherIsWet(int wx) {
    // the catalog's wet-set: drizzle, rain, storm, thunderstorm and
    // the two fog bridges. 17 (native random) counts as wet because it CAN be, and there's no
    // way to know until the session starts -- with this module it stops appearing, which is the goal.
    return wx == WX_DRIZZLE || wx == WX_RAIN || wx == WX_STORM || wx == WX_THUNDER
        || wx == WX_MIST_RAIN || wx == WX_HEAVYFOG_RAIN || wx == WX_RANDOM;
}

const std::map<int, int>& WeatherDefaultWeights() {
    static const std::map<int, int> kDefaults = MakeDefaults();
    return kDefaults;
}

std::vector<int> WeatherSequence(const std::string& raceKey, int year, int slots, const WeatherPrefs& prefs) {
    if (slots < 1) slots = 1;
    if (slots > 4) slots = 4;

    // Seed: the race AND the year. Without the year, the same round would repeat the same weather season after season; with
    // it, entering and leaving the pre-race screen still gives the same result (which is what's wanted), but next year
    // it doesn't rain at Northham Ring by decree.
    const uint64_t seed = GenomeSeed(raceKey + "|" + std::to_string(year));

    const std::map<int, int>& w =
        (prefs.mode == WeatherMode::Rotation && !prefs.weights.empty()) ? prefs.weights : WeatherDefaultWeights();

    // (1) FAMILY. Fog is rare and separate. If the player has unchecked it entirely, it never shows up.
    int fogTotal = 0;
    for (int i = 0; i < kAxisFogN; i++) fogTotal += WeightOf(w, kAxisFog[i]);
    int skyTotal = 0;
    for (int i = 0; i < kAxisSkyN; i++) skyTotal += WeightOf(w, kAxisSky[i]);
    bool fog = fogTotal > 0 && (int)(GenomeUniform(seed, G_FAMILY) * 10000) < kFogPctX100;
    if (skyTotal <= 0 && fogTotal > 0) fog = true;      // only fog was left checked

    // Nobody checked anything usable -> clear. It's the honest degradation: a dry session beats returning
    // 17 and falling back to the game's dice roll, which is exactly what this module exists to remove.
    if (skyTotal <= 0 && fogTotal <= 0) return std::vector<int>((size_t)slots, WX_CLEAR);

    const std::vector<int> axis = fog ? AllowedAxis(kAxisFog, kAxisFogN, w)
                                      : AllowedAxis(kAxisSky, kAxisSkyN, w);
    const int n = (int)axis.size();
    if (n == 1) return std::vector<int>((size_t)slots, axis[0]);   // a single weather checked: a constant session

    // (2) arc SHAPE, before the starting weather: the shape is what decides which way things walk.
    const Shape shape = PickShape(GenomeUniform(seed, G_SHAPE));

    // (3) STARTING WEATHER, biased by the weights.
    int idx = PickIndexWeighted(axis, w, GenomeUniform(seed, G_START));
    if (idx < 0) idx = 0;

    // (4) the walk. dir = which way severity moves in each leg.
    const bool escape = (int)(GenomeUniform(seed, G_ESCAPE) * 10000) < kEscapePctX100;
    const int  escAt  = 1 + (int)(GenomeUniform(seed, G_ESCWHERE) * (double)(slots > 1 ? slots - 1 : 1));

    std::vector<int> out;
    out.push_back(axis[idx]);
    for (int k = 1; k < slots; k++) {
        int dir = 0;
        switch (shape) {
            case Shape::Worsen:  dir = +1; break;
            case Shape::Improve: dir = -1; break;
            case Shape::Steady:  dir =  0; break;
            case Shape::Peak:    dir = (k <= slots / 2) ? +1 : -1; break;
        }
        // Step size: 0..kMaxStep when stable, 1..kMaxStep when there's a direction. The rare escape breaks the
        // cap ONCE, and only on a leg with a direction (a sudden front doesn't hit a flat session).
        const double u = GenomeUniform(seed, G_STEP0 + ((k - 1) % 3));
        // A two-level step is the exception, not half the cases: weather changes slowly within
        // a session. With 1-2 at 50% the arc was eating up the entire axis in three slots.
        int step = (dir == 0) ? 0 : (u < kStep2Share ? 2 : 1);
        if (step > kMaxStep) step = kMaxStep;
        if (escape && dir != 0 && k == escAt) step = kEscapeStep;

        // The axis is ALREADY filtered to what's allowed, so the step is measured in states of the player's
        // palette and there's no need to search for the nearest allowed state: there are no gaps to fall into.
        int next = idx + dir * step;
        if (next < 0) next = 0;
        if (next >= n) next = n - 1;
        idx = next;
        out.push_back(axis[idx]);
    }
    return out;
}

bool WeatherSlotsForRace(int calWeather, const std::string& raceKey, int year, int slots,
                         const WeatherPrefs& prefs, int out[4]) {
    if (!out) return false;
    // (1) an event's AUTHORED weather overrides everything, including the player's mode.
    if (calWeather > 0) {
        for (int i = 0; i < 4; i++) out[i] = calWeather;
        return true;
    }
    // (2) inherited: the player wants THEIR Custom Race config left intact -> nothing gets written.
    if (prefs.mode == WeatherMode::Inherit) return false;
    // (3) RANDOM = the native 17 across the N slots, which is what 0.4.0 did: the game rolls the dice.
    // A 2026-07-29 decision that CORRECTS the spec (C1 said "the generator instead of the native 17"): the generator
    // is what you get when you check weathers, not what you're stuck with by default. The number of slots is still
    // resolved by duration, same as before.
    if (prefs.mode == WeatherMode::Random) {
        for (int i = 0; i < 4; i++) out[i] = WX_RANDOM;
        return true;
    }
    // (4) rotation: the generator, restricted to the checked weathers and biased by their weights.
    const std::vector<int> seq = WeatherSequence(raceKey, year, slots, prefs);
    for (int i = 0; i < 4; i++)
        out[i] = seq[(size_t)i < seq.size() ? (size_t)i : seq.size() - 1];
    return true;
}

// from the portal: slot list and launch plan ----------------------------------------------

std::vector<int> ParseWeatherList(const std::string& cell) {
    std::vector<int> out;
    std::string tok;
    auto flush = [&]() {
        size_t a = tok.find_first_not_of(" \t"), b = tok.find_last_not_of(" \t");
        if (a != std::string::npos && out.size() < 4) {
            const std::string t = tok.substr(a, b - a + 1);
            bool digits = !t.empty();
            for (char c : t) if (c < '0' || c > '9') { digits = false; break; }   // unsigned: -1 is garbage, not "don't force"
            if (digits) { int v = atoi(t.c_str()); if (v >= 0 && v <= 17) out.push_back(v); }
        }
        tok.clear();
    };
    for (char c : cell) { if (c == ',') flush(); else tok += c; }
    flush();
    return out;
}

std::string FormatWeatherList(const std::vector<int>& wx) {
    std::string s;
    for (size_t i = 0; i < wx.size(); i++) { if (i) s += ','; s += std::to_string(wx[i]); }
    return s;
}

bool AnyWeatherWet(const std::vector<int>& wx) {
    for (int w : wx) if (WeatherIsWet(w)) return true;
    return false;
}

RaceWeatherPlan PlanRaceWeather(const std::vector<int>& authored, const std::string& raceKey, int year,
                                int sessionMin, const WeatherPrefs& prefs) {
    RaceWeatherPlan p;
    if (!authored.empty()) {
        const int n = (int)(authored.size() < 4 ? authored.size() : 4);
        for (int i = 0; i < 4; i++) p.slots[i] = authored[i < n ? i : n - 1];   // filled with the last slot
        p.forcedSlots = (n >= 2) ? n : 0;
        p.write = true; p.authored = true;
        return p;
    }
    const int autoSlots = WeatherSlotCountForSession(sessionMin);
    p.write = WeatherSlotsForRace(-1, raceKey, year, autoSlots, prefs, p.slots);
    p.forcedSlots = (p.write && autoSlots >= 2) ? autoSlots : 0;
    return p;
}

}  // namespace career
