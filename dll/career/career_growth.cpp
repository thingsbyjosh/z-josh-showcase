#include "career_growth.h"

#include "career_balance.h"
#include "career_data_json.h"   // BirthYearFor
#include "career_fields.h"      // AgeRisePart, DriverAge
#include "career_save.h"        // SaveData
#include "career_seats.h"       // SeatsForClass, OccupantRating, ClassAgeCeiling, BaseTalent
#include "paths.h"
#include <cstdio>
#include <cstdlib>
#include "career_series.h"      // AllSeries, IsEraMemorySeries

#include <algorithm>
#include <cmath>
#include <vector>

namespace career {

namespace {

// Stable hash (FNV-1a) — the same one the rest of the engine uses so there aren't two ways to draw lots.
unsigned long GrowthFnv1a(const std::string& s) {
    unsigned long h = 2166136261ul;
    for (char c : s) { h ^= (unsigned char)c; h *= 16777619ul; }
    return h;
}

}  // namespace

const char* ShapeName(GrowthShape s) {
    switch (s) {
        case GrowthShape::Meteorico:  return "meteorico";
        case GrowthShape::Progresivo: return "progresivo";
        case GrowthShape::Tardio:     return "tardio";
        case GrowthShape::Meseta:     return "meseta";
        case GrowthShape::SeApaga:    return "se apaga";
    }
    return "progresivo";
}

GrowthShape ShapeFor(const std::string& driverId) {
    if (driverId.empty()) return GrowthShape::Progresivo;
    // The distribution is not uniform on purpose: `progresivo` is the NORMAL and the rest are exceptions. A
    // world where a fifth of the people are meteoric has no meteors, it has noise.
    //   progresivo 40 · tardio 20 · meseta 20 · meteorico 12 · se apaga 8
    const unsigned long d = GrowthFnv1a(driverId + "|crecimiento") % 100ul;
    if (d < 40) return GrowthShape::Progresivo;
    if (d < 60) return GrowthShape::Tardio;
    if (d < 80) return GrowthShape::Meseta;
    if (d < 92) return GrowthShape::Meteorico;
    return GrowthShape::SeApaga;
}

double ShapeStep(GrowthShape s, int age) {
    if (age <= 0) return bal::GROWTH_STEP_BASE;          // no age: the normal pace, neither reward nor punishment
    const double base = bal::GROWTH_STEP_BASE;
    switch (s) {
        case GrowthShape::Meteorico:
            // Rises very fast and very young, and at 24 is already where he'll be.
            if (age <= 21) return base * 2.5;
            if (age <= 24) return base * 1.2;
            return base * 0.2;
        case GrowthShape::Tardio:
            // The one that stops anyone from being written off over his first season (): starts truly weak and
            // explodes around 26. Without this, following the farm system for years would have no appeal.
            if (age <= 22) return base * 0.3;
            if (age <= 25) return base * 0.9;
            if (age <= 30) return base * 2.2;
            return base * 0.8;
        case GrowthShape::Meseta:
            // Reaches his ceiling early and stays: rises fast and then stops moving.
            if (age <= 23) return base * 1.8;
            return base * 0.15;
        case GrowthShape::SeApaga:
            // The promise that doesn't pan out: rises early and then the natural pace turns against him.
            if (age <= 21) return base * 1.3;
            if (age <= 25) return base * 0.2;
            return -base * 0.6;
        case GrowthShape::Progresivo:
        default:
            return base;
    }
}

double GrowthProgress(const std::map<std::string, double>& curve, const std::string& id, int age, int ageShift) {
    const auto it = curve.find(id);
    if (it != curve.end() && it->second > 0.0) return it->second;
    // 17/09 — EXCEPT THE COHORT, WHICH STARTS WHERE ITS SEEDING SAYS. Its ceiling is the authored `tier_max`
    // (see BaseTalent), so the universal rise would have made it worth almost an ace on day one: the two
    // numbers of the pair — potential and starting point — have to come in together or not at all. As soon as they run a
    // season, `AdvanceGrowth` writes their entry and this shortcut stops being consulted.
    const double coh = CohortCurveStart(id);
    if (coh > 0.0) return coh;
    // ABSENT = THE UNIVERSAL RISE AT HIS AGE. It's what makes an old save, or a world that hasn't yet
    // run a season, give EXACTLY the same number as before `AgeRisePart` x existed.
    // `AgeDeclinePart` is `AgeCurveMultiplier`, and there is a test that pins it.
    return AgeRisePart(age, ageShift);
}

double GrowthProgress(const SaveData& save, const std::string& id, int age, int ageShift) {
    return GrowthProgress(save.driver_curve, id, age, ageShift);
}


// ===================================================================================================
// NAMED CAREER ARCHETYPES (see career_growth.h)
// ===================================================================================================

namespace {
std::vector<GrowthArchetype> g_arqs;
std::vector<double> g_corpus;          // real talents, SORTED
bool g_corpusHecho = false;

GrowthShape FormaPorNombre(const std::string& s) {
    if (s == "meteorico")  return GrowthShape::Meteorico;
    if (s == "tardio")     return GrowthShape::Tardio;
    if (s == "meseta")     return GrowthShape::Meseta;
    if (s == "se_apaga")   return GrowthShape::SeApaga;
    return GrowthShape::Progresivo;
}

std::vector<std::string> PartePipe(const char* line) {
    std::vector<std::string> out; std::string cur;
    for (const char* p = line; *p; ++p) {
        if (*p == '\r' || *p == '\n') continue;
        if (*p == '|') { out.push_back(cur); cur.clear(); } else cur += *p;
    }
    out.push_back(cur);
    return out;
}
}  // anon

bool LoadGrowthArchetypes(const char* path) {
    g_arqs.clear();
    FILE* f = paths::FOpen(path, "rb");
    if (!f) return false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n' || line[0] == '\0') continue;
        auto t = PartePipe(line);
        if (t.size() < 6) continue;
        if (t[0] == "id" || t[0].empty()) continue;          // header
        GrowthArchetype a;
        a.id = t[0]; a.referencia = t[1];
        a.pct_min = atof(t[2].c_str());
        a.pct_max = atof(t[3].c_str());
        a.forma = FormaPorNombre(t[4]);
        a.peso = atoi(t[5].c_str());
        if (t.size() > 6) a.nota = t[6];
        if (a.peso <= 0) continue;                            // weight 0 = disabled, not an error
        if (a.pct_max < a.pct_min) std::swap(a.pct_min, a.pct_max);
        g_arqs.push_back(a);
    }
    fclose(f);
    return true;
}

