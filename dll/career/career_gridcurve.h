#pragma once
// PURE math for the grid band. No ImGui, no game.
// Converts a per-class TARGET SPREAD (the last car's % off the leader's pace, from real data) into a
// non-linearly shaped race_skill band [floor, 1.0], by inverting the MEASURED TRANSFER (how much lap-time %
// each race_skill value costs at the high reference slider). See also career_classpace (the per-class
// spreads) and career_customai (BuildGridXml, which consumes this).
#include <vector>
#include <algorithm>
#include <cmath>

namespace career {

// ===================================================================================================
// 24/08 — WHAT RULES HERE, AFTER THE CLEANUP (task 5 of the plan "the gap")
// ===================================================================================================
//
// Five knobs were fighting over the same number (the per-class talent yardstick, the global width, the band of
// Curve, its per-class trimming and the floor distribution). TWO things and a dial remain:
//
//   · the CLASS DATUM — `spread_pct_dry` and `median_pct_dry` from `class_pace_curve.psv`, with the
//     car's part already discounted by the caller. They rule again on the width and on the height of the knee;
//   · the INSTRUMENT — `kSkillTransfer` and its two inverses, `PctOffForRaceSkill` and `FloorForSpread`. It is the
//     ONLY thing measured inside the game, and that's why it is the only thing that translates between lap % and `race_skill`;
//   · the DIAL — `kHeadOpen`, which moves the knee's height and only that;
//   · and `kMinDriveableSkill` as a NET: the lower edge of what was measured, not a calibration. With the
//     three-pin conversion nobody touches it, and there is a test that pins it.
//
// Everything else left in this file (`GridShape` and its family) is OUTSIDE the production path and
// lives on for the suite's substitution demonstrations; it is marked where it applies.

// race_skill -> % lap-time lost vs race_skill 1.0. MEASURED IN-GAME on Jul 26, 2026 (the first
// calibration pass); full source and method in docs/career/skill_transfer_measured.psv.
// METHOD (one line): 18 Juniper_Cup cars at Coldvale Park GP, a 30-minute practice session, a CONTROL grid
// (every parameter identical except race_skill, stepped from 1.00 to 0.30); reference SK100 = 83.124s,
// pct_off = (t - t_ref)/t_ref*100; the control replicate at 0.30 was repeatable within 0.12s.
// CAVEAT: the transfer SCALES with the global difficulty slider. This curve was measured at the default slider
// and is representative of ~100-110 (the [0.85,1.00] band gives ~2.2% here, 2.6% at slider 100, and 1.7% at 120). For
// sliders far from that, it needs rescaling or remeasuring; the measurement harness already captures this on its own (laplog + autodump).
// Replaces the EXTRAPOLATED table from Jul 25, which was 2.4x to 10x too flat below 0.90 and was the
// reason the grid band was coming out ~1.7x wider than it should.
// The real shape: it's NOT flat at the top — already 1.5% is lost from 1.00 to 0.90; the slope grows further down.
// Known noise: the points at 0.72 (4.594) and 0.68 (4.639) nearly tie; that's session-to-session scatter, not a
// real step. They're kept as measured (monotonic data); the effect is that the INVERSE is very sensitive in
// that 0.045pp gap. If it ever becomes a problem, re-measure that stretch — don't paper over it.
struct SkillTransfer { double race_skill; double pct_off; };
constexpr SkillTransfer kSkillTransfer[] = {
    {1.00,  0.000}, {0.96,  0.669}, {0.92,  1.061}, {0.88,  1.872}, {0.84,  2.329},
    {0.80,  2.834}, {0.76,  3.413}, {0.72,  4.594}, {0.68,  4.639}, {0.64,  5.392},
    {0.60,  5.867}, {0.56,  6.612}, {0.52,  7.526}, {0.48,  8.375}, {0.40, 10.534},
    {0.33, 12.391}, {0.30, 13.283},
};
constexpr int kSkillTransferN = (int)(sizeof(kSkillTransfer)/sizeof(kSkillTransfer[0]));
// ~~~09/09 (, the coupling with the slider): THE TRANSFER SCALES WITH THE SLIDER, and until today nobody
//  discounted it: the class delivered its fan only at the slider at which the table was measured, and as soon as the adaptive
//  difficulty moved the category's number the width got distorted without anyone touching the conversion (the
//  I1 invariant of the 17/08 spec failed on its own). What was measured: the band [0.85, 1.00] gives 2.6 % of lap at slider
//  100, 1.7 % at 120 and 2.2 % in the session in which the table was measured. With those three points, a line:
//      pct(s) = 2.6 - 0.045 x (s - 100)      (the table's session falls at s ~ 109)
//  and the factor with respect to the table is pct(s) / 2.2. With unknown slider (0) the factor is 1 = the table as
//  is, which is what there was. Outside 100-120 it is linear extrapolation, bounded to [0.5, 2.0]; when the game
//  can be opened it is re-measured per segment with the harness (laplog + autodump) and this line is replaced by the table.
constexpr double kTransferPctAt100    = 2.6;
constexpr double kTransferPctPerPoint = 0.045;
constexpr double kTransferPctRef      = 2.2;
inline double TransferScaleForSlider(int aiPct) {
    if (aiPct <= 0) return 1.0;
    const double pct = kTransferPctAt100 - kTransferPctPerPoint * (double)(aiPct - 100);
    const double k = pct / kTransferPctRef;
    return k < 0.5 ? 0.5 : (k > 2.0 ? 2.0 : k);
}

inline double GcClamp(double v, double lo, double hi){ return v<lo?lo:(v>hi?hi:v); }

// % of lap time lost for a given race_skill (interpolates the table; out of range -> clamps to the extreme).
// `scale` (09/09, ) = TransferScaleForSlider of the effective slider; 1 = the table as is.
inline double PctOffForRaceSkill(double raceSkill, double scale = 1.0) {
    if (raceSkill >= kSkillTransfer[0].race_skill) return kSkillTransfer[0].pct_off * scale;   // >=1.0 -> 0
    const SkillTransfer& last = kSkillTransfer[kSkillTransferN-1];
    if (raceSkill <= last.race_skill) return last.pct_off * scale;                             // below the table -> clamp to the extreme
    for (int i = 0; i < kSkillTransferN-1; i++) {
        const SkillTransfer& a = kSkillTransfer[i];      // race_skill DECREASES as we go
        const SkillTransfer& b = kSkillTransfer[i+1];
        if (raceSkill <= a.race_skill && raceSkill >= b.race_skill) {
            double t = (a.race_skill - raceSkill) / (a.race_skill - b.race_skill);
            return (a.pct_off + t * (b.pct_off - a.pct_off)) * scale;
        }
    }
    return last.pct_off * scale;
}

// The race_skill floor whose % lap-time loss equals spreadPct (inverts PctOffForRaceSkill). spreadPct<=0 -> 1.0
// (everyone equal); above the table's maximum -> the table's minimum floor (clamp).
//
// HERE IS THE ROOT OF THE GRID KNOT, TRACED IN FULL ON 12/08. Five reverted attempts blamed the
// yardstick, the affinity and the seeding; measured, all of them only shuffle people INSIDE the gap that this
// function leaves. The chain, end to end:
//
//   1. `spread_pct_dry` (class_pace_curve.psv) is real LAP TIME %. Median 3.9 across 100 classes.
//   2. -> `FloorForSpread(3.9)` comes out ~0.74 (the table gives 3.413 -> 0.76 and 4.594 -> 0.72).
//   3. -> the whole grid ends up confined to [0.74 · 1.00]: 0.26 of travel. Measured: 0.214.
//   4. -> 64% of drivers fall in Curve's T1 segment (0.86-0.99) and NOBODY below T2.
//
// And that's why the three knobs tried cancel each other out:
//   · widening the yardstick (`GRID_SPREAD_SCALE`) ×2.9 -> from 33% to 36% of Curve: pushes the tail against the floor
//   · distributing the floor (`EsparceElSuelo`) -> undoes the stacking but sends the tail back upward
//   · lowering the knee (`kHeadOpen` 1.2 -> 3.0) -> T1 drops from 64% to 39%, but people only get as far as T2
//
// WHAT IS MISSING IS NOT A CONSTANT, IT'S A DATA DECISION. To populate Curve's T3-T5 (0.35-0.67) the
// floor would have to be at ~0.35, meaning the class would have to declare a fan of ~10.5% instead of
// 3.9%. The user already decided what tips this (12/08): **the XMLs rule**. That is, the XML's band must NOT
// come out of the category's REAL TIMES fan — they are two different magnitudes, and confusing them is
// what has kept five attempts costing.
//
// The next step is to separate them in the data: what fan the race really has (for whatever needs
// realism) and what band is written to AMS2 (so that the grid behaves). This function is NOT touched
// until then.
inline double FloorForSpread(double spreadPct, double scale = 1.0) {
    if (spreadPct <= 0.0) return 1.0;
    if (scale > 0.0 && scale != 1.0) spreadPct /= scale;   // 09/09 (): at this slider each point buys `scale` times the table
    const SkillTransfer& last = kSkillTransfer[kSkillTransferN-1];
    if (spreadPct >= last.pct_off) return last.race_skill;
    for (int i = 0; i < kSkillTransferN-1; i++) {
        const SkillTransfer& a = kSkillTransfer[i];      // pct_off INCREASES as we go
        const SkillTransfer& b = kSkillTransfer[i+1];
        if (spreadPct >= a.pct_off && spreadPct <= b.pct_off) {
            double t = (spreadPct - a.pct_off) / (b.pct_off - a.pct_off);
            return a.race_skill + t * (b.race_skill - a.race_skill);
        }
    }
    return last.race_skill;
}

// race_skill value for a grid position (rankFrac 0=front .. 1=back), interpolated between 1.0 and
// floor using POWER spacing. gamma=1 = linear; gamma>1 bunches up the front (the gradient shifts to the back).
//
// 24/08 — THIS ONE AND `GammaForMedian` ARE STILL IN PRODUCTION, and it is worth knowing where. They are the
// branch WITHOUT TALENT of `BuildGridXml`, which the plan keeps on purpose: the suite's fixtures and old calls
// compose grids without talent, and so does the overlay's tribute fallback. There are no gaps to
// measure there, so the class's fan is distributed by RANK — which is correct when there's nothing else — and
// `GammaForMedian` places the knee where `median_pct_dry` says. Not to be confused with Curve's distribution by
// rank, which has been retired: that one replaced talent while having it; this one only acts when there is NONE.
// `KClassForSpread` and `ShapeFromKClass`, on the other hand, are no longer called by production: they live in the suite as
// a reference for the affine conversion (see the note on `RaceSkillShaped`).
inline double RaceSkillForRankFrac(double rankFrac, double floor, double gamma) {
    double f = GcClamp(rankFrac, 0.0, 1.0);
    double s = std::pow(f, gamma <= 0.0 ? 1.0 : gamma);   // 0..1
    double v = 1.0 - s * (1.0 - floor);
    return GcClamp(v, floor, 1.0);
}

// The gamma exponent such that the MEDIAN car (rankFrac 0.5) loses medianPct, given the total spread (which sets the
// floor). Binary search over gamma in [0.2, 6.0]; if medianPct falls outside what's reachable, it saturates to the extreme.
inline double GammaForMedian(double spreadPct, double medianPct, double scale = 1.0) {
    double floor = FloorForSpread(spreadPct, scale);
    if (floor >= 1.0) return 1.0;
    double lo = 0.2, hi = 6.0;
    for (int it = 0; it < 40; it++) {
        double g = 0.5*(lo+hi);
        double rs = RaceSkillForRankFrac(0.5, floor, g);
        double pct = PctOffForRaceSkill(rs, scale);
        // higher gamma -> the median gets a HIGHER race_skill -> loses LESS. We want pct == medianPct.
        if (pct > medianPct) lo = g; else hi = g;
    }
    return 0.5*(lo+hi);
}

// ACE-ANCHORED conversion ----------------------------------------
// Replaces the range-based remap, which was the ROOT DEFECT: there, race_skill was just a driver's POSITION in their
// class (rankFrac), and talent only sorted them before being discarded, so two different drivers in the same
// slot in two different classes got the SAME number — interchangeable by construction. Here the class's ace occupies
// 1.00 (like real driver packs, which always have a single ace at the top), and each driver drops by THEIR OWN talent
// gap. The intended consequence: a weak driver in a strong class gets a genuinely low number (the long tail
// seen in real driver packs) instead of "last place = floor".
// RE-ANCHORED on Jul 26 with the MEASURED table (previously 0.30, inherited from the extrapolated one). The conversion is AFFINE
// (race_skill = 1 - gap*k, with k = (1-floor)/S), so SD(final) = k * SD(talent), and S is the only thing that sets
// the gain. All the arithmetic below is measured, nothing eyeballed:
//   · TALENT's SD within a grid = 0.0812 (median across the ladder's 101 classes, measured with
//     tools/gen_all_ai over 2,450 drivers; matches the 0.72..1.00 reference band = 0.28/√12 = 0.0808,
//     which career_traits.cpp uses for standardization).
//   · target measured across 45 real grids: SD 0.078 (= kSdRace) -> k* = 0.078 / 0.0812 = 0.960. In other words:
//     the conversion must NOT inflate, only carry the talent gap over to the class's scale.
//   · the ladder's MEDIAN class: spread 3.80% -> FloorForSpread = 0.747 -> 1 - floor = 0.253.
//   · S = 0.253 / 0.960 = 0.2635  ->  0.26.
// Verified against the generated population: with 0.26, the median per-class SD comes out 0.0780 (target 0.078), and the p10
// median comes out 0.760 (target 0.75). With the previous 0.30 it came out 0.0676 and 0.792 — the conversion was SHRINKING
// the real shape by 13%, and on top of that, with the extrapolated table k was 1.7x too high: it was inflating.
// KNOWN TENSION, read this before touching this number: the two measured targets are NOT the same target.
//   · the SD of 0.078 comes from 45 REAL driver packs that don't distinguish by class (they all write the same 1.00..0.72 band,
//     which, with the measured table, equates to a 4.59% spread);
//   · the per-class spread comes from real classification sheets, and the ladder's MEDIAN class shows 3.80%.
// Matching the SD therefore widens every grid ~1.2x relative to its class's REAL spread. The
// alternative criterion (that the generated grid loses EXACTLY its class's measured spread) calls for S = 0.298, which
// is the typical measured ace-to-tail-lighter talent band (0.298) — i.e., keeping the old 0.30. The two options'
// numbers: 0.26 -> SD 0.0780 / p10 0.760 / median 0.844; 0.30 -> SD 0.0676 / p10 0.792 / median 0.865.
// 0.26 is chosen because it nails 2 of the 3 real-grid targets (SD and p10). If, when tested in-game, grids
// come out too wide, THIS is the number to move, and the way back is 0.30, not the table.
// AMENDMENT Jul 26 (shape curve, below): the MEDIAN no longer depends on this number — it's set by the
// median's elbow — so this one is back to just its original job, the scale. With the shape curve in place, 0.26 gives
// SD 0.0796 / p10 0.773 / median 0.890 / 44 drivers on the floor. The way back stays open, and it was
// re-measured with the new model: 0.30 (in which case kTalentRefMedianGap becomes 0.553, because the measured
// median gap is 0.166 of talent and has to be expressed in the new units) gives SD 0.0662 / p10 0.802 /
// median 0.895 / 19 on the floor. In other words: 0.30 buys grid depth and costs SD and p10, same as before.
// RESOLVED Jul 27 — taking the way back this comment left open: 0.26 -> 0.30. This is no longer
// a preference between two targets, because the loop has been CLOSED COLD and gives a
// number (test_class_spread_delivered_vs_target: 100 classes, PSV target -> band -> XML -> transfer ->
// % delivered). Measured across real grids, with the authored real-driver profiles integrated:
//        span | delivered/target p50 | within ±20% |     SD (target 0.078) | p10 (target 0.75)
//        0.26 |                   1.15 |            55% |  0.0853  (+0.0073)   |  0.780
//        0.30 |                   1.00 |            78% |  0.0713  (-0.0067)   |  0.810
// The Jul 26 argument for 0.26 was that it nailed SD and p10. **With the authored profiles integrated, it no longer nails the SD**: the
// authoring of the 1,634 drivers shifted the talent distribution, and the SD moved to 0.0853. Today 0.30 comes out
// even a bit CLOSER to that target (0.0067 vs 0.0073), and it also nails the class spread and raises the
// hit rate from 55% to 78%. It wins 2 of 3 targets; the one it gives up (p10) is the least central. On top of that, the project's
// rule applies: when in doubt, match real life — and the spread comes from real classification sheets, while
// the SD comes from 45 driver packs that write the SAME band for every class.
// Jul 28 — THIS NUMBER IS NO LONGER THE YARDSTICK FOR EVERY CLASS, it's just the DEFAULT. Each class carries its own in
// column 7 of docs/career/class_pace_curve.psv, and it travels inside GridShape.
//
// Why it changed. The comment below assumed that classes falling short (MERROWDOWN_MYR373 at 55% of its
// target, Ivory_Cup_10072 at 65%) would be fixed "in those classes' authoring". When we went to do that, we measured what
// that meant, and it's NOT POSSIBLE: talent is ABSOLUTE and global per driver, but drivers race in
// several classes at once — Ivory_Cup_10072 shares all 24, Falcon_Series 22 of 24, Beacon_Challenge 19 of 22. Rubens
// Barrichello is in Beacon_Challenge (real spread 2.6%) and Falcon_Series (6.0%): there's no single talent number
// that works for both. Re-authoring one class would break the others its drivers also race in.
//
// The way out is what the non-negotiable property was already hinting at: the yardstick is a CLASS CONSTANT, like the
// floor and the median. Nothing changes within a class — same talent gap, same skill gap, and neither the
// grid position nor the grid's makeup factor into it. What changes is that the same talent
// distance is worth something different in a one-make series than in a 1998 F1, which is exactly what the real
// classification sheets the spread comes from say.
//
// Talent still governs the ORDER and the RELATIVE gaps; the class's spread governs the
// WIDTH. Before, the two were fighting over the same number.
constexpr double kTalentRefSpan = 0.30;   // DEFAULT yardstick (a class with no PSV data)

// Converts a talent gap into a race_skill gap using the class's MEASURED spread: the class's floor
// (what the tail-ender loses) is spread across kTalentRefSpan of talent.
// Yesterday's KNOWN LIMITATION (the table saturated at 5.5% -> 0.30, so two different spreads gave the SAME k)
// disappeared with the new measurement: the table now reaches 13.28%, above the catalog's widest spread
// (11.0%), so today every class gets its own k. Saturation only kicks in above 13.28%.
inline double KClassForSpread(double spreadPct) {
    double floor = FloorForSpread(spreadPct);
    return (1.0 - floor) / kTalentRefSpan;
}

// HARD FLOOR, backed by data (Jul 26). Replaces the provisional patch from Jul 25 (0.55), which had been put in
// "until the first measurement pass" and had 671 drivers (27.4% of the generated grid) pinned EXACTLY at the
// same number, all lapping at the same pace: a cloned pack, exactly what this work is fighting against.
// It's measured now, so the value comes from the measurement, not an invented margin: in that pass, the two cars at
// race_skill 0.30 ran 7 laps in 94.164 and 94.040s (13.3% off, repeatable within 0.12s) — 0.30
// DRIVES. Below 0.30 there's no measurement at all, and at 0.00 the AI shuts down (43 cars stopped dead on Jul 25),
// so 0.30 stands as the SAFETY CAP at the edge of what's measured, not as a calibration choice.
// It affects 1.8% of the generated grid (44 of 2,450) versus yesterday's 27.4%, and that remainder concentrates
// in 8 historic formula classes (Ivory_Series, DRIFT MASTERS 1585, Onyx_Masters/2, Falcon_Series, Aurora_Masters,
// Ivory_Trophy/Gen4). There, the problem is NOT the cap: it's that those classes DOUBLE UP their disparity — their authored
// talent band is already wide (SD 0.12-0.15 versus the typical 0.081), and on top of that their measured spread is among the
// widest, so the product explodes.
// The Jul 26 shape curve (below) raises their median by 0.04 to 0.13 (Falcon Cup 0.73->0.86,
// Falcon_Masters 0.75->0.87, Lantern_Trophy 0.74->0.85) WITHOUT moving this count: it stays at 44, exactly the
// same ones. What's left below is no longer a conversion problem — it's the AUTHORED TALENT BAND of
// those classes; lowering this number fixes nothing, and there's no measurement below 0.30.
constexpr double kMinDriveableSkill = 0.30;

// SHAPE: the anchored conversion stops being AFFINE (Jul 26) --------------------------------------
// What was missing: `RaceSkillAnchored` was purely affine (1 - gap*k), so it PRESERVED the shape of the
// talent distribution and only changed the scale. Measured consequence across the 2,450 generated drivers:
// SD and p10 nailed the target (0.078 / 0.760), but the MEDIAN sat at 0.845 versus the 0.90 of real
// driver packs, because nothing anchored the middle of the pack. That's why `median_pct_dry` from class_pace_curve
// — the second data point for each class — went unused in this branch: there was nowhere to put it.
// Now the band has SEGMENTS, one per PSV data point:
//   · head (gap 0 .. kTalentRefMedianGap): from the ace (1.00) to the class's MEDIAN,
//   · tail   (.. 1 reference span):        from the median to LAST,
//   · outside the measured ENVELOPE (gap > 1 span): the usual AFFINE rate, because out there the class's data
//     says nothing: it describes pole to the tail-ender, no further.
// Both endpoints come from inverting the SAME measured transfer: the median loses median_pct_dry, and
// last place loses spread_pct_dry. It's what `GammaForMedian` used to do in the range-based branch, but applied to the
// TALENT GAP instead of the grid position.
// The third segment is NOT decoration: it's what stops the shape curve from inventing the back of the grid.
// Extending the tail (which in some classes runs 3-4x steeper than the head) out to talent gaps
// of 2 spans, the drivers hitting the 0.30 safety cap went from 44 to 61, and the classes with someone below
// 0.40 went from 16 to 22, almost all historic formulas. Cutting it off at the envelope's edge keeps it at 44 and 16 —
// EXACTLY what it was before this change — without losing anything on the median. In other words: the shape curve only
// touches whoever falls INSIDE the class's measured spread; outside it, the conversion is the same as always, bit for bit.
// THE NON-NEGOTIABLE PROPERTY STILL HOLDS: both segments are a function of the driver's gap and two
// CLASS constants (from the PSV). Neither grid position nor the grid's makeup factor into it, so
// the same talent gap -> the same skill gap, exactly as before.
// Why it's NOT a power function over the gap (which was the obvious path): tried it, and it breaks the shape. A
// power function u^gamma has ZERO slope at the ace, and lifting the median to 0.90 needs gamma≈2.0; with
// that, the ace->2nd gap (measured median: 0.0137 of talent) turns into 0.0007 of skill — essentially NOTHING at two
// decimal places. Measured across all 101 classes: the ones that lose their standalone ace go from 20 to 71, and the drivers on the
// floor go from 44 to 91. With STRAIGHT segments the slope at the ace is finite and that shape holds up: 29 classes with a
// duplicated ace, and the same 44 drivers on the floor.
// THE DEBT THIS CHANGE LEAVES, in numbers: those 29 used to be 20. Of the 29, thirteen are an EXACT talent
// TIE (the cast cycles and repeats a driver: two identical numbers, no conversion can separate them), and the nine
// that get lost again have ace->2nd gaps of 0.006 to 0.018 of talent, i.e. between 0.08 and 0.22
// standard deviations of their class. It's the price of anchoring the median: the top half of the pack gets
// squeezed (there's only 0.10 of skill from ace to median in a typical class), and at the XML's two decimal places that ties
// drivers who were already nearly tied. It's NOT fixed here: it gets fixed by making each class's ace STAND OUT in the
// talent model, which is where the real gap lives.
// Jul 27: 0.638 -> 0.553 because it's expressed IN UNITS of kTalentRefSpan, and that went from 0.26 to 0.30. The
// MEASURED median gap doesn't change (0.166 of talent); only the yardstick it's expressed in changes: 0.166/0.30 =
// 0.553, just like 0.166/0.26 gave 0.638. Moving one without the other shifts the elbow and warps the band.
constexpr double kTalentRefMedianGap = 0.553;   // the MEDIAN's gap, in units of kTalentRefSpan
// MEASURED the same way as kTalentRefSpan, over the same population: median across the ladder's 101 classes of
// (ace_talent - median_talent)/kTalentRefSpan = 0.638 (p25 0.544, p75 0.739). In those same units
// the gap to LAST measures 1.145 -- meaning the real ace-to-tail-ender band (0.298) that kTalentRefSpan compresses to
// 0.26 on purpose, to nail the SD of the 45 driver packs. It's NOT 0.5: the talent distribution within a
// class isn't symmetric around the ace, which is an extreme value (the max of N), not a midpoint.

// A class's race_skill band: the two points that fix its shape. Both come from inverting the
// measured transfer, so both speak the same language (% lap time lost -> race_skill).
struct GridShape {
    double floor  = 1.0;   // LAST place's skill   (gap = 1 reference span)  <- spread_pct_dry
    double median = 1.0;   // MEDIAN's skill  (gap = kTalentRefMedianGap)   <- median_pct_dry
    // Jul 28 — the yardstick used to measure the talent gap, NOW PER CLASS (it used to be the global 0.30).
    // See the long block below, at kTalentRefSpan.
    double ref_span = kTalentRefSpan;
};

// The class band from its two PSV data points. medianPct<=0 or inconsistent (median slower than
// last place) gets clamped to the floor: the shape degenerates to a single segment, never to absurd values.
// refSpan: THIS class's talent yardstick (docs/career/class_pace_curve.psv col 7). Omitting it falls back to the
// global one, so callers that don't know about it behave exactly as before.
// 19/08 — THE PLAYABILITY DIAL, AND ONLY THE KNEE (spec 2026-08-17 ).
//
// What it is. `kHeadOpen` multiplies the MIDDLE pin of `PinsForGrid` — the lap % that the grid's median
// loses — and nothing else. It raises the drop that the top half consumes WITHOUT touching the total width: the ace
// stays at 1.00 and the backmarker stays exactly at its class's fan. It's the answer to the 09/08 on-track
// feedback ("the first 10 are too close together, within 3 seconds"), which pointed at the
// HEAD and not at the width, and it's the only magnitude of the new conversion that isn't tautological by
// construction — see of the spec.
//
// WHY IT STARTS AT 1.0, AND IT'S NOT CONSERVATISM. The same day the delivered width goes from Curve's uniform
// 11.3-11.6% to the ~3% that each class's datum says, also opening the head would make
// it impossible to know which of the two changes produced what is seen on track. One change, one measurement. 1.0 is
// also THE REALITY: it reproduces exactly `median_pct_dry`, which is what the class declares (invariant I9).
//
// AND LOWERING IT FROM 1.80 WAS NOT A REGRESSION OF ANYTHING, which is what made that step cheap. Between 16/08 and
// 19/08 production distributed by RANK, and that `RaceSkillByRank` began with `(void)sh;`: it discarded the
// whole `GridShape`, knee included. That is, the 1.80 that was on this line had been a month WITHOUT TOUCHING the
// XML's `race_skill` — it only grazed `qualifying`, through the patch that restitched the two paths. Today the knee
// rules for real again, so this number DOES move the grid: moving it calls for a measurement.
//
// THE KNOWN USEFUL MARGIN IS 1.0-1.3. At 1.40 it was measured to INVERT the contract of F3's authored talent
// (the gaps came out more regular WITH authoring than without it: irregularity 0.909 -> 0.901, and F3 went up in 34
// series and down in 48, against 43/39 at knee 1.00) and the test knocked it down. The crossover falls near 1.30.
//
// AND THE HONEST NUMBER IS PENDING AN IN-GAME MEASUREMENT, which is the only piece of this plan that asks for
// a user session with AMS2. The starting datum is FASTEST-LAP fans and what the player
// judges are FINISH GAPS; in between there's slipstream, traffic and AI trains that nobody has measured. Until
// then this is a dial inherited from three mods, not a measured number, and it is 1.0.
//
// The cap at 95% of the fan that `PinsForGrid` applies over the product isn't decorative: with `kHeadOpen`
// at 1.80 it bit in 6 of the 109 classes (the knee ate the whole tail segment and left it flat); at 1.0
// it bites in none. Lowering the dial isn't just harmless: it removes a clip that acts today.
constexpr double kHeadOpen = 1.0;

inline GridShape ShapeForClass(double spreadPct, double medianPct, double refSpan = kTalentRefSpan) {
    GridShape sh;
    sh.floor  = FloorForSpread(spreadPct);
    sh.median = FloorForSpread(medianPct);
    if (sh.median < sh.floor) sh.median = sh.floor;
    if (sh.median > 1.0)      sh.median = 1.0;
    // Sanity guard: an absurd yardstick (0 or negative from a malformed PSV) would blow up the
    // gap division. It falls back to the global one, which is the usual behavior.
    sh.ref_span = (refSpan > 0.05 && refSpan < 2.0) ? refSpan : kTalentRefSpan;
    return sh;
}

// The AFFINE band equivalent to a kClass: the median falls EXACTLY on the ace->last straight line. With it,
// RaceSkillShaped returns exactly 1 - gap*kClass, which is the conversion used before Jul 26. It's the
// "gamma = 1" of this model, and it's not a numerical coincidence: it's proven by substitution (see the
// test_shape_reduces_to_affine, which checks it across both segments and at the elbow).
inline GridShape ShapeFromKClass(double kClass) {
    GridShape sh;
    sh.floor  = 1.0 - kClass * kTalentRefSpan;
    sh.median = 1.0 - kClass * kTalentRefSpan * kTalentRefMedianGap;
    return sh;
}

// How much steeper the tail runs than the head. 1.0 = both slopes equal = the usual AFFINE
// conversion. >1 = tight head and long tail (the shape of a real driver pack). <1 = the opposite, and it exists:
// classes whose data says the pack opens up at the front and bunches up at the back (CINDER TROPHY: median 6.5% of 11.0%).
// Diagnostics and tests only; the conversion doesn't need it.
inline double ShapeGamma(const GridShape& sh) {
    double head = (1.0 - sh.median) / kTalentRefMedianGap;
    double tail = (sh.median - sh.floor) / (1.0 - kTalentRefMedianGap);
    if (head <= 1e-12) return tail <= 1e-12 ? 1.0 : 6.0;
    return tail / head;
}

// A driver's race_skill, measured downward from the ace of THEIR grid, via the two-segment band. It doesn't
// depend on grid position or how many drivers there are: same talent gap -> same skill gap, in any class.
//
// 24/08 — OUTSIDE THE PRODUCTION PATH, AND KEPT ON PURPOSE. Since the wiring of the three-pin
// conversion, the XML's `race_skill` does NOT go through here: `BuildGridXml` calls `RaceSkillOnPins`. This
// function — and with it `GridShape`, `ShapeForClass`, `ShapeFromKClass`, `ShapeGamma`, `RaceSkillAnchored`,
// `kTalentRefSpan` and `kTalentRefMedianGap` — survives because it's the suite's SUBSTITUTION REFERENCE:
// a good dozen tests demonstrate against it the underlying property ("same talent gap -> same
// race_skill") and the identity with the affine conversion. Deleting it would delete the demonstrations, not the control.
// If an XML writer ever calls it again, it's a bug: the grid width control today is
// the CLASS's fan (`spread_pct_dry`), and the extremes are set by the live grid.
inline double RaceSkillShaped(double wDriver, double wAce, const GridShape& sh) {
    double u = (wAce - wDriver) / sh.ref_span;       // normalized gap; the ace sits at 0, last place at 1
    if (u <= 0.0) return 1.0;                        // above the ace (happens in qualifying) -> ceiling
    double v;
    if (u <= kTalentRefMedianGap)                    // head: from the ace to the class's median
        v = 1.0 - (1.0 - sh.median) * (u / kTalentRefMedianGap);
    else if (u <= 1.0)                               // tail: from the median to last
        v = sh.median - (sh.median - sh.floor) * ((u - kTalentRefMedianGap) / (1.0 - kTalentRefMedianGap));
    else                                             // outside the measured envelope: the usual AFFINE rate, no invented shape
        v = sh.floor - (1.0 - sh.floor) * (u - 1.0);
    if (v < kMinDriveableSkill) v = kMinDriveableSkill;   // safety cap at the edge of what's measured (see above)
    return v > 1.0 ? 1.0 : v;
}

// The AFFINE conversion (the one from before Jul 26). Kept because it's the "gamma = 1" case of the new model and
// because the test suite uses it as the reference for the non-negotiable property. It delegates, so the two
// can't diverge: any fix in RaceSkillShaped is inherited automatically.
inline double RaceSkillAnchored(double wDriver, double wAce, double kClass) {
    return RaceSkillShaped(wDriver, wAce, ShapeFromKClass(kClass));
}

// =====================================================================================================
// 19/08 — THE THREE-PIN CONVERSION (spec 2026-08-17 /, of canonical )
// =====================================================================================================
//
// What it fixes. Until 19/08 the XML's `race_skill` was produced by `RaceSkillByRank`, which distributed by RANK
// within Curve's five bands and began literally with `(void)sh;`: it discarded the whole `GridShape`.
// That's why the 109 classes delivered THE SAME THING (11.3%-11.6% of lap for the backmarker, the same for a rental kart
// as for an F1). This reads each class's datum again. THIS is the production path, and it is already
// the only one: the distribution by rank was retired entirely on 24/08 (task 5 of the "the gap" plan).
//
// How. Three pins and two straight segments between them:
//   · the ACE and the BACKMARKER come from the GRID BEING WRITTEN — the live yardstick. The ace goes to 1.00 and
//     the backmarker to its class's DRIVER FAN, so the delivered fan is its own by
//     construction and doesn't depend on a global yardstick being in sync with anything;
//   · the MEDIAN comes from the CLASS DATUM, its DRIVER part too, with the playability dial
//     `kHeadOpen` already inside. It sets the knee's HEIGHT.
// The lap % is translated to `race_skill` by inverting `kSkillTransfer` (via `FloorForSpread`), which is the
// ONLY instrument measured inside the game. There is no other scale in between.
//
// WHAT COMES IN HERE IS THE DRIVER'S PART, NOT THE PSV'S RAW COLUMN. `spread_pct_dry` and
// `median_pct_dry` from `class_pace_curve.psv` are the class's TOTAL fan: driver + car. The two
// arguments of `PinsForGrid` are what remains AFTER discounting the car, and the caller is the one who
// discounts. of the spec says why it matters so much: with the live yardstick almost everything is satisfied by
// construction, and that discount is the table's ONLY criterion that is NOT tautological. Passing the total
// delivers an inflated fan and the harness doesn't see it, because the extreme still falls where it's told.
//
// And the knee has two coordinates, which come from different places: the ABSCISSA is set by `w_med` — the median
// talent of the live grid, i.e. the distribution of people that weekend — and the HEIGHT is set by
// `median_pct`, which is class data. Moving the dial moves the height; changing who races moves the abscissa.
//
// WHY TWO STRAIGHT SEGMENTS AND NOT A POWER. It was tried, and it breaks the silhouette at the top: a power
// `u^gamma` has ZERO slope at the ace, and with the gamma needed to raise the median the ace->2nd gap
// measured in the ladder (0.0137 of talent) became 0.0007 of skill — i.e. NOTHING at the two
// decimals that the XML writes. Measured at the time over the 101 classes: those that came out with the ace DUPLICATED
// went from 20 to 71. With straight segments the slope at the ace is finite and that silhouette holds.
//
// WHY THE MIDDLE PIN IS THE MEDIAN AND NOT A FIXED INDEX. The ace and the backmarker are the two
// MOST VOLATILE points of a sample (they are extremes: max and min of N, not robust statistics). Anchoring the
// top half on the MEDIAN, an anomalous backmarker — a disastrous pay-driver who lowers the minimum all by himself —
// only compresses the BOTTOM half: the head of the grid, which is what the player looks at, doesn't notice.
// With a fixed index (the 5th, the 10th...) the reference would move with the grid's size and the same
// driver would receive different numbers depending on how many race that weekend.
//
// AND WHAT WAS RETIRED WITH CURVE, SO THAT NOBODY LOOKS FOR IT: `RaceSkillByRank` and its switch
// `g_curveByRank`, `ShapeCurve` with its eight `kCurve*` constants, `kClassFloorRef`, `EsparceElSuelo` with its
// `kFloorSpreadBand`, and `kAceLeadMax` (measured on 09/08 and NEVER applied: bounding the ace's lead moved the
// reference that everyone shares). Curve's shape wasn't the problem — a smooth per-class curve reproduces
// its distribution within three hundredths at the three quartiles (spec ) —; what was excessive was its
// WIDTH, 1.7x what the category really delivers. So there was nothing left to rescue from the mechanism:
// what was worth anything is already in here.
// PLACEMENT NOTE: it goes here and not next to `FloorForSpread` — which is the inverse it uses — because it needs
// `kMinDriveableSkill` and `kHeadOpen`, both declared FURTHER DOWN than that one. C++ has no forward
// declaration for constexpr, so the order rules.

// The three pins of the grid being written. The two at the extremes belong to the GRID (the
// live yardstick); the middle one belongs to the CLASS, with the playability dial already inside.
//
// WHY `no_data` IS BORN `true`. The struct's other defaults (the three talents at 1.0, the two
// percentages at 0) are the ONLY configuration that the conversion doesn't know how to handle: without `no_data`, a
// freshly declared `GridPins` falls into the tail branch, divides by `(w_med - w_last) = 0`, comes out NaN, and
// `FloorForSpread(NaN)` slips through the end of the table down to `kMinDriveableSkill` = 0.30. It neither blows up
// nor warns: it writes the entire grid stacked on the floor, which is exactly the defect this spec
// exists to kill, and looking like a legitimate result. Being born `true`, an unfilled pins delivers
// 1.0 to everyone — the behavior of a class with no row in the PSV, which is the safe one.
// It matters for the wiring: the natural pattern is to declare `GridPins pins;` above `BuildGridXml` and
// fill it inside the `if (haveTalent)`, whereupon the branch WITHOUT talent — which the plan keeps on
// purpose — would get the empty pins.
struct GridPins {
    double w_ace  = 1.0;      // talent of the best of THIS grid
    double w_med  = 1.0;      // MEDIAN talent of THIS grid
    double w_last = 1.0;      // talent of the backmarker
    double spread_pct = 0.0;  // backmarker's lap % — the DRIVER's part, without what the car contributes
    double median_pct = 0.0;  // median's lap % — from the CLASS, DRIVER part and with the dial inside
    bool   flat    = false;   // all with the same talent -> distribution by rank (guard 3)
    bool   one_leg = false;   // one of the two gaps is 0, or there's no knee -> a single straight segment (guard 2)
    bool   no_data = true;    // unfilled: NOTHING is invented: 1.0 to everyone (guards 1 and 4)
};

// `spreadDriverPct` and `medianDriverPct` arrive ALREADY with the car discounted (they are the DRIVER's part, NOT the PSV's
// raw columns). Nothing is discounted again here: counting it twice is exactly the failure
// that the harness watches for.
//
// `headOpen` IS A PARAMETER, not the constant read from inside, and the reason is that otherwise it CAN'T BE
// TESTED. `kHeadOpen` is `constexpr`: without injecting it, the only way to compare two dials is for the test
// to fabricate the pins and overwrite `median_pct` by hand, which demonstrates that the knee scales with the FIELD
// — which is nothing — and not that the dial reaches it. Deleting the `* headOpen` on the line below would leave the
// suite green. With the parameter, the test passes 1.0 and 1.3 down the real path, 95% cap included.
inline GridPins PinsForGrid(const std::vector<double>& talents, double spreadDriverPct,
                            double medianDriverPct, double headOpen = kHeadOpen) {
    GridPins p;
    // Guards 1 and 4 (spec ): with no grid or no class datum NO fan is invented. The caller
    // writes 1.0 to everyone, which is what a class with no row in the PSV does today.
    if (talents.size() < 2 || spreadDriverPct <= 0.0) return p;          // p.no_data is born true
    p.no_data = false;                                                   // only here: the guards were passed
    std::vector<double> s = talents;
    std::sort(s.begin(), s.end());
    p.w_last = s.front();
    p.w_ace  = s.back();
    const size_t n = s.size();
    p.w_med = (n % 2) ? s[n/2] : 0.5 * (s[n/2 - 1] + s[n/2]);
    p.spread_pct = spreadDriverPct;
    // The playability dial, and ONLY the knee: it raises the drop that the top half consumes without touching the width.
    // The cap at 95% of the fan stops the knee from eating the whole tail segment and leaving it flat.
    p.median_pct = GcClamp(medianDriverPct * headOpen, 0.0, spreadDriverPct * 0.95);
    if (p.w_ace - p.w_last <= 0.0) { p.flat = true; return p; }          // guard 3, BEFORE 2
    if (p.w_ace - p.w_med <= 0.0 || p.w_med - p.w_last <= 0.0) p.one_leg = true;
    // Median missing or incoherent -> a SINGLE segment. `ShapeForClass` already had this guard ("the median is
    // trimmed to the floor") and the conversion had lost it: with the middle pin at 0, the head branch
    // returns pct = 0 for the WHOLE top half, i.e. half the grid tied at 1.0000 without a bit of noise.
    // It isn't reachable from the curated datum (109 rows of class_pace_curve.psv, 0 with median <= 0), but it is
    // from a pins half-declared in the caller.
    if (p.median_pct <= 0.0) p.one_leg = true;
    return p;
}

// Lap % that a talent `w` loses over this grid. The two straight segments of , bounded to
// [0, spread_pct]: however much it extrapolates, nobody loses more than their class's fan.
//
// IT IS NOT THE INVERSE OF `RaceSkillOnPins` ON A FLAT GRID, and it's not a bug: the two functions take
// different inputs. With everyone at the same talent there's no gap to measure, so here it comes out 0% FOR EVERYONE
// — there's nothing to look up — while `RaceSkillOnPins` distributes by RANK over the whole fan, which
// is the spec's brake so that two drivers don't come out with the same pace. Two consequences, both
// for the wiring:
//   1. a diagnostic that wants to count the DELIVERED % can't read it from here: on a flat grid it
//      would report 0 while the XML carries the full fan written;
//   2. quali on a flat grid ignores its shift (Saturday = Sunday), because the shift comes in through
//      talent and here talent isn't looked at.
inline double PctOffOnPins(double w, const GridPins& p) {
    if (p.no_data) return 0.0;                       // guard 1/4: without data there's no fan to distribute
    if (w >= p.w_ace) return 0.0;                    // the ace (and quali, which can rise above it)
    double pct;
    if (p.one_leg || p.flat) {
        // Guard 2 (and 3 for safety): with a zero gap there's no knee to place, so the whole band
        // is ONE segment from the ace to the backmarker. The denominator is checked so that not even the flat grid
        // can divide by zero.
        const double den = p.w_ace - p.w_last;
        pct = (den <= 0.0) ? 0.0 : p.spread_pct * (p.w_ace - w) / den;
    } else if (w >= p.w_med) {
        pct = p.median_pct * (p.w_ace - w) / (p.w_ace - p.w_med);                      // head: ace -> median
    } else {
        pct = p.median_pct + (p.spread_pct - p.median_pct)
                             * (p.w_med - w) / (p.w_med - p.w_last);                   // tail: median -> last
    }
    return GcClamp(pct, 0.0, p.spread_pct);
}

// The number written to the XML. `rank`/`n` are only used on the FLAT grid — the last brake of the
// spec: if everyone brings the same talent there's no gap to measure, so it's distributed by rank over
// the class's fan and so two drivers can't come out with the same pace.
// 25/08 — THE SHAPE IS SET BY THE CLASS, NOT BY THIS GRID'S TALENT DISTRIBUTION.
//
// User decision, and it INVERTS what task 2 fixed ("talent goes back to deciding the gaps,
// not just the order"). The argument that tips it: "if there are 20 drivers with the same talent the same
// time curve should come out, because those 20 are spread along that curve".
//
// And there's a measurement behind it. PRODUCTION grids had steps where the cast had a gap:
// Harbor_Masters_8186 distributed 1.0000 · 0.9945 · 0.9452 · 0.9158 and then a 0.1312 chasm at the fifth —
// typical gap 0.0123, i.e. TEN TIMES the normal. On track that's four cars gone and a
// second and a quarter of emptiness behind. Pinebrook_Motors the same (11.3x). The grids written with a uniform
// ramp don't have it.
//
// So talent decides the ORDER and the class decides the GAPS: the same three-pin curve is walked,
// but by RANK. The grid's median falls on the class's knee by construction, which
// is what preserves its character.
inline double PctOffByRank(int rank, int n, const GridPins& p) {
    if (p.no_data || n < 2) return 0.0;
    const int r = (rank < 0) ? 0 : ((rank > n - 1) ? n - 1 : rank);
    const double f = (double)r / (double)(n - 1);          // 0 = ace, 1 = tail-ender
    double pct;
    if (p.one_leg || p.median_pct <= 0.0) {
        pct = p.spread_pct * f;                             // no knee: a single straight segment
    } else if (f <= 0.5) {
        pct = p.median_pct * (f / 0.5);                     // head: ace -> median
    } else {
        pct = p.median_pct + (p.spread_pct - p.median_pct) * ((f - 0.5) / 0.5);   // tail
    }
    return GcClamp(pct, 0.0, p.spread_pct);
}

// Switch for the A/B: true = the gaps are set by the CLASS by rank (the above);
//  false = they are set by each driver's talent (task 2's path). It's kept so the pair
//  can be measured, just as was done with Curve.
inline bool& GridShapeByRank() { static bool v = true; return v; }

// 25/08 — QUALIFYING COMES FROM THE DRIVER, NOT FROM THE GRID.
//
// The bias toward Saturday or Sunday is ARCHETYPE: the driver carries it and it isn't distributed across the grid.
// Here the pace is taken ALREADY converted (which does come from the class's curve) and the driver's
// OWN shift is applied, without looking at anyone else. That way, if a one-lap specialist improves, your Saturday
// time doesn't change - only your position - which is how a qualifying session works.
//
// The shift arrives on the TALENT scale; it's taken to lap % with this grid's ratio
// (fan / talent travel) so that the same bias weighs the same in a tight class and in an
// open one. Without that conversion, 0.05 of bias would be worth seven times more in the '86 F1 than in a kart.
inline double QualifyingFromOwnShift(double raceSkill, double shiftTalento, const GridPins& p, double scale = 1.0) {
    if (p.no_data) return raceSkill;
    const double den = p.w_ace - p.w_last;
    if (den <= 0.0 || p.spread_pct <= 0.0) return raceSkill;   // flat grid: nothing to scale
    const double pctRitmo = PctOffForRaceSkill(raceSkill, scale);
    const double pctQuali = pctRitmo - shiftTalento * (p.spread_pct / den);
    return GcClamp(FloorForSpread(GcClamp(pctQuali, 0.0, p.spread_pct), scale), kMinDriveableSkill, 1.0);
}

inline double RaceSkillOnPins(double w, const GridPins& p, int rank = -1, int n = -1, double scale = 1.0) {
    if (p.no_data) return 1.0;                       // a class with no datum delivers the whole grid at 1.0
    double pct;
    if (p.flat) {
        if (rank < 0 || n < 2) return 1.0;           // without a rank there's nothing to distribute
        const int r = (rank > n - 1) ? n - 1 : rank;
        pct = p.spread_pct * (double)r / (double)(n - 1);
    } else if (GridShapeByRank() && rank >= 0 && n >= 2) {
        pct = PctOffByRank(rank, n, p);      // the shape is set by the CLASS (see the note above)
    } else {
        pct = PctOffOnPins(w, p);            // old path: the gaps are set by talent
    }
    pct = GcClamp(pct, 0.0, p.spread_pct);
    // The MEASURED inverse, and the kMinDriveableSkill net below it: it's the lower edge where it was verified
    // that the AI drives, not a calibration.
    return GcClamp(FloorForSpread(pct, scale), kMinDriveableSkill, 1.0);   // 09/09: at the class's slider
}

// Custom Race s26 is a multiplier: 100 in memory = 1.00x in the game's UI.
// Always neutral, regardless of weather or base difficulty. Individual XML wet_skill is separate.
constexpr int kWetMultNeutral = 100;
inline int WetMultForRain(int /*maxWetness*/) { return kWetMultNeutral; }

// Highest wetness in a weather plan, with legacy fallback. Random counts as medium rain.
// Retained for weather-plan callers; it no longer reduces the Custom Race wet multiplier.
inline int MaxWetnessOfSlots(const int weather4[4], int weatherLegacy) {
    auto wet = [](int w) {
        return w == 5 || w == 6 || w == 7 || w == 8 || w == 13 || w == 15 || w == 17;
    };
    auto rank = [&](int w) {                       // "how wet", so they can be compared against each other
        if (!wet(w)) return 0;
        switch (w) { case 5: return 1; case 13: case 15: case 17: return 2;
                     case 6: return 2; case 7: return 3; default: return 4; }   // 8 = thunderstorm
    };
    int best = 0, bestRank = 0;
    const bool haveSlots = weather4 && weather4[0] >= 0;
    for (int i = 0; i < 4; i++) {
        const int w = haveSlots ? weather4[i] : (i == 0 ? weatherLegacy : -1);
        if (w < 0) continue;
        const int r = rank(w);
        if (r > bestRank) { bestRank = r; best = w; }
    }
    return best;                                   // 0 = dry
}

} // namespace career
