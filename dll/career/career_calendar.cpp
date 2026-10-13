#include "career_calendar.h"
#include "career_dateutil.h"   // Real calendar model: DaysInYear/YmdFromYearDoy/AbsDayFromYmd/WeekdayFromAbsDay
#include "career_content.h"    // SeriesPlayable + ResolveTrackSubstitute + DLC products
#include "career_econ.h"       // EconLog -- ledger [money] (entry fees, insurance premium)
#include "career_fields.h"     // ComposeSeasonGrid, RosterForTier, DriverAge, ShouldRetire, PromoteFreedSeats
#include "career_customai.h"   // LiveriesForClass, GridDriver
#include "career_scoring.h"    // PointsSystem, ScoreRound
#include "career_standings.h"  // 01/10: RenameDriverInStandings (RenamePlayerInEnrollments)
#include "career_worldseason.h" // RunWorldSeason — the world runs too (the FIRST thing in the rollover)
#include "career_growth.h"     // AdvanceGrowth — the personal arc, with the results just made
#include "career_save.h"       // SaveData (rival_form/contract/completed_seasons/completed_races)
#include "career_contract.h"   // BreakContract (auto-break at MK_NOSHOW_BREACH_LIMIT)
#include "career_archetype.h"  // ARCH_RENEWAL (the renewal offer archetype at rollover)
#include "career_market.h"     // market::OfferTerms (renewal term shift base)
#include "career_data_json.h"  // BirthYearFor and the identity pool (skill growth was retired on 02/07)
#include "career_balance.h"    // bal::RATING_SEED_* (aged-rating projection)
#include "career_econ.h"       // ContractTypeIndex + InsurancePremiumForContract (premium at rollover)
#include "career_seats.h"      // ApplyRolloverSeatChanges (freed/open seats -> seat_occupant)
#include "career_memories.h"   // Memory table + MemoryStatusOf for BuildMemoryCalendar
#include "career_cards.h"      // CardOpSeasonReset -- sponsor-card season reset (clause_miss at rollover)
#include "career_mail.h"       // Living inbox mail: MailParams/MailVariantCount/MailHash/MailFirstName/MailFmtCredits/MailManagerName
#include "career_profile.h"    // ProfileMyRank/ProfileWorldRanking (preseason power ranking; imgui.h on the include path)
#include "json_min.h"          // Loads docs/career/audience.json (curated per-round audience)
#include "paths.h"             // Encoding fix (22/07): FOpen/RemoveFile/RenameFile (UTF-8 paths -> wide-char APIs)
#include "career_userlog.h"    // Calendar save failures now go to the support log (previously silent)
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <cstdarg>   // 0.7.0 review: SaveSink (vsnprintf) of the clock stamp
#include <cstdlib>
#include <cmath>
#include <map>
#include <set>
#include <mutex>     // 19/09: memo of the substitute distribution per season (SeasonTrackSubstitute)
#include <utility>   // std::pair/std::move for the (seriesId, year) memo-cache key

