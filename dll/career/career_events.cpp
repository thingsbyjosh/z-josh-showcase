// Special events -- see career_events.h. A PURE module (no network, no game
// globals). The registry replaces the old inject-into-the-calendar mechanism: an event is
// looked up by id and assembled directly onto the session, without cloning series or adding rounds.
#include "career_events.h"
#include "career_customai.h"    // TrackHashForSlug (the inverse of the curated hash->slug catalog)
#include "career_fields.h"      // EventGridSizeFor: MeetingGridSize / GridSizeForSeries / MaxGridSize / SeriesForClassName
#include "career_userlog.h"     // Warn when an event's base_profile doesn't exist (it used to degrade silently)
#include "json_min.h"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>      // 01/10: gmtime_s (EventDefaultDate)
#include <deque>

namespace career {

long long ParseIso8601Utc(const std::string& iso) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) != 6) return -1;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60) return -1;
    // days-from-civil (Howard Hinnant): days since 1970-01-01, proleptic Gregorian. No timezone, no DST.
    long long yy  = y - (mo <= 2 ? 1 : 0);
    long long era = (yy >= 0 ? yy : yy - 399) / 400;
    long long yoe = yy - era * 400;
    long long doy = (153LL * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

std::vector<SpecialEventDef> LoadSpecialEvents(const char* path, long long nowEpochSec) {
    std::vector<SpecialEventDef> out;
    std::string err;
    jsonm::ValuePtr root = jsonm::ParseFile(path, &err);
    if (!root || !root->isObject()) return out;                     // absent/corrupt -> empty (non-fatal)
    // The SERVER's clock, not the PC's: an event's window is the SAME for everyone, so what
    // governs it is the time the backend gave at sync (the serverNow field), not a local clock that could be
    // set wrong. The app stores both timestamps from the moment it fetched; the difference is this
    // machine's own offset, and it's added onto the local clock (so the event keeps progressing offline, without asking again).
    // Old file / missing fields -> offset 0 = the previous behavior.
    long long skewSec = 0;
    {
        long long srv = ParseIso8601Utc(root->memberString("serverNow"));
        long long loc = ParseIso8601Utc(root->memberString("fetchedAt"));
        if (srv >= 0 && loc >= 0) skewSec = srv - loc;
    }
    nowEpochSec += skewSec;
    const jsonm::Value* evs = root->member("events");
    if (!evs || !evs->isArray()) return out;
    for (const auto& e : evs->arr) {
        if (!e || !e->isObject()) continue;
        const jsonm::Value* pay = e->member("payload");
        if (!pay || !pay->isObject()) continue;                     // teaser-only: nothing to play (the app renders the paywall)
        SpecialEventDef d;
        d.id        = e->memberString("id");
        d.tier_min  = e->memberString("tier_min", "free");
        d.starts_at = ParseIso8601Utc(e->memberString("starts_at"));
        d.ends_at   = ParseIso8601Utc(e->memberString("ends_at"));
        if (const jsonm::Value* t = e->member("teaser")) {
            d.title_en = t->memberString("title_en");
            d.title_es = t->memberString("title_es");
            d.body_en  = t->memberString("body_en");
            d.body_es  = t->memberString("body_es");
            d.image    = t->memberString("image");
        }
        if (const jsonm::Value* cls = pay->member("classes"))
            if (cls->isArray())
                for (const auto& c : cls->arr)
                    if (c && c->isString()) d.classes.push_back(c->str);
        d.track        = pay->memberString("track");
        d.base_profile = pay->memberString("base_profile");
        if (const jsonm::Value* ov = pay->member("overrides"))
            if (ov->isObject())
                for (const auto& kv : ov->obj)
                    if (kv.second && kv.second->isString()) d.overrides[kv.first] = kv.second->str;
        d.laps         = pay->memberInt("laps");
        d.duration_min = pay->memberInt("duration_min");
        d.discount_pct = pay->memberInt("discount_pct");
        if (const jsonm::Value* rw = pay->member("rewards")) {
            d.reward_credits = rw->memberLong("credits");
            d.reward_rep     = rw->memberInt("rep");
        }
        if (d.id.empty() || d.classes.empty()) continue;             // no playable payload -> discard
        if (d.starts_at < 0 || d.ends_at < 0) continue;
        if (nowEpochSec < d.starts_at || nowEpochSec > d.ends_at) continue;   // two clocks: the window uses REAL time
        out.push_back(std::move(d));
    }
    return out;
}

// lowercase + alphanumeric-only: "Highgate 24 Hours" -> "lemans24hours" (matches the slug "lemans").
static std::string NormSlug(const std::string& s) {
    std::string o;
    for (char c : s) {
        unsigned char u = (unsigned char)c;
        if (isalnum(u)) o += (char)tolower(u);
    }
    return o;
}

bool ResolveTrackSlug(const std::string& slug, const std::vector<CalRound>& rounds,
                      long* outHash, std::string* outName) {
    const std::string n = NormSlug(slug);
    if (n.empty()) return false;
    // 1) normalized equality against the loaded rounds (gives us the real hash + display NAME)
    for (const auto& r : rounds)
        if (r.track_hash != 0 && NormSlug(r.track_name) == n) {
            *outHash = r.track_hash; *outName = r.track_name; return true;
        }
    // 2) the inverse of the curated hash->slug catalog (the same slug namespace as the track maps)
    if (int h = TrackHashForSlug(slug)) { *outHash = (long)h; *outName = slug; return true; }
    // 2.5) the inverse of the LIVE installed-tracks catalog: covers any installed circuit, even one that doesn't
    // have a curated track map in track_images.psv. Same hash namespace -> applies directly.
    if (int h = TrackHashForSlugLive(slug)) { *outHash = (long)h; *outName = slug; return true; }
    // 3) normalized containment (long slug) -- "lemans" inside "Highgate 24 Hours"
    if (n.size() >= 5)
        for (const auto& r : rounds)
            if (r.track_hash != 0 && NormSlug(r.track_name).find(n) != std::string::npos) {
                *outHash = r.track_hash; *outName = r.track_name; return true;
            }
    return false;
}

// Registry of active events. std::deque (not a vector): push_back NEVER invalidates pointers/refs to
// existing elements, so the pointers SpecialEventById returns stay stable across
// any later repopulation (not by calling convention, by construction).
static std::deque<SpecialEventDef> g_events;

void SetSpecialEventRegistry(std::vector<SpecialEventDef> evs) {
    g_events.clear();
    for (auto& e : evs) g_events.push_back(std::move(e));
}

const SpecialEventDef* SpecialEventById(const std::string& id) {
    for (const auto& e : g_events)
        if (e.id == id) return &e;
    return nullptr;
}

int SpecialEventRegistryCount() { return (int)g_events.size(); }

std::vector<const SpecialEventDef*> SpecialEventsAll() {
    std::vector<const SpecialEventDef*> out;
    out.reserve(g_events.size());
    for (const auto& e : g_events) out.push_back(&e);
    return out;
}

bool ResolveEventTrack(const SpecialEventDef& ev, long* outHash, std::string* outName) {
    std::vector<CalRound> rounds;
    for (const auto& c : ev.classes) { auto rs = CalendarForSeries(c); rounds.insert(rounds.end(), rs.begin(), rs.end()); }
    return ResolveTrackSlug(ev.track, rounds, outHash, outName);
}

int VehicleDiscountForClass(const std::string& className) {
    int best = 0;
    for (const auto& e : g_events)
        for (const auto& c : e.classes)
            if (c == className && e.discount_pct > best) best = e.discount_pct;
    return best;
}

void ResetSpecialEventsForTest() { g_events.clear(); }

ConfigProfile EventEffectiveProfile(const std::string& baseId, const std::map<std::string,std::string>& ov) {
    ConfigProfile out;
    if (const ConfigProfile* base = ProfileById(baseId)) out = *base;   // base copy
    // A base_profile that does NOT exist used to degrade SILENTLY to an empty profile, and the event would run with the
    // rules the player happened to have set in Custom Race. The backend admin writes this field into a free-text,
    // unvalidated input, so a typo or a renamed profile would silently swallow the rules.
    // It can't hard-fail (the event must still be runnable with just its overrides), but it DOES need to shout about it.
    else if (!baseId.empty())
        UserLog("events", "base_profile '%s' DOES NOT EXIST: the event runs with no base rules, only its overrides. "
                          "Check the name against docs/series_config_profiles.psv", baseId.c_str());
    out.id = "event_effective";
    for (const auto& kv : ov) {
        if (kv.first == "formlap") continue;   // hard rule: this setting is never touched by an event
        out.fields[kv.first] = kv.second;
    }
    return out;
}

int EventCarRule(const ConfigProfile& eff, const std::string& playerClass) {
    const std::string raw = eff.get("car");
    const size_t a = raw.find_first_not_of(" \t"), b = raw.find_last_not_of(" \t");
    if (a == std::string::npos) return 0;
    const std::string v = raw.substr(a, b - a + 1);
    char* end = nullptr;
    const long long h = strtoll(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0' || h < INT32_MIN || h > INT32_MAX) {
        UserLog("events", "overrides.car '%s' is not a vehicle hash -> ignored", raw.c_str());
        return 0;
    }
    // Fix round 1 (Minor): 0 is the "no rule" sentinel (not an invalid vehHash); distinct log.
    if (h == 0) {
        UserLog("events", "overrides.car '%s' is the no-rule sentinel (0) -> ignored", raw.c_str());
        return 0;
    }
    const std::string cls = ClassForVehicle((int)h);
    // final fix (, final review round): compare against the RESOLVED class, same as the three
    // launch paths (career::EffectiveCarRule(career::ResolveClass(...), ...) in screen_calendar.cpp).
    // playerClass may arrive as a variant token or a display name, not always as the class_name
    // of the catalog that ClassForVehicle returns -- comparing raw lost the rule in that case.
    const std::string resolvedClass = ResolveClass(playerClass);
    if (cls != resolvedClass) {
        UserLog("events", "overrides.car %lld is a '%s', not a '%s' -> ignored for this class", h,
                cls.empty() ? "?" : cls.c_str(), playerClass.c_str());
        return 0;
    }
    return (int)h;
}

// The word the form publishes when the author asks for "as many as fit on the track" (backend:
// eventSchema.GRID_MAX). Exact and lowercase: the backend validator does not let any other
// spelling be published, so accepting variants here would only let typos in through the back door.
static const char kGridMax[] = "max";

// The author's value without the surrounding spaces. They are trimmed even though the backend no longer lets them be published:
// an event can also arrive from a hand-edited file.
static std::string TrimmedGrid(const std::string& v) {
    const size_t a = v.find_first_not_of(" \t");
    if (a == std::string::npos) return std::string();
    return v.substr(a, v.find_last_not_of(" \t") - a + 1);
}

// The author's integer, with a STRICT criterion: digits only (the backend form validates /^\d+$/) and >= 2.
// "20 coches", "-4", "auto", "max", "" -> 0 = no authored number. `atoi` will not do here: it would swallow
// "20 coches" as 20 and "auto" as 0, so a form typo would silently decide the grid.
static int AuthoredGridOf(const std::string& v) {
    const std::string s = TrimmedGrid(v);
    if (s.empty()) return 0;
    long long n = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (!isdigit((unsigned char)s[i])) return 0;
        n = n * 10 + (s[i] - '0');
        if (n > 100000) return 0;   // does not fit on any grid: it is an odd value, not a number
    }
    return n >= 2 ? (int)n : 0;     // 0 and 1 are not a race
}

