// wasm_main.cpp -- the engine's browser entry point. Replaces the WebSocket transport with a direct
// call: the request JSON goes in, the response JSON comes out. The dispatch itself is untouched --
// ApiCore was already decoupled from its transport (see career_api_core.h's first line), so there
// is nothing to port here, only to omit.
//
// It is also this file's job to be the engine's HOST. On Windows that role belongs to the overlay:
// it owns the live save and calendar, and it installs the view providers every career query reads
// through (career_api_queries.h). The overlay is not part of this repository, so without a host
// here every career query answers not_ready -- which is not a browser problem at all (a native
// build of this same tree would say exactly the same thing), but it does make the module useless.
// The host layer below is small and deliberately reads only from the published engine.
#include "career_api_core.h"
#include "career_api_queries.h"
#include "career_balance.h"    // bal::REP_BAND -- the anchor the reputation bands are cut from
#include "career_calendar.h"
#include "career_contract.h"
#include "career_dataload.h"
#include "career_engine.h"
#include "career_save.h"
#include "career_series.h"     // SeriesById -> a series' display name, not its id
#include "paths.h"
#include <cstdio>
#include <emscripten.h>
#include <set>
#include <string>
#include <vector>

// The host's view providers: one file per query.* view, engine-wasm/<view>_view_wasm.cpp, each
// defining one installer here and compiled by build_wasm.sh's glob. An installer takes the
// career-state seat instead of this file's globals, so a view reads the world through the same
// pointers query.career_state does. Declared here rather than in a shared header because the export
// resolves every bare quoted include in engine-wasm/ against dll/career/; a mismatched signature
// still fails loudly, at link time.
namespace wasmhost {
void InstallCalendarView(capi::CareerStateProvider state);   // calendar_view_wasm.cpp
void InstallMarketView(capi::CareerStateProvider state);     // market_view_wasm.cpp
void InstallSeasonView(capi::CareerStateProvider state);     // season_view_wasm.cpp
void InstallGarageView(capi::CareerStateProvider state);     // garage_view_wasm.cpp
void InstallProfileView(capi::CareerStateProvider state);    // profile_view_wasm.cpp
void InstallTreeView(capi::CareerStateProvider state);       // tree_view_wasm.cpp
void InstallPreraceView(capi::CareerStateProvider state);    // prerace_view_wasm.cpp
// Not a view: the four owned-car accessors career_data.cpp calls (career_ui.h), read through the
// same seat. Listed in build_wasm.sh by name, since the view glob does not reach it.
void InstallOwnedCars(capi::CareerStateProvider state);      // owned_wasm.cpp
}  // namespace wasmhost

