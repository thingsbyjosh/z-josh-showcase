// owned_wasm.cpp -- the owned-car accessors for the WASM/browser build.
//
// career_data.cpp (the UI's class/car tables and the loaders behind the tree and the garage) asks the
// host four questions about the cars the player owns: how many, and for car i its class id, its
// vehicle hash and its saved livery. career_ui.h declares them; the game-side host defines them over
// its own loaded save, and that host is not part of this repository. So the browser build answers
// them here, and the answers are real ones: they come from the save this page loaded, read through the
// same career-state seat every engine-wasm/<view>_view_wasm.cpp provider uses (wasm_main.cpp installs
// it from Core()), never from wasm_main.cpp's globals.
//
// Same behaviour as the game-side definitions: out-of-range index -> "" / 0 / -1, and no save loaded
// reads as owning nothing. A new career owns no car (the engine grants none at creation), so in the
// demo as shipped CareerOwnedCount() is 0 until something in the save says otherwise.
//
// This is a separate translation unit supplying the declared symbols, never an #ifdef threaded
// through the engine -- the same shape appgate_wasm.cpp and vr_mirror_wasm.cpp already have. The build
// script (build_wasm.sh) lists it by name; it is not a view, so the view glob does not reach it.
#include "career_api_queries.h"
#include "career_save.h"
#include "career_ui.h"
#include <utility>

namespace {

capi::CareerStateProvider g_state;

// The save the seat currently points at, or null when there is none to read.
const career::SaveData* Save() {
    if (!g_state) return nullptr;
    const capi::CareerStateView st = g_state();
    return st.loaded ? st.save : nullptr;
}

const career::CarOwnership* Owned(int i) {
    const career::SaveData* save = Save();
    if (!save || i < 0 || i >= (int)save->owned.size()) return nullptr;
    return &save->owned[i];
}

}  // namespace

namespace wasmhost {

void InstallOwnedCars(capi::CareerStateProvider state) { g_state = std::move(state); }

}  // namespace wasmhost

int CareerOwnedCount() {
    const career::SaveData* save = Save();
    return save ? (int)save->owned.size() : 0;
}

// The returned pointer is into the save itself, exactly as the game-side host returns it: valid until
// the save's owned list next changes.
const char* CareerOwnedClassId(int i) {
    const career::CarOwnership* c = Owned(i);
    return c ? c->class_id.c_str() : "";
}

int CareerOwnedVehId(int i) {
    const career::CarOwnership* c = Owned(i);
    return c ? c->vehid : 0;
}

int CareerOwnedLivery(int i) {
    const career::CarOwnership* c = Owned(i);
    return c ? c->livery : -1;
}