int GrowthArchetypeCount() { return (int)g_arqs.size(); }
const std::vector<GrowthArchetype>& GrowthArchetypes() { return g_arqs; }
std::string g_trazaGrowth;   // ~~~02/09: diagnostic only
double g_sueloSubidaMedicion = -1.0;   // ~~~02/09: <0 = the production one (sweep only)
double g_growthBendMedicion  = -1.0;   // ~~~07/09: <0 = production's GROWTH_BEND (sweep only)
void InvalidateTalentCorpus() { g_corpusHecho = false; g_corpus.clear(); }

const std::vector<double>& TalentCorpus() {
    if (g_corpusHecho) return g_corpus;
    g_corpusHecho = true;
    // The corpus is the REAL talents of the static world: the ones that already exist, with their shape. Cloning is
    // sampling from here, not from an invented curve. It's taken with an empty save on purpose: the reference is the
    // AUTHORED world, not what the market has left today -- otherwise generation 20 would clone 19 and the
    // distribution would drift downward by itself.
    const SaveData vacio;
    std::set<std::string> vistos;
    for (const auto& sd : AllSeries()) {
        for (const auto& s : SeatsForClass(sd.id, vacio)) {
            if (s.occupant_id.empty() || !vistos.insert(s.occupant_id).second) continue;
            const double t = BaseTalent(s.occupant_id);
            if (t > 0.0) g_corpus.push_back(t);
        }
    }
    std::sort(g_corpus.begin(), g_corpus.end());
    return g_corpus;
}

