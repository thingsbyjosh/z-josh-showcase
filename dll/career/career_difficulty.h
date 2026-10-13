#pragma once
// PURE difficulty logic (no ImGui, no game). Header-only (inline) -> testable with career_test
// and compilable in the DLL without touching the build scripts, per the adaptive-difficulty design.
#include <vector>
#include <string>
#include <algorithm>

namespace career {

// The mode chosen by the player (persisted in CareerProgress.difficulty_mode).
// DM_CUSTOM: difficulty FIXED by hand. The four bands move the number with your category level and the
// Adaptive mode nudges it race by race; this one never moves on its own. It's the exact AMS2 slider
// number, the same every race, until the player changes it.
enum DifficultyMode { DM_EASY = 0, DM_MEDIUM = 1, DM_HARD = 2, DM_EXTREME = 3, DM_ADAPTIVE = 4, DM_CUSTOM = 5 };

namespace dda {
  constexpr int kAiMin = 70, kAiMax = 120, kAiSeed = 80;   // AMS2 slider domain + seed
  constexpr int kCustomDefault = 90;                       // DM_CUSTOM untouched: high-center of the domain
  constexpr int kBandLo[4] = { 70, 80, 90, 100 };          // [Easy,Medium,Hard,Extreme]
  constexpr int kBandHi[4] = { 80, 90, 100, 120 };
  constexpr int kLevels = 7;                                // ladder levels (1..7)
  // Model in TIME (not in "cars") ----
  // The signal is the player's advantage as a FRACTION of their lap time against the podium-edge
  // reference (ComputeGapNorm), converted to slider points via the LOCAL slope of the measured
  // calibration samples. The original model normalized by the gap between the front-runners: on tight
  // grids (Formula Vee: a 0.043s gap) that spiked the signal, and the linear gain translated it into +33 points.
  // The time-based model fixed that, but left two pieces this block corrects: a FLAT slope anchored to
  // Formula Vee Gen1 at Millvale, and a 1.5% target IN THE PLAYER'S FAVOR that made the balance mean "you dominate".
  // See the notes below for the full reasoning.
  // Block A: ZERO target against the podium-edge reference (ComputeGapNorm). The previous +1.5%
  // target meant "the player laps 1.5% faster than the fastest AI", about 0.95s at
  // Stoneton Bay: pole and the win were guaranteed barring a mistake. It also contradicted kCalibTargetGap (-0.2s,
  // AI ahead): the game calibrated for a fight while the loop pushed toward dominance. With a zero target, your potential pace
  // lands at 3rd-place level and the podium is actually contested. The +/-5 trim is what moves that point.
  // Converting the error to slider points is done by SlopePctPerPointAt (the local slope of the measured calibration samples);
  // the old flat constant is REMOVED (it was anchored to Formula Vee under the previous model).
  constexpr double kTargetMarginPct  = 0.000;
  constexpr double kDeadbandPct      = 0.004;   // |advantage - target| <= 0.4% -> leave it alone
  constexpr double kDownFactor       = 0.5;     // DECREASES run at half speed (reluctant to drop the level over one bad race)
  // telemetry from 0.5.0 (1,082 races, 526 measured base transitions): the loop climbed +8.16
  // points for a +4.67% error but only dropped -3.61 for a -4.31% one -- two and a half times slower
  // going down, and in 12 of 38 races lost by a wide margin it didn't drop at all. Three dampers in series were
  // stacked to blame (the deadband, the rolling-window median, and the down-factor) -- only the first is symmetric.
  // The original reasoning for a slow decrease ("one bad race isn't your real level") holds for a -0.5% error, not a
  // 3% one: at that point it's not noise, the level is simply wrong and the player eats the whole session for it.
  // Above this threshold the sample counts as CLEAR and the correction runs at full speed.
  constexpr double kClearErrorPct    = 0.015;
  constexpr double kMaxStep  =  8.0;   // adjustment cap for a NORMAL recalibration (the measurement race uses the full slider range = no cap)
  // CONFIDENCE cap on a single advantage sample (winsorizing). One huge swing in a single
  // race (e.g. +4s over the AI: a class where the AI struggles, an atypical track) is NOT reliable signal of the overall level:
  // it's capped at +/-3% before entering the rolling window, so that race's step lands around +/-4 points, and if the advantage
  // is real, the following races confirm it and keep adjusting (dampen, then recompute -- the chosen tradeoff).
  constexpr double kGapSampleCap = 0.03;
  constexpr int    kMinRivals = 3;     // minimum rivals with a valid lap
  constexpr int    kWindow   =  3;     // rolling window of the advantage (median) for smoothing
  constexpr int    kTrimMin = -5, kTrimMax = 5;
  // Per-class DDA rework: there are NO separate per-category tuning constants for the loop. The loop IS the base one
  // (DdaStep + DecisionGap + ClampGapSample), with its deadband, its gain, its +/-8 cap and its clamp, exactly as
  // 1,082 races of telemetry validated it. The first revision introduced its own kClassGainClear/Doubt/MaxStep/Cap/
  // Deadband for a SECOND controller, and that's exactly what sank it: two loops correcting the same
  // error fight each other (36-42 points of measured oscillation in multiclass). See the notes below.
  constexpr int kClassWindow = 5;   // per-category rolling window, SAVED (the median takes the last 3 samples)
  // Block D: local effect of the player's FEEL feedback, deliberately WEAK. A single answer could
  // be about the circuit, the car, or just a bad day, not the difficulty level: the objective loop still rules
  // always. That's why only the extreme answers move anything (1 = way too easy, 5 = way too hard), only when they REPEAT in the
  // same category, and only one slider point per repetition. The cap becomes the slider's own domain
  // (kAiMin/kAiMax): player feel can never push the number further than the loop would reach on its own.
  constexpr double kFeelStepPts = 1.0;   // slider points per confirmed repetition
  constexpr int    kFeelRepeat  = 2;     // identical answers in a row, in the same class, before moving anything
}

inline int Clampi(int v, int lo, int hi){ return v<lo?lo:(v>hi?hi:v); }

// Skill for a FIXED preset: a linear ramp across the band by category level (1..7).
inline int SkillForFixedMode(int mode, int level) {
    if (mode < DM_EASY || mode > DM_EXTREME) mode = DM_MEDIUM;
    int lv = Clampi(level, 1, dda::kLevels);
    int lo = dda::kBandLo[mode], hi = dda::kBandHi[mode];
    double t = (dda::kLevels > 1) ? double(lv - 1) / (dda::kLevels - 1) : 0.0;
    return (int)(lo + (hi - lo) * t + 0.5);
}

// DM_CUSTOM: the number the player picked by hand, verbatim, clamped to the slider's real domain. It doesn't depend on the category
// level, the class, the circuit, or the trim: if the player sets 105, the race runs at 105, ALWAYS. That's
// the whole promise of that setting card, and adding any hidden layer would break it invisibly.
inline int SkillForCustom(int customAi) { return Clampi(customAi, dda::kAiMin, dda::kAiMax); }

// Per-class DDA rework: the race's effective skill. In adaptive mode the number is the
// CATEGORY's own number (aiBase, already resolved by career::ClassAiNow: community-seeded until you have races, yours once you do)
// plus your manual trim. offClass/offTrack stay in the function signature so callers don't break, but they now always
// resolve to ZERO from here on: the per-class-and-track community layer was retired, and the class offset
// no longer exists as a separate layer -- it's now folded INTO the category's own number. The clamp to the slider's domain
// is applied ONCE, to the total. The fixed modes ignore all of this: they're just a level-based band ramp with
// nothing to learn.
inline int ResolveRaceSkillLayered(int mode, int level, int aiBase, int trim, int offClass, int offTrack,
                                   int customAi = dda::kCustomDefault) {
    if (mode == DM_ADAPTIVE)
        return Clampi(aiBase + trim + offClass + offTrack, dda::kAiMin, dda::kAiMax);
    if (mode == DM_CUSTOM) return SkillForCustom(customAi);
    return SkillForFixedMode(mode, level);
}

// Wrapper for call sites that don't know about the layers yet (offsets forced to zero).
inline int ResolveRaceSkill(int mode, int level, int aiDifficulty, int trim, int customAi = dda::kCustomDefault) {
    return ResolveRaceSkillLayered(mode, level, aiDifficulty, trim, 0, 0, customAi);
}

// MEASUREMENT race: difficulty by INTERPOLATION over real measured samples ----
// The AI's improvement with the slider is NOT linear for the calibration combo used (Kingsmere Uma713 at Blackvale
// Hatch Indy, dry, 14:00): going from 80->90 gains ~2.6s but 110->120 only gains ~1.2s. A linear model
// gets the middle right and misses at the extremes. Instead: a table of MEASURED
// in-game samples (average of the AI's 3 best laps per difficulty setting; measured by the user on 07/17, iteration 2 of
// the setting card) + direct piecewise interpolation (a deliberate choice: stable, and it never invents behavior
// between points). ONLY for the measurement race: normal-race recalibration still uses the relative model
// (ComputeGapNorm/DdaStep), which doesn't depend on the car/track.
// If the calibration car/track combo ever changes (kCalib* in the overlay), RE-MEASURE this table.
struct CalibSample { int difficulty; double aiTop3; };   // aiTop3 = average of the AI's 3 best laps (s)
constexpr CalibSample kCalibSamples[] =
    { {80, 63.095}, {90, 60.467}, {100, 58.497}, {110, 57.348}, {120, 56.106} };
constexpr int    kCalibSampleN   = 5;
constexpr double kCalibTargetGap = -0.2;   // target: aiTop3Average - playerBest (the AI ends up ~0.2s ahead)

// Estimated difficulty so the AI's top-3 average sits targetGap seconds off the player's lap.
// Inside the measured range: piecewise interpolation. OUTSIDE the range: EXTRAPOLATES using the slope of the
// end segment, clamped to the slider's real domain (70..120). The iteration-2 table already covers up to 120 (the
// slider's ceiling); at the low end, extrapolation covers very green pace (70..80 unmeasured). Rounding goes
// DOWN (conservative: when in doubt, make the AI a touch slower -- the adaptive loop corrects it afterward).
inline int EstimateCalibDifficulty(double playerBest, double targetGap = kCalibTargetGap) {
    if (playerBest <= 0.0) return dda::kAiSeed;                    // no valid lap -> use the seed value
    double targetAi = playerBest + targetGap;                       // AI's target lap
    // segment that contains targetAi (aiTop3 DECREASES as difficulty rises); outside the table -> end segment
    int i = 0;
    while (i < kCalibSampleN - 2 && targetAi < kCalibSamples[i+1].aiTop3) i++;
    const CalibSample& a = kCalibSamples[i];
    const CalibSample& b = kCalibSamples[i+1];
    double t = (targetAi - a.aiTop3) / (b.aiTop3 - a.aiTop3);       // <0 or >1 = extrapolate past the end
    double d = a.difficulty + t * (b.difficulty - a.difficulty);
    if (d < (double)dda::kAiMin) return dda::kAiMin;                // clamp BEFORE the floor (avoids truncating toward 0)
    return Clampi((int)d, dda::kAiMin, dda::kAiMax);                // (int) = floor (d>=70 here)
}

// Block A: the slider's LOCAL slope, in lap-time FRACTION per point.
// The old flat constant was anchored to Formula Vee Gen1 at Millvale under the previous model, and the
// measured calibration samples show the AI's improvement is NOT linear: 80 to 90 gains 2.628s but 100 to 110 gains only
// 1.149s. With a flat slope the controller OVERSHOOTS at the low end (the real slope is steeper there, so
// it needs fewer points than it computes) and UNDERSHOOTS at the high end. Piecewise, it self-corrects.
// Outside the measured range it extrapolates with the end segment. At a segment boundary, the upper segment wins.
inline double SlopePctPerPointAt(int difficulty) {
    int i = 0;
    while (i < kCalibSampleN - 2 && difficulty >= kCalibSamples[i+1].difficulty) i++;
    const CalibSample& a = kCalibSamples[i];
    const CalibSample& b = kCalibSamples[i+1];
    double secPerPoint = (a.aiTop3 - b.aiTop3) / (double)(b.difficulty - a.difficulty);   // s/pt (>0)
    double lapMean     = 0.5 * (a.aiTop3 + b.aiTop3);
    return secPerPoint / lapMean;
}

// DDA (adaptive mode / measurement race only) ----

// Normalized signal: the player's advantage as a FRACTION of their lap time against the podium-EDGE
// reference -- the average of the class's 2nd, 3rd and 4th-place AI best laps. Previously the
// reference was the single fastest AI: with today's tight grid it barely mattered, but as soon as
// the grid spread widens, one fast driver alone would set the level for the whole field. Three drivers are used, and
// not just the third one, because a single lap can be noise.
// true + outGapNorm when there's enough data. rivalBest = best laps of rivals in the SAME class
// (excluding the player); non-positive laps are filtered out. + means the player is faster than the reference (raise the difficulty).
inline bool ComputeGapNorm(double playerBest, std::vector<double> rivalBest, double& outGapNorm) {
    if (playerBest <= 0.0) return false;
    rivalBest.erase(std::remove_if(rivalBest.begin(), rivalBest.end(),
                    [](double v){ return v <= 0.0; }), rivalBest.end());
    if ((int)rivalBest.size() < dda::kMinRivals) return false;
    std::sort(rivalBest.begin(), rivalBest.end());
    // indices 1..3 = the 2nd, 3rd and 4th-place AI. With a short grid it uses whatever's available from index 1 (with the minimum
    // of 3 rivals, at least two are always left, so the average is never a single car).
    const int lo = 1;
    const int hi = (int)rivalBest.size() - 1 < 3 ? (int)rivalBest.size() - 1 : 3;
    double sum = 0.0; int n = 0;
    for (int i = lo; i <= hi; i++) { sum += rivalBest[i]; n++; }
    double ref = sum / (double)n;
    outGapNorm = (ref - playerBest) / playerBest;
    return true;
}

// Controller step: the new ai_difficulty given the current one and the measured advantage (lap-time fraction).
// Converts the advantage-over-target into slider points via the LOCAL slope, SlopePctPerPointAt.
// a large maxStep (the full slider range) on the MEASUREMENT race -> sets the level in one shot; kMaxStep applies on
// normal recalibration.
// `slopeAt`: the level the race was actually RUN at (base + trim + offsets, already clamped). The advantage was
// measured against an AI running at THAT level, and the curve isn't linear (0.42% per point at 80-90 versus 0.22% at
// 110-120): converting with the base's slope is off by up to 67% once the layers add up to 10 points.
// Today it barely bites (offClass was non-zero in 8 of 279 real races, offTrack in none), but it would bite exactly
// the day community offsets ship. Default -1 = use aiDifficulty, the same behavior as
// always, for callers that don't know about the effective value.
inline int DdaStep(int aiDifficulty, double advPct, double maxStep = dda::kMaxStep, int slopeAt = -1) {
    double error = advPct - dda::kTargetMarginPct;     // advantage above the target (fraction)
    double absErr = error < 0 ? -error : error;
    if (absErr <= dda::kDeadbandPct) return Clampi(aiDifficulty, dda::kAiMin, dda::kAiMax);
    double pts = error / SlopePctPerPointAt(slopeAt < 0 ? aiDifficulty : slopeAt);   // fraction -> points
    // The half-speed decrease is reserved for the zone of DOUBT. With clear evidence
    // (|error| >= kClearErrorPct) it corrects at full speed in both directions.
    if (error < 0 && absErr < dda::kClearErrorPct) pts *= dda::kDownFactor;
    if (pts >  maxStep) pts =  maxStep;
    if (pts < -maxStep) pts = -maxStep;
    int idelta = (int)(pts + (pts >= 0 ? 0.5 : -0.5));
    return Clampi(aiDifficulty + idelta, dda::kAiMin, dda::kAiMax);
}

// iteration 2, generalized in Block A. The race is run at clamp(base+trim+offClass+
// offTrack), but the loop learns the BASE value. Only what the TRIM contributed is subtracted out, because the trim is a
// DELIBERATE deviation from the balance point (the player wants it easier or harder on purpose). The class and track offsets
// are CORRECTIONS toward the balance point: they already did their job during the race, and their effect must
// stay inside the measurement. Subtracting them out would let the base value eat the community's correction.
// The trim delta is computed as the difference between the effective level WITH trim and WITHOUT it, so the
// slider clamp stays consistent (base 118 + offset 4 + trim 5 runs at 120, and without the trim it's also 120: delta 0).
// The slope is evaluated at the EFFECTIVE level (with trim), not at the base. The trim being subtracted out was
// produced by an AI running at that level, and the measured calibration curve isn't linear. With a base of 88 and an effective level of 99
// (class offset +6, trim +5), converting with the base's slope inflates the subtracted amount by 40%.
inline double GapSampleVsBase(double gapNorm, int aiBase, int trim, int offClass = 0, int offTrack = 0) {
    int withTrim = Clampi(aiBase + trim + offClass + offTrack, dda::kAiMin, dda::kAiMax);
    int noTrim   = Clampi(aiBase        + offClass + offTrack, dda::kAiMin, dda::kAiMax);
    int applied  = withTrim - noTrim;
    return gapNorm + (double)applied * SlopePctPerPointAt(withTrim);
}

// Which sample decides the base's step. The rolling median exists to reject noise, and
// it still rules for that. But with a CLEAR downward sample, waiting for two more races to move the
// median means leaving the player half an hour per race at a level we already know isn't theirs.
// Only DOWNWARD, on purpose: the telemetry shows that increases are already fast (median step +6 after
// winning a race on pace), and the hard tail of the feedback form started showing up (31 votes of "too
// hard"), so speeding up increases too would fix what isn't broken and worsen what is.
inline double DecisionGap(double lastSample, double windowMedian) {
    if (lastSample < -dda::kClearErrorPct) return lastSample;
    return windowMedian;
}

// Should the player be offered the jump the loop didn't dare take? `suggested` is where the
// loop would go trusting the sample fully and with no step cap; `applied` is where it actually landed.
//  - anomalous (winsorized) sample: the usual offer, in both directions.
//  - NEW: a CLEAR downward sample whose step fell SHORT because of kMaxStep. This is exactly the "and
//    on top of that, it takes another race" case: the loop already knows it needs 13 points, can only give 8, and without an offer
//    the player eats an entire extra race at a level neither side believes is right.
// Only downward, and only with clear evidence: upward the loop already gets there fast (measured median step +6 after
// winning on pace), and surfacing the card in 199 of every 526 races would turn it into noise nobody reads.
inline bool GapOfferOpens(bool capped, double gapVsBase, int suggested, int applied) {
    if (suggested == applied) return false;         // offering what's already there isn't an offer
    if (capped) return true;
    return gapVsBase <= -dda::kClearErrorPct && suggested <= applied - 3;
}

// Winsorizes an advantage sample at the confidence cap (see dda::kGapSampleCap).
inline double ClampGapSample(double gapNorm) {
    if (gapNorm >  dda::kGapSampleCap) return  dda::kGapSampleCap;
    if (gapNorm < -dda::kGapSampleCap) return -dda::kGapSampleCap;
    return gapNorm;
}

// Median of a set of samples (copies + sorts). Empty -> 0.
inline double MedianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t m = v.size()/2;
    return (v.size()%2) ? v[m] : 0.5*(v[m-1]+v[m]);
}

