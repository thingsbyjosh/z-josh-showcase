#pragma once
// Per-class qualifying spreads (2026-07-24): the last-place and median gaps as a % of the
// pole time, from REAL data by category and era. Packaged per release in docs/career/class_pace_curve.psv.
// BuildGridXml consumes it to set the band's floor+shape. Missing/empty/unreadable = sane defaults (never a
// crash): the grid falls back to a reasonable club-level spread until the real data is available.
#include <map>
#include <string>

namespace career {

// Per-class spreads, in % of lap time. Dry and wet kept separate (wet spreads out more in reality).
struct ClassPace {
    double spread_dry = 6.0;    // % last vs. first, dry (default = a reasonable club spread)
    double median_dry = 2.4;
    double spread_wet = 9.0;    // wet spreads out more
    double median_wet = 3.6;
    // 28/07 (col 7, optional): this class's TALENT SPAN, i.e. how much talent separates its ace from its
    // backmarker. It's what converts a talent gap into a pace gap. 0 = no data -> falls back to
    // career_gridcurve's global value (0.30). It's needed per class because a driver races in several and their talent is
    // just one number: the class dictates the width, talent dictates the order. See kTalentRefSpan.
    double talent_ref_span = 0.0;
    // — THE BAR IS DERIVED, NOT CURATED. `talent_ref_trim` is a
    // MULTIPLIER over the bar that the seeding scale produces at the series' level, never an independent
    // absolute value. 0 = no trim (falls to `talent_ref_span`, the legacy column).
    //
    // Why the change: `talent_ref_span` is a CONVERSION factor between two scales (talent -> % of
    // lap), not a property of the category, and it was measured against the grids of one specific day. As
    // soon as the seeding changes —and it changed in .5— those 99 numbers describe grids that no longer
    // exist, silently. Measured: `u` = real fan / bar went from 0.81 to 4.22 before C.5 and from 0.77 to
    // 4.33 after; the scales fix did not move it, because it was not tied to anything.
    //
    // The curation is NOT lost: the initial trims come from dividing the curated bar by the derived one, so
    // the starting point is EXACTLY today's world. And they carry information that the level alone does not
    // have — prototypes come out narrow (CINDER_CHAMPIONSHIP_7571 0.59, DPI 0.61, MERROWDOWN_MYR373 0.64) and the old formulas
    // wide (Falcon_Series 2.49, Ember_Cup 2.12), which is true in reality.
    double talent_ref_trim = 0.0;
    // 24/08 — HERE WAS `curve_trim` (col 9), THE PER-CLASS CURVE TRIM, and it went away with the per-position
    // split. It was never written: the column did not exist in any of the 109 rows of the PSV, so the
    // 109 classes carried the whole range. The reader still tolerates 9-column rows (the
    // leftover is ignored), so an old PSV loads the same.
    bool   found = false;       // true if it came from a PSV row (not from the default)
};

bool LoadClassPace(const char* path);                 // false if it couldn't be opened; leaves the table empty
void ReplaceClassPace(std::map<std::string, ClassPace> m);
ClassPace ClassPaceFor(const std::string& classId);   // found=false + defaults if there's no row
// One-shot guarded load from paths::Data("career\class_pace_curve.psv"), in the style of SlugForTrackEnsured.
// Needed by the XML WRITER (WriteSeasonGridXml), which runs both after the overlay boots and from
// the offline generator and the test suite, with no single shared startup point. No-op if the table already has
// rows (an earlier explicit LoadClassPace/ReplaceClassPace call wins) and doesn't retry if the file is missing.
void EnsureClassPaceLoaded();
int  ClassPaceCount();                                // number of classes loaded (diagnostics/test)

// — THE DERIVED BAR of level `level` (1..7): how much talent separates the first from the
// last of a grid that the seeding places at that level. It is COMPUTED from the seeding constants
// (`LEVEL_SEED_MIDPOINT` ± `LEVEL_SEED_SPREAD`/2, run through `RatingQuality`), so if tomorrow the
// seeding is recalibrated, the bar follows it on its own. That is the whole point: a derived number does not
// fall out of sync; a hundred hand-curated ones do.
//
//   level  1      2      3      4      5      6      7
//   bar    0.176  0.217  0.274  0.306  0.299  0.256  0.217
double DerivedTalentRefSpan(int level);

// MEASURED ON 12/08, BEFORE WIDENING ANYTHING — AND IT TURNS OUT THERE IS NOTHING TO WIDEN HERE.
//
// The grid knot has five reverted attempts and the written conclusion was "the root is the bar":
// we deliver 4.6% of lap against the 10-12% on which Curve, [competitor] and [competitor] agree. Before
// touching the conversion we measured what OUR OWN DATA asks for — `spread_pct_dry` of the 100 classes of
// `class_pace_curve.psv`, which is the % of the last relative to the first in the dry:
//
//     min 1.6   ·   p10 2.2   ·   p50 3.9   ·   p90 7.0   ·   max 11.0
//
// We deliver ~4.6% against an own target whose MEDIAN is 3.9%. We are not falling short of our own
// data: we overshoot a little. The 3x gap is not between the bar and the target, it is between OUR
// target and that of the external references. They are two data sets that do not coincide, not a broken
// conversion.
//
// AND THAT IS THE MISSING DECISION, the one that avoids the sixth reverted attempt: widening the bar up to
// 10-12% would put the grids THREE TIMES above what our own curves say a real race of that
// category measures. That is not calibrating, it is changing source without saying so. The first thing is not
// "how much to widen" but **which of the two data sets rules** — and for that we need to know whether the 10-12% of the
// references measures lap time, like our `spread_pct_dry`, or measures the XML's `race_skill` adjustment,
// which is another magnitude.
//
// Until that is resolved, the bar is NOT touched. And `g_formPerturbsCapacity` () does not wait for a small
// widening: it waits for this decision.

// The EFFECTIVE bar of a class in a series of level `level`: the one derived by the class's curated trim.
// Without a trim it falls to `talent_ref_span` (the legacy column) and, with neither, to the global
// `kTalentRefSpan`. Empty or unknown `className` -> derived from the level.
//
// =====================================================================================================
// 24/08 — THE BAR LEAVES THE PRODUCTION PATH AND STAYS ALIVE OUTSIDE IT (task 5, plan "the gap")
// =====================================================================================================
//
// With the three-nail conversion, the WIDTH of the grid is set by the CLASS's fan and the extremes
// are set by the grid being written. The bar no longer divides any gap on its way to the XML. But it is NOT
// deleted blindly —it has a good dozen live uses outside production—, so they are enumerated and
// decided one by one, which is what the plan asked for:
//
//   RETIRED (they were the two XML writers, and they are THE change of this task):
//     · career_fields.cpp::WriteSeasonGridXml  — it passed `refSpan` to `BuildGridXml`. Out with the parameter.
//     · career_overlay.cpp::WriteChampGrid     — same, with `level = 0` because a Custom Race has no level.
//       That caller could only disappear WITH the parameter, not before: it was its only reason to exist.
//
//   KEPT, and why each one:
//     · career_canon.cpp — the `[canon-vara]` diagnostic. It measures `u = (ace - backmarker) / bar` per class, which is
//       the question "how much do these people truly spread apart, compared with what the seeding should give?".
//       It is still the measure that governs the of the seeding; it does not depend on how the XML is written.
//     · career_test.cpp::MedirVara / FotoVara — the instrument with which the grids of the simulated world are
//       measured over the seasons. It is the one task 4 of the plan uses; retiring it would leave the
//       measurement without apparatus.
//     · the tests that pin the DERIVATION by level and the precedence trim > legacy column > global. They pin the
//       contract of this function, not the writer's: they remain valid exactly the same.
//
//   And consequently `DerivedTalentRefSpan` ALSO SURVIVES (`[canon-vara]` uses it for its PURE derived
//   bar column, and four tests pin its per-level table), the PSV's `talent_ref_span` (7) and
//   `talent_ref_trim` (8) columns —which are what this function reads— and `bal::GRID_SPREAD_SCALE`, which remains
//   the only multiplier that goes through it.
//
// WHAT HAS CHANGED AND MUST BE SAID OUT LOUD: `GRID_SPREAD_SCALE` used to be "the grid's width knob".
// IT NO LONGER IS. Turning it today moves the bar with which the DIAGNOSTICS measure, and not a single XML number. The
// width knob is each class's fan in `class_pace_curve.psv`. See the block in career_balance.h.
double TalentRefSpanFor(const std::string& className, int level);

// Experiment 12/08: a single GLOBAL bar instead of one per class.
extern bool g_varaGlobal;

}  // namespace career