// The event's series as a MEETING: dedup by CLASS (pinnacle_masters_4414 and ivory_cup_4455 are the same GRANITE_MASTERS_4373, and adding them
// twice would split the total into one half too many) and drop those the catalog does not know (an
// unknown series is worth 0 cars and would drag the whole distribution to zero).
static std::vector<std::string> EventMeetingSeries(const SpecialEventDef& ev) {
    std::vector<std::string> out;
    for (const auto& c : ev.classes) {
        const SeriesDef* sd = SeriesById(c);
        if (!sd) continue;
        bool dup = false;
        for (const auto& x : out) { const SeriesDef* xd = SeriesById(x); if (xd && xd->class_name == sd->class_name) { dup = true; break; } }
        if (!dup) out.push_back(c);
    }
    return out;
}

EventGridPlan EventGridSizeFor(const SpecialEventDef& ev, const ConfigProfile& eff,
                               const std::string& playerSeriesId, int trackCapacity) {
    EventGridPlan p;
    p.cap = MaxGridSize();
    p.trackCap = trackCapacity > 0 ? trackCapacity : 0;
    const std::string wish = TrimmedGrid(eff.get("grid"));
    p.wantsMax = (wish == kGridMax);
    p.authored = p.wantsMax ? 0 : AuthoredGridOf(wish);

    // (3) what the engine computes on its own. Multiclass -> the whole meeting, like the calendar. If that
    // number does not come out (a class without liveries leaves the distribution at nothing) it falls to the single-class path instead of to the
    // floor of 16: it is the 0.6.8 behavior and is never worse than a made-up number.
    const std::vector<std::string> cita = EventMeetingSeries(ev);
    if (cita.size() > 1) p.computed = MeetingGridSize(cita);
    if (p.computed < 2) {
        const SeriesDef* psd = SeriesById(playerSeriesId);
        const std::string className = psd ? psd->class_name : playerSeriesId;
        const SeriesDef* esd = SeriesForClassName(className);
        // same source as the XML. An event is calendar-agnostic and its class may not be run by any series
        // of the ladder; there the XML is written by the class path, so the number also comes from it.
        p.computed = esd ? GridSizeForSeries(esd->id) : GridSizeForClassName(className);
    }

    // (1) the author decides -- their number, or the track's slots if they asked for the maximum. A "max" on a circuit with no
    // known slots is NOT made up: it falls to (3), which is exactly what the engine did before it knew how to measure
    // tracks. (2) the cap of the player's machine takes precedence over the authored one; both paths of (3) already carry it
    // inside.
    const int autorado = p.wantsMax ? p.trackCap : p.authored;
    p.onTrack = autorado > 0 ? autorado : p.computed;
    // 16/09 (Josh) — THE TRACK'S SLOTS ARE A HARD CEILING, also for the author. Until now their number
    // prevailed and the mismatch was only logged: the Westvale Speedway asked for 48 cars on a circuit with 26 slots,
    // and that is asking the game to place twenty-two that have no room. It is trimmed HERE, so an already
    // published event stops asking for the impossible without republishing. Only with known capacity (>= 2): an
    // unmeasured circuit trims nothing.
    if (p.trackCap >= 2 && p.onTrack > p.trackCap) { p.cutByTrack = p.onTrack; p.onTrack = p.trackCap; }
    if (autorado > 0 && p.cap > 0 && p.onTrack > p.cap) p.onTrack = p.cap;
    if (p.onTrack < 2) p.onTrack = 16;   // historic floor: unresolved class -> a normal race
    return p;
}

