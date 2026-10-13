// dll/career/career_traits.cpp
#include "career_traits.h"
#include <algorithm>
#include <cmath>

namespace career {
namespace {

inline double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// RECALIBRATED 11/08 AGAINST THE 346 MEASURED ANCHORS (). The shape of this model was right —loadings
// per trait, shared subfactors, accounted variance— but its constants came from a corpus where
// 85% of the cards take their knobs from a POSITIONAL CURVE, and the curve inflates all the correlations.
// Measured, the product kLamRace*kLamX reproduced exactly what it measured at the output:
//
//     trait    before (lam_race*lam_X)   measured in the 346    new lam_X
//     qual           0.884                    0.829             0.873
//     cons           0.694                    0.465             0.489
//     tyre           0.523                    0.254             0.267
//     stam           0.513                    0.129             0.136
//     wet            0.494                    0.290             0.305
//     avmi           0.485                    0.113             0.119
//     avfm           0.447                    0.176             0.185
//     defn           0.399                    0.133             0.140
//
// kLamRace is NOT touched. The grid width hangs off race_skill's dispersion, and lowering it would repeat the
// affinity error of 09/08: fixing one thing by narrowing the grids by 20%.
// corr(race, X) = lambda_race * lambda_X  ->  lambda_X = corr / 0.95
constexpr double kLamRace  = 0.95;
constexpr double kLamQual  = 0.873;
constexpr double kLamCons  = 0.489;
constexpr double kLamTyre  = 0.267;
constexpr double kLamStam  = 0.136;
constexpr double kLamWet   = 0.305;
constexpr double kLamAvMi  = 0.119;
constexpr double kLamAvFm  = 0.185;
constexpr double kLamAggr  = 0.235;   // aggression: measured 0.223
constexpr double kLamDefn  = 0.140;   // defending separate: measured 0.133, half of aggression
constexpr double kLamStrt  = 0.492;   // starts separate: measured 0.467, they stick VERY close to pace

// AND A SINGLE SUBFACTOR INSTEAD OF TWO PAIRS: CRAFT. Once each knob has its pace part removed, the
// residuals of the anchors don't end up independent — they share an axis, and it's strong. Loadings measured on it:
//
//     avoidance_of_mistakes 0.87 · stamina 0.71 · defending 0.68 · consistency 0.67
//     vehicle_reliability 0.66 · start_reactions 0.66 · wet_skill 0.63 · tyre_management 0.61
//     qualifying_skill 0.08  <-- the ONLY one left out
//
// I.e.: speed and CRAFT are two different axes, qualifying belongs only to the first and all the
// rest shares the second. The two narrow pairs from before (errors, mechanical) were a poor version of
// this: they tied avmi with avfm and tyre with stam, but left consistency, wet, defending and starts loose,
// which in reality go along with them. Control done: craft varies WITHIN a pack as much as between packs
// (sd 0.652 vs 0.654), so it belongs to the person and isn't the style of whoever authored the pack.
//
// mu_X = measured_loading * sqrt(1 - lam_X^2)   (the loading is on the RESIDUAL; mu is on the total)
constexpr double kMuCons = 0.583, kMuDefn = 0.668, kMuWet  = 0.605, kMuTyre = 0.586;
constexpr double kMuStam = 0.707, kMuAvMi = 0.868, kMuAvFm = 0.709, kMuStrt = 0.571;
// Aggression does NOT get craft even though it measures a loading of 0.477. It's a CHARACTER axis —the manual says
// more isn't better— and hanging it off craft would claim that whoever has more craft is more aggressive, which isn't
// a conclusion these data support: what they measure is that whoever was authored well was authored well
// at everything. The others with high loading (reliability 0.657, fuel 0.648) have an owner elsewhere:
// reliability belongs to the TEAM (spec ) and fuel/weather_tyre are set by the archetype's target.

// Target standard deviations within a real grid.
constexpr double kSdRace = 0.078, kSdQual = 0.077, kSdStam = 0.100, kSdCons = 0.112;
constexpr double kSdTyre = 0.121, kSdAggr = 0.142, kSdMist = 0.148, kSdWet  = 0.152;

// Counterweight: the aggressive driver pays in consistency and mistakes. Tuned so the
// final correlations land at 0.40 (pace), 0.27 (mistakes) and 0.19 (mistakes under pressure).
constexpr double kCwCons = 0.10, kCwAvMi = 0.12, kCwAvFm = 0.15;

// Shift of the BAR for the two error traits (see the long comment in TraitsFor). Negative =
// the whole grid makes more mistakes. It touches neither correlation nor SD: both are MEASURED and stay put.
constexpr double kMistakeLevelShift = -0.08;

// REFERENCE talent band (spec p10 0.75, median 0.90, p90 0.98 -> usable band 0.72..1.00).
// Talent enters the model STANDARDIZED, (talent - center) / sd, because the loadings above are
// written in standard-deviation units: corr(race,X) = lam_race*lam_X only holds if the common
// factor has variance 1. Dividing by a number bigger than the band's real SD leaves ALL
// correlations short by the same percentage (with 0.09 they ended up 10% below what was measured).
constexpr double kTalentMid = 0.86;              // center of the 0.72..1.00 band
constexpr double kTalentSd  = 0.0808;            // SD of that band spread uniformly = 0.28/sqrt(12)

// Trait anchor. A trait is NOT hung off raw talent: it's anchored to a fixed center and talent
// enters ONLY through its loading. Adding raw talent (as a base) injects a second common factor identical for
// every trait and drives correlations up to ~0.87, which is exactly the scalar model this
// module exists to kill. The anchor sits 1.6 of the trait's own standard deviations below the 1.00 ceiling: that way the
// best of the grid grazes 1.00 without half the grid piling up there (a trait with SD 0.152
// centered at 0.90 would have a third of the population pinned at 1.00 and its measured SD would be impossible).
constexpr double kAnchorHeadroom = 1.6;
constexpr double kAnchorCap      = 0.90;         // the measured race_skill median: no anchor rises above that
inline double AnchorFor(double sd) { double a = 1.0 - kAnchorHeadroom * sd; return a > kAnchorCap ? kAnchorCap : a; }

// The archetype's bias pushes through the SLACK that remains (see the header): asymptotic, it never crushes a
// driver's own variance against the ceiling or the floor.
inline double ApplyBias(double v, double bias) {
    if (bias > 0.0) return v + bias * (1.0 - v);
    if (bias < 0.0) return v + bias * v;
    return v;
}

// Genome stream indices. Fixed: changing them changes EVERY driver.
enum GenIdx { G_RACE=0, G_QUAL, G_AGGR, G_DEFN, G_STAM, G_CONS, G_STRT, G_WET,
              G_TYRE, G_AVMI, G_AVFM, G_FERR, G_FMEC, G_MISC,
              // 2026-07-27: deliberately AT THE END. GenomeNormal indexes by (seed, idx), so adding here
              // doesn't shift a single draw for existing traits -> the previous grid comes out IDENTICAL.
              G_FUEL, G_WXTY };

// Genome indices reserved for affinity (behind the traits, so as not to shift them).
constexpr int kAffBase = 40;
// 2026-08-09 -- AFFINITY WAS WIDER THAN THE WHOLE CLASS, and that's why it was deciding who the ace was.
// It stood at sd 0.055 / cap 0.12. The talent spread the curve hands out across a FULL grid is
// 0.125 (CurveSkill over [0.85, 1.00]): meaning per-family affinity, on its own, measured the same as
// the distance from first to last. In classes with no authored talent, the ace wasn't chosen by talent,
// it was chosen by this draw -- measured across the 109 production XML, in 12 classes the ace ate up MORE THAN 35%
// of the entire top-10 gap (P3 86%, Larkspur_Zel878 72%, GARROWBY_HEX394 50%), and across the dump's 100 grids the
// ace's lead over 2nd reaches p90 0.305 ref_span units with the next step at 0.014.
// See customai_affinity_noise_decides_ace.
// TESTED AND REVERTED THE SAME DAY (2026-08-09), and the result changes the diagnosis. Turning it down to a third
// (sd 0.018 / cap 0.040) does fix the ace, yes -- but **NARROWS THE GRIDS BY 20%**: the average width across
// the 114 series drops from 0.389 to 0.309, the suite loses its contract that the generated spread matches the
// class's (`within20 >= 90%`), and F3's flips (irregularity rises in 28 series and drops in 54).
//
// So affinity was NOT just noise on top of talent: it was **carrying a fifth of the grid's
// width**. And that's the same underlying flaw seen from the other side -- the spread the class
// curve hands out (0.125 in CurveSkill over [0.85,1.00]) is TOO NARROW, and affinity was
// papering over it. That's why we hand back only 4.6% against the 10-12% that Curve, [competitor],
// and another all agree on: without affinity it would be even less.
// THE PRACTICAL CONSEQUENCE: affinity CANNOT be turned down on its own. It has to happen alongside widening
// the class curve, which is exactly the talent-scale work that's also blocking Curve's
// band (see customai_affinity_noise_decides_ace and curve_ruleset_applied). One change, not two.
constexpr double kAffSd  = 0.055;   // typical +-0.06, tail up to ~+-0.12 for a strongly marked specialist
constexpr double kAffCap = 0.12;    // hard cap on the tail

// 26/08 — how much of its archetype shift a driver has already reached, by level. 0.60 for the
// back of the grid, 1.00 for the elite. Not 0 at the bottom: a rookie has character too, just less
// pronounced. It's the only invented figure in this change and it's the knob if the back comes out flat.
static const double kMatMin = 0.60;
inline double Madurez(double talent) {
    const double lvl = (talent - kTalentMid) / (3.0 * kTalentSd);
    return kMatMin + (1.0 - kMatMin) * (0.5 + 0.5 * (lvl < -1 ? -1 : (lvl > 1 ? 1 : lvl)));
}

// Composes a trait: anchor + (talent + sub-factor + own noise) scaled to its target SD + bias.
// The residual part uses the variance NOT explained by talent or sub-factor, so the total SD checks out:
// lam^2 + mu^2 + residual^2 = 1, so Var(trait) = sd^2 and corr(A,B) = lamA*lamB + muA*muB.
inline double Compose(double talent, double lam, double sub, double mu,
                      double noise, double sd, double bias, double centro = 0.0) {
    double explained = lam * lam + mu * mu;
    double residual  = explained >= 1.0 ? 0.0 : std::sqrt(1.0 - explained);
    double z = lam * ((talent - kTalentMid) / kTalentSd) + mu * sub + residual * noise;
    // 26/08 — THE BIAS IS ADDED LINEARLY AND SCALED BY MATURITY, not by headroom.
    // `ApplyBias` pushed ASYMPTOTICALLY (v + bias*(1-v)), so nobody ever reached their grade and the whole
    // grid got bunched up: RAW defending came out between 0.14 and 0.41 and that's why raising ceilings did
    // nothing. Added linearly, the archetype shifts UP TO where its grade says.
    // And maturity (driver level) decides how much of that shift has already been reached: , the
    // inherited grade is the DESTINATION, not what you have on day one. A mid-talent `metronomo` is already
    // more consistent than its generic twin -- just less than an elite one.
    // 26/08 — THE CENTER IS THE `solido` GRADE, not `1 - 1.6*sd`. The two were at different levels
    // for the same thing: the anchor said the average driver has 0.90 consistency and the re-anchored grade says
    // 0.74. Adding a shift on top of an anchor that's already high goes out through the ceiling -- two drivers
    // of the same archetype came out pinned at 1.00, which is exactly what the struct's comment
    // had warned about since July. With the center at the grade, a `metronomo` lands at ITS 0.94 and not at the cap.
    // center <= 0 -> the usual anchor (neutral bias, i.e. the statistical tests: they don't move).
    const double ancla = (centro > 0.0) ? centro : AnchorFor(sd);
    // 26/08 — THIS PERSON'S PLACE FIRST, THE NOISE AFTER AND WITHIN THE GAP THAT REMAINS.
    // Adding the noise and the shift separately and clipping at the end piles people up again at
    // exactly 1.000: with consistency (sd 0.112) a `metronomo` centered at 0.91 saturated one in every
    // five, and the human band maps ALL of them to the same 0.99. It's the defect of the 464 drivers with
    // exactly 1.000 showing up again through another door. So the center absorbs the shift and the
    // noise is scaled to the gap left up to the nearest edge: 2.2 sigma fit without touching it.
    // Intended consequence: an archetype with an EXTREME grade comes out more like its peers than one with a
    // middling grade. That's right on a bounded scale -- at the very top there's no room to differ --
    // and it preserves exactly the ORDER and the LEVEL of the grades, which is what was asked for.
    // 26/08 (corrected) — THE SQUEEZE IS RELATIVE TO THE GAP THE MODEL ALREADY ASSUMED, not absolute.
    // The first version did `s = min(sd, gap/2.2)` looking only at the final value, and that also shrank
    // the driver WITHOUT an archetype: wet dropped from SD 0.152 to 0.107 and errors from 0.148 to 0.105. Those
    // two SDs are MEASURED against the corpus and are the factor model's variance contract -- the
    // suite catches it in two checks. Tied to the SHIFT, with neutral bias the ratio is 1 and the
    // model comes out byte for byte as it was; only what the archetype has eaten of headroom gets squeezed.
    const double c     = Clamp01(ancla + Madurez(talent) * bias);
    const double h0    = (ancla < 1.0 - ancla) ? ancla : 1.0 - ancla;
    const double h1    = (c < 1.0 - c) ? c : 1.0 - c;
    const double razon = (h0 > 0.0 && h1 < h0) ? h1 / h0 : 1.0;
    return Clamp01(c + sd * razon * z);
}

}   // namespace

// 2026-07-27: per-driver spread for fuel/weather_tyre around the archetype's target. Small on
// purpose: the archetype sets the CHARACTER (0.46/0.45 range); this only stops two teammates
// of the same archetype from pitting on the exact SAME lap.
static const double kSdSoft = 0.05;
// 26/08 — the OWN variation of defending within a single archetype. Wider than kSdSoft (0.05)
// on purpose: that one only avoids two teammates pitting on the same lap, and here we do want
// two fighters to look different. 0.10 = +-0.20 in 95% of cases on a grade of 0.55 to 0.95.
static const double kSdDefend = 0.10;

// 26/08 — THE ARCHETYPE'S GRADE IS THE DESTINATION, NOT THE VALUE (). The user put it this way:
// «growth must preserve the shape of where it comes from [...] but arrive at the end with a shape similar to
// the top of the driver it inherits». And a question of theirs catches it: if the grade is written as is, a
// `peleon` who is the F1 ace and a `peleon` who is last in karts defend EXACTLY THE SAME, because
// talent doesn't enter anywhere. That's the absolute reading, and the decision was the RELATIVE one: an
// amateur metronome is the most regular of THEIR kind and is still more erratic than any Pro.
// Here the level modulates HOW MUCH of its grade has already been reached. It preserves the archetype's ORDER within each
// level -- a fighter defends more than a clean driver of the same talent -- and orders by level among equals.
// 26/08 (corrected) — MATURITY INTERPOLATES, IT DOESN'T SHRINK. The first version did `grade * m`, and that
// applied an ABSOLUTE penalty: a mid-talent `metronomo` came out LESS consistent than a generic driver
// of the same talent, which is absurd -- its grade says it's a metronome. It was caught by three
// «archetype against neutral twin» checks at once. Now it interpolates FROM the base of its level TOWARD
// its grade: whoever starts is at the generic for how good they are, and gets closer to their shape. An
// archetype with a high grade ALWAYS ends up above the neutral one, and one with a low grade below, which is
// what having character means. kMatMin = 0.60: what the back-of-the-grid driver has already covered toward their grade.
// 26/08 — CRAFT KEEPS ENTERING ON THE GRADE PATH, and this was caught by the harness, not by me.
// By skipping `Compose` so the trait would come from its grade I took the `craft` term with it,
// which is how the 346 people with MEASURED attributes put their craft into the grid (.0). The
// test `r(craft, avoidance_of_mistakes) > 0.35` failed instantly, with its comment right beside it: «and what
// it brings gets USED: inert would give ~0». It was a real regression: it disconnected the measured corpus from the XML.
// So the grade path is: the archetype's GRADE (the center) + the person's CRAFT (what
// distinguishes them within it) + own noise. All three layers, not two.
static const double kMatMin = 0.60;



TraitVector TraitsFor(uint64_t seed, double talent, const TraitBias& bias, const double* craftOverride) {
    TraitVector t;
    // CRAFT: a single axis shared by the eight quality knobs. It's taken from the G_FERR band, which already
    // existed and was one of the two pairs: that way the draws don't shift and the grid only changes by what
    // this change wants to change. G_FMEC is left unused on purpose — moving it would shift the flow.
    // And if the person has it MEASURED, the measurement rules: it's the only thing in the anchors that travels from one pack to
    // another, because craft is theirs whereas their fifteen knobs are relative to the grid where they were
    // measured.
    const double craft = craftOverride ? *craftOverride : GenomeNormal(seed, G_FERR);

    // Aggression is NOT anchored high: it isn't "more is better", its center is the midpoint.
    // 26/08 — AGGRESSION COMES FROM ITS GRADE. Before: center 0.50 for EVERYONE + asymptotic push of the
    // bias, so the low tail slammed into the clipping floor -- minimum EXACTLY 0.200 in the
    // four measured classes, i.e. the whole grid stuck to the edge. The grades were already good (0.42 to
    // 0.95, `solido` at 0.60, against 0.39-0.91 and median 0.56 of the 324 real ones): the defect was the
    // MECHANISM, not the data. Without a target (neutral bias = the tests) it falls to the usual path.
    // 26/08 — same as Compose: the bias is ADDED linearly and scaled by maturity. Before it went through
    // ApplyBias and the low tail slammed into the clipping floor (minimum EXACTLY 0.200 in the four
    // measured classes, the whole grid stuck to the edge).
    // 26/08 — same shape as Compose: the center absorbs the shift and the noise fits in the
    // gap. Here the center is 0.50 (aggression isn't «more is better»), so it only compresses on
    // the marked archetypes -- a `cargador` has less room to differ than a `solido`.
    {
        const double zA = kLamAggr * ((talent-kTalentMid)/kTalentSd)
                        + std::sqrt(1.0 - kLamAggr*kLamAggr) * GenomeNormal(seed, G_AGGR);
        const double cA = Clamp01(0.50 + Madurez(talent) * bias.aggression);
        const double hA = (cA < 1.0 - cA) ? cA : 1.0 - cA;
        const double rA = (hA < 0.50) ? hA / 0.50 : 1.0;   // gap from the 0.50 center = 0.50
        t.aggression  = Clamp01(cA + kSdAggr * rA * zA);
    }
    const double aggrCw = t.aggression - 0.5;

    t.race_skill      = Compose(talent, kLamRace, 0, 0, GenomeNormal(seed, G_RACE), kSdRace, bias.race_skill);
    // Qualifying is the ONLY one without craft: measured, its loading on that axis is 0.08. It sticks to
    // pace and nothing else, and that's no defect of ours — it's how motorsport is.
    t.qualifying      = Compose(talent, kLamQual, 0, 0, GenomeNormal(seed, G_QUAL), kSdQual, bias.qualifying);
    t.consistency     = Compose(talent, kLamCons, craft, kMuCons, GenomeNormal(seed, G_CONS), kSdCons, bias.consistency, bias.consistency_target) - kCwCons*aggrCw;
    t.wet_skill       = Compose(talent, kLamWet,  craft, kMuWet,  GenomeNormal(seed, G_WET),  kSdWet,  bias.wet_skill, bias.wet_skill_target);
    t.tyre_management = Compose(talent, kLamTyre, craft, kMuTyre, GenomeNormal(seed, G_TYRE), kSdTyre, bias.tyre_management, bias.tyre_management_target);
    t.stamina         = Compose(talent, kLamStam, craft, kMuStam, GenomeNormal(seed, G_STAM), kSdStam, bias.stamina, bias.stamina_target);
    t.avoid_mistakes  = Compose(talent, kLamAvMi, craft, kMuAvMi, GenomeNormal(seed, G_AVMI), kSdMist, bias.avoid_mistakes, bias.avoid_mistakes_target) - kCwAvMi*aggrCw;
    t.avoid_forced    = Compose(talent, kLamAvFm, craft, kMuAvFm, GenomeNormal(seed, G_AVFM), kSdMist, bias.avoid_forced, bias.avoid_forced_target) - kCwAvFm*aggrCw;
    // 26/08 — DEFENDING COMES FROM ITS GRADE, not from a push on a common anchor. Same pattern as
    // fuel/weather further down. `kSdDefend` is the OWN variation within the archetype: two `guerrera`
    // don't defend the same, but both defend. Without a target (neutral bias, i.e. the tests) it falls to the
    // usual path and not a single statistical check moves.
    t.defending       = Compose(talent, kLamDefn, craft, kMuDefn, GenomeNormal(seed, G_DEFN), kSdAggr, bias.defending, bias.defending_target);
    t.start_reactions = Compose(talent, kLamStrt, craft, kMuStrt, GenomeNormal(seed, G_STRT), kSdStam, bias.start_reactions, bias.start_reactions_target);

    t.consistency    = Clamp01(t.consistency);
    // 2026-07-28 -- a literal user request: "in general, have the AI make mistakes". The BAR for the
    // two error traits gets lowered, which is the one thing in this model that does NOT come from a measurement: the correlations and
    // standard deviations come from the matrix measured over real grids, but the LEVEL is set by the anchor
    // (1 - 1.6*sd), a construction chosen so the best of the grid grazes 1.00.
    // A CONSTANT shift leaves both measured quantities untouched and only moves the bar, which is
    // exactly what was requested. Previously measured across 101 production grids (2,346 drivers):
    // avoidance_of_mistakes p10 0.429, p50 0.600, p90 0.886.
    // It's the per-driver counterpart to the game's GLOBAL adjustment (OpponentMistakes, already raised to x4/x5 by phase
    // in an earlier commit): that one multiplies the frequency for the whole grid, this one decides who fails more.
    t.avoid_mistakes = Clamp01(t.avoid_mistakes + kMistakeLevelShift);
    t.avoid_forced   = Clamp01(t.avoid_forced   + kMistakeLevelShift);

    // blue_flag stays flat: it's set by the CLASS ROLE in multiclass, not the driver (F1/T6, correct).
    t.blue_flag       = 0.95;
    // 2026-07-27 -- fuel and weather_tyre STOP being flat. They sat at 0.65 and 0.70 for the WHOLE grid (measured in
    // the XML: 0.00 spread across the three sampled classes) while ProfileForArch had them authored with a
    // range of 0.46 and 0.45. The two reasons in the earlier comment ("fuel only matters on ovals",
    // "weather_tyre barely adds character") were true when written and aren't today: with high wear and
    // consumption, fuel FIXES the pit window, and in the rain weather_tyre decides the lap of the
    // switch to wets. With both flat, the WHOLE grid pits on the same lap -> a race with no dynamics
    // (observed on track on 2026-07-27). The archetype sets the target; the genome gives per-driver
    // variation so two teammates of the same archetype aren't clones. No archetype -> the old flat values.
    t.fuel_management = (bias.fuel_target > 0.0)
                      ? Clamp01(bias.fuel_target + kSdSoft * GenomeNormal(seed, G_FUEL)) : 0.65;
    t.weather_tyre    = (bias.weather_tyre_target > 0.0)
                      ? Clamp01(bias.weather_tyre_target + kSdSoft * GenomeNormal(seed, G_WXTY)) : 0.70;
    return t;
}

// Affinity by car family: ZERO SUM across families -- nobody is good at everything, what's gained in
// single-seaters is paid for in tin-tops. The n families are drawn and the mean is subtracted (that gives the exact
// zero sum); if the tail exceeds the cap, the WHOLE vector is SCALED rather than clamped trait by trait,
// because clamping just one breaks the zero sum, the property the whole model rests on.
// 17/08 — THE PER-FAMILY AFFINITY STAYS OFF IN PRODUCTION (user's decision: «remove the
// affinity»). The mechanism isn't deleted: the zero sum and the split are still here, correct, and it's enough to set
// this to 1.0 to get them back.
//
// The reason, measured: its cap (kAffCap 0.12) was the size of the ENTIRE F1 grid (0.11 of talent),
// so it added no texture, it drew the order by lot. With affinity, the worst driver came out 10 places ahead
// or behind what his card says and one of the six best fell out of the lead group; without it,
// 0 and 0. And the grid doesn't narrow by removing it: it WIDENS (0.2510 -> 0.3000 in Juniper_Cup),
// because with less noise there's less to clip.
//
// What's lost, plainly: the same driver now performs the same in an open-wheeler and in a GT. Recovering that
// texture without drawing the order by lot requires expressing the cap in race_skill and converting it PER CLASS (in F1 there
// is no room for a perceptible affinity; in an open category there is), and that belongs to the
// conversion phase. See docs/superpowers/specs/2026-08-17-curva-por-clase-y-vara-viva-design.md.
double g_affinityScale = 0.0;   // 0 = off in production; 1.0 brings it fully back

double AffinityFor(uint64_t seed, CarFamily f) {
    if (g_affinityScale == 0.0) return 0.0;   // shortcut: without affinity there's no need to draw
    const int n = (int)CarFamily::COUNT;
    double v[(int)CarFamily::COUNT], mean = 0;
    for (int i = 0; i < n; i++) { v[i] = kAffSd * GenomeNormal(seed, kAffBase + i); mean += v[i]; }
    mean /= n;
    double peak = 0;
    for (int i = 0; i < n; i++) { v[i] -= mean; double a = std::fabs(v[i]); if (a > peak) peak = a; }
    const double scale = (peak > kAffCap) ? (kAffCap / peak) : 1.0;
    return v[(int)f] * scale * g_affinityScale;
}

double TalentInFamily(uint64_t seed, double talent, CarFamily f) {
    double w = talent + AffinityFor(seed, f);
    return w < 0.0 ? 0.0 : (w > 1.0 ? 1.0 : w);
}

}   // namespace career
