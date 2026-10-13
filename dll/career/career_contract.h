#pragma once
// career_contract — LIFECYCLE of the driver contract against the real save.
//
// Accepting a market offer creates the persisted Contract + enrolls the series with the entry
// fee WAIVED (the team covers the entry). Settle reads the active contract to decide the
// factory seat ("you get paid"). Breaking the contract applies the REP penalty and frees the seat.
//
// It's kept separate from career_market (which is PURE: it only COMPUTES offers, with no SaveData or CareerCalendar) so as not to
// drag persistence/the calendar into the offers module. This is where save MUTATIONS live.
//
// Persisted contracts, designed against the career contracts & market spec.

#include <string>
#include <vector>
#include <set>
#include <cstdint>
#include "career_userlog.h"   // 0.7.0 review: CapturedLogLine (the logs a clock preview keeps)
#include <map>
#include "career_save.h"          // career::SaveData, career::Contract, career::Courting
#include "career_calendar.h"      // career::CareerCalendar, Enroll, ConflictsFor
#include "career_market_data.h"   // market::OfferDef

namespace market { struct GenOffer; }   // fwd: career_market.h only in the .cpp (avoids dragging it into the header)

namespace career {

// Accepts an offer: creates the active contract + enrolls the series with the entry fee WAIVED + adds the
// sign-on bonus to the credits. Refuses (returns false, without touching the save) if there's ALREADY an active contract (for now,
// only one at a time). 'year' = the calendar year at signing (signed_year). It does NOT consult declined_offers: rejection
// is a UI-level decision (save.declined_offers.insert(offer.id)); accepting something previously declined is valid.
// 'terms' (optional) = the player's NEGOTIATED package (8 demands + salary package + sponsor).
// If != nullptr it's persisted on the contract (ct.accepted_deal) so SIGNING reflects what was negotiated; nullptr =
// signs the BASE offer (Accept from the Market / "accept as-is": a neutral deal by default).
// SAVE RESCUE: undoes, on an already-saved game, the contract mis-stamped by the bug from
// version 0.6.1 (see market::SigningSeasonFor). Only the one that hasn't gone through the rollover yet arrives in time: the one that already
//     has, had its contract closed and deleted by the rollover, and there's nothing left in the save to reconstruct it without
//     making it up. Returns how many it repaired (0 = nothing to do). IDEMPOTENT: a second pass finds none.
//
// It's deliberately CONSERVATIVE, because getting it wrong means either granting or removing a season for free. A contract
// legitimately signed for the CURRENT season also carries signed_year == cc.year in December, so it takes
// four signals at once to touch it, and any one of them saves it: having already paid an insurance premium
// (premium_year != 0 -> it lived through a rollover, so it predates this), accumulated absences (missed_rounds != 0 ->
// the season is already underway) or having an enrollment with races behind it (round_idx/history). A contract
// signed two days ago has none of the three. That's exactly why the enrollment it removes is UNTOUCHED by
// construction: no raced season is ever lost.
int RepairWindowSignedContracts(SaveData& save, CareerCalendar& cc);

bool AcceptOffer(const market::OfferDef& offer, SaveData& save, CareerCalendar& cc, int year,
                 const DealTerms* terms = nullptr);

// WHY AcceptOffer would say no — the engine used to stay silent about the reason and the app showed a mute "no" (a
// refetch, and off it went). PURE and non-mutating: returns the key of the FIRST blocker that bites, in the SAME order
// AcceptOffer checks in, or "" if it would sign. Keys = the same ones SignWindowSeat's translator uses (career_seats.cpp),
// which the app already knows how to render: "refused" (offer with no series: malformed), "already" (duplicate series+season),
// "exclusive" (an exclusive contract covers the target season), "afford" (buy-in > credits).
// It's not a mirror that can drift out of sync: AcceptOffer actually CALLS this function as its blocker check.
std::string AcceptRejectReason(const market::OfferDef& offer, const SaveData& save, int year);

// Silly season (0.4.0): signs a RENEWAL offer (pending_renewals). If the series' contract is still
// ACTIVE (an early renewal, tagged "#early"), it EXTENDS it in place: +commitment seasons (only for ARCH_RENEWAL;
// clamped 1..3), a new per_race figure (frozen at signing), the sign-on bonus paid out, target re-set.
// With no active contract (the final one from the rollover, seat already freed) it falls through to AcceptOffer (the normal path).
bool AcceptRenewal(const market::OfferDef& ren, SaveData& save, CareerCalendar& cc, int year,
                   const DealTerms* terms = nullptr);

// Breaks contract(s): applies REP_BROKEN_CONTRACT (floored at 0, ONCE) and frees the seat(s). With
// seriesId="" it terminates ALL active contracts (the hub's generic button); with a seriesId it terminates only that one.
// No-op if there is nothing to break. The enrollment already made in the calendar is NOT reverted here: this version
// is for the paths that must NOT touch it (the auto-break for absences, which runs INSIDE the enrollment loop of
// DnfPassedRounds, and the team change within the SAME series of SignWindowSeat).
void BreakContract(SaveData& save, const std::string& seriesId = "");

// Terminating BY HAND ("Terminate contract" and the hub's generic button): the same as the one above (amount,
// REP, relationship mark: it calls it) and ALSO removes from the calendar the enrollment of the CURRENT season that
// that contract sustained. Without this the enrollment survived the contract and the calendar kept counting its
// rounds as yours: the clash warning did not go away, the jump listed them as absences and the rollover paid the
// bonus of an abandoned season. It is only removed if (1) the broken contract covered the clock's season, (2)
// there is no other active contract covering it in that series and (3) the season still has rounds left: one already
// closed is a record and is not touched. Returns how many enrollments it removed.
int BreakContract(SaveData& save, CareerCalendar& cc, const std::string& seriesId = "");

// The PAYOUT that BreakContract charges, derived so it can be shown to the player BEFORE they click.
// It's the same function that actually charges it, so the warning can't lie. An empty seriesId sums all of them.
// Anchor: MK_BREAK_RACES_OWED races' worth of salary, or the tier's entry cost if the seat doesn't pay;
// scales with the seasons remaining. 0 if there's no active contract to break.
long BreakContractFee(const SaveData& save, const std::string& seriesId = "");

// Is the seat being raced a FACTORY seat under contract? True iff there's an active contract, for the SAME series raced,
// and kind==0 (works). A PURE helper (no game state) so the overlay decides the payout and the test
// can verify it. seriesId == "" or a different series -> false (falls back to the standalone behavior in the overlay).
bool ContractFactorySeat(const SaveData& save, const std::string& seriesId);

// Does the player have any active EXCLUSIVE contract? Iterates save.contracts
// (bounded), resolving each contract's profile (ContractCoverageProfile(kind, archetype).exclusive) — or
// the persisted Contract.exclusive flag if the profile doesn't apply. true => AcceptOffer BLOCKS signing another
// (a fine-grained gate at signing time; open/low-tier types keep stacking uncapped). PURE + deterministic (no RNG or
// game state), so the overlay decides and the test can verify it. Empty/no exclusives -> false.
bool AnyExclusiveContractHeld(const SaveData& save);

// A PER-SEASON variant — the one AcceptOffer consults. Only vetoes if an active exclusive covers
// the TARGET season ([signed_year, signed_year+seasons-1]); an exclusive ending this year does NOT veto the
// December signing for next year (the same lesson as the series+season duplicate bug from 0.6.3).
// signed_year<=0 (a legacy save) always vetoes, like the old gate did.
bool AnyExclusiveContractHeldFor(const SaveData& save, int targetYear);

// ============================ market dynamics ============================

// Pre-signing conflict check: does 'offer''s series calendar overlap any
// current enrollment? Builds a CalSlot for every dated round (CalendarForSeries) and projects it against
// enrollments via ConflictsFor (REUSES SlotsConflict; doesn't reinvent the overlap check). Returns the set of
// already-enrolled series_ids that clash (sorted, deduplicated). Empty = no conflict. PURE + testable.
std::vector<std::string> OfferConflicts(const market::OfferDef& offer, const CareerCalendar& cc);
// 18/09 (Josh's decision): the same question, but knowing WHICH SEASON is being signed. In the signing window
// the next year's is signed, and the version above only looked at THIS year's enrollments: two signings
// of the same window could clash all season without warning (a window signing is not enrolled until
// January). With a future target season it compares THAT year's calendar against all the series you have
// signed for it (active contracts that cover it). Outside the window, the same as the two-argument one.
std::vector<std::string> OfferConflicts(const market::OfferDef& offer, const CareerCalendar& cc, const SaveData& save);

// Registers courting from HELD offers: for every GenOffer with held==true, if a record doesn't already exist
// for its (team_id, series_id), it adds one with first_seen_year=year. DETERMINISTIC + IDEMPOTENT (no
// duplicates on repeat calls). Doesn't freeze terms: salary/bonus are recalculated in GenerateOffers at signing.
void RecordCourting(SaveData& save, const std::vector<market::GenOffer>& offers, int year);

// Result of the world tick: offer ids that just EXPIRED (their round-minus-1 fell behind the cursor).
struct WeekTickResult {
    std::vector<std::string> expired_offer_ids;
    // ids of the MAILS GENERATED on the AdvanceWeek pulse (one per new mail emitted).
    // The generator already persisted them in save.emails; this vector exposes them so the UI can drain/notify
    // (the same additive pattern as expired_offer_ids). Empty if nothing new was generated this tick.
    std::vector<std::string> generated_mail_ids;
};

// Tick of the CONTINUOUS CLOCK's "advance time". Moves cc.cursor_doy forward by 7*weeks; on CROSSING the year boundary
// (cursor + 7*weeks > 365) it does NOT clamp: it fires RolloverYear (the FULL hook list: year++,
// cursor=0, standings, contract, rival_form, market, gating) and rolls the remaining days into the new year.
// Safe across multiple years: it loops, one rollover per year boundary crossed in the same jump. After each
// cursor advance it DNFs the rounds left behind (DnfPassedRounds, "you didn't show up") and
// closes (FinalizeSeason) any season whose calendar just completed. Then it regenerates offers,
// detects expired ones (round-minus-1 behind the cursor) -> result + save.expired_offers, and registers courting from the
// held ones. prog/playerName are forwarded to RolloverYear. Deterministic. weeks<=0 -> no advance (just recomputes
// expiry/courting with the current cursor).
// The real temporal model advances the clock by DAYS (not weeks). Leap-aware year boundary
// (DaysInYear); every Dec-31 crossing fires RolloverYear. After each stretch: DnfPassedRounds +
// FinalizeSeason for completed seasons. days<=0 -> no-op. It's the primitive behind AdvanceWeek/
// AdvanceToDate/AdvanceToNextEvent.
void AdvanceDays(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                 const std::string& playerName, int days);

// The real temporal model's post-advance "world pulse" (regenerates offers, expires by TTL,
// detects expired ones, registers courting, emits mails). advanced=true also runs the
// preseason churn (previously gated by weeks>0). Returns the WeekTickResult (expired ones + new mails).
WeekTickResult RunWorldTick(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                            const std::string& playerName, bool advanced);

// 18/09 — advances `days` days giving ONE PULSE PER DAY, and returns the sum of the pulses. It is the
// complete pair (move the cursor + pulse the world) that every clock path has to use: a bare `AdvanceDays`
// moves the cursor and does NOT pulse, so a long jump followed by a single pulse swallows whole the
// things that only happen if the clock is inside them when the pulse falls — the signing window,
// for one. days <= 0 -> only re-pulse, without advancing (and never goes back).
WeekTickResult AdvanceDaysPulsed(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                                 const std::string& playerName, int days);

WeekTickResult AdvanceWeek(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                           const std::string& playerName, int weeks = 1);

// The real temporal model's abs_day of the NEXT actionable round (enrolled or OPEN in the future) ahead
// of the cursor. Lands at the START of the weekend (doy - its series' footprint.lead), not on race day. If
// none remain this year, it RE-PROJECTS next year's calendar (RealignDoy) and jumps to that projection's earliest
// opener. Returns -1 if the calendar has no actionable round at all.
// `save` adds to the filter the access already enforced by launch (CanEnterSeries): a grid series that's
// CLOSED without an ACTIVE contract is no longer a valid destination — "Next available" used to land on events that then couldn't be
// raced. nullptr = fail-open (no access filter; old projections/callers keep their previous behavior).
int NextEventAbsDay(const CareerCalendar& cc, int frontierPhase, const std::set<std::string>* gates,
                    const SaveData* save = nullptr);

// A variant of NextEventAbsDay (above) filtered to ONE specific series. The SAME filter
// EXACTLY (valid/locked/raced/dnf/enrolled-or-not-past + access if there's a save) and the SAME landing
// (doy - footprint.lead) + the SAME year+1 fallback (re-projection, doesn't reuse this year's doys); any change
// to NextEventAbsDay's filter must be reflected here too (as NextEventLookahead already warns below). -1 if the
// series has no actionable round at all (neither this year nor next), the series_id doesn't resolve in
// AllSeries(), or (with a save) it's a closed series you can't enter.
int NextEventAbsDayForSeries(const CareerCalendar& cc, const std::string& seriesId, int frontierPhase,
                             const std::set<std::string>* gates, const SaveData* save = nullptr);

// A variant of NextEventAbsDay for a DATE PICKER — the earliest actionable event
// (of ANY series) whose LANDING (abs_day after subtracting footprint.lead) is >= targetAbsDay. An
// empty chosen day -> lands on the next race; a day WITH an actionable event -> returns that same abs_day
// (doesn't skip it). The SAME slot filter as NextEventAbsDay (valid/locked/raced/dnf/enrolled-or-not-past) +
// the SAME year+1 fallback, but the SELECTION criterion compares the LANDED abs_day against
// targetAbsDay (NOT the raw doy vs cursor_doy like NextEventAbsDay): lead time varies by series, so
// each candidate is landed BEFORE comparing/sorting (a later raw doy can land earlier than a
// smaller one if its lead is longer). -1 if no actionable round lands on/after targetAbsDay (neither this year
// nor next). Same optional `save`-based access filter as NextEventAbsDay.
int NextEventAbsDayOnOrAfter(const CareerCalendar& cc, int targetAbsDay, int frontierPhase,
                             const std::set<std::string>* gates, const SaveData* save = nullptr);

// A variant of NextEventAbsDay (above) that only counts series in
// which the player is ENROLLED (s.enrolled) — it distinguishes "next AVAILABLE event" (NextEventAbsDay,
// any open series, enrolled or not) from "next event you're ALREADY enrolled in". The SAME landing
// (doy - footprint.lead) and the SAME year+1 fallback (re-projection) as NextEventAbsDay; the only change is the
// filter, which adds the "enrolled" condition to the existing ones. With NO enrollment at all -> -1 (there's no
// reasonable fallback that wouldn't confuse this query with NextEventAbsDay; see the comment next to its
// implementation in career_contract.cpp for the full justification).
int NextEventAbsDayEnrolled(const CareerCalendar& cc, int frontierPhase, const std::set<std::string>* gates);

// The next actionable event + what's needed to know about it for the treasury/
// track-history mails. days_away = the distance in days from TODAY (cc.cursor_doy) to the
// event's landing (the same landing as NextEventAbsDay: start of the weekend, doy-lead; 0 = lands today); -1 = no
// actionable event (calendar exhausted, not even the year+1 fallback). min_outlay = the guaranteed minimum
// payout to race it (RaceEntryOutlay); 0 if the series' class couldn't be resolved.
struct NextEventInfo {
    int days_away = -1;
    long min_outlay = 0;
    std::string track;
    long track_hash = 0;
};

// Replicates NextEventAbsDay's filter (above) EXACTLY to locate the SAME slot that
// the clock would jump to, but also returns its distance in days + the minimum payout to race it. The car
// used follows the SAME pattern as Prerace/settle (screen_vehicle.cpp, see RaceEntryOutlay(): InscriptionDue+EntryFeeFor
// for the series + RaceEntryOutlay): OwnCar if you already own a car in the slot's class (OwnsCarOfClass), otherwise
// Rent with the cheapest car in that class (CheapestCarPriceForClass). PURE except for the global
// economy/calendar tables (already loaded at startup); deterministic. With no actionable event -> days_away=-1, the rest
// falls to 0/"" (fail-safe: neither alert fires).
NextEventInfo NextEventLookahead(const SaveData& save, const CareerCalendar& cc, int frontierPhase);

// Real-time model: advances the clock to a target abs_day (AdvanceDays over the delta) and runs the
// world pulse. A target in the past/today -> doesn't move backward (just re-pulses). Returns the WeekTickResult.
WeekTickResult AdvanceToDate(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                             const std::string& playerName, int targetAbsDay);

// Real-time model: advances to the schedule's next event (NextEventAbsDay with
// the save's frontier/gates). No pending event -> just a re-pulse.
WeekTickResult AdvanceToNextEvent(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                                  const std::string& playerName);

// ===================== C1 · Clock with stops (spec ) =====================
// A "day event" is something the player would want to know about on crossing it. StopReasons compares the snapshot
// BEFORE the day (mails, seen offers, expired, renewals, marks, offered seats, round history)
// with the save AFTER and adds the races that land today: yours (the PendingSlot of each enrollment,
// landing doy - lead, and also race day as a second chance) and the other enterable ones (same
// filter as NextEventAbsDay over the master calendar's slots, computed once per year by the loop).
// `stops` comes from StopRules; the loop (AdvanceUntilStop) adds the hard stops on its own.
// Review 0.7.0 (finding 4a, ): SEATS_LAST_DAY = eve of the close with offered seats unsigned (it stops
// with the offers rule: they can still be signed); SEATS_LOST = the day CloseExpiredWindowOffers gives them
// to someone else (feed line, does not stop).
enum DayEventKind { DEK_RACE_MINE = 0, DEK_RACE_OTHER = 1, DEK_MAIL = 2, DEK_OFFER = 3, DEK_OFFER_EXPIRED = 4,
                    DEK_RELATION = 5, DEK_DECISION = 6, DEK_ROLLOVER = 7, DEK_YEAR_END = 8, DEK_CAP = 9,
                    DEK_UNTIL = 10, DEK_NOSHOW = 11, DEK_SEATS_LAST_DAY = 12, DEK_SEATS_LOST = 13,
                    DEK_OFFER_LAST_DAY = 14 };   // 18/09: eve of a normal offer expiring
constexpr int kDayEventKinds = 15;
const char* DayEventKindName(DayEventKind k);   // "race_mine"…"seats_lost" (the app uses it as a key)
struct DayEvent {
    int abs_day = 0; DayEventKind kind = DEK_MAIL;
    std::string label_key;   // "time.ev.<kind>" (the APP's i18n key)
    std::string params;      // "k=v\x1Fk=v" (series, track, n, team) — DayEventParam()
    std::string series_id;
    bool stops = false;      // according to StopRules (hard stops always true)
};
// Mails and renewals are compared by IDENTITY, not by SIZE: GenerateCareerEmails ends up PRUNING the
// inbox, so a day that delivers one mail and prunes another left the counter the same and the event was lost —
// precisely the day when there is something to read. With ids, deliver-and-prune still counts as "one arrived".
// Review (finding 4): the expired ones and the offered seats also go by identity. By size, the feed
// counted as "expired" every series that left the offers horizon even if the player had never seen it,
// and there was no way to know which OFFERED seat had just been lost.
struct DaySnapshot {
    size_t rel_marks = 0, offered_seats = 0;
    std::set<std::string> expired_ids;                   // save.expired_offers
    std::set<std::string> lapsed_ids;                    // save.lapsed_offers
    std::set<std::string> offered_seat_keys;             // "<series>\x1F<team>" of each WSEAT_OFFERED seat
    std::set<std::string> email_ids;                     // ids of save.emails (GeneratedEmail::id)
    std::set<std::string> renewal_ids;                   // ids of save.pending_renewals (market::OfferDef::id)
    std::set<std::string> offers_seen;                   // keys of save.offer_first_seen
    std::map<std::string, size_t> history_len;           // per enrolled series: cc.enrollments[i].history.size()
};
DaySnapshot SnapshotDay(const SaveData& save, const CareerCalendar& cc);
void DayEventParam(std::string& params, const char* key, const std::string& value);

// C1 (final round): what StopReasons needs from the YEAR, computed ONCE and not 365 times. Before, each day redid
// BuildMasterCalendar inside (SlotJumpEnterable + FootprintForSeries, which rescans CalendarForSeries with
// std::string comparisons and sorts) for each slot and each enrollment; now each slot's landing
// is an already-computed integer and each series' footprint is looked up in a map.
//   slots   = the year's master calendar (the same BuildMasterCalendar as always)
//   landing = LANDING abs_day of slot i (doy - lead), or -1 if the slot does not pass the enterable filter
//   lead    = footprint (lead) per series; `mutable` because StopReasons memoizes there the enrolled series that was not
//             already in the master calendar (once, not once per day)
struct DayContext {
    std::vector<CalSlot> slots;
    std::vector<int> landing;
    mutable std::map<std::string, int> lead;
};
DayContext BuildDayContext(const CareerCalendar& cc, const SaveData& save);
std::vector<DayEvent> StopReasons(const DaySnapshot& before, const SaveData& save, const CareerCalendar& cc,
                                  const DayContext& ctx, int absDay, const StopRules& rules);

constexpr int kAdvanceCapDays = 366;   // safety cap (spec ). 366, not 365: from a 31 December, the
                                       // 31st of the following year is 366 days away if that year is a leap year, and with 365 the
                                       // cap stopped on the 30th instead of the year-end stop
struct AdvanceReport {
    int from_abs_day = 0, stop_abs_day = 0, days = 0;
    DayEventKind stop_reason = DEK_CAP;
    bool crossed_year = false;
    std::vector<DayEvent> crossed;        // EVERYTHING crossed, in order, with `stops` according to rules
    std::vector<JumpNoShow> no_shows;     // enrolled rounds left behind (DnfPassedRounds marked them)
    WeekTickResult ticks;                 // ids of expired/mails accumulated from all the pulses
    // 18/09: a PREVIEW abandoned half-done because newer clock work arrived (see
    // SetPreviewAbortProbe). Its figures are partial and describe nothing: they are neither shown nor adopted.
    bool aborted = false;
};
// AdvanceUntilStop: ONE-day loop over AdvanceDays (DNF/FinalizeSeason/Rollover live there), world pulse
// RunWorldTick(advanced=true) EVERY DAY (18/09: it was every 7th of THIS advance), StopReasons after each day. Stops on the first day with a
// reason according to the rules, or at untilAbsDay, and always on 31 December (hard stop) and at the cap
// kAdvanceCapDays. Since each day brings its pulse, so does the stop day: the world arrives fresh at the landing
// with no extra pulse. PreviewAdvance = the same over copies.
// R4: untilAbsDay = 0 (the default value) means "no cap" — the clock runs free until the first
// reason or the hard stop, and the API will use 0 for "until the first stop". It coincides on purpose with abs_day
// 0 (2025-01-01, the career::dateutil epoch): no race can land there (the season starts on
// doy 80) and nobody can ask for "stop here" on that day (it is always <= today), so the sentinel does not collide
// with a real target that someone could pass.
// Clock diagnostic stopwatch: inert unless ZTA_CLOCK_PROFILE points to a file.
// Public so that the api handler can time the serialization, which is outside this module.
void ClockProf(const char* fmt, ...);
bool ClockProfOn();

AdvanceReport AdvanceUntilStop(CareerCalendar& cc, CareerProgress& prog, SaveData& save,
                               const std::string& playerName, const StopRules& rules, int untilAbsDay = 0);

// Review 0.7.0 (finding 1): the popup asks for the preview (a year can be ~4-5 s of simulation on the
// game thread) and on pressing Advance the command simulated it all again: double the hkPresent blocking and, if both
// went over the client's 10 s, the app said "it failed" with the clock already advanced. The preview keeps its
// ARRIVAL state and a seal of the STARTING state; the command adopts it if the seal, the destination, the rules
// and the pilot match, and if not, it simulates as usual. It is safe because AdvanceUntilStop is deterministic over
// that state (test_preview_equals_real).
//   AdvanceSeal = FNV-1a of SerializeCareer(save) + SerializeCalendar(cc): exactly what would be saved. A
//   change in the world that does not reach the save (installed content that finishes scanning in between) does not
//   move it; adopting then amounts to having advanced a few seconds earlier.
struct AdvancePreviewKeep {
    bool valid = false;
    uint64_t seal = 0;                    // seal of the STARTING state
    int untilAbsDay = 0;
    std::string playerName;
    StopRules rules;
    AdvanceReport report;
    CareerCalendar cc;                    // ARRIVAL state
    SaveData save;
    std::vector<CapturedLogLine> logs;    // what the advance would have written to the logs (finding 7)
};
uint64_t AdvanceSeal(const CareerCalendar& cc, const SaveData& save);
// Over copies and with the logs captured (finding 7): nothing it simulates reaches the log files. With
// `keep`, it stores there the arrival state, the starting seal and the logs so the command can adopt it.
// 18/09 — THE PREVIEW CAN BE ABANDONED. With the daily pulse, previewing a whole year costs ~12 s, and the engine
// serves requests one at a time: a "stop here" pressed while the preview was running waited behind
// it for those 12 s, with the calendar sheet frozen. A preview is speculative — nobody needs it if
// behind it the real advance or another preview that replaces it is already waiting —, so it is left half-done.
// The API layer registers the probe here (is there newer clock work in the queue?); PreviewAdvance
// queries it every simulated day and, if it answers yes, returns a report with `aborted` and stores NOTHING to
// adopt. The real advance never queries it. nullptr = no probe (tests, overlay): it is never abandoned.
void SetPreviewAbortProbe(bool (*probe)());

AdvanceReport PreviewAdvance(const CareerCalendar& cc, const SaveData& save, const std::string& playerName,
                             const StopRules& rules, int untilAbsDay = 0, AdvancePreviewKeep* keep = nullptr);
// If `keep` corresponds to THIS state and to this request: cc/save become the arrival state, its logs
// are written, `out` is its report, `keep` is emptied and it returns true. If not, it touches nothing and returns false.
bool AdoptPreviewedAdvance(CareerCalendar& cc, CareerProgress& prog, SaveData& save, const std::string& playerName,
                           const StopRules& rules, int untilAbsDay, AdvancePreviewKeep& keep, AdvanceReport& out);

// Stamps the first time an offer is seen (idempotent via emplace — it doesn't
// overwrite an already-persisted firstSeen). Empty id -> no-op. PURE.
void MarkOfferSeen(SaveData& save, const std::string& id, int absDay);

// Moves offers from offer_first_seen into save.lapsed_offers once they're
// >= bal::MK_OFFER_TTL_DAYS old (28d), removing them from offer_first_seen. NEUTRAL: lapsed does NOT count
// as a snub in TeamRelationship (only declined_offers/expired_offers do) — expiring by TTL isn't
// a player decision, so it doesn't cool the relationship. PURE.
void LapseExpiredOffers(SaveData& save, int absDay);

// Read-only accessor for save.offer_first_seen. true+out = the absolute day
// stamped if the offer has already been seen (and hasn't lapsed/been purged); false if it isn't stamped yet. PURE.
bool OfferFirstSeen(const SaveData& save, const std::string& id, int& out);

// offer_lifecycle: weeks (ceil) until an offer stamped at firstSeen expires, given today's absolute day.
// Range [0, ceil(MK_OFFER_TTL_DAYS/7)]. For an offer NOT yet stamped, the caller passes firstSeen=absToday (the full window).
int WeeksLeftFromFirstSeen(int firstSeen, int absToday);

// 18/09 (Josh): AN OFFER'S DEADLINE, in a single place. Returns the first absolute day on which the offer can
// no longer be signed. It is whichever comes first of:
//  · its TTL: MK_OFFER_TTL_DAYS since you saw it (LapseExpiredOffers). Unstamped yet it counts from today;
//  · the deadline of its negotiation thread if one is open: NegoTick withdraws it the day AFTER deadline_day
//    (or reserve_day if you already shook hands), and a rival's pressure can shorten it;
//  · the market close, if it is open today: the day after closing nothing can be signed.
// Days left = death - today; 1 = today is the last day to reply.
int OfferDeathAbs(const SaveData& save, const std::string& offerId, int absToday);
// The live offers that are on their LAST day (tomorrow they can no longer be signed). Rejected, expired,
// withdrawn ones or those of a started season are out, and those of a series in which you already have a contract. Only with the market
// open, and if that day had a pulse only those the market still offers (SaveData::live_offer_ids).
std::vector<std::string> OffersOnLastDay(const SaveData& save, int absDay);
// And that day the mail of each one goes back to UNREAD: the offer's warning is repeated on the eve of its
// expiring. Only unanswered mails. Returns how many it changed. RunWorldTick calls it once per advanced day.
int ReopenOfferMailsOnLastDay(SaveData& save, int absDay);

// ============================ mail generator ============================

// Milestone context for the mail generator. Filled in by each pulse site (post-settle /
// AdvanceWeek / RolloverYear) with whatever it has on hand; 0/false fields disable that milestone (never forced).
// year = the calendar year (season-scoped milestone ids EMBED it -> they re-fire next season,
// same pattern as elsewhere); first_podium/first_win are one-shots.
// titlefight_series/standings_pos/rounds_left (the late-stage "title fight") are RETIRED — the
// generator never emits ms_titlefight again (it's replaced by the real press system); the
// ms_titlefight_* templates stay in strings.cpp ONLY to render old saves with that subtype persisted.
struct EmailGenContext {
    int year = 0;                 // calendar year (embedded in season-scoped milestone ids)
    int day  = 0;                 // calendar doy at generation time (ordering + retention)
    int rep_band = -1;            // current RepDisplayBand(rep); if it changed from the persisted one -> a band mail
    bool first_podium = false;    // one-shot: first podium of the career
    bool first_win    = false;    // one-shot: first win of the career
    std::string final_series;     // series whose contract is in its FINAL season (seasons_left==1; empty = none)

