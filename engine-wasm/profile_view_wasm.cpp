// profile_view_wasm.cpp -- the browser host's provider for query.profile.
//
// PROVISIONAL -- SECOND IMPLEMENTATION OF THE PROFILE VIEW.
//
// The authoritative assembly is career_overlay.cpp's SetProfileViewProvider lambda (it lives in the
// career_api_wiring.inc the overlay includes, with the profile bridge functions it calls), which this
// repository does not publish. What follows is a copy written so the browser build can answer
// query.profile at all, and copies drift: grep PROVISIONAL to find every one of them. The static_assert
// below makes a change to the view structure itself loud.
//
// Nothing here is new logic. The profile's four views of the one rating are the engine's PURE functions,
// all published in career_profile.h: ProfileRating, ProfileRatingArc, ProfileRivals, ProfileWorldRanking,
// ProfileMyRank. The overlay feeds them through bridges defined only in the private tree
// (ProfilePlayerRatingUi, ProfileRatingArcUi, ProfileRivalsUi, ProfileWorldPoolUi, ProfileActiveModernUi,
// ProfileWorldFilterOptionsUi, RaceLogUiEntries, CareerIdentity*); each is a thin read of the save, so it
// is rebuilt here from the same published pieces: SaveData's driver_ratings / h2h / race_log / birth_year
// / retired, ActiveModernSet, IsModernSeries, WeakestIncumbentRating, DriverDisplayName. The serializer
// is the engine's own QueryProfile (career_api_queries.cpp) and is not touched or repeated here.
#include "career_api_queries.h"
#include "career_balance.h"
#include "career_calendar.h"
#include "career_data_json.h"
#include "career_fields.h"
#include "career_profile.h"
#include "career_save.h"
#include "career_seats.h"
#include "career_series.h"
#include "strings.h"
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

// The guard that makes drift loud, for the same reason wasm_main.cpp guards HubView: ProfileView is a
// plain serialization view, so a field added upstream would ship default-initialized through this demo
// without anything here having to mention it. Size, because C++17 cannot count members; exact for the
// one toolchain this file is built with (wasm32, libc++). When it fires: read the new ProfileView, fill
// the new field in MakeProfileView below, THEN update the number. Do not just update the number.
static_assert(sizeof(capi::ProfileView) == 208,
              "capi::ProfileView changed upstream. This is a SECOND, provisional copy of the overlay's "
              "profile assembly: fill the new/changed field in MakeProfileView below, THEN update this size.");

capi::CareerStateProvider g_state;

// ProfilePlayerRatingUi: the persisted rating, rounded; a career never seeded reads as the rookie base.
int PlayerRating(const career::SaveData& save) {
    int r = (int)(save.prog.rating + 0.5);
    if (r <= 0) r = (int)career::bal::RATING_ROOKIE_BASE;
    return r;
}

// The world clock the active-modern set is aged against: the save's world year once the world has rolled
// over at least once, the calendar's year before that.
int WorldYear(const career::SaveData& save, const career::CareerCalendar& cal) {
    return save.current_world_year > 0 ? save.current_world_year : cal.year;
}

// ProfileRivalsUi. The proximity scope is the union of the active-modern fields of every series the
// player holds an active contract in; the "market" rival is the weakest displaceable incumbent of the
// highest-level one of those series. With no contract both are empty, and ProfileRivals falls back to
// its own neighbours -- an empty set is its input, not a gap.
std::vector<career::RivalRow> Rivals(const career::SaveData& save, int worldYear, int playerRating) {
    std::set<std::string> scope;
    std::string topSeries;
    int topLevel = -1;
    for (const auto& c : save.contracts) {
        if (!c.active || c.series_id.empty()) continue;
        const std::set<std::string> one = career::ActiveModernSet(save.birth_year, save.retired, worldYear, c.series_id);
        scope.insert(one.begin(), one.end());
        const career::SeriesDef* sd = career::SeriesById(c.series_id);
        const int lvl = sd ? sd->level : 1;
        if (lvl > topLevel) { topLevel = lvl; topSeries = c.series_id; }
    }
    std::string marketId, marketName;
    int marketRating = 0;
    if (!topSeries.empty()) {
        double weakest = 0.0;
        std::string weakestId;
        if (career::WeakestIncumbentRating(topSeries, save, worldYear, weakest, weakestId) && !weakestId.empty()) {
            marketId = weakestId;
            marketRating = (int)(weakest + 0.5);
            // DriverDisplayName, not a bare DriverById: the weakest incumbent may be a generated driver whose
            // name lives in the generated registry. An unresolved id comes back as itself, and stays unnamed.
            const std::string nm = career::DriverDisplayName(marketId);
            if (nm != marketId) marketName = nm;
        }
    }
    return career::ProfileRivals(playerRating, scope, save.driver_ratings, save.h2h, marketId, marketRating, marketName);
}

