// dll/career/career_simrunner.cpp -- Task 2+3: SimulateRound / SimulateSeason / SimulateYears
#include "career_simrunner.h"
#include "career_series.h"
#include "career_fields.h"
#include "career_customai.h"
#include "career_results.h"
#include "career_standings.h"
#include "career_econ.h"
#include "career_rating.h"
#include "career_calendar.h"   // RoundWinnerPrize, EntryFeeFor, RolloverYearReport, CareerCalendar
#include "career_cards.h"      // Task 12: CardShopDraw/CardOpBuy/CardOpShopClose/CardsSettle
#include "career_contract.h"   // phase 7: AdvanceDays -- the clock goes through the SAME path the app triggers
#include "career_dateutil.h"   // phase 7: AbsDayFromYearDoy (the date range is measured in absolute days)
#include "career_seats.h"      // phase 7: SignWindowSeat / InDrought / WSEAT_* (the window and the floor)
#include "career_difficulty.h" // phase 7: ResolveRaceSkill -- the race's REAL difficulty (it used to come in as 0)
#include "career_balance.h"    // phase 7: MK_NOSHOW_BREACH_LIMIT -- classify the breach caused by absences
#include "career_profile.h"    // phase 8: AccumulateH2H -- the SAME duel capture the game uses
#include "career_candidate.h"  // phase 8: SettleTeammateDuel -- the grudge with the other side of the garage
#include <algorithm>
#include <map>
#include <cmath>
#include <cstdio>
#include <set>

