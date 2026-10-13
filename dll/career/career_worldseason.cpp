#include "career_worldseason.h"

#include "career_customai.h"    // GridDriver
#include "career_engine.h"      // GridFinisher, SettleRatingGrid
#include "career_fields.h"      // ComposeSeasonGrid, IsModernSeries
#include "career_racemodel.h"   // RaceEntrant, SimulateRace
#include "career_save.h"        // SaveData
#include "career_scoring.h"     // PointsSystem
#include "career_seats.h"       // SeatsForClass, SeatRowsForSeries
#include "career_series.h"      // AllSeries, SeriesById, PointsById, IsEraMemorySeries
#include "career_simrunner.h"   // RoundSeed
#include <cmath>

#include <algorithm>

namespace career {

namespace {

// The same set the market moves (`EsMercadoVivo`, career_seats.cpp). The condition is replicated instead
// of exported because here the reason is different and it's worth it being readable:
//
// ERA series that are in the calendar ARE run (their seats exist), but their market is
// frozen, so a championship of theirs would have no consumer. And there are 135 drivers with a seat in an
// era series AND in a modern one at once: resolving both seasons for them would charge the ELO twice and
// mix two eras into a single number. The world that runs is the world that signs.
bool CorreEnElMundoVivo(const SeriesDef& sd) {
    if (IsEraMemorySeries(sd)) return false;
    if (IsEventOnlySeries(sd)) return false;   // 01/10: event-only: there is no championship to simulate
    return IsModernSeries(sd.id);
}

// The points for position `pos` (1-based) with the series' system. With no known system, a sober table:
// what matters here is the championship ORDER, not nailing each era's rulebook.
int PuntosDe(const PointsSystem* ps, int pos) {
    if (pos <= 0) return 0;
    if (ps && !ps->points.empty())
        return pos <= (int)ps->points.size() ? ps->points[pos - 1] : 0;
    static const int kDefault[10] = { 25, 18, 15, 12, 10, 8, 6, 4, 2, 1 };
    return pos <= 10 ? kDefault[pos - 1] : 0;
}

}  // namespace

std::string TeamRankKey(const std::string& seriesId, const std::string& teamLabel) {
    return seriesId + "|" + teamLabel;
}

int TeamLastRank(const SaveData& save, const std::string& seriesId, const std::string& teamLabel) {
    const auto it = save.team_last_rank.find(TeamRankKey(seriesId, teamLabel));
    return it == save.team_last_rank.end() ? 0 : it->second;
}

double SeriesScaleFor(int level) {
    if (level < 1) level = 1;
    if (level > 7) level = 7;
    return (double)bal::SALARY_ANCHOR_BASE * std::pow(bal::SALARY_ANCHOR_GROWTH, (double)(level - 1));
}


// ===================================================================================================
// THE BASE BAND COMES FROM THE DRIVERS THE TEAM ALREADY HAS (14/08, user's idea)
// ===================================================================================================
//
// «You'll have to scale both things, the budgets and the drivers... or you can stipulate it with a
// hypothetical value of their current real drivers' worth and pin it there.» And it's right, because it removes from
// the middle the two constants I was tuning by eye (the width of the team band and the cap on the
// price), which were also fighting each other: the band went from x0.60 to x1.60 --2.67 times from rich to poor--
// while the price curve opened 16 or 32 times, so almost the whole price range was reached by
// NOBODY and the aces ended up without a team.
//
// THE RULE, and it's a single one: A TEAM'S BASE BUDGET IS WHAT ITS BEST DRIVER COSTS. With that the
// world starts IN EQUILIBRIUM by construction — every team can pay exactly the one it already has, not one
// more — and the paddock curve (from whoever can afford the most to whoever can afford the least, through all the
// in-betweens) comes out of the CONTENT instead of an invented interpolation. Greystead Motors can pay a star
// because it does in fact have one; a back-marker team can't, because it doesn't.
//
// It's computed over the STATIC world (empty save, AUTHORED talent) and cached: it's the starting snapshot, not
// what the market has done today. If it were recomputed from the live world the circle «you're rich because you
// have good drivers and you have them because you're rich» would come back, which is exactly what was removed when taking the band off the
// championship position.
//
// On top of this base, RESULTS move it: last year's position raises or lowers it (see BudgetFor).
namespace {
std::map<std::string, double> g_bandaBase;   // "series|team" -> multiplier over its level's scale
bool g_bandaHecha = false;

void ConstruirBandasBase() {
    if (g_bandaHecha) return;
    g_bandaHecha = true;
    const SaveData vacio;
    for (const auto& sd : AllSeries()) {
        if (IsEraMemorySeries(sd)) continue;
        const int tier = (sd.level >= 1 && sd.level <= 7) ? sd.level : 1;
        const double suelo = bal::RATING_SEED_BOTTOM + (double)(tier - 1) * bal::RATING_TIER_STEP;
        std::map<std::string, double> mejorDe;      // team -> the price of its BEST driver, in multiples
        for (const auto& a : SeatsForClass(sd.id, vacio)) {
            if (a.team_label.empty() || a.occupant_id.empty()) continue;
            const double t = BaseTalent(a.occupant_id);
            if (t <= 0.0) continue;
            // The same `dentro` that PriceFor uses, but with AUTHORED talent instead of live ELO.
            double dentro = (bal::RATING_SEED_BOTTOM + t - suelo) / (bal::RATING_TIER_STEP * 4.0);
            if (dentro < 0.0) dentro = 0.0;
            if (dentro > 1.0) dentro = 1.0;
            const double f = bal::MARKET_PRICE_MIN + (bal::MARKET_PRICE_MAX - bal::MARKET_PRICE_MIN)
                                                     * std::pow(dentro, bal::MARKET_PRICE_POWER);
            double& m = mejorDe[a.team_label];
            if (f > m) m = f;
        }
        for (const auto& kv : mejorDe) g_bandaBase[TeamRankKey(sd.id, kv.first)] = kv.second;
    }
}
}  // anon

double BaselineBandFor(const std::string& seriesId, const std::string& teamLabel) {
    ConstruirBandasBase();
    const auto it = g_bandaBase.find(TeamRankKey(seriesId, teamLabel));
    // With no data (new team, series without authored talent): the neutral of the price scale, i.e. it
    // can pay someone from the pack. A rich team is never invented.
    return it == g_bandaBase.end() ? 1.0 : it->second;
}

void InvalidateBaselineBands() { g_bandaHecha = false; g_bandaBase.clear(); }

TeamBudget BudgetFor(const std::string& seriesId, const std::string& teamLabel,
                     const SaveData& save, int worldYear) {
    (void)worldYear;
    TeamBudget out;
    const SeriesDef* sd = SeriesById(seriesId);
    if (!sd) return out;
    // 14/08 — its LEVEL's scale by its SERIES'. Without the second, the eight level-7 series
    // paid the same and F1 had nothing to attract an ace against HARBOR_CHALLENGE_8227 or NIMBUS_MASTERS_5890: of the 16
    // stars that make it to the top in twenty seasons it got TWO. `econ_scale` is 1.0 unless
    // docs/career/series_scale.psv lists it, so without the file nothing changes.
    out.scale = SeriesScaleFor(sd->level) * (sd->econ_scale > 0.0 ? sd->econ_scale : 1.0);

    // How many teams are in this paddock? Needed to read the position in QUARTILES: being fourth of 24 isn't
    // the same as being fourth of 6, and a band that didn't account for it would make almost everyone in the
    // small series rich.
    std::set<std::string> equipos;
    for (const auto& a : SeatsForClass(seriesId, save))
        if (!a.team_label.empty()) equipos.insert(a.team_label);
    const int n = (int)equipos.size();
    const int rank = TeamLastRank(save, seriesId, teamLabel);
    out.stance = StanceFor(rank, n);

    // The BASE is what the best driver the team has in the authored world costs: that way the world
    //   starts in equilibrium and the paddock curve comes out of the content.
    const double base = BaselineBandFor(seriesId, teamLabel);

    if (rank <= 0 || n <= 1) {
        // No championship yet (season 1) or a one-team paddock: the base as is. A
        // position is never invented — that would hand out money by a map's traversal order.
        out.band = base;
        out.per_race = (long)(out.scale * out.band);
        return out;
    }
    // And LAST YEAR'S POSITION only MODULATES it, it doesn't replace it: a good year gives you a bit more margin and
    // a bad one takes it away, but a back-marker team doesn't become Greystead Motors by winning once. The
    // range is +-40%, which is what separates two neighboring teams without breaking the paddock's hierarchy.
    const double p = 1.0 - (double)(rank - 1) / (double)(n - 1);      // 1.0 the champion, 0.0 the wooden spoon
    out.band = base * (0.60 + 0.80 * p);
    out.per_race = (long)(out.scale * out.band);
    out.resolved = true;
    return out;
}

long PriceFor(const std::string& driverId, const SaveData& save, int worldYear, int tier) {
    if (tier < 1) tier = 1;
    if (tier > 7) tier = 7;
    // THE PRICE GOES BY LEVEL, NOT BY SERIES, AND THAT'S ON PURPOSE. What a driver asks for is what they're WORTH, and their
    // value doesn't change because the series looking at them is rich: if the price rose with the series' scale,
    // F1 would have more money AND everyone would cost it more in the same proportion, the two factors would
    // cancel out and the economic advantage of the top category would serve no purpose. Just the opposite of
    // what the per-series scale came to fix.
    const double escala = SeriesScaleFor(tier);
    // Their place WITHIN their level: the absolute rating carries the tier's anchor inside, so it's subtracted
    // to keep what they get above their category's floor, in step units.
    const auto it = save.driver_ratings.find(driverId);
    double dentro = 0.5;                                  // no rating -> the pack, neither a bargain nor a star
    if (it != save.driver_ratings.end() && it->second > 0.0) {
        const double suelo = bal::RATING_SEED_BOTTOM + (double)(tier - 1) * bal::RATING_TIER_STEP;
        dentro = (it->second - suelo) / (bal::RATING_TIER_STEP * 4.0);   // ~4 steps of useful range
        if (dentro < 0.0) dentro = 0.0;
        if (dentro > 1.0) dentro = 1.0;
    }
    // THE PROPORTION IS THE REAL-LIFE ONE, and that's the whole fix. Before, the team band was used
    // (0.60-1.60), i.e. the BEST in a category cost 2.67 times the WORST — and with that
    // compression everyone could pay everyone. In real F1 the best paid takes around
    // fifty million and a debutant one or two: thirty or forty times. See the constants in
    // career_balance.h for the why of each number.
    const double f = bal::MARKET_PRICE_MIN + (bal::MARKET_PRICE_MAX - bal::MARKET_PRICE_MIN)
                                             * std::pow(dentro, bal::MARKET_PRICE_POWER);
    long p = (long)(escala * f);
    if (p < bal::SALARY_FLOOR)      p = bal::SALARY_FLOOR;
    if (p > bal::MARKET_PRICE_CAP)  p = bal::MARKET_PRICE_CAP;
    return p;
}

TeamStance StanceFor(int rank, int teams) {
    if (rank <= 0 || teams <= 0) return TeamStance::Media;      // no data -> the neutral
    if (teams <= 2) return rank == 1 ? TeamStance::Titulo : TeamStance::Sobrevive;
    // By paddock QUARTILES, not absolute position: the top quarter of a 24-team championship fights for the
    // title and the top quarter of a 6-team one doesn't. That way the stance means the same in F1 as in a single-make cup.
    const double q = (double)(rank - 1) / (double)teams;
    if (q < 0.25) return TeamStance::Titulo;
    if (q < 0.50) return TeamStance::Media;
    if (q < 0.75) return TeamStance::Reconstruye;
    return TeamStance::Sobrevive;
}

WorldSeasonReport RunWorldSeason(SaveData& save, int worldYear, std::uint64_t baseSeed,
                                 const std::set<std::string>& skip) {
    WorldSeasonReport rep;
    rep.year = worldYear;

    for (const auto& sd : AllSeries()) {
        if (!CorreEnElMundoVivo(sd)) continue;
        if (skip.count(sd.id)) continue;                  // the player runs it: it isn't charged twice

        // THE FIELD COMES FROM THE SEATS, which is who knows whom the world has signed. And the pace each one
        // runs at comes from `ComposeSeasonGrid`: authored capacity + attributes of Never from the ELO
        // (see the long note in the header: that loop was already measured once and closed itself).
        // 18/08 — AND WITH THE AGE OF WHOEVER DRIVES IT. The card of a GENERATED occupant is sampled from the
        // corpus of authored cards, which are levels of MATURE people today: without the last three arguments,
        // a 19-year-old RUNS in this simulator like the adult he was cloned from. And since the XML AMS2
        // reads does pass them (WriteAllClassGrids / ComposeSeasonGridForSave), the world would decide the
        // results with one pace and the asphalt would write another -- the divergence this project has already
        // paid for twice. `worldYear` is the year this season runs, which is the one that has the age.
        const std::vector<SeatRow> asientos = SeatRowsForSeries(save, sd.id);
        if (asientos.empty()) continue;
        const std::vector<GridDriver> grid = ComposeSeasonGrid(
            sd.id, sd.level, /*playerForm=*/0.0, (int)asientos.size(), /*useHomage=*/false,
            save.rival_form, /*exclude=*/nullptr, &save.driver_ratings,
            ClassRole::SingleClass, &asientos, /*skipLiveries=*/nullptr, &save.driver_races,
            &save.driver_curve, &save.birth_year, worldYear);
        if (grid.size() < 2) continue;

        // Whose each car is, so the TEAMS championship can be summed. The livery is the key that ties the
        // two halves (seat <-> grid); it's the same bridge the livery uncrossing uses.
        std::map<std::string, std::string> equipoDeLibrea;
        for (const auto& a : SeatsForClass(sd.id, save))
            if (!a.livery_name.empty() && !a.team_label.empty()) equipoDeLibrea[a.livery_name] = a.team_label;

        std::vector<RaceEntrant> ents;
        ents.reserve(grid.size());
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

        const PointsSystem* ps = PointsById(sd.points_id);
        const int rondas = sd.rounds > 0 ? sd.rounds : 1;
        std::map<std::string, int> puntosPiloto, puntosEquipo;

        for (int ronda = 1; ronda <= rondas; ++ronda) {
            const std::uint64_t seed = RoundSeed(baseSeed, sd.id, worldYear, ronda);
            // World time carries no weather report: dry, and the variation is put in by the model. A
            // per-round weather would be inventing a datum nobody has yet.
            const RaceOutcome ro = SimulateRace(ents, /*weather=*/0, seed);

            // Points go by CLASSIFIED position (retirements don't score), and the complete finishing
            // order -- retirements included -- is what's passed to the ELO.
            std::vector<GridFinisher> llegada;
            llegada.reserve(ro.order.size());
            int puesto = 1;
            for (size_t rank = 0; rank < ro.order.size(); ++rank) {
                const int ei = ro.order[rank];
                const bool dnf = (ro.dnf_of[ei] != DnfKind::Finished);
                GridFinisher gf;
                gf.id         = ents[ei].id;
                gf.level      = sd.level;
                gf.order      = (int)rank;
                gf.field_size = (int)ents.size();
                gf.dnf        = dnf;
                gf.is_player  = false;
                llegada.push_back(gf);
                if (dnf) continue;
                const int pts = PuntosDe(ps, puesto++);
                if (pts > 0 && !ents[ei].id.empty()) {
                    puntosPiloto[ents[ei].id] += pts;
                    const auto il = equipoDeLibrea.find(grid[ei].livery_name);
                    if (il != equipoDeLibrea.end()) puntosEquipo[il->second] += pts;
                }
            }
            // THE SAME MACHINE AS A PLAYER RACE: zero sum per race and dynamic K by seniority.
            // There's no second rating model to calibrate or keep in sync.
            SettleRatingGrid(save.driver_ratings, save.driver_races, llegada);
            rep.races++;
            if (llegada.size() >= 2) rep.settled++;
        }

        WorldSeasonSeries ws;
        ws.series_id = sd.id;
        ws.rounds    = rondas;
        ws.field     = (int)ents.size();

        int mejor = -1;
        for (const auto& kv : puntosPiloto)
            if (kv.second > mejor || (kv.second == mejor && kv.first < ws.champion_id)) {
                mejor = kv.second; ws.champion_id = kv.first;   // tie -> lower id: deterministic
            }

        // The TEAMS championship. Sorted by points desc and, on a tie, by name: without this the position
        // would depend on a map's traversal order and change from one build to another.
        std::vector<std::pair<int, std::string>> tabla;
        for (const auto& kv : puntosEquipo) tabla.push_back({ kv.second, kv.first });
        // Also the teams that raced and didn't score: they exist, and they're exactly the tail ones that
        // get sent to «survive». Leaving them out would make them pass as «no data», which is the neutral.
        for (const auto& kv : equipoDeLibrea)
            if (!puntosEquipo.count(kv.second)) { puntosEquipo[kv.second] = 0; tabla.push_back({ 0, kv.second }); }
        std::sort(tabla.begin(), tabla.end(), [](const std::pair<int, std::string>& a,
                                                 const std::pair<int, std::string>& b) {
            if (a.first != b.first) return a.first > b.first;
            return a.second < b.second;
        });
        int puestoEq = 1;
        for (const auto& t : tabla) {
            ws.teams_by_rank.push_back(t.second);
            save.team_last_rank[TeamRankKey(sd.id, t.second)] = puestoEq++;
        }

        rep.per_series.push_back(ws);
        rep.series++;
    }
    return rep;
}

}  // namespace career
