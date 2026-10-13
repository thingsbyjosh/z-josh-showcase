#include "career_userlog.h"
#include "paths.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // GetLocalTime (millisecond timestamp)
#endif

namespace career {

// file ceiling: on the process's first use, if it's exceeded the file rotates (deleted and started fresh,
// leaving a record). ~80 bytes/line -> 5MB ~ 65k lines ~ months of play.
static const long kUserLogMaxBytes = 5 * 1024 * 1024;

// Feedback 2026-07-20: support and engine logs live in ONE folder (logs\) -- Settings' "View log" button
// opens logs\ and the user shares BOTH files (zta_support.log + zta_engine.log) at once.
const char* UserLogRelPath() { return "logs\\zta_support.log"; }

// One-shot move support\zta_support.log -> logs\zta_support.log preserving history (rename;
// if the destination already exists the legacy file is left orphaned, never overwritten). Removes support\ if it's left empty.
// mojibake fix 2026-07-22: all I/O goes through paths:: (UTF-8 paths -> wide APIs).
void UserLogMigrateLegacy(const std::string& userRoot) {
    std::string oldP = userRoot + "\\support\\zta_support.log";
    if (!paths::FileExists(oldP)) return;   // no legacy: no-op
    std::string newP = userRoot + "\\" + UserLogRelPath();
    paths::EnsureParentDir(newP);
    if (paths::RenameFile(oldP, newP))
        paths::RemoveDirIfEmpty(userRoot + "\\support");   // best-effort: only removed if it ended up empty
}

// resolved ONCE per process; the move and rotation are decided here (under UserLog's mutex)
static std::string UserLogInitPath(bool& rotated) {
    std::string p = paths::UserData(UserLogRelPath());
    paths::EnsureParentDir(p);
    UserLogMigrateLegacy(paths::UserData(""));
    long long sz = paths::FileSize(p);
    if (sz > kUserLogMaxBytes) rotated = paths::RemoveFile(p);
    return p;
}

// Rebrand 2026-07-20: the ENGINE log (diagnostics/decisions) ------------------------------------------
// Before: logs\ams2_overlay.log with NO ceiling (seen at 154MB on a real machine). Now: logs\zta_engine.log
// with a ceiling (higher than support's: the engine is far more verbose). Separate, pure helpers so
// career_test can cover them without depending on the static init (once per process).

static const long kEngineLogMaxBytes = 8 * 1024 * 1024;

const char* EngineLogRelPath() { return "logs\\zta_engine.log"; }

bool EngineLogRotateIfHuge(const std::string& path, long maxBytes) {
    long long sz = paths::FileSize(path);
    if (sz < 0 || sz <= maxBytes) return false;
    paths::RemoveFile(path);
    FILE* f = paths::FOpen(path, "wb");
    if (f) { fprintf(f, "(log rotated: exceeded %ld MB)\r\n", maxBytes / (1024 * 1024)); fclose(f); }
    return true;
}

void EngineLogCleanupLegacy(const std::string& logsDir) {
    paths::RemoveFile(logsDir + "\\ams2_overlay.log");
}

const std::string& EngineLogPath() {
    // magic static: the first thread to log does the prep work (rotation + legacy cleanup) once
    static std::string p = []{
        std::string path = paths::UserData(EngineLogRelPath());
        paths::EnsureParentDir(path);
        size_t cut = path.find_last_of('\\');
        if (cut != std::string::npos) EngineLogCleanupLegacy(path.substr(0, cut));
        EngineLogRotateIfHuge(path, kEngineLogMaxBytes);
        return path;
    }();
    return p;
}

// Timestamp "[YYYY-MM-DD HH:MM:SS.mmm]" -- with milliseconds (2026-07-22: to correlate support<->engine<->app).
void LogStampNow(char* out, size_t cap) {
#ifdef _WIN32
    SYSTEMTIME st{};
    GetLocalTime(&st);
    snprintf(out, cap, "[%04d-%02d-%02d %02d:%02d:%02d.%03d]",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
    time_t t = time(nullptr); struct tm tmv {};
    localtime_r(&t, &tmv);
    snprintf(out, cap, "[%04d-%02d-%02d %02d:%02d:%02d.000]", tmv.tm_year + 1900, tmv.tm_mon + 1,
             tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
#endif
}

namespace {
thread_local std::vector<CapturedLogLine>* t_logCapture = nullptr;
}
LogCapture::LogCapture() : prev_(t_logCapture) { t_logCapture = &lines_; }
LogCapture::~LogCapture() { t_logCapture = prev_; }
std::vector<CapturedLogLine> LogCapture::Take() { std::vector<CapturedLogLine> out; out.swap(lines_); return out; }
bool LogCaptureActive() { return t_logCapture != nullptr; }
void LogCaptureEngineLine(const std::string& line) {
    if (!t_logCapture) return;
    CapturedLogLine l; l.support = false; l.text = line;
    t_logCapture->push_back(std::move(l));
}
void LogReplay(const std::vector<CapturedLogLine>& lines) {
    for (const auto& l : lines) {
        if (l.support) { UserLog(l.cat.c_str(), "%s", l.text.c_str()); continue; }
        if (t_logCapture) { t_logCapture->push_back(l); continue; }
        FILE* f = paths::FOpen(EngineLogPath(), "a");
        if (!f) continue;
        fputs(l.text.c_str(), f);
        fclose(f);
    }
}

void UserLog(const char* cat, const char* fmt, ...) {
    if (t_logCapture) {   // finding 7: clock forecast -> it's stored, not written
        CapturedLogLine l; l.support = true; l.cat = cat ? cat : "";
        va_list ap; va_start(ap, fmt);
        va_list cp; va_copy(cp, ap);
        const int n = vsnprintf(nullptr, 0, fmt, cp);
        va_end(cp);
        if (n > 0) { l.text.resize((size_t)n + 1); vsnprintf(&l.text[0], (size_t)n + 1, fmt, ap); l.text.resize((size_t)n); }
        va_end(ap);
        t_logCapture->push_back(std::move(l));
        return;
    }
    static std::mutex mx;
    std::lock_guard<std::mutex> lk(mx);
    static bool rotated = false;
    static std::string path = UserLogInitPath(rotated);
    FILE* f = paths::FOpen(path.c_str(), "a");
    if (!f) return;
    char ts[40];
    LogStampNow(ts, sizeof(ts));
    if (rotated) {
        rotated = false;
        // NOTE: fopen "a" = TEXT mode -> '\n' already comes out as CRLF (a literal \r\n would duplicate the \r)
        fprintf(f, "%s [log] rotated (file exceeded the size cap)\n", ts);
    }
    fprintf(f, "%s [%s] ", ts, cat && *cat ? cat : "-");
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputs("\n", f);
    fclose(f);
}

} // namespace career