// 19/09 — see the contract in career_events.h. The four pit fields keep the `atoi` they had in the
// arming (CareerArmEvent): the only thing that changes is that the -1 of "not set" stops travelling to the game as "do not
// force" and comes out with the house rule.
EventRaceRules EventRaceRulesFor(const ConfigProfile& eff) {
    auto pInt = [&](const char* k) { std::string v = eff.get(k); return v.empty() ? -1 : atoi(v.c_str()); };
    EventRaceRules r;
    const int pit = pInt("pitreq"), refuel = pInt("refok"), tyres = pInt("mintyres"), kmh = pInt("pitkmh");
    if (pit    >= 0) r.mandatoryPit = pit;
    if (refuel >= 0) r.refuelling   = refuel;
    if (tyres  >= 0) r.minTyres     = tyres;
    if (kmh    >= 0) r.pitKmh       = kmh;
    const std::string m = TrimmedGrid(eff.get("mist"));   // (trimming spaces is not just a grid matter)
    r.mistakes = (m.size() == 1 && m[0] >= '0' && m[0] - '0' <= bal::kMistMax) ? m[0] - '0'
                                                                                  : bal::EVENT_AI_MISTAKES_DEFAULT;
    return r;
}

// 01/10 ([tester], 0.7.1): see the contract in career_events.h. Same strict criterion as the grid: only digits and
// only the multipliers the game offers; anything else is a form typo and decides nothing.
int EventTimeProgOverride(const ConfigProfile& eff) {
    const std::string raw = TrimmedGrid(eff.get("timeprog"));
    if (raw.empty()) return 0;
    bool digits = true;
    for (size_t i = 0; i < raw.size(); i++) if (!isdigit((unsigned char)raw[i])) digits = false;
    const int mult = digits ? atoi(raw.c_str()) : -1;
    for (int idx = 1; idx <= 11; idx++) if (TimeProgMultiplier(idx) == mult) return idx;
    UserLog("events", "overrides.timeprog '%s' is not one of the game's multipliers (1,2,5,10,15,20,25,30,40,50,60) -> ignored", raw.c_str());
    return 0;
}