// Fraction of a lap the AI gains when going from `from` to `to` slider points, walking the curve point by
// point with the LOCAL slope of each segment (a step from 98 to 106 crosses 100: 2 points at 0.33% and 6 at 0.20%).
// Signed: + if it goes up. from == to -> 0.
inline double SliderFracBetween(int from, int to) {
    const int lo = from < to ? from : to;
    const int hi = from < to ? to : from;
    double f = 0.0;
    for (int p = lo; p < hi; p++) f += SlopePctPerPointAt(p);
    return from < to ? f : -f;
}

// 19/09 (report: adaptive went up from 106 to 112 after a P12 from the pack). Player log: Blackmoor Park 90->98
// with sample +2.65%, Northcombe Bay 98->106 with +1.21%, Wildham 106->112 with +0.05% -- and at Wildham they ran EXACTLY at their level.
// The window stored each sample RAW, measured against the AI of ITS race: the +2.65% from Blackmoor Park had already
// been corrected by the Blackmoor Park step itself, but it stayed in the window saying "you are 2.65% faster" and
// the median charged it again race after race. At Wildham the median of [2.65 1.21 0.05] was 1.21% -> +6.
// Now, when the number moves, the whole window is RE-EXPRESSED against the new number: from each sample is
// removed what the AI has gained with the step (SliderFracBetween), so the window always says "how much you
// have to spare against the AI you are going to face", not against the one that is no longer there. A step that over- or
// under-corrected leaves its remainder inside, and the next race confirms or undoes it.
// The reference (podium edge), the deadband, the cap and the half descent speed do not change: it is the same
// loop (DdaStep + DecisionGap), only it no longer eats its own correction twice. The number only moves
// through the loop here; the feel (FeelAiStep) and the lock re-express nothing.
struct DdaLearnResult {
    int    number = 0;      // the category's number after this race
    double median = 0.0;    // median of the last kWindow (with the new sample, BEFORE re-expressing)
    double decision = 0.0;  // what the step decided (the median, or the clear sample downward)
    int    windowN = 0;     // samples that went into the median
    double discount = 0.0;  // fraction removed from each stored sample (0 = the number did not move)
};
inline DdaLearnResult DdaLearnSample(std::vector<float>& win, int number, int aiEff, double sample, bool locked) {
    DdaLearnResult r;
    win.push_back((float)sample);
    while ((int)win.size() > dda::kClassWindow) win.erase(win.begin());
    r.windowN = (int)win.size() < dda::kWindow ? (int)win.size() : dda::kWindow;
    r.median = MedianOf(std::vector<double>(win.end() - r.windowN, win.end()));
    r.decision = DecisionGap(sample, r.median);
    r.number = locked ? number : DdaStep(number, r.decision, dda::kMaxStep, aiEff);
    if (r.number != number) {
        r.discount = SliderFracBetween(number, r.number);
        for (float& g : win) g = (float)((double)g - r.discount);
    }
    return r;
}

