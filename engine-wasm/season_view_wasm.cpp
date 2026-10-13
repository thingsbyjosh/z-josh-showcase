// season_view_wasm.cpp -- the browser host's provider for query.season.
//
// PROVISIONAL -- SECOND IMPLEMENTATION OF THE SEASON (CHAMPIONSHIPS) VIEW.
//
// The authoritative assembly is career_overlay.cpp's SetSeasonViewProvider lambda (it lives in the
// career_api_wiring.inc the overlay includes, with the season bridge functions it calls), which this
// repository does not publish. What follows is a copy written so the browser build can answer
// query.season at all, and copies drift: grep PROVISIONAL to find every one of them. The static_assert
// below makes a change to the view structure itself loud.
//
// Nothing here is new logic. The rail colour is the overlay's own RaceListColor (career_data.cpp). The
// other bridges this screen calls (CalUiMyChampionships, CalUiGatePassed, CalUiEnrollment) are declared
// in career_season_ui.h and defined in the overlay's bridge, which is not published, but each is a short
// wrapper over published functions, so it is rebuilt here from them: AllSeries, SeriesById,
// FindEnrollment, CalendarForSeries / CalendarForSeriesInYear, ApplyDlcToRound, FinalTable, PointsById.
// The serializer is the engine's own QuerySeason (career_api_queries.cpp) and is not touched or
// repeated here.
#include "career_api_queries.h"
#include "career_calendar.h"
#include "career_save.h"
#include "career_season.h"
#include "career_season_ui.h"   // RaceListColor
#include "career_series.h"
#include "career_standings.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

// The guard that makes drift loud, for the same reason wasm_main.cpp guards HubView: SeasonView is a
// plain serialization view, so a field added upstream would ship default-initialized through this demo
// without anything here having to mention it. Size, because C++17 cannot count members; exact for the
// one toolchain this file is built with (wasm32, libc++). When it fires: read the new SeasonView, fill
// the new field in MakeSeasonView below, THEN update the number. Do not just update the number.
static_assert(sizeof(capi::SeasonView) == 204,
              "capi::SeasonView changed upstream. This is a SECOND, provisional copy of the overlay's "
              "season assembly: fill the new/changed field in MakeSeasonView below, THEN update this size.");

capi::CareerStateProvider g_state;

// CalUiMyChampionships: one summary per live enrollment, with the player's place read off the
// championship's own table by name.
struct Champ {
    std::string seriesId;
    int roundIdx = 0, totalRounds = 0, field = 0, playerPos = 0, myPoints = 0;
    bool done = false;
};
std::vector<Champ> MyChampionships(const career::CareerCalendar& cal, const std::string& playerName) {
    std::vector<Champ> out;
    for (const auto& e : cal.enrollments) {
        Champ c;
        c.seriesId    = e.series_id;
        c.roundIdx    = e.round_idx;
        c.totalRounds = (int)career::CalendarForSeries(e.series_id).size();
        c.done        = (e.status == 1);
        const std::vector<career::StandingRow> table = career::FinalTable(e.standings);
        c.field = (int)table.size();
        for (int i = 0; i < (int)table.size(); i++)
            if (table[i].name == playerName) { c.playerPos = i + 1; c.myPoints = table[i].total; break; }
        out.push_back(std::move(c));
    }
    return out;
}

