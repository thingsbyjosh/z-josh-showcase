#pragma once
// ===================================================================================================
// — THE WORLD RUNS TOO (12/08)
// ===================================================================================================
//
// Everything hangs off a phrase that appears twice in the spec: «last year's results». The team
// band () wants them as a prestige signal, and WHAT a team BUYS () is derived
// literally from its standing — «fights for the title / midfield / rebuilds / survives».
//
// MEASURED ON 12/08: THAT INPUT DOESN'T EXIST. The world's ELO only moves in the grids where THE PLAYER
// HAS RACED (`SettleRatingGrid`, which the overlay calls on closing a race). The other ninety series
// dispute nothing, ever. `SimulateWorld` moves the clock and runs the player's races; `SimulateSeason`
// is also theirs. Nobody else competes.
//
// The consequences of that gap, all measured:
//
//   · No team knows what happened last year, so none changes policy: the market repeats the
//     same winter forever and («variety is NOT a die») is left without its main source.
//   · Nobody has a reason to FIRE whoever declines — the measured defect of 12/08, 310 drivers clinging to
//     seats above their level while those who deserve to move up drop from 124 to 29.
//   · And (`form_delta`) compares «results» against «capacity» when the results are... the seed
//     of the ELO, which comes out of the capacity itself. The subtraction couldn't mean anything because both sides come
//     from the same place.
//
// THE RULE THAT CANNOT BE BROKEN: THE FINISHING ORDER COMES FROM CAPACITY, NEVER FROM THE ELO.
//
// The pace each one runs at is set by `ComposeSeasonGrid` (authored talent + attributes of ). The ELO
// is UPDATED with the result, but doesn't produce it. If it were inverted —deducing the pace from the ELO and then
// updating the ELO with the result— it closes the loop this project already measured once and that cost half a
// phase to understand: worse rating -> slower -> worse rating, «rating pinned to the floor after 6 years» (the
// long note of `PerfSkillInField`, career_simrunner.cpp). With capacity ruling, the ELO becomes
// exactly what it needs to be: a MEASURE of results independent of capacity, so that its subtraction
// means something.
//
// The rest of the loop DOES exist on purpose and is bounded: `EffectivePace` lets the ELO perturb the
// capacity up to ±`FORM_RATING_CAP` (0.10 on the talent scale) weighted by confidence. That's
// doing its job, it saturates at the cap and the ELO is zero-sum, so it's a SHARING OUT within the field,
// not inflation. But it's the first time that formula receives real results: what it does to
// the ladder's shape has to be MEASURED, not taken as good.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace career {

struct SaveData;

// The championship of ONE series, resolved.
struct WorldSeasonSeries {
    std::string series_id;
    std::string champion_id;                  // the driver who won the championship ("" if the field doesn't give one)
    int rounds = 0;
    int field = 0;
    // The TEAMS in championship order (sum of their cars' points). It's what turns into
    // buying policy and band signal: the first fights for the title, the last survives.
    std::vector<std::string> teams_by_rank;
};

struct WorldSeasonReport {
    int year = 0;
    int series = 0;      // series resolved
    int races = 0;       // races disputed in total
    int settled = 0;     // races that moved ELO (>=2 competing)
    std::vector<WorldSeasonSeries> per_series;
};

// Resolves the season of ALL the world's live series and leaves its trace in the save:
//   · `driver_ratings` / `driver_races` — via `SettleRatingGrid`, the SAME machine a player's race
//     uses. Zero sum per race and dynamic K by seniority: nothing new to calibrate.
//   · `team_last_rank` — the teams championship position, which is what will be read by
//
// `skip` = series NOT resolved here because the player runs them (theirs are closed by the overlay or the
// simulator; resolving them twice would charge them the ELO twice).
//
// Deterministic by `baseSeed` + (series, year, round) — the same seed gives the same world. No global RNG.
WorldSeasonReport RunWorldSeason(SaveData& save, int worldYear, std::uint64_t baseSeed,
                                 const std::set<std::string>& skip = std::set<std::string>());

// — a team's BUYING POLICY, derived from where it finished last year. With no data (new series,
// new team, or a world that hasn't yet run a season) it returns `Media`, which is the neutral.
enum class TeamStance {
    Titulo = 0,      // fights for the title: wants craft NOW
    Media,           // midfield
    Reconstruye,     // bets on the young
    Sobrevive,       // whatever it can afford
};