// Per-class DDA rework: the OVERALL number = the arithmetic mean of the per-category numbers, ONE CATEGORY
// ONE VOTE. `ais` holds the numbers for categories WITH races (the caller filters: this function can't
// know anything about race counters). Empty -> `fallback`, the number the measurement race left behind.
// It doesn't learn, it's CALCULATED. That's the difference from the first revision: a calculated number can't drift,
// so there's nothing to anchor and nothing for it to oscillate against.
inline int ClassGeneralFrom(const std::vector<int>& ais, int fallback) {
    if (ais.empty()) return Clampi(fallback, dda::kAiMin, dda::kAiMax);
    double s = 0.0;
    for (int v : ais) s += (double)v;
    const double m = s / (double)ais.size();
    const int r = (int)(m + (m >= 0 ? 0.5 : -0.5));   // round away from zero
    return Clampi(r, dda::kAiMin, dda::kAiMax);
}

// Per-class DDA rework: the number that governs a category. With 0 races it's seeded by the community
// value ON TOP of the overall number (the community offset is a relative correction: "AMS2's AI is weak here"); with one
// race of your own, the saved value takes over and no later release touches it again. It's resolved on the fly and not stored
// because a category's entry can exist with 0 races.
// ~~~09/09 (): THE EFFECTIVE SLIDER OF A CLASS, computed from the game state and NOTHING ELSE: the
//  category's number (its own if it has run, otherwise the general plus the community), the mode, the trim and the
//  custom, via ResolveRaceSkillLayered. It is what CalUiSkillForLevel resolves when arming (career_ui_bridge.inc)
//  and what pregen needs BEFORE arming to write the XML at the slider it will run at. No side effects:
//  it leaves no latch and no log. Defined in career_engine.cpp (needs CareerProgress and the community offsets).
struct CareerProgress;
int EffectiveAiForClass(const CareerProgress& prog, const std::string& classId, int level, bool wet = false);
// ~~~09/09 (, 2): THE NUMBER OF A CATEGORY, in a single calculation: its own if it has run; if you
//  are new to it, the starting point plus the community. And the starting point is no longer always the general: if you have
//  run other categories of the SAME car FAMILY (single-seaters, GT, prototypes, touring cars, ovals,
//  historics), it is the average of your numbers in them -- the “family_delta” of , but COMPUTED, not learned
//  (revision 2 of the DDA knocked down the layers that learn because they oscillated: a computed number cannot). Without
//  a family run, the general, as until today. Does NOT create the entry (find, never operator[]).
// `wet` (09/09, 3): with WET weather, the category's wet number if it already has one; if not, the dry one.
int ClassNumberFor(const CareerProgress& prog, const std::string& classId, bool wet = false);
// Diag/UI: the starting point of a class you are new to (family average or the general) and how many categories
// of its family support it (0 = falls to the general).
int ClassColdStartFor(const CareerProgress& prog, const std::string& classId, int* familyClasses = nullptr);
// ~~~09/09 (, 4): SATURATION, VISIBLE (). +1 = the category's number is at the CEILING
//  (kAiMax) and the median of its last kWindow samples still says “you are faster” (above the deadband);
//  -1 = at the FLOOR (kAiMin) and you are still slower; 0 = nothing to flag (or fewer than kWindow samples). It does not distort the
//  hierarchy nor touch the number: it is only SHOWN, which is what the spec asks for (“it is not distorted to hide it”).
//  It is derived from the saved state (number + window): there is no new field in the save. `wet` = the wet window.
struct ClassDdaState;
int ClassSaturation(const ClassDdaState& st, bool wet);
inline int ClassAiFor(int races, int storedAi, int general, int communityOffset) {
    if (races > 0) return Clampi(storedAi, dda::kAiMin, dda::kAiMax);
    return Clampi(general + communityOffset, dda::kAiMin, dda::kAiMax);
}