    // Param sources — each pulse fills them in with whatever it has on hand;
    // "" / 0 = no data (the placeholder is stripped at interpolation time, the v0 template still reads fine).
    std::string player;           // first name ({player}) — MailFirstName(save.player.name / playerName)
    std::string series_display;   // readable series name ({series}) from the last race or the relevant contract
    std::string track;            // readable track name ({track}) from the last race
    int race_pos = 0;             // last race's finishing position ({pos}); 0 = this pulse isn't coming from a race
    long credits = 0;             // current balance ({credits})
    std::string team_display;     // the relevant contract's team ({team})
    bool has_race_result = false; // true ONLY on the post-settle pulse (enables race beats)

    // TEAMOPS — only the post-settle fill site (via FillSettleContractCtx)
    // fills these in. deductible/cleanliness=-1 disable the bill (0 damage credits / no cleanliness data
    // yet); per_race=0 with no salaried contract (the bill falls to the credits floor, MK_MAILOPS_DEDUCT_MIN_CR).
    long deductible  = 0;         // insurance deductible for THIS race (SettleReport.deductible)
    // cleanliness=-1 means no cleanliness data yet (EMA not computed yet) -> the
    // damage-bill tone ladder (GenerateCareerEmails) treats -1 as the default NEUTRAL case
    // ("ops_damage_calm"), NEVER the harshest one ("ops_damage_warn"): without an EMA there's no basis to infer carelessness.
    int  cleanliness = -1;        // current cleanliness EMA (g_save.prog.cleanliness); -1 = no data
    long per_race     = 0;        // per-race pay from the relevant active contract (0 = no salaried contract)