// ~~~02/09 — THE CORPUS OF A SINGLE TIER, and why it was needed.
//
// `RookieTalentCloned` sampled the WHOLE ladder: a kid born in karting drew his talent from the
// same bag as an F1 driver. Measured, the corpus runs from 86 (p10) to 400 (p90) and the ladder anchors each level
// every 52.5 points -- i.e. the corpus MEDIAN (257) is the anchor of a level 5-6. Each batch put
// oversized people in at the bottom, and after twenty seasons level 1 ended up worth MORE than 2, 3 and
// 4 (p50 1162 -> 1515). It wasn't a clogged market: it was the entry badly calibrated.
//
// Now the archetype's percentile is read over the people of ITS tier. A clone of a generational born
// for GT3 is the best in GT3, not an F1 driver parked in GT3 -- and he rises to F1 by RACING, because the
// ceiling (`tier_max`) still comes from the archetype's percentile and not from the absolute value.
//
// If the tier has few people it falls back to the global corpus: better the usual behavior than a
// sample of twelve.
static std::map<int, std::vector<double> > g_corpusTier;

const std::vector<double>& TalentCorpusForTier(int tier) {
    auto it = g_corpusTier.find(tier);
    if (it != g_corpusTier.end()) return it->second;
    std::vector<double>& v = g_corpusTier[tier];
    const SaveData vacio;
    std::set<std::string> vistos;
    for (const auto& sd : AllSeries()) {
        if (sd.level != tier) continue;
        for (const auto& s : SeatsForClass(sd.id, vacio)) {
            if (s.occupant_id.empty() || !vistos.insert(s.occupant_id).second) continue;
            const double t = BaseTalent(s.occupant_id);
            if (t > 0.0) v.push_back(t);
        }
    }
    std::sort(v.begin(), v.end());
    return v;
}

const GrowthArchetype* ArchetypeForRookie(unsigned long seed) {
    if (g_arqs.empty()) return nullptr;
    long total = 0;
    for (const auto& a : g_arqs) total += a.peso;
    if (total <= 0) return nullptr;
    const unsigned long d = GrowthFnv1a("arq|" + std::to_string(seed)) % (unsigned long)total;
    long acc = 0;
    for (const auto& a : g_arqs) { acc += a.peso; if ((long)d < acc) return &a; }
    return &g_arqs.back();
}

// This person's spot WITHIN his archetype's bracket. It's drawn separately because the two scales
// share it: if each drew its own, the same driver could be p90 in rating and p10 in card.
double RookiePercentile(unsigned long seed) {
    const GrowthArchetype* a = ArchetypeForRookie(seed);
    if (!a) return 0.5;
    const double u = (double)(GrowthFnv1a("pct|" + std::to_string(seed)) % 100000ul) / 100000.0;
    double pct = a->pct_min + u * (a->pct_max - a->pct_min);
    if (pct < 0.0) pct = 0.0;
    if (pct > 1.0) pct = 1.0;
    return pct;
}

double RookieTalentCloned(unsigned long seed, const GrowthArchetype** outArch, int tier) {
    if (outArch) *outArch = nullptr;
    const GrowthArchetype* a = ArchetypeForRookie(seed);
    if (!a) return 0.0;
    // ~~~02/09: the corpus of ITS tier if it's big enough; if not, the usual one.
    const std::vector<double>& global = TalentCorpus();
    const std::vector<double>& propio = (tier > 0) ? TalentCorpusForTier(tier) : global;
    const std::vector<double>& c = (propio.size() >= 50) ? propio : global;
    if (c.size() < 50) return 0.0;                            // insufficient corpus -> the usual formula
    if (outArch) *outArch = a;
    // His spot WITHIN the archetype's bracket: uniform, so that two of the same archetype don't come out
    // identical (the archetype gives the band, the person the number). Shared with the 0..1 card
    // (RookieProfileTalentCloned) via RookiePercentile: the same person, the same spot on both scales.
    size_t idx = (size_t)(RookiePercentile(seed) * (double)(c.size() - 1));
    if (idx >= c.size()) idx = c.size() - 1;
    return c[idx];
}

// 18/08 — THE TWIN OF `TalentCorpus` ON THE CARD SCALE (0..1). See career_growth.h.
static std::vector<double> g_corpusFichas;
static bool g_corpusFichasHecho = false;

const std::vector<double>& ProfileTalentCorpus() {
    if (g_corpusFichasHecho) return g_corpusFichas;
    g_corpusFichasHecho = true;
    // The cards of the AUTHORED world. Same criterion as TalentCorpus: the authoring is taken, not what the
    // market has left today -- otherwise generation 20 would clone 19 and the distribution would drift by itself
    // downward.
    for (double t : AllAuthoredProfileTalents())
        if (t > 0.0 && t <= 1.0) g_corpusFichas.push_back(t);
    std::sort(g_corpusFichas.begin(), g_corpusFichas.end());
    return g_corpusFichas;
}

