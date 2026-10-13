#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include "career_season.h"     // career::SeasonState, career::SeasonOutcome
#include "career_calendar.h"   // career::CalSlot, career::CareerCalendar (engine; no ImGui, no career_state.h)
#include "career_contract.h"   // C1: career::AdvanceReport (career::StopRules lives in career_save.h and arrives through here)
#include "career_market.h"     // market::GenOffer / OfferGenInput / GenerateOffers (Market bridge)
#include "career_living_data.h"  // living::MemoryDef referenced by the UI bridges (forward use)
#include "career_profile.h"    // career::RivalRow / ArcPoint for the ProfileRivalsUi/ProfileRatingArcUi bridges
#include "career_ui.h"         // CareerEventParams (for CalibrationEventParams -> onboarding via the vehicle screen)
// overlay<->UI bridge for the SEASON. Implemented in career_overlay.cpp (owns g_seasonData);
// used by screen_season.cpp (the views) and the overlay itself (handoff/settle/return hooks).
namespace career {

void SeasonUiEnsureData();                 // lazy load of series/calendar/points/profiles/fields/roster/liveries
// moves the save's seat keys from INDEX to SLOT. It needs both things loaded
// —save and catalog— and the order between them is not guaranteed, so both ends call it and the one that
// arrives second does the work. Idempotent and cheap. It lives in the shared bridge (career_ui_bridge.inc),
// so the game and the double run it the same.
void EnsureSeatKeysMigrated();
// 0.7.0: repairs once per game the skipped multiclass appointment rounds that scored other classes
// (career_calendar.h::RepairSkippedMeetingRounds). It needs the XMLs up to date: PregenAllAi and the double call it
// after writing or reading them.
void CalUiRepairSkippedMeetingRounds();
bool SeasonUiActive();                     // there's a season in progress (status 0)
const SeasonState* SeasonUiState();        // state of the active season (nullptr if none)
void SeasonUiStart(const std::string& seriesId, const std::string& branch);  // starts + persists
void SeasonUiAbandon();                    // discards the active season
bool SeasonUiFinished();                   // true if the season just closed (show the bonus panel)
SeasonOutcome SeasonUiLastOutcome();       // the bonus from the last FinishSeason (valid if SeasonUiFinished())

// Championship calendar bridge (implemented in career_overlay.cpp) ---
// CalSlot / CareerCalendar / SeasonOutcome are already defined by career_calendar.h + career_season.h.

void  CalUiEnsureData();                                   // loads data + g_calendar (idempotent)
std::vector<CalSlot> CalUiMasterCalendar(int frontierPhase);
bool  CalUiEnrolled(const std::string& seriesId);
long  CalUiInscription(const std::string& seriesId);
// Entry is deferred to SETTLE (with a result); the UI shows/gates it.
long  CalUiInscriptionDue(const std::string& seriesId);   // entry still OWED (0 only if already paid/covered)
long  CalUiEntryFee(const std::string& seriesId);         // entry fee per race (EntryFeeFor; 0 if series unknown)
// amount of the car cost covered by the series CONTRACT (team); 0 = no contract.
long  CalUiRaceCoverage(const std::string& seriesId, int rented, int rentPct, long carValue, int raceLaps);   // effective LAP COUNT
long  CalUiRaceTravel(const std::string& seriesId, long trackHash);   // A->B travel cost for the race (net of coverage)
// ARMS a round's launch, IN MEMORY ONLY (calendar slot + settlement
// context). It does NOT enroll, does NOT charge, does NOT DNF or persist anything — all of that happens at settle, with a result.
// returns false if the (series,round) doesn't exist. Does NOT write the grid (CareerApplyEvent does that).
bool  CalUiLaunchSlot(const std::string& seriesId, int round);
std::vector<CalSlot> CalUiConflictsForPending(const std::string& seriesId);  // for the warning modal
std::vector<CalSlot> CalUiConflictsForSlot(const CalSlot& s);   // conflicts between `s`'s weekend and existing entries
// "my championships": one row per enrolled series.
struct ChampSummary { std::string series_id, display; int round_idx, total_rounds, player_pos, field, my_points; bool done; };
std::vector<ChampSummary> CalUiMyChampionships(const std::string& playerName);
const CalSlot* CalUiPendingSlotPtr(const std::string& seriesId);   // optional (may go unused)
// Hub: the NEXT real race = the earliest pending slot among your enrolled series (valid=false if none).
struct NextRaceView { bool valid=false; std::string series_disp, track_name, date; int round=0, track_hash=0; int doy=0; };
NextRaceView CalUiNextRace();
bool  CalUiSeasonFinishedFlag();
void  CalUiClearSeasonFinished();   // consumes the bonus modal's flag (cmd.season.ackBonus); the screen's button does NOT clear it
SeasonOutcome CalUiLastOutcome();

// The agenda as the temporal spine ---
// cursor's day-of-year (g_calendar.cursor_doy): "today's" date on the continuous clock (with CalUiYear -> "14 Mar 2025").
int  CalUiCursorDoy();
int  CalUiEraMemoriesAvailable();   // number of era memories (pre-2000 series) available this week (topbar badge)
// number of advance-time weeks until the next pending event (WeeksToNextEvent over the master calendar
// for the given frontier). 0 = none left (you're already on/past it). For the "jump to next event" button.
int  CalUiWeeksToNextEvent(int frontierPhase);
// Per-championship RESULT card: a CLOSED series (its calendar finished). Gathers live entries
// that just finished (status==1, with a champion from the table) + the archived record (completed_seasons from
// previous years). 'final_pos' = the player's final position (0 = didn't classify/total DNF). For the "Season result:
// <series> — P{n}" moment in the agenda (one per championship, NOT a single monolithic banner).
struct SeasonResultCard { std::string series_id, display, champion; int year = 0, final_pos = 0, field = 0, rounds = 0; };
std::vector<SeasonResultCard> CalUiSeasonResults(const std::string& playerName);
const char* CalUiPlayerName();                                  // player's name (from shared memory) or "Player"
// Living World — IDENTITY / onboarding (player profile; first-launch gate). Implemented in career_overlay.cpp.
bool        CareerIdentityNeeded();                             // true if it's incomplete -> Identity screen, hides everything
void        CareerIdentitySubmit(const char* first, const char* last, const char* sex, const char* nationality, int year, int month, int day);
const char* CareerIdentityFirst();                             // first name (demo "Josh" if empty)
const char* CareerIdentityLast();                              // last name -> lineage (demo "Bo" if empty)
const char* CareerIdentitySex();                               // "male"/"female"/"other"/""
const char* CareerIdentityNationality();                       // ISO country code (2 letters) or "" if not chosen
int         CareerIdentityAge();                               // age = world year - birth year (0 if no data)
const char* CareerIdentityAmsName();                           // AMS2 name (from shared memory) to pre-fill the profile
const SeasonState* CalUiEnrollment(const std::string& seriesId);// a series' entry (or nullptr)
// item 5 (skin=team): locks in car+skin (vehHash+ordinal) for the series' season if none was set yet.
// No-op if already locked / no entry / invalid values. Persists. Returns whether it locked. Implemented in career_overlay.cpp.
bool CalUiLatchLivery(const std::string& seriesId, int vehHash, int liveryOrd);
// item 5: ARMS the privateer's DEFERRED latch (locked in at RACE COMPLETION, at settle). Empty seriesId = disarms it.
void CalUiArmLiveryLatch(const std::string& seriesId, int vehHash, int liveryOrd);
// the privateer's DURABLE pick (save.privateer_pick) — car+skin "married" to the series, survives
// rollover. false if there's no pick. Fallback for the pre-race preset when there's no enrollment/latch yet.
bool CalUiPrivateerPick(const std::string& seriesId, int& vehHash, int& liveryOrd);
// item 5 (contract): the FIXED-RANDOM car+skin the team assigns in the series (deterministic, not persisted).
// Returns false if there's no contract with a fixed car. vehHash/liveryOrd = the car + the seat's skin ordinal.
bool CalUiContractSeat(const std::string& seriesId, int& vehHash, int& liveryOrd);
// Is this launch a FACTORY seat (salary + EVERYTHING covered)?
// FACTORY REQUIRES a works contract (kind==0): only then does the team pay you and cover costs/entry. A
// TEAM car under customer/pay-driver is NOT factory. And an OWN car with NO contract (heritage/gift/era,
// standalone car) isn't either: it's driven as OWN/RENTAL -> pays its own costs and collects the race purse, 0 salary
// (a deliberate design decision). Mirrors settle EXACTLY, which now uses only ContractFactorySeat.
bool CalUiFactorySeatForRace(const std::string& seriesId);
// Variable-salary design: the PER-RACE salary shown in pre-race == what settle actually pays (per_race of
// the series' active contract), NOT the flat FACTORY_SEAT_SALARY constant. 0 if there's no contract (e.g. customer/pay-driver).
long CalUiContractPerRace(const std::string& seriesId);
// can the player enter this series as a privateer? (wrapper over g_save's CanEnterSeries). The
// calendar UI checks this to block launching a CLOSED series with no contract.
bool CalUiCanEnterSeries(const std::string& seriesId);
// item 5 (contract): PREVIEW of the car+skin a TEAM assigns in an OFFER (before signing). Deterministic per
// (series|team) -> matches the seat after signing. false if the contract type doesn't fix the car. Implemented in the overlay.
bool CalUiOfferSeat(const std::string& seriesId, const std::string& teamId, int contractKind, int archetype,
                    std::string& carName, std::string& carSlug, int& liveryNN);
bool CalUiGatePassed(const std::string& champId);               // has the championship gate been cleared? (save)
int  CalUiSkillForLevel(int level, const std::string& classId, long trackHash, bool wet = false);   // 09/09: `wet` = forecast wet weather -> the category's wet number if it already has one   // effective AI skill for the given level, according to the chosen difficulty
bool    CareerCalibrated();          // was the calibration race completed? (onboarding gate)
CalSlot CalibrationCalSlot();        // synthetic slot for the calibration race (Kingsmere Uma713 @ Stoneton Bay)
void    LaunchCalibrationRace();     // launches the DIRECT calibration race (`calib` command): Kingsmere Uma713 @ Blackvale Indy, 4 laps, free
CareerEventParams CalibrationEventParams();   // calibration params; onboarding puts them into st.launchEp and launches via the VEHICLE SCREEN (Rent flow, commits s12)

// lifecycle (advance time + break contract). Year rollover is no longer a button ---
// are ALL enrolled seasons finished? (AllSeasonsComplete over g_calendar). It no longer gates
// any banner (rollover is automatic); it remains a pure helper (useful for a year-end summary).
bool CalUiAllSeasonsComplete();
int  CalUiEnrollmentCount();                                    // number of series entered this year (>0 = season in progress)
int  CalUiYear();                                               // calendar year (g_calendar.year)
// The qualifier/dream-race ENTRY cost for a slot's phase (1..7), reusing QualifierEntryCost.
// The calendar confirm modal shows this; the solvency block compares it against MarketUiCredits(). Pure reads.
long CalUiQualifierCost(int phase);
// Advances N weeks on the CONTINUOUS CLOCK (AdvanceWeek): bumps the cursor + DNFs any rounds left behind +
// AUTO-rolls the year over when crossing Dec 31 (full RolloverYear) + expires offers + courting. Persists
// in order. Returns the resulting cursor_doy. (The old rollover-confirm banner was retired once this became automatic.)
int  CalUiAdvanceWeek(int weeks);
// jumps to the NEXT EVENT in the agenda (career::AdvanceToNextEvent) / advances
// to a target abs_day DATE (career::AdvanceToDate). Same persistence pattern + both return cursor_doy
// like CalUiAdvanceWeek. Additive; the "advance week" button can keep using CalUiAdvanceWeek for compat.
int  CalUiAdvanceToNextEvent();
int  CalUiAdvanceToDate(int targetAbsDay);
// C1 (spec ): the CLOCK WITH STOPS. Runs day by day until the first stop reason according to the
// save's rules (or until untilAbsDay, 0 = no cap) and returns the COMPLETE report of what was crossed. Replaces the three
// advances above as the app's path (CalUiAdvanceWeek/ToNextEvent/ToDate are kept for the native
// overlay). Same persistence pattern as CalUiAdvanceToDate: calendar first, race after.
// 0.7.0 review (finding 1): with `preview`, it adopts that forecast if it still describes the state (does not re-simulate)
career::AdvanceReport CalUiAdvanceUntilStop(int untilAbsDay, career::AdvancePreviewKeep* preview = nullptr);
career::AdvanceReport CalUiPreviewAdvance(int untilAbsDay);          // dry run: does NOT persist or mutate anything
void                  CalUiSetStopRules(const career::StopRules& r); // persists the save
career::StopRules     CalUiStopRules();
// Resolution + preview bridge for the overlay's DOUBLE-CONFIRMATION modal for
// time jumps (calendar: jump to date / jump a year; season end: next championship event). SAME
// selectors that query.time.preview / ResolveTimeTarget (career_api_queries.cpp), over the overlay's LIVE calendar/gates/
// phase. -1 = no reachable event (the caller disables the jump / does not open the modal).
int  CalUiNextEventAbsDayForSeries(const std::string& seriesId, int frontierPhase);
int  CalUiNextEventAbsDayOnOrAfter(int targetAbsDay, int frontierPhase);
// TRUTHFUL preview (career::JumpPreview) for a 'targetAbsDay' ALREADY RESOLVED by one of the two selectors above.
// The caller must NEVER recompute the destination separately: the warning and the actual jump (CalUiAdvanceToDate) must use
// this exact same targetAbsDay, always.
JumpPreviewResult CalUiJumpPreview(int targetAbsDay);
bool CalUiContractActive();                                     // is there ANY active driver contract? (multi-contract)
const char* CalUiContractSeries();                              // series_id of the FIRST active contract ("" if none)
// The active contract's four FACETS surfaced where the player already looks
// (hub/garage). Read-only — the bridge only READS the active Contract (the engine stays pure). NO archetype
// is exposed (internal-only this phase). active=false -> no contract (C1 hidden, C2 empty caption). The screen
// formats the cells via TR(): Duration "Season N of M" (seasonsLeft/seasonsTotal), Money flow "+Cr/race" or
// "Self-funded" (perRace/kind), Role lead/team (roleLead from the accepted_deal NEGO_ROLE_LEAD demand), Objective
// "objDone/objTotal met".
// fixesCar = the active contract's type FIXES the car (customer/works/factory) ->
// the garage shows "Car fixed by team" instead of a picker and uses teamCar (the contract's team_car). For a
// privateer/rental contract fixesCar=false (the player selects). Additive (default false/"" -> back-compat with
// the pre-4.1 strip render). teamCar is the contract.team_car (display id; "" when not yet assigned/privateer).
// typeLabel + the per-category coverage SUMMARY so the Garage
// active-contract strip shows the contract TYPE label + "the team covers X / you pay Y". typeLabel = the
// ContractTypeIndex 0..5 (0 Self-funded .. 5 Factory/works) the screen maps to a TR() label; the four
// covered booleans (seat/entry, consumables, damage, travel) come from the SAME pure OfferPresentationFacets
// (presentation == reality). Additive (default 0/false -> back-compat with the pre-4.1 strip).
struct ContractFacetsView { bool active=false; std::string seriesDisp; int seasonsLeft=0, seasonsTotal=0;
                            long perRace=0; int kind=0; bool roleLead=false; int objDone=0, objTotal=0;
                            int objTargetPos=0;   // 0.4.0 SEASON objective ("finish Top N"; 0 = no objective)
                            bool renewOnTable=false;   // 0.4.0 silly season: an early renewal waiting in the inbox
                            bool fixesCar=false; std::string teamCar;
                            int typeLabel=0;
                            bool covSeatEntry=false, covConsumables=false, covDamage=false, covTravel=false; };
ContractFacetsView CalUiContractFacets();
void CalUiBreakContract();                                      // terminates ALL contracts (the hub's generic button) + persists
void CalUiBreakContractSeries(const std::string& seriesId);     // terminates ONE series' contract (from the market) + persists

// Results: the active championship's standings table (reads g_seasonData/FinalTable in the overlay;
// stubbed to 0/"" in the harness). row 0..N-1; outName/outYou/outTotal return the row sorted by points.
int  CalUiStandingsCount();
void CalUiStandingsRow(int row, char* outName, int nameCap, int* outTotal, int* outYou, int* outRank);
const char* CalUiStandingsSeriesName();
int  CalUiPlayerRank();      // player's position in the table (1-based; 0 if not applicable)
// query.results: display name + color (ImU32 packed as unsigned) for a class by its id. Wraps
// FindClass (lives in career_state.h, which the overlay does NOT include due to the 'struct Track' clash). Implemented in
// career_data.cpp. Returns false if the class doesn't exist (out* left untouched).
bool ClassDisplayColor(const std::string& classId, std::string& outName, unsigned int& outColor);
// lock panel: a row's identity BY CLASS (readable name + color). Cascades from the ladder's class ->
// to the series that runs that class -> raw id + accent color. ALWAYS returns something: a nameless row is the defect that
// this redesign fixes. Implemented in career_data.cpp (the overlay can't include career_state.h).
void ClassRowIdentity(const std::string& classId, std::string& outName, unsigned int& outColor);
// query.times: color (packed ImU32) for a race's accent strip — same criterion as RaceColor in
// screen_championships.cpp: the series' discipline color if it exists, otherwise the class color, otherwise the accent.
// And a grid row's class color (multiclass): ResolveClass(car_class)->FindClass->color,
// with fallback. Both wrap FindClass/SeriesById (career_state.h/career_series.h). Implemented in career_data.cpp.
unsigned int RaceListColor(const std::string& seriesId, const std::string& classId);
unsigned int RowClassColor(const std::string& carClass, unsigned int fallback);
// query.tree: a FLAT mirror of ::LadderNode (career_state.h) for the overlay's provider, which can't
// include career_state.h (the 'struct Track' clash). LadderNodesView() returns the REAL ladder
// (LoadLadder()+LADDER). Implemented in career_data.cpp (the provider's only translation unit that sees ::LADDER). No ImGui.
struct LadderNodeView { std::string id, display, disc; int level = 1; int carCount = 0; bool isDlc = false; int vehid = 0; };
std::vector<LadderNodeView> LadderNodesView();
// query.garage: RESOLVED owned cars (name/class/stats) as a flat view. The overlay can't
// include career_state.h (SAMPLE_CARS/FindClass/CarsByClass) -> this wrapper (career_data.cpp) replicates the
// DrawGarage's loop. classColor = FindClass->color() (packed ImU32). No ImGui in the header.
// price (0.7.0): the car's CarItem.price, the same base price paid when buying it -> resale base.
struct GarageOwnedCarView { std::string id, name, classId, className, classShort; unsigned int classColor = 0;
                            int year = 0, pwr = 0, grp = 0, bal = 0, vehHash = 0, livery = -1; long price = 0; };
std::vector<GarageOwnedCarView> GarageOwnedCars();
// query.prerace / cmd.prerace.*: the PRE-RACE screen (screen_vehicle.cpp) computes over CarsByClass/FindClass/PendingRace/
// PreraceLock/SampleState (career_state.h), which the overlay/handler can NOT include (the 'struct Track' clash). These
// POD wrappers (defined in screen_vehicle.cpp, the translation unit with all the types+bridges) replicate DrawVehicle: PreraceUiCompute
// builds the read view (a flat mirror of capi::PreraceView, colors as unmapped ImU32), PreraceBuy/PreraceLaunch
// mutate (purchase / handoff to Custom Race). The overlay copies PreraceUiData->capi::PreraceView field by field. No ImGui.
struct PreraceUiCar {
    std::string id, name, classShort;
    int  year = 0, pwr = 0, grp = 0, bal = 0;
    long price = 0;
    int  rentPct = 0, vehHash = 0;
    bool factory = false, owned = false;
    ImU32 discColor = 0;
    std::string slug;   // image slug (SlugForVehicleEnsured) -> the app builds the PNG path
};
struct PreraceUiData {
    bool loaded = false, clsEmpty = false;
    std::string raceName, className;
    ImU32 discColor = 0;
    std::string trackName;
    int  trackHash = 0, laps = 0, durationMin = 0, grid = 0;
    long reward = 0;
    std::string reasonKey;
    bool lockCar = false, lockSkin = false, lockDuration = false;
    int  presetVehHash = 0, presetLivery = -1, presetDurPct = 100;
    int  divorceRep = 0;             // v2: the cost of changing identity MID-SEASON (0 = free)
    long divorceCr  = 0;             // renting does NOT cost REP (privateer rule): the old rentRep field was retired
    std::string ratingTierLabel;
    ImU32 ratingTierColor = 0;
    long credits = 0;
    bool visual = false;
    std::string seriesId;
    bool freeEntry = false;
    int  raceLaps = 0;
    std::vector<PreraceUiCar> cars;
    std::string selCarId;
    int  selVehHash = 0;
    bool selIsOwned = false, selFactory = false;
    std::vector<int> liveryNN;
    int  liveryCount = 0, ownedLiveryOrdinal = -1;
    std::vector<int> ownedLiveries;  // 1:1 Memories: livery ordinals OWNED for the selected car (car+skin units)
    long costTyres = 0, costFuel = 0, costFluids = 0, costInsurance = 0, costTotal = 0;
    long entryFee = 0, inscDue = 0, travel = 0, coverAmt = 0, rentCost = 0;
    long outlayRent = 0, outlayOwn = 0, totalRent = 0, totalOwn = 0;
    long factorySalary = 0;
    bool canBuy = false, canRent = false, canRaceOwn = false, seatOk = false;
    int  launchLaps = 0, launchDurationMin = 0;
};
PreraceUiData PreraceUiCompute(const std::string& carId);   // read-only: does NOT mutate st.vehSel (carId = the detail's car)
bool PreraceBuy(const char* carId, int liveryOrdinal);      // buys the car + persists the skin (ordinal); false if it can't proceed
bool PreraceLaunch(const char* carId, int action, int liveryOrdinal, int durationPct);   // action 0=own,1=rent; arms+launches the race
// query.market: display name + DISCIPLINE color (Col::Disc(cd->disc), NOT cd->color()) for a class, by
// id, for an offer's discipline dot. Wraps FindClass (career_state.h, inaccessible to the overlay/handler).
bool MarketClassInfo(const std::string& classId, std::string& outName, unsigned int& outDiscColor);

// DRIVER MARKET bridge (implemented in career_overlay.cpp; owns g_save/g_calendar) ---
// Generates DYNAMIC offers from the player's live state (rep/phase/year/calendar cursor) +
// gates (g_save.unlocked_gates). Doesn't create the .cpp; the screen renders the vector. Deterministic.
std::vector<market::GenOffer> MarketUiOffers(int rep, int frontierPhase, int year, int cursorDoy);
// An offer's calendar conflict: already-enrolled series_ids whose weekend overlaps (g_calendar). Empty = OK.
std::vector<std::string> MarketUiConflicts(const market::OfferDef& offer);
// An offer's persisted state: 0=undecided, 1=active contract for THAT series, 2=declined.
int MarketUiOfferState(const std::string& offerId);
// Accepts the offer: AcceptOffer (contract + waived entry + signing bonus) + persists save/calendar. false
// if there was already an active contract (v1: only one) or the series is invalid. 'year' = the calendar year at signing.
struct DealTerms;   // fwd (career_negotiate.h): the negotiated package is passed by pointer
// 'terms' (optional) = the negotiated package (g_terms from Negotiate); nullptr (Accept from the Market / "as-is") signs the base offer.
// sponsorCardId (sponsor cards, optional): card_id of the card GIVEN UP as a sponsor in the negotiation
// ("" = none). If signing succeeds, that card is REMOVED from g_save.cards (given up, no sale/refund).
// whyNot (optional): if signing FAILS, this holds the reason key — "window" (market closed, this layer's own
// lock), or a block from AcceptOffer via AcceptRejectReason ("already"/"exclusive"/"afford"/
// "refused"). Left untouched on success. nullptr = the old behavior (existing callers unaffected).
bool MarketUiAccept(const market::OfferDef& offer, int year, const DealTerms* terms = nullptr,
                     const std::string& sponsorCardId = std::string(), std::string* whyNot = nullptr);
// Declines the offer: g_save.declined_offers.insert(id) + persists the save.
void MarketUiDecline(const std::string& offerId);
long MarketUiCredits();                                         // live credits from the save (refresh st after accepting)
bool MarketUiHasContract();                                     // do you already have an active contract? (v1: only 1)
const char* MarketUiContractLabel();                            // "TEAM · SERIES · type" of the active contract ("" if none)
// GENERATED emails, persisted (OFFER + PROGRESS/MILESTONE). The inbox reads them from
// save.emails and persists read/answered HERE (they moved out of screen_inbox.cpp's statics). The "you let it
// pass" family (MarketUiExpiredOffers) was ELIMINATED entirely: silent expiry, no email.
// Living emails: 4 queue fields mirroring GeneratedEmail (career_save.h) —
// variant (persisted deterministic variant), params (MailParamsEncode, "" = no params/old email),
// from_name (raw sender-persona; "" = fallback by family) and from_role_key (the role's TR key).
struct UiGeneratedMail { std::string id, payload_key; int tag, day, family, read, answered, choice, subtype;
                          int variant = 0; std::string params, from_name, from_role_key; };
std::vector<UiGeneratedMail> MarketUiMails();                  // generated emails, persisted (empty if none)
void MarketUiMarkRead(const std::string& mailId);             // marks it read (persists GeneratedEmail.read=1)
// MarketUiMarkAnswered (marks answered+choice) was retired — it was orphaned after the
// ImGui cleanup, with no caller left; MailAnswerCommit (career_mail.h) is the live path for answered/choice.

// Bridges (Memories / Championships / Rollover / Tryout) ---
// Live wherever the corresponding engine phase exists; otherwise STUB returning spec-shaped data.
// (The Sponsors bridges — SponsorUiSigned/SponsorUiSign/SponsorUiMy*/SponsorUiCredits/
// SponsorUiSignedInTier/SponsorUiSlots/SponsorUiCurrentDisc/SponsorUiAcqCost/SponsorUiTop8/SponsorUiEndurance
// — retired with the objective-sponsor catalog; the cards system has no ImGui-overlay bridge, it's app-only.)

// Era memories. count = times cleared (0 = never), used to pick first-clear vs replay reward.
int  MemoryUiClearedCount(const std::string& memoryId);
// status: 0=locked (level/gate not met or prev gen not cleared), 1=available, 2=completed.
int  MemoryUiStatus(const std::string& memoryId, bool prevGenCleared);
void MemoryUiEnter(const std::string& memoryId);        // enroll the ancestor seat + arm launch

// Championships. One standings table for the player's enrolled series of class 'clsId'.
// NOTE: named ChampUiRow/ChampUiView (NOT ChampRow/ChampView) to avoid colliding with the engine's
// career::ChampRow in career_standings.h (the engine's display row). These are the bridge's view-models.
struct ChampUiRow { std::string name, team; bool you; int rank, total, wins;
                  std::vector<int> pos;            // 1 = round not yet run; else finishing position
                  std::vector<int> ptsPerRound;    // points earned that round (0 if pos<0)
                  int moveLastRound; int ptsLastRound; };
struct ChampUiView { std::string seriesId, display, clsId; ImU32 color;
                   int roundsTotal, roundsDone; std::vector<std::string> roundNames;
                   std::vector<ChampUiRow> rows; };
std::vector<ChampUiView> ChampUiStandings(const std::string& playerName);

// STANDINGS tab = TIME LOG (not championship positions). One row per race run in career
// mode (best lap / total time / result / track / class / date). RAW fields: the screen maps
// class_id->display+color (FindClass/Col::Disc) and doy->date (DoyToDate). Most recent first.
// One row of the FULL grid, for the times screen (mirrors RaceGridRow from the save,
// decoupled from career_save.h). gap_best = best_lap - the race's best best_lap (computed by the screen, here
// raw). laps_t is reserved for future use.
struct RaceGridRowView {
    int pos = 0;
    std::string name, driver_id, car_class, car_name;
    int  laps = 0;
    float best_lap = -1.0f, total_time = -1.0f;
    bool dnf = false, is_player = false;
    bool finished = true;      // false = still on track when captured (partial laps/total; deficit-1 = lapped)
    std::vector<float> laps_t;
};
struct RaceLogView {
    std::string class_id, series_id, track;
    int pos = 0, field = 0, year = 0, doy = 0;
    bool finished = true;   // P2 (F15): false = abandonment; `pos` is where you left (RaceLogEntry::finished)
    float best_lap = -1.0f, total_time = -1.0f;
    std::vector<RaceGridRowView> grid;   // the race's full grid (empty for pre-feature races / old saves)
};
std::vector<RaceLogView> RaceLogUiEntries();

// The de-mocked Profile world-ranking bridge (implemented in career_overlay.cpp). ---
// The overlay OWNS g_save; these hand the screen the inputs for the PURE career::ProfileWorldRanking/ProfileRating
// (which live in career_profile.cpp) without leaking g_save into the screen. ProfileWorldPoolUi = the live
// per-AI rating pool (g_save.driver_ratings). ProfileActiveModernUi = ActiveModernSet over g_save's birth_year/
// retired + the world clock, optionally restricted to one series (the screen's series filter); cached per
// (seriesFilter,year) so it isn't recomputed every frame. ProfilePlayerRatingUi = the player's persisted
// prog.rating; ProfilePlayerDeltaUi = the per-race delta from the last settle (0 if no race yet).
const std::map<std::string, double>& ProfileWorldPoolUi();
const std::set<std::string>&         ProfileActiveModernUi(const std::string& seriesFilter);
int ProfilePlayerRatingUi();
int ProfilePlayerDeltaUi();
// The set of modern series ids that currently HAVE active drivers, for the screen's series-filter dropdown
// (display + id). Empty entry (id "") = "All / Todas". Computed from g_save's world; stable per session.
struct WorldFilterOption { std::string id, display; };
std::vector<WorldFilterOption> ProfileWorldFilterOptionsUi();

// The de-mocked Profile RIVALS + career-ARC bridges (implemented in career_overlay.cpp). ---
// Same split pattern as ProfileWorldPoolUi above: the overlay OWNS g_save; these read it and hand the PURE
// career::ProfileRivals/ProfileRatingArc (career_profile.cpp) their real inputs WITHOUT leaking g_save into
// screen_profile.cpp. ProfileRivalsUi() assembles (player rating, the union of the active-contract classes =
// the proximity scope, g_save.driver_ratings, g_save.h2h, and the seat-market rival id+rating+name from
// WeakestIncumbentRating over the highest-tier active-contract series) -> career::ProfileRivals(...).
// ProfileRatingArcUi() hands career::ProfileRatingArc(g_save.race_log, prog.rating, es) the real log. BODIES are
// DEFINED separately (rivals / arc) in career_overlay.cpp, and STUBBED in careerproof.cpp (which
// does NOT link career_overlay.cpp) with real-shaped seed data for the profile/profile_es PNGs.
std::vector<RivalRow> ProfileRivalsUi();
std::vector<ArcPoint> ProfileRatingArcUi();

// Rollover. The aging-engine output for the end-of-season screen.
struct RolloverRetire { std::string name, team; int age; std::string reason_es, reason_en; };
struct RolloverPromo  { std::string name, from_es, from_en, to_es, to_en; };
// 23/09 (): the row of a promotion, by field NAME. The report carries levels, not classes, so the
// level is labelled. Before, it was built by position in the bridge and the English fell into the Spanish field.
inline RolloverPromo RolloverPromoRow(const std::string& name, int fromTier, int toTier) {
  RolloverPromo p;
  p.name    = name;
  p.from_es = "Nivel " + std::to_string(fromTier); p.from_en = "Tier " + std::to_string(fromTier);
  p.to_es   = "Nivel " + std::to_string(toTier);   p.to_en   = "Tier " + std::to_string(toTier);
  return p;
}
struct RolloverMove   { std::string driver, from, to; ImU32 color; };
// The renewal-terms summary the rollover screen surfaces above the 3 choice
// buttons. renewState classifies the shifted RENEWAL OfferDef in SaveData::pending_renewals (matched by the
// active contract's series_id): 0=better / 1=same / 2=worse / 3=none-offered. hasRenewal=false (state 3) when
// no entry matches -> Renew is DISABLED. Derived read-only in RolloverUiPreview from the precomputed quality on
// the offer (degrades to commitment-based when a save round-trip dropped the transient quality). renewPerRace =
// the shifted +Cr/race carried by the offer (for the summary line copy). The screen routes "New objectives"
// into Negotiate at negotiationLevel 2 reusing the same OfferDef (id "of_"+series); the bridge owns g_save.
enum RolloverRenewState { RENEW_BETTER = 0, RENEW_SAME = 1, RENEW_WORSE = 2, RENEW_NONE = 3 };
struct RolloverView   { std::vector<RolloverRetire> retirements; std::vector<RolloverPromo> promotions;
                        std::vector<RolloverMove> marketMoves; std::string yourTeam, yourClass; int nextYear;
                        int movesTotal = 0;               // 18/09: winter signings (the list is a sample)
                        // Renewal summary:
                        bool hasRenewal = false; int renewState = RENEW_NONE; long renewPerRace = 0;
                        std::string renewSeries;          // series_id of the renewal (for "New objectives" routing)
                        // The season INSURANCE-PREMIUM mail-bill charged this
                        // rollover (sum across held contracts, cleanliness-priced). 0 = no premium this rollover
                        // (no held contract / already billed / insolvent). The screen renders it as a season
                        // mail-bill communicating the cleanliness->price loop. cleanliness is the 0..100 EMA so the
                        // bill copy can frame "cleaner next season = lower premium".
                        long insurancePremium = 0; double cleanliness = 0.0;
                        // 17/09: the winter report is SHOWN as a sample, but the news is the TOTAL —
                        // a winter moves a hundred-odd people and the screen fits eight. `reportYear` is the
                        // year that closed; 0 = this game has not gone through any winter yet, and then the
                        // lists are empty on purpose (before they came out as two handwritten names).
                        int retiresTotal = 0, promosTotal = 0, reportYear = 0; };
RolloverView RolloverUiPreview();
void RolloverUiConfirm(int contractChoice);             // 0=renew 1=newGoals 2=decline -> advance season
bool RolloverUiConfirmApply(int choice);   // 18/09: real Renew/Decline; false = the signing failed
// Routes "New objectives" (choice 1) into Negotiate on the pending RENEWAL OfferDef (negLevel 2). Returns the
// offer id ("of_"+series) to localize via MarketUiOffers-style resolution; "" if no renewal is pending.
std::string RolloverUiRenewOfferId();

// Tryout. The trial is a POSITION race: target = a Top-N (or beat-rival) objective,
// NOT a lap time. target_es/target_en already hold the formatted position-objective string (e.g. "Finish Top 8").
// topN = the absolute Top-N target handed to the real race launch (TrialUiLaunch); the result verdict is binary
// (PASS = final position <= topN), read from the real settled position via TrialUiResult* (no roll).
struct TryoutStake { std::string r, es, en; ImU32 color; };
struct TryoutView  { std::string clsId, trackId, target_es, target_en, standard_es, standard_en, team;
                     std::vector<TryoutStake> stakes; int topN = 0; int trackHash = 0; };
TryoutView TryoutUiActive();
// ARMS the trial from the Market offer (the "Take trial" CTA). Resolves the OFFER's specific
// series/class/track/car -> TryoutUiActive renders the real brief and TrialUiLaunch launches the real race
// (a grid of 5 IDENTICAL cars, same rules, free). Without this, the brief falls back to sample data.
void TrialUiArm(const market::OfferDef& o);
// result: "pass"|"ok"|"fail" derived from the player's unified rating (RatingQuality), deterministic
// per (rating, seed) so the same eval reproduces; seed is the frame's tryout attempt counter.
// LEGACY: the fake roll. SUPERSEDED by the real-position verdict (TrialUiResult*). Kept only so any
// straggler caller links; screen_tryout no longer calls it (the real settled position drives the verdict).
std::string TryoutUiRoll(int rating, unsigned seed);

// The REAL trial verdict bridge. After a trial race settles, the overlay publishes
// the final position vs the fixed Top-N here so screen_tryout's result panel reads the REAL outcome (no roll).
//   TrialUiHasResult() -> true once a trial race has been settled (a verdict is available).
//   TrialUiPass()      -> 1 = passed (final position <= Top-N), 0 = failed (binary).
//   TrialUiPos()/Field()/TopN() -> the real settled position, grid size, and Top-N target (for the result copy).
//   TrialUiLaunch(topN) -> arm + launch the real trial race via CareerApplyEvent with the trial flag (Start CTA).
//   TrialUiClearResult() -> consume the verdict (call when leaving the result screen).
//   TrialUiOfferId() -> the id of the OFFER this verdict belongs to (g_trialResultValid/Pass are
//   NOT keyed by offer on their own — callers negotiating a DIFFERENT offer must compare this before trusting Pass().
bool TrialUiHasResult();
const std::string& TrialUiOfferId();
int  TrialUiPass();
int  TrialUiPos();
int  TrialUiField();
int  TrialUiTopN();
void TrialUiLaunch(int topN);
void TrialUiClearResult();

// Data (plain POD) for the already-armed TRIAL (TrialUiArm), to route it through the real PRE-RACE screen instead of
// launching it directly. The car+skin = the TEAM's seat (ResolveTeamSeat, deterministic) -> matches the
// contract if signed. armed=false if no trial is armed. Implemented in career_overlay.cpp (owns g_trial*).
struct TrialSeatInfo { bool armed = false; std::string series, className, trackName; int trackHash = 0, vehHash = 0, liveryOrd = -1, topN = 0, aiSkill = 0; };
TrialSeatInfo TrialUiSeatInfo();

} // namespace career
