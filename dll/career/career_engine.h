#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include "career_balance.h"   // bal::COST_REF_LAPS is the default for Settle's raceLaps parameter

namespace career { struct SaveData; }   // fwd (): seeding by primary seat takes it by
                                        // pointer; the full type is only needed in the .cpp
// Public engine -- NO ImGui, NO game dependencies. Pure logic, testable standalone.

namespace career {

// real race wear (defined in career_econ.h, which in turn uses the SettleReport type from HERE -> the
// include would be circular). Settle only receives it by POINTER, so a forward declaration is enough: the .cpp files that pass
// real telemetry already include career_econ.h.
struct WearTelemetry;

// bits of prog.inherit_mask (Auto/Inherited, per field). Each bit groups the game session settings that
// stop being written when the player chooses INHERITED for that field (see CareerApplyEvent). The order is the
// contract with the app (cmd.settings.setInherit tokens); ALWAYS append new ones at the end.
enum : int {
    kInhWeather  = 1 << 0,   // RETIRED: weather is now inherited via wx_mode=1 (Settings > Weather), not this bit.
                             // The value is kept so the others don't shift, and so the old save-format migration
                             // can still read it from an old save. Nothing else reads it anymore.
    kInhHour     = 1 << 1,   // start time
    kInhTimeProg = 1 << 2,   // time progression
    kInhStart    = 1 << 3,   // start type (plus its lock)
    kInhPit      = 1 << 4,   // pit rules (plus the pit-lane speed limit)
    kInhAi       = 1 << 5,   // AI difficulty
    kInhSessions = 1 << 6,   // practice/qualifying: duration and on/off
};

// Block A, evolved by the per-class DDA rework: the SLOW loop's state
// per class. `gap` is the class's rolling window of residuals, from which the median that decides whether the
// category's number goes up or down is drawn (career::FeelAiStep/ClassAiFor).
struct ClassDdaState {
    // Per-class DDA rework: this category's ABSOLUTE difficulty number (domain kAiMin..kAiMax), not an
    // offset. The first revision stored a delta over a global base here; the layered model was retired
    // because the base and the delta fought each other. 0 = no number yet (a category with no races, or a pending migration).
    int   ai = 0;
    // Races run in this category. Decides whether the number comes from the COMMUNITY (0) or the player (>0).
    int   races = 0;
    std::vector<float> gap;
    // Block D: the last post-race feedback-form answer in THIS class (1..5; 0 = none or
    // skipped), and the streak of identical consecutive answers. These only decide whether the feel rating is already
    // CONFIRMED before touching `ai`; the objective loop above ignores them otherwise.
    int   last_feel   = 0;
    int   feel_streak = 0;
    // DDA lock: the player has said "this category is right where I want it". With this
    // set, NONE of the four paths move `ai`. Everything else keeps working: races are counted, pace samples
    // are still saved, and feel is still logged -- unlocking it lets the loop start with a history already in place.
    bool  locked = false;
    // ~~~09/09 (, 3): DRY AND WET SEPARATE. The second number and the second window of this category,
    //  only for races with MEASURED rain (rain_max > 0 at settlement). Same loop, same domain; 0 = no
    //  wet number yet (the first wet race runs at the dry one and is born from it). They do NOT vote in the general.
    int   ai_wet = 0;
    int   races_wet = 0;
    std::vector<float> gap_wet;
};

// The player's persistent progress (no ImGui). Embedded in CareerState.
struct CareerProgress {
    long credits = 0;
    long xp_total = 0;
    long xp_toward_next = 0;
    int  frontier_phase = 1;
    int  rep = 0;
    int  titles = 0;       // Number of championships WON (at season close). Incremented by FinishSeason;
                           // the gating currency for the market/sponsors. Persisted additively.
    // REWARD mode. NOTE: this is NOT difficulty_mode (the on-track AI, right below) -- they're two separate
    // different. Chosen AT PROFILE CREATION (the app writes it into profile.json and the engine seeds it here on
    // load) and since 0.7.0 it is changed in Settings (cmd.settings.setReward rewrites both places).
    // Scales what you EARN (income, REP, XP); the world rating is NOT touched. ---
    int  reward_mode = 1;         // 0=Easy 1=Normal 2=Hard (bal::REWARD_MULT)
    // Difficulty. difficulty_mode: career::DifficultyMode (4 = adaptive, the default). ---
    int  difficulty_mode = 4;     // 0=Easy 1=Medium 2=Hard 3=Extreme 4=Adaptive 5=Manually fixed
    // DM_CUSTOM: the FIXED difficulty picked by hand (AMS2's slider domain, 70..120). It's always saved,
    // even with a different mode active, so coming back to this card restores the number the player left it at.
    int  custom_difficulty = 90;  // career::dda::kCustomDefault
    int  ai_difficulty   = 80;    // the global value the DDA has learned (adaptive mode only); domain 70..120
    int  adaptive_trim   = 0;     // the adaptive mode's manual trim [-5..+5]
    // The DDA lock's master switch. `locked(class) = lock_all || class_dda[class].locked`, so clearing it
    // restores each individual lock exactly as it was. It also reaches categories that don't exist yet:
    // with this set, a brand-new category is born locked to the overall setting and stays there.
    bool lock_all = false;
    // Block A: the loop's per-CLASS layer. Empty = everything at zero (old saves and new profiles).
    std::map<std::string, ClassDdaState> class_dda;
    // AI aggression as chosen in Settings. 0 = AUTO (scales by phase, HandoffAiForPhase);
    // >0 = a fixed user override (40/60/80/100 = Low/Medium/High/Maximum, domain 0..200).
    // The DEFAULT moves from 0 (Auto) to an EXPLICIT 60 (Medium) -- the same effective value the
    // Auto setting already wrote anyway, but now the setting SAYS so instead of hiding behind "Auto".
    // Only affects new profiles and saves without the key: a save with `ai_aggression` explicitly set to 0 stays in Auto.
    int  ai_aggression   = 60;
    bool calibrated      = false; // has the measurement race been run yet? (an onboarding gate)
    // per-field Auto/Inherited bitmask (the kInh* bits). Bit set to 1 = INHERITED: the mod does NOT write those
    // settings when launching calendar races, and whatever the player has set in their own Custom Race config is left standing.
    // 0 (default) = everything AUTO = the usual behavior. Only applies to launches that honor inheritance
    // (calendar races); calibration/testing/special events/debug still always force their own settings.
    int inherit_mask = 0;
    // Settings toggles for the automatic scaling of gameplay multipliers (ON by default).
    // FCY/safety car in races. 0=No (force off) - 1=Yes (force on, the default --
    // the player's choice) - 2=Inherited (don't touch it: whatever the player has in their Custom Race stands,
    // same philosophy as inherit_mask). Save: the additive key `fcy_mode`; `fcy_enabled` is still written
    // (0/1) so an old DLL still reads something coherent. Migrated in LoadCareer: fcy_enabled -> 0/1.
    int  fcy_mode = 1;
    bool scale_tyre_wear  = true; // the mod scales tire Wear by race length
    bool scale_fuel_usage = true; // the mod scales fuel Consumption by race length
    // Block D: EXPLICIT telemetry consent, off by default. With it off, the
    // engine still assembles the payload (it's just a read of the last settle, costs nothing) but the app does NOT
    // send it anywhere. The post-race feedback form works the same without consent: its effect is purely
    // local. A toggle in Settings.
    bool telemetry_opt_in = false;
    // Block D: has the player already been ASKED? Consent is requested ONCE, the first
    // time the Play button appears, with details of what's sent and why. Any explicit
    // answer (yes or no) sets this to true and it's never asked again: the player can change their mind in Settings.
    // Keeping this separate from telemetry_opt_in is what stops the app from pestering someone who already said no.
    bool telemetry_asked = false;
    // Block C -- WEATHER. wx_mode: 0 random (default) - 1 inherited from your Custom Race (the mod
    // does NOT write the weather slots) - 2 rotates through YOUR own selection. wx_weights is only read in mode 2: a weight per
    // catalog weather state (AMS2 code -> weight); a state that's absent or at 0 is BANNED. Empty = factory-default weights.
    // An event's AUTHORED weather overrides all three modes: it's part of the event's design.
    int wx_mode = 0;
    std::map<int, int> wx_weights;
    std::vector<float> recent_gap;   // rolling window of the last gap_norm values (max kWindow=3) -> feeds the DDA's median
    // CAREER form -- a rolling window of the player's finishing QUALITY across ALL series (not
    // per season). Same orientation as PlayerForm (1.0 = win, 0.0 = last place). Capped at N=10 (the
    // oldest sample is dropped). Empty on old saves -> CareerForm returns 0.7 (back-compat). SettleRound
    // pushes one sample per settled race; PlayerForm uses it to CALIBRATE a series with a short history
    // (a returning rival enters at your REAL level, not at 0.7). REP NEVER factors in here.
    std::vector<float> recent_form;
    // Living World: the player's LIVE unified rating (ELO=REP). One number; REP tier and ELO band
    // are two VIEWS of it (RatingRepTier / RatingDisplayBand). Default = bal::RATING_ROOKIE_BASE for a fresh
    // career (1150). 0.0 in an old save -> LoadCareer's migration seeds it from the old REP.
    double rating = 0.0;
    // Provisional-window race counter feeding RatingK (high K while < K_PROVISIONAL_RACES). Distinct from
    // class_stats races (those are per-class); this is the player's GLOBAL settle count for K. 0 in old saves.
    int rating_races = 0;
    // The player's 0..100 cleanliness EMA. Sourced from the REAL $pcars2$ incident
    // latch (r.clean) -- NOT a finish proxy. Updated each race in Settle by clean += alpha*(100*cleanRace - clean).
    // back-compat default 0.0; on first use Settle SEEDS it to bal::CLEAN_ROOKIE_SEED (70) before the first EMA
    // step (a fresh/old save behaves as a rookie). Feeds teamInterest (soft penalty below CLEAN_TEAMINTEREST_FLOOR,
    // live) + the gray-zone accident trial trigger (ComingFromAccidents). Persisted additively.
    double cleanliness = 0.0;
    // REAL result-objective counters for sponsor thematic objectives,
    // replacing the prototype ObjProgress stand-ins (top8->podiums, endur->0). Bumped at Settle by slice
    // (top8 when pos<=8 && finished; endurance when an endurance event finishes). Additive on
    // CareerProgress with back-compat default 0 (old saves -> 0, behaves as a rookie with no progress yet).
    int top8 = 0;
    int endurance = 0;
};
// The result of one race (filled in from the $pcars2$ shared memory).
struct RaceResult {
    int  pos = 1;          // player's final position
    int  field = 1;        // number of participants
    bool finished = true;  // false = DNF/retired
    bool clean = false;    // no contact/penalty
    int  race_phase = 1;   // phase of the class raced
    int  ai_level = 0;     // race's AI % (0 = unknown -> REP isn't scaled)
};
// The settlement breakdown (shown on the results screen).
struct SettleReport {
    int  pos = 0, field = 0;                         // copy of the result (for the UI)
    long prize = 0, clean_bonus = 0;                 // income (`sponsor`, the flat retainer by
                                                     // REP band from the old v1 system, RETIRED -- see career_balance.h)
    long salary = 0;                                 // Factory-seat salary (factorySeat); 0 if not applicable
    long entry = 0;                                  // entry fee per race (by phase, or per category via override)
    long costs = 0;                                  // car: rent (if rented) OR run-cost insurance+consumables (if owned)
    long coverage = 0;          // credits of your costs covered by signed sponsors this race (reduces your-share)
    long deal_salary = 0;       // per-race salary from the active deal (paid EVERY race, not only factory)
    long sponsor_payout = 0;    // A thematic fixed payout when a sponsor objective completes (set by the
                                // overlay post-Settle). DISTINCT from s.coverage (the dormant cost-coverage trait
                                // -- still dormant). Additive default 0. This is the ONLY sponsor income term left in
                                // the report.
    // Additive cost/income terms threaded in via the new defaulted Settle params.
    // All default 0 -> a bare Settle(...) call is byte-identical (mirrors sponsor_payout). deductible/travel
    // are SUBTRACTED from net (always the player's, never covered by the team); result_bonus is ADDED.
    long deductible = 0;        // per-race insurance deductible (damage_share-scaled; always yours)
    long travel = 0;            // per-race travel-leg cost net of the type's covered fraction
    long result_bonus = 0;      // low-tier result bonus (Top10/Top5/win) for this race
    int  rep_bonus = 0;         // special-event REP bonus (mirrors result_bonus; added into rep_delta)
    long credits_delta = 0;                          // neto = prize + clean_bonus + salary + deal_salary - entry - costs + coverage + sponsor_payout + result_bonus - deductible - travel
    int  xp_gained = 0;
    int  rep_delta = 0;
    bool phase_unlocked = false;
    int  new_phase = 1;
};

// Living World -- how a non-finish maps onto the Elo duels.
//   Finished      = normal: real duels against everyone, finishing-order based.
//   MechanicalDnf = neutral: NO duels (no contest); rating unchanged.
//   ErrorDnf      = counts as the position you went out at (you're in the order at that slot).
//   NoShow        = neutral on duels (absent) + a small flat global ding (bal::RATING_NOSHOW_DING).
enum class DnfKind { Finished = 0, MechanicalDnf = 1, ErrorDnf = 2, NoShow = 3 };

// Living World -- the rating delta the Results screen shows. Mirrors SettleReport's "report" pattern.
struct RatingSettleReport {
    double before = 0.0, after = 0.0, delta = 0.0;
    int    band_before = 0, band_after = 0;   // RatingBand index 0..5 (F..A) for the ELO-band view
    int    tier_before = 0, tier_after = 0;   // RatingRepTier index 0..5 (Unknown..Legend) for the REP view
};

int  XpThreshold(int phase);             // XP needed to go from 'phase' to 'phase+1'
bool CheckPhaseUnlock(long xpTowardNext, int frontier);
int  XpFor(int pos, int field, bool clean, int racePhase, int frontier);
int  RepFor(int pos, bool finished, int field = 0, int aiLevel = 0); // field/aiLevel 0 = unscaled
int  RepDisplayBand(int rep);            // 0..5: the Market's COSMETIC band (different from the sponsor/factory RepTier)
// Living World -- rating-tier CHROME for the screens (label + color of the display band).
// Banding view of the ONE unified rating: idx = RepDisplayBand(rep). lang: 0=en 1=es.
// Returns a static string (caller must not free). Colors mirror market::REP_TIERS (kept in sync
// via REP_BAND); screens get the badge without pulling in career_market_data.h.
const char* RatingTierLabel(int rep, int lang);
unsigned    RatingTierColor(int rep);   // ImU32
// Living World: the player's starting rating for a brand-new career (bal::RATING_ROOKIE_BASE). Pure helper
// so the overlay (new-career flow) and tests share one source of truth instead of hardcoding 1150.
double RatingSeedPlayer();
// Living World -- apply a race to the PLAYER's rating. `ratingsInFinishOrder` = the rating of EVERY car in
// finishing order (index 0 = winner); `playerIdx` = where the player sits in that order. `aiPct` = the AMS2
// difficulty slider (0 => no fold; matches RepFor's "0 = unscaled" convention). `dnf` selects semantics. Mutates
// prog.rating + prog.rating_races and returns the before/after/delta + both view indices. For ErrorDnf the
// caller has already placed the player at the exit slot in `ratingsInFinishOrder`/`playerIdx`. For Mechanical
// DNF and NoShow the order/idx are ignored (neutral); NoShow additionally applies the flat ding.
RatingSettleReport SettleRating(CareerProgress& prog,
                                const std::vector<double>& ratingsInFinishOrder,
                                int playerIdx, int aiPct, DnfKind dnf,
                                int audience = 0);   // audience 0-100; scales ONLY the positive delta (a bonus, never a penalty). 0 = no change.
// Living World -- REP TIER as a 6-band VIEW of the unified rating (0=Unknown..5=Legend; bal::REP_TIER_RATING).
// This re-anchors the original REP_TIERS concept: tier is NOT a second number, it's a banding
// of the SAME rating. Replaces RepTier(rep)'s role for the new unified path (RepTier stays for the old sponsor gate).
int RatingRepTier(double r);
// Living World -- the ELO letter-band view (S/A..F). Thin alias over career::RatingBand for naming symmetry
// with RatingRepTier (band = one display view; tier = the other; both of the one rating).
int RatingDisplayBand(double r);
// Living World -- the rating-scale reqRep floor for a series of `level` (1..7). `works`=closed/works seat
// (higher, never 0); open ad (works=false) at level<=2 => 0 (free ad). Re-anchors the market's old REP_BAND
// gate onto the rating scale (bal::REQREP_RATING). Returns a rating threshold (double), not REP points.
double ReqRepRating(int level, bool works);
// Living World -- fill any MISSING driver ids in `ratings` from a field's order/iconic via RatingSeedFromOrder.
// Idempotent: an id already present (an evolved rating) is left untouched; only absent ids are seeded. `field`
// = the FieldForSeries(seriesId) result (order 1 = best, fieldSize = field.size()). Pure: no globals, no I/O.
// Declared with the engine's own lightweight view of a field entry to avoid dragging career_fields.h into the
// pure header; the .cpp includes career_fields.h and adapts. Returns the number of ids newly seeded.
struct SeedFieldEntry { std::string id; int order = 0; bool iconic = false; };
int SeedDriverRatings(std::map<std::string, double>& ratings,
                      const std::vector<SeedFieldEntry>& field);
// Living World -- WHOLE-POOL materialization. One series for the seed pass: its ladder
// `level` (1..7) plus the SeedFieldEntry view of its field (order/iconic/id). Lets the pure overload
// be tested on a synthetic pool without dragging career_fields.h into this header.
struct SeedSeriesEntry { int level = 1; std::vector<SeedFieldEntry> field; };
// Pure testable overload: seed every absent id across `pool` via LevelAnchoredSeed(level, order,
// fieldSize, iconic). Idempotent (skips empty id; never overwrites an evolved/present rating).
// Returns the count newly seeded.
int SeedAllDrivers(std::map<std::string, double>& ratings,
                   const std::vector<SeedSeriesEntry>& pool);
// Production entry point: build the pool from the real ladder/series (AllSeries() -> level +
// FieldForSeries) and seed it. Same idempotence. The .cpp pulls in career_series.h/career_fields.h.
// — SEEDING NO LONGER DEPENDS ON THE WALK ORDER.
//
// Until today the idempotence guard made the FIRST series walked win, so the ELO of anyone who
// runs in several categories was decided by the order of `AllSeries()` — a production datum at the mercy of the order
// of a file. Now their PRIMARY SEAT decides (highest tier compatible with their affinity, then the
// oldest contract, then the lower `seat_id`), which is semantically right —your category is that of
// your primary contract— and does not depend on where the walk starts.
//
// This was MEASURED AND REJECTED on 10/08, and goes in now because the data changed sign. With the grid
// coming from the static files it got worse (classes with u>2 from 28 to 32); with the grid coming from the
// seats () it **improves**: u>2 from 22 to 18 with the hierarchy intact (the academy still leads 0 of 100).
// What was wrong was not the rule: it was the world it was measured on.
//
// `save` optional: without it, it is derived from the STATIC world (year 1, and what the tests want). With it, from the
// live seat distribution — which is what matters for someone who shows up mid-game.
int SeedAllDrivers(std::map<std::string, double>& ratings, const SaveData* save);
int SeedAllDrivers(std::map<std::string, double>& ratings);   // = SeedAllDrivers(ratings, nullptr)
// Seed migration: deletes from `ratings` any id whose owner NEVER ran a settled
// race (absent from `races` or with a counter <= 0): that value is an ARTIFACT of whatever seeding was active when
// the save was created, not real evolution -> deleting it lets the SeedAllDrivers pass that follows re-seed it with the
// current logic (CastSeedFromBase for the recurring cast; fields left un-inflated). Ids with races are ALWAYS kept.
// Deterministic and idempotent in effect (an id with no races re-seeds to the same value on every boot).
// Returns how many it deleted. Pure: no globals, no I/O.
int DropUnracedSeeds(std::map<std::string, double>& ratings,
                     const std::map<std::string, int>& races);
// 23/09 — the same, EXCEPT the ids in `keep`: the ELO that a seat gave (SaveData::seat_seeded, D4 “you are worth where you
// sit”) is not an artifact of any old rule, it is the decision of the market that seated them; it is kept until
// they run. Decision 23/09: startup does NOT recompute it.
int DropUnracedSeeds(std::map<std::string, double>& ratings,
                     const std::map<std::string, int>& races,
                     const std::set<std::string>& keep);

// Living World -- one car in a settled race's finishing order, AS SEEN by the
// whole-grid update. `id` = the stable pool driver id (the rating/race-count store key). `level`/`order`/
// `field_size`/`iconic` = the LevelAnchoredSeed inputs used to lazily seed an id ABSENT from driver_ratings
// before its delta is applied (same idempotent lazy seed as SeedAllDrivers). `dnf` = a DNF car does not
// contest the duels (v1 rule: excluded from the finishing-order field) but still counts a race. `is_player`
// = true marks the human; the player is updated by SettleRating (NOT here) so this path never double-updates
// them. The vector is in finishing order (index 0 = race winner). An empty id is skipped (the SeedAllDrivers
// guard, kept verbatim).
struct GridFinisher {
    std::string id;
    int  level = 1, order = 0, field_size = 0;
    bool iconic = false;
    bool dnf = false;
    bool is_player = false;
};
// Living World -- apply a settled race to the WHOLE grid's ratings (the AI finishers; the
// player stays on SettleRating). The non-DNF, non-player ids form the duel field IN FINISHING ORDER; their
// ratings (seeded via LevelAnchoredSeed if absent) feed RatingUpdateRace with an AGGREGATE K = the mean of
// the per-driver dynamic K (RatingK(driver_races[id], isPlayer=false)) so the whole-field result stays
// strictly ZERO-SUM while an unproven field (low race-count) still swings more than a settled one.
// Each driver's delta is applied to `driver_ratings[id]` and `driver_races[id]` is bumped by 1 (incl. DNFs:
// the race still counts toward the provisional window).
//
// — DIFFICULTY DOES **NOT** ENTER HERE, and it used to. This path is AI-vs-AI duels, and
// `EffectiveOpp` shifts the RIVAL's rating by the slider but not one's own: in a field where everyone is
// AI, each one sees the others inflated and themselves not, so the deltas stop summing to zero and **the whole
// world inflates or deflates depending on the difficulty the player has set**. With the slider at 110 the
// AI ladder rises on its own, season after season, without anyone driving better.
// The slider describes how the AI runs AGAINST YOU; it does not change who is better than whom among themselves. That is why the
// parameter is not ignored: it has been REMOVED -- an unused parameter is a trap waiting to happen.
// Pure: no globals, no SHM, no I/O — the overlay only assembles the order and calls this.
void SettleRatingGrid(std::map<std::string, double>& driver_ratings,
                      std::map<std::string, int>& driver_races,
                      const std::vector<GridFinisher>& finishOrder);
int  RepTier(int rep);                   // 0=Local 1=Associate 2=Primary 3=Title
// The reward-mode multiplier (bal::REWARD_MULT), clamped to the 0..2 domain. An old save without the
// field loads at 1 (Normal, x1.00) -> byte-identical behavior to before reward mode existed.
double RewardMult(int rewardMode);
// Reward mode token, the SAME vocabulary as profile.json ("difficulty") and the app (CareerDifficulty):
// 0 "easy" · 1 "normal" · 2 "hard". RewardModeToken clamps to the domain; RewardModeFromToken returns -1 if the
// token is not one of the three (the caller decides: reject or leave whatever there was).
const char* RewardModeToken(int rewardMode);
int  RewardModeFromToken(const std::string& token);
// Applies the multiplier to a GAIN (rounded to the nearest integer). Meant to be run separately on each
// INCOME term, not the total: that way the Results panel's lines still add up to the net.
long ScaleGain(long amount, double mult);
//
long PrizeFor(int pos, long winnerPrize, long floorPrize);
// Settlement: prize by finishing position + clean_bonus
//   - entry (by phase) - car cost.
// Car cost: RENTED -> rentPct% of the WINNER'S PRIZE (winnerPrize, fixed);
//                   OWNED     -> RUN_COST_PCT % of carValue (insurance+consumables). carValue is ignored if rented.
// winnerPrize/floorPrize come from the event/phase; rented/rentPct/carValue from the chosen car.
// A factorySeat (model=factory): the team covers EVERYTHING (costs=0, entry=0) and pays
// bal::FACTORY_SEAT_SALARY per race ("you don't pay any more, you get paid").
SettleReport Settle(CareerProgress& prog, const RaceResult& r,
                    long winnerPrize, long floorPrize, bool rented, int rentPct, long carValue,
                    long entryOverride = -1,     // entryOverride>=0 replaces the per-phase entry fee
                    bool factorySeat = false,    // true = factory seat (costs/entry 0 + salary)
                    long coverage = 0,           // credits of (entry+costs) covered by sponsors this race
                    long dealSalary = 0,         // per-race salary from the active deal (any seat)
                    long deductible = 0,         // per-race insurance deductible (subtracted)
                    long travel = 0,             // per-race travel-leg cost net of coverage (subtracted)
                    long resultBonus = 0,        // low-tier result bonus (added)
                    int raceLaps = bal::COST_REF_LAPS,    // The race's LAPS; scales an OWNED car's consumables (default = reference race length = back-compat)
                    // REAL race wear (shared memory) -> scales an OWNED car's consumables on top of the
                    // nominal value (ApplyWearToCosts). nullptr / fields < 0 = NO DATA -> NOMINAL cost (back-compat,
                    // byte-identical: every previous call site and test still holds). Ignored if rented/factory.
                    const WearTelemetry* wear = nullptr,
                    // an ADDITIVE REP bonus (rewards.rep). Trailing-defaulted
                    // (matching the noShowSeason/resultBonus precedent): every previous call site compiles byte-identical.
                    long repBonus = 0);

} // namespace career