double RookieProfileTalentCloned(unsigned long seed) {
    const std::vector<double>& c = ProfileTalentCorpus();
    if (c.size() < 50) return 0.0;                    // insufficient corpus -> the caller stays as it was
    size_t idx = (size_t)(RookiePercentile(seed) * (double)(c.size() - 1));
    if (idx >= c.size()) idx = c.size() - 1;
    return c[idx];
}

void InvalidateProfileTalentCorpus() { g_corpusFichasHecho = false; g_corpusFichas.clear(); }

// THE SHAPE ALSO COMES FROM THE ARCHETYPE, not from a separate draw. Without this the PSV's `forma` column would be
// decorative: a "generacional" could come out meseta, which is exactly the opposite of what the table says he is,
// a Verstappen. It's resolved BY HIS TALENT -- looking up the archetype whose percentile bracket contains his
// level -- so nothing needs to be stored in the save nor migrated, and it holds equally for a REAL driver:
// someone in the top 1% had, almost by definition, a top-tier career.
//
// Without a table or without a corpus it returns the usual draw (additive).
GrowthShape ShapeForTalent(const std::string& driverId, double talent) {
    if (talent <= 0.0 || g_arqs.empty()) return ShapeFor(driverId);
    const std::vector<double>& c = TalentCorpus();
    if (c.size() < 50) return ShapeFor(driverId);
    // ~~~02/09 — A GENERATED ONE IS ASKED ABOUT HIS SEED, not his value. Since talent is anchored
    //  to the entry tier, the absolute value no longer tells the percentile: a clone of a generational born in
    //  karting is worth little IN ABSOLUTE terms and would still be a generational. The seed keeps who he is.
    double pct;
    if (driverId.rfind("gn_", 0) == 0) {
        pct = RookiePercentile(SeedFromGeneratedId(driverId));
    } else {
        // His percentile within the corpus: how many people are left below.
        const size_t pos = (size_t)(std::lower_bound(c.begin(), c.end(), talent) - c.begin());
        pct = (double)pos / (double)(c.size() - 1);
    }
    // The NARROWEST archetype that contains it: if two overlap, the more specific rules (the
    // generational beats the phenomenon at 0.995, which is what is meant).
    const GrowthArchetype* mejor = nullptr;
    double masEstrecho = 2.0;
    for (const auto& a : g_arqs) {
        if (pct < a.pct_min || pct > a.pct_max) continue;
        const double ancho = a.pct_max - a.pct_min;
        if (ancho < masEstrecho) { masEstrecho = ancho; mejor = &a; }
    }
    return mejor ? mejor->forma : ShapeFor(driverId);
}