namespace {

// The REP band's i18n TOKEN. career::RatingTierLabel gives the localized literal instead, and putting
// that in `tierKey` was a real bug: the app looks the field up as `market.tier.<token>`
// (Home.tsx, Market.tsx, against the i18n bundle's market.tier block), so a resolved label missed in
// English and would have missed differently in Spanish. A key that changes with the language is not
// a key. The engine keeps this same six-word vocabulary in career_api_queries.cpp twice over
// (TierToken for query.garage, TierNameKey for query.tree) and the app keeps it in every locale file;
// both C++ copies are `static`, so this is a fourth -- but six lowercase literals indexed by the
// RepDisplayBand we already compute is strictly less invention than a localized label.
const char* const kTierToken[6] = { "unknown", "prospect", "known", "established", "star", "legend" };

// The follower count, reproduced the same way kTierToken above was: from the published anchor, not
// from a comment about it. career_api_queries.h's own note on the `followers` field describes a
// linear count (1200 plus 95 per point of reputation) and claims it is what the overlay computes.
// That note is STALE, and taking it at its word was a real defect here. The overlay stopped being
// linear: it calls a piecewise curve whose knots sit on
// the six band thresholds, which are career_balance.h's bal::REP_BAND (published, and the same table
// the tier/next-tier arithmetic above is already cut from). The divergence is not a rounding matter
// -- at rep 15 the stale formula says 2,625 against 6,000, and at rep 600 it says 58,200 against
// 9,000,000, which is the reason the overlay abandoned the line in the first place: a linear count
// topped out around 58k for a driver the game calls a Legend.
//
// The overlay's implementation is authoritative; this is a copy, like MakeHubView below and for the
// same reason (the assembly it belongs to is not published). Linear interpolation between the anchor
// pairs, and beyond the top anchor the last segment's slope continues -- rep is not capped at 600.
// The anchors themselves are the one thing here that cannot be derived from anything published, so
// they are transcribed, and the +0.5 is the overlay's own rounding, not a re-invention of it.
const long kFollowersAtBand[6] = { 500, 6000, 35000, 200000, 1500000, 9000000 };

long FollowersForRep(int rep) {
    const int* R = career::bal::REP_BAND;      // { 0, 15, 40, 120, 300, 600 }
    const long* F = kFollowersAtBand;
    if (rep <= R[0]) return F[0];
    for (int i = 1; i < 6; i++)
        if (rep <= R[i]) {
            const double t = (double)(rep - R[i - 1]) / (double)(R[i] - R[i - 1]);
            return (long)(F[i - 1] + t * (F[i] - F[i - 1]) + 0.5);
        }
    const double slope = (double)(F[5] - F[4]) / (double)(R[5] - R[4]);
    return (long)(F[5] + slope * (rep - R[5]) + 0.5);
}

// The live world this module answers from. One save and one calendar, for the lifetime of the page
// -- the same single-career shape the real product has, minus the file the real product reloads.
career::SaveData       g_save;
career::CareerCalendar g_calendar;
bool                   g_loaded = false;

// Seeds the user root's car-class cache from the world, once. LoadLadder (career_data.cpp) counts each
// ladder node's cars from paths::UserData("cache/ams2_car_class.csv") -- a cache the game side writes
// from the installed content, so it lives in the USER root, not the data root. A browser tab starts
// with an empty user root, and without this every node of query.tree would report zero cars. The world
// ships the invented catalogue beside its other datasets, so the seed is a plain copy; an existing file
// is left alone, as a cache the user already has would be. Only the catalogue: a new career owns no car
// and the engine grants none at start, so nothing here puts one in the garage.
void SeedCarCatalogue() {
    const std::string dst = paths::UserData("cache/ams2_car_class.csv");
    if (paths::FileExists(dst)) return;
    std::FILE* in = paths::FOpen(paths::Data("ams2_car_class.csv"), "rb");
    if (!in) return;   // a world without the catalogue: the ladder counts zero cars, as the game does
    paths::EnsureParentDir(dst);
    std::FILE* out = paths::FOpen(dst, "wb");
    if (out) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, in)) > 0) std::fwrite(buf, 1, n, out);
        std::fclose(out);
    }
    std::fclose(in);
}

