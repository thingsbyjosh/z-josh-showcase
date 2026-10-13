#include "career_candidate.h"
#include "career_rating.h"     // RatingQuality
#include "career_seats.h"      // SeatsForClass -> who drives for each team (cohort brand affinity)
#include "career_series.h"     // AllSeries/IsEraMemorySeries (walk the series in the class)
#include "career_data_json.h" // DriverById -- your teammate's NAME, so it can be told in copy
#include <algorithm>

namespace career {

// Local FNV-1a-32 (avoids an ODR clash with the copies in career_market/career_seats).
static unsigned long candFnv1a(const std::string& s) {
    unsigned long h = 2166136261UL;
    for (unsigned char c : s) { h ^= c; h *= 16777619UL; }
    return h;
}
static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

// age -> [0..1] youth: 18 years old -> 1.0, 45 -> 0.0 (linear, clamped). birth_year 0 (unknown) -> neutral 0.5.
static double youthFrom(int birthYear, int worldYear) {
    if (birthYear <= 0) return 0.5;
    int age = worldYear - birthYear;
    double y = (45.0 - (double)age) / (45.0 - 18.0);
    return clamp01(y);
}
// simulated cleanliness by driving archetype (0..1).
static double cleanFromArchetype(const std::string& a) {
    if (a == "metronomo" || a == "frio") return 0.9;
    if (a == "solido" || a == "tecnico") return 0.75;
    if (a == "variable") return 0.55;
    if (a == "agresivo" || a == "temerario") return 0.4;
    return 0.65;   // unknown -> middling
}

int TeamPersonality(const std::string& teamLabel) {
    if (teamLabel.empty()) return 0;   // no label -> meritocratic (neutral)
    return (int)(candFnv1a(teamLabel) % (unsigned long)bal::CANDFIT_PERSONALITY_COUNT);
}

// The tokens, in the SAME order as the bal:: tables -- personalities follow the order of
// CANDFIT_W's rows, and the factors follow its columns. If someone reorders a table without touching this, the
// test catches it: it checks that the "money" row's token is the one that weights budget above
// everything else, i.e. the mapping is verified by MEANING, not by index.
static const char* kPersonalityTok[bal::CANDFIT_PERSONALITY_COUNT] = {
    "meritocratico", "cantera_joven", "veterano", "dinero", "patriotico", "fiel_marca"
};
static const char* kFactorTok[bal::CANDFIT_FACTOR_COUNT] = {
    "skill", "clean", "seriesExp", "brandAff", "rep", "natMatch", "youth", "budget"
};

const char* TeamPersonalityToken(const std::string& teamLabel) {
    int p = TeamPersonality(teamLabel);
    if (p < 0 || p >= bal::CANDFIT_PERSONALITY_COUNT) p = 0;
    return kPersonalityTok[p];
}

std::vector<SeatAsk> SeatAsksFor(const SaveData& save, const std::string& className,
                                 const std::string& teamLabel, const std::string& teamCountry,
                                 double playerMoneyNorm, int worldYear, int topN) {
    int p = TeamPersonality(teamLabel);
    if (p < 0 || p >= bal::CANDFIT_PERSONALITY_COUNT) p = 0;
    const double* w = bal::CANDFIT_W[p];
    // your scores come from the SAME extraction the seat contest uses; nothing gets recomputed a different way
    const CandidateFactors tu = PlayerCandidateFactors(save, className, teamLabel, teamCountry,
                                                       playerMoneyNorm, worldYear);
    std::vector<SeatAsk> out;
    out.reserve(bal::CANDFIT_FACTOR_COUNT);
    for (int i = 0; i < bal::CANDFIT_FACTOR_COUNT; ++i) {
        SeatAsk a;
        a.key    = kFactorTok[i];
        a.weight = (int)(w[i] * 100.0 + 0.5);
        a.you    = (int)(clamp01(tu.f[i]) * 100.0 + 0.5);
        out.push_back(a);
    }
    // STABLE ordering: by weight descending and, on ties, by catalog order (stable_sort preserves it). Without
    // this, two tied factors could swap between calls and the screen would flicker on its own.
    std::stable_sort(out.begin(), out.end(),
                     [](const SeatAsk& a, const SeatAsk& b) { return a.weight > b.weight; });
    if (topN > 0 && (int)out.size() > topN) out.resize((size_t)topN);
    return out;
}

double TeamCandidateFit(const CandidateFactors& fac, const std::string& teamLabel,
                        const std::string& candidateId, int year) {
    int p = TeamPersonality(teamLabel);
    if (p < 0 || p >= bal::CANDFIT_PERSONALITY_COUNT) p = 0;
    const double* w = bal::CANDFIT_W[p];
    double s = 0.0;
    for (int i = 0; i < bal::CANDFIT_FACTOR_COUNT; ++i) s += w[i] * fac.f[i];
    unsigned long h = candFnv1a(teamLabel + "|" + candidateId + "|" + std::to_string(year));
    double noise = (double)(h % 1000UL) / 1000.0;   // 0..1, deterministic, stable within the year
    return s + bal::CANDFIT_NOISE_W * noise;
}

// Has the player ever driven for this team (brand)? completed_seasons + an active contract.
static bool playerDroveForTeam(const SaveData& save, const std::string& teamLabel) {
    if (teamLabel.empty()) return false;
    for (const auto& cs : save.completed_seasons) if (cs.team_id == teamLabel) return true;
    for (const auto& c : save.contracts) if (c.active && c.team_id == teamLabel) return true;
    return false;
}

CandidateFactors PlayerCandidateFactors(const SaveData& save, const std::string& className,
                                        const std::string& teamLabel, const std::string& teamCountry,
                                        double money_norm, int worldYear) {
    CandidateFactors r;
    r.f[0] = clamp01(RatingQuality(save.prog.rating));                          // skill
    r.f[1] = clamp01(save.prog.cleanliness / 100.0);                            // clean
    int races = 0; auto it = save.class_stats.find(className);
    if (it != save.class_stats.end()) races = it->second.races;
    r.f[2] = clamp01((double)races / bal::CANDFIT_EXP_REF);                      // seriesExp
    r.f[3] = playerDroveForTeam(save, teamLabel) ? 1.0 : 0.0;                    // brandAff
    r.f[4] = clamp01((double)save.prog.rep / bal::CANDFIT_REP_REF);             // rep
    r.f[5] = (!teamCountry.empty() && save.player.nationality == teamCountry) ? 1.0 : 0.0;  // natMatch
    r.f[6] = youthFrom(save.player.birth_year, worldYear);                       // youth
    r.f[7] = clamp01(money_norm);                                                // budget
    return r;
}

std::set<std::string> TeamOccupantIds(const SaveData& save, const std::string& className,
                                      const std::string& teamLabel) {
    std::set<std::string> out;
    if (className.empty() || teamLabel.empty()) return out;   // no team means no affinity with anyone
    for (const auto& sd : AllSeries()) {
        if (sd.class_name != className) continue;
        if (IsEraMemorySeries(sd)) continue;   // a memory (era) is frozen: its seats aren't contested
        for (const auto& s : SeatsForClass(sd.id, save))
            if (s.team_label == teamLabel && !s.occupant_id.empty()) out.insert(s.occupant_id);
    }
    return out;
}

CandidateFactors CohortCandidateFactors(const RosterDriver& rd, const SaveData& save,
                                        const std::string& className, const std::string& teamLabel,
                                        const std::string& teamCountry, int worldYear,
                                        const std::set<std::string>* teamOccupants,
                                        const std::string& seriesId,
                                        const CohortFactorsCache* cache) {
    CandidateFactors r;
    // skill: the live rating if it exists; otherwise ANCHORED TO THE TIER of the seat being contested; and only as a last
    // resort, the flat mapping.
    // The flat mapping (1200 + base_skill x 700) used to be the only path the cohort took, because
    // rk_* drivers don't show up in any `field` and SeedAllDrivers never seeds them. And that mapping IGNORES
    // category: base_skill is a RANK WITHIN the grid, not an absolute level, so a
    // karting kid with 0.97 scored 1879 -- a world-champion-level rating -- and ate the player alive in their own
    // backyard. Measured: in a karting seat contest, Lucia Vega at 0.667 against the player's 0.183, and
    // 0 seats won out of 23. It's the same bug that v0.5 fixed for the difficulty floor ("the karting
    // ace and the F1 ace produced the same number") and that stayed alive at this other gate.
    double rating = 0.0;
    auto rit = save.driver_ratings.find(rd.id);
    if (rit != save.driver_ratings.end()) rating = rit->second;
    else if (!seriesId.empty()) rating = OccupantRating(seriesId, rd.id, save, worldYear);
    if (rating <= 0.0)
        rating = bal::RATING_SEED_BOTTOM + rd.base_skill * (bal::RATING_SEED_TOP - bal::RATING_SEED_BOTTOM);
    r.f[0] = clamp01(RatingQuality(rating));                                     // skill
    r.f[1] = cleanFromArchetype(rd.archetype);                                  // clean (simulated)
    // seriesExp: are they an incumbent in this class? (shows up as an occupant in some series of the class) -> 1; otherwise 0.3
    bool incumbent = false;
    if (cache && cache->incumbents) incumbent = cache->incumbents->count(rd.id) > 0;   // 09/09: once per triple
    else for (const auto& kv : save.seat_occupant) if (kv.second == rd.id) { incumbent = true; break; }
    r.f[2] = incumbent ? 1.0 : 0.3;                                             // seriesExp (simulated)
    // This used to be a FIXED 0.0. The original reasoning still held -- a cohort driver
    // doesn't earn brand credit just for sharing a name with a team, which was the old tautology (rd.homage.find(
    // teamLabel) != npos -> 1.0, which was unmissable in classes where the label was derived from the occupant's
    // own homage) -- but it left a whole column stair-stepped against the player's 1.0/0.0. Real affinity is
    // DRIVING for them: if they occupy a seat at this team, they have it. Read from the living world, not made up.
    // And it gives the right bias as a side effect: a team leans toward renewing its own driver.
    r.f[3] = (teamOccupants && teamOccupants->count(rd.id)) ? 1.0 : 0.0;         // brandAff
    r.f[4] = clamp01(RatingQuality(rating));                                     // rep proxy = skill
    r.f[5] = (!teamCountry.empty() && rd.country == teamCountry) ? 1.0 : 0.0;    // natMatch
    int by = rd.birth_year; auto byit = save.birth_year.find(rd.id);
    if (byit != save.birth_year.end() && byit->second != 0) by = byit->second;
    r.f[6] = youthFrom(by, worldYear);                                          // youth
    // A cohort driver also brings sponsorship money along. Deterministic by id (the same
    // driver keeps the same pocket for their whole career, regardless of team/class/year) and SPREAD OUT, so that some
    // rivals come with money and others don't, and the contest depends on who you're up against. The seeding uses its
    // own prefix so it doesn't correlate with TeamCandidateFit's noise, which starts from the same id.
    r.f[7] = bal::CANDFIT_COHORT_BUDGET_CAP
           * (double)(candFnv1a("budget|" + rd.id) % 1000UL) / 1000.0;          // budget
    return r;
}

// the seat-contest TABLE. This used to live inside PlayerWinsSeat and got lost when it returned
// a yes/no; the verdict now comes from here, so what's shown and what the engine decides are the same
// calculation and can't drift apart.
std::vector<SeatCandidate> RankSeatCandidates(const SaveData& save, const std::string& className,
                                              const std::string& teamLabel, const std::string& teamCountry,
                                              double playerMoneyNorm, int worldYear,
                                              const std::vector<RosterDriver>& cohortDrivers,
                                              const std::string& seriesId) {
    std::vector<SeatCandidate> out;
    out.reserve(cohortDrivers.size() + 1);
    {
        SeatCandidate p;
        p.id = "player";
        p.name = save.player.name + (save.player.surname.empty() ? "" : " " + save.player.surname);
        CandidateFactors pf = PlayerCandidateFactors(save, className, teamLabel, teamCountry, playerMoneyNorm, worldYear);
        p.fit = TeamCandidateFit(pf, teamLabel, "player", worldYear);
        p.is_player = true;
        out.push_back(std::move(p));
    }
    // A single resolution for the whole table. Inside the loop this would be O(drivers x series x seats),
    // and TeamOccupantIds walks every series in the whole class assembling its seats.
    const std::set<std::string> teamOccupants = TeamOccupantIds(save, className, teamLabel);
    // 09/09 (performance): the world's incumbents, ONCE per triple (see CohortFactorsCache in the .h).
    std::set<std::string> incumbents;
    for (const auto& kv : save.seat_occupant) if (!kv.second.empty()) incumbents.insert(kv.second);
    CohortFactorsCache cache; cache.incumbents = &incumbents;
    for (const auto& rd : cohortDrivers) {
        SeatCandidate c;
        c.id = rd.id;
        c.name = rd.name.empty() ? rd.id : rd.name;
        CandidateFactors cf = CohortCandidateFactors(rd, save, className, teamLabel, teamCountry, worldYear,
                                                     &teamOccupants, seriesId, &cache);
        c.fit = TeamCandidateFit(cf, teamLabel, rd.id, worldYear);
        out.push_back(std::move(c));
    }
    // score descending; ties -> id ascending, so the order does NOT depend on how the cohort happens to be sorted
    std::sort(out.begin(), out.end(), [](const SeatCandidate& a, const SeatCandidate& b) {
        if (a.fit != b.fit) return a.fit > b.fit;
        return a.id < b.id;
    });
    return out;
}

bool PlayerWinsRankedSeat(const std::vector<SeatCandidate>& rank) {
    double playerFit = 0.0, bestCohort = 0.0;
    for (const auto& c : rank) {
        if (c.is_player) playerFit = c.fit;
        else if (c.fit > bestCohort) bestCohort = c.fit;   // starts at 0.0 on purpose: no known rivals
    }                                                       //  the player wins; absence of data can't penalize them
    return playerFit >= bestCohort - bal::MK_SEAT_CONTEST_MARGIN;
}

const SeatCandidate* TopRivalInRank(const std::vector<SeatCandidate>& rank) {
    for (const auto& c : rank) if (!c.is_player) return &c;   // sorted descending -> the first non-player entry is the rival
    return nullptr;
}

bool PlayerWinsSeat(const SaveData& save, const std::string& className, const std::string& teamLabel,
                    const std::string& teamCountry, double playerMoneyNorm, int worldYear,
                    const std::vector<RosterDriver>& cohortDrivers) {
    return PlayerWinsRankedSeat(
        RankSeatCandidates(save, className, teamLabel, teamCountry, playerMoneyNorm, worldYear, cohortDrivers));
}

// "you're being watched" ------------------------------------------------------------------
int ClassifyRankedSeat(const std::vector<SeatCandidate>& rank) {
    if (PlayerWinsRankedSeat(rank)) return SEAT_CONTEST_WON;   // the SAME criterion, not a parallel copy
    double playerFit = 0.0, bestCohort = 0.0;
    for (const auto& c : rank) {
        if (c.is_player) playerFit = c.fit;
        else if (c.fit > bestCohort) bestCohort = c.fit;
    }
    if (playerFit >= bestCohort - bal::MK_SEAT_CONTEST_MARGIN - bal::MK_SEAT_WATCH_BAND) return SEAT_CONTEST_WATCH;
    return SEAT_CONTEST_LOST;
}

// 28/07 feedback: ALL of your races, wherever you race them -- NOT just the seat's own series. A team that's
// watching you sees you race wherever you are: the normal case is precisely that it isn't in their series (they're looking down
// at you while you compete in yours); it only matches on a renewal. If we only counted races in their own class, a GT3 team
// watching you while you race in F4 would NEVER have seen a single race, and the window would never count down.
// (`w` is there so the signature doesn't have to change once this stops being a simple total.)
int SeatWatchRacesNow(const SaveData& save, const SeatWatch& /*w*/) {
    int n = 0;
    for (const auto& kv : save.class_stats) n += kv.second.races;
    return n;
}

int SeatWatchRacesLeft(const SaveData& save, const SeatWatch& w) {
    int run = SeatWatchRacesNow(save, w) - w.races_at_open;
    if (run < 0) run = 0;                       // defensive: a tampered save shouldn't be able to extend the window
    int left = w.races_window - run;
    return left < 0 ? 0 : left;
}

bool SeatWatchDue(const SaveData& save, const SeatWatch& w, int absToday) {
    if (w.state != 0) return false;
    if (SeatWatchRacesLeft(save, w) <= 0) return true;
    return absToday - w.opened_day >= bal::MK_SEAT_WATCH_MAX_DAYS;
}

bool SeatWatchNoteRaces(const SaveData& save, SeatWatch& w, bool rivalAhead) {
    if (w.state != 0) return false;
    const int run = SeatWatchRacesNow(save, w) - w.races_at_open;
    const int nuevas = (run < 0 ? 0 : run) - w.races_seen;
    if (nuevas <= 0) return false;                 // nothing new to record
    w.races_seen += nuevas;
    // Several races logged at once (you didn't open the market in between) get recorded using the CURRENT comparison: it's
    // the only data that exists for that stretch, and faking a detail that was never measured would be worse than treating it as constant.
    if (rivalAhead) w.rival_ahead_streak += nuevas;
    else            w.rival_ahead_streak = 0;      // you beat them once -> you're back in
    return true;
}

// (28/07 feedback): losing to them ONCE can't cost you the seat -- anyone can have
// a bad weekend, and that's the margin it allows. What costs you the seat is losing EVERY race they watch: that's when the team stops
// waiting and hands it to them, without running out the clock. And conversely: if you get ahead EARLY, you don't get
// made to run all 3 races just as a formality.
int SeatWatchActionNow(const SaveData& save, const SeatWatch& w, int outcomeToday, int absToday) {
    if (w.state != 0) return SEATWATCH_KEEP;                              // already closed: nothing to decide
    if (outcomeToday == SEAT_CONTEST_WON) return SEATWATCH_CLOSE_WON;     // you've pulled ahead
    if (w.races_window > 0 && w.rival_ahead_streak >= w.races_window)
        return SEATWATCH_CLOSE_LOST;                                      // they've beaten you EVERY time -> it's theirs
    if (SeatWatchDue(save, w, absToday)) return SEATWATCH_CLOSE_LOST;     // time's up and you're still behind
    return SEATWATCH_KEEP;
}

SeatWatch* FindSeatWatch(SaveData& save, const std::string& seriesId, const std::string& teamLabel) {
    for (auto& w : save.seat_watches)
        if (w.state == 0 && w.series_id == seriesId && w.team_label == teamLabel) return &w;
    return nullptr;
}
const SeatWatch* FindSeatWatch(const SaveData& save, const std::string& seriesId, const std::string& teamLabel) {
    for (const auto& w : save.seat_watches)
        if (w.state == 0 && w.series_id == seriesId && w.team_label == teamLabel) return &w;
    return nullptr;
}

// Have you been battling this driver? Returns the INTENSITY (0 = no history), so
// ties among several rivals can be broken without looking at the save again. Beef outranks h2h because it comes from
// a duel the inbox already counted as such; loose head-to-head encounters serve as a floor when there's no beef.
static int rivalryHeat(const SaveData& save, const std::string& id) {
    auto bit = save.beef.find(id);
    const int beef = (bit != save.beef.end() && bit->second.level > 0) ? bit->second.level : 0;
    auto hit = save.h2h.find(id);
    const int met = (hit != save.h2h.end()) ? (hit->second.w + hit->second.l) : 0;
    if (beef > 0) return 1000 + beef * 100 + (met > 99 ? 99 : met);   // with beef: the level rules, it breaks the tie
    if (met >= bal::MK_RIVAL_TERNA_MIN_H2H) return met;               // without beef: only if you've genuinely crossed paths
    return 0;
}

std::vector<SeatContender> ShortlistFromRank(const std::vector<SeatCandidate>& rank, const SaveData* save) {
    std::vector<SeatContender> out;
    // The first slot is reserved for whoever has history with you. It's searched for WITHIN the
    // table and never outside it: injecting someone who wasn't a candidate would break the contest's
    // merit model, the one calibrated earlier. Reserving a slot is more honest than adding points, because it doesn't lie
    // about who's actually the strongest -- the other two slots still go by score.
    std::string nemesis;
    if (save) {
        int mejor = 0;
        for (const auto& c : rank) {                   // walked in score order -> a tie in heat goes to the better score
            if (c.is_player) continue;
            const int calor = rivalryHeat(*save, c.id);
            if (calor > mejor) { mejor = calor; nemesis = c.id; }
        }
    }
    if (!nemesis.empty())
        for (const auto& c : rank)
            if (c.id == nemesis) { SeatContender sc; sc.id = c.id; sc.name = c.name; out.push_back(sc); break; }

    for (const auto& c : rank) {                       // sorted descending -> the first non-player entries are the best
        if (c.is_player) continue;
        if (c.id == nemesis) continue;                 // the first slot is already taken: no duplicate
        if ((int)out.size() >= bal::MK_SEAT_WATCH_SHORTLIST) break;
        SeatContender sc; sc.id = c.id; sc.name = c.name;
        out.push_back(sc);
    }
    return out;
}

bool SeatWatchOpen(SaveData& save, const std::string& seriesId, const std::string& className,
                   const std::string& teamLabel, const std::vector<SeatContender>& contenders, int absToday) {
    if (seriesId.empty() || teamLabel.empty()) return false;
    if (FindSeatWatch(save, seriesId, teamLabel)) return false;   // they were already watching you: the deadline doesn't reset
    SeatWatch w;
    w.series_id = seriesId; w.class_name = className; w.team_label = teamLabel;
    w.contenders = contenders;                                    // The list gets SEALED right here
    if (!contenders.empty()) { w.rival_id = contenders[0].id; w.rival_name = contenders[0].name; }
    w.opened_day    = absToday;
    w.races_at_open = SeatWatchRacesNow(save, w);   // YOUR races, wherever you race them (see SeatWatchRacesNow)
    w.races_window  = bal::MK_SEAT_WATCH_RACES;
    w.state = 0; w.resolved_day = 0;
    save.seat_watches.push_back(w);
    return true;
}

void SeatWatchClose(SeatWatch& w, bool won, int absToday) {
    if (w.state != 0) return;
    w.state = won ? 1 : 2;
    w.resolved_day = absToday;
}

// see career_candidate.h.
TeammateDuel SettleTeammateDuel(SaveData& save, const std::string& seriesId, int playerPos,
                                const std::map<std::string, int>& posById, int worldYear) {
    TeammateDuel d;
    d.why = 1;
    if (seriesId.empty() || playerPos <= 0) return d;   // a DNF: it's not a loss against anyone

    // your contract decides who your team is; with no contract there's no teammate that counts
    d.why = 2;
    const Contract* mio = nullptr;
    for (const auto& c : save.contracts)
        if (c.active && c.series_id == seriesId) { mio = &c; break; }
    if (!mio || mio->team_id.empty()) return d;

    d.why = 3;
    const int idx = TeammateSeatIndex(save, seriesId, mio->team_id, mio->seat_idx);
    if (idx < 0) return d;                               // the team runs a single car
    d.why = 4;
    const std::vector<Seat> asientos = SeatsForClass(seriesId, save);
    if (idx >= (int)asientos.size() || asientos[idx].occupant_id.empty()) return d;
    d.id = asientos[idx].occupant_id;
    d.name = DriverDisplayName(d.id);   // also covers generated drivers (gn_*), which aren't in the pool

    { const auto ith = save.h2h.find(d.id);
      if (ith != save.h2h.end()) { d.w = ith->second.w; d.l = ith->second.l; } }

    d.why = 5;
    const auto itp = posById.find(d.id);
    if (itp == posById.end() || itp->second <= 0) {       // didn't race or didn't classify: no duel
        const auto itb = save.beef.find(d.id);
        if (itb != save.beef.end()) d.beef = itb->second.level;
        return d;
    }
    d.raced = true;
    d.why   = 0;
    d.lost  = playerPos > itp->second;

    // the grudge RISES when you lose and FALLS when you win. Being two-way is what stops it from turning into
    // a one-way ratchet: if you keep beating them consistently, they stop being your nemesis, which is exactly the intended behavior.
    // The entry is only touched if there's something to change, so the whole save doesn't get seeded with zero-value beefs.
    if (d.lost) {
        Beef& bf = save.beef[d.id];
        bf.level = (bf.level + bal::MK_BEEF_MATE_LOSS < 5) ? bf.level + bal::MK_BEEF_MATE_LOSS : 5;
        bf.last_year = worldYear;                         // without a date, the rollover would wipe it out at the first year change
        d.beef = bf.level;
    } else {
        const auto itb = save.beef.find(d.id);
        if (itb != save.beef.end() && itb->second.level > 0) {
            itb->second.level -= bal::MK_BEEF_MATE_WIN;
            if (itb->second.level < 0) itb->second.level = 0;
            itb->second.last_year = worldYear;
            d.beef = itb->second.level;
        }
    }
    return d;
}

void BeefOnSeatLost(SaveData& save, const SeatWatch& w, bool won) {
    if (won || w.rival_id.empty()) return;   // the grudge is generated by LOSING, not by competing
    Beef& bf = save.beef[w.rival_id];
    bf.level = (bf.level + bal::MK_BEEF_SEAT_LOST < 5) ? bf.level + bal::MK_BEEF_SEAT_LOST : 5;
    bf.last_year = save.current_world_year;   // without a date, BeefDecayAtRollover would wipe it out at the first year change
}

bool SeatWatchOnCooldown(const SaveData& save, const std::string& seriesId, const std::string& teamLabel,
                         int absToday) {
    for (const auto& w : save.seat_watches) {
        if (w.state != 2) continue;                       // only LOSING it cools it down; winning doesn't close anything
        if (w.series_id != seriesId || w.team_label != teamLabel) continue;
        if (absToday - w.resolved_day < bal::MK_SEAT_WATCH_COOLDOWN_DAYS) return true;
    }
    return false;
}

int SeatWatchPrune(SaveData& save, int absToday) {
    const size_t before = save.seat_watches.size();
    std::vector<SeatWatch> keep;
    keep.reserve(before);
    for (const auto& w : save.seat_watches) {
        if (w.state == 0) { keep.push_back(w); continue; }                       // alive
        if (absToday - w.resolved_day < bal::MK_SEAT_WATCH_COOLDOWN_DAYS) { keep.push_back(w); continue; }
        // closed and past its cooldown: nothing changes it anymore, drop it
    }
    save.seat_watches.swap(keep);
    return (int)(before - save.seat_watches.size());
}

} // namespace career
