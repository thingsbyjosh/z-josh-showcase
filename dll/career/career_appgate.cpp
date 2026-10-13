// career_appgate.cpp - the lock's implementation. See career_appgate.h for why it exists.
#include "career_appgate.h"
#include <windows.h>

namespace career {

const char* AppGateName() { return ZTA_APP_GATE_NAME; }

bool AppGateIsOpen(const char* name) {
    if (!name || !*name) return false;
    // ANSI variants on purpose: the name is ASCII and fixed at compile time, so there is nothing here
    // from the user's own tree that could bring accents in (which is where the mojibake came from).
    // SYNCHRONIZE is the MINIMUM right a named event can be opened with. Only that one is asked for because
    // the less you ask for, the fewer ways Windows has to refuse it if the app and the game do not run at
    // the same integrity level (e.g. Steam launched as administrator).
    HANDLE h = OpenEventA(SYNCHRONIZE, FALSE, name);
    if (!h) return false;
    CloseHandle(h);
    return true;
}

bool AppIsAlive() { return AppGateIsOpen(AppGateName()); }

void* AppGateArm(const char* name) {
    if (!name || !*name) return nullptr;
    // No security descriptor: this path is for the harness and for dev, inside a single process. The
    // one that arms it in production is the app (appgate.rs), which does give it an explicit descriptor so the
    // game can open it whatever integrity level it comes with.
    return (void*)CreateEventA(nullptr, TRUE, FALSE, name);
}

void AppGateDisarm(void* handle) {
    if (handle) CloseHandle((HANDLE)handle);
}

}  // namespace career