// Block D: streak of IDENTICAL, CONSECUTIVE feedback-form answers in the SAME class. Returns the
// updated streak, counting this answer. Changing the answer resets it to 1; skipping the question (feel
// outside 1..5, typically 0) resets it to 0 -- not answering confirms nothing, so it doesn't carry the previous
// streak forward either: two "way too easy" answers with a silent skip in between don't count as consecutive.
inline int FeelStreakNext(int prevFeel, int prevStreak, int feel) {
    if (feel < 1 || feel > 5) return 0;
    if (feel != prevFeel || prevStreak <= 0) return 1;
    return prevStreak + 1;
}

// Block D, in the per-class rework: the feedback form's FEEL rating moves the category's number by
// one point per confirmed repetition, and only at the extremes. `streak` is the ALREADY-updated streak (FeelStreakNext).
// Deliberately weak: one answer could be about the circuit or a bad day, not the difficulty level, so
// the objective loop always rules and can undo it in the very next race.
inline int FeelAiStep(int ai, int feel, int streak) {
    if (feel != 1 && feel != 5) return ai;
    if (streak < dda::kFeelRepeat) return ai;
    const int step = (feel == 1) ? (int)dda::kFeelStepPts : -(int)dda::kFeelStepPts;
    return Clampi(ai + step, dda::kAiMin, dda::kAiMax);
}

} // namespace career