capi::SeasonView MakeSeasonView(const std::string& selected) {
    capi::SeasonView v;
    if (!g_state) return v;
    const capi::CareerStateView st = g_state();
    if (!st.loaded || !st.save || !st.calendar) return v;   // loaded=false -> the serializer says not_ready
    const career::SaveData& save = *st.save;
    const career::CareerCalendar& cal = *st.calendar;
    v.loaded = true;
    // The same name query.career_state reports (the overlay uses the session's name for the same job).
    v.playerName = st.playerName;
    const int frontier = save.prog.frontier_phase;
    auto gatePassed = [&save](const std::string& id) { return save.unlocked_gates.count(id) > 0; };

    // One way to build the round columns, shared by the three detail variants, carrying what is needed to
    // DECIDE (hour, laps or duration, practice and qualifying) and the track that is actually raced (the
    // DLC substitution the master calendar applies).
    auto pushRounds = [&v](const std::vector<career::CalRound>& rs, const career::SeriesDef* sd) {
        for (const auto& rr : rs) {
            career::CalRound r2 = rr;
            career::ApplyDlcToRound(r2, sd);
            capi::SeasonRoundCol c;
            c.round = r2.round; c.trackName = r2.track_name; c.date = r2.date; c.doy = r2.doy;
            c.hour = r2.hour; c.laps = r2.laps; c.durationMin = r2.durationMin;
            c.practiceMin = r2.practiceMin; c.qualifyMin = r2.qualifyMin;
            v.detailRounds.push_back(c);
        }
    };

    const std::vector<Champ> champs = MyChampionships(cal, v.playerName);
    // championship -> its qualifier (the first series that unlocks it wins), as the engine maps it
    std::map<std::string, std::string> gatedBy;
    for (const auto& q : career::AllSeries())
        if (!q.unlocks.empty() && !gatedBy.count(q.unlocks)) gatedBy[q.unlocks] = q.id;

    // The rail: my championships first, then every other championship as available or locked (by a
    // super-licence gate, or by level). Qualifiers are not listed.
    struct RailEntry { const career::SeriesDef* sd; int section; int champIdx; bool gateLocked; };
    std::vector<RailEntry> rail;
    for (int i = 0; i < (int)champs.size(); i++)
        rail.push_back({ career::SeriesById(champs[i].seriesId), 0, i, false });
    for (const auto& sd : career::AllSeries()) {
        if (sd.kind == "standalone") continue;
        bool enrolled = false;
        for (const auto& c : champs) if (c.seriesId == sd.id) { enrolled = true; break; }
        if (enrolled) continue;
        const bool gated = gatedBy.count(sd.id) > 0 && !gatePassed(sd.id);
        const bool levelLocked = sd.level > frontier;
        if (!gated && !levelLocked) rail.push_back({ &sd, 1, -1, false });
        else                        rail.push_back({ &sd, 2, -1, gated });
    }

    // The selection: the requested series if the rail has it, else the first one in the rail.
    std::string sel = selected;
    bool selOk = false;
    for (const auto& ri : rail) if (ri.sd && ri.sd->id == sel) { selOk = true; break; }
    if (!selOk) { sel.clear(); for (const auto& ri : rail) if (ri.sd) { sel = ri.sd->id; break; } }
    v.selectedSeriesId = sel;

    for (const RailEntry& ri : rail) {
        const career::SeriesDef* sd = ri.sd;
        if (!sd) continue;
        capi::SeasonRailItem it;
        it.seriesId = sd->id; it.display = sd->display; it.className = sd->class_name;
        {
            std::string s;
            for (char c : sd->class_name) { if (c == '_') c = ' '; else if (c >= 'a' && c <= 'z') c = (char)(c - 32); s.push_back(c); }
            it.classShort = s;
        }
        it.discColor = (ImU32)career::RaceListColor(sd->id, sd->class_name);
        it.section = ri.section; it.gateLocked = ri.gateLocked; it.level = sd->level;
        it.selected = (sd->id == sel);
        if (ri.section == 0 && ri.champIdx >= 0) {
            const Champ& c = champs[ri.champIdx];
            it.playerPos = c.playerPos; it.myPoints = c.myPoints;
            it.roundIdx = c.roundIdx; it.totalRounds = c.totalRounds;
            it.field = c.field; it.done = c.done;
        }
        v.rail.push_back(std::move(it));
    }

    // The detail: three EXCLUSIVE variants by priority (enrolled > access > info); do not reorder.
    const career::SeasonState* e = career::FindEnrollment(cal, sel);
    const career::SeriesDef* selSd = career::SeriesById(sel);
    const bool selGated = selSd && gatedBy.count(selSd->id) > 0 && !gatePassed(selSd->id);
    auto nextDoyOf = [&cal](const std::vector<career::CalRound>& rounds) -> int {
        int jd = 0;
        for (const auto& ev : rounds) if (ev.doy > 0 && ev.doy >= cal.cursor_doy) { jd = ev.doy; break; }
        if (jd == 0 && !rounds.empty()) jd = rounds.front().doy;
        return jd > 0 ? jd : 0;
    };
    auto pointsScaleOf = [&v](const career::SeriesDef* sd) {
        if (!sd) return;
        const career::PointsSystem* ps = career::PointsById(sd->points_id);
        if (ps) for (size_t i = 0; i < ps->points.size() && i < 10; i++) v.detailPointsScale.push_back(ps->points[i]);
    };
    if (e) {
        v.detailKind = 1;
        const career::SeriesDef* sd = career::SeriesById(e->series_id);
        v.detailSeriesDisplay = sd ? sd->display : e->series_id;
        // dates PROJECTED onto the live year, so they compare against the live clock
        const std::vector<career::CalRound> rounds = career::CalendarForSeriesInYear(e->series_id, cal.year);
        int totalR = (int)rounds.size();
        if (totalR < 1) totalR = 1;
        v.detailRoundIdx = e->round_idx; v.detailTotalRounds = totalR; v.detailSeasonYear = e->season_year;
        pointsScaleOf(sd);
        pushRounds(rounds, sd);
        const std::vector<career::StandingRow> table = career::FinalTable(e->standings);
        for (int i = 0; i < (int)table.size(); i++)
            if (table[i].name == v.playerName) { v.detailMyPos = i + 1; v.detailMyPts = table[i].total; break; }
        for (int i = 0; i < (int)table.size(); i++) {
            const career::StandingRow& row = table[i];
            capi::SeasonStandRow sr;
            sr.rank = i + 1; sr.name = row.name; sr.isPlayer = (row.name == v.playerName); sr.total = row.total;
            const auto itD = e->standings.drivers.find(row.name);
            const std::vector<career::DriverRound>* drs = (itD != e->standings.drivers.end()) ? &itD->second : nullptr;
            for (int rr = 0; rr < totalR; rr++) {
                capi::SeasonRoundCell cell;
                cell.round = rr + 1;
                const career::DriverRound* d = nullptr;
                if (drs) for (const auto& dr : *drs) if (dr.round == rr + 1) { d = &dr; break; }
                if (d) { cell.ran = true; cell.pos = d->pos; cell.points = d->points; cell.dnf = d->dnf; }
                sr.cells.push_back(cell);
            }
            v.detailStandings.push_back(std::move(sr));
        }
        v.detailNextRaceDoy = nextDoyOf(rounds);
    } else if (selGated) {
        v.detailKind = 2;
        v.detailSeriesDisplay = selSd->display;
        pushRounds(career::CalendarForSeriesInYear(selSd->id, cal.year), selSd);
        const std::string qualId = gatedBy[selSd->id];
        const career::SeriesDef* qd = career::SeriesById(qualId);
        v.detailQualDisplay = qd ? qd->display : qualId;
        const std::vector<career::CalRound> qcal = career::CalendarForSeriesInYear(qualId, cal.year);
        v.detailQualHasCal = !qcal.empty();
        if (!qcal.empty()) v.detailQualFirstDate = qcal[0].date;
        v.detailHasFeeder = !selSd->gate_feeder_class.empty();
        v.detailFeederClass = selSd->gate_feeder_class;
        v.detailFeederTop = selSd->gate_feeder_top;
        for (const auto& c : champs) {
            const career::SeriesDef* fsd = career::SeriesById(c.seriesId);
            if (fsd && fsd->class_name == selSd->gate_feeder_class && c.playerPos > 0) {
                v.detailFeederHasContext = true; v.detailFeederContextPos = c.playerPos;
                v.detailFeederContextSeries = fsd->display;
                break;
            }
        }
    } else if (selSd) {
        v.detailKind = 3;
        v.detailSeriesDisplay = selSd->display;
        pointsScaleOf(selSd);
        const std::vector<career::CalRound> rounds = career::CalendarForSeriesInYear(selSd->id, cal.year);
        pushRounds(rounds, selSd);
        v.detailNextRaceDoy = nextDoyOf(rounds);
    }   // else kind 0 = none (empty rail / unknown series)

    // The end-of-season bonus modal. The overlay reads two pieces of PROCESS state here
    // (CalUiSeasonFinishedFlag / CalUiLastOutcome) that its race settlement sets when a season closes and
    // the app clears with cmd.season.ackBonus; they are not on the save. Nothing settles a race in this
    // host, so no season has just finished: the flag is false and the outcome is the empty one, which is
    // what the overlay reports in the same situation.
    v.bonusShown = false;
    return v;
}

}  // namespace

namespace wasmhost {

void InstallSeasonView(capi::CareerStateProvider state) {
    g_state = std::move(state);
    capi::SetSeasonViewProvider(&MakeSeasonView);
}

}  // namespace wasmhost
