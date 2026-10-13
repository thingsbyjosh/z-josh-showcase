// career_api_core — JSON-RPC dispatch for career_api, decoupled from the transport.
// Protocol v1 (see the external-app architecture design):
//   req : {"v":1,"id":"...","kind":"query"|"cmd","method":"query.x","params":{...}}
//   resp: {"v":1,"id":"...","kind":"result","payload":{...}}
//       | {"v":1,"id":"...","kind":"error","code":"...","message":"..."}
//   push: {"v":1,"kind":"event","method":"event.x","payload":{...}}
//
// THREADING RULE (v2, VR, Jul 24): OnRaw() runs on the WS thread (only parses and queues). Pump() runs the
// handlers and has TWO mutually exclusive callers under the overlay's g_apiPumpBusy lock: the flat-screen frame tick
// and the VR menu loop (used when the flat-screen tick doesn't fire). The per-frame race tick (CareerFrameTick)
// runs under the SAME lock, so handlers and the tick never interleave across two threads.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "json_min.h"

namespace capi {

struct HandlerResult {
    bool ok = true;
    std::string payload = "{}";     // JSON payload if ok
    std::string code;               // bad_method|bad_params|not_ready|internal if !ok
    std::string message;
};

// params can be nullptr (request without params)
using ApiHandler = std::function<HandlerResult(const jsonm::Value* params)>;

class ApiCore {
public:
    void Register(const std::string& method, ApiHandler h);

    // WS thread: parses the raw text and queues it. Never runs handlers.
    void OnRaw(const std::string& raw);
    // 18/09: is there clock work waiting in the queue that makes a forecast in progress obsolete? That is, a
    // `cmd.time.*` (the real advance, a rules change) or a full `query.time.preview` (not
    // `resolveOnly`, which does not simulate). Called from the game thread while it simulates; takes the same mutex
    // as OnRaw (network thread), and the queue has only a few elements.
    bool HasQueuedTimeWork();

    // Game thread: drains up to maxRequests, runs handlers, and emits responses
    // via 'send'. Kept short so it doesn't introduce frame hitches.
    void Pump(const std::function<void(const std::string&)>& send, int maxRequests = 2);

    // 22/09 VR STUTTER: cost of each request served by Pump, measured on the game thread. The [slowtick]
    // said `api=12/23` every ~0.8 s with the app alive but not WHICH method. All of this is touched only by Pump and
    // its host (same g_apiPumpBusy lock), so it carries no mutex.
    struct MethodStat { long n = 0; double sumMs = 0, maxMs = 0; size_t maxBytes = 0; };
    // Returns what has accumulated since the last call and resets it to zero (the caller's window).
    std::map<std::string, MethodStat> TakeMethodStats();
    // Immediate warning of a request that exceeds thresholdMs (method, ms, bytes of the response).
    void SetSlowSink(std::function<void(const std::string&, double, size_t)> sink, double thresholdMs);

    // Builds a push event ready to send.
    static std::string MakeEvent(const std::string& method, const std::string& payloadJson);

    // auth: local token (set before Start). "" = no auth (dev). With a token set, every request
    // except query.api_info and query.auth gets "unauthorized" until a correct query.auth{token} call.
    void SetAuthToken(const std::string& token);
    // New connection -> must re-authenticate. Called from the WS thread (authed_ is atomic).
    void ResetAuth();

private:
    struct Queued {
        std::string id;
        std::string kind;      // "query"|"cmd"
        std::string method;
        jsonm::ValuePtr root;  // full parsed request (keeps params alive)
        std::string errCode;   // if parsing/validation already failed, a direct response
        std::string errMsg;
        std::string paramsTrace;   // raw params truncated for the support log (only kind=="cmd")
    };
    static std::string MakeResult(const std::string& id, const std::string& payloadJson);
    static std::string MakeError(const std::string& id, const std::string& code, const std::string& msg);

    std::map<std::string, ApiHandler> handlers_;   // only mutated during Register (init)
    std::mutex mx_;
    std::vector<Queued> queue_;
    std::string authToken_;                        // set before Start (no race in progress)
    std::atomic<bool> authed_{false};              // ResetAuth (WS thread) <-> Pump (game thread)
    std::map<std::string, MethodStat> stats_;      // game thread (see TakeMethodStats)
    std::function<void(const std::string&, double, size_t)> slowSink_;
    double slowMs_ = 0;
};

} // namespace capi
