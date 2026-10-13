#pragma once
// ALL of the career engine's balance constants in one place (tune without touching logic).
// Numbers from the project's economy design spec + the research doc.
namespace career { namespace bal {

// XP / phases ---
constexpr double XP_BASE_FIRST   = 1000.0;  // base XP for 1st place (before bonus/relevance)
constexpr double XP_BASE_LAST    = 200.0;   // base XP for last place
constexpr double XP_CLEAN_BONUS  = 0.25;    // +25% for a clean race
constexpr double XP_PODIUM_BONUS = 0.15;    // +15% podium (pos<=3)
constexpr double XP_RELEVANCE    = 0.55;    // relevance = XP_RELEVANCE^(frontier - race_phase)
constexpr double XP_THRESH_BASE  = 5000.0;  // (legacy) old model 5000*1.6^; replaced by PHASE_XP_THRESH
constexpr double XP_THRESH_GROW  = 1.6;     // (legacy)
// Per-phase promotion thresholds. CALIBRATED to real play pace, NOT a hard cap. Anchors:
// ~1 race/week (player pace) × a ~38-40 week racing season × a clean win at your level ~1438 XP
// (XpFor pos1 = 1000·1.25·1.15) ~= 57,500 XP/year winning everything. Soft target ~2 tiers/YEAR at that ceiling ->
// threshold ~28k/tier; an increasing curve (the top costs more). Racing less/not winning = slower (soft, no cap).
// FUTURE: with new AMS content these figures would need recalculating; the IDEAL is to DERIVE
// the threshold from the calendar at load time (races-per-tier × win-XP / ~2) so it self-recalibrates. TUNABLE.
// 2nd pass: playtesting felt "too fast" WITHOUT winning everything (just participating gave too much XP -> ~2.5/year).
// Raised ~1.5x so ~2/year requires actually WINNING, and a mixed season (podiums/mid-pack) lands around ~1.3-1.5/year.
constexpr long PHASE_XP_THRESH[7] = { 38000, 41000, 45000, 50000, 56000, 63000, 2000000000L }; // F7 = ceiling (there is no F8)

// REP ---
constexpr int REP_WIN    =  5;
constexpr int REP_PODIUM =  3;   // pos 2-3
constexpr int REP_POINTS =  1;   // pos 4-10
constexpr int REP_DNF    = -5;
constexpr int REP_BROKEN_CONTRACT = -10;
// sponsor-tier thresholds (accumulated REP): 0=Local 1=Associate 2=Primary 3=Title
constexpr int REP_TIER1 = 30;    // Associate
constexpr int REP_TIER2 = 120;   // Primary
constexpr int REP_TIER3 = 400;   // Title
// REP-per-race scaling (anti-farming for weak grids/AI). field/ai = 0 -> factor 1.0 (no scaling).
constexpr int    REP_FIELD_REF = 20;    // reference grid size (factor 1.0)
constexpr double REP_FIELD_LO  = 0.5;   // floor for the grid-size factor (a tiny grid)
constexpr double REP_FIELD_HI  = 1.5;   // ceiling (a large grid)
constexpr int    REP_AI_REF    = 100;   // reference AI % (factor 1.0)
constexpr double REP_AI_LO     = 0.6;   // floor for the AI factor (very low AI)
constexpr double REP_AI_HI     = 1.3;   // ceiling (headroom; AMS2's real slider caps at 120 -> max 1.2)
// Market DISPLAY bands (6) over the engine's REP — label/color ONLY.
// Sponsors and the factory gate still use REP_TIER1/2/3 (unchanged).
constexpr int REP_BAND[6] = { 0, 15, 40, 120, 300, 600 }; // Unknown/Prospect/Known/Established/Star/Legend

// Sponsor v1 · RETIRED ---------------------------------------------------
// SPONSOR_RETAINER_PER_RACE (a flat retainer per REP tier: 800/4000/20000/80000) was the LAST living remnant
// of the v1 sponsor system that was retired (catalog/gates/payout). It paid out every race just for the
// REP tier alone, with no signed sponsor or card: money "from nowhere" that also rendered a line
// item labeled "Sponsor" in the post-race breakdown, confusing next to the LIVE "Sponsor payout" line (sponsor_payout,
// cards). Sponsorship income today is: cards (sponsor_payout + cardLines), contract salary
// (deal_salary), and team coverage. REP_TIER1/2/3 are STILL alive (factory/negotiation gate).

// Save REWARD mode (chosen WHEN THE PROFILE IS CREATED, fixed for that save) ---
// Multiplies what you EARN: income money (prize, bonuses, salary, coverage, card payouts), REP and XP.
// It is NOT on-track AI difficulty (that's difficulty_mode/DDA, a separate axis), and it does NOT touch the world rating:
// rating is ZERO-SUM against the ~1300 drivers in the world, and scaling it would inflate the ranking and
// seat access until the whole table lost its meaning (a deliberate design decision).
// LOSSES are NOT scaled: costs, deductible and travel are paid in full in all three modes, and a
// REP hit from a DNF stings just the same. Otherwise "hard" would also shield you from the penalties and stop being hard.
constexpr double REWARD_MULT[3] = { 1.25, 1.00, 0.75 };   // 0=Easy 1=Normal 2=Hard

// Economy ---
constexpr double PRIZE_DECAY    = 0.82;   // prize(pos) = winner * 0.82^(pos-1)
// minimum margin of the WINNER's prize over the cost of the worst self-funded round
// (entry fee + entry cost + rental + intercontinental travel). Winning has to pay off, even if just
// barely; it only RAISES underfunded prizes from the PSV data — well-funded series don't change.
constexpr double PRIZE_MIN_MARGIN = 1.15;
constexpr double RENT_PCT       = 0.22;   // reference %; the real % is PER CAR (rentPct from the catalog)
// rev.2 (design decision: "yes, I want collector cars in the game — their running costs don't have to
// be collector-priced too"): the real price catalog (ams2_vehicle_prices.psv) has gems at 10-21x their class (a Eskdale Gry320 at 2.8M
// vs. a baseline KEYSTONE_MASTERS_11138 at 130k; 56 of 190 cars above x3). The PURCHASE price is honored in full — the
// collector's item is real —; what gets compressed to the class band is the per-race ECONOMIC value
// (CarItem.runValue, CompressPriceToClassBand), which rental, tires, insurance and run-cost are all derived from.
// It preserves the ORDER (the pricier car is still pricier) but stops dragging the economy along: x3->x3, x8->x4, x21->x5.
constexpr double PRICE_SPREAD_CAP = 3.0;
constexpr double RUN_COST_PCT   = 0.025;  // per-race cost of an OWNED car = 2.5% of its value (insurance 1.5% + consumables 1.0%), from research

// spec 2026-07-02 (AMENDMENT): the REAL components of an OWNED car's run-cost, INDEPENDENT and separately
// tunable (each a % of value). CONSUMABLES (tires/fuel/fluids) scale with the LAPS in the
// race (a 40-lap race spends more than an 18-lap one), NOT with the duration multiplier; INSURANCE is fixed (per
// event). The PCTs below are the cost at COST_REF_LAPS laps (the "reference race"): there they SUM to
// RUN_COST_PCT -> economics identical to the reference (an invariant checked in tests). Future work: also scale by
// real level/wear and/or per-component coverage (today the PCTs are fixed constants).
constexpr double COST_INSURANCE_PCT = 0.015;  // insurance — FIXED (per event, doesn't scale with laps)
constexpr double COST_TYRES_PCT     = 0.005;  // tires — % at COST_REF_LAPS laps (scales with laps)
constexpr double COST_FUEL_PCT      = 0.003;  // fuel — same
constexpr double COST_FLUIDS_PCT    = 0.002;  // fluids — same
constexpr int    COST_REF_LAPS      = 20;     // REFERENCE race: at this many laps, consumables = their base PCT (total = RUN_COST_PCT). Tunable.
constexpr double COST_MIN_PER_LAP   = 2.0;    // rule for TIME-based races: a typical min/lap -> equivalent laps = durationMin/this value. A proxy (an estimate based on track LENGTH would be more accurate; there's no length data today). Tunable.

// 0.7.0 (thread «The ability to sell cars», decision by Josh): RESALE of a purchased car. With no kilometers it is worth
// 40 % of its base price (CarItem.price, the same one paid on purchase; an event discount doesn't count).
// Kilometers subtract in a straight line until HALF of that 40 % is removed at CAR_RESALE_KM_FULL, and it stays there:
//   price = base * 0.40 * (1 - 0.50 * min(km, 10,000) / 10,000)   ->  0 km 40 % · 5,000 km 30 % · 10,000+ km 20 %
// Why 10,000 km: it is of the order of what a competition engine withstands between major overhauls (a GT3 of
// customer or an F3 hover around that figure), and in this career a sprint season adds ~1,000-1,500 km, so a
// car of one or two seasons loses little and one squeezed for years or in endurance races reaches the floor. The
// floor exists because a chassis with a worn engine still sells: it gets rebuilt, not thrown away.
constexpr double CAR_RESALE_PCT      = 0.40;      // fraction of the base price at 0 km
constexpr double CAR_RESALE_KM_FULL  = 10000.0;   // km at which mileage reaches its maximum effect
constexpr double CAR_RESALE_KM_LOSS  = 0.50;      // the most mileage subtracts from that 40 % (floor = 20 % of base)

// (REAL wear): the NOMINAL cost above already prices in race LENGTH; telemetry from the
// game's shared-memory telemetry ($pcars2$) says how much you punished the car RELATIVE TO a normal race of that length, and SCALES the
// consumables accordingly: babying the tires is cheap, trashing them is expensive. The REFERENCE values below are the wear from
// a "normal" race -> the multiplier is exactly 1.0 there (reference economics untouched; an invariant checked in tests).
// DAMAGE isn't part of this: it's already priced separately in the DEDUCTIBLE (ComputeRaceDeductible), and charging it again
// would be double punishment. WITHOUT telemetry (shared memory off / a broken read) -> multiplier 1.0 -> NOMINAL cost (fail-open).
constexpr double WEAR_REF_TYRE   = 0.50;   // wear on the WORST tire at the end of a normal race (0..1)
constexpr double WEAR_REF_FUEL   = 0.70;   // fraction of the fuel tank a normal race consumes (0..1)
constexpr double WEAR_MULT_MIN   = 0.50;   // floor: no matter how carefully you drive, the consumable never drops below half
constexpr double WEAR_MULT_MAX   = 1.75;   // ceiling: no matter how hard you trash it, it never runs away unbounded

// Buying vs. renting: the car is bought at its own PRICE (a per-car figure).
//   RENTING = rentPct% of the class's WINNER'S PRIZE (fixed, not the player's decayed prize); insurance+consumables included.
//   OWNING = RUN_COST_PCT % of the car's value per race (insurance+consumables). No capital outlay, but paid every race.
// Entry fee per phase (cross-cutting), from research
constexpr long ENTRY_BY_PHASE[7] = { 500, 1000, 2000, 4000, 8000, 15000, 20000 };

// Credit cushion a real ROOKIE starts with (granted ONCE, when the career is minted at creation).
// Covers entry fees (~500-1000/race) + consumables for the first entry-level races, until prize money kicks in
// (e.g. keystone_championship prize=6000). A rookie goes to the Market for their first seat (rental/customer), they don't buy.
// 10000 -> 20000 (starting at 10k left no margin after 2-3 weak races).
constexpr long STARTING_CREDITS = 20000;

// Factory seat (model=factory: Pemberton_Zel121/PINNACLE_CHALLENGE_5480, modern F1) ---
// The car is neither bought nor rented (PSV comprar=0, UI shows "UNDER CONTRACT"): the team covers the costs
// (costs=0, entry=0) and PAYS a per-race salary — the spec's payoff moment ("you no longer pay: you get paid").
// PROPOSED from the factory-driver audit (research: 150-400k credits/race), PENDING
//   validation. Tunable without touching logic.
constexpr long FACTORY_SEAT_SALARY = 150000;

// The GLOBAL aggression slider has to stay at MEDIUM ------------------------------------
// The official Custom AI Drivers guide is explicit: the session's aggression slider REMAPS the
// aggression the XML sets. At Maximum, EVERY driver gets remapped to 1.00 (the XML value is completely overridden); at High it's
// compressed to 0.6-1.0, and ONLY at Medium is the full 0.2-1.0 range mapped. Since each driver now carries THEIR OWN aggression
// (a factor model, SD 0.142), any setting above Medium throws that work away and returns
// a grid of aggressive clones. The native slider's vocabulary: 40 Low · 60 Medium · 80 High · 100 Maximum.
constexpr int AI_AGGR_MEDIUM = 60;
// Clamps an aggression value to the ceiling that preserves the XML's personality. 0 = "don't force" and is honored.
inline int AggrSliderKeepingPersonality(int aggr) {
    return (aggr > AI_AGGR_MEDIUM) ? AI_AGGR_MEDIUM : aggr;
}

// Gate qualifiers (the heroic path): you have to WIN against the AI at MAXIMUM ---
// Anchors: HandoffAiForPhase produces skill 82..108 (AMS2's real slider range: 70..120),
// aggr 40/60/80 and mistakes 2/1. The heroic path forces the absolute ceiling.
// enum for OpponentMistakes, MEASURED by reading the game's own labels. There are SEVEN options,
// indices 0..6. The earlier hypothesis (0=Off..7=x5, with an intermediate x1.5) was wrong on both counts: there's
// no x1.5, and the maximum is 6. Writing a 7, like the old per-phase table did, was writing out of range.
constexpr int kMistOff      = 0;   // off
constexpr int kMistHalf     = 1;   // x0.5
constexpr int kMistStandard = 2;   // standard (x1)
constexpr int kMistX2       = 3;
constexpr int kMistX3       = 4;
constexpr int kMistX4       = 5;
constexpr int kMistX5       = 6;   // maximum
constexpr int kMistMax      = kMistX5;
// 19/09 — the "don't force" sentinel for AI mistakes is -1, not 0. 0 is a REAL value of the enum
// ("off") and with 0 as the sentinel nobody could turn them off: CareerApplyEvent only wrote s28 with a value
// > 0, so the gate qualifier (GATE_AI_MISTAKES = off) left the setting the player had in
// their last Custom Race. Everything that falls within the enum is written and NOTHING outside it: a 7 was once saved
// in the game and left the setting rendering corrupt.
constexpr int kMistNotForced = -1;
inline bool MistakesForced(int m) { return m >= kMistOff && m <= kMistMax; }

constexpr int GATE_AI_SKILL    = 120;   // AMS2's slider ceiling
constexpr int GATE_AI_AGGR     = 100;   // maximum aggression
constexpr int GATE_AI_MISTAKES = kMistOff;   // gate qualifier: no AI mistakes (0 = off, measured)

// 19/09 — AI mistakes of a SPECIAL EVENT when the author doesn't set them (decision by Josh: they are authored per
// event, `mist` field of the form). Until today the event ran with synthetic phase 1, which in the race
// table is the maximum (x5), and that's how the player's report came in. x2 is the midpoint of the measured enum; the
// race table (HandoffAiForPhase) is NOT touched.
constexpr int EVENT_AI_MISTAKES_DEFAULT = kMistX2;

// Unified rating (ELO=REP) · Living World ---
// Pure rating maths live in career_rating.{h,cpp}; these are its tunables.
constexpr double RATING_DIV          = 400.0;   // Elo logistic divisor
constexpr double RATING_Q_R0         = 1600.0;  // q(R) logistic center (mid)
constexpr double RATING_Q_S          = 275.0;   // q(R) logistic slope
constexpr double RATING_SEED_TOP     = 1900.0;  // career_fields order 1 (best real) seed
constexpr double RATING_SEED_BOTTOM  = 1200.0;  // career_fields last (worst real) seed
constexpr double RATING_ICONIC_BUMP  = 60.0;    // +bump for iconic drivers
constexpr double RATING_ROOKIE_BASE  = 1150.0;  // generated rookie (and the world's floor: promote-min, mem-unlock, reqRep)
// PLAYER's starting point above the world's floor, so they don't start too far behind the pack in the world rank.
// Deliberately separate from RATING_ROOKIE_BASE (raising that one would drag along promotion/memory/reqRep gates). Tunable.
constexpr double RATING_PLAYER_SEED  = 1250.0;  // upper-mid of the level-1 field / midpoint of level 2
// ===================================================================================================
// / — THE FORM PERTURBS CAPABILITY, IT NO LONGER REPLACES IT
// ===================================================================================================
//
// What dies: `ratingSkill()`, which returned the WHOLE `RatingQuality(elo)` and threw away the authored talent as
// soon as the driver had a rating. And they had it from minute zero, because `SeedAllDrivers` seeds the ELO at
// start — meaning the authorship was lost in a round trip with the seed that had come out of
// itself.
//
// It is the root of the «detached ace» of with the XML pace coming out of the ELO, a driver in a
// fast car becomes a BETTER PERSON race after race. With the separation, what belongs to the car stays in the car.
//
//     confidence = races / (races + FORM_CONFIDENCE_WINDOW)
//     form_delta = clamp( (results − capability) × confidence, ±FORM_RATING_CAP )
//     effective  = capability + form_delta
//
// With 0 races confidence is 0 and the grid is 100% authorship; with many it tends toward today's, but
// CAPPED. Both constants are on the 0..1 scale of `RatingQuality`, the same one talent lives on.
// AND BEFORE SUBTRACTING, THE TWO MAGNITUDES HAVE TO BE ON THE SAME SCALE. Measured on production:
//
//     authored talent      mean 0.859   sd 0.080
//     RatingQuality(ELO)   mean 0.451   sd 0.237
//
// The means are 0.41 apart —half the scale— and the dispersion differs 3 times. Subtracting them bare gives not a
// deviation per form: it gives a constant bias for everyone in the same direction. It is not a new defect: the old
// code did the same but WHOLE, swapping 0.86 for 0.45. The XML normalization washes out
// the shift, but does NOT wash out the triple dispersion — a serious candidate for why the grids come out
// ~1.9 times wider than their level produces (see customai_affinity_noise_decides_ace).
//
// So the ELO is standardized over the live population and projected onto the talent scale before subtracting.
// These are the two constants of the TARGET scale; the source one is measured at each composition, because the
// population changes with the world.
// ===================================================================================================
// GRID WIDTH, IN A SINGLE KNOB (12/08)
// ===================================================================================================
//
// How far apart the XML puts the first from the last. It is what the grid knot has been circling for five attempts, and
// until today there was no place to turn it.
//
// WHAT BECAME CLEAR ON MEASURING IT. Two different magnitudes were being compared and that is why nothing added up:
//
//   · `spread_pct_dry` (class_pace_curve.psv) is the % of LAP TIME of the last relative to the first.
//     Median 3.9 over 100 classes. It is what a real race measures, and it is the target of the anchoring.
//   · Curve speaks of the XML's `race_skill` setting, which is another magnitude. Its bands per tier go from
//     0.86-0.99 (T1 single-seater) to 0.35-0.43 (T5): a full grid spans **0.64 of knob** (0.59 in
//     sports cars).
//
// AND THERE WAS A MISQUOTED FIGURE HERE that is worth leaving corrected: the old note said «we deliver 4.6%
// against the 10-12% on which Curve, [competitor] and Apex coincide». That 10-12 is NOT in Curve's manual; what
// is written is the table above. Measured against it: we deliver **0.419 of knob, 65% of 0.64**.
// We are narrower, but by a third — not by three times, which is what the misquoted figure suggested and
// what made it seem that an enormous change was missing.
//
// USER DECISION (12/08): «the XMLs rule, but this should be something we can adjust easily
// in the future». So the width becomes ONE NUMBER, here, applied at a single point (`TalentRefSpanFor`).
//
//     1.00 = exactly today's (0.419 of knob, 65% of Curve)
//     1.53 = just Curve's width ·        2.00 = double today's
//
// It goes to 1.0 on purpose: changing it is a design decision with measured consequences (see
// grid_knot_five_reverted_attempts), not a step adjustment. What changes today is that it can now be turned and
// measured in one place, instead of spreading the effect among affinity, seeding and the curve.
//
// 24/08 — THIS NUMBER NO LONGER MOVES THE GRID, AND THAT IS THE FIRST THING TO KNOW ABOUT IT. Everything
// above describes the world in which the XML's `race_skill` came from dividing a talent gap by a
// yardstick: there, narrowing the yardstick widened the grid. Since the three-nail conversion (19/08) the XML doesn't
// go through any yardstick —the ace goes to 1.00, the tail-ender to the DRIVER fan of its class and the median to the
// class's data—, so turning this doesn't change a single value of the file AMS2 reads.
//
// What it DOES do: multiply what `TalentRefSpanFor` returns, which today only feeds DIAGNOSTICS —the
// `[canon-vara]` of career_canon and the harness's measurement dumps—. That is, it is the zoom of the measuring
// apparatus, not the control of the machine. It is kept for that reason and because a test pins that the apparatus responds.
//
// THE REAL WIDTH CONTROL, for whoever comes looking for it, is each class's `spread_pct_dry` in
// docs/career/class_pace_curve.psv —a measured datum, one per category— and `kHeadOpen` for the head.
constexpr double GRID_SPREAD_SCALE = 1.0;

constexpr double TALENT_MEAN = 0.859;   // measured on the authored cards that run
constexpr double TALENT_SD   = 0.080;

constexpr int    FORM_CONFIDENCE_WINDOW = 20;   // ~1.5 seasons to reach half confidence
// The talent fan of a full grid is 0.125 (CurveSkill over [0.85, 1.00]). A cap of 0.10
// lets sustained evidence invert two neighbors and even a stretch of the pack, but NEVER
// reorder the whole grid: the authored hierarchy remains the skeleton.
constexpr double FORM_RATING_CAP        = 0.10;
// ~~~04/09 (, 2nd half) — THE YOUNG ONE IS NOT SLOWER, HE IS MORE IRREGULAR.
//
// Request from Josh: «what we need to add is some noise, that varies and decays over time». The anchored
// arc (career_fields.cpp) already makes a kid IMPROVE with the years; this is the other side: while
// he rises, his weekends resemble each other less. One day he is where he should be and another he goes off.
//
// The size is NOT a number by eye: it is proportional to how much he has left to grow. The model's ramp goes from
// `AGE_RISE_FLOOR` (0.82) to 1.00, so at 18 -- with the whole path ahead -- the form cap
// is multiplied by 1+K, and at 27, with the rise done, it is worth exactly the usual. With K=1 that is
// «when young your results oscillate twice as much», which is what you see in any academy.
//
// It can't run away: `ClampGridNoise` still bounds the total deviation to the class's fan.
constexpr double FORM_CAP_YOUTH_K       = 1.00;
// 17/08 — HOW MUCH THE NOISE CAN MOVE A DRIVER, as a fraction of the AUTHORED fan of their grid.
// The problem it solves, measured: `FORM_RATING_CAP` (0.10) and the affinity cap `kAffCap` (0.12) are
// ABSOLUTE and can stack up to 0.22, whereas the fan of CURATED CARDS in F1 measures 0.11. That is,
// the noise can take the 6th and put him last -- and it does: Russell comes out 19th of 20 with the sixth best
// card. See docs/superpowers/specs/2026-08-17-curva-por-clase-y-vara-viva-design.md sec 6.
// At a quarter of the fan, the noise shuffles NEIGHBORS and doesn't reorder the field. It doesn't replace the two caps: the
// two are still there with their shape intact, and this bounds their COMBINED effect in a single place.
// 18/08 — the «0.22 stacked» above is HISTORY (it is what there was on 17/08 and what motivated this
// number). Since that same day only one of the two caps is LIVE: affinity was left at scale 0 in
// production (career_traits.cpp::g_affinityScale) and the mechanism remains whole so it can come back. That is,
// today the real noise is the form alone, 0.10 -- and this cap bounds it the same, which is the reason it exists
// in a single place instead of spread between the two functions.
//
// 18/08 — AND WATCH THE DENOMINATOR, WHICH IS NOT THE 0.11 ABOVE. This cap is applied over the fan of
// `authored_talent` of the COMPOSED grid, and that number is dominated by the CURVE, not by authorship: a row without a
// curated card inherits the curve point of ITS SLOT (career_fields.cpp, `base = p.talent > 0 ? p.talent :
// puntoCurva`). Measured in F1: curated cards 0.11, but composed fan **0.3000** -> the real limit is
// 0.0750, not 0.0275. That is, the form can move a driver ~68% of the CURATED fan.
// It is not a defect, it is how it is built -- but whoever computes the limit with 0.11 is off by 2.7x,
// and the conversion phase reads that same field.
constexpr double NOISE_SPAN_FRAC = 0.25;

constexpr double K_PROVISIONAL       = 32.0;    // K while provisional (rookie / first races)
constexpr double K_SETTLED           = 12.0;    // K once settled
constexpr int    K_PROVISIONAL_RACES = 10;      // races before a driver settles
constexpr double K_PLAYER_MIN        = 16.0;    // player's K floor (always responsive)
constexpr double AI_RATING_PER_PCT   = 8.0;     // effOpp = rOpp + this*(aiPct-100)
constexpr double RATING_NOSHOW_DING  = 15.0;    // small flat global ding for a no-show (reserved — consumed by a later integration step)
// Letter bands ([competitor]): A>=2000 B>=1700 C>=1500 D>=1200 E>=1100 F<1100. Index 0=F .. 5=A.
constexpr double RATING_BAND_MIN[6]  = { 0.0, 1100.0, 1200.0, 1500.0, 1700.0, 2000.0 };

// Level-anchored seed — the single global ladder, anchored by series prestige ---
// LevelAnchoredSeed(level, order, fieldSize, iconic) places a field's in-field order-spread AROUND
// the per-level midpoint below (instead of the per-field 1900..1200 band that created the "wall").
// r = LEVEL_SEED_MIDPOINT[level-1] + (0.5 - t) * LEVEL_SEED_SPREAD  (+ iconic bump). t = order-fraction
// (0 = field #1, 1 = field last). Index = level-1 (1..7). Anchored on REQREP_RATING (the spec
// level->rating floor) so adjacent levels' midpoints are only ~100-150 apart; the SPREAD (wider than
// the largest adjacent gap) makes adjacent levels OVERLAP at the edges (the "hero in a bad car",
// Tuned live behind these constants — not user-facing decisions.
constexpr double LEVEL_SEED_MIDPOINT[7] = { 1150.0, 1250.0, 1400.0, 1550.0, 1700.0, 1850.0, 1950.0 };
constexpr double LEVEL_SEED_SPREAD      = 350.0;   // > max adjacent midpoint gap (150) => edge overlap

// Cast seed (feedback from playtesting) — the rk_* cohort seeds INDEPENDENTLY of tier ---
// The cast is seeded across ALL tiers 1..7 with ONE global rating: anchoring it to the field of "the first series
// raced" left it at the bottom of that band (a weak Lucía) and also inflated that series' fieldSize.
// CastSeedFromBase maps base_skill (the authored hierarchy from the PSV) linearly onto the ladder:
// base CAST_SEED_BASE_LO -> RATING_ROOKIE_BASE (1150) .. base CAST_SEED_BASE_HI -> CAST_SEED_RATING_HI.
// The apex (base 0.97) lands at ~2140: above tier 6's top iconic (~2085) and right next to tier 7's (~2185)
// => "among the best" in EVERY series they race in.
constexpr double CAST_SEED_BASE_LO   = 0.70;
constexpr double CAST_SEED_BASE_HI   = 1.00;
constexpr double CAST_SEED_RATING_HI = 2250.0;

// Grid XML band (feedback from playtesting) — on-TRACK race_skill compressed to a realistic band ---
// BuildGridXml writes race_skill as a multiplier of the global slider; the logistic q(R) over seeds
// 975..2185 produced 0.2..1.0 (a runaway front group). Remapped by RANGE in the class table:
// upper half in [MID, 1.0] (the best = 1.0), lower half in [FLOOR, MID). Nobody drops below FLOOR.
constexpr double GRIDXML_SKILL_FLOOR = 0.85;
constexpr double GRIDXML_SKILL_MID   = 0.95;

// Per-class XML wet ability: preserve relative differences, best wet specialist = 1.
constexpr double GRIDXML_WET_SKILL_FLOOR = 0.70;
constexpr double GRIDXML_WET_SKILL_CEILING = 1.00;
// 04/10 (Josh; [tester], "the AI starts slowly"): the start, SAME as the wet. The human band left p05 at 0.67
// and a quarter of the field stuck to that floor; the best starter of the class now writes 1.0 and the rest is spread out.
constexpr double GRIDXML_START_REACTIONS_FLOOR = 0.70;
constexpr double GRIDXML_START_REACTIONS_CEILING = 1.00;

// REP-as-a-VIEW of the unified rating (Living World) ---
// The prototype's REP_TIERS (Unknown..Legend) and ELO bands are now TWO
// VIEWS of the ONE rating. RATING_BAND_MIN[6] above is the letter band (F..A). This is the 6-tier REP view,
// re-anchored from the prototype's stand-in 0..2800 scale onto the rating scale (rookie≈1150 .. legend≈2100+).
// Index 0=Unknown 1=Prospect 2=Known 3=Established 4=Star 5=Legend.
constexpr double REP_TIER_RATING[6] = { 0.0, 1200.0, 1400.0, 1600.0, 1850.0, 2100.0 };

// reqRep re-anchored to the rating scale (spec soft floor; here a hard-ish display floor per series LEVEL
// 1..7, clamped). Closed (works) seats scale x1.15 with a floor; open ads use the lower row. Index = level-1.
// Tuned so club/GT3-works/top-works land near 1300/1600/1900. Open: level<=2 => 0 (free ad).
constexpr double REQREP_RATING[7] = { 1150.0, 1250.0, 1400.0, 1550.0, 1700.0, 1850.0, 1950.0 };

// Contract engine / market (ELO=REP) · Living World ---
// Two-sided matching constants (re-tunable; values are [competitor]'s Um/Ym/Jm/Tm re-anchored).
constexpr double MK_W_Q          = 0.70;   // merit: weight of quality q(R)
constexpr double MK_W_P          = 0.30;   // merit: weight of prestigeFit   (w_q + w_p = 1)
constexpr double MK_L_TEAM       = 0.45;   // L_t: team loyalty (rel vs merit blend in Want_T)
constexpr double MK_L_DRIVER     = 0.40;   // L_d: driver loyalty (rel vs q(R) blend in Want_D)
constexpr double MK_RIV_O        = 0.10;   // rivBoost = o*(nemesis/total); +0.1 same cat / +0.1 same team
constexpr double MK_K_PAY        = 0.50;   // pay_driver: payAdjust = 1 + k_pay*money_norm
constexpr double MK_K_COV        = 0.35;   // coverage term in per-race salary (deal axis; filled in by later work)
constexpr double MK_K_BONUS      = 0.50;   // sign_bonus = per_race * k_bonus
constexpr double MK_W_SPONSOR    = 0.20;   // w_s: sponsorFit weight in Want_T (the real overlap is filled in by later work)
constexpr double MK_PRESTIGE_LO  = 30.0;   // prestigeAttract = clamp((prestige - LO)/SPAN, 0, 1)
constexpr double MK_PRESTIGE_SPAN= 60.0;
constexpr double MK_CONTINENT_PEN= 0.60;   // continentPen strength for foreign low-prestige series
constexpr long   MK_PAYDRIVER_BUYIN[7] = { 2000, 4000, 8000, 16000, 32000, 60000, 100000 }; // base buy-in by tier
// The credit reference that maps the player's liquid budget to money_norm = 1.0 (the
// pay-driver budget axis 0..1). money_norm = clamp(credits / MK_PAYDRIVER_MONEY_REF, 0, 1); >0 surfaces
// above-pace backmarker buy-in seats (ContractKindForSeat's Latifi branch). Tunable (live-tuned, not a user
// decision). ~ the top-tier base buy-in so a wealthy player can buy into the upper grid; a broke player (0
// credits) -> money_norm 0 -> no pay seats.
constexpr double MK_PAYDRIVER_MONEY_REF = 100000.0;
// pay-driver competence floor for a CLOSED (works) grid. The player can buy a
// seat ABOVE their pace, but NOT if their quality is more than this margin (on RatingQuality's 0..1 scale)
// below the seat's bar. This prevents pay-to-win with zero skill (a rookie can't buy their way into an F1 seat).
constexpr double MK_PAYDRIVER_SKILL_FLOOR = 0.15;
// MK_OFFER_TOP_K — RETIRED. The Market offer set is ORDERED by teamInterest (descending,
// stable, id-tiebreak) and NEVER truncated (NO-CAP user decision): the four gates (fit/eligibility/timing/
// scarcity) prune to a small relevant set; the organic overlap/missed-race penalty is the only limiter. There
// is no concurrent-offer cap anywhere. The constant was referenced ONLY at its old definition (no .cpp used it),
// so its removal is logic-free. Do NOT reintroduce a numeric offer/contract cap.
constexpr double MK_TRYOUT_WANT_T= 0.78;   // a high-standard team (Want_T above this) demands a tryout first
constexpr int    MK_TRYOUT_MIN_LEVEL = 4;  // ...and only at tier >= this (top works seats)

// tunables — sensible defaults picked when the design spec left them unspecified ---
// All live-tuned (via career_test / feel), not user decisions. Seeded with sensible defaults; each slice
// references the named constant so no magic number ever lands in engine logic. MK_OFFER_TOP_K above was
// RETIRED by the teamInterest work — see its retirement note above (ordering-only, never a cap).
// Timing window: how far AHEAD of the cursor a seat's round-1 may be and still surface
// (season-relative rolling window; next-season seats appear in the back half of the season).
constexpr int    MK_OFFER_HORIZON_DOY  = 200;   // season-relative offer window
constexpr int    MK_OFFER_TTL_DAYS     = 28;    // offer_lifecycle: an offer expires 4 weeks after appearing (neutral, not a snub)
// Relevance band (feedback from playtesting): an OPEN series only ADVERTISES (bypassing "always advertises without a
// freed-up seat" logic) if its tier isn't more than MK_OFFER_BAND_BELOW BELOW the player's phase — a GT3 driver isn't
// courted by karting teams. FLOOR only (series ABOVE already surface as 'held'/courting). Does NOT apply
// to real FREED-UP seats or to DISPLACEMENT (an earned vacancy isn't spam — it can surface at any tier). It's a
// relevance FILTER, NEVER a numeric cap (NO-CAP). Raise it to see more low-tier series; lower it to be more selective.
constexpr int    MK_OFFER_BAND_BELOW   = 2;     // Relevance band: tiers below the phase that still advertise an open seat
// Scarcity 4th gate: deterministic per-(seat,season) availability draw; works seats are scarce,
// open ads common (asymmetry works<<open). fnv1a(series|year|seed)%100 >= rate -> seat absent that season.
constexpr int    MK_SCARCITY_OPEN_RATE_PCT  = 70;   // open ads ~7-in-10 seasons available
constexpr int    MK_SCARCITY_WORKS_RATE_PCT = 25;   // works seats ~1-in-4 seasons available
// GATE_fit margins: closed seats pass iff rating in [reqRep - UNDER, reqRep + OVER] (rating scale).
constexpr double MK_FIT_UNDER_MARGIN = 150.0;   // thin-field soft floor below reqRep
constexpr double MK_FIT_OVER_MARGIN  = 400.0;   // backmarker won't court an over-qualified driver
// GATE_afford: signability check (credits vs buy-in), NOT generation. Dormant at money_norm=0.
constexpr double MK_AFFORD_MARGIN       = 1.0;   // credits >= cost * margin to enable Accept
constexpr double MK_GATE_AFFORD_MARGIN  = 1.0;   // alias: the accept-time UI soft-mark threshold
// Soft-score weights. NEW form/jump terms; CLEAN reserved at 0 (signal source).
constexpr double MK_W_FORM       = 0.10;   // weight of CareerForm in the soft score (one source w/ why-bar)
constexpr double MK_FORM_NEUTRAL = 0.7;    // MUST equal CareerForm's empty default (neutral form)
constexpr double MK_W_JUMP       = 0.10;   // weight of the tier-gap (jump) penalty term
constexpr int    MK_JUMP_FREE    = 1;      // tier-gap freebie (a 1-tier stretch is unpenalized)
constexpr double MK_W_CLEAN      = 0.10;   // now LIVE — cleanliness soft term in Want_T
                                           // (was 0.0/RESERVED previously; goes live as the cleanliness signal source lands).
// Provisional-spike discount: damp want_t while rating is provisional (rating_races < K_PROVISIONAL_RACES).
constexpr double MK_PROVISIONAL_DISCOUNT = 0.85;   // <1.0 multiplier while rating is provisional
// Deprecated: MK_MILESTONE_TITLEFIGHT_MAXPOS/ROUNDS_LEFT (the late "title fight" trigger)
// RETIRED — the generator no longer fires ms_titlefight_* (replaced by real press coverage). The
// ms_titlefight_* templates still live in strings.cpp to render old saves that used that subtype.
// Inbox retention housekeeping: prune oldest READ + non-actionable at generation time.
// This is mail housekeeping, NOT the offer cap (the Market offer set stays uncapped).
constexpr int    MK_INBOX_MAX_MAILS = 60;   // inbox redesign: total retained (previously 30)
// Per-family table (index = family: OFFER, PROGRESS, SPONSOR, NEGO, INTEL, RIVAL, PRESS, INVITE,
// TEAMOPS). -1 = no cap of its own (SPONSOR is legacy; NEGO lives/dies with its thread). Still just
// INBOX housekeeping, NOT an offer cap.
constexpr int    MK_INBOX_FAMILY_CAPS[9] = { 8, 20, -1, -1, 10, 10, 10, 8, 10 };
// Mail with a lifecycle: streak threshold (consecutive podiums / bottom-third-or-DNF) and a cap
// on Atenea milestones per pulse (whatever doesn't fit gets DISCARDED — except the kickoff one, which always goes first).
constexpr int MK_MAIL_STREAK_PODIUMS = 3;
constexpr int MK_MAIL_STREAK_BAD     = 3;
// Atenea's cap is generalized into a SHARED cap between family 1 (Atenea milestones)
// and family 8 (TEAMOPS) — 2 flavors per pulse TOTAL, not 2+2. REPLACES MK_MAIL_ATENEA_PER_PULSE.
constexpr int MK_MAIL_P2_PER_PULSE = 2;
// Damage invoice: the deductible threshold that triggers the mail (the larger of two: a floor in
// credits WITHOUT a contract, or a % of the per-race pay WITH one).
constexpr long MK_MAILOPS_DEDUCT_MIN_CR = 1500;
constexpr int  MK_MAILOPS_DEDUCT_PCT    = 15;
// Tone thresholds by cleanliness (EMA) — >=IRKED calm, >=WARN annoyed, otherwise a serious warning.
constexpr int MK_MAILOPS_CLEAN_IRKED = 80;
constexpr int MK_MAILOPS_CLEAN_WARN  = 50;
// Clean streak — consecutive incident-free races that trigger team recognition.
constexpr int MK_MAIL_CLEAN_STREAK = 5;
// Treasury alert — the next actionable event triggers the warning if it lands within this many
// days AND its minimum outlay (RaceEntryOutlay) exceeds the current balance.
constexpr int MK_MAILOPS_TREASURY_DAYS = 14;
// On-track rivalry — tone driven by the rival's aggression (base_aggression >= this threshold -> "heated";
// otherwise "calm"), unless accumulated beef forces the "you again" template (beef.level >= this threshold).
// Cooldown in races between rivalries triggered with the SAME rival (prevents the same id from firing every race).
constexpr double MK_BEEF_HEATED_AGGR      = 0.55;
constexpr int    MK_BEEF_LEVEL_BEEF       = 2;
constexpr int    MK_RIVAL_DUEL_COOLDOWN_RACES = 2;
// Rollover's INTEL block (career_calendar.cpp IntelRolloverMails). Cap on INTEL mail
// (retirement / market summary / promotion) per rollover — "whatever doesn't fit is discarded", priority
// retirement-h2h > summary > promo-h2h. The nemesis mail (RIVAL family) does NOT count against this cap.
// MIN_H2H = total head-to-head duels (w+l) against the player for a retirement to generate its own dedicated intel_retire mail;
// below the threshold, the retiree can only feed the market summary (never their own mail).
constexpr int    MK_MAIL_INTEL_PER_ROLLOVER = 2;
constexpr int    MK_INTEL_RETIRE_MIN_H2H    = 4;
// Contract subtype mirror of Contract::kind (career_save.h). 0/1/2 already exist; 3/4 are new.
constexpr int CONTRACT_WORKS       = 0;
constexpr int CONTRACT_CUSTOMER    = 1;
constexpr int CONTRACT_PAY_DRIVER  = 2;
constexpr int CONTRACT_TRIAL       = 3;   // short contract (e.g. half-season) + objective set
constexpr int CONTRACT_CONDITIONAL = 4;   // development: promotion clause to a target series next year
constexpr int MK_MAIL_SUBTYPE_COURTING = 10;   // Inbox redesign: courtship letter (family 0, NO actions)
// Press interview (family 6, subtype 0..3) — the chosen TONE moves REP and relationship
// marks, NEVER the sporting rating (ELO is zero-sum against the world — an interview doesn't win races).
constexpr int MK_PRESS_REP_HUMBLE      = 4;    // humble: modest +REP
constexpr int MK_PRESS_REP_SPICY       = 8;    // spicy: higher +REP (risk/reward of the headline)
constexpr int MK_PRESS_REP_SAFE        = 2;    // institutional: minimal +REP, no relationship marks
constexpr int MK_PRESS_MARK_X1000      = 50;   // ±0.05 of rel_mark (same ×1000 scale as RelMark.delta_x1000)
constexpr int MK_PRESS_SUBTYPE_RANKING = 10;   // preseason piece (ranking) -- NEVER actionable
// THE CASCADE CHRONICLE: the column summarizing the season's comings and goings. Like the
// ranking, it's a press piece with NO actions -- it's not a question to answer, so it coexists with
// any pending interview.
constexpr int MK_PRESS_SUBTYPE_CASCADE = 11;
// "title fight" window from the settle emitter (career_contract.cpp). The press only asks
// the leader or runner-up (position <= MAXPOS in the standings) when few rounds remain (>=1 and <= ROUNDS): a
// close, imminent championship finish — not a comfortable mid-season lead. Tunables.
constexpr int MK_PRESS_TITLEFIGHT_MAXPOS = 2;  // P1..P2 in the standings
constexpr int MK_PRESS_TITLEFIGHT_ROUNDS = 3;  // 3 rounds or fewer from the end

// One-off substitution: a weekly deterministic draw (same idiom as seatFnv1a/
// SillySeasonFreeSeats in career_seats.cpp: career_seed|year|week -> %). MK_SUB_WEEK_PCT = the weekly
// probability a team offers its seat for ONE round (a rare event, not a steady trickle). The fee is
// MK_SUB_FEE_BASE scaled by the LEVEL of the offered series (1..7, higher toward the top of the pyramid).
// MK_SUB_LEAD_MIN_DAYS = minimum lead time between TODAY and the offered round (never "show up tomorrow").
constexpr int  MK_SUB_WEEK_PCT      = 4;      // ~1 week out of every 25
constexpr long MK_SUB_FEE_BASE      = 2500;   // base credits per round, × series level
constexpr int  MK_SUB_LEAD_MIN_DAYS = 7;      // minimum required lead time (days)

// Deal coverage economy (DORMANT) · Living World ---
// SPONSOR_TIER_BASE (the old per-tier base income table) retired with the rest of the objective-
// sponsor catalog — it was only consumed by the deleted career_sponsor.cpp::SponsorTierBase() + its test;
// IncomePerRace below has always taken `base` as a caller-supplied param (never literally read this array).
// income/coverage maths stay in career_econ.{h,cpp}.
// k_cov: how much covered cost suppresses your per-race income (the coverage<->income trade-off).
// income_per_race = base*(0.7 + 0.6*q(R))*(1 - coverage*K_COV); coverage in 0..1, clamped.
constexpr double K_COV               = 0.45;   // coverage->income discount weight (tunable)
constexpr double INCOME_Q_FLOOR      = 0.7;    // income base multiplier at q(R)=0 (worst fit)
constexpr double INCOME_Q_SPAN       = 0.6;    // +span at q(R)=1 (best fit) -> 0.7..1.3 of base
// Qualifier / dream-race ENTRY money sink by phase (the legacy inheritance bankrolls these).
// Scales with phase like ENTRY_BY_PHASE but heavier (it's a one-off shot, not a per-round fee).
// DORMANT for now. Do NOT remove.
constexpr long QUALIFIER_ENTRY_BY_PHASE[7] = { 2000, 5000, 12000, 25000, 50000, 90000, 140000 };

// survivors · the objective-sponsor catalog/gates/payout was retired
// (SLOTS_BY_TIER, PAYOUT_BY_TIER, PAYOUT_OBJ_MULT, OBJ_* indices, TOP8_GOAL/ENDUR_GOAL, MAIL_THROTTLE — the
// last was already unreferenced, GenerateSponsorEmails never read it). These 2 groups SURVIVE because other
// LIVE systems still consume them: ACQ_COST feeds the v3 save migration refund; the thresholds feed
// career::CardFlagsFrom (cards-sponsor spec 2026-07-11) via the RaceTelemetry moved to career_cards.h.
//
// Acquisition cost per tier (0=local..4=elite; career::SponsorTier in career_save.h) — the migration refund
// for saves v<3 that still had signed_sponsors (SPONSORS-04 retire-with-refund, see career_save.cpp).
constexpr long SPONSOR_ACQ_COST[5]        = { 2000, 8000, 24000, 60000, 130000 }; // credits to sign, by tier
// Thematic-objective thresholds, from telemetry. One pitfall to note: mTyreWear is WEAR consumed (0=fresh,
// 1=fully worn) — "low wear" = max corner BELOW this. Damage reuses the same 0.15 aero threshold. Fuel is
// fraction REMAINING at finish (length-sensitive, an MVP threshold). Now consumed by CardFlagsFrom.
constexpr double SPONSOR_TYREWEAR_MAX     = 0.55;   // max mTyreWear (worst corner) at finish for "low wear"
constexpr double SPONSOR_DAMAGE_THRESHOLD = 0.15;   // max component damage (0..1) for a "damage-free" run (== CLEAN_AERO_THRESHOLD)
constexpr double SPONSOR_FUEL_MIN_FRAC    = 0.05;   // min mFuelLevel fraction remaining at finish (an MVP threshold)

// ===== SPONSOR CARDS (spec 2026-07-11 — numbers fixed by the design prototype) =====
constexpr long   CARD_COST_BY_RARITY[5] = { 2500, 8000, 22000, 52000, 110000 }; // credits by rarity, local..elite
constexpr double CARD_SELL_PCT      = 0.40;   // sale price = 40% of what was INVESTED (purchase+upgrades), rounded to tens
constexpr double CARD_DISCOUNT_CAP  = 0.75;   // cap on accumulated discount per cost ROLL
// 19/09 (tester: +15 M in ONE race, 226,600 × 2.5 × 1.6 × 3 × 2 × 2.8): the prize multipliers that
// RAISE are no longer chained. Decision by Josh: they stack organically, with diminishing returns and no
// hard cap. They are sorted by how much they raise (v − 1) from largest to smallest; the best counts in full, the next one by
// this factor, the next by its square... (extra = Σ inc_i × DECAY^i). A single card pays what it prints.
// With 0.5 the total extra never exceeds double the best raise. Reductions (v < 1) keep multiplying.
constexpr double CARD_PRIZE_STACK_DECAY = 0.5;
constexpr int    CARD_SHOP_SIZE     = 3;      // base shop offer
constexpr long   CARD_REROLL_BASE   = 1200;   // credits for the first paid reroll
constexpr int    CARD_REROLL_GROWTH = 2;      // cost = BASE * GROWTH^paid_reroll (same shop)
constexpr int    CARD_MAX_BUYS      = 1;      // purchases per shop visit (shop.extra_buy raises it to 2)
constexpr int    CARD_MAX_SLOTS     = 6;
constexpr int    CARD_SLOTS_BY_PHASE[8] = { 2, 2, 2, 3, 4, 4, 5, 6 };  // index = frontier_phase (1..7; [0] = fallback)
// rarity weights per phase [local, regional, national, international, elite] — sum to 100
constexpr int    CARD_WEIGHTS_BY_PHASE[8][5] = {
    { 60, 30, 10,  0,  0 },   // [0] fallback (= phase 1)
    { 60, 30, 10,  0,  0 },   // phase 1
    { 48, 32, 16,  4,  0 },   // phase 2
    { 34, 32, 22, 10,  2 },   // phase 3
    { 24, 30, 26, 15,  5 },   // phase 4
    { 16, 26, 28, 21,  9 },   // phase 5
    { 10, 20, 28, 28, 14 },   // phase 6
    {  6, 15, 26, 32, 21 },   // phase 7
};

// Championship points (Living World) ---
// The prototype's POINTS table: F1-modern 25..1 top-10.
// PtsFor(pos) in career_scoring returns CHAMP_POINTS[pos-1] for pos in 1..10, else 0. This is the
// DISPLAY/standings-bonus table used by ChampStandings and the UI; period-correct per-series scoring
// uses the per-series PointsSystem (points_id) via ScoreRound/StandingsTable instead.
constexpr int CHAMP_POINTS[10] = { 25, 18, 15, 12, 10, 8, 6, 4, 2, 1 };

// Negotiation (leverage %, demands, sponsor chip) · Living World ---
// Pure maths live in career_negotiate.{h,cpp}; these are its tunables. Demand ids/costs + sponsor-delta
// table + salary curve are lifted verbatim from the design prototype.
// grown 4 -> 8 demands. The first 4 (0..3) are UNCHANGED (prototype order); the
// new 4 (4..7) wire to real systems — role/lead (objective pressure), damage-responsibility (Settle cost,
// bite scales with how much responsibility you accepted), calendar-freedom (less team support), easier-goals (NEGATIVE = refund, the
// inverse of harder_goals). Live-tuned (not user decisions), seeded from early design research.
constexpr int NEGO_DEMAND_COUNT = 8;
// Per-demand leverage cost (% of budget). Index = NegoDemandId. A NEGATIVE cost REFUNDS budget
// (harder_goals / easier-goals-refund: regain leverage by accepting more pressure). Matches
// NEGOTIATION_DEMANDS[].cost for 0..3; the 4..7 magnitudes are hand-picked starting values.
constexpr int NEGO_DEMAND_COST[NEGO_DEMAND_COUNT] = {
    22,   // 0 guaranteed_seat — guaranteed seat to season's end          (+cost)
    14,   // 1 anti_replace    — anti-replacement protection              (+cost)
    16,   // 2 duration        — extend duration to 2 seasons             (+cost)
   -18,   // 3 harder_goals    — accept tougher objectives (REFUND)
    20,   // 4 role/lead       — lead-driver role (raises objective/pressure)   (+cost)
    18,   // 5 damage-resp     — damage responsibility (lower bonus; bite scales with the lead-driver role) (+cost)
    16,   // 6 calendar-freedom— exclusivity/inscription freedom (less support)  (+cost)
   -16,   // 7 easier-goals    — softer objectives (REFUND, inverse of harder_goals)
};
// Salary as 3 DISCRETE packages: the UI feeds ONLY these 3 pct values into NegoSalaryCost
// (no continuous slider value reachable). more-coverage(10, refunds budget) / neutral(50, cost 0) /
// more-salary(90, costs budget). Index 0=more-coverage 1=neutral 2=more-salary.
constexpr int NEGO_SALARY_PKG[3] = { 10, 50, 90 };
// Over-reach relationship ding: when a deal is rejected AND rel < this floor, the over-reach
// dings the relationship signal (computed + shown + tested this phase; persisting needs the per-team
// store, which doesn't exist yet). 0.5 = the current rel stub midpoint.
constexpr double NEGO_RELDING_REL_FLOOR = 0.5;
// Sponsor-as-chip: % delta by sponsor tier rank 0..4 (local..elite). Local OFFENDS (negative).
// Matches sponsorDelta(): SPONSOR_TIERS rank -> [-12,-4,6,16,26].
constexpr int NEGO_SPONSOR_DELTA[5] = { -12, -4, 6, 16, 26 };
// Salary <-> coverage cost: salaryCost(pct) = round((pct-50)*0.5)  ->  -25..+25 for pct 0..100.
constexpr double NEGO_SALARY_K   = 0.5;    // slope of the salary cost curve
constexpr int    NEGO_SALARY_MID = 50;     // pct at which salary cost is 0 (50/50 split)
// Leverage budget: give = NEGO_K * (Want_T - NEGO_THETA) * (1 + competing).
constexpr double NEGO_K        = 60.0;     // converts the (Want_T-theta) gap into a % budget
constexpr double NEGO_THETA    = 0.5;      // the desire floor below which the team won't budge
// The surfaced % is dominated by the trial objective-completion ratio (4/4 >> 1/4). Blend:
//   margin% = NEGO_TRIAL_W * (trialRatio*NEGO_TRIAL_SPAN) + (1-NEGO_TRIAL_W) * give
constexpr double NEGO_TRIAL_W    = 0.6;    // weight of the trial ratio in the base margin (dominant)
constexpr double NEGO_TRIAL_SPAN = 70.0;   // a perfect trial ratio (1.0) is worth this many %
constexpr int    NEGO_MARGIN_MAX = 100;    // surfaced % clamps to 0..this
// Outcome bands of the FINAL margin (after demands/salary/sponsor spend): <0 reject, <WEAK weak, else strong.
constexpr int    NEGO_WEAK_THRESH = 14;    // margin in [0,14) = weak (take-it-or-leave-it); >=14 = strong

// Negotiation redesign (spec 2026-07-20) · asynchronous thread ---
// Binary gate: an offer with NegoImportance >= THRESH is negotiated by mail; below that, it's signed as-is.
constexpr int NEGO_IMPORTANCE_THRESH = 45;
constexpr int NEGO_IMP_LEVEL_PTS   = 14;   // points by series level (1..5 -> 14..70)
constexpr int NEGO_IMP_ECON_DIV    = 2500; // (signBonus + 10*perRace) / DIV, cap NEGO_IMP_ECON_MAX
constexpr int NEGO_IMP_ECON_MAX    = 30;
// bonus by economic type (index = bal::CONTRACT_*): works/conditional high, trial/pay low
constexpr int NEGO_IMP_KIND_PTS[5] = { 16, 6, -8, -20, 6 };   // WORKS,CUSTOMER,PAY_DRIVER,TRIAL,CONDITIONAL
// Personality derived from hash(team_id) — no persisted state.
constexpr int NEGO_DELAY_MIN = 3, NEGO_DELAY_SPAN = 8;          // response in 3..10 days
constexpr int NEGO_PAT_START_MIN = 40, NEGO_PAT_START_SPAN = 30;// base 40..69 by personality
constexpr int NEGO_PAT_WANT_GAIN = 30;                          // + wantT*GAIN (they want you -> they hold out)
constexpr int NEGO_PAT_COST_ROUND = 22;                         // cost per round sent
constexpr int NEGO_PAT_COST_OVERREACH = 18;                     // extra for overreaching (deficit > COUNTER_BAND)
constexpr int NEGO_PAT_COST_NEGLECT = 6;                        // per week sitting against them
// The agent's band (a noisy estimate, no oracle)
constexpr int NEGO_BAND_START_W = 34;                           // initial width
constexpr int NEGO_BAND_NARROW_PER_ROUND = 8;                   // narrows each round (information)
constexpr int NEGO_BAND_MIN_W = 12;                             // minimum width: never an exact margin
// Thread persistence: cap on saved history entries per negotiation (oldest drops off).
constexpr int NEGO_HISTORY_MAX = 12;
// Pure thread resolution: mood/form/urgency + counter-offer bands.
constexpr int NEGO_MOOD_SPAN   = 8;    // deterministic mood ±SPAN on the margin when resolving
constexpr int NEGO_COUNTER_BAND = 18;  // deficit in (0, BAND] -> counter-offer (if patience remains)
constexpr int NEGO_INSULT_BAND  = 40;  // deficit > INSULT -> walks away outright (a serious insult)
constexpr double NEGO_FORM_GAIN = 25.0;// margin points per point of CareerForm delta
constexpr int NEGO_CONCESSION_PCT[3] = { 0, 6, 12 };  // generosity by personality
constexpr int NEGO_URGENT_DAYS = 21;   // within <21 days of round 1: urgency (patience drains x2)
constexpr double NEGO_DESPERATE_WANT = 0.95; // wantT above this + urgency -> a sweetener is possible
// Thread mutations: a signing reservation after ACCEPTED_PENDING + a one-time TTL grace period when the
// computed respond_day doesn't fit before the deadline.
constexpr int NEGO_SIGN_RESERVE_DAYS = 7;
constexpr int NEGO_TTL_GRACE_DAYS = 3;

// Contract archetypes · per-archetype negotiationLevel gate ---
// Pure archetype resolver lives in career_archetype.{h,cpp}; this is its negotiationLevel tunable.
// negotiationLevel gates which demands are askable: 0=none, 1=medium, 2=high. Index = ContractArchetype
// enum order (career_archetype.h): SELF_FUNDED_ENTRY=0, CUSTOMER_SEAT, OPEN_VACANCY, DIRECT_OFFER,
// JUNIOR_PROGRAM, FACTORY_PROGRAM, WILDCARD, RENEWAL, BRIDGE_CONTRACT (9 values 0..8).
// By design: none(0) = self-funded/wildcard/junior · medium(1) = customer/open-vacancy/bridge ·
// high(2) = direct-offer/renewal/factory.
constexpr int NEGO_ARCH_NEG_LEVEL[10] = {
    0,   // 0 SELF_FUNDED_ENTRY — none
    1,   // 1 CUSTOMER_SEAT     — medium
    1,   // 2 OPEN_VACANCY      — medium
    2,   // 3 DIRECT_OFFER      — high
    0,   // 4 JUNIOR_PROGRAM    — none
    2,   // 5 FACTORY_PROGRAM   — high
    0,   // 6 WILDCARD          — none
    2,   // 7 RENEWAL           — high
    1,   // 8 BRIDGE_CONTRACT   — medium
    2,   // 9 ACADEMY_PROMOTION — high (the parent team signs you; no rookie haggling)
};

// Aging / advancing world · Living World ---
// Pure aging maths live in career_fields.{h,cpp}; these are its tunables.
// Age = current_year - birth_year. The curve is a skill MULTIPLIER on base (1.0 = peak).
constexpr int    AGE_PEAK_LO        = 27;     // start of the plateau (single-seaters peak earlier; see AGE_GT_SHIFT)
constexpr int    AGE_PEAK_HI        = 32;     // end of the plateau
constexpr int    AGE_RISE_FROM      = 18;     // below this the driver isn't on the grid yet (clamp)
constexpr double AGE_RISE_FLOOR     = 0.82;   // multiplier at AGE_RISE_FROM (rookies start below ceiling)
constexpr int    AGE_DECLINE_KNEE   = 38;     // gentle decline PEAK_HI..KNEE, then accelerates
constexpr double AGE_DECLINE_GENTLE = 0.004;  // per-year drop, plateau-end..knee
constexpr double AGE_DECLINE_STEEP  = 0.020;  // per-year drop, past the knee
constexpr double AGE_MULT_FLOOR     = 0.55;   // a veteran never drops below this fraction of peak
// (RETIRED) the per-archetype rival_form drift (ParamsForArchetype) no longer exists: the age
// curve now only feeds retirement/promotion (agedRating) and AgeAdjustedSkill; the cast's skill is innate.

// Modern/historic world split ---
// A series whose era (SeriesDef.era) parses to a 4-digit year >= this is MODERN-ladder (renders on the modern
// world board); an older year is a bygone-era memory (excluded from the modern ranking). Content with no
// parseable era year (fictional/timeless Reiza formulas, karts) is treated as modern. Tuned to split the
// spec lists: modern 2020/2021/2024 vs memory up to 2005 — 2015 sits cleanly in the gap.
constexpr int    MODERN_ERA_CUTOFF  = 2015;
// Upper alive bound for the active-modern filter: a driver older than this is treated as no-longer-active
// (mirrors the procedural retire window so the board never shows a centenarian). RETIRE_AGE_BASE + SPREAD.
constexpr int    ACTIVE_MAX_AGE     = 43;     // = RETIRE_AGE_BASE(40) + RETIRE_AGE_SPREAD(3)

// Retirement (hybrid) ---
constexpr int    RETIRE_AGE_BASE    = 40;     // "retirement" age reference (base for the per-class age_shift)
// A HARD age cap, no longer the criterion. Retirement is decided by LEVEL (if you don't meet your
// category's bar -> you drop down; if you don't meet any -> you're out). This only prevents the absurd, and should fire
// almost never: if it fires a lot, the cascade isn't doing its job. The class's age_shift is added to it,
// so an endurance gentleman driver (+12) can hold on until 67, and a
// single-seater driver until 55 — roughly what happens in real life.
constexpr int    RETIRE_AGE_HARD    = 55;
constexpr int    RETIRE_AGE_SPREAD  = 3;      // deterministic id-hashed spread so they don't all go at once
constexpr double RETIRE_RATING_FLOOR= 1080.0; // OR retire when aged rating falls below this floor
// AI driver contract duration, in years. Staggered so the
// whole world doesn't expire at once: the concrete duration comes from a deterministic hash of (seat, occupant)
// within this range. See career_seats.h::ContractExpiryYear.
// (RETIRED) this used to hold AI_CONTRACT_LEAVE_PER_MILLE: "out of every 1000 expiring contracts,
// how many DON'T renew" — a deterministic 25% roll. That earlier iteration left it as a known debt -- "WHO
// renews and who doesn't is a later decision" -- and now it's no longer a dice roll but PERFORMANCE against the
// bar for their category (career_seats.h::ClassDemand). The constant is left with zero callers, so
// it goes: a balance constant that governs nothing is a lie waiting for someone to read it.

constexpr int    AI_CONTRACT_MIN_YEARS = 1;
constexpr int    AI_CONTRACT_MAX_YEARS = 3;

constexpr int    RETIRE_MIN_AGE     = 34;     // never retire-by-rating below this age (a slump isn't a career end)
constexpr double RETIRE_REPLACE_RATE = 0.08;  // fraction of real drivers replaced by generated ones each season

// Promotion (freed seat -> best climber) · ---
constexpr int    PROMOTE_TIER_STEP  = 1;      // a freed seat promotes a climber one tier up
constexpr double PROMOTE_MIN_RATING = 1150.0; // a climber must clear this aged rating to take a vacated seat

// How many tiers ABOVE where they already race a REAL driver in the world can aspire to
// (career_seats.h::WorldOccupants -> tier_max). ZERO, and that's a design decision, not a limitation:
//
//   the spec's rule: "Nobody climbs above their history: a real driver's ceiling is their current category, their
//   arc only goes down or out."
//
// A real driver is where their history put them. What DOES have an upward arc is the cohort -- their
// tier_max comes authored in series_roster.psv, and rollover seeds it into the cast BEFORE it seeds the world's
// census, so it keeps its ceiling -- as do the generated rookies, who get theirs from the power-law distribution.
// Raising this to 1 would give the ~900 real drivers a promotion they don't deserve and would steal the spot from the
// two populations that are actually meant to climb.
// A DELIBERATE DESIGN DECISION, and it contradicts the spec on purpose: it said "nobody climbs
// above their history: a real driver's ceiling is their current category." Measured, that rule was thinning out the
// world -- the upper rungs could only be filled by the 19-strong cohort and rookies as they grew,
// and that pipeline takes ten seasons to produce its first result. With one tier of headroom, a
// proven real driver can make the jump they'd make in real life. One tier, not seven.
constexpr int    CASCADE_CLIMB_STEP = 1;

// THE LEVEL LADDER, IN RATING POINTS. It didn't exist, and measuring made that obvious:
//
//   tier 1 (karting)    bar 1521      <-- karting demanded MORE
//   tier 7 (Pemberton_Zel121/elite) bar 1470          than the elite did
//
// The cause is in the data. career_fields.cpp::CurveSkill derives a driver's skill from their ORDER within
// their OWN grid, mapped to their profile's AI range -- and NO profile defines a range (there isn't a single
// ai= key anywhere in series_config_profiles.psv), so all 115 series use the default 85-100. Karting's
// ace and F1's ace came out with the SAME number, and the only thing separating the tiers was an
// artifact: the age curve of their occupants (karting full of kids at their peak, the elite full of
// veterans past theirs). That's why it came out backwards.
//
// A class's spread is (100-85)/120 * (1900-1200) = 87.5 points: that's what separates its ace
// from its backmarker. One RUNG of the ladder is worth 60% of that spread, meaning two consecutive
// categories overlap by 40%: F2's best can beat F1's worst, but a GT3 ace lands
// at the VERY BOTTOM in F1. That's the missing transfer function ("their 99 in GT3 becomes a 70
// in F1").
constexpr double RATING_CLASS_SPREAD = 87.5;                    // (100-85)/120 * (TOP-BOTTOM)
constexpr double RATING_TIER_STEP    = 0.60 * RATING_CLASS_SPREAD;   // 52.5 points per rung
constexpr double RATING_SKILL_FLOOR  = 85.0 / 120.0;            // backmarker's skill (base of the curve)

// HOW FAR you can fall below your category's average before people stop
// counting on you (career_seats.h::ClassDemand). It's the criterion that replaces "retires at age
// N": a driver loses their seat when they no longer match the level of the people they race against, and age is
// the CAUSE of losing it, not the criterion itself.
// IN UNITS OF A CLASS'S SPREAD, which is the scale where that statement means something: 0.8 = you're
// out when you're almost a full spread below your peers' average. In absolute
// points it would mean nothing, because a spread is 87 points and the whole ladder is 315.
constexpr double CLASS_DEMAND_MARGIN = 0.80 * RATING_CLASS_SPREAD;   // 70 points
// ~~~02/09 — THE MARGIN OF THE , SEPARATE FROM THE CLASS BAR. Same value, zero change of
// behavior: it is separated so it can be MEASURED. It is the real brake on the occupied-seat market --
// of the ~36k candidacies that get as far as it, 26k die here and only 11 end up in a change of incumbent.
constexpr double SWAP_MARGIN = CLASS_DEMAND_MARGIN;

// (12/08) — HOW MUCH PERFORMANCE CAN COST YOU IN THE DECISION OF WHETHER YOU CONTINUE.
//
// Until today the only way to decline was HAVING BIRTHDAYS: `agedRatingFor` is authored capability x age curve
// and didn't read results, because until the world started running there were no results to read. Measured:
// 402 drivers clinging to seats above their level at twenty seasons.
//
// It is tied to `CLASS_DEMAND_MARGIN` on purpose, and in its same unit: the cap is EXACTLY the class's
// tolerance, so the sentence that defines the number can be said out loud — «performing below what you
// promise can cost you, at most, the same as what the class forgives you». With a larger cap, a bad
// season would throw out someone who is still above the average of their peers; with a smaller one, a
// sunk driver would never get below the bar and the decline channel would stay closed.
constexpr double FORM_RATING_POINTS_CAP = 1.00 * CLASS_DEMAND_MARGIN;   // 70 points

// — THE PERSONAL ARC. How much each season a driver's progress moves TOWARD THEIR CEILING.
//
// `GROWTH_STEP_BASE` is copied from the pace of the universal curve it replaces: the old ramp rose from
// 0.82 to 1.00 over the nine years from 18 to 27, that is 0.02 per season. Starting there is what makes the
// change one of SHAPE and not of SPEED: people still take the same time to reach their ceiling, only
// now each by their own path.
//
// `GROWTH_BEND` is how much circumstance weighs, and the 2.0 comes from decision 2 of («circumstance
// weighs HALF»): with this value, half a bad season (circumstance −0.5) leaves the progress at ZERO —the
// stagnation the spec describes— and a fully bad season makes them regress at the same speed
// at which they would have risen. Symmetric, and with no magic numbers to explain.
constexpr double GROWTH_STEP_BASE = 0.020;
// ~~~07/09 — D2, DECISION BY JOSH: DEVELOPMENT STOPS READING RESULTS. It was 2.00. «Maybe what we shouldn't do is
// make the AI progress according to its performance; we should make it progress
// jointly, like in FIFA: the player progresses, period.» The card grows along the age curve toward its ceiling;
// what you perform is judged by the ELO and decides the SEAT (ExpireSeatContracts, pending in career_seats), not
// development. Measured with `g_growthBendMedicion` on three seeds and twenty seasons before touching it: generated
// aces reaching the queen 18 -> 25-33, equal relegations (the age decline already does the work
// `circ` did: the 12/08 fear doesn't show up), youngsters without sinking (ELO p50 1165 -> 1175). And it dissolves the
// circularity of «we penalized him for being where we had put him». The mechanism
// (`circ`, the factor, the floor) stays whole behind this constant so it can be measured; at 0 it is inert.
constexpr double GROWTH_BEND      = 0.00;
// ~~~08/09 (D3, decision by Josh): HOW MUCH OF THE AGE CURVE REACHES THE SIX QUALITY KNOBS
// (consistency, wet, tyres, stamina, mistakes, forced errors). The knobs are scaled by
// `(1 - peso) + peso x curva`, with the SAME `AGE_*` curve as talent: a single shape for the seven axes.
// It was measured before choosing it (145 anchors with age at their pack's era): NO knob peaks later than
// pace; consistency, racecraft and mistakes FALL with age (r -0.17..-0.25) and collapse at 40+, and
// tyres is flat. The shape «craft peaks late» is not in the data, so there is no per-axis curve.
// The weight does NOT come from a measure of the world -- the cost in aces reaching the queen is a step of 3-5 with
// any weight > 0 and doesn't grow with it (sweep 0 / 0.25 / 0.5 / 1, two seeds) -- but from the card: with
// 0.5 an 18-year-old rookie carries the six knobs 5-10 points below their pace and converges at 27; with 1,
// in GT3 tyres fell 20 points because of how tight the family is, not because of the driver. At 0 it is inert
// (what there was until 08/09). `g_traitCurveMedicion` (>= 0) overrides it only to measure.
constexpr double TRAIT_CURVE_WEIGHT = 0.50;
// ~~~08/09 (pool origin category, spec E7): does the curated origin class also rule over
// AFFINITY (bounds which families the person can move to)? Josh decided «read it to seat them and for their
// affinity», and the seating (career_seats::PersonaRealLibreEn) went in that same day; this half stayed
// off until he saw the measurement. DECIDED by Josh on 16/09 (card 13): ON. true = the origin class's affinity
// ahead of that of the series where the person has landed.
// MEASURED 08/09 (three seeds, twenty seasons) and re-measured on 16/09 in this tree: see
// `career_seats::g_afinOrigenMedicion`, the hook with which the test bench turns it off to compare
// (`career_test.exe --afin-origen 0`). What it costs is written in the lane's report; in short, the
// origin class bounds the movements of the 1,488 curated people and thins the flow upward.
constexpr bool POOL_ORIGIN_AFFINITY = true;
// ~~~16/09 — HERE WE TRIED YIELDING THE DISCIPLINE RATHER THAN LEAVE A SEAT EMPTY, AND IT WASN'T NEEDED. The
// suspicion was that affinity, demanded in the three passes of `career_seats::SealLiveGrids`, was a veto that
// left the seat empty when the pool of the right discipline ran out. It was implemented as a fourth pass and
// measured: ZERO seats signed in twenty seasons, and the world with fallback came out byte-for-byte identical
// to the hard filter's. The empty seats came from elsewhere (the seal can't fill a level-1 seat, with
// discipline or without it) and were fixed there: see the second rookie pass in `RolloverYearReport`
// (career_calendar.cpp). The fourth pass was removed for never executing. It is left written so it isn't
// tried again without first looking at where the gaps come from.
// ~~~08/09 (pool origin category): do legends with an «era»/«retired» note stay OUT of the registry's
// reserve (career_seats::ReservaLibrePara)? It is the path by which 60-72 of them entered level 1
// every five winters. Measured with the curated file, three seeds: closing it leaves 180 fewer seats with a
// real name (real ones with a seat 1259 -> 1083) that go to generated rookies (298 -> 483), 4 > 5 in two
// seeds and high arrivals at the base 33/24/17. The curation says those people belong to their era; the world
// says that without them there are 180 more invented ones. DECIDED by Josh on 08/09: ON -- «if they are era or
// retired shouldn't be there». The cost (more generated) is temporary: it is covered when the seat market
// of C3 lets the 413 people with curated origin compete at levels 2-7 for the seats of their level.
constexpr bool POOL_ORIGIN_LEGENDS_STAY_OUT = true;
constexpr double GROWTH_FACTOR_MIN = -1.0;   // the fastest possible decline = the rise, in negative
constexpr double GROWTH_FACTOR_MAX =  2.0;   // and a great season moves you ahead twice as much, no more
// ~~~02/09 — the factor's floor WHILE YOU RISE (before AGE_PEAK_LO). With -1 a bad season
// un-developed a 20-year-old; with 0 it left him frozen, which in practice is the same because the
// weakest of his grid can never get a positive `circ`. 0.35 = even if you do badly, you keep
// learning at a third of the pace. After the peak it doesn't apply: there the decline is the right answer.
constexpr double GROWTH_FACTOR_RISING_MIN = 0.35;

// — WHAT A DRIVER COSTS WITHIN THEIR CATEGORY, AND THE PROPORTION IS THE REAL-LIFE ONE.
//
// The first version used the team band (0.60-1.60), meaning the BEST of a category cost
// 2.67 times the WORST. With that compression money decided nothing at the top: measured, a level-7 ace
// asked ~213k and even the poorest F1 team had 282k, so at level 7 everybody could
// pay everybody and the economy only pinched at the bottom.
//
// In reality the distance is enormous and within the SAME championship: in today's F1 the best paid
// takes in the order of fifty million and a debutant one or two — thirty or forty times. That is the
// proportion to reproduce, not a compromise 2.67.
//
//   price = level scale x [MIN + (MAX-MIN) * (their place within the level)^POWER]
//
// The exponent 3 is what makes only the TOP expensive: the bulk stays cheap (mid-table comes out
// x0.7) and the price shoots up in the last stretch, which is how a market of stars behaves.
//
// AND THE CAP IS NOT CHOSEN BY EYE: IT HAS TO EQUAL THE HIGHEST BUDGET THAT EXISTS, no more, no less.
// The richest possible team is F1 (scale x2.50) at the head of its championship (band x1.60) = x4.00 of the
// scale of its level. If the maximum price were higher, the best driver in the world COULD NOT BE PAID BY ANYONE and
// would end up falling to the emergency pass — measured with a cap of x8: an ace asked 1.26M against the 752k
// of the richest team, and the unfilled seats rose from 12 to 22. Matching it, the star is affordable
// EXACTLY for the one at the very top, and for nobody else: the richest HARBOR_CHALLENGE_8227 reaches x2.08 and falls
// halfway short. That is what concentrates the aces where they can really be paid.
constexpr double MARKET_PRICE_MIN   = 0.25;   // the tail-ender of its category, cheap
constexpr double MARKET_PRICE_MAX   = 4.00;   // = F1 scale (2.50) x champion band (1.60). 16x the tail-ender
constexpr double MARKET_PRICE_POWER = 3.0;    // only the top shoots up
// The MARKET's own cap, distinct from SALARY_CAP (which governs the player's salary and must not move because of
// this). If they share a cap, the 300k one trims all the stars to the same number and the problem returns.
constexpr long   MARKET_PRICE_CAP   = 2000000;

// How many rungs a driver who doesn't get renewed can fall before running out of world. Two, not
// seven: the spec's rule, "from F1 you drop to GT/prototypes, not to karting". If neither of those two rungs has
// a single seat whose bar they clear, they retire -- the model's only real way out.
constexpr int    CASCADE_DEMOTE_MAX_STEPS = 2;

// ~~~29/08 — HOW MANY WINTERS A FREE AGENT LASTS BEFORE GIVING UP. User's idea: whoever loses their
// seat doesn't leave the sport that same night. They stay available and keep competing for seats; only
// when they chain this number of winters with none, they retire. Measured before it existed: 19 people
// every five seasons left because there wasn't, THAT winter, a gap that fit them.
// 2 and no more: after three seasons out nobody comes back, and a pool that never empties ends up
//  giving seats to people the player doesn't remember seeing race.
constexpr int    FREE_AGENT_MAX_IDLE = 2;

// ROOKIES, the world's only WAY IN. Without them, ~15 drivers leave every season and nobody
// comes in: measured over ten seasons, 940 -> 862 distinct occupants and 140 open seats that
// never get filled. And with nobody entering from the bottom there's no upward flow either, so the bar for the
// upper categories only rises -- the flattening effect this is meant to check against.
// How many rungs someone can climb at once when a vacancy pulls them up. One: you're promoted
// rung by rung, not from Formula Vee straight to F1 just because a slot opened up.
constexpr int    CASCADE_PROMOTE_MAX_STEPS = 1;

// DISCIPLINE AFFINITY. Without this, the cascade moved people between worlds that don't
// touch each other: caught in the transfer report — "Horst Felbermayr: KEYSTONE CUP 2200 -> Lantern Cup 2487" and
// "Vicente Saez-Merino: NIMBUS CHAMPIONSHIP 2159 -> Kingsmere Kor964". A GT driver doesn't end up in a junior single-seater.
//
// The axis already existed, authored but unused: docs/career/f3_authored_profiles.psv carries affinity_family for
// 1634 of 1634 rows -- touring, openwheel, gt, prototype, oval, historic.
//
// A FROM (row) TO (column) matrix; 1 = a plausible move. It's the "small, curated table of
// destinations" the spec called for ("from F1 you drop to GT/prototypes, not to karting").
//   openwheel -> gt/prototype is the classic path (Gené from Dunfield Racing 6889 to prototypes, Mazzacane from
//     F1 to GT1). NOT the other way around: you get into single-seaters young, you don't go back at 38 -- the
//     openwheel column is only opened by openwheel and historic. Caught by the transfer report: with
//     prototype->openwheel set to 1, it produced "Ben Hanley: LANTERN_MASTERS_7489 Redham Motors Fal933 -> Onyx Trophy 2405", which never happens.
//   oval is a world apart and only connects with touring cars (stock cars are touring cars with bodywork).
//   historic accepts anyone: a historic championship runs with whatever's available.
constexpr int    AFFINITY_COUNT = 6;   // touring, openwheel, gt, prototype, oval, historic
constexpr int    AFFINITY_OK[AFFINITY_COUNT][AFFINITY_COUNT] = {
    //            tou  ope   gt  pro  ova  his
    /*touring  */ {  1,   0,   1,   0,   1,   1 },
    /*openwheel*/ {  1,   1,   1,   1,   0,   1 },
    /*gt       */ {  1,   0,   1,   1,   0,   1 },
    /*prototype*/ {  1,   0,   1,   1,   0,   1 },
    /*oval     */ {  1,   0,   1,   0,   1,   1 },
    /*historic */ {  1,   1,   1,   1,   1,   1 },
};

// A DELIBERATE DESIGN DECISION: "fill however many gaps are needed." The spec said
// tiers 1-2, and with that the upper gaps the cascade couldn't fill stayed open
// forever. The pull-up runs BEFORE the rookies, so almost everything that reaches here is at the bottom
// anyway; if something's still open at the top, it means nobody below had the level, and then someone new debuts
// there -- which also happens in real life (a Verstappen doesn't start out in the game world's karting).
// Up to which tier THE COHORT (your generation) occupies seats. Above that, the tail end of
// short grids is covered by drivers DERIVED from each series itself, not the same 28 repeated over and over.
//
// Without this, the 41 series whose field is shorter than their grid all shared the same cohort: 1.74
// seats per head and 23 drivers appearing in more than two tiers at once -- exactly what the phase's
// closing criteria explicitly forbids. It's also what the redesign called for: "the
// cohort concentrates at the BOTTOM, it isn't spread evenly... if it's plugging census holes it stops being the
// player's generation and becomes filler."
// 11/08 — TIER 1 IN SEASON 1. User decision, in their words: «in the first season the
// cohort is only in tier 1 (...) in the following seasons it will move through the different
// contracts competing with you». It was at 2, and that spread them over 206 seats at two levels; at tier 1
// they are the people you start with, and from there they climb through the cascade like everyone else.
constexpr int    COHORT_HOME_MAX_TIER = 1;

// USER DECISION (11/08) — WHERE THEY SIT AND HOW MUCH THEY ARE WORTH ARE TWO THINGS, and until today they were the
// same constant. Their words: «in the first season the cohort is only in tier 1 (...) but wherever
// they are they'll be among the BEST drivers on the grid. Is the whole cohort good? No, but from here come
// those destined to be champions someday».
//
// With a single constant that can't be expressed: anchoring them to the level where they sit turns them into the
// AVERAGE driver of that level — measured on 11/08, 52nd percentile in their own grids, i.e. the generation
// of the pack. The worst One Piece generation is not pack people in their own category.
//
//   COHORT_HOME_MAX_TIER  the highest tier where the seat layer sits them       (PLACEMENT)
//   COHORT_SEED_LEVEL     the level to which their rating is anchored, and may be HIGHER   (LEVEL)
//
// With home at 1 and anchor at 2: the best of the cohort comes out above the best of a level-1 grid
// (1425 against 1325) and the worst falls in its rear third (1075 over a band 975..1325) — which is
// exactly «not all are good, but from here come the champions».
// MEASURED on 11/08 and that's why it is 3, not 2. With anchor at 2 —the midpoint of their home tier— the
// cohort comes out at the 63rd percentile of its OWN grid and leads no class out of 100: it is the generation
// of the pack. With anchor at 3 it comes out at 28% (front third) and leads 7 of 100, with no cost in the fan
// (classes with u>2 stays at 14). With anchor at 4 it leads 15 and starts to resemble the problem that the
// .5 fixed (it led 48). The 3 is «very good level wherever they go» without taking over the ladder.
// GOES BACK TO 2 on 11/08, and the history matters so it isn't repeated. It was raised to 3 because the cohort came out at
// the 63rd percentile of its own grid — but that was NOT the anchor: it was an ORDER bug in the seeding (the
// seat pass ran before the cohort one and stole its anchoring). With the bug fixed, with anchor
// at 2 the cohort sits at a 45% median with its BEST above the whole grid (1425 against 1325) and
// its WORST mid-table — which is exactly what the user asked for: «is the whole cohort good? no, but from
// here come those destined to be champions someday». With 3, not even the worst fell below the front third.
//
// And let it be said what this knob CANNOT fix: `CohortAnchoredSeed` keeps only the ORDER of the cast
// and throws away the authored MAGNITUDES (it spreads 28 people uniformly over the band). The curation of «this one is
// a future champion and this one a back-marker» is lost there. That is fixed by the attribute model of , not by a
// constant.
constexpr int    COHORT_SEED_LEVEL    = 2;

constexpr int    ROOKIE_MAX_TIER  = 7;
// A DELIBERATE DESIGN DECISION -- LIFE HAPPENS. Besides age and level, every season some
// people simply walk away: they run out of money, get injured, find a job, lose interest.
// And it happens FAR more at the bottom than at the top: in entry-level categories half the grid is amateurs paying
// out of pocket, and in the elite nobody walks away from an F1 seat out of boredom. Per thousand seats per season,
// indexed by tier (1..7; [0] is unused).
// Deterministic by hashing (career seed, year, seat), like everything else in the engine: "almost
// random" for the player, reproducible for debugging.
constexpr int    ATTRITION_PER_MILLE[8] = { 0, 80, 60, 40, 25, 15, 8, 4 };

constexpr int    ROOKIE_AGE_MIN   = 17;
constexpr int    ROOKIE_AGE_MAX   = 22;
// Exponent of the talent power-law: u^N with u uniform in [0,1). The higher it is, the more the
// crop bunches up at the bottom and the fewer stars come out. At 3, one in eight clears 50% of the band, and
// one in a thousand reaches all the way to the top -- which is roughly how a real generation turns out.
constexpr int    ROOKIE_TALENT_POWER = 3;

// Era memories + legacy · Living World ---
// Pure memory logic lives in career_memories.{h,cpp}; these are its tunables.
// Level-unlock metric (an open item): a memory unlocks at a RATING threshold on the player's
// unified career rating (career_rating). gen 0 / 1 / 2 = progressively higher bars; tune freely.
constexpr double MEM_UNLOCK_RATING[3] = { 1150.0, 1450.0, 1700.0 }; // gen 0=from start, 1, 2
// Replay: the annual money-only payout is a fraction of first-clear inheritance, performance-scaled.
// performance factor in [MEM_REPLAY_MIN_MULT, MEM_REPLAY_MAX_MULT] by how well you met the objective.
constexpr double MEM_REPLAY_MIN_MULT = 0.50;   // objective barely met
constexpr double MEM_REPLAY_MAX_MULT = 1.30;   // crushed the objective (P1 in the period grid)
// Period grid: when gridFlavor=="cohort", privateer fillers added on top of the named legends to
// reach grid_size; the ancestor is one of the privateers. Pure grids ("pure") add zero fillers.
constexpr int    MEM_COHORT_GRID_SIZE   = 20;  // target field for a cohort memory (legends + privateers)
constexpr int    MEM_COHORT_MIN_FILLERS = 4;   // always at least this many privateers (incl. the ancestor)
// Period-driver rating seeds: legends seat near the top of the era scale; privateers fill the tail.
constexpr double MEM_LEGEND_TOP    = 1900.0;   // best legend in the period grid
constexpr double MEM_LEGEND_BOTTOM = 1600.0;   // weakest legend
constexpr double MEM_PRIVATEER_TOP = 1450.0;   // best privateer (the ancestor's ceiling slot)
constexpr double MEM_PRIVATEER_BOT = 1150.0;   // backmarker privateer

// Cleanliness -- a real safety EMA sourced from live telemetry ---
// The cleanliness model itself is pure (an EMA in career_engine.cpp plus the Want_T term in career_market.cpp); only the telemetry
// incident latch lives in the overlay. Cleanliness is 0..100 on CareerProgress (additive; 0.0 in old
// saves -- seeded to ROOKIE_SEED on first use). EMA: clean <- clean + ALPHA*(100*cleanRace - clean).
// Tuned empirically -- not a user-exposed setting.
constexpr double CLEAN_ALPHA                 = 0.2;    // EMA smoothing toward 100*cleanRace
constexpr double CLEAN_ROOKIE_SEED           = 70.0;   // rookie / fresh-save starting cleanliness
constexpr double CLEAN_TEAMINTEREST_FLOOR    = 55.0;   // below this, cleanliness softly lowers teamInterest (Want_T)
constexpr double CLEAN_ACCIDENT_TRIGGER_FLOOR= 45.0;   // below this, the gray-zone "coming off a string of incidents" trial trigger fires
constexpr double CLEAN_AERO_THRESHOLD        = 0.15;   // mAeroDamage (0..1) above this counts as an incident at Settle

// No-show penalty -- the organic missed-race limiter ---
// Per-contract missed-round counter; at this many missed CONTRACTED rounds the contract auto-BreakContract.
// The REP-floor fix (career_season.cpp) keys off the same missed-round signal (a true no-show season may
// land <=0 REP instead of flooring at +1). RATING_NOSHOW_DING (above) is already plumbed via DnfKind::NoShow.
constexpr int    MK_NOSHOW_BREACH_LIMIT      = 3;      // missed contracted rounds -> auto-BreakContract

// Silly season -- seats freed up midway through the season ---
// Without this, a new career goes through the entire first year without a single open seat (seat_occupant empty =>
// the roster fills everything and GenerateOffers skips every series) => an empty market and no offer emails until
// the first rollover. Deterministic by (career_seed, year, week), no RNG.
constexpr int    MK_SILLY_WEEK_PCT = 30;   // % of weeks that free up 1 seat (~1 every 3 weeks)
constexpr int    MK_SILLY_OPEN_CAP = 6;    // cap on simultaneously open seats across the whole board
// Startup signing window: a new career begins mid preseason -> the world
// already has a few vacancies in the entry tiers (like real life: seats fill up before the
// season opener). Without this, a rookie putting in good races still wouldn't see a single offer until the rollover.
// When the career is created, the entire entry ladder opens up, not a random handful.
//
// Previously N seats opened from a mixed pool of tier 1 and 2, offset by career_seed, so out of
// the twelve tier-1 series only five would show up, and which ones depended on the seed. Obvious entry-ladder series were missing --
// Brackenridge Wyn710, Aurora Trophy 5275, both Formula Vee classes -- and the player started out unable to even see them.
//
// The new rule isn't a number: it's that an entry-level category always takes on new people, which is what
// makes it an entry category. So they all open a seat. What stops you from "signing them all" isn't that they aren't offered: it's the
// real cost of it -- calendar clashes, what each entry costs, and exclusivity if someone demands it of you.
//
// On top of that, a few tier-2 seats act as bait: those ARE contested (they're above your current stage) and you
// lose them to the cohort, which is where the "so close" feeling of your first decision week comes from.
constexpr int    MK_PRESEASON_ASPIRE_SEATS = 3;   // tier-2 seats opened in addition to the full entry tier
constexpr int    MK_PRESEASON_MAX_LEVEL  = 2;   // entry tiers only (level <= 2)
// per-class preseason signing window. It opens MK_PRESEASON_LEAD days before the
// opener (round-1 day-of-year) of each series; within it, that class's churn quota is released.
constexpr int    MK_PRESEASON_LEAD = 28;   // 4 weeks before the class's opener
// The market's calendar, in month and day. Season runs through August 31,
// (paddock, no seats in play), signing window from September 1 to December 14, and the decision
// week on the 15th. It's expressed as month-and-day rather than day-of-year on purpose: the clock is a real Gregorian one
// (career_dateutil.h), so a hardcoded day-of-year would drift by a day every leap year.
// By date, and globally -- not per series: with multiple contracts on different calendars, a window relative to each
// series' own opener (MK_PRESEASON_LEAD, above) produced a tiny window per series and was unreadable.
// Cap on the number of seats sealed when the window opens. Explicitly requested: "few and
// readable, not 60" -- because a board with hundreds of entries isn't a silly season, it's a shopping list. With
// staggered 1-to-3-year contracts, close to a third of the world expires every year (~550 of 1656 seats),
// so without a cap the window would be unreadable. It's not a blind truncation: candidates are ordered by
// proximity to the player's level, keeping the most relevant ones -- what's meant by "within your reach".
constexpr int    MK_WINDOW_SEAT_CAP   = 24;
// Quota of aspirational seats (above your level) that the window always reserves.
// Without this, the silly season had nothing worth fighting for. Ordering by proximity put what's at your
// own level first (distance 0), and a seat at your own level is never contested, per the on-ramp rule
// (SeatIsAspirational only kicks in if level > frontier). Since your own level alone produces more than 24 candidates,
// the cap ran out before reaching even one seat above you. Measured with the simulator over six years: of
// 126 applications, only 4 ever got judged, and all 4 landed in the first December -- after that the
// world said yes to everything. The quota doesn't touch the on-ramp (you still keep 24-N guaranteed seats at
// your level): it just guarantees there's always something above worth earning.
constexpr int    MK_WINDOW_ASPIRATIONAL_MIN = 8;   // out of 24
// How many seats approach you purely by level proximity, with no history involved. It's deliberately a low cap:
// being sought out has to be rare or it stops being news, and with the window sealing up to
// 24 seats, approaching half of them would turn "they want you" into the default state. Seats that DO have history
// with you are handled separately and without a cap -- a real reason doesn't compete for quota.
constexpr int    MK_WINDOW_SOUND_CAP  = 3;
constexpr int    MK_WINDOW_OPEN_MONTH = 9;   // September 1: signing window opens
constexpr int    MK_WINDOW_OPEN_DAY   = 1;
constexpr int    MK_DECISION_MONTH    = 12;  // December 15: decision week, everything resolves at once
constexpr int    MK_DECISION_DAY      = 15;
// Year 1 runs the same cycle, compressed. A new career begins in preseason and
// no AI contract expires in its first year (they run 1 to 3 years from the start), so with the
// normal calendar the player would wait until September to see their first silly season. This is
// resolved by giving year 1 its own decision week in February: exhibitions in January, you decide in
// February, and enter your first season with seats already contested. From year 2 onward, the normal calendar applies.
// And the career's start date isn't moved up, which was the other possible fix: that wouldn't work,
// because contract expirations are anchored to that same start year and would shift right along with it.
constexpr int    MK_Y1_DECISION_MONTH = 2;   // February 15: year 1's decision week
constexpr int    MK_Y1_DECISION_DAY   = 15;
// How long the decision week lasts. In December it doesn't matter (it runs into year-end anyway), but the February one has to
// close so the year can return to the paddock and the season can start.
constexpr int    MK_DECISION_WEEK_DAYS = 7;
// churn quota of seats freed per class and season (CLOSED grids). The real quota is
// max(MK_CHURN_MIN, round(grid * MK_CHURN_PCT / 100)); bounded by the global MK_SILLY_OPEN_CAP.
constexpr int    MK_CHURN_PCT = 15;        // ~15% of the grid turns over per season
constexpr int    MK_CHURN_MIN = 1;         // at least 1 seat per class with an active window

// TeamCandidateFit -- a catalog of factors weighted by team personality.
constexpr int    CANDFIT_PERSONALITY_COUNT = 6;   // meritocratico, cantera_joven, veterano, dinero, patriotico, fiel_marca
constexpr int    CANDFIT_FACTOR_COUNT      = 8;   // skill,clean,seriesExp,brandAff,rep,natMatch,youth,budget
// weights[personality][factor]; each row reorders what that kind of team values. Tunable.
constexpr double CANDFIT_W[CANDFIT_PERSONALITY_COUNT][CANDFIT_FACTOR_COUNT] = {
    //  skill clean sExp brand  rep  nat  youth budg
    {   0.45, 0.10, 0.15, 0.05, 0.10, 0.00, 0.05, 0.10 },   // 0 meritocratico
    {   0.20, 0.20, 0.10, 0.05, 0.05, 0.05, 0.30, 0.05 },   // 1 cantera_joven
    {   0.25, 0.10, 0.25, 0.05, 0.20, 0.05, 0.02, 0.08 },   // 2 veterano
    {   0.20, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.50 },   // 3 dinero
    {   0.25, 0.10, 0.10, 0.05, 0.10, 0.30, 0.05, 0.05 },   // 4 patriotico
    {   0.25, 0.10, 0.15, 0.30, 0.10, 0.05, 0.00, 0.05 },   // 5 fiel_marca
};
constexpr double CANDFIT_NOISE_W  = 0.05;   // weight of deterministic noise (avoids perfect ties / total predictability)
// Cap on a cohort driver's budget factor. It used to be a fixed 0.0, and with the
// player's money_norm landing in the same column, the contest was decided by money alone. But spreading it
// 0..1 uncapped is worse: the rival's score is the MAX across a cohort of dozens, and the max of a
// uniform distribution saturates to 1, so a loaded rival always shows up. The cap keeps money as a real
// advantage for the player without turning it into a lottery.
//
// Value measured on 04/08 with tools/seatwatch_probe.cpp against save-1 (14 contests, MK_SEAT_WATCH_BAND swept):
//   cap | offers | WIN | WATCH | LOSE | adjusted loss | adjusted win | WITHIN BAND
//   |---------|------|---------|--------|------------------|-------------------|----------------
//    0.00 |    51   |   4  |    0    |   10   |      -0.231      |      +0.027       |   0/14
//    0.10 |    48   |   1  |    3    |   10   |      -0.011      |      +0.006       |   3/14  <-- CHOSEN
//    0.20 |    47   |   0  |    4    |   10   |      -0.035      |        --         |   4/14
//    0.25 |    47   |   0  |    4    |   10   |      -0.055      |        --         |   4/14
//    0.30 |    47   |   0  |    1    |   13   |      -0.075      |        --         |   1/14
//    0.35 |    47   |   0  |    0    |   14   |      -0.095      |        --         |   0/14
//    0.45 |    47   |   0  |    0    |   14   |      -0.136      |        --         |   0/14
//    0.60 |    47   |   0  |    0    |   14   |      -0.197      |        --         |   0/14
//
// The criteria, in order: (1) at least one contest lands WITHIN the band -- without that this doesn't do
// anything; (2) offers don't collapse; (3) verdicts don't all fall on the same side.
// 0.20/0.25/0.30 satisfy (1) and (2) but fail (3): the player stops winning even a single seat. 0.10 is the
// only value that leaves win, watch and loss all in play at once, and it's also the one giving the continuous distribution
// (-0.011 right next to +0.006, no gap around zero). From 0.35 up, everything is a loss.
constexpr double CANDFIT_COHORT_BUDGET_CAP = 0.10;
constexpr double CANDFIT_EXP_REF  = 20.0;   // races in the class that saturate seriesExp to 1
constexpr double CANDFIT_REP_REF  = 300.0;  // reputation that saturates the rep factor to 1
// fit margin by which the cohort takes the seat away from the player (merit-based contest).
// Aggressive contest rule: 0.0 = the player must be AT LEAST as strong as the best candidate in the
// cohort (it used to be 0.03, which gave a free margin). >0 = more lenient (the player can win with some disadvantage); <0 =
// the player must beat the cohort outright. Only applies in real-team classes (the contest doesn't run in cohort/generation classes).
constexpr double MK_SEAT_CONTEST_MARGIN = 0.0;

// "you're being watched". Until now the contest only had two outcomes: the seat is yours, or silence. The
// band carves out a third case within what used to just be silence -- you stay close, the team doesn't rule you out, it
// watches you for a few races and decides on your numbers from then. Nobody loses an offer they'd otherwise get: above
// MK_SEAT_CONTEST_MARGIN the verdict is untouched.
constexpr double MK_SEAT_WATCH_BAND  = 0.08;  // how far below the cohort's best you can be and still be a candidate
constexpr int    MK_SEAT_WATCH_RACES = 3;     // races (wherever you race) the observation period lasts
// How many cohort aspirants enter the contest when it opens. The list is sealed right there: the next stage decides
// among those who entered in the first stage, not whoever happens to be around by then.
constexpr int    MK_SEAT_WATCH_SHORTLIST = 3;
// On-track encounters (h2h.w + h2h.l) after which a driver
// counts as a "rival with history" and gets the first slot in the shortlist. A single crossed race isn't
// enough to call it a feud; three already is a pattern. The beef itself needs no minimum because it's born from a duel that the
// inbox already deemed worth reporting (career_contract.cpp, rival_duel family).
constexpr int    MK_RIVAL_TERNA_MIN_H2H = 3;
// How much beef rises when you lose a seat to someone. It's the payoff that was
// missing from the rivalry loop, which until now was only born on track and decayed on its own. It's 2, not 1, on
// purpose: in a single hit it reaches MK_BEEF_LEVEL_BEEF, meaning losing a seat isn't just another scrape, it's a
// grievance the inbox already counts as beef. Capped at 5, the same idiom used by the on-track duel and the spicy press.
constexpr int    MK_BEEF_SEAT_LOST = 2;
// The teammate is the rival that actually matters. Same car, same team, so a bad
// result against him has no excuse -- it's the only clean comparison that exists on the whole grid. The
// engine didn't distinguish him from a stranger: rivalryHeat, the beef counter and the on-track duel never look at team membership
// at any point, and the on-track duel also picks whoever's adjacent by position, which is almost never your teammate.
//
// It rises slowly (1, versus 2 for losing a seat) because losing a single race to your teammate is the
// most normal thing in the world; what tells a story is him beating you many times. And it drops when you beat him, which is
// what stops the grudge from being a one-way ratchet: a teammate you dominate isn't your nemesis.
constexpr int    MK_BEEF_MATE_LOSS = 1;   // he beats you -> +1 grudge (capped at 5, same cap as always)
constexpr int    MK_BEEF_MATE_WIN  = 1;   // you beat him -> -1 (floor 0)
// Cap in days: without a contract in that class you don't race in it, so a races-based window would never tick down. A
// team doesn't keep you under observation indefinitely; past this point it resolves with whatever data exists.
constexpr int    MK_SEAT_WATCH_MAX_DAYS = 120;
// After LOSING an observation, that seat locks up for a while: otherwise the same team would reopen the contest on
// the very next cycle and losing it would cost nothing. It only affects that team in that series.
constexpr int    MK_SEAT_WATCH_COOLDOWN_DAYS = 60;

// Privateer divorce ---
// You're "married" to a car+skin in a series (privateer_pick): you can pick a different identity in pre-race,
// but finishing the race with it costs REP plus a credit compensation, and it becomes your new marriage.
// cost = ENTRY_BY_PHASE[level-1] * PRIV_DIVORCE_ENTRY_MULT (scales with the series' level).
constexpr int    PRIV_DIVORCE_REP        = 3;
constexpr double PRIV_DIVORCE_ENTRY_MULT = 2.0;

// Trials -- fixed Top-N approval target ---
// One approval threshold for EVERY gray-zone trial: finish within the top TRIAL_TOPN FRACTION of the grid
// (does NOT vary by archetype/trigger). A FRACTION (0..1) of grid size; the resolver/settle computes the
// absolute target as ceil(TRIAL_TOPN * gridSize). 0.5 = "beat at least the bottom half".
constexpr double TRIAL_TOPN                  = 0.5;    // top-N target as a fraction of grid size
// "Low category" never-condition: below this tier no trial is required (the gray-zone gate is for
// big jumps / contested / top seats; entry tiers court freely). The gray-zone "2+ jump" condition still
// applies above it -- this only suppresses the gate at the very bottom of the ladder.
constexpr int    TRIAL_MIN_LEVEL             = 3;      // tier < this -> never a trial (low category)
// "Top team" gray-zone condition: a seat at/above this tier counts as a top team -- combined with
// "no recent strong result" it arms the trigger (a marquee seat without proof demands a trial).
constexpr int    TRIAL_TOPTEAM_LEVEL         = 6;      // tier >= this == a top team for the trigger-set

// === Team economy ===========================================================================
// Per-type coverage economy + insurance + travel + seat displacement/job-security tunables (defaults chosen
// by design judgment, cross-checked against a research table). Pure logic in career_econ.{h,cpp}
// + career_seats.{h,cpp}; every magnitude is a named constant here so NO magic number lands in any .cpp
// body (per the project's coding conventions). The 6 types index a fixed taxonomy: a coexisting set, not a
// ladder. Index: 0 Self-funded · 1 Pay-per-event · 2 Cost-share · 3 Covered/semi-pro · 4 Paid pro ·
// 5 Factory/works. Fractions in 0..1 = the team's covered share of that cost category for the type.
// Defaults are realism-grounded (research-based): the privateer eats
// everything plus the full deductible; factory drivers are shielded; the deductible is ALWAYS > 0 (never team/sponsor-covered).
constexpr int    COVERAGE_TYPE_COUNT = 6;   // the 6 coexisting contract types (not a ladder)
// seat/entry coverage: fraction of (rent/seat-fee + entry) the team covers per race
constexpr double COVERAGE_SEAT_ENTRY[COVERAGE_TYPE_COUNT]   = { 0.0, 0.6, 0.5, 1.0, 1.0, 1.0 };
// consumables (tyres/fuel/maintenance) covered fraction
constexpr double COVERAGE_CONSUMABLES[COVERAGE_TYPE_COUNT]  = { 0.0, 0.0, 0.3, 1.0, 1.0, 1.0 };
// repair-cost covered fraction (never covers the deductible)
constexpr double COVERAGE_DAMAGE[COVERAGE_TYPE_COUNT]       = { 0.0, 0.0, 0.5, 0.7, 0.9, 1.0 };
// travel-leg covered fraction
constexpr double COVERAGE_TRAVEL[COVERAGE_TYPE_COUNT]       = { 0.0, 0.5, 0.3, 1.0, 1.0, 1.0 };
// your deductible share base: 1.0 for privateers .. 0.1 for factory (smaller for more-covered types)
constexpr double COVERAGE_DAMAGE_SHARE_BASE[COVERAGE_TYPE_COUNT] = { 1.0, 0.8, 0.6, 0.35, 0.2, 0.1 };
// salary direction per type: -1 = you pay (buy-in path), 0 = none, +1 = team pays a salary
constexpr int    COVERAGE_SALARY_DIR[COVERAGE_TYPE_COUNT]   = {   0,   0,   0,    0,   +1,  +1 };
// cost-share damage cap: max credits of damage you eat before the team covers the rest (0 = no cap).
// Only the cost-share type (idx 2) carries a finite cap; the rest are uncapped (0) -- covered types absorb it fully
// via COVERAGE_DAMAGE, privateer/rental eat all of it.
constexpr long   COVERAGE_DAMAGE_CAP[COVERAGE_TYPE_COUNT]   = {   0,   0, 8000,    0,    0,   0 };
// exclusivity flag per type: holding an exclusive contract blocks signing another (a thin gate)
constexpr bool   COVERAGE_EXCLUSIVE[COVERAGE_TYPE_COUNT]    = { false, false, false, false, true, true };
// car fixed by the team (brand-locked): no selection. Design decision (realism): ANY contract
// SIGNED locks in the team's car+skin -- including type 0 (self-funded / pay-driver who pays for everything): a
// pay-driver drives the TEAM's car (they bring money to sit in that seat). The player only chooses their own car
// when racing WITHOUT a contract (a true privateer): that case doesn't even go through here (CareerSeatLockForSeries short-circuits when there's no contract).
// fixes_car controls ONLY car selection; the economy/coverage is set by the other COVERAGE_* tables.
constexpr bool   COVERAGE_FIXES_CAR[COVERAGE_TYPE_COUNT]    = { true, true, true, true, true, true };
// per-race salary base by type (folded into dealSalary when COVERAGE_SALARY_DIR > 0). Paid-pro
// gets a low salary, factory a high one; the other types pay no salary (0). Tunable magnitudes.
constexpr long   COVERAGE_SALARY_BASE[COVERAGE_TYPE_COUNT]  = {   0,   0,   0,    0, 6000, 20000 };

// Low-type result bonuses: per-type [Top10 / Top5 / win] schedule (credits), folded into a distinct
// SettleReport.result_bonus field. The low types (rental/cost-share/covered) lean on result bonuses since they
// pay little or no salary; pro/factory drivers already get a salary so their result bonuses are smaller. The win bonus
// includes Top10+Top5 stacking semantics being the OWNING slice's call -- here is just the per-bucket schedule.
constexpr long   RESULT_BONUS_BY_TYPE[COVERAGE_TYPE_COUNT][3] = {
    {     0,     0,      0 },   // 0 Self-funded -- no team bonuses (prizes only)
    {  2000,  5000,  12000 },   // 1 Pay-per-event -- result bonuses are the upside
    {  1500,  4000,  10000 },   // 2 Cost-share
    {  1200,  3000,   8000 },   // 3 Covered/semi-pro
    {   800,  2000,   5000 },   // 4 Paid pro (already salaried)
    {   500,  1500,   4000 },   // 5 Factory/works (already well salaried)
};

// Variable salary per contract ----
// Anchor per series: base figure (credits/race) by level 1..7. Compressed geometric curve:
// L1 (e.g. Kingsmere) ~4k; L7 (e.g. F1) ~188k. Realistic ordering, compressed range (no millions).
constexpr long   SALARY_ANCHOR_BASE   = 4000;    // credits/race at level 1
constexpr double SALARY_ANCHOR_GROWTH = 1.90;    // multiplier per level (L_n = BASE * GROWTH^(n-1))
// Per-team band (multipliers on top of the anchor). The center comes from the team's strength (percentile of
// its average rating within the series field): backmarker -> MIN_CENTER, frontrunner -> MAX_CENTER.
constexpr double SALARY_TEAM_MIN_CENTER = 0.60;  // band center for the weakest team in the field
constexpr double SALARY_TEAM_MAX_CENTER = 1.60;  // band center for the strongest team
// Half band-width around the center -> room for the driver's position (rating + negotiation).
constexpr double SALARY_BAND_HALFWIDTH  = 0.15;  // team band = [center-HALF, center+HALF]
// Absolute floor/ceiling on the salary paid (final clamp).
constexpr long   SALARY_FLOOR = 2500;            // no salaried seat pays less than this
constexpr long   SALARY_CAP   = 300000;          // hard cap (compressed magnitude)

// Mandatory insurance: season premium base by phase (1..7), charged once per held contract at
// rollover, scaled by past-season cleanliness. Heavier at higher phases (faster cars cost more to insure).
constexpr long   INSURANCE_PREMIUM_BY_PHASE[7] = { 3000, 6000, 12000, 24000, 45000, 80000, 130000 };
// Cleanliness multiplier band on the premium: a clean season (high cleanliness) pays less; a dirty
// season pays MORE. premium = base * lerp(CLEAN_MULT .. DIRTY_MULT) by (1 - cleanliness/100). Tunable.
constexpr double INSURANCE_CLEAN_MULT_MIN = 0.70;   // spotless season (cleanliness -> 100)
constexpr double INSURANCE_CLEAN_MULT_MAX = 1.60;   // crash-prone season (cleanliness -> 0)
// Per-race deductible: base credits scaled by the type's damage_share_base AND the race's damage-share
// severity (0..1 from telemetry). deductible = round(DEDUCTIBLE_BASE * COVERAGE_DAMAGE_SHARE_BASE[type] *
//   (DEDUCTIBLE_DAMAGE_SCALE * severity)). Always the player's, never covered. 0 severity -> 0.
constexpr long   DEDUCTIBLE_BASE         = 5000;    // credits base deductible at full severity, before the type scale
constexpr double DEDUCTIBLE_DAMAGE_SCALE = 1.0;     // severity multiplier (1.0 = linear in damage-share)
// Presentation layer: a cost category is shown as "team covers" in the "Includes" checklist
// when the type's covered fraction for that category is at or above this threshold; below it, "you pay". Half
// coverage (>=0.5) reads as "covered" -- privateer 0.0 = you pay, cost-share 0.5 seat = covered, factory 1.0 =
// covered. Pure display threshold (the Settle math uses the exact fractions; this only buckets the check/cross chip).
constexpr double COVERAGE_PRESENT_THRESHOLD = 0.5;
// A more-coverage negotiated package (DealTerms.salaryPct below the neutral 50 -- the
// "More coverage" pick) RAISES the shown coverage toward full. The lift = (50 - salaryPct)/50 * this gain, so
// a full-coverage pick (salaryPct 0) lifts each category's fraction halfway to 1.0 by default. Display-only:
// it nudges the presentation toward the negotiated reality, never changes the underlying type profile.
constexpr double COVERAGE_PRESENT_NEGO_GAIN = 0.5;
// Optional damage-responsibility reducer: when a contract's accepted_deal carries the
// damage-responsibility demand (NEGO_DAMAGE_RESP), the team shoulders part of the deductible -> the player's
// share is multiplied by this reducer (<1.0). Absent demand -> reducer 1.0 (no reduction, the safe default).
// The deductible is NEVER zeroed by it (realism: the deductible is always at least partly the player's).
constexpr double INSURANCE_DAMAGE_RESP_REDUCER = 0.5;   // damage-responsibility demand -> halve the deductible

// Travel leg cost: an A->B proxy by country/continent (no coordinates). Same country is cheap, same
// continent medium, cross-continent expensive. Covered per type by COVERAGE_TRAVEL; first leg origin = player
// nationality. Tunable magnitudes.
// Realistic magnitudes = the driver's PERSONAL cost of getting to the race (flight +
// transfers + weekend nights), NOT team logistics. A flight from Spain to Brazil doesn't cost 25,000: ~1.5-2.5k with
// a hotel. The cost is roughly tier-independent (a flight costs the same whether you're a rookie or a champion); the difference by
// tier comes from the contract's coverage (COVERAGE_TRAVEL: a factory team covers your travel), not the base price.
constexpr long   TRAVEL_SAME_COUNTRY     = 200;     // same country (car/train/short flight + 1 night)
constexpr long   TRAVEL_SAME_CONTINENT   = 600;     // same continent, different country (regional flight + hotel)
constexpr long   TRAVEL_CROSS_CONTINENT  = 2000;    // crossing continents (long-haul flight + transfers + weekend)

// Living seat market thresholds. DISPLACEMENT_MARGIN = how much the player's aged rating
// must exceed the weakest in-reach incumbent's to trigger a displacement offer. JOBSEC_PERF_BAR =
// the objective-completion ratio (0..1) below which an UNPROTECTED contract is not renewed / is replaced at
// rollover (protected by guaranteed_seat/anti_replace). Tunable.
// (15/08): the MINIMUM level at which a team can swap its incumbent for someone better. In a
// club cup or a school nobody displaces anybody — you race with whoever pays the entry fee. 5 = from the
// professional categories upward, which is where the bottleneck was.
constexpr int    SWAP_MIN_TIER           = 5;
constexpr double DISPLACEMENT_MARGIN     = 25.0;    // rating points the player must clear the weakest incumbent by
constexpr double JOBSEC_PERF_BAR         = 0.35;    // obj-ratio below this -> not-renewed/replaced unless protected

// Season objectives: final championship-position target by contract
// type (index = ContractTypeIndex 0..5, the same axis as the economy). A team that pays you more demands more:
// works/factory (you draw a salary) -> podium; covered/cost-share -> top 8; pay-per-event -> top 12;
// self-funded -> 0 = no objective (nobody demands anything from someone racing on their own money). Assigned when you SIGN
// (AcceptOffer), and at rollover renewal/dismissal is judged against it (goal-relative), not against the raw table.
constexpr int SEASON_OBJ_TARGET[COVERAGE_TYPE_COUNT] = { 0, 12, 8, 8, 3, 3 };
// Post-season rival offers: a podium finish (pos <= POS_BAR) at the end of a contract puts
// your name on the market -> up to MAX different teams from your own series bid against your renewal, with
// salaries from their own band (TeamSalaryBand via OfferTermsCtx: a top team pays more than a modest one).
constexpr int RIVAL_OFFER_POS_BAR = 3;              // max final position that triggers rival bids (podium)
constexpr int RIVAL_OFFER_MAX     = 2;              // max number of rival teams bidding at once
// Early renewal: with this many or fewer rounds left in the final season of a contract,
// the team opens renewal talks with a provisional offer based on your current position vs. the target. Signing
// locks in the seat right away; waiting until the end re-derives the offer (and any rival bids) from the actual result.
// If your season is going so badly the team would decline (BuildRenewalOffer), no early offer arrives at all.
constexpr int EARLY_RENEW_ROUNDS_LEFT = 2;

// The renewal bar within the window. It's the pivot point of ContractSeasonRatio, where
// "you just barely met the objective" is worth exactly 0.65 (pos == obj_target_pos). Above it, the team
// renews you; below it, your seat goes up for contest and whoever stands out in your class takes it.
// This isn't the same bar as the early renewal, and that's on purpose: that one only declines in the face of
// a disaster (BuildRenewalOffer: perf < 0.35, which with rel=0.5 gives ratio < 0.20) because that's the team acting
// early, while you can still recover. This is the end-of-season judgment, and there what
// matters is whether you met what was asked of you. Same formula, different moments, different bars.
constexpr double MK_RENEW_MET_RATIO = 0.65;

// "leaving by the back door": what it costs to walk away. Breaking a contract used to only penalize REP and nothing else,
// so over-signing and bailing was free in credits -- exactly the opposite of what's wanted, which is that
// calendar management is the player's responsibility, and over-signing has a cost.
// The amount is anchored to what the seat pays you (3 races' worth of salary) and, if the seat pays nothing
// (customer/pay-driver), to the entry cost of its level -- the same anchor used by the privateer divorce
// (PRIV_DIVORCE_ENTRY_MULT), so both ways of leaving a seat cost in the same currency.
// Scales with the seasons remaining: leaving in year one of three isn't the same as leaving in the last one.
constexpr int    MK_BREAK_RACES_OWED   = 3;
constexpr double MK_BREAK_ENTRY_MULT   = 1.0;
// terminating a contract leaves a mark: a cold-relationship mark (rel_marks) on the abandoned team, folded in by
// market::TeamRelationship in its future offers. Colder than a snub (REL_SNUBBED 0.20): a
// snub is not signing at all; this is leaving mid-contract. It decays at 0.5^year and is purged after 3 years (RelMarksSum).
// The ELO rating itself is untouched (rating measures pace on track; the contractual penalty works through trust instead).
constexpr int    MK_BREAK_MARK_X1000   = -300;   // -0.30 of relationship (same x1000 scale as RelMark.delta_x1000)

// Absences carry weight at renewal time. Missing a contracted round already dings your rating
// (RATING_NOSHOW_DING), and at MK_NOSHOW_BREACH_LIMIT the contract breaks on its own, but in between it used not to
// cost you the seat. Now each absence eats 0.10 off the ratio the team judges you by, and it's calibrated
// against the bar: for someone who JUST met the objective (0.65), a single absence already costs the renewal, while someone
// who dominated (0.90) can afford two before the contract breaks on its own from a third. Dominating buys you margin;
// scraping by doesn't.
constexpr double MK_RENEW_NOSHOW_PENALTY = 0.10;

// Team relationship 0..1 (the "WHY THEY WANT YOU -> RELATIONSHIP" bar + Want_T rel term) ---
// Pure TeamRelationship(save, series_id) (career_market.cpp) derives a per-team relationship from existing
// PERSISTED history (no RNG, no new save field). It feeds wi.rel in GenerateOffers -> BOTH the why-bar
// (o.whyRel = rel*100) AND the soft matching (Want_T's L_t*rel term). Tuned empirically
// -- not a user-facing decision; every magnitude is a named constant here so no magic number lands in the logic.
// PRECEDENCE (documented in TeamRelationship): a brand-new team with NO shared history MUST read EXACTLY
// REL_BASE (so test_mk_want_t stays 0.5825 byte-for-byte). Holding an active contract OR a pending renewal on
// the series takes PRECEDENCE (return the high value directly, no additive history). Otherwise start at
// REL_BASE and apply the additive ex-team / courted / snubbed adjustments, then clamp to [REL_MIN, REL_MAX].
// The ex-team term SCALES WITH TENURE (seasons raced in the series), with diminishing returns and a cap.
// Realism: shared history WARMS a team to you; longer tenure warms it MORE (to a point); snubbing COOLS it.
constexpr double REL_BASE         = 0.50;   // neutral base -- a brand-new team with no shared history (no-op -> 0.5825)
constexpr double REL_CURRENT_TEAM = 0.90;   // you hold an active contract on this series -- they're your team
constexpr double REL_RENEWAL      = 0.85;   // a pending renewal exists for this series -- they're trying to keep you
// ex-team / shared history NOW SCALES with TENURE: more seasons in this series = warmer relationship, with
// diminishing returns capped by REL_EX_TEAM_CAP. exTeam = min(REL_EX_TEAM_PER_SEASON * seasonsHere, REL_EX_TEAM_CAP).
// completed_seasons holds one entry per series+year, so counting entries with series_id==X = seasons raced there.
// race_log is a finer-grained fallback (counts as ONE season of tenure when there are no completed_seasons).
// Realism: a long-tenured driver is "family"; the first seasons matter most, then it plateaus (the cap).
constexpr double REL_EX_TEAM_PER_SEASON = 0.08; // +per FULL season raced in this series (1->0.08, 2->0.16, 3->0.24)
constexpr double REL_EX_TEAM_CAP        = 0.25; // tenure ceiling (4+ seasons capped here; diminishing returns)
constexpr double REL_EX_TEAM_PODIUM = 0.07; // +small extra if you finished WELL there (best final_pos <= REL_PODIUM_POS)
constexpr int    REL_PODIUM_POS   = 3;      // "finished well" threshold (a podium-or-better best result at the team)
constexpr double REL_COURTED      = 0.15;   // +they courted you (courting carries this series/team)
constexpr double REL_SNUBBED      = 0.20;   // you snubbed them (declined_offers or expired_offers has of_<series>)
// Cross-series brand affinity (per-org footprint warmth). When the offer's team is an org you
// have history with in ANOTHER series (drove for it / drive for it elsewhere now / it courted you elsewhere),
// the relationship warms: "the team has entries in other series where you've raced". ADDITIVE alongside the
// ex-team / courted / snubbed terms (before the clamp); does NOT override the current-team / pending-renewal
// PRECEDENCE returns (a brand match doesn't make you their current driver). NO-OP when offerTeam == "" (the
// back-compat single-arg path / no seat-team resolved -> byte-identical to today, test_mk_want_t stays 0.5825).
// Realism: a multi-series org that already knows your name across the paddock courts you a little warmer.
constexpr double REL_BRAND_AFFINITY = 0.12; // +the offer's org has a footprint in OTHER series of your history

// Farm-team network -- affiliation propagation: generalized into the diplomacy-spread mechanism described
// below. The old single-hop farm-team -> parent-team propagation (REL_AFFILIATION_* constants, now retired) is now just the
// 1-hop case of AllyReach (an affiliation edge x->parent, weighted by AffiliationStrengthFor); its magnitude is
// preserved because REL_DIPLOMACY_PER_SEASON == 0.10 (the same value as the old REL_AFFILIATION_PER_SEASON).

// Diplomacy spread (team<->team relationships). The reputation of teams from the
// player's history flows along allied edges (peer family/ally + farm-team -> parent affiliation) to offerTeam, decaying
// per hop and capped; direct rivals cool it down. This generalizes the old single-hop farm-team -> parent propagation. Tunable.
constexpr int    REL_DIPLOMACY_MAX_HOPS   = 2;     // affiliation/peer = 1 hop; peer->peer / affiliation->peer = 2
constexpr double REL_DIPLOMACY_DECAY      = 0.5;   // multiplicative decay per EXTRA hop (2nd hop x0.5)
constexpr double REL_DIPLOMACY_PER_SEASON = 0.10;  // + per season/contract at a team with a path to offerTeam (= REL_AFFILIATION_PER_SEASON)
constexpr double REL_DIPLOMACY_CAP        = 0.30;  // ceiling on total allied warming
constexpr double REL_RIVAL_COOL_CAP       = 0.20;  // ceiling on total rival cooling
// weights by kind (positive magnitude; the warmth/cool terms supply the sign):
constexpr double REL_KIND_FAMILY = 0.60;   // same brand/group or a variant of the same team (strong ally)
constexpr double REL_KIND_ALLY   = 0.40;   // works partner <-> customer / farm-team sibling (ally)
constexpr double REL_KIND_RIVAL  = 0.40;   // direct competitor (cools it)
constexpr double REL_KIND_BITTER = 0.70;   // historical rivalry (cools it more)

constexpr double REL_MIN          = 0.10;   // clamp floor (a cooled-but-not-dead relationship)
constexpr double REL_MAX          = 0.95;   // clamp ceiling (short of the precedence current-team value)

// Rival selection + career-arc tunables. ---
// The single source for the two magic counts the profile de-mock needs; no other module may hardcode
// either value (mirrors the same idiom used by the seat-market thresholds -- every magnitude is a named constant here).
// RIVAL_PROXIMITY_COUNT = the number of nearest-ELO "proximity" rival cards drawn over the union of your active-
// contract classes (proximity scoped to "2 proximity + 1 market"); the 3rd card is the market rival
// (seat competitor / ELO displacement), so the total rival set is RIVAL_PROXIMITY_COUNT + 1 = 3. Selection
// is deterministic (fnv1a tie-break); the nemesis is the worst-balance captured rival once h2h data exists.
constexpr int RIVAL_PROXIMITY_COUNT = 2;    // nearest-ELO proximity rivals (+1 market rival -> 3 cards total)
// ARC_DOWNSAMPLE_POINTS = the target point count the career-rating arc downsamples to when the race_log has many
// rating-bearing entries (<=kRaceLogMax=80). Mid-range of the target "~12-16"; the pure ProfileRatingArc keeps
// first+last and even-strides the middle, then appends the live "Now" point. A legibility tunable, chosen
// by design judgment; sentinel-0 (legacy/unknown) entries are filtered out before this count is applied.
constexpr int ARC_DOWNSAMPLE_POINTS = 14;   // target arc point count when downsampling a long race_log (~12-16)

} } // namespace career::bal
