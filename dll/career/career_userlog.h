// career_userlog -- unified SUPPORT log for the end user.
// %LOCALAPPDATA%\Career Engine\logs\zta_support.log — the complete record of the session: what the
// user did and when (app commands, races, money, difficulty, connections). Lives in logs\ alongside
// zta_engine.log (engine) -- feedback 2026-07-20: ONE folder; Settings' "View log" button opens it and
// the user shares both files at once.
// Portable DLL + career_test (same fopen/append pattern as EconLog); never breaks the engine.
#pragma once
#include <string>   // rebrand 2026-07-20: EngineLogPath/RotateIfHuge take std::string
#include <vector>

namespace career {

// Appends a "[YYYY-MM-DD HH:MM:SS.mmm] [cat] message" line to the support log. Thread-safe (mutex);
// silent if the file isn't writable. Rotation: if it exceeds 5MB on the process's first use,
// it starts fresh (months of history; never grows unbounded).
void UserLog(const char* cat, const char* fmt, ...);

// "[YYYY-MM-DD HH:MM:SS.mmm]" stamp with milliseconds (2026-07-22). Shared by UserLog and the overlay's
// LogF so support<->engine<->app can be correlated by time.
void LogStampNow(char* out, size_t cap);

// Path relative to userdata (for tests and anyone who needs to locate it).
const char* UserLogRelPath();

// Feedback 2026-07-20: one-shot move support\zta_support.log -> logs\ preserving history (testable).
void UserLogMigrateLegacy(const std::string& userRoot);

// Rebrand 2026-07-20: the ENGINE log (diagnostics/decisions; previously logs\ams2_overlay.log with NO ceiling).
// EngineLogPath resolves the path ONCE per process, and on that first call it deletes the legacy
// ams2_overlay.log and rotates if it exceeds the ceiling (8MB). Shared by LogF (overlay), EconLog and MktLogF.
const std::string& EngineLogPath();
const char* EngineLogRelPath();                                       // "logs\\zta_engine.log"
bool EngineLogRotateIfHuge(const std::string& path, long maxBytes);   // true if it rotated (testable)
void EngineLogCleanupLegacy(const std::string& logsDir);              // deletes <dir>\ams2_overlay.log (testable)

// 0.7.0 review (finding 7): the clock forecast runs the world on COPIES, and what it would write to the
// logs (not presented, insurance premium, early renewal, negotiation threads) has NOT happened. While
// a LogCapture is alive on this thread, UserLog and the engine logs (EconLog, MktLogF, NegoLogF) keep the line
// here instead of writing it. If the forecast is later adopted as a real advance, LogReplay writes them then;
// if not, they're thrown away with it. Nestable: the inner capture covers the outer one while it lasts.
struct CapturedLogLine {
    bool support = false;   // true = support log (UserLog: cat + text); false = engine log (line as is)
    std::string cat;
    std::string text;
};
class LogCapture {
public:
    LogCapture();
    ~LogCapture();
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
    std::vector<CapturedLogLine> Take();   // what's been captured so far (the capture stays active)
private:
    std::vector<CapturedLogLine> lines_;
    std::vector<CapturedLogLine>* prev_;
};
bool LogCaptureActive();                                          // there's a live LogCapture on this thread
void LogCaptureEngineLine(const std::string& line);               // stores it in the active capture (doesn't write)
void LogReplay(const std::vector<CapturedLogLine>& lines);        // writes what's been captured now

} // namespace career
