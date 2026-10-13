// career_api_queries — READ-ONLY queries for career_api against the engine's REAL state.
// JSON contract (from the external-app migration design) — this IS the
// external UI's contract; the app/ui-fixtures fixtures are generated from these very same handlers):
//   query.career_state -> { "profile":{"name","money","eloFame","xp","week","year","dateDoy"},
//                           "contract":{"seriesId","seriesName","teamName","raceSeat"} | null,
//                           "flags":{"inSeason","pendingResults"} }
//   query.standings {seriesId?} -> { "seriesId","seriesName",
//                           "rows":[{"pos","driverId","driverName","teamName","points","isPlayer"}] }
//   query.inbox -> { "messages":[{"id","week","year","from","subject","body","read","kind"}] }  newest->oldest
//
// SOURCES (hard rule: the same access paths as the screens, without duplicating formatting logic):
//   career_state -> SaveData.prog (credits/rating/xp) + CareerCalendar (year/cursor_doy) + the
//                   active contract (mirrors CalUiContractFacets: the FIRST active contract).
//   standings    -> FindEnrollment + FinalTable (THE same table screen_season.cpp draws) +
//                   DriverIdForName (stable id from the pool). AI teamName = "" (mirrors the live
//                   ChampUiStandings, which doesn't model a team per AI); the player's row carries the
//                   team_id from their contract in that series, if any.
//   inbox        -> SaveData.emails (persisted GeneratedEmail entries). from/subject/body are
//                   i18n keys/payload_key (bilingual via TR: the resolved body is NEVER persisted;
//                   the app resolves them the same way screen_inbox.cpp resolves with TR()).
//
// THREADS: the handlers run on the GAME THREAD (ApiCore::Pump from the frame tick) and read
// live state through the provider. The state's OWNER installs it at startup (career_overlay.cpp:
// g_save/g_calendar/CalUiPlayerName); career_test installs a synthetic one. With no provider, or with
// loaded=false -> HandlerResult { ok=false, code="not_ready" }.
#pragma once
#include "career_api_core.h"
#include "career_profile.h"   // ProfileRatingInfo / ArcPoint / RivalRow / RankRow view-models (query.profile)
#include "career_calendar.h"  // career::CalSlot for CalendarView (query.calendar); engine header, no ImGui
#include "career_save.h"      // SignedCard/CardShopState travel BY VALUE in CardsView (a fwd-decl isn't enough)
#include "career_contract.h"  // C1: career::AdvanceReport (career::StopRules lives in career_save.h, included above)
#include "career_difficulty.h"   // dda::kCustomDefault — default value for SettingsView.customAi
#include <vector>
#include <string>
#include <map>
#include <utility>

// fwd (json_write.h is NOT dragged into the header: only the .cpp files that serialize use it; see the forward
// declaration rule. career_save.h/career_calendar.h ARE included above: CalSlot/SignedCard/CardShopState
// travel by value in several views, so the complete type is needed, not just a fwd-decl of the struct.)
namespace jsonw { class Writer; }