namespace career {

// local helper: looks up a series' entry (or nullptr)
static const SeasonState* enrollmentOf(const CareerCalendar& cc, const std::string& sid) {
    for (const auto& e : cc.enrollments) if (e.series_id == sid) return &e;
    return nullptr;
}

// footprint by the series' tier, without looking at durations. Common base for FootprintForSeries and FootprintForSlot.
static Footprint footprintByTier(const SeriesDef* d) {
    if (!d) return Footprint{1, 0};              // unknown series: the usual default
    if (d->level >= 6) return Footprint{2, 0};   // top-tier formulas: Fri-Sun
    if (d->level <= 1) return Footprint{0, 0};   // club / karting: race day only
    return Footprint{1, 0};                       // regional / GT / stock: Sat-Sun
}

Footprint FootprintForSeries(const std::string& seriesId) {
    const SeriesDef* d = SeriesById(seriesId);
    if (!d) return Footprint{1, 0};
    // endurance: any round of >=24h -> occupies the long week (roughly Wed-Sun). Answered per series, for
    // consumers that don't have a specific round in hand (the clock's landing logic, career_contract.cpp): it errs on the
    // safe side (lands BEFORE, never overshoots the event). Anyone who does have the round should use FootprintForSlot instead.
    for (const auto& c : CalendarForSeries(seriesId)) if (c.durationMin >= 1440) return Footprint{4, 1};
    return footprintByTier(d);
}

// Bug found on 05/08: the footprint of the specific round about to be raced. FootprintForSeries decides the endurance flag
// by checking whether ANY round in the series lasts >=24h, so a single 24h round on the calendar was contaminating the
// "long week" footprint onto every other round of that series: 199 of the 221 rounds across the 22 affected series
// were occupying 6 days without actually being 24h rounds -- including the 14 rounds of aurora_trophy_11261 and granite_championship_11220, which are club-level (a
// level-1 classics cup that was eating Wednesday-to-Monday just because a Thornmere round existed on that series' calendar). A 6h IVORY_CUP_12573 round at Wildham
// doesn't occupy Highgate week either. Here the endurance flag is decided by THIS round's own duration; the tier stays the same.
Footprint FootprintForSlot(const CalSlot& s) {
    if (s.durationMin >= 1440) return Footprint{4, 1};   // this round is 24h -> long week
    return footprintByTier(SeriesById(s.series_id));
}

// calendar-day window a slot occupies, inclusive on both ends: [doy-lead, doy+trail]. It's
// exactly the footprint documented by FootprintForSeries and nothing more: club/karting = race day only,
// regional = eve + race day, top-tier formula = Fri-Sun, endurance = the long week.
//
// Bug found on 05/08 ("Why can't I do the Jun 30 race?"): this was returning hi = doy+trail+1, one EXTRA day
// after the race that doesn't belong to any real footprint. Since SlotsConflict compares intervals
// inclusively (alo<=bhi && blo<=ahi), that extra day made two rounds on consecutive days collide
// every single time, even between two club series whose footprint is "just the race day". And a collision isn't
// just a warning: ResolveAfterRace immediately DNFs the other series' pending round, so as soon as you finish
// Sunday's race, Monday's would come back DNF without ever being run, while the clock was still on Sunday.
// This wasn't a rare edge case: the curated catalog has 206 rounds on MONDAY (14 of them harbor_trophy_6546) because
// apply_proposed_calendar.py resolves (track, doy) collisions with doy+=1 and it doesn't always land on a weekend
// -- over 7013 pairs of rounds from different series end up on consecutive days. The juniper_cup_3635 (Sundays) +
// harbor_trophy_6546 (Mondays) pair -- the two series any new career starts with -- shared 3 weekends: Apr
// 6/7, Jun 8/9 and Jun 29/30. On each one you could only race ONE; the other came back DNF no matter what.
// The +1 was a leftover from the pre-footprint window ([doy-1, doy+1], a rough +-1 day guess); footprints already model
// this precisely and don't need it.
void SlotOccupiedDays(const CalSlot& s, int& loDoy, int& hiDoy) {
    Footprint fp = FootprintForSlot(s);   // per round: one endurance round in a series doesn't inflate the others
    loDoy = s.doy - fp.lead; hiDoy = s.doy + fp.trail;
}

// (duration nuance): parameters for estimating a race's time window. Tunable.
//   AVG_LAP_MIN         = minutes per lap when the round is lap-based (no durationMin given). ~2min is a
//                         reasonable average for a tarmac circuit; it errs slightly HIGH on purpose (conservative: when in
//                         doubt, let them collide). Validated against the data: it leaves ~8 real short doubleheaders intact and blocks
//                         the long endurance rounds that used to slip through.
//   RACE_GAP_BUFFER_MIN = buffer between the end of one race and the start of the next (podium, packing up, traveling).
//   DEFAULT_RACE_MIN    = fallback when a round has neither a duration nor a lap count (only happens in synthetic tests).
static const double AVG_LAP_MIN          = 2.0;
static const int    RACE_GAP_BUFFER_MIN  = 30;
static const int    DEFAULT_RACE_MIN     = 30;

int EstimatedRaceMinutes(const CalSlot& s) {
    if (s.durationMin > 0) return s.durationMin;                 // time-based race: exact
    if (s.laps > 0) return (int)(s.laps * AVG_LAP_MIN + 0.999);  // lap-based: an estimate (ceil)
    return DEFAULT_RACE_MIN;
}
// [start, end] window of a race in minutes-of-day (start = hour*60; end += transition buffer).
static void raceWindow(const CalSlot& s, int& start, int& end) {
    start = s.hour * 60;
    end   = start + EstimatedRaceMinutes(s) + RACE_GAP_BUFFER_MIN;
}
// do two windows [as,ae) and [bs,be) overlap?
static bool windowsOverlap(int as, int ae, int bs, int be) { return as < be && bs < ae; }

// Memories: is this slot from a historical-era series? (single source of truth for the predicate: IsEraMemorySeries).
static bool slotIsEraMemory(const CalSlot& s) {
    const SeriesDef* d = SeriesById(s.series_id);
    return d && IsEraMemorySeries(*d);
}

bool SlotsConflict(const CalSlot& a, const CalSlot& b) {
    if (a.series_id == b.series_id) return false;  // the same series never collides with itself (doubleheaders = one trip)
    // Memories: a historical-era memory is FREE TIME -- it doesn't occupy the weekend, so it never collides
    // with anything, in either direction: it doesn't mark a warning/"clash" on the calendar, doesn't trigger the launch warning, and doesn't
    // DNF the real race that day (nor the reverse: running a modern race doesn't take the memory away from you). It's the
    // same rule that already exempts the clock and the latch in ResolveAfterRace, placed here -- in the single source of the
    // collision check -- so every consumer agrees: calendar marking, the launch warning,
    // overlap-driven DNF, and the market's season-clash check (SeasonClashesFor).
    if (slotIsEraMemory(a) || slotIsEraMemory(b)) return false;
    int alo, ahi, blo, bhi;
    SlotOccupiedDays(a, alo, ahi); SlotOccupiedDays(b, blo, bhi);
    if (!(alo <= bhi && blo <= ahi)) return false; // no day overlap -> no clash
    // travel + duration nuance. On the SAME day at the SAME circuit you can run both ONLY if their
    // time windows (start..start+duration+buffer) don't overlap -- a 66-lap race doesn't leave room
    // for another race that day even at a different start time. Elsewhere in the country on the same day -> always a clash (you can't
    // be in two places, and there's no time to travel). track_hash!=0 avoids merging slots that have no track.
    if (a.doy == b.doy && a.track_hash != 0 && a.track_hash == b.track_hash) {
        int as, ae, bs, be; raceWindow(a, as, ae); raceWindow(b, bs, be);
        return windowsOverlap(as, ae, bs, be);      // same circuit and day: only collides if the time windows overlap
    }
    return true;                                    // overlapping days at a different location -> travel-driven clash
}

// Real dates + time navigation (pure) ---
std::string DoyToDate(int doy) {
    if (doy < 1 || doy > 366) return std::string();
    static const int CUM[12]  = { 0,31,59,90,120,151,181,212,243,273,304,334 };  // Gregorian, not leap-aware
    static const char* MON[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    int d = doy; if (d > 365) d = 365;             // day-of-year 366 (leap year) -> treated as Dec 31 (no leap-year table)
    int m = 11; for (int i = 11; i >= 0; i--) if (d > CUM[i]) { m = i; break; }
    int day = d - CUM[m];
    char buf[16]; snprintf(buf, sizeof(buf), "%d %s", day, MON[m]);
    return std::string(buf);
}

// Real calendar model: a real date (year + day-of-year) with day-of-week, leap-aware. The visible face of the
// Gregorian clock (career_dateutil); replaces DoyToDate for consumers that need a real year.
std::string DateLabel(int year, int doy) {
    if (doy < 1) return std::string();
    int maxd = career::dateutil::DaysInYear(year); if (doy > maxd) doy = maxd;
    int m, d; career::dateutil::YmdFromYearDoy(year, doy, m, d);
    static const char* WD[7]   = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
    static const char* MON[12]  = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    int wd = career::dateutil::WeekdayFromAbsDay(career::dateutil::AbsDayFromYmd(year, m, d));
    char buf[32]; snprintf(buf, sizeof(buf), "%s %d %s", WD[wd], d, MON[m - 1]);
    return std::string(buf);
}

std::string WeekdayLabel(int year, int doy) {
    if (doy < 1) return std::string();
    static const char* WD[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
    int wd = career::dateutil::WeekdayFromAbsDay(career::dateutil::AbsDayFromYearDoy(year, doy));
    return WD[wd];
}

// real practice/qualifying/race dates for a slot within its weekend, based on its series'
// footprint (FootprintForSeries). See career_calendar.h for the full contract.
SessionDates SessionDatesForSlot(const CalSlot& s, int year) {
    SessionDates out;
    Footprint fp = FootprintForSlot(s);   // per round: a 6h race's practice/qualifying doesn't start on Highgate' Wednesday
    int raceDoy  = s.doy;
    int practiceDoy = raceDoy - fp.lead;                 // start of the weekend
    int qualDoy  = fp.lead > 0 ? raceDoy - 1 : raceDoy;  // day before the race (or the same day for a 1-day event)
    if (practiceDoy < 1) practiceDoy = raceDoy;
    out.race     = DateLabel(year, raceDoy);
    out.qualify  = DateLabel(year, qualDoy);
    out.practice = DateLabel(year, practiceDoy);
    return out;
}

int WeeksToNextEvent(const std::vector<CalSlot>& slots, int cursorDoy) {
    int best = 0x7fffffff;
    for (const auto& s : slots) {
        if (!s.valid || s.raced || s.dnf || s.locked) continue;   // only rounds you can still race
        if (!(s.enrolled || (!s.past))) continue;                  // entered, or still open in the future
        if (s.doy > cursorDoy && s.doy < best) best = s.doy;
    }
    if (best == 0x7fffffff) return 0;              // no event left ahead
    int days = best - cursorDoy;                   // full weeks, rounded up (ceil) -> never come up short
    return (days + 6) / 7;
}

// 19/09 (player report: Wildton Ring SIX times in the Thornwick Motors Ax797). The data was
// clean: each round with the missing track picked its substitute ALONE, with no memory of what the season was
// already running, and all of them asked for the same thing (same series era) -> the same circuit over and over. Now the substitute is
// decided over the WHOLE SEASON: the rounds are walked in order and each one excludes the circuits the
// season already runs and the substitutes already handed to earlier rounds (ResolveTrackSubstitute repeats only if
// nothing different is left).
// The season is that of the multiclass GROUP, with the HEAD series first (its rounds in order) and then the other
// members' own rounds: this way the series of one same event ask for exactly the same plan and the multiclass
// weekend isn't split in two (the hub regression ). A series with no group is its own group. The plan goes by
// missing track and not by round, so it doesn't matter who asks for it: the master calendar, the launch
// (SlotForRound) or the championship views (ApplyDlcToRound, the other hub regression ).
// Memo: a season's plan costs one ResolveTrackSubstitute per missing round and the master calendar would
// ask for it once per slot. It is recomputed only when something it depends on changes: the playable circuits
// (live catalog, Settings switches and the Steam veto change without notice, which is why what is there is signed and
// nobody is waited on to invalidate) or the calendar/series data (DataEpoch).
static unsigned long long PlayableTrackSignature() {
    unsigned long long h = 1469598103934665603ULL;   // FNV-1a over the hashes of the playable circuits
    for (const auto& kv : AllCircuitGeo()) {
        if (!TrackInstalled((int)kv.first)) continue;
        const unsigned long long x = (unsigned long long)(unsigned long)kv.first;
        for (int i = 0; i < 4; i++) { h ^= (x >> (i * 8)) & 0xffULL; h *= 1099511628211ULL; }
    }
    return h;
}
struct SeasonSubPlan {
    std::map<long, TrackSub> subs;   // missing track -> its substitute in this season
    std::set<long> used;             // what the season runs + all the substitutes already handed out
};
static std::mutex g_seasonSubMx;
static std::map<std::string, SeasonSubPlan> g_seasonSubPlans;
static unsigned long long g_seasonSubSig = 0, g_seasonSubEpoch = 0;

static bool SeasonTrackSubstitute(const std::vector<std::string>& members, const std::string& era,
                                  long missingHash, TrackSub& out) {
    std::string key = era;
    for (const auto& m : members) { key += '|'; key += m; }
    const unsigned long long sig = PlayableTrackSignature(), epoch = DataEpoch();
    std::lock_guard<std::mutex> lk(g_seasonSubMx);
    if (sig != g_seasonSubSig || epoch != g_seasonSubEpoch) {
        g_seasonSubPlans.clear(); g_seasonSubSig = sig; g_seasonSubEpoch = epoch;
    }
    auto it = g_seasonSubPlans.find(key);
    if (it == g_seasonSubPlans.end()) {
        SeasonSubPlan plan;
        std::vector<long> missing;                       // in order: head per round, then the other members
        for (const auto& m : members)
            for (const auto& r : CalendarForSeries(m)) {
                if (TrackInstalled((int)r.track_hash)) plan.used.insert(r.track_hash);
                else if (std::find(missing.begin(), missing.end(), r.track_hash) == missing.end())
                    missing.push_back(r.track_hash);
            }
        for (long h : missing) {
            TrackSub sub;
            if (!ResolveTrackSubstitute(h, era, sub, &plan.used)) continue;
            plan.subs[h] = sub;
            plan.used.insert(sub.hash);
        }
        it = g_seasonSubPlans.emplace(key, std::move(plan)).first;
    }
    auto sit = it->second.subs.find(missingHash);
    if (sit != it->second.subs.end()) { out = sit->second; return true; }
    // track the season doesn't run (a lone round or an off-calendar view): same, out with everything
    // the season already uses.
    return ResolveTrackSubstitute(missingHash, era, out, &it->second.used);
}

// single source for DLC gating/substitution (see career_calendar.h). Order: first the series
// (a class with no cars installed = entirely unplayable, regardless of track), then this round's track.
void ApplyDlcContentToSlot(CalSlot& s, const SeriesDef* sd) {
    if (sd && !SeriesPlayable(*sd)) {
        s.dlc_locked = true;
        s.dlc_product = DlcProductDisplay(SeriesMissingProduct(*sd));
        s.locked = true;
        return;
    }
    if (!TrackInstalled((int)s.track_hash)) {
        TrackSub sub;
        // Tester feedback (Aug 28, hub, generalized): the ERA used to weight the substitute has to be CONSISTENT
        // inside a multiclass group. Groups MIX eras (IVORY_CUP_12573: 2024 + "modern" + 2019) and each class
        // each class was moving to a different circuit -> divergent track_hash -> MeetingSeriesSet no longer grouped them, and the
        // multiclass weekend SPLIT silently (each class would race its own venue alone). The anchor is the FIRST
        // member of the group (its lead championship): they all ask for the same (hash, era) -> the same substitute,
        // which is also what a real championship would do (the whole meeting moves together).
        std::string era = sd ? sd->era : std::string();
        std::vector<std::string> season;   // 19/09: the season over which the substitutes are distributed
        if (sd) {
            season.push_back(sd->id);
            int gi = MulticlassGroupOf(sd->id);
            if (gi >= 0) {
                const auto& groups = MulticlassGroups();
                if (gi < (int)groups.size() && !groups[gi].empty()) {
                    const SeriesDef* anchor = SeriesById(groups[gi][0]);
                    if (anchor) era = anchor->era;
                    season = groups[gi];   // head first: the same plan for the whole group
                }
            }
        }
        const bool found = season.empty() ? ResolveTrackSubstitute(s.track_hash, era, sub)
                                          : SeasonTrackSubstitute(season, era, s.track_hash, sub);
        if (found) {
            s.dlc_subbed = true;
            s.orig_track_hash = s.track_hash; s.orig_track_name = s.track_name;
            s.track_hash = sub.hash; s.track_name = sub.name;
            s.dlc_product = sub.productDisplay;
        } else {
            s.dlc_locked = true;
            s.dlc_pack = TrackDlcPack(s.track_hash);
            s.dlc_product = DlcProductDisplay(DlcProductForTrackPack(s.dlc_pack));
            s.locked = true;
            // 31/08 (testing with a no-DLC account): a round that gets BLOCKED instead of moving is a dead
            // end for the player, and it doesn't reproduce in the suite (with 0/32 products owned, 107 circuits
            // are left playable and Coldvale Park moves to Thorndale 2001). If it happens live, log WHY: how many
            // circuits the engine sees as playable at that moment. Capped: the calendar has 1,342 rounds --
            // one line PER SERIES: the first 20 were eaten by the 3 rallycross series (6-8 rounds
            // each) and you couldn't see whether the lock was touching anyone else.
            static std::set<std::string> s_lockLogged;
            if (s_lockLogged.size() < 40 && s_lockLogged.insert(s.series_id).second) {
                int jugables = 0;
                for (const auto& kv : AllCircuitGeo()) if (TrackInstalled((int)kv.first)) jugables++;
                UserLog("dlc", "round %s R%d LOCKED at '%s' (pack=%s): no substitute among %d playable of %d circuits",
                        s.series_id.c_str(), s.round, s.track_name.c_str(), s.dlc_pack.c_str(),
                        jugables, CircuitGeoCount());
            }
        }
    }
}

// Tester feedback (Aug 28, hub): championship views were listing the ORIGINAL track of a round whose circuit isn't
// available -- the master calendar substitutes it, and the Championships tab was saying something else
// ("Championship tab still shows Coldvale Park"). Same logic and the same group anchor as the slot (via
// ApplyDlcContentToSlot with a temporary slot). Substitution only: a round with no substitute keeps its name --
// these views are informational; the lock lives in the master calendar and at launch.
void ApplyDlcToRound(CalRound& r, const SeriesDef* sd) {
    if (TrackInstalled((int)r.track_hash)) return;
    CalSlot s; s.series_id = sd ? sd->id : std::string();
    s.track_hash = r.track_hash; s.track_name = r.track_name;
    ApplyDlcContentToSlot(s, sd);
    if (s.dlc_subbed) { r.track_hash = s.track_hash; r.track_name = s.track_name; }
}
void ApplyDlcToRounds(std::vector<CalRound>& rs, const SeriesDef* sd) {
    for (auto& r : rs) ApplyDlcToRound(r, sd);
}

// the reference calendar (2025) realigned to 'year' via RealignDoy; year==kStartYear is the
// identity case (zero regression against CalendarForSeries). Rounds with no date (doy<=0) stay untouched. Sorted by doy.
std::vector<CalRound> ProjectSeason(const std::string& seriesId, int year) {
    std::vector<CalRound> cal = CalendarForSeries(seriesId);
    for (auto& c : cal) {
        if (c.doy <= 0) continue;
        c.doy  = career::dateutil::RealignDoy(kStartYear, c.doy, year);
        {   // date as "Mon DD" (month-first, the calendar's own convention), leap-aware; NOT DoyToDate (which gives "DD Mon").
            //         At year==kStartYear it reproduces the PSV's string byte for byte (2025 isn't a leap year).
            int mm, dd; career::dateutil::YmdFromYearDoy(year, c.doy, mm, dd);
            static const char* MON3[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
            char buf[16]; snprintf(buf, sizeof(buf), "%s %d", MON3[mm - 1], dd);
            c.date = buf;
        }
    }
    std::stable_sort(cal.begin(), cal.end(), [](const CalRound& a, const CalRound& b){ return a.doy < b.doy; });
    return cal;
}

// Memo cache for CalendarForSeriesInYear, keyed by (seriesId, year) ----
// ProjectSeason is pure (same arguments -> same vector) but it walks, realigns and reorders the entire
// calendar on every call; the call sites migrating to CalendarForSeriesInYear (everything that currently compares day-of-year/date
// against the live clock) invoke it in a loop over the ~115 series in the catalog (the same pattern as
// BuildMasterCalendar), so memoizing by (seriesId, year) saves that repeated work without changing the
// result.
//
// Mandatory invalidation -- why this is the critical point:
//   1) Tests: the harness reloads PSVs constantly between cases (LoadSeries/LoadSeriesCalendar). Without clearing
//      this cache, a test that fills it with calendar A and then loads calendar B for the SAME
//      series_id would keep reading A's dates: cross-test contamination that has nothing to do
//      with whatever bug the test is actually investigating (test_calendar_in_year guards exactly this).
//   2) The game: any operation that hot-swaps the catalog (a data reload, syncing the live
//      copy, a future hot-reload) would serve stale dates to the UI/engine until the process restarts -- a
//      silent bug indistinguishable from badly curated data (nobody would suspect a cache).
// That's why every call site that replaces the series or calendar table (career_series.cpp: LoadSeries,
// ReplaceSeries, LoadSeriesCalendar, ReplaceCalendar) calls InvalidateCalendarProjectionCache() right after
// replacing its table. LoadSeries/ReplaceSeries are included even though ProjectSeason doesn't currently read SeriesDef (it doesn't
// change its result): this is a defensive policy -- if ProjectSeason ever starts consulting SeriesDef
// (e.g. to vary something by level/tier), the invalidation is already in the right place without having to
// remember to add it then. The cost (a full clear(), not selective invalidation) is negligible
// compared to the risk of serving stale data.
static std::map<std::pair<std::string, int>, std::vector<CalRound>> g_calInYearCache;

std::vector<CalRound> CalendarForSeriesInYear(const std::string& seriesId, int year) {
    auto key = std::make_pair(seriesId, year);
    auto it = g_calInYearCache.find(key);
    if (it != g_calInYearCache.end()) return it->second;
    std::vector<CalRound> proj = ProjectSeason(seriesId, year);
    auto ins = g_calInYearCache.emplace(std::move(key), proj);
    return ins.first->second;
}

// 0.7.0 port (F22): the data epoch. See career_calendar.h.
static unsigned long long g_dataEpoch = 1;
unsigned long long DataEpoch() { return g_dataEpoch; }
void BumpDataEpoch() { ++g_dataEpoch; }

// see the cache comment above: the sole safeguard against a reload serving stale dates.
void InvalidateCalendarProjectionCache() {
    g_calInYearCache.clear();
    // 0.7.0 port (F22): and with it the data epoch, which is what invalidates the career_seats memos. It goes
    //  HERE and not in the four callers for the same reason as the cache: whoever replaces a table tomorrow
    //  doesn't have to remember anything.
    BumpDataEpoch();
}

std::set<std::string> ContractedSeries(const SaveData& save) {
    std::set<std::string> out;
    for (const auto& c : save.contracts) if (c.active) out.insert(c.series_id);
    return out;
}

std::vector<CalSlot> BuildMasterCalendar(const CareerCalendar& cc, int frontierPhase,
                                         const std::set<std::string>* unlockedGates,
                                         const std::set<std::string>* ownedClasses,
                                         const std::set<std::string>* contractSeries) {
    // reverse map: championship -> its qualifier series (the first one that unlocks it)
    std::map<std::string, std::string> gatedBy;
    for (const auto& q : AllSeries())
        if (!q.unlocks.empty() && !gatedBy.count(q.unlocks)) gatedBy[q.unlocks] = q.id;

    std::vector<CalSlot> out;
    for (const auto& sd : AllSeries()) {
        const SeasonState* en = enrollmentOf(cc, sd.id);
        std::vector<CalRound> cal = ProjectSeason(sd.id, cc.year);   // dates realigned to the real year
        for (const auto& c : cal) {
            if (c.doy <= 0) continue;              // a series with no dates -> excluded from the master calendar
            CalSlot s;
            s.series_id  = sd.id;
            s.track_name = c.track_name;
            s.date       = c.date;
            s.note       = c.note;
            s.round      = c.round;
            s.doy        = c.doy;
            s.track_hash = c.track_hash;
            s.fmt        = c.fmt;
            s.laps       = c.laps;
            s.durationMin= c.durationMin;
            s.hour       = c.hour;
            s.weather    = c.weather;
            s.level      = sd.level;
            s.pitKmh      = c.pitKmh;
            s.practiceMin = c.practiceMin;
            s.qualifyMin  = c.qualifyMin;
            s.gridSize    = c.gridSize;
            s.valid      = true;
            // Hybrid gating: level AND a gate (the gate only applies to gated championships).
            // unlockedGates stores the series_id of the destination championship (sd.id), not of the qualifier.
            auto git = gatedBy.find(sd.id);
            bool gated = (git != gatedBy.end());
            bool gatePassed = gated && unlockedGates && unlockedGates->count(sd.id) > 0;
            s.gate_locked    = gated && !gatePassed;
            s.gate_qualifier = gated ? git->second : std::string();
            // unified DLC gating/substitution (unplayable series + missing track with/without a substitute).
            ApplyDlcContentToSlot(s, &sd);
            // Memories: for historical-era series, the gate switches from tier to car ownership.
            // Only when ownedClasses is supplied (via the live path); ownedClasses==nullptr keeps the tier-based gate (legacy/compat).
            bool useOwnGate = IsEraMemorySeries(sd) && ownedClasses != nullptr;
            // 01/10 (jse67): an active contract in the series lifts the level lock (see the .h). The window
            // offers seats at fp+1 and the player signs them; otherwise they ran next year as "locked".
            const bool contracted = contractSeries && contractSeries->count(sd.id) > 0;
            bool lockedByAccess = useOwnGate ? (ownedClasses->count(sd.class_name) == 0)
                                             : (sd.level > frontierPhase && !contracted);
            s.locked = lockedByAccess || s.gate_locked || s.dlc_locked;
            s.enrolled   = (en != nullptr);
            if (en) {
                // has this round already been resolved?
                int resolved = (int)en->history.size();
                int idxInCal = -1, k = 0;
                for (const auto& cc2 : cal) { if (cc2.round == c.round) { idxInCal = k; break; } k++; }
                if (idxInCal >= 0 && idxInCal < resolved) {
                    s.dnf   = en->history[idxInCal].dnfFlag;
                    s.raced = !s.dnf;
                }
            }
            s.past = (!s.enrolled && c.doy < cc.cursor_doy);
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const CalSlot& a, const CalSlot& b) {
        if (a.doy != b.doy) return a.doy < b.doy;
        if (a.series_id != b.series_id) return a.series_id < b.series_id;
        return a.round < b.round;
    });
    // flag conflicts between pending ENTERED slots on the same weekend (different series)
    for (size_t i = 0; i < out.size(); i++) {
        if (!out[i].enrolled || out[i].raced || out[i].dnf) continue;
        for (size_t j = i + 1; j < out.size(); j++) {
            if (!out[j].enrolled || out[j].raced || out[j].dnf) continue;
            if (SlotsConflict(out[i], out[j])) { out[i].conflict = true; out[j].conflict = true; }
        }
    }
    // corollary: "one race per day". If you've already raced TODAY (cursor==last_race_doy), any other races
    // today at a different circuit are blocked by travel -- including OPEN ones (not entered), which is exactly the
    // hole that was found: you'd race a Kingsmere at Northford and could still race another cup in Brazil the same day.
    // Exception: the same circuit (a same-venue doubleheader at another time) is NOT blocked. It only applies "today"; once the
    // clock advances those rounds move to `past` and this goes inert (self-limiting, doesn't contaminate future years).
    if (cc.last_race_doy > 0 && cc.last_race_doy == cc.cursor_doy) {
        int ls = cc.last_race_hour * 60, le = ls + cc.last_race_minutes + RACE_GAP_BUFFER_MIN;   // window already raced
        for (auto& s : out) {
            if (s.raced || s.dnf) continue;
            if (s.doy != cc.last_race_doy) continue;
            // Memories: a memory round is NOT a trip -- it's free time outside the clock (which is why
            // running it doesn't arm this latch either, in ResolveAfterRace). Having actually raced today can't lock you out of
            // an era's door: the same exemption as in SlotsConflict, here for "one race per day".
            if (slotIsEraMemory(s)) continue;
            if (s.track_hash != cc.last_race_circuit) { s.travel_locked = true; continue; }   // a different venue -> travel
            // same venue (doubleheader): only blocks if its window overlaps the one you already raced (66 laps don't leave
            // room for another); if they don't overlap, it's still available (a short, well-separated same-venue doubleheader).
            int ss, se; raceWindow(s, ss, se);
            if (windowsOverlap(ss, se, ls, le)) s.travel_locked = true;
        }
    }
    return out;
}

// Economy: per CATEGORY (ams2_series_econ.psv); the per-level tables remain as a safety net ----
// Season entry fee re-anchored to the research (0.4k -> 60k). The previous table {4k..280k} was ~10x
// too high at low tiers and it was accidentally compensated by charging zero entry; once entry is charged per category
// (EntryFeeFor) both numbers were reconciled together.
static const long INSCRIPTION_BASE[8] = { 0, 400, 600, 1500, 4000, 10000, 30000, 60000 };
static const long ROUND_PURSE[8]      = { 0, 3000, 6000, 12000, 24000, 48000, 96000, 190000 };
static int clampLevel(int lv) { return lv < 1 ? 1 : (lv > 7 ? 7 : lv); }

// Is this a superlicense qualifier? (a standalone round with a championship as its destination). It gates two
// economy exceptions below: its own econ row keyed by series_id, and an exemption from the prize floor.
static bool IsGateQualifier(const SeriesDef* d) {
    return d && d->kind == "standalone" && !d->unlocks.empty();
}
const SeriesEcon* EconForSeries(const std::string& seriesId) {
    const SeriesDef* d = SeriesById(seriesId);
    if (!d) return nullptr;
    // Aug 28 (tester feedback): the qualifier gets its own econ row (node_id == series_id, no class
    // lookup) -- prize == entry fee (approving it refunds the fee). Without this it inherited the real championship's row BY
    // CLASS: the F1 qualifier was paying out a real factory F1 round's prize (180k for one afternoon). Only the qualifier
    // branch looks up by series_id; every other series still resolves by class, same as always.
    if (IsGateQualifier(d)) { const SeriesEcon* own = FindEcon(seriesId); if (own) return own; }
    return FindEconByClassName(d->class_name);
}
long InscriptionFor(const std::string& seriesId) {
    const SeriesDef* d = SeriesById(seriesId);
    // Aug 28: the QUALIFIER doesn't charge an entry fee -- the entry fee belongs to the CHAMPIONSHIP, and the qualifier
    // is a standalone test whose fee IS the entry fee (which the prize refunds on passing). Without this, the settlement in the
    // common path (Enroll + ChargeInscription) was charging it the level-6 30k fee, and the "net 0" was really net -30k.
    if (IsGateQualifier(d)) return 0;
    return d ? INSCRIPTION_BASE[clampLevel(d->level)] : 0;
}
long RoundWinnerPrize(const std::string& seriesId) {
    const SeriesEcon* ec = EconForSeries(seriesId);
    if (ec) {
        long prize = ec->premio;     // per category (root fix: heritage series now carry an era prize pool too)
        // Winning has to pay off, even if tight. Real case: an international Formula Vee with
        // a round cost of ~3700 (season fee + entry + rental + travel) and a win prize of 3000 from the PSV. Dynamic floor:
        // the cost of the WORST self-funded round times PRIZE_MIN_MARGIN, rounded to 100.
        // heritage is EXEMPT from the floor: its worst round includes RENTAL, and renting a historical car (e.g. F-V10:
        // 200k/race) would inflate the prize pool above modern F1 (190k), breaking the hierarchy. The era
        // prize pool is anchored to owning the car (run-cost + entry); renting a classic is a gentleman's luxury, not a business.
        // Aug 28: the QUALIFIER sits outside the prize floor -- the floor guarantees that winning a SELF-FUNDED
        // round breaks even, and the qualifier test is run in a BORROWED car (no rental, no round to
        // fund): its prize IS the refunded entry fee (prize == entry fee, net 0), and the floor would inflate it
        // back to the exploit that was just closed.
        if (prize > 0 && ec->model != "heritage" && !IsGateQualifier(SeriesById(seriesId))) {
            long worstRound = InscriptionFor(seriesId) + ec->entry + ec->alquiler + bal::TRAVEL_CROSS_CONTINENT;
            long minPrize = (long)(worstRound * bal::PRIZE_MIN_MARGIN + 0.5);
            minPrize = ((minPrize + 99) / 100) * 100;
            if (prize < minPrize) prize = minPrize;
        }
        return prize;
    }
    const SeriesDef* d = SeriesById(seriesId);
    return d ? ROUND_PURSE[clampLevel(d->level)] : 0;
}
// prize to DISPLAY on the calendar/pre-race screen. Since the root fix (every
// series now carries a prize pool, heritage included), RoundWinnerPrize no longer returns 0 for healthy data; this branch
// remains as a safety net (a series with no econ row and no level -> shows the floor, never a raw "0").
long RoundWinnerPrizeDisplay(const std::string& seriesId) {
    long p = RoundWinnerPrize(seriesId);
    if (p > 0) return p;
    const SeriesDef* d = SeriesById(seriesId);
    return d ? FloorForPhase(clampLevel(d->level)) : 0;
}
// "audience" 0-100 for a race (the crowd/followers it draws). It only scales ELO/reputation,
// never the prize money. First weighted model: a base by level (higher tier = more audience) plus a boost from
// endurance format (format 2 draws more of a crowd). Fine-grained per-circuit/category values are curated separately.
static const int AUDIENCE_BASE[8] = { 0, 25, 35, 45, 58, 70, 84, 95 };  // index by level 1..7
int AudienceForRace(int level, int fmt) {
    int a = AUDIENCE_BASE[clampLevel(level)];
    if (fmt == 2) a += 5;                 // endurance
    return a < 0 ? 0 : (a > 100 ? 100 : a);
}
// Curated audience table (docs/career/audience.json). Empty until LoadAudienceData runs;
// after that, EMIT/SETTLE use the fine per-circuit x category value instead of the base per-level model. Loaded once.
static std::map<std::string, std::map<int,int>> g_audience;   // series_id -> round -> audience 0-100
int LoadAudienceData(const char* careerDir) {
    g_audience.clear();
    if (!careerDir || !*careerDir) return 0;
    std::string path = careerDir;
    if (!path.empty() && (path.back() == '\\' || path.back() == '/')) path.pop_back();
    path += "\\audience.json";
    std::string err;
    jsonm::ValuePtr root = jsonm::ParseFile(path.c_str(), &err);
    if (!root || !root->isObject()) return 0;
    int n = 0;
    for (const auto& kv : root->obj) {
        const jsonm::Value* rounds = kv.second.get();
        if (!rounds || !rounds->isObject()) continue;
        auto& m = g_audience[kv.first];
        for (const auto& rk : rounds->obj) {
            int rnd = atoi(rk.first.c_str());
            const jsonm::Value* v = rk.second.get();
            if (rnd <= 0 || !v || !v->isNumber()) continue;
            int a = v->asInt(0);
            if (a < 0) a = 0; if (a > 100) a = 100;
            m[rnd] = a; n++;
        }
    }
    return n;
}
int AudienceForRaceEx(const std::string& seriesId, int round, int level, int fmt) {
    auto it = g_audience.find(seriesId);
    if (it != g_audience.end()) {
        auto jt = it->second.find(round);
        if (jt != it->second.end()) return jt->second;   // fine-grained curated value
    }
    return AudienceForRace(level, fmt);                  // fallback: base per-level model
}
void SetAudienceEntryForTest(const std::string& seriesId, int round, int audience) {
    if (audience < 0) audience = 0; if (audience > 100) audience = 100;
    g_audience[seriesId][round] = audience;
}
void ClearAudienceForTest() { g_audience.clear(); }
// ELO/reputation multiplier from audience. Upside-only by design: never
// below 1.0 (unpopular races don't get penalized); the cap gives +25%. It only applies to the positive delta
// (a bad race doesn't get punished harder just for being popular) in SettleRating.
double AudienceRepFactor(int audience) {
    if (audience < 0) audience = 0; if (audience > 100) audience = 100;
    return 1.0 + (audience / 100.0) * 0.25;   // 1.0 (low audience) .. 1.25 (cap)
}
long EntryFeeFor(const std::string& seriesId) {
    const SeriesEcon* ec = EconForSeries(seriesId);
    if (ec) return ec->entry;        // per category (the old flow passed entryOverride=0: free)
    const SeriesDef* d = SeriesById(seriesId);
    return d ? EntryForPhase(d->level) : 0;
}

SeasonState* FindEnrollment(CareerCalendar& cc, const std::string& sid) {
    for (auto& e : cc.enrollments) if (e.series_id == sid) return &e;
    return nullptr;
}
const SeasonState* FindEnrollment(const CareerCalendar& cc, const std::string& sid) {
    for (const auto& e : cc.enrollments) if (e.series_id == sid) return &e;
    return nullptr;
}
bool IsEnrolled(const CareerCalendar& cc, const std::string& sid) { return FindEnrollment(cc, sid) != nullptr; }

// see the declaration in career_calendar.h -- strict resolution by the series actually raced, no fallback to another series.
const SeasonState* StandingsForRace(const CareerCalendar& cc, const std::string& raceSeriesId) {
    if (raceSeriesId.empty()) return nullptr;            // a standalone race -> no championship to show
    const SeasonState* e = FindEnrollment(cc, raceSeriesId);
    if (!e || e->standings.drivers.empty()) return nullptr;
    return e;
}

// see the declaration in career_calendar.h -- the privateer rule, in a testable function.
bool IsMidSeasonIdentitySwitch(int seasonVeh, int seasonOrd, int newVeh, int newOrd) {
    if (seasonVeh == 0 || seasonOrd < 0) return false;   // haven't raced this season yet -> free to switch
    if (newVeh == 0 || newOrd < 0) return false;         // no concrete new identity -> nothing to penalize
    return seasonVeh != newVeh || seasonOrd != newOrd;
}

// access enforcement: see the declaration in career_calendar.h.
bool CanEnterSeries(const SaveData& save, const std::string& seriesId) {
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd || !sd->closed) return true;                 // unknown or OPEN -> privateer allowed
    return ContractForSeries(save, seriesId) != nullptr; // CLOSED -> only with an active contract
}

// Is the season (year) complete? True iff there's >=1 entry and ALL of them are closed. An entry
// counts as finished if status==1 (set by FinishSeason/FinalizeSeason) OR if its calendar is complete
// (round_idx >= number of rounds; SeasonEnded). No entries -> false (nothing to close).
bool AllSeasonsComplete(const CareerCalendar& cc) {
    if (cc.enrollments.empty()) return false;
    for (const auto& e : cc.enrollments)
        if (e.status != 1 && !SeasonEnded(e)) return false;
    return true;
}

// The rookie calendar's opening date = the earliest doy>0 among ALL level-1 series. Deterministic
// (walks AllSeries in load order and takes the minimum). 0 if no level-1 round has a loaded date.
int RookieOpenerDoy() {
    int best = 0;
    for (const auto& sd : AllSeries()) {
        if (sd.level != 1) continue;
        for (const auto& c : CalendarForSeries(sd.id)) {
            if (c.doy <= 0) continue;
            if (best == 0 || c.doy < best) best = c.doy;
        }
    }
    return best;
}

EnrollResult Enroll(CareerCalendar& cc, const std::string& seriesId, CareerProgress& prog,
                    bool waiveInscription) {
    (void)prog;   // The entry fee is now deferred to commit time -> Enroll no longer touches credits
    EnrollResult r;
    if (IsEnrolled(cc, seriesId)) { r.ok = true; r.already = true; r.paid = 0; return r; }
    const SeriesDef* d = SeriesById(seriesId);
    SeasonState s = StartSeason(seriesId, d ? d->region : std::string());
    // The normal path creates the record with inscription_paid=false (charged at commit time via
    // ChargeInscription) and does NOT require a balance -- the "no money" gate lives in the car-selection UI. The
    // waived path (contract/rollover) marks the entry as PAID (the team covers it), like a factory seat.
    s.inscription_paid = waiveInscription;
    cc.enrollments.push_back(s);
    r.paid = 0; r.ok = true;   // nothing charged at enrollment on either path
    return r;
}

// Entry fee still OWED for a series. Not entered -> InscriptionFor (the
// enroll+charge are deferred to the settle of its first race, but the UI/gate has to count it right away); entered
// but unpaid -> InscriptionFor; paid/covered -> 0.
long InscriptionDue(const CareerCalendar& cc, const std::string& seriesId) {
    const SeasonState* e = enrollmentOf(cc, seriesId);
    if (e && e->inscription_paid) return 0;
    return InscriptionFor(seriesId);
}

// Charges the owed entry fee at commit time (once), marks inscription_paid, debits credits.
long ChargeInscription(CareerCalendar& cc, const std::string& seriesId, CareerProgress& prog) {
    SeasonState* e = FindEnrollment(cc, seriesId);
    if (!e || e->inscription_paid) return 0;   // not entered, or already paid -> nothing to charge
    long ins = InscriptionFor(seriesId);
    if (ins <= 0) { e->inscription_paid = true; return 0; }   // qualifier (entry fee 0): nothing to charge or log
    prog.credits -= ins;                       // the UI gate already guaranteed the balance
    EconLog("inscription", -ins, prog.credits);   //
    e->inscription_paid = true;
    return ins;
}

// SimulateRound: deterministic AI finishing order (without the player) ----

// FNV-1a 32-bit, deterministic and portable (std::hash isn't stable across compilers)
static unsigned long fnv1a(const std::string& s) {
    unsigned long h = 2166136261UL;
    for (unsigned char c : s) { h ^= c; h *= 16777619UL; }
    return h;
}

// the SAME source used by the XML and by the race's seat count. This used to be a THIRD, independent calculation
// (`min(liveries, 26)`), so the grid being SIMULATED could differ from the one actually raced and the one written out:
// three different numbers for the same question. The 26 was also an unjustified literal, not an actual game limit.
// class size, bounded by how many people there are to fill it and by how the weekend is split: in a
// multiclass round this class doesn't grid its entire roster, only its share of the meeting (Pemberton_Zel121 grids 12 cars at the IVORY_CUP_12573,
// not 18). What's simulated is what actually gets raced; otherwise the standings for rounds you skip wouldn't match
// the ones you do race. The specific round matters because some series run certain rounds as part of a meeting and others standalone.
// 16/09 ...and by what FITS IN THE CIRCUIT of that round, for the same reason: what is run is simulated, and
// what is run can't exceed the track's seats. See RoundClassShare in career_fields.h.
static int gridSizeFor(const std::string& seriesId, int round) {
    const std::vector<std::string> cita = MulticlassMeetingAt(seriesId, round);
    long trackHash = 0;
    for (const auto& r : CalendarForSeries(seriesId)) if (r.round == round) { trackHash = r.track_hash; break; }
    return RoundClassShare(cita, seriesId, (int)trackHash,
                           RoundGridSizeOverride(cita, seriesId, round));
}

// see career_calendar.h. The bridge to the XML entry list, installed by the overlay (which can actually read files).
static RacedGridProvider g_racedGridProvider;
void SetRacedGridProvider(RacedGridProvider p) { g_racedGridProvider = std::move(p); }

// the drivers in the XML the game is about to read take precedence over any recomposition: they are, literally,
// the ones who would have raced this round. With no provider or no XML for this series, it composes the grid as usual.
// The WHOLE class, uncut: what fits is decided by whoever asks for it.
static std::vector<GridDriver> CastForRound(const SeriesDef& d, int round, double playerForm, int gs) {
    std::vector<GridDriver> grid;
    if (g_racedGridProvider) grid = g_racedGridProvider(d.id, round);
    if (grid.empty())
        grid = ComposeSeasonGrid(d.id, d.level, playerForm, gs, /*useHomage=*/false);
    return grid;
}

std::vector<ClassifiedEntry> SimulateRound(const std::string& seriesId, int round, double playerForm) {
    std::vector<ClassifiedEntry> out;
    const SeriesDef* d = SeriesById(seriesId);
    if (!d) return out;
    int gs = gridSizeFor(seriesId, round);
    if (gs < 1) return out;
    std::vector<GridDriver> grid = CastForRound(*d, round, playerForm, gs);
    // ...AND ONLY THOSE THAT FIT. The XML names the WHOLE class (22 LANTERN_MASTERS_7489) and at a IVORY_CUP_12573 event the class lines up
    // 12: all 22 were simulated, and since the provider also joined the three classes of the event, lantern_trophy_7694 came out with
    // 54 rows and the skipped round scored in its table 14 MERROWDOWN_MYR373 drivers and 18 Pemberton_Zel121 ones. The first ones stay
    // in the XML order, which is the seats' order: the same people in all the rounds you skip.
    if ((int)grid.size() > gs) grid.resize((size_t)gs);
    struct K { GridDriver* g; double key; };
    std::vector<K> ks;
    for (auto& g : grid) {
        double noise = (double)(fnv1a(g.name + "#" + std::to_string(round)) % 350);   // 0..349
        ks.push_back({ &g, g.race_skill * 1000.0 - noise });
    }
    std::sort(ks.begin(), ks.end(), [](const K& a, const K& b) {
        if (a.key != b.key) return a.key > b.key;
        return a.g->name < b.g->name;        // deterministic tie-break
    });
    int pos = 1;
    for (auto& k : ks) {
        ClassifiedEntry e;
        e.name = k.g->name;
        e.carClass = d->class_name;
        e.pos = pos++;
        e.finished = true;
        e.dnf = false;
        e.driver_id = k.g->driver_id;   // stable identity -> the simulated round actually moves ELO
        out.push_back(e);
    }
    return out;
}

// see career_calendar.h. The whole event: each class with its SimulateRound (its part and its label), and the
// classes in the order the XMLs are written in (level, and prototype before GT). The position that counts is the
// class one, which the settle derives on its own; the order between classes only places the blocks.
std::vector<ClassifiedEntry> SimulateMeeting(const std::string& seriesId, int round, double playerForm) {
    const std::vector<std::string> cita = MulticlassMeetingAt(seriesId, round);
    if (cita.size() < 2) return SimulateRound(seriesId, round, playerForm);
    std::map<std::string, int> rango;
    { const std::vector<ClassDeployment> orden = CurrentClassDeployments();
      for (int i = 0; i < (int)orden.size(); i++) rango.emplace(orden[i].class_name, i); }
    std::vector<std::pair<int, std::vector<ClassifiedEntry>>> clases;
    for (size_t i = 0; i < cita.size(); i++) {
        const SeriesDef* s = SeriesById(cita[i]);
        if (!s) continue;
        const auto it = rango.find(s->class_name);
        const int r = (it != rango.end()) ? it->second : (int)(rango.size() + i);   // class without XML: behind
        clases.push_back({ r, SimulateRound(cita[i], round, playerForm) });
    }
    std::stable_sort(clases.begin(), clases.end(),
                     [](const std::pair<int, std::vector<ClassifiedEntry>>& a,
                        const std::pair<int, std::vector<ClassifiedEntry>>& b) { return a.first < b.first; });
    std::vector<ClassifiedEntry> out;
    for (auto& c : clases)
        for (auto& e : c.second) { e.pos = (int)out.size() + 1; out.push_back(std::move(e)); }
    return out;
}

// see career_calendar.h. The tables that the simulation of multiclass events dirtied up to 0.7.0.
MeetingRepairResult RepairSkippedMeetingRounds(CareerCalendar& cc) {
    MeetingRepairResult res;
    for (auto& e : cc.enrollments) {
        if (e.status != 0) continue;                    // closed season: its result was already settled
        const SeriesDef* d = SeriesById(e.series_id);
        if (!d) continue;
        const std::vector<CalRound> cal = CalendarForSeries(e.series_id);
        for (int i = 0; i < (int)cal.size() && i < e.round_idx; i++) {
            const int r = cal[i].round;
            const std::vector<std::string> cita = MulticlassMeetingAt(e.series_id, r);
            if (cita.size() < 2) continue;
            // You skipped the round: ApplyDnf records your result as DNF at pos == field. A round you
            // ran doesn't have that signature, and even if it did (abandon at the tail) it brings no other classes.
            const RoundOutcome* h = nullptr;
            for (const auto& ro : e.history) if (ro.round == r) h = &ro;
            if (!h || !h->dnfFlag || h->pos != h->field) continue;
            // Your row is the ONLY DNF of the round (the simulation doesn't abandon anyone) and it's at your position. It
            // is identified by that and not by name: your name in the table is the one from the session back then.
            int filas = 0, dnfs = 0;
            std::string tuya;
            for (const auto& kv : e.standings.drivers)
                for (const auto& dr : kv.second) {
                    if (dr.round != r) continue;
                    filas++;
                    if (dr.dnf) { dnfs++; if (dr.pos == h->pos) tuya = kv.first; }
                }
            if (dnfs != 1 || tuya.empty()) continue;
            const int gs = gridSizeFor(e.series_id, r);
            if (gs < 1) continue;
            // Contaminated: more rows than fit (its part + you) or someone from ANOTHER class of the event, i.e.
            // from the cast of another series of the event and not yours.
            const std::vector<GridDriver> propio = CastForRound(*d, r, PlayerForm(e), gs);
            bool contaminada = filas > gs + 1;
            if (!contaminada) {
                std::set<std::string> propios, ajenos;
                for (const auto& g : propio) propios.insert(g.name);
                for (const auto& sid : cita) {
                    const SeriesDef* o = (sid == e.series_id) ? nullptr : SeriesById(sid);
                    if (!o) continue;
                    for (const auto& g : CastForRound(*o, r, PlayerForm(e), gridSizeFor(sid, r)))
                        if (!propios.count(g.name)) ajenos.insert(g.name);
                }
                for (const auto& kv : e.standings.drivers) {
                    if (!ajenos.count(kv.first)) continue;
                    for (const auto& dr : kv.second) if (dr.round == r) contaminada = true;
                }
            }
            if (!contaminada) continue;
            // A cast with the same name twice is a 0.6.8 XML: re-simulating with it would leave the round
            // wrong in another way. It is left for when the XMLs are regenerated.
            { std::set<std::string> vistos; bool roto = false;
              for (const auto& g : propio) if (!vistos.insert(g.name).second) roto = true;
              if (roto) { res.skipped_dirty_xml++; continue; } }

            const std::vector<ClassifiedEntry> sim = SimulateRound(e.series_id, r, PlayerForm(e));
            PointsSystem sys; if (const PointsSystem* ps = PointsById(e.standings.points_system)) sys = *ps;
            std::vector<RoundScore> scores = ScoreRound(sim, sys, RoundPointsScale(cal, i), "");
            scores.erase(std::remove_if(scores.begin(), scores.end(),
                                        [&tuya](const RoundScore& sc) { return sc.name == tuya; }), scores.end());
            // out with the AI rows in that round; yours isn't touched, and whoever is left with no rounds leaves the table
            for (auto it = e.standings.drivers.begin(); it != e.standings.drivers.end(); ) {
                if (it->first != tuya) {
                    auto& v = it->second;
                    v.erase(std::remove_if(v.begin(), v.end(), [r](const DriverRound& dr) { return dr.round == r; }),
                            v.end());
                    if (v.empty()) { it = e.standings.drivers.erase(it); continue; }
                }
                ++it;
            }
            ApplyRound(e.standings, r, scores);
            int despues = 0;
            for (const auto& kv : e.standings.drivers)
                for (const auto& dr : kv.second) if (dr.round == r) despues++;
            res.fixed.push_back(MeetingRoundRepair{ e.series_id, r, filas, despues });
        }
    }
    return res;
}

bool SeasonEnded(const SeasonState& s) {
    return s.round_idx >= (int)CalendarForSeries(s.series_id).size();
}

// Real bug fix (year-2+ divergence): a pending slot's dates must come from the
// calendar projected onto the current year (ProjectSeason) -- the same source BuildMasterCalendar uses (what the player
// actually sees) --, not from the raw base-2025 calendar (CalendarForSeries). At kStartYear both coincide (RealignDoy
// is the identity there); from kStartYear+1 on they diverge by up to ~6 days, and the cursor (which lands on the
// projected date via AdvanceToNextEvent) could read this round as "already past" -> an instant DNF on arrival.
// round_idx is still the positional index into 'raw' (ordered by round, as it always has been); from
// there the round is looked up by number within 'cal' (never by position): ProjectSeason reorders by day-of-year after
// realigning, and that position can differ from the one in 'raw' (audited offline against the real catalog:
// ivory_challenge_2856/aurora_cup_2897 swap a couple of adjacent rounds in some years).
CalSlot PendingSlot(const CareerCalendar& cc, const std::string& seriesId) {
    CalSlot s;
    const SeasonState* e = FindEnrollment(cc, seriesId);
    if (!e) return s;                                  // valid=false
    std::vector<CalRound> raw = CalendarForSeries(seriesId);   // ordered by round: the source of the positional index
    if (e->round_idx < 0 || e->round_idx >= (int)raw.size()) return s;
    int round = raw[e->round_idx].round;
    std::vector<CalRound> cal = ProjectSeason(seriesId, cc.year);   // dates realigned to the current year
    const CalRound* c = nullptr;
    for (const auto& r : cal) if (r.round == round) { c = &r; break; }
    if (!c) return s;
    const SeriesDef* d = SeriesById(seriesId);
    s.series_id = seriesId; s.track_name = c->track_name; s.date = c->date; s.note = c->note;
    s.round = c->round; s.doy = c->doy; s.track_hash = c->track_hash; s.fmt = c->fmt;
    s.laps = c->laps; s.durationMin = c->durationMin; s.hour = c->hour; s.weather = c->weather;
    s.gridSize = c->gridSize;
    s.level = d ? d->level : 1; s.enrolled = true; s.valid = true;
    return s;
}

// A specific round's slot, taken from the calendar (enroll is deferred to settle, so
// you can't rely on the enrollment record to build the launch). Same fields as PendingSlot.
// Real bug fix: uses ProjectSeason (dates realigned to the current year), not CalendarForSeries (the raw
// base-2025 data) -- same source/reason as PendingSlot (see its comment). It already looked up by round (not by
// position), so ProjectSeason's reordering doesn't affect it.
CalSlot SlotForRound(const CareerCalendar& cc, const std::string& seriesId, int round) {
    CalSlot s;
    std::vector<CalRound> cal = ProjectSeason(seriesId, cc.year);
    const CalRound* c = nullptr;
    for (const auto& r : cal) if (r.round == round) { c = &r; break; }
    if (!c) return s;                                  // valid=false: round doesn't exist
    const SeriesDef* d = SeriesById(seriesId);
    s.series_id = seriesId; s.track_name = c->track_name; s.date = c->date; s.note = c->note;
    s.round = c->round; s.doy = c->doy; s.track_hash = c->track_hash; s.fmt = c->fmt;
    s.laps = c->laps; s.durationMin = c->durationMin; s.hour = c->hour; s.weather = c->weather;
    s.gridSize = c->gridSize;
    s.level = d ? d->level : 1; s.enrolled = IsEnrolled(cc, seriesId); s.valid = true;
    // Same substitution/gating as the master calendar -- this is the LAUNCH path (CalUiLaunchSlot).
    ApplyDlcContentToSlot(s, d);
    return s;
}

// Bug fix (01/08): see the contract in career_calendar.h. Takes the calendar that's already loaded (both callers have it
// on hand already) instead of re-requesting it: CalendarForSeries returns by value and this runs per round.
double RoundPointsScale(const std::vector<CalRound>& cal, int roundIdx) {
    if (roundIdx < 0 || roundIdx >= (int)cal.size()) return 1.0;
    if (cal[roundIdx].fmt != 0) return 1.0;                      // endurance format: full points
    for (const CalRound& c : cal) if (c.fmt != 0) return 0.5;    // the championship mixes formats -> a sprint is worth less
    return 1.0;                                                  // an all-sprint championship: these are its own races
}

double RoundPointsScaleFor(const std::string& seriesId, int round) {
    std::vector<CalRound> cal = CalendarForSeries(seriesId);
    for (int i = 0; i < (int)cal.size(); i++) if (cal[i].round == round) return RoundPointsScale(cal, i);
    return 1.0;   // unknown round: never truncate just because it wasn't found
}

// the race happens WITHOUT the player: rivals score points, the player gets 0 (DNF).
// with 'save', the simulated order ALSO settles the AI's ELO (SettleRatingGrid) -- your cohort
// keeps developing in the weeks you don't race (the player isn't in the field, so there's no duel against them).
void ApplyDnf(SeasonState& s, const std::string& playerName, SaveData* save) {
    if (SeasonEnded(s)) return;
    std::vector<CalRound> cal = CalendarForSeries(s.series_id);
    const CalRound& c = cal[s.round_idx];
    double scale = RoundPointsScale(cal, s.round_idx);   // Bug fix (01/08): same criterion as the player's own round
    int gs = gridSizeFor(s.series_id, c.round);
    double form = PlayerForm(s);
    std::vector<ClassifiedEntry> sim = SimulateRound(s.series_id, c.round, form);
    PointsSystem sys; const PointsSystem* ps = PointsById(s.standings.points_system); if (ps) sys = *ps;
    std::vector<RoundScore> scores = ScoreRound(sim, sys, scale, "");
    scores.push_back(RoundScore{ playerName, gs + 1, 0, false, true });   // player absent
    ApplyRound(s.standings, c.round, scores);
    RoundOutcome ro; ro.round = c.round; ro.pos = gs + 1; ro.field = gs + 1; ro.dnfFlag = true;
    s.history.push_back(ro);
    s.round_idx++;
    if (save && !sim.empty()) {
        const SeriesDef* d = SeriesById(s.series_id);
        const int level = d ? d->level : 1;
        const int fieldSize = (int)sim.size();
        std::vector<GridFinisher> fo; fo.reserve(sim.size());
        for (const auto& e : sim) {
            GridFinisher gf;
            gf.id = e.driver_id;          // "" (a PSV with no ids) -> skipped by SettleRatingGrid's own pure guard
            gf.level = level; gf.order = e.pos; gf.field_size = fieldSize;
            gf.iconic = false; gf.dnf = e.dnf; gf.is_player = false;
            fo.push_back(gf);
        }
        SettleRatingGrid(save->driver_ratings, save->driver_races, fo);   // without the difficulty ()
    }
}

// re-applies the privateer's durable pick to the freshly created enrollment (latch not yet set).
void ApplyPrivateerPick(CareerCalendar& cc, const std::string& seriesId, const SaveData& save) {
    SeasonState* e = FindEnrollment(cc, seriesId);
    if (!e || e->livery_ord >= 0) return;              // no entry, or the latch is already set this season
    auto it = save.privateer_pick.find(seriesId);
    if (it == save.privateer_pick.end()) return;
    e->livery_veh = it->second.first;
    e->livery_ord = it->second.second;
}

// DNFs any earlier rounds of THIS series the player skipped, leaving round_idx at 'round'.
void PrepareToRace(CareerCalendar& cc, const std::string& seriesId, int round, const std::string& playerName,
                   SaveData* save) {
    SeasonState* e = FindEnrollment(cc, seriesId);
    if (!e) return;
    std::vector<CalRound> cal = CalendarForSeries(seriesId);
    while (e->round_idx < (int)cal.size() && cal[e->round_idx].round != round) {
        ApplyDnf(*e, playerName, save);
    }
}

std::vector<CalSlot> ConflictsFor(const CareerCalendar& cc, const CalSlot& chosen) {
    std::vector<CalSlot> out;
    for (const auto& e : cc.enrollments) {
        if (e.series_id == chosen.series_id) continue;
        CalSlot p = PendingSlot(cc, e.series_id);
        if (p.valid && SlotsConflict(chosen, p)) out.push_back(p);
    }
    return out;
}

// season-vs-season clash check. Shares the single overlap rule (SlotsConflict) with the
// calendar, so both screens agree -- which is exactly what used to be broken: the market screen
// only asked about the pending round, while the calendar screen asked about all of them.
// Already-resolved rounds of the entered series, and anything behind the cursor, are ignored: warning about
// a clash on a date that's already passed would just be noise.
std::vector<SeasonClash> SeasonClashesFor(const CareerCalendar& cc, const std::string& seriesId) {
    std::vector<SeasonClash> out;
    if (seriesId.empty()) return out;
    // the candidate's calendar, projected onto the current year (with anything already past excluded)
    std::vector<CalSlot> mine;
    for (const auto& c : CalendarForSeriesInYear(seriesId, cc.year)) {
        if (c.doy <= 0 || c.doy < cc.cursor_doy) continue;
        CalSlot s; s.series_id = seriesId; s.doy = c.doy; s.track_hash = c.track_hash;
        s.laps = c.laps; s.durationMin = c.durationMin; s.hour = c.hour; s.valid = true;
        mine.push_back(s);
    }
    if (mine.empty()) return out;
    for (const auto& e : cc.enrollments) {
        if (e.series_id == seriesId) continue;
        const int resolved = (int)e.history.size();     // already-raced rounds of the entry: don't count
        int k = 0; SeasonClash sc; sc.series_id = e.series_id;
        for (const auto& c : CalendarForSeriesInYear(e.series_id, cc.year)) {
            const int idx = k++;
            if (c.doy <= 0 || c.doy < cc.cursor_doy || idx < resolved) continue;
            CalSlot o; o.series_id = e.series_id; o.doy = c.doy; o.track_hash = c.track_hash;
            o.laps = c.laps; o.durationMin = c.durationMin; o.hour = c.hour; o.valid = true;
            for (const auto& m : mine)
                if (SlotsConflict(m, o)) {
                    sc.rounds++;
                    if (!sc.firstDoy || o.doy < sc.firstDoy) sc.firstDoy = o.doy;
                    break;                              // a round of the entry counts once
                }
        }
        if (sc.rounds > 0) out.push_back(sc);
    }
    std::sort(out.begin(), out.end(), [](const SeasonClash& a, const SeasonClash& b) {
        if (a.firstDoy != b.firstDoy) return a.firstDoy < b.firstDoy;
        return a.series_id < b.series_id;
    });
    return out;
}

SeasonOutcome FinalizeSeason(SeasonState& s, CareerProgress& prog, const std::string& playerName,
                             bool noShowSeason) {
    if (s.status == 1) return SeasonOutcome{};         // idempotent: already closed, doesn't pay out twice
    return FinishSeason(s, prog, playerName, noShowSeason);   // sets status=1 and pays the big bonus (gated by the no-show check)
}

// Applies the no-show sting to ONE missed contracted round. If the absent series
// has an active contract, it bumps its missed_rounds, applies RATING_NOSHOW_DING (via DnfKind::NoShow; the
// order/idx are ignored for NoShow) and, on reaching bal::MK_NOSHOW_BREACH_LIMIT, auto-breaks the contract (reusing
// BreakContract's single penalty path -- no second place where a penalty is applied). No-op if the series has no contract.
static void applyNoShowSting(SaveData& save, const std::string& seriesId) {
    Contract* c = ContractForSeriesMut(save, seriesId);
    if (!c) return;                                    // a round with no contract -> no contract sting
    c->missed_rounds++;
    SettleRating(save.prog, /*ratingsInFinishOrder=*/{}, /*playerIdx=*/0, /*aiPct=*/0, DnfKind::NoShow);
    if (c->missed_rounds >= bal::MK_NOSHOW_BREACH_LIMIT)
        BreakContract(save, seriesId);                 // a single REP penalty (BreakContract's own)
}

// Was the 'seriesId' season an entire no-show? The condition is
// read from the per-contract COUNTER (missed_rounds == number of rounds in the series), NOT from playerPos==fieldSize (which
// would unfairly penalize a PARTIALLY raced season: you ran some rounds, missed others). With no
// active contract for the series -> false (a seat with no contract can never be a contracted no-show;
// and after an auto-break the contract is gone anyway -> it correctly stops being treated as a no-show, since it was already penalized).
bool NoShowSeasonForSave(const SaveData& save, const std::string& seriesId) {
    const Contract* c = ContractForSeries(save, seriesId);
    if (!c) return false;
    int rounds = (int)CalendarForSeries(seriesId).size();
    return rounds > 0 && c->missed_rounds >= rounds;
}

// "You didn't show up": DNFs every pending round (any entry) whose date has fallen behind
// the cursor. It does NOT move the cursor. Shared by ResolveAfterRace (after racing) and the advance-time path.
// If 'save' is passed, each missed CONTRACTED round triggers the sting (counter + rating ding +
// auto-break at the limit); without 'save' (pure clock / legacy tests) it just DNFs the simulation as before.
void DnfPassedRounds(CareerCalendar& cc, const std::string& playerName, SaveData* save) {
    for (auto& e : cc.enrollments) {
        CalSlot p = PendingSlot(cc, e.series_id);
        while (p.valid && p.doy < cc.cursor_doy) {
            // Diagnosed on 09/08: a real case closed an 8-round season in JUNE -- the
            // July, August, September and October rounds all came back DNF at once, with the cursor at day 165, and
            // with round_idx at its max the championship was declared over. The log gave no way to see which
            // round was being discarded or on what date -- exactly the data needed to tell whether the bug is in
            // the round's projected date or in the cursor. One log line per discarded round, with both numbers.
            UserLog("cal", "NO-SHOW %s round=%d doy=%d < cursor=%d (year=%d) -> DNF",
                    e.series_id.c_str(), p.round, p.doy, cc.cursor_doy, cc.year);
            ApplyDnf(e, playerName, save);   // with save, the skipped round also moves AI ELO
            if (save) applyNoShowSting(*save, e.series_id);   // per-contract sting for a missed contracted round
            p = PendingSlot(cc, e.series_id);
        }
    }
}

// Truthful preview of a time-skip. It does NOT reimplement the no-show logic: it simulates on
// a COPY ('sim'), moving the cursor EXACTLY like AdvanceDays (career_contract.cpp), and calls the SAME
// DnfPassedRounds (save=nullptr: marks DNF without the sting) -- so the preview and the real penalty can never diverge.
JumpPreviewResult JumpPreview(const CareerCalendar& cc, const std::string& playerName, int targetAbsDay) {
    JumpPreviewResult out;
    out.target_abs_day = targetAbsDay;
    dateutil::YearDoyFromAbsDay(targetAbsDay, out.target_year, out.target_doy);

    CareerCalendar sim = cc;   // a copy: 'cc' is NEVER mutated (received by const-ref)
    if (out.target_year == cc.year) {
        sim.cursor_doy = out.target_doy;             // same year: the same idiom as AdvanceDays (whatever span fits)
    } else {
        // crosses the year boundary: the rollover (RolloverYear) empties the entries -> everything pending this year
        // is lost all at once. It's enough to move the cursor to year-end (the same span AdvanceDays uses before the
        // rollover); whatever happens in later years is no longer a "no-show" of an entry that no longer exists.
        out.crosses_year = true;
        sim.cursor_doy = dateutil::DaysInYear(sim.year);
    }

    DnfPassedRounds(sim, playerName, nullptr);   // same criterion as the real penalty; no 'save' -> no sting

    // History diff: DnfPassedRounds only APPENDS to SeasonState::history (never inserts/deletes
    // entries), so 'sim.enrollments' keeps the same size/order as 'cc.enrollments', and any
    // NEW entry in sim.history (index >= cc.history.size()) is a round that just got DNFed by the skip.
    for (size_t i = 0; i < sim.enrollments.size() && i < cc.enrollments.size(); ++i) {
        const SeasonState& before = cc.enrollments[i];
        const SeasonState& after  = sim.enrollments[i];
        if (after.series_id != before.series_id) continue;   // defensive safeguard (shouldn't ever diverge)
        // Real bug fix: PendingSlot/DnfPassedRounds now read PROJECTED dates (see their comment);
        // this missed round is ALWAYS in the CURRENT year (cc.year, see the comment further below), so it aligns
        // to the same source, so the warning's track_name/date/doy match what actually happened.
        std::vector<CalRound> cal = ProjectSeason(after.series_id, cc.year);   // same source as PendingSlot/DnfPassedRounds
        for (size_t h = before.history.size(); h < after.history.size(); ++h) {
            const int round = after.history[h].round;
            JumpNoShow ns;
            ns.series_id = after.series_id;
            ns.round = round;
            if (const SeriesDef* sd = SeriesById(after.series_id)) {
                ns.class_name = sd->class_name;
                ns.series_display = sd->display;
            }
            for (const auto& r : cal) {
                if (r.round == round) { ns.track_name = r.track_name; ns.date = r.date; ns.doy = r.doy; break; }
            }
            ns.year = cc.year;   // every round missed by a time-skip belongs to the CURRENT year (the only one that can DNF)
            ns.abs_day = dateutil::AbsDayFromYearDoy(ns.year, ns.doy);
            out.no_shows.push_back(ns);
        }
    }
    std::sort(out.no_shows.begin(), out.no_shows.end(),
              [](const JumpNoShow& a, const JumpNoShow& b) { return a.abs_day < b.abs_day; });
    return out;
}

void ResolveAfterRace(CareerCalendar& cc, CareerProgress& prog, const CalSlot& chosen, const std::string& playerName,
                      SaveData* save) {
    // a) DNFs any pending ENTERED rounds that clash with the weekend just raced (another series).
    //    An overlap against a contracted seat IS a contracted no-show -> sting.
    for (auto& e : cc.enrollments) {
        if (e.series_id == chosen.series_id) continue;
        CalSlot p = PendingSlot(cc, e.series_id);
        if (p.valid && SlotsConflict(chosen, p)) {
            ApplyDnf(e, playerName, save);   // the overlapping weekend also moves AI ELO
            if (save) applyNoShowSting(*save, e.series_id);
        }
    }
    // Memories: running a historical-era series is "free time" -- it doesn't move the clock, doesn't arm the latch, doesn't DNF anything, doesn't finalize anything.
    { const SeriesDef* _sd = SeriesById(chosen.series_id);
      if (_sd && IsEraMemorySeries(*_sd)) return; }
    // b) advance the cursor to the day just raced
    // racing ALSO moves the clock -> the weeks crossed while jumping from weekend to weekend trigger the
    // same silly-season draws as AdvanceWeek (without this, a player who never presses "advance week" would
    // never see a single seat open up all year).
    if (chosen.doy > cc.cursor_doy) {
        int weeksCrossed = (chosen.doy - cc.cursor_doy) / 7;
        cc.cursor_doy = chosen.doy;
        if (save && weeksCrossed > 0)
            SillySeasonFreeSeats(*save, cc.year, cc.cursor_doy, weeksCrossed, prog.frontier_phase + 1);
    }
    // b.5) corollary: a "you're physically at this circuit TODAY" latch. After racing, any other race
    // that same day at a different circuit becomes unreachable (one race per day; see travel_locked in BuildMasterCalendar).
    // Races at the SAME circuit at a different time stay available (a same-venue doubleheader). Only armed for a slot that was actually raced.
    if (chosen.valid && chosen.doy > 0) {
        cc.last_race_doy     = chosen.doy;
        cc.last_race_circuit = chosen.track_hash;
        cc.last_race_hour    = chosen.hour;                  // duration: stores the window just raced (for the same-venue doubleheader check)
        cc.last_race_minutes = EstimatedRaceMinutes(chosen);
    }
    // c) DNFs every pending round (any series) whose date has fallen behind (shared helper); 'save' enables the sting
    DnfPassedRounds(cc, playerName, save);
    // d) closes out (big bonus) every season whose calendar is now complete. If the series
    //    is under contract and was missed ENTIRELY (missed_rounds == number of rounds), the season is a no-show -> lifts the
    //    +1 REP floor (keyed off the missed-round counter, NOT playerPos==fieldSize).
    for (auto& e : cc.enrollments)
        if (SeasonEnded(e)) FinalizeSeason(e, prog, playerName, save && NoShowSeasonForSave(*save, e.series_id));
}

// The player's final position in a season, derived from the final standings table (independent of status).
// FinalizeSeason only pays out/closes; for the record book we need the position whether it was already closed
// or we're closing it right now. Number of rounds = the series' calendar size.
static int playerFinalPos(const SeasonState& e, const std::string& playerName) {
    std::vector<StandingRow> table = FinalTable(e.standings);
    int pos = (int)table.size();                            // default: last place (= field size) if not found
    for (int i = 0; i < (int)table.size(); i++)
        if (table[i].name == playerName) { pos = i + 1; break; }
    return pos;
}

// Build the RENEWAL offer for a contract reaching end-of-term at rollover. The terms
// shift by perfRatio + relationship (a 0.5 stub for now -- a real per-team store is planned), as a GRADIENT (not
// binary): over-performed -> better salary/role/duration + possible category jump; met -> neutral;
// under-performed -> worse terms or decline. Returns false (no renewal generated) on a clear decline.
// Renewal fix: perfRatio arrives already RESOLVED from the caller (RolloverYear): the objectives ratio if
// the contract carries objectives, otherwise the one derived from the championship's FINAL TABLE (the real production case).
// The carrier is a market::OfferDef with archetype = ARCH_RENEWAL, id "of_"+series_id (so the Accept/Negotiate
// path resolves its series exactly like any other offer); negotiationLevel 2 is implied by ARCH_RENEWAL.
static bool BuildRenewalOffer(const Contract& c, int newYear, market::OfferDef& out, double perfRatio) {
    if (perfRatio < 0.0) perfRatio = 0.0; if (perfRatio > 1.0) perfRatio = 1.0;
    const double rel = 0.5;   // Relationship stub for now (a real per-team relationship store is planned)
    // perf in 0..1 blends season performance with relationship; the gradient pivots around 0.5 (= "met").
    double perf = 0.5 * perfRatio + 0.5 * rel;

    // a clear under-performance (well below "met") declines the renewal -- no offer is generated.
    if (perf < 0.35) return false;

    out = market::OfferDef{};
    out.id          = "of_" + c.series_id;
    out.direct      = true;                       // a renewal is the team coming back to you
    out.team        = c.team_id;
    out.from        = c.team_id;
    out.clsId       = c.series_id;
    out.scope       = "season";
    out.contractKind= c.kind;                     // re-sign the SAME economics kind
    out.archetype   = ARCH_RENEWAL;               // The renewal archetype (negLevel 2)
    out.commitment  = 1;

    // term GRADIENT off perf, anchored on the current per_race. shift in roughly -25%..+35% across perf 0.35..1.0.
    // In-game fix: a contract with NO salary (customer/pay: salary_dir=0, per_race=0) now renews with NO salary --
    // it used to fall back to an anchor of 1, and the renewal carried a phantom per_race=1 credit (display should match what's actually paid).
    double shift = (perf - 0.5) * 0.7;            // met (0.5) -> 0 (neutral); over -> positive; under -> negative
    if (c.per_race > 0) {
        out.perRace   = (long)(c.per_race * (1.0 + shift) + 0.5);
        if (out.perRace < 1) out.perRace = 1;
        out.signBonus = (long)(out.perRace * career::bal::MK_K_BONUS + 0.5);
    }

    // over-performed -> better DURATION (2 seasons) + possible CATEGORY JUMP (carry the conditional promote_to).
    if (perf >= 0.75) {
        out.commitment = 2;                       // better duration
        if (!c.promote_to.empty()) out.promote_to = c.promote_to;   // possible category jump (conditional clause)
    }
    // Classify the shifted terms for the rollover summary line (transient -- not persisted; read
    // in-session by RolloverUiPreview). perf>=0.75 -> better; ~met (>=0.5) -> same; under (<0.5, but >=0.35 so an
    // offer exists) -> worse. The neutral pivot is perf 0.5 (= "met"), matching the shift gradient above.
    out.renewQuality = (perf >= 0.75) ? 0 : (perf >= 0.5 ? 1 : 2);
    return true;
}

// Performance ratio (0..1) for a contract's season -- the SAME formula shared by
// the rollover (final judgment) and the silly season (early offer based on your CURRENT position). Precedence:
// (1) championship table relative to the contract's OBJECTIVE (obj_target_pos>0): pivot 0.65 = "just
//     met it", ceiling 1.0, floor 0.15; (2) raw table (no objective): position mapped to 0.15..1.0;
// (3) the legacy objectives ratio (the binary pass/fail test) only when there's no table; (4) no data -> 0.5 "met".
static double ContractSeasonRatio(const CareerCalendar& cc, const Contract& c, const std::string& playerName,
                                  int* outPos, int* outField, bool* outFromTable) {
    double ratio = 0.5; bool fromTable = false; int pos = 0, field = 0;
    if (const SeasonState* se = FindEnrollment(cc, c.series_id)) {
        field = (int)FinalTable(se->standings).size();
        if (field > 1) {
            pos = playerFinalPos(*se, playerName);
            ratio = 0.15 + 0.85 * (double)(field - pos) / (double)(field - 1);
            fromTable = true;
        }
    }
    if (fromTable && c.obj_target_pos > 0) {
        int K = c.obj_target_pos > field ? field : c.obj_target_pos;
        if (pos <= K) ratio = 0.65 + 0.35 * (double)(K - pos) / (double)(K > 1 ? K - 1 : 1);
        else          ratio = 0.65 - 0.50 * (double)(pos - K) / (double)(field - K);
    }
    if (!fromTable && c.obj_total > 0)
        ratio = (double)c.obj_done / (double)c.obj_total;
    if (ratio < 0.0) ratio = 0.0; if (ratio > 1.0) ratio = 1.0;
    if (outPos) *outPos = pos; if (outField) *outField = field; if (outFromTable) *outFromTable = fromTable;
    return ratio;
}

// See career_calendar.h. A single pass over active contracts using the SAME formula as the
// rollover. It doesn't decide anything: it only measures, and the decision week is what judges.
std::map<std::string, double> PlayerSeasonRatios(const CareerCalendar& cc, const SaveData& save,
                                                 const std::string& playerName) {
    std::map<std::string, double> out;
    for (const auto& c : save.contracts) {
        if (!c.active || c.series_id.empty()) continue;
        out[c.series_id] = ContractSeasonRatio(cc, c, playerName, nullptr, nullptr, nullptr);
    }
    return out;
}

// Early renewal. With <= EARLY_RENEW_ROUNDS_LEFT
// rounds left in a contract's final season, the team opens renewal talks: a provisional offer (BuildRenewal-
// Offer) based on your CURRENT position vs. the objective, plus an actionable email from the team principal. Signing right away locks in the
// seat (AcceptRenewal extends the contract in place); ignoring it -> the rollover cleans it up and issues the
// definitive offer with your final result (better or worse) plus rival bids if there was a podium. If the team would decline
// (a disastrous season), no early offer arrives at all -- silence is the signal. Idempotent by (series, year):
// dedup by offer id in pending_renewals + mailExists. Called by RunWorldTick (on every calendar advance).
void EarlyRenewalTick(CareerCalendar& cc, SaveData& save, const std::string& playerName) {
    for (const auto& c : save.contracts) {
        if (!c.active || c.seasons_left != 1) continue;   // only the FINAL season of a contract opens silly season
        const SeasonState* se = FindEnrollment(cc, c.series_id);
        if (!se) continue;
        int total = (int)CalendarForSeries(c.series_id).size();
        if (total <= 0) continue;
        // In-game fix: progress by round_idx (the engine's canonical pointer: settled rounds, advanced by
        // SettleRound), not by standings.round (the number of the last round applied -- which only matches if the
        // calendar's numbering is a strict 1..N).
        int left = total - se->round_idx;
        if (left < 1 || left > bal::EARLY_RENEW_ROUNDS_LEFT) continue;
        const std::string offId = "of_" + c.series_id + "#early";
        bool have = false;
        for (const auto& pr : save.pending_renewals) if (pr.id == offId) { have = true; break; }
        if (have) continue;
        double ratio = ContractSeasonRatio(cc, c, playerName, nullptr, nullptr, nullptr);
        market::OfferDef ren;
        if (!BuildRenewalOffer(c, cc.year + 1, ren, ratio)) {
            UserLog("renew", "silly season %s: %d round(s) from the end, ratio=%.2f -> the team does NOT move early",
                    c.series_id.c_str(), left, ratio);
            continue;   // the team is unsure -> no early offer (silence is the signal)
        }
        UserLog("renew", "silly season %s: %d round(s) from the end, ratio=%.2f -> early offer %s (perRace=%ld commit=%d)",
                c.series_id.c_str(), left, ratio, offId.c_str(), ren.perRace, ren.commitment);
        ren.id = offId;                                    // '#early': seriesIdOfOffer/MailActionsFor split on '#'
        save.pending_renewals.push_back(ren);
        // actionable email (family 0, same lane as the rollover renewal). Suffix = CURRENT YEAR ->
        // CloseStaleRenewalMails(cc.year) closes it at the rollover that issues the definitive offer.
        std::string mid = "renew_" + offId + "_" + std::to_string(cc.year);
        bool exists = false;
        for (const auto& e : save.emails) if (e.id == mid) { exists = true; break; }
        if (exists) continue;
        GeneratedEmail ge;
        ge.id = mid; ge.tag = 1; ge.day = cc.cursor_doy; ge.year = cc.year; ge.family = 0;
        ge.subtype = ContractTypeIndex(ren.contractKind, ren.archetype);
        ge.payload_key = offId;
        MailParams p;
        std::string pf = MailFirstName(save.player.name);
        if (!pf.empty()) p.push_back({"player", pf});
        const SeriesDef* rsd = SeriesById(c.series_id);
        p.push_back({"series", rsd ? rsd->display : c.series_id});
        p.push_back({"team", c.team_id});
        p.push_back({"negotiable", "0"});
        ge.params = MailParamsEncode(p);
        ge.from_name = c.team_id; ge.from_role_key = "inbox_role_teamprincipal";
        save.emails.push_back(ge);
    }
}

// Year rollover -- one function with a list of hooks. The order is critical:
//  1) SNAPSHOT the record book (completed_seasons) BEFORE touching anything: each entry's season gets closed
//     (FinalizeSeason, idempotent: pays the big bonus only if it wasn't already closed) and is archived as
//     {series, current year, final position, number of rounds}. This has to happen here because step 6 empties cc.enrollments.
//  2) Contract: if there's an active contract, seasons_left--. >0 -> the deal continues (we re-enter its series in
//     step 4, now with the new year). <=0 -> it expires and the seat is freed (contract = Contract{}).
//  3) (Retired 02/07) rival_form drift (scheduled skill growth) no longer exists: a driver's
//     skill is innate (base_skill), and their reputation is carried by ELO from results. Age/retirement/promotion still apply.
//  4) Market: declined_offers/expired_offers.clear() (new season -> fresh offers). courting and contract
//     are PRESERVED ("they remember you" + the current deal).
//  5) Intra-year gating: completed_races.clear() (this is year-scoped gating, not the record book). recent_form is NOT touched
//     (cross-season form), class_stats (career-long record), or unlocked_gates (super-licenses).
//  6) Year advance: year++, cursor_doy=0, enrollments.clear(). AFTER this we re-enter any
//     still-active contract (step 2): the re-entry is for the NEW year, with the entry fee waived (the team covers it).
// See career_calendar.h: for every renewal still pending (from the year that's ending), it looks up
// its mail "renew_"+ren.id+"_"+year (year = the toYear it was issued with, one rollover earlier) and if it's still
// unanswered it closes it out (answered=1, choice=0 -> "you let it lapse", never a phantom signature). ALWAYS call it
// right before save.pending_renewals.clear() -- once it's emptied there's nothing left to reconstruct the id from.
void CloseStaleRenewalMails(SaveData& save, int year) {
    for (const auto& ren : save.pending_renewals) {
        std::string mid = "renew_" + ren.id + "_" + std::to_string(year);
        for (auto& e : save.emails) {
            if (e.id == mid && !e.answered) { e.answered = 1; e.choice = 0; break; }
        }
    }
}

// An ignored interview expires at rollover -- it loses its effect (choice=0,
// ApplyPressAnswer never runs) AND unblocks the press (PressInterviewPending goes back to false). Unlike
// CloseStaleRenewalMails, no id needs to be reconstructed: family==6 && subtype<=3 && !answered identifies
// the interview on its own, so it just walks save.emails directly.
void CloseStalePressMails(SaveData& save) {
    for (auto& e : save.emails) {
        if (e.family == 6 && e.subtype <= 3 && !e.answered) { e.answered = 1; e.choice = 0; }
    }
}

// The rollover's INTEL block + nemesis handling. See the header docs. Fully deterministic, no RNG.
void IntelRolloverMails(SaveData& save, const RolloverReport& rep,
                        const std::map<std::string, std::string>& seatsBefore, int fromYear, int cursorDoy) {
    const std::string year = std::to_string(fromYear);
    const std::string pf   = MailFirstName(save.player.name);   // {player} for the INTEL/nemesis mails ("" -> omitted)
    auto mailExists = [&](const std::string& id) {
        for (const auto& e : save.emails) if (e.id == id) return true;
        return false;
    };

    // orbit: series level within [frontier_phase-1, frontier_phase+1] OR the series of an active contract ----
    const int fp = save.prog.frontier_phase;
    std::set<std::string> contractSeries;
    for (const auto& c : save.contracts) if (c.active) contractSeries.insert(c.series_id);
    auto isOrbitSeries = [&](const std::string& sid) -> bool {
        if (contractSeries.count(sid)) return true;
        const SeriesDef* sd = SeriesById(sid);
        return sd && sd->level >= fp - 1 && sd->level <= fp + 1;
    };
    auto isOrbitTier = [&](int tier) -> bool { return tier >= fp - 1 && tier <= fp + 1; };
    auto seriesDisplay = [&](const std::string& sid) -> std::string {
        const SeriesDef* sd = SeriesById(sid);
        return (sd && !sd->display.empty()) ? sd->display : sid;   // fallback: raw id
    };
    auto seriesOfKey = [](const std::string& key) -> std::string {   // single read of the format
        return SeriesOfSeatKey(key);
    };

    // retirements/promotions from THIS rollover: they aren't "signings" (they have their own path) -> excluded from the seat diff.
    std::set<std::string> retiredIds, promotedIds;
    for (const auto& r  : rep.retirements) retiredIds.insert(r.driver_id);
    for (const auto& pr : rep.promotions)  promotedIds.insert(pr.driver_id);

    // Real signings: diff seatsBefore vs save.seat_occupant (both already in their "after" state) ----
    // id -> seat, skipping the open-seat sentinel (kSeatOpen) and empty values.
    std::map<std::string, std::string> beforeKeyOf, afterKeyOf;
    for (const auto& kv : seatsBefore)
        if (!kv.second.empty() && kv.second != kSeatOpen) beforeKeyOf[kv.second] = kv.first;
    for (const auto& kv : save.seat_occupant)
        if (!kv.second.empty() && kv.second != kSeatOpen) afterKeyOf[kv.second] = kv.first;

    std::vector<std::string> moveHeadlines;                  // pre-formatted headlines "<name>: <old> -> <new>"
    for (const auto& kv : afterKeyOf) {
        if (moveHeadlines.size() >= 3) break;                // max 3 signing candidates
        const std::string& id       = kv.first;
        const std::string& afterKey = kv.second;
        auto bit = beforeKeyOf.find(id);
        if (bit == beforeKeyOf.end()) continue;              // no known "before" seat -> nothing to show as the origin
        if (bit->second == afterKey) continue;               // same seat -> didn't move
        if (retiredIds.count(id) || promotedIds.count(id)) continue;   // retirement/promotion are NOT signings
        std::string oldSeries = seriesOfKey(bit->second);
        std::string newSeries = seriesOfKey(afterKey);
        if (oldSeries == newSeries) continue;                // a seat change within the SAME series -> a degenerate headline
        if (seriesDisplay(oldSeries) == seriesDisplay(newSeries)) continue;   // different ids, same display text -> a degenerate "X -> X"
        if (!isOrbitSeries(oldSeries) && !isOrbitSeries(newSeries)) continue;   // orbit by origin OR destination series
        std::string name = DriverDisplayName(id);   // Generated drivers have names too
        // The arrow here is FUNCTIONAL NOTATION for a transfer (an origin->destination marker), exempt from the
        // prose anti-tell rule: it's embedded in {m1}/{m2} and rendered inside BOTH bodies (EN and ES), so it
        // has to stay language-neutral. The guard test (test_strings_parity) only sweeps the TR table, not these params.
        moveHeadlines.push_back(name + ": " + seriesDisplay(oldSeries) + " \xE2\x86\x92 " + seriesDisplay(newSeries));
    }

    // pool of summary headlines (by priority): signings > promotions of rivals you've beaten; retirements only get
    // their own dedicated email with history (a bare name in the summary would frame it as a "signing" and lie) ----
    std::vector<std::string> pool = moveHeadlines;
    for (const auto& pr : rep.promotions) {
        auto hit = save.h2h.find(pr.driver_id);
        int w = (hit != save.h2h.end()) ? hit->second.w : 0;
        int l = (hit != save.h2h.end()) ? hit->second.l : 0;
        if (w > l) continue;                                 // a favorable head-to-head balance -> it's an intel_promo email, not a summary headline
        if (!isOrbitTier(pr.to_tier)) continue;              // promotion: orbit by destination tier
        std::string name = pr.name.empty() ? pr.driver_id : pr.name;
        // -> = functional notation for a transfer (Tier N->M), exempt from the anti-tell rule (see the note in moveHeadlines).
        pool.push_back(name + ": Tier " + std::to_string(pr.from_tier) + " \xE2\x86\x92 Tier " + std::to_string(pr.to_tier));
    }
    std::vector<std::string> headlines;                      // the summary takes at most 2
    for (size_t i = 0; i < pool.size() && headlines.size() < 2; ++i) headlines.push_back(pool[i]);

    // INTEL emission with a priority cap: h2h-retirement > summary > h2h-promotion. "Priority 1: whatever doesn't fit gets dropped". ----
    int intelEmitted = 0;
    const int cap = bal::MK_MAIL_INTEL_PER_ROLLOVER;

    // priority 1: retirements with enough h2h history -> one intel_retire email per retiree.
    for (const auto& r : rep.retirements) {
        if (intelEmitted >= cap) break;                      // priority 1: whatever doesn't fit gets dropped
        auto hit = save.h2h.find(r.driver_id);
        int w = (hit != save.h2h.end()) ? hit->second.w : 0;
        int l = (hit != save.h2h.end()) ? hit->second.l : 0;
        if (w + l < bal::MK_INTEL_RETIRE_MIN_H2H) continue;
        std::string mid = "intel_retire_" + r.driver_id + "_" + year;
        if (mailExists(mid)) continue;
        GeneratedEmail ge;
        ge.id = mid; ge.tag = 1; ge.day = cursorDoy; ge.year = fromYear; ge.family = 4; ge.subtype = 0;
        ge.payload_key = "intel_retire";
        int vc = MailVariantCount("intel_retire");
        ge.variant = vc > 0 ? (int)(MailHash(mid) % (unsigned)vc) : 0;   // both variants require the same params -> free choice by hash
        MailParams p;
        if (!pf.empty()) p.push_back({"player", pf});
        p.push_back({"driver", r.name.empty() ? r.driver_id : r.name});
        p.push_back({"w", std::to_string(w)});
        p.push_back({"l", std::to_string(l)});
        ge.params = MailParamsEncode(p);
        ge.from_name = MailManagerName(); ge.from_role_key = "inbox_role_manager";
        save.emails.push_back(ge);
        intelEmitted++;
    }

    // priority 2: market summary (1 email). Variant contract: >=2 headlines -> hash%2 (v0/v1, both
    // embed {m1}+{m2}); ==1 -> force v2 (only {m1}); 0 -> don't emit.
    if (intelEmitted < cap && !headlines.empty()) {
        std::string mid = "intel_market_" + year;
        if (!mailExists(mid)) {
            GeneratedEmail ge;
            ge.id = mid; ge.tag = 1; ge.day = cursorDoy; ge.year = fromYear; ge.family = 4; ge.subtype = 1;
            ge.payload_key = "intel_market";
            MailParams p;
            if (!pf.empty()) p.push_back({"player", pf});
            p.push_back({"m1", headlines[0]});
            if (headlines.size() >= 2) {
                p.push_back({"m2", headlines[1]});
                ge.variant = (int)(MailHash(mid) % 2u);      // >=2 headlines -> v0/v1 (NEVER v2, which drops {m2})
            } else {
                ge.variant = 2;                              // ==1 headline -> v2 (the only one that only needs {m1})
            }
            ge.params = MailParamsEncode(p);
            ge.from_name = MailManagerName(); ge.from_role_key = "inbox_role_manager";
            save.emails.push_back(ge);
            intelEmitted++;
        }
    }

    // priority 3: promotions of drivers WITH h2h history (ones you beat who are now moving up a tier).
    for (const auto& pr : rep.promotions) {
        if (intelEmitted >= cap) break;                      // priority 1: whatever doesn't fit gets dropped
        auto hit = save.h2h.find(pr.driver_id);
        int w = (hit != save.h2h.end()) ? hit->second.w : 0;
        int l = (hit != save.h2h.end()) ? hit->second.l : 0;
        if (w <= l) continue;   // "you beat him": requires a favorable head-to-head balance (the template never lies)
        std::string mid = "intel_promo_" + pr.driver_id + "_" + year;
        if (mailExists(mid)) continue;
        GeneratedEmail ge;
        ge.id = mid; ge.tag = 1; ge.day = cursorDoy; ge.year = fromYear; ge.family = 4; ge.subtype = 2;
        ge.payload_key = "intel_promo";
        int vc = MailVariantCount("intel_promo");
        ge.variant = vc > 0 ? (int)(MailHash(mid) % (unsigned)vc) : 0;
        MailParams p;
        if (!pf.empty()) p.push_back({"player", pf});
        p.push_back({"driver", pr.name.empty() ? pr.driver_id : pr.name});
        p.push_back({"tier", "Tier " + std::to_string(pr.to_tier)});   // tier display the same way the app shows it (i18n tierN)
        ge.params = MailParamsEncode(p);
        ge.from_name = MailManagerName(); ge.from_role_key = "inbox_role_manager";
        save.emails.push_back(ge);
        intelEmitted++;
    }

    // the NEMESIS email (rival family) -- does NOT count against the INTEL cap. Gate: a nemesis exists and their name
    // resolves (otherwise it's skipped). h2h w>l -> respect (the rival signs with their own name {driver}); otherwise -> needling. ----
    if (!save.nemesis_id.empty()) {
        const PoolDriver* pd = DriverById(save.nemesis_id);
        std::string nemName = pd ? (pd->name.empty() ? pd->id : pd->name) : std::string();
        if (!nemName.empty()) {
            std::string mid = "rival_nemesis_" + save.nemesis_id + "_" + year;
            if (!mailExists(mid)) {
                auto hit = save.h2h.find(save.nemesis_id);
                int w = (hit != save.h2h.end()) ? hit->second.w : 0;
                int l = (hit != save.h2h.end()) ? hit->second.l : 0;
                bool respect = w > l;
                std::string key = respect ? "rival_nemesis_respect" : "rival_nemesis_pique";
                GeneratedEmail ge;
                ge.id = mid; ge.tag = 2; ge.day = cursorDoy; ge.year = fromYear; ge.family = 5; ge.subtype = 1;
                ge.payload_key = key;
                int vc = MailVariantCount(key);
                ge.variant = vc > 0 ? (int)(MailHash(mid) % (unsigned)vc) : 0;
                MailParams p;
                if (!pf.empty()) p.push_back({"player", pf});
                if (respect) p.push_back({"driver", nemName});   // respect: the rival signs "-- {driver}" (their own name)
                p.push_back({"w", std::to_string(w)});
                p.push_back({"l", std::to_string(l)});
                ge.params = MailParamsEncode(p);
                ge.from_name = nemName; ge.from_role_key = "inbox_role_rival";
                save.emails.push_back(ge);
            }
        }
    }
}

// Emits a press interview at rollover (title won / fired). Respects the "one
// pending at a time" rule (PressInterviewPending: family 6, subtype<=3, !answered) -- if one already exists, this one is dropped.
// The headline's rival: save.nemesis_id or the highest-level beef (DriverById; nullptr -> no rival). With a rival ->
// params rival+rival_id and variant hash%2 (v0/v1); with no rival -> v0 rival-free (the emitter forces it because v1 weaves in
// {rival}). rival_team is OMITTED: there's no cheap seat->team resolver in career_seats.h (ApplyPressAnswer
// tolerates that). Builds the GeneratedEmail directly (same idiom as job_lost/season_close); the sender is a journalist
// (MailPressName / inbox_role_press, tag=2). Content-derived id -> dedups + can re-fire the following season.
static void emitRolloverPress(SaveData& save, const std::string& id, int subtype, const char* payloadKey,
                              MailParams params, int year, int day) {
    for (const auto& e : save.emails) if (e.id == id) return;      // dedup content-id
    if (PressInterviewPending(save)) return;                       // one interview at a time
    std::string rivalId = save.nemesis_id;
    if (rivalId.empty()) {                                         // no nemesis -> the highest-level beef
        int best = 0;
        for (const auto& kv : save.beef) if (kv.second.level > best) { best = kv.second.level; rivalId = kv.first; }
    }
    int variant = 0;                                               // v0 rival-free by default
    if (!rivalId.empty()) {
        const PoolDriver* pd = DriverById(rivalId);
        if (pd) {
            params.push_back({"rival", pd->name.empty() ? pd->id : pd->name});
            params.push_back({"rival_id", rivalId});
            int vc = MailVariantCount(payloadKey);
            variant = vc > 0 ? (int)(MailHash(id) % 2u) : 0;       // a rival is present -> hash%2 (v0/v1)
        }
    }
    GeneratedEmail ge;
    ge.id = id; ge.tag = 2; ge.day = day; ge.year = year; ge.family = 6; ge.subtype = subtype;
    ge.payload_key = payloadKey; ge.variant = variant;
    ge.params = MailParamsEncode(params);
    ge.from_name = MailPressName(); ge.from_role_key = "inbox_role_press";
    save.emails.push_back(ge);
}

RolloverReport RolloverYearReport(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                                  const std::string& playerName) {
    RolloverReport rep;
    rep.from_year = cc.year;

    // The player's seats, snapshotted FIRST. Two hundred lines further down, the
    // renewals block will already have removed the ones that ended from save.contracts, so asking
    // after that cascade runs would return an EMPTY set -- and then attrition, expiry, or a
    // rookie would sweep away the player's own seat while it's still supposed to be shielded by a protection clause.
    // Measured on 04/08, and it took two tries to place it high enough.
    const std::set<std::string> asientosDelJugador = PlayerSeatKeys(save);

    // 18/09 — THE PHOTO FROM BEFORE, to count the SIGNINGS. «Market moves» always came out empty: nobody counted
    // who changed team. Here it is photographed who sits where (series + team) and again at the end of
    // winter; those who change place without retiring or being promoted are the signings. A person with
    // several seats counts as a signing if they win a new seat elsewhere.
    // 23/09 (Tester Hub ): ONLY the live market. With the era and the access tests included, «signings»
    // came out toward the IVORY_CUP_12573 Prologue (a lone race, not a team) or toward an era series (the era
    // has no market: its grid is composed, not signed).
    auto fotoAsientos = [&save]() {
        std::map<std::string, std::set<std::pair<std::string, std::string>>> donde;   // id -> {(series, team)}
        for (const auto& sd : AllSeries()) {
            if (IsEraMemorySeries(sd) || !IsModernSeries(sd.id) || !sd.unlocks.empty()) continue;
            for (const auto& st : SeatsForClass(sd.id, save)) {
                if (st.open || st.occupant_id.empty() || st.team_label.empty()) continue;
                donde[st.occupant_id].insert({ sd.id, st.team_label });
            }
        }
        return donde;
    };
    const auto antesDelInvierno = fotoAsientos();

    // ~~~29/08 — THE GRID IS REMEMBERED. Until here the occupancy was DERIVED from scratch on each query:
    //  `SeatsForClass` distributes the drivers over the class's seats, and only what the save declared
    //  explicitly in `seat_occupant` was respected. With that, anything that moved the distribution --
    //  a retirement, a promotion, a new datum -- displaced from their seat people nobody had signed:
    //  measured ~110 seats per winter changing occupant without the market declaring it. The photo is fixed
    //  BEFORE touching anything, and from there on each seat change has an author. Idempotent:
    //  it only writes the seats the save doesn't yet declare, so a half-done save keeps its own
    //  and the second pass fixes nothing. 1599 seats in the live-market world.
    MaterializeSeatOccupancy(save, asientosDelJugador);

    // A snapshot of the occupancy map BEFORE the rollover moves any seats (retirements/promotions via
    // ApplyRolloverSeatChanges). The INTEL block further down diffs it against the "after" state to work out
    // the real signings (drivers who changed seats and are neither a retirement nor a promotion). Copied by value.
    std::map<std::string, std::string> seatsBefore = save.seat_occupant;

    // — THE WORLD RUNS TOO, and runs HERE: the FIRST thing in the rollover, before the
    // seasons are closed and before the market moves a single seat. It is the phase 7 rule —RUN BEFORE
    // ADVANCING—, and here it isn't a preference: the championship being resolved is that of the year that is LEAVING, so
    // it has to be contested by the people who ran it. One line below the market has already signed,
    // and then the championship would be won by someone who just arrived.
    //
    // Without this, the world's ELO only moved on the grids where the player had raced: the other
    // ninety series contested nothing, so no team knew what had happened last year — which is
    // literally the input that (the band) and (what each team buys) hang from.
    //
    // The player's series are SKIPPED: their races are settled by the overlay one by one (SettleRatingGrid), and
    // resolving them again here would charge the ELO twice to the whole field.
    {
        std::set<std::string> lasDelJugador;
        for (const auto& e : cc.enrollments) lasDelJugador.insert(e.series_id);
        const WorldSeasonReport ws = RunWorldSeason(save, cc.year, (std::uint64_t)save.career_seed, lasDelJugador);
        rep.world_series = ws.series;
        rep.world_races  = ws.races;
        for (const auto& s : ws.per_series)
            if (!s.champion_id.empty()) rep.world_champions.push_back({ s.series_id, s.champion_id });

        // — AND WITH THE RESULTS JUST MADE, EACH ONE'S ARC ADVANCES. It goes here and not further down
        // because the circumstance that bends the curve is THOSE results: if it were done after the market,
        // people would be judged by someone else's season. The player doesn't enter — their progression is the
        // game, and their own contract carries it.
        const GrowthReport gr = AdvanceGrowth(save, cc.year, std::set<std::string>{ playerName });
        rep.growth_moved = gr.movidos;
        rep.growth_up    = gr.subieron;
        rep.growth_down  = gr.bajaron;
    }

    // the 6 stages of the classic rollover (identical to the previous version) ----
    // 1) snapshot the record book BEFORE clearing entries
    for (auto& e : cc.enrollments) {
        int finalPos = playerFinalPos(e, playerName);
        int rounds   = (int)CalendarForSeries(e.series_id).size();
        // Close with the no-show gate, so a fully missed contracted season isn't
        // floored at +1 REP (keyed off the per-contract missed-round counter, not playerPos==fieldSize).
        if (e.status != 1) FinalizeSeason(e, prog, playerName, NoShowSeasonForSave(save, e.series_id));
        // Snapshot the TEAM you drove for in this series this season. Captured HERE -- in step 1,
        // BEFORE step 2 rolls/clears contracts -- so ContractForSeries is still valid. "" if no active contract
        // (self-funded with no team), the safe "unknown org" value the future per-org affinity treats as no-signal.
        const Contract* drove = ContractForSeries(save, e.series_id);
        save.completed_seasons.push_back(CompletedSeason{ e.series_id, cc.year, finalPos, rounds,
                                                          drove ? drove->team_id : std::string() });
    }
    // 2) Contracts (multi): decrements each one's remaining seasons; expired ones are removed; those still
    // alive get re-entered (entry fee waived) for the new year. "End of season = the
    // market moment": for every contract reaching end-of-term (seasons_left<=0) we generate a
    // RENEWAL offer (archetype ARCH_RENEWAL) with terms shifted by obj_ratio + relationship, and we
    // push it onto save.pending_renewals (keyed by series_id via id "of_"+series). The rollover screen
    // reads it for its "your contract" line, and "New objectives" feeds it in through the Accept/Negotiate path.
    // Closes the "renew_" mails from the year that's ending BEFORE clearing pending_renewals -- the list
    // CloseStaleRenewalMails needs to reconstruct the id lives right here, right before it gets cleared.
    CloseStaleRenewalMails(save, cc.year);
    // An ignored interview expires at rollover -- it loses its effect and unblocks the
    // press. Same idiom as CloseStaleRenewalMails, right next to it (it doesn't depend on pending_renewals, so the
    // ordering with the clear() below doesn't matter, but it lives here so the rollover's two "amnesties"
    // stay together).
    CloseStalePressMails(save);
    save.pending_renewals.clear();   // new season -> fresh renewals (dedup implicit by series_id)
    std::vector<std::string> reEnrollSeries;
    // Mandatory season insurance premium -- charged ONCE per held contract at rollover,
    // priced by the contract's series phase x its TYPE (ContractTypeIndex) x the past-season cleanliness EMA.
    // Guarded by Contract.premium_year != worldYear so a renewed/carried contract is never double-billed in the
    // same season. Solvency-aware: if the player can't pay, the premium is SKIPPED (credits never go negative --
    // this is a known edge case), and premium_year is NOT stamped so it can be billed once funds exist. Covered types
    // still pay (insurance is mandatory) but at a lower base via the type's damage_share_base. rep accumulates
    // the total charged, for the season mail-bill.
    int premiumYear = (save.current_world_year != 0) ? save.current_world_year : cc.year;
    // A title win outranks a firing in the same week. press_jl (fired) does NOT get
    // emitted right here -- (series_id, team_id) is COLLECTED and deferred until after the season-close loop
    // (season_close, further below), which is where press_tw (title) gets its turn with emitRolloverPress.
    // So if the same rollover brings BOTH, press_tw claims the single "one pending interview" slot
    // (PressInterviewPending) and press_jl gets dropped when it tries afterward -- never the other way around. Atenea's email
    // (job_lost, family 1) is untouched: it's still emitted right here, immediately, independent of the press.
    std::vector<std::pair<std::string, std::string>> pendingPressJobLoss;   // {series_id, team_id}
    for (size_t i = 0; i < save.contracts.size(); ) {
        Contract& c = save.contracts[i];
        if (!c.active) { save.contracts.erase(save.contracts.begin() + i); continue; }
        // premium FIRST (the contract raced this past season -> it owes the season's premium), guarded + solvent.
        if (c.premium_year != premiumYear) {
            const SeriesDef* csd = SeriesById(c.series_id);
            int phase = csd ? csd->level : 1;
            int typeIdx = ContractTypeIndex(c.kind, c.archetype);
            long prem = InsurancePremiumForContract(phase, typeIdx, prog.cleanliness);
            if (prem > 0 && prog.credits >= prem) {     // solvency block: can't-pay -> skip (no negative credits)
                prog.credits -= prem;
                EconLog("insurance_premium", -prem, prog.credits);   //
                c.premium_year = premiumYear;           // stamp so a second rollover this season doesn't re-charge
                rep.insurance_premium += prem;
            } else if (prem == 0) {
                c.premium_year = premiumYear;           // a 0-cost premium (type 0 base 0 edge) still counts as billed
            }
        }
        // A contract that hasn't raced a single round yet doesn't burn a season. The signing
        // window closes in DECEMBER and signs for the FOLLOWING year, so whatever you sign there arrives at
        // the rollover five days later without having raced a single race. Without this guard, that rollover would
        // dock its one and only season and the contract would die on January 5th: measured with the simulator, 100%
        // of signings were breaking at the very next rollover (22 signings / 22 breaks), the player lived in
        // permanent drought, and it didn't matter whether you dominated or finished last, because nobody ever kept a seat.
        // `signed_year` is the season the contract is signed FOR (stamped by SignWindowSeat with window_year), so
        // "hasn't started yet" is exactly signed_year >= the year that's now beginning.
        if (c.signed_year >= cc.year + 1) { reEnrollSeries.push_back(c.series_id); i++; continue; }
        c.seasons_left--;
        if (c.seasons_left > 0) { reEnrollSeries.push_back(c.series_id); i++; }
        else {
            // Job security cuts both ways. At end-of-term the team judges the
            // player's season. If the player UNDER-performed (perf < bal::JOBSEC_PERF_BAR) and is NOT protected
            // by a negotiated guaranteed_seat or anti_replace clause (accepted_deal.demands), he LOSES the seat:
            // NO renewal offer is built and the seat is FREED (marked open in seat_occupant -> an
            // open-seat the AI/market can re-fill, and the displaced player surfaces a job-lost mail). A
            // protected or on-target player gets the normal renewal. The bulk of job loss happens HERE at
            // rollover; the extreme mid-season path stays the existing no-show/breach logic.
            // Renewal fix: the performance the team judges is REAL, following this
            // precedence: (1) the FINAL TABLE of the just-archived championship if a season was actually raced (the
            // enrollment is still alive: step 6 clears enrollments AFTER this loop) -- position mapped to the
            // 0.15..1.0 range -> P1=1.0 (upgrade+duration), podium ~0.9, mid-table ~0.55 (neutral), last place
            // 0.15 (fired/not renewed, unless a protection clause applies); (2) the contract's objectives ratio
            // (obj_total>0) ONLY as a fallback with no table -- the only thing that currently writes obj is the TRIAL
            // race (binary 1/1, met AT SIGNING): passing the audition can't shield the renewal against
            // a disastrous season. Once a real season-objectives layer exists ("finish top 5"),
            // it'll decide the blend here. (3) no data (season not raced) -> 0.5 "met" (the previous behavior).
            // The SAME formula as the silly season (ContractSeasonRatio): a table relative to the
            // contract's objective if it has one (pivot 0.65 "just met it"), a raw table if not, the legacy test with no table.
            int tablePos = 0, tableField = 0; bool fromTable = false;
            double objRatioJS = ContractSeasonRatio(cc, c, playerName, &tablePos, &tableField, &fromTable);
            bool guaranteedSeat = c.accepted_deal.demands[NEGO_GUARANTEED_SEAT];
            bool antiReplace    = c.accepted_deal.demands[NEGO_ANTI_REPLACE];
            if (PlayerLosesSeat(objRatioJS, bal::JOBSEC_PERF_BAR, guaranteedSeat, antiReplace)) {
                // player dropped: free his SPECIFIC team seat (per-team granularity, mirroring the AI move-out
                // shape ApplyRolloverSeatChanges uses) so it becomes a concrete open seat (an offer source),
                // and record the non-renewal for the job-lost inbox mail.
                if (c.seat_idx >= 0)
                    save.seat_occupant[SeatKey(c.series_id, c.seat_idx)] = kSeatOpen;
                RolloverJobLoss jl;
                jl.series_id = c.series_id; jl.team_label = c.team_id; jl.perf = objRatioJS;
                rep.job_losses.push_back(jl);
                // Emits the "you were not renewed / replaced" inbox mail (family=1 MILESTONE,
                // payload_key "job_lost" re-resolved EN/ES by the renderer; NEVER persist the body). Content-derived
                // id (job_lost_<series>_<year>) so it dedups + re-fires a later season. Pushed straight onto
                // save.emails (the engine is the persistent mail source -- robust across every UI/auto-clock path).
                {
                    std::string mid = "job_lost_" + c.series_id + "_" + std::to_string(cc.year);
                    bool exists = false;
                    for (const auto& e : save.emails) if (e.id == mid) { exists = true; break; }
                    if (!exists) {
                        GeneratedEmail ge;
                        ge.id = mid; ge.tag = 1; ge.day = cc.cursor_doy; ge.year = cc.year; ge.family = 1;
                        ge.subtype = 0; ge.payload_key = "job_lost";
                        // Living inbox mail: Atenea's voice -- a deterministic variant plus {player}/{team} plus the sender.
                        int vc = MailVariantCount("job_lost");
                        ge.variant = vc > 0 ? (int)(MailHash(mid) % (unsigned)vc) : 0;
                        MailParams p; std::string pf = MailFirstName(save.player.name);
                        if (!pf.empty()) p.push_back({"player", pf});
                        p.push_back({"team", c.team_id});
                        ge.params = MailParamsEncode(p);
                        ge.from_name = MailManagerName(); ge.from_role_key = "inbox_role_manager";
                        save.emails.push_back(ge);
                    }
                }
                // Fired -> a candidate for a press interview
                // (press_jl_<series>_<year>, subtype 3). NOT emitted here -- it's collected and deferred until after
                // the season_close loop (further below), so that a title, if there is one this same rollover, gets
                // its turn first at the single "one pending interview" slot.
                pendingPressJobLoss.push_back({c.series_id, c.team_id});
            } else {
                // a contract at end-of-term -> offer a RENEWAL before freeing the seat (gradient term
                // shift). Renewal fix: the SAME real ratio (objectives or final table) that judged job security.
                market::OfferDef ren;
                bool built = BuildRenewalOffer(c, cc.year + 1, ren, objRatioJS);
                // A PROTECTED player (guaranteed_seat/anti_replace) cannot be dropped -- the team is
                // contractually bound to keep him. If the gradient would decline (a deeply under-performing but
                // protected player), HONOR the clause with a baseline renewal at the current terms (the
                // protection is exactly what overrides the team's reluctance). An on-target player whose gradient
                // declines for another reason is rare, but the clause guarantee is the binding rule here.
                if (!built && (guaranteedSeat || antiReplace)) {
                    ren = market::OfferDef{};
                    ren.id           = "of_" + c.series_id;
                    ren.direct       = true;
                    ren.team         = c.team_id;
                    ren.from         = c.team_id;
                    ren.clsId        = c.series_id;
                    ren.scope        = "season";
                    ren.contractKind = c.kind;
                    ren.archetype    = ARCH_RENEWAL;
                    ren.commitment   = 1;
                    ren.perRace      = c.per_race;   // baseline: same terms (no raise, no cut; 0 = no salary, no phantom value)
                    ren.signBonus    = (long)(ren.perRace * career::bal::MK_K_BONUS + 0.5);
                    ren.renewQuality = 2;                                   // "worse"/floor terms (clause honored)
                    built = true;
                }
                if (built) save.pending_renewals.push_back(ren);
                // Post-season rival offers: a podium at the end of a contract puts your
                // name on the market -- up to RIVAL_OFFER_MAX different teams from your own series bid against
                // your renewal, with salaries from their own band (TeamSalaryBand via OfferTermsCtx: a top team pays
                // more than a modest one -> real "better or worse terms"). They travel down the SAME lane as
                // the renewal (pending_renewals -> an actionable family-0 email -> AcceptOffer); signing
                // ANY offer for that series switches off the rest's actions (MailActionsFor splits on '#').
                // Mid-table -> nobody bids: the podium IS the news. ARCH_DIRECT_OFFER (not RENEWAL) -> 1
                // season on signing and a salaried type 4 (they sign you on salary: the podium vouches for you).
                if (fromTable && tablePos >= 1 && tablePos <= bal::RIVAL_OFFER_POS_BAR) {
                    const SeriesDef* rsd = SeriesById(c.series_id);
                    int rlvl = rsd ? rsd->level : 1;
                    int made = 0;
                    for (const Seat& seat : SeatsForClass(c.series_id, save)) {
                        if (made >= bal::RIVAL_OFFER_MAX) break;
                        if (seat.team_label.empty() || seat.team_label == c.team_id) continue;
                        bool dup = false;   // a team bids once (two seats at the same team don't duplicate the bid)
                        for (const auto& pr : save.pending_renewals)
                            if (pr.team == seat.team_label && pr.clsId == c.series_id) { dup = true; break; }
                        if (dup) continue;
                        made++;
                        market::OfferDef rv{};
                        rv.id = "of_" + c.series_id + "#rv" + std::to_string(made);
                        rv.direct = true; rv.team = seat.team_label; rv.from = seat.team_label;
                        rv.clsId = c.series_id; rv.scope = "season";
                        rv.contractKind = bal::CONTRACT_WORKS;
                        rv.archetype = ARCH_DIRECT_OFFER;
                        rv.commitment = 1;
                        market::OfferTermsOut t = market::OfferTermsCtx(save.prog.rating, rlvl,
                                                                        bal::CONTRACT_WORKS, ARCH_DIRECT_OFFER,
                                                                        c.series_id, seat.team_label, save,
                                                                        cc.year + 1);
                        rv.perRace = t.per_race; rv.signBonus = t.sign_bonus;
                        // classification for the summary line: does the bid come in above or below your current salary?
                        rv.renewQuality = (rv.perRace > (c.per_race > 0 ? c.per_race : 1)) ? 0 : 2;
                        save.pending_renewals.push_back(rv);
                    }
                }
            }
            save.contracts.erase(save.contracts.begin() + i);
        }
    }
    // Delivers renewals to the inbox. Until now
    // pending_renewals was only consumed by the Rollover screen; the promised email was never actually sent.
    // For every renewal built above, an actionable family-0 email is generated, id "renew_"+ren.id+"_"+toYear
    // (dedup via mailExists + re-fires the following season), payload_key = ren.id ("of_"+series -> the
    // inbox_offer_subj/body template). The team's own voice (renewed by YOUR OWN team), team-principal role, just like a real offer.
    // Instant sign/reject is resolved by cmd.inbox.answer (overlay): choice 1 -> MarketUiAccept(ren)
    // (the SAME AcceptOffer path used by the market/negotiation; RolloverUiConfirm is a stub, there's no separate "Rollover path"
    // to reuse); choice 2 -> dismiss the email, leaving the renewal alive for the Rollover screen.
    // Since the renewal isn't in the live market, the email does NOT expose an offerId (QueryInbox skips the composer
    // for "renew_" ids, which would bounce back found:false) -- only the generic bus's instant actions apply.
    {
        int toYear = cc.year + 1;
        for (const auto& ren : save.pending_renewals) {
            std::string mid = "renew_" + ren.id + "_" + std::to_string(toYear);
            bool exists = false;
            for (const auto& e : save.emails) if (e.id == mid) { exists = true; break; }
            if (exists) continue;
            GeneratedEmail ge;
            ge.id = mid; ge.tag = 1; ge.day = cc.cursor_doy; ge.year = cc.year; ge.family = 0;
            ge.subtype = ContractTypeIndex(ren.contractKind, ren.archetype);   // type index (0..5), same as offer emails
            ge.payload_key = ren.id;                                           // "of_"+series -> the offer template + actionable
            MailParams p;
            std::string pf = MailFirstName(save.player.name);
            if (!pf.empty()) p.push_back({"player", pf});
            const SeriesDef* rsd = SeriesById(ren.clsId);
            p.push_back({"series", rsd ? rsd->display : ren.clsId});
            p.push_back({"team", ren.team});
            // In-game fix: a renewal NEVER lives in the live market -> QueryInbox already excludes it
            // from offerId by id ("renew_", isRenewalMail earlier in this same file), so the negotiation composer
            // never sees it (it would bounce back found:false, same note a few lines below). "negotiable"="0"
            // is documentation/defense-in-depth for any future reader of the param, it doesn't change the actual
            // gate (which remains isRenewalMail); Accept/Reject still work through the instant actions.
            p.push_back({"negotiable", "0"});
            ge.params = MailParamsEncode(p);
            ge.from_name = ren.team; ge.from_role_key = "inbox_role_teamprincipal";
            save.emails.push_back(ge);
        }
    }
    // Living inbox mail: season closing -- the real balance sheet for EVERY season archived at this rollover
    // (cs.year == rep.from_year, the pre-increment year set above). Emitted unconditionally (unlike
    // the previous version, closing has no "first season" exception). Emitted HERE, AFTER the contracts loop
    // from step 2 (which charges the closing season's insurance premium), so that {net} = prog.credits -
    // season_start_credits INCLUDES that premium (the contract raced this season -> it owes it). It still comes BEFORE
    // the mailnarr reset at the end of the function (season_start_credits is reset there, not here).
    for (const auto& cs : save.completed_seasons) {
        if (cs.year != rep.from_year) continue;
        // A WON title (final_pos == 1) -> a press interview (press_tw_<series>_<year>, subtype 2).
        // Comes BEFORE the season-close dedup/continue below so it isn't coupled to that id (it has its own).
        // Respects the one-at-a-time rule via emitRolloverPress (if an interview is already pending, this one is dropped).
        if (cs.final_pos == 1) {
            const SeriesDef* twSd = SeriesById(cs.series_id);
            MailParams tp; std::string pfTw = MailFirstName(save.player.name);
            if (!pfTw.empty()) tp.push_back({"player", pfTw});
            tp.push_back({"series", twSd ? twSd->display : cs.series_id});
            emitRolloverPress(save, "press_tw_" + cs.series_id + "_" + std::to_string(cs.year), /*subtype=*/2,
                              "press_q_titlewon", tp, cc.year, cc.cursor_doy);
        }
        std::string mid = "season_close_" + cs.series_id + "_" + std::to_string(cs.year);
        bool exists = false; for (const auto& e : save.emails) if (e.id == mid) { exists = true; break; }
        if (exists) continue;
        GeneratedEmail ge;
        ge.id = mid; ge.tag = 1; ge.day = cc.cursor_doy; ge.year = cs.year; ge.family = 1; ge.subtype = 12;
        ge.payload_key = "season_close";
        MailParams p; std::string pf = MailFirstName(save.player.name);
        if (!pf.empty()) p.push_back({"player", pf});
        const SeriesDef* closeSd = SeriesById(cs.series_id);          // human-readable display if the catalog resolves it
        p.push_back({"series", closeSd ? closeSd->display : cs.series_id});
        p.push_back({"year", std::to_string(cs.year)});
        p.push_back({"pos", std::to_string(cs.final_pos)});
        p.push_back({"podiums", std::to_string(save.mailnarr.season_podiums)});
        p.push_back({"wins", std::to_string(save.mailnarr.season_wins)});
        p.push_back({"net", MailFmtCredits(prog.credits - save.mailnarr.season_start_credits)});
        // {best} = the season's most notable day (the latest circuit with a podium/win,
        // save.mailnarr.last_notable_track); {nemesis} = the rival's name saved in save.nemesis_id, resolved
        // via the SAME idiom as RolloverRetirement.name/ProfileRivals (DriverById: id->PoolDriver, an empty name
        // falls back to the id itself; an id that doesn't resolve -> the param is OMITTED, never sent empty).
        std::string best = save.mailnarr.last_notable_track;
        std::string nemesisName;
        if (!save.nemesis_id.empty()) {
            const PoolDriver* pd = DriverById(save.nemesis_id);
            if (pd) nemesisName = pd->name.empty() ? pd->id : pd->name;
        }
        if (!best.empty())        p.push_back({"best", best});
        if (!nemesisName.empty()) p.push_back({"nemesis", nemesisName});
        // Data contract: each variant requires a different set of params --
        // v0 needs best+nemesis TOGETHER, v1 needs ONLY nemesis (no {best}), v2 needs ONLY best (no {nemesis})
        // (verified in strings.cpp: v0 has both placeholders, v1 only {nemesis}, v2 only {best}).
        // MailInterpolate erases a placeholder with no data instead of substituting it -> force v2 when
        // either one is missing (the original bug) leaves "Your best day, , was..." when ONLY {best} is missing (a rookie
        // with a nemesis but no podium yet, a common case). Now each combination forces the variant that actually
        // fits the available data; if NEITHER is available, v3 is used (no tokens, see
        // strings.cpp), because season closing is a ritual and the email must always go out.
        // % 3 is DELIBERATE, not % vc: v3 is RESERVED for the no-data case; if it were drawn alongside
        // v0-v2 when data IS available, v3 (with no {best}/{nemesis}) could come up even though there was data to tell.
        if (!best.empty() && !nemesisName.empty()) {
            int vc = MailVariantCount("season_close");
            ge.variant = vc > 0 ? (int)(MailHash(mid) % 3u) : 0;
        } else if (!nemesisName.empty()) {
            ge.variant = 1;   // season_close_v1 -- only needs {nemesis}
        } else if (!best.empty()) {
            ge.variant = 2;   // season_close_v2 -- only needs {best}
        } else {
            ge.variant = 3;   // season_close_v3 -- no tokens, the email always goes out
        }
        ge.params = MailParamsEncode(p);
        ge.from_name = MailManagerName(); ge.from_role_key = "inbox_role_manager";
        save.emails.push_back(ge);
    }

    // NOW (after season_close/press_tw above), the deferred firings from the
    // contracts loop are attempted. If this same rollover brought a title, press_tw already claimed the single
    // "one pending interview" slot (PressInterviewPending), and emitRolloverPress silently drops these --
    // a title outranks a firing in the same week. With no title, this behaves exactly as before.
    for (const auto& jl : pendingPressJobLoss) {
        MailParams jp; std::string pfJl = MailFirstName(save.player.name);
        if (!pfJl.empty()) jp.push_back({"player", pfJl});
        jp.push_back({"team", jl.second});
        emitRolloverPress(save, "press_jl_" + jl.first + "_" + std::to_string(cc.year), /*subtype=*/3,
                          "press_q_joblost", jp, cc.year, cc.cursor_doy);
    }

    // Build the cast (union of tiers, minus retirees) + birth_year ----
    std::vector<RosterDriver> cast; std::set<std::string> seen;
    for (int t = 1; t <= 7; t++)
        for (const auto& r : RosterForTier(t))
            if (!save.retired.count(r.id) && seen.insert(r.id).second) cast.push_back(r);
    // Also everyone who occupies a seat. RosterForTier is only the cohort (28 drivers):
    // the ~900 real occupants on the board never entered here, so ShouldRetire never looked at them and
    // the world's actual side couldn't age. Measured on a fresh career: 0 retirements in three
    // seasons, even with ages already set. The age data existed; the cast just never reached it.
    for (const auto& r : WorldOccupants(save))
        if (!save.retired.count(r.id) && seen.insert(r.id).second) cast.push_back(r);
    for (auto& r : cast) {
        // seeds birth_year: if the save doesn't have it yet, it takes the roster's value (0 -> stays 0 = unknown).
        // Only what the pool DOESN'T already know gets persisted. With the cast expanded from 28 to ~940, this
        // would write nine hundred birth years that are already in drivers.json -- the same mistake avoided
        // earlier with contracts: a derivable value isn't saved, because saving it means it can drift out of sync.
        if (save.birth_year.find(r.id) == save.birth_year.end() && r.birth_year != 0 && BirthYearFor(r.id) == 0)
            save.birth_year[r.id] = r.birth_year;
        // and copies the save's value into the local RosterDriver so the age functions can see it.
        auto by = save.birth_year.find(r.id);
        if (by != save.birth_year.end()) r.birth_year = by->second;
    }

    int worldYear = (save.current_world_year != 0) ? save.current_world_year : cc.year;

    // 3) (Retired 02/07) rival_form used to drift here (scheduled SKILL growth, AdvanceRivalSeasonAged).
    //    The cast's skill is INNATE (authored base_skill); what grows is their fame = ELO from results.
    //    Age and retirement (steps 4-5) remain intact.

    // Aged rating by id (a deterministic projection: base_skill x age curve) ----
    // Close enough for retirement/promotion purposes (the exact "live" rating is tracked per-driver elsewhere;
    // here a consistent, monotonic projection is all that's needed).
    std::map<std::string,double> agedRating;
    for (const auto& r : cast) {
        // The SAME projection the market uses (DriverAgedRating), not a copy. The rollover
        // used to have its own, and once the level ladder got anchored, the two ended up measuring on different scales: the
        // market judged a driver with their tier's anchor applied, while retirement used a flat band. The
        // same driver ended up with two different ratings depending on who was asking.
        agedRating[r.id] = DriverAgedRating(r.id, r.base_skill, r.birth_year, save, worldYear,
                                            r.age_shift, r.tier_max);
    }

    // 4) Retirements (hybrid) -> accumulate freed seats by tier (the outgoing tier = the driver's tier_max)
    // Besides the per-tier COUNT (freedByTier, consumed by PromoteFreedSeats), we also collect the
    // specific ids of the retirees to free their SPECIFIC team seat (per-team granularity), which
    // ApplyRolloverSeatChanges writes into save.seat_occupant (the source for open-seat offers).
    // 18/09 — THE LAST DOOR. The census already resolves the name (see `EnsureRosterNames`), but this
    // report is the ONLY thing the player reads of the whole winter: if something slipped through with no identity, here
    // it would show «rd_1045361494 · -1 yrs». So the name is requested through the presentation chain and the id is NEVER
    // accepted as a name; and the unknown age (`DriverAge` returns -1 with no birth year) travels
    // as 0, which is what the bridge and the screen know not to paint.
    auto nombreDe = [](const std::string& id, const std::string& yaTengo) {
        if (!yaTengo.empty() && yaTengo != id) return yaTengo;
        const std::string n = DriverDisplayName(id);
        return (!n.empty() && n != id) ? n : std::string();
    };
    auto edadDe = [](int birthYear, int worldYear) {
        const int e = DriverAge(birthYear, worldYear);
        return e > 0 ? e : 0;                       // 0 = unknown; never the -1 sentinel
    };
    std::map<int,int> freedByTier;
    std::set<std::string> retiredThisRollover;
    for (const auto& r : cast) {
        auto rit = agedRating.find(r.id);
        double rr = (rit == agedRating.end()) ? 0.0 : rit->second;
        // 23/09 (Josh): whoever isn't in the driver market never retires -- not even Frank Gardner at 94 in
        //  the Keystone Challenge of his pack. See career_seats.h::FueraDelMercadoDePilotos.
        if (!FueraDelMercadoDePilotos(r.id) && ShouldRetire(r, worldYear, rr)) {
            save.retired.insert(r.id);
            retiredThisRollover.insert(r.id);
            int tier = (r.tier_max >= 1 && r.tier_max <= 7) ? r.tier_max : 7;
            freedByTier[tier]++;
            RolloverRetirement rr2;
            rr2.driver_id = r.id; rr2.name = nombreDe(r.id, r.name);
            rr2.age = edadDe(r.birth_year, worldYear);
            rr2.reason = (r.retire_year > 0) ? "Retires" : "Age";
            rep.retirements.push_back(rr2);
        }
    }

    // 5) Promotions: the cast minus the freshly retired, matched against the freed seats
    std::vector<RosterDriver> survivors;
    for (const auto& r : cast) if (!save.retired.count(r.id)) survivors.push_back(r);
    auto proms = PromoteFreedSeats(freedByTier, survivors, agedRating);
    rep.promotions_attempted = (int)proms.size();   // ~~~29/08: before any filter
    std::map<std::string,int> promotedToTier;   // id -> destination tier (the AI re-fill target)
    for (const auto& p : proms) {
        promotedToTier[p.id] = p.to_tier;
        RolloverPromotion pr;
        pr.driver_id = p.id; pr.to_tier = p.to_tier; pr.from_tier = (p.to_tier > 1) ? p.to_tier - 1 : 1;
        // display name from the cast
        for (const auto& r : survivors) if (r.id == p.id) { pr.name = r.name.empty() ? r.id : r.name; break; }
        rep.promotions.push_back(pr);
    }
    // Wire the freed/open seats into the persisted occupancy (per-team granularity). A
    // retiree's SPECIFIC team seat becomes open (offer source); a promoted climber re-occupies a freed seat at
    // his destination tier (AI->AI fill), so only genuinely-still-open seats remain open -- consistent with
    // PromoteFreedSeats. Era-memory series are skipped inside the helper. This is the offer
    // source GenerateOffers consumes (replacing the old static-per-series generation). Deterministic.
    // `survivors` (the living cast) + worldYear turn the AI->AI re-fill into a merit-based contest:
    // every team with an open seat chooses among the promoted candidates with the same model that chooses you.
    // Contracts expire FIRST, THEN everything that's freed up gets redistributed. The order
    // matters: if redistribution happened first, seats expiring this year would go uncontested until
    // the next rollover. Before this, retirement was the ONLY way a seat could ever
    // get freed, and measured on a fresh save that gave 0 free seats out of 1656.
    // And someone who isn't renewed does NOT vanish: they drop a category. ExpireSeatContracts decides by
    // performance (are they still up to their category's level?) and returns whoever's leaving;
    // CascadeDemoteFreeAgents finds them the highest seat they can still hold, up to two
    // tiers down, and only retires whoever finds none. Without this second step the world
    // was losing them: measured on 03/08, 155 drivers vanished over three seasons.
    // And life happens. Before anything else in the market: every season some people leave for
    // reasons that are neither age nor level -- they run out of money, get injured, find a job. It happens
    // mostly at the BOTTOM, where half the grid is amateurs paying out of pocket. It runs first so its
    // vacancies enter the same redistribution as everything else and get inherited by whoever's coming up.
    std::vector<std::string> seVan;
    CascadeAttrition(save, worldYear, asientosDelJugador, &seVan);
    for (const auto& id : seVan) {
        if (!retiredThisRollover.insert(id).second) continue;
        RolloverRetirement rr4;
        rr4.driver_id = id; rr4.name = nombreDe(id, std::string());
        for (const auto& r : cast) if (r.id == id) { rr4.name = nombreDe(id, r.name);
                                                    rr4.age = edadDe(r.birth_year, worldYear); break; }
        rr4.reason = "Left";                          // leaves the sport: neither age nor level
        rep.retirements.push_back(rr4);
    }

    // — THE REAL MARKET, AND IT GOES HERE: BEFORE `ExpireSeatContracts`.
    //
    // Teams swap an incumbent for someone clearly better that they can afford. The place is CRITICAL and
    // it cost me two attempts: placed after, `ExpireSeatContracts` has already RENEWED the contract of everyone
    // who clears the bar, so nobody is left free and the step does nothing — measured, zero changes. It is the
    // same ordering trap that took down the contract rule two days ago.
    //
    // Here the contracts are in their real state: whoever has one alive is protected (which is what makes
    // signing for three years mean something) and whoever has it expired is in dispute. And renewing or replacing
    // are the SAME decision, so taking it right before the renewal is the natural thing.
    //
    // Without this F1 had ONE entry in twenty seasons: when sealing the gaps the doors were sealed, and
    // tripling the aces didn't move a single digit of its grid.
    std::vector<FreeAgent> salientes;
    // 23/09 (, «One driver, one seat»): FIRST, what the signing window already decided is seated. The decision
    //  week named winners that nobody seated, and ExpireSeatContracts then renewed the contract of the
    //  usual incumbent. It goes before and before the renewals for the same reason as they do: with the
    //  contracts still in their real state. The displaced incumbent enters `salientes`, like the one who doesn't renew.
    { double mnw = (double)save.prog.credits / bal::MK_PAYDRIVER_MONEY_REF;
      if (mnw < 0.0) mnw = 0.0; else if (mnw > 1.0) mnw = 1.0;
      rep.window_winners_seated = SeatWindowWinnersForNewSeason(save, worldYear, mnw, &salientes); }
    //  And the newly signed one no longer gets promoted this winter: the promotion above (PromoteFreedSeats) was decided
    //  before knowing who signed, and ApplyRolloverSeatChanges also seated him in another seat of his destination
    //  level -- the winner ended up racing in two series at once. He is removed from the list and from the report.
    { std::set<std::string> fichados;
      if (save.window_year == worldYear + 1)
          for (const auto& w : save.window_seats) if (w.seated && !w.winner_id.empty()) fichados.insert(w.winner_id);
      for (const auto& id : fichados) promotedToTier.erase(id);
      rep.promotions.erase(std::remove_if(rep.promotions.begin(), rep.promotions.end(),
                                          [&](const RolloverPromotion& p) { return fichados.count(p.driver_id) > 0; }),
                           rep.promotions.end()); }
    // ~~~29/08 — the incumbent whom a team swaps for someone better enters the SAME distribution as the one
    //  who doesn't renew: he loses the seat, not the career. Before, he stayed out of the world without anyone
    //  saying so -- 10 named people in five winters.
    const SwapReport cambios = ReplaceWeakIncumbents(save, worldYear, asientosDelJugador, &salientes);
    rep.seats_swapped = cambios.cambios;

    const int seatsExpired = ExpireSeatContracts(save, worldYear, &salientes, asientosDelJugador);
    rep.contracts_expired = seatsExpired;   // ~~~29/08: and now also in the report
    std::vector<std::string> retiradosPorNivel;
    std::vector<Demotion> bajadas;
    // ~~~29/08 — FIRST pass: places whoever it can and retires NOBODY. Those who find no gap
    //  are kept to retry after the promotions, which are what open seats below.
    std::vector<FreeAgent> sinSitio;
    CascadeDemoteFreeAgents(save, salientes, worldYear, &retiradosPorNivel, asientosDelJugador, &bajadas,
                            /*retirarSinSitio=*/false, &sinSitio);
    // Those who found nowhere to go actually retire, and the report has to count it the same way
    // it counts age-based retirements: to the player, it's the same kind of news.
    for (const auto& id : retiradosPorNivel) {
        if (!retiredThisRollover.insert(id).second) continue;   // already listed under age -> don't duplicate
        RolloverRetirement rr3;
        rr3.driver_id = id;
        rr3.name = nombreDe(id, std::string());
        for (const auto& r : cast) if (r.id == id) { rr3.name = nombreDe(id, r.name);
                                                    rr3.age = edadDe(r.birth_year, worldYear); break; }
        rr3.reason = "Level";                                   // no longer up to standard anywhere
        rep.retirements.push_back(rr3);
    }
    ApplyRolloverSeatChanges(save, retiredThisRollover, promotedToTier, survivors, worldYear,
                             asientosDelJugador);
    // And the rookies, at the end of the cascade. They come LAST on purpose: first whoever needs to retire
    // retires, then those below the bar get demoted, then those above it get promoted, and only what's
    // left over after that gets inherited by a new generation. The other way around, a rookie would take the spot from
    // an established driver, which is exactly what's meant to be avoided ("the cohort only as a
    // last resort"). They enter at the bottom (tiers 1-2), with their own name, age, and ceiling.
    // And the trickle-down, before the rookies: every open seat pulls from the tier below it, top
    // to bottom, and only what's left at the very bottom gets inherited by a new generation. Without this, promotions used to stay
    // tied to retirements, and a vacancy from an expired contract wouldn't move anyone.
    std::vector<std::string> ascendidos;
    CascadePromoteToOpenSeats(save, worldYear, &ascendidos, asientosDelJugador);
    // SECOND pass of demotions: now yes, with the seats the promotions have opened below. Whoever
    //  is still without a place after this retires for real -- it is no longer «there was no gap today», it is «there is none».
    if (!sinSitio.empty())
        CascadeDemoteFreeAgents(save, sinSitio, worldYear, &retiradosPorNivel, asientosDelJugador, &bajadas,
                                /*retirarSinSitio=*/true, nullptr);
    rep.demotions = (int)bajadas.size();
    rep.retired_no_seat = (int)retiradosPorNivel.size();
    // ~~~29/08 — THE DRAG PROMOTIONS DIDN'T REACH THE REPORT, and they are ALL there are.
    //
    // `ascendidos` was filled and used nowhere. `rep.promotions` came from the OLD mechanism
    // (`PromoteFreedSeats`, tied only to retirements) which today returns **zero always** -- measured with
    // `promotions_attempted`: 0 generated in five winters. Meanwhile the drag moved between **78 and
    // 153 people of category each year**, and the player read «0 promotions» on the end-of-season
    // screen. Besides `grids_regenerated` hangs on this list not being empty.
    //
    // The destination tier is resolved from the ALREADY moved board (`tier_now` = their highest seat today), which is
    //  the only source that knows where each one ended up without duplicating the cascade logic.
    if (!ascendidos.empty()) {
        std::map<std::string,int> tierAhora;
        for (const auto& r : WorldOccupants(save)) tierAhora[r.id] = r.tier_now;
        std::set<std::string> yaListado;
        for (const auto& pr : rep.promotions) yaListado.insert(pr.driver_id);
        for (const auto& id : ascendidos) {
            if (!yaListado.insert(id).second) continue;      // don't duplicate if the old one already counted it
            RolloverPromotion pr;
            pr.driver_id = id;
            const auto it = tierAhora.find(id);
            pr.to_tier   = (it == tierAhora.end()) ? 0 : it->second;
            pr.from_tier = pr.to_tier > 1 ? pr.to_tier - 1 : 1;
            for (const auto& r : survivors)
                if (r.id == id) { pr.name = r.name.empty() ? r.id : r.name; break; }
            if (pr.name.empty()) pr.name = id;
            rep.promotions.push_back(pr);
        }
    }
    std::vector<std::string> novatos;
    rep.rookies_born = CascadeGenerateRookies(save, worldYear, &novatos, asientosDelJugador);
    // THE FINAL SWEEP, and it goes HERE: after all the mechanisms have done their thing. It seals the two
    // user rules (13/08) — ERA drivers don't take live seats, and NO gap is left, even if
    // the bar has to be lowered and someone who doesn't belong promoted. See `SealLiveGrids`.
    const SealReport sello = SealLiveGrids(save, worldYear, asientosDelJugador);
    rep.seats_evicted = sello.desalojados;
    rep.seats_backfilled = sello.rellenados;
    rep.seats_unfilled = sello.sinNadie;
    rep.signed_on_level = sello.daElNivel;
    rep.signed_below_bar = sello.bajoElListon;
    rep.signed_over_budget = sello.sobreCoste;
    // ~~~16/09 (decision by Josh, card 13) — AND THE ROOKIES AGAIN, AFTER THE SEAL. The seal fills from
    // top to bottom and, each time it promotes someone, LEAVES ALL THEIR OTHER SEATS FREE. Those seats fall
    // almost always at level 1, and level 1 is exactly the only one the seal CAN'T refill: its pool of
    // candidates is `WorldOccupants` —people who ALREADY have a seat— and the guard «their tier >= the destination's» discards
    // all of them when the destination is the step below, because there is nobody below level 1.
    // As the rookies had already run twenty lines earlier, those seats stayed empty until the next
    // winter, and there they piled up.
    //
    // MEASURED (20 seasons, the three discipline states): the empty seats were ALL at
    // level 1, at every milestone. With this second pass, T1/T2/T5 go from 12/10/5 to 0/0/0 and T20 drops from 37
    // to 20 — Kingsmere Uma713, from 18 of its 24 seats to 12.
    //
    // It is NOT «fill at all costs», which is the option card 9 left untaken: `CascadeGenerateRookies`
    // looks FIRST at the pool of real people —the one that just became free, the person with curated origin, the
    // reserve— and only invents if nobody is left. Substitutions of a person by an invented one stay at 20 and the
    // real ones seated at T20 GO UP (1,342 → 1,347). What it covers are seats that nobody occupies today, and a car
    // that doesn't go out is worse than an invented rookie in a karting school.
    //
    // It only looks at OPEN seats, so it takes the seat from nobody, and it is idempotent: if the seal left
    // none open, this pass does nothing.
    //
    // 17/09 — AND HERE THE GAP WINS, which is what it was missing. This pass ran with the class bar
    // in place, so in an entry category it could leave the seat empty because the rookie coming out
    // didn't make the level — exactly the case this paragraph says it doesn't want. It was noticed when distributing the cohort properly:
    // with the strong half of the player's generation running at level 1, the average of those classes rises 64
    // points, the bar with it, and four of the twelve entry classes began to reject rookies.
    // `elHuecoGana` is the same rule the seal already applies: the bar is for DECIDING WHO comes in, not for
    // leaving a car without going out on track. It still looks at the pool of real people before inventing anyone.
    rep.rookies_born += CascadeGenerateRookies(save, worldYear, &novatos, asientosDelJugador,
                                               /*elHuecoGana*/true);
    PruneGenDrivers(save);   // and drops from the save the generated drivers who no longer race or exist
    // 13/08 — A PROMOTION THAT DIDN'T TAKE ISN'T COUNTED. `PromoteFreedSeats` runs BEFORE attrition, so
    // someone can be promoted and, forty lines further down, leave the sport: their seat reopens and
    // another inherits it, but the report kept announcing the promotion. It wasn't seen because until today the promoted was
    // always the best of the step —whom attrition almost never touches—; when fixing the lateral transfer
    // it began to promote ordinary people and the test jumped. It is filtered at the end, which is the only place where
    // it is already known who is still alive.
    rep.promotions.erase(std::remove_if(rep.promotions.begin(), rep.promotions.end(),
                                        [&save](const RolloverPromotion& p) {
                                            return save.retired.count(p.driver_id) != 0;
                                        }),
                         rep.promotions.end());
    // 18/09 — THE WINTER SIGNINGS, from the photo before against the one now (see `fotoAsientos` above).
    // Before this was left empty on purpose («promotions ARE the visible moves») and the end-of-season
    // screen showed an always-empty column of signings. Out with the promoted (they already show up as
    // promotion) and the retired; whoever wasn't seated before is a rookie or a return, not a signing.
    {
        const auto despues = fotoAsientos();
        std::set<std::string> ascendidos;
        for (const auto& pr : rep.promotions) ascendidos.insert(pr.driver_id);
        auto rotulo = [](const std::pair<std::string, std::string>& sitio) {
            const SeriesDef* sd = SeriesById(sitio.first);
            return sitio.second + " \xC2\xB7 " + (sd ? sd->display : sitio.first);
        };
        for (const auto& kv : despues) {
            const auto antes = antesDelInvierno.find(kv.first);
            if (antes == antesDelInvierno.end()) continue;              // had no seat: not a signing
            if (ascendidos.count(kv.first) || save.retired.count(kv.first)) continue;
            std::vector<std::pair<std::string, std::string>> nuevos, dejados;
            for (const auto& x : kv.second) if (!antes->second.count(x)) nuevos.push_back(x);
            if (nuevos.empty()) continue;                                // stays where they were
            for (const auto& x : antes->second) if (!kv.second.count(x)) dejados.push_back(x);
            RolloverMarketMove mv;
            mv.driver_id = kv.first;
            mv.name = DriverDisplayName(kv.first);
            mv.from_team = dejados.empty() ? std::string() : rotulo(dejados.front());
            mv.to_team = rotulo(nuevos.front());
            rep.market_moves.push_back(std::move(mv));
        }
    }

    // 6) fresh market + intra-year gating reset (identical to the previous version)
    save.declined_offers.clear();
    save.expired_offers.clear();
    save.lapsed_offers.clear();
    save.offer_first_seen.clear();
    save.withdrawn_offers.clear();   // Same pattern as declined/expired/lapsed. The
                                      // negotiation THREADS aren't cleared here (AdvanceWeek closes them out with an email).
    save.completed_races.clear();
    // Season reset for active sponsor cards (clause_miss=0; does NOT touch
    // lifespan/level/slot). This IS the canonical point where "the season resets" (year++, fresh intra-year gating);
    // RolloverYearReport is the SINGLE rollover shared by both the overlay (AdvanceWeek) and the simrunner (career_simrunner.cpp),
    // so anchoring it here (pure engine code) covers both paths instead of just the overlay's.
    career::CardOpSeasonReset(save);

    // Decays the narrative "beef" level -- a year with no fresh duel subtracts 1 level (and at 0 it gets
    // erased). fromYear = cc.year HERE, before incrementing (the year that's closing is the one compared against
    // last_year); calling it after the ++ would compare against the year that JUST started and nothing would ever decay.
    career::BeefDecayAtRollover(save, cc.year);

    // Marks older than 3 years already weigh 0 in RelMarksSum -- they get purged so the save doesn't grow without bound.
    career::RelMarksPurge(save, cc.year);

    // The INTEL + nemesis block. HERE, after retirements/promotions (rep is populated) and after
    // ApplyRolloverSeatChanges (save.seat_occupant in its "after" state), and BEFORE cc.year++ -- fromYear is
    // the year that's closing (the ids carry that year, just like season_close), and save.nemesis_id still holds the one from the year that's
    // ending. Everything is delegated to the helper (a single source, testable without assembling a whole rollover).
    IntelRolloverMails(save, rep, seatsBefore, cc.year, cc.cursor_doy);

    // The chronicle of the cascade. A season moves ~70 seats, and that doesn't turn into 70
    // emails: it becomes ONE column with what's actually worth telling. The criterion for what makes it in isn't
    // volume, it's who the reader cares about -- the one leaving after a long career, the debutant
    // appearing out of nowhere at the top, and the one falling out of their category.
    //
    // Comes AFTER the INTEL block on purpose: that block is what touches the player up close (rivals with
    // shared history, moves within their orbit), and this is the wider world. A press piece with no actions,
    // like the ranking piece, so it doesn't compete for the "one interview at a time" slot.
    {
        const std::string cid = "press_cascade_" + std::to_string(cc.year + 1);
        bool yaEsta = false;
        for (const auto& e : save.emails) if (e.id == cid) { yaEsta = true; break; }
        if (!yaEsta) {
            // (1) The one leaving: the retiree from the highest category, tie-broken by age. A
            //     tier-7 name hanging up the helmet is news; a karting driver quitting at 19 isn't.
            std::string retirado; int mejorTier = 0, mejorEdad = 0;
            for (const auto& r : rep.retirements) {
                if (r.name.empty() || r.name == r.driver_id) continue;   // no name -> nothing gets published
                int tier = 0;
                for (const auto& kv : seatsBefore) {
                    if (kv.second != r.driver_id) continue;
                    const SeriesDef* sd = SeriesById(SeriesOfSeatKey(kv.first));   // ditto
                    if (sd && sd->level > tier) tier = sd->level;
                }
                if (tier > mejorTier || (tier == mejorTier && r.age > mejorEdad)) {
                    mejorTier = tier; mejorEdad = r.age; retirado = r.name;
                }
            }

            // (2) The one showing up: the rookie who debuted the highest. It's the story this step just
            //     made exceptional on purpose -- only whoever clears the bar on their own merit gets there.
            std::string novato, novatoEquipo; int novatoTier = 0;
            for (const auto& id : novatos) {
                auto g = save.gen_drivers.find(id);
                if (g == save.gen_drivers.end() || g->second.name.empty()) continue;
                for (const auto& sd : AllSeries()) {
                    if (sd.level <= novatoTier) continue;
                    for (const auto& s : SeatsForClass(sd.id, save)) {
                        if (s.open || s.occupant_id != id) continue;
                        novatoTier = sd.level; novato = g->second.name;
                        novatoEquipo = s.team_label.empty() ? sd.display : s.team_label;
                        break;
                    }
                }
            }

            // (3) The one falling: the biggest drop, taken from the actual data the cascade just produced.
            //     This used to be INFERRED by comparing snapshots of the world, and it published drops that didn't exist: a
            //     driver holding seats in several tiers who lost the top one would "show up" in the lower one, where they were
            //     already sitting. On a real save it produced "Bruno Almeida -> Ivory Cup 6423", and nobody
            //     goes back to karting. This is the THIRD time this exact inference error has shown up in this
            //     step; asking whoever made the move doesn't allow for it.
            std::string caido, caidoDestino;
            {
                int mayorCaida = 0;
                for (const auto& d : bajadas) {
                    const int caida = d.from_tier - d.to_tier;
                    if (caida <= mayorCaida) continue;
                    const std::string nom = DriverDisplayName(d.driver_id);
                    if (nom.empty() || nom == d.driver_id) continue;   // no name -> nothing gets published
                    const SeriesDef* sd = SeriesById(d.to_series);
                    if (!sd) continue;
                    mayorCaida = caida; caido = nom;
                    caidoDestino = sd->display.empty() ? sd->id : sd->display;
                }
            }

            // Without both mandatory pieces there's no column: better not to publish than to publish a gap.
            // The drop is OPTIONAL on purpose, and the first season shows why: nobody's had
            // time to decline yet, so there isn't a single demotion anywhere in the world. Requiring it would leave
            // the player with no chronicle in exactly the year the career begins.
            if (!retirado.empty() && !novato.empty()) {
                GeneratedEmail ge;
                ge.id = cid; ge.tag = 2; ge.day = cc.cursor_doy; ge.year = cc.year; ge.family = 6;
                ge.subtype = bal::MK_PRESS_SUBTYPE_CASCADE; ge.payload_key = "press_cascade";
                // The variant is chosen by CONTENT, not by hash: v1 names {faller}, and with no drop
                // it would print the raw key. That's the difference between a proper column and a visible bug.
                // The variant is chosen by CONTENT, not by hash: v1 names {faller}, and with no drop
                // it would print the raw key. And the hash here would also be CONSTANT -- the content-id doesn't
                // depend on the specific career -- so it would always fail the same way.
                ge.variant = caido.empty() ? 0 : 1;
                MailParams p;
                std::string pf2 = MailFirstName(save.player.name);
                if (!pf2.empty()) p.push_back({"player", pf2});
                p.push_back({"retiree", retirado});
                p.push_back({"rookie", novato});
                p.push_back({"rookie_team", novatoEquipo});
                if (!caido.empty()) { p.push_back({"faller", caido}); p.push_back({"faller_to", caidoDestino}); }
                ge.params = MailParamsEncode(p);
                ge.from_name = MailPressName(); ge.from_role_key = "inbox_role_press";
                save.emails.push_back(ge);
            }
        }
    }

    // Preseason power ranking -- a press piece (family 6, subtype RANKING, with NO actions and no
    // one-at-a-time rule: it isn't a question to answer, so it coexists fine with any pending interview).
    // After the INTEL block (retirements/promotions already applied to save.retired/driver_ratings) and BEFORE cc.year++.
    // {rank} = the player's position in the REAL world ranking (ProfileMyRank over driver_ratings filtered to the
    // active-modern set with worldYear); {leader} = the first RankRow that isn't the player. Empty pool, rank<=0, or no
    // resolvable leader -> don't emit. Sender is a journalist (MailPressName / inbox_role_press, tag 2). The id embeds the
    // destination year (toYear = the year being entered) -> re-fires every rollover (dedup by content-id).
    {
        int toYear = cc.year + 1;   // cc.year++ happens right below this; the ranking is for the season that's about to start
        std::string rid = "press_ranking_" + std::to_string(toYear);
        bool exists = false; for (const auto& e : save.emails) if (e.id == rid) { exists = true; break; }
        if (!exists && !save.driver_ratings.empty()) {
            std::set<std::string> active = ActiveModernSet(save.birth_year, save.retired, worldYear);
            int rank = ProfileMyRank(save.driver_ratings, active, playerName, save.prog.rating);
            std::vector<RankRow> board = ProfileWorldRanking(save.driver_ratings, active, playerName, save.prog.rating);
            std::string leader;
            for (const auto& rr : board) if (!rr.you) { leader = rr.name; break; }
            // The #1-ranked player doesn't get a ranking piece -- the template names the LEADER
            // above you (board[0] discarding the player themselves), so at rank==1 that leader would be the
            // runner-up, contradicting the headline ("still above you" when you're actually leading). Gate:
            // rank>=2: only emitted when there's genuinely someone above you. A champion's variant
            // (celebrating the #1 spot instead of needling them) is left for later.
            if (rank >= 2 && !leader.empty()) {
                GeneratedEmail ge;
                ge.id = rid; ge.tag = 2; ge.day = cc.cursor_doy; ge.year = cc.year; ge.family = 6;
                ge.subtype = bal::MK_PRESS_SUBTYPE_RANKING; ge.payload_key = "press_ranking";
                int vc = MailVariantCount("press_ranking");
                ge.variant = vc > 0 ? (int)(MailHash(rid) % (unsigned)vc) : 0;
                MailParams p; std::string pf = MailFirstName(save.player.name);
                if (!pf.empty()) p.push_back({"player", pf});
                p.push_back({"rank",   std::to_string(rank)});
                p.push_back({"leader", leader});
                ge.params = MailParamsEncode(p);
                ge.from_name = MailPressName(); ge.from_role_key = "inbox_role_press";
                save.emails.push_back(ge);
            }
        }
    }

    // 7) year advance + world tick + contract re-entry
    // corollary: the presence latch is year-scoped; at the start of a new year you haven't raced yet -> reset it
    // (prevents a repeated day-of-year in the following year from inheriting last year's "you raced here" flag).
    cc.year++; cc.cursor_doy = 0;
    cc.last_race_doy = 0; cc.last_race_circuit = 0; cc.last_race_hour = 0; cc.last_race_minutes = 0;
    cc.enrollments.clear();
    save.current_world_year = worldYear + 1;
    rep.to_year = cc.year;
    for (const auto& cs : reEnrollSeries) {
        Enroll(cc, cs, prog, /*waiveInscription=*/true);   // multi-contrato: re-inscribe cada serie viva
        ApplyPrivateerPick(cc, cs, save);                  // the privateer keeps their car+skin
    }

    // Living inbox mail: season preview -- ONLY if the primary contract is still active after this
    // rollover AND at least one season is already archived (a new career's first season is
    // covered by the onboarding arc, EmitOnboardIntro/ob_first_race/ob_first_goal, not this preview). One preview per
    // rollover (the primary contract = the first active one in save.contracts); dedup by id, same as the closing mail.
    if (!save.completed_seasons.empty()) {
        for (const auto& c : save.contracts) {
            if (!c.active) continue;
            std::string mid = "season_preview_" + c.series_id + "_" + std::to_string(cc.year);
            bool exists = false; for (const auto& e : save.emails) if (e.id == mid) { exists = true; break; }
            if (exists) break;
            std::string rival;
            for (const auto& fd : FieldForSeries(c.series_id))
                if (fd.name != save.player.name) { rival = fd.name; break; }
            GeneratedEmail ge;
            ge.id = mid; ge.tag = 1; ge.day = cc.cursor_doy; ge.year = cc.year; ge.family = 1; ge.subtype = 11;
            ge.payload_key = "season_preview";
            int vc = MailVariantCount("season_preview");
            ge.variant = vc > 0 ? (int)(MailHash(mid) % (unsigned)vc) : 0;
            MailParams p; std::string pf = MailFirstName(save.player.name);
            if (!pf.empty()) p.push_back({"player", pf});
            const SeriesDef* prevSd = SeriesById(c.series_id);         // same display note as the closing mail
            p.push_back({"series", prevSd ? prevSd->display : c.series_id});
            p.push_back({"year", std::to_string(cc.year)});
            if (!rival.empty()) p.push_back({"rival", rival});
            ge.params = MailParamsEncode(p);
            ge.from_name = MailManagerName(); ge.from_role_key = "inbox_role_manager";
            save.emails.push_back(ge);
            break;                                            // one preview per rollover (the primary contract)
        }
    }

    // Living inbox mail: new season, season counters reset to zero + a balance snapshot. AT THE END
    // (after the closing mail above has already read them) -- RolloverYearReport is the SINGLE rollover shared by both the
    // overlay (AdvanceWeek), the simrunner, AND RolloverYear (which delegates here), so a single place covers
    // both paths.
    save.mailnarr.season_podiums = 0;
    save.mailnarr.season_wins = 0;
    save.mailnarr.season_start_credits = prog.credits;

    rep.grids_regenerated = !rep.retirements.empty() || !rep.promotions.empty();
    // 17/09 — THE REPORT IS SAVED. Until today this report was thrown away: the end-of-season screen
    // is painted AFTER the winter and showed three lists written by hand in the code, the same in any
    // game and any year. The TOTAL of each list stays —which is the news, «a hundred and something
    // promotions»— and a short SAMPLE to show names without bloating the game with hundreds of lines.
    {
        const size_t kMuestra = 8;
        SaveData::RolloverSaved& ls = save.last_rollover;
        ls = SaveData::RolloverSaved{};
        ls.year = rep.from_year;
        ls.total_retires = (int)rep.retirements.size();
        ls.total_promos = (int)rep.promotions.size();
        ls.total_moves = (int)rep.market_moves.size();
        for (const auto& m : rep.market_moves) {
            if (ls.moves.size() >= kMuestra) break;
            if (m.name.empty() || m.name == m.driver_id) continue;   // your rule: never an id for a name
            SaveData::RolloverSaved::Move e;
            e.name = m.name; e.from = m.from_team; e.to = m.to_team;
            ls.moves.push_back(std::move(e));
        }
        for (const auto& r : rep.retirements) {
            if (ls.retires.size() >= kMuestra) break;
            // The usual filter was `name.empty()`, and it NEVER fired: the name was the id. Now that
            // the id is not accepted as a name; this really picks the eight that can be named.
            if (r.name.empty() || r.name == r.driver_id) continue;
            SaveData::RolloverSaved::Retire e;
            e.name = r.name; e.age = r.age; e.reason = r.reason;
            ls.retires.push_back(std::move(e));
        }
        for (const auto& p : rep.promotions) {
            if (ls.promos.size() >= kMuestra) break;
            if (p.name.empty()) continue;
            SaveData::RolloverSaved::Promo e;
            e.name = p.name; e.from_tier = p.from_tier; e.to_tier = p.to_tier;
            ls.promos.push_back(std::move(e));
        }
    }
    return rep;
}

void RolloverYear(CareerCalendar& cc, CareerProgress& prog, SaveData& save, const std::string& playerName) {
    (void)RolloverYearReport(cc, prog, save, playerName);   // The canonical rollover now ages the world
}

// Review 0.7.0 (finding 1): the SAME text that goes to disk also goes to memory, to seal the state of the
// clock (AdvanceSeal) without writing files. The lines below are still `fprintf(f, ...)`: `f` is a
// SaveSink and this overload routes it to the FILE* or to the std::string. That way a new format line is written as
// always and enters the seal on its own; `using ::fprintf` keeps the CRT one at hand for any FILE* in the file.
namespace {
struct SaveSink { FILE* file = nullptr; std::string* mem = nullptr; };
}
using ::fprintf;
static int fprintf(SaveSink* s, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = 0;
    if (s->file) {
        n = vfprintf(s->file, fmt, ap);
    } else {
        va_list cp; va_copy(cp, ap);
        n = vsnprintf(nullptr, 0, fmt, cp);
        va_end(cp);
        if (n > 0) {
            const size_t at = s->mem->size();
            s->mem->resize(at + (size_t)n + 1);
            vsnprintf(&(*s->mem)[at], (size_t)n + 1, fmt, ap);
            s->mem->resize(at + (size_t)n);
        }
    }
    va_end(ap);
    return n;
}

static void WriteCalendar(const CareerCalendar& cc, SaveSink* f) {
    // corollary: 4th/5th field = presence latch (doy|circuit); duration: 6th/7th = window (hour|minutes).
    // Old save (3 fields) -> everything 0. Additive compatibility.
    fprintf(f, "C|%d|%d|%d|%ld|%d|%d\r\n", cc.year, cc.cursor_doy, cc.last_race_doy, cc.last_race_circuit,
            cc.last_race_hour, cc.last_race_minutes);
    for (const auto& e : cc.enrollments) {
        fprintf(f, "E|%s|%s|%d|%d|%d|%d|%d|%d\r\n", e.series_id.c_str(), e.branch.c_str(),
                e.season_year, e.round_idx, e.status, e.inscription_paid ? 1 : 0,   // 6th field
                e.livery_ord, e.livery_veh);   // item 5 (skin=team): 7th/8th field = latched skin ordinal + vehHash
        for (const auto& h : e.history)
            fprintf(f, "H|%d|%d|%d|%d\r\n", h.round, h.pos, h.field, h.dnfFlag ? 1 : 0);
        fprintf(f, "champ|%s|%s|%d|%d\r\n", e.standings.node_id.c_str(),
                e.standings.points_system.c_str(), e.standings.season_year, e.standings.round);
        for (const auto& kv : e.standings.drivers)
            for (const auto& dr : kv.second)
                fprintf(f, "r|%s|%d|%d|%d|%d|%d\r\n", kv.first.c_str(), dr.round, dr.pos,
                        dr.points, dr.fl ? 1 : 0, dr.dnf ? 1 : 0);
    }
}

int RenamePlayerInEnrollments(CareerCalendar& cc, const std::string& oldName, const std::string& newName, bool guarded) {
    int changed = 0;
    for (auto& e : cc.enrollments)
        if (guarded ? MergePlayerKeyInStandings(e.standings, oldName, newName)
                    : RenameDriverInStandings(e.standings, oldName, newName)) changed++;
    return changed;
}

std::string SerializeCalendar(const CareerCalendar& cc) {
    std::string out;
    SaveSink sink; sink.mem = &out;
    WriteCalendar(cc, &sink);
    return out;
}

bool SaveCalendar(const CareerCalendar& cc, const char* path) {
    char tmp[512]; snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* fp = paths::FOpen(tmp, "wb");
    if (!fp) { UserLog("save", "calendar save FAILED: cannot write %s (errno=%d)", tmp, errno); return false; }
    SaveSink sink; sink.file = fp;
    WriteCalendar(cc, &sink);
    fclose(fp);
    // same as career.sav -- a one-step replace, never leaving the destination missing in between.
    if (!paths::ReplaceFileAtomic(tmp, path)) {
        UserLog("save", "calendar save FAILED: cannot replace %s (the previous calendar is INTACT)", path);
        return false;
    }
    return true;
}

bool LoadCalendar(CareerCalendar& cc, const char* path) {
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    cc = CareerCalendar{};
    char line[1024];
    SeasonState* cur = nullptr;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        std::vector<std::string> t; { std::string c; for (char* p = line; *p; ++p) {
            if (*p == '\r' || *p == '\n') continue; if (*p == '|') { t.push_back(c); c.clear(); } else c += *p; }
            t.push_back(c); }
        if (t.empty()) continue;
        if (t[0] == "C" && t.size() >= 3) {
            cc.year = atoi(t[1].c_str()); cc.cursor_doy = atoi(t[2].c_str());
            // Real calendar model: legacy saves stored a 1-based year (pre-kStartYear). Maps it to the
            // real year kStartYear+(year-1) so the Gregorian clock gets a valid year. Idempotent
            // (year>=kStartYear left untouched); the day-of-year doesn't change.
            if (cc.year > 0 && cc.year < kStartYear) cc.year = kStartYear + (cc.year - 1);
            // corollary: presence latch + window duration (back-compat: an old 3-field save -> 0).
            cc.last_race_doy     = (t.size() >= 4) ? atoi(t[3].c_str()) : 0;
            cc.last_race_circuit = (t.size() >= 5) ? atol(t[4].c_str()) : 0;
            cc.last_race_hour    = (t.size() >= 6) ? atoi(t[5].c_str()) : 0;
            cc.last_race_minutes = (t.size() >= 7) ? atoi(t[6].c_str()) : 0;
        }
        else if (t[0] == "E" && t.size() >= 6) {
            SeasonState s;
            s.series_id = t[1]; s.branch = t[2]; s.season_year = atoi(t[3].c_str());
            s.round_idx = atoi(t[4].c_str()); s.status = atoi(t[5].c_str());
            // 6th field, inscription_paid. Old save (5 fields, t.size()==6) -> true (it already paid
            // under the old model, which charged at Enroll). New save (t.size()>=7) -> the stored flag.
            s.inscription_paid = (t.size() >= 7) ? (atoi(t[6].c_str()) != 0) : true;
            // item 5 (skin=team): 7th/8th field = ordinal+vehHash of the latched livery. Old save (<8 fields)
            // -> no latch (-1/0): you'll use whatever skin until you lock one in on your first race.
            s.livery_ord = (t.size() >= 8) ? atoi(t[7].c_str()) : -1;
            s.livery_veh = (t.size() >= 9) ? atoi(t[8].c_str()) : 0;
            const SeriesDef* d = SeriesById(s.series_id);
            s.standings.node_id = s.series_id;
            s.standings.points_system = d ? d->points_id : std::string();
            cc.enrollments.push_back(s);
            cur = &cc.enrollments.back();
        }
        else if (t[0] == "H" && t.size() >= 5 && cur) {
            cur->history.push_back(RoundOutcome{ atoi(t[1].c_str()), atoi(t[2].c_str()),
                                                 atoi(t[3].c_str()), atoi(t[4].c_str()) != 0 });
        }
        else if (t[0] == "champ" && t.size() >= 5 && cur) {
            cur->standings.node_id = t[1]; cur->standings.points_system = t[2];
            cur->standings.season_year = atoi(t[3].c_str()); cur->standings.round = atoi(t[4].c_str());
        }
        else if (t[0] == "r" && t.size() >= 7 && cur) {
            DriverRound dr; dr.round = atoi(t[2].c_str()); dr.pos = atoi(t[3].c_str());
            dr.points = atoi(t[4].c_str()); dr.fl = atoi(t[5].c_str()) != 0; dr.dnf = atoi(t[6].c_str()) != 0;
            cur->standings.drivers[t[1]].push_back(dr);
        }
    }
    fclose(f);
    return true;
}

static bool isSportscarDisc(const std::string& d) { return d == "endurance" || d == "gt"; }

// the series in the meeting, since that's where both the classes selected in the game and the
// requested car count come from. This used to return only class hashes, and the grid size was
// computed from the PLAYER's own class: on a IVORY_CUP_12573 weekend it would request all 18 Pemberton_Zel121 cars for a grid that's actually
// three classes. With the series on hand, the size is set by the whole meeting (MeetingGridSize).
// Order: yours first; dedup BY CLASS (two series of the same class don't count twice in the split).
std::vector<std::string> MeetingSeriesSet(const std::vector<CalSlot>& all, const CalSlot& chosen) {
    std::vector<std::string> set;
    const SeriesDef* csd = SeriesById(chosen.series_id);
    if (!isSportscarDisc(DiscForSeries(csd))) return set;              // the player isn't in a sportscar class -> single-class
    unsigned pcls = ClassHashForName(csd ? csd->class_name : chosen.series_id);
    // Curated multiclass: only merges co-located series from the SAME curated group (SameMulticlassGroup).
    // Co-location alone (same doy+track) ISN'T enough: separate championships sharing a circuit+weekend
    // (IVORY_CUP_12573 vs NIMBUS_MASTERS_5890 at Ashwick Ring, Group C at Highgate, support races) stay separate. See career_series (groups).
    bool meeting = false;
    for (const auto& t : all) {
        if (t.doy != chosen.doy || t.track_hash != chosen.track_hash) continue;
        if (t.series_id == chosen.series_id) continue;
        if (!SameMulticlassGroup(chosen.series_id, t.series_id)) continue;   // curated: same group or nothing
        const SeriesDef* td = SeriesById(t.series_id);
        unsigned th = ClassHashForName(td ? td->class_name : t.series_id);
        if (th && th != pcls) { meeting = true; break; }
    }
    if (!meeting) return set;                                          // no distinct group-mate -> single-class
    std::vector<unsigned> seen;
    if (pcls) { set.push_back(chosen.series_id); seen.push_back(pcls); }   // your own series first
    for (const auto& t : all) {
        if (set.size() >= 6) break;
        if (t.doy != chosen.doy || t.track_hash != chosen.track_hash) continue;
        if (t.series_id == chosen.series_id) continue;
        if (!SameMulticlassGroup(chosen.series_id, t.series_id)) continue;   // curated
        const SeriesDef* td = SeriesById(t.series_id);
        unsigned th = ClassHashForName(td ? td->class_name : t.series_id);
        if (!th) continue;
        bool dup = false; for (unsigned x : seen) if (x == th) dup = true;
        if (!dup) { set.push_back(t.series_id); seen.push_back(th); }
    }
    return set;
}

std::vector<std::string> MulticlassMeetingAt(const std::string& seriesId, int round) {
    std::vector<std::string> set;
    const int g = MulticlassGroupOf(seriesId);
    if (g < 0) return set;                                             // a series with no curated group -> single-class
    const std::vector<CalRound> mine = CalendarForSeries(seriesId);
    const CalRound* me = nullptr;
    for (const auto& r : mine) if (r.round == round) { me = &r; break; }
    if (!me) return set;
    const SeriesDef* csd = SeriesById(seriesId);
    const unsigned pcls = ClassHashForName(csd ? csd->class_name : seriesId);
    std::vector<std::string> otras; std::vector<unsigned> seen;
    if (pcls) seen.push_back(pcls);
    for (const auto& other : MulticlassGroups()[g]) {
        if (other == seriesId) continue;
        if (set.size() + otras.size() >= 6) break;
        bool coincide = false;
        for (const auto& r : CalendarForSeries(other))
            if (r.doy == me->doy && r.track_hash == me->track_hash) { coincide = true; break; }
        if (!coincide) continue;
        const SeriesDef* od = SeriesById(other);
        const unsigned oh = ClassHashForName(od ? od->class_name : other);
        if (!oh) continue;
        bool dup = false; for (unsigned x : seen) if (x == oh) dup = true;
        if (dup) continue;                                             // same class as one already present: doesn't merge
        otras.push_back(other); seen.push_back(oh);
    }
    if (otras.empty()) return set;                                     // no distinct group-mate -> single-class
    if (pcls) set.push_back(seriesId);                                 // your own series first
    for (auto& o : otras) set.push_back(std::move(o));
    return set;
}

int RoundGridSizeOverride(const std::vector<std::string>& cita,
                          const std::string& seriesId, int round) {
    const std::vector<CalRound> mine = CalendarForSeries(seriesId);
    const CalRound* anchor = nullptr;
    for (const auto& r : mine) if (r.round == round) { anchor = &r; break; }
    if (!anchor) return 0;

    // If it is a multiclass meeting, ALL rows of the curated group that match on day+track are inspected,
    // even a duplicate series of the same class as `cita` that was deduplicated for the split. That way a hand-edited
    // PSV cannot hide a contradiction by picking another companion.
    const int group = cita.size() > 1 ? MulticlassGroupOf(seriesId) : -1;
    int requested = group < 0 ? anchor->gridSize : 0;
    bool inheritedCompanion = false;
    if (group >= 0) {
        for (const auto& sid : MulticlassGroups()[(size_t)group]) {
            for (const auto& r : CalendarForSeries(sid)) {
                if (r.doy != anchor->doy || r.track_hash != anchor->track_hash) continue;
                if (r.gridSize <= 0) { inheritedCompanion = true; continue; }
                if (requested > 0 && requested != r.gridSize) return 0; // conflict -> common inheritance
                requested = r.gridSize;
            }
        }
    }
    if (requested > 0 && inheritedCompanion) return 0;            // exception not propagated to the whole meeting
    const int classes = cita.size() > 1 ? (int)cita.size() : 1;
    if (requested > 0 && requested / classes < 2) return 0;       // never a class of 0/1 cars
    return requested;
}

// see career_calendar.h. Two passes so that the usual rule (your series, the nearest race wins and the
// tie is won by the first) comes out intact: the meeting's classes only enter where there was no owner or where their
// race is STRICTLY earlier.
std::map<std::string, std::string> ClassOwnersForUpcomingRaces(const std::vector<CalSlot>& slots) {
    std::map<std::string, std::string> owner;
    std::map<std::string, int> soonest;
    auto pendiente = [](const CalSlot& s) { return s.enrolled && !s.raced && !s.past; };
    for (const auto& s : slots) {
        if (!pendiente(s)) continue;
        const SeriesDef* sd = SeriesById(s.series_id);
        if (!sd) continue;
        const auto it = soonest.find(sd->class_name);
        if (it != soonest.end() && it->second <= s.doy) continue;
        soonest[sd->class_name] = s.doy;
        owner[sd->class_name] = s.series_id;
    }
    for (const auto& s : slots) {
        if (!pendiente(s)) continue;
        for (const auto& otra : MulticlassMeetingAt(s.series_id, s.round)) {
            if (otra == s.series_id) continue;
            const SeriesDef* od = SeriesById(otra);
            if (!od) continue;
            const auto it = soonest.find(od->class_name);
            if (it != soonest.end() && it->second <= s.doy) continue;
            soonest[od->class_name] = s.doy;
            owner[od->class_name] = otra;
        }
    }
    return owner;
}

std::vector<unsigned> MeetingClassSet(const std::vector<CalSlot>& all, const CalSlot& chosen) {
    std::vector<unsigned> set;
    for (const auto& sid : MeetingSeriesSet(all, chosen)) {
        const SeriesDef* d = SeriesById(sid);
        unsigned h = ClassHashForName(d ? d->class_name : sid);
        if (h) set.push_back(h);
    }
    return set;
}

std::vector<CalSlot> BuildMasterCalendar(const CareerCalendar& cc, int frontierPhase) {
    return BuildMasterCalendar(cc, frontierPhase, nullptr, nullptr, nullptr);
}

std::vector<CalSlot> BuildMemoryCalendar(const CareerCalendar& cc,
                                         const std::map<std::string,int>& clearedCount,
                                         double playerRating) {
    std::vector<CalSlot> out;
    for (const auto& m : AllMemories()) {
        // level half of the double-gate: only unlocked (Available) or already-Completed memories surface.
        MemoryStatus st = MemoryStatusOf(m, clearedCount, playerRating);
        if (st == MemoryStatus::Locked) continue;
        for (const auto& r : m.rounds) {
            CalSlot s;
            s.series_id  = m.id;                  // a memory's "series" is the memory itself
            s.track_name = r.track_name;
            s.track_hash = r.track_hash;
            s.round      = r.round;
            s.doy        = r.doy;
            s.date       = DoyToDate(r.doy);
            s.is_memory  = true;
            s.memory_id  = m.id;
            s.valid      = true;
            // date half of the double-gate: locked until the SHARED clock reaches the round's doy.
            s.locked     = (cc.cursor_doy < r.doy);
            s.past       = (cc.cursor_doy >= r.doy) && (st == MemoryStatus::Completed);
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const CalSlot& a, const CalSlot& b) {
        if (a.doy != b.doy) return a.doy < b.doy;
        if (a.memory_id != b.memory_id) return a.memory_id < b.memory_id;
        return a.round < b.round;
    });
    return out;
}

bool GateUnlockOnResult(const SeriesDef* q, int playerPos, std::set<std::string>& gates) {
    if (!q || q->unlocks.empty() || q->gate_top <= 0) return false;   // not a qualifier series
    if (playerPos <= 0 || playerPos > q->gate_top) return false;      // DNF/invalid, or outside the top-N
    if (gates.count(q->unlocks)) return false;                        // already open
    gates.insert(q->unlocks);
    return true;
}

bool GateUnlockOnSeason(const SeriesDef* finished, int finalPos, std::set<std::string>& gates) {
    if (!finished || finalPos <= 0) return false;
    bool any = false;
    for (const auto& sd : AllSeries()) {
        if (sd.gate_feeder_class.empty() || sd.gate_feeder_top <= 0) continue;   // no season-based path
        if (sd.gate_feeder_class != finished->class_name) continue;              // the class isn't a feeder
        if (finalPos > sd.gate_feeder_top) continue;                             // outside the top-N
        if (gates.count(sd.id)) continue;                                        // already open
        gates.insert(sd.id); any = true;
    }
    return any;
}

} // namespace career