bool EventDefaultDate(long long startsAtEpoch, int* month, int* day) {
    if (startsAtEpoch <= 0) return false;
    const time_t t = (time_t)startsAtEpoch;
    struct tm tmv;
    if (gmtime_s(&tmv, &t) != 0) return false;
    if (month) *month = tmv.tm_mon + 1;
    if (day) *day = tmv.tm_mday;
    return true;
}

bool EventDateOverride(const ConfigProfile& eff, int* month, int* day, int* year) {
    const std::string raw = TrimmedGrid(eff.get("date"));
    if (raw.empty()) return false;
    static const int kDays[13] = { 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    // "MM-DD" or "YYYY-MM-DD": digits in every position except the hyphens
    const bool withYear = raw.size() == 10 && raw[4] == '-' && raw[7] == '-';
    bool shape = withYear || (raw.size() == 5 && raw[2] == '-');
    for (size_t i = 0; shape && i < raw.size(); i++) {
        const bool dash = withYear ? (i == 4 || i == 7) : (i == 2);
        if (!dash && !isdigit((unsigned char)raw[i])) shape = false;
    }
    const size_t off = withYear ? 5 : 0;
    const int yr = (shape && withYear) ? atoi(raw.substr(0, 4).c_str()) : 0;
    const int mo = shape ? atoi(raw.substr(off, 2).c_str()) : 0;
    const int da = shape ? atoi(raw.substr(off + 3, 2).c_str()) : 0;
    // without a year 02/29 is accepted (the year is set by the era and may be a leap year); with a year, only if it is one
    const bool leap = !withYear || ((yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0);
    const int maxDay = (mo == 2 && !leap) ? 28 : (mo >= 1 && mo <= 12 ? kDays[mo] : 0);
    if (!shape || (withYear && (yr < 1900 || yr > 2100)) || mo < 1 || mo > 12 || da < 1 || da > maxDay) {
        UserLog("events", "overrides.date '%s' is not a valid date (YYYY-MM-DD or MM-DD) -> the event keeps its default date", raw.c_str());
        return false;
    }
    if (month) *month = mo;
    if (day) *day = da;
    if (year && withYear) *year = yr;
    return true;
}

int EventStartMinute(const ConfigProfile& eff) {
    const std::string raw = TrimmedGrid(eff.get("minute"));
    if (raw.empty()) return 0;
    bool digits = !raw.empty() && raw.size() <= 2;
    for (size_t i = 0; i < raw.size(); i++) if (!isdigit((unsigned char)raw[i])) digits = false;
    const int m = digits ? atoi(raw.c_str()) : -1;
    if (m < 0 || m > 59) {
        UserLog("events", "overrides.minute '%s' is not a minute (0..59) -> 0", raw.c_str());
        return 0;
    }
    return m;
}

} // namespace career
