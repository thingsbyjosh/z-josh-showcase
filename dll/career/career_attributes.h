#pragma once
#include <string>
#include <vector>

// ===================================================================================================
// THE ATTRIBUTE MODEL () — the calibration set
// ===================================================================================================
//
// gives each driver INDEPENDENT attributes instead of fourteen shadows of a single number. This
// file is its floor: the livery pack cards whose fifteen knobs were MEASURED against
// real results, not derived from a curve.
//
// WHY ONLY THOSE. Measurement that decides it is buildable (calibration set commit):
//
//                    measured (346)   from curve (1,981)   our genome ()
//     qualifying         +0,83            +0,98               +0,89
//     consistency        +0,46            +0,89               +0,67
//     tyre               +0,26            +0,88               +0,53
//     defending          +0,14            +0,90                 —
//
// In the curve ones everything is the shadow of pace, exactly like in our genome. In the measured
// ones the axes are truly independent and the dispersion is 2-4 times greater. An attribute model
// built on the curve ones would be the same number with seven names.
//
// IT IS NOT THE POPULATION, IT IS THE YARDSTICK. 346 anchors for ~3,177 identities: they cover 10-28%. They serve to
// calibrate what the numbers are about, not to populate the world — the derivation will do that.
namespace career {

// The fifteen AMS2 knobs, in the order of the pack catalog and the PSV. ALL are stored, raw: the
// mapping to the seven visible attributes () is a model decision, so changing it doesn't require
// touching the data
enum Knob {
    KNOB_RACE = 0, KNOB_QUAL, KNOB_AGGRESSION, KNOB_DEFENDING, KNOB_STAMINA, KNOB_CONSISTENCY,
    KNOB_STARTS, KNOB_WET, KNOB_TYRE, KNOB_FUEL, KNOB_BLUEFLAG, KNOB_WXTYRE, KNOB_RELIABILITY,
    KNOB_MISTAKES, KNOB_FORCED_MISTAKES, KNOB_COUNT
};

const char* KnobName(int k);   // the name AMS2 uses, for diagnostics and for writing the XML

// THREE OF THE FIFTEEN ARE POSITIONAL and reading them as absolute capability is the error that prohibits
// (AMS2 manual ): `race_skill` and `qualifying_skill` are relative to their pack's grid —the game
// remaps them with the difficulty slider— and `vehicle_reliability` is a ratio within the reliability band of
// ITS CLASS. That's why each anchor carries `src_classes`: without knowing which pack it comes from, the
// drift of ±0.05 between packs can't be corrected.
bool KnobIsPositional(int k);

// AND FOUR ARE NOT QUALITY, THEY ARE CHARACTER. The manual says it in so many words: `fuel_management` «is not
// better: it is a character», a high `blue_flag_conceding` means pulling aside WILLINGLY (not the opposite),
// `weather_tyre_changes` «isn't better either», and `aggression` is style. An average can only average axes
// where more is better, so these four are left out of it by construction.
bool KnobIsCharacter(int k);

// A person with the fifteen knobs measured: one row of docs/career/driver_attributes.psv.
struct DriverAttributes {
    std::string name, country;
    std::string evidence;              // avg(N) = average of N sources · perturb = a noisy measurement
    int         evidence_weight = 0;   // N; 1 for `perturb`; 0 = unmeasured (derived)
    std::string src_classes;           // the origin pack, to correct its drift
    std::string present_in;            // roster/text/authored/pack, or "none" = measured but doesn't run
    float       v[KNOB_COUNT];         // 0..1 · <0 = that knob wasn't on the card
    float       craft = 0.0f;          // their measured CRAFT, in standard deviations (see below)
    bool        craft_known = false;   // false = there weren't enough knobs to compute it

