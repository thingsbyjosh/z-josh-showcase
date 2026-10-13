#pragma once
#include "career_engine.h"
#include "career_negotiate.h"   // DealTerms (the negotiated salary%/demands/sponsor chip on a contract)
#include "career_market_data.h" // market::OfferDef — the renewal carrier in pending_renewals
#include <vector>
#include <map>
#include <set>
#include <string>

namespace career {
// An owned car (the wear/insurance feature uses wear/insured; otherwise it's just persisted).
// livery = the ordinal of the livery chosen at purchase within the car's list (CareerVehicleLiveryList / the
// selector strip). -1 = not chosen -> the game's default (lowest NN). Additive: old saves with no column
// load -1. It's the same value used by the preview, the garage, and the launch handoff (3-way consistency).
// km (0.7.0): race kilometers accumulated with THIS car (laps x track length when settling); they lower the
// resale price (CarResaleValue). Additive 'ownedkm' line in the save, only if km > 0.
struct CarOwnership { std::string class_id; int vehid = 0; int races = 0; float wear = 0.0f; bool insured = false; int livery = -1; double km = 0.0; };
struct ClassStats   { int races = 0, wins = 0, podiums = 0, dnf = 0; };

// One row of the full grid for a race (you + AI), captured at settle from the
// classification (career::Classify) plus the shared-memory read (best_lap per participant). Enables the
// native-style post-race times screen (every position) and the per-driver drill-down. Real total_time only for the
// player (shared memory doesn't give a total for AI); -1 = unknown (AI will get it by summing laps). laps_t =
// reserved for later use. Additive: persisted in its own 'rgrow' lines right after its
// 'racelog'; an old save without those lines loads empty grids (the UI falls back to the single summary row, back-compat).
struct RaceGridRow {
    int pos = 0;               // classified rank (1..N); dnf flagged separately
    std::string name;          // driver name (display)
    std::string driver_id;     // stable pool id ("" if unresolved) — used by the drill-down/nemesis views
    std::string car_class;     // mCarClassNames[i]
    std::string car_name;      // mCarNames[i] (model)
    int  laps = 0;
    float best_lap = -1.0f;    // mFastestLapTimes[i] (s); <=0 = no valid lap
    bool dnf = false;          // DNF/DSQ (not classified)
    bool is_player = false;    // the player's row (highlight)
    float total_time = -1.0f;  // real total time (player only); -1 = unknown (AI doesn't have this yet)
    // (Aug 24) Is this row a final result (crossed the line / classified)? false = still on track when
    // captured (settle fires when the player finishes, and half the grid is still on its last lap): its
    // laps/total are partial, and with a deficit of d laps versus the leader it will end up d-1 laps down — not d. The
    // TIMES screen was marking "+1 L" on the entire trailing field because it derived the lap deficit from laps alone.
    bool finished = true;
    std::vector<float> laps_t; // time per lap; empty in
};

// The "Standings" tab = the times log (not championship standings, which live under Championships). One entry
// per settled career race: your best lap + total time + result + track/class + date. It's
// added at settle (only our own races, after the signature guard); capped at the last kRaceLogMax. Empty in
// old saves -> back-compat ('racelog' lines are additive, only if present).
struct RaceLogEntry {
    std::string class_id;      // class raced (display via FindClass / ResolveClass)
    std::string series_id;     // series/championship ("" for a standalone race)
    std::string track;         // readable track name (from shared memory)
    long track_hash = 0;
    int pos = 0, field = 0;    // your result: the position you finished in or the one you left in (see `finished`)
    // P2 (0.7.0 port, F15): you finished or abandoned. The comment above said «pos<=0 = DNF» and nobody
    // wrote it: the settle sets rl.pos = your position from the shm, and AMS2 classifies you the same when you retire, so an
    // abandonment showed as «P24 / 24» in Hub, TIMES, Results and Profile, the bad-streak mail was not cut and the
    // telemetry took everything as finished. It comes from the settle's `r.finished` (RETIRED and DSQ count too).
    // On disk it does NOT lengthen `racelog` (0.6.8 would stick the field onto the rating): it goes on a separate 'rldnf' line.
    bool finished = true;      // false = DNF / retired / disqualified
    float best_lap = -1.0f;    // your best lap (s); <=0 = no valid lap
    float total_time = -1.0f;  // your total time (s); <=0 = no data
    int year = 0, doy = 0;     // the continuous-clock date when it was raced
    // Post-settle player rating snapshot taken once at Settle (after prog.rating
    // updates, so it INCLUDES this race's delta). Feeds the de-mocked career-arc (ProfileRatingArc reads the
    // race_log). 0 = legacy/unknown (the sentinel for entries written before this field existed; rating is always
    // > 0 in practice) -> the arc omits sentinel-0 points (no zero-spike). Additive: serialized as a trailing
    // pipe-field on the 'racelog' line written when present; a legacy 10-field line parses with rating -> 0.
    // kSaveVersion stays 2 (MigrateSave no-op).
    int rating = 0;
    // Full grid for this race (you + AI), in classified order. Empty in old saves /
    // races that predate this feature -> the UI falls back to the summary row. Persisted in 'rgrow' lines after 'racelog' (additive).
    std::vector<RaceGridRow> grid;
};
constexpr int kRaceLogMax = 80;   // cap on stored entries (the most recent ones); it's a log, not infinite history

// (Jul 24 2026) Dumps a grid to CSV as part of a difficulty review, to measure
// in-game the transfer function from race_skill to lap time. A fixed header plus one row per driver, in
// vector order (which is the classified order). A sibling of the 'rgrow' serialization in career_save.cpp.
std::string FormatRaceGridCsv(const std::vector<RaceGridRow>& grid);

// The player's persisted identity (first-class, like a pool driver). player_id is stable
// (synthetic "player" by default); name/nationality are captured from shared memory; number is reserved.
struct PlayerIdentity { std::string player_id, name, nationality; int number = 0;
    // Living World onboarding (a profile filled in by the player on the first screen; gated until completed).
    std::string surname;                                  // surname -> lineage (ancestors = first name + this surname)
    std::string sex;                                      // "male"/"female"/"other"
    int birth_year = 0, birth_month = 0, birth_day = 0;   // 0 = not filled in yet (CareerIdentityNeeded)
};

// Courting record. A held offer (you have the reputation, but the series is blocked by the XP phase or
// superlicense gate) leaves a persistent trace: "the team remembers you". Once the XP/gate opens, GenerateOffers
// stops marking it held and it becomes signable; courting is just the memory that they already wanted you. It does not freeze
// the terms (GenerateOffers recalculates salary/bonus at signing). first_seen_year = the year it was first detected.
struct Courting {
    std::string team_id, series_id;
    int first_seen_year = 0;
    bool met_rep = false;      // you already clear the REP soft-floor (only XP/gate holds it)
    bool tier_locked = true;   // still blocked by XP phase / superlicense (the retained-offer state)
};

// A closed, archived season (career honors). Snapshotted in RolloverYear before clearing the
// entries: for each enrolled SeasonState, the player's final position is computed (FinishSeason/
// FinalTable) and {series, year, final position, number of rounds} is saved. Empty in old saves -> back-compat.
struct CompletedSeason {
    std::string series_id;
    int year = 0;
    int final_pos = 0;
    int rounds = 0;
    // The team the player drove for in this archived season (a snapshot of the active
    // contract's team_id at rollover, BEFORE contracts are rolled/cleared). "" = legacy archived season
    // OR a self-funded season with no team -> the safe "unknown org" value the future per-org cross-series
    // brand-affinity signal treats as no-signal. ADITIVE: serialized as a trailing pipe-field written
    // only when non-empty, so a legacy 4-field 'season' line still parses (team_id -> ""). kSaveVersion
    // stays 2 (MigrateSave no-op). This change ONLY captures+persists the data; it does NOT wire the
    // relationship term yet (deferred to a later real-team-naming fix).
    std::string team_id;
};

// A persisted generated email. Emitted by the email generator (career_contract.cpp) at
// the 3 deterministic ticks (post-Settle, AdvanceWeek, RolloverYear). Saved additively in SaveData.emails
// ('email' lines, write-only-when-present, pipe-delimited). family = 0 OFFER (they come looking for you) / 1 PROGRESS|MILESTONE.
// read/answered live here (they moved out of screen_inbox.cpp's statics). payload_key = the
// translation-template key that the renderer re-resolves to EN/ES when rendering — the resolved body is never saved (bilingual via translation keys).
// The id is content-derived (stable, dedup); season milestones embed the year (ms_titlefight_<series>_<year>)
// so they can re-fire the following season with no cleanup step. POD: the Courting model.
struct GeneratedEmail {
    std::string id;            // content-derived id (dedup): "offer_of_<series>" | "ms_<type>[_<series>]_<year>"
    int tag = 1;               // 0=sponsor 1=team 2=official (the sender's voice, EmailDef.tag)
    int day = 0;               // calendar day-of-year when generated (ordering + retention = oldest first)
    int family = 0;            // 0=OFFER 1=PROGRESS/MILESTONE (there is no "you let it pass" family)
    int read = 0;              // 0=unread 1=read (moved from screen_inbox.cpp's static g_read)
    int answered = 0;          // 0=unanswered 1=answered (the choice/design; moved from g_emailChoice)
    int choice = 0;            // index of the chosen option (0 by default)
    int subtype = 0;           // sub-type within the PROGRESS/MILESTONE family (the OFFER family uses its own
                                // contract-type index, 0..5, see DeriveOfferPresentation): 1=repband
                                // 2=firstpodium 3=firstwin 4=titlefight 5=finalseason 6=ob_intro 7=ob_first_race
                                // 8=ob_first_goal 9=hotstreak 10=coldstreak; 11=season_preview and
                                // 12=season_close (rollover — career_calendar.cpp RolloverYearReport).
    std::string payload_key;   // translation key of the template (the renderer resolves EN/ES); can contain spaces -> pipe