// The team's position in last year's championship, 1-based. 0 = no data.
int TeamLastRank(const SaveData& save, const std::string& seriesId, const std::string& teamLabel);

// The buying stance from that position and the paddock size. Quartiles: the first quarter fights for the
// title, the last survives, and in between midfield and rebuild.
TeamStance StanceFor(int rank, int teams);

// The key under which the position is stored: "<series_id>|<team_label>". A single place, because the team
// label HAS SPACES and the save format is read with sscanf.
std::string TeamRankKey(const std::string& seriesId, const std::string& teamLabel);

// ===================================================================================================
// — THE BUDGET: SERIES SCALE x TEAM BAND
// ===================================================================================================
//
// «The anchor moves from the driver to the TEAM.» An anchor on the driver («you're worth 1250 because you race at level 2») can't
// be defended or explained; a budget per team is a real thing. With the anchor on the team
// the distribution EMERGES: an 80 doesn't fit in tier 1 because nobody in tier 1 can pay them, not because a rule
// forbids it.
//
//   budget = SERIES SCALE (rules)  x  TEAM BAND within it (nuances)
//
// THE SCALE ISN'T INVENTED: it's the salary anchor that already exists (`SALARY_ANCHOR_BASE` x
// `SALARY_ANCHOR_GROWTH`^(level-1)). Writing a second scale in parallel would mean two sources for
// the same number, and this project already knows how that ends.
//
// AND THE BAND COMES FROM LAST YEAR'S POSITION, not from how good its driver is today. `TeamSalaryBand`
// —the one that already existed— derives the band from the ELO average of its CURRENT occupants, and that's a circle:
// you're rich because you have good drivers and you have good drivers because you're rich. A team could never
// fall or rise. The championship position is a fact external to the driver sitting there today, and since the
// running-world commit, it exists.
// USER'S DECISION (14/08): SIGNING MONEY EXPIRES. IT DOESN'T ACCUMULATE.
//
// It's an allowance per season, not a bank account: each team receives its own, spends it or loses it.
// And it's not a balance detail — it's what makes all this need no state:
//
//   · There's NO per-team balance in the save. Nothing to persist, nothing to migrate, nothing to desync.
//   · A team CAN'T save for five seasons and buy half a grid, which is the classic failure
//     of piggy-bank economies: the world freezes for a few years and then makes a jump that explains nothing.
//   · The budget is a PURE FUNCTION of (series level, last year's position). It's recomputed when
//     asked, so it always tells the truth of THIS year.
struct TeamBudget {
    long   per_race = 0;      // ₡/race this team can pay per driver THIS season (expires)
    double scale = 0.0;       // its series' scale (by level)
    double band = 1.0;        // its multiplier within the paddock (by last year's position)
    TeamStance stance = TeamStance::Media;
    bool   resolved = false;  // false = no series/position: pure scale and neutral band
};

TeamBudget BudgetFor(const std::string& seriesId, const std::string& teamLabel,
                     const SaveData& save, int worldYear);

// A series' scale by its level (1..7), in ₡/race. Exposed because the budget and the
// diagnostic use it, and because the day it's recalibrated it has to move in ONE place.
double SeriesScaleFor(int level);

// A team's BASE band: what the BEST driver it has in the authored world costs, in multiples
// of its level's scale. With that the world starts in equilibrium (every team can pay the one it already has)
// and the paddock curve comes out of the CONTENT instead of two constants tuned by eye. Static and cached.
double BaselineBandFor(const std::string& seriesId, const std::string& teamLabel);
void   InvalidateBaselineBands();   // the data has changed (tests)

// — WHAT A DRIVER ASKS FOR, in ₡/race. «Skill is bought and fame is paid for»: the skill is their
// absolute rating (which already carries the anchor of their level), and on top of it the price grows MORE THAN
// PROPORTIONALLY — the ace doesn't cost a bit more than the pack, he costs quite a lot more, which is what makes a
// poor team able to fill a grid but not have an ace.
//
// It doesn't yet include trajectory, age or money the driver brings (they ask for it: the 20-year-old
// F2 champion is an INVESTMENT and the 29-year-old with a failed F1 is a COST). It's missing on purpose and it says so: first
// make the budget bite, and then the axes get added, measuring each one.
long PriceFor(const std::string& driverId, const SaveData& save, int worldYear, int tier);

}  // namespace career
