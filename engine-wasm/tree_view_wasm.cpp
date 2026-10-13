// tree_view_wasm.cpp -- the browser host's provider for query.tree.
//
// PROVISIONAL -- SECOND IMPLEMENTATION OF THE TREE VIEW.
//
// The authoritative assembly is career_overlay.cpp's SetTreeViewProvider lambda (it lives in the
// career_api_wiring.inc the overlay includes, with the ladder bridge it calls), which this repository
// does not publish. What follows is a copy written so the browser build can answer query.tree at all,
// and copies drift: grep PROVISIONAL to find every one of them. The static_asserts below make a change
// to the view structures themselves loud.
//
// Nothing here is new logic. The header of the view -- frontier phase, reputation and its band and
// colour -- is read as the overlay reads it (prog.frontier_phase, prog.rep, RepDisplayBand,
// RatingTierColor). The serializer is the engine's own QueryTree (career_api_queries.cpp) and is not
// touched or repeated here.
//
// The NODES are the overlay's too: LadderNodesView (career_data.cpp), the flat copy of the UI's ::LADDER
// table that LoadLadder fills from career_ladder.psv in the data directory, with car counts from the
// car-class cache in the user root (wasm_main.cpp's BootWorld seeds it from the world) and the user's
// saved layout. Each node is copied with locked = (level > frontierPhase), as the overlay does.
#include "career_api_queries.h"
#include "career_engine.h"
#include "career_save.h"
#include "career_season_ui.h"   // LadderNodesView
#include <utility>

namespace {

// The guard that makes drift loud, for the same reason wasm_main.cpp guards HubView: TreeView is a plain
// serialization view, so a field added upstream would ship default-initialized through this demo without
// anything here having to mention it. Size, because C++17 cannot count members; exact for the one
// toolchain this file is built with (wasm32, libc++). When it fires: read the new TreeView (or TreeNode),
// fill the new field in MakeTreeView below, THEN update the number. Do not just update the number.
static_assert(sizeof(capi::TreeView) == 32,
              "capi::TreeView changed upstream. This is a SECOND, provisional copy of the overlay's tree "
              "assembly: fill the new/changed field in MakeTreeView below, THEN update this size.");
static_assert(sizeof(capi::TreeNode) == 56,
              "capi::TreeNode changed upstream. This is a SECOND, provisional copy of the overlay's tree "
              "assembly: fill the new/changed field in MakeTreeView below, THEN update this size.");

capi::CareerStateProvider g_state;

capi::TreeView MakeTreeView() {
    capi::TreeView v;
    if (!g_state) return v;
    const capi::CareerStateView st = g_state();
    if (!st.loaded || !st.save) return v;   // loaded=false -> the serializer says not_ready
    const career::SaveData& save = *st.save;
    v.loaded = true;
    v.frontierPhase = save.prog.frontier_phase;
    v.rep = save.prog.rep;   // REP, not prog.rating: the band is the reputation's
    v.ratingTier = career::RepDisplayBand(v.rep);
    v.ratingTierColor = (ImU32)career::RatingTierColor(v.rep);
    for (const career::LadderNodeView& n : career::LadderNodesView()) {
        capi::TreeNode t;
        t.id = n.id; t.display = n.display; t.disc = n.disc;
        t.level = n.level; t.carCount = n.carCount;
        t.isDlc = n.isDlc; t.vehid = n.vehid;
        t.locked = (n.level > v.frontierPhase);
        v.nodes.push_back(std::move(t));
    }
    return v;
}

}  // namespace

namespace wasmhost {

void InstallTreeView(capi::CareerStateProvider state) {
    g_state = std::move(state);
    capi::SetTreeViewProvider(&MakeTreeView);
}

}  // namespace wasmhost
