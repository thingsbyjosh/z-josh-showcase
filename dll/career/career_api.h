// career_api — singleton that ties together WsServer (transport) + ApiCore (dispatch).
// Used from the overlay (career_overlay.cpp), behind a config flag:
//   startup:  capi::StartServer(port)            [once]
//   per frame: capi::PumpGameThread()             [on the game thread]
//   push:      capi::Emit("event.x", payloadJson) [any thread]
// Flag config: %LOCALAPPDATA%\Career Engine\config.json  {"api_enabled":true,"api_port":47123}
// (same source of truth the external app will use; see the PathProvider design).
#pragma once
#include <cstdint>
#include <string>
#include "career_api_core.h"

namespace capi {

ApiCore& Core();   // for registering handlers (RegisterCareerQueries etc.)

// Registers the builtin methods (query.api_info) on a given core (testable).
void RegisterBuiltins(ApiCore& core);

// idempotent; false if the port couldn't be opened. token: local auth; "" = no auth (dev).
bool StartServer(uint16_t port, const std::string& token = "");
// Jul 22: WSA code from the last StartServer failure (0 = no failure). 10048=WSAEADDRINUSE
// (typically another game instance with the DLL still alive is listening on the port).
int StartServerError();
void StopServer();
void PumpGameThread();
void Emit(const std::string& method, const std::string& payloadJson);
bool HasClient();
uint16_t ServerPort();

// The overlay sets this to true on startup inside the game; in career_test it stays false.
void SetGameAttached(bool attached);

#ifdef ZTA_FAKEGAME
// Only in the AMS2 double's build (tools/build_fakegame.ps1): who hosts the engine. Travels in
// query.api_info as "host":"double" and the app stops launching AMS2. The published DLL does not compile it and does not
// send the field, and the app treats its absence as the game. The engine does not query it for anything.
void SetHostName(const std::string& name);
#endif

} // namespace capi
