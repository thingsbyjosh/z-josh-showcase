// appgate_wasm.cpp -- the app-presence gate for the WASM/browser build.
//
// career_appgate.cpp (dll/career/career_appgate.cpp) answers one question on Windows: is a
// companion application alive right now? It answers it with a named kernel event, because the
// engine ships as a file inside another program's folder and can be loaded with nobody watching.
// Neither half of that situation exists here.
//
// In a browser the engine is not a file somebody else's process may pick up by accident: it is a
// module the host page explicitly instantiated. There is no way for this code to run without the
// application that loaded it being alive -- the module and the application are the same tab. So
// the honest answer to "is the app alive?" is yes, unconditionally, and it is a true statement
// about this environment rather than a stub standing in for behaviour that went missing.
//
// This is a separate translation unit supplying the same symbols, never an #ifdef threaded through
// the original -- the same shape paths_wasm.cpp already established for paths.cpp. The build
// script (build_wasm.sh) excludes career_appgate.cpp and compiles this instead.
#include "career_appgate.h"

namespace career {

const char* AppGateName() { return ZTA_APP_GATE_NAME; }

// True for any well-formed name. There is no OS namespace of named objects to look a name up in,
// and nothing to look up: whoever is asking is running inside the page that owns the engine.
// The name is still validated so a caller passing an empty string gets the same "no" it would get
// on Windows, rather than a confident yes about a gate it never actually named.
bool AppGateIsOpen(const char* name) { return name && *name; }

bool AppIsAlive() { return AppGateIsOpen(AppGateName()); }

// Arm/disarm exist so the declared surface links, not because a browser has anything to arm. The
// sentinel is a non-null address, so a caller's `if (!handle)` failure check reads "armed" -- the
// truthful result, since the gate this handle would hold open is already open and cannot be shut.
// Deliberately not a heap allocation: nothing owns it, nothing frees it, and Disarm below must be
// safe to call with any handle a caller kept from an earlier build.
static char g_armSentinel = 0;

void* AppGateArm(const char* name) {
    if (!name || !*name) return nullptr;
    return &g_armSentinel;
}

void AppGateDisarm(void* /*handle*/) {
    // Nothing to release. Closing this gate would mean telling the engine the application is gone
    // while the page that IS the application keeps running -- a lie the Windows build can make
    // truthfully (its owner really can die) and this one cannot.
}

}  // namespace career