    // Variant chosen at generation time (persisted so the text doesn't
    // change if variants are added later); percent-escaped k=v params (MailParamsEncode — the body is still not
    // persisted), and the sender persona (from_name raw display + from_role_key, the role's translation key).
    // Additive: 4 trailing fields on the 'email' line (13 fields); a 9-field save loads with defaults.
    int variant = 0;
    std::string params;          // MailParamsEncode() — "" = no params (old email)
    std::string from_name;       // "Atenea Montero" | team principal | sponsor brand; "" = fallback by family
    std::string from_role_key;   // "inbox_role_manager" | "inbox_role_teamprincipal" | "inbox_sponsor"

    // (Jul 20 2026) An email that belongs to a negotiation thread (a proposal
    // received/counter-offer/close). "" = an email with no thread (every other family/subtype). Additive 14th field
    // at the end of the 'email' line (13 -> 14 fields); an old 13-field save loads with neg_id = "".
    std::string neg_id;

    // fix for emails getting an absurd date across a year boundary (Jul 23 2026): the calendar year at the time the email was sent.
    // Before this field, query.inbox rendered every email with the current year (cal.year) because 'day' only
    // stores the day-of-year (no year) — an old email survived retention fine within the same year, but as soon as
    // the career crossed into a new year, the same day-of-year got redrawn with the year already advanced (a nonsensical date,
    // sometimes "future"). 0 = unknown (a pre-fix save, field never written) -> it's the only case where the
    // API's emitter falls back to the current year as the best available data (the same behavior every
    // email had before this fix: we never invent a year we don't have). Additive 15th field, at the end
    // of the 'email' line (14 -> 15 fields); an old 14-field save loads with year = 0.
    int year = 0;
};

// Per-memory legacy progress. `cleared` = times this era memory has been completed (0 = never;
// >0 means first-clear rewards already granted -> replays pay money only). `last_played_year` = the
// calendar year of the most recent clear (the annual-replay throttle: one money replay per in-game year).
// Keyed by memory_id in SaveData::memories_done. Empty in old saves -> back-compat.
struct MemoryProgress { int cleared = 0; int last_played_year = 0; };

// (Jul 20 2026) A compact per-round log of the thread (feeds the conversation view).
struct NegoRoundLog {
    int day = 0;               // absolute day (year*365+doy)
    int who = 0;               // 0=player's proposal · 1=team's response
    int kind = 0;              // who=0: 0=proposal · who=1: NegoReplyKind (career_nego_thread.h)
    unsigned demands_mask = 0; // bitmask of the 8 demands
    int salaryPct = 50;
    int sponsorTierRank = -1;
    std::string sponsorCardId;
};
// A live/closed negotiation over a fixed snapshot of the offer (offers regenerate every
// cycle; the thread can't shift under the player's feet). Closed threads are kept until rollover.
struct Negotiation {
    std::string neg_id;         // "neg_" + offer.id + "_" + year + "_" + the opening absDay (unique even if reopened the same year)
    market::OfferDef offer;     // snapshot (the same subset persisted by the 'renewal' line)
    int state = 0;              // NegoThreadState
    int round = 0;              // proposals sent
    int patience = 0;           // remaining hidden budget
    int patience_base = 0;      // for the qualitative band
    int respond_day = 0;        // absolute day of the promised response (WAITING)
    int deadline_day = 0;       // absolute day the offer dies (TTL/events)
    int reserve_day = 0;        // ACCEPTED_PENDING: sign before this day
    int last_action_day = 0;    // last player action (decay from inactivity)
    int want_pct = 50;          // want_t*100 sealed when opened
    int form_pct = 70;          // CareerForm*100 sealed when opened (delta at resolution)
    int trial_pct = 100;        // trialRatio*100 sealed when opened
    int flags = 0;              // bit0=pressured bit1=reminded bit2=sweetened
    DealTerms player_terms;     // last proposal
    DealTerms table_terms;      // what's on the table (neutral=base offer, or its counter)
    bool table_is_counter = false;
    std::vector<NegoRoundLog> history;   // capped at NEGO_HISTORY_MAX (12), oldest drops off
};

// A signed sponsor with its running coverage-cap usage. id/tier/traits snapshot the catalog entry at
// signing (so a later catalog edit doesn't retro-change an active deal); per-trait usage advances each race.
// Persisted as additive 'sponsor' lines (only when signed -> old saves stay byte-identical / back-compat).
//
// A sponsor's value is now a fixed sum credited when its thematic objective
// completes — not per-race cost coverage. The obj_*/paid/signed_year fields below are the
// per-SPONSOR fields (a single telemetry/result objective + its running progress + a pay-once latch +
// the year it was signed), persisted via the additive `sponsor_obj` save line. They default to
// empty/0/false so a legacy save (no sponsor_obj line) loads clean (back-compat). The coverage-cap
// fields (traits/rounds_used/value_used) are dormant for now — they're consumed by a separate
// economic layer ("Economy Level 0–6") — keep them, do not delete.
// (retirement of the old sponsor system): these 2 types used to live in career_sponsor.h (deleted). They're
// moved here, byte-identical, because SignedSponsor (right below) and its save parser/writer (career_save.cpp)
// need them to be able to migrate saves v<3 (the refund migration) — the rest of the old system (catalog,
// gates, payout) is retired; these 2 types are pure data shapes, with no logic.
// Sponsor tier (0..4). Index == bal::SPONSOR_ACQ_COST index. Higher = more useful, harder to land.
enum SponsorTier { ST_LOCAL = 0, ST_REGIONAL = 1, ST_NATIONAL = 2, ST_INTERNATIONAL = 3, ST_ELITE = 4 };
// A coverage trait covers a deal cost component partially, up to a cap. Media-type traits are FLAVOR
// (coverage=false): no cap, no gameplay effect. tyres/fuel/transport/logistics are COVERAGE traits.
enum TraitKind { TRAIT_TYRES = 0, TRAIT_FUEL = 1, TRAIT_TRANSPORT = 2, TRAIT_LOGISTICS = 3, TRAIT_MEDIA = 4 };
struct SponsorTrait {
    int  kind = TRAIT_MEDIA;     // TraitKind
    int  cap_rounds = 0;         // 0 = no cap (flavor) ; >0 = covers at most this many rounds
    long cap_value  = 0;         // 0 = no cap (flavor) ; >0 = covers at most this many credits total
};

struct SignedSponsor {
    std::string id;
    int tier = 0;                          // SponsorTier snapshot
    std::vector<SponsorTrait> traits;      // coverage traits (kind + caps) snapshot  [dormant for now]
    std::vector<int>  rounds_used;         // parallel to traits: rounds consumed per trait  [dormant]
    std::vector<long> value_used;          // parallel to traits: credits consumed per trait  [dormant]
    // Per-sponsor objective fields (additive `sponsor_obj` line; back-compat defaults):
    std::string obj_kind;                  // thematic objective kind ("" = no objective -> line omitted)
    int  obj_goal     = 0;                 // objective target (e.g. N podiums / top8 / wins)
    int  obj_progress = 0;                 // running count toward the goal (bumped at Settle)
    bool paid         = false;             // pay-once latch: fixed payout credited
    int  signed_year  = 0;                 // calendar year the sponsor was signed
};

// ===== Sponsor cards (Jul 11 2026). uid == card_id: the draw excludes owned cards, no duplicates. =====
struct SignedCard {
    std::string card_id;
    int  level        = 1;
    int  slot         = -1;    // 1 = bench; 0..5 = active slot
    int  races_active = 0;     // races settled with the card in a slot (the upgrade gate)
    int  races_left   = 0;     // remaining life (only when life_kind==RACES)
    int  uses_left    = 0;     // remaining uses (only when life_kind==USES)
    int  clause_miss  = 0;     // consecutive failures (notop10_lose); resets on a top-10
    long invested     = 0;     // purchase + paid upgrades (the base for the 40% resale)
};
struct CardShopState {
    bool open = false;         // a shop visit is pending (post-race, not yet closed)
    std::string race_id;       // the race that generated it
    int  seed_base = 0;        // completed_races.size() at generation time (part of the seed)
    int  rerolls   = 0;
    int  bought    = 0;        // purchases made in this shop
    std::vector<std::string> offers;      // offered card_ids
    std::vector<std::string> bought_ids;  // already bought from this offer (for the "SIGNED" stamp)
};

// A driver's persisted contract. v1 = a single active contract. 'kind' = how the seat was gotten:
// 0=works (factory/signed: "you get paid" -> Settle applies the salary), 1=customer (no salary),
// 2=pay_driver (reserved). signed_year = the calendar year at signing. active=false -> no contract
// (old saves load to this default via back-compat). The seat is freed when broken (BreakContract).
struct Contract {
    std::string series_id, team_id; int seat_idx = 0;
    int kind = 0;                 // 0=works 1=customer 2=pay_driver
    int seasons_total = 1, seasons_left = 1;
    long sign_bonus = 0, per_race = 0; int signed_year = 0;
    bool active = false;
    DealTerms accepted_deal;      // The negotiated terms locked in at signing (default = neutral, no demands)
    // Trial/conditional extras. obj_ratio = completed/total of the trial objective set
    // (drives renewal + negotiation leverage); promote_to = conditional-contract target series_id; buy_in =
    // pay_driver up-front cost paid at signing. All default-zero -> a works/customer save is byte-identical.
    int  obj_done = 0, obj_total = 0;     // trial objective completion ratio
    std::string promote_to;               // conditional: target series_id ("" = none)
    long buy_in = 0;                      // pay_driver up-front cost
    // 0.4.0 season objectives (Jul 21): the championship finishing position the team demands of you
    // (0 = no objective). Assigned at signing (bal::SEASON_OBJ_TARGET by type); the rollover judges the
    // renewal/dismissal against it (goal-relative). Persisted as the 5th field of 'contract_ext' (additive:
    // an old save loads 0 -> judged by the raw table; kSaveVersion stays 4).
    int  obj_target_pos = 0;
    // Narrative archetype carried from the offer at sign (career::ContractArchetype 0..8).
    // Default 0 = ARCH_SELF_FUNDED_ENTRY -> a legacy save (no contract_arch line) loads to this safe default.
    int  archetype = 0;
    // Per-contract missed-round counter. Incremented when a contracted round is DNF'd as
    // a no-show (DnfPassedRounds), each miss applies RATING_NOSHOW_DING via DnfKind::NoShow; at
    // bal::MK_NOSHOW_BREACH_LIMIT the contract auto-BreakContracts. Also the no-show-season signal: when
    // missed_rounds == the series' round count the ChampionshipBonus REP-floor is lifted (no-show season may
    // land <=0 REP). Persisted via an ADITIVE 'contract_miss' line (write-when-non-zero; NOT extending
    // contract_ext). Default 0 -> a legacy save (no contract_miss line) loads to this safe default; kSaveVersion 2.
    int  missed_rounds = 0;
    // In works/customer the seat predefines car+livery (you don't choose); in privateer/pay_driver
    // you choose. team_car/team_livery are set by the team (filled in by the launch flow). "" = unassigned.
    std::string team_car, team_livery;
    // Exclusivity flag — holding an exclusive contract blocks signing another (a thin gate
    // at sign). Snapshot from the type's CoverageProfile.exclusive at sign. ADITIVE 'contract_excl' line,
    // write-when-true (default false -> a legacy save loads to the safe default; kSaveVersion stays 2).
    bool exclusive = false;
    // The calendar year this contract's insurance premium was last charged at rollover
    // (the per-contract premium-charged-year guard so a renewed contract isn't double-billed). ADITIVE
    // 'contract_prem' line, write-when-non-zero (default 0 -> never charged -> back-compat; kSaveVersion 2).
    int premium_year = 0;
};

// Per-opponent head-to-head record vs the player, keyed by the stable pool
// driver_id (the inverse of DriverById). w = races the player finished AHEAD of this driver; l = races the
// player finished BEHIND. Accumulated at the live Settle by sweeping the already-classified finishing grid
// (the player row + non-pool/generic AI are excluded). The 3 displayed rival cards are derived AFTERWARD from
// this store (so a rotating rival loses no history). Empty before any captured race -> the card reads "—"/0–0
// The nemesis is the worst-balance id once data exists. POD, no methods.
struct H2H { int w = 0, l = 0; };

// "beef" — narrative rivalry level with a specific driver_id. level grows when the
// inbox writer flags a fresh duel (last_year/last_race_idx snapshot the trigger for display, "since ..."); it
// decays by 1 for every rollover where the driver DIDN'T get a fresh duel (BeefDecayAtRollover, engine-pure,
// called from RolloverYearReport BEFORE cc.year++) and the entry is erased once level<=0 (a decayed-out rivalry
// leaves no trace). last_race_idx = -1 means no triggering race yet. POD, no methods.
struct Beef { int level = 0; int last_year = 0; int last_race_idx = -1; };

// "rel_marks" — small warm-ups/cool-downs recorded toward a specific
// team (team label), written by press responses (the tone chosen in an interview) and other email families
// that want to nudge a relationship one-off. Unlike Beef, it is not mutated at rollover: it decays on
// read (RelMarksSum, career_market.cpp) with a ×0.5 weight per year of age, discarded past 3 years.
// delta_x1000 = the real delta ×1000 (an integer; avoids %f churn in the save file). POD, no methods.
struct RelMark { std::string team; int year = 0; int delta_x1000 = 0; };

// C1 (spec ): clock stop rules. Persisted on the 'stoprules' line (career_save.cpp),
// write-when-non-default: a save with the defaults stays byte-identical and a downgrade loses nothing.
struct StopRules {
    bool offers = true;     // an offer / reply arrives
    bool myRaces = true;    // a race you are entered in lands (or is)
    bool anyRace = false;   // a race you could enter lands
    bool mail = false;      // a mail arrives
    bool relation = false;  // a relationship changes (new mark)
};
inline bool operator==(const StopRules& a, const StopRules& b) {
    return a.offers == b.offers && a.myRaces == b.myRaces && a.anyRace == b.anyRace
        && a.mail == b.mail && a.relation == b.relation;
}
inline bool operator!=(const StopRules& a, const StopRules& b) { return !(a == b); }

// Lightweight narrative memory for the email channel. Streak counters and
// season counters (for the closing balance) plus onboarding-arc progress. Additive:
// the 'mailnarr' line is write-only-when-non-default; absent in old saves -> defaults (back-compat).
struct MailNarrative {
    int onboard_stage = 0;        // 0=no intro 1=intro sent 2=first_race sent 3=arc complete
    int consec_podiums = 0;       // streak of consecutive podiums (settle to settle)
    int consec_bottom = 0;        // streak of consecutive bottom-third-or-DNF finishes
    std::string last_notable_track; // last podium/win circuit (callbacks {track})
    int season_podiums = 0;       // counters for the current season (used at close); reset on rollover
    int season_wins = 0;
    long season_start_credits = 0; // snapshot at season open -> the {net} figure at close
    int consec_clean = 0;         // streak of consecutive clean races (settle to settle)
};

// The active/offered substitution invitation. v1: only one
// at a time (SubRideWeeklyDraw refuses to draw another while state != 0 — "one substitution at a time", a
// global design constraint). state 0=no substitution (default; no line written) · 1=offered (email sent,
// waiting for Accept/Pass) · 2=accepted (the race gets assembled from these fields).
// event_absday = the absolute day (career::dateutil::AbsDayFromYearDoy) of the offered round — the actual race day
// itself, not the landing-minus-lead value that NextEventAbsDay uses. fee = the round's fixed rate (MK_SUB_FEE_BASE ×
// the series level), frozen when offered (it isn't recalculated if the level/series change later).
struct SubRide {
    std::string series_id;
    int  seat_idx     = -1;
    int  event_absday = 0;
    long fee          = 0;
    int  state        = 0;   // 0=none 1=offered 2=accepted
    // The label of the team whose seat you're covering, frozen when offered from the seat chosen by the
    // draw (SeatsForClass[seat_idx].team_label). Storing it here makes SubRideSettle self-contained: it can
    // record the warmth (a rel_mark toward the team) without recomputing the seat (which would depend on the live
    // overlay/save). 6th field of the 'subride' line; old saves (5 fields) load it empty (back-compat).
    std::string team_label;
};

// — "YOU'RE BEING WATCHED". A seat contest used to be resolved entirely within the same
// call that generated the offers: either you got it or you didn't, with no "almost" ever existing. That made
// two things impossible — seeing that a team has its eye on you, and having the seat slip away while
// you're weighing another option. This record is that live contest: it opens when you land close to the best in the cohort
// (within MK_SEAT_WATCH_BAND) and resolves a few races later using your data from back then, not from now.
//
// races_at_open freezes your races in the class when it opens, so the remaining count is derived (window minus the ones
// you've done since) instead of needing a hook on every race: a save loaded mid-way can't
// get out of sync. The day-based expiry exists because if you have no contract in that class you don't race in it, and the
// window would never count down: the team doesn't wait forever.
// A sealed candidate for a seat (the 'seatwatchc' line). Roster id + name for display.
struct SeatContender { std::string id, name; };

struct SeatWatch {
    std::string series_id;      // watched seat (series)
    std::string class_name;     // that series' class (for re-ranking at resolution)
    std::string team_label;     // the team watching you
    std::string rival_id;       // who you were competing against when it opened (sealed: the world moves on, the story doesn't)
    std::string rival_name;
    int opened_day    = 0;      // absolute day it opened
    int races_at_open = 0;      // your races (wherever you race) at that moment — see SeatWatchRacesNow
    int races_window  = 0;      // how many races the watch lasts
    int state         = 0;      // 0=watching · 1=you won (the seat is yours) · 2=you lost it
    int resolved_day  = 0;      // absolute day it closed (0 while still live)
    // (Jul 28) Your rival beating you once can't cost you the seat — anyone can have
    // a bad weekend. What costs it is losing every race while they're watching. That's why it tracks the streak, not
    // a running total: as soon as you beat them once, the team sees you back in it and the count resets.
    int races_seen         = 0; // window races already counted (so the same one isn't counted twice)
    int rival_ahead_streak = 0; // how many in a row they've been ahead; >= races_window -> the seat is theirs
    // (Jul 28) The contest is two phases: first who's fighting for it gets chosen, and only then is it decided
    // among them. Without sealing the list, every refresh re-ranked the entire cohort and could end up handing the
    // seat to someone who wasn't even there when it opened — which would make the first phase pointless.
    // Sealed when opened; while the watch is live, the seat is only contested between them and you.
    std::vector<SeatContender> contenders;
};

// Silly season window design: a WINDOW SEAT. When the window opens, only the KEY is persisted
// (series|team) plus its state and the day it entered; the details — conditions, requirements, rivals, notes —
// are still derived live. It's the same pattern already validated with the sealed shortlist: from it are sealed
// identities, and the rest is derived. It gives the stability of a snapshot without the snapshot, which would age badly when the
// world moves underneath (a team goes under, a driver retires), and it takes away from "persisting only the
// process" its one real hole: with the key sealed, a seat can't evaporate mid-dispute.
// `state` follows the states of the enum (see WindowSeatState). `applied` = you've applied yourself, which per the
// enum puts you LAST in the shortlist: the team already had its candidates and you're the one who offered. POD.
struct WindowSeat {
    std::string series_id;
    std::string team_label;
    int state       = 0;   // WindowSeatState
    int opened_day  = 0;   // absolute day the seat entered the window
    int applied     = 0;   // 1 = the player applied (enters last in the shortlist)
    std::string winner_id;    // who got it, once resolved (for the "and why" of decision week)
    std::string winner_name;
    // 1 = THE TEAM REACHED OUT TO YOU. The APPROACHED state says the team has talked to
    // someone, but not who, and that difference is half the screen: "they've been asking around" and "they've"
    // asked about you" aren't the same news. It's stored separately from state because state belongs to the WORLD
    // (anyone can see it) and this is yours. Additive at the end of the 'winseat' line: an old save reads it as 0.
    int sounded     = 0;
    // 23/09 (, "one driver, one seat"): 1 = the winner of this slot HAS ALREADY sat in it. The decision
    // week only noted the name and nobody ever seated them; now winter seats them (or the close of the
    // year-1 window) ONCE, and this mark is what prevents seating them again after the world has
    // moved them. Additive at the end of 'winseat' and only when it is 1: a save without seated slots stays identical.
    int seated      = 0;
    // Diagnostic: WHY you got rejected. It's deliberately NOT persisted -- it's the explanation of the
    // verdict for this round, not a fact of the world, and saving it would force a new format for something that
    // recalculates on its own. It exists because "109 rejections from crushing it vs. 104 from tanking it" says NOTHING without
    // knowing whether the bar or the table brought you down: they're two different failures and the count conflates them into one.
    int    reject_why  = 0;     // WindowSeatReject
    double fit_margin  = 0.0;   // player's fit MINUS the best of the cohort's (<0 = the table beats you)
};
// Reason for rejection (WindowSeat::reject_why). Diagnostic, not persisted.
enum WindowSeatReject {
    WSREJ_NONE  = 0,
    WSREJ_BAR   = 1,   // you don't meet the seat's rating requirement (reqRep)
    WSREJ_TABLE = 2,   // you clear the bar but the cohort wins the dispute
    WSREJ_BOTH  = 3,   // neither the bar nor the table
    WSREJ_RENEW = 4,   // your seat: you didn't meet the season's target
};
// States. RENEWED wasn't named in the spec but is in the UI design (PD_SEAT_STATUS.renewed):
// the driver stays one more year, so the seat exists in the paddock but isn't in play.
enum WindowSeatState {
    WSEAT_FREE      = 0,   // OPEN: the seat opens for next year, nobody has claimed it
    WSEAT_SOUNDED   = 1,   // APPROACHED: the team has talked to someone (you or someone else)
    WSEAT_DISPUTED  = 2,   // IN DISPUTE: you're in and being evaluated against a sealed shortlist
    WSEAT_OFFERED   = 3,   // OFFERED: decision week's verdict, it's yours if you sign it
    WSEAT_REJECTED  = 4,   // REJECTED: decision week's verdict, someone else got it
    WSEAT_RENEWED   = 5,   // RENEWED: the incumbent stays, the seat never came into play
};

// Full persisted state. Line format: "key value" + repeated blocks for owned/stats.
struct SaveData {
    CareerProgress prog;
    std::vector<CarOwnership> owned;
    std::map<std::string, ClassStats> class_stats;
    std::set<std::string> completed_races;   // ids of calendar races already run (actual gating)
    std::set<std::string> claimed_events;    // ids of special events whose reward
                                              // has already been granted (don't re-grant). Persisted/read as completed_races;
                                              // absent in old saves -> back-compat (LoadCareer ignores unknown keys).
    // 19/09 (tester: finished P9 and was paid ₡51,104; then won their class and was paid nothing, with the pre-race screen
    // promising ₡250,000). Josh's decision: THE DIFFERENCE IS PAID. Here goes what was ALREADY paid per event; a
    // better race pays max(0, new − paid) (EventClaimCredits). NEW and additive line `evpaid <₡> <id>`:
    // an old save loads with nothing paid and an earlier build ignores it.
    std::map<std::string, long> event_paid;
    std::set<std::string> unlocked_gates;    // championships opened by qualifier gate (series_id)
    int sponsor_tier = 0;   // mirror of RepTier(prog.rep) at save time (read directly by the UI)
    // deterministic seed for the CAREER, minted ONCE when the career is created (from the
    // player's identity; falling back to a stable non-zero hash of last+first name). Feeds the SCARCITY draw
    // via OfferGenInput. Additive on top of kSaveVersion=2 (do NOT re-bump): a legacy save without a 'seed' line loads as 0
    // -> shared-but-deterministic scarcity (accepted). Numeric, always written (no string risk).
    unsigned long career_seed = 0;
    PlayerIdentity player;  // player identity (empty in old saves -> back-compat)
    // (Aug 24): the player's last name AS SEEN IN SHARED MEMORY (the Steam/AMS2 nickname — the key used to
    // accumulate championship tables). If on a given launch shared memory brings a DIFFERENT name, racesig migrates the
    // profile's tables to the new name (merging) — without this, changing your nickname split the player into two
    // drivers with the points split between them. "" in old saves: stamped without migrating the first time.
    std::string shm_player_name;
    std::vector<Contract> contracts;   // multi-contract: 0..N active contracts at once (different series). Old single-line save -> vector of 1; no contract -> empty (back-compat)
    std::set<std::string> declined_offers;  // ids of rejected offers (don't re-offer; purged per season)
    // Negotiation redesign: live/closed negotiation threads ('nego'+'negoh' lines per
    // thread). Closed ones are kept until rollover (AdvanceWeek closes them, NOT rollover). Empty in
    // old saves -> back-compat (LoadCareer ignores unknown keys).
    std::vector<Negotiation> negotiations;
    // live/closed seat disputes ('seatwatch' line). Empty in old saves -> back-compat.
    std::vector<SeatWatch> seat_watches;
    // The SEALED KEYS of the signing window (line 'winseat', write-when-present).
    // Only the seat's identity and its state; everything else is still derived. Empty in old saves, which
    // is exactly "the window hasn't opened yet" — no special branches or old-save flag needed.
    std::vector<WindowSeat> window_seats;
    // The year whose window is sealed. 0 = none. Prevents re-sealing the same window on every call and
    // lets the year change reopen it on its own, without a separate "already did this" flag that could fall out of sync.
    int window_year = 0;
    // Negotiation redesign: ids of offers WITHDRAWN by the team mid-negotiation (stops being
    // available to re-offer). Same pattern as declined/expired/lapsed; purged per season (rollover).
    std::set<std::string> withdrawn_offers;
    // 18/09: NOT persisted. The live (non-held) offers generated by the last pulse, and the day of that pulse.
    // A sealed offer can leave the market without expiring (another driver takes the seat in the window), and the
    // «last day» notice must not count it. RunWorldTick fills it; if that day had no pulse (a test, a
    // freshly loaded game) the day does not match and OffersOnLastDay does not filter.
    std::set<std::string> live_offer_ids;
    int live_offers_day = 0;
    std::vector<Courting> courting;         // teams courting you (HELD offers seen). Persistent memory.
    std::set<std::string> expired_offers;   // ids of offers whose season already started (you let them pass). Mirror of declined_offers.
    std::map<std::string,int> offer_first_seen;  // offer_lifecycle: id ("of_"+series) -> absolute day (real Gregorian abs_day, NOT year*365; sanitized on load) it was first seen
    std::set<std::string> lapsed_offers;         // offer_lifecycle: ids expired by TTL (28 days). Suppressed from display/re-issue; NEUTRAL (not a snub). Purged per season.
    // rival_form — persistent skill delta ON TOP OF base_skill. LEGACY as of 07/02: the scheduled
    // growth that used to write it (AdvanceRivalSeason at rollover) was REMOVED (cast skill is
    // innate; fame is carried by ELO). Nothing writes it in production anymore, and the seed_epoch migration
    // clears it; it remains a dormant hook that ComposeSeasonGrid still reads in the
    // grid. Empty in old saves -> back-compat (LoadCareer ignores unknown keys).
    std::map<std::string, float> rival_form;
    // Per-AI-driver LIVE rating, keyed by the stable pool driver id. Seeded lazily
    // from career_fields order/iconic (RatingSeedFromOrder) via SeedDriverRatings; then evolves race-to-race
    // (aging pass). Empty in older saves -> back-compat (only ids present are written; unknown ids re-seed on
    // demand). The player's own rating lives on prog.rating, NOT here.
    std::map<std::string, double> driver_ratings;
    // Per-AI-driver races-completed counter, keyed by the same stable pool
    // driver id as driver_ratings. Mirror of the player's prog.rating_races for the whole grid: it drives the
    // DYNAMIC K (RatingK -> bigger early swings, smaller once settled) and is available to the market to
    // discount unproven rating spikes. Bumped per settled race by SettleRatingGrid. Empty in older/pre-this
    // saves -> back-compat (only ids with n>0 are written; the field defaults to an empty map, old saves load
    // clean with NO migration step). NOT a stored Glicko deviation (full Glicko spec is YAGNI).
    std::map<std::string, int> driver_races;
    // (12/08) — WHERE EACH TEAM FINISHED LAST YEAR. Key "<series_id>|<team_label>" (see
    // `TeamRankKey`), value = its position in the TEAMS championship, 1-based. Written by `RunWorldSeason` and
    // read by  (what a team buys: fights for the title / midfield / rebuilds / survives) and  (the
    // budget band).
    //
    // It is PERSISTED instead of recomputed because «last year» can no longer be reproduced: the season is
    // deterministic by (seed, series, year, round), but it was run with THE OCCUPANTS OF THE TIME, and the
    // market has already moved them. Only the last year: each rollover overwrites it.
    //
    // Empty in earlier saves -> `StanceFor` returns `Media`, the neutral, and nothing changes in behavior
    // (additive back-compat, no migration step).
    std::map<std::string, int> team_last_rank;
    // (12/08) — EACH ONE'S PROGRESS TOWARD THEIR CEILING, 0..1. It is the RISE half of the
    // age curve, made personal: the archetype sets the shape, the results bend it, and `BaseTalent` —the ceiling—
    // is never touched. `AdvanceGrowth` moves it once per season.
    //
    // ABSENT = the universal rise at their age (`AgeRisePart`), so a save from before this gives
    // EXACTLY the same number as before: additive back-compat with no migration step.
    std::map<std::string, double> driver_curve;
    // .5 (10/08): 4 = the cohort was re-seeded ANCHORED to its home tier (before 1150..2250 without
    // a level term). An rk_* that has already raced carries a rating evolved from the old seed and the ELO
    // does not drop 700 points over seasons, so the migration deletes it and `SeedAllDrivers` re-seeds it.
    // Seeding epoch applied to the save. 0 = before CastSeedFromBase; 3 = cast rk_*
    // reseeded with the "worst generation" hierarchy (3 legends + the rest distributed) and rival_form
    // cleared (growth retired). PregenAllAi does the one-shot migration (wipes the rk_* ratings even if
    // they've raced: they evolved from a buried seed) and bumps the marker. Additive: only written
    // if != 0 -> earlier saves load clean with 0.
    int seed_epoch = 0;
    // FOOTPRINT OF THE SEATS THE GRID XML FILES WERE WRITTEN WITH.
    //
    // AMS2 reads CustomAIDrivers AT STARTUP and doesn't hot-reload it. And the app lives INSIDE the game (the
    // engine is what serves the API), so while you're playing, AMS2 is always open: when the
    // signing window moves seats, the XML files on disk go stale and there's no way to apply them without
    // restarting the game. Without this, the only option was to ALWAYS warn -- which is what the
    // rollover screen did, unconditionally -- and always warning is the same as never warning.
    //
    // The FOOTPRINT is stored instead of a "restart needed" boolean on purpose: a boolean is a fact that
    // someone has to remember to set and clear, and it falls out of sync the moment one path skips it.
    // The footprint is COMPARED against the current world, so it can't lie. Additive: an old save
    // reads it as 0 and comes out "stale", which is the safe side (not warning is worse than warning once too often).
    unsigned long grids_stamp = 0;
    // the fingerprint of the OWNER of each class with which those XMLs were written. It goes apart
    // from `grids_stamp` because it is evaluated differently: the plan is only known to whoever has the calendar in front,
    // so `ClassDeploymentStamp()` returns 0 = «I don't know» while no preference is set, and
    // `GridsAreStale` cannot assert a change it cannot see. Additive; an old save reads it as 0.
    unsigned long grids_owner_stamp = 0;
    // 0.7.0: the championship tables have already gone through the repair of the skipped multiclass appointments
    // (career_calendar.h::RepairSkippedMeetingRounds). 1 = done, not looked at again. Additive: an old save
    // reads it as 0 and is repaired once when pregenerating.
    int meetfix = 0;
    // 17/09: the seats the game came with EMPTY have already been recovered (career_seats.h::RecoverEmptySeats).
    // 1 = done, not looked at again. Additive: an old save reads it as 0 and is recovered once when pregenerating, and from
    // then on an open slot goes back to meaning what it means — a market vacancy.
    int seatfill = 0;
    // 17/09 — THE LAST WINTER'S REPORT, SAVED. The end-of-season screen showed three lists
    // handwritten in the code (`RolloverUiPreview`): the same two promotions and the same two retirements in
    // any game and in any year. The real report already existed —`RolloverYearReport` returns the real
    // `retirements` and `promotions`— but it was thrown away when winter ended, because the screen is painted
    // AFTER and nobody saved it.
    //
    // A SAMPLE of each list is saved plus its TOTAL. The total is what lets you say «116 promotions» without
    // writing a hundred and sixteen lines in the game; the sample is what is shown. Additive: an old save
    // carries no line, and then the screen invents nothing — it stays empty until the first winter.
    struct RolloverSaved {
        struct Retire { std::string name; int age = 0; std::string reason; };  // reason: Retires|Age|Left|Level
        struct Promo  { std::string name; int from_tier = 0, to_tier = 0; };
        // 18/09: whoever CHANGED TEAM in winter (neither retired nor promoted). from/to = "team · series".
        struct Move   { std::string name, from, to; };
        int year = 0;                                     // the year that closed; 0 = no winter has happened yet
        int total_retires = 0, total_promos = 0, total_moves = 0;
        std::vector<Retire> retires;
        std::vector<Promo>  promos;
        std::vector<Move>   moves;
    };
    RolloverSaved last_rollover;
    // nemesis_id — STABLE driver_id (not name: tribute renames would change and break the key) of the
    // hottest driver in the cast closest to your final position, fixed at season close. Empty in old saves ->
    // back-compat (the line is only written if it isn't empty).
    std::string nemesis_id;
    // career record — archived CLOSED seasons (one per entry, written at year rollover). Grows without
    // limit (it's the career's history). Empty in old saves -> back-compat (LoadCareer ignores unknown
    // keys, and we don't write lines when it's empty).
    std::vector<CompletedSeason> completed_seasons;
    // signed sponsors (the progression axis). Each carries its running coverage-cap usage. Empty in
    // old saves -> back-compat (no lines are written when empty; the parser ignores unknown keys).
    std::vector<SignedSponsor> signed_sponsors;
    // sponsor cards (empty in old saves -> back-compat)
    std::vector<SignedCard> cards;      // sponsor cards; empty in old saves
    CardShopState card_shop;            // pending shop (persists if you save without closing it)
    // number of cards already "seen" in the Sponsors tab (cmd.cards.seen sets it equal to cards.size()).
    // The tab's badge = (cards.size() - cards_seen) unseen + ready upgrades. 0 in old saves.
    int cards_seen = 0;
    // the privateer's "marriage" to their team — car+skin locked in the first time they race a series,
    // re-applied season after season (the SeasonState latch dies at rollover: enrollments.clear()).
    // series_id -> {veh_hash, livery_ord}. Signing a NEW contract in the series clears it (new identity).
    std::map<std::string, std::pair<int, int>> privateer_pick;
    // state of the aging WORLD (additive; empty in saves <v2 -> back-compat).
    // birth_year by driver_id (only the ones that differ from the roster/JSON data, or a snapshot of a new-world driver).
    std::map<std::string, int> birth_year;
    std::set<std::string> retired;        // driver_ids already retired (they don't return to the grid)
    // 23/09 — who has the ELO that THEIR SEAT gave them (D4, «you are worth where you sit», and the rung of the rookie
    // who is born) and has not raced yet. Boot re-seeds everyone who has never raced; not these: the number
    // is kept until they race (ReseedUnracedForBoot). «seatseed <id>» line, additive.
    std::set<std::string> seat_seeded;
    int current_world_year = 0;           // world year for the age curve (0 = default to cc.year)
    // legacy/era-memory progress (memory_id -> {cleared count, last played year}). Drives the
    // first-clear-vs-replay reward split (MemoryPayout/MemoryGrantsContent) and the once-per-year replay
    // throttle. Empty in old saves -> back-compat (LoadCareer ignores unknown keys).
    std::map<std::string, MemoryProgress> memories_done;
    // "Standings" tab: log of race TIMES (best lap / total time / result per race).
    // Appended at settle time, capped to kRaceLogMax (most recent). Empty in old saves -> back-compat (additive 'racelog').
    std::vector<RaceLogEntry> race_log;
    // GENERATED emails, persisted (OFFER + PROGRESS/MILESTONE). The email generator
    // emits them on the 3 deterministic pulses and dedupes them by content-derived id against THIS vector before emitting.
    // read/answered live here (they moved out of screen_inbox.cpp's statics). Only id+keys+payload
    // key are persisted (NEVER the resolved EN/ES body, bilingual-by-translation). Additive 'email' lines (write-only-when-present) ->
    // a save with no generated emails stays byte-identical (back-compat). Rides on kSaveVersion=2 (do NOT re-bump).
    // Retention (housekeeping, NOT the offer cap): the oldest READ+non-actionable item is pruned at generation time.
    std::vector<GeneratedEmail> emails;
    // Living emails: narrative memory of the mail channel (streaks + season counters +
    // rookie-arc progress). ADDITIVE 'mailnarr' line, write-only-when-non-default; an old save without
    // that line loads the struct's defaults (back-compat).
    MailNarrative mailnarr;
    // PENDING RENEWAL offers generated at year rollover. One market::OfferDef per
    // renewed contract (archetype = ARCH_RENEWAL), keyed by its series_id (OfferDef.id == "of_"+series_id), terms
    // shifted by obj_ratio (obj_done/obj_total) + rel. The rollover screen reads this (matched by series_id)
    // for its "your contract" summary line; "New objectives" feeds the same OfferDef into the Accept/Negotiate
    // path at negotiationLevel 2. ADDITIVE (write-when-non-empty 'renewal' lines, pipe-delimited): a save with no
    // pending renewals stays byte-identical (back-compat); kSaveVersion stays 2 (MigrateSave no-op).
    std::vector<market::OfferDef> pending_renewals;
    // Seat occupancy — team-seat key -> occupant driver_id. Keyed by a stable
    // "<series_id>#<seat_idx>" (or the seat model's own key); persists which driver holds which seat across
    // rollovers so the living-seat market knows what is open vs occupied. ADDITIVE 'seatocc' lines
    // (write-when-present): a save with no seat state stays byte-identical (back-compat); kSaveVersion 2.
    std::map<std::string, std::string> seat_occupant;
    // year the current occupant ENTERED the seat, with the same key as
    // seat_occupant. Only DEVIATIONS are written: a seat missing from this map has been
    // occupied since the career started. EXPIRY isn't stored -- it's derived from (seat,
    // occupant, start year) so it can never drift out of sync with the seat. See career_seats.h.
    std::map<std::string, int> seat_contract_start;

