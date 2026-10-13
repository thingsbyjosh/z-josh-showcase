// calendar_view_wasm.cpp -- the browser host's provider for query.calendar.
//
// PROVISIONAL -- SECOND IMPLEMENTATION OF THE CALENDAR VIEW.
//
// The authoritative assembly is career_overlay.cpp's SetCalendarViewProvider lambda (with the calendar
// bridge functions it calls), which this repository does not publish. What follows is a copy written so
// the browser build can answer query.calendar at all, and copies drift: grep PROVISIONAL to find every
// one of them. The static_assert below makes a change to the view structure itself loud.
//
// Nothing here is new logic. Every field is filled the way the overlay fills it, from functions this
// tree publishes (BuildMasterCalendar, WeeksToNextEvent, CanEnterSeries, IsEraMemorySeries, SeriesById,
// CalendarForSeries, FinalTable, OwnedClassSet). The serializer that turns the view into JSON --
// status ladder, prize, grid, multiclass, market marks, the next-year queue's cap and ordering -- is the
// engine's own QueryCalendar (career_api_queries.cpp) and is not touched or repeated here.
//
// The pattern every <view>_view_wasm.cpp follows: build_wasm.sh compiles it by glob; it defines one
// wasmhost::Install<View>View(capi::CareerStateProvider), declared and called in wasm_main.cpp; the
// installer keeps the career-state seat and hands the engine a provider that reads the save and the
// calendar through it (never through wasm_main.cpp's globals); and test_dispatch.mjs checks the query
// against the fixture the real engine dumped for it (fixture_shape.mjs).
#include "career_api_queries.h"
#include "career_calendar.h"
#include "career_save.h"
#include "career_season.h"
#include "career_series.h"
#include "career_standings.h"
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

// The guard that makes drift loud, for the same reason wasm_main.cpp guards HubView: CalendarView is a
// plain serialization view, so a field added upstream would ship default-initialized through this demo
// without anything here having to mention it. Size, because C++17 cannot count members; exact for the
// one toolchain this file is built with (wasm32, libc++). When it fires: read the new CalendarView, fill
// the new field in MakeCalendarView below, THEN update the number. Do not just update the number.
static_assert(sizeof(capi::CalendarView) == 96,
              "capi::CalendarView changed upstream. This is a SECOND, provisional copy of the overlay's "
              "calendar assembly: fill the new/changed field in MakeCalendarView below, THEN update this size.");

// "By invitation": a closed-grid series the player holds no active contract for. career_calendar.h is
// explicit that this flag is NOT BuildMasterCalendar's to set -- it needs the save's contracts, so the
// host resolves it when it builds the view. The overlay does exactly this pass over both the current
// year and the next-year queue. Era-memory series are exempt: they are entered by owning the car, not
// by contract, and marking them would also make them unclickable. Memory rounds are exempt for the
// same reason. Memoized per series because a calendar is hundreds of slots over about a hundred series.
void MarkInviteLocked(const career::SaveData& save, std::vector<career::CalSlot>& slots) {
    std::map<std::string, bool> memo;
    for (auto& s : slots) {
        if (s.is_memory) continue;
        auto it = memo.find(s.series_id);
        if (it == memo.end()) {
            const career::SeriesDef* sd = career::SeriesById(s.series_id);
            const bool eraMemory = sd && career::IsEraMemorySeries(*sd);
            it = memo.emplace(s.series_id, !eraMemory && !career::CanEnterSeries(save, s.series_id)).first;
        }
        s.invite_locked = it->second;
    }
}