// Loads the invented world off the virtual disk and puts a career on the clock.
//
// LoadSeasonData is the engine's OWN loader -- the same 33-dataset pass the game runs at startup,
// reading through paths::Data(), which paths_wasm.cpp has pointed at /world. Datasets the published
// world does not carry simply come back with zero rows; the loader is documented as degrading
// quietly rather than failing, so this is the ordinary path and not a special browser case.
void BootWorld() {
    career::SeasonDataOpts opts;      // PSV branch: the published world is .psv, not career JSON
    career::SeasonDataReport report;
    career::LoadSeasonData(opts, report);
    SeedCarCatalogue();

    // A career that has not started yet: the real clock and the rookie rating the engine itself
    // hands a new player. Everything else is a genuinely empty career, because it is one.
    //
    // kCareerStartDoy, NOT RookieOpenerDoy. career_calendar.h is explicit and dated about this: a
    // career is born on January 1st because starting it at the entry tier's opener put the player
    // NINE DAYS PAST their own signing window, and the whole compressed year-1 cycle was unreachable
    // in production. (The header's older text above that block still says the opener; the later,
    // measured decision is the one that counts.) With this world it happened not to bite -- opener at
    // day 20, window still open at day 26 -- but that is a property of the published testdata, not of
    // this code, and the next world to be published would have decided it differently.
    g_calendar.year = career::kStartYear;
    g_calendar.cursor_doy = career::kCareerStartDoy;
    g_save.prog.rating = career::RatingSeedPlayer();

    // If a career was saved into the writable root earlier in this session, it wins. On a page that
    // has just loaded there is nothing there, and LoadCareer says so by returning false -- which is
    // the ORDINARY case here, not a failure, so g_loaded is true either way: the world is loaded and
    // a career exists, whether it came off disk or was born three lines up.
    const bool restored = career::LoadCareer(g_save, paths::UserData("saves/career.sav").c_str());
    // `restored` is named rather than discarded because there is a real gap behind it. LoadCareer
    // fills the SAVE only; the calendar is a separate file with a separate loader (LoadCalendar), and
    // this build does not call it -- the published tree never names the file it reads (save_migrate.h
    // lists career_season.sav and career_seasons.sav without saying which one is the calendar, and
    // guessing would be invention). So a restored career would keep the brand-new clock set above.
    // Nothing writes a save in this build, so the two cannot disagree today; the moment persistence
    // lands (IDBFS), this is the first thing to fix.
    (void)restored;

    g_loaded = true;
}

std::string PlayerDisplayName() {
    const career::PlayerIdentity& p = g_save.player;
    if (p.name.empty()) return p.surname;
    return p.surname.empty() ? p.name : p.name + " " + p.surname;
}

int UnreadMailCount() {
    int n = 0;
    for (const auto& e : g_save.emails) if (e.read == 0) n++;
    return n;
}

// The state seat query.career_state / query.standings / query.inbox read through: raw pointers to
// the two objects above, exactly as the overlay hands over its own globals.
capi::CareerStateView MakeCareerStateView() {
    capi::CareerStateView v;
    v.loaded = g_loaded;
    v.save = &g_save;
    v.calendar = &g_calendar;
    v.playerName = PlayerDisplayName();
    v.badgeInbox = UnreadMailCount();
    return v;
}

// PROVISIONAL -- SECOND IMPLEMENTATION OF THE HUB VIEW.
//
// The authoritative assembly is career_overlay.cpp's, which this repository does not publish. What
// follows is a copy written so the browser build can answer at all, and copies drift. It is marked
// with a greppable word because the prose below explains every individual choice but nothing in it
// said the one thing a future reader most needs to know: there are now two of these. Phase B should
// either replace this or, better, publish the real one.
//
// The static_assert is the guard that makes the drift loud. HubView is a plain serialization view:
// a field added upstream would ship default-initialized through this demo in silence, because
// nothing here has to mention it in order to compile. Size rather than member count, because C++17
// cannot count members -- and size is exact and stable for the one toolchain this file is ever built
// with (wasm32, libc++). When it fires: read the new HubView, fill the new field below, then update
// the number here. Do not just update the number.
static_assert(sizeof(capi::HubView) == 248,
              "capi::HubView changed upstream. This is a SECOND, provisional copy of the overlay's hub "
              "assembly: fill the new/changed field in MakeHubView below, THEN update this size.");

