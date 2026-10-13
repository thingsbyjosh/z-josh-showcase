#include "career_season.h"
#include "career_scoring.h"   // PointsSystem, RoundScore, ScoreRound
#include "career_data_json.h" // DriverIdForName / DriverById (name->id bridge + hot lookup) for nemesis
#include "career_econ.h"      // PER-CLASS economy: FindEcon/FindEconByClassName + SeriesEcon.champion
#include "paths.h"            // mojibake fix (07/22): FOpen (UTF-8 paths -> wide APIs)
#include "career_fields.h"    // GridSizeForSeries / MeetingClassShare — the single source of truth for grid size
#include "career_calendar.h"  // MulticlassMeetingAt: if the round is multiclass, the class aligns ITS portion
#include "career_weather.h"   // AnyWeatherWet(c.weather) for RoundPlan.weather
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <climits>

namespace career {

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

SeasonState StartSeason(const std::string& seriesId, const std::string& branch) {
    SeasonState s;
    s.series_id = seriesId; s.branch = branch;
    s.season_year = 1; s.round_idx = 0; s.status = 0;
    const SeriesDef* d = SeriesById(seriesId);
    s.standings.node_id = seriesId;
    s.standings.points_system = d ? d->points_id : std::string();
    s.standings.round = 0;
    return s;
}

RoundPlan PlanRound(const SeasonState& s) {
    RoundPlan p;
    std::vector<CalRound> cal = CalendarForSeries(s.series_id);
    if (s.round_idx < 0 || s.round_idx >= (int)cal.size()) { p.valid = false; return p; }
    const CalRound& c = cal[s.round_idx];
    p.valid = true; p.round = c.round; p.track_hash = c.track_hash; p.track_name = c.track_name;
    p.fmt = c.fmt; p.laps = c.laps; p.durationMin = c.durationMin; p.hour = c.hour;
    // RoundPlan.weather is the scalar that SimulateRace reads, which only distinguishes wet (wet = weather >= 2) from
    // dry: it does not need the slots, only whether the round gets wet at some point. c.weather is the authored list of
    // the round (); AnyWeatherWet resolves "wet" over the whole list, not only the first slot.
    p.weather = AnyWeatherWet(c.weather) ? 2 : 0;
    const SeriesDef* d = SeriesById(s.series_id);
    if (d) {
        p.class_name = d->class_name; p.profile_id = d->profile_id;
        // SAME SOURCE AS THE XML, THE RACE, AND THE SIMULATION. There used to be a FOURTH calculation of grid
        // size here (`min(liveries, 26)`), and with four places independently deciding the same thing, it was only a
        // matter of time before they disagreed: in fact they did, because the XML didn't cap at 26 and these did.
        // and BOUNDED by available drivers: 13 series don't have enough people to fill their class size, and asking for more
        // seats than the XML names brings back the generic auto-fill that all this exists to get rid of.
        // and by the weekend's SPLIT: the plan is for ONE class, so in a multiclass round it only carries its
        // share of the meeting (12 Pemberton_Zel121 cars in the IVORY_CUP_12573), not the class's whole grid. The simulator consumes it.
        // 16/09 and for what FITS ON THE CIRCUIT of this round: a series has ONE size, but its tracks
        // do not have the same slots (Northhurst Park 1971 is 26, Coldton Bay 32). See RoundClassShare in career_fields.h.
        const std::vector<std::string> cita = MulticlassMeetingAt(s.series_id, c.round);
        p.gridSize = RoundClassShare(cita, s.series_id, (int)c.track_hash,
                                     RoundGridSizeOverride(cita, s.series_id, c.round));
    }
    return p;
}

// rolling RACE-window average (finish quality across ALL series). Same orientation as
// PlayerForm (1.0 = win). 0.7 if empty (back-compat: saves with no samples).
double CareerForm(const CareerProgress& prog) {
    if (prog.recent_form.empty()) return 0.7;
    double sum = 0; for (float q : prog.recent_form) sum += q;
    return sum / (double)prog.recent_form.size();
}

double PlayerForm(const SeasonState& s, double careerForm) {
    const int K = 5;
    if (s.history.empty()) return careerForm;   // series with no history -> calibrate to your REAL career-level form
    int n = (int)s.history.size(); int start = n > K ? n - K : 0;
    double sum = 0; int cnt = 0;
    for (int i = start; i < n; i++) {
        const RoundOutcome& o = s.history[i];
        double q = (o.field > 1) ? (double)(o.field - o.pos) / (double)(o.field - 1) : 0.5;
        if (q < 0) q = 0; if (q > 1) q = 1;
        sum += q; cnt++;
    }
    double local = cnt ? sum / cnt : careerForm;
    // SHORT history (0 < n < K) -> blends local/career weighted by n/K (less history = more career).
    // n >= K -> pure local (no change from previous behavior).
    if (n < K) {
        double w = (double)n / (double)K;
        return w * local + (1.0 - w) * careerForm;
    }
    return local;
}

bool SeasonComplete(const SeasonState& s) {
    return s.round_idx >= (int)CalendarForSeries(s.series_id).size();
}

SettleReport SettleRound(SeasonState& s, CareerProgress& prog, const RaceResult& r,
                         const std::vector<ClassifiedEntry>& classification, const std::string& flName,
                         long winnerPrize, long floorPrize, bool rented, int rentPct, long carValue,
                         long entryOverride, bool factorySeat,
                         long coverage, long dealSalary,
                         long deductible, long travel, long resultBonus, int raceLaps,
                         const WearTelemetry* wear, long repBonus) {
    if (SeasonComplete(s)) return SettleReport{};   // invariant: never settle outside the calendar
    // thread coverage/dealSalary (the dropped args) + the new deductible/travel/resultBonus
    // terms through to Settle. Defaulted to 0 at every prior call site -> byte-identical net (regression guard).
    // raceLaps also passes through (default = reference -> back-compat: identical own cost).
    // wear (real wear from shared memory) also passes through; nullptr -> NOMINAL consumables (back-compat).
    // repBonus also passes through (default 0 -> byte-identical back-compat).
    SettleReport rep = Settle(prog, r, winnerPrize, floorPrize, rented, rentPct, carValue, entryOverride,
                              factorySeat, coverage, dealSalary, deductible, travel, resultBonus, raceLaps,
                              wear, repBonus);
    std::vector<CalRound> cal = CalendarForSeries(s.series_id);
    int round = (s.round_idx < (int)cal.size()) ? cal[s.round_idx].round : (s.round_idx + 1);
    double scale = RoundPointsScale(cal, s.round_idx);   // Fix (08/01): sprint scores 0.5 ONLY if the championship mixes formats
    PointsSystem sys; const PointsSystem* ps = PointsById(s.standings.points_system); if (ps) sys = *ps;
    std::vector<RoundScore> scores = ScoreRound(classification, sys, scale, flName);
    ApplyRound(s.standings, round, scores);
    // the other half of the reported cause. The season's history was ALWAYS saved as a finish
    // (dnfFlag stayed at its default false), so even though the engine knew it was a DNF, the round
    // it was recorded as "position N", and neither the classification nor the calendar showed the DNF. The data was already
    // there, carried by RaceResult::finished; it just had to not be thrown away.
    s.history.push_back(RoundOutcome{ round, r.pos, r.field, !r.finished });
    // pushes the player's finish quality into the RACE window (same orientation as PlayerForm:
    // 1.0 = win). Capped at N=10 (drops the oldest). Persisted via SaveCareer (prog.recent_form).
    {
        const int N = 10;
        double q = (r.field > 1) ? (double)(r.field - r.pos) / (double)(r.field - 1) : 0.5;
        if (q < 0) q = 0; if (q > 1) q = 1;
        prog.recent_form.push_back((float)q);
        if ((int)prog.recent_form.size() > N)
            prog.recent_form.erase(prog.recent_form.begin(),
                                   prog.recent_form.begin() + ((int)prog.recent_form.size() - N));
    }
    s.round_idx++;
    return rep;
}

// Championship purse by tier (level 1..7). Tunable.
static const long SEASON_PURSE[8] = { 0, 20000, 40000, 80000, 150000, 300000, 600000, 1200000 };

SeasonOutcome ChampionshipBonus(int playerPos, int fieldSize, int level, bool noShowSeason, long seasonPurseOverride) {
    SeasonOutcome o; o.player_pos = playerPos; o.field = fieldSize;
    if (playerPos < 1) playerPos = 1;
    int lv = (level < 1) ? 1 : (level > 7 ? 7 : level);
    double decay = std::pow(0.75, playerPos - 1);
    // PER-CLASS economy: the championship purse comes from SeriesEcon.champion (the class's own prestige/reward)
    // if the caller passes it (>0); otherwise, falls back to the SEASON_PURSE per-tier table (compat).
    long basePurse = (seasonPurseOverride > 0) ? seasonPurseOverride : SEASON_PURSE[lv];
    int xpv = (int)(XpThreshold(lv) * 0.6 * decay); if (xpv < 200) xpv = 200;
    int repv = (int)(25 * decay + 0.5);
    // The REP floor. Normally a season never NETS below +1 REP. But a NO-SHOW season (defined as:
    // the player missed every contracted round) must NOT be rewarded — gate the +1 floor on !noShowSeason so a
    // no-show lands <=0 (here the deep-decay bonus rounds to 0, and the per-round RATING_NOSHOW_DING + the
    // auto-break REP penalty make the season net-negative). A partially-raced season is NOT a no-show (its
    // missed_rounds < rounds) so it keeps the +1 floor (never wrongly floored down).
    if (!noShowSeason && repv < 1) repv = 1;
    o.xp = xpv; o.rep = repv; o.money = (long)(basePurse * decay);
    return o;
}

std::string ComputeNemesis(const std::vector<StandingRow>& finalTable, int playerPos) {
    if (playerPos <= 0) return std::string();          // no valid player position -> no nemesis
    std::string best;                                  // winning driver_id
    int bestDelta = INT_MAX;                           // |position - playerPos| of the best candidate
    int bestPos   = INT_MAX;                           // best candidate's final position (tiebreak: better = lower)
    for (int i = 0; i < (int)finalTable.size(); i++) {
        int pos = i + 1;                               // FinalTable comes sorted: index 0 = P1
        // 18/09: the row already says who it is; the name is only used in tables saved before it was known
        std::string id = !finalTable[i].id.empty() ? finalTable[i].id : DriverIdForName(finalTable[i].name);
        if (id.empty()) continue;                      // name doesn't resolve to a pool id
        const PoolDriver* pd = DriverById(id);
        if (!pd || !pd->hot) continue;                 // only HOT rivals from the cast
        int delta = pos - playerPos; if (delta < 0) delta = -delta;
        // tiebreak: smaller |Δpos|, then better (lower) final position, then lexicographic driver_id.
        bool better = false;
        if (delta < bestDelta) better = true;
        else if (delta == bestDelta) {
            if (pos < bestPos) better = true;
            else if (pos == bestPos && (best.empty() || id < best)) better = true;
        }
        if (better) { best = id; bestDelta = delta; bestPos = pos; }
    }
    return best;
}

SeasonOutcome FinishSeason(SeasonState& s, CareerProgress& prog, const std::string& playerName,
                           bool noShowSeason) {
    std::vector<StandingRow> table = FinalTable(s.standings);
    int pos = (int)table.size(); int field = (int)table.size();
    for (int i = 0; i < (int)table.size(); i++) if (table[i].name == playerName) { pos = i + 1; break; }
    const SeriesDef* d = SeriesById(s.series_id);
    int level = d ? d->level : 1;
    // PER-CLASS economy: champion bonus from SeriesEcon (by series_id, or by class_name); 0 => fallback per tier.
    const SeriesEcon* ec = FindEcon(s.series_id);
    if (!ec && d) ec = FindEconByClassName(d->class_name);
    long champOverride = ec ? ec->champion : 0;
    // A no-show season lifts the +1 REP floor (the caller computes noShowSeason from the
    // per-contract missed-round signal). A non-contracted / partially-raced season passes false (no-op).
    SeasonOutcome o = ChampionshipBonus(pos, field, level, noShowSeason, champOverride);
    if (pos == 1) prog.titles++;                            // champion -> +1 title (career record/gate)
    prog.credits += o.money;
    if (o.money != 0) EconLog("season_bonus", o.money, prog.credits);   //
    prog.rep += o.rep;
    prog.xp_total += o.xp;
    prog.xp_toward_next += o.xp;
    while (prog.frontier_phase < 7 && CheckPhaseUnlock(prog.xp_toward_next, prog.frontier_phase)) {   // A big XP gain can jump several phases at once; capped at phase 7 (same as Settle)
        prog.xp_toward_next -= XpThreshold(prog.frontier_phase);
        prog.frontier_phase++;
    }
    s.status = 1;
    return o;
}

std::vector<const SeriesDef*> AvailableSeries(int frontierPhase) {
    std::vector<const SeriesDef*> out;
    for (const auto& s : AllSeries()) if (s.level <= frontierPhase) out.push_back(&s);
    return out;
}

bool SaveSeason(const SeasonState& s, const char* path) {
    FILE* f = paths::FOpen(path, "wb");
    if (!f) return false;
    fprintf(f, "S|%s|%s|%d|%d|%d\n", s.series_id.c_str(), s.branch.c_str(), s.season_year, s.round_idx, s.status);
    for (const auto& h : s.history) fprintf(f, "H|%d|%d|%d\n", h.round, h.pos, h.field);
    fclose(f);
    std::string sp = std::string(path) + ".st";
    return SaveStandings(s.standings, sp.c_str());
}

bool LoadSeason(SeasonState& s, const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    s = SeasonState();
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        auto t = SplitPipe(line);
        if (t.empty()) continue;
        if (t[0] == "S" && t.size() >= 6) {
            s.series_id = t[1]; s.branch = t[2]; s.season_year = atoi(t[3].c_str());
            s.round_idx = atoi(t[4].c_str()); s.status = atoi(t[5].c_str());
        } else if (t[0] == "H" && t.size() >= 4) {
            s.history.push_back(RoundOutcome{ atoi(t[1].c_str()), atoi(t[2].c_str()), atoi(t[3].c_str()) });
        }
    }
    fclose(f);
    std::string sp = std::string(path) + ".st";
    if (!LoadStandings(s.standings, sp.c_str())) {   // .st missing/corrupt: rebuilds metadata instead of silently scoring 0
        s.standings.node_id = s.series_id;
        const SeriesDef* d = SeriesById(s.series_id);
        s.standings.points_system = d ? d->points_id : std::string();
    }
    return true;
}

} // namespace career
