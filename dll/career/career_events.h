#pragma once
// Special events (calendar-agnostic design). The app fetches GET /v1/events and
// writes it to <data>\special_events.json ({schemaVersion, fetchedAt, events:[...]}). Here it loads
// ONLY the events with a payload whose ISO window (real time) contains 'now' AND a non-empty 'classes',
// and stores them in a REGISTRY keyed by id (SetSpecialEventRegistry/SpecialEventById) that the engine queries
// on demand (nothing is injected into the series/calendar tables: the event is assembled directly
// onto the session). PURE: no network, no game globals, no ImGui -- testable in career_test.
#include <string>
#include <vector>
#include <map>
#include "career_series.h"   // CalRound (used by ResolveTrackSlug)
#include "career_balance.h"  // 19/09: EVENT_AI_MISTAKES_DEFAULT (EventRaceRules)

namespace career {

struct SpecialEventDef {
    std::string id;                              // "ev-mc" (a stable key)
    std::string title_en, title_es;               // teaser: title
    std::string body_en, body_es;                 // teaser: long body text
    std::string tier_min;                         // "free"|"supporter"|"plus" -- INFORMATIONAL (the real gating happens on the backend)
    std::string image;                             // teaser: image url/slug
    long long starts_at = 0, ends_at = 0;         // window in UTC epoch time (real time, not the in-career clock)
    std::vector<std::string> classes;             // payload.classes: class_name (multiclass; order = grid order)
    std::string track;                            // track slug (payload.track)
    std::string base_profile;                     // base session profile to clone/parametrize (payload.base_profile)
    std::map<std::string,std::string> overrides;  // payload.overrides: one-off overrides on top of the base_profile
    int  laps = 0;                                // race by LAPS (0 -> uses duration_min)
    int  duration_min = 0;                        // race by TIME (0 -> the session default)
    long reward_credits = 0;                      // > Settle's additive resultBonus term
    int  reward_rep = 0;                          // > Settle's additive repBonus term
    int  discount_pct = 0;                        // entry discount (0-100), informational for the app
};

// "2026-08-10T00:00:00Z" -> UTC epoch seconds. Strict YYYY-MM-DDTHH:MM:SS[Z] format.
// Deterministic (days-from-civil, proleptic Gregorian), WITHOUT the system's timezone. -1 if it doesn't parse.
long long ParseIso8601Utc(const std::string& iso);

// Parses special_events.json and returns ONLY the events WITH a payload AND a non-empty 'classes' whose
// window contains nowEpochSec (two clocks: the window uses REAL time, evaluated at load). A
// missing/corrupt file, or empty key fields -> skipped (non-fatal, never breaks the engine's loading).
// nowEpochSec is LOCAL time: it's corrected with the offset (serverNow - fetchedAt) the app stored in
// the file, so the SERVER's clock decides the window and it's the same on every machine.
std::vector<SpecialEventDef> LoadSpecialEvents(const char* path, long long nowEpochSec);

// Resolves the payload's track slug: (1) an EXACT normalized match for track_name against the loaded
// rounds; (2) the inverse of the curated hash->slug catalog (TrackHashForSlug, career_customai);
// (3) normalized containment (slug>=5 chars) against track_name. false = unresolved -> skip.
bool ResolveTrackSlug(const std::string& slug, const std::vector<CalRound>& rounds,
                      long* outHash, std::string* outName);

// ResolveTrackSlug, feeding it the event's own series' rounds internally (which is what's needed
// for path 1 to match against REAL calendar hashes). Used by both the assembly step and the content
// availability query, so both resolve EXACTLY the same track.
bool ResolveEventTrack(const SpecialEventDef& ev, long* outHash, std::string* outName);

// A view of the registry to walk it in full (the availability query). Stable pointers (deque).
std::vector<const SpecialEventDef*> SpecialEventsAll();

// Populates the event registry (replaces the previous contents; the deque stays stable against future push_backs).
void SetSpecialEventRegistry(std::vector<SpecialEventDef> evs);

// Looks up an event in the registry by id. nullptr if absent.
const SpecialEventDef* SpecialEventById(const std::string& id);

// Registry size -- 0 could mean "genuinely no active events" OR "the process
// (or the test) never got around to populating it". The event-invite expiry sync uses this to avoid
// confusing the two cases: it only treats an event as "removed from the registry" when the registry IS
// populated (>0) and the id doesn't appear in it -- with an empty registry it couldn't tell "expired" from
// "hasn't loaded yet", and would close out live invites by mistake.
int SpecialEventRegistryCount();

// Per-class car discount = the highest discount_pct among the registry's ACTIVE events that include
// className in their 'classes'. 0 if none do. The registry only holds events with a live window (LoadSpecialEvents).
int VehicleDiscountForClass(const std::string& className);

void ResetSpecialEventsForTest();

// An event's effective profile = a copy of base_profile with the override keys overwritten.
// IGNORES 'formlap' (a hard rule). A missing base_profile -> an empty profile + overrides.
ConfigProfile EventEffectiveProfile(const std::string& baseId, const std::map<std::string,std::string>& overrides);

// from the portal: the car rule of an EVENT (overrides.car = veh_hash of the single car, as a string).
// Returns the veh_hash if it is numeric, is not 0 and its class (ams2_vehclass) is playerClass; 0 otherwise (with a
// log line if the author put something that does not match). Earlier engines copy the key without reading it.
int EventCarRule(const ConfigProfile& eff, const std::string& playerClass);

// 01/10 ([tester], 0.7.1): two more settings the event author can FORCE; without a key, the engine does what it always did.
// `overrides.timeprog` = time progression multiplier (1,2,5,10,15,20,25,30,40,50,60) -> s27 index (1..11);
// 0 = no key or a value that is not a game multiplier (with a log line). Until today the event inherited what
// the player had in their Custom Race if they chose to inherit it: Dunmoor 1000 at 100% ended up lasting 24 hours.
int EventTimeProgOverride(const ConfigProfile& eff);
// `overrides.date` = "MM-DD" (real calendar day, no year: the year is still set by the class's era).
// true and month/day if the key is valid; false (and log) if not, and the event goes out with the usual date (06/15).
// Also accepts "YYYY-MM-DD" (year 1900..2100): then it sets the year as well as the day; with "MM-DD" it does not touch `year`.
bool EventDateOverride(const ConfigProfile& eff, int* month, int* day, int* year = nullptr);
// `overrides.minute` = minute of the start time (0..59); 0 with no key or with a value that is not a minute (with a log).
int EventStartMinute(const ConfigProfile& eff);
// 01/10 (Josh): without `date`, the race is run ON THE DAY THE EVENT OPENS (starts_at, UTC), not on June 15:
// the October Dunmoor takes place in October. false if there is no window (starts_at <= 0): the caller keeps its fallback.
bool EventDefaultDate(long long startsAtEpoch, int* month, int* day);

// HOW MANY CARS ARE ON TRACK IN A SPECIAL EVENT, AND WHO DECIDES.
//
// Up to 0.6.8 the number was set by the class the PLAYER CHOSE (`GridSizeForSeries(chosen class)`). In a
// multiclass event that number is the meeting's TOTAL, which the game's builder splits equally among
// the classes, so the Irondale Seca 500 (beacon_series_4332 + cinder_series_4291 + pinnacle_masters_4414) came out with 20 cars if you chose GT1 90s,
// 16 with IRONMARCH GRY36 and 12 with QUENBY QUO872: the event's author counted for nothing and the player changed the event without
// knowing it. The calendar never had the flaw — on a multiclass weekend it asks for those of the whole MEETING.
//
// The ladder, from most to least authority:
//   1. The AUTHOR'S `overrides.grid` (cars on track, player included; the backend already validates it as an integer >= 2). It is
//      ALWAYS respected, even if AMS2 has to fill with generic drivers because the classes cannot name
//      that many: the event is an authored piece and its size is part of its design.
//      16/09: `grid` also accepts the word "max" = AS MANY AS FIT ON THE EVENT'S TRACK, which is also the
//      default value of a new event in the form. The number comes from `trackCapacity` (real slots
//      of the circuit, career_customai). It travels as a word and not already resolved from the backend because the maximum
//      depends on the CIRCUIT: a number would freeze that of whichever track was set on the day of authoring.
//   2. The PLAYER'S CAP (, config.json "max_grid") takes precedence over the author's number. It is not a design
//      opinion, it is the performance limit of THEIR machine. The other two paths already carry it inside
//      (GridSizeForClassName / MeetingClassShare), so it only has to be applied to the authored number.
//   3. Without `grid`: the multiclass event uses `MeetingGridSize(classes)` THE SAME as the calendar, and the single-class one
//      the size of its own class (as always). This way the number stops depending on whom you choose.
// A `grid` that is not an integer >= 2 nor the word "max" (empty, "0", "1", negative, text) is IGNORED and falls
// to point 3, quietly: the engine is not the one who validates the backend form. A "max" on a circuit WITHOUT
// known slots also falls to point 3 — never to a made-up number — and the caller notes it in the log.
//
// 4. THE TRACK'S SLOTS ARE THE CEILING (16/09, Josh's decision). The author's number takes precedence over the
// engine's, but neither can exceed what fits on the circuit: an event that asks for 48 cars where
// there are 26 slots runs with 26. Before, their number prevailed there too and the mismatch was only logged, which on track
// means asking AMS2 to place cars with no room. Trimming here also fixes the events ALREADY published
// without having to republish them. A circuit with no known capacity trims nothing.
struct EventGridPlan {
    int  authored = 0;       // the author's integer; 0 = absent, "max", or discarded as odd
    bool wantsMax = false;   // the author asked for "as many as fit on the track"
    int  computed = 0;       // what the engine gives on its own (the meeting if multiclass, the class if not)
    int  cap      = 0;       // the player's cap on cars on track; 0 = no cap
    int  trackCap = 0;       // cars that fit on the event's circuit; 0 = unknown
    int  cutByTrack = 0;     // what was asked for before trimming by the track; 0 = no trimming was needed
    int  onTrack  = 0;       // the FINAL number of cars on track, player included (>= 2)
};
// `eff` = the effective profile ALREADY composed (the caller has it; recomposing it here would repeat the warning of a nonexistent
// base_profile). `playerSeriesId` = the class the player has chosen, as series_id.
// `trackCapacity` = TrackGridCapacity(hash of the circuit ALREADY resolved), 0 if unknown. It comes in as a parameter instead of
// being looked up in here so that the ladder stays pure and measurable without catalogs loaded.
EventGridPlan EventGridSizeFor(const SpecialEventDef& ev, const ConfigProfile& eff,
                               const std::string& playerSeriesId, int trackCapacity = 0);

// 19/09 — PIT RULES AND AI MISTAKES of a special event, with a value for what the author did NOT set.
// Player report: an event where nobody could refuel. An unset `refok` arrived as -1 = "do not force", so
// the game kept whatever the previous race had left (e.g. a modern F1, which forbids
// refueling). An event is a closed piece and cannot depend on what the player raced before, so what the
// author does not set comes out with the house rule (refuel_ban_only_real_rule): refueling YES (permissive, does not
// force anyone to stop), mandatory stop NO, minimum tyres 0. The pit limit keeps its usual 60
// (multiclass + mandatory stop without a limit crashed the game's spawn).
// `mistakes` = the AI's mistakes (VM498 s28), which are authored per event (Josh's decision, 19/09): `mist` with
// ONE digit 0..6 of the measured enum. Unset, or with anything else (a "media" from an old profile, a 7),
// bal::EVENT_AI_MISTAKES_DEFAULT (x2). Never `atoi`: it would swallow "media" as 0 and switch the mistakes off without warning.
struct EventRaceRules {
    int mandatoryPit = 0;    // VM550 s20
    int refuelling   = 1;    // VM550 s24
    int minTyres     = 0;    // VM550 s21
    int pitKmh       = 60;   // PitSpeedLimit node
    int mistakes     = bal::EVENT_AI_MISTAKES_DEFAULT;   // VM498 s28
};
// `eff` = the effective profile ALREADY composed (base_profile ⊕ overrides), same as EventGridSizeFor.
EventRaceRules EventRaceRulesFor(const ConfigProfile& eff);

} // namespace career
