#pragma once
#include <map>
#include <string>
#include <vector>

// Block C of the 2026-07-24 spec (adaptive AI) -- THE WEATHER. (weather selection) and
// (almost the whole calendar comes out Random, with no criteria).
//
// The problem it solves: until today `WeatherSlotsForEvent` was writing 17 (AMS2's NATIVE random) into the
// four slots of almost every race, because `series_calendar.psv` carries weather=0 in 1314 of its 1315
// rounds. So variety was decided by the game rolling the dice, slot by slot with no memory, hence
// the jumps from sun to storm and back to sun within the same session.
//
// AMS2's FOUR slots are a TIME SEQUENCE, not four independent draws. This module generates
// them as such. It's PURE and deterministic: the same (race, year) always gives the same sequence, so
// entering and leaving the pre-race screen doesn't reshuffle anything, but the same round in a different season is different.
namespace career {

// AMS2's canonical catalog (the same codes as the app's wxname_* and the game's own weather slots) ---
constexpr int WX_CLEAR        = 0;    // Clear
constexpr int WX_PARTLY       = 1;    // Partly cloudy
constexpr int WX_MEDIUM       = 2;    // Moderately cloudy
constexpr int WX_MOSTLY       = 3;    // Mostly cloudy
constexpr int WX_OVERCAST     = 4;    // Overcast
constexpr int WX_DRIZZLE      = 5;    // Drizzle
constexpr int WX_RAIN         = 6;    // Rain
constexpr int WX_STORM        = 7;    // Storm
constexpr int WX_THUNDER      = 8;    // Thunderstorm
constexpr int WX_FOGGY        = 12;   // Misty
constexpr int WX_MIST_RAIN    = 13;   // Mist with rain   (bridge to the rain family)
constexpr int WX_HEAVY_FOG    = 14;   // Thick fog
constexpr int WX_HEAVYFOG_RAIN= 15;   // Thick fog and rain (bridge to the rain family)
constexpr int WX_MIST         = 16;   // Mist
constexpr int WX_RANDOM       = 17;   // The game's NATIVE random (what used to be written everywhere)

// Does this state make the track wet? Drives the anti-crash gate for fragile classes in wet conditions and the AI's
// wet-weather pace multiplier. A useful side effect of C2: with a CONCRETE sequence resolved, this can finally
// be decided for real -- with 17 the value stayed uncertain until the session actually started.
bool WeatherIsWet(int wx);

// Weather mode chosen by the player in Settings (spec C1). An event's AUTHORED weather always
// beats all three: it's part of the event's design.
enum class WeatherMode {
    Random   = 0,   // default: the NATIVE 17 across the N slots (0.4.0's behavior), the game rolls the dice
    Inherit  = 1,   // inherited from your Custom Race: the mod does NOT write the slots, and whatever you had stays
    Rotation = 2,   // the generator, restricted to the weathers you check and biased by their weights
};

struct WeatherPrefs {
    WeatherMode mode = WeatherMode::Random;
    // Weight per state, indexed by the catalog code. Only read by Rotation mode: a state that's absent or
    // weighted 0 is FORBIDDEN, and the rest bias the starting-weather draw. Empty = factory weights.
    std::map<int, int> weights;
};

// The N-slot sequence for a race. slots is clamped to 1..4 (resolved by WeatherSlotCountForSession from
// session duration, already done). raceKey identifies the round ("<series>#<round>" works); year is the
// SEASON's year, and it's deliberately in the seed: the same round in 2026 and 2027 gets different weather.
// PURE. Never returns WX_RANDOM: falling back to the game's dice is the Random mode, which doesn't even reach here.
std::vector<int> WeatherSequence(const std::string& raceKey, int year, int slots, const WeatherPrefs& prefs);

// Factory weights (Rotation mode's fallback when there's no saved selection yet). Exposed so the UI can
// show the player where they start from before touching anything.
const std::map<int, int>& WeatherDefaultWeights();

// The bridge the race launch consumes. Fills the game's FOUR slots (s27/s30/s33/s36).
//   calWeather > 0  -> the event's AUTHORED weather: always wins and fills all four. Events are fixed and
//                      their weather is part of their design (spec C1).
//   Inherited mode  -> returns FALSE: the caller should write nothing, leaving the player's config as is.
//   everything else -> the sequence of `slots` slots, and the leftover ones repeat the last one (the game
//                      always reads all four; how many it USES is s26's job, which the caller sets separately).
// Returns true if there's anything to write.
bool WeatherSlotsForRace(int calWeather, const std::string& raceKey, int year, int slots,
                         const WeatherPrefs& prefs, int out[4]);

// 19/09 (player report: with weather on "Inherited" ALL sessions came out sunny). The handoff carries,
// besides the four slots (weather4), a single-value LEGACY weather that CareerApplyEvent replicates into the
// four when weather4 comes unset (the debug commands use it). In the calendar the weather column
// is 0 when there's NO data (the player decides), but in the handoff 0 is SUN, and the round passed it through as is:
// in Inherited mode weather4 was left unwritten, the legacy 0 won and the four slots came out clear.
// This is the translation: only an AUTHORED weather (> 0) travels as legacy; everything else is -1 = don't force. The
// authored one also already goes in weather4 (WeatherSlotsForRace), so the legacy never decides anything in a round.
inline int HandoffLegacyWeather(int calWeather) { return calWeather > 0 ? calWeather : -1; }

// The four slots that CareerApplyEvent really WRITES: weather4 rules; if it isn't set (weather4[0] < 0),
// the legacy >= 0 is replicated into the four; if neither, it returns false and no weather is written (the player's stays).
// One single rule for the write, the debug snapshot (query.race_config) and the load cover, which repeated it.
inline bool HandoffWeatherSlots(const int weather4[4], int weatherLegacy, int out[4]) {
    for (int i = 0; i < 4; i++) out[i] = -1;
    if (weather4 && weather4[0] >= 0) { for (int i = 0; i < 4; i++) out[i] = weather4[i]; }
    else if (weatherLegacy >= 0)      { for (int i = 0; i < 4; i++) out[i] = weatherLegacy; }
    return out[0] >= 0;
}

// from the portal (19/09/2026): the AUTHORED weather is a LIST of 1..4 slots, the same in a
// calendar round (`weather` column of series_calendar.psv) and in an event (overrides.weather). EMPTY list = «the
// player decides» (their Settings mode, above). Before, the calendar stored ONE integer with 0 = «no data», so
// a round couldn't author clear (which is the catalog's 0). "8" -> {8}; "0,8,12,17" -> {0,8,12,17}.
// Tokens that aren't a catalog integer (0..17) are DISCARDED; from the fifth on they're ignored.
std::vector<int> ParseWeatherList(const std::string& cell);
// Inverse, for dumps and parity: {} -> "", {0,8} -> "0,8".
std::string FormatWeatherList(const std::vector<int>& wx);
// Does any slot get wet? (empty -> false). It's WeatherIsWet over the list.
bool AnyWeatherWet(const std::vector<int>& wx);

// What the launch writes into the game for a race. The SINGLE rule of the three paths (calendar,
// substitution, special event), which used to live duplicated in screen_calendar.cpp with two different conventions.
struct RaceWeatherPlan {
    bool write = false;              // false = write nothing (Inherited mode and no authored weather)
    bool authored = false;           // true = the authored list ruled (it beats the three modes, decision 29/07)
    int  slots[4] = {-1, -1, -1, -1};// the game's 4 slots (VM550 s33/36/39/42)
    int  forcedSlots = 0;            // no. of slots to FORCE in all sessions (2..4); 0 = counter by duration
};
//   authored non-empty -> slots = the list (filled with the last), forcedSlots = n if n>=2 (1 value = automatic
//                        counter, like a one-slot event today). write = true ALWAYS, also in Inherited.
//   authored empty    -> WeatherSlotsForRace(-1, …) with the player's mode and the number of slots from
//                        WeatherSlotCountForSession(sessionMin) (): Inherited -> write=false; Random -> 17 in
//                        all; Your selection -> the generator. forcedSlots = that number if >=2 (so that all of them are SEEN).
RaceWeatherPlan PlanRaceWeather(const std::vector<int>& authored, const std::string& raceKey, int year,
                                int sessionMin, const WeatherPrefs& prefs);

}  // namespace career
