#include "career_engine.h"
#include "career_difficulty.h"    // 09/09 (): EffectiveAiForClass
#include "career_dda_offsets.h"   //   ... and the community seeding of a category you are new to
#include "career_balance.h"
#include "career_econ.h"
#include "career_rating.h"
#include "career_series.h"   // SeedAllDrivers production overload: AllSeries() -> SeriesDef.level
#include "career_fields.h"   // SeedAllDrivers production overload: FieldForSeries(seriesId)
#include "career_data_json.h"// DriverIdForName: resolve a PSV field name -> stable pool id (PSV fields carry no id)
#include "career_contracts.h"// WorldContracts -> the PRIMARY seat takes precedence over the order
#include "career_seats.h"    // FieldOccupantId -- the SAME id resolution used by seats and the grid
#include <algorithm>
#include <vector>
#include <map>       // Seeding resolves each driver's HIGHEST level before anchoring
#include <cmath>

namespace career {

int XpThreshold(int phase) {
    if (phase < 1) phase = 1; if (phase > 7) phase = 7;
    // per-phase thresholds (career_balance.h). A "~1.5-2 seasons per phase" model; replaces the old 5000*1.6^ curve.
    return (int)bal::PHASE_XP_THRESH[phase - 1];
}

bool CheckPhaseUnlock(long xpTowardNext, int frontier) {
    return xpTowardNext >= XpThreshold(frontier);
}

int XpFor(int pos, int field, bool clean, int racePhase, int frontier) {
    if (field < 1) field = 1;
    if (pos < 1) pos = 1; if (pos > field) pos = field;
    // base lerps 1000 -> 200 by finishing position
    double t = (field > 1) ? (double)(pos - 1) / (double)(field - 1) : 0.0;
    double base = bal::XP_BASE_FIRST + (bal::XP_BASE_LAST - bal::XP_BASE_FIRST) * t;
    if (clean)    base *= (1.0 + bal::XP_CLEAN_BONUS);
    if (pos <= 3) base *= (1.0 + bal::XP_PODIUM_BONUS);
    int gap = frontier - racePhase; if (gap < 0) gap = 0;     // you don't race above the frontier
    double rel = std::pow(bal::XP_RELEVANCE, gap);
    return (int)(base * rel + 0.5);    // rounded to an integer
}

static double repClampFactor(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
int RepFor(int pos, bool finished, int field, int aiLevel) {
    if (!finished) return bal::REP_DNF;          // a DNF is ALWAYS -5, unscaled
    int base;
    if (pos == 1) base = bal::REP_WIN;
    else if (pos <= 3) base = bal::REP_PODIUM;
    else if (pos <= 10) base = bal::REP_POINTS;
    else return 0;
    double f = 1.0;
    if (field > 0)   f *= repClampFactor((double)field   / bal::REP_FIELD_REF, bal::REP_FIELD_LO, bal::REP_FIELD_HI);
    if (aiLevel > 0) f *= repClampFactor((double)aiLevel / bal::REP_AI_REF,    bal::REP_AI_LO,    bal::REP_AI_HI);
    int v = (int)(base * f + 0.5);
    if (v < 1) v = 1;                            // a scoring result never rounds down to 0 after scaling
    return v;
}

int RepTier(int rep) {
    if (rep >= bal::REP_TIER3) return 3;
    if (rep >= bal::REP_TIER2) return 2;
    if (rep >= bal::REP_TIER1) return 1;
    return 0;
}

int RepDisplayBand(int rep) {
    int b = 0;
    for (int i = 0; i < 6; i++) if (rep >= bal::REP_BAND[i]) b = i;
    return b;
}

// Display-band chrome (mirror of market::REP_TIERS -- keep in sync; both anchored to bal::REP_BAND).
// 6 bands: Unknown / Prospect / Known / Established / Star / Legend.
const char* RatingTierLabel(int rep, int lang) {
    static const char* EN[6] = { "Unknown", "Prospect", "Known", "Established", "Star", "Legend" };
    static const char* ES[6] = { "Desconocido", "Promesa", "Conocido", "Consolidado", "Estrella", "Leyenda" };
    int b = RepDisplayBand(rep); if (b < 0) b = 0; if (b > 5) b = 5;
    return (lang == 1) ? ES[b] : EN[b];
}
unsigned RatingTierColor(int rep) {
    // ARGB packed as ImGui IM_COL32(r,g,b,a) order: 0xAABBGGRR. Mirrors market::REP_TIERS colors.
    static const unsigned C[6] = {
        0xff80726bu, // Unknown     (6b,72,80)
        0xffb2a39au, // Prospect    (9a,a3,b2)
        0xffffc85eu, // Known       (5e,c8,ff)
        0xff7cd219u, // Established  (19,d2,7c)
        0xff4dcfffu, // Star         (ff,cf,4d)
        0xff2e4dffu, // Legend       (ff,4d,2e)
    };
    int b = RepDisplayBand(rep); if (b < 0) b = 0; if (b > 5) b = 5;
    return C[b];
}

double RatingSeedPlayer() { return bal::RATING_PLAYER_SEED; }   // the player starts at the world's floor level

int RatingRepTier(double r) {
    int t = 0;
    for (int i = 0; i < 6; i++) if (r >= bal::REP_TIER_RATING[i]) t = i;
    return t;
}
int RatingDisplayBand(double r) { return RatingBand(r); }

int SeedDriverRatings(std::map<std::string, double>& ratings,
                      const std::vector<SeedFieldEntry>& field) {
    const int fieldSize = (int)field.size();
    int seeded = 0;
    for (const auto& e : field) {
        if (e.id.empty()) continue;                         // unidentified entry -> skip
        if (ratings.find(e.id) != ratings.end()) continue;  // already present (evolved) -> leave it
        ratings[e.id] = RatingSeedFromOrder(e.order, fieldSize, e.iconic);
        seeded++;
    }
    return seeded;
}

// Whole-pool materialization. Same idempotent lazy seed as SeedDriverRatings
// (the two `continue` guards are kept verbatim), but it walks EVERY series in `pool` and anchors each
// driver by that series' ladder LEVEL via LevelAnchoredSeed (not the per-field RatingSeedFromOrder
// wall). Pure: no globals, no save IO -- the caller passes g_save.driver_ratings.
int SeedAllDrivers(std::map<std::string, double>& ratings,
                   const std::vector<SeedSeriesEntry>& pool) {
    // THIS pass is still “first in the walk wins” — the idempotence guard below — and
    // that is FINE here: it is the FILL pass. Whoever has a seat already comes seeded by their primary
    // series from pass 0 of the production caller (), so the only thing that reaches here
    // is people who appear in a field and hold no seat anywhere. For those there is no “their” series to
    // pick, and the first in the walk is as good an answer as any.
    //
    // HISTORY, because it took two attempts: until 10/08 this was the ONLY pass, so the ELO of anyone who
    // runs in several categories was decided by the order of `AllSeries()` — a production datum at the mercy of the
    // order of a file. Measured then: ELO range within a class up to 802 points not counting the
    // cohort, with `LEVEL_SEED_SPREAD` at 350.
    //
    // And fixing it by seeding at the HIGHEST level they run at was TRIED, and reverted: the spread barely
    // improved and the drivers pinned to the floor went from 15 to 27, because anchoring high turns those who
    // double up into outliers of the lower grids. What finally went in is something else —the PRIMARY
    // seat of , which is the highest category AMONG THEIRS— and it only worked with the world already placed
    // by the . See two_talent_scales_in_one_grid.
    int seeded = 0;
    for (const auto& s : pool) {
        const int fieldSize = (int)s.field.size();
        for (const auto& e : s.field) {
            if (e.id.empty()) continue;                         // unidentified entry -> skip
            if (ratings.find(e.id) != ratings.end()) continue;  // already present (evolved) -> leave it
            ratings[e.id] = LevelAnchoredSeed(s.level, e.order, fieldSize, e.iconic);
            seeded++;
        }
    }
    return seeded;
}

// Production entry point: build the seed pool from the REAL ladder/series and materialize it. Each
// series contributes its SeriesDef.level (ladder prestige) and FieldForSeries(id) (order/iconic/
// stable id). Delegates to the pure overload so the idempotence + anchoring live in one place. The
// caller (overlay PregenAllAi) runs this before WriteAllClassGrids so boot grids reflect real ratings.
int SeedAllDrivers(std::map<std::string, double>& ratings) { return SeedAllDrivers(ratings, nullptr); }

int SeedAllDrivers(std::map<std::string, double>& ratings, const SaveData* save) {
    // — THE PRIMARY SEAT DECIDES, not the file order. See career_engine.h.
    // It is resolved BEFORE walking anything: with the map in hand, the pass below can keep the
    // entry of ITS series instead of the first one that comes along.
    const SaveData mundoEstatico;                      // no game save = year 1, derived from the data
    std::map<std::string, std::string> serieDelPrincipal;
    for (const auto& c : WorldContracts(save ? *save : mundoEstatico))
        if (c.primary) serieDelPrincipal[c.driver_id] = c.series_id;

    std::vector<SeedSeriesEntry> pool;
    std::vector<std::string> poolSeries;          // the series of each entry (pass 0 needs it)
    const std::vector<SeriesDef>& series = AllSeries();
    pool.reserve(series.size()); poolSeries.reserve(series.size());
    for (const auto& s : series) {
        SeedSeriesEntry e;
        e.level = s.level;
        std::vector<FieldDriver> field = FieldForSeries(s.id);
        e.field.reserve(field.size());
        for (const auto& fd : field) {
            // .5 (10/08) — HERE WAS HALF OF THE TWO TALENT SCALES, and no longer.
            //
            // This resolved the id on its own and returned "" when the name was not in the pool, so
            // `SeedAllDrivers` SKIPPED the row: 792 of 2,253 drivers with no rating, their talent coming out
            // of the static curve while their rivals in the SAME car came out of RatingQuality(ELO) --
            // gap 0.461 in 72 of 100 classes. In Ivory_Cup_10072 the first eight were exactly the eight
            // unseeded: the ladder was not talent, it was having an ELO or not.
            //
            // `FieldOccupantId` is THE shared resolution (id column -> pool by name+country -> stable
            // hash), the same one used by the seats and the grid composition. Measured after the change:
            // unseeded 792 -> 60, classes that mix 72 -> 11.
            //
            // This could NOT be brought in on its own, which is why it took so long: with everyone on the same scale, the
            // cohort seeded without a level term ate the top grids (it led 48 of 100).
            // It goes in TOGETHER with the cohort anchoring, a few lines below. Both halves or neither.
            e.field.push_back(SeedFieldEntry{ FieldOccupantId(fd), fd.order, fd.iconic });
        }
        // .5 — AND THE PINNED ROSTERS TOO, which was the other half of the hole. This is where the
        // people of the 35 series with a curated pack come from, and this pass did not walk them: their ELO did not exist and their
        // talent came out of the static curve. The file order IS the standings order, so row i is
        // the i-th of the real season — the same `order` a field carries.
        // — THE FIELD AND THE PINNED ROSTER ARE TWO STANDINGS, NOT ONE, and until today they were added
        // into the same denominator. `LevelAnchoredSeed(level, order, fieldSize, ...)` spreads the place over
        // the size of ITS list; by merging them, 29 series (the ones that have both) measured the places against
        // a size that does not exist: in beacon_cup_9211 the first of the pinned roster —place 1 of 33— was spread
        // over 57, and the last of the field —24 of 24— came out as if it were 24 of 57, i.e. half a grid.
        // They are emitted as TWO entries of the same series, each with its own size.
        if (!e.field.empty()) { poolSeries.push_back(s.id); pool.push_back(std::move(e)); }
        std::vector<PinnedDriver> pin = PinnedRosterForSeries(s.id);
        if (!pin.empty()) {
            SeedSeriesEntry p;
            p.level = s.level;
            p.field.reserve(pin.size());
            for (int i = 0; i < (int)pin.size(); i++)
                p.field.push_back(SeedFieldEntry{ PinnedOccupantId(pin[i]), i + 1, false });
            poolSeries.push_back(s.id);
            pool.push_back(std::move(p));
        }
    }
    // , PASS 0: whoever has a PRIMARY seat is seeded with the series of THAT seat,
    // whatever happens afterwards. It is the only thing needed to take the decision away from the file order: the
    // idempotence guard of the normal pass already respects whatever this one leaves in place.
    // Measured with the world placed (): classes with u>2 from 22 to 18, hierarchy intact.
    int seeded = 0;
    for (size_t si = 0; si < pool.size(); si++) {
        const int fieldSize = (int)pool[si].field.size();
        for (const auto& e : pool[si].field) {
            if (e.id.empty() || ratings.count(e.id)) continue;
            const auto pr = serieDelPrincipal.find(e.id);
            if (pr == serieDelPrincipal.end() || pr->second != poolSeries[si]) continue;
            ratings[e.id] = LevelAnchoredSeed(pool[si].level, e.order, fieldSize, e.iconic);
            seeded++;
        }
    }
    seeded += SeedAllDrivers(ratings, pool);
    // .5 — THE COHORT, ANCHORED TO ITS HOME LEVEL (user decision, 10/08/2026).
    //
    // Until today it was seeded with `CastSeedFromBase`: the authored base_skill mapped onto 1150..2250 WITHOUT a
    // level term. It was deliberate (02/07: "the worst generation", monsters from day one) and
    // it worked while the real ones did not find their ELO. As soon as they do -- which is the other half
    // of this phase -- the cohort came to lead 48 of 100 grids, Ivory_Cup_10072 with the first three from
    // the academy ahead of Serra, Giaffone and Girolami. Measured with `career_test --seedlab`.
    //
    // The base_skill is NOT thrown away: it still decides the ORDER of the cast (who is the apex, who is last).
    // What changes is the SCALE -- that order is placed around the midpoint of its home tier, like
    // any other driver. They start low and climb by results, which is by design.
    // With the anchoring: cohort leads 3 of 100, gap 0.141, classes with u>2 from 33 to 28.
    //
    // The cast is the SAME for the seven tiers (RosterForTier returns the whole cohort), so it is
    // sorted once by base_skill and seeded once. Idempotence guard same as before.
    {
        std::vector<RosterDriver> cast = RosterForTier(1);
        std::sort(cast.begin(), cast.end(), [](const RosterDriver& a, const RosterDriver& b) {
            if (a.base_skill != b.base_skill) return a.base_skill > b.base_skill;
            return a.id < b.id;   // stable tiebreak: seeding cannot depend on the file order
        });
        for (int i = 0; i < (int)cast.size(); i++) {
            const RosterDriver& r = cast[i];
            if (r.id.empty()) continue;
            if (ratings.find(r.id) != ratings.end()) continue;   // already present (evolved) -> not touched
            ratings[r.id] = CohortAnchoredSeed(i + 1, (int)cast.size(), r.hot);
            seeded++;
        }
    }
    // CRITICAL ORDER (fixed 11/08): the SEATS pass goes AFTER the cohort, and until today it
    // went before. A cohort member holding a seat was seeded as an ordinary occupant
    // of THAT level, and the idempotence guard meant their anchoring was NEVER applied: they ended up with the
    // ELO of their home tier instead of their own. Measured — not a single one appeared in the top 8 of the
    // level-1 grids where they do sit, having a minimum seed (1225) above the first of the
    // grid (1325). Two idempotent passes do NOT commute: the one in charge has to go first.
    // — AND EACH SERIES' OWN PEOPLE, which nobody seeded until now.
    //
    // Moving to one seat per running car () brought up 370 slots that neither the pinned roster nor the
    // field cover, and they are filled by the series' own people (`fl_*`). Those people RUN — they appear in the XML, their
    // name is visible — and had no ELO, so their talent came out of the static curve while that of their rival in the
    // same car came out of RatingQuality(ELO). It is exactly the disease that .5 closed, again and
    // in the new people: measured, 223 of 2,287 rows on two different scales.
    //
    // It is seeded with the SAME anchor as everyone: the level of its series and its place in it (the seat index
    // is the standings order, same as row i of a pinned roster). The idempotence guard of
    // `SeedAllDrivers` applies here equally — whoever already has a rating is not touched — so this only adds those who
    // were missing and cannot overwrite an evolved rating.
    for (const auto& s : series) {
        const std::vector<std::string> ocupantes = DerivedSeatOccupants(s.id);
        const int n = (int)ocupantes.size();
        for (int i = 0; i < n; i++) {
            if (ocupantes[i].empty() || ratings.count(ocupantes[i])) continue;
            ratings[ocupantes[i]] = LevelAnchoredSeed(s.level, i + 1, n, /*iconic=*/false);
            seeded++;
        }
        // 11/08 — AND THE PACK DRIVERS OF THE PERIOD MEMORIES. Those have no seats (they are
        // frozen), so the pass above does not see them; but since the cohort stopped filling them
        // () their grid is completed by the class pack, and those people RUN. Without seeding them the
        // two scales in the same car came back: measured, 4 rows and 1 class as soon as the pack fill came in.
        const int gs = GridSizeForClassName(s.class_name);
        for (int k = 0; k < gs; k++) {
            const PackDriver* pk = PackDriverForClassSlot(s.class_name, k);
            if (!pk) break;
            const std::string pid = PackDriverId(*pk);
            if (pid.empty() || ratings.count(pid)) continue;
            ratings[pid] = LevelAnchoredSeed(s.level, k + 1, gs, /*iconic=*/false);
            seeded++;
        }
    }
    return seeded;
}

int DropUnracedSeeds(std::map<std::string, double>& ratings,
                     const std::map<std::string, int>& races) {
    return DropUnracedSeeds(ratings, races, std::set<std::string>());
}

int DropUnracedSeeds(std::map<std::string, double>& ratings,
                     const std::map<std::string, int>& races,
                     const std::set<std::string>& keep) {
    int dropped = 0;
    for (auto it = ratings.begin(); it != ratings.end(); ) {
        auto r = races.find(it->first);
        if ((r == races.end() || r->second <= 0) && !keep.count(it->first)) { it = ratings.erase(it); ++dropped; }
        else ++it;
    }
    return dropped;
}

double ReqRepRating(int level, bool works) {
    int idx = level - 1; if (idx < 0) idx = 0; if (idx > 6) idx = 6;
    if (!works) {
        if (level <= 2) return 0.0;                      // open ad, low tier -> free entry
        return bal::REQREP_RATING[idx];
    }
    // works/closed seat: scale x1.15, never below the rookie base (a signing always asks for some standing).
    double req = bal::REQREP_RATING[idx] * 1.15;
    if (req < bal::RATING_ROOKIE_BASE) req = bal::RATING_ROOKIE_BASE;
    return req;
}

double AudienceRepFactor(int audience);   // forward declaration (implemented in career_calendar.cpp) -- avoids pulling in the heavy header

RatingSettleReport SettleRating(CareerProgress& prog,
                                const std::vector<double>& ratingsInFinishOrder,
                                int playerIdx, int aiPct, DnfKind dnf,
                                int audience) {
    RatingSettleReport rep;
    rep.before = prog.rating;
    rep.band_before = RatingBand(prog.rating);
    rep.tier_before = RatingRepTier(prog.rating);

    double delta = 0.0;
    const double k = RatingK(prog.rating_races, /*isPlayer=*/true);

    if (dnf == DnfKind::MechanicalDnf) {
        delta = 0.0;                                   // no contest, neutral
    } else if (dnf == DnfKind::NoShow) {
        delta = -bal::RATING_NOSHOW_DING;              // small flat global ding, no duels
    } else {
        // Finished OR ErrorDnf: the caller has the player at playerIdx in the finishing order.
        const int n = (int)ratingsInFinishOrder.size();
        if (n >= 2 && playerIdx >= 0 && playerIdx < n) {
            std::vector<double> deltas = RatingUpdateRace(ratingsInFinishOrder, aiPct, k);
            delta = deltas[playerIdx];
        }
    }

    // audience effect only pushes UPWARD -- a good race at a more-watched event earns more ELO/rep;
    // a bad one doesn't get punished harder for being popular (only the positive delta is scaled).
    if (delta > 0.0) delta *= AudienceRepFactor(audience);
    prog.rating += delta;
    // FLOOR. There wasn't one, and the rating could go NEGATIVE -- measured with the long-run simulator:
    // 19.1 after two years averaging P22 finishes. A negative ELO isn't "very bad", it's a number outside the
    // domain the model itself declares: RATING_BAND_MIN[0] is 0.0, i.e. the lowest band STARTS
    // at zero. And it's not cosmetic: RatingQuality(rating) feeds the player's pace in the race model,
    // so below the floor it keeps subtracting even though quality can't drop any further -- an invisible debt that
    // has to be paid back before a good race shows up at all. The floor comes from the band table itself, not a
    // brand-new constant: the model already says where its own scale ends.
    if (prog.rating < bal::RATING_BAND_MIN[0]) prog.rating = bal::RATING_BAND_MIN[0];
    prog.rating_races += 1;                            // every settled race counts toward the provisional window
    rep.after = prog.rating;
    rep.delta = delta;
    rep.band_after = RatingBand(prog.rating);
    rep.tier_after  = RatingRepTier(prog.rating);
    return rep;
}

// Whole-grid sibling of SettleRating. Mirrors the player path (dynamic K +
// the lazy-seed idiom) but for EVERY AI finisher. Design choices (documented per the plan):
//   * DNF v1 rule: the AMS2 SHM split (5 RETIRED / 6 DNF) can't cleanly separate mechanical vs error, so
//     a DNF car is EXCLUDED from the duel field (no contest) -- but its race still counts toward the
//     provisional window (driver_races bumped). This is the v1 rule; a finer mechanical/error split is later.
//   * The PLAYER is updated by SettleRating, never here: an is_player entry is skipped (rating + race-count
//     untouched) so the player is not double-updated.
//   * Dynamic K + strict zero-sum: RatingUpdateRace takes ONE k for the whole field (the only way it stays
//     strictly zero-sum). We pass the AGGREGATE K = mean of the per-driver RatingK(driver_races[id], false)
//     (AI => isPlayer=false). An all-unproven field therefore has a higher aggregate K and swings more
//     than a settled field for the same finishing result, while the applied deltas still sum to 0.
// Pure: no globals, no SHM, no I/O -- the overlay assembles `finishOrder` and calls this.
void SettleRatingGrid(std::map<std::string, double>& driver_ratings,
                      std::map<std::string, int>& driver_races,
                      const std::vector<GridFinisher>& finishOrder) {
    // Build the duel field (the contesting cars) IN FINISHING ORDER: skip empty ids (the SeedAllDrivers
    // guard), the player (SettleRating owns them), and DNFs (v1: no contest). Lazily seed any absent id
    // via LevelAnchoredSeed BEFORE reading its rating, so an unseen grid driver enters at its anchored level
    // (same idempotent lazy seed as SeedAllDrivers -- never overwrite an evolved rating).
    std::vector<const GridFinisher*> contenders;   // parallel to ratingsInFinishOrder below
    std::vector<double> ratingsInFinishOrder;
    double kSum = 0.0;
    for (const auto& gf : finishOrder) {
        if (gf.id.empty() || gf.is_player) continue;          // unidentified / player -> not in the AI duel field
        if (driver_ratings.find(gf.id) == driver_ratings.end())   // absent -> lazy seed (idempotent)
            driver_ratings[gf.id] = LevelAnchoredSeed(gf.level, gf.order, gf.field_size, gf.iconic);
        if (gf.dnf) continue;                                 // v1: DNF does not contest (still counts a race below)
        contenders.push_back(&gf);
        ratingsInFinishOrder.push_back(driver_ratings[gf.id]);
        kSum += RatingK(driver_races[gf.id], /*isPlayer=*/false);   // per-driver dynamic K -> aggregate (mean) below
    }
    // Apply the zero-sum whole-field deltas with the AGGREGATE (mean) K (>=2 contenders, else no contest).
    const int n = (int)ratingsInFinishOrder.size();
    if (n >= 2) {
        const double kAgg = kSum / (double)n;                 // mean per-driver K (keeps strict zero-sum)
        // aiPct = 0 ALWAYS: AI-vs-AI duels (see career_engine.h). With any other value the
        // deltas stop summing to zero and the whole ladder drifts with the player's slider.
        std::vector<double> deltas = RatingUpdateRace(ratingsInFinishOrder, /*aiPct=*/0, kAgg);
        for (int i = 0; i < n; i++)
            driver_ratings[contenders[i]->id] += deltas[i];
    }
    // Bump the race-count for every settled grid driver (incl. DNFs; excl. empty id + player): the race
    // happened for them, so it advances their provisional window even when they did not contest the duels.
    for (const auto& gf : finishOrder) {
        if (gf.id.empty() || gf.is_player) continue;
        driver_races[gf.id] += 1;
    }
}

// (SponsorRetainerPerRace -- the flat per-REP-band retainer from the old v1 sponsor system --
//  retired along with its balance table. See career_balance.h for why.)

double RewardMult(int rewardMode) {
    if (rewardMode < 0) rewardMode = 0; if (rewardMode > 2) rewardMode = 2;
    return bal::REWARD_MULT[rewardMode];
}
const char* RewardModeToken(int rewardMode) {
    if (rewardMode <= 0) return "easy";
    if (rewardMode >= 2) return "hard";
    return "normal";
}
int RewardModeFromToken(const std::string& token) {
    if (token == "easy") return 0;
    if (token == "normal") return 1;
    if (token == "hard") return 2;
    return -1;
}
long ScaleGain(long amount, double mult) {
    if (amount <= 0) return amount;   // only scales GAINS: costs and penalties are always paid in full
    return (long)(amount * mult + 0.5);
}

long PrizeFor(int pos, long winnerPrize, long floorPrize) {
    if (pos < 1) pos = 1;
    long p = (long)(winnerPrize * std::pow(bal::PRIZE_DECAY, pos - 1) + 0.5);
    return (p > floorPrize) ? p : floorPrize;
}

SettleReport Settle(CareerProgress& prog, const RaceResult& r,
                    long winnerPrize, long floorPrize, bool rented, int rentPct, long carValue,
                    long entryOverride, bool factorySeat, long coverage, long dealSalary,
                    long deductible, long travel, long resultBonus, int raceLaps,
                    const WearTelemetry* wear, long repBonus) {
    SettleReport s;
    s.pos = r.pos; s.field = r.field;     // copy of the result, for the UI
    // income
    s.prize       = PrizeFor(r.pos, winnerPrize, floorPrize);
    s.clean_bonus = r.clean ? (long)(s.prize * bal::XP_CLEAN_BONUS + 0.5) : 0;
    // A FACTORY seat -- the team covers entry+costs ("you don't pay any more, you get paid").
    // A salaried seat's pay is NO LONGER the flat
    // FACTORY_SEAT_SALARY -- it comes through dealSalary (= the negotiated contract.per_race, see s.deal_salary below).
    s.salary      = 0;
    // costs
    s.entry       = factorySeat ? 0
                  : (entryOverride >= 0) ? entryOverride : EntryForPhase(r.race_phase);   // category (override) or phase
    //   RENTED -> rentPct% of the CAR'S VALUE (rentPct = flat_rent/buy_price): insurance+consumables included.
    //                referencing the car's value (not the prize) makes rent work for
    //                heritage classes too (prize=0), and it reproduces the table's absolute values
    //   OWNED     -> RUN_COST_PCT % of the car's value (insurance+consumables). The capital was already paid at PURCHASE.
    //   AMENDMENT: an OWNED car uses the per-component breakdown (OwnedRaceCosts): consumables
    //   scale with LAPS (raceLaps) + flat insurance; their total == carValue*RUN_COST_PCT at the reference
    //   race length (raceLaps == COST_REF_LAPS -> back-compat).
    //   and if the overlay brings REAL wear from shared memory, the consumables are re-scaled on top of that nominal
    //   value (ApplyWearToCosts): easy on the tires/fuel makes it cheaper, hammering them makes it pricier. No telemetry -> nominal.
    //   Only applies to an OWNED car (for a rental, consumables are baked into the rent price).
    long ownedCosts = 0;
    if (!factorySeat && !rented) {
        RaceCostBreakdown rc = OwnedRaceCosts(carValue, raceLaps);
        if (wear) rc = ApplyWearToCosts(rc, *wear);
        ownedCosts = rc.total;
    }
    s.costs       = factorySeat ? 0
                  : rented ? (long)(carValue * (rentPct / 100.0) + 0.5)
                           : ownedCosts;
    // Sponsor coverage reduces your out-of-pocket (net = income - your-share); never exceeds entry+costs.
    long coverable = s.entry + s.costs;
    s.coverage    = coverage < 0 ? 0 : (coverage > coverable ? coverable : coverage);
    // Per-race deal salary (paid every race, any seat type; separate from the factory-seat s.salary).
    s.deal_salary = dealSalary < 0 ? 0 : dealSalary;
    // Insurance deductible + travel-leg cost net of coverage are INDEPENDENT
    // SUBTRACTED terms (NOT part of the coverable clamp above -- like sponsor_payout, they sit outside it);
    // the result bonus is an INDEPENDENT ADDED term. All clamp >= 0 and default 0 = byte-identical.
    s.deductible   = deductible  < 0 ? 0 : deductible;     // always the player's, never covered by the team
    s.travel       = travel      < 0 ? 0 : travel;         // already net of the type's covered fraction
    s.result_bonus = resultBonus < 0 ? 0 : resultBonus;   // low-tier Top10/Top5/win bonus
    s.rep_bonus    = repBonus    < 0 ? 0 : (int)repBonus; // event REP bonus (an exact mirror)
    // The thematic fixed payout is a DISTINCT field summed into net here so the displayed
    // MONEY lines sum to credits_delta once the OVERLAY fills s.sponsor_payout post-Settle (default 0 = old
    // behavior). It does NOT relabel s.coverage.
    // + result_bonus - deductible - travel (each defaulted 0 -> a bare Settle is byte-identical).
    // the flat per-REP-band retainer (s.sponsor) no longer exists -- sponsorship now comes through
    // CARDS (sponsor_payout) and the contract (deal_salary/coverage), not from merely having REP.
    // REWARD MODE (prog.reward_mode, chosen at profile creation and fixed). Scales what you EARN:
    // the INCOME terms, one at a time, and NOT the total -- so the Results panel's lines still add up to
    // the net amount (an invariant the query.results contract relies on). Deliberately left OUT:
    //   - entry/costs/deductible/travel -> costs are paid in full in all three modes (otherwise "hard" would
    //     get cheaper on its own and stop being hard).
    //   - coverage -> it's a REIMBURSEMENT of YOUR costs, clamped to entry+costs; scaling it would turn it into
    //     money out of thin air, beyond what you actually spent.
    //   - sponsor_payout -> always 0 here (the overlay fills it in after Settle, from the cards).
    // Cards are settled AFTERWARD using this already-scaled rep.prize, so their multiplicative part inherits
    // the mode; their flat bonus/penalty is not scaled (that's a separate layer, the card meta-game's).
    const double rw = RewardMult(prog.reward_mode);
    s.prize        = ScaleGain(s.prize,        rw);
    s.clean_bonus  = ScaleGain(s.clean_bonus,  rw);   // it already came from the NOMINAL prize: it's scaled only once
    s.result_bonus = ScaleGain(s.result_bonus, rw);
    s.salary       = ScaleGain(s.salary,       rw);
    s.deal_salary  = ScaleGain(s.deal_salary,  rw);
    s.credits_delta = s.prize + s.clean_bonus + s.salary + s.deal_salary
                    - s.entry - s.costs + s.coverage + s.sponsor_payout
                    + s.result_bonus - s.deductible - s.travel;
    // XP / REP -- scale the same as money. ScaleGain only touches the POSITIVE side: a REP drop from a DNF
    // hurts the same in all three modes ("easy" doesn't protect you from the penalty either).
    s.xp_gained = (int)ScaleGain(XpFor(r.pos, r.field, r.clean, r.race_phase, prog.frontier_phase), rw);
    // + rep_bonus (special event) -- an independent additive term, like result_bonus is for the net.
    s.rep_delta = (int)ScaleGain(RepFor(r.pos, r.finished, r.field, r.ai_level) + s.rep_bonus, rw);
    // apply to progress
    prog.credits        += s.credits_delta;
    EconLog("race_settle", s.credits_delta, prog.credits);   // race net (the breakdown lives in [settle])
    prog.xp_total       += s.xp_gained;
    prog.xp_toward_next += s.xp_gained;
    prog.rep            += s.rep_delta; if (prog.rep < 0) prog.rep = 0;
    // Cleanliness EMA toward 100*cleanRace. r.clean is the REAL incident signal
    // (set by the overlay's SHM latch; the deterministic sim sets its finish-proxy). On a fresh/old save
    // (cleanliness==0.0) seed the rookie baseline (70) BEFORE the first EMA step so a clean rookie debut
    // nudges up from 70, not from 0. A DNF is never counted as "clean" regardless of
    // r.clean (a non-finish is always treated as dirty) -- using the available r.finished signal (no DNF-kind detail in Settle).
    {
        double cleanRace = (r.clean && r.finished) ? 1.0 : 0.0;
        if (prog.cleanliness == 0.0) prog.cleanliness = bal::CLEAN_ROOKIE_SEED;
        prog.cleanliness += bal::CLEAN_ALPHA * (100.0 * cleanRace - prog.cleanliness);
    }
    // phase unlock? (can chain if the surplus clears the next threshold too)
    while (prog.frontier_phase < 7 && CheckPhaseUnlock(prog.xp_toward_next, prog.frontier_phase)) {
        prog.xp_toward_next -= XpThreshold(prog.frontier_phase);
        prog.frontier_phase++;
        s.phase_unlocked = true;
    }
    s.new_phase = prog.frontier_phase;
    return s;
}

// ~~~09/09 (): see career_difficulty.h. The SAME calculation as ClassAiNow + CalUiSkillForLevel
//  (career_ui_bridge.inc / career_race_settle.inc), with no latch or log, so that pregen writes the XML of each
//  class at the slider it will run at.
// ~~~09/09 (, 2): see career_difficulty.h. The starting point of a category you are new to.
int ClassColdStartFor(const CareerProgress& prog, const std::string& classId, int* familyClasses) {
    std::vector<int> todas, familia;
    const CarFamily fam = FamilyForClass(classId);
    for (const auto& kv : prog.class_dda) {
        if (kv.second.races <= 0 || kv.second.ai <= 0) continue;
        todas.push_back(kv.second.ai);
        if (kv.first != classId && FamilyForClass(kv.first) == fam) familia.push_back(kv.second.ai);
    }
    if (familyClasses) *familyClasses = (int)familia.size();
    const int general = ClassGeneralFrom(todas, prog.ai_difficulty);
    return familia.empty() ? general : ClassGeneralFrom(familia, general);   // the family average, rounded the same way
}
// ~~~09/09 (, 4): see career_difficulty.h.
int ClassSaturation(const ClassDdaState& st, bool wet) {
    const int numero = wet ? st.ai_wet : st.ai;
    const std::vector<float>& ventana = wet ? st.gap_wet : st.gap;
    if (numero <= 0 || (int)ventana.size() < dda::kWindow) return 0;
    std::vector<double> ult(ventana.end() - dda::kWindow, ventana.end());
    std::sort(ult.begin(), ult.end());
    const size_t m = ult.size() / 2;
    const double med = (ult.size() % 2) ? ult[m] : 0.5 * (ult[m - 1] + ult[m]);
    if (numero >= dda::kAiMax && med >  dda::kDeadbandPct) return  1;   // at the ceiling and you are still faster
    if (numero <= dda::kAiMin && med < -dda::kDeadbandPct) return -1;   // at the floor and you are still slower
    return 0;
}
int ClassNumberFor(const CareerProgress& prog, const std::string& classId, bool wet) {
    const int com = dda::ClassOffsetFor(classId);
    auto it = prog.class_dda.find(classId);
    if (wet && it != prog.class_dda.end() && it->second.races_wet > 0 && it->second.ai_wet > 0)
        return ClassAiFor(it->second.races_wet, it->second.ai_wet, 0, 0);            // the WET number, if there is one
    if (it != prog.class_dda.end() && it->second.races > 0)
        return ClassAiFor(it->second.races, it->second.ai, /*general=*/0, /*com=*/0);   // el suyo (clamp)
    return ClassAiFor(0, 0, ClassColdStartFor(prog, classId, nullptr), com);              // debut: game save + community
}
int EffectiveAiForClass(const CareerProgress& prog, const std::string& classId, int level, bool wet) {
    const int numero = ClassNumberFor(prog, classId, wet);
    return ResolveRaceSkillLayered(prog.difficulty_mode, level, numero, prog.adaptive_trim,
                                   /*offClass=*/0, /*offTrack=*/0, prog.custom_difficulty);
}

} // namespace career