GrowthReport AdvanceGrowth(SaveData& save, int worldYear, const std::set<std::string>& skip) {
    GrowthReport rep;
    if (worldYear <= 0) return rep;

    // ONE PERSON, ONE ARC PER SEASON. The circumstance of ALL his series is accumulated and applied a
    // single time at the end. Applying it inside the series loop seemed natural and was wrong: there are 346 drivers
    // with a seat in more than one series — one with TWELVE — and they advanced their arc once per seat, so
    // ran twelve times faster toward their ceiling than the rest. It showed because the average progress came out 1.27
    // when the ceiling is 1.0.
    //
    // And averaging is also the fair thing: if you race in three championships, your year is all three.
    struct Acumulado { double circ = 0.0; int n = 0, edad = 0, shift = 0; };
    std::map<std::string, Acumulado> porPiloto;

    for (const auto& sd : AllSeries()) {
        if (IsEraMemorySeries(sd)) continue;

        // THE CIRCUMSTANCE IS MEASURED IN RANKS, NOT IN POINTS, and that decision avoids at the root the failure that has
        // already appeared TWICE in this project: subtracting two magnitudes that live on different scales
        // (talent 0.859/0.080 against RatingQuality 0.451/0.237; capability 1393/105 against ELO 1513/318). A
        // rank has no units, so there's nothing to standardize and no bias that can slip in.
        //
        // What is compared: where you FINISH (your ELO within your field) against where your capability said
        // you'd finish. Performing above what you promise brings you closer to your ceiling; below, it moves you away.
        struct Fila { std::string id; double cap = 0.0, elo = 0.0; int rc = 0, re = 0; };
        std::vector<Fila> filas;
        for (const auto& seat : SeatsForClass(sd.id, save)) {
            if (seat.open || seat.occupant_id.empty()) continue;
            if (save.retired.count(seat.occupant_id)) continue;
            if (skip.count(seat.occupant_id)) continue;
            const auto ie = save.driver_ratings.find(seat.occupant_id);
            if (ie == save.driver_ratings.end() || ie->second <= 0.0) continue;
            const double cap = OccupantRating(sd.id, seat.occupant_id, save, worldYear);
            if (cap <= 0.0) continue;
            Fila f; f.id = seat.occupant_id; f.cap = cap; f.elo = ie->second;
            filas.push_back(f);
        }
        const int n = (int)filas.size();
        if (n < 4) continue;      // a field of three gives no ranks to say anything with

        std::vector<int> idx(n);
        for (int i = 0; i < n; i++) idx[i] = i;
        // Ties by id: the order can't depend on how the seat map was walked.
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            if (filas[a].cap != filas[b].cap) return filas[a].cap > filas[b].cap;
            return filas[a].id < filas[b].id;
        });
        for (int r = 0; r < n; r++) filas[idx[r]].rc = r;
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            if (filas[a].elo != filas[b].elo) return filas[a].elo > filas[b].elo;
            return filas[a].id < filas[b].id;
        });
        for (int r = 0; r < n; r++) filas[idx[r]].re = r;

        // The same peak shift that the rest of the engine uses (GT and endurance peak later). It is
        // recomputed here instead of exporting career_seats's helper because it's a subtraction and exporting it
        // would couple the two files over one line.
        const int techoEdad = ClassAgeCeiling(sd.class_name);
        const int shift = techoEdad > bal::RETIRE_AGE_BASE ? techoEdad - bal::RETIRE_AGE_BASE : 0;
        const double medioCampo = (double)n * 0.5;
        for (const auto& f : filas) {
            int by = 0;
            const auto ib = save.birth_year.find(f.id);
            if (ib != save.birth_year.end() && ib->second) by = ib->second;
            if (by == 0) by = BirthYearFor(f.id);
            const int edad = DriverAge(by, worldYear);

            // circ > 0 = you finish BETTER than your capability promised (re less than rc).
            double circ = (double)(f.rc - f.re) / medioCampo;
            if (circ >  1.0) circ =  1.0;
            if (circ < -1.0) circ = -1.0;

            Acumulado& ac = porPiloto[f.id];
            ac.circ += circ;
            if (ac.n == 0) { ac.edad = edad; ac.shift = shift; }   // his first series sets age and shift
            ac.n++;
        }
    }

    for (const auto& kv : porPiloto) {
        const Acumulado& ac = kv.second;
        if (ac.n <= 0) continue;
        const double circ = ac.circ / (double)ac.n;

        const double bend = g_growthBendMedicion >= 0.0 ? g_growthBendMedicion : bal::GROWTH_BEND;
        double factor = 1.0 + bend * circ;
        if (factor < bal::GROWTH_FACTOR_MIN) factor = bal::GROWTH_FACTOR_MIN;
        if (factor > bal::GROWTH_FACTOR_MAX) factor = bal::GROWTH_FACTOR_MAX;
        // ~~~02/09 — WHILE YOU RISE, A BAD SEASON SLOWS YOU DOWN; IT DOESN'T DE-DEVELOP YOU.
        //
        // says that the RISE half is personal progress "bent by what happens to him on track" and
        // that the DECLINE half is age's. Bending isn't inverting, and with `GROWTH_FACTOR_MIN = -1` it was:
        // traced a rookie with F1 potential, `circ` came out -1.000 SATURATED at 20 and at 21, i.e.
        // factor -1, so every season pushed his curve backward. He stayed stuck at 0.648
        // for four years -- only the floor saved him -- and at 38 he was still at 0.715 against a potential
        // of 1712. Zero of 45 born with ace potential reached the premier category in twenty seasons.
        //
        // And the circularity is the worst part: the ladder lifts him to level 6 in two years, there he loses all the
        // races because he is not yet who he'll be, his ELO sinks below what his capability
        // promised, and growth reads "performs below" and punishes him. We penalize him for being where
        // we put him ourselves.
        //
        // Before the peak, the factor's floor is ZERO: a bad year doesn't set you ahead, and that's it. After the
        // peak nothing is touched -- there the decline is indeed the right answer.
        // AND THE FLOOR IS NOT ZERO, IT'S `GROWTH_FACTOR_RISING_MIN`. With zero he still didn't grow: `circ` measures
        //  your rank by CAPABILITY against your rank by RESULTS, so the weakest on a grid
        //  at most ties -- he can't outdo himself, by the definition of the measure. And a kid with
        //  potential gets lifted by the ladder to a category where he is exactly that, the last. Traced:

        //  `circ` saturated at -1 at 20, 21, 22, 25 and 26. A bad year slows development; it doesn't switch it off.
        const double sueloSubida = g_sueloSubidaMedicion >= 0.0 ? g_sueloSubidaMedicion
                                                                : bal::GROWTH_FACTOR_RISING_MIN;
        if (ac.edad > 0 && ac.edad < bal::AGE_PEAK_LO + ac.shift && factor < sueloSubida)
            factor = sueloSubida;

        const double antes = GrowthProgress(save, kv.first, ac.edad, ac.shift);
        // His talent: the save's if he's generated, the authored one if he's real. His SHAPE comes from there.
        double suTalento = 0.0;
        {
            const auto gd = save.gen_drivers.find(kv.first);
            suTalento = (gd != save.gen_drivers.end()) ? gd->second.talent : BaseTalent(kv.first);
        }
        // ~~~02/09 — THE STEP IS NORMALIZED TO THE TRAVEL THAT'S LEFT TO YOU, and without this the above is useless.
        //
        // The shape is calibrated to take you from 0.82 to 1.0: an 18% travel, because until today EVERYONE
        // was born at 0.82. Since the rookie starts where he belongs for his tier (0.5-0.65 if his
        // potential is F1 and he's born in a promotion), he has to travel 40% and the shape only knows how to
        // deliver 18%: measured, the 45 born with ace potential stayed stuck at curve
        // 0.64-0.70 with ELO 1350-1450 against a potential of 1700. They reached level 6 in two years and there
        // they died, because they didn't make the grade for the last tier -- ZERO of 45 reached the premier category.
        //
        // The factor is 1 for everyone at 0.82 or above, so the usual world isn't
        // altered at all: it only accelerates whoever starts below, in exact proportion to what he lacks.
        double recorrido = 1.0;
        if (antes < bal::AGE_RISE_FLOOR)
            recorrido = (1.0 - antes) / (1.0 - bal::AGE_RISE_FLOOR);
        const double paso = ShapeStep(ShapeForTalent(kv.first, suTalento), ac.edad);
        double ahora = antes + paso * factor * recorrido;
        if (!g_trazaGrowth.empty() && kv.first == g_trazaGrowth)
            printf("  [growth] antes %.3f  edad %d  series %d  circ %+.3f  factor %+.3f  paso %.4f  recorrido %.2f  -> %.3f\n",
                   antes, ac.edad, ac.n, circ, factor, paso, recorrido, ahora);
        // The ceiling is 1.0 and it's the PERSON's: `BaseTalent` is NEVER touched, so nobody ends up worth
        // more than what he is. And the floor is the rookie's — however badly it goes, you can't end up worse than you
        // were when starting.
        if (ahora > 1.0) ahora = 1.0;
        // ~~~02/09 — THE FLOOR IS YOURS. It was 0.82 for everyone, and with the start curve of a
        //  rookie (global potential, his tier's starting point) that teleported him to 0.82 in the
        //  first winter -- just what the start curve wants to prevent. For everyone already
        //  at 0.82 or above the number doesn't change: the minimum is still 0.82.
        const double suelo = (antes < bal::AGE_RISE_FLOOR) ? antes : bal::AGE_RISE_FLOOR;
        if (ahora < suelo) ahora = suelo;

        if (std::fabs(ahora - antes) > 1e-9) {
            if (ahora > antes) rep.subieron++; else rep.bajaron++;
            rep.movidos++;
        }
        save.driver_curve[kv.first] = ahora;
        if (ahora >= 0.9999) rep.techo++;
        rep.medio += ahora;
    }
    if (!porPiloto.empty()) rep.medio /= (double)porPiloto.size();
    return rep;
}

}  // namespace career