// The season result cards, built as the overlay builds them: first the live enrollments that have just
// finished this year (they still carry standings, so champion, field and the player's position are
// known), then the archived record of earlier years (no standings survive the rollover, so only series,
// year, position and rounds). A career that has not raced has neither, and the list is empty.
std::vector<capi::CalendarSeasonResultInput> SeasonResults(const career::SaveData& save,
                                                           const career::CareerCalendar& cal,
                                                           const std::string& playerName) {
    std::vector<capi::CalendarSeasonResultInput> out;
    for (const auto& e : cal.enrollments) {
        if (e.status != 1) continue;
        const career::SeriesDef* sd = career::SeriesById(e.series_id);
        capi::CalendarSeasonResultInput c;
        c.seriesId = e.series_id;
        c.display  = sd ? sd->display : e.series_id;
        c.year     = cal.year;
        c.rounds   = (int)career::CalendarForSeries(e.series_id).size();
        const std::vector<career::StandingRow> table = career::FinalTable(e.standings);
        c.field = (int)table.size();
        if (!table.empty()) c.champion = table[0].name;
        for (int i = 0; i < (int)table.size(); i++)
            if (table[i].name == playerName) { c.finalPos = i + 1; break; }
        out.push_back(std::move(c));
    }
    for (const auto& cs : save.completed_seasons) {
        const career::SeriesDef* sd = career::SeriesById(cs.series_id);
        capi::CalendarSeasonResultInput c;
        c.seriesId = cs.series_id;
        c.display  = sd ? sd->display : cs.series_id;
        c.year     = cs.year;
        c.finalPos = cs.final_pos;
        c.rounds   = cs.rounds;
        c.field    = 0;   // archived seasons keep no table
        out.push_back(std::move(c));
    }
    return out;
}

capi::CareerStateProvider g_state;

capi::CalendarView MakeCalendarView() {
    capi::CalendarView v;
    if (!g_state) return v;
    const capi::CareerStateView st = g_state();
    if (!st.loaded || !st.save || !st.calendar) return v;   // loaded=false -> the serializer says not_ready
    const career::SaveData& save = *st.save;
    const career::CareerCalendar& cal = *st.calendar;

    v.loaded = true;

    // The onboarding gate. calibrated is the save's own flag, read as the overlay reads it; a career
    // that has not run its measurement race is uncalibrated, and the app shows it only that race.
    //
    // The calibration slot's constants (series, track hash) live in an engine header this tree does not
    // publish -- checked: no published header defines them, only CalibrationCalSlot's declaration in
    // career_season_ui.h, whose definition is not published either. What IS published is the prose:
    // CalendarView documents calibCarName as the screen's literal "Kingsmere Uma713", and that
    // declaration names the race "Kingsmere Uma713 @ Stoneton Bay". Those two strings are used as
    // documented. The track hash is not written anywhere published, so it stays 0 (no circuit map)
    // rather than being invented.
    v.calibrated = save.prog.calibrated;
    if (!v.calibrated) {
        v.calibCarName   = "Kingsmere Uma713";
        v.calibTrackName = "Stoneton Bay";
        v.calibTrackHash = 0;
    }

    const int frontier = save.prog.frontier_phase;
    v.frontierPhase = frontier;
    v.cursorDoy     = cal.cursor_doy;
    v.year          = cal.year;

    // The master calendar for this year, gated by the same two sets the overlay passes: championships
    // opened by a gate, and the classes of the cars the player owns.
    const std::set<std::string> ownedClasses = career::OwnedClassSet(save);
    v.slots = career::BuildMasterCalendar(cal, frontier, &save.unlocked_gates, &ownedClasses);
    // The overlay counts weeks over the calendar BEFORE the invitation pass (it builds a fresh one for
    // the count), so this is taken before marking. WeeksToNextEvent does not read invite_locked today;
    // the order keeps it equal to the overlay's even if that changes.
    v.weeksToNextEvent = career::WeeksToNextEvent(v.slots, cal.cursor_doy);
    MarkInviteLocked(save, v.slots);

    // The next-year queue, handed over RAW: the serializer caps, sorts and forces it unclickable. Same
    // projection as the overlay -- a copy of the live calendar moved to year+1 with the cursor at 0, so it
    // projects clean, as the player will see it on arrival -- with the same gates as this year.
    {
        career::CareerCalendar next = cal;
        next.year = cal.year + 1;
        next.cursor_doy = 0;
        v.nextYearSlots = career::BuildMasterCalendar(next, frontier, &save.unlocked_gates, &ownedClasses);
        MarkInviteLocked(save, v.nextYearSlots);
    }

    // The same name query.career_state reports, so "champion = you" is hidden against the identity the
    // rest of the host uses rather than against a second idea of who the player is.
    v.playerName = st.playerName;
    v.seasonResults = SeasonResults(save, cal, v.playerName);
    return v;
}

}  // namespace

namespace wasmhost {

void InstallCalendarView(capi::CareerStateProvider state) {
    g_state = std::move(state);
    capi::SetCalendarViewProvider(&MakeCalendarView);
}

}  // namespace wasmhost