namespace career {

// Task 1: sandbox state accessors -------------------------------------------------

void SimInit(SaveData& save, CareerCalendar& cc, const std::string& playerName) {
    save = SaveData{};
    save.prog.frontier_phase = 1;
    save.prog.credits = 50000;
    save.prog.rating = RatingSeedPlayer();
    save.prog.rating_races = 0;
    save.player.name = playerName;
    cc = CareerCalendar{};
    cc.year = kStartYear;
    cc.cursor_doy = RookieOpenerDoy();
}

SimState SimSnapshot(const SaveData& save, const CareerCalendar& cc) {
    SimState st;
    st.year = cc.year; st.doy = cc.cursor_doy;
    st.credits = save.prog.credits; st.xp_total = save.prog.xp_total; st.rep = save.prog.rep;
    st.rating = save.prog.rating; st.rating_band = RatingBand(save.prog.rating);
    st.enrollments = (int)cc.enrollments.size();
    st.retired_count = (int)save.retired.size();
    for (const auto& id : save.retired) st.retired.push_back(id);
    return st;
}

//

SimGridResult SimComposeGrid(const std::string& seriesId) {
    SimGridResult out;
    out.series_id = seriesId;
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return out;   // found = false -> the caller emits "unknown series"
    out.found      = true;
    out.display    = sd->display;
    out.class_name = sd->class_name;
    out.region     = sd->region;
    out.era        = sd->era;
    out.tier       = sd->level;

    // Same sequence as SimulateRound, but against a FRESH state (reproducible baseline):
    // starts the season, plans round 1 for the grid size, and composes it.
    SeasonState season = StartSeason(seriesId, "");
    RoundPlan   plan   = PlanRound(season);
    int gridSize = plan.gridSize > 1 ? plan.gridSize : 16;
    out.grid_size = gridSize;

    SaveData fresh;   // without live ratings -> ComposeSeasonGrid uses the static curve (canonical)
    double pf = PlayerForm(season, CareerForm(fresh.prog));
    out.drivers = ComposeSeasonGrid(seriesId, out.tier, pf, gridSize, /*useHomage=*/false);
    for (const auto& g : out.drivers) {
        if (g.kind == "roster") out.roster_count++;
        else                    out.real_count++;
    }
    return out;
}

std::uint64_t RoundSeed(std::uint64_t base, const std::string& seriesId, int year, int round) {
    std::uint64_t h = base ^ 0x9E3779B97F4A7C15ull;
    for (char c : seriesId) { h ^= (unsigned char)c; h *= 0x100000001B3ull; }
    h ^= (std::uint64_t)year  * 0xD1B54A32D192ED03ull;
    h ^= (std::uint64_t)round * 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    return h ? h : 1;
}

// phase 7 -- THE PLAYER'S LEVEL IS CHOSEN. Returns the race_skill needed to make them perform as
// the profile requests, placed on the scale OF THE FIELD ITSELF: the grid's race_skill values are sorted and
// a percentile is taken. That way "dominating" means the same in karting as in F1, with no conversion constant needed.
// DOMINANT is placed ABOVE the best (by the field's own spread as margin) because winning almost
// always isn't tying with first place, it's having a cushion ahead.
static double PerfSkillInField(const std::vector<GridDriver>& grid, int perf) {
    std::vector<double> sk;
    sk.reserve(grid.size());
    for (const auto& g : grid) sk.push_back(g.race_skill);
    if (sk.empty()) return 0.8;                       // no field: a middle-of-the-pack value, not zero
    std::sort(sk.begin(), sk.end());
    auto pct = [&](double p) -> double {
        if (p <= 0.0) return sk.front();
        if (p >= 1.0) return sk.back();
        const double x = p * (double)(sk.size() - 1);
        const size_t i = (size_t)x;
        const double t = x - (double)i;
        return (i + 1 < sk.size()) ? sk[i] + (sk[i + 1] - sk[i]) * t : sk[i];
    };
    const double abanico = sk.back() - sk.front();    // how wide this field is, to measure the cushion
    switch (perf) {
        case SIMPERF_DOMINANT: return sk.back() + (abanico > 0.0 ? abanico * 0.35 : 0.05);
        case SIMPERF_STRONG:   return pct(0.90);      // upper tier: you win some, podium often
        case SIMPERF_MID:      return pct(0.50);      // the midfield
        case SIMPERF_POOR:     return pct(0.05);      // at the back, but WITHIN the field (not a broken car)
        default:               return pct(0.50);
    }
}

SimRoundResult SimulateRound(SeasonState& season, SaveData& save, const std::string& playerName,
                             std::uint64_t baseSeed, const RaceModelConfig& cfg, int perf) {
    SimRoundResult res;
    if (SeasonComplete(season)) { res.season_complete = true; return res; }

    const SeriesDef* sd  = SeriesById(season.series_id);
    RoundPlan        plan = PlanRound(season);
    int tier     = sd ? sd->level : 1;
    int gridSize = plan.gridSize > 1 ? plan.gridSize : 16;

    // 1) compose grid + seed per-driver ratings from field order
    double pf = PlayerForm(season, CareerForm(save.prog));
    // THE LIVING WORLD -> GRID BRIDGE. Without the last argument, the grid came from the
    // static files and the market never touched it: the driver the world put in a car never actually raced, which is why the
    // duel with your teammate gave ZERO outside the top tiers (66 races of the dominant profile with the
    // teammate absent from the grid, measured). It's threaded through here first, in the simulator, because the same
    // bridge in the XML path changes what AMS2 actually races, and that calls for on-track validation.
    // 18/08 — KNOWN DISCREPANCY, DELIBERATELY NOT FIXED, the same one as the census in
    // career_canon.cpp. 19/08 — and it's FIVE arguments, not two: the original note listed `skipLiveries` and
    // `driverRaces` and came up three short when the aging trio was added that same day.
    // The COMPLETE list, because an incomplete enumeration reads as complete:
    //   · `skipLiveries` — can seat a rival in the player's livery;
    //   · `driverRaces`  — pace comes from the whole `RatingQuality(ELO)` and not from the perturbed
    //                      authored capacity;
    //   · `driverCurve`, `birthYear`, `worldYear` — the 18/08 trio: without them a CLONE runs at its CEILING
    //                      and not where it actually is.
    // And `role` is fixed to `SingleClass` instead of `ClassRoleForSeries`, which doesn't change the result here
    // because the simulator doesn't write blue flags.
    //
    // WHAT THIS WEIGHS, plainly: this is THE SIMULATOR THIS PROJECT USES TO DECIDE WHETHER IT MOVES FORWARD
    // — phase 7 runs here, and its outputs are what get looked at before a design decision is accepted as
    // good. Since 18/08 it measures UNAGED grids while production DOES age the clones, so
    // the gap between what this instrument sees and what AMS2 puts on track grows with the cloned share
    // and with the number of simulated seasons. Anyone who reads a number here and takes it to a decision should read this first.
    //
    // It isn't touched here because this simulator is an INSTRUMENT: its outputs live in specs and in test
    // thresholds, and moving it without re-deriving what it fed has already cost us three times. It has its own card. The
    // right door once it's closed is `career_seats.h::ComposeSeasonGridForSave`, which the
    // three overlay writers already go through.
    const std::vector<SeatRow> asientos = SeatRowsForSeries(save, season.series_id);   // SORTED
    std::vector<GridDriver> grid = ComposeSeasonGrid(
        season.series_id, tier, pf, gridSize, /*useHomage=*/false,
        save.rival_form, /*exclude=*/nullptr, &save.driver_ratings,
        ClassRole::SingleClass, asientos.empty() ? nullptr : &asientos);

    std::vector<FieldDriver> fld = FieldForSeries(season.series_id);
    std::vector<SeedFieldEntry> seeds;
    seeds.reserve(fld.size());
    for (const auto& f : fld)
        seeds.push_back(SeedFieldEntry{ f.id, f.order, f.iconic });
    SeedDriverRatings(save.driver_ratings, seeds);

    // 2) build entrant list (AI from grid, then player)
    std::vector<RaceEntrant> ents;
    ents.reserve(grid.size() + 1);
    for (const auto& g : grid) {
        RaceEntrant e;
        e.id          = g.driver_id;
        e.name        = g.name;
        e.is_player   = false;
        e.skill       = g.race_skill;
        e.consistency = g.consistency;
        e.aggression  = g.aggression;
        e.reliability = g.reliability;
        e.wet_skill   = g.wet_skill;
        ents.push_back(e);
    }
    RaceEntrant me;
    me.id          = "__player__";
    me.name        = playerName;
    me.is_player   = true;
    // phase 7 -- THE PLAYER WAS RACING ON A DIFFERENT SCALE. `RatingQuality` is a logistic centered on 1600, so
    // with the SEED rating (1250) it returns 0.219... while the grid's race_skill ranges from 0.599 to 0.967
    // with a mean of 0.802 (measured in harbor_trophy_6546). The race model compares the two numbers directly, meaning
    // the player was born slower than the WORST car in the field and ended up at the back by construction: they lost
    // rating, the rating made them even slower, and so on. The spiral didn't come from difficulty or DDA --
    // it came from comparing two quantities that aren't the same thing. Matching the average would need an ELO of
    // ~1985, and the whole world lives between 1000 and 1600: it was unreachable by design.
    //
    // You're placed where you belong WITHIN THE FIELD: rivals are sorted by rating and their
    // race_skill is interpolated at the point where yours falls. That way the two quantities are the same by construction -- if your
    // ELO is that of the back of the pack, you race like the back of the pack; if it's the front, like the front -- and there's no need to invent
    // any conversion constant that could drift out of tune tomorrow.
    // And the level is CHOSEN, not deduced. A user request, and it's the right call for a debug tool: what we
    // want to test are the CONSEQUENCES of performing that way -- what the world does if you dominate, fight, or
    // sink --, not modeling how anyone drives. Deducing it from ELO also fed back on itself: worse rating ->
    // slower -> worse rating.
    // The profile is placed ON THE FIELD'S OWN SCALE (percentiles of its race_skill), so it holds equally
    // in karting as in F1 with no conversion constants to drift out of tune tomorrow.
    me.skill = PerfSkillInField(grid, perf);
    me.consistency = 0.85;
    me.aggression  = 0.55;
    me.reliability = 0.97;
    me.wet_skill   = me.skill;
    int playerEnt  = (int)ents.size();
    ents.push_back(me);

    // 3) simulate race
    std::uint64_t seed = RoundSeed(baseSeed, season.series_id, season.season_year, plan.round);
    RaceOutcome ro = SimulateRace(ents, plan.weather, seed, cfg);

    // 4) build ParticipantSnap in classified order for Classify()
    std::vector<ParticipantSnap> snaps;
    snaps.reserve(ro.order.size());
    int classifiedPos = 1;
    for (int rank = 0; rank < (int)ro.order.size(); ++rank) {
        int ei = ro.order[rank];
        const RaceEntrant& e = ents[ei];
        bool dnf = (ro.dnf_of[ei] != DnfKind::Finished);
        ParticipantSnap p;
        p.name      = e.name;
        p.driver_id = (e.id == "__player__") ? "" : e.id;
        p.active    = true;
        p.raceState = dnf ? RS_DNF : RS_FINISHED;
        p.pos       = dnf ? 0 : classifiedPos++;
        p.laps      = dnf ? 1 : plan.laps;
        snaps.push_back(p);
    }
    std::vector<ClassifiedEntry> cls = Classify(snaps);

    // 5) locate player position in the classified list
    int field     = (int)cls.size();
    int playerPos = ro.player_pos;
    for (int i = 0; i < field; ++i)
        if (cls[i].name == playerName) { playerPos = cls[i].pos; break; }
    DnfKind playerDnf = ro.dnf_of[playerEnt];

    RaceResult rr;
    rr.pos        = playerPos;
    rr.field      = field;
    rr.finished   = (playerDnf == DnfKind::Finished);
    rr.clean      = (playerDnf == DnfKind::Finished);
    rr.race_phase = tier;
    // phase 7 -- DIFFICULTY CAME IN AS ZERO, and it's not a decorative field: the rating model USES it.
    // EffectiveOpp(rOpp, aiPct) shifts the rival's effective strength by AI_RATING_PER_PCT points for every point
    // of difficulty below 100, and RepFor scales reputation by aiLevel/REP_AI_REF. With 0, EffectiveOpp
    // returns the RAW rating: the simulation was scoring as if the whole AI field ran at 100%.
    //
    // And that's exactly what was sinking the player. DDA doesn't adjust you, it adjusts the AI -- but the
    // ELO charges based on the rival's EFFECTIVE strength, so racing an AI turned down to 80 while getting
    // charged as if it were 100 bleeds your rating every race. Measured: rating stuck on the floor.
    //
    // It's resolved with the SAME cascade as the real race (ResolveRaceSkill: mode + level + base + trim), not
    // by reading ai_difficulty raw, because fixed modes ignore the base and reading it directly would give a number that
    // that save doesn't use.
    { const SeriesDef* sdd = SeriesById(season.series_id);
      rr.ai_level = ResolveRaceSkill(save.prog.difficulty_mode, sdd ? sdd->level : 1,
                                     save.prog.ai_difficulty, save.prog.adaptive_trim,
                                     save.prog.custom_difficulty); }

    // 6) settle economy + standings
    double credBefore  = (double)save.prog.credits;
    res.rating_before  = save.prog.rating;

    // v1 simplification: treat as rented unless player actually owns a car of this class
    bool rented = sd ? !OwnsCarOfClass(save, sd->class_name) : true;

    long winnerPrize = RoundWinnerPrize(season.series_id);
    long floorPrize  = FloorForPhase(tier);
    long entryFee    = EntryFeeFor(season.series_id);

    SettleReport sr = SettleRound(season, save.prog, rr, cls, /*flName=*/"",
                                  winnerPrize, floorPrize, rented, /*rentPct=*/22, /*carValue=*/0,
                                  // phase 7: the FACTORY seat was being paid for by the player. `factorySeat` was
                                  // hardwired to false, so a works contract ran at their own expense in the
                                  // simulation, making the economy come out worse than it would while actually playing. It's derived from the
                                  // contract with the SAME function the real settlement uses.
                                  /*entryOverride=*/entryFee,
                                  /*factorySeat=*/ContractFactorySeat(save, season.series_id));

    // phase 7 (6b) -- PHASE PROMOTION AND OPENING GATES. This used to live ONLY in the overlay, not in SettleRound, so
    // the simulation never promoted: measured 2 years and 75 races with xp_total 48k and the player was still stuck in phase 1,
    // signing forever for the same tier-1 series. Without promotion there's no ladder, and the ladder IS the
    // career -- a simulation that never promotes doesn't simulate the game, it simulates a loop.
    // SAME loop and SAME functions as the real settlement (CheckPhaseUnlock/XpThreshold): if the bar ever
    // moves, it changes in both places because it's the same code.
    while (save.prog.frontier_phase < 7 &&
           CheckPhaseUnlock(save.prog.xp_toward_next, save.prog.frontier_phase)) {
        save.prog.xp_toward_next -= XpThreshold(save.prog.frontier_phase);
        save.prog.frontier_phase++;
    }
    // and the qualifying GATES: a series with `unlocks` and `gate_top` opens access if you finish within the
    // cutoff. In the game a separate entry fee is charged and it's gated by affordability; here the unlock alone is enough,
    // because what the simulation needs to know is whether ACCESS opens, not to charge the entry fee again.
    { const SeriesDef* qd = SeriesById(season.series_id);
      if (qd && !qd->unlocks.empty() && qd->gate_top > 0 && playerPos > 0 && playerPos <= qd->gate_top)
          GateUnlockOnResult(qd, playerPos, save.unlocked_gates); }

    // 7) ELO update: player rating via SettleRating, AI ratings via RatingUpdateRace
    std::vector<double> ratingsInOrder;
    std::vector<std::string> idsInOrder;
    ratingsInOrder.reserve(field);
    idsInOrder.reserve(field);
    for (const auto& ce : cls) {
        if (ce.name == playerName) {
            ratingsInOrder.push_back(save.prog.rating);
            idsInOrder.push_back("");
        } else {
            double rv = save.driver_ratings.count(ce.driver_id)
                ? save.driver_ratings.at(ce.driver_id)
                : RatingSeedFromOrder(ce.pos, field, false);
            ratingsInOrder.push_back(rv);
            idsInOrder.push_back(ce.driver_id);
        }
    }
    int playerIdx = playerPos - 1;
    if (playerIdx < 0)      playerIdx = 0;
    if (playerIdx >= field) playerIdx = field - 1;

    SettleRating(save.prog, ratingsInOrder, playerIdx, rr.ai_level, playerDnf);

    // phase 8: THE DUEL WITH YOUR TEAMMATE, and the h2h along with it. Both were missing HERE: the h2h capture used to live
    // only on the game's path (career_overlay), so in the simulator `save.h2h` was never populated and
    // any measurement of rivalries came out zero by construction -- exactly the blind spot that
    // phase 7 exists to eliminate. It uses the SAME pure function as the game (AccumulateH2H) so the two
    // halves can't diverge.
    {
        std::vector<H2HFinish> orden; orden.reserve(cls.size());
        std::map<std::string, int> posPorId;
        for (const auto& ce : cls) {
            if (ce.name == playerName) continue;              // your own row never compares against itself
            if (ce.driver_id.empty()) continue;               // generic AI outside the pool
            H2HFinish f; f.driver_id = ce.driver_id; f.pos = ce.pos; f.carClass = ce.carClass;
            orden.push_back(f);
            if (ce.pos > 0) posPorId[ce.driver_id] = ce.pos;
        }
        const int posJugador = rr.finished ? playerPos : 0;   // DNF -> no duels, same as in the game
        AccumulateH2H(orden, /*playerId=*/std::string(), posJugador, save.h2h);
        const TeammateDuel md = SettleTeammateDuel(save, season.series_id, posJugador, posPorId,
                                                   save.current_world_year != 0 ? save.current_world_year
                                                                                : season.season_year);
        res.mate_raced = md.raced; res.mate_lost = md.lost; res.mate_beef = md.beef; res.mate_why = md.why;
    }

    std::vector<double> deltas = RatingUpdateRace(ratingsInOrder, rr.ai_level, RatingK(0, false));
    for (int i = 0; i < field && i < (int)deltas.size(); ++i)
        if (!idsInOrder[i].empty())
            save.driver_ratings[idsInOrder[i]] += deltas[i];

    // 8) fill result
    res.round          = plan.round;
    res.player_pos     = playerPos;
    res.field          = field;
    res.credits_delta  = (long)((double)save.prog.credits - credBefore);
    res.xp_gained      = sr.xp_gained;
    res.rep_delta      = sr.rep_delta;
    res.rating_after   = save.prog.rating;
    res.winner_name    = cls.empty() ? std::string() : cls.front().name;
    res.dnf_kind       = (int)playerDnf;
    res.season_complete = SeasonComplete(season);
    return res;
}

SimSeasonResult SimulateSeason(SaveData& save, const std::string& seriesId,
                               const std::string& playerName, std::uint64_t baseSeed,
                               const RaceModelConfig& cfg) {
    SimSeasonResult out;
    out.series_id = seriesId;
    SeasonState season = StartSeason(seriesId, "");
    int guard = 0;
    while (!SeasonComplete(season) && guard++ < 64) {
        SimRoundResult r = SimulateRound(season, save, playerName, baseSeed, cfg);
        out.rounds.push_back(r);
        if (r.season_complete) break;
    }
    out.rounds_run = (int)out.rounds.size();
    SeasonOutcome so = FinishSeason(season, save.prog, playerName);
    out.player_pos = so.player_pos;
    out.money      = so.money;
    out.xp         = so.xp;
    out.rep        = so.rep;
    std::vector<StandingRow> table = FinalTable(season.standings);
    out.champion = table.empty() ? std::string() : table.front().name;
    return out;
}

SimYearsResult SimulateYears(CareerCalendar& cc, SaveData& save, const std::string& playerName,
                              int years, std::uint64_t baseSeed) {
    // Pre-seed birth_year for roster drivers that lack one.
    // Without birth years the engine's ShouldRetire can never fire (it needs age to be known).
    // Assign ages 25..54 deterministically via id hash so the field spans junior-to-veteran;
    // at least the oldest cohort (age>=40) will cross the retire threshold within a few years.
    {
        std::set<std::string> seen;
        for (int t = 1; t <= 7; t++) {
            for (const auto& r : RosterForTier(t)) {
                if (save.birth_year.count(r.id) || seen.count(r.id)) continue;
                seen.insert(r.id);
                if (r.birth_year != 0) { save.birth_year[r.id] = r.birth_year; continue; }
                // derive a stable age 25-54 from the driver id: FNV-1a hash -> age bucket
                std::uint64_t h = 14695981039346656037ull ^ baseSeed;
                for (char c : r.id) { h ^= (unsigned char)c; h *= 1099511628211ull; }
                int age = 25 + (int)(h % 30u);   // 25..54
                save.birth_year[r.id] = cc.year - age;
            }
        }
    }

    SimYearsResult out;
    out.from_year = cc.year;
    for (int y = 0; y < years; ++y) {
        // Use RolloverYearReport directly: ages the world (retirements/promotions/rating drift)
        // and advances cc.year by 1. Accumulate retirements + promotions per year.
        RolloverReport rep = RolloverYearReport(cc, save.prog, save, playerName);
        out.retirements += (int)rep.retirements.size();
        out.promotions  += (int)rep.promotions.size();
        for (const auto& r : rep.retirements)
            out.retired_names.push_back(r.name);
    }
    out.to_year = cc.year;
    return out;
}

// Task 12 (sponsor-cards spec 2026-07-11, anti-inflation guard) --------------------------------------
// DESIGN NOTE: SimulateRound/SimulateSeason (above) compose a REAL grid from external data
// (series/fields/roster PSV|JSON via LoadSeries/LoadFields/LoadRoster) and its SimRoundResult does NOT expose the
// entry/travel breakdown from settlement (SettleReport stays inside SettleRound) -- tangling this guard
// with that external data load would make it fragile (it fails if the file is missing the series) and adds nothing:
// the guard doesn't need grid fidelity, only comparing economy WITH/WITHOUT cards under
// IDENTICAL, deterministic conditions. So, following the brief's exception, I use a SELF-CONTAINED loop: a fixed
// 10-race season, prize via the bal::PRIZE_DECAY curve over a fixed winner, entry fee from
// bal::ENTRY_BY_PHASE, cyclic P1..P10 finishing positions (deterministic, no RNG from the race engine). Without
// cards, it's pure bal:: arithmetic -- with cards, a layer identical to production (career_overlay.cpp):
// CardShopDraw seeded by race index -> greedy policy (CardOpBuy) -> CardOpShopClose ->
// CardsSettle with flags derived from the simulated result.
//
// Greedy policy (refined): buys the FIRST affordable offer, but ONLY if a free active slot remains.
// CardOpBuy auto-slots into the first free slot, and if there isn't one, it leaves it on the bench (frozen: it never
// triggers, CardsSettle never touches it) -- buying with no room would spend the price with no payoff at all,
// which isn't "greedy", it's throwing money away, and it was sinking the guard (withCards < without with the version lacking this
// filter). With CardSlotsUnlocked(3)=3, the active wallet saturates at 3 purchases and the next shops only
// buy again once a card with a per-race lifespan expires and frees a slot.
//
// CardFlags mapping (documented; there's no real RaceTelemetry in this sim):
//   win/podium/top8/top10  <- simulated position (pos<=1/3/8/10), race always "finished"
//   clean                  <- pos <= half the field (front half = clean race; brief's example)
//   damagefree/tyrewear/fueluse <- = clean (same deterministic proxy; no real RaceTelemetry in the sim, a
//                                  "clean" race also stands for taking care of the car/consumables -- this avoids
//                                  punishing car-care triggers for no reason with a blind false)
//   endur                  <- false (the sim doesn't model endurance events)
// Costs: entry (bal::ENTRY_BY_PHASE) AND rent (RENT_PCT of a nominal car value -- the sim treats the
// player as RENTED, see constants below) ARE passed to CardsSettle so the
// CARD_HOOK_COST_ENTRY/RENT discounts return their savings, same as career_overlay.cpp (cardCosts.entry = rep.entry;
// rented -> cardCosts.rent = rep.costs). travel/tyres/fuel stay at 0 (not modeled in this loop -> "if it's not
// available, zero", per the brief; tyres/fuel only apply to your OWN car, not a rented one).
void SimRunCredits(int seasons, bool useCards, long* outCredits) {
    if (!outCredits) return;
    using namespace bal;
    const int  kRacesPerSeason = 10;
    const int  kFieldSize      = 10;
    const int  kPhase          = 3;                          // fixed phase -> 3 slots, stable entry/weights
    const long kWinnerPrize    = 20000;                       // fixed winner's prize (PRIZE_DECAY curve)
    const long kEntryFee       = ENTRY_BY_PHASE[kPhase - 1];
    // RENTED car (the STARTING_CREDITS rookie goes to the Market for a rental seat, not a purchase --
    // career_balance.h): representative cost = RENT_PCT of a modest nominal car value, same order
    // of magnitude as the prize, so CARD_HOOK_COST_RENT cards have a real saving to compare against
    // (with cost 0 those hooks are mathematically inert -- that's how this design bug was caught).
    const long kNominalCarValue = 27000;
    const long kRentCost        = (long)((double)kNominalCarValue * RENT_PCT + 0.5);

    SaveData sd;                                              // only credits/frontier_phase/cards/card_shop matter here
    sd.prog.credits        = STARTING_CREDITS;
    sd.prog.frontier_phase = kPhase;

    for (int s = 0; s < seasons; s++) {
        for (int race = 0; race < kRacesPerSeason; race++) {
            int  pos      = (race % kFieldSize) + 1;          // P1..P10 cyclic, deterministic
            bool finished = true;
            long prize    = (long)((double)kWinnerPrize * std::pow(PRIZE_DECAY, pos - 1) + 0.5);
            sd.prog.credits += prize - kEntryFee - kRentCost;

            if (useCards) {
                int raceIdx = s * kRacesPerSeason + race;
                char seedBuf[64];
                snprintf(seedBuf, sizeof(seedBuf), "simrun:%d:0", raceIdx);   // seed: race index + fixed counter
                sd.card_shop = CardShopState{};
                sd.card_shop.open   = true;
                sd.card_shop.offers = CardShopDraw(sd.cards, kPhase, seedBuf, CardShopOfferCount(sd.cards));
                // greedy: buys the FIRST affordable offer, BUT only if there's a free active slot -- buying with
                // no room leaves it frozen on the bench (it never triggers, CardsSettle never touches it), and would just be throwing
                // away the price with no payoff, not a sensible "greedy" policy. CardSlotsUnlocked(3)=3 ->
                // at most 3 active cards at a time; per-race lifespans free up a slot when they expire.
                int activeSlots = 0;
                for (const auto& c : sd.cards) if (c.slot >= 0) activeSlots++;
                if (activeSlots < CardSlotsUnlocked(kPhase)) {
                    for (const auto& id : sd.card_shop.offers)
                        if (CardOpBuy(sd, id)) break;
                }
                CardOpShopClose(sd);

                CardFlags f;
                f.win        = finished && pos == 1;
                f.podium     = finished && pos <= 3;
                f.top8       = finished && pos <= 8;
                f.top10      = finished && pos <= 10;
                f.clean      = finished && pos <= (kFieldSize / 2);   // front half = clean
                // damagefree/tyrewear/fueluse: no real RaceTelemetry in this sim (no shm) -- tied to the SAME
                // "front half" proxy as clean (a well-driven, incident-free race also takes care of the
                // car/consumables; using the same deterministic sign as clean, instead of a blind false, avoids
                // punishing a third of the catalog for no reason with car-care triggers -- see the note above).
                f.damagefree = f.clean;
                f.tyrewear   = f.clean;
                f.fueluse    = f.clean;
                f.endur      = false;                          // the sim doesn't model endurance events
                f.dnf        = !finished;

                CardCosts costs{};                             // travel doesn't exist in this loop -> 0 (documented)
                costs.entry = kEntryFee;                        // entry and rent ARE available (already subtracted
                costs.rent  = kRentCost;                        // above) -> passed to CardsSettle so the
                                                                 // CARD_HOOK_COST_ENTRY/RENT discounts return their
                                                                 // saving in credits_delta, same as career_overlay.cpp
                                                                 // (cardCosts.entry = rep.entry; rented -> cardCosts.rent = rep.costs)
                CardsSettleReport rep = CardsSettle(sd.cards, f, prize, costs);
                sd.prog.credits += rep.credits_delta;
            }
        }
    }
    *outCredits = sd.prog.credits;
}

//
// PHASE 7: the whole world, week by week. See the long note in career_simrunner.h.
//

// MINIMAL snapshot of what needs diffing to derive events. Stored by value and cheap:
// the living world's state doesn't fit here, and it doesn't need to -- what's tracked is the CHANGES a player
// would notice, not the whole world.
namespace {
struct WorldSnap {
    int  windowYear = 0;
    std::map<std::string, int>  seatState;    // (series|team) -> WindowSeatState
    // series -> missed_rounds of the active contract. It used to be a `bool` ("does it have a contract?"), and with that the breach was
    // counted but never explained. Signing five series at once makes weekends COLLIDE: the round that
    // overlaps doesn't get run, DnfPassedRounds counts it as an absence, and at MK_NOSHOW_BREACH_LIMIT the
    // contract breaks on its own. Without this counter that whole chain was invisible and breaches looked like magic.
    std::map<std::string, int>  contract;
    bool drought = false;
    int  year = 0;
    long credits = 0;
    // 18/09: the fingerprint of who occupies each seat — the SAME one that decides whether the AMS2 grids have
    // gone stale (GridsAreStale). If it changes between two steps, a seat moved and the game would ask for a restart.
    unsigned long occStamp = 0;
};

WorldSnap takeSnap(const SaveData& s, const CareerCalendar& cc) {
    WorldSnap w;
    w.windowYear = s.window_year;
    w.year = cc.year;
    w.credits = s.prog.credits;
    for (const auto& ws : s.window_seats) w.seatState[ws.series_id + "|" + ws.team_label] = ws.state;
    for (const auto& c : s.contracts) if (c.active) w.contract[c.series_id] = c.missed_rounds;
    w.drought = InDrought(s, cc.year, cc.cursor_doy, kStartYear);
    w.occStamp = SeatOccupancyStamp(s);
    return w;
}
}  // namespace

SimWorldResult SimulateWorld(CareerCalendar& cc, SaveData& save, const std::string& playerName,
                             int toYear, int toDoy, std::uint64_t baseSeed, int signPolicy, int perf) {
    SimWorldResult r;
    r.fromYear = cc.year; r.fromDoy = cc.cursor_doy;
    const int absDesde = career::dateutil::AbsDayFromYearDoy(cc.year, cc.cursor_doy);
    const int absHasta = career::dateutil::AbsDayFromYearDoy(toYear, toDoy);
    int restan = absHasta - absDesde;
    if (restan < 0) restan = 0;          // past date: don't go back, do nothing

    auto push = [&](const char* kind, const std::string& serie, const std::string& equipo,
                    int num, const std::string& detail) {
        SimWorldEvent e;
        e.year = cc.year; e.doy = cc.cursor_doy; e.kind = kind;
        e.series_id = serie; e.team = equipo; e.num = num; e.detail = detail;
        e.money = save.prog.credits;
        r.timeline.push_back(e);
    };

    while (restan > 0) {
        // the jump is at most 7 days, and the LAST one is the remainder: that way it lands exactly on the requested
        // date. Always advancing 7 at a time would overshoot, and for a simulator that exists to answer
        // "what happens up to March 3rd", overshooting the date means answering a different question.
        const int paso = restan < 7 ? restan : 7;
        restan -= paso;

        // (1) RUN BEFORE ADVANCING ------------------------------------------------------------
        // Every signed-up round whose weekend falls within the week we're about to consume runs NOW. If it's
        // let slip, DnfPassedRounds marks it an absence when advancing, and three absences break the contract on their own.
        // The guard of 64 is a safety cap: if a degenerate calendar left PendingSlot stuck on the
        // same round, this loop would never end. I'd rather cut it off visibly than hang the simulator.
        // ...AND ONE RACE PER DAY. This used to be walked SERIES BY SERIES, which makes the rule unenforceable: each
        // series exhausted its week's rounds without knowing about the others, and the player raced at Northford
        // and at Northham Ring the same Sunday. Measured: 12 contracts at once and ZERO absences in six years, which is
        // impossible -- exactly the ORGANIC limit that the multi-contract design takes for granted ("on
        // overlap, one race will be a DNF") never actually existed inside the simulator.
        // The walk is now DAY BY DAY because the rule belongs to the PLAYER, not the series. The unreachable round
        // is deliberately left PENDING: no penalty is invented here, DnfPassedRounds charges it when the
        // clock advances, with the same per-contract sting as in the game (applyNoShowSting).
        // EXCEPTION, the same one as BuildMasterCalendar: two races on the same day at the SAME circuit do fit
        // (a doubleheader venue).
        const int hasta = cc.cursor_doy + paso;
        for (int dia = cc.cursor_doy; dia < hasta; dia++) {
            bool corridaHoy = false; long sedeHoy = 0;
            for (auto& e : cc.enrollments) {
                for (int guard = 0; guard < 64; guard++) {
                    const CalSlot p = PendingSlot(cc, e.series_id);
                    if (!p.valid || p.doy != dia) break;      // nothing of theirs TODAY (or they have no rounds left)
                    if (corridaHoy && p.track_hash != sedeHoy) {
                        r.clashSkipped++;                     // another venue the same day: you can't be in two places
                        push("clash", e.series_id, "", dia, "");
                        break;                                // stays pending -> absence when advancing
                    }
                    const SimRoundResult rr = SimulateRound(e, save, playerName, baseSeed, RaceModelConfig(), perf);
                    r.races++;
                    if (rr.dnf_kind == (int)DnfKind::MechanicalDnf) r.dnfMech++;
                    else if (rr.dnf_kind == (int)DnfKind::ErrorDnf) r.dnfCrash++;
                    if (rr.mate_raced) {
                        if (rr.mate_lost) r.mateL++; else r.mateW++;
                        if (rr.mate_beef > r.mateBeefMax) r.mateBeefMax = rr.mate_beef;
                    } else if (rr.mate_why >= 0 && rr.mate_why < 6) {
                        r.mateWhy[rr.mate_why]++;
                    }
                    push("race", e.series_id, "", rr.player_pos, "");
                    corridaHoy = true; sedeHoy = p.track_hash;
                }
            }
        }

        // (2) the CLOCK, through the same path the app triggers ---------------------------------------
        // AdvanceDays moves the CURSOR and resolves what got left behind, but does NOT tick the world: the tick comes from
        // RunWorldTick, and the WHOLE pair has to be reproduced -- otherwise the clock runs and the transfer window
        // never opens, which is exactly the blind simulator this comes to fix. The test caught it
        // on the first try.
        // 18/09: and the pair is AdvanceDaysPulsed, ONE PULSE PER DAY, which is what the game does now. With the
        // old pair (move `paso` days at once + a single pulse) the simulator measured a world that beats more
        // slowly than the real one as soon as `paso` went above 1, and its numbers stopped describing the engine.
        const WorldSnap antes = takeSnap(save, cc);
        AdvanceDaysPulsed(cc, save.prog, save, playerName, paso);
        const WorldSnap ahora = takeSnap(save, cc);

        // (3) events are DERIVED from the diff, nobody emits them ----------------------------------
        if (ahora.year != antes.year) { r.rollovers++; push("rollover", "", "", ahora.year, ""); }
        // 18/09: a seat moved in this step (which is what would trigger the AMS2 restart warning)
        if (ahora.occStamp != antes.occStamp) push("seats", "", "", 0, "");
        if (ahora.windowYear != antes.windowYear && ahora.windowYear > 0) {
            r.windows++;
            push("window", "", "", (int)save.window_seats.size(), std::to_string(ahora.windowYear));
        }
        for (const auto& kv : ahora.seatState) {
            const auto it = antes.seatState.find(kv.first);
            if (it != antes.seatState.end() && it->second == kv.second) continue;   // no change
            const size_t bar = kv.first.find('|');
            const std::string serie = kv.first.substr(0, bar == std::string::npos ? 0 : bar);
            const std::string equipo = bar == std::string::npos ? kv.first : kv.first.substr(bar + 1);
            const char* kind = nullptr;
            switch (kv.second) {
                case WSEAT_SOUNDED:  kind = "sounded";  break;
                case WSEAT_OFFERED:  kind = "offered";  break;
                case WSEAT_REJECTED: kind = "rejected"; break;
                case WSEAT_RENEWED:  kind = "renewed";  break;
                default: break;                                   // FREE/DISPUTED: noise, not news
            }
            if (!kind) continue;
            std::string quien;
            int porQue = 0; double margen = 0.0; bool hayMargen = false;
            for (const auto& ws : save.window_seats)
                if (ws.series_id == serie && ws.team_label == equipo) {
                    quien = ws.winner_name; porQue = ws.reject_why;
                    margen = ws.fit_margin; hayMargen = (ws.fit_margin != 0.0);
                    break;
                }
            // the margin is averaged over EVERY judged dispute (won or lost), not just the losses: if
            // only the discards were looked at, the average would come out negative by construction and say nothing.
            if (hayMargen) { r.fitMarginSum += margen; r.fitMarginN++; }
            if (kv.second == WSEAT_REJECTED) {
                switch (porQue) {
                    case WSREJ_BAR:   r.rejBar++;   break;
                    case WSREJ_TABLE: r.rejTable++; break;
                    case WSREJ_BOTH:  r.rejBoth++;  break;
                    case WSREJ_RENEW: r.rejRenew++; break;
                    default: break;
                }
            }
            push(kind, serie, equipo, porQue, quien);
        }
        // ABSENCES: contracted rounds that never got run because the weekend clashed with another
        // series. They're read from the per-contract counter, the same one the auto-break hangs off, so
        // this can't disagree with the real penalty.
        for (const auto& kv : ahora.contract) {
            const auto it = antes.contract.find(kv.first);
            if (it != antes.contract.end() && kv.second > it->second) {
                const int nuevas = kv.second - it->second;
                r.noShows += nuevas;
                push("noshow", kv.first, "", nuevas, "");
            }
        }
        if ((int)ahora.contract.size() > r.maxConcurrent) r.maxConcurrent = (int)ahora.contract.size();

        for (const auto& kv : antes.contract) {                      // contracts that are ALREADY gone
            if (ahora.contract.count(kv.first)) continue;
            r.breaks++;
            // WHY it broke. Rollover retires expired contracts, and that's NOT a breach in the
            // painful sense; what's painful is the mid-season auto-break from accumulating absences.
            // Counting them together made "16 signings / 16 breaches" read like a disaster when it could just be the
            // normal end of every season.
            int motivo = 0;
            if (ahora.year != antes.year)                                  motivo = 1;   // expired when the year rolled over
            else if (kv.second >= bal::MK_NOSHOW_BREACH_LIMIT - 1)         motivo = 2;   // absences -> auto-break
            else                                                           motivo = 3;   // some other cause
            if (motivo == 1) r.breaksExpired++;
            else if (motivo == 2) r.breaksNoShow++;
            else r.breaksOther++;
            push("broken", kv.first, "", motivo, "");
        }
        // the drought is counted in WEEKS, not occurrences: what hurts isn't entering it, it's staying in it.
        if (ahora.drought) r.droughtDays += paso;
        if (ahora.drought && !antes.drought) push("drought", "", "", 0, "");

        // (4) the player's policy -------------------------------------------------------------
        // without this the simulation measures nothing past the first December: nobody signs, you end up without
        // a seat, and the following weeks watch a player who no longer races. It's what the player WOULD DO,
        // so it's part of the simulated world, not the scaffolding.
        if (signPolicy == SIMSIGN_GREEDY) {
            // APPLYING FIRST, and this was missing: measured on 2026-08-05, two simulated years produced ZERO races.
            // The window would open and teams would approach you, but ResolveDecisionWeek only judges what's
            // IN DISPUTE, and a seat isn't disputed until you apply for it. Without applying there's no verdict, without a
            // verdict there's no offer, without an offer there's no signing, and without signing you don't race. The whole chain
            // was hanging off a call nobody made.
            for (size_t i = 0; i < save.window_seats.size(); i++) {
                const int st = save.window_seats[i].state;
                if (st != WSEAT_FREE && st != WSEAT_SOUNDED) continue;
                if (save.window_seats[i].applied) continue;
                const std::string serie = save.window_seats[i].series_id;
                const std::string equipo = save.window_seats[i].team_label;
                if (ApplyToWindowSeat(save, serie, equipo)) {
                    r.applications++;
                    push("applied", serie, equipo, 0, "");
                }
            }
            for (size_t i = 0; i < save.window_seats.size(); i++) {
                if (save.window_seats[i].state != WSEAT_OFFERED) continue;
                const std::string serie = save.window_seats[i].series_id;
                const std::string equipo = save.window_seats[i].team_label;
                // the REASON for a failed signing is now captured: without this, a year with 24 seats offered and ZERO
                // signings (measured across all three profiles) is indistinguishable from a year with no market at all, and behind it there's
                // a months-long drought that looks like the world's fault when it's actually caused by one specific block.
                std::string porQueNo;
                if (SignWindowSeat(save, cc, serie, equipo, cc.year, &porQueNo)) {
                    r.signings++;
                    push("signed", serie, equipo, 0, "");
                    // the sign-up itself does NOT happen here: AcceptOffer does, which is the only signing gate the
                    // game has, and it knows which season it was signed for. Repeating it here was, at best, an
                    // idempotent no-op, and at worst it hid where the decision really lives.
                } else {
                    r.signFails++;
                    push("signfail", serie, equipo, 0, porQueNo);
                }
            }
        }
    }

    r.days = absHasta > absDesde ? (absHasta - absDesde) : 0;
    r.toYear = cc.year; r.toDoy = cc.cursor_doy;
    r.endState = SimSnapshot(save, cc);
    return r;
}

} // namespace career
