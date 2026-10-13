#include "career_api_core.h"
#include "json_write.h"
#include <chrono>
#include "career_userlog.h"   // support trace — EVERY app cmd, plus errors, ends up in the user's log

namespace capi {

// extracts the raw params from the request (for a readable trace in the support log). Truncates to 200
// chars and flattens newlines (one action = one line). NEVER used for query.auth (it would carry the token);
// the caller only fills this in for kind=="cmd" (login is kind=="query").
static std::string ParamsForTrace(const std::string& raw) {
    size_t pp = raw.find("\"params\"");
    if (pp == std::string::npos) return std::string();
    std::string s = raw.substr(pp + 8);
    size_t b = s.find_first_not_of(" \t:");
    if (b == std::string::npos) return std::string();
    s.erase(0, b);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    if (!s.empty() && s.back() == '}') s.pop_back();          // closing brace of the ENVELOPE, not of params
    for (char& c : s) if (c == '\r' || c == '\n') c = ' ';
    if (s.size() > 200) { s.resize(200); s += "..."; }
    return s;
}

void ApiCore::Register(const std::string& method, ApiHandler h) {
    handlers_[method] = std::move(h);
}

void ApiCore::SetAuthToken(const std::string& token) { authToken_ = token; authed_.store(false); }
void ApiCore::ResetAuth() { authed_.store(false); }

void ApiCore::OnRaw(const std::string& raw) {
    Queued q;
    std::string err;
    jsonm::ValuePtr root = jsonm::Parse(raw, &err);
    if (!root || !root->isObject()) {
        q.errCode = "bad_params";
        q.errMsg = "unparseable request: " + err;
    } else {
        q.id = root->memberString("id");
        q.kind = root->memberString("kind");
        q.method = root->memberString("method");
        q.root = root;
        if (root->memberInt("v", 0) != 1) {
            q.errCode = "bad_params"; q.errMsg = "unsupported protocol version";
        } else if (q.id.empty()) {
            q.errCode = "bad_params"; q.errMsg = "missing id";
        } else if (q.kind != "query" && q.kind != "cmd") {
            q.errCode = "bad_params"; q.errMsg = "kind must be query|cmd";
        } else if (q.method.empty()) {
            q.errCode = "bad_method"; q.errMsg = "missing method";
        }
        // only cmds carry a params trace (query.auth is kind "query" -> the token is never logged)
        if (q.errCode.empty() && q.kind == "cmd") q.paramsTrace = ParamsForTrace(raw);
    }
    std::lock_guard<std::mutex> lk(mx_);
    queue_.push_back(std::move(q));
}

std::map<std::string, ApiCore::MethodStat> ApiCore::TakeMethodStats() {
    std::map<std::string, MethodStat> out;
    out.swap(stats_);
    return out;
}

void ApiCore::SetSlowSink(std::function<void(const std::string&, double, size_t)> sink, double thresholdMs) {
    slowSink_ = std::move(sink);
    slowMs_ = thresholdMs;
}

bool ApiCore::HasQueuedTimeWork() {
    std::lock_guard<std::mutex> lk(mx_);
    for (const auto& q : queue_) {
        if (!q.errCode.empty()) continue;
        if (q.kind == "cmd" && q.method.compare(0, 9, "cmd.time.") == 0) return true;
        if (q.method == "query.time.preview") {
            const jsonm::Value* params = q.root ? q.root->member("params") : nullptr;
            if (!(params && params->memberBool("resolveOnly", false))) return true;
        }
    }
    return false;
}

void ApiCore::Pump(const std::function<void(const std::string&)>& send, int maxRequests) {
    for (int i = 0; i < maxRequests; i++) {
        Queued q;
        {
            std::lock_guard<std::mutex> lk(mx_);
            if (queue_.empty()) return;
            q = std::move(queue_.front());
            queue_.erase(queue_.begin());
        }
        if (!q.errCode.empty()) {
            career::UserLog("error", "invalid request from the app: %s (%s)", q.errCode.c_str(), q.errMsg.c_str());   //
            send(MakeError(q.id, q.errCode, q.errMsg));
            continue;
        }
        // auth: query.auth (login) is handled here (it isn't a registered handler).
        if (q.method == "query.auth") {
            const jsonm::Value* params = q.root ? q.root->member("params") : nullptr;
            std::string tok = params ? params->memberString("token") : std::string();
            if (authToken_.empty() || tok == authToken_) {
                authed_.store(true);
                send(MakeResult(q.id, "{\"authed\":true}"));
            } else {
                send(MakeError(q.id, "unauthorized", "bad token"));
            }
            continue;
        }
        // With a token configured and not yet authenticated, only api_info gets through (version negotiation).
        if (!authToken_.empty() && !authed_.load() && q.method != "query.api_info") {
            send(MakeError(q.id, "unauthorized", "auth required"));
            continue;
        }
        auto it = handlers_.find(q.method);
        if (it == handlers_.end()) {
            career::UserLog("error", "unknown method: %s", q.method.c_str());   //
            send(MakeError(q.id, "bad_method", "unknown method: " + q.method));
            continue;
        }
        const jsonm::Value* params = q.root ? q.root->member("params") : nullptr;
        auto t0 = std::chrono::steady_clock::now();
        HandlerResult r = it->second(params);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        {
            MethodStat& st = stats_[q.method];
            st.n++; st.sumMs += ms;
            if (ms > st.maxMs) st.maxMs = ms;
            if (r.payload.size() > st.maxBytes) st.maxBytes = r.payload.size();
        }
        if (slowSink_ && ms >= slowMs_) slowSink_(q.method, ms, r.payload.size());
        // support trace: EVERY cmd the app runs ends up in the user's log ("what it did and
        // when"). cmd.log.note is excluded (its content IS already the line its handler writes). Errors
        // are logged for BOTH kinds (a query failing in a loop is also diagnostic).
        if (q.kind == "cmd" && q.method != "cmd.log.note")
            career::UserLog("app", "%s %s%s", q.method.c_str(), q.paramsTrace.c_str(), r.ok ? "" : " -> ERROR");
        // 18/09: an abandoned forecast (`superseded`) is expected, not a failure: it does not dirty the log
        if (!r.ok && r.code != "superseded")
            career::UserLog("error", "%s code=%s msg=%s", q.method.c_str(),
                            r.code.empty() ? "internal" : r.code.c_str(), r.message.c_str());
        send(r.ok ? MakeResult(q.id, r.payload)
                  : MakeError(q.id, r.code.empty() ? "internal" : r.code, r.message));
    }
}

std::string ApiCore::MakeResult(const std::string& id, const std::string& payloadJson) {
    jsonw::Writer w;
    w.BeginObject();
    w.KI("v", 1);
    w.KS("id", id);
    w.KS("kind", "result");
    w.KRaw("payload", payloadJson.empty() ? "{}" : payloadJson);
    w.EndObject();
    return w.Str();
}

std::string ApiCore::MakeError(const std::string& id, const std::string& code, const std::string& msg) {
    jsonw::Writer w;
    w.BeginObject();
    w.KI("v", 1);
    w.KS("id", id);
    w.KS("kind", "error");
    w.KS("code", code);
    w.KS("message", msg);
    w.EndObject();
    return w.Str();
}

std::string ApiCore::MakeEvent(const std::string& method, const std::string& payloadJson) {
    jsonw::Writer w;
    w.BeginObject();
    w.KI("v", 1);
    w.KS("kind", "event");
    w.KS("method", method);
    w.KRaw("payload", payloadJson.empty() ? "{}" : payloadJson);
    w.EndObject();
    return w.Str();
}

} // namespace capi