// Every field below is copied from the source career_api_queries.h documents for it. The one place
// this cannot follow the overlay literally is the reputation badge: the overlay reads
// market::RepInfoFor, whose catalog (career_market_data.h) is declared with no published definition.
// But everything that function computes is anchored to career_balance.h's REP_BAND, which IS
// published -- and career_engine.h says in as many words that the display bands mirror it -- so the
// band, the next band and the distance to it are all derived from the same table the real one uses,
// not guessed and not reported as absent.
capi::HubView MakeHubView() {
    capi::HubView v;
    v.loaded = g_loaded;
    if (!g_loaded) return v;

    const career::CareerProgress& prog = g_save.prog;

    v.first = g_save.player.name;
    v.last  = g_save.player.surname;

    const int band = career::RepDisplayBand(prog.rep);
    v.repScore  = prog.rep;
    v.repIdx    = band;
    v.tierKey   = kTierToken[band < 0 ? 0 : (band > 5 ? 5 : band)];
    v.tierColor = career::RatingTierColor(prog.rep);

    // The next band and the progress towards it, cut from bal::REP_BAND. Reporting these as absent
    // is not the safe option it looks like: query.hub turns hasNext=false into "next": null, and the
    // app then hides the whole next-tier strip -- so a rookie fifteen REP short of Prospect would be
    // told there is nothing above them at all. A wrong answer, not a missing one.
    v.hasNext = band >= 0 && band < 5;
    if (v.hasNext) {
        const int floorRep = career::bal::REP_BAND[band];
        const int nextRep  = career::bal::REP_BAND[band + 1];
        const int span     = nextRep - floorRep;
        v.nextTierKey = kTierToken[band + 1];
        v.nextTierMin = nextRep;
        const int pct = span > 0 ? (int)(((long)prog.rep - floorRep) * 100 / span) : 100;
        v.repPct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    } else {
        v.repPct = 100;   // top band: there is nothing left to be progressing towards
    }

    v.followers = FollowersForRep(prog.rep);

    for (const auto& kv : g_save.class_stats) {
        v.wins    += kv.second.wins;
        v.podiums += kv.second.podiums;
        v.races   += kv.second.races;
    }

    // Next race. career_season_ui.h defines this field exactly: "the earliest pending slot among your
    // ENROLLED series (valid=false if none)". So `enrolled` is a filter, not an afterthought, and a
    // `locked` round is not pending -- without both, a round the player cannot enter could be
    // advertised to them as their next race.
    //
    // With no enrollments that yields nothing, and the honest answer is null. It stays that way on
    // purpose: the field belongs to the engine's contract, not to this demo, and a hub that shows a
    // race the player is not entered in would be a nicer-looking lie. A career with a signed contract
    // fills it by itself.
    //
    // This is also the one place this file genuinely touches the world read off the virtual disk:
    // BuildMasterCalendar is the same function the calendar screen is drawn from.
    std::set<std::string> owned = career::OwnedClassSet(g_save);
    std::vector<career::CalSlot> slots =
        career::BuildMasterCalendar(g_calendar, prog.frontier_phase, &g_save.unlocked_gates, &owned);
    for (const auto& s : slots) {
        if (!s.enrolled) continue;
        if (s.locked || s.past || s.raced) continue;
        if (s.doy < g_calendar.cursor_doy) continue;
        const career::SeriesDef* sd = career::SeriesById(s.series_id);
        v.nextValid  = true;
        v.nextSeries = sd ? sd->display : s.series_id;   // the field is a display name, not an id
        v.nextTrack  = s.track_name;
        v.nextDate   = s.date;
        v.nextRound  = s.round;
        v.nextDoy    = s.doy;
        break;
    }

    v.unread = UnreadMailCount();
    v.directOffers = 0;   // offer generation is a market pass the host has not run; zero is the count, not a placeholder

    v.enrollmentCount = (int)g_calendar.enrollments.size();
    v.contractCount   = career::ActiveContractCount(g_save);
    v.contractActive  = v.contractCount > 0;
    for (const auto& c : g_save.contracts) {
        if (!c.active) continue;
        if (v.contractSeries.empty()) {
            // ContractFacetsView calls this field seriesDisp: a display name, not an id.
            const career::SeriesDef* sd = career::SeriesById(c.series_id);
            v.contractSeries = sd ? sd->display : c.series_id;
        }
        // team_label is what this field documents, and `team_id` already holds one despite its name:
        // career_contract.cpp assigns it from the offer's team and calls that, in its own comment, a
        // synthetic team LABEL. So no catalog lookup is missing here -- an earlier comment in this
        // spot said one was, which would have sent Phase B looking for work already done.
        //
        // The empty check is the overlay's, not an addition: it skips a contract whose team_id is
        // empty rather than pushing a blank string, because this list is rendered as the names of
        // the teams a player would leave stranded by breaking their contracts, and a blank entry
        // there is a nameless row, not a team.
        if (c.team_id.empty()) continue;
        v.contractTeams.push_back(c.team_id);
    }
    v.breakFee = career::BreakContractFee(g_save, "");

    // DoyToDate ("1 Jan"), NOT DateLabel ("Wed 1 Jan"), because the weekday is a SEPARATE field: the
    // serializer emits cursorDate verbatim and adds `weekday` beside it from WeekdayLabel(year, doy),
    // and the app renders the two joined. DateLabel here made the demo read "Wed Wed 1 Jan". The
    // overlay fills this field from DoyToDate for exactly that reason.
    v.cursorDate = career::DoyToDate(g_calendar.cursor_doy);
    v.cursorDoy  = g_calendar.cursor_doy;
    v.year       = g_calendar.year;

    v.ownedCount      = (int)g_save.owned.size();
    v.completedRaces  = (int)g_save.completed_races.size();

    v.eloRating   = (int)prog.rating;
    v.eloDelta    = 0;                    // the delta belongs to the last settled race; nothing has raced yet
    v.xpToward    = prog.xp_toward_next;
    v.xpThreshold = career::XpThreshold(prog.frontier_phase);
    v.phase       = prog.frontier_phase;

    return v;
}

