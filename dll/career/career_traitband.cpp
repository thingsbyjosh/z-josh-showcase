// dll/career/career_traitband.cpp — see the full why and the comparison table in the header.
#include "career_traitband.h"

namespace career {
namespace traitband {
namespace {

// THIRD MEASUREMENT OF THE SAME DAY, and for the same reason all three times. p05/p50/p95 of THEM (324
// human cards) and of US, RE-MEASURED on 26/08 at 20:00 over the 101 classes of the batch generator
// (2,162 drivers), RAW before the band. The 17:10 measurement expired as soon as the trait started to be
// composed around its GRADE with the noise put into the gap: the two error knobs dropped their
// p05 from 0.23 to 0.14 and 0.07, and since the band SATURATES below our p05, 387 drivers (18 %)
// ended up written at the same exact 0.27. It's the defect of the 464 with 1.000 again, from the floor.
// THE RULE, no excuses now: **touching the generator FORCES re-measuring this column**. It isn't fine
// calibration, it's half of a rule of three; if one half moves and the other doesn't, the band piles people on an
// edge. The measurement harness is toolsuild_gen_all_ai.ps1 -> build\grids_all.csv (RAW columns).
// (historical) p05/p50/p95 of THEM and of US, measured on 26/08 at 17:10 over 2,742
// drivers, AFTER defending and aggression started to come from their grade and the level arc.
// The first version of this table was calibrated in the morning and expired the same day: in between came
// the combat ceiling and the grades, which moved our entire distribution. Applying it then widened
// TWICE (defending median at 0.94). If the generator changes shape again, this column gets
// RE-MEASURED again: it's half of the rule of three, not a constant.
// If the generator changes shape, the `ours` columns have to be RE-MEASURED: they're half of the
// rule of three. The test test_traitband_* leaves written down how to get them.
struct Band { double e05, e50, e95;   // them
              double n05, n50, n95; };// us
const Band kBands[kTraitCount] = {
    /* kAggression     */ {0.39, 0.56, 0.91,   0.22, 0.55, 0.91},
    /* kDefending      */ {0.22, 0.48, 0.90,   0.41, 0.64, 0.90},
    /* kStamina        */ {0.54, 0.74, 0.90,   0.56, 0.79, 0.93},
    /* kConsistency    */ {0.54, 0.74, 0.94,   0.40, 0.78, 0.96},
    /* kStartReactions */ {0.67, 0.81, 0.95,   0.66, 0.83, 0.96},
    /* kWetSkill       */ {0.44, 0.69, 0.93,   0.49, 0.73, 0.92},
    /* kTyreManagement */ {0.66, 0.79, 0.92,   0.55, 0.79, 0.92},
    /* kFuelManagement */ {0.40, 0.66, 0.92,   0.46, 0.71, 0.98},
    /* kBlueFlag       */ {0.86, 0.96, 0.98,   0.00, 0.73, 1.00},
    /* kWeatherTyre    */ {0.52, 0.68, 0.94,   0.51, 0.74, 0.93},
    /* kAvoidMistakes  */ {0.24, 0.53, 0.86,   0.14, 0.58, 0.82},
    /* kAvoidForced    */ {0.27, 0.48, 0.86,   0.07, 0.48, 0.79},
    // 26/08: qualifying comes in as a DRIVER knob. Our p95 is EXACTLY 1.00 — meaning the
    // top 5 % of the field writes precisely one of the two values AMS2 treats as a special case.
    /* kQualifying     */ {0.72, 0.86, 0.98,   0.73, 0.85, 0.93},
};

// The two extremes that are ALWAYS avoided. They're not design: the community reports that AMS2 treats the
// exact 0.0000 and 1.000 as special cases (0.00 errors can FLIP to the maximum). Not
// verified and falsifiable in a race; meanwhile, they aren't written.
const double kNever0 = 0.01, kNever1 = 0.99;

double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // anon namespace

void HumanAnchors(Trait t, double& p05, double& p50, double& p95) {
    if (t < 0 || t >= kTraitCount) { p05 = p50 = p95 = 0.5; return; }
    p05 = kBands[t].e05; p50 = kBands[t].e50; p95 = kBands[t].e95;
}

void OurAnchors(Trait t, double& p05, double& p50, double& p95) {
    if (t < 0 || t >= kTraitCount) { p05 = p50 = p95 = 0.5; return; }
    p05 = kBands[t].n05; p50 = kBands[t].n50; p95 = kBands[t].n95;
}

double ToHumanBand(Trait t, double ours) {
    if (t < 0 || t >= kTraitCount) return Clamp(ours, kNever0, kNever1);
    // 26/08 — `blue_flag_conceding` IS NOT MAPPED, and this is a correction of a diagnosis of mine.
    // The percentile comparison said we write from 0.00 to 1.00 where humans go from 0.86 to
    // 0.98, and I called it a defect. It's DELIBERATE: `BlueFlagForRole` (career_fields.cpp) returns 0.00 for the
    // SLOW class of a multiclass on the official guide's recommendation — a GT lapped by an LANTERN_MASTERS_7489
    // holds its line instead of moving aside constantly and ending up in the gravel. It's not the driver's character,
    // it's their ROLE in the race, and forcing it into the human band would break behavior already researched.
    // The lesson: the human band is EVIDENCE of the usable range, not a target to copy blindly.
    // Where we know what the knob does, what we know rules.
    if (t == kBlueFlag) return Clamp(ours, 0.0, 1.0);
    const Band& b = kBands[t];
    double v;
    if (ours <= b.n05) {
        // BELOW our p05 it isn't extrapolated: it saturates at theirs. Extrapolating here is what
        // would reproduce the broken tail this file comes to remove.
        v = b.e05;
    } else if (ours <= b.n50) {
        const double d = b.n50 - b.n05;
        v = b.e05 + (d > 0.0 ? (ours - b.n05) / d : 0.0) * (b.e50 - b.e05);
    } else if (ours <= b.n95) {
        const double d = b.n95 - b.n50;
        v = b.e50 + (d > 0.0 ? (ours - b.n50) / d : 0.0) * (b.e95 - b.e50);
    } else {
        // ABOVE our p95 a bit of headroom up to the ceiling is left: the exceptional driver
        // exists and is what makes an ace stand out. But bounded, not free.
        const double d = 1.0 - b.n95;
        const double f = (d > 0.0) ? (ours - b.n95) / d : 0.0;
        v = b.e95 + f * (kNever1 - b.e95);
    }
    return Clamp(v, kNever0, kNever1);
}

} // namespace traitband
} // namespace career
