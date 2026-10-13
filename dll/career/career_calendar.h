#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <functional>          // RacedGridProvider: the XML cast the overlay installs (see SimulateRound)
#include "career_customai.h"   // GridDriver (the type that provider returns)
#include "career_season.h"     // SeasonState, SeasonOutcome, FinishSeason, PlanRound...
#include "career_engine.h"     // CareerProgress
#include "career_series.h"     // SeriesById, CalendarForSeries, AllSeries
#include "career_results.h"    // ClassifiedEntry
#include "career_econ.h"       // SeriesEcon (economy per category)

namespace career {

// forward decl: RolloverYear touches SaveData (rival_form/contract/completed_seasons/completed_races) by reference.
// Declared here, with career_save.h included only in the .cpp -> we don't drag persistence into the header
// (and avoid an include cycle with career_save.h, which already includes career_engine.h).
struct SaveData;

// A master-calendar slot = one round of one series, annotated with runtime state.
struct CalSlot {
    std::string series_id, track_name, date, note;
    int round = 0, doy = 0;
    long track_hash = 0;
    int fmt = 0, laps = 0, durationMin = 0, hour = 12;
    std::vector<int> weather;   // copy of CalRound.weather (empty = the player decides)
    int level = 1;
    bool enrolled = false, locked = false, raced = false, dnf = false, conflict = false, past = false;
    // corollary: "one race per day". You raced TODAY at a different circuit -> you can't be here (travel
    // is impossible). Flagged in BuildMasterCalendar for TODAY's slots (doy==cursor==last_race_doy) whose
    // circuit != the one you raced. Exception: same circuit at another time = a same-venue doubleheader -> NOT blocked.
    bool travel_locked = false;
    bool gate_locked = false;          // blocked by a GATE (in addition to level); locked will also be true
    std::string gate_qualifier;        // series_id of the qualifier series that unlocks it ("" if not applicable)
    // hub a CLOSED-grid series (series_access) without an ACTIVE contract -> not launchable ("by
    // invitation"). Resolved by the overlay when it builds the view (needs the save: contracts), NOT
    // BuildMasterCalendar. The term already lived in the native overlay's open (screen_calendar:1158) and got
    // lost porting the calendar to the app: 14 closed series painted as available and the launch died
    // SILENTLY. Memories stay exempt (their access is the CAR, not the contract).
    bool invite_locked = false;
    // the round's track is NOT installed (missing DLC). locked will also be true. dlc_pack = the slug of the
    // missing pack (circuits_ref's dlc column; "" if unknown). Fail-open: with no live catalog, it's never flagged.
    // dlc_locked is reserved for (a) an entirely unplayable series (a class with no cars) and (b) a missing track with NO
    // possible substitute. If a substitute exists, the round is NOT blocked: it's raced on the substitute track.
    bool dlc_locked = false;
    std::string dlc_pack;
    // track substitution for missing DLC. track_hash/track_name become
    // the substitute (what the race actually runs on); orig_* keeps the curated track for the badge/hover.
    bool dlc_subbed = false;
    long orig_track_hash = 0;
    std::string orig_track_name;
    std::string dlc_product;    // display name of the missing purchasable product ("" if unknown)
    bool valid = false;        // false = slot doesn't exist (e.g. a PendingSlot for a series that's already finished)
    // An era-memory round. is_memory rounds are surfaced via the legacy section (family-tree
    // screen), NEVER mixed into BuildMasterCalendar's modern listing -- but they SHARE this clock/doy
    // (the date-gate). memory_id ties the slot back to its Memory; "" for a normal modern slot.
    bool is_memory = false;
    std::string memory_id;
    // Per-round session-duration overrides forwarded from CalRound. 0 = use the engine's fixed default.
    int pitKmh = 0;        // >0 = a pit-lane limit specific to THIS track (Blackgate Speedway); 0 = the profile default applies
    int practiceMin = 0;   // >0 = explicit practice minutes for this round
    int qualifyMin  = 0;   // >0 = explicit qualify minutes for this round
    int gridSize = 0;      // total requested for the meeting; 0 = inherited
    // a round injected from special_events.json. event_id links back to the
    // SpecialEventDef in the registry (rewards on settle); tier_min is INFORMATIONAL for the app
    // (the real gating already happened server-side: without the right tier no payload arrives, so there's nothing to inject).
    bool special = false;
    std::string event_id;
    std::string tier_min;
};

struct CareerCalendar {
    int year = 1;
    int cursor_doy = 0;
    // corollary: where/when you raced your LAST race. Used for "one race per day": after racing,
    // every other race that day at a DIFFERENT circuit becomes unreachable (you can't be in the UK and Brazil at the
    // same time). It only takes effect while cursor_doy==last_race_doy (today); once the clock advances, that day's races move
    // to `past` and this goes inert. Cleared at the year rollover. 0 = haven't raced yet (or it's a new year).
    int  last_race_doy = 0;
    long last_race_circuit = 0;             // circuit_hash of the last circuit raced (0 = none / a standalone race with no track)
    // (duration nuance): time window of the last race raced (start hour + estimated minutes),
    // for the same-venue doubleheader check: a 2nd race at the same circuit on the same day is only reachable if its window doesn't
    // overlap the one you already raced (a 66-lap race doesn't leave time for another). hour 0..23, minutes = estimated duration.
    int  last_race_hour = 0;
    int  last_race_minutes = 0;
    std::vector<SeasonState> enrollments;   // one per series entered this year
};

// The career clock is now an absolute, real (year, doy). A NEW career now starts at
// (kStartYear, RookieOpenerDoy()): a real year plus the level-1 calendar's opener (not year=1/doy=0). Existing
// saves load their stored year/doy as-is (back-compat); only a brand-new career uses these values.
constexpr int kStartYear = 2025;
// Rookie calendar opener: the earliest doy>0 among the calendars of ALL
// level-1 series (the entry tier). Derived from the data (not pinned to a specific month). Deterministic. 0 if there's
// no dated level-1 round at all (data not loaded -> a new career would fall back to doy 0, the old behavior).
int RookieOpenerDoy();

// The day a career is born. January 1st, and NOT the rookie calendar's
// opener (RookieOpenerDoy), for a reason MEASURED on 05/08: the earliest level-1 opener falls on day 55
// (karting, February 24th), while year 1's compressed decision week falls on day 46 (February 15th).
// Starting at the opener, a career would begin NINE DAYS AFTER its own signing window had
// already resolved: the player would show up already in the paddock phase, with their own silly season already over, never getting to see it
// even once. The entire compressed year-1 cycle was unreachable in production.
//
// January 1st is what was said from the very start ("you're born in preseason, the window has been open
// since day one"); this just makes it true. Preseason stops being a figure of speech and becomes actually playable:
// you look at the paddock, you apply, and it's all decided by February 15th, before your first race even runs.
constexpr int kCareerStartDoy = 1;

// a series' calendar projected onto a real year. Takes CalendarForSeries (the reference calendar
// for kStartYear=2025) and realigns every round with doy>0 to the same weekend of 'year' (RealignDoy); year==kStartYear
// is the identity case. Rounds with no date (doy<=0) stay untouched. Sorted by the realigned doy. Pure. It's the source that
// BuildMasterCalendar and SlotForRound consume for the current year.
std::vector<CalRound> ProjectSeason(const std::string& seriesId, int year);

// Series calendar with dates PROJECTED onto a given year (ProjectSeason). Use it on EVERY code path in the
// game that compares doy/date against the live clock; CalendarForSeries (raw, based on kStartYear) is kept for
// data/parity purposes. At kStartYear it's the identity. Wraps ProjectSeason with a memo cache keyed by (seriesId, year):
// it gets called in loops over the ~115 series in the catalog (BuildMasterCalendar and every future migrated path),
// and ProjectSeason re-aligns and re-sorts the entire calendar on every call.
// MANDATORY INVALIDATION: see the comment next to the cache in career_calendar.cpp for why. In
// short, the cache MUST be cleared at any point that replaces the series catalog or the base calendar
// -- LoadSeries/ReplaceSeries/LoadSeriesCalendar/ReplaceCalendar (career_series.cpp) do this by calling
// InvalidateCalendarProjectionCache() right after replacing their table.
std::vector<CalRound> CalendarForSeriesInYear(const std::string& seriesId, int year);
// Clears the CalendarForSeriesInYear cache. Call it at ANY point that replaces the base catalog of
// series or calendar (see career_series.cpp) -- without this, a reload serves STALE dates (cross-contamination
// between tests that reload different PSVs; in-game, stale dates after any hot reload of data at
// runtime). Exposed in the header (not static) because career_series.cpp calls it from another translation unit.
void InvalidateCalendarProjectionCache();

// Port 0.7.0 (F22) — THE DATA EPOCH. A counter that goes up every time the DATA under the engine changes:
// the series catalog or the base calendar (done by `InvalidateCalendarProjectionCache`, which is called by
// LoadSeries / ReplaceSeries / LoadSeriesCalendar / ReplaceCalendar) and the end of `LoadSeasonData`.
//
// It exists for the performance memos in `career_seats` (StayingDrivers and the data part of SeatsForClass):
// their answers depend on the catalog, and a key that does not look at it answers with stale data as soon as something
// reloads. In f0 that role was played by `AffinityEpoch()`, which lives in `career_affinity` -- a contracts module
// that does not exist here. Measured in the port lab: with the epoch PINNED to 0 five team-name tests fail;
// with the counter really going up, they all pass.
//
// It does not pretend to be a content signature: it only has to CHANGE when the data changes. It lives here, next to the
// engine's other global invalidation, and not in career_seats, so that whoever replaces a table sees it right next to
// the cache they were already obliged to clear.
unsigned long long DataEpoch();
void BumpDataEpoch();

// query ---
// unlockedGates = championships opened via a gate (save): contains the series_id of the TARGET CHAMPIONSHIP
// (e.g. "ivory_trophy_7202"), NOT the qualifier series' id. nullptr = no state -> gated ones stay locked.
// 01/10 (jse67, 0.7.0): contractSeries = series of the player's ACTIVE contracts. The transfer window
// deliberately offers seats one level above the phase (career_contract.cpp, orbit fp+1) and the player
// can sign them; without this, the calendar locked them by level and the signed series came out as "locked".
// A contract IS the invitation: it lifts the level lock of ITS series (not the gate lock or the DLC one).
// nullptr = no contracts (compat/tests).
std::vector<CalSlot> BuildMasterCalendar(const CareerCalendar& cc, int frontierPhase,
                                         const std::set<std::string>* unlockedGates,
                                         const std::set<std::string>* ownedClasses = nullptr,
                                         const std::set<std::string>* contractSeries = nullptr);
std::vector<CalSlot> BuildMasterCalendar(const CareerCalendar& cc, int frontierPhase);   // compat: delegates with nullptr,nullptr
// The series of the save's active contracts, to pass them to BuildMasterCalendar.
std::set<std::string> ContractedSeries(const SaveData& save);
// The date-gated rounds of UNLOCKED era memories, as CalSlots tagged is_memory. SEPARATE from
// the modern master calendar (memories live in the Legacy section, not the calendar listing) but on the
// SHARED clock: a round's slot is `locked` until cursor_doy reaches its doy. `clearedCount` + `playerRating`
// gate which memories are unlocked (level half); the per-round doy is the date half. Requires LoadMemories.
std::vector<CalSlot> BuildMemoryCalendar(const CareerCalendar& cc,
                                         const std::map<std::string,int>& clearedCount,
                                         double playerRating);
SeasonState*       FindEnrollment(CareerCalendar& cc, const std::string& seriesId);
const SeasonState* FindEnrollment(const CareerCalendar& cc, const std::string& seriesId);
bool IsEnrolled(const CareerCalendar& cc, const std::string& seriesId);
// Results: the championship shown after a race is the one for the SERIES THAT JUST RACED
// (race_log.back().series_id), NEVER the series SELECTED in the UI. Returns the enrollment for
// raceSeriesId only if it exists AND has a populated standings table; nullptr if the race was a one-off (empty id),
// if its series isn't enrolled, or if there's no table yet -> Results shows no panel, which is the honest
// answer. It used to be resolved as "the last enrollment with a table", which is why after racing Formula Vee
// the championship for a different series would show up. Pure and testable.
const SeasonState* StandingsForRace(const CareerCalendar& cc, const std::string& raceSeriesId);
// PRIVATEER RULE (ruling, 28/07): being a privateer carries NO penalty at all. What actually gets
// penalized is racing for TEAM A and switching to TEAM B **mid-season** (also paying the
// rental fee). In other words: it punishes INCONSISTENCY within a season, never the act of renting a seat.
// That's why the split is measured against the SEASON LATCH (enrollment.livery_veh/livery_ord, which dies at
// rollover), not against the DURABLE pick in the save: switching teams BETWEEN seasons is a normal transfer and
// costs nothing. season* = the identity already raced this season (0/-1 = none yet -> free to change). Pure.
bool IsMidSeasonIdentitySwitch(int seasonVeh, int seasonOrd, int newVeh, int newOrd);
// access enforcement: can the player ENROLL as a privateer in this series? true for OPEN series and
// for unknown ones (fail-open). For a CLOSED series (SeriesDef.closed) only true if there's an ACTIVE contract
// for that series (ContractForSeries). Pure (reads g_series + the save; doesn't mutate). Consumed by the calendar launch flow.
bool CanEnterSeries(const SaveData& save, const std::string& seriesId);
// Are ALL enrolled seasons finished? true only if there's >=1 enrollment AND all of them are
// closed (status==1 OR their calendar ran to completion: round_idx >= number of rounds). No enrollments -> false.
// This no longer triggers the rollover (the clock does it automatically when crossing the year, see AdvanceTime). It remains
// as a pure helper (useful elsewhere for a year-end summary). Pure and testable.
bool AllSeasonsComplete(const CareerCalendar& cc);
CalSlot PendingSlot(const CareerCalendar& cc, const std::string& seriesId);

// conflict ---
// occupancy footprint of an event in calendar days around the race day (lead=before,
// trail=after). Governs the "you can't be in two places" window for travel conflicts: F1 occupies the
// whole weekend (Fri-Sun), a regional cup only the day itself. Defaults by the series' tier/profile; an
// unknown series -> (1,0). Pure.
struct Footprint { int lead = 1; int trail = 0; };
Footprint FootprintForSeries(const std::string& seriesId);
// bug reported (05/08): the footprint is for THE ROUND, not the series. FootprintForSeries used to decide the enduro case by checking whether
// ANY round of the series runs >=24h, so a single 24h round spread the "long week" footprint to all
// the other rounds (199 of 221 rounds across 22 series, including aurora_trophy_11261/granite_championship_11220, which are CLUB events). Here it's
// decided by THIS slot's own duration; the tier is unchanged. Use this version whenever you have the round at hand. Pure.
Footprint FootprintForSlot(const CalSlot& s);
// Calendar days a slot OCCUPIES, INCLUSIVE on both ends: [loDoy, hiDoy] = [doy-lead, doy+trail].
// SOLE source of that window: used by SlotsConflict (which produces the overlap DNF) and carried into the slot's
// JSON (winLoDoy/winHiDoy) so the app's clash warning can NEVER diverge from the engine -- the app used to warn
// only about the SAME day while the engine was DNF-ing the whole window, so the player lost rounds with no warning. Pure.
void SlotOccupiedDays(const CalSlot& s, int& loDoy, int& hiDoy);
// (duration nuance): minutes a race OCCUPIES. durationMin if >0; if lap-based, laps x ~2min
// (rough estimate, TUNABLE); if neither is set, a default. Used for the time-window overlap in a
// same-venue doubleheader (same day+track): two races are only compatible if their time windows don't overlap.
int EstimatedRaceMinutes(const CalSlot& s);
bool SlotsConflict(const CalSlot& a, const CalSlot& b);
std::vector<CalSlot> ConflictsFor(const CareerCalendar& cc, const CalSlot& chosen);
// (tester feedback 24/07: "when picking in the market it says there's no conflict, but the enrolled calendar
// has races on the same day"). ConflictsFor only looked at the PENDING round of each enrollment, so it
// answers "what clashes THIS weekend", not "do these two seasons clash at all". At the start of a
// career every series is on round 1 (different days) -> no warning, and the clash would show up later, mid-
// season, on the calendar. This instead compares the FULL calendars (already-run rounds and past days
// excluded) so it can be flagged BEFORE signing. Pure over the calendar data.
struct SeasonClash {
    std::string series_id;   // already-enrolled series that clashes
    int rounds = 0;          // number of rounds that overlap
    int firstDoy = 0;        // first day of the clash (for ordering/warning)
};
std::vector<SeasonClash> SeasonClashesFor(const CareerCalendar& cc, const std::string& seriesId);

// Real dates + temporal navigation for the schedule (pure, stateless; testable) ---
// doy (1..366) -> "DD Mon" in the Gregorian calendar (non-leap: 1=Jan 1, 60=Mar 1, 365=Dec 31). Deterministic.
// Out of range -> "" (doy 0 = no date). This is the visible face of the continuous clock in the schedule spine.
std::string DoyToDate(int doy);
// Real-time model: an actual date with weekday, "Www DD Mon" (EN), leap-aware.
// year + doy (1-based, doy 1 = Jan 1). doy<1 -> "". This is the visible face of the Gregorian clock (replacing
// DoyToDate, which ignored the year and leap years. The overlay/app wiring uses this.
std::string DateLabel(int year, int doy);
// the real weekday (EN, "Sun".."Sat") for a (year, doy) pair. "" if doy<1. Additive field for the UI
// (shown alongside the `date` string "Mon DD"; it does NOT replace it, so as not to break date parsers).
std::string WeekdayLabel(int year, int doy);
// real per-session dates for a slot within its weekend (schedule/UI data, consumed by the UI).
// Race on the slot's day; qualifying the day before; practice at the start of the weekend (doy - footprint.lead).
// Footprint (0,0) -> all three coincide (single-day event). In AMS2 it's all run in a single launch.
struct SessionDates { std::string practice, qualify, race; };
SessionDates SessionDatesForSlot(const CalSlot& s, int year);
// number of WEEKS of advance-time needed to reach the next PENDING event (the open/enrolled slot not yet run,
// the nearest one with doy>cursor). 0 = you're already at/past the next event (or none remain). The caller
// passes it to CalUiAdvanceWeek for the "skip to next event" button. 'slots' = the master calendar already
// built (BuildMasterCalendar). Counts FULL weeks rounding up (ceil) so it never falls short. Pure.
int WeeksToNextEvent(const std::vector<CalSlot>& slots, int cursorDoy);

// gate qualifier ---
// Evaluates a completed gate qualifier: if playerPos is within the top-N (<= gate_top), marks its championship
// (unlocks) as unlocked in 'gates'. Returns true ONLY if it unlocks it NOW (newly). Retries are
// allowed: failing (pos>top, DNF/pos<=0) doesn't penalize or close anything off.
bool GateUnlockOnResult(const SeriesDef* qualifier, int playerPos, std::set<std::string>& gates);

// Season-based variant: when closing a 'finished' season with final position 'finalPos', unlocks
// ALL championships whose gate_feeder_class == the class of 'finished' (any series/branch of that class
// counts) and finalPos <= gate_feeder_top. true if it unlocked AT LEAST one new one. Idempotent; never penalizes.
bool GateUnlockOnSeason(const SeriesDef* finished, int finalPos, std::set<std::string>& gates);

// multiclass meeting (co-location rule) ---
// Set of class hashes for 'chosen''s multiclass meeting: entries co-located (same doy+track_hash)
// in the sportscar discipline (endurance/gt), and ONLY if the meeting includes an 'endurance' entry. 'chosen' comes first.
// Returns <=1 element if it's NOT multiclass. Capped at 6. Requires LoadClassHash to be loaded (career_customai).
std::vector<unsigned> MeetingClassSet(const std::vector<CalSlot>& all, const CalSlot& chosen);

// The SERIES in that same meeting ('chosen''s first, deduped by class). MeetingClassSet is derived from this:
// it's the same computation, but returning who's racing rather than just which class, which is what's needed to know
// how many cars the weekend calls for (career_fields: MeetingGridSize / MeetingClassShare).
std::vector<std::string> MeetingSeriesSet(const std::vector<CalSlot>& all, const CalSlot& chosen);
// Common exception of the meeting. Ignores 0 (inherited); distinct positives or <2 cars/class -> 0.
// `cita` holds the deduplicated classes that share the grid; `round` locates the chosen row.
int RoundGridSizeOverride(const std::vector<std::string>& cita,
                          const std::string& seriesId, int round);

// The SAME meeting, derived from the CALENDAR instead of the master's slots: the simulation and the round plan don't
// have the slot list on hand, only the series and the round. Same criterion (same doy+track, same curated
// group, different class). Empty = that round is single-class -- and there are some: measured 06/08, 5 of the 15 series in the
// group also run standalone rounds besides the meeting ones (pinnacle_series_4783: 9 standalone out of 10).
std::vector<std::string> MulticlassMeetingAt(const std::string& seriesId, int round);

// — WHOSE XML EACH CLASS GETS at the next boot (class -> series_id that writes it). AMS2 loads ONE
// XML per class, and several classes are run by two or more series: the owner is the series of your NEXT pending race
// (entered, not run, not passed) in which that class goes on track. 19/09: "goes on track" includes the OTHER classes
// of the multiclass meeting (MulticlassMeetingAt). Without that, on a IVORY_CUP_12573 weekend the GT3 ran with the XML of its main series
// (NIMBUS_MASTERS_5890 CINDER_SERIES_5357), and whoever runs LMGT3 saw the Hypercar with the NIMBUS_MASTERS_5890 PINNACLE_CHALLENGE_5480 one. The classes of your own series win the day
// tie over those of the meeting; between two of the same kind, the first in `slots` order. Classes with no race: they do not appear.
std::map<std::string, std::string> ClassOwnersForUpcomingRaces(const std::vector<CalSlot>& slots);

// economy (by CATEGORY via ams2_series_econ.psv; requires LoadSeriesEcon to be loaded) ---
// A series' economy row (SeriesById -> class_name -> FindEconByClassName); nullptr if it doesn't resolve.
const SeriesEcon* EconForSeries(const std::string& seriesId);
long InscriptionFor(const std::string& seriesId);       // per season: INSCRIPTION_BASE[level] (baseline derived from research).
long RoundWinnerPrize(const std::string& seriesId);     // winner's purse BY CATEGORY (ec->premio; heritage = era purse, EXEMPT from the floor); fallback ROUND_PURSE[level]
long RoundWinnerPrizeDisplay(const std::string& seriesId); // prize to DISPLAY (never 0; safety net when there's no econ row). DISPLAY ONLY, NOT settlement.
int  AudienceForRace(int level, int fmt);                  // audience 0-100 (tier+endurance). BASE model (fallback).
// part 3: CURATED audience per round. Loads docs/career/audience.json ({series_id:{round:0-100}}) once;
// returns the number of (series, round) pairs loaded. careerDir = the career data root (paths::Data("career")). The
// overlay calls it on startup, after LoadCareerDataFromJson. Without that call, AudienceForRaceEx falls back to the BASE model.
int  LoadAudienceData(const char* careerDir);
// audience for a SPECIFIC round: the curated value from audience.json if present (series+round), otherwise the BASE model
// AudienceForRace(level,fmt). This is the path both EMIT (query.calendar) and SETTLE (rating settlement) should use.
int  AudienceForRaceEx(const std::string& seriesId, int round, int level, int fmt);
// test seam: injects/clears curated entries without touching disk (career_test stays deterministic without audience.json).
void SetAudienceEntryForTest(const std::string& seriesId, int round, int audience);
void ClearAudienceForTest();
double AudienceRepFactor(int audience);                    // ELO/rep multiplier from audience, UPWARD ONLY (1.0..1.25)
long EntryFeeFor(const std::string& seriesId);          // entry fee per race (ec->entry); fallback EntryForPhase(level)

// 2026-07-01 spec + 11/07 feedback: the OUTSTANDING enrollment fee for a series = InscriptionFor if it's NOT enrolled yet
// (it will be enrolled and charged when its first race SETTLES) or if it's enrolled but unpaid; 0 if already paid /
// covered (waived). Used by the car UI for the "Entry fee" line and the Option A gate.
long InscriptionDue(const CareerCalendar& cc, const std::string& seriesId);
// 2026-07-01 spec: charges the outstanding enrollment fee at the COMMIT-to-race point (once only): debits prog.credits,
// marks inscription_paid=true. Idempotent (already paid / not enrolled -> 0, credits untouched). Returns what
// was charged. Does NOT require a minimum balance (the UI gate already guaranteed that); it never drives credits negative on its own.
long ChargeInscription(CareerCalendar& cc, const std::string& seriesId, CareerProgress& prog);

// enrollment + race ---
struct EnrollResult { bool ok = false; bool already = false; long paid = 0; };
// 2026-07-01 spec: the normal path no longer charges or requires a balance -- it only creates the record with inscription_paid=false
// (the fee is charged at commit time via ChargeInscription). waiveInscription: the team covers the
// entry -> record with inscription_paid=true, no charge. r.paid stays 0 on both paths.
EnrollResult Enroll(CareerCalendar& cc, const std::string& seriesId, CareerProgress& prog,
                    bool waiveInscription = false);
// re-applies the privateer's DURABLE pick (save.privateer_pick) to the series' enrollment if its
// latch isn't set yet. Call it after Enroll (the race commit and the rollover's re-enrollment): that way the
// car+skin "marriage" survives from season to season. No-op with no pick, or with the latch already set.
void ApplyPrivateerPick(CareerCalendar& cc, const std::string& seriesId, const SaveData& save);
void PrepareToRace(CareerCalendar& cc, const std::string& seriesId, int round, const std::string& playerName,
                   SaveData* save = nullptr);
// 11/07 feedback: a SPECIFIC round's slot built from the series' CALENDAR (not from the
// enrollment, which may not exist yet -- enrollment is deferred to settle time). valid=false if the round doesn't exist.
CalSlot SlotForRound(const CareerCalendar& cc, const std::string& seriesId, int round);
// applies DLC gating/substitution to a slot that's ALREADY filled in (track and series). The SOLE source of that
// logic, called by both BuildMasterCalendar (UI) and SlotForRound (LAUNCH) -- if it were only applied in the
// master calendar, the launched race would run the original, missing track. Unplayable series -> dlc_locked+product;
// missing track -> substitutes (dlc_subbed + orig_*) or locks. Forces s.locked |= s.dlc_locked.
void ApplyDlcContentToSlot(CalSlot& s, const SeriesDef* sd);
// Tester feedback (Aug 28, hub): DLC substitution for the ROUNDS in championship views (same logic and the same
// group anchor as the slot). Substitution only; with no substitute the round keeps its name (an informational view).
void ApplyDlcToRound(CalRound& r, const SeriesDef* sd);
void ApplyDlcToRounds(std::vector<CalRound>& rs, const SeriesDef* sd);

// DNF / simulation ---
std::vector<ClassifiedEntry> SimulateRound(const std::string& seriesId, int round, double playerForm);
// The WHOLE MEETING of that round, for whoever puts the race on track without the game (simulation mode and the
// double). Each class comes out of its SimulateRound — its part of the meeting, labeled with ITS class — and the classes go
// in blocks, in the order the XMLs are written. Single-class round = SimulateRound. A championship
// table is NOT filled with this: ApplyDnf uses SimulateRound, which only brings the series' class.
std::vector<ClassifiedEntry> SimulateMeeting(const std::string& seriesId, int round, double playerForm);

// THE TABLES THE MULTICLASS MEETING SIMULATION SOILED (up to 0.7.0). A meeting round that you
// skipped scored the whole meeting's classes in your series' table, with mixed positions: lantern_trophy_7694 with
// 52 drivers and a MERROWDOWN_MYR373 as leader. This re-simulates with SimulateRound those rounds and only those:
//   · of ACTIVE seasons (status 0) and already past (index < round_idx);
//   · of a multiclass meeting, that you did NOT run: your result is the DNF that ApplyDnf records (pos == field) and your row
//     is the only DNF of the round;
//   · CONTAMINATED: more rows than its part + 1, or someone from another series' cast of the meeting who is not in the
//     cast of yours.
// Your row is not touched (neither your points nor your races); the AI rows of that round are replaced. Deterministic (the
// simulation is) and idempotent: a repaired round is no longer contaminated. A round whose cast has
// repeated names (0.6.8 XML not regenerated) is not repaired and is counted in skipped_dirty_xml.
struct MeetingRoundRepair { std::string series_id; int round = 0; int rows_before = 0; int rows_after = 0; };
struct MeetingRepairResult { std::vector<MeetingRoundRepair> fixed; int skipped_dirty_xml = 0; };
MeetingRepairResult RepairSkippedMeetingRounds(CareerCalendar& cc);

// THE CAST THAT ACTUALLY RACES. Reported 09/08: "the drivers who've raced against you aren't the ones who
// show up on the Championships tab". The championship table fills from two paths -- the rounds you race
// (the REAL standings, the ones from the XML the game reads) and the ones you skip (SimulateRound) -- and each one built
// its own grid independently: a different size at multiclass meetings, and the XML also carried the living world's
// (rival_form + driver_ratings) data that the simulation never received. Since points accumulate BY NAME, the table
// ended up being the UNION of two populations: people who never set foot on track, mixed in with those who did.
//
// The fix isn't to keep the two rosters in sync -- two synced things drift apart again the moment
// someone touches one of them -- it's to remove the second source: the skipped round is now simulated with the same drivers from the XML
// the game was going to read. That way they can't diverge, by definition.
//
// This is done with an installable PROVIDER, rather than reading the file here, because this module is the pure engine:
// career_test compiles it and the simulator uses it, and both run without the game and without XML. With no provider installed (tests,
// the harness, or a class not yet pregenerated), SimulateRound composes the grid exactly as before, byte for byte.
//
// THE PROVIDER RETURNS ONLY THAT SERIES' CLASS, also in a multiclass meeting. Until 0.7.0 it gathered
// the meeting's classes and SimulateRound scored them all in the series table (lantern_trophy_7694 with 14 MERROWDOWN_MYR373 and 18
// Pemberton_Zel121). GridDriver carries no class, so from here they cannot be separated again: the whole meeting is assembled by
// SimulateMeeting, class by class, and each class is trimmed by SimulateRound to its part.
using RacedGridProvider = std::function<std::vector<GridDriver>(const std::string& seriesId, int round)>;
void SetRacedGridProvider(RacedGridProvider p);   // nullptr = uninstall
// (living cohort): if 'save' is passed, the round simulated WITHOUT the player also moves the AI's
// ELO (SettleRatingGrid over SimulateRound's order) -- the world keeps racing even when you skip the
// date. Default nullptr = previous behavior (only championship points, ratings frozen).
void ApplyDnf(SeasonState& s, const std::string& playerName, SaveData* save = nullptr);
// ACCEPTED limitation (20/07 decision): doy is 1..366 WITHIN a year, so by design a season CANNOT
// cross the year boundary (Nov->Mar isn't representable). None of the current 115 series need it.
// Guarded by test_calendar_no_year_crossing (tools/career_test.cpp).
bool SeasonEnded(const SeasonState& s);

// resolving the world after racing ---
// An optional 'save' parameter (default nullptr) enables the no-show penalty. When it's passed,
// DnfPassedRounds increments the per-contract counter for the missed series, applies RATING_NOSHOW_DING, and auto-breaks
// the contract once the limit is hit. Default nullptr -> previous behavior (simulation DNF only, contracts untouched).
void ResolveAfterRace(CareerCalendar& cc, CareerProgress& prog, const CalSlot& chosen, const std::string& playerName,
                      SaveData* save = nullptr);
// noShowSeason (default false) lifts the ChampionshipBonus +1 REP floor; the caller with
// SaveData passes (contract.missed_rounds == series rounds). Default false keeps every existing caller unchanged.
SeasonOutcome FinalizeSeason(SeasonState& s, CareerProgress& prog, const std::string& playerName,
                             bool noShowSeason = false);
// "You didn't show up." DNFs EVERY pending round (any enrollment) whose date has fallen behind the
// cursor (doy < cc.cursor_doy). Helper shared by ResolveAfterRace (after racing) and the advance-time path
// (AdvanceTime). Does NOT move the cursor (the caller does that). Deterministic.
// An optional 'save' parameter (default nullptr) enables the per-contract no-show penalty
// (counter + ding + auto-break). Without it (pure-clock tests) it only DNFs the simulation as before.
void DnfPassedRounds(CareerCalendar& cc, const std::string& playerName, SaveData* save = nullptr);
// Was 'seriesId''s season an entire NO-SHOW? true iff there's an
// active contract for the series AND its missed_rounds >= the series' round count. Read from the per-contract COUNTER (NOT from
// playerPos==fieldSize). Callers with SaveData pass it to FinalizeSeason/FinishSeason to lift the +1
// REP floor. No active contract -> false (uncontracted seat / after an auto-break). Pure and testable.
bool NoShowSeasonForSave(const SaveData& save, const std::string& seriesId);

// Time-skip preview: a TRUTHFUL preview of a jump -- an ENROLLED round that the jump would leave behind as
// a no-show. Enough data for the confirmation card (which series/class, which track, which date).
struct JumpNoShow {
    std::string series_id, class_name, series_display, track_name, date;
    int round = 0, doy = 0, year = 0, abs_day = 0;
};
// Result of previewing a jump to 'targetAbsDay'. no_shows is in CHRONOLOGICAL order (abs_day ascending).
struct JumpPreviewResult {
    int  target_abs_day = 0, target_year = 0, target_doy = 0;
    bool crosses_year = false;
    std::vector<JumpNoShow> no_shows;   // ENROLLED rounds that would be lost, in chronological order
};
// Preview of what it would cost to jump TO 'targetAbsDay' (Gregorian absolute day, career::dateutil).
// DESIGN RULE: does NOT reimplement the no-show criterion -- it simulates on a COPY of 'cc' by moving the cursor
// (same semantics as AdvanceDays: same year -> cursor_doy=target_doy; year crossing -> cursor_doy=end of year,
// because the rollover clears enrollments and with them everything pending for this year is lost) and calls
// the SAME DnfPassedRounds (with save=nullptr: marks DNF without applying the penalty) to observe what gets DNF'd. That way the
// preview and the real penalty can NEVER diverge: if DnfPassedRounds changes, the preview changes with it. Pure
// (doesn't mutate 'cc'; received by const-ref).
JumpPreviewResult JumpPreview(const CareerCalendar& cc, const std::string& playerName, int targetAbsDay);

// YEAR rollover with the spec's hook list (order is CRITICAL; see the .cpp). Snapshots the
// record (completed_seasons) BEFORE clearing enrollments, decrements/re-enrolls the contract, refreshes
// the market, and resets intra-year gating. (rival_form drift was REMOVED 02/07: a driver's skill
// is innate; ELO carries their reputation.) Needs SaveData (contract/completed_seasons/completed_races).
// === end-of-season report -> rollover screen contract ===
// The overlay's end-of-season screen calls RolloverYearReport(...) (NOT the void RolloverYear), persists the
// save (SaveCalendar + SaveCareer, calendar first), and if report.grids_regenerated is true it:
//   1) calls WriteAllClassGrids(CustomAiDir(), save.rival_form) to regenerate every <class>.xml (boot-read),
//   2) shows the restartWarn text and prompts an AMS2 restart (CustomAIDrivers is boot-read, no hot reload).
// report.retirements / promotions / market_moves drive the three columns of the prototype's ROLLOVER screen.
// The void RolloverYear remains for the auto-clock path (AdvanceWeek) which advances time without showing the
// screen; it discards the report (the screen is only shown on the player-confirmed end-of-season path).

// The end-of-season report that the Rollover screen renders. Mirrors the
// ROLLOVER prototype's {retirements, promotions, marketMoves}. Stable driver_id + display name.
struct RolloverRetirement { std::string driver_id, name; int age = 0; std::string reason; };
struct RolloverPromotion  { std::string driver_id, name; int from_tier = 0, to_tier = 0; };
struct RolloverMarketMove  { std::string driver_id, name; std::string from_team, to_team; };
// A contract the player LOST at rollover (not renewed / replaced) because they
// under-performed the objective and was NOT protected (guaranteed_seat/anti_replace). Drives the "you were not
// renewed" inbox mail (job_lost) + the rollover summary. perf = the obj-completion ratio that triggered it.
struct RolloverJobLoss { std::string series_id, team_label; double perf = 0.0; };
struct RolloverReport {
    int from_year = 0, to_year = 0;
    std::vector<RolloverRetirement> retirements;
    std::vector<RolloverPromotion>  promotions;
    std::vector<RolloverMarketMove> market_moves;
    bool grids_regenerated = false;   // true -> the UI prompts an AMS2 restart (CustomAIDrivers is boot-read)
    // The TOTAL mandatory insurance premium charged across all held contracts
    // this rollover (once per contract/season, cleanliness-priced). Surfaced as the season mail-bill on the
    // rollover screen (communicates the cleanliness->price loop). 0 = nothing charged (no held contract / all
    // already billed this season / insolvent-blocked). Additive (default 0); a legacy reader ignores it.
    long insurance_premium = 0;
    // The contracts the player LOST this rollover (bidirectional job security). Empty =
    // every held contract renewed/was protected/on-target. Drives the job-lost mail + rollover summary. Additive.
    std::vector<RolloverJobLoss> job_losses;
    // (12/08): the world also ran. `world_series` championships resolved in `world_races` races
    // (the player's do NOT count here: their races are settled by the overlay one by one). 0 = nobody ran,
    // which in a living world is a symptom, not a state. Additive: an old reader ignores it.
    int world_series = 0, world_races = 0;
    // (12/08): how many people moved their personal arc this season, and in which direction. It is the signal
    // that the world has real sporting careers and not just clocks. Additive.
    int growth_moved = 0, growth_up = 0, growth_down = 0;
    // (13/08): WHO WON EACH CHAMPIONSHIP of the world this season (series_id -> driver_id). It is the
    // news the world has to be able to tell — until now the other ninety championships were not
    // contested, so there was literally nothing to tell. Additive.
    std::vector<std::pair<std::string, std::string>> world_champions;
    // The final sweep (13/08, user's rules): live seats from which a driver was evicted by
    // EPOCH, seats filled so there would be no gap, and those that still ended up EMPTY — those last
    // are not a market failure but lack of content (), and that is why they are counted apart. Additive.
    int seats_evicted = 0, seats_backfilled = 0, seats_unfilled = 0;
    // (15/08): seats where the team REPLACED its incumbent with someone better. It is the signal
    // that the market exists: without this, a sealed grid only opens when someone retires. Additive.
    // ~~~29/08 — HOW MANY PROMOTIONS WERE GENERATED, before filtering. `promotions` comes out ALWAYS empty (measured:
    // five winters, zero) while between 78 and 153 people really move up a level every year. The cause is
    // ORDER: `PromoteFreedSeats` runs BEFORE attrition and at the end the promotion of whoever
    // retired along the way is deleted. Without this counter there is no way to know whether the zero is "none was generated"
    // or "they were generated and all deleted", and those are two different failures. Additive.
    // ~~~29/08 — THE DOWNWARD FLOW, which was not anywhere. Measured: 78-153 people move up a
    // category every winter and between 0 and 2 move DOWN. A world that only goes up fills the top and empties the bottom.
    // Without these three numbers there is no way to know where it breaks: how many contracts expire, how many of those
    // find a place further down, and how many leave the sport for not finding one. Additive.
    int contracts_expired = 0;    // seats freed by expiry (ExpireSeatContracts)
    int demotions = 0;            // free agents RELOCATED further down (CascadeDemoteFreeAgents)
    int retired_no_seat = 0;      // ...and those that found no place and retire
    int promotions_attempted = 0;
    int seats_swapped = 0;
    int window_winners_seated = 0;   // 23/09 (): window winners seated this winter
    // (14/08) — the BREAKDOWN of how each filled seat was signed, which is the answer to "and what if
    // no candidate fits their budget?": it signs whoever gives the level and can pay — lowers the bar
    // within their pocket — or STRETCHES above the budget rather than leave the gap. Additive.
    int signed_on_level = 0, signed_below_bar = 0, signed_over_budget = 0;
    // 14/08: how many ROOKIES the world created this season. It is the denominator of the talent
    // lottery — without it, the generational archetype's weight is set by eye. Additive.
    int rookies_born = 0;
};

// Year rollover that ALSO ages the world and EMITS the report. Does everything RolloverYear does
// (record/contract/market/gating) and, on top of that: seeds birth_year (the first time), retires drivers (ShouldRetire)
// while accumulating freed-up seats by tier, promotes
// (PromoteFreedSeats), advances current_world_year, and fills in the RolloverReport. It does NOT write XML or restart
// (that's the overlay/UI's job); it marks report.grids_regenerated=true so the UI can trigger it. Deterministic.
RolloverReport RolloverYearReport(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                                  const std::string& playerName);

void RolloverYear(CareerCalendar& cc, CareerProgress& prog, SaveData& save, const std::string& playerName);

// v0.4.0 silly season: EARLY renewal -- once <= bal::EARLY_RENEW_ROUNDS_LEFT rounds remain in the LAST
// season of a contract, it pushes a provisional offer into pending_renewals (id "of_<serie>#early") based on
// the CURRENT standing vs. the objective, and emits its actionable inbox mail. Idempotent per (series, year); if the team
// would decline (a disastrous season) it emits nothing. Called by RunWorldTick on every calendar advance.
void EarlyRenewalTick(CareerCalendar& cc, SaveData& save, const std::string& playerName);

// the season ratio for EVERY active contract, per series. It's the SAME formula
// the rollover judges you by (ContractSeasonRatio: championship standing relative to obj_target_pos, with 0.65 as the pivot
// meaning "you just met the objective"), exposed so the decision window can renew you or not without
// reimplementing that judgment. It lives here rather than in career_seats because the table lives in the calendar.
// Active contracts only; series with no enrollment come out with ContractSeasonRatio's neutral 0.5.
std::map<std::string, double> PlayerSeasonRatios(const CareerCalendar& cc, const SaveData& save,
                                                 const std::string& playerName);

// Closes the "renew_" mails from the departing year that the player left UNANSWERED, BEFORE
// clearing save.pending_renewals (the list needed to reconstruct its id "renew_"+ren.id+"_"+year -- once
// it's cleared, there's no way left to recover which mails belong to it). Without this, an ignored "renew_" mail stays
// "actionable" forever, pointing at a renewal that no longer exists. year = cc.year for the rollover that's
// closing (the same toYear those mails were emitted with, one rollover earlier). Factored out as a
// pure, testable helper (doesn't require assembling a full rollover in the test).
void CloseStaleRenewalMails(SaveData& save, int year);

// Mirrors CloseStaleRenewalMails for the press -- an interview (family 6,
// subtype<=3) left unanswered leaves PressInterviewPending() stuck FOREVER (blocking every new interview)
// and the mail stays "actionable" forever (MailActionablePending never prunes it). It's closed on every rollover:
// answered=1, choice=0 ("you let it go" -- no effect; ApplyPressAnswer is NEVER called with choice 0). It doesn't
// need a reconstructed year/id (unlike CloseStaleRenewalMails): it walks save.emails directly,
// family+subtype already identify the interview. Factored out as pure and testable without assembling a full rollover.
void CloseStalePressMails(SaveData& save);

// Block of INTEL + nemesis mails that the rollover emits AFTER resolving
// retirements and promotions (rep already populated) and AFTER ApplyRolloverSeatChanges (save.seat_occupant already in its
// "after" state). Factored out directly as a pure, testable helper (doesn't require assembling a full rollover):
//  Real signings: diff between 'seatsBefore' (a snapshot of save.seat_occupant at the start of the rollover) and the
//    current state -> market headlines for drivers whose seat changed (retirements/promotions excluded).
//  INTEL (family 4, Atenea): intel_retire (a retirement with h2h history), intel_market (an orbit summary),
//    intel_promo (the promotion of someone you raced against). Capped at bal::MK_MAIL_INTEL_PER_ROLLOVER per rollover.
//  Nemesis (family 5, RIVAL): respect/rivalry based on the h2h balance; does NOT count against the INTEL cap.
// fromYear = the year that's CLOSING (cc.year before incrementing); cursorDoy = cc.cursor_doy (the day it's issued).
// Deduped by id (same semantics as season_close): a 2nd call for the same year doesn't duplicate. Deterministic, no RNG.
void IntelRolloverMails(SaveData& save, const RolloverReport& rep,
                        const std::map<std::string, std::string>& seatsBefore, int fromYear, int cursorDoy);

// persistence ---
bool SaveCalendar(const CareerCalendar& cc, const char* path);
// 01/10 ([tester]): renames (merging, same criterion as RenameDriverInStandings) the player in the table of EACH
// enrolled championship. Returns how many tables changed. It is the half that the nick migration was missing (),
// which only touched the per-class files (career_standings_*.sav) and not career_seasons.sav, the one shown on screen.
int RenamePlayerInEnrollments(CareerCalendar& cc, const std::string& oldName, const std::string& newName, bool guarded = false);   // guarded: MergePlayerKeyInStandings
std::string SerializeCalendar(const CareerCalendar& cc);   // the SAME bytes as SaveCalendar, in memory (clock seal)
bool LoadCalendar(CareerCalendar& cc, const char* path);

} // namespace career