    // The next actionable event (NextEventLookahead) — filled in ONLY by the other code path
    // (RunWorldTick, the AdvanceWeek/AdvanceDays pulse). A same-day post-settle pulse (overlay) does NOT
    // touch it: there's no point warning about treasury/track-history in the SAME instant you settle.
    NextEventInfo next_event;

    // On-track rivalry — filled in ONLY by the post-settle overlay path, same as
    // deductible/cleanliness/per_race above: captured in the head-to-head block (career_overlay.cpp, the same
    // reused `cls` classification) for the classified CAST driver ADJACENT to the player
    // (|Δpos|==1). An empty duel_rival_id = no rivalry this race (nobody in the cast has a stable id at that distance,
    // or the race has no grid) -> the emitter doesn't fire. duel_rival_name = DriverById(duel_rival_id)->name ("" if it doesn't
    // resolve). duel_won = that specific rival finished BEHIND the player in the results; race_clean = r.clean
    // for THIS race (the same causal signal that feeds clean_bonus and the cleanliness EMA).
    std::string duel_rival_id;
    std::string duel_rival_name;
    bool duel_won = false;
    bool race_clean = true;

    // Title fight — filled in ONLY by the post-settle overlay path, with the
    // REAL standings signal from the season just raced (FinalTable + CalendarForSeries, the same pattern as
    // CalUiMyChampionships). press_series = the series the title is being fought in; press_standing_pos = the
    // player's championship standing (index+1; 0 = not found -> the emitter doesn't fire); press_rounds_left = the
    // remaining rounds on the calendar. An AdvanceWeek pulse leaves them at 0/"": the title-fight
    // emitter only fires on settle, by construction.
    std::string press_series;
    int press_standing_pos = 0;
    int press_rounds_left  = 0;
};

// Fills in team_display/per_race/final_series from the relevant ACTIVE contract for the
// post-settle pulse (career_overlay.cpp). Multi-contract careers (career_save.h:318): save.contracts
// can have 0..N active at once (different series) -> team_display/per_race (the ones that sign the damage
// bill and clean streak mails) must come from the contract for THE SERIES just raced (raceSeriesId ==
// g_save.race_log.back().series_id), not "whichever active one comes first" (that used to attribute TEAMOPS to a team/series
// unrelated to the race in multi-contract careers). raceSeriesId=="" (privateer/standalone race) or no contract for that series
// > team_display/per_race are left empty/0 (the TEAMOPS emitter already falls back to Atenea in that case, an existing rule
// in emitOps). ctx.final_series follows its OWN rule (the contract with seasons_left==1), independent of which
// series was raced -> it can differ from the contract that supplied team_display/per_race. Doesn't touch ctx.year/day/etc.
// (the caller fills those in separately). PURE + testable (extracted from the overlay path so the test can cover it
// directly, since career_overlay.cpp isn't part of the test build).
void FillSettleContractCtx(const SaveData& save, const std::string& raceSeriesId, EmailGenContext& ctx);

// The duel template NEVER asserts what the engine doesn't actually know -- duel_won and
// race_clean force the coherent half of the variants (0/1 vs 2/3); the hash only picks within that half.
void RivalDuelTemplate(int beefLevel, double aggr, bool duelWon, bool raceClean, unsigned hash,
                       std::string& keyOut, int& variantOut);

// The mail generator. Emits EXACTLY TWO families of market::EmailDef: (A) OFFER —
// one per freshly-surfaced DIRECT offer (not held, not rejected), team-principal voice, offer=true/CTA to the Market; (B)
// PROGRESS/MILESTONE — crossing a REP band, first podium/win one-shot, the late-stage title fight, final
// season of your own contract. NEVER produces the "you let it slip" family. Dedup by
// content-derived id against save.emails (a 2nd call does NOT duplicate). Persists every new mail as a GeneratedEmail
// (id+keys+payload_key, NEVER the body). Applies retention at generation time (MK_INBOX_MAX_*: prunes the oldest
// READ+non-actionable mail, never unread/pending; housekeeping, NOT the offers cap). Uses no RNG or timer.
// Returns the EmailDef for the NEW mails from this call (ones already in save.emails aren't re-emitted).
std::vector<market::EmailDef> GenerateCareerEmails(SaveData& save,
                                                   const std::vector<market::GenOffer>& offers,
                                                   const EmailGenContext& ctx);

// Sponsor-card mail family (DEDICATED, reuses the same mailExists/emitMail
// dedup+persist idiom used by GenerateCareerEmails above; family=2, sponsor voice — the inbox renders it
// the same way). id = "card_"+kind+"_"+cardId+"_"+year+"_"+day -> dedup content-id (a repeat of the SAME event
// this same day doesn't duplicate). kind: 0 = purchase (buy) · 1 = clause triggered (dnf_fine/notop10_lose
// triggered) · 2 = expiration/loss (life used up or card lost to a streak with no top-10). payload_key is a TR
// template key (bilingual-by-TR; body never persisted). Returns the NEW EmailDef(s) (empty if deduped). The
// overlay calls this from CareerSettleFromShm's cards-settle block (fines/expired/lost).
std::vector<market::EmailDef> GenerateCardEmails(SaveData& save, const std::string& cardId,
                                                 int year, int day, int kind);

// ============================ One-off substitution ============================

// Deterministic weekly draw for a ONE-OFF SUBSTITUTION — same pattern as SillySeasonFreeSeats (career_seats.cpp):
// hash(career_seed|year|week) % 100 < MK_SUB_WEEK_PCT (career::MailHash, the same fnv1a-32 as seatFnv1a).
// Gates (one substitution at a time): no draw if save.sub_ride.state != 0 (there's already an
// active/offered one) or if an unanswered family-7 (INVITE) invitation remains. Candidates: reachable series
// (not standalone/branch/era — the same filter as SillySeasonFreeSeats) with level in [frontier_phase-1,
// frontier_phase+1], the gate unlocked (the same criterion as the market: it only blocks if the series IS the destination
// of a qualifier AND that qualifier is not in unlocked_gates), the player's rating within the
// fit band of a WORKS seat (career::ReqRepRating(level,true) ± MK_FIT_UNDER/OVER_MARGIN) and whose NEXT
// round (CalendarForSeries, this year) lands with >= MK_SUB_LEAD_MIN_DAYS of lead time and doesn't clash with the
// absday of the player's own next event (NextEventAbsDay). The series is picked by the SAME draw
// hash (wraps around until it finds a candidate with a real-name OCCUPIED seat, SeatsForClass). Once chosen:
// it sets up save.sub_ride (state=1, fee=MK_SUB_FEE_BASE×level) and emits the mail from the seat's team principal
// (family 7 INVITE, subtype 0, payload_key "invite_sub", id "subride_<sid>_<absday>", variant hash%2).
// Excludes candidates where the player ALREADY has an active contract (ContractForSeries != null): there's nobody to
// substitute for on your own team. The seat is only PINNED (sub_ride.state=1) when the
// mail is actually emitted — a dedup hit in mailExists (an already-used id) pins nothing; it tries the next
// candidate instead of leaving sub_ride.state=1 with no mail (a softlock). pruneInbox(save) runs
// after emitting (the same housekeeping pattern as GenerateCareerEmails/GenerateCardEmails; unit-scoped, anonymous
// namespace). outMailIds (optional) receives the mail's id if one was emitted — the caller
// (RunWorldTick) threads it into res.generated_mail_ids, same as GenerateCareerEmails/EmitNegoMail.
// Called from RunWorldTick after GenerateCareerEmails. Returns true iff a substitution was just offered.
bool SubRideWeeklyDraw(SaveData& save, const CareerCalendar& cc, const std::string& playerName,
                       std::vector<std::string>* outMailIds = nullptr);

} // namespace career
