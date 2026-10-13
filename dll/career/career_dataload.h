#pragma once
// career_dataload — the SHARED list for loading season data, in a
// NEW translation unit with no ImGui. It exists because `career_data.cpp` (the obvious home at first glance) drags in
// ImGui via career_state.h/theme.h and depends on Col::Disc (theme.cpp) — and the probe (tools/seatwatch_probe.cpp)
// does NOT link ImGui. This header declares ONLY the contract: what it loads, what it reports. Who wraps it with the
// mutex/latch (the overlay) and who decides whether an empty dataset is a problem (the caller —
// under a "report + caller's policy" design) is a responsibility that lives outside this
// file.
#include <string>
#include <vector>
#include "career_events.h"   // SpecialEventDef: the report carries the loaded registry (the overlay populates it)

namespace career {

// A report entry: a season dataset as it stood after the last load.
struct SeasonDataset {
    std::string name;       // the dataset's STABLE name (the contract in tools/fixtures/season_dataset_order.txt)
    std::string rel;        // path relative to its root, with forward slashes ('/'), NEVER absolute
    std::string root;       // "data" | "userdata" — which paths.h root resolved `rel`
    bool opened = false;    // the loader opened the file. For the 3 datasets that return a COUNT instead
                             // of a bool (special_events, audience, memory_lore), it isn't separately
                             // measurable — it's left as true (the loader ran) and the empty-dataset warning relies
                             // on `rows`/`has_count` instead, not this field.
    int rows = 0;            // rows loaded. rows = opened ? accessor() : 0: a
                             // mutilated data root that failed to open must not inherit the count from a previous
                             // load in the same process (the loaders' clear() runs AFTER the
                             // fopen call — measured).
    bool has_count = true;   // false ONLY for special_events: its count depends on time(nullptr) and
                             // would inject a non-deterministic value into an output that has to be byte-for-byte
                             // identical across runs (a fingerprint/baseline). A report with no measurable number
                             // for a dataset can NOT be called clean (see SeasonDataIsClean).
    std::string reason;      // An ANNOTATION (never an exemption) of why a zero is expected for this dataset.
                             // Only 4 of the 33 carry one: the two UserData caches (absent on a
                             // clean install), special_events (clock-dependent), and class_difficulty
                             // (absent from the data root today — a rule from the dda header itself: absent = zeros).
};

// Which loading branch to run. useJson=true replaces the first 12 PSV entries (series..series_econ) with
// a single "career_json" entry; the 21 common ones (authored_profiles..liveries) load the same way through both
// paths (deliberately outside the if/else in today's block).
struct SeasonDataOpts {
    bool useJson = false;
};

// The complete report from ONE run of LoadSeasonData.
struct SeasonDataReport {
    std::vector<SeasonDataset> datasets;          // IN load ORDER (contract: tools/fixtures/season_dataset_order.txt)
    std::vector<SpecialEventDef> special_events;  // events loaded by LoadSpecialEvents; the queryable REGISTRY
                                                   // (SetSpecialEventRegistry) is populated by the overlay using this, not by this function.
};

// Runs the season data load (the 33 calls that today live in the body of SeasonUiEnsureData,
// dll/career_overlay.cpp:10226-10360) and FILLS `report` with what it loaded, in what order, and with how many rows.
//
// What it deliberately does NOT do (under a "report + caller's policy" design):
//  Takes NO lock at all: the recursive mutex and the latch stay in the overlay, which wraps
//    this call exactly the way it wraps the block it replaces today.
//  Doesn't decide whether an empty dataset is a problem: that's for SeasonDataIsClean/SeasonDataWarnings to say, and it's
//    the CALLER who interprets it. The game, which will only call this function, gains NO
//    new branch — its graceful degradation in the face of a missing optional dataset stays intact by construction
//    (the empty-dataset warning never turns into a hard failure on the game's path).
//  Doesn't touch g_save, doesn't register the special event in any queryable registry, doesn't emit mail, doesn't
//    log, and never aborts.
void LoadSeasonData(const SeasonDataOpts& opts, SeasonDataReport& report);

// true if EVERY entry in the report came back with a measurable number that's ALSO greater than zero. A dataset with no
// measurable count (special_events) counts as "not clean", same as one with zero rows: the report can't
// claim everything is fine about something it couldn't even count.
bool SeasonDataIsClean(const SeasonDataReport& report);

// "" if the report is clean. Otherwise, one line per affected dataset: name + relative path + its
// reason annotation, when it has one. Composed by the PROBE for its highlighted warning; the game never calls it.
std::string SeasonDataWarnings(const SeasonDataReport& report);

// The data root's FINGERPRINT — a header with the branch that ran (inferred from the
// report itself: the JSON branch leaves "career_json" as the FIRST entry, the PSV branch leaves "series") and the
// dataset count; one line per dataset IN LOAD ORDER with name, labeled root, relative path
// (forward slashes, never absolute) and row count — or "NO-NUMBER" + a reason when `has_count` is false, so it
// can never be confused with a genuinely empty dataset; and a final clean/not-clean status line (the
// same criterion as SeasonDataIsClean). PURE: reads no disk, checks no clock, touches no global
// it just formats what `report` already carries. That's why it lives in the engine and not the probe: it's tested in the
// harness without launching the probe or the game. Formatting the SAME report twice gives EXACTLY the same
// string (a byte-for-byte reproducibility guarantee).
std::string FormatDataRootFingerprint(const SeasonDataReport& report);

} // namespace career