    // ~~~29/08 — CONSECUTIVE SEASONS WITHOUT A SEAT (user's idea). Whoever loses their slot and finds no
    // other does NOT leave the sport that same night: they stay available for a couple of winters and only then
    // retire. Measured before writing it: 19 people every five seasons left because there was not, THAT
    // winter, a gap that fit them -- and the world has 1,549 slots of real people waiting outside.
    //
    // And it serves the other thing the user asked for: whoever has just become free is MORE likely to
    // chain a contract than someone who has been out for years, so the counter orders the queue.
    // Absent = 0 = «has a seat or has just lost it». An old save reads it as 0 and nothing changes.
    std::map<std::string, int> seasons_idle;
    // ...and WHERE they came from, which is what allows them to return: the series gives their level and their
    // discipline, so a GT driver does not reappear in a junior single-seater.
    std::map<std::string, std::string> idle_from_series;
    // mark that the seat keys of this save are already PER SLOT
    // ("<series>|<veh>|<livery>") and not by index ("<series>#<n>"). 0 = save from before the migration, or a
    // new game with no persisted deviation — in both cases the migration is a no-op, so 0 does not
    // mean "pending" but "not needed". ADDITIVE ('seatepoch' write-when-non-zero): a save that does not
    // carry it loads identical. kSaveVersion is NOT bumped: nothing in the format changes, only the content of a
    // key, and the old keys are still read.
    // 0.7.0 port (F12): since then 0 DOES mean something for the contract and the substitution: its seat_idx is
    // from 0.6.8, and MigrateSeatKeys translates it once and leaves 1. Whoever fabricates a save with today's indices without
    // going through the load (DBGSAVE presets, careerreset) has to leave 1.
    int seat_key_epoch = 0;
    // which of a driver's seats is the PRIMARY one. It is only written for
    // whoever has TWO OR MORE —with just one, deriving it is trivial and saving it would be state that ages— and only
    // when it was not already fixed: once decided, the save rules, because the driver's career arc
    // hangs on "their category" and cannot change because a data point is recalibrated.
    // Measured in year 1: 192 AI drivers occupy two or more seats and none had a primary.
    // ADDITIVE ('primaryseat' write-when-present): a save without this line loads identical. kSaveVersion intact.
    std::map<std::string, std::string> primary_seat_by_driver;
    // GENERATED DRIVERS. The rookies the cascade injects at the bottom every season so
    // the world doesn't dry up: without them, ~15 leave per season and nobody comes in (measured: 940 -> 862
    // occupants over ten years). This DOES get persisted, and it's the exception to the year-entered rule above: a
    // made-up driver can't be derived from any table -- it doesn't exist anywhere else. The ones who
    // no longer race or hold a seat get pruned, so the map grows with the living world, not with the clock.
    // ADDITIVE ('gendrv' write-when-present): a game with no generated rookies stays byte-identical.
    // Birth year does NOT go here: it goes to save.birth_year, which is the canonical map already read by the
    // whole engine (ActiveModernSet, the age curve). Storing it twice would risk it drifting out of sync.
    struct GenDriver {
        std::string name, country;
        double talent = 0.0;   // on the anchored scale (career_seats.h::BaseTalent)
        int    tier_max = 1;   // their CEILING: how far they can go. Distributed by a power law.
    };
    std::map<std::string, GenDriver> gen_drivers;
    // the LAST circuit raced (track hash), the "previous leg" source for the A->B travel
    // cost. The season's first leg falls back to player nationality (no previous hash). ADDITIVE
    // 'lastcircuit' line, write-when-non-zero (default 0 -> first leg / back-compat); kSaveVersion stays 2.
    unsigned long last_circuit_hash = 0;
    // Head-to-head store — driver_id -> {w,l} vs the player, accumulated at
    // Settle and persisted ADDITIVELY as 'h2h <driver_id> <w> <l>' lines (write-when-present, grouped-by-id on
    // read; mirrors the sponsor_obj / contract_arch idiom). A save with no captured h2h stays byte-identical
    // (back-compat — only ids with w|l>0 are written; an old save loads to an empty map). kSaveVersion stays 2
    // (MigrateSave no-op). The accumulation and read/write wiring live alongside it.
    std::map<std::string, H2H> h2h;
    // Narrative rivalry store — driver_id -> Beef, decayed at year rollover
    // (BeefDecayAtRollover, engine-pure) and persisted ADDITIVELY as 'beef <driver_id> <level> <last_year>
    // <last_race_idx>' lines (write-when level>0; mirrors the h2h idiom). A save with no beef stays byte-identical
    // (back-compat — an old save loads to an empty map). kSaveVersion stays 2 (MigrateSave no-op).
    std::map<std::string, Beef> beef;
    // Relationship-mark history — team label -> list of {year,
    // delta_x1000}. Persisted ADDITIVELY as 'relmark <year>|<delta_x1000>|<team>' lines (pipe-delimited; team goes
    // LAST because it can contain spaces, same idiom as 'mailnarr'/'season'; h2h's %s doesn't work here).
    // Feeds career::RelMarksSum -> folded into market::TeamRelationship. Empty in old saves -> back-compat
    // (LoadCareer ignores unknown keys). kSaveVersion stays 4 (MigrateSave no-op).
    std::vector<RelMark> rel_marks;
    StopRules stop_rules;   // C1: 'stoprules' line (write-when-non-default)
    // One-off substitution — the active/offered invitation (see struct SubRide
    // above). Persisted ADDITIVELY as a 'subride <series_id>|<seat_idx>|<event_absday>|<fee>|<state>|
    // <team_label>' line (write-only-when state != 0; team_label is the 6th field, added later, tolerant of 5-field saves).
    // A save with no active substitution stays byte-identical (back-compat). kSaveVersion doesn't change (additive, MigrateSave no-op).
    SubRide sub_ride;
};

// Multi-contract: access helpers (you can hold several contracts at once, one per series) ---
inline bool HasAnyContract(const SaveData& s){ for (const auto& c : s.contracts) if (c.active) return true; return false; }
inline int  ActiveContractCount(const SaveData& s){ int n=0; for (const auto& c : s.contracts) if (c.active) n++; return n; }
inline const Contract* ContractForSeries(const SaveData& s, const std::string& sid){
    if (sid.empty()) return nullptr; for (const auto& c : s.contracts) if (c.active && c.series_id==sid) return &c; return nullptr; }
inline Contract* ContractForSeriesMut(SaveData& s, const std::string& sid){
    if (sid.empty()) return nullptr; for (auto& c : s.contracts) if (c.active && c.series_id==sid) return &c; return nullptr; }

// SCHEMA version of career.sav. Written on the "version <N>" line and read by LoadCareer.
// The design is ADDITIVE (new keys are only written if they have content; unknown keys are ignored when
// reading), so a save with a LOWER version (or no version line -> treated as 0) loads clean. This constant
// is the anchor for the migration seam: bump it when a schema change is NOT purely additive, and add the
// corresponding step in MigrateSave. Today (v1) everything is additive -> MigrateSave is a no-op.
constexpr int kSaveVersion = 5;   // + per-driver rating (rating/rating_races/driverrating). v1->v2 seeds rating from REP.
                                  // v4->v5 RETIRES kInhWeather from inherit_mask. Weather had TWO controls for the
                                  //     same thing (the Inherited-from row and wx_mode), and the bit silently overrode the mode. The
                                  //     migration translates the bit to wx_mode=1 and clears it (NOT additive -> its own step).
                                  // + contract_ext / courting_ext additive lines (write-only-when-present); rides on v2
                                  //     (purely additive: absent in old saves -> struct defaults are correct, no migration).
                                  // + signed_sponsors ('sponsor' lines, write-only-when-present); also rides on v2
                                  //     (purely additive -> a save with no sponsors stays byte-identical; no migration step needed).
                                  // + birth_year/retired/current_world_year ('birthyear'/'retired'/'worldyear' lines,
                                  //     write-only-when-present); also rides on v2 (purely additive -> a save with no aged
                                  //     world stays byte-identical; the v1->v2 MigrateSave step is a no-op for these fields).
                                  // + memories_done ('memory' lines, write-only-when cleared>0); also rides on v2 (purely
                                  //     additive -> a save with no era memories stays byte-identical; no migration step needed).
                                  // + emails ('email' lines, write-only-when-present, pipe-delimited); also rides on v2
                                  //     (purely additive -> a save with no generated mails stays byte-identical; MigrateSave no-op).
                                  // + h2h ('h2h <id> <w> <l>' lines, write-when-present) + a trailing
                                  //     rating field on the 'racelog' line (a legacy 10-field line -> rating 0). Both ride on v2
                                  //     (purely additive -> a save with no h2h/rating stays back-compat; MigrateSave no-op).
                                  // v3: signed_sponsors are retired (the CARDS system replaces them);
                                  //     each sponsor's acquisition cost is refunded to prog.credits; save v2 -> v3 migration
                                  //     clears signed_sponsors (refunding SPONSOR_ACQ_COST[tier] per sponsor, tier clamped 0..4).
                                  // v4: FREEZES the signed salary. The re-derive of per_race
                                  //     to the variable model (SalaryFor) in LoadCareer becomes ONE-SHOT LEGACY: it only runs for
                                  //     saves with fileVersion < 4 (migrates the old flat figure once); a v4 save does NOT re-derive
                                  //     -> per_race stays frozen at the value set AT SIGNING (OfferTermsCtx). Without this, the auto-heal
                                  //     ran on every load with inputs that DIFFER from the offer (live worldYear/rating), and the
                                  //     figure would DRIFT after reloading. NOT additive (it changes the semantics of the re-derive) -> version bump.

bool SaveCareer(const SaveData& d, const char* path);   // atomic write (temp + rename)
std::string SerializeCareer(const SaveData& d);        // the SAME bytes as SaveCareer, in memory (clock seal)
bool LoadCareer(SaveData& d, const char* path);          // false if it doesn't exist or is corrupt
// Decays save.beef by 1 for every entry whose last_year < fromYear (a year without a
// fresh duel), erasing entries that land at level<=0. Pure save-state mutation (no calendar/prog dependency) ->
// lives beside SaveData in career_save.cpp; RolloverYearReport calls it with fromYear = cc.year BEFORE cc.year++.
void BeefDecayAtRollover(SaveData& d, int fromYear);
// Purge rel_marks entries whose age exceeds 3 (they weigh 0 in RelMarksSum anyway).
// Pure save-state mutation; called from RolloverYearReport with fromYear = cc.year BEFORE cc.year++.
void RelMarksPurge(SaveData& d, int fromYear);
bool OwnsClassId(const SaveData& d, const std::string& classId);  // true if they own a car with that EXACT class_id (car id)
std::string ClassPrefix(const std::string& id);              // part before '#' (class); or the whole id if there's no '#'
bool OwnsCarOfClass(const SaveData& d, const std::string& className);  // do they own a car in this class? (by prefix)
std::set<std::string> OwnedClassSet(const SaveData& d);  // set of class PREFIXES for owned cars (calendar gates)
extern const char* const kStarterMemoryClass;   // "Falcon_Masters" — gift from the 3rd race (CINDER CHALLENGE 400)
extern const char* const kStarterMemoryCarId;   // "Falcon_Masters#759059246" (car id of the gift; purchase format class#veh)
bool GrantStarterMemoryIfDue(SaveData& save);    // 3rd race + not claimed -> grants the car for free; true if it grants it NOW
bool StarterGiftPending(const SaveData& s);     // granted but the Memories popup hasn't been seen yet
void AckStarterGift(SaveData& s);               // marks the popup as seen (idempotent)
// 19/09 (tester: P9 was paid ₡51,104 and winning afterwards paid nothing). What an event pays NOW, with `earned` =
// what this result is worth: the DIFFERENCE with what was already paid at that event, never negative. It notes the new
// maximum in save.event_paid, so repeating an equal or worse result pays 0. PURE over the save.
long EventClaimCredits(SaveData& save, const std::string& eventId, long earned);
// forward-migration hook. LoadCareer calls it AT THE END, with the version READ from the file
// (fromVersion; 0 = old save with no version line). v1->v2 migrates 'calibrated': a new field defaulting to false,
// but v1 saves with existing progress must end up calibrated=true so onboarding doesn't re-gate. The seam
// exists so future NON-additive changes have a single place to live. Returns true if it left the save at kSaveVersion.
bool MigrateSave(SaveData& d, int fromVersion);
// 0.7.0 («Progression / career speed» thread): the career PACE (prog.reward_mode) is chosen at creation, but
// it can now be changed from Settings. It lives in TWO places: career.sav and the "difficulty" of the profile's
// profile.json, which on load RULES over the save (the overlay seeds it after each LoadCareer). That is why changing it
// only in the save would last until the next startup; profile.json has to be rewritten too.
// ProfileRewardModeRead: 0/1/2 if profile.json exists, parses and carries a valid token; -1 in any other case
// (and then the save keeps its own, same criterion as before).
int  ProfileRewardModeRead(const std::string& profileJsonPath);
// ProfileRewardModeSync: leaves profile.json in agreement with `rewardMode` PRESERVING the rest of the fields (name,
// mode and whatever else comes). Atomic write (.tmp + ReplaceFileAtomic). true if at the end the file can no longer
// contradict the save: rewritten, it already matched, or it does not exist/does not parse (then it seeds nothing). false ONLY if
// there was a readable profile.json with another mode and it could not be rewritten: the caller must NOT touch the save.
bool ProfileRewardModeSync(const std::string& profileJsonPath, int rewardMode);
} // namespace career