capi::ApiCore& Core() {
    static capi::ApiCore* core = [] {
        // Fixed virtual roots; paths_wasm.cpp (POSIX PathProvider for this build) agrees on the
        // same four strings as its own fallback default, so this call is redundant with it in
        // practice -- but explicit beats implicit for what the engine sees on first touch.
        paths::SetRootsForTest("/world", "/save", "/cache", "/log");
        BootWorld();
        auto* c = new capi::ApiCore();
        capi::RegisterCareerQueries(*c);
        capi::SetCareerStateProvider(&MakeCareerStateView);
        capi::SetHubViewProvider(&MakeHubView);
        wasmhost::InstallCalendarView(&MakeCareerStateView);
        wasmhost::InstallMarketView(&MakeCareerStateView);
        wasmhost::InstallSeasonView(&MakeCareerStateView);
        wasmhost::InstallGarageView(&MakeCareerStateView);
        wasmhost::InstallProfileView(&MakeCareerStateView);
        wasmhost::InstallTreeView(&MakeCareerStateView);
        wasmhost::InstallPreraceView(&MakeCareerStateView);
        wasmhost::InstallOwnedCars(&MakeCareerStateView);
        // RegisterCareerQueries does NOT register query.api_info -- that one lives in
        // capi::RegisterBuiltins (career_api.h), which ties the handler to the WsServer handshake
        // and isn't even defined in this tree. The socket handled the handshake/auth; without a
        // socket there is neither, so this is a small dedicated stand-in, not a duplicate.
        // ApiHandler is a std::function<HandlerResult(const jsonm::Value*)>, so a lambda (capturing
        // or not) is as valid a target as a free-function pointer -- this one just doesn't need to capture.
        c->Register("query.api_info", [](const jsonm::Value*) {
            capi::HandlerResult r;
            r.payload = "{\"api\":1,\"build\":\"wasm\",\"game_attached\":false}";
            return r;
        });
        return c;
    }();
    return *core;
}

std::string g_response;   // owns the buffer the JS side reads before the next call

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* engine_dispatch(const char* request_json) {
    g_response.clear();
    Core().OnRaw(request_json ? request_json : "");
    // Each call to engine_dispatch queues exactly one request via OnRaw above, so Pump drains
    // exactly one response and returns (queue_.empty() short-circuits the loop before any further
    // iteration touches send). maxRequests only bounds how many queued items ONE Pump call may
    // drain -- it doesn't loop Pump itself -- so a generous bound here is inert, not unsound: it
    // costs nothing beyond the single response this synchronous caller ever produces.
    Core().Pump([](const std::string& out) { g_response = out; }, 64);
    return g_response.c_str();
}

}  // extern "C"