namespace capi {

// writes "paths":{data,userdata,gameInstall,gameUserDir} + "paths_ok" (data and userdata exist
// on disk) into an already-open writer object. Used by query.api_info (career_api.cpp) so the
// app's health-check can confirm the DLL resolved the SAME roots the setup wizard wrote.
void WritePathsInfo(jsonw::Writer& w);

// View of the live state the queries read. The pointers must stay alive for as long as the server
// is running (in the overlay they're globals; in tests, harness statics).
struct CareerStateView {
    bool loaded = false;                              // false -> not_ready
    const career::SaveData* save = nullptr;           // g_save (overlay) / synthetic (tests)
    const career::CareerCalendar* calendar = nullptr; // g_calendar / synthetic
    std::string playerName;                           // stable name for standings (CalUiPlayerName)
    int resultSeq = 0;                                // MONOTONIC settlement counter: the app, while in
                                                      // "launched" state, polls it; once it increments -> a NEW result ->
                                                      // navigate to Results. Mirrors the overlay's g_settleSeq.
    bool simRace = false;                             // SIMULATION mode active (flags.simRace) — the
                                                      // debug menu draws the toggle, and the shell doesn't minimize on launch.
    // 05/09 ([tester]): SETTLEMENT WITHHELD. Finishing/retiring and LEAVING the session are two steps, and between
    // between, the engine keeps the snapshot UNSETTLED until the race really ends (RaceIsOver = the leader
    // crossed the line) or the player leaves the session. Measured in [tester]'s logs: 17, 17, 26, 10, 9 seconds.
    // The app declared ABANDON after 6 s (2 polls of 3 s), went back to the calendar and killed its interval; the
    // result arrived afterwards, on a screen nobody was watching any more. With this the app can tell "left
    // without racing" from "still settling" and waits. Mirrors the overlay's own g_pendSettleValid.
    bool settlePending = false;
    // Per-tab badges: same counts as the overlay's topbar (career_ui.cpp). 0 in tests.
    int badgeMarket = 0;      // direct offers coming after you (sufficient REP, not signed/occupied)
    int badgeInbox = 0;       // unread emails
    int badgeSponsors = 0;    // sponsors signable RIGHT NOW (requirement met, not signed)
    int badgeMemories = 0;    // era memories available this week
};
using CareerStateProvider = std::function<CareerStateView()>;

// Installs the provider (init, game thread). nullptr -> queries respond not_ready.
void SetCareerStateProvider(CareerStateProvider p);

// query.profile: PROFILE view (contract in app/src/api/contracts.ts / screen_profile.cpp) ---
// The state's OWNER (overlay) ASSEMBLES this view by calling the SAME bridges the screen
// uses (ProfileRatingArcUi / ProfileRivalsUi / ProfileWorldRanking / ...), so the handler does NOT duplicate logic:
// it only serializes + maps presentation (color ImU32 -> "var(--x)", band letter -> i18n key,
// last arc point -> "profile.now", top-10 slice + neighborhood). The rating is the ONLY number
// from the engine; hero/arc/rivals/ranking are 4 views of that same score. The series filter arrives via
// params.filter, and the view is built FOR that filter (a read-only equivalent of st.prof_filter).
struct ProfileFilterOptView { std::string id, display; };   // display "" on the "All" entry
struct ProfileView {
    bool loaded = false;                                     // false -> not_ready
    std::string playerName;
    std::string nationality;                                 // country code (player's row only)
    career::ProfileRatingInfo rating{};                      // ProfileRating(ratingUi, deltaUi)
    std::vector<career::ArcPoint> arc;                       // ProfileRatingArcUi() (last = "Now"/"Ahora")
    std::vector<career::RivalRow> rivals;                    // ProfileRivalsUi() (up to 3)
    // <=8 races, OLDEST first. P2 (F15): the abandon goes in `finished`; `pos` is where you got out.
    struct FormEntry { int pos = 0; bool finished = true; };
    std::vector<FormEntry> recentForm;
    struct { int wins=0, podiums=0, races=0, titles=0, xp=0, age=0; } palmares;
    std::vector<career::RankRow> ranked;                     // ProfileWorldRanking(...) fully sorted desc
    int meIdx = -1;                                          // 0-based index of the player in `ranked` (the FILTERED table)
    // GLOBAL world rank (filter "" = every active driver), stable across queries. The
    // "32 -> 1175" bug was worldRank=meIdx+1 with the DENOMINATOR changing based on the table's series filter
    // (32 = rank within ONE series; 1175 = rank in the global pool). The hero ALWAYS uses this one.
    int worldRankGlobal = 0;                                 // 1-based; 0 = unknown
    std::vector<ProfileFilterOptView> filterOptions;         // series-filter dropdown
    std::string selectedFilter;                              // the filter this view was built with
};
// Provider parametrized by the series filter (same flow as st.prof_filter on the screen).
using ProfileViewProvider = std::function<ProfileView(const std::string& filter)>;
void SetProfileViewProvider(ProfileViewProvider p);

// query.memories: ERA MEMORIES (contract in contracts.ts / screen_memories.cpp) ---
// The screen is a PURE walk over the catalog (AllSeries + IsEraMemorySeries + CalendarForSeries +
// DiscForSeries + DoyToDate), so the handler reconstructs it directly (same pure functions)
// from 3 scalars of the live state: the continuous clock's cursor, the player's Phase (gating)
// and the lineage (surname). loaded=false -> not_ready.
struct MemoriesInputs {
    bool loaded = false;
    int cursorDoy = 0;        // CalUiCursorDoy(): "today" on the continuous clock (open/next/... state)
    // the calendar's LIVE year (CalUiYear()) — the handler projects each era series' calendar
    // onto THIS year (CalendarForSeriesInYear) before comparing day-of-year against cursorDoy; at kStartYear it's an identity mapping
    // (default = kStartYear, so a caller that doesn't fill it in — old tests — sees no behavior change).
    int year = career::kStartYear;
    int frontierPhase = 0;    // g_save.prog.frontier_phase: series above it -> locked
    std::string lineage;      // CareerIdentityLast(): surname = lineage ("" if none)
    int playerBirthYear = 0;  // player's birth year -> the ancestor's relation by age (0 = default 2000)
    std::set<std::string> ownedClasses;   // Memories: owned classes -> locked/owned by ownership (not by tier)
    bool giftPending = false;    // F1'67 gift granted and unseen (popup + badge)
    int  giftVehHash = 0;        // the gift car (759059246) for the popup's photo
    int  giftLivery  = 0;        // the gift's livery ordinal (50 = Elmmoor Racing #1)
    std::string giftClass;       // the gift's class ("Falcon_Masters")
};

// lore for the era series (docs/career/memory_lore.json). Loads once; QueryMemories emits
// bilingual desc/highlight per series if it exists. Without loading, empty strings are emitted (fallback in the app).
int LoadMemoryLore(const char* careerDir);   // returns the number of series with lore loaded

// (Aug 24, tester report): the ancestor's relation by decade, made COHERENT as a family. The
// ancestor's name is per DECADE (by design, one raced every series of their era), but the relation was per
// GENERATION: two neighboring decades fell into the same generation, producing TWO "father" entries with different names
// ("strange family dynamic... I don't want to judge"). Rule: within a generation, the MOST
// RECENT decade is the direct line (father/grandfather/greatGrandfather/greatGreatGrandfather), and the rest are
// uncles of that generation (uncle/greatUncle/greatGreatUncle/greatGreatGreatUncle). Deterministic and pure:
// returns decade -> i18n key (the app resolves t('memories.relation.<key>')).
std::map<int, std::string> AncestorRelationsForDecades(const std::vector<int>& decades, int playerBirthYear);
using MemoriesInputsProvider = std::function<MemoriesInputs()>;
void SetMemoriesInputsProvider(MemoriesInputsProvider p);
using MemoriesAckGiftExec = std::function<void()>;
void SetMemoriesAckGiftExec(MemoriesAckGiftExec e);   // F1'67 gift: marks it seen (badge + popup)

// query.calendar: MASTER CALENDAR (contract in contracts.ts / screen_calendar.cpp) ---
// The overlay hands over the raw CalSlots (CalUiMasterCalendar) + the continuous clock's scalars + the
// season result cards; the handler maps each slot the same way the screen does: SeriesById->display/
// class/region, DiscForSeries->discipline, state resolved by a priority ladder (completed>DNF>
// locked>clashes>enrolled>past>next), prize (RoundWinnerPrize), grid size (LiveriesForClass),
// multiclass (MeetingClassSet), historic (era<2000), and derives commitments/today. No colors: the app
// has its own STATUS_META/DISC_META; only enum tokens + data go here. loaded=false->not_ready.
struct CalendarSeasonResultInput {   // a flat mirror of career::SeasonResultCard (avoids dragging in career_season_ui.h)
    std::string seriesId, display, champion;
    int year = 0, finalPos = 0, field = 0, rounds = 0;
};
struct CalendarView {
    bool loaded = false;
    bool calibrated = false;                          // CareerCalibrated(): false -> calibration race only
    std::string calibCarName;                         // "Kingsmere Uma713" (a literal from the screen)
    std::string calibTrackName;                       // CalibrationCalSlot().track_name
    long calibTrackHash = 0;                          // CalibrationCalSlot().track_hash
    int frontierPhase = 0;                            // g_save.prog.frontier_phase
    int cursorDoy = 0;                                // CalUiCursorDoy()
    int year = 0;                                     // CalUiYear()
    int weeksToNextEvent = 0;                         // CalUiWeeksToNextEvent(frontier)
    std::vector<career::CalSlot> slots;               // CalUiMasterCalendar(frontier)
    // year-continuity: RAW candidates for the NEXT year (cc.year+1), un-capped/un-sorted —
    // the handler (QueryCalendar) caps them at kNextYearQueueCap, sorts them by day-of-year, and forces them NOT
    // clickable before appending them to the end of "slots" (with year=cc.year+1). The PROVIDER (overlay,
    // career_overlay.cpp) fills them in by re-projecting BuildMasterCalendar over a copy of g_calendar
    // with year+1/cursor_doy=0 — the SAME pattern as NextEventAbsDay's year+1 fallback (career_contract.cpp)
    // — because it's the one holding the LIVE cc (enrollments/gates); the handler couldn't reconstruct it on
    // its own from this view's scalars. Empty = compat (no queue, previous behavior).
    std::vector<career::CalSlot> nextYearSlots;
    std::vector<CalendarSeasonResultInput> seasonResults;  // CalUiSeasonResults(playerName), flattened
    std::string playerName;                           // to hide "champion = you"
};
using CalendarViewProvider = std::function<CalendarView()>;
void SetCalendarViewProvider(CalendarViewProvider p);

// query.game_state: COARSE game state from shared memory (mGameState/mSessionState). The app POLLS this to
// know when AMS2 is at the MENU (front-end, gameState==1) before overlaying it / firing commands (avoids freezing
// startup with "pause on losing focus"). Provider = ShmRead in the overlay. atMenu = gameState==1. Not parametrized.
// shmMapped = does the shared-memory mapping exist? Since the engine runs inside the game session, the game is ALWAYS running; if the
// mapping can't be opened, it's because the user does NOT have Options>System>Shared Memory="Project CARS 2" set. The app
// uses this to WARN (races can't be settled without shared memory). Different from gameState/atMenu (which would be 0
// whether shared memory is off or the game is simply loading).
// VR: race-LAUNCH phase + config-verification verdict. The two ImGui notices
// accompanying a launch (the "Configuring…" cover and the "all set, hit Start" pill) are
// drawn on the DESKTOP swapchain, so with a headset on they're NOT visible: the VR player used to be left
// staring at the raw menu navigation with no signal at all. The app reproduces them on its own screen — which DOES
// reach the headset panel — by polling this. launchPhase: 0 = nothing in progress · 1 = configuring · 2 = ready
// to Start. Scoped to a career-mode race PENDING settlement, so the CLOSING cover
// (post-settlement) is never confused with a launch. cfgFail* mirrors StartHintFailInfo.
struct GameStateView { int gameState = 0; int sessionState = 0; bool shmMapped = false;
                       int launchPhase = 0; int cfgFailN = 0; std::string cfgFailGroups; };
using GameStateProvider = std::function<GameStateView()>;
void SetGameStateProvider(GameStateProvider p);
HandlerResult QueryGameState(const jsonm::Value* params);   // > { gameState, sessionState, atMenu, launchPhase, cfgFail* }

// cmd.vr.panel {mode:"auto"|"show"|"hide", force?:bool} -> governs the headset panel (vrmirror).
// The app uses this in the two moments where the panel's context-based automation isn't enough: during
// race configuration (which has to COVER the menu navigation) and at RESULTS (where AMS2 still hasn't
// returned to the menu, and the panel would never reappear). force only applies with "show" — see vr_mirror.h.
HandlerResult CmdVrPanel(const jsonm::Value* params);

// query.results: POST-RACE SETTLEMENT (contract in contracts.ts / screen_results.cpp) ---
// TRANSIENT state: only valid right after a race (g_lastSettleValid). The overlay hands over the raw
// scalars from the last settlement (money/times/progression) + identity (class/car/track) + the
// championship standings; the handler composes the money LINES (they all SUM to the net = credits_delta), the
// progression, and the championship block, and converts ImU32 colors (class->#hex; ELO band/REP tier->
// var(--x)). loaded=false -> not_ready (the app shows "no result yet").
struct ResultsStandingInput { std::string name; int points = 0; bool isPlayer = false; };
struct ResultsView {
    bool loaded = false;                 // g_lastSettleValid
    int pos = 0, field = 0;
    bool finished = true;                // P2 (F15): false = DNF/retired/DSQ; `pos` is where you got out
    // Multiclass: when multiclass=true, pos/field are the IN-CLASS values and overall* give the
    // overall (UI context). Single-class -> multiclass=false and overall*=0 (back-compat with old DLLs/saves).
    bool multiclass = false;
    int overallPos = 0, overallField = 0;
    std::string className;
    ImU32 classColor = 0;                // FindClass(clsId)->color() (arbitrary -> #hex)
    std::string carName, trackName;
    float totalTimeS = -1.0f, bestLapS = -1.0f, gapS = 0.0f;
    // FINAL gap to the class leader (not a pace gap): gapLeaderS = seconds behind the leader at the finish (<0 = n/a or
    // you're the leader); gapLeaderLaps > 0 => they put N laps on you ("+N L"). The app hides the line when you finish 1st (pos==1).
    float gapLeaderS = -1.0f; int gapLeaderLaps = 0;
    // raw money terms (the handler applies the sign per line); net = SettleReport.credits_delta.
    // (`sponsor` — the v1 system's flat retainer by REP tier — retired; live sponsorship
    //  now comes in via sponsorPayout (cards) + coverage/salary (contract).)
    long prize = 0, cleanBonus = 0, resultBonus = 0, salary = 0, sponsorPayout = 0,
         coverage = 0, entry = 0, costs = 0, deductible = 0, travel = 0, net = 0;
    // progression (before->after)
    int eloBefore = 0, eloAfter = 0;
    std::string eloBand;                 // "A".."F" (career::RatingBand, mapped from screen_results.cpp)
    ImU32 eloColor = 0;                  // theme constant -> var(--x)
    int repBefore = 0, repAfter = 0, repDelta = 0;
    ImU32 repColor = 0;                  // REP tier color
    int xpBefore = 0, xpAfter = 0, xpGained = 0;
    long crBefore = 0, crAfter = 0;      // balance (credits before/after)
    // championship (absent -> one-off race)
    bool hasChampionship = false;
    std::string champSeriesName;
    int champPointsGained = 0, champPlayerRank = 0;
    std::vector<ResultsStandingInput> champRows;   // top 5 in standings order
    // sponsor cards: EXTRA breakdown contributed by the cards active in the settled race, ON TOP
    // of the base lines above (which know NOTHING about cards: v.prize/v.costs/... are the SettleReport, pure).
    // The overlay derives them from g_lastCardsSettle (a snapshot taken at settlement) + g_save.cards; the handler only serializes.
    // DISCOUNT lines (brand/trigger unknown, fixed text) carry
    // a `key` (an engine i18n key, results_card_disc_* -- bilingual, strings.cpp); INCOME/penalty lines carry
    // a `label` instead (brand name + dynamic trigger, not translatable by key). Exactly one of the two per
    // line; the handler (career_api_queries.cpp) emits whichever is present.
    struct ResultsCardLine { std::string label; std::string key; long amount = 0; int tone = 0; };  // 0 gold 1 cyan 2 danger
    std::vector<ResultsCardLine> cardLines;   // extra breakdown lines (raw label or i18n key, not both)
    struct ResultsCardChip { std::string text; int kind = 0; };  // 0 fired 1 idle 2 expired 3 lost
    std::vector<ResultsCardChip> cardChips;   // "✓/·/✕ Mark" chips for triggered/active/expired/lost cards
    // Card showcase (feedback from user testing): the app renders the REAL CARD (full def via WriteCardDef in the
    // handler) when it triggers/expires/is lost. level = the wallet's level if the card is still active; 1 if it already fired.
    struct ResultsCardShow { std::string id; int level = 1; int kind = 0; };  // kind = same code as chips
    std::vector<ResultsCardShow> cardShowcase;
    // Block D (spec from 2026-07-24 + follow-up proposal): the TWO post-race surfaces of the screen, which
    // don't mix. `feel` is the SUBJECTIVE FEEL (optional, 5 levels, local effect deliberately
    // weak); `offer` is OBJECTIVE — it only appears when the advantage sample was clipped for being anomalous and the
    // loop, out of caution, decided not to trust it. Declining the offer is an exact no-op.
    struct ResultsFeedback {
        bool ask = false;              // a race has been settled -> ask (always; consent only governs SENDING)
        int  feel = 0;                 // 1..5 already answered for THIS race; 0 = unanswered
        bool telemetryOptIn = false;   // is sending consented? (the app states this at the bottom of the card)
        bool offer = false;            // is there a difficulty offer due to an anomalous gap?
        double gapPct = 0.0;           // measured advantage, as a FRACTION of lap time (+ = the player is faster)
        int offerDifficulty = 0;       // the one that would apply if accepted
        int curDifficulty = 0;         // the CURRENT number for that category (decline = stay here)
        // settlement HELD BACK the sample (it was anomalous) and applied nothing -- only accepting writes,
        // so not answering leaves the race showing nothing. false = the step DID apply and the offer
        // is only about the shortfall; saying "it won't count" there would be a lie.
        bool held = false;
    };
    ResultsFeedback feedback;
};
using ResultsViewProvider = std::function<ResultsView()>;
void SetResultsViewProvider(ResultsViewProvider p);

// Block D — cmd.results.feel { feel: 1..5, reasons: [token...] }: records the response of the
// form. The engine applies the local effect (extremes only, only with repetition, one point) and keeps the
// reasons for the telemetry payload. reasons is OPTIONAL: answering the scale is already enough.
// Block D (follow-up proposal) — cmd.results.gapOffer { accept: bool }: accepts or declines the offer for
// anomalous gap. accept=true locks in the offered difficulty; false is a no-op (just closes the offer).
using FeelCmdExec     = std::function<bool(int feel, const std::vector<std::string>& reasons, double* outDelta)>;
using GapOfferCmdExec = std::function<bool(bool accept, int* outDifficulty)>;
void SetResultsFeedbackCmds(FeelCmdExec feel, GapOfferCmdExec offer);
HandlerResult CmdResultsFeel(const jsonm::Value* params);
HandlerResult CmdResultsGapOffer(const jsonm::Value* params);

// query.cards: SPONSOR CARDS (spec from 2026-07-11) ---
// Wallet (signed cards, slotted or benched) + pending post-race shop (if any) + gating scalars
// (phase -> number of slots, credits). The handler assembles the payload directly from CardsView + the
// pure catalog (career::CARD_DEFS / CardById / CardCost / CardFxV / CardUpgradeNext / CardSellValue / CardShop*):
// every card (signed or offered) travels with its FULL DEF embedded (the app doesn't hardcode the catalog). The
// provider (overlay) only hands over a consistent COPY of the state (g_save.cards/g_save.card_shop under the game
// thread, same threading model as the rest of the queries). loaded=false -> not_ready.
struct CardsView {
    std::vector<career::SignedCard> cards;
    career::CardShopState shop;
    int phase = 1;
    long credits = 0;
    bool loaded = false;
};
using CardsViewProvider = std::function<CardsView()>;
void SetCardsViewProvider(CardsViewProvider p);
HandlerResult QueryCards(const jsonm::Value* params);

// card execs (IdCmd pattern from sponsors/market; slot carries id+int). The engine RE-VALIDATES everything (CardOp*
// re-checks shop open/free slot/credit/gating): the UI only reflects, never decides.
using CardIdExec   = std::function<bool(const std::string&)>;
using CardSlotExec = std::function<bool(const std::string&, int)>;
using CardVoidExec = std::function<bool()>;
using CardArrangeExec = std::function<bool(const std::vector<std::pair<std::string, int>>&)>;
void SetCardsCmds(CardIdExec buy, CardVoidExec reroll, CardVoidExec shopClose,
                  CardSlotExec slotc, CardIdExec bench, CardIdExec upgrade, CardIdExec sell);
// (additive; separate setter so as not to touch the SetCardsCmds call site): seen = marks the
// wallet as viewed (tab badge); arrange = ATOMIC multi-slot reassignment (drag & drop).
void SetCardsCmds2(CardVoidExec seen, CardArrangeExec arrange);
HandlerResult CmdCardsBuy(const jsonm::Value* params);       // params: { id } -> {ok, id}
HandlerResult CmdCardsReroll(const jsonm::Value* params);    // params: {} -> {ok}
HandlerResult CmdCardsShopClose(const jsonm::Value* params); // params: {} -> {ok}
HandlerResult CmdCardsSlot(const jsonm::Value* params);      // params: { id, slot } -> {ok, id, slot}
HandlerResult CmdCardsBench(const jsonm::Value* params);     // params: { id } -> {ok, id}
HandlerResult CmdCardsUpgrade(const jsonm::Value* params);   // params: { id } -> {ok, id}
HandlerResult CmdCardsSell(const jsonm::Value* params);      // params: { id } -> {ok, id}
HandlerResult CmdCardsSeen(const jsonm::Value* params);      // params: {} -> {ok}
HandlerResult CmdCardsArrange(const jsonm::Value* params);   // params: { moves: [{id, slot}] } -> {ok}

// query.times: TIMES LOG (contract in contracts.ts / screen_championships.cpp TIMES tab) ---
// Mirror of the career::RaceLogUiEntries() vector (most recent first): one row per race run in
// career mode with its FULL GRID. The overlay assembles the view, resolving class/discipline color (ImU32,
// via career::RaceListColor/RowClassColor); the handler serializes raw (times in seconds, colors->CssVar,
// day-of-year->DoyToDate) and the app (Times.tsx) computes leader/gap/fastest-lap the way DrawTimesDetail did. loaded=false ->
// not_ready. NO lap-by-lap drill-down. date=DoyToDate(doy); pos<=0 => DNF; empty grid => race predates this feature.
struct TimesGridRow {
    int pos = 0;
    std::string name, className;         // className = raw car_class from shared memory (multiclass)
    ImU32 classColor = 0;                // RowClassColor(car_class, race color) -> #hex/var
    int laps = 0;
    float bestLapS = -1.0f, totalTimeS = -1.0f;
    bool dnf = false, isPlayer = false;
    bool finished = true;                // false = still on track when captured; deficit-1 = laps down
};
struct TimesRace {
    std::string track, className;        // className = FindClass(class_id) display
    ImU32 classColor = 0;                // RaceListColor(series_id, class_id) -> #hex/var
    int doy = 0, year = 0, pos = 0, field = 0;
    bool finished = true;                // P2 (F15): false = you abandoned (pos = where you got out)
    float bestLapS = -1.0f, totalTimeS = -1.0f;   // the player's (row L1)
    std::vector<TimesGridRow> grid;      // full grid (empty for races predating this feature)
};
struct TimesView {
    bool loaded = false;                 // g_saveLoaded
    std::vector<TimesRace> races;        // most recent first (RaceLogUiEntries order)
};
using TimesViewProvider = std::function<TimesView()>;
void SetTimesViewProvider(TimesViewProvider p);

// query.tree: PROGRESSION TREE (contract in contracts.ts / screen_tree.cpp) ---
// Discipline×level matrix from the REAL ladder + the player's rating band (REP tier). The overlay hands over the
// FLAT nodes (career::LadderNodesView, since it can't include career_state.h) + rep/frontierPhase/tier.
// The handler serializes RAW (disc as a token, tier index→i18n key, color ImU32→CssVar); the app groups
// by disc×level and colors the disciplines with its DISC_META (same as query.calendar). loaded=false → not_ready.
struct TreeNode {
    std::string id, display, disc;   // disc = token single/touring/gt/endurance/entry/offroad
    int  level = 1;                  // 1..7 (column; already clamped)
    int  carCount = 0;               // 0 => hidden
    bool isDlc = false;              // dlc == "dlc"
    int  vehid = 0;                  // nav key (action deferred to a later phase); the data is inert for now
    bool locked = false;             // level > frontierPhase (cosmetic)
};
struct TreeView {
    bool loaded = false;             // g_saveLoaded
    int  frontierPhase = 0;          // g_save.prog.frontier_phase
    int  rep = 0;                    // g_save.prog.rep (only the band is shown, not the number)
    int  ratingTier = 0;             // career::RepDisplayBand(rep) → 0..5
    ImU32 ratingTierColor = 0;       // career::RatingTierColor(rep) → var(--x)/#hex
    std::vector<TreeNode> nodes;     // PSV file order
};
using TreeViewProvider = std::function<TreeView()>;
void SetTreeViewProvider(TreeViewProvider p);

// query.settings: SETTINGS · Difficulty (contract in contracts.ts / screen_settings.cpp) ---
// v1 = ONLY the Difficulty section (5 cards + Adaptive trim); the other sections in the prototype are
// design EXAMPLES, they don't exist in the engine. The overlay assembles the cards (literal IM_COL32 colors, no
// FindClass → no wrapper) + live state (SettingsUiMode/Trim). The handler serializes raw (mode→token,
// meter/trim raw, color→CssVar, i18n keys as strings). Selecting a card / ±trim = deferred to a later phase.
struct SettingsDiffCard {
    int         mode = 0;        // career::DifficultyMode 0..5 -> token
    std::string nameKey, descKey; // i18n TR keys (the app resolves them with t())
    int         meter = 0;      // 1..5 meter bars
    ImU32       color = 0;      // level color (literal IM_COL32) -> CssVar
    bool        isAuto = false; // Adaptive only (AUTO badge + trim row)
};
// Per-class DDA, rev. 2: one row per category with an entry in class_dda, for the difficulty number's hover
// tooltip. `difficulty` is the number ACTUALLY RACED in that category (its number + trim, clamped)
// and `delta` is how far that number drifts from the save's OVERALL difficulty, so it can be EXPLAINED -- it's no longer its own
// layer (that model was retired: it oscillated). `seeded` = races == 0 -> the number was set by the community, not the
// player (which is why the row shows 0 races).
struct SettingsDiffClass {
    std::string classId;
    // lock panel (08/19 redesign): row identity. `classId` is the KEY (the one that travels in
    // commands and indexes the save) and is still authoritative; these other two fields are just for display, because a
    // list of raw ids doesn't read like a list of categories. They're filled in by career::ClassRowIdentity, which
    // never fails: if it can't resolve anything, className falls back to classId itself.
    std::string className;
    ImU32       classColor = 0;
    int  difficulty = 0;
    int  delta = 0;
    int  races = 0;
    bool seeded = false;
    // DDA lock: this category's OWN flag, NOT the effective one. The app needs both (this one
    // and `lockAll`) to render "checked but disabled" and to restore state when the master switch is turned off.
    bool locked = false;
    // 09/09 (, 4): the visible saturation (): +1 at the ceiling and you carry on faster, -1 at the floor and you carry on
    //  slower, 0 nothing. And the category's WET number (with trim), 0 if it doesn't have one yet; racesWet holds it up.
    int  saturation = 0;
    int  wetDifficulty = 0;
    int  racesWet = 0;
};
struct SettingsView {
    bool loaded = false;
    int  selectedMode = 0;                    // g_save.prog.difficulty_mode
    int  trim = 0;                            // g_save.prog.adaptive_trim (-5..+5)
    int  trimMin = 0, trimMax = 0;            // dda::kTrimMin/kTrimMax (for the future ± control)
    // REAL AI difficulty number for adaptive mode (AMS2 slider). aiDifficulty = base value learned by
    // the DDA (prog.ai_difficulty); aiEffective = clamp(base + trim, aiMin, aiMax) = the one actually SET for the race.
    int  aiDifficulty = 80, aiEffective = 80, aiMin = 70, aiMax = 120;
    // FIXED difficulty chosen by hand (prog.custom_difficulty). It's the one raced outright when the mode is
    // DM_CUSTOM; in other modes it still travels along, so the card can show the saved number without activating it.
    int  customAi = career::dda::kCustomDefault;
    std::vector<SettingsDiffCard> cards;      // 6, same order as the provider (4 bands + adaptive + fixed)
    // Per-class DDA: sorted by race count DESCENDING; empty = the player hasn't raced anything yet. Only the
    // classes with an entry in class_dda: listing all ~109 installed classes would just be noise — this is the player's history.
    std::vector<SettingsDiffClass> classes;
    bool lockAll = false;   // DDA lock: the master switch; effective(class) = lockAll || classes[i].locked
    int  aggr = 0;                            // prog.ai_aggression (0=auto, 40/60/80/100=override)
    bool fcyOn = false;                       // legacy 0/1 for the old app (Inherited -> true)
    int  fcyMode = 1;                         // FCY 0=No 1=Yes 2=Inherited
    bool scaleTyre = true;                    // auto-scaling of Tyre Wear (Settings toggle)
    bool scaleFuel = true;                    // auto-scaling of Fuel Consumption
    int  inheritMask = 0;                     // prog.inherit_mask (kInh* bits); the handler serializes it as tokens
    // 21/07 reward mode of THIS career (prog.reward_mode: 0=easy 1=normal 2=hard). Chosen on
    // creating the save and, since 0.7.0, changed in Settings with cmd.settings.setReward.
    // rewardMult = bal::REWARD_MULT for the mode (the app renders the % without duplicating the balance table).
    int    rewardMode = 1;
    double rewardMult = 1.0;
    bool telemetryOptIn = false;              // Block D — send consent (prog.telemetry_opt_in)
    bool telemetryAsked = false;              // has it already been asked? (false -> the app asks for consent before Play)
    // the SEED for the calibration race and whether that race has already been run. While `calibrated` is
    // false, `aiSeed` is the level the first race will run at and the player can change it; afterwards
    // the adaptive loop takes over and the number is no longer the player's to set. The app uses `calibrated` to decide whether to show the control.
    int  aiSeed = 80;
    bool calibrated = false;
    // Block C (spec C1): weather mode (0 random · 1 inherited · 2 rotation) and the per-state weights
    // from the AMS2 catalog the player has flagged (code -> weight; 0 = forbidden). Empty = factory weights.
    int wxMode = 0;
    std::map<int,int> wxWeights;
    std::map<int,int> wxDefaults;            // the factory ones, so the UI can show where it starts from
};
using SettingsViewProvider = std::function<SettingsView()>;
void SetSettingsViewProvider(SettingsViewProvider p);

// query.garage: GARAGE (contract in contracts.ts / screen_garage.cpp) ---
// Owned cars (resolved via career::GarageOwnedCars) + active-contract strip (CalUiContractFacets)
// + rating band + credits. Buying lives in vehicle/pre-race; selling, here (0.7.0: cmd.garage.sell).
// The handler serializes raw (color ImU32->CssVar, tier->token, contract facets).
struct GarageCarView {
    std::string id, name, classId, className, classShort;
    ImU32 classColor = 0;            // FindClass->color() -> CssVar; 0 -> accent
    int year = 0, pwr = 0, grp = 0, bal = 0;   // stats 0..100
    int vehHash = 0;                 // image key (the app renders a placeholder)
    int livery = -1;                 // saved ordinal (-1 default)
    // 0.7.0 (car selling): accumulated km, what you get if you sell it (CarResaleValue) and, if it can't be sold,
    // why (CarSaleBlockToken: "ok" | "pending_race" | "contract" | "gift" | "no_value" | "not_owned")
    double km = 0.0;
    long sellPrice = 0;
    std::string sellBlock = "ok";
};
struct GarageContractView {          // flat mirror of career::ContractFacetsView
    bool active = false;
    std::string seriesDisp;
    int  seasonsLeft = 0, seasonsTotal = 0;
    long perRace = 0;
    int  kind = 0;                   // 1 = customer/self-funded
    bool roleLead = false;
    int  objDone = 0, objTotal = 0;
    int  objTargetPos = 0;           // 0.4.0 — SEASON objective: "finish Top N" required by the team (0 = no objective)
    bool renewOnTable = false;       // 0.4.0 silly season: an early renewal waiting in the inbox
    bool fixesCar = false;
    std::string teamCar;
    int  typeLabel = 0;              // 0..5 -> ctype_<idx>
    bool covSeatEntry = false, covConsumables = false, covDamage = false, covTravel = false;
};
struct GarageView {
    bool loaded = false;
    long credits = 0;
    int  tierBand = 0;               // RepDisplayBand 0..5
    ImU32 tierColor = 0;             // RatingTierColor -> CssVar
    GarageContractView contract;
    std::vector<GarageCarView> cars; // save file order
};
using GarageViewProvider = std::function<GarageView()>;
void SetGarageViewProvider(GarageViewProvider p);
// 0.7.0: cmd.garage.sell {carId, livery} -> sells that unit. The executor (overlay) resolves the base price,
// calls career::CarOpSell and saves; returns the rejection token in *reason ("ok" if it sold) and the amount collected.
using GarageSellExec = std::function<bool(const std::string& carId, int livery, std::string* reason, long* paid)>;
void SetGarageSellExec(GarageSellExec e);

// query.sponsors (SPONSORS — catalog/gates/payout from the old system) retired along with
// SponsorBiText/SponsorTraitView/SponsorMyView/SponsorAvailView/SponsorsView/SponsorsViewProvider/
// SetSponsorsViewProvider/QuerySponsors — replaced by the card system (query.cards).

// query.market: DRIVER MARKET (contract in contracts.ts / screen_market.cpp) ---
// Reputation + stats + contract banner + dynamic offers (MarketUiOffers, {} until calibrated). The overlay
// resolves class name/color (MarketClassInfo wrapper), tier/type colors (ImU32), the expectation band
// (i18n key) and the gate flags; the handler serializes raw (color->CssVar, tokens/keys as strings).
// Bilingual literals composed by the engine (title/lockReason/expectation) travel as an en/es pair. Signing/
// declining/negotiating = mutation deferred to a later phase. loaded=false -> not_ready. Empty offers (rookie) are NOT an error.
struct MarketConflictView { std::string seriesName; };
struct MarketTierDot { ImU32 color = 0; };
struct MarketContractView {
    bool active = false; int count = 0;
    std::string teamName, seriesName; int kind = 0;   // count==1
    std::vector<std::string> seriesNames;             // count>1
};
struct MarketOfferView {
    std::string id, category;                         // category: "direct"|"open"
    int state = 0; bool held = false, direct = false;
    std::string team, from, title, titleEn;
    ImU32 teamColor = 0;
    std::string className, clsId; ImU32 discColor = 0;
    std::string scope; int commitment = 0;
    int contractKind = 0; std::string contractKindIcon; ImU32 contractKindColor = 0;
    int archetype = 0;                                // Feeder-program narrative archetype (career::ContractArchetype), for the voice of the promotion
    bool requiresTryout = false, canAfford = true; int reqRep = 0;
    bool displacement = false; long buyIn = 0;
    std::string expectationKey, expectation, expectationEn;
    std::string pitchKey; bool pitchPrestige = false;
    int whyRating = 0, whyForm = 0, whyRel = 0;
    long signBonus = 0, perRace = 0; std::string seatName;
    int typeLabel = 0;
    bool seatEntryCovered = false, consumablesCovered = false, damageCovered = false, travelCovered = false;
    std::string lockReason, lockReasonEn;
    // contract field: PREVIEW of the car+skin the team assigns (works/customer). fixesCar=false -> the player
    // chooses (privateer): the app shows no car. teamCarSlug + teamLiveryNN -> real thumbnail of the assigned livery.
    bool fixesCar = false; std::string teamCarName, teamCarSlug; int teamLiveryNN = -1;
    std::vector<MarketConflictView> conflicts;
    int weeksLeft = 0;                                 // offer lifecycle: weeks until expiry (0..4), for the UI countdown
    int daysLeft = 0;                                  // 18/09: days until it can no longer be signed (OfferDeathAbs); 1 = today is the last
    // Negotiation redesign: state of this offer's negotiation thread (if one is open). The card
    // in the app derives the status chip from negState; negRespondDays only makes sense in WAITING.
    bool negotiable = false; int negState = -1; int negRespondDays = 0;
    // WHO you beat for this seat. Empty when the seat wasn't contested (the access ramp doesn't
    // contest seats at your level and below: there's always room there) or when there was no rival competing for it.
    std::string rivalId, rivalName;
};
// a seat that's WATCHING you. It's not an offer (yet): you came close to the best
// candidate and the team will decide within a few races. It's the only surface where the player can see there's a
// seat in play that isn't theirs yet — without this, the observation period would exist but be invisible.
struct MarketWatchView {
    std::string seriesId, seriesName, teamName, rivalName;
    int racesLeft = 0;
};
struct MarketView {
    bool loaded = false;
    int repScore = 0, repIdx = 0, repPct = 0;
    std::string tierKey; ImU32 tierColor = 0;
    bool hasNext = false; std::string nextTierKey; int nextTierMin = 0;
    std::vector<MarketTierDot> tierDots;
    int wins = 0, podiums = 0, races = 0;
    MarketContractView contract;
    int seekingYouCount = 0, openSeatsCount = 0, playerRating = 0;
    std::vector<MarketOfferView> offers;
    std::vector<MarketWatchView> watching;   // seats under observation (empty if none)
    // THE MARKET HAS A SCHEDULE. Outside the signing window the board comes back empty, and an
    // unexplained empty screen reads as "this is broken" instead of "come back in September" — the same
    // lesson learned from the dry-spell tab. `daysToWindow` is the countdown that turns it into a wait
    // with a date. Defaults to open: an old payload behaves as it did before.
    bool marketOpen = true;
    int  daysToWindow = 0;
};
using MarketViewProvider = std::function<MarketView()>;
void SetMarketViewProvider(MarketViewProvider p);

// query.hub: HOME / HUB (contract in contracts.ts / screen_hub.cpp) ---
// Driver card (identity+tier+reputation+trophies+fanbase) + NEXT race (CalUiNextRace) + navigation
// badges (direct offers / unread mail) + season progress + lifecycle state (advance
// week / break contract). The overlay assembles with the same bridges; the handler serializes raw. loaded=false->not_ready.
struct HubView {
    bool loaded = false;
    std::string first, last;                          // CareerIdentityFirst/Last
    int repScore = 0, repIdx = 0, repPct = 0;         // market::RepInfoFor
    std::string tierKey; ImU32 tierColor = 0;
    bool hasNext = false; std::string nextTierKey; int nextTierMin = 0;
    int wins = 0, podiums = 0, races = 0;             // sum class_stats
    long followers = 0;                               // 1200 + score*95 (same formula as the overlay)
    bool nextValid = false;                           // CalUiNextRace
    std::string nextSeries, nextTrack, nextDate; int nextRound = 0, nextDoy = 0;
    int directOffers = 0, unread = 0;                 // badges (market / mail)
    int enrollmentCount = 0; bool contractActive = false;   // lifecycle
    int contractCount = 0;                                  // number of ACTIVE contracts (you can have N) -> hub at-a-glance
    std::string contractSeries;                             // series of the active contract (CalUiContractFacets.seriesDisp), for naming "break contract with X"
    // the hub button breaks ALL contracts: the modal needs the TOTAL amount (BreakContractFee(save,""),
    // the same function that later charges it) and the teams you're leaving hanging (rel_marks), so it can promise what actually happens.
    long breakFee = 0;
    std::vector<std::string> contractTeams;                 // team_label of every ACTIVE contract
    std::string cursorDate; int year = 0;
    int cursorDoy = 0;                                      // raw day-of-year of the cursor, for WeekdayLabel(year, doy)
    int ownedCount = 0, completedRaces = 0;
    // USEFUL hub info (unified ELO rating + delta + phase progression + recent results), to
    // fill the panel with something meaningful instead of stretching a single-button panel.
    int eloRating = 0, eloDelta = 0;
    long xpToward = 0, xpThreshold = 0; int phase = 1;
    struct Result { int pos = 0; int field = 0; std::string track; bool finished = true; };   // P2 (F15): finished=false = DNF
    std::vector<Result> recentResults;               // most recent first
};
using HubViewProvider = std::function<HubView()>;
void SetHubViewProvider(HubViewProvider p);
HandlerResult QueryHub(const jsonm::Value* params);

// query.identity: CAREER-CREATION GATE (screen_identity.cpp) ---
// This is the app's global gate: if needed=true, ONLY the form is drawn (no topbar/tabs) until it's completed.
// The provider reads g_save.player.* RAW (not CareerIdentityFirst/Last, which override the demo placeholder name).
// loaded=true even when needed=true (the save is loaded, the identity is just blank -> NOT not_ready).
struct IdentityView {
    bool loaded = false;
    bool needed = false;                                   // CareerIdentityNeeded()
    std::string amsName;                                   // CareerIdentityAmsName() (prefill from shared memory)
    int startYear = 0;                                     // kStartYear (age anchor)
    std::string first, last, sex, nationality;             // g_save.player.{name,surname,sex,nationality} RAW
    int birthYear = 0, birthMonth = 0, birthDay = 0;       // g_save.player.birth_* RAW (0 = unfilled)
    int age = 0;                                           // CareerIdentityAge() (0 if no DOB)
};
using IdentityViewProvider = std::function<IdentityView()>;
void SetIdentityViewProvider(IdentityViewProvider p);
HandlerResult QueryIdentity(const jsonm::Value* params);

// query.negotiate: contract NEGOTIATION (screen_negotiate.cpp), PARAMETERIZED by {offerId} ---
// The provider looks up the offer in MarketUiOffers; found=false -> the app returns to the Market. The
// margin MATH is replicated client-side per toggle (same formula as NegoEvaluate) from these catalogs; the
// baseMargin is fixed for the offer. Demand/tier labels travel as {en,es} pairs; colors -> CssVar.
struct NegoDemandRow  { int demandId = 0; std::string en, es, descEn, descEs; int cost = 0; bool positive = false; };
struct NegoSalaryPkg  { int pct = 0, cost = 0; };
// cardId (sponsor cards): card_id of the wallet card backing this chip ("" = the chip
// is "none offered" and carries no card). cmd.negotiate.accept returns it as terms.sponsorCardId so the
// engine knows which card to hand over if the deal closes.
struct NegoSponsorChip{ int tierRank = 0; std::string name, tierEn, tierEs; ImU32 color = 0; int delta = 0; std::string cardId; };
// Negotiation redesign: what's ON THE TABLE (neutral base or the team's counter) — no margin, just
// the concrete terms the app can render/accept. History row: 1 turn of the thread (day+who+type).
struct NegoTermsView { bool demands[8] = {}; int salaryPct = 50; int sponsorTierRank = -1; };
struct NegoHistoryRow { int day = 0; int who = 0; int kind = 0; unsigned demandsMask = 0; int salaryPct = 50; };
struct NegotiateView {
    bool loaded = false, found = false;
    std::string offerId, teamName; ImU32 teamColor = 0;
    int rating = 0, relationship = 0;
    long startBonus = 0, startPerRace = 0; int startDurationSeasons = 1; bool startProtectionNone = true;
    int negotiationLevel = 0; double wantT = 0.0; int competing = 0; double trialRatio = 1.0;
    int baseMargin = 0, weakThresh = 0, marginMax = 0; long credits = 0;
    std::vector<NegoDemandRow> demands;          // 8 rows
    std::vector<NegoSalaryPkg> salaryPackages;   // 3 discrete packages {10,50,90}
    std::vector<NegoSponsorChip> sponsorChips;   // MySponsors + chip "none" (rank -1, delta 0)
    int initialSalaryPct = 50, initialSponsorTierRank = -1;
    // Negotiation redesign: band/table/history for the asynchronous THREAD (career_nego_thread.h). The
    // exact margin (baseMargin/weakThresh/marginMax/wantT/trialRatio) is NO LONGER SERIALIZED in the JSON — it
    // stays in the struct so as not to break screen_negotiate.cpp (legacy, reads the struct directly, not the payload).
    bool negotiable = false;      // gated by importance (false -> the UI only offers Sign)
    std::string negId;            // id of the open thread ("" = no thread) — the app passes it to the cmds
    int  negState = -1;           // NegoThreadState of the open thread; -1 = no thread
    int  negRound = 0;
    int  patienceBand = 0;        // NegoPatienceBand (0 receptive / 1 tense / 2 at the limit)
    int  budgetLo = 0, budgetHi = 0;   // the agent's band (NegoEstimateBand)
    NegoTermsView table;          // what's on the table (neutral base or the team's counter)
    bool tableIsCounter = false;
    int  respondInDays = 0;       // WAITING: days until the promised response (0 = not waiting)
    int  deadlineWeeks = 0;       // weeks until the current deadline
    int  deadlineDays = 0;        // 18/09: days until the offer can no longer be signed (OfferDeathAbs); 1 = last day
    std::vector<NegoHistoryRow> history;
};
using NegotiateViewProvider = std::function<NegotiateView(const std::string& offerId)>;
void SetNegotiateViewProvider(NegotiateViewProvider p);
HandlerResult QueryNegotiate(const jsonm::Value* params);   // params: { offerId }

// query.season: CHAMPIONSHIPS (screen_season.cpp DrawSeasonSelect), PARAMETERIZED by {seriesId} ---
// Master-detail: a rail of ALL series (my entries with position/points, available, locked) +
// a detail pane that changes NATURE depending on the selected series' state (standings grid if entered,
// an ACCESS panel if gated by a super-license, or an INFO panel if available) + an end-of-season BONUS
// modal. The overlay ASSEMBLES the view with the SAME bridges as the screen (CalUiMyChampionships /
// AllSeries / CalUiGatePassed / CalUiEnrollment / FinalTable / PointsById / CalUiSeasonFinishedFlag /
// CalUiLastOutcome); discipline color comes through the career::RaceListColor wrapper (the handler/app CANNOT
// include career_state.h/theme.h). The handler only serializes (ImU32 -> CssVar). loaded=false -> not_ready.
// selected (params.seriesId) picks the series for the detail pane; if it's not in the rail -> the first one in the rail (default).
struct SeasonRailItem {
    std::string seriesId, display, classShort, className;   // classShort = class_name in UPPERCASE, '_'->' '
    ImU32 discColor = 0;                                     // RaceListColor(seriesId, class_name) -> CssVar
    int  section = 0;                                        // 0=my championships, 1=available, 2=locked
    bool gateLocked = false;                                 // section 2: super-license gate (vs. level lock)
    int  level = 0;                                          // for the "PHASE N" label
    bool selected = false;
    // section 0 only (my championships):
    int  playerPos = 0, myPoints = 0, roundIdx = 0, totalRounds = 0, field = 0;
    bool done = false;                                       // "SEASON FINISHED"
};
// The R1..RN column was fine for the standings TABLE, where round+date is enough. When
// showing the calendar of a championship you're NOT in yet, what's being decided is something else
// ("does this fit my schedule?"), and that needs the time and especially the DURATION: a championship with a
// 24h round is not the same commitment as one made of sprints. laps XOR durationMin, same as in the calendar.
struct SeasonRoundCol  { int round = 0; std::string trackName, date; int doy = 0;
                         int hour = 0, laps = 0, durationMin = 0, practiceMin = 0, qualifyMin = 0; };
struct SeasonRoundCell { int round = 0, pos = 0, points = 0; bool dnf = false, ran = false; };  // ran=false -> "-"
struct SeasonStandRow {
    int rank = 0; std::string name; bool isPlayer = false; int total = 0;
    std::vector<SeasonRoundCell> cells;                     // one cell per round (totalRounds)
};
struct SeasonView {
    bool loaded = false;
    std::string playerName;
    std::string selectedSeriesId;                          // the series actually selected (default applied)
    std::vector<SeasonRailItem> rail;
    // detail (one of 3 variants depending on kind; fields that don't apply stay at their default):
    int detailKind = 0;                                    // 0=none, 1=enrolled, 2=access, 3=info
    std::string detailSeriesDisplay;
    int detailRoundIdx = 0, detailTotalRounds = 0, detailSeasonYear = 0;
    int detailMyPos = 0, detailMyPts = 0;
    std::vector<int> detailPointsScale;                    // <=10 values
    std::vector<SeasonRoundCol> detailRounds;
    std::vector<SeasonStandRow> detailStandings;
    int detailNextRaceDoy = 0;
    // ACCESS detail (kind==2):
    std::string detailQualDisplay, detailQualFirstDate;
    bool detailQualHasCal = false;
    bool detailHasFeeder = false;
    std::string detailFeederClass;
    int  detailFeederTop = 0;
    bool detailFeederHasContext = false;
    int  detailFeederContextPos = 0;
    std::string detailFeederContextSeries;
    // end-of-season BONUS modal (GLOBAL flag, independent of the selected series):
    bool bonusShown = false;
    int  bonusPlayerPos = 0, bonusField = 0;
    long bonusMoney = 0;
    int  bonusXp = 0, bonusRep = 0;
};
using SeasonViewProvider = std::function<SeasonView(const std::string& selected)>;
void SetSeasonViewProvider(SeasonViewProvider p);
HandlerResult QuerySeason(const jsonm::Value* params);   // params: { seriesId? }

// query.superlicenses: one row per GATED championship (the target of a qualifying series) — the
// profile shows status + both paths with progress. SAME composition as the Access detail (kind=2)
// from the season view, aggregated: everything comes from series.psv + save.unlocked_gates, no new state.
struct SuperlicenseRow {
    std::string champId, champDisplay;
    bool unlocked = false;
    std::string qualDisplay, qualNextDate;   // qualifying path (date PROJECTED onto the live year)
    bool qualHasCal = false;
    int  gateTop = 0;                        // finish <= gateTop in the qualifier (1 = win it)
    // tester note 29/08: the TEST's level (today all three open at tier 6) -- the class it uses may
    // unlock earlier (IVORY_CUP_12573 uses Fenwick Dax189, tier 5) and that was confusing; the app paints it as "opens at Tier N".
    int  qualLevel = 0;
    bool hasFeeder = false;                  // season path (top-N in the feeder class)
    std::string feederClass; int feederTop = 0;
    bool feederHasContext = false; int feederContextPos = 0; std::string feederContextSeries;
};
struct SuperlicensesView { bool loaded = false; std::vector<SuperlicenseRow> rows; };
using SuperlicensesProvider = std::function<SuperlicensesView()>;
void SetSuperlicensesProvider(SuperlicensesProvider p);
HandlerResult QuerySuperlicenses(const jsonm::Value* params);

// query.prerace: PRE-RACE · vehicle/livery selection (screen_vehicle.cpp DrawVehicle), PARAMETERIZED
// by {carId?} (same flow as ProfileViewProvider's filter): carId picks the detail car; empty = priority
// order (presetVehHash > first owned > first in the list). The overlay CANNOT include career_state.h
// (CarsByClass/FindClass/PendingRace/PreraceLock, name clash with 'struct Track') -> ASSEMBLES the view by calling the
// POD wrapper career::PreraceUiCompute (career_season_ui.h, defined in screen_vehicle.cpp) and copies it field by field. The handler
// only serializes (ImU32->CssVar; cars[] + liveryNN[] as arrays). clsEmpty=true -> the app shows "pick a race".
// COSTS = NOMINAL LAPS (raceLaps), NEVER the duration % (that's applied client-side at launch). loaded=false->not_ready.
struct PreraceCar {
    std::string id, name, classShort;
    int  year = 0, pwr = 0, grp = 0, bal = 0;
    long price = 0;
    int  rentPct = 0, vehHash = 0;
    bool factory = false, owned = false;
    ImU32 discColor = 0;             // Col::Disc(pr.disc) -> CssVar
    std::string slug;                // image slug for the car -> the app loads <data>/vehimages/.../<slug>_livery_NN.png
};
struct PreraceView {
    bool loaded = false;             // false -> not_ready
    bool clsEmpty = false;           // st.pending.clsId empty -> "pick a race" (loaded is still true)
    std::string raceName, className;
    ImU32 discColor = 0;             // Col::Disc(pr.disc)
    std::string trackName;
    int  trackHash = 0, laps = 0, durationMin = 0, grid = 0;
    long reward = 0;
    long eventPaid = 0;              // 19/09: armed event -> ₡ ALREADY paid on it (a better finish pays the difference)
    std::string reasonKey;           // "lock_calibration"|"lock_contract"|"" (i18n)
    bool lockCar = false, lockSkin = false, lockDuration = false;
    int  presetVehHash = 0, presetLivery = -1, presetDurPct = 100;
    int  divorceRep = 0;             // v2: >0 = "locked in" to preset* for THIS season; switching identity costs this much
    long divorceCr  = 0;             // renting does NOT cost REP (privateer rule): the old rentRep field was retired
    std::string ratingTierLabel;
    ImU32 ratingTierColor = 0;
    long credits = 0;
    bool visual = false;             // Entitlements::IsVisual()
    std::string seriesId;            // raceId.substr(0,'#')
    bool freeEntry = false;          // factory || calibration
    int  raceLaps = 0;               // RaceLapEquivalent(launchEp) (nominal, to scale costs)
    // (community feature request): the mod scales tire wear and fuel consumption with race
    // length, which completely changes the pit stop and fuel load. Previously this was only visible after launch (in the
    // config panel), so here's what would apply at EACH duration %, so the player can decide with the numbers in front of them.
    // The multipliers are computed by the engine (career::WearMultTyre/Fuel), never by the app: a single formula.
    struct WearOpt { int pct = 100; int tyre = 1; int fuel = 1; };
    std::vector<WearOpt> wearByPct;          // one element per career::kDurationPcts
    bool wearScaleTyre = true, wearScaleFuel = true;   // Settings toggles; off = your setting is respected
    std::vector<PreraceCar> cars;
    std::string selCarId;
    int  selVehHash = 0;
    bool selIsOwned = false, selFactory = false;
    std::vector<int> liveryNN;       // ordinal i <-> liveryNN[i] (the NN in the DDS filename the race consumes)
    int  liveryCount = 0;
    int  ownedLiveryOrdinal = -1;    // saved skin for the selected car (-1 = none)
    std::vector<int> ownedLiveries;  // 1:1 Memories: livery ordinals OWNED for the selected car (car+skin units)
    long costTyres = 0, costFuel = 0, costFluids = 0, costInsurance = 0, costTotal = 0;
    long entryFee = 0, inscDue = 0, travel = 0, coverAmt = 0, rentCost = 0;
    long outlayRent = 0, outlayOwn = 0, totalRent = 0, totalOwn = 0;
    long factorySalary = 0;
    bool canBuy = false, canRent = false, canRaceOwn = false, seatOk = false;
    int  launchLaps = 0, launchDurationMin = 0;   // NOMINAL preview of launchEp (the % is applied at launch)
    // REAL specs for the selected car, read from AMS2's VehicleDetails VM (strings already formatted like
    // in the game's own details screen). Empty = unavailable -> the app falls back to the PWR/GRP/BAL bars.
    std::string specDrivetrain, specEnginePos, specEngineType, specTopSpeed, specHorsepower, specWeight, specTorque;
};
using PreraceViewProvider = std::function<PreraceView(const std::string& carId)>;
void SetPreraceViewProvider(PreraceViewProvider p);
HandlerResult QueryPrerace(const jsonm::Value* params);   // params: { carId? }

// query.tryout: TRIAL / trial-as-race (screen_tryout.cpp DrawTryout), NOT parameterized (reads the ONE armed
// trial). The overlay assembles from career::TryoutUiActive() (real brief if armed, sample otherwise) + the
// real verdict (TrialUiHasResult/Pass/Pos/Field/TopN) + the g_trialArmed flag; className/discColor via the
// career::MarketClassInfo wrapper (the handler/overlay CANNOT call FindClass). trackName arrives already RESOLVED in TryoutUiActive
// (NOT a slug). The app POLLS this query while running until hasResult. loaded=false -> not_ready.
struct TryoutStake { std::string key, en, es; ImU32 color = 0; };   // key = i18n key for the bullet point; en/es = bilingual pair
struct TryoutView {
    bool loaded = false;             // false -> not_ready
    bool armed = false;              // g_trialArmed: false = brief falls back to the sample (stock car/Northham_Ring/Top 8)
    std::string clsId, className;
    ImU32 discColor = 0;             // MarketClassInfo(clsId) -> CssVar
    std::string trackName;           // TryoutUiActive().trackId (already resolved, NOT a slug)
    int  trackHash = 0;
    std::string team;
    int  topN = 0;                   // absolute objective (field of 5 -> Top-3); fixed when armed
    std::string targetEn, targetEs;  // objective already formatted ("Finish Top 3")
    std::string standardEn, standardEs;   // the ⚠ warning line
    std::vector<TryoutStake> stakes;
    bool hasResult = false;          // TrialUiHasResult(): verdict available -> running->result
    bool pass = false;               // TrialUiPass(): binary, pos<=topN
    int  pos = 0, field = 0, resultTopN = 0;   // real position/grid/Top-N from the settlement
};
using TryoutViewProvider = std::function<TryoutView()>;
void SetTryoutViewProvider(TryoutViewProvider p);
HandlerResult QueryTryout(const jsonm::Value* params);   // params: {} (reads the armed trial)

// query.rollover: END OF SEASON (screen_rollover.cpp DrawRollover), NOT parameterized. The overlay copies
// career::RolloverUiPreview() field by field + renewOfferId=RolloverUiRenewOfferId(); marketMoves.color (a literal
// ImU32 from the aging engine) -> CssVar. NO career_state.h wrapper (yourClass is already display-ready; color is a literal).
// ENGINE REALITY: retirements/promotions/marketMoves/yourTeam/yourClass/nextYear are sample STUBS; the renewal
// fields (hasRenewal/renewState/renewPerRace/renewSeries) + insurancePremium/cleanliness are LIVE from g_save. loaded=false->not_ready.
struct RolloverRetireView { std::string name, team; int age = 0; std::string reasonEn, reasonEs; };
struct RolloverPromoView  { std::string name, fromEn, fromEs, toEn, toEs; };
struct RolloverMoveView   { std::string driver, from, to; ImU32 color = 0; };
struct RolloverView {
    bool loaded = false;             // g_saveLoaded; false -> not_ready
    int  nextYear = 0;
    std::vector<RolloverRetireView> retirements;
    std::vector<RolloverPromoView>  promotions;
    std::vector<RolloverMoveView>   marketMoves;
    int  movesTotal = 0;             // 18/09: winter signings (marketMoves is the sample)
    std::string yourTeam, yourClass;
    bool hasRenewal = false;
    int  renewState = 3;             // RolloverRenewState 0 better/1 same/2 worse/3 none -> token
    long renewPerRace = 0;
    std::string renewSeries, renewOfferId;   // renewOfferId = "of_"+series (route to Negotiate for choice 1)
    long insurancePremium = 0;
    double cleanliness = 0.0;        // 0..100 EMA
    // 17/09: the report goes by SAMPLE (eight per list) and the total is the news. reportYear 0 = this
    // game hasn't gone through any winter yet -> the lists are empty on purpose.
    int retiresTotal = 0, promosTotal = 0, reportYear = 0;
};
using RolloverViewProvider = std::function<RolloverView()>;
void SetRolloverViewProvider(RolloverViewProvider p);
HandlerResult QueryRollover(const jsonm::Value* params);   // params: {}

// query.race_config: EXACT CONFIG OF THE LAST LAUNCH (DEBUG tooling, not a product feature) ---
// 1:1 mirror of the handoff CareerApplyEvent is about to write into the game, captured AFTER applying the
// AUTO/Inherited mask and BEFORE touching anything: it's literally "what the race would have been
// configured with." Its consumer is SIMULATION MODE coverage, where there's no game to inspect and the
// only place this was ever visible was the log ([event] APPLY / [event] params).
// Sentinel convention INHERITED FROM the engine struct (CareerEventParams), left untranslated: <0 (or 0 for
// fields marked "0 = don't force") = the mod does NOT write that slot -> the player's Custom Race config stands. The
// app renders it as "Inherited"; `inherited[]` says WHICH ones are inherited by a Settings decision (vs. simply not
// applying to the event). loaded=false -> not_ready (no race has been launched yet this session).
struct RaceConfigView {
    bool loaded = false;
    bool sim = false;                 // the captured launch was SIMULATED (none of this reached the game)
    std::string raceId;               // g_curRaceId ("series_id#round"; "tryout"/"" for trials/one-offs)
    std::string seriesName, trackName;   // resolved via SeriesById / CalendarForSeries ("" = unresolvable)
    int vehHash = 0, trackHash = 0, livery = -1;
    int laps = 0, durationMin = 0;    // laps XOR durationMin (0 = the other one governs)
    int opponents = 0, oppType = -1, numClasses = 0;
    int skill = 0, aggression = 0, mistakes = 0, aiWetSkill = -1;
    int startHour = -1, timeProg = 0;
    int weather[4] = { -1, -1, -1, -1 };
    int weatherSlots = 0;             // 0 = number of AUTO states by duration; 1..4 = forced
    int startType = -1, formationLap = -1;
    int mandatoryPit = -1, pitMinTyres = -1, pitMinFuel = -1, refuelling = -1, pitSpeedLimit = -1;
    int fcy = -1;                     // the REAL value being forced (Settings): 1/0 = Yes/No · -1 = Inherited (untouched)
    int practiceOn = -1, qualifyOn = -1, practiceMin = 0, qualifyMin = 0;
    int dateDay = -1, dateMonth = -1, dateYear = -1;
    int phase = 0, trialFlag = 0, trialTopN = 0;
    int honorInherit = 0;             // 0 forces everything / 1 calendar / 2 heroic gate (doesn't inherit AI)
    int inheritMask = 0;              // EFFECTIVE mask applied (kInh* bits, already accounting for the honorInherit==2 nuance)
    // Gameplay multipliers (tire wear / fuel consumption). They're NOT part of the handoff: they're
    // GLOBAL game options (the Options mega-struct) the mod writes at launch and RESTORES afterward, scaled
    // by race length. They travel here because for anyone auditing this, they're part of "how this race is actually
    // going to run" — and in simulation (which never writes them: nothing actually runs) this is the only place they're visible.
    bool wearArmed = false;           // will this launch actually apply them? (simulation -> false)
    int  wearLaps = 0;                // laps that will actually be RUN (duration % already applied) — the ones that scale
    bool scaleTyre = false, scaleFuel = false;   // Settings toggles, per axis
    int  tyreMult = 0, fuelMult = 0;  // multiplier that would apply (0 = not scaled: your global setting stands)
    int  tyreIdx = -1, fuelIdx = -1;  // TARGET game index (0..8; -1 = mega-struct unresolvable)
    int  tyreOrig = -1, fuelOrig = -1;// user's ORIGINAL index (the one restored after the race)
};
using RaceConfigProvider = std::function<RaceConfigView()>;
void SetRaceConfigProvider(RaceConfigProvider p);
HandlerResult QueryRaceConfig(const jsonm::Value* params);   // params: {} (reads the last launch)

// query.telemetry.race: PAYLOAD for the last race (spec 2026-07-24) ---
// It's assembled entirely by the ENGINE (a single source of truth for the backend schema) and sent by the APP, which is
// the one with network access. `optIn` is the consent flag: with it off the payload is still assembled (it's a read,
// it costs nothing) but the app does NOT send it anywhere — so the same code path doubles as local debugging.
// NO PERSONAL DATA: the PLAYER's name is deliberately omitted from their grid row (the AI drivers' names are
// ours and do travel: they're needed to tune per-driver pace). loaded=false -> not_ready.
struct TelemetryRaceView {
    bool loaded = false;
    bool optIn = false;
    int  schema = 1;                          // payload schema version (required by the backend)
    std::string raceId, seriesId, classId, className, trackName;
    long trackHash = 0;
    int  round = 0, year = 0, doy = 0;
    // Per-class DDA, rev.2: the trio that has to ADD UP for the corpus to be able to reconstruct the
    // race -- `aiBase` is THE CATEGORY's number before this race (what the adaptive loop had when arming,
    // before trim); `trim` is the one applied to THIS launch; `aiEffective` is what was actually written to the
    // game. By construction aiBase + trim == aiEffective WHEN `signal.valid` is true (the race revealed
    // something: there was a per-category recalibration) -- before this fix `aiBase` reported the save's OVERALL difficulty
    // and the trio didn't add up even along that path. With `signal.valid` false (fixed mode, the CALIBRATION race,
    // or the forced-level guard) `aiBase`/`trim` fall back to the LIVE overall/trim while `aiEffective` still
    // reflects what actually raced (e.g. DM_CUSTOM), so the identity does NOT hold: whoever consumes the
    // corpus has to filter on `signal.valid` before assuming it. offClass/offTrack have been zeroed since the
    // rev.2 spec -- they're kept in the payload because the backend already has them in its schema, not
    // because they still do anything.
    int  mode = 0;                            // career::DifficultyMode
    int  aiBase = 0, trim = 0, offClass = 0, offTrack = 0, aiEffective = 0, aggr = 0;
    // Per-class DDA, rev.2: despite the name (inherited from rev.1, where it was a LOCAL class/track
    // offset), this field is finally a real delta again: how far this
    // category's number drifts from the save's OVERALL difficulty (same criterion as SettingsDiffClass::delta), not an absolute. The
    // absolute value lives in `classAi`, further down.
    double classDelta = 0.0;
    int  classRaces = 0;
    // Per-class DDA, rev.2: the category's ABSOLUTE number after this race (0 = didn't learn) -- without this
    // the corpus can't measure whether the per-category loop converges, which is the whole point of this change.
    int  classAi = 0;
    // what the 0.5.0 data harvest COULDN'T answer, and this can.
    // `calibration`: this race is the CALIBRATION one (deliberately run at the seed value). Until now you had to
    //   infer it externally from class+track, which is fragile and breaks the day that combination changes.
    // `aiAfter`: the save's OVERALL difficulty after this race -- it travels SEPARATELY from the base/trim/effective trio
    //   (which is purely per-category): it's not "the after" of `aiBase` — they're two different quantities, kept
    //   deliberately separate, for anyone who wants to track the number Settings shows without reconstructing it.
    // `windowN`: how many samples THIS category's window held when it decided (after this race). It distinguishes
    //   "didn't move because it wasn't due" from "didn't move because it didn't have enough data yet".
    bool calibration = false;
    int  aiAfter = 0;
    int  windowN = 0;
    // loop signal measured at settle time (not reconstructible afterward)
    bool ddaValid = false, gapCapped = false;
    double gapNorm = 0.0, gapVsBase = 0.0;
    int  rivals = 0;
    // result
    int  pos = 0, field = 0;
    bool finished = false, clean = false;
    double bestLapS = 0.0, totalTimeS = 0.0;
    // weather: the ARMED sequence (what was requested) + the REAL rain that fell (peak mRainDensity 0..1)
    int  weather[4] = { -1, -1, -1, -1 };
    int  weatherSlots = 0;
    double rainMax = 0.0;
    // grid with best laps (already saved in race_log)
    struct TelemetryGridRow {
        std::string name, carClass, carName;
        int pos = 0, laps = 0;
        bool dnf = false, isPlayer = false;
        double bestLapS = 0.0, totalTimeS = 0.0;
        // SUMMARIZED pace. Best lap carries ±0.3s of noise over 13 laps, and between neighboring drivers
        // there's only 0.08-0.25s of signal, so it's useless for ranking pace. Three numbers travel here instead of the whole series
        // (by design): a median robust to pit stops and contact, the standard deviation of that same sample, and
        // how many laps support it. lapN = 0 -> the session produced no timing edges (e.g. a SIMULATED race).
        int lapN = 0;
        double lapMedianS = 0.0, lapSdS = 0.0;
        // LAP BY LAP. Accumulating this series reconstructs the gap between drivers on every lap and the
        // stint-by-stint degradation. It goes in FULL or not at all: a half series would produce false cumulative values with no warning, so
        // in long races (PaceSeriesFits) it's left empty and only the three summary numbers above travel. The
        // INDEX is the lap number; a 0 marks a lap with no credible time, not a lap of zero seconds.
        std::vector<float> lapSeries;
    };
    // true = the series above travel in full; false = only the summary (the race didn't fit).
    bool paceSeries = false;
    std::vector<TelemetryGridRow> grid;
    // the form response, if there was one
    int feel = 0;
    std::vector<std::string> reasons;
    // the FULL launch config, exactly as query.raceConfig serves it (same serializer)
    RaceConfigView config;
};
using TelemetryRaceProvider = std::function<TelemetryRaceView()>;
void SetTelemetryRaceProvider(TelemetryRaceProvider p);
HandlerResult QueryTelemetryRace(const jsonm::Value* params);

// Handlers exposed as functions: registered by RegisterCareerQueries and reused by
// career_test / the fixture export (--dump-api-fixtures) WITHOUT going through the envelope.
HandlerResult QueryCareerState(const jsonm::Value* params);
HandlerResult QueryStandings(const jsonm::Value* params);
HandlerResult QueryInbox(const jsonm::Value* params);
HandlerResult QueryProfile(const jsonm::Value* params);
HandlerResult QueryMemories(const jsonm::Value* params);
HandlerResult QueryCalendar(const jsonm::Value* params);
HandlerResult QueryResults(const jsonm::Value* params);
HandlerResult QueryTimes(const jsonm::Value* params);
HandlerResult QueryTree(const jsonm::Value* params);
HandlerResult QuerySettings(const jsonm::Value* params);
HandlerResult QueryGarage(const jsonm::Value* params);
HandlerResult CmdGarageSell(const jsonm::Value* params);   // 0.7.0: { carId, livery? } -> { ok, reason, paid, carId }
HandlerResult QueryMarket(const jsonm::Value* params);
// (QueryHub declared above next to HubView)

// === COMMANDS (cmd.*): MUTATE the save on the game thread. Sibling pattern to the queries: a THIN handler
// (parses params + builds the response, testable in career_test) + an EXECUTOR installed by the overlay (mutates
// g_save via the bridges). The handler returns not_ready if the executor isn't installed, bad_params if the
// parameter is invalid. The app calls client.cmd(...) and then refetch()es the affected query. ===
using SettingsSetModeExec = std::function<bool(int mode)>;   // 0..5 (5 = fixed by hand); returns ok
using SettingsSetTrimExec = std::function<bool(int trim)>;   // 5..+5 (the bridge clamps); returns ok
using SettingsSetAggrExec = std::function<bool(int aggr)>;   // 0=auto or 40/60/80/100 (the bridge anchors it)
using SettingsSetScaleExec = std::function<bool(bool tyre, bool fuel)>;   // gameplay scaling toggles
void SetSettingsCmds(SettingsSetModeExec setMode, SettingsSetTrimExec setTrim, SettingsSetAggrExec setAggr, SettingsSetScaleExec setScale);
using SettingsSetFcyExec = std::function<bool(int mode)>;                 // FCY 0=No 1=Yes 2=Inherited
void SetSettingsCmds2(SettingsSetFcyExec setFcy);
// AUTO/Inherited per field. field = contract token ("weather"/"hour"/"timeProg"/"start"/
// "pit"/"ai"/"sessions"); the executor returns false if the token doesn't exist (the bridge validates against
// career::SettingsInheritBit, the single source-of-truth table).
using SettingsSetInheritExec = std::function<bool(const std::string& field, bool inherit)>;
void SetSettingsCmds3(SettingsSetInheritExec setInherit);
// Block D: telemetry consent. Off by default; without it the app sends nothing.
using SettingsSetTelemetryExec = std::function<bool(bool on)>;
void SetSettingsCmds4(SettingsSetTelemetryExec setTelemetry);
// seed for the CALIBRATION race. That race runs at dda::kAiSeed (80) by design, and it's the
// first impression for everyone who installs the mod: 162 real measurements averaged a 1.68 feel rating, the worst in the whole
// corpus. With this, the player picks the number. Only BEFORE calibrating; afterward the level is learned by the adaptive loop
// and letting it be hand-tuned would just fight the loop. Returns false if already calibrated.
using SettingsSetSeedExec = std::function<bool(int seed)>;
void SetSettingsCmds5(SettingsSetSeedExec setSeed);
// FIXED difficulty chosen by hand (career::DM_CUSTOM). Unlike the calibration seed, this one can
// ALWAYS be changed: the card promises the player sets the number. The bridge clamps to 70..120.
using SettingsSetCustomAiExec = std::function<bool(int value)>;
void SetSettingsCmds6(SettingsSetCustomAiExec setCustomAi);
// DDA lock: two new controls. The rev.2 spec forbade new controls because back then it only
// grew the payload; here the control IS the feature, so the rule is deliberately lifted.
using SettingsSetClassLockExec = std::function<bool(const std::string& classId, bool locked)>;
using SettingsSetLockAllExec   = std::function<bool(bool locked)>;
void SetSettingsCmds7(SettingsSetClassLockExec setClassLock, SettingsSetLockAllExec setLockAll);
// 0.7.0 (thread «Progression / career speed»): the career PACE (prog.reward_mode, 0=easy 1=normal
// 2=hard) stops being fixed for life. The handler validates the token; the executor applies it to the save AND to
// profile.json (which wins on load) and returns false if it couldn't leave the two in agreement.
using SettingsSetRewardExec = std::function<bool(int mode)>;
void SetSettingsCmds8(SettingsSetRewardExec setReward);
HandlerResult CmdSettingsSetReward(const jsonm::Value* params);   // params: { mode: "easy"|"normal"|"hard" } -> { ok, mode, mult }

// Command-driven memory probe. Idea: instead of recompiling and restarting the game on
// every iteration of a memory investigation, the game stays open and queries go through the
// API. Returns the already-assembled JSON payload; the implementation lives in career_overlay.cpp, which is
// owns the sweeps and the navigation pointers.
using DbgMemExec = std::function<std::string(const jsonm::Value* params)>;
void SetDbgMem(DbgMemExec exec);
// Block C (spec C1): weather mode + selection weights. empty weights = don't touch the saved ones.
using SettingsSetWeatherExec = std::function<bool(int mode, const std::map<int,int>& weights)>;
void SetSettingsCmds5(SettingsSetWeatherExec setWeather);
HandlerResult CmdSettingsSetTelemetry(const jsonm::Value* params);   // params: { on: bool }
HandlerResult CmdSettingsSetSeed(const jsonm::Value* params);        // params: { seed: int } (70..120)
HandlerResult CmdSettingsSetCustomAi(const jsonm::Value* params);   // params: { value: int } (70..120) DM_CUSTOM
HandlerResult CmdSettingsSetMode(const jsonm::Value* params);   // params: { mode: "easy".."custom" }
HandlerResult CmdSettingsSetTrim(const jsonm::Value* params);   // params: { trim: int }
HandlerResult CmdSettingsSetAggr(const jsonm::Value* params);   // params: { aggr: int } (0|40|60|80|100)
HandlerResult CmdSettingsSetInherit(const jsonm::Value* params);   // params: { field: token, inherit: bool }

// per-id commands (accept/decline a market offer). The executor returns ok (the action re-validates in the
// bridge). market uses {offerId}. Accepting/declining refreshes the affected query in the app. (The
// sponsors sibling — SetSponsorsCmds/CmdSponsorsSign — was retired along with query.sponsors.)
using IdCmdExec = std::function<bool(const std::string& id)>;
// the engine's "no" used to get lost here — the exec returned a bool and the app showed a silent rejection.
// The ACCEPT exec now returns the REASON ("" = signed): "expired" (the offer is no longer on the market),
// "window" (market closed), or one of AcceptOffer's own blocks ("already"/"exclusive"/"afford"/"refused"). The
// handler emits it as {ok:false, offerId, code} — same {ok,...} envelope as always, code only on failure.
using IdReasonCmdExec = std::function<std::string(const std::string& id)>;
void SetMarketCmds(IdReasonCmdExec accept, IdCmdExec decline);
HandlerResult CmdMarketAccept(const jsonm::Value* params);     // params: { offerId } -> {ok, offerId, code?}
HandlerResult CmdMarketDecline(const jsonm::Value* params);    // params: { offerId }

// Inbox redesign: reading/answering from the app. markRead reuses IdCmdExec (idempotent:
// true even if it was already read). answer returns career::MailAnswer's code (0 ok / 1 not_found /
// 2 already_answered / 3 not_answerable / 4 bad_choice), mapped here to a typed code.
using InboxAnswerExec = std::function<int(const std::string& id, int choice)>;
void SetInboxCmds(IdCmdExec markRead, InboxAnswerExec answer);
// 18/09: cmd.inbox.markAllRead -> the exec marks everything and returns how many changed (and persists once).
using InboxMarkAllExec = std::function<int()>;
void SetInboxMarkAllCmd(InboxMarkAllExec markAll);
HandlerResult CmdInboxMarkRead(const jsonm::Value* params);   // params: { id } -> {ok, id}
HandlerResult CmdInboxAnswer(const jsonm::Value* params);     // params: { id, choice } -> {ok, id, choice}

// hub lifecycle command: terminate ALL driver contracts (CalUiBreakContract). Mirror of the
// hub bottom bar button. C1: the clock advance NO LONGER lives here — cmd.time.advance (below) replaced
// it, and with it went the TimeAdvanceExec that this setter carried as its first parameter.
using ContractBreakExec = std::function<void()>;            // terminates ALL contracts + persists
void SetLifecycleCmds(ContractBreakExec breakAll);
HandlerResult CmdContractBreak(const jsonm::Value* params);    // params: {} -> {ok}

// terminate the contract for ONE series, from "My Contracts" (CalUiBreakContractSeries). The engine already knew
// how to do this — the old market screen used it —, but it wasn't exposed, so the card stated what
// leaving would cost with no way to actually do it. A SEPARATE setter instead of extending SetLifecycleCmds, for the same reason
// as the rest of the hub commands: an existing signature is not touched.
// Returns false if that series had NO active contract (the screen was working from stale data), and in that case it
// neither charges nor penalizes anything: breaking something that's already gone can't cost money.
using ContractBreakSeriesExec = std::function<bool(const std::string& seriesId)>;
void SetContractCmds(ContractBreakSeriesExec breakSeries);
HandlerResult CmdContractBreakSeries(const jsonm::Value* params);   // params: { seriesId } -> {ok}

// C1 (spec ): the CLOCK WITH STOPS. query.time.preview and cmd.time.advance share params, destination resolver
// and payload: the app renders the preview and, if the player confirms, executes EXACTLY the same. OPTIONAL
// params, evaluated IN THIS ORDER (the SAME one query.time.jumpPreview had, retired here along with
// cmd.time.advanceWeek / advanceToNextEvent / advanceToDate):
// { seriesId }    -> destination = career::NextEventAbsDayForSeries (next event of THAT series);
// { untilAbsDay } -> destination = that day AS IS, WITHOUT snap («stop here» is that day; <=0 is equivalent to no cap);
// { scope: "enrolled" }  -> career::NextEventAbsDayEnrolled (only series you are already enrolled in);
// { scope: "available" } -> career::NextEventAbsDay (next global event);
// {} no params -> 0 = until the FIRST stop according to the rules. Never resolved:false: there is always a stop
// (at the latest December 31, a hard stop, or the career::kAdvanceCapDays cap).
// resolved:false (rest at 0/""/[], SAME keys ALWAYS) only when the series/scope selector returns -1;
// in cmd.time.advance that same case is the typed error 'unreachable' and executes NOTHING.
// The provider hands over RAW access to the live calendar (same idiom as CareerStateView: pointer, not copy)
// because the handler needs to invoke the destination selectors AND career::PreviewAdvance on the SAME
// calendar -- neither can be rebuilt from an already-resolved view. gates/frontierPhase
// are the SAME ones that gate BuildMasterCalendar (see query.calendar); playerName is required by the world pulse;
// rules are the save's five stop rules (career::StopRules), which travel in the payload so that the
// screen renders the switches without a second query.
struct TimeInputs {
    bool loaded = false;
    const career::CareerCalendar* calendar = nullptr;   // g_calendar / synthetic
    const std::set<std::string>* gates = nullptr;       // g_save.unlocked_gates / synthetic
    int frontierPhase = 0;                              // g_save.prog.frontier_phase
    std::string playerName;                             // CalUiMailPlayerName() / synthetic
    // access filter for the selectors (a closed series without a contract isn't a valid target). nullptr = fail-open.
    const career::SaveData* save = nullptr;             // g_save / synthetic
    career::StopRules rules;                            // g_save.stop_rules / synthetic
};
using TimeInputsProvider   = std::function<TimeInputs()>;
// 0.7.0 review (finding 1): the executor receives the last forecast (with its arrival state) to adopt it
// if the state hasn't moved since; nullptr or a forecast that doesn't correspond = simulates as usual.
using AdvanceUntilStopExec = std::function<career::AdvanceReport(int untilAbsDay, career::AdvancePreviewKeep* preview)>;   // CalUiAdvanceUntilStop
using StopRulesExec        = std::function<void(const career::StopRules&)>;           // CalUiSetStopRules
void SetTimeInputsProvider(TimeInputsProvider p);
void SetTimeAdvanceCmds(AdvanceUntilStopExec advance, StopRulesExec rules);
HandlerResult QueryTimePreview(const jsonm::Value* params);   // { seriesId? } | { untilAbsDay? } | { scope? } | {}, + resolveOnly?
HandlerResult CmdTimeAdvance(const jsonm::Value* params);     // SAME params; executes (executed:true)
HandlerResult CmdTimeRules(const jsonm::Value* params);       // { offers, myRaces, anyRace, mail, relation } (all FIVE)

// cmd.identity.submit: the ONLY entry point for career creation. Persists the profile form + seeds the seed/credits/calendar
// (all inside CareerIdentitySubmit; the executor does NOT duplicate effects). Validated in the handler (bad_params).
using IdentitySubmitExec = std::function<bool(const std::string& first, const std::string& last, const std::string& sex, const std::string& nat, int year, int month, int day)>;
void SetIdentityCmds(IdentitySubmitExec submit);
HandlerResult CmdIdentitySubmit(const jsonm::Value* params);   // params: { first, last, sex, nationality, year, month, day } -> {ok}

// Negotiation redesign: commands for the negotiation THREAD — send opens/counter-offers (creates or advances the negId
// via the engine's NegoOpen/NegoOffer); acceptTable/sign/walk operate on negId (sign what's on the table, sign
// the ACCEPTED_PENDING state, or walk away). They replace the instant-sign cmd.negotiate.accept (retired in this
// same change: negotiation is now a back-and-forth thread, not a one-shot accept). The handler passes an engine-free POD
// (NegotiateTermsInput) to the executor, which reconstructs career::DealTerms. sponsorCardId (sponsor
// cards): card_id of the chip picked in sponsorTierRank ("" = no card / "none offered").
struct NegotiateTermsInput { bool demands[8] = {}; int salaryPct = 50; int sponsorTierRank = -1; std::string sponsorCardId; };  // 8 = NEGO_DEMAND_COUNT
// cmd.negotiate.send   {offerId, terms:{demands[8],salaryPct,sponsorTierRank,sponsorCardId}} -> {ok, negId}
// cmd.negotiate.acceptTable {negId} -> {ok, code?} · signs what's on the table (base or counter)
// cmd.negotiate.sign        {negId} -> {ok, code?} · signs the ACCEPTED_PENDING deal
// cmd.negotiate.walk        {negId} -> {ok, code?} · walks away from the thread
using NegoSendExec = std::function<std::string(const std::string& offerId, const NegotiateTermsInput& terms)>; // "" = failure; otherwise, negId
// same twist as the market's accept — the exec returns the failure REASON ("" = ok): "closed"
// (thread doesn't exist or is no longer open/pending), or one of signing's own blocks ("window"/"already"/"exclusive"/
// "afford"/"refused"). runNegoIdCmd emits it as {ok:false, negId, code}.
using NegoIdExec   = std::function<std::string(const std::string& negId)>;
void SetNegoThreadCmds(NegoSendExec send, NegoIdExec acceptTable, NegoIdExec sign, NegoIdExec walk);
HandlerResult CmdNegoSend(const jsonm::Value* params);         // params: { offerId, terms:{demands[8],salaryPct,sponsorTierRank,sponsorCardId} } -> {ok, offerId, negId}
HandlerResult CmdNegoAcceptTable(const jsonm::Value* params);  // params: { negId } -> {ok, negId}
HandlerResult CmdNegoSign(const jsonm::Value* params);         // params: { negId } -> {ok, negId}
HandlerResult CmdNegoWalk(const jsonm::Value* params);         // params: { negId } -> {ok, negId}

// cmd.calendar.launch / launchCalibration: ARM the race in the live UI state (SampleState via
// CareerArmRaceLaunch/Calibration), same as clicking the calendar -> the app navigates to Prerace. launched=false
// (insufficient funds / nonexistent slot / already calibrated) => the app stays on the calendar. Overlay-only executors.
using CalLaunchExec     = std::function<bool(const std::string& seriesId, int round)>;
using CalCalibrateExec  = std::function<bool()>;
using CalSkipCalibExec  = std::function<bool()>;   // marks calibrated=true WITHOUT running the calibration race
void SetCalendarCmds(CalLaunchExec launch, CalCalibrateExec calibrate, CalSkipCalibExec skipCalib);
HandlerResult CmdCalendarLaunch(const jsonm::Value* params);            // params: { seriesId, round } -> {ok, launched}
HandlerResult CmdCalendarLaunchCalibration(const jsonm::Value* params); // params: {} -> {ok, launched}
HandlerResult CmdCalendarSkipCalibration(const jsonm::Value* params);   // params: {} -> {ok}

// cmd.event.arm {eventId, carClassId}: ARMS the EXHIBITION race for a special event (CareerArmEvent) in
// the live UI state -- WITHOUT a calendar slot -- so query.prerace/cmd.prerace.* can pick a car and launch. armed=false
// = event/class/track unresolvable. Overlay-only executor.
using EventArmExec = std::function<bool(const std::string& eventId, const std::string& classId)>;
void SetEventCmds(EventArmExec arm);
HandlerResult CmdEventArm(const jsonm::Value* params);   // params: { eventId, carClassId } -> {ok, armed}

// cmd.debug.setState: applies a DBGSAVE preset (0=legend/1=midpoint/2=clean/3=dda95) — OVERWRITES g_save (debug).
// The app exposes it behind the Ñ key (debug mini-menu). Overlay-only executor (calls CareerDebugSetSave).
using DebugSetStateExec = std::function<void(int preset)>;
// — cmd.debug.simRace {enabled}: toggles race SIMULATION mode (debug, Ñ menu).
// With the mode ON, launching a race does NOT touch the game: the overlay fabricates a synthetic result (biased toward
// a win/podium, with a chance of failure) and settles it through the real settlement path. Overlay-only executor (sets g_simRaceMode).
using DebugSimRaceExec = std::function<void(int enabled)>;
// cmd.debug.simWorld: simulates the world FROM TODAY TO A DATE using the real logic (SimulateWorld:
// runs whichever rounds are due and advances the clock through the same path the app itself uses). It exists so there's no need to
// manually test what happens as time passes.
//
// By default it does NOT touch the save: it works on a COPY and returns the timeline. "Let's see what happens
// up to 2028" can't cost you your save. With apply=true it does commit, which is the other legitimate use (jumping
// forward to check the state), but it has to be explicitly requested.
//
// Returns the already-assembled JSON payload: this way the simrunner's types stay where they live and this module doesn't
// need to know them. "" = the engine couldn't do it (no save loaded).
using DebugSimWorldExec = std::function<std::string(int toYear, int toDoy, bool apply, int perf)>;

void SetDebugCmds(DebugSetStateExec setState, DebugSimRaceExec simRace = nullptr,
                  DebugSimWorldExec simWorld = nullptr);
// params: { toYear, toDoy, apply? } -> { ok, applied, days, from, to, races, windows, ..., timeline[] }
HandlerResult CmdDebugSimWorld(const jsonm::Value* params);
HandlerResult CmdDebugSetState(const jsonm::Value* params);   // params: { preset: 0..3 } -> {ok, preset}
HandlerResult CmdDebugSimRace(const jsonm::Value* params);    // params: { enabled: bool } -> {ok, enabled}

// Experimental Low RAM Mode — cmd.debug.lowRam {enabled}: a switch on the Play window. With it ON, the overlay does NOT
// trigger the global ~10GB memory scan of the game's live state on the handoff's per-frame path (the cause of the freeze+RAM spike
// on DDR4 machines). It does NOT persist in the engine; the app re-sends it on connect. Overlay-only executor.
using DebugLowRamExec = std::function<void(int enabled)>;
void SetDebugLowRamExec(DebugLowRamExec exec);
HandlerResult CmdDebugLowRam(const jsonm::Value* params);     // params: { enabled: bool } -> {ok, enabled}

// — cmd.app.closing {}: the app signals that it's CLOSING (best-effort, before the window closes).
// The overlay quiesces the visuals (a notice, a cover) and goes passive; the WS server KEEPS listening for the
// warm-attach when the app reopens. The engine is NOT unloaded: ejecting a module with live callbacks and its own
// threads is a real crash risk for the game (a deliberate design decision).
using AppClosingExec = std::function<void()>;
void SetAppClosingExec(AppClosingExec exec);
HandlerResult CmdAppClosing(const jsonm::Value* params);      // params: {} -> {ok}

// (multi-profile): query.profiles + cmd.profile.create/select/delete. Each profile = a save subfolder
// (userdata\saves\profiles\<slug>\) with its own career.sav/seasons/standings + a profile.json {name, mode}.
// mode: "career" (default) | "sandbox" (everything unlocked + 10M credits on creation). The ACTIVE profile is persisted
// in userdata\saves\active_profile.txt. Overlay-only providers/executors (the overlay owns paths and reload).
struct ProfileEntry { std::string slug, name, mode; };
struct ProfilesView { std::string active; std::vector<ProfileEntry> profiles; };
using ProfilesProvider  = std::function<ProfilesView()>;
// create(name, mode, outSlug, outErr) -> creates + ACTIVATES the new profile (fresh state; sandbox applies its preset).
// select(slug, outErr) -> activates + RELOADS all state from the profile. remove(slug, outErr) -> deletes a
// NON-active profile. false => outErr carries the reason ("exists"/"not_found"/"active"/"io").
using ProfileCreateExec = std::function<bool(const std::string& name, const std::string& mode, std::string& outSlug, std::string& outErr)>;
using ProfileSelectExec = std::function<bool(const std::string& slug, std::string& outErr)>;
using ProfileDeleteExec = std::function<bool(const std::string& slug, std::string& outErr)>;
void SetProfilesProvider(ProfilesProvider p);
void SetProfileCmds(ProfileCreateExec create, ProfileSelectExec select, ProfileDeleteExec remove);
HandlerResult QueryProfiles(const jsonm::Value* params);      // {} -> {active, profiles:[{slug,name,mode}]}
HandlerResult CmdProfileCreate(const jsonm::Value* params);   // { name, mode?: "career"|"sandbox" } -> {ok, slug}
HandlerResult CmdProfileSelect(const jsonm::Value* params);   // { slug } -> {ok}
HandlerResult CmdProfileDelete(const jsonm::Value* params);   // { slug } -> {ok}

// cmd.prerace.buy / cmd.prerace.launch: MUTATE the pre-race flow on the live UI state (SampleState).
// Overlay-only executors -> POD wrappers career::PreraceBuy / PreraceLaunch (career_season_ui.h, defined in
// screen_vehicle.cpp). buy(carId,liveryOrdinal) = CareerBuyCarId (buys + persists the skin; does NOT launch). launch(carId,
// action{0=own,1=rent},liveryOrdinal,durationPct) = reconstructs ep from launchEp + ApplyDurationPercent(durPct) +
// resolves liveryNN[ordinal], and runs the CareerSetRaceEconomics+SetRaceId+SetChampClass+ApplyEvent chain
// (feedback from testing: NOTHING is charged here — entry fees/enrollment/DNFs happen at settle time, once there's a result).
using PreraceBuyExec    = std::function<bool(const std::string& carId, int liveryOrdinal)>;
using PreraceLaunchExec = std::function<bool(const std::string& carId, int action, int liveryOrdinal, int durationPct)>;
void SetPreraceCmds(PreraceBuyExec buy, PreraceLaunchExec launch);
HandlerResult CmdPreraceBuy(const jsonm::Value* params);     // params: { carId, livery?: int } -> {ok}
HandlerResult CmdPreraceLaunch(const jsonm::Value* params);  // params: { carId, action:"own"|"rent", livery?: int, durationPct?: int } -> {ok, launched}

// cmd.tryout.arm/start/clear: MUTATE the armed trial (the overlay's live UI state). arm(offerId) = resolves the
// OfferDef in MarketUiOffers (like cmd.market.accept) and calls TrialUiArm; start() = TrialUiLaunch(TryoutUiActive().topN)
// (launches the real race); clear() = TrialUiClearResult (consumes the verdict when leaving the result screen). Overlay-only executors.
using TryoutArmExec   = std::function<bool(const std::string& offerId)>;   // false = offer unresolvable
using TryoutStartExec = std::function<bool()>;                             // launches the real race
using TryoutClearExec = std::function<void()>;
void SetTryoutCmds(TryoutArmExec arm, TryoutStartExec start, TryoutClearExec clear);
HandlerResult CmdTryoutArm(const jsonm::Value* params);      // params: { offerId } -> {ok, armed}
HandlerResult CmdTryoutStart(const jsonm::Value* params);    // params: {} -> {ok, launched}
HandlerResult CmdTryoutClear(const jsonm::Value* params);    // params: {} -> {ok}

// cmd.rollover.confirm: commits the end-of-season contract decision. choice token "renew"|"newgoals"|
// "decline" -> 0/1/2 (bad_params if unknown); executor = RolloverUiConfirm(choice) (a STUB today). Overlay-only.
using RolloverConfirmExec = std::function<bool(int choice)>;
void SetRolloverCmds(RolloverConfirmExec confirm);
HandlerResult CmdRolloverConfirm(const jsonm::Value* params);   // params: { choice: "renew"|"newgoals"|"decline" } -> {ok}

// cmd.season.ackBonus: consumes the "season finished" flag (a screen bug: the BACK TO SERIES button doesn't
// clear it; it only clears when starting/leaving a season). The overlay's executor calls
// career::CalUiClearSeasonFinished() -> after the cmd, query.season comes back with bonusShown=false. {} -> {ok}.
using SeasonAckBonusExec = std::function<void()>;
void SetSeasonCmds(SeasonAckBonusExec ack);
HandlerResult CmdSeasonAckBonus(const jsonm::Value* params);   // params: {} -> {ok}

// silly season: THE PADDOCK (from the design prototype) ---
//
// query.paddock does NOT use its own view provider, and that's deliberate: everything it renders is DERIVED from the save and
// the clock (career::MarketCalendarTick has already left the state consistent with the date), so the handler reads
// CareerStateView — the same seat query.inbox/standings use — and computes from it. A provider in the overlay would be
// just another copy of the same computation, and outside the test harness to boot: this way the whole tab is testable without the game.
//
// {} -> { phase: "season"|"window"|"decision", year, targetYear, windowYear, daysToWindow, daysToDecision,
//         seats: [ { seriesId, seriesDisplay, level, team, status, applied, winnerName,
//                    occupantName, expiryYear } ] }
// status = free|sounded|disputed|offered|rejected|renewed (an exact mirror of WindowSeatState / PD_SEAT_STATUS).
// windowYear = the SEASON the live window is sealed for (0 = none yet).
// expiryYear = year the occupant's contract expires (derived; 0 if the seat is free).
HandlerResult QueryPaddock(const jsonm::Value* params);        // params: {} -> see above

// cmd.paddock.apply: APPLY to a window seat (career::ApplyToWindowSeat). Mutates the save, so it
// goes through an executor installed by the state's owner (overlay: applies + persists; test harness: synthetic save).
// ok=false with applied=false if the seat isn't in play, you'd already applied, or it's renewed/resolved.
using PaddockApplyExec = std::function<bool(const std::string& seriesId, const std::string& teamLabel)>;
// cmd.paddock.sign: SIGN an OFFERED seat (career::SignWindowSeat -> AcceptOffer). It's not "pick one":
// holding multiple contracts is deliberate; the real limits are the calendar, money, and exclusivity — not a hard cap.
// The executor returns the failure REASON via `reason` so the screen can explain it.
using PaddockSignExec  = std::function<bool(const std::string& seriesId, const std::string& teamLabel,
                                            std::string* reason)>;
void SetPaddockCmds(PaddockApplyExec apply, PaddockSignExec sign);
HandlerResult CmdPaddockApply(const jsonm::Value* params);     // params: { seriesId, team } -> {ok, applied}
HandlerResult CmdPaddockSign(const jsonm::Value* params);      // params: { seriesId, team } -> {ok, signed, reason}
// the team's CAR for a paddock seat (for example: Jessamy Lum364 has 6 cars and the
// seat didn't say which one). The overlay resolves it with the SAME deterministic ResolveTeamSeat as the offer
// preview and the contract seat — what you see is what you'd end up driving. Without a provider (tests,
// engine without an overlay) or without a resolution (a class with no cars installed) the handler OMITS the fields.
using PaddockSeatCarFn = std::function<bool(const std::string& seriesId, const std::string& teamLabel,
                                            std::string& carName, std::string& carSlug, int& liveryNN)>;
void SetPaddockSeatCarProvider(PaddockSeatCarFn fn);

// Registers query.career_state / standings / inbox / profile / memories / calendar / results under 'core'.
void RegisterCareerQueries(ApiCore& core);

} // namespace capi
