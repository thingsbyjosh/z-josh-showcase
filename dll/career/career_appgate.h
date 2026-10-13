// career_appgate.h - "is Career Engine alive?". The lock that governs the engine's ENTIRE startup.
//
// 07/09 - WHY IT EXISTS. The engine reaches AMS2 as a file in the game's own folder, and that file
// is put there by the app just before launching and taken back out when it closes. That removal is best-effort: if the app is
// killed, hangs, the machine loses power, or it closes with AMS2 still open (Windows will not delete a file that is
// in use), the file stays behind. From then on EVERY start of AMS2 -straight from Steam, with the app nowhere
// in sight- loaded the whole engine and regenerated the CustomAIDrivers XML files. It was reported by the developer
// of another mod that writes those same files: his users were finding them overwritten with Career Engine
// closed. He was right. The rule, from here on: with no app alive, the engine does NOTHING and stays what it
// claims to be: an inert file.
//
// WHY A KERNEL OBJECT AND NOT A FILE. A flag file would have exactly the same problem as
// the file above: it stays behind when its owner dies abruptly, which is precisely the case to cover. A
// named event is closed by Windows when the process dies, however it was killed. There is no state to
// clean up, nothing that can go stale, and nobody has to remember anything.
#pragma once

// The EXACT name of the object. It is DUPLICATED in app/src-tauri/src/appgate.rs because they are two languages; there
// a test on the Rust side that reads THIS line and compares the two literals. If they diverge, the lock stays
// shut forever and the mod silently stops doing anything on EVERY machine.
//
// NO PREFIX on purpose. A name with no "Global\" or "Local\" goes to the SESSION namespace, which is
// exactly where we want it (the game and the app always live in the same interactive session; "Global\"
// also demands a privilege an ordinary user does not have). And with no backslash in it, the literal
// survives intact through any layer that escapes strings between the two files that have to agree.
#define ZTA_APP_GATE_NAME "Career Engine.AppAlive.v1"

namespace career {

// The production name. It exists as a function so the harness can check the contract's literal.
const char* AppGateName();

// Is a Career Engine alive right now? The only question the engine's startup needs to ask.
bool AppIsAlive();

// The three below take the name on purpose: the harness arms its own, so the suite does not depend on whether
// a real app happens to be open on the machine it runs on.
bool AppGateIsOpen(const char* name);
void* AppGateArm(const char* name);   // returns the handle (nullptr on failure); keeping it alive = lock open
void AppGateDisarm(void* handle);

}  // namespace career