// ProfileWorldFilterOptionsUi: "All" first, then every modern series that has active drivers, so the
// dropdown never offers a filter that would show an empty table.
std::vector<capi::ProfileFilterOptView> FilterOptions(const career::SaveData& save, int worldYear) {
    std::vector<capi::ProfileFilterOptView> out;
    out.push_back({ std::string(), std::string() });
    for (const auto& sd : career::AllSeries()) {
        if (!career::IsModernSeries(sd.id)) continue;
        if (career::ActiveModernSet(save.birth_year, save.retired, worldYear, sd.id).empty()) continue;
        out.push_back({ sd.id, sd.display.empty() ? sd.id : sd.display });
    }
    return out;
}

capi::ProfileView MakeProfileView(const std::string& filter) {
    capi::ProfileView v;
    if (!g_state) return v;
    const capi::CareerStateView st = g_state();
    if (!st.loaded || !st.save || !st.calendar) return v;   // loaded=false -> the serializer says not_ready
    const career::SaveData& save = *st.save;
    const career::CareerCalendar& cal = *st.calendar;
    v.loaded = true;

    // The overlay joins CareerIdentityFirst() + " " + CareerIdentityLast(), whose fallbacks for a save with
    // no identity are a private placeholder name, not data. The career-state seat's name is the same join
    // of the save's own identity without that placeholder, and it is the name every other view of this host
    // reports -- and the one the world ranking must find to mark the player's row.
    v.playerName  = st.playerName;
    v.nationality = save.player.nationality;

    const int rating = PlayerRating(save);
    // The delta is the last settled race's (ProfilePlayerDeltaUi reads process state the settlement sets).
    // No race settles in this host, so there is none: 0, which is also what the overlay reports before a race.
    v.rating = career::ProfileRating(rating, 0);
    v.arc    = career::ProfileRatingArc(save.race_log, rating, GetLang() == 1);
    const int worldYear = WorldYear(save, cal);
    v.rivals = Rivals(save, worldYear, rating);

    // recent form: the last 8 races, OLDEST first (the race log is chronological)
    {
        const int n = (int)save.race_log.size();
        const int from = n > 8 ? n - 8 : 0;
        for (int i = from; i < n; i++) v.recentForm.push_back({ save.race_log[i].pos, save.race_log[i].finished });
    }

    // palmares: the screen's own idiom (wins/podiums from class_stats; races = completed_races;
    // xp = 900 + races*22; titles = prog.titles; age = the identity's, against the live year).
    for (const auto& kv : save.class_stats) { v.palmares.wins += kv.second.wins; v.palmares.podiums += kv.second.podiums; }
    v.palmares.races  = (int)save.completed_races.size();
    v.palmares.titles = save.prog.titles;
    v.palmares.xp     = 900 + v.palmares.races * 22;
    {
        const int by = save.player.birth_year;
        int wy = cal.year;
        if (wy < 1900) wy = career::kStartYear;
        if (wy < by) wy = by;
        v.palmares.age = by ? wy - by : 0;
    }

    // The world ranking: the live rating pool, restricted to the active-modern subset for this filter.
    const std::set<std::string> active = career::ActiveModernSet(save.birth_year, save.retired, worldYear, filter);
    v.ranked = career::ProfileWorldRanking(save.driver_ratings, active, v.playerName, v.rating.score, filter);
    for (int i = 0; i < (int)v.ranked.size(); i++) if (v.ranked[i].you) { v.meIdx = i; break; }
    // The hero's rank is ALWAYS the global one (filter ""), never the filtered table's.
    if (filter.empty()) {
        v.worldRankGlobal = v.meIdx >= 0 ? v.meIdx + 1 : 0;
    } else {
        const std::set<std::string> allActive = career::ActiveModernSet(save.birth_year, save.retired, worldYear, std::string());
        v.worldRankGlobal = career::ProfileMyRank(save.driver_ratings, allActive, v.playerName, v.rating.score, std::string());
    }
    v.filterOptions = FilterOptions(save, worldYear);
    v.selectedFilter = filter;
    return v;
}

}  // namespace

namespace wasmhost {

void InstallProfileView(capi::CareerStateProvider state) {
    g_state = std::move(state);
    capi::SetProfileViewProvider(&MakeProfileView);
}

}  // namespace wasmhost