    DriverAttributes() { for (int k = 0; k < KNOB_COUNT; k++) v[k] = -1.0f; }
    bool has(int k) const { return k >= 0 && k < KNOB_COUNT && v[k] >= 0.0f; }
};

// ===================================================================================================
// THE TWO-FACTOR MODEL () — speed and CRAFT
// ===================================================================================================
//
// WHAT THE ANCHORS SAY THAT WE WOULD NOT HAVE GUESSED. After removing each knob's share of pace, the
// residuals do NOT come out independent: there is a second axis, and it is strong. Loadings measured on it:
//
//     avoidance_of_mistakes  0.87    stamina      0.71    defending    0.68    consistency  0.67
//     vehicle_reliability    0.66    starts       0.66    wet_skill    0.63    tyre         0.61
//     qualifying_skill       0.08   <-- the ONLY one that stays out
//
// That is: **speed and craft are two different axes**, qualifying belongs only to the first and everything
// else shares the second. Control run: craft varies WITHIN a pack as much as between packs (sd
// 0.652 versus 0.654), so it belongs to the person and not merely to the style of whoever authored the pack.
//
// WHY IT MATTERS SO MUCH. measured that in our genome the fourteen traits are shadows of talent. A
// second axis ORTHOGONAL to pace is exactly what makes possible what was promised to the user: two
// drivers with the same average and different skills. Without it, deriving would repeat the defect under another
// name.
//
// AND WHERE EACH ONE'S CRAFT COMES FROM. Measured: the authored archetype orders craft monotonically
// (`apex` +1.44 · `cargador` +0.65 · `rapido` +0.40 · `metronomo` +0.14 · `novato` −0.55) but with an internal sd of
// 0.82-1.25 — the noise is FIVE times the gap between neighbors. And the order gives the problem away: `apex` on top and
// `metronomo` mid-table is an order of LEVEL, not of style, the same defect that was already measured.
// Using it as the source of craft would tie craft to pace again, so it is NOT used: craft is drawn
// deterministically by identity, except at the anchors, where it is measured.
// THIS DERIVES NOBODY: it says WHICH the result has to be. What really produces each driver's fourteen knobs
// is `career_traits::TraitsFor`, and its constants are calibrated against this measurement. Having
// a second deriver here would again be two scales on the same grid, which is the failure this whole
// architecture exists to kill. It is computed LIVE when loading the anchors, so if the corpus grows the measurement
// moves and the test warns that the constants have gone stale.
struct AttrModel {
    bool  calibrated = false;      // false = no anchors; the fallback constants are used
    float mean_pace = 0.849f, sd_pace = 0.088f;
    float mean[KNOB_COUNT];        // measured mean of the knob
    float r_pace[KNOB_COUNT];      // its measured correlation with pace — the target of `career_traits`
    float beta[KNOB_COUNT];        // slope on pace (r · sd/sd_pace)
    float sd_resid[KNOB_COUNT];    // dispersion left after removing pace
    float craft_load[KNOB_COUNT];  // how much of that residual is CRAFT (the rest belongs to the axis itself)
};

const AttrModel& AttributeModel();

// A person's CRAFT in standard deviations: MEASURED if they are an anchor, drawn deterministically by their
// identity if not. Stable across runs and across machines, and with no cost in the save.
//
// It is the slot through which the 346 enter the SAME model as everyone else. Using their raw knobs for them and
// the model for the rest would put two scales on one grid: their measured values are relative to THEIR pack,
// not to the class where they run here.
float CraftFor(const std::string& driverId, const std::string& name);

// The MEASURED craft, and only if there is one: true + `out` filled for the anchors, false for everyone else.
// It exists separately from `CraftFor` for a concrete reason: whoever paints the grid must only REPLACE the
// genome's draw when there is a measurement. Using the drawn value from `CraftFor` would change the traits of the
// ~3,000 people we have no measurements for, gaining nothing — the genome already draws them, and well.
bool MeasuredCraftFor(const std::string& driverId, const std::string& name, float& out);

// Load (clears its table; false if it doesn't open) ----
bool LoadDriverAttributes(const char* path);   // docs/career/driver_attributes.psv
int  DriverAttributeCount();                   // number of anchors loaded (diag/test)

// A person's anchor, or nullptr if we don't have them measured.
const DriverAttributes* AttributesForId(const std::string& driverId);
const DriverAttributes* AttributesForName(const std::string& name);
// Walk the whole set (diag/test): index in [0, DriverAttributeCount()).
const DriverAttributes* AttributesForIndex(int i);

// THE ONE CONSUMERS MUST USE. The pool is matched by EXACT string, so «Aurélien Panis» and
// «Aurelien Panis» are two people to it, and the fallback id is a hash of the RAW name: the crack
// propagates. Measured on the composed grid, 18 rows where the name finds the anchor and the id doesn't — in
// BOTH directions, meaning the engine already splits some person into two identities today. Everyone who
// has an id also has a name at hand (`GridDriver` carries both), so the combined lookup
// costs nothing and doesn't lose an anchor over an accent.
const DriverAttributes* AttributesFor(const std::string& driverId, const std::string& name);

// Port to 0.7.0: the SEVEN VISIBLES of lived here (the `VisibleAttr` enum), their projection from
// the fifteen knobs (`VisibleAttrName`, `KnobForVisible`, `VisibleValue`), the 0-100 scale with floor 50 facing
// the player (`ToPlayerScale`) and the CLASS WEIGHTING of (`ClassWeights`, `WeightsForClass`,
// `BaseClassWeight`, `ClassWeightedMedia`). They were removed with no consumer: in f0 only their tests read them, the
// D1 card (`career_card`) and the contracts (`career_plaza`), and neither of the two comes in. If the card returns,
// they return with it (f0 bf1423c8, career_attributes.{h,cpp}).
// And with them returns a decision by Josh from 09/09 (f0 60c24c65, /) that lived there as a
// comment next to `kVisibleNames` and has nowhere to go here: the SEVENTH visible is SHOWN as «Finishing
// races» (EN «Finishing»), not as «reliability». For the player it is the fraction of races they finish and for
// the AI what its reliability knob produces; every abandon counts and mechanical breakage isn't separated. The internal
// key remains `reliability`: the data, the JSON and the tests don't change. The UI label
// comes in with the card screen.

} // namespace career
