#pragma once
// 02/08 -- GRID DATA STAMP.
//
// What it is: a tag that travels with every telemetry submission (`dataVersion`) saying which driver
// corpus and which pace curves that grid was built from.
//
// Why it exists: telemetry measures the player's advantage over the AI. That number is only comparable between
// races that ran against the SAME grid. The day the drivers change (names, teams, talent)
// or the `class_pace_curve.psv` curves change, the before-samples and after-samples stop being the same
// population, and an aggregate that mixes them reports a class offset that describes neither one.
// With the stamp, analysis segments on its own, and a driver-roster change goes from noise to a measurable experiment.
//
// WHEN TO BUMP IT: whenever a class' roster changes (docs/career/driver_profiles.psv, series_roster*.psv,
// the driver packs) or the columns of docs/career/class_pace_curve.psv change. It's the same moment you need to
// re-dump the talent span:
//
//     $env:ZTA_DUMP_SPAN='1'; .\build\career_test.exe | Select-String '^\[span\] ' > spans.txt
//     python tools/apply_talent_ref_span.py spans.txt
//
// Format: the date of the change plus a suffix if there's more than one on the same day. It's never parsed anywhere, it's grouped
// by equality, so any stable string works as long as it CHANGES whenever the data changes.

namespace career {

// 16/09/2026 (0.7.0 port): the corpus changed entirely — liveries and rosters per person, measured
// attributes, curated origin pool, economic scale of series and the driver cards. The samples from before
// and those from after are not the same population, so the seal goes up ONCE for the whole port.
constexpr const char* kContentVersion = "2026-09-16";

}   // namespace career
