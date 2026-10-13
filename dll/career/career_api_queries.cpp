// career_api_queries — engine-state adapters -> career_api JSON (see the contract in the header).
// Hard project rule: do NOT duplicate logic the screens already derive — this file calls
// the SAME pure engine functions (FinalTable, SeriesById, FindEnrollment, DriverIdForName)
// that screen_season.cpp / screen_hub.cpp / screen_inbox.cpp use through the CalUi* bridges.
#include <chrono>   // clock diagnostic stopwatch
#include "career_api_queries.h"
#include "career_content_version.h"   // Aug 2: kContentVersion -> `dataVersion` in the telemetry payload
#include "career_difficulty.h"        // Aug 2: dda::kAiMin/kAiMax/kAiSeed + Clampi (cmd.settings.setSeed)
#include "career_save.h"        // SaveData, Contract, GeneratedEmail, ContractForSeries (inline)
#include "career_cards.h"       // CARD_DEFS/CardById/CardCost/CardFxV/CardUpgradeNext/CardSellValue/CardShop*
#include "career_calendar.h"    // CareerCalendar, SeasonState, FindEnrollment
#include "career_series.h"      // SeriesById -> display (same fallback as the screens: the id)
#include "career_content.h"     // DlcProductStates + product toggles (query.content/cmd.content.toggle)
#include "career_standings.h"   // FinalTable (THE table screen_season.cpp draws)
#include "career_data_json.h"   // DriverIdForName (name -> stable id from the pool; "" if it doesn't resolve)
#include "career_mail.h"        // MailSubjectKey/MailBodyKey/MailParamsDecode (query.inbox)
#include "career_contract.h"    // WeeksLeftFromFirstSeen (deadlineWeeks for the offer in query.inbox)
#include "career_nego_thread.h" // canonical NegoAbsDay (real absolute day, not an inline year*365)
#include "career_profile.h"     // query.profile: RivalRow/RankRow/ArcPoint/ProfileRatingInfo view-models
#include "career_customai.h"    // query.calendar: LiveriesForClass -> grid size per class
#include "career_skins.h"       // "real skins": SkinSlotPresent/InstalledSkinCount (cmd.skins.status)
#include "career_econ.h"        // Memories: FindEconByClassName -> purchase (showroom car price)
#include "career_seats.h"       // Junior team pipeline: career::ParentTeamFor -> "" if the offer isn't from an affiliated feeder team
#include "career_market.h"      // market::MarketPhaseFor/WindowTargetYear/DaysTo* — the transfer-market calendar
#include "career_candidate.h"   // TeamPersonalityToken/SeatAsksFor — what the seat is asking for, spelled out
#include "career_fields.h"      // (seat detail): DriverAge — the incumbent's age
#include "career_events.h"      // Memories: VehicleDiscountForClass (active event discount per class)
#include "career_dateutil.h"    // AbsDayFromYearDoy — deadlineWeeks for the stand-in invitation
#include "entitlements.h"       // Patreon-app integration: Tier + Entitlements::SetTier/Get (tier plumbing)
#include "career_userlog.h"     // cmd.log.note -> breadcrumbs from the app into the support log
#include "strings.h"            // cmd.app.lang -> SetLang (the engine's bilingual overlays)
#include "strings_locale.h"     // Ten-language localization: cmd.app.lang -> SetLangCode (data layer)
#include "theme.h"              // Col:: ImU32 palette -> mapped to the app's CSS variables ("var(--x)")
#include "json_write.h"
#include "paths.h"              // resolved roots for query.api_info
#include "../vr_mirror.h"       // VR: NotifyRaceLaunched (hides the VR panel when a race launches)

#include <algorithm>
#include <cmath>        // 0.7.0: std::floor (garage km with one decimal)
#include <cstdio>       // snprintf (CssHex)
#include <cstdlib>      // atol (numeric params: ends_at)
#include <ctime>        // time(nullptr) — deadlineWeeks for the event invitation

namespace capi {

void WritePathsInfo(jsonw::Writer& w) {
    const std::string& data = paths::RootDir(paths::Root::Data);
    const std::string& ud   = paths::RootDir(paths::Root::UserData);
    w.Key("paths");
    w.BeginObject();
    w.KS("data",        data);
    w.KS("userdata",    ud);
    w.KS("gameInstall", paths::RootDir(paths::Root::GameInstall));
    w.KS("gameUserDir", paths::RootDir(paths::Root::GameUserDir));
    w.EndObject();
    bool dataOk = paths::DirExists(data);   // mojibake fix, Jul 22: DirExists (UTF-8 -> wide)
    bool udOk   = paths::DirExists(ud);
    w.KB("paths_ok", dataOk && udOk);
}

// global provider: installed ONCE at init (overlay or harness) and ALWAYS read from the
// game thread (Pump) — no new race conditions, same threading model as the screens.
static CareerStateProvider g_provider;

void SetCareerStateProvider(CareerStateProvider p) { g_provider = std::move(p); }

// Resolves the state view; false -> 'err' already carries the not_ready ready to return.
static bool ResolveView(CareerStateView& v, HandlerResult& err) {
    err.ok = false; err.code = "not_ready";
    if (!g_provider) { err.message = "career engine not initialized"; return false; }
    v = g_provider();
    if (!v.loaded || !v.save || !v.calendar) { err.message = "career save not loaded"; return false; }
    err = HandlerResult{};
    return true;
}

// day-of-year (1..366) -> week of year 1..53 (0 = no date). The continuous clock advances 7 at a time
// (AdvanceWeek), so the week is the natural view of the cursor for the external UI.
static int WeekOfDoy(int doy) { return doy > 0 ? (doy - 1) / 7 + 1 : 0; }

// player's KEY: priority is the CalUiPlayerName bridge (shm) -> save identity -> "Player" (the same
// fallback the overlay uses). hub this is the KEY that standings are minted/matched against (the
// telemetry name, deliberately stable: renaming the card doesn't split the points) -- NOT the display.
static std::string PlayerDisplayName(const CareerStateView& v) {
    if (!v.playerName.empty()) return v.playerName;
    const career::PlayerIdentity& p = v.save->player;
    if (!p.name.empty()) return p.surname.empty() ? p.name : p.name + " " + p.surname;
    return "Player";
}
// hub the player's DISPLAY = their CARD (query.identity, the SAME source the Hub already uses --
// that's why the Hub showed the new name and nothing else did). After a rename, the standings key doesn't
// change (points don't split) but EVERYTHING shown must say the current name: the top-right, your row
// in standings/results/times/season and the profile ranking. "" = provider not registered or an empty
// card -> each caller keeps its usual source.
static std::string PlayerFichaDisplay();   // defined next to g_identityProvider (further down in this TU)

// series display: SeriesById->display falling back to the id (same pattern as EVERY screen).
static std::string SeriesDisplay(const std::string& seriesId) {
    const career::SeriesDef* d = career::SeriesById(seriesId);
    return d ? d->display : seriesId;
}

// first active contract (mirrors CalUiContractFacets/CalUiContractSeries: v1 = the first one).
static const career::Contract* FirstActiveContract(const career::SaveData& s) {
    for (const auto& c : s.contracts) if (c.active) return &c;
    return nullptr;
}

HandlerResult QueryCareerState(const jsonm::Value* /*params*/) {
    HandlerResult r; CareerStateView v;
    if (!ResolveView(v, r)) return r;
    const career::SaveData& s = *v.save;
    const career::CareerCalendar& cal = *v.calendar;

    // eloFame = unified rating (ELO=REP, the living-world economy); a fresh career with no seed -> rookie baseline
    // (exact mirror of ProfilePlayerRatingUi).
    int elo = (int)(s.prog.rating + 0.5);
    if (elo <= 0) elo = (int)career::bal::RATING_PLAYER_SEED;   // fresh, no seed -> player's starting point

    jsonw::Writer w;
    w.BeginObject();
    w.Key("profile"); w.BeginObject();
    // hub the TOP-RIGHT is display -> the card takes over (with a rename it used to show the shm key = old name)
    { std::string f = PlayerFichaDisplay(); w.KS("name", f.empty() ? PlayerDisplayName(v) : f); }
    w.KI("money", s.prog.credits);
    w.KI("eloFame", elo);
    w.KI("xp", s.prog.xp_total);
    w.KI("week", WeekOfDoy(cal.cursor_doy));
    w.KI("year", cal.year);
    w.KI("dateDoy", cal.cursor_doy);
    w.KS("weekday", career::WeekdayLabel(cal.year, cal.cursor_doy));   // real day of the week, additive alongside date/dateDoy
    w.EndObject();
    w.Key("contract");
    if (const career::Contract* c = FirstActiveContract(s)) {
        w.BeginObject();
        w.KS("seriesId", c->series_id);
        w.KS("seriesName", SeriesDisplay(c->series_id));
        w.KS("teamName", c->team_id);
        // engine v1: every contract (works/customer/pay_driver) is a RACE seat; there's no
        // test/reserve role yet. The field is emitted anyway so the JSON contract doesn't change once that arrives.
        w.KB("raceSeat", true);
        w.EndObject();
    } else {
        w.Null();
    }
    w.Key("flags"); w.BeginObject();
    // inSeason: there are entries this year (same criterion as the hub: CalUiEnrollmentCount() > 0)
    w.KB("inSeason", !cal.enrollments.empty());
    // pendingResults: some enrolled season is CLOSED (status==1) with a result still to review
    // (mirrors the "just-closed live seasons" section of CalUiSeasonResults).
    { bool pend = false;
      for (const auto& e : cal.enrollments) if (e.status == 1) { pend = true; break; }
      w.KB("pendingResults", pend); }
    // resultSeq: monotonic settlement counter; the app polls it while in "launched" state -> once it increments,
    // there's a NEW result -> navigate to Results. (Mirrors the overlay's g_settleSeq; 0 in tests with no settlement.)
    w.KI("resultSeq", v.resultSeq);
    // settlePending: there is a settlement ON HOLD (the player is out, the race has not finished yet). The shell
    // must NOT read it as a retirement while this position holds -- the result is on its way. (See CareerStateView.)
    w.KB("settlePending", v.settlePending);
    // calibrated: a freshly created profile (not yet measured) makes the app show ONLY the epic intro for
    // calibration (no tabs) until the calibration race is settled. Explicit false -> the shell can gate on it.
    w.KB("calibrated", s.prog.calibrated);
    // SIMULATION race mode (debug) active — the debug menu (opened with a dedicated key) shows the toggle, and the shell
    // doesn't minimize the app on launch (there's no game to hand focus to).
    w.KB("simRace", v.simRace);
    w.EndObject();
    // per-tab badges: the shell draws a dot with a count over Market/Inbox/Sponsors/Memories.
    w.Key("badges"); w.BeginObject();
    w.KI("market", v.badgeMarket);
    w.KI("inbox", v.badgeInbox);
    w.KI("sponsors", v.badgeSponsors);
    w.KI("memories", v.badgeMemories);
    w.EndObject();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

HandlerResult QueryStandings(const jsonm::Value* params) {
    HandlerResult r; CareerStateView v;
    if (!ResolveView(v, r)) return r;
    const career::SaveData& s = *v.save;
    const career::CareerCalendar& cal = *v.calendar;

    // series: params.seriesId; defaults to the LAST RACE RUN that's still enrolled — it's the one the
    // player just looked at. It used to default to the 1st active contract, which with several open contracts
    // returned the championship for a DIFFERENT series (same family of bug as the Results one). Fallback chain:
    // active contract with an enrollment -> first enrollment.
    std::string sid = params ? params->memberString("seriesId") : std::string();
    const bool explicitSid = !sid.empty();
    if (sid.empty()) {
        for (auto it = s.race_log.rbegin(); it != s.race_log.rend(); ++it)
            if (!it->series_id.empty() && career::FindEnrollment(cal, it->series_id)) { sid = it->series_id; break; }
        for (const auto& c : s.contracts)
            if (sid.empty() && c.active && career::FindEnrollment(cal, c.series_id)) { sid = c.series_id; break; }
        if (sid.empty() && !cal.enrollments.empty()) sid = cal.enrollments.front().series_id;
    }
    if (sid.empty()) {
        r.ok = false; r.code = "not_ready"; r.message = "no active season or contract";
        return r;
    }
    const career::SeasonState* e = career::FindEnrollment(cal, sid);
    if (!e) {
        r.ok = false; r.code = explicitSid ? "bad_params" : "not_ready";
        r.message = "series not enrolled: " + sid;
        return r;
    }

    // THE same sorted table screen_season.cpp draws (FinalTable: total desc, wins, name).
    std::vector<career::StandingRow> table = career::FinalTable(e->standings);
    const std::string me = PlayerDisplayName(v);          // KEY (match of your row; stable across renames)
    const std::string ficha = PlayerFichaDisplay();       // hub DISPLAY of your row (current name)
    const career::Contract* myContract = career::ContractForSeries(s, sid);

    jsonw::Writer w;
    w.BeginObject();
    w.KS("seriesId", sid);
    w.KS("seriesName", SeriesDisplay(sid));
    w.Key("rows"); w.BeginArray();
    for (int i = 0; i < (int)table.size(); i++) {
        const career::StandingRow& row = table[i];
        const bool isPlayer = (row.name == me);
        w.BeginObject();
        w.KI("pos", i + 1);
        // driverId: player -> their persisted player_id; AI -> stable id from the pool ("" if it doesn't resolve),
        // same name->id resolution the engine uses in ComputeNemesis.
        if (isPlayer) w.KS("driverId", s.player.player_id.empty() ? "player" : s.player.player_id);
        // 18/09: the row already says who it is (namesakes); the name only in tables saved before it was known
        else          w.KS("driverId", !row.id.empty() ? row.id : career::DriverIdForName(row.name));
        w.KS("driverName", (isPlayer && !ficha.empty()) ? ficha : row.name);   // hub your row, your CURRENT name
        // teamName: the engine doesn't model a team per AI driver (the live ChampUiStandings emits team="");
        // the player's row carries the team_id from their contract in THIS series, if any.
        w.KS("teamName", (isPlayer && myContract) ? myContract->team_id : std::string());
        w.KI("points", row.total);
        w.KB("isPlayer", isPlayer);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// GeneratedEmail.family -> contract kind ("info|offer|manager|nego"): 0=OFFER -> "offer"; 1 = mail from
// Atenea (the manager persona's milestones/beats) -> "manager"; 2=SPONSOR -> "info"; 3=NEGOTIATION (the negotiation redesign) -> "nego".
static const char* MailKind(int family) {
    if (family == 0) return "offer";
    if (family == 1) return "manager";
    if (family == 3) return "nego";
    return "info";
}
// sender's voice: the SAME TR keys screen_inbox.cpp uses when composing (the app resolves them
// through its own i18n layer, just like the screen resolves them with TR()).
static const char* MailFromKey(int family) {
    if (family == 0) return "mkt_offer_inbox";
    if (family == 2) return "inbox_sponsor";
    return "inbox_paddock";
}

HandlerResult QueryInbox(const jsonm::Value* /*params*/) {
    HandlerResult r; CareerStateView v;
    if (!ResolveView(v, r)) return r;
    const career::SaveData& s = *v.save;
    const career::CareerCalendar& cal = *v.calendar;

    // newest->oldest ordering by vector INDEX descending only — the insertion index IS
    // the timeline (mail is appended in real chronological order, even across career years);
    // day is NO LONGER the sort key. With day-desc, season_preview (emitted on day=0 of the new season) used to sink below
    // the ENTIRE previous season (days 1-365 of the old year share the 0-365 range with the new year -> collision).
    std::vector<int> order(s.emails.size());
    for (int i = 0; i < (int)order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [](int a, int b) {
        return a > b;
    });
    // 18/09 (Josh): days left on a mail's offer (0 = does not apply). Only live offer mails,
    // unanswered and with something to press; neither courtship nor renewal, which do not live in the market.
    const int absHoy = career::dateutil::AbsDayFromYearDoy(cal.year, cal.cursor_doy);
    auto diasDeOferta = [&](const career::GeneratedEmail& e) -> int {
        if (e.family != 0 || e.answered || e.subtype == career::bal::MK_MAIL_SUBTYPE_COURTING) return 0;
        if (e.payload_key.empty() || e.id.rfind("renew_", 0) == 0) return 0;
        if (!s.offer_first_seen.count(e.payload_key)) return 0;
        if (career::MailActionsFor(e, s, cal.year, cal.cursor_doy).empty()) return 0;
        const int d = career::OfferDeathAbs(s, e.payload_key, absHoy) - absHoy;
        return d > 0 ? d : 0;
    };
    // ...and the eve of expiry, at the VERY TOP: that day the mail warns again (ReopenOfferMailsOnLastDay),
    // and a warning buried under three weeks of mail warns of nothing. The rest keeps its order.
    std::stable_partition(order.begin(), order.end(),
                          [&](int i) { return diasDeOferta(s.emails[i]) == 1; });

    jsonw::Writer w;
    w.BeginObject();
    w.Key("messages"); w.BeginArray();
    for (int idx : order) {
        const career::GeneratedEmail& e = s.emails[idx];
        w.BeginObject();
        w.KS("id", e.id);
        w.KI("week", WeekOfDoy(e.day));
        // fix for "mail with an absurd date across a year rollover" (2026-07-23): e.year is the REAL year the
        // mail was sent (persisted alongside the day-of-year). e.year<=0 only happens for mail from a save PRE-fix
        // (the field was never written) -- the ONLY case where the calendar's CURRENT year is the best available
        // data (before this fix, EVERY mail always used this fallback, hence the bug: as soon as
        // the career crossed a year boundary, an old mail would repaint with the year ALREADY ADVANCED).
        w.KI("year", e.year > 0 ? e.year : cal.year);
        w.KS("from", MailFromKey(e.family));            // legacy fallback (mail with no persisted from_name)
        if (!e.from_name.empty())     w.KS("fromName", e.from_name);
        if (!e.from_role_key.empty()) w.KS("fromRole", e.from_role_key);
        // keys resolved to a variant (bilingual via TR: the app interpolates the params).
        // courting (subtype COURTING) falls into the generic branch -> courting_subj/vN, NOT the
        // fixed offer template (family 0 WITHOUT courting still uses the historical fixed key).
        if (e.family == 0 && e.subtype != career::bal::MK_MAIL_SUBTYPE_COURTING) {
            // 0.4.0 in-game fix (feedback from Jul 21): RENEWAL/BID mail (id "renew_...") used to use the
            // generic "Contract offer: {series}" template — with the renewal plus 2 rival bids arriving
            // TOGETHER at rollover, three identical mails were indistinguishable, and none of them said they get signed FROM
            // the mail itself (they don't go through the Market). Each type now gets its own template (same family/actions).
            bool isRenewMail = e.id.rfind("renew_", 0) == 0;
            bool isRivalMail = isRenewMail && e.payload_key.find("#rv") != std::string::npos;
            bool isEarlyMail = isRenewMail && e.payload_key.find("#early") != std::string::npos;
            if (isRivalMail)      { w.KS("subject", "inbox_rival_subj");       w.KS("body", "inbox_rival_body"); }
            else if (isEarlyMail) { w.KS("subject", "inbox_renew_early_subj"); w.KS("body", "inbox_renew_early_body"); }
            else if (isRenewMail) { w.KS("subject", "inbox_renew_subj");       w.KS("body", "inbox_renew_body"); }
            else                  { w.KS("subject", "inbox_offer_subj");       w.KS("body", "inbox_offer_body"); }
        }
        else { w.KS("subject", career::MailSubjectKey(e.payload_key, e.variant));
               w.KS("body",    career::MailBodyKey(e.payload_key, e.variant)); }
        w.Key("params"); w.BeginObject();
        for (const auto& kv : career::MailParamsDecode(e.params)) w.KS(kv.first.c_str(), kv.second);
        w.EndObject();
        w.KB("read", e.read != 0);
        w.KS("kind", MailKind(e.family));
        // inbox redesign: stable family + instant actions + resolution. Fields
        // are optional: the app tolerates their absence (old DLLs/saves), fromName pattern.
        w.KS("family", career::MailFamilyToken(e.family));
        if (const int dias = diasDeOferta(e)) w.KI("expiresInDays", dias);   // 18/09: 1 = last day to respond
        // an offer that's no longer on the table gets STATED, not left in silence. MailActionsFor stops
        // offering buttons (see MailOfferExpired), and this flag is what the screen uses to show "offer
        // expired" where the button used to be — if it just vanished, the mail would look broken.
        const int expiryReason = career::MailOfferExpiryReason(e, s, cal.year, cal.cursor_doy);
        if (expiryReason != career::MAIL_OFFER_LIVE) {
            w.KB("expired", true);
            // and the WHY: "your deadline ran out" and "the market is closed" are different things, and the
            // second one is worth waiting out (it reopens in September). Without this, the gap where the
            // button used to be says it can't be done, but not whether there's anything to do about it.
            if (const char* tok = career::MailOfferExpiryToken(expiryReason)) w.KS("expiredReason", tok);
        }
        if (!e.answered) {
            std::vector<career::MailAction> acts = career::MailActionsFor(e, s, cal.year, cal.cursor_doy);
            if (!acts.empty()) {
                w.Key("actions"); w.BeginArray();
                for (const auto& a : acts) { w.BeginObject(); w.KI("choice", a.choice);
                    w.KS("label", a.label_key); w.KS("kind", a.kind); w.EndObject(); }
                w.EndArray();
            }
        } else if (const char* rk = career::MailResolvedKey(e)) {
            w.KS("resolvedKey", rk);
        }
        // the offer's action surface — offerId opens the composer; deadlineWeeks = the remaining TTL
        // since offer_first_seen; courting only navigates to Market.
        // the gate is that the offer must STILL be actionable (MailActionsFor not empty)
        // > a decline/sign done from the Market (declined_offers / a contract in that series) hides Negotiate+chip
        // instantly, without waiting for the mail to be answered. Doesn't apply to a thread's offerId (family 3).
        // A RENEWAL mail (id "renew_") does NOT expose an offerId: its renewal doesn't live in the live
        // market (query.negotiate would bounce with found:false); it's signed via an instant action (cmd.inbox.answer).
        bool isRenewalMail = e.id.rfind("renew_", 0) == 0;
        // in-game fix: "Negotiate" is a composer CTA, not merely a sign that an offer exists — the
        // negotiation redesign (2026-07-20) has a binary negotiable gate per offer (career_nego_thread.cpp,
        // NegoNegotiable, the SAME source as query.market and the emit above in career_contract.cpp). This gate is
        // STRICT: only param "negotiable"=="1" opens the composer; "0" or ABSENT (an old mail not yet backfilled
        // by the sync pass) -> no offerId, but Accept/Decline stay available via MailActionsFor below.
        bool negotiable = false;
        for (const auto& kv : career::MailParamsDecode(e.params))
            if (kv.first == "negotiable") { negotiable = (kv.second == "1"); break; }
        if (e.family == 0 && !e.answered && e.subtype != career::bal::MK_MAIL_SUBTYPE_COURTING
            && !e.payload_key.empty() && !isRenewalMail && negotiable
            && !career::MailActionsFor(e, s, cal.year, cal.cursor_doy).empty()) {
            w.KS("offerId", e.payload_key.c_str());
            auto it = s.offer_first_seen.find(e.payload_key);
            if (it != s.offer_first_seen.end()) {
                int wl = career::WeeksLeftFromFirstSeen(it->second, career::NegoAbsDay(cal.year, cal.cursor_doy));
                if (wl >= 0) w.KI("deadlineWeeks", wl);
            }
        }
        if (e.family == 0 && e.subtype == career::bal::MK_MAIL_SUBTYPE_COURTING) w.KS("nav", "market");
        // the treasury alert (family TEAMOPS=8, subtype 2) navigates straight to the garage
        // (selling/managing cars is the action that resolves the alert) — same pattern as courting -> Market.
        // Only while it's still UNANSWERED (the MailActionsFor/answered pattern already used above for offerId).
        if (e.family == 8 && e.subtype == 2 && !e.answered) w.KS("nav", "garage");
        // family INVITE (7) — eventId (subtype 1, event) + deadlineWeeks (both subtypes).
        // Same pattern as offerId/deadlineWeeks for family 0 above: only while the mail is still
        // unanswered — once it's answered or closed by expiry, the decision is no longer on the table.
        if (e.family == 7 && !e.answered) {
            career::MailParams ip = career::MailParamsDecode(e.params);
            if (e.subtype == 1) {
                // subtype 1 (event): eventId from the "event_id" param (EmitEventInviteMail); deadline =
                // weeks until ends_at (a real epoch timestamp) vs now — the same real clock that closes it.
                for (const auto& kv : ip) if (kv.first == "event_id") { w.KS("eventId", kv.second); break; }
                for (const auto& kv : ip) {
                    if (kv.first != "ends_at") continue;
                    long endsAt = atol(kv.second.c_str());
                    long now = (long)time(nullptr);
                    long leftSec = endsAt - now;
                    int wl = leftSec > 0 ? (int)((leftSec + 6L * 86400) / (7L * 86400)) : 0;
                    w.KI("deadlineWeeks", wl);
                    break;
                }
            } else if (e.subtype == 0 && s.sub_ride.state != 0) {
                // subtype 0 (stand-in): deadline = days until event_absday from the calendar cursor, in
                // weeks (same rounding as WeeksLeftFromFirstSeen). Cross-check between
                // mail and sub_ride by reconstructed id (same defensive idiom as ApplySubRideAnswer).
                std::string expectedId = "subride_" + s.sub_ride.series_id + "_"
                                        + std::to_string(s.sub_ride.event_absday);
                if (e.id == expectedId) {
                    int absToday = career::dateutil::AbsDayFromYearDoy(cal.year, cal.cursor_doy);
                    int left = s.sub_ride.event_absday - absToday;
                    w.KI("deadlineWeeks", left > 0 ? (left + 6) / 7 : 0);
                }
            }
        }
        // subReady — sub_ride ACCEPTED (state==2) and TODAY <= event_absday
        // (the SAME condition as SubRideArm, screen_calendar.cpp). The original [absday-1, absday] window was
        // UNREACHABLE: the app's only time control (cmd.time.advanceToNextEvent) jumps between the
        // player's OWN events, skipping right over someone else's stand-in round by definition — the cursor
        // never landed on exactly those two days. Fix: the window opens on ACCEPTANCE, not on the
        // eve before — any day from acceptance through race day (inclusive) arms it. It's an
        // out-of-championship exhibition, so it doesn't require exact-day fidelity (same criterion as the
        // special events, armable throughout their whole window). It deliberately lives OUTSIDE the !e.answered guard above,
        // because by the time the stand-in is armable, the mail is ALREADY answered (choice==1,
        // resolvedKey invite_sub_res_accept) — the "take the wheel" CTA hangs off the already-closed mail, not off
        // one with pending actions. Cross-check between mail and sub_ride by reconstructed id
        // (same defensive idiom as the deadlineWeeks block above).
        if (e.family == 7 && e.subtype == 0 && e.answered && e.choice == 1 && s.sub_ride.state == 2) {
            std::string expectedId = "subride_" + s.sub_ride.series_id + "_"
                                    + std::to_string(s.sub_ride.event_absday);
            if (e.id == expectedId) {
                int absToday = career::dateutil::AbsDayFromYearDoy(cal.year, cal.cursor_doy);
                if (absToday <= s.sub_ride.event_absday) {
                    w.KB("subReady", true);
                }
            }
        }
        // negotiation redesign bridge: negId only if the mail belongs to a thread; you=false ALWAYS
        // on the persisted branch (saved mail is always sent by the team/app, never the player).
        if (!e.neg_id.empty()) {
            w.KS("negId", e.neg_id);
            // a THREAD mail's offerId is ALWAYS taken from the
            // LIVE negotiation by neg_id — the real neg_id carries year+absDay suffixes (career_nego_thread.cpp) and can NOT
            // be derived client-side by slicing. If the thread was purged (rollover) there's no offerId -> the thread's
            // CTA disappears (correct). The family-0 gate from MailActionsFor doesn't apply here: this is family 3.
            for (const auto& n : s.negotiations)
                if (n.neg_id == e.neg_id) { w.KS("offerId", n.offer.id); break; }
        }
        w.KB("you", false);
        w.EndObject();
    }
    // player entries: synthesized from history, NEVER persisted as mail (the same bilingual-key
    // analog as elsewhere — the body isn't stored, it's re-resolved when served from the live negotiation thread).
    for (const auto& n : s.negotiations) {
        // with no persisted mail for this thread, there's nothing to synthesize (a pruned/purged thread,
        // e.g. after rollover's clear()); without this guard, orphaned "you" entries render with no context.
        bool anyMail = false;
        for (const auto& e : s.emails) if (e.neg_id == n.neg_id) { anyMail = true; break; }
        if (!anyMail) continue;
        int ri = 0;
        for (const auto& h : n.history) {
            ri++;
            if (h.who != 0 || h.kind != 0) continue;             // only proposals SENT by the player
            w.BeginObject();
            w.KS("id", (n.neg_id + "_you_r" + std::to_string(ri)).c_str());
            // fix for "mail with an absurd date across a year rollover" (audited 2026-07-23): h.day is an ABSOLUTE day,
            // a real one (career_dateutil, not a year*365+doy approximation) -- it already carries the year this
            // specific proposal was made in. Before this fix, that data was thrown away and the CURRENT calendar
            // year (cal.year) was rendered instead — the SAME underlying bug as GeneratedEmail.day/year above: a
            // thread surviving a year rollover would repaint its old rounds with the year already advanced.
            int hyear, hdoy; career::dateutil::YearDoyFromAbsDay(h.day, hyear, hdoy);
            w.KI("week", WeekOfDoy(hdoy));
            w.KI("year", hyear);
            w.KS("from", "inbox_you");
            w.KS("subject", "nego_you_subj");
            w.KS("body", "nego_you");
            w.Key("params"); w.BeginObject();
            w.KS("team", n.offer.team.c_str());
            {   // {series} for the nego_you_subj subject (fix: the placeholder had no source)
                std::string sid = (n.offer.id.rfind("of_", 0) == 0) ? n.offer.id.substr(3) : n.offer.id;
                const career::SeriesDef* sd = career::SeriesById(sid);
                w.KS("series", sd ? sd->display.c_str() : sid.c_str());
            }
            w.EndObject();
            w.KB("read", true);
            w.KS("kind", "nego");
            w.KS("family", "nego");
            w.KS("negId", n.neg_id.c_str());
            w.KS("offerId", n.offer.id.c_str());   // the synthetic "you" entry also opens the composer with the real offer
            w.KB("you", true);
            w.EndObject();
        }
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// ===== query.profile ========================================================================
// ImU32 -> "#rrggbb" (IM_COL32 packs R in the low byte). For non-theme colors (class color).
static std::string CssHex(ImU32 c) {
    unsigned r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    char buf[8]; snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return std::string(buf);
}
// Maps the theme's ImU32 color (theme.h) to the app's CSS variable (design-system.css). The engine
// is the source of the color (band/rival/class); the app just renders the string. Theme color -> "var(--x)";
// arbitrary color (e.g. a class color from FindClass) -> "#rrggbb" (both are valid CSS).
static std::string CssVar(ImU32 c) {
    if (c == Col::accent) return "var(--accent)";
    if (c == Col::gold)   return "var(--gold)";
    if (c == Col::green)  return "var(--green)";
    if (c == Col::cyan)   return "var(--cyan)";
    if (c == Col::amber)  return "var(--amber)";
    if (c == Col::purple) return "var(--purple)";
    if (c == Col::danger) return "var(--danger)";
    if (c == Col::text2)  return "var(--text-2)";
    if (c == Col::text3)  return "var(--text-3)";
    return CssHex(c);
}
// Band letter (RatingBandView.key: S/A..F) -> i18n key for the band NAME that the app resolves.
// The EN/ES literals live in the engine (career_profile.cpp) for the overlay; the app translates them via t().
static const char* BandNameKey(const char* letter) {
    if (!letter || !letter[0]) return "";
    switch (letter[0]) {
        case 'S': return "profile.band.legend";
        case 'A': return "profile.band.elite";
        case 'B': return "profile.band.established";
        case 'C': return "profile.band.competent";
        case 'D': return "profile.band.prospect";
        case 'E': return "profile.band.rookie";
        case 'F': return "profile.band.unknown";
    }
    return "";
}

static ProfileViewProvider g_profileProvider;
void SetProfileViewProvider(ProfileViewProvider p) { g_profileProvider = std::move(p); }

HandlerResult QueryProfile(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_profileProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string filter = params ? params->memberString("filter") : std::string();
    // world-ranking "show more". The top-10 + neighborhood slice is what renders in the
    // Profile panel; with full=true, the FULL table (worldRanking.all) is ALSO emitted for the modal. On demand, because
    // the entire list is hundreds of rows and shouldn't travel on every panel poll.
    const bool full = params ? params->memberBool("full", false) : false;
    ProfileView v = g_profileProvider(filter);
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    const int score = v.rating.score;
    // world-ranking slice: top-10 + a ±2 window around the player if they're outside the top,
    // same calculation as screen_profile.cpp). meInTop -> no neighborhood needed.
    const int total = (int)v.ranked.size();
    const int meIdx = v.meIdx;
    const int kTop = 10, kNbr = 2;
    int topRows = total < kTop ? total : kTop;
    bool meInTop = (meIdx >= 0 && meIdx < kTop);
    int nbLo = -1, nbHi = -1;
    if (!meInTop && meIdx >= 0) {
        nbLo = meIdx - kNbr; if (nbLo < topRows) nbLo = topRows;   // never overlaps the top block
        nbHi = meIdx + kNbr; if (nbHi > total - 1) nbHi = total - 1;
    }

    jsonw::Writer w;
    auto emitRow = [&](int idx0) {
        const career::RankRow& d = v.ranked[idx0];
        w.BeginObject();
        w.KI("rank", idx0 + 1);                                   // real 1-based world position
        { std::string f = d.you ? PlayerFichaDisplay() : std::string();
          w.KS("name", f.empty() ? d.name : f); }   // hub your ranking row, your CURRENT name
        w.KI("rating", d.rating);
        w.KS("bandLetter", d.band.key ? d.band.key : "");
        w.KS("bandColor", CssVar(d.band.color));
        w.KB("isPlayer", d.you);
        if (d.you && !v.nationality.empty()) w.KS("nationality", v.nationality);
        w.EndObject();
    };

    w.BeginObject();
    w.KS("playerName", v.playerName);

    // hero: score + delta + world rank + band + next band
    w.Key("rating"); w.BeginObject();
    w.KI("score", score);
    w.KI("delta", v.rating.delta);
    // stable GLOBAL rank (independent of the table's series filter); falls back to the filtered one.
    w.KI("worldRank", v.worldRankGlobal > 0 ? v.worldRankGlobal : (meIdx >= 0 ? meIdx + 1 : 0));
    w.Key("band"); w.BeginObject();
    w.KS("letter", v.rating.band.key ? v.rating.band.key : "");
    w.KS("nameKey", BandNameKey(v.rating.band.key));
    w.KS("color", CssVar(v.rating.band.color));
    w.EndObject();
    w.Key("next");
    if (v.rating.next_key && v.rating.next_key[0]) {
        w.BeginObject();
        w.KI("gap", v.rating.next_min - score);
        w.KS("letter", v.rating.next_key);
        w.KS("nameKey", BandNameKey(v.rating.next_key));
        w.EndObject();
    } else {
        w.Null();
    }
    w.EndObject();

    // arc: the last point carries the i18n key "profile.now" (the engine produces "Now"/"Ahora"); the rest
    // are literal year labels ('25) that the app renders as-is (missing key -> fallback).
    w.Key("arc"); w.BeginArray();
    for (int i = 0; i < (int)v.arc.size(); i++) {
        bool last = (i == (int)v.arc.size() - 1);
        w.BeginObject();
        w.KS("season", last ? std::string("profile.now") : v.arc[i].season);
        w.KI("rating", v.arc[i].rating);
        w.EndObject();
    }
    w.EndArray();
    w.KI("arcDelta", v.arc.empty() ? 0 : (score - v.arc.front().rating));

    // rivals: color and note are presentation-translatable (CSS var + i18n key)
    w.Key("rivals"); w.BeginArray();
    for (const auto& rv : v.rivals) {
        w.BeginObject();
        w.KS("name", rv.name);
        w.KS("team", rv.team);
        w.KI("rating", rv.rating);
        w.KI("h2hWins", rv.h2h_w);
        w.KI("h2hLosses", rv.h2h_l);
        w.KS("noteKey", rv.note_key ? rv.note_key : "");
        w.KS("color", CssVar(rv.color));
        w.EndObject();
    }
    w.EndArray();

    w.Key("recentForm"); w.BeginArray();
    for (const auto& fe : v.recentForm) { w.BeginObject(); w.KI("pos", fe.pos); w.KB("finished", fe.finished); w.EndObject(); }
    w.EndArray();

    w.Key("palmares"); w.BeginObject();
    w.KI("wins", v.palmares.wins);
    w.KI("podiums", v.palmares.podiums);
    w.KI("races", v.palmares.races);
    w.KI("titles", v.palmares.titles);
    w.KI("xp", v.palmares.xp);
    w.KI("age", v.palmares.age);
    w.EndObject();

    w.Key("worldRanking"); w.BeginObject();
    w.Key("top"); w.BeginArray();
    for (int i = 0; i < topRows; i++) emitRow(i);
    w.EndArray();
    w.Key("neighborhood"); w.BeginArray();
    if (nbLo >= 0 && nbHi >= nbLo) for (int i = nbLo; i <= nbHi; i++) emitRow(i);
    w.EndArray();
    // total is ALWAYS sent (the app decides whether to offer "show more"); `all` ONLY with full=true.
    w.KI("total", total);
    if (full) {
        w.Key("all"); w.BeginArray();
        for (int i = 0; i < total; i++) emitRow(i);
        w.EndArray();
    }
    w.EndObject();

    w.Key("filter"); w.BeginObject();
    w.Key("options"); w.BeginArray();
    for (const auto& o : v.filterOptions) {
        w.BeginObject();
        w.KS("id", o.id);
        // "" -> i18n key "profile.filterAll" (All); otherwise, the series' literal name.
        w.KS("label", o.id.empty() ? std::string("profile.filterAll") : o.display);
        w.EndObject();
    }
    w.EndArray();
    w.KS("selectedId", v.selectedFilter);
    w.EndObject();

    w.EndObject();
    r.payload = w.Str();
    return r;
}

// ===== query.memories =======================================================================
static MemoriesInputsProvider g_memoriesProvider;
void SetMemoriesInputsProvider(MemoriesInputsProvider p) { g_memoriesProvider = std::move(p); }

// era lore (docs/career/memory_lore.json) — {series_id:{desc_es,desc_en,highlight_es,highlight_en}}.
struct MemLore { std::string desc_es, desc_en, hl_es, hl_en; };
static std::map<std::string, MemLore> g_memLore;
int LoadMemoryLore(const char* careerDir) {
    g_memLore.clear();
    if (!careerDir || !*careerDir) return 0;
    std::string path = careerDir;
    if (!path.empty() && (path.back() == '\\' || path.back() == '/')) path.pop_back();
    path += "\\memory_lore.json";
    std::string err;
    jsonm::ValuePtr root = jsonm::ParseFile(path.c_str(), &err);
    if (!root || !root->isObject()) return 0;
    int n = 0;
    for (const auto& kv : root->obj) {
        const jsonm::Value* v = kv.second.get();
        if (!v || !v->isObject()) continue;
        MemLore m;
        m.desc_es = v->memberString("desc_es"); m.desc_en = v->memberString("desc_en");
        m.hl_es   = v->memberString("highlight_es"); m.hl_en = v->memberString("highlight_en");
        g_memLore[kv.first] = m; n++;
    }
    return n;
}
// ancestor PER DECADE. Deterministic name from (surname + decade); the relation (father..great-great-grandfather)
// is anchored to the player's AGE. One ancestor per decade raced EVERY series from that era.
static unsigned memFnv1a(const std::string& s) { unsigned h = 2166136261u; for (char c : s) { h ^= (unsigned char)c; h *= 16777619u; } return h; }
static std::string ancestorFirstName(const std::string& surname, int decade) {
    static const char* kFirst[] = { "Aldo","Marco","Dar\xC3\xADo","Emil","Otto","Sergio","Enzo","Bruno","Le\xC3\xB3n","\xC3\x8Dker","Gaspar","Tom\xC3\xA1s","Nicol\xC3\xA1s","Rafael","Vittorio","Klaus" };
    unsigned h = memFnv1a(surname + "|" + std::to_string(decade));
    return kFirst[h % (sizeof(kFirst) / sizeof(kFirst[0]))];
}
static int ancestorGen(int decade, int playerBirthYear) {
    int by = playerBirthYear > 0 ? playerBirthYear : 2000;
    int fatherEra = by + 4;                     // the father in his prime shortly after you were born
    int decadeMid = decade + 5;
    int gen = 1 + (fatherEra - decadeMid + 11) / 22;   // ~22 years/generation (fatherEra>decadeMid always under 2000)
    if (gen < 1) gen = 1; if (gen > 4) gen = 4;
    return gen;
}
static const char* ancestorRelationKey(int gen) {
    switch (gen) { case 1: return "father"; case 2: return "grandfather"; case 3: return "greatGrandfather"; default: return "greatGreatGrandfather"; }
}
static const char* ancestorUncleKey(int gen) {
    switch (gen) { case 1: return "uncle"; case 2: return "greatUncle"; case 3: return "greatGreatUncle"; default: return "greatGreatGreatUncle"; }
}
// see career_api_queries.h — the most recent decade in each generation is the direct line; the rest
// are uncles. That way the tree is a FAMILY (one father, one grandfather...) rather than two fathers with different names.
std::map<int, std::string> AncestorRelationsForDecades(const std::vector<int>& decades, int playerBirthYear) {
    std::map<int, int> genOf;                    // decade -> generation
    std::map<int, int> newestOfGen;              // generation -> most recent decade
    for (int d : decades) {
        int g = ancestorGen(d, playerBirthYear);
        genOf[d] = g;
        auto it = newestOfGen.find(g);
        if (it == newestOfGen.end() || d > it->second) newestOfGen[g] = d;
    }
    std::map<int, std::string> out;
    for (const auto& kv : genOf)
        out[kv.first] = (newestOfGen[kv.second] == kv.first) ? ancestorRelationKey(kv.second)
                                                             : ancestorUncleKey(kv.second);
    return out;
}

HandlerResult QueryMemories(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_memoriesProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    MemoriesInputs in = g_memoriesProvider();
    if (!in.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }
    const int cur = in.cursorDoy;
    const int frontier = in.frontierPhase;

    // Gather the era series (the SAME criterion as screen_memories.cpp: pre-2000, non-qualifying),
    // sorted by year asc then display asc. State derived from round + availability + next round.
    struct Era { const career::SeriesDef* sd; int year; std::vector<career::CalRound> cal; bool locked; };
    std::vector<Era> items;
    for (const auto& sd : career::AllSeries()) {
        if (sd.kind == "standalone") continue;              // qualifying series aren't memories
        if (career::IsEventOnlySeries(sd)) continue;        // 01/10: event-only neither
        if (!career::IsEraMemorySeries(sd)) continue;       // pre-2000 era only (shared predicate)
        Era e; e.sd = &sd; e.year = atoi(sd.era.c_str());
        // dates PROJECTED onto the LIVE year (in.year), not CalendarForSeries (raw, based on kStartYear) --
        // roundState/availNow/nextDoy compare the day-of-year against the live clock cursor; at kStartYear it's an identity mapping.
        e.cal = career::CalendarForSeriesInYear(sd.id, in.year);
        e.locked = in.ownedClasses.count(sd.class_name) == 0;   // Memories: locked until you own its car
        items.push_back(std::move(e));
    }
    std::sort(items.begin(), items.end(), [](const Era& a, const Era& b) {
        return a.year < b.year || (a.year == b.year && a.sd->display < b.sd->display);
    });
    // locked series -> round "locked"; otherwise, cursor reached the day -> "open"; future -> "upcoming".
    auto roundState = [&](const Era& e, const career::CalRound& rr) -> const char* {
        if (e.locked) return "locked";
        if (rr.doy > 0 && cur >= rr.doy) return "open";
        return "upcoming";
    };
    // rounds within ±7 days of the cursor (0 if locked) — wraps the year (for the "N this week" chip).
    auto availNow = [&](const Era& e) -> int {
        if (e.locked) return 0; int n = 0;
        for (const auto& rr : e.cal) { if (rr.doy <= 0) continue; int dd = cur - rr.doy; if (dd < 0) dd = -dd; if (dd > 183) dd = 365 - dd; if (dd <= 7) n++; }
        return n;
    };
    // highlighted next round: the first with day-of-year>=cursor; if none, the first on the calendar.
    auto nextDoy = [&](const Era& e) -> int {
        int best = 0; for (const auto& rr : e.cal) if (rr.doy >= cur) { best = rr.doy; break; }
        if (best == 0 && !e.cal.empty()) best = e.cal.front().doy; return best;
    };

    jsonw::Writer w;
    w.BeginObject();
    w.KS("lineage", in.lineage);
    w.KI("frontierPhase", frontier);
    w.KB("giftPending", in.giftPending);
    w.KI("giftVehHash", in.giftVehHash);
    w.KI("giftLivery", in.giftLivery);
    w.KS("giftClass", in.giftClass);
    // relations PER FAMILY — a pre-pass over all present decades so that within a
    // generation only the most recent decade is the direct line, and the rest come out as uncles.
    std::map<int, std::string> relByDecade;
    { std::vector<int> decades;
      for (const auto& e : items) decades.push_back((e.year / 10) * 10);
      relByDecade = AncestorRelationsForDecades(decades, in.playerBirthYear); }
    w.Key("series"); w.BeginArray();
    for (const auto& e : items) {
        int nd = nextDoy(e);
        const career::CalRound* nr = nullptr;
        for (const auto& rr : e.cal) if (rr.doy == nd) { nr = &rr; break; }
        // nextRoundOpen: open (cursor reached it, not locked) or there's a round available this week.
        bool nextOpen = nr && ((!e.locked && nr->doy > 0 && cur >= nr->doy) || availNow(e) > 0);

        w.BeginObject();
        w.KS("id", e.sd->id);
        w.KS("era", e.sd->era);
        w.KI("year", e.year);
        w.KS("name", e.sd->display);
        w.KS("region", e.sd->region);
        w.KS("discipline", career::DiscForSeries(e.sd));   // raw token; the app uppercases it
        w.KI("level", e.sd->level);
        // Memories: ownership + car price (showroom listing) + active event discount.
        w.KB("owned", in.ownedClasses.count(e.sd->class_name) > 0);
        w.KI("carPrice", (int)career::CheapestCarPriceForClass(e.sd->class_name));
        w.KI("discountPct", career::VehicleDiscountForClass(e.sd->class_name));
        // ancestor (by decade) + the series' historical lore. The relation comes from the
        // per-family pre-pass (direct line or uncle), not from the bare generation — no more "two fathers".
        { int decade = (e.year / 10) * 10;
          std::string first = ancestorFirstName(in.lineage, decade);
          w.KS("ancestorName", in.lineage.empty() ? first : (first + " " + in.lineage));
          auto rit = relByDecade.find(decade);
          w.KS("ancestorRelation", rit != relByDecade.end() ? rit->second : std::string("ancestor"));
          auto lit = g_memLore.find(e.sd->id);
          if (lit != g_memLore.end()) {
              w.KS("loreDescEs", lit->second.desc_es); w.KS("loreDescEn", lit->second.desc_en);
              w.KS("loreHlEs",   lit->second.hl_es);   w.KS("loreHlEn",   lit->second.hl_en);
          } }
        w.KB("locked", e.locked);
        w.KI("availableThisWeek", availNow(e));
        if (nr) w.KI("nextRoundNumber", nr->round); else { w.Key("nextRoundNumber"); w.Null(); }
        w.KB("nextRoundOpen", nextOpen);
        w.Key("rounds"); w.BeginArray();
        for (const auto& rr : e.cal) {
            // Tester feedback (Aug 28, hub): the round reports the track that's ACTUALLY RACED -- when the circuit isn't available, the
            // same substitution (and group anchor) as the master calendar is applied, instead of the original.
            career::CalRound r2 = rr; career::ApplyDlcToRound(r2, e.sd);
            w.BeginObject();
            w.KI("round", r2.round);
            w.KI("doy", r2.doy);
            w.KS("date", r2.doy > 0 ? career::DoyToDate(r2.doy) : std::string());
            w.KS("trackName", r2.track_name);
            w.KI("trackHash", r2.track_hash);
            w.KS("trackSlug", career::SlugForTrackEnsured(r2.track_hash));   // circuit map (PNG in data/)
            w.KS("state", roundState(e, rr));
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// ===== query.calendar =======================================================================
static CalendarViewProvider g_calendarProvider;
void SetCalendarViewProvider(CalendarViewProvider p) { g_calendarProvider = std::move(p); }

// An era's year counts ONLY if it's a 4-digit number in (1900,2100) — the SAME criterion as yearFromEra in
// screen_calendar.cpp. Eras with a decade suffix ("1960s") do NOT count as a year (-> treated as modern), same as
// IsEraMemorySeries. atoi("1960s")=1960 would wrongly mark it "historic": you have to require exactly 4 digits.
static int EraYear4Digit(const std::string& era) {
    if (era.size() != 4) return 0;
    for (char c : era) if (c < '0' || c > '9') return 0;
    int y = atoi(era.c_str());
    return (y > 1900 && y < 2100) ? y : 0;
}

// Cars ON TRACK for that round — the SAME count the launch uses, not a third one. It used to be
// `min(liveries, 26)` per class, so the app's calendar card promised 26 cars in races that
// actually run with 12. Multiclass -> the whole meeting (36 in the IVORY_CUP_12573). <2 = a class with no liveries -> 16, as before.
// the meeting is already resolved (the same one that decides this slot's multiclass badge): that way the number
// the app shows comes from the SAME count as the launch, not a second derivation that could drift apart.
// NOTE: composing a grid costs a measured 0.036 ms, and this gets asked PER SLOT — a calendar of
// 1200 cells would be 43 ms on every query, and the app queries often. The epoch invalidates the memo when
// data is reloaded; the effective hash distinguishes a DLC replacement from the originally authored track.
static int GridForSlot(const std::vector<std::string>& cita, const std::string& seriesId, int round,
                       int effectiveTrackHash) {
    static std::map<std::string, int> memo;
    static unsigned long long memoEpoch = 0;
    const unsigned long long epoch = career::DataEpoch();
    if (memoEpoch != epoch) { memo.clear(); memoEpoch = epoch; }
    const std::string k = seriesId + "#" + std::to_string(round) + "#" +
                          std::to_string(effectiveTrackHash);
    auto it = memo.find(k);
    if (it != memo.end()) return it->second;
    // 16/09: bounded by the circuit's slots for THAT round, same as the launch. Without the cap here, the
    // app card would promise 40 cars in a race that starts with 32 — the same divorce this already brought
    // to close once.
    const int requested = career::RoundGridSizeOverride(cita, seriesId, round);
    int n = career::RoundGridSize(cita, seriesId, effectiveTrackHash, requested);
    if (n < 2) n = 16;
    memo[k] = n;
    return n;
}

// year-continuity: a cap on PROJECTED rounds from next year shown queued after
// the current year's rounds (a deliberate choice: "current year + a queue of the next", NOT continuous scrolling
// across multiple years).
static const int kNextYearQueueCap = 6;

// Writes ONE slot in the query.calendar format. Extracted so the SAME emission logic is reused for the current
// year and for the informational next-year queue: `yearForRound` is the year announced in
// the JSON (v.year for the current year; v.year+1 for the queue), and `forceNotClickable` unconditionally
// turns off clickable/isAvailable for the queue (a year+1 round is NEVER launchable TODAY, no matter what
// its status flags say). `meetingCtx` is the universe "multiclass" is computed over (MeetingClassSet
// groups by date/track WITHIN that universe): v.slots for the current year, v.nextYearSlots for
// the queue — mixing them would compare days-of-year from different years as if they were the same weekend.
// ADDITIVE change: for the current year (forceNotClickable=false) this is BIT-FOR-BIT the same write
// as before this change, with only "year" added.
static void WriteCalendarSlot(jsonw::Writer& w, const career::CalSlot& s,
                               const std::vector<career::CalSlot>& meetingCtx,
                               int yearForRound, bool forceNotClickable) {
    const career::SeriesDef* sd = career::SeriesById(s.series_id);
    const std::string seriesName = sd ? sd->display    : s.series_id;
    const std::string className  = sd ? sd->class_name : s.series_id;
    const std::string region     = sd ? sd->region     : std::string();
    const std::string discipline = sd ? career::DiscForSeries(sd) : std::string("single");
    const int eraYear = sd ? EraYear4Digit(sd->era) : 0;
    const bool historic = eraYear > 0 && eraYear < 2000;   // 4-digit era < 2000 (== the screen's vint)
    // resolved state (priority ladder identical to screen_calendar.cpp)
    const char* status = s.raced        ? "completed"
                       : s.dnf          ? "dnf"
                       : s.locked       ? "locked"
                       : s.travel_locked? "traveled"   // bis: you already raced elsewhere today (one race per day)
                       : s.invite_locked? "invite"     // hub closed grid without a contract ("by invitation")
                       : s.conflict     ? "clash"
                       : s.enrolled     ? "enrolled"
                       : s.past         ? "past"
                       :                  "upcoming";
    // hub invite_locked joins the availability check -- the engine used to reject these series on
    // launch (launchSlot via CanEnterSeries) while the JSON painted them clickable: the click died SILENTLY
    // in the 14 series closed (F1/IVORY_CUP_12573/PINNACLE_CHALLENGE_5480/Keystone_Cup_6095/heritage works). Now clickable == what the engine accepts.
    const bool isAvailableRaw = !(s.locked || s.gate_locked || s.raced || s.dnf || s.past || s.travel_locked || s.invite_locked);
    // the LAUNCH gate now honors the same availability model the UI renders (previously clickable
    // ignored past/gate_locked -> the game let you launch "past"/locked rounds that appeared greyed out, and
    // let you race rounds earlier than ones already DNF'd). Now clickable == available: no racing in the past.
    // forceNotClickable unconditionally turns off BOTH for the year+1 queue (no combination of
    // the projected slot's status flags can sneak through as "launchable today").
    const bool isAvailable = forceNotClickable ? false : isAvailableRaw;
    const bool clickable   = forceNotClickable ? false : (isAvailableRaw && !s.is_memory);
    const bool isLocked    = (s.locked || s.gate_locked) && !s.raced && !s.dnf;
    const char* fmt = s.fmt == 0 ? "sprint" : s.fmt == 1 ? "feature" : "endurance";
    const std::vector<std::string> cita = career::MeetingSeriesSet(meetingCtx, s);   // a single count for the meeting
    const bool multiclass = cita.size() > 1;

    w.BeginObject();
    w.KS("id", s.series_id + "#" + std::to_string(s.round));   // stable key = seriesId#round
    w.KS("seriesId", s.series_id);
    w.KS("seriesName", seriesName);
    w.KS("className", className);
    w.KS("discipline", discipline);
    w.KS("region", region);
    w.KS("trackName", s.track_name);
    w.KI("trackHash", s.track_hash);
    w.KI("hour", s.hour);                          // event time (for the app's conflict warning)
    w.KS("trackSlug", career::SlugForTrackEnsured(s.track_hash));   // circuit map (PNG in data/)
    // thumbnails field for HOVER — up to 4 cars representative of the class (PNG slug +
    // name for the tooltip). The app resolves the thumbnail with useCarLiveries(dataDir, slug) on hover.
    w.Key("field"); w.BeginArray();
    { int nf = 0;
      for (const auto& ve : career::VehiclesForClass(className)) {
          if (nf >= 4) break;
          std::string vslug = career::SlugForVehicleEnsured(ve.vehHash);
          if (vslug.empty()) continue;
          w.BeginObject(); w.KS("name", ve.name); w.KS("slug", vslug); w.EndObject();
          nf++;
      } }
    w.EndArray();
    w.KI("doy", s.doy);
    w.KS("date", s.date);                          // "Feb 14" (month day), raw from the calendar
    w.KI("year", yearForRound);                    // current year (v.year) or cc.year+1 (queue)
    w.KS("weekday", career::WeekdayLabel(yearForRound, s.doy));   // real day of the week, additive
    // bug reported Aug 5: days the round OCCUPIES, resolved by the ENGINE (SlotOccupiedDays = the
    // series' footprint). The app's clash warning only compared the SAME day, while the engine DNFs the entire
    // window, so an F1 weekend (Fri-Sun) would take down another series' Saturday round WITHOUT
    // warning. They travel pre-resolved so the client-side mirror can't drift from the footprint again.
    { int wlo = 0, whi = 0; career::SlotOccupiedDays(s, wlo, whi);
      w.KI("winLoDoy", wlo); w.KI("winHiDoy", whi); }
    w.KI("round", s.round);
    w.KS("format", fmt);                           // i18n token (the app runs t(format))
    // FIX Aug 1: what travels is NOT "is sprint" but "this round scores at half points", which is the only thing that
    // actually changes anything for the player. They stopped being the same thing once all-sprint series started scoring full points (see
    // RoundPointsScale): showing "SPRINT" on Oakfield Motors would promise a point cut that no longer exists. So the badge
    // only shows up where it actually affects the championship — Underhill Eld246, Ashmere Motors KEYSTONE_CUP_6259 and Hypercars.
    w.KB("halfPoints", career::RoundPointsScaleFor(s.series_id, s.round) < 1.0);
    w.KI("laps", s.laps);
    w.KI("durationMin", s.durationMin);
    { const int effectiveGrid = GridForSlot(cita, s.series_id, s.round, (int)s.track_hash);
      const int requestedGrid = career::RoundGridSizeOverride(cita, s.series_id, s.round);
      w.KI("grid", effectiveGrid);                 // compat: what will actually go out on track
      w.KI("gridEffective", effectiveGrid);        // explicit, to compare against the requested exception
      if (requestedGrid > 0) w.KI("gridRequested", requestedGrid);
      else { w.Key("gridRequested"); w.Null(); } }
    w.KI("prize", career::RoundWinnerPrizeDisplay(s.series_id));   // never 0 (heritage series -> a floor per level)
    w.KI("level", s.level);
    w.KI("audience", career::AudienceForRaceEx(s.series_id, s.round, s.level, s.fmt));   // per-round CURATED audience (falls back to the BASE model)
    w.KS("status", status);
    w.KB("clickable", clickable);
    w.KB("enrolled", s.enrolled);
    w.KB("conflict", s.conflict);
    w.KB("multiclass", multiclass);
    w.KB("isMemory", s.is_memory);
    w.KB("gateLocked", s.gate_locked);
    // tester suggestion (hub 0.6.5): the superlicense chip jumps to the qualifier -- the app needs to know which one
    if (s.gate_locked && !s.gate_qualifier.empty()) w.KS("gateQual", s.gate_qualifier);
    // hub closed grid without a contract -- the app paints the "By invitation" lock instead of a dead click
    w.KB("inviteLocked", s.invite_locked);
    // tester suggestion (hub 29/08): a SUPERLICENSE TEST's card stands out at a glance -- the same
    // criterion as the launch's heroic gate (standalone that unlocks something = qualifier E2.5)
    w.KB("isQualifier", sd && sd->kind == "standalone" && !sd->unlocks.empty());
    // track not installed -> lock with a DLC reason (dlcPack = slug of the missing pack; "" unknown)
    w.KB("dlcLocked", s.dlc_locked);
    if (s.dlc_locked) w.KS("dlcPack", s.dlc_pack);
    // round with its track SWAPPED due to missing DLC (a discreet badge + hover in the app); dlcProduct =
    // the sellable product that's missing (also used for the unplayable-series lock).
    w.KB("dlcSubbed", s.dlc_subbed);
    if (s.dlc_subbed) w.KS("origTrackName", s.orig_track_name);
    if (!s.dlc_product.empty()) w.KS("dlcProduct", s.dlc_product);
    // SPECIAL badge in the app + link to the teaser (additive /v1)
    w.KB("special", s.special);
    if (s.special) { w.KS("eventId", s.event_id); w.KS("tierMin", s.tier_min); }
    w.KB("historic", historic);
    if (historic) w.KI("eraYear", eraYear);   // 04/10: the app paints the card with the skin of its decade (additive /v1)
    w.KB("isAvailable", isAvailable);
    w.KB("isLocked", isLocked);
    w.EndObject();
}

// fwd-decl: JumpTargetDateLabel lives next to the clock handlers (its first historical use), further down in
// this same translation unit. Market markers need the SAME "Feb 15" format as the slots, so it's
// declared here instead of duplicating the formatting.
static std::string JumpTargetDateLabel(int year, int doy);

HandlerResult QueryCalendar(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_calendarProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    CalendarView v = g_calendarProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    // commitments: pending live enrollments + clashes (each clash spans 2 slots -> /2).
    int enrolledCount = 0, clashSlots = 0;
    for (const auto& s : v.slots) {
        if (!s.valid) continue;
        if (s.enrolled && !s.raced && !s.dnf) enrolledCount++;
        if (s.conflict && s.enrolled && !s.raced && !s.dnf) clashSlots++;
    }

    jsonw::Writer w;
    w.BeginObject();
    w.KB("calibrated", v.calibrated);
    // calibration: present only when NOT yet calibrated (onboarding gate -> the calibration race)
    w.Key("calibration");
    if (!v.calibrated) {
        w.BeginObject();
        w.KS("carName", v.calibCarName);
        w.KS("trackName", v.calibTrackName);
        w.KI("trackHash", v.calibTrackHash);
        w.KS("trackSlug", career::SlugForTrackEnsured(v.calibTrackHash));   // circuit map (PNG in data/)
        w.EndObject();
    } else {
        w.Null();
    }
    w.KI("phase", v.frontierPhase);
    w.Key("today"); w.BeginObject();
    w.KI("doy", v.cursorDoy);
    w.KS("date", v.cursorDoy > 0 ? career::DoyToDate(v.cursorDoy) : std::string());   // "14 Mar" (day month)
    w.KI("year", v.year);
    w.KS("weekday", career::WeekdayLabel(v.year, v.cursorDoy));   // real day of the week, additive
    w.EndObject();
    w.KI("weeksToNextEvent", v.weeksToNextEvent);
    // THE MARKET DATES, in their own lane. Deliberately NOT part of "slots": a slot is a
    // RACE (series, round, track), and cramming a transfer-window week in there would force inventing it a track.
    // A separate lane means the screen decides whether to draw it as a background band, and an old consumer just ignores it.
    // Without this, preseason is eight blank weeks with one decisive date buried inside and nothing to
    // call it out, which reads as "nothing happening here".
    w.Key("marketMarks"); w.BeginArray();
    for (const auto& m : market::MarketMarksForYear(v.year, career::kStartYear)) {
        w.BeginObject();
        const bool esDecision = (m.kind == market::MARK_DECISION_WEEK);
        w.KS("kind", esDecision ? "decision" : "window");
        w.KI("doy", m.doy);
        w.KI("days", m.days);
        // "Feb 15" (month day), NOT the "15 Feb" from DoyToDate: it's the format the calendar slots already use,
        // straight from the data file, and the one the screen's date column knows how to parse. Emitting the other would have rendered
        // the number as the month and the month as the day, and you'd only notice by eyeballing it.
        w.KS("date", JumpTargetDateLabel(v.year, m.doy));
        // a TR key, not text: the app resolves it through its i18n layer, just like the screen used to resolve it with TR().
        // This lives here, not in the UI, because the label belongs to the DATA -- if a third marker shows up tomorrow, whoever
        // adds it to the engine can't forget to name it.
        w.KS("labelKey", esDecision ? "cal_mark_decision" : "cal_mark_window");
        // a label PER DAY: the decision week is seven days that would otherwise render blank on the calendar, and a
        // blank day reads as "free". With this, each of those rows can carry its own text instead of
        // staying silent, which is what lets the date show up without having to go hunting for it.
        w.KS("dayLabelKey", esDecision ? "cal_day_decision" : "cal_day_window");
        w.EndObject();
    }
    w.EndArray();
    w.Key("commitments"); w.BeginObject();
    w.KI("enrolled", enrolledCount);
    w.KI("clashes", clashSlots / 2);
    w.EndObject();

    w.Key("slots"); w.BeginArray();
    for (const auto& s : v.slots) {
        if (!s.valid) continue;   // PendingSlot for a finished series -> discarded
        // current year: the SAME write as before this change (forceNotClickable=false), + a "year"=v.year field.
        WriteCalendarSlot(w, s, v.slots, v.year, /*forceNotClickable=*/false);
    }
    // informational queue for the NEXT year, appended to the SAME "slots" array after the current year's
    // (an old consumer that ignores "year" and the queue simply sees a few more entries at the end — an
    // additive change). CHRONOLOGICAL order by day-of-year + a kNextYearQueueCap cap; ALWAYS not-clickable/not-available,
    // no matter what enrolled/raced/etc. say on the projected slot (WriteCalendarSlot with forceNotClickable=true).
    // The cap/ordering lives HERE (not in the provider) so it's covered by tests with a synthetic provider that
    // hands over v.nextYearSlots raw (career_overlay.cpp, which does call BuildMasterCalendar with year+1,
    // isn't part of the test build).
    {
        std::vector<const career::CalSlot*> queue;
        for (const auto& s : v.nextYearSlots) if (s.valid) queue.push_back(&s);
        std::sort(queue.begin(), queue.end(),
                  [](const career::CalSlot* a, const career::CalSlot* b) { return a->doy < b->doy; });
        if ((int)queue.size() > kNextYearQueueCap) queue.resize(kNextYearQueueCap);
        for (const career::CalSlot* sp : queue)
            WriteCalendarSlot(w, *sp, v.nextYearSlots, v.year + 1, /*forceNotClickable=*/true);
    }
    w.EndArray();

    w.Key("seasonResults"); w.BeginArray();
    for (const auto& c : v.seasonResults) {
        // champion: hidden if it's you, or if there isn't one (avoids "champion = the player" in a degenerate 1-entry season)
        const std::string champion = (c.champion.empty() || c.champion == v.playerName) ? std::string() : c.champion;
        w.BeginObject();
        w.KS("seriesId", c.seriesId);
        w.KS("seriesName", c.display);
        w.KI("year", c.year);
        w.KI("finalPos", c.finalPos);
        w.KS("champion", champion);
        w.KI("field", c.field);
        w.KI("rounds", c.rounds);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// ===== query.results ========================================================================
static void WriteCardDef(jsonw::Writer& w, const career::CardDef& d);   // defined next to query.cards (below)
static ResultsViewProvider g_resultsProvider;
void SetResultsViewProvider(ResultsViewProvider p) { g_resultsProvider = std::move(p); }

HandlerResult QueryResults(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_resultsProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    ResultsView v = g_resultsProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "no race result yet"; return r; }

    jsonw::Writer w;
    w.BeginObject();
    w.KI("pos", v.pos);                                // where you finished or got out
    w.KB("finished", v.finished);                      // P2 (F15): false => DNF (the app renders «DNF (P#)»)
    w.KI("field", v.field);
    w.KB("multiclass", v.multiclass);
    w.KI("overallPos", v.overallPos);
    w.KI("overallField", v.overallField);
    w.KS("className", v.className);
    w.KS("classColor", CssVar(v.classColor));          // arbitrary class color -> #hex
    w.KS("carName", v.carName);
    w.KS("trackName", v.trackName);

    w.Key("timing"); w.BeginObject();
    w.KN("totalTimeS", v.totalTimeS);                  // seconds; <=0 => "—" (the app decides)
    w.KN("bestLapS", v.bestLapS);
    w.KN("gapS", v.gapS);                              // <=0.001 => leader (PACE gap, legacy)
    w.KN("gapLeaderS", v.gapLeaderS);                  // seconds behind the class leader at the FINISH; <0 = n/a / you're the leader
    w.KI("gapLeaderLaps", v.gapLeaderLaps);            // laps behind the leader (>0 => "+N L")
    w.EndObject();

    // money: EVERY non-zero SettleReport term as its own line (the screen hides some, but the
    // contract promises SUM=net). credits_delta = prize+clean+salary+deal-entry-costs+coverage+
    // sponsor_payout+result_bonus-deductible-travel -> emitting all of them guarantees the lines SUM to the net.
    w.Key("money"); w.BeginObject();
    w.Key("lines"); w.BeginArray();
    auto line = [&](const char* key, long amount, const char* tone, bool force) {
        if (amount == 0 && !force) return;
        w.BeginObject(); w.KS("key", key); w.KI("amount", amount); w.KS("tone", tone); w.EndObject();
    };
    line("results.prize",       v.prize,          "gold",   true);   // always present (1st line, even if 0)
    line("results.cleanBonus",  v.cleanBonus,     "gold",   false);
    line("results.resultBonus", v.resultBonus,    "gold",   false);
    line("results.salary",      v.salary,         "cyan",   false);  // salary + deal_salary (overlay)
    // (the "results.sponsor" line — the v1 system's flat retainer by REP tier — retired along with its
    //  engine-side term; the LIVE sponsorship is "results.spPayout" + the cardLines below.)
    line("results.spPayout",    v.sponsorPayout,  "cyan",   false);  // themed payout for a target hit
    line("results.coverage",    v.coverage,       "cyan",   false);
    line("results.entry",      -v.entry,          "danger", false);
    line("results.costs",      -v.costs,          "danger", false);
    line("results.deductible", -v.deductible,     "danger", false);
    line("results.travel",     -v.travel,         "danger", false);
    // sponsor cards: EXTRA lines contributed by the cards (absent from the base lines above,
    // which are the SettleReport pure, with no cards). Emits `key` when the line carries an i18n
    // key (discounts, fixed bilingual text), or `label` when it carries RAW text (income/penalty: brand name +
    // a dynamic trigger) -- the app renders "label ?? (key ? t(key) : '')". Never both at once.
    static const char* kCardToneStr[3] = { "gold", "cyan", "danger" };
    for (const auto& cl : v.cardLines) {
        w.BeginObject();
        if (!cl.key.empty())   w.KS("key", cl.key);
        if (!cl.label.empty()) w.KS("label", cl.label);
        w.KI("amount", cl.amount);
        w.KS("tone", kCardToneStr[cl.tone >= 0 && cl.tone <= 2 ? cl.tone : 0]);
        w.EndObject();
    }
    w.EndArray();
    w.KI("net", v.net);                                // == sum of the lines
    w.EndObject();

    w.Key("progression"); w.BeginObject();
    w.Key("elo"); w.BeginObject();
    w.KI("before", v.eloBefore); w.KI("after", v.eloAfter); w.KI("delta", v.eloAfter - v.eloBefore);
    w.KS("band", v.eloBand); w.KS("color", CssVar(v.eloColor));
    w.EndObject();
    w.Key("rep"); w.BeginObject();
    w.KI("before", v.repBefore); w.KI("after", v.repAfter); w.KI("delta", v.repDelta);
    w.KS("color", CssVar(v.repColor));
    w.EndObject();
    w.Key("exp"); w.BeginObject();                      // fixed color (cyan) is set by the app
    w.KI("before", v.xpBefore); w.KI("after", v.xpAfter); w.KI("delta", v.xpGained);
    w.EndObject();
    w.Key("balance"); w.BeginObject();                  // fixed color (gold) is set by the app; credits
    w.KI("before", v.crBefore); w.KI("after", v.crAfter); w.KI("delta", v.crAfter - v.crBefore);
    w.EndObject();
    w.EndObject();

    w.Key("championship");
    if (v.hasChampionship) {
        w.BeginObject();
        w.KS("seriesName", v.champSeriesName);
        w.KI("pointsGained", v.champPointsGained);
        w.KI("playerRank", v.champPlayerRank);
        w.Key("rows"); w.BeginArray();
        for (const auto& row : v.champRows) {
            w.BeginObject();
            { std::string f = row.isPlayer ? PlayerFichaDisplay() : std::string();
              w.KS("name", f.empty() ? row.name : f); }   // hub your row, your CURRENT name
            w.KI("points", row.points);
            w.KB("isPlayer", row.isPlayer);
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
    } else {
        w.Null();                                       // one-off race (no championship)
    }
    // chips "✓/·/✕ Brand" for cards triggered/active/expired/lost this race (payload root).
    static const char* kCardChipKindStr[4] = { "fired", "idle", "expired", "lost" };
    w.Key("cardChips"); w.BeginArray();
    for (const auto& chip : v.cardChips) {
        w.BeginObject();
        w.KS("text", chip.text);
        w.KS("kind", kCardChipKindStr[chip.kind >= 0 && chip.kind <= 3 ? chip.kind : 0]);
        w.EndObject();
    }
    w.EndArray();
    // Showroom (feedback from Jul 12): the actual CARD (full def) per event, so the app can render it with
    // <SponsorCardView/>. Entries whose id doesn't resolve in the catalog are skipped (a save newer than the DLL).
    w.Key("cardShowcase"); w.BeginArray();
    for (const auto& show : v.cardShowcase) {
        const career::CardDef* def = career::CardById(show.id);
        if (!def) continue;
        w.BeginObject();
        w.Key("def"); WriteCardDef(w, *def);
        w.KI("level", show.level);
        w.KS("kind", kCardChipKindStr[show.kind >= 0 && show.kind <= 3 ? show.kind : 0]);
        w.EndObject();
    }
    w.EndArray();
    // the two post-race surfaces. `ask` is the FEEL question (whenever there's a settled
    // race; consent only governs whether it's sent, not whether it's asked) and `offer` is the OBJECTIVE difficulty
    // proposal, which only appears when the advantage sample was clipped as an outlier.
    w.Key("feedback"); w.BeginObject();
    w.KB("ask", v.feedback.ask);
    w.KI("feel", v.feedback.feel);
    w.KB("telemetryOptIn", v.feedback.telemetryOptIn);
    w.KB("offer", v.feedback.offer);
    w.KN("gapPct", v.feedback.gapPct);
    w.KI("offerDifficulty", v.feedback.offerDifficulty);
    w.KI("curDifficulty", v.feedback.curDifficulty);
    w.KB("held", v.feedback.held);   // if you don't answer, that race doesn't count
    w.EndObject();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// ===== query.cards + cmd.cards.* (sponsor cards) =================================
static CardsViewProvider g_cardsProvider;
void SetCardsViewProvider(CardsViewProvider p) { g_cardsProvider = std::move(p); }

static const char* CardHookStr(int hook) {
    switch (hook) {
    case career::CARD_HOOK_PRIZE_MULT:   return "income.prize_mult";
    case career::CARD_HOOK_INCOME_BONUS: return "income.bonus";
    case career::CARD_HOOK_COST_TYRES:   return "cost.tyres";
    case career::CARD_HOOK_COST_FUEL:    return "cost.fuel";
    case career::CARD_HOOK_COST_TRAVEL:  return "cost.travel";
    case career::CARD_HOOK_COST_ENTRY:   return "cost.entry";
    case career::CARD_HOOK_COST_RENT:    return "cost.rent";
    case career::CARD_HOOK_XP_MULT:      return "xp.mult";
    case career::CARD_HOOK_REP_MULT:     return "rep.mult";        // Cards v2 plumbing (catalog v2)
    default:                             return "special";
    }
}
static const char* kCardRarityKey[5] = { "local", "regional", "national", "international", "elite" };

// Full DEF for a card (pure catalog data, career::CardDef) — the app does NOT hardcode the catalog: every card
// (signed or offered) travels with its complete sheet. See career_cards.h for what each field means.
static void WriteCardDef(jsonw::Writer& w, const career::CardDef& d) {
    w.BeginObject();
    w.KS("id", d.id); w.KS("name", d.name);
    w.Key("tag"); w.BeginObject(); w.KS("es", d.tag_es); w.KS("en", d.tag_en); w.EndObject();
    int rarity = d.rarity < 0 ? 0 : (d.rarity > 4 ? 4 : d.rarity);
    w.KI("rarity", d.rarity); w.KS("rarityKey", kCardRarityKey[rarity]);
    w.Key("life"); w.BeginObject();
    w.KS("k", d.life_kind == career::CARD_LIFE_PERM ? "perm" : d.life_kind == career::CARD_LIFE_RACES ? "races" : "uses");
    w.KI("n", d.life_n); w.EndObject();
    w.KS("trigger", d.trigger);
    w.Key("fx"); w.BeginObject(); w.KS("hook", CardHookStr(d.hook)); w.KN("v", d.v); w.EndObject();
    // Cards v2 plumbing (catalog v2): 0-2 EXTRA effects, same vocabulary as "fx" -- empty array
    // when the card has none (all 27 from v1, today).
    w.Key("fxExtra"); w.BeginArray();
    for (const auto& e : d.extras) {
        w.BeginObject(); w.KS("hook", CardHookStr(e.hook)); w.KN("v", e.v); w.EndObject();
    }
    w.EndArray();
    if (d.clause.kind) {
        w.Key("clause"); w.BeginObject();
        w.KS("kind", d.clause.kind == 1 ? "dnf_fine" : "notop10_lose");
        w.KI("fine", (int)d.clause.fine); w.KI("races", d.clause.races); w.EndObject();
    } else { w.Key("clause"); w.Null(); }
    if (!d.special_id.empty()) {
        w.Key("special"); w.BeginObject();
        w.KS("id", d.special_id); w.KS("es", d.special_es); w.KS("en", d.special_en); w.EndObject();
    } else { w.Key("special"); w.Null(); }
    if (d.upgrade.levels > 0) {
        w.Key("upgrade"); w.BeginObject();
        w.KI("after", d.upgrade.after); w.KI("cost", (int)d.upgrade.cost); w.KI("levels", d.upgrade.levels);
        // 0.7.0 (): magnitude of each level, so the app explains the improvement on mouse hover.
        w.KN("v2", d.upgrade.v2); w.KN("v3", d.upgrade.v3);
        w.EndObject();
    } else { w.Key("upgrade"); w.Null(); }
    w.KI("cost", (int)career::CardCost(d));
    w.EndObject();
}

// query.cards: wallet (signed, slotted or benched) + pending post-race shop + gating. Every card
// signed one travels with its embedded DEF (WriteCardDef) + its live state (level/slot/life/upgrade/sale-value/current fx);
// the shop embeds the DEF for each offer. loaded=false -> not_ready.
HandlerResult QueryCards(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_cardsProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    CardsView v = g_cardsProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    jsonw::Writer w;
    w.BeginObject();
    w.Key("signed"); w.BeginArray();
    for (const auto& sc : v.cards) {
        const career::CardDef* d = career::CardById(sc.card_id);
        if (!d) continue;   // catalog out of sync with the save -> skip it (defensive)
        w.BeginObject();
        w.Key("def"); WriteCardDef(w, *d);
        w.KI("level", sc.level);
        w.KI("slot", sc.slot);
        w.KI("racesActive", sc.races_active);
        w.KI("racesLeft", sc.races_left);
        w.KI("usesLeft", sc.uses_left);
        w.KI("clauseMiss", sc.clause_miss);
        w.KI("invested", sc.invested);
        w.KI("sellValue", career::CardSellValue(sc));
        career::CardUpgradeInfo up;
        if (career::CardUpgradeNext(sc, up)) {
            w.Key("upgradeNext"); w.BeginObject();
            w.KI("toLevel", up.to_level); w.KI("cost", (int)up.cost);
            w.KI("need", up.need); w.KI("have", up.have); w.KB("ready", up.ready);
            w.EndObject();
        } else { w.Key("upgradeNext"); w.Null(); }
        w.KN("fxV", career::CardFxV(*d, sc.level));
        w.EndObject();
    }
    w.EndArray();
    w.Key("shop");
    if (v.shop.open) {
        w.BeginObject();
        w.KS("raceId", v.shop.race_id);
        w.KI("rerolls", v.shop.rerolls);
        w.KI("bought", v.shop.bought);
        w.KI("maxBuys", career::CardShopMaxBuys(v.cards));
        w.KI("offerCount", career::CardShopOfferCount(v.cards));
        w.KI("rerollCost", (int)career::CardShopRerollCost(v.shop.rerolls, v.cards));
        w.Key("offers"); w.BeginArray();
        for (const auto& id : v.shop.offers) {
            const career::CardDef* d = career::CardById(id);
            if (d) WriteCardDef(w, *d);
        }
        w.EndArray();
        w.Key("boughtIds"); w.BeginArray();
        for (const auto& id : v.shop.bought_ids) w.S(id);
        w.EndArray();
        w.EndObject();
    } else { w.Null(); }
    w.KI("slotsUnlocked", career::CardSlotsUnlocked(v.phase));
    w.KI("phase", v.phase);
    w.KI("money", v.credits);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// RunIdCmd lives further down (next to cmd.sponsors.sign/cmd.market.*, its first historical use); fwd-decl so
// it can be reused here (same id->{ok,<key>} pattern as sponsors/market).
static HandlerResult RunIdCmd(const IdCmdExec& exec, const jsonm::Value* params, const char* key);

static CardIdExec   g_cardBuyExec, g_cardBenchExec, g_cardUpgradeExec, g_cardSellExec;
static CardVoidExec g_cardRerollExec, g_cardShopCloseExec, g_cardSeenExec;
static CardSlotExec g_cardSlotExec;
static CardArrangeExec g_cardArrangeExec;
void SetCardsCmds(CardIdExec buy, CardVoidExec reroll, CardVoidExec shopClose,
                  CardSlotExec slotc, CardIdExec bench, CardIdExec upgrade, CardIdExec sell) {
    g_cardBuyExec = std::move(buy); g_cardRerollExec = std::move(reroll); g_cardShopCloseExec = std::move(shopClose);
    g_cardSlotExec = std::move(slotc); g_cardBenchExec = std::move(bench);
    g_cardUpgradeExec = std::move(upgrade); g_cardSellExec = std::move(sell);
}
void SetCardsCmds2(CardVoidExec seen, CardArrangeExec arrange) {
    g_cardSeenExec = std::move(seen); g_cardArrangeExec = std::move(arrange);
}

// shared helper: a parameterless command -> {ok}. not_ready with no executor wired up.
static HandlerResult RunVoidCmd(const CardVoidExec& exec) {
    HandlerResult r;
    if (!exec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    bool ok = exec();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.EndObject();
    r.payload = w.Str();
    return r;
}

HandlerResult CmdCardsBuy(const jsonm::Value* params)        { return RunIdCmd(g_cardBuyExec,     params, "id"); }
HandlerResult CmdCardsReroll(const jsonm::Value* /*params*/) { return RunVoidCmd(g_cardRerollExec); }
HandlerResult CmdCardsShopClose(const jsonm::Value* /*params*/) { return RunVoidCmd(g_cardShopCloseExec); }
HandlerResult CmdCardsBench(const jsonm::Value* params)       { return RunIdCmd(g_cardBenchExec,   params, "id"); }
HandlerResult CmdCardsUpgrade(const jsonm::Value* params)     { return RunIdCmd(g_cardUpgradeExec, params, "id"); }
HandlerResult CmdCardsSell(const jsonm::Value* params)        { return RunIdCmd(g_cardSellExec,    params, "id"); }
HandlerResult CmdCardsSeen(const jsonm::Value* /*params*/)    { return RunVoidCmd(g_cardSeenExec); }

// cmd.cards.arrange { moves: [{id, slot}] } -> atomic multi-slot reassignment.
// slot -1 = bench. The engine validates EVERYTHING (ids, gating, no duplicates) and applies it all-or-nothing.
HandlerResult CmdCardsArrange(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_cardArrangeExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    const jsonm::Value* moves = params ? params->member("moves") : nullptr;
    if (!moves || !moves->isArray() || moves->arr.empty()) {
        r.ok = false; r.code = "bad_params"; r.message = "missing moves"; return r;
    }
    std::vector<std::pair<std::string, int>> mv;
    for (const auto& mp : moves->arr) {
        if (!mp) continue;
        std::string id = mp->memberString("id");
        if (id.empty() || !mp->has("slot")) { r.ok = false; r.code = "bad_params"; r.message = "move without id/slot"; return r; }
        mv.emplace_back(id, mp->memberInt("slot", -1));
    }
    bool ok = g_cardArrangeExec(mv);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.cards.slot { id, slot } -> moves a card into an active slot (or fails if it's occupied/gated out).
// The only cards cmd with 2 parameters -> its own handler (same style as CmdSettingsSetAggr).
HandlerResult CmdCardsSlot(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_cardSlotExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string id = params ? params->memberString("id") : std::string();
    if (id.empty() || !params || !params->member("slot")) { r.ok = false; r.code = "bad_params"; r.message = "missing id/slot"; return r; }
    int slot = params->memberInt("slot", -1);
    bool ok = g_cardSlotExec(id, slot);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KS("id", id); w.KI("slot", slot); w.EndObject();
    r.payload = w.Str();
    return r;
}

static TimesViewProvider g_timesProvider;
void SetTimesViewProvider(TimesViewProvider p) { g_timesProvider = std::move(p); }

// query.times: serializes the timing log (mirrors the TIMES tab). RAW data (times in
// seconds, day-of-year->DoyToDate, colors ImU32->CssVar); leader/gap/fastest-lap are computed by the app the same way
// DrawTimesDetail does (leaderBest = min best_lap; leaderTotal/laps = the pos1 winner, or a most-laps fallback).
HandlerResult QueryTimes(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_timesProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    TimesView v = g_timesProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    jsonw::Writer w;
    w.BeginObject();
    w.Key("races"); w.BeginArray();
    for (const auto& e : v.races) {
        w.BeginObject();
        w.KS("track", e.track);
        w.KS("className", e.className);
        w.KS("classColor", CssVar(e.classColor));
        w.KS("date", e.doy > 0 ? career::DoyToDate(e.doy) : std::string());   // "14 Mar"
        w.KI("year", e.year);
        w.KI("pos", e.pos);                          // where you finished or got out
        w.KB("finished", e.finished);                // P2 (F15): false => DNF (the app renders it)
        w.KI("field", e.field);
        w.KN("bestLapS", e.bestLapS);                // seconds; <=0 => "—"
        w.KN("totalTimeS", e.totalTimeS);
        w.KB("hasGrid", !e.grid.empty());            // no grid => a race from before this feature existed (nothing to drill into)
        w.Key("grid"); w.BeginArray();
        for (const auto& g : e.grid) {
            w.BeginObject();
            w.KI("pos", g.pos);
            { std::string f = g.isPlayer ? PlayerFichaDisplay() : std::string();
              w.KS("name", f.empty() ? g.name : f); }   // hub your row, your CURRENT name
            w.KS("className", g.className);           // raw car_class (multiclass)
            w.KS("classColor", CssVar(g.classColor));
            w.KI("laps", g.laps);
            w.KN("bestLapS", g.bestLapS);
            w.KN("totalTimeS", g.totalTimeS);
            w.KB("dnf", g.dnf);
            w.KB("isPlayer", g.isPlayer);
            w.KB("finished", g.finished);   // false = still on track when captured -> deficit-1 = laps down
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

static TreeViewProvider g_treeProvider;
void SetTreeViewProvider(TreeViewProvider p) { g_treeProvider = std::move(p); }

// tier index 0..5 -> i18n key for the REP band NAME (the app resolves it via t(); the EN/ES literals
// live in the engine for the overlay via RatingTierLabel). Same pattern as BandNameKey.
static const char* TierNameKey(int idx) {
    switch (idx) {
        case 0: return "tree.tier.unknown";
        case 1: return "tree.tier.prospect";
        case 2: return "tree.tier.known";
        case 3: return "tree.tier.established";
        case 4: return "tree.tier.star";
        case 5: return "tree.tier.legend";
    }
    return "";
}

// query.tree: serializes the ladder (discipline×level matrix) + rating band. Serializes only; discipline as a token
// (the app colors it with DISC_META), tier color ImU32->CssVar, tier band->i18n key. loaded=false->not_ready.
HandlerResult QueryTree(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_treeProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    TreeView v = g_treeProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    jsonw::Writer w;
    w.BeginObject();
    w.KI("frontierPhase", v.frontierPhase);
    w.KI("rep", v.rep);                                  // raw (the app doesn't render it; only the band)
    w.Key("ratingTier"); w.BeginObject();
    w.KI("index", v.ratingTier);
    w.KS("key", TierNameKey(v.ratingTier));             // i18n key -> t()
    w.KS("color", CssVar(v.ratingTierColor));
    w.EndObject();
    w.Key("nodes"); w.BeginArray();
    for (const auto& n : v.nodes) {
        w.BeginObject();
        w.KS("id", n.id);
        w.KS("display", n.display);                     // raw (class name from the data file, 1 language)
        w.KS("disc", n.disc);                           // enum token -> the app colors it with DISC_META
        w.KI("level", n.level);
        w.KI("carCount", n.carCount);                   // the app hides it if 0
        w.KB("isDlc", n.isDlc);
        w.KI("vehid", n.vehid);                         // deferred nav key; inert for now
        w.KB("locked", n.locked);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

static SettingsViewProvider g_settingsProvider;
void SetSettingsViewProvider(SettingsViewProvider p) { g_settingsProvider = std::move(p); }

// career::DifficultyMode -> a stable contract token (DM_EASY..DM_CUSTOM = 0..5).
static const char* ModeToken(int m) {
    switch (m) { case 0: return "easy"; case 1: return "medium"; case 2: return "hard"; case 3: return "extreme";
                 case 4: return "adaptive"; case 5: return "custom"; }
    return "medium";
}

// query.settings: Settings · Difficulty (mirrors screen_settings.cpp). Serializes RAW: mode->token,
// meter/trim/bounds raw, color ImU32->CssVar, i18n keys as strings. loaded=false->not_ready.
HandlerResult QuerySettings(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_settingsProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    SettingsView v = g_settingsProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    jsonw::Writer w;
    w.BeginObject();
    w.Key("difficulty"); w.BeginObject();
    w.KS("mode", ModeToken(v.selectedMode));    // active card
    w.KI("trim", v.trim);                        // -5..+5 (the app renders "%+d")
    w.KI("trimMin", v.trimMin);
    w.KI("trimMax", v.trimMax);
    w.KB("lockAll", v.lockAll);   // #DDA-lock: the master switch
    // #DDA-class: additive -- an old engine build doesn't send this key, and the app reads it as [] and skips the
    // hover. Zero new controls: the trim is still the same one, and it's still global.
    w.Key("classes"); w.BeginArray();
    for (const auto& c : v.classes) {
        w.BeginObject();
        w.KS("classId", c.classId);
        w.KS("className", c.className);          // redesign: a readable name; never empty (falls back to classId)
        w.KS("classColor", CssVar(c.classColor));// discipline color for the row
        w.KI("difficulty", c.difficulty);
        w.KI("delta", c.delta);
        w.KI("races", c.races);
        w.KB("seeded", c.seeded);
        w.KB("locked", c.locked);   // #DDA-lock: the OWN flag, not the effective one (see the header)
        w.KI("saturation", c.saturation);       // 09/09 (, 4): +1 ceiling / -1 floor / 0
        w.KI("wetDifficulty", c.wetDifficulty); //   the wet number with trim (0 = doesn't have it yet)
        w.KI("racesWet", c.racesWet);
        w.EndObject();
    }
    w.EndArray();
    w.KI("aiDifficulty", v.aiDifficulty);        // base number learned by the DDA (AMS2 slider 70..120)
    w.KI("aiEffective", v.aiEffective);          // the one actually SET in the race = clamp(base + trim)
    w.KI("aiMin", v.aiMin);
    w.KI("aiMax", v.aiMax);
    w.KI("customAi", v.customAi);                // DM_CUSTOM: the FIXED number chosen by hand (same aiMin..aiMax domain)
    w.Key("cards"); w.BeginArray();
    for (const auto& c : v.cards) {
        w.BeginObject();
        w.KS("mode", ModeToken(c.mode));
        w.KS("nameKey", c.nameKey);
        w.KS("descKey", c.descKey);
        w.KI("meter", c.meter);
        w.KS("color", CssVar(c.color));
        w.KB("isAuto", c.isAuto);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();   // difficulty
    // AI aggression (0 = auto by phase; 40/60/80/100 = user override).
    w.Key("aggression"); w.BeginObject();
    w.KI("value", v.aggr);
    w.EndObject();
    // toggles for Gameplay's automatic multiplier scaling.
    w.Key("gameplayScaling"); w.BeginObject();
    w.KB("tyre", v.scaleTyre);
    w.KB("fuel", v.scaleFuel);
    w.EndObject();
    w.KB("fcyOn", v.fcyOn);   // legacy (old app)
    w.KI("fcyMode", v.fcyMode);   // FCY 0=No 1=Yes 2=Inherited
    // weather mode + weights. `catalog` is the set of AMS2 catalog codes the UI
    // can offer (the NAMES are supplied by the app via its wxname_* strings, not duplicated here), and `defaults` is the
    // factory weighting, so the panel can show where it starts before the player touches anything.
    w.Key("weather"); w.BeginObject();
    w.KI("mode", v.wxMode);
    w.Key("weights"); w.BeginObject();
    for (const auto& kv : v.wxWeights) w.KI(std::to_string(kv.first).c_str(), kv.second);
    w.EndObject();
    w.Key("defaults"); w.BeginObject();
    for (const auto& kv : v.wxDefaults) w.KI(std::to_string(kv.first).c_str(), kv.second);
    w.EndObject();
    w.EndObject();
    // AUTO/Inherited per field. Same tokens as cmd.settings.setInherit; bit set to 1 = INHERITED
    // (the mod doesn't write those slots for calendar races). The bits follow the kInh* order.
    // "weather" always arrives false — that bit is retired (v5), and weather is now inherited via
    // wx_mode=1. It's kept in the object because the bits are POSITIONAL; the app no longer renders that row.
    w.Key("inherit"); w.BeginObject();
    { static const char* const kInhToks[7] = { "weather", "hour", "timeProg", "start", "pit", "ai", "sessions" };
      for (int i = 0; i < 7; i++) w.KB(kInhToks[i], (v.inheritMask & (1 << i)) != 0); }
    w.EndObject();
    // Jul 21: the save's REWARD mode (read-only; fixed when the career is created). Not to be confused with
    // difficulty/selectedMode above, which is the on-track AI.
    w.Key("reward"); w.BeginObject();
    w.KI("mode", v.rewardMode);      // 0=easy 1=normal 2=hard
    w.KN("mult", v.rewardMult);      // income/REP/XP multiplier (the app renders the %)
    w.EndObject();
    w.KB("telemetryOptIn", v.telemetryOptIn);   // telemetry-consent flag (off by default)
    w.KB("telemetryAsked", v.telemetryAsked);   // false = not asked yet (first-time dialog)
    // Aug 2: the calibration race's seed, and whether it's already been run. Once calibrated=true, the app stops
    // offering the control: from then on, the number is learned by the feedback loop.
    w.KI("aiSeed", v.aiSeed);
    w.KB("calibrated", v.calibrated);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

static GarageViewProvider g_garageProvider;
void SetGarageViewProvider(GarageViewProvider p) { g_garageProvider = std::move(p); }

// tokens parallel to career::RatingTierLabel (the engine only gives the localized literal, not a TR key).
static const char* TierToken(int band) {
    static const char* T[6] = { "unknown", "prospect", "known", "established", "star", "legend" };
    if (band < 0) band = 0; if (band > 5) band = 5; return T[band];
}

// query.garage: owned cars + contract strip + band/credits. Serializes only (color->CssVar,
// tier->token, raw facets). loaded=false->not_ready. owned/class counts are derived by the app from cars[].
HandlerResult QueryGarage(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_garageProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    GarageView v = g_garageProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    jsonw::Writer w; w.BeginObject();
    w.KI("credits", v.credits);
    w.Key("tier"); w.BeginObject();
    w.KI("band", v.tierBand);
    w.KS("token", TierToken(v.tierBand));
    w.KS("color", CssVar(v.tierColor));
    w.EndObject();

    w.Key("contract");
    if (v.contract.active) {
        const GarageContractView& c = v.contract;
        w.BeginObject();
        w.KS("series", c.seriesDisp);
        w.KI("seasonsLeft", c.seasonsLeft);
        w.KI("seasonsTotal", c.seasonsTotal);
        w.KB("selfFunded", c.kind == 1);
        w.KI("perRace", c.perRace);
        w.KB("roleLead", c.roleLead);
        w.KI("objDone", c.objDone);
        w.KI("objTotal", c.objTotal);
        w.KI("objTargetPos", c.objTargetPos);   // 0.4.0 SEASON objective ("finish Top N"; 0 = no objective)
        w.KB("renewOnTable", c.renewOnTable);   // 0.4.0 silly season: an early renewal waiting in the inbox
        w.KB("fixesCar", c.fixesCar);
        w.KS("teamCar", c.teamCar);
        w.KI("typeLabel", c.typeLabel);
        w.Key("coverage"); w.BeginObject();
        w.KB("seatEntry", c.covSeatEntry);
        w.KB("consumables", c.covConsumables);
        w.KB("damage", c.covDamage);
        w.KB("travel", c.covTravel);
        w.EndObject();
        w.EndObject();
    } else { w.Null(); }

    w.Key("cars"); w.BeginArray();
    for (const auto& c : v.cars) {
        w.BeginObject();
        w.KS("id", c.id);
        w.KS("name", c.name);
        w.KS("classId", c.classId);
        w.KS("className", c.className);   // "" -> the app renders "—"
        w.KS("classShort", c.classShort);
        w.KS("classColor", CssVar(c.classColor));
        w.KI("year", c.year);
        w.Key("stats"); w.BeginObject();
        w.KI("pwr", c.pwr); w.KI("grp", c.grp); w.KI("bal", c.bal);
        w.EndObject();
        w.KI("vehHash", c.vehHash);
        w.KI("livery", c.livery);
        w.KS("slug", career::SlugForVehicleEnsured(c.vehHash));   // the app builds the PNG path from the slug (same as prerace)
        // 0.7.0 (car selling): km with one decimal, sale price and why it can't be sold ("ok" = yes).
        w.KN("km", std::floor(c.km * 10.0 + 0.5) / 10.0);
        w.KI("sellPrice", c.sellPrice);
        w.KS("sellBlock", c.sellBlock);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

static GarageSellExec g_garageSellExec;
void SetGarageSellExec(GarageSellExec e) { g_garageSellExec = std::move(e); }

// 0.7.0 (thread «The ability to sell cars»): sell one garage unit. The engine re-validates everything (CarOpSell): the
// app only requests. reason always travels so the screen can say WHY it wasn't sold.
HandlerResult CmdGarageSell(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_garageSellExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string carId = params ? params->memberString("carId") : std::string();
    if (carId.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing carId"; return r; }
    int livery = params ? params->memberInt("livery", -1) : -1;
    std::string reason; long paid = 0;
    const bool ok = g_garageSellExec(carId, livery, &reason, &paid);
    jsonw::Writer w; w.BeginObject();
    w.KB("ok", ok); w.KS("carId", carId); w.KS("reason", ok ? std::string("ok") : reason); w.KI("paid", ok ? paid : 0);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// Task 11: query.sponsors's provider/writer/handler (g_sponsorsProvider, WriteBiText, WriteSponsorTraits,
// QuerySponsors) retired with the objective-sponsor catalog. The cards system's query.cards (Task 9, below)
// is the live replacement.

static MarketViewProvider g_marketProvider;
void SetMarketViewProvider(MarketViewProvider p) { g_marketProvider = std::move(p); }

// query.market: driver market (mirrors screen_market.cpp). Serializes raw (color->CssVar, tokens/
// keys as strings, bilingual literals in pairs). loaded=false->not_ready; empty offers are NOT an error.
HandlerResult QueryMarket(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_marketProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    MarketView v = g_marketProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    jsonw::Writer w; w.BeginObject();
    w.Key("reputation"); w.BeginObject();
    w.KI("score", v.repScore);
    w.KI("tierIdx", v.repIdx);
    w.KI("pct", v.repPct);
    w.KS("tierKey", v.tierKey);
    w.KS("tierColor", CssVar(v.tierColor));
    w.Key("next");
    if (v.hasNext) { w.BeginObject(); w.KS("tierKey", v.nextTierKey); w.KI("min", v.nextTierMin); w.KI("gap", v.nextTierMin - v.repScore); w.EndObject(); }
    else w.Null();
    w.Key("dots"); w.BeginArray();
    for (int i = 0; i < (int)v.tierDots.size(); i++) { w.BeginObject(); w.KS("color", CssVar(v.tierDots[i].color)); w.KB("filled", i <= v.repIdx); w.EndObject(); }
    w.EndArray();
    w.EndObject();

    w.Key("stats"); w.BeginObject();
    w.KI("wins", v.wins); w.KI("podiums", v.podiums); w.KI("races", v.races);
    w.EndObject();

    w.Key("contract");
    if (v.contract.active) {
        w.BeginObject();
        w.KI("count", v.contract.count);
        if (v.contract.count == 1) { w.KS("teamName", v.contract.teamName); w.KS("seriesName", v.contract.seriesName); w.KI("kind", v.contract.kind); }
        else { w.Key("seriesNames"); w.BeginArray(); for (const auto& s : v.contract.seriesNames) w.S(s); w.EndArray(); }
        w.EndObject();
    } else w.Null();

    w.KI("seekingYouCount", v.seekingYouCount);
    w.KI("openSeatsCount", v.openSeatsCount);
    w.KI("playerRating", v.playerRating);
    w.KB("marketOpen", v.marketOpen);        // market hours: false = paddock, no signing
    w.KI("daysToWindow", v.daysToWindow);

    w.Key("offers"); w.BeginArray();
    for (const auto& o : v.offers) {
        w.BeginObject();
        w.KS("id", o.id);
        w.KS("category", o.category);
        w.KI("state", o.state);
        w.KB("held", o.held);
        w.KB("direct", o.direct);
        w.KS("team", o.team);
        w.KS("from", o.from);
        w.KS("title", o.title); w.KS("titleEn", o.titleEn);
        w.KS("teamColor", CssVar(o.teamColor));
        w.KS("className", o.className);
        w.KS("clsId", o.clsId);
        w.KS("discColor", CssVar(o.discColor));
        w.KS("scope", o.scope);
        w.KI("commitment", o.commitment);
        w.KI("contractKind", o.contractKind);
        w.KS("contractKindIcon", o.contractKindIcon);
        w.KS("contractKindColor", CssVar(o.contractKindColor));
        w.KB("requiresTryout", o.requiresTryout);
        w.KB("canAfford", o.canAfford);
        w.KI("reqRep", o.reqRep);
        w.KB("displacement", o.displacement);
        w.KI("buyIn", o.buyIn);
        w.KS("expectationKey", o.expectationKey);
        w.KS("expectation", o.expectation); w.KS("expectationEn", o.expectationEn);
        w.KS("pitchKey", o.pitchKey);
        w.KB("pitchPrestige", o.pitchPrestige);
        w.Key("why"); w.BeginObject(); w.KI("rating", o.whyRating); w.KI("form", o.whyForm); w.KI("rel", o.whyRel); w.EndObject();
        w.KI("signBonus", o.signBonus);
        w.KI("perRace", o.perRace);
        w.KS("seatName", o.seatName);
        w.Key("includes"); w.BeginObject();
        w.KI("typeLabel", o.typeLabel);
        w.KB("seatEntry", o.seatEntryCovered);
        w.KB("consumables", o.consumablesCovered);
        w.KB("damage", o.damageCovered);
        w.KB("travel", o.travelCovered);
        w.EndObject();
        w.KS("lockReason", o.lockReason); w.KS("lockReasonEn", o.lockReasonEn);
        // contract preview: the team's fixed car+skin (works/customer). Absent if the player gets to choose.
        w.KB("fixesCar", o.fixesCar);
        if (o.fixesCar) { w.KS("teamCarName", o.teamCarName); w.KS("teamCarSlug", o.teamCarSlug); w.KI("teamLiveryNN", o.teamLiveryNN); }
        w.Key("conflicts"); w.BeginArray();
        for (const auto& c : o.conflicts) w.S(c.seriesName);
        w.EndArray();
        w.KI("archetype", o.archetype);
        w.KI("weeksLeft", o.weeksLeft);
        if (o.daysLeft > 0) w.KI("daysLeft", o.daysLeft);   // 18/09: the «last day» mark
        // who you contested the seat against ("" = uncontested / nobody else in the running).
        w.KS("rivalId", o.rivalId); w.KS("rivalName", o.rivalName);
        // negotiation redesign: the negotiation thread's state (if any) for the card's chip.
        w.KB("negotiable", o.negotiable); w.KI("negState", o.negState); w.KI("negRespondDays", o.negRespondDays);
        w.KS("parentTeam", career::ParentTeamFor(o.team));   // "" if the offer isn't from an affiliated feeder team
        // team<->team relations for the offer (direct allies/rivals + prestige pathways).
        w.Key("rel"); w.BeginObject();
        w.Key("allies"); w.BeginArray();
        for (const auto& e : career::TeamRelationsFor(o.team))
            if (e.kind == "family" || e.kind == "ally") { w.BeginObject(); w.KS("team", e.other); w.KS("kind", e.kind); w.EndObject(); }
        w.EndArray();
        w.Key("rivals"); w.BeginArray();
        for (const auto& e : career::TeamRelationsFor(o.team))
            if (e.kind == "rival" || e.kind == "bitter") { w.BeginObject(); w.KS("team", e.other); w.KS("kind", e.kind); w.EndObject(); }
        w.EndArray();
        w.Key("pathways"); w.BeginArray();
        for (const auto& p : career::TeamPathways(o.team, 3)) { w.BeginObject(); w.KS("team", p.first); w.KI("prestige", p.second); w.EndObject(); }
        w.EndArray();
        w.EndObject();
        w.EndObject();
    }
    w.EndArray();
    // seats currently on trial. A SEPARATE list from the offers: they aren't yours yet.
    w.Key("watching"); w.BeginArray();
    for (const auto& t : v.watching) {
        w.BeginObject();
        w.KS("seriesId", t.seriesId); w.KS("seriesName", t.seriesName);
        w.KS("teamName", t.teamName); w.KS("rivalName", t.rivalName);
        w.KI("racesLeft", t.racesLeft);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

static HubViewProvider g_hubProvider;
void SetHubViewProvider(HubViewProvider p) { g_hubProvider = std::move(p); }

// query.hub: HOME (mirrors screen_hub.cpp). Driver card + next race + badges + progress +
// lifecycle. Serializes raw (color->CssVar, tier->key, dates already formatted). loaded=false->not_ready.
HandlerResult QueryHub(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_hubProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    HubView v = g_hubProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    jsonw::Writer w; w.BeginObject();
    w.Key("identity"); w.BeginObject();
    w.KS("first", v.first); w.KS("last", v.last);
    w.EndObject();
    w.Key("reputation"); w.BeginObject();
    w.KI("score", v.repScore); w.KI("tierIdx", v.repIdx); w.KI("pct", v.repPct);
    w.KS("tierKey", v.tierKey); w.KS("tierColor", CssVar(v.tierColor));
    w.Key("next");
    if (v.hasNext) { w.BeginObject(); w.KS("tierKey", v.nextTierKey); w.KI("min", v.nextTierMin); w.KI("gap", v.nextTierMin - v.repScore); w.EndObject(); }
    else w.Null();
    w.KI("followers", v.followers);
    w.EndObject();
    w.Key("stats"); w.BeginObject();
    w.KI("wins", v.wins); w.KI("podiums", v.podiums); w.KI("races", v.races);
    w.EndObject();
    w.Key("nextRace");
    if (v.nextValid) {
        w.BeginObject();
        w.KS("series", v.nextSeries); w.KS("track", v.nextTrack); w.KS("date", v.nextDate);
        w.KI("round", v.nextRound); w.KI("doy", v.nextDoy);
        w.KS("weekday", v.nextDoy > 0 ? career::WeekdayLabel(v.year, v.nextDoy) : std::string());   // additive
        w.EndObject();
    } else w.Null();
    w.Key("badges"); w.BeginObject();
    w.KI("directOffers", v.directOffers); w.KI("unread", v.unread);
    w.EndObject();
    w.Key("lifecycle"); w.BeginObject();
    w.KI("enrollmentCount", v.enrollmentCount); w.KB("contractActive", v.contractActive);
    w.KI("contractCount", v.contractCount);     // number of active contracts (hub at-a-glance)
    w.KS("contractSeries", v.contractSeries);   // names the contract in the break-contract button/modal
    // the hub button breaks EVERY contract: the modal states the TOTAL cost (BreakContractFee(save,""),
    // the same function that later charges it) and WHO you're leaving stranded — not the "costs 10 REP" of some invisible scale.
    w.KI("breakFee", v.breakFee);
    w.Key("contractTeams"); w.BeginArray();
    for (const auto& tm : v.contractTeams) w.S(tm);
    w.EndArray();
    w.KS("cursorDate", v.cursorDate); w.KI("year", v.year);
    w.KS("weekday", career::WeekdayLabel(v.year, v.cursorDoy));   // real day of the week, additive
    w.EndObject();
    w.Key("progress"); w.BeginObject();
    w.KI("ownedCount", v.ownedCount); w.KI("completedRaces", v.completedRaces);
    w.EndObject();
    // unified rating (ELO) + delta + phase progression (XP) for the hub.
    w.Key("rating"); w.BeginObject();
    w.KI("elo", v.eloRating); w.KI("delta", v.eloDelta);
    w.KI("xpToward", (int)v.xpToward); w.KI("xpThreshold", (int)v.xpThreshold); w.KI("phase", v.phase);
    w.EndObject();
    // recent results (pos<=0 = DNF), most recent first.
    w.Key("recentResults"); w.BeginArray();
    for (const auto& rr : v.recentResults) { w.BeginObject(); w.KI("pos", rr.pos); w.KI("field", rr.field); w.KS("track", rr.track); w.KB("finished", rr.finished); w.EndObject(); }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.identity: the career-creation GATE (mirrors screen_identity.cpp). Serializes raw. IMPORTANT:
// loaded=true even with needed=true (the save IS loaded, the identity is just blank) -> NOT not_ready,
// so the app can render the form. Only not_ready if the provider isn't installed or the save hasn't loaded.
static IdentityViewProvider g_identityProvider;
void SetIdentityViewProvider(IdentityViewProvider p) { g_identityProvider = std::move(p); }
// hub (see the decl above): the card as display. Reads the provider from query.identity (raw from the save).
static std::string PlayerFichaDisplay() {
    if (!g_identityProvider) return std::string();
    IdentityView iv = g_identityProvider();
    if (iv.first.empty()) return std::string();
    return iv.last.empty() ? iv.first : iv.first + " " + iv.last;
}
HandlerResult QueryIdentity(const jsonm::Value* /*params*/) {
    HandlerResult r;
    if (!g_identityProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    IdentityView v = g_identityProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }
    jsonw::Writer w; w.BeginObject();
    w.KB("needed", v.needed);
    w.KS("amsName", v.amsName);
    w.KI("startYear", v.startYear);
    w.KS("first", v.first); w.KS("last", v.last); w.KS("sex", v.sex); w.KS("nationality", v.nationality);
    w.KI("birthYear", v.birthYear); w.KI("birthMonth", v.birthMonth); w.KI("birthDay", v.birthDay);
    w.KI("age", v.age);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.negotiate: NEGOTIATION (mirrors screen_negotiate.cpp), parametrized by {offerId}. If the offer no longer
// exists -> {found:false} (the app goes back to Market). Serializes the catalogs (demands/salary/sponsors) so
// the client can replicate the margin math per toggle; baseMargin is fixed. Colors -> CssVar; text as {en,es}.
static NegotiateViewProvider g_negotiateProvider;
void SetNegotiateViewProvider(NegotiateViewProvider p) { g_negotiateProvider = std::move(p); }
HandlerResult QueryNegotiate(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_negotiateProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string offerId = params ? params->memberString("offerId") : std::string();
    NegotiateView v = g_negotiateProvider(offerId);
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }
    jsonw::Writer w; w.BeginObject();
    w.KB("found", v.found);
    w.KS("offerId", v.offerId);
    if (v.found) {
        w.KS("teamName", v.teamName); w.KS("teamColor", CssVar(v.teamColor));
        w.KI("rating", v.rating); w.KI("relationship", v.relationship);
        w.KI("startBonus", v.startBonus); w.KI("startPerRace", v.startPerRace);
        w.KI("startDurationSeasons", v.startDurationSeasons); w.KB("startProtectionNone", v.startProtectionNone);
        w.KI("negotiationLevel", v.negotiationLevel); w.KI("competing", v.competing);
        // negotiation redesign: band/table/patience/history for the async thread; the EXACT margin
        // (baseMargin/weakThresh/marginMax/wantT/trialRatio) no longer travels — it stays in the struct for
        // the legacy screen_negotiate.cpp, which reads the field directly, not this JSON.
        w.KB("negotiable", v.negotiable);
        w.KS("negId", v.negId);
        w.KI("negState", v.negState); w.KI("negRound", v.negRound);
        w.KI("patienceBand", v.patienceBand);
        w.KI("budgetLo", v.budgetLo); w.KI("budgetHi", v.budgetHi);
        w.KB("tableIsCounter", v.tableIsCounter);
        w.KI("respondInDays", v.respondInDays); w.KI("deadlineWeeks", v.deadlineWeeks);
        if (v.deadlineDays > 0) w.KI("deadlineDays", v.deadlineDays);   // 18/09
        w.Key("table"); w.BeginObject();
        w.Key("demands"); w.BeginArray();
        for (int i = 0; i < 8; i++) w.B(v.table.demands[i]);
        w.EndArray();
        w.KI("salaryPct", v.table.salaryPct); w.KI("sponsorTierRank", v.table.sponsorTierRank);
        w.EndObject();
        w.Key("history"); w.BeginArray();
        for (const auto& h : v.history) {
            w.BeginObject();
            w.KI("day", h.day); w.KI("who", h.who); w.KI("kind", h.kind);
            w.KI("demandsMask", (long long)h.demandsMask); w.KI("salaryPct", h.salaryPct);
            w.EndObject();
        }
        w.EndArray();
        w.KI("credits", v.credits);
        w.Key("demands"); w.BeginArray();
        for (const auto& d : v.demands) {
            w.BeginObject();
            w.KI("demandId", d.demandId); w.KS("en", d.en); w.KS("es", d.es); w.KS("descEn", d.descEn); w.KS("descEs", d.descEs);
            w.KI("cost", d.cost); w.KB("positive", d.positive);
            w.EndObject();
        }
        w.EndArray();
        w.Key("salaryPackages"); w.BeginArray();
        for (const auto& s : v.salaryPackages) { w.BeginObject(); w.KI("pct", s.pct); w.KI("cost", s.cost); w.EndObject(); }
        w.EndArray();
        w.Key("sponsorChips"); w.BeginArray();
        for (const auto& c : v.sponsorChips) {
            w.BeginObject();
            w.KI("tierRank", c.tierRank); w.KS("name", c.name); w.KS("tierEn", c.tierEn); w.KS("tierEs", c.tierEs);
            w.KS("color", CssVar(c.color)); w.KI("delta", c.delta); w.KS("cardId", c.cardId);
            w.EndObject();
        }
        w.EndArray();
        w.KI("initialSalaryPct", v.initialSalaryPct); w.KI("initialSponsorTierRank", v.initialSponsorTierRank);
    }
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.season: CHAMPIONSHIPS (mirrors screen_season.cpp's DrawSeasonSelect), parametrized by {seriesId}. The
// provider (overlay) assembles the rail (3 sections) + the detail (3 mutually exclusive variants, priority
// enrolled>access>info) + the end-of-season bonus; the handler only serializes RAW data (discColor ImU32->CssVar).
// loaded=false -> not_ready. seriesId="" or not present on the rail -> the provider applies the default (the rail's 1st).
static SeasonViewProvider g_seasonProvider;
void SetSeasonViewProvider(SeasonViewProvider p) { g_seasonProvider = std::move(p); }

// super-license data for the Profile. The provider (overlay) composes it; only raw serialization happens here.
static SuperlicensesProvider g_superlicensesProvider;
void SetSuperlicensesProvider(SuperlicensesProvider p) { g_superlicensesProvider = std::move(p); }
HandlerResult QuerySuperlicenses(const jsonm::Value* params) {
    (void)params;
    HandlerResult r;
    if (!g_superlicensesProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    SuperlicensesView v = g_superlicensesProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career not loaded"; return r; }
    jsonw::Writer w; w.BeginObject();
    w.Key("licenses"); w.BeginArray();
    for (const auto& s : v.rows) {
        w.BeginObject();
        w.KS("champId", s.champId); w.KS("champDisplay", s.champDisplay);
        w.KB("unlocked", s.unlocked);
        w.KS("qualDisplay", s.qualDisplay); w.KB("qualHasCal", s.qualHasCal);
        w.KS("qualNextDate", s.qualNextDate); w.KI("gateTop", s.gateTop);
        w.KI("qualLevel", s.qualLevel);   // tester note 29/08: "opens at Tier N" (the test opens at ITS tier, not its class's)
        w.KB("hasFeeder", s.hasFeeder); w.KS("feederClass", s.feederClass); w.KI("feederTop", s.feederTop);
        w.KB("feederHasContext", s.feederHasContext); w.KI("feederContextPos", s.feederContextPos);
        w.KS("feederContextSeries", s.feederContextSeries);
        w.EndObject();
    }
    w.EndArray(); w.EndObject();
    r.ok = true; r.payload = w.Str();
    return r;
}
HandlerResult QuerySeason(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_seasonProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string seriesId = params ? params->memberString("seriesId") : std::string();
    SeasonView v = g_seasonProvider(seriesId);
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }

    jsonw::Writer w; w.BeginObject();
    w.KS("playerName", v.playerName);
    w.KS("selectedSeriesId", v.selectedSeriesId);
    w.Key("rail"); w.BeginArray();
    for (const auto& it : v.rail) {
        w.BeginObject();
        w.KS("seriesId", it.seriesId);
        w.KS("display", it.display);
        w.KS("classShort", it.classShort);   // class_name in UPPERCASE, '_'->' ' (composed by the provider)
        w.KS("className", it.className);      // raw class
        w.KS("discColor", CssVar(it.discColor));   // RaceListColor -> #hex/var(--x)
        w.KI("section", it.section);          // 0=mine, 1=available, 2=locked
        w.KB("gateLocked", it.gateLocked);    // section 2: super-license vs level
        w.KI("level", it.level);
        w.KB("selected", it.selected);
        w.KI("playerPos", it.playerPos);      // section 0
        w.KI("myPoints", it.myPoints);
        w.KI("roundIdx", it.roundIdx);
        w.KI("totalRounds", it.totalRounds);
        w.KI("field", it.field);
        w.KB("done", it.done);
        w.EndObject();
    }
    w.EndArray();

    w.Key("detail"); w.BeginObject();
    w.KI("kind", v.detailKind);               // 0=none 1=enrolled 2=access 3=info
    w.KS("seriesDisplay", v.detailSeriesDisplay);
    w.KI("roundIdx", v.detailRoundIdx);
    w.KI("totalRounds", v.detailTotalRounds);
    w.KI("seasonYear", v.detailSeasonYear);
    w.KI("myPos", v.detailMyPos);
    w.KI("myPts", v.detailMyPts);
    w.Key("pointsScale"); w.BeginArray();
    for (int p : v.detailPointsScale) w.I(p);
    w.EndArray();
    w.Key("rounds"); w.BeginArray();
    for (const auto& c : v.detailRounds) {
        w.BeginObject();
        w.KI("round", c.round);
        w.KS("trackName", c.trackName);
        w.KS("date", c.date);                 // "Jun 15", raw from the calendar
        w.KI("doy", c.doy);
        // what's needed to decide whether a championship fits you, not just to render the table.
        w.KI("hour", c.hour);
        w.KI("laps", c.laps);                 // laps XOR durationMin, same as the calendar
        w.KI("durationMin", c.durationMin);
        w.KI("practiceMin", c.practiceMin);
        w.KI("qualifyMin", c.qualifyMin);
        w.EndObject();
    }
    w.EndArray();
    w.Key("standings"); w.BeginArray();
    for (const auto& row : v.detailStandings) {
        w.BeginObject();
        w.KI("rank", row.rank);
        { std::string f = row.isPlayer ? PlayerFichaDisplay() : std::string();
          w.KS("name", f.empty() ? row.name : f); }   // hub your row, your CURRENT name
        w.KB("isPlayer", row.isPlayer);
        w.KI("total", row.total);
        w.Key("cells"); w.BeginArray();
        for (const auto& cell : row.cells) {
            w.BeginObject();
            w.KI("round", cell.round);
            w.KI("pos", cell.pos);
            w.KI("points", cell.points);
            w.KB("dnf", cell.dnf);
            w.KB("ran", cell.ran);            // false => a muted "-" (round not raced by the driver)
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();
    w.KI("nextRaceDoy", v.detailNextRaceDoy);
    // ACCESS (kind==2): the 2 pathways (qualifying + feeder season)
    w.KS("qualDisplay", v.detailQualDisplay);
    w.KS("qualFirstDate", v.detailQualFirstDate);
    w.KB("qualHasCal", v.detailQualHasCal);
    w.KB("hasFeeder", v.detailHasFeeder);
    w.KS("feederClass", v.detailFeederClass);        // raw class ('_'->' ' + uppercasing is done by the app)
    w.KI("feederTop", v.detailFeederTop);
    w.KB("feederHasContext", v.detailFeederHasContext);
    w.KI("feederContextPos", v.detailFeederContextPos);
    w.KS("feederContextSeries", v.detailFeederContextSeries);
    w.EndObject();   // detail

    // BONUS modal (a GLOBAL flag, independent of the selected series): +money CR / +xp XP / +rep REP
    w.KB("bonusShown", v.bonusShown);
    w.KI("bonusPlayerPos", v.bonusPlayerPos);
    w.KI("bonusField", v.bonusField);
    w.KI("bonusMoney", v.bonusMoney);
    w.KI("bonusXp", v.bonusXp);
    w.KI("bonusRep", v.bonusRep);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// ===== COMMANDS (cmd.*) ======================================================================
static SettingsSetModeExec g_setModeExec;
static SettingsSetTrimExec g_setTrimExec;
static SettingsSetAggrExec g_setAggrExec;   //
static SettingsSetScaleExec g_setScaleExec; //
static SettingsSetFcyExec   g_setFcyExec;   //
static SettingsSetWeatherExec g_setWeatherExec;   // weather mode + weights
static SettingsSetInheritExec g_setInheritExec;   //
void SetSettingsCmds(SettingsSetModeExec sm, SettingsSetTrimExec st, SettingsSetAggrExec sa, SettingsSetScaleExec sc) {
    g_setModeExec = std::move(sm); g_setTrimExec = std::move(st); g_setAggrExec = std::move(sa); g_setScaleExec = std::move(sc);
}
void SetSettingsCmds2(SettingsSetFcyExec sf) { g_setFcyExec = std::move(sf); }
void SetSettingsCmds5(SettingsSetWeatherExec sw) { g_setWeatherExec = std::move(sw); }
void SetSettingsCmds3(SettingsSetInheritExec si) { g_setInheritExec = std::move(si); }
static SettingsSetTelemetryExec g_setTelemetryExec;   // telemetry consent
void SetSettingsCmds4(SettingsSetTelemetryExec st) { g_setTelemetryExec = std::move(st); }
static SettingsSetSeedExec g_setSeedExec;             // Aug 2: the calibration race's seed
void SetSettingsCmds5(SettingsSetSeedExec ss) { g_setSeedExec = std::move(ss); }
static SettingsSetCustomAiExec g_setCustomAiExec;    // FIXED difficulty chosen by hand (DM_CUSTOM)
void SetSettingsCmds6(SettingsSetCustomAiExec sc) { g_setCustomAiExec = std::move(sc); }
static SettingsSetRewardExec g_setRewardExec;        // 0.7.0: career pace from Settings
void SetSettingsCmds8(SettingsSetRewardExec sr) { g_setRewardExec = std::move(sr); }

// 0.7.0 — cmd.settings.setReward { mode: "easy"|"normal"|"hard" } -> sets+persists the career PACE
// (prog.reward_mode). Same vocabulary as profile.json and the create-career card. Applies from the
// NEXT settlement: what was already paid is not recomputed. {ok, mode, mult}; bad_params with an unknown token.
HandlerResult CmdSettingsSetReward(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setRewardExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    const std::string tok = params ? params->memberString("mode") : std::string();
    const int mode = career::RewardModeFromToken(tok);
    if (mode < 0) { r.ok = false; r.code = "bad_params"; r.message = "unknown reward mode: " + tok; return r; }
    const bool ok = g_setRewardExec(mode);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KS("mode", tok); w.KN("mult", career::RewardMult(mode)); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.dbg.mem — an on-demand memory probe (see the header). Pure diagnostics: it only READS process
// memory. With no implementation registered it answers not_ready instead of pretending nothing's wrong.
static DbgMemExec g_dbgMemExec;
void SetDbgMem(DbgMemExec exec) { g_dbgMemExec = std::move(exec); }
HandlerResult CmdDbgMem(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_dbgMemExec) { r.ok = false; r.code = "not_ready"; r.message = "debug probe not wired"; return r; }
    r.payload = g_dbgMemExec(params);
    return r;
}

// cmd.settings.setTelemetry { on: bool } -> sets+persists telemetry-send consent.
HandlerResult CmdSettingsSetTelemetry(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setTelemetryExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    bool on = params && params->memberBool("on", false);
    bool ok = g_setTelemetryExec(on);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KB("on", on); w.EndObject();
    r.payload = w.Str();
    return r;
}

// Aug 2 — cmd.settings.setSeed { seed: int } -> sets+persists the level the CALIBRATION race will run
// at. Only has an effect before calibrating: afterward the level is learned by the feedback loop, and this returns
// ok=false (code "already_calibrated") without touching anything. Clamping to the slider's domain is done by the bridge.
HandlerResult CmdSettingsSetSeed(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setSeedExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    if (!params || !params->member("seed")) { r.ok = false; r.code = "bad_params"; r.message = "missing seed"; return r; }
    int seed = params->memberInt("seed", career::dda::kAiSeed);
    bool ok = g_setSeedExec(seed);
    if (!ok) { r.ok = false; r.code = "already_calibrated"; r.message = "the measurement race already ran"; return r; }
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true);
    w.KI("seed", career::Clampi(seed, career::dda::kAiMin, career::dda::kAiMax)); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.settings.setCustomAi { value: int } -> sets+persists the hand-picked FIXED difficulty card. Always
// applicable (unlike setSeed, which closes once calibrated): that card's whole promise is that
// the number is set by the player and nobody else. Clamping to the slider's domain is done by the bridge; here
// the already-clamped value is returned so the screen doesn't need to know the range.
HandlerResult CmdSettingsSetCustomAi(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setCustomAiExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    if (!params || !params->member("value")) { r.ok = false; r.code = "bad_params"; r.message = "missing value"; return r; }
    int value = params->memberInt("value", career::dda::kCustomDefault);
    bool ok = g_setCustomAiExec(value);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok);
    w.KI("value", career::Clampi(value, career::dda::kAiMin, career::dda::kAiMax)); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.results.feel { feel: 1..5, reasons: [token...] }. The scale is the only required field:
// the reasons are optional and travel as-is (the app owns the catalog and translates them). The
// LOCAL effect is applied by the bridge; here only the range is validated and the resulting class delta is returned
// so the screen can say whether this answer moved anything.
static FeelCmdExec     g_feelExec;
static GapOfferCmdExec g_gapOfferExec;
void SetResultsFeedbackCmds(FeelCmdExec feel, GapOfferCmdExec offer) {
    g_feelExec = std::move(feel); g_gapOfferExec = std::move(offer);
}

HandlerResult CmdResultsFeel(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_feelExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    int feel = params ? params->memberInt("feel", 0) : 0;
    if (feel < 1 || feel > 5) { r.ok = false; r.code = "bad_params"; r.message = "feel must be 1..5"; return r; }
    std::vector<std::string> reasons;
    if (params) {
        const jsonm::Value* arr = params->member("reasons");
        if (arr && arr->isArray())
            for (const auto& it : arr->arr)
                if (it && it->isString() && !it->str.empty()) reasons.push_back(it->str);
    }
    double delta = 0.0;
    bool ok = g_feelExec(feel, reasons, &delta);
    if (!ok) { r.ok = false; r.code = "not_ready"; r.message = "no settled race to attach the answer to"; return r; }
    jsonw::Writer w; w.BeginObject();
    w.KB("ok", true); w.KI("feel", feel); w.KN("classDelta", delta);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.results.gapOffer { accept: bool }. Accepting sets the difficulty the feedback loop
// would have set had it trusted the whole sample; declining is an EXACT no-op (the winsorized path
// already applied at settlement stays), and it just closes the offer so it won't show again for this race.
HandlerResult CmdResultsGapOffer(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_gapOfferExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    bool accept = params && params->memberBool("accept", false);
    int difficulty = 0;
    bool ok = g_gapOfferExec(accept, &difficulty);
    if (!ok) { r.ok = false; r.code = "not_ready"; r.message = "no difficulty offer pending"; return r; }
    jsonw::Writer w; w.BeginObject();
    w.KB("ok", true); w.KB("accepted", accept); w.KI("difficulty", difficulty);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.settings.setWeather { mode: int, weights: { "<code>": <weight> } } -> sets+persists the
// weather mode and, if the object is present, the player's selection. weights ABSENT = the saved ones are untouched
// (switching modes can't wipe out a selection that took effort to make).
HandlerResult CmdSettingsSetWeather(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setWeatherExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    const int mode = params ? (int)params->memberNumber("mode", 0.0) : 0;
    if (mode < 0 || mode > 2) { r.ok = false; r.code = "bad_params"; r.message = "mode out of range"; return r; }
    std::map<int,int> weights;
    if (params) {
        const jsonm::Value* w = params->member("weights");
        if (w && w->isObject())
            for (const auto& kv : w->obj) {
                const int code = atoi(kv.first.c_str());
                if (code >= 0 && code <= 17 && kv.second) weights[code] = kv.second->asInt(0);
            }
    }
    const bool ok = g_setWeatherExec(mode, weights);
    jsonw::Writer w2; w2.BeginObject(); w2.KB("ok", ok); w2.KI("mode", mode); w2.EndObject();
    r.payload = w2.Str();
    return r;
}
// cmd.settings.setFcy { mode: 0|1|2 } -> sets+persists FCY/safety car (0=No 1=Yes 2=Inherited).
// The legacy { on: bool } is still accepted (old app): on -> Yes, off -> No.
HandlerResult CmdSettingsSetFcy(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setFcyExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    int mode = params ? (int)params->memberInt("mode", -1) : -1;
    if (mode < 0 || mode > 2) mode = (params && params->memberBool("on", false)) ? 1 : 0;
    bool ok = g_setFcyExec(mode);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KI("mode", mode); w.EndObject();
    r.payload = w.Str();
    return r;
}
static SettingsSetClassLockExec g_setClassLockExec;
static SettingsSetLockAllExec   g_setLockAllExec;
void SetSettingsCmds7(SettingsSetClassLockExec setClassLock, SettingsSetLockAllExec setLockAll) {
    g_setClassLockExec = std::move(setClassLock); g_setLockAllExec = std::move(setLockAll);
}
// #DDA-lock — cmd.settings.setClassLock { classId, locked } -> freezes/unfreezes ONE category.
HandlerResult CmdSettingsSetClassLock(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setClassLockExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string cid = params ? params->memberString("classId") : std::string();
    if (cid.empty()) { r.ok = false; r.code = "bad_params"; r.message = "classId required"; return r; }
    bool locked = params && params->memberBool("locked", false);
    bool ok = g_setClassLockExec(cid, locked);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.EndObject();
    r.payload = w.Str();
    return r;
}
// #DDA-lock — cmd.settings.setLockAll { locked } -> the master switch.
HandlerResult CmdSettingsSetLockAll(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setLockAllExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    bool locked = params && params->memberBool("locked", false);
    bool ok = g_setLockAllExec(locked);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.EndObject();
    r.payload = w.Str();
    return r;
}
// cmd.settings.setScale { tyre: bool, fuel: bool } -> sets+persists the gameplay-scaling toggles.
HandlerResult CmdSettingsSetScale(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setScaleExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    bool tyre = params ? params->memberBool("tyre", true) : true;
    bool fuel = params ? params->memberBool("fuel", true) : true;
    bool ok = g_setScaleExec(tyre, fuel);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KB("tyre", tyre); w.KB("fuel", fuel); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.settings.setInherit { field: token, inherit: bool } -> sets+persists the AUTO/Inherited mode
// for a field (bits in prog.inherit_mask). The executor validates the token against the engine's single
// source of truth (career::SettingsInheritBit) and returns false if it doesn't exist -> bad_params.
HandlerResult CmdSettingsSetInherit(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setInheritExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string field = params ? params->memberString("field") : std::string();
    bool inherit = params && params->memberBool("inherit", false);
    if (field.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing field"; return r; }
    bool ok = g_setInheritExec(field, inherit);
    if (!ok) { r.ok = false; r.code = "bad_params"; r.message = "unknown field: " + field; return r; }
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KS("field", field); w.KB("inherit", inherit); w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.content: DLC product status for Settings>Content. scanned=false => the
// live catalogs aren't populated yet: detection isn't trustworthy (fail-open), and the app softens its wording accordingly.
static HandlerResult QueryContent(const jsonm::Value*) {
    HandlerResult r;
    jsonw::Writer w; w.BeginObject();
    w.KB("scanned", career::InstalledTrackCount() > 0 || career::InstalledVehicleCount() > 0);
    w.Key("products"); w.BeginArray();
    for (const auto& p : career::DlcProductStates()) {
        w.BeginObject();
        w.KS("id", p.id); w.KS("display", p.display); w.KS("kind", p.kind);
        w.KB("detected", p.detected); w.KB("enabled", p.enabled);
        // who has the final say on "enabled": detection (auto) or the player (hand-set on/off)
        w.KS("override", p.ovr == career::DlcOverride::On ? "on"
                       : p.ovr == career::DlcOverride::Off ? "off" : "auto");
        // Steam's answer about OWNERSHIP (unknown = no data: no curated appids, or no API)
        w.KS("owned", p.owned == career::Ownership::Owned ? "owned"
                    : p.owned == career::Ownership::NotOwned ? "not_owned" : "unknown");
        w.EndObject();
    }
    w.EndArray(); w.EndObject();
    r.payload = w.Str();
    return r;
}

// DLC in events — query.events.content: for every LIVE event in the registry, whether its track and each of
// its classes are installed, and which product is missing if not. Events don't substitute their track (it IS their track),
// so the app uses this to warn on the card BEFORE the player walks into a dead end.
// scanned=false => live catalogs not populated: everything shows as available (fail-open), and the app shows no warning.
// An empty registry (special_events.json not loaded yet) => events:[] and the app gates nothing.
static HandlerResult QueryEventsContent(const jsonm::Value*) {
    HandlerResult r;
    jsonw::Writer w; w.BeginObject();
    w.KB("scanned", career::InstalledTrackCount() > 0 || career::InstalledVehicleCount() > 0);
    w.Key("events"); w.BeginArray();
    for (const career::SpecialEventDef* ev : career::SpecialEventsAll()) {
        long th = 0; std::string tn;
        career::ResolveEventTrack(*ev, &th, &tn);          // 0 = unresolved: this doesn't claim anything is missing
        career::EventContentState base = career::EventContentFor(th, std::string(), ev->classes);
        w.BeginObject();
        w.KS("id", ev->id);
        w.KB("trackOk", base.trackOk); w.KS("trackProduct", base.trackProduct); w.KS("trackName", tn);
        w.Key("classes"); w.BeginArray();
        for (const auto& c : ev->classes) {
            career::EventContentState cs = career::EventContentFor(0, c, std::vector<std::string>());
            const career::SeriesDef* sd = career::SeriesById(c);
            w.BeginObject();
            w.KS("id", c); w.KS("display", sd ? sd->display : c);
            w.KB("ok", cs.classOk); w.KS("product", cs.classProduct);
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
    }
    w.EndArray(); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.content.toggle { product, mode }: sets the player's choice for a DLC product.
// mode = "auto" (detection decides) | "on" | "off". OFF drops its content from the EFFECTIVE install (simulates
// the pack being absent: substitutions/locks as if it weren't there). Persists to cache\dlc_overrides.psv (a property
// of the INSTALLATION, not the save — a deliberate choice for that card).
// `mode` is the new contract. Without it, the old { product, enabled } from a previous app is accepted, and there
// enabled=true means EXPLICIT ON, not AUTO: that's what that app expects to see when it re-renders the row.
static HandlerResult CmdContentToggle(const jsonm::Value* params) {
    HandlerResult r;
    std::string product = params ? params->memberString("product") : std::string();
    std::string mode = params ? params->memberString("mode") : std::string();
    if (career::DlcProductDisplay(product).empty()) {
        r.ok = false; r.code = "bad_params"; r.message = "unknown product: " + product; return r;
    }
    career::DlcOverride ovr;
    if (mode.empty())        ovr = (params && params->memberBool("enabled", true))
                                       ? career::DlcOverride::On : career::DlcOverride::Off;
    else if (mode == "auto") ovr = career::DlcOverride::Auto;
    else if (mode == "on")   ovr = career::DlcOverride::On;
    else if (mode == "off")  ovr = career::DlcOverride::Off;
    else { r.ok = false; r.code = "bad_params"; r.message = "unknown mode: " + mode; return r; }
    career::SetProductOverride(product, ovr);
    // used to be silently ignored on save, so a write failure returned ok=true and the user discovered on
    // reopening the app that their toggles had reverted to ON. If it can't persist, it now says so.
    if (!career::SaveDlcOverrides(paths::UserData("cache\\dlc_overrides.psv").c_str())) {
        career::UserLog("error", "could not save the DLC selection to cache\\dlc_overrides.psv");
        r.ok = false; r.code = "io_error";
        r.message = "the DLC selection could not be saved (cache\\dlc_overrides.psv)";
        return r;
    }
    // `enabled` on the way back = the already-resolved EFFECTIVE state (in AUTO, detection decides), so the app doesn't
    // have to replicate the rule; either way it re-requests query.content afterward.
    bool eff = ovr != career::DlcOverride::Off;
    for (const auto& st : career::DlcProductStates()) if (st.id == product) { eff = st.enabled; break; }
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KS("product", product);
    w.KS("mode", ovr == career::DlcOverride::On ? "on" : ovr == career::DlcOverride::Off ? "off" : "auto");
    w.KB("enabled", eff); w.EndObject();
    r.payload = w.Str();
    return r;
}

// difficulty token -> DifficultyMode index (inverse of ModeToken); -1 if unknown.
static int ModeFromToken(const std::string& t) {
    if (t == "easy") return 0; if (t == "medium") return 1; if (t == "hard") return 2;
    if (t == "extreme") return 3; if (t == "adaptive") return 4; if (t == "custom") return 5; return -1;
}

// cmd.settings.setMode { mode: token } -> sets+persists the difficulty mode (SettingsUiSetMode). Returns
// {ok, mode}. bad_params if the token is invalid; not_ready if the executor isn't installed.
HandlerResult CmdSettingsSetMode(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setModeExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string tok = params ? params->memberString("mode") : std::string();
    int mode = ModeFromToken(tok);
    if (mode < 0) { r.ok = false; r.code = "bad_params"; r.message = "unknown mode: " + tok; return r; }
    bool ok = g_setModeExec(mode);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KS("mode", tok); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.settings.setTrim { trim: int } -> sets+persists the adaptive trim (SettingsUiSetTrim, clamps -5..+5).
HandlerResult CmdSettingsSetTrim(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setTrimExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    if (!params || !params->member("trim")) { r.ok = false; r.code = "bad_params"; r.message = "missing trim"; return r; }
    int trim = params->memberInt("trim", 0);
    bool ok = g_setTrimExec(trim);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KI("trim", trim); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.settings.setAggr { aggr: int } -> sets+persists AI aggression
// (SettingsUiSetAggr snaps to 0/40/60/80/100). Returns {ok, aggr}.
HandlerResult CmdSettingsSetAggr(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_setAggrExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    if (!params || !params->member("aggr")) { r.ok = false; r.code = "bad_params"; r.message = "missing aggr"; return r; }
    int aggr = params->memberInt("aggr", 0);
    bool ok = g_setAggrExec(aggr);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KI("aggr", aggr); w.EndObject();
    r.payload = w.Str();
    return r;
}

static IdReasonCmdExec g_marketAcceptExec; static IdCmdExec g_marketDeclineExec;
void SetMarketCmds(IdReasonCmdExec accept, IdCmdExec decline) { g_marketAcceptExec = std::move(accept); g_marketDeclineExec = std::move(decline); }

// a variant WITH A REASON — the exec returns "" (ok) or the rejection key, which travels as `code` in the
// payload ONLY on failure: {ok:false, <key>, code}. On success the envelope is identical to RunIdCmd's.
static HandlerResult RunIdReasonCmd(const IdReasonCmdExec& exec, const jsonm::Value* params, const char* key) {
    HandlerResult r;
    if (!exec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string id = params ? params->memberString(key) : std::string();
    if (id.empty()) { r.ok = false; r.code = "bad_params"; r.message = std::string("missing ") + key; return r; }
    std::string why = exec(id);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", why.empty()); w.KS(key, id);
    if (!why.empty()) w.KS("code", why);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// shared helper: pulls `key` out of params, calls the executor, replies {ok, <key>}. not_ready with no executor,
// bad_params if the id is missing.
static HandlerResult RunIdCmd(const IdCmdExec& exec, const jsonm::Value* params, const char* key) {
    HandlerResult r;
    if (!exec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string id = params ? params->memberString(key) : std::string();
    if (id.empty()) { r.ok = false; r.code = "bad_params"; r.message = std::string("missing ") + key; return r; }
    bool ok = exec(id);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KS(key, id); w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdMarketAccept(const jsonm::Value* params)   { return RunIdReasonCmd(g_marketAcceptExec, params, "offerId"); }   //
HandlerResult CmdMarketDecline(const jsonm::Value* params)  { return RunIdCmd(g_marketDeclineExec, params, "offerId"); }

// inbox redesign: mailbox commands.
static IdCmdExec g_inboxMarkReadExec; static InboxAnswerExec g_inboxAnswerExec;
void SetInboxCmds(IdCmdExec markRead, InboxAnswerExec answer) {
    g_inboxMarkReadExec = std::move(markRead); g_inboxAnswerExec = std::move(answer);
}
HandlerResult CmdInboxMarkRead(const jsonm::Value* params) { return RunIdCmd(g_inboxMarkReadExec, params, "id"); }
// 18/09 (Josh): «a mark-all-as-read button». A single command that marks all and saves once:
// doing it from the app with cmd.inbox.markRead in a loop would save the whole game once per mail.
static InboxMarkAllExec g_inboxMarkAllExec;
void SetInboxMarkAllCmd(InboxMarkAllExec markAll) { g_inboxMarkAllExec = std::move(markAll); }
HandlerResult CmdInboxMarkAllRead(const jsonm::Value*) {
    HandlerResult r;
    if (!g_inboxMarkAllExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    const int n = g_inboxMarkAllExec();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KI("marked", n); w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdInboxAnswer(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_inboxAnswerExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string id = params ? params->memberString("id") : std::string();
    if (id.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing id"; return r; }
    int choice = params ? params->memberInt("choice", -1) : -1;
    if (choice < 0) { r.ok = false; r.code = "bad_params"; r.message = "missing choice"; return r; }
    int rc = g_inboxAnswerExec(id, choice);
    if (rc != 0) {
        r.ok = false;
        r.code = (rc == 1) ? "not_found" : (rc == 2) ? "already_answered" : (rc == 3) ? "not_answerable" : "bad_choice";
        r.message = "inbox answer rejected";
        return r;
    }
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KS("id", id); w.KI("choice", choice); w.EndObject();
    r.payload = w.Str();
    return r;
}

// hub lifecycle. C1: the clock advance (cmd.time.advanceWeek) was retired along with the clock with stops;
// what remains here is terminating ALL contracts.
static ContractBreakExec g_contractBreakExec;
void SetLifecycleCmds(ContractBreakExec breakAll) { g_contractBreakExec = std::move(breakAll); }

// Month-first date label ("Mar 7"), the SAME format career::ProjectSeason uses for CalRound.date
// (career_calendar.cpp) -- there it computes it FOR ONE ROUND of a series; here it's needed for a single day of the
// clock (a (year, doy) without its own CalRound: today, the stop day, each crossed event), so the
// same MON array is duplicated locally (same idiom as DateLabel/DoyToDate/ProjectSeason: each formatting site defines
// its own). PURE, does not
// touch the engine.
static std::string JumpTargetDateLabel(int year, int doy) {
    if (doy < 1) return std::string();
    int m, d; career::dateutil::YmdFromYearDoy(year, doy, m, d);
    static const char* MON[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    char buf[16]; snprintf(buf, sizeof(buf), "%s %d", MON[m - 1], d);
    return std::string(buf);
}

static TimeInputsProvider   g_timeInputsProvider;
static AdvanceUntilStopExec g_advanceUntilStopExec;
static StopRulesExec        g_stopRulesExec;
// 0.7.0 review (finding 1): the last forecast with its arrival state, so cmd.time.advance adopts it
// instead of simulating again. It is cleared on advance (and the next forecast replaces it).
static career::AdvancePreviewKeep g_timePreviewKeep;
void SetTimeInputsProvider(TimeInputsProvider p) { g_timeInputsProvider = std::move(p); }
void SetTimeAdvanceCmds(AdvanceUntilStopExec advance, StopRulesExec rules) { g_advanceUntilStopExec = std::move(advance); g_stopRulesExec = std::move(rules); }

// C1: advance destination. seriesId/scope are resolved with the SAME selectors as always (career::
// NextEventAbsDay*, access filter included) and converted to untilAbsDay; an explicit untilAbsDay is
// respected AS IS, with no forward snap ("stop here" is that day, not the next event from it, which
// is what the retired jump did). No params -> 0 = until the first stop according to the rules. -1 = UNRESOLVABLE
// destination (can only come from the selectors): the query counts it as resolved:false and the cmd as
// 'unreachable'. An untilAbsDay <= today is NOT unresolvable: the engine doesn't go backwards and returns days=0 / DEK_UNTIL.
// A NON-EMPTY scope we don't recognize is bad_params, not "no selector": treating a typo as «advance until
// the first stop» would turn an app typo into a clock mutation. Detection lives HERE (a single
// place); the two handlers only translate the sentinel.
// And a NEGATIVE untilAbsDay is bad_params for the SAME reason as the unknown scope: there is no day before
// the engine epoch, so it can only come from a miscomputation in the app, and letting it through as 0 ("no
// cap") would turn that arithmetic error into a free clock advance. 0 is legitimate: it is the
// documented sentinel for "until the first stop" (R-T3-R4).
static constexpr int kTimeTargetBadScope = -2;   // < -1: checked BEFORE the unresolvable branch
static constexpr int kTimeTargetBadUntil = -3;
static const char* TimeTargetBadParams(int target) {   // nullptr = the destination is not a params error
    if (target == kTimeTargetBadScope) return "unknown scope";
    if (target == kTimeTargetBadUntil) return "untilAbsDay must be >= 0";
    return nullptr;
}
static int ResolveTimeTarget(const jsonm::Value* params, const TimeInputs& in) {
    const std::string seriesId = params ? params->memberString("seriesId") : std::string();
    if (!seriesId.empty()) return career::NextEventAbsDayForSeries(*in.calendar, seriesId, in.frontierPhase, in.gates, in.save);
    if (params && params->member("untilAbsDay")) { const int t = params->memberInt("untilAbsDay", 0); return t < 0 ? kTimeTargetBadUntil : t; }
    const std::string scope = params ? params->memberString("scope") : std::string();
    if (scope == "enrolled")  return career::NextEventAbsDayEnrolled(*in.calendar, in.frontierPhase, in.gates);
    if (scope == "available") return career::NextEventAbsDay(*in.calendar, in.frontierPhase, in.gates, in.save);
    if (!scope.empty())       return kTimeTargetBadScope;
    return 0;
}

// C1: a clock day, always with the SAME five keys { absDay, year, doy, date, weekday }. `has`
// distinguishes "no day" (unresolvable destination: year/doy at 0 and empty labels, never absent) from a
// real day, and is NOT inferred from absDay>0: January 1 of kStartYear IS abs_day 0 (the career::dateutil epoch
// coincides with the career start, kCareerStartDoy=1), so the first day of a new career would
// have been serialized with a blank date. The "no day" sentinel is the flag, not the value.
static void WriteDay(jsonw::Writer& w, int absDay, bool has) {
    int y = 0, d = 0;
    if (has) career::dateutil::YearDoyFromAbsDay(absDay, y, d);
    w.KI("absDay", has ? absDay : 0); w.KI("year", y); w.KI("doy", d);
    w.KS("date",    has ? JumpTargetDateLabel(y, d) : std::string());
    w.KS("weekday", has ? career::WeekdayLabel(y, d) : std::string());
}
// C1: the five stop rules, as they live in the save. They travel in ALL clock payloads so that
// the screen renders the switches without a second query.
static void WriteRules(jsonw::Writer& w, const career::StopRules& r) {
    w.Key("rules"); w.BeginObject();
    w.KB("offers", r.offers); w.KB("myRaces", r.myRaces); w.KB("anyRace", r.anyRace);
    w.KB("mail", r.mail); w.KB("relation", r.relation);
    w.EndObject();
}
// C1: the advance report (career::AdvanceReport) serialized. SAME payload for preview and for
// execution — only `executed` changes —, so the app renders a single screen for both. rep == nullptr =
// unresolvable destination: same keys, everything 0/""/[] (a contract that appears and disappears forces the app to
// defend against its absence, and that defense is where "undefined" leaks onto the screen).
// each event's `params` travels as a {k:v} object: in the engine it is a string "k=v\x1Fk=v" (DayEventParam) so
// as not to drag a map through the report, and here it is unpacked once, at the JSON edge.
static std::string WriteAdvanceReport(const career::AdvanceReport* rep, bool resolved, bool executed,
                                      int todayAbs, const career::StopRules& rules) {
    jsonw::Writer w; w.BeginObject();
    w.KB("ok", true); w.KB("resolved", resolved); w.KB("executed", executed);
    w.Key("today"); w.BeginObject(); WriteDay(w, todayAbs, true); w.EndObject();
    w.Key("stop");  w.BeginObject(); WriteDay(w, rep ? rep->stop_abs_day : 0, rep != nullptr);
    w.KS("reason", rep ? std::string(career::DayEventKindName(rep->stop_reason)) : std::string()); w.EndObject();
    w.KI("days", rep ? rep->days : 0);
    w.KB("crossedYear", rep ? rep->crossed_year : false);
    w.Key("crossed"); w.BeginArray();
    if (rep) for (const auto& e : rep->crossed) {
        w.BeginObject();
        WriteDay(w, e.abs_day, true);
        w.KS("kind", career::DayEventKindName(e.kind));
        w.KS("label", e.label_key);
        w.KS("seriesId", e.series_id);
        w.KB("stops", e.stops);
        w.Key("params"); w.BeginObject();
        for (size_t i = 0; i < e.params.size(); ) {
            size_t j = e.params.find('\x1F', i);
            if (j == std::string::npos) j = e.params.size();
            const std::string kv = e.params.substr(i, j - i);
            const size_t eq = kv.find('=');
            if (eq != std::string::npos) w.KS(kv.substr(0, eq).c_str(), kv.substr(eq + 1));
            i = j + 1;
        }
        w.EndObject();
        w.EndObject();
    }
    w.EndArray();
    w.Key("noShows"); w.BeginArray();
    if (rep) for (const auto& ns : rep->no_shows) {   // SAME keys as the retired query's noShows
        w.BeginObject();
        w.KS("seriesId", ns.series_id); w.KS("className", ns.class_name); w.KS("seriesName", ns.series_display);
        w.KI("round", ns.round); w.KS("trackName", ns.track_name); w.KS("date", ns.date);
        w.KS("weekday", ns.doy > 0 ? career::WeekdayLabel(ns.year, ns.doy) : std::string());
        w.KI("year", ns.year); w.KI("absDay", ns.abs_day);
        w.EndObject();
    }
    w.EndArray();
    WriteRules(w, rules);
    w.EndObject();
    return w.Str();
}

// query.time.preview { seriesId? } | { untilAbsDay? } | { scope? } | {} -> what would happen if the clock ran
// now: career::PreviewAdvance over COPIES of the live calendar/save (touches nothing), serialized. It is the
// SAME computation cmd.time.advance runs, so what the screen renders is what will happen.
HandlerResult QueryTimePreview(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_timeInputsProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    TimeInputs in = g_timeInputsProvider();
    if (!in.loaded || !in.calendar || !in.save) { r.ok = false; r.code = "not_ready"; r.message = "calendar not loaded"; return r; }
    const int today  = career::dateutil::AbsDayFromYearDoy(in.calendar->year, in.calendar->cursor_doy);
    const int target = ResolveTimeTarget(params, in);
    if (const char* bad = TimeTargetBadParams(target)) { r.ok = false; r.code = "bad_params"; r.message = bad; return r; }
    // 0.7.0 review (finding 1): resolveOnly = only «is there a destination?», without simulating. The Rollover CTA requested it
    // in full on mount to enable a button: seconds of game thread for a boolean.
    if (params && params->memberBool("resolveOnly", false)) {
        r.payload = WriteAdvanceReport(nullptr, target >= 0, false, today, in.rules);
        return r;
    }
    if (target < 0) { r.payload = WriteAdvanceReport(nullptr, false, false, today, in.rules); return r; }
    const bool cron = career::ClockProfOn();
    auto tp = std::chrono::steady_clock::now();
    career::AdvanceReport rep = career::PreviewAdvance(*in.calendar, *in.save, in.playerName, in.rules, target, &g_timePreviewKeep);
    // 18/09: abandoned because newer clock work was waiting behind it. Its figures are partial: it is
    // answered with its own error so nobody renders them, and the one that arrived behind is served right away.
    if (rep.aborted) { r.ok = false; r.code = "superseded"; r.message = "a newer time request replaced this preview"; return r; }
    if (cron) { career::ClockProf("  [query] PreviewAdvance %lld ms\n",
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tp).count());
        tp = std::chrono::steady_clock::now(); }
    r.payload = WriteAdvanceReport(&rep, true, false, today, in.rules);
    if (cron) career::ClockProf("  [query] WriteAdvanceReport %lld ms (%d bytes)\n",
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tp).count(),
        (int)r.payload.size());
    return r;
}

// cmd.time.advance: SAME params as the preview; runs the clock FOR REAL (career::AdvanceUntilStop via the
// injected executor, which also persists) and returns the SAME payload with executed:true. Unresolvable destination
// > 'unreachable' and NOTHING is executed (the app must not be able to advance toward a series it doesn't reach).
HandlerResult CmdTimeAdvance(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_timeInputsProvider || !g_advanceUntilStopExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    TimeInputs in = g_timeInputsProvider();
    if (!in.loaded || !in.calendar || !in.save) { r.ok = false; r.code = "not_ready"; r.message = "calendar not loaded"; return r; }
    const int today  = career::dateutil::AbsDayFromYearDoy(in.calendar->year, in.calendar->cursor_doy);
    const int target = ResolveTimeTarget(params, in);
    if (const char* bad = TimeTargetBadParams(target)) { r.ok = false; r.code = "bad_params"; r.message = bad; return r; }
    if (target < 0) { r.ok = false; r.code = "unreachable"; r.message = "no reachable destination"; return r; }
    career::AdvanceReport rep = g_advanceUntilStopExec(target, &g_timePreviewKeep);
    g_timePreviewKeep = career::AdvancePreviewKeep{};   // adopted or not, it no longer describes the current state
    r.payload = WriteAdvanceReport(&rep, true, true, today, in.rules);
    return r;
}

// cmd.time.rules { offers, myRaces, anyRace, mail, relation } -> persists the five stop rules and
// returns them. All FIVE are mandatory: the app always sends the full set, and accepting a missing one as
// "false" would turn an incomplete payload into a silent rules change.
HandlerResult CmdTimeRules(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_stopRulesExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    static const char* K[5] = { "offers", "myRaces", "anyRace", "mail", "relation" };
    for (int i = 0; i < 5; i++) {
        if (!params || !params->member(K[i])) { r.ok = false; r.code = "bad_params"; r.message = std::string("missing ") + K[i]; return r; }
    }
    career::StopRules rules;
    rules.offers   = params->memberBool("offers");
    rules.myRaces  = params->memberBool("myRaces");
    rules.anyRace  = params->memberBool("anyRace");
    rules.mail     = params->memberBool("mail");
    rules.relation = params->memberBool("relation");
    g_stopRulesExec(rules);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); WriteRules(w, rules); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.contract.break {} -> rescinds EVERY driver contract (CalUiBreakContract) + persists. Returns {ok}.
HandlerResult CmdContractBreak(const jsonm::Value* params) {
    (void)params;
    HandlerResult r;
    if (!g_contractBreakExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    g_contractBreakExec();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.contract.breakSeries { seriesId } -> rescinds the contract for THAT series (CalUiBreakContractSeries) +
// persists. Returns {ok}: false if the series had no active contract, which is what the screen needs to
// avoid announcing a rescission that never happened. The amount and REP penalty are applied by BreakContract, which
// is the SAME function the `breakFee` shown before you click comes from.
static ContractBreakSeriesExec g_contractBreakSeriesExec;
void SetContractCmds(ContractBreakSeriesExec breakSeries) { g_contractBreakSeriesExec = std::move(breakSeries); }
HandlerResult CmdContractBreakSeries(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_contractBreakSeriesExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    const std::string seriesId = params ? params->memberString("seriesId") : std::string();
    // an empty seriesId is NOT accepted: in BreakContract an empty string means "rescind EVERYTHING", so
    // letting it through would turn a screen bug into the loss of the player's entire career.
    if (seriesId.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing seriesId"; return r; }
    const bool ok = g_contractBreakSeriesExec(seriesId);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.identity.submit: creates the career (CareerIdentitySubmit seeds the seed/credits/calendar). Validates the gate
// IN THE HANDLER (same rule as the overlay's START button): first>=2, last>=2, sex in {male,female,other},
// nationality non-empty, year/month/day != 0. An "unusual" age does NOT block (it's only a warning). After {ok}, the app
// refetches query.identity + query.career_state/hub/calendar (the commit initializes the world).
static IdentitySubmitExec g_identitySubmitExec;
void SetIdentityCmds(IdentitySubmitExec submit) { g_identitySubmitExec = std::move(submit); }
HandlerResult CmdIdentitySubmit(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_identitySubmitExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string first = params ? params->memberString("first") : std::string();
    std::string last  = params ? params->memberString("last") : std::string();
    std::string sex   = params ? params->memberString("sex") : std::string();
    std::string nat   = params ? params->memberString("nationality") : std::string();
    int year  = params ? params->memberInt("year", 0) : 0;
    int month = params ? params->memberInt("month", 0) : 0;
    int day   = params ? params->memberInt("day", 0) : 0;
    if (first.size() < 2 || last.size() < 2) { r.ok = false; r.code = "bad_params"; r.message = "name/surname too short"; return r; }
    if (sex != "male" && sex != "female" && sex != "other") { r.ok = false; r.code = "bad_params"; r.message = "invalid sex"; return r; }
    if (nat.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing nationality"; return r; }
    if (year == 0 || month == 0 || day == 0) { r.ok = false; r.code = "bad_params"; r.message = "incomplete date of birth"; return r; }
    // hub (30/08): a world driver's name CANNOT be taken. Championship tables accumulate
    // BY NAME, so naming yourself the same as a rival merges you with him: a player named themself after
    // the leader and inherited his first-place finish. It's rejected here, right where the name is chosen,
    // instead of trying to undo the tangle afterward. Checked through the same identity resolver the engine uses everywhere.
    if (!career::DriverIdForName(first + " " + last).empty()) {
        r.ok = false; r.code = "name_taken";
        r.message = "that name belongs to a driver on the grid — pick another one";
        return r;
    }
    bool ok = g_identitySubmitExec(first, last, sex, nat, year, month, day);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.EndObject();
    r.payload = w.Str();
    return r;
}

// negotiation redesign: THREAD commands — send (open/counter-offer), acceptTable/sign/walk (by negId).
// Replace the instant-sign cmd.negotiate.accept (retired: see RegisterCareerQueries). send extracts
// {offerId, terms:{demands[8],salaryPct,sponsorTierRank,sponsorCardId}} just like the old CmdNegotiateAccept
// and calls the executor, which returns the negId ("" = failure, offer doesn't exist, or the margin rejects it). The other three
// only carry {negId} — they share runNegoIdCmd (same pattern as RunIdCmd for market/cards).
static NegoSendExec g_negoSendExec;
static NegoIdExec g_negoAcceptTableExec, g_negoSignExec, g_negoWalkExec;
void SetNegoThreadCmds(NegoSendExec send, NegoIdExec acceptTable, NegoIdExec sign, NegoIdExec walk) {
    g_negoSendExec = std::move(send); g_negoAcceptTableExec = std::move(acceptTable);
    g_negoSignExec = std::move(sign); g_negoWalkExec = std::move(walk);
}
HandlerResult CmdNegoSend(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_negoSendExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string offerId = params ? params->memberString("offerId") : std::string();
    if (offerId.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing offerId"; return r; }
    NegotiateTermsInput terms;   // defaults: every demand false, salaryPct 50, sponsorTierRank -1
    const jsonm::Value* t = params ? params->member("terms") : nullptr;
    if (t) {
        const jsonm::Value* dm = t->member("demands");
        if (dm && dm->isArray())
            for (size_t i = 0; i < dm->arr.size() && i < 8; ++i)
                terms.demands[i] = dm->arr[i] ? dm->arr[i]->asBool() : false;
        terms.salaryPct = t->memberInt("salaryPct", 50);
        terms.sponsorTierRank = t->memberInt("sponsorTierRank", -1);
        terms.sponsorCardId = t->memberString("sponsorCardId");   // "" if none arrives / "none offered"
    }
    std::string negId = g_negoSendExec(offerId, terms);
    jsonw::Writer w; w.BeginObject();
    w.KB("ok", !negId.empty()); w.KS("offerId", offerId); w.KS("negId", negId);
    w.EndObject();
    r.payload = w.Str();
    return r;
}
// shared helper for the 3 per-negId commands (mirrors RunIdCmd, but fixed on "negId").
// the execs now return the failure REASON ("" = ok), which travels here as `code` — mirroring
// RunIdReasonCmd (market.accept), so the app can show the specific rejection instead of a mute banner.
static HandlerResult runNegoIdCmd(const NegoIdExec& exec, const jsonm::Value* params) {
    HandlerResult r;
    if (!exec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string id = params ? params->memberString("negId") : std::string();
    if (id.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing negId"; return r; }
    std::string why = exec(id);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", why.empty()); w.KS("negId", id);
    if (!why.empty()) w.KS("code", why);
    w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdNegoAcceptTable(const jsonm::Value* p) { return runNegoIdCmd(g_negoAcceptTableExec, p); }
HandlerResult CmdNegoSign(const jsonm::Value* p)        { return runNegoIdCmd(g_negoSignExec, p); }
HandlerResult CmdNegoWalk(const jsonm::Value* p)        { return runNegoIdCmd(g_negoWalkExec, p); }

// cmd.season.ackBonus: consumes the season-finished flag (the screen's BACK TO SERIES button does NOT
// clear it — a bug; it only clears when starting/abandoning a season). The overlay's executor calls
// career::CalUiClearSeasonFinished(); after the cmd the app refetches query.season (bonusShown goes to false).
static SeasonAckBonusExec g_seasonAckBonusExec;
void SetSeasonCmds(SeasonAckBonusExec ack) { g_seasonAckBonusExec = std::move(ack); }
HandlerResult CmdSeasonAckBonus(const jsonm::Value* params) {
    (void)params;
    HandlerResult r;
    if (!g_seasonAckBonusExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    g_seasonAckBonusExec();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.calendar.launch {seriesId, round}: arms the race (CareerArmRaceLaunch via the executor) and returns {ok, launched}.
// launched=false = it didn't arm (insufficient balance / nonexistent slot); the app stays on the calendar. With
// launched=true the app navigates to Prerace (reading query.prerace off the armed state).
static CalLaunchExec    g_calLaunchExec;
static CalCalibrateExec g_calCalibrateExec;
static CalSkipCalibExec g_calSkipCalibExec;   //
void SetCalendarCmds(CalLaunchExec launch, CalCalibrateExec calibrate, CalSkipCalibExec skipCalib) {
    g_calLaunchExec = std::move(launch); g_calCalibrateExec = std::move(calibrate); g_calSkipCalibExec = std::move(skipCalib);
}
HandlerResult CmdCalendarLaunch(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_calLaunchExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string seriesId = params ? params->memberString("seriesId") : std::string();
    if (seriesId.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing seriesId"; return r; }
    int round = params ? params->memberInt("round", 0) : 0;
    bool launched = g_calLaunchExec(seriesId, round);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KB("launched", launched); w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdCalendarLaunchCalibration(const jsonm::Value* params) {
    (void)params;
    HandlerResult r;
    if (!g_calCalibrateExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    bool launched = g_calCalibrateExec();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KB("launched", launched); w.EndObject();
    r.payload = w.Str();
    return r;
}
// cmd.calendar.skipCalibration {}: skips the calibration race (calibrated=true + persist).
// The player is left in adaptive difficulty with the default seed; they can recalibrate from Settings.
HandlerResult CmdCalendarSkipCalibration(const jsonm::Value* params) {
    (void)params;
    HandlerResult r;
    if (!g_calSkipCalibExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    bool ok = g_calSkipCalibExec();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.event.arm {eventId, carClassId}: arms a special event's EXHIBITION race (CareerArmEvent via the
// executor) and returns {ok, armed}. armed=false = event/class/track couldn't be resolved; the app stays put.
// With armed=true the app navigates to Prerace (reading query.prerace off the armed state).
static EventArmExec g_eventArmExec;
void SetEventCmds(EventArmExec arm) { g_eventArmExec = std::move(arm); }
HandlerResult CmdEventArm(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_eventArmExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string eventId = params ? params->memberString("eventId") : std::string();
    std::string classId = params ? params->memberString("carClassId") : std::string();
    if (eventId.empty() || classId.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing eventId/carClassId"; return r; }
    bool armed = g_eventArmExec(eventId, classId);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KB("armed", armed); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.debug.setState { preset: 0..3 }: applies a DBGSAVE preset (CareerDebugSetSave via the executor) — OVERWRITES
// g_save. Debug-only (the app exposes it behind the debug menu key). preset outside 0..3 -> bad_params.
static DebugSetStateExec g_debugSetStateExec;
static DebugSimRaceExec  g_debugSimRaceExec;   // SIMULATION-mode toggle (debug menu)
static DebugSimWorldExec g_debugSimWorldExec;   // simulate the world up to a given date (debug menu)
void SetDebugCmds(DebugSetStateExec setState, DebugSimRaceExec simRace, DebugSimWorldExec simWorld) {
    g_debugSetStateExec = std::move(setState); g_debugSimRaceExec = std::move(simRace);
    g_debugSimWorldExec = std::move(simWorld);
}

// cmd.debug.simWorld { toYear, toDoy, apply? }. See career_api_queries.h. The work is done by the
// executor (it's the one holding g_save/g_calendar) and it returns the payload already assembled; only the
// parameters are validated here. apply absent = false: peeking at the future can't cost you the game by accident.
HandlerResult CmdDebugSimWorld(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_debugSimWorldExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    if (!params || !params->member("toYear") || !params->member("toDoy")) {
        r.ok = false; r.code = "bad_params"; r.message = "missing toYear/toDoy"; return r;
    }
    const int toYear = params->memberInt("toYear", 0);
    const int toDoy  = params->memberInt("toDoy", 0);
    if (toYear < 1900 || toYear > 3000 || toDoy < 1 || toDoy > 366) {
        r.ok = false; r.code = "bad_params"; r.message = "date out of range"; return r;
    }
    // `perf` = the player's level ON TRACK (SimPlayerPerf). Out of range -> the midpack, which is
    // the neutral assumption; a malformed parameter can't invent a dominant player.
    int perf = params->memberInt("perf", 2);
    if (perf < 0 || perf > 3) perf = 2;
    const std::string payload = g_debugSimWorldExec(toYear, toDoy, params->memberBool("apply", false), perf);
    if (payload.empty()) { r.ok = false; r.code = "no_career"; r.message = "no career loaded"; return r; }
    r.payload = payload;
    return r;
}
HandlerResult CmdDebugSetState(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_debugSetStateExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    if (!params || !params->member("preset")) { r.ok = false; r.code = "bad_params"; r.message = "missing preset"; return r; }
    int preset = params->memberInt("preset", -1);
    if (preset < 0 || preset > 3) { r.ok = false; r.code = "bad_params"; r.message = "preset out of range"; return r; }
    g_debugSetStateExec(preset);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KI("preset", preset); w.EndObject();
    r.payload = w.Str();
    return r;
}
// cmd.debug.simRace { enabled }: turns SIMULATION mode on/off. The current state is
// mirrored by query.career_state.flags.simRace (the debug menu draws the toggle from there after the refetch).
HandlerResult CmdDebugSimRace(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_debugSimRaceExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    if (!params || !params->member("enabled")) { r.ok = false; r.code = "bad_params"; r.message = "missing enabled"; return r; }
    bool enabled = params->memberBool("enabled");
    g_debugSimRaceExec(enabled ? 1 : 0);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KB("enabled", enabled); w.EndObject();
    r.payload = w.Str();
    return r;
}

// Experimental Low RAM Mode — cmd.debug.lowRam { enabled }: turns the mode on/off. The overlay stops sweeping
// the full heap (~10GB) in the per-frame handoff path. The app re-sends this on connect (it isn't persisted here).
static DebugLowRamExec g_debugLowRamExec;
void SetDebugLowRamExec(DebugLowRamExec exec) { g_debugLowRamExec = std::move(exec); }
HandlerResult CmdDebugLowRam(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_debugLowRamExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    if (!params || !params->member("enabled")) { r.ok = false; r.code = "bad_params"; r.message = "missing enabled"; return r; }
    bool enabled = params->memberBool("enabled");
    g_debugLowRamExec(enabled ? 1 : 0);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KB("enabled", enabled); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.debug.contentLog { enabled }: debug menu switch. Lifts the Steam ownership veto
// (career::SetOwnershipVetoBypass). Not persisted in the engine: the app resends it on connect.
static HandlerResult CmdDebugContentLog(const jsonm::Value* params) {
    HandlerResult r;
    if (!params || !params->member("enabled")) { r.ok = false; r.code = "bad_params"; r.message = "missing enabled"; return r; }
    bool enabled = params->memberBool("enabled");
    if (enabled != career::OwnershipVetoBypass()) career::UserLog("content", "content log: verbose=%d", enabled ? 1 : 0);
    career::SetOwnershipVetoBypass(enabled);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KB("enabled", enabled); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.app.closing: a best-effort notice that the app is closing. Visually quiesces the overlay; the
// WS server doesn't stop (warm-attach). With no executor wired up -> still ok (nothing to quiesce).
static AppClosingExec g_appClosingExec;
void SetAppClosingExec(AppClosingExec exec) { g_appClosingExec = std::move(exec); }
HandlerResult CmdAppClosing(const jsonm::Value*) {
    HandlerResult r;
    if (g_appClosingExec) g_appClosingExec();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.EndObject();
    r.payload = w.Str();
    return r;
}

// APP language -> the engine's TR(). With the legacy UI turned off, nobody was calling SetLang, and the
// loading cover + the READY notice always came out in English. The app pushes it on connect and on language
// changes (same source of truth: i18next). Runs on the game thread (Pump), like TR() does.
HandlerResult CmdAppLang(const jsonm::Value* params) {
    HandlerResult r;
    // Localization: the app sends `code` ("de", "pt-BR"...). `lang` (0/1) is still accepted so
    // an old app against a new DLL doesn't go mute.
    std::string code = params ? params->memberString("code") : std::string();
    if (!code.empty()) SetLangCode(code.c_str());
    else               SetLang(params && (int)params->memberInt("lang", 0) == 1 ? 1 : 0);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.EndObject();
    r.payload = w.Str();
    return r;
}

// breadcrumb from the APP for the SUPPORT log (screen navigation, events the engine can't see).
// The text IS the line; the central dispatcher (Pump) excludes cmd.log.note from its own trace so it isn't duplicated.
HandlerResult CmdLogNote(const jsonm::Value* params) {
    HandlerResult r;
    std::string text = params ? params->memberString("text") : std::string();
    if (text.size() > 300) text.resize(300);   // Jul 22: 300 (previously 200) — long paths + suffix (ok/MISSING)
    for (char& c : text) if (c == '\r' || c == '\n') c = ' ';
    if (!text.empty()) career::UserLog("app", "%s", text.c_str());
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.EndObject();
    r.payload = w.Str();
    return r;
}

// (multi-profile): query.profiles + cmd.profile.create/select/delete. Thin serialization; the real logic
// (paths, migration, state reload, sandbox preset) lives in the overlay's executors.
static ProfilesProvider  g_profilesProvider;
static ProfileCreateExec g_profileCreateExec;
static ProfileSelectExec g_profileSelectExec;
static ProfileDeleteExec g_profileDeleteExec;
void SetProfilesProvider(ProfilesProvider p) { g_profilesProvider = std::move(p); }
void SetProfileCmds(ProfileCreateExec create, ProfileSelectExec select, ProfileDeleteExec remove) {
    g_profileCreateExec = std::move(create); g_profileSelectExec = std::move(select); g_profileDeleteExec = std::move(remove);
}
HandlerResult QueryProfiles(const jsonm::Value*) {
    HandlerResult r;
    if (!g_profilesProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    ProfilesView v = g_profilesProvider();
    jsonw::Writer w; w.BeginObject();
    w.KS("active", v.active);
    w.Key("profiles"); w.BeginArray();
    for (const auto& p : v.profiles) {
        w.BeginObject(); w.KS("slug", p.slug); w.KS("name", p.name); w.KS("mode", p.mode); w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdProfileCreate(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_profileCreateExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string name = params ? params->memberString("name") : std::string();
    std::string mode = params ? params->memberString("mode") : std::string();
    if (name.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing name"; return r; }
    if (mode.empty()) mode = "career";
    if (mode != "career" && mode != "sandbox") { r.ok = false; r.code = "bad_params"; r.message = "mode must be career|sandbox"; return r; }
    std::string slug, err;
    if (!g_profileCreateExec(name, mode, slug, err)) { r.ok = false; r.code = "rejected"; r.message = err; return r; }
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KS("slug", slug); w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdProfileSelect(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_profileSelectExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string slug = params ? params->memberString("slug") : std::string();
    if (slug.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing slug"; return r; }
    std::string err;
    if (!g_profileSelectExec(slug, err)) { r.ok = false; r.code = "rejected"; r.message = err; return r; }
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdProfileDelete(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_profileDeleteExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string slug = params ? params->memberString("slug") : std::string();
    if (slug.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing slug"; return r; }
    std::string err;
    if (!g_profileDeleteExec(slug, err)) { r.ok = false; r.code = "rejected"; r.message = err; return r; }
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.prerace {carId?}: PRERACE (mirrors DrawVehicle). The provider (overlay) copies career::PreraceUiCompute
// > capi::PreraceView; here we just serialize it raw (ImU32->CssVar; cars[]+liveryNN[] as arrays; cost as
// an object). clsEmpty=true -> loaded but with no detail (the app shows "pick a race"). loaded=false -> not_ready.
static PreraceViewProvider g_preraceProvider;
void SetPreraceViewProvider(PreraceViewProvider p) { g_preraceProvider = std::move(p); }
HandlerResult QueryPrerace(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_preraceProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string carId = params ? params->memberString("carId") : std::string();
    PreraceView v = g_preraceProvider(carId);
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }
    jsonw::Writer w; w.BeginObject();
    w.KB("clsEmpty", v.clsEmpty);
    if (!v.clsEmpty) {
        w.KS("raceName", v.raceName); w.KS("className", v.className); w.KS("discColor", CssVar(v.discColor));
        w.KS("trackName", v.trackName); w.KI("trackHash", v.trackHash);
        w.KI("laps", v.laps); w.KI("durationMin", v.durationMin); w.KI("grid", v.grid); w.KI("reward", v.reward);
        w.KI("eventPaid", v.eventPaid);   // 19/09: what was already paid on the armed event (0 = nothing / not an event)
        w.KS("reasonKey", v.reasonKey);
        w.KB("lockCar", v.lockCar); w.KB("lockSkin", v.lockSkin); w.KB("lockDuration", v.lockDuration);
        w.KI("presetVehHash", v.presetVehHash); w.KI("presetLivery", v.presetLivery); w.KI("presetDurPct", v.presetDurPct);
        w.KI("divorceRep", v.divorceRep); w.KI("divorceCr", v.divorceCr);   // v2: a 'divorce' warning
        w.KS("ratingTierLabel", v.ratingTierLabel); w.KS("ratingTierColor", CssVar(v.ratingTierColor));
        w.KI("credits", v.credits); w.KB("visual", v.visual);
        w.KS("seriesId", v.seriesId);
        { const career::SeriesDef* _sd = career::SeriesById(v.seriesId);
          w.KB("isMemory", _sd && career::IsEraMemorySeries(*_sd)); }
        w.KB("freeEntry", v.freeEntry); w.KI("raceLaps", v.raceLaps);
        // wear/consumption that will apply per % of duration (empty = old engine build -> the app renders nothing)
        w.KB("wearScaleTyre", v.wearScaleTyre); w.KB("wearScaleFuel", v.wearScaleFuel);
        w.Key("wearByPct"); w.BeginArray();
        for (const auto& o : v.wearByPct) { w.BeginObject(); w.KI("pct", o.pct); w.KI("tyre", o.tyre); w.KI("fuel", o.fuel); w.EndObject(); }
        w.EndArray();
        w.Key("cars"); w.BeginArray();
        for (const auto& c : v.cars) {
            w.BeginObject();
            w.KS("id", c.id); w.KS("name", c.name); w.KS("classShort", c.classShort);
            w.KI("year", c.year); w.KI("pwr", c.pwr); w.KI("grp", c.grp); w.KI("bal", c.bal);
            w.KI("price", c.price); w.KI("rentPct", c.rentPct); w.KI("vehHash", c.vehHash);
            w.KB("factory", c.factory); w.KB("owned", c.owned); w.KS("discColor", CssVar(c.discColor));
            w.KS("slug", c.slug);   // the app builds <data>/vehimages/.../<slug>_livery_NN.png
            w.EndObject();
        }
        w.EndArray();
        w.KS("selCarId", v.selCarId); w.KI("selVehHash", v.selVehHash);
        w.KB("selIsOwned", v.selIsOwned); w.KB("selFactory", v.selFactory);
        w.Key("liveryNN"); w.BeginArray(); for (int nn : v.liveryNN) w.I(nn); w.EndArray();
        w.KI("liveryCount", v.liveryCount); w.KI("ownedLiveryOrdinal", v.ownedLiveryOrdinal);
        w.Key("ownedLiveries"); w.BeginArray(); for (int o : v.ownedLiveries) w.I(o); w.EndArray();   // Memories 1:1: livery ordinals owned for the selected car
        w.Key("cost"); w.BeginObject();
        w.KI("tyres", v.costTyres); w.KI("fuel", v.costFuel); w.KI("fluids", v.costFluids);
        w.KI("insurance", v.costInsurance); w.KI("total", v.costTotal);
        w.EndObject();
        w.KI("entryFee", v.entryFee); w.KI("inscDue", v.inscDue); w.KI("travel", v.travel); w.KI("coverAmt", v.coverAmt);
        w.KI("rentCost", v.rentCost);
        w.KI("outlayRent", v.outlayRent); w.KI("outlayOwn", v.outlayOwn); w.KI("totalRent", v.totalRent); w.KI("totalOwn", v.totalOwn);
        w.KI("factorySalary", v.factorySalary);
        w.KB("canBuy", v.canBuy); w.KB("canRent", v.canRent); w.KB("canRaceOwn", v.canRaceOwn); w.KB("seatOk", v.seatOk);
        w.KI("launchLaps", v.launchLaps); w.KI("launchDurationMin", v.launchDurationMin);
        // real specs (VehicleDetails). Empty strings -> the app falls back to the bar charts.
        w.Key("specs"); w.BeginObject();
        w.KS("drivetrain", v.specDrivetrain); w.KS("enginePos", v.specEnginePos); w.KS("engineType", v.specEngineType);
        w.KS("topSpeed", v.specTopSpeed); w.KS("horsepower", v.specHorsepower); w.KS("weight", v.specWeight); w.KS("torque", v.specTorque);
        w.EndObject();
    }
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.prerace.buy {carId, livery?} / cmd.prerace.launch {carId, action, livery?, durationPct?}: MUTATE the prerace
// (purchase / handoff to Custom Race) via the overlay executors (career::PreraceBuy / PreraceLaunch). Thin handler:
// validates carId (bad_params), calls the executor, replies. action "rent" -> 1, anything else -> 0 (own, default).
static PreraceBuyExec    g_preraceBuyExec;
static PreraceLaunchExec g_preraceLaunchExec;
void SetPreraceCmds(PreraceBuyExec buy, PreraceLaunchExec launch) { g_preraceBuyExec = std::move(buy); g_preraceLaunchExec = std::move(launch); }

// cmd.memories.ackgift {} -> marks the 3rd-race gift (F1'67) as seen; no args, safe no-op executor.
static MemoriesAckGiftExec g_memAckGiftExec;
void SetMemoriesAckGiftExec(MemoriesAckGiftExec e) { g_memAckGiftExec = std::move(e); }
static HandlerResult CmdMemoriesAckGift(const jsonm::Value*) {
    HandlerResult r; if (g_memAckGiftExec) g_memAckGiftExec();
    r.payload = "{\"ok\":true}"; return r;
}
HandlerResult CmdPreraceBuy(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_preraceBuyExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string carId = params ? params->memberString("carId") : std::string();
    if (carId.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing carId"; return r; }
    int livery = params ? params->memberInt("livery", -1) : -1;
    bool ok = g_preraceBuyExec(carId, livery);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KS("carId", carId); w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdPreraceLaunch(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_preraceLaunchExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string carId = params ? params->memberString("carId") : std::string();
    if (carId.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing carId"; return r; }
    std::string action = params ? params->memberString("action") : std::string();
    int act = (action == "rent") ? 1 : 0;   // default own (RACE WITH THIS CAR / factory / owned)
    int livery = params ? params->memberInt("livery", -1) : -1;
    int durationPct = params ? params->memberInt("durationPct", 100) : 100;
    bool launched = g_preraceLaunchExec(carId, act, livery, durationPct);
    // VR: 'launched' is the REAL result of career::PreraceLaunch (not a "the
    // request was accepted") -- false when internal validation rejects it (seatOk/canBuy/canRent, etc., with
    // no race actually starting). The VR panel is only notified when a race REALLY launches; otherwise,
    // the user stays in the menu and the panel should stay where it was (automatic, based on context).
    if (launched) vrmirror::NotifyRaceLaunched();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KB("launched", launched); w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.tryout: TRIAL (mirrors DrawTryout). The provider (overlay) assembles it from TryoutUiActive() + the real
// verdict; the handler serializes raw (ImU32->CssVar; stakes[] as an array). loaded=false -> not_ready.
static TryoutViewProvider g_tryoutProvider;
void SetTryoutViewProvider(TryoutViewProvider p) { g_tryoutProvider = std::move(p); }
HandlerResult QueryTryout(const jsonm::Value* params) {
    (void)params;
    HandlerResult r;
    if (!g_tryoutProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    TryoutView v = g_tryoutProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }
    jsonw::Writer w; w.BeginObject();
    w.KB("armed", v.armed);
    w.KS("clsId", v.clsId); w.KS("className", v.className); w.KS("discColor", CssVar(v.discColor));
    w.KS("trackName", v.trackName); w.KI("trackHash", v.trackHash);
    w.KS("team", v.team); w.KI("topN", v.topN);
    w.KS("targetEn", v.targetEn); w.KS("targetEs", v.targetEs);
    w.KS("standardEn", v.standardEn); w.KS("standardEs", v.standardEs);
    w.Key("stakes"); w.BeginArray();
    for (const auto& s : v.stakes) {
        w.BeginObject();
        w.KS("key", s.key); w.KS("en", s.en); w.KS("es", s.es); w.KS("color", CssVar(s.color));
        w.EndObject();
    }
    w.EndArray();
    w.KB("hasResult", v.hasResult); w.KB("pass", v.pass);
    w.KI("pos", v.pos); w.KI("field", v.field); w.KI("resultTopN", v.resultTopN);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.rollover: END OF SEASON (mirrors DrawRollover). The provider copies RolloverUiPreview() + renewOfferId;
// the handler serializes raw (marketMoves.color ImU32->CssVar; renewState int->token). loaded=false -> not_ready.
static RolloverViewProvider g_rolloverProvider;
void SetRolloverViewProvider(RolloverViewProvider p) { g_rolloverProvider = std::move(p); }
static const char* RolloverStateToken(int s) {
    switch (s) { case 0: return "better"; case 1: return "same"; case 2: return "worse"; default: return "none"; }
}
HandlerResult QueryRollover(const jsonm::Value* params) {
    (void)params;
    HandlerResult r;
    if (!g_rolloverProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    RolloverView v = g_rolloverProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "career save not loaded"; return r; }
    jsonw::Writer w; w.BeginObject();
    w.KI("nextYear", v.nextYear);
    // 17/09: the winter total (the screen shows eight names; the news is «a hundred and something»)
    w.KI("retiresTotal", v.retiresTotal);
    w.KI("promosTotal", v.promosTotal);
    w.KI("reportYear", v.reportYear);
    w.Key("retirements"); w.BeginArray();
    for (const auto& d : v.retirements) {
        w.BeginObject();
        w.KS("name", d.name); w.KS("team", d.team); w.KI("age", d.age);
        w.KS("reasonEn", d.reasonEn); w.KS("reasonEs", d.reasonEs);
        w.EndObject();
    }
    w.EndArray();
    w.Key("promotions"); w.BeginArray();
    for (const auto& d : v.promotions) {
        w.BeginObject();
        w.KS("name", d.name); w.KS("fromEn", d.fromEn); w.KS("fromEs", d.fromEs);
        w.KS("toEn", d.toEn); w.KS("toEs", d.toEs);
        w.EndObject();
    }
    w.EndArray();
    w.Key("marketMoves"); w.BeginArray();
    for (const auto& d : v.marketMoves) {
        w.BeginObject();
        w.KS("driver", d.driver); w.KS("from", d.from); w.KS("to", d.to); w.KS("color", CssVar(d.color));
        w.EndObject();
    }
    w.EndArray();
    w.KI("movesTotal", v.movesTotal);
    w.KS("yourTeam", v.yourTeam); w.KS("yourClass", v.yourClass);
    w.KB("hasRenewal", v.hasRenewal); w.KS("renewState", RolloverStateToken(v.renewState));
    w.KI("renewPerRace", v.renewPerRace); w.KS("renewSeries", v.renewSeries); w.KS("renewOfferId", v.renewOfferId);
    w.KI("insurancePremium", v.insurancePremium); w.KN("cleanliness", v.cleanliness);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.tryout.arm/start/clear: MUTATE the trial (arm from an offer / launch the real race / consume the verdict).
static TryoutArmExec   g_tryoutArmExec;
static TryoutStartExec g_tryoutStartExec;
static TryoutClearExec g_tryoutClearExec;
void SetTryoutCmds(TryoutArmExec arm, TryoutStartExec start, TryoutClearExec clear) {
    g_tryoutArmExec = std::move(arm); g_tryoutStartExec = std::move(start); g_tryoutClearExec = std::move(clear);
}
HandlerResult CmdTryoutArm(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_tryoutArmExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string offerId = params ? params->memberString("offerId") : std::string();
    if (offerId.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing offerId"; return r; }
    bool armed = g_tryoutArmExec(offerId);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KB("armed", armed); w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdTryoutStart(const jsonm::Value* params) {
    (void)params;
    HandlerResult r;
    if (!g_tryoutStartExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    bool launched = g_tryoutStartExec();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KB("launched", launched); w.EndObject();
    r.payload = w.Str();
    return r;
}
HandlerResult CmdTryoutClear(const jsonm::Value* params) {
    (void)params;
    HandlerResult r;
    if (!g_tryoutClearExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    g_tryoutClearExec();
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.rollover.confirm: commits the contract decision. Token "renew"|"newgoals"|"decline" -> 0/1/2 (bad_params).
static RolloverConfirmExec g_rolloverConfirmExec;
void SetRolloverCmds(RolloverConfirmExec confirm) { g_rolloverConfirmExec = std::move(confirm); }
HandlerResult CmdRolloverConfirm(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_rolloverConfirmExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string choice = params ? params->memberString("choice") : std::string();
    int c;
    if      (choice == "renew")    c = 0;
    else if (choice == "newgoals") c = 1;
    else if (choice == "decline")  c = 2;
    else { r.ok = false; r.code = "bad_params"; r.message = "unknown choice"; return r; }
    bool ok = g_rolloverConfirmExec(c);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KS("choice", choice); w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.game_state: COARSE game state (from shared memory). Provider = ShmRead (overlay). No provider -> gameState=0
// (not in menu), so the app's polling degrades cleanly before shared memory is available.
static GameStateProvider g_gameStateProvider;
void SetGameStateProvider(GameStateProvider p) { g_gameStateProvider = std::move(p); }
HandlerResult QueryGameState(const jsonm::Value* params) {
    HandlerResult r;
    GameStateView v = g_gameStateProvider ? g_gameStateProvider() : GameStateView{};
    jsonw::Writer w; w.BeginObject();
    w.KI("gameState", v.gameState);
    w.KI("sessionState", v.sessionState);
    w.KB("atMenu", v.gameState == 1);   // GAME_FRONT_END (menu) — safe to overlay / to fire commands
    w.KB("shmMapped", v.shmMapped);      // is shared memory active? false -> warn (races can't be settled)
    w.KI("launchPhase", v.launchPhase);        // VR: 0 nothing · 1 configuring · 2 configured
    w.KI("cfgFailN", v.cfgFailN);              // values that did NOT land (0 = all good)
    w.KS("cfgFailGroups", v.cfgFailGroups);    // affected groups, already translated by the engine
    // is the in-headset panel alive (did the engine's VR path come up)? The app reads this to decide which of the two headset
    // panels is in charge: if this one renders, its OpenVR companion steps aside. It can't be inferred from the launch mode
    // (SteamVR via Link loads LibOVR; SteamVR via Steam Link doesn't), so it's asked of whoever actually knows.
    w.KB("vrPanelInstalled", vrmirror::IsInstalled());
    // IS THE GRID STALE? true = seat assignments changed since the grid XML files were
    // grid data, so what runs on track is NOT what the paddock says.
    //
    // This has to be said here and not on the signings screen, because AMS2 reads CustomAIDrivers at STARTUP and
    // never hot-reloads it, and because the app lives INSIDE the game: while you're playing, AMS2 is always
    // running, so a new signing doesn't hit the tarmac until a restart. Until now, the rollover screen
    // warned ALWAYS, unconditionally, and warning always is the same as not warning at all.
    //
    // It's a QUESTION, not a saved flag: it compares the world's current fingerprint against the one sealed
    // at write time. A boolean would need to be remembered and cleared by hand, and it drifts out of sync at the first
    // code path that skips it.
    //
    // 22/09 VR STUTTER: and that's why it comes out EXPENSIVE. The fingerprint walks all 118 series and recomposes each one's
    // distribution (SeatsForClass), on the game thread. Seven places in the app poll this query and only ONE reads
    // the field (AppShell, every 30 s and out of race); the VR panel polls (800 and 1000 ms, whole race)
    // paid for it without looking at it: the `api=12/23` every ~0.8 s of the [slowtick]. Now it is only computed if
    // whoever asks requests it with {"gridsStale":true}; without it, the field doesn't travel (undefined = don't warn).
    if (params && params->memberBool("gridsStale", false))
    { CareerStateView sv; HandlerResult ignora;
      if (ResolveView(sv, ignora) && sv.save)
          w.KB("gridsStale", career::GridsAreStale(*sv.save)); }
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.vr.panel: see the contract in the header. With no VR panel mounted (flat-screen game, LibOVR not loaded), the
// vrmirror setters just write to inert globals -> the command still replies ok, and the app doesn't need to
// know whether there's a headset: asking to "show the panel" with no panel present correctly does nothing.
HandlerResult CmdVrPanel(const jsonm::Value* params) {
    HandlerResult r;
    const std::string mode = params ? params->memberString("mode") : std::string();
    int m;
    if      (mode == "show") m =  1;
    else if (mode == "hide") m =  0;
    else if (mode == "auto") m = -1;
    else { r.ok = false; r.code = "bad_params"; r.message = "mode must be auto|show|hide"; return r; }
    const bool force = params ? params->memberBool("force", false) : false;
    vrmirror::SetPanelMode(m, force);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KS("mode", mode); w.KB("force", force); w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.race_config: the config of the LAST launch (a debug tool; rendered by SIMULATION-mode
// coverage). RAW serialization: the engine's sentinels (<0, or 0 in fields meaning "0 = don't force")
// travel as-is, and the app translates them to "Inherited"/"Auto" — translating them here would hide exactly what
// this is meant to audit (which slot gets written, and with what value). No provider or no launch -> not_ready.
static RaceConfigProvider g_raceConfigProvider;
void SetRaceConfigProvider(RaceConfigProvider p) { g_raceConfigProvider = std::move(p); }
// Body of the launch config (WITHOUT BeginObject/EndObject: the caller supplies those). Extracted so the
// telemetry payload embeds the SAME config query.raceConfig serves, without a second
// serializer that could drift out of sync whenever a field gets added.
static void WriteRaceConfigBody(jsonw::Writer& w, const RaceConfigView& v) {
    w.KB("sim", v.sim);
    w.KS("raceId", v.raceId);
    w.KS("seriesName", v.seriesName);
    w.KS("trackName", v.trackName);
    w.KI("vehHash", v.vehHash);
    w.KI("trackHash", v.trackHash);
    w.KI("livery", v.livery);
    // format: laps XOR durationMin (the engine writes either LAPS mode or TIME mode, never both)
    w.KI("laps", v.laps);
    w.KI("durationMin", v.durationMin);
    // grid
    w.KI("opponents", v.opponents);
    w.KI("oppType", v.oppType);          // 1 = auto by numClasses; 0 identical / 1 same class / 2 multiclass
    w.KI("numClasses", v.numClasses);
    // AI
    w.Key("ai"); w.BeginObject();
    w.KI("skill", v.skill);
    w.KI("aggression", v.aggression);
    w.KI("mistakes", v.mistakes);
    w.KI("wetSkill", v.aiWetSkill);
    w.EndObject();
    // conditions
    w.KI("startHour", v.startHour);
    w.KI("timeProg", v.timeProg);        // 0 = no forzar; 1..11 = real/x2/x5/x10/x15/x20/x25/x30/x40/x50/x60
    w.Key("weather"); w.BeginArray();
    for (int i = 0; i < 4; i++) w.I(v.weather[i]);
    w.EndArray();
    w.KI("weatherSlots", v.weatherSlots);
    // rules
    w.KI("startType", v.startType);      // 0 grid / 1 launched
    w.KI("formationLap", v.formationLap);// ALWAYS -1: this specific session setting is never touched (a hard rule from Jun 24)
    w.Key("pit"); w.BeginObject();
    w.KI("mandatory", v.mandatoryPit);
    w.KI("minTyres", v.pitMinTyres);
    w.KI("minFuel", v.pitMinFuel);
    w.KI("refuelling", v.refuelling);
    w.KI("speedLimit", v.pitSpeedLimit);
    w.EndObject();
    w.KI("fcy", v.fcy);
    // weekend
    w.Key("sessions"); w.BeginObject();
    w.KI("practiceOn", v.practiceOn);
    w.KI("qualifyOn", v.qualifyOn);
    w.KI("practiceMin", v.practiceMin);  // 0 = the engine's fixed default (30 practice / 15 quali)
    w.KI("qualifyMin", v.qualifyMin);
    w.EndObject();
    w.Key("date"); w.BeginObject();
    w.KI("day", v.dateDay);
    w.KI("month", v.dateMonth);
    w.KI("year", v.dateYear);
    w.EndObject();
    // settlement (handoff context, not track config)
    w.KI("phase", v.phase);
    w.KB("trial", v.trialFlag == 1);
    w.KI("trialTopN", v.trialTopN);
    // wear/consumption: the game's GLOBAL options (not the handoff's) scaled by race length.
    // mult 0 = not scaled (reference race or longer) -> the player's global setting stands. idx =
    // the game's target index (lower = MORE wear); orig = its own value, restored once the race ends.
    w.Key("wear"); w.BeginObject();
    w.KB("armed", v.wearArmed);
    w.KI("laps", v.wearLaps);
    w.Key("tyre"); w.BeginObject();
    w.KB("scale", v.scaleTyre); w.KI("mult", v.tyreMult); w.KI("idx", v.tyreIdx); w.KI("orig", v.tyreOrig);
    w.EndObject();
    w.Key("fuel"); w.BeginObject();
    w.KB("scale", v.scaleFuel); w.KI("mult", v.fuelMult); w.KI("idx", v.fuelIdx); w.KI("orig", v.fuelOrig);
    w.EndObject();
    w.EndObject();
    // AUTO/Inherited: same tokens as query.settings/cmd.settings.setInherit. Bit set to 1 = that field is NOT
    // written, by a Settings decision (the sentinels above already reflect it neutralized).
    w.KI("honorInherit", v.honorInherit);
    w.Key("inherited"); w.BeginObject();
    { static const char* const kInhToks[7] = { "weather", "hour", "timeProg", "start", "pit", "ai", "sessions" };
      for (int i = 0; i < 7; i++) w.KB(kInhToks[i], (v.inheritMask & (1 << i)) != 0); }
    w.EndObject();
}

HandlerResult QueryRaceConfig(const jsonm::Value*) {
    HandlerResult r;
    RaceConfigView v = g_raceConfigProvider ? g_raceConfigProvider() : RaceConfigView{};
    if (!v.loaded) {
        r.ok = false; r.code = "not_ready"; r.message = "no race launched yet in this session";
        return r;
    }
    jsonw::Writer w; w.BeginObject();
    WriteRaceConfigBody(w, v);
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// ===== query.telemetry.race ==================================================
// The COMPLETE payload for a race, assembled by the engine and sent by the app. Versioned schema
// (`schema`): the backend rejects anything it doesn't recognize instead of swallowing half-filled rows.
static TelemetryRaceProvider g_telemetryProvider;
void SetTelemetryRaceProvider(TelemetryRaceProvider p) { g_telemetryProvider = std::move(p); }

HandlerResult QueryTelemetryRace(const jsonm::Value*) {
    HandlerResult r;
    if (!g_telemetryProvider) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    TelemetryRaceView v = g_telemetryProvider();
    if (!v.loaded) { r.ok = false; r.code = "not_ready"; r.message = "no race result yet"; return r; }

    jsonw::Writer w; w.BeginObject();
    w.KI("schema", v.schema);
    // Aug 2, a stamp on the grid data: without it, races from before and after a change of
    // drivers get aggregated together, and whatever offset comes out describes neither of them. See
    // career_content_version.h, which also documents the procedure for when to bump it.
    w.KS("dataVersion", career::kContentVersion);
    // Aug 2, the CALIBRATION race deliberately runs at the seed: how it feels doesn't speak to the level. It's flagged
    // here so aggregation can leave it out without having to guess it from class+track.
    w.KB("calibration", v.calibration);
    w.KB("optIn", v.optIn);                    // the app does NOT send with this at false (the payload is still assembled either way)
    w.KS("raceId", v.raceId);
    w.KS("seriesId", v.seriesId);
    w.KI("round", v.round);
    w.KS("classId", v.classId);
    w.KS("className", v.className);
    w.KS("trackName", v.trackName);
    w.KI("trackHash", (long long)v.trackHash);
    w.KI("year", v.year);
    w.KI("doy", v.doy);
    // difficulty: the number that was actually run, and what it's made of. #DDA-class rev.2: it's NO LONGER
    // four layers -- in adaptive mode the number is the CATEGORY's plus the trim; offClass/offTrack stay
    // at zero (the per class+track community layer was retired) and are only kept because the backend already has them
    // in its schema.
    w.Key("difficulty"); w.BeginObject();
    w.KI("mode", v.mode);
    w.KI("base", v.aiBase);
    w.KI("trim", v.trim);
    w.KI("offClass", v.offClass);
    w.KI("offTrack", v.offTrack);
    w.KI("effective", v.aiEffective);
    w.KI("aggression", v.aggr);
    w.KN("classDelta", v.classDelta);   // how far this category's number strays from the overall one (no longer an absolute)
    w.KI("classRaces", v.classRaces);
    w.KI("classAi", v.classAi);   // #DDA-class rev.2: without this, the corpus can't measure whether the per-
                                  // category loop converges, which is the only thing that validates this change.
    // Aug 2 (see TelemetryRaceView): the step the feedback loop took with THIS race; `after` is the OVERALL value (it travels
    // separately from the base/trim/effective trio, which is already per-category), and `windowN` is the CATEGORY's window.
    w.KI("after", v.aiAfter);
    w.KI("windowN", v.windowN);
    w.EndObject();
    // the signal the feedback loop measured at settlement (it can't be reconstructed afterward)
    w.Key("signal"); w.BeginObject();
    w.KB("valid", v.ddaValid);
    w.KN("gapNorm", v.gapNorm);                // RAW advantage vs. the podium cutoff (fraction of a lap)
    w.KN("gapVsBase", v.gapVsBase);            // the same value, already compensated for trim
    w.KB("capped", v.gapCapped);               // was it clipped as an outlier?
    w.KI("rivals", v.rivals);
    w.EndObject();
    w.Key("result"); w.BeginObject();
    w.KI("pos", v.pos); w.KI("field", v.field);
    w.KB("finished", v.finished); w.KB("clean", v.clean);
    w.KN("bestLapS", v.bestLapS); w.KN("totalTimeS", v.totalTimeS);
    w.EndObject();
    // weather: what was ARMED (what was requested) and what actually fell (peak rain sampled during the race)
    w.Key("weather"); w.BeginObject();
    w.Key("armed"); w.BeginArray();
    for (int i = 0; i < 4; i++) w.I(v.weather[i]);
    w.EndArray();
    w.KI("slots", v.weatherSlots);
    w.KN("rainMax", v.rainMax);
    w.EndObject();
    // pace mode: "series" = every driver brings their COMPLETE lap-by-lap (the
    // per-lap distances can be reconstructed); "summary" = the race was too long, and only the three summary numbers travel.
    // It's stated explicitly so nobody confuses "it didn't fit" with "there were no laps".
    w.KS("paceMode", v.paceSeries ? "series" : "summary");
    // grid of best laps. The PLAYER's name is deliberately left empty (no personal data); the AI's
    // names are ours, and they're needed to fine-tune pace per driver.
    w.Key("grid"); w.BeginArray();
    for (const auto& g : v.grid) {
        w.BeginObject();
        w.KI("pos", g.pos);
        w.KS("name", g.isPlayer ? std::string() : g.name);
        w.KS("carClass", g.carClass);
        w.KS("carName", g.carName);
        w.KI("laps", g.laps);
        w.KB("dnf", g.dnf);
        w.KB("isPlayer", g.isPlayer);
        w.KN("bestLapS", g.bestLapS);
        w.KN("totalTimeS", g.totalTimeS);
        // summarized pace: median and spread in whole MILLISECONDS (the source is a float in
        // seconds; "%.17g" would turn 101.712 into 18 characters of floating-point noise).
        w.KI("lapN", g.lapN);
        w.KI("lapMedianMs", (long long)(g.lapMedianS * 1000.0 + 0.5));
        w.KI("lapSdMs", (long long)(g.lapSdS * 1000.0 + 0.5));
        // lap by lap (series mode only): index = lap number, 0 = a lap with no credible time.
        if (!g.lapSeries.empty()) {
            w.Key("lapMs"); w.BeginArray();
            for (float t : g.lapSeries) w.I((long long)(t * 1000.0f + 0.5f));
            w.EndArray();
        }
        w.EndObject();
    }
    w.EndArray();
    // the survey, if they answered it. feel 0 = unanswered -> the backend still aggregates the race.
    w.Key("feedback"); w.BeginObject();
    w.KI("feel", v.feel);
    w.Key("reasons"); w.BeginArray();
    for (const auto& s : v.reasons) w.S(s);
    w.EndArray();
    w.EndObject();
    // the full launch config, same serializer as query.raceConfig
    w.Key("config");
    if (v.config.loaded) { w.BeginObject(); WriteRaceConfigBody(w, v.config); w.EndObject(); }
    else w.Null();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// cmd.entitlements.set { tier: "free"|"supporter"|"plus" } -> global Entitlements::SetTier.
// The app pushes this on every (re)connection from the Patreon token.
// The real per-tier gating (Has/IsVisual) is activated separately; this is just the tier plumbing.
static HandlerResult CmdEntitlementsSet(const jsonm::Value* params) {
    HandlerResult r;
    std::string tok = params ? params->memberString("tier") : std::string();
    Tier t;
    if (tok == "free") t = Tier::Free;
    else if (tok == "supporter") t = Tier::Supporter;
    else if (tok == "plus") t = Tier::SupporterPlus;
    else { r.ok = false; r.code = "bad_params"; r.message = "unknown tier: " + tok; return r; }
    Entitlements::SetTier(t);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", true); w.KS("tier", tok.c_str()); w.EndObject();
    r.payload = w.Str();
    return r;
}

// query.entitlements -> { tier } from Entitlements::Get() (read-back for the app and for tests).
static HandlerResult QueryEntitlements(const jsonm::Value*) {
    HandlerResult r;
    const char* tok = "plus";
    switch (Entitlements::Get()) {
        case Tier::Free: tok = "free"; break;
        case Tier::Supporter: tok = "supporter"; break;
        default: tok = "plus"; break;
    }
    jsonw::Writer w; w.BeginObject(); w.KS("tier", tok); w.EndObject();
    r.payload = w.Str();
    return r;
}

// "real skins" (enriched): cmd.skins.status -> { ok, installed_overrides,
// packs:[{slug,title,url,author,season_year,series[],sample_vehicle,sample_livery_id,entries_total,
// detected,detected_entries}] }. The author/season_year/series fields (a class_name array) come from the catalog;
// entries_total = number of liveries in the resource (for "N liveries" / "detected/total" in the app);
// sample_vehicle/sample_livery_id = a race car (not a safety car), preferably WITH photos (for the preview).
// installed_overrides = career::InstalledSkinCount() (total number of (slug,
// livery_id) slots with a skin installed). packs comes from the app's curated catalog
// (docs/skins/recommended_skins.json, via paths::Data — NOTE: the real path lives under the "skins\\" subdir,
// not at the root of Data as the brief suggested). An entry is "detected" if the skin installed in its slot is
// EXACTLY its skin_name (career::SkinNameFor == skin_name); by NAME, not just presence, to avoid flagging
// two packs for the same car (2024 vs 2025) just because one of them is installed.
// Missing/malformed file -> {ok:true, installed_overrides:N, packs:[]}: the catalog is optional,
// NEVER an error (the same "forgiving" rule as LoadMemoryLore/CalendarView for missing data files).
// "real skins" (fixed Jul 17): does the car have generated photos (vehimages)? The app reads them from that folder; if none
// exist, the card falls back to a placeholder. Used to pick a sample card WITH a photo.
static bool SkinVehImagesExist(const std::string& slug) {
    if (slug.empty()) return false;
    // mojibake fix, Jul 22: DirExists (exists AND is a directory, same check as the FILE_ATTRIBUTE_DIRECTORY bit)
    return paths::DirExists(paths::Data("vehimages\\gui\\vehicleimages\\vehicleimages_" + slug));
}
// safety/pace/medical car: not a representative race car -> avoided as a sample (e.g. the F1
// 2000 pack includes fenwick_ss_safetycar as an entry, and we do NOT want the card showing the safety car).
static bool SkinIsSafetyVehicle(const std::string& slug) {
    return slug.find("safety") != std::string::npos || slug.find("pace") != std::string::npos
        || slug.find("medical") != std::string::npos;
}

static HandlerResult CmdSkinsStatus(const jsonm::Value* /*params*/) {
    HandlerResult r;
    jsonw::Writer w; w.BeginObject();
    w.KB("ok", true);
    w.KI("installed_overrides", career::InstalledSkinCount());
    w.Key("packs");
    w.BeginArray();
    std::string err;
    jsonm::ValuePtr root = jsonm::ParseFile(paths::Data("skins\\recommended_skins.json").c_str(), &err);
    const jsonm::Value* packs = root ? root->member("packs") : nullptr;
    if (packs && packs->isArray()) {
        // PASS 1: per pack, the SLOTS that match by NAME (SkinNameFor == skin_name) + a preview sample.
        struct PackScan {
            const jsonm::Value* p;
            std::vector<std::string> matched;   // matching slotKeys (vehicle + livery_id)
            int entriesTotal;
            std::string sampleVehicle; int sampleLiveryId;
        };
        std::vector<PackScan> scans;
        scans.reserve(packs->arr.size());
        for (const auto& pp : packs->arr) {
            const jsonm::Value* p = pp.get();
            if (!p || !p->isObject()) continue;
            PackScan sc; sc.p = p; sc.sampleLiveryId = -1;
            const jsonm::Value* entries = p->member("entries");
            sc.entriesTotal = (entries && entries->isArray()) ? (int)entries->arr.size() : 0;
            // preview sample: a race car (not safety) WITH photos is PREFERRED; degrades to non-safety,
            // then to the first entry. (Previously: just the first entry -> the safety car in F1 2000, a car with no photos.)
            std::string bestVeh, okVeh, firstVeh; int bestId = -1, okId = -1, firstId = -1;
            if (entries && entries->isArray()) {
                for (const auto& ee : entries->arr) {
                    const jsonm::Value* e = ee.get();
                    if (!e || !e->isObject()) continue;
                    std::string veh = e->memberString("vehicle");
                    int liveryId = e->memberInt("livery_id", -1);
                    std::string skinName = e->memberString("skin_name");
                    // Matches by NAME: the skin installed at (car,livery_id) is EXACTLY the one from this entry.
                    if (!veh.empty() && !skinName.empty() && career::SkinNameFor(veh, liveryId) == skinName)
                        sc.matched.push_back(veh + "\x1f" + std::to_string(liveryId));
                    if (!veh.empty()) {
                        if (firstVeh.empty()) { firstVeh = veh; firstId = liveryId; }
                        if (!SkinIsSafetyVehicle(veh)) {
                            if (okVeh.empty()) { okVeh = veh; okId = liveryId; }
                            if (bestVeh.empty() && SkinVehImagesExist(veh)) { bestVeh = veh; bestId = liveryId; }
                        }
                    }
                }
            }
            sc.sampleVehicle = !bestVeh.empty() ? bestVeh : (!okVeh.empty() ? okVeh : firstVeh);
            sc.sampleLiveryId = !bestVeh.empty() ? bestId : (!okVeh.empty() ? okId : firstId);
            scans.push_back(std::move(sc));
        }
        // DOMINANCE: an installed slot that matches by NAME across SEVERAL packs (e.g. "Max Verstappen #1"
        // IDENTICAL in both the 2024 and 2025 pack, or "Nico Hulkenberg") is attributed ONLY to the pack with MORE
        // total matches (= the one actually installed). Without this, installing one pack would mark
        // others for the same car as "detected" when they only share a few names (a false 6/28).
        // Tied matches -> the lower index wins (deterministic).
        std::map<std::string, int> owner;   // slotKey -> index of the owning pack
        for (size_t i = 0; i < scans.size(); i++)
            for (const auto& key : scans[i].matched) {
                auto it = owner.find(key);
                if (it == owner.end()) { owner[key] = (int)i; continue; }
                if (scans[i].matched.size() > scans[(size_t)it->second].matched.size()) it->second = (int)i;
            }
        // PASS 2: output. detected_entries = slots THIS pack owns after the dominance pass.
        for (size_t i = 0; i < scans.size(); i++) {
            const PackScan& sc = scans[i];
            int detectedEntries = 0;
            for (const auto& key : sc.matched) if (owner[key] == (int)i) detectedEntries++;
            const jsonm::Value* p = sc.p;
            w.BeginObject();
            w.KS("slug",  p->memberString("slug"));
            w.KS("title", p->memberString("title"));
            w.KS("url",   p->memberString("url"));
            w.KS("author", p->memberString("author"));
            w.KS("season_year", p->memberString("season_year"));
            w.Key("series");
            w.BeginArray();
            const jsonm::Value* series = p->member("series");
            if (series && series->isArray())
                for (const auto& ss : series->arr)
                    if (ss && ss->isString()) w.S(ss->str);
            w.EndArray();
            w.KS("sample_vehicle", sc.sampleVehicle);
            w.KI("sample_livery_id", sc.sampleLiveryId);
            w.KI("entries_total", sc.entriesTotal);
            w.KB("detected", detectedEntries > 0);
            w.KI("detected_entries", detectedEntries);
            w.EndObject();
        }
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// "real skins" cmd.skins.installed -> { ok, slots:[{slug,livery_id,name,preview_rel}] }. Ground truth from
// DISK: every active override detected at boot (career::InstalledSkinSlots), with or without a catalog. The app
// uses slug+preview_rel to locate the DDS (<gameInstall>\...\Overrides\<slug>\<preview_rel>) and convert it to
// PNG in the background (sync_override_previews). preview_rel "" = the override declares no <PREVIEWIMAGE> -> no real
// preview, falls back to vanilla. Never an error (same graceful rule as cmd.skins.status).
static HandlerResult CmdSkinsInstalled(const jsonm::Value* /*params*/) {
    HandlerResult r;
    jsonw::Writer w; w.BeginObject();
    w.KB("ok", true);
    w.Key("slots");
    w.BeginArray();
    for (const auto& s : career::InstalledSkinSlots()) {
        w.BeginObject();
        w.KS("slug",        s.vehicle_slug);
        w.KI("livery_id",   s.livery_id);
        w.KS("name",        s.name);
        w.KS("preview_rel", s.preview_rel);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

// silly season: THE PADDOCK ---------------------------------------------------------------
// An EXACT mirror of WindowSeatState (career_save.h) in the tokens the design already uses (PD_SEAT_STATUS from
// data_paddock.jsx). A new engine state has to show up here, or the UI would render it as "free",
// which is the most silent lie possible; that's why the default is its own token, not the first one.
static const char* WindowSeatToken(int s) {
    switch (s) {
        case career::WSEAT_FREE:     return "free";
        case career::WSEAT_SOUNDED:  return "sounded";
        case career::WSEAT_DISPUTED: return "disputed";
        case career::WSEAT_OFFERED:  return "offered";
        case career::WSEAT_REJECTED: return "rejected";
        case career::WSEAT_RENEWED:  return "renewed";
        default: return "unknown";
    }
}
static const char* MarketPhaseToken(int p) {
    switch (p) {
        case market::MARKET_WINDOW:   return "window";
        case market::MARKET_DECISION: return "decision";
        default:                      return "season";   // MARKET_PADDOCK == the season (PD_PHASES.season)
    }
}

static PaddockSeatCarFn g_paddockSeatCarFn;   // (setter at the end of the file, next to SetPaddockCmds)

HandlerResult QueryPaddock(const jsonm::Value* params) {
    HandlerResult r; CareerStateView v;
    if (!ResolveView(v, r)) return r;
    const career::SaveData& s = *v.save;
    const career::CareerCalendar& cal = *v.calendar;
    const int year = cal.year, doy = cal.cursor_doy;

    jsonw::Writer w; w.BeginObject();
    w.KS("phase", MarketPhaseToken(market::MarketPhaseFor(year, doy, career::kStartYear)));
    w.KI("year", year);
    w.KI("doy", doy);
    // THE DROUGHT. The window passed and you were left with nothing. It's its own flag, not a
    // phase, because it isn't a moment on the calendar: it's a STATE you're in within it. And it has to be said
    // out loud -- renting a drive isn't game over, it's the floor -- instead of leaving the tab blank, which is what
    // it did until now, and that reads as "the game has frozen".
    w.KB("drought", career::InDrought(s, year, doy, career::kStartYear));
    // the season being signed for RIGHT NOW (in year 1's compressed cycle, that's this same year) versus
    // the one the live window has sealed. If they differ, what's on screen is the PREVIOUS window, already
    // resolved — the UI needs to tell them apart so it doesn't title "signing for 2026" over 2025 seats.
    w.KI("targetYear", market::WindowTargetYear(year, doy, career::kStartYear));
    w.KI("windowYear", s.window_year);
    // both countdowns look at year 1's COMPRESSED cycle, just like the three lines above. Without
    // `kStartYear`, the screen used to say "348 days left" for the decision week on a freshly
    // started game, when the one actually coming up is February's, only 45 days out.
    w.KI("daysToWindow",   market::DaysToMarketWindow(year, doy, career::kStartYear));
    w.KI("daysToDecision", market::DaysToDecisionWeek(year, doy, career::kStartYear));

    w.Key("seats"); w.BeginArray();
    for (const auto& ws : s.window_seats) {
        const career::SeriesDef* sd = career::SeriesById(ws.series_id);
        // occupant and expiry are DERIVED from the live seat, not stored in the sealed key. A
        // team can have two cars; the window seals the TEAM, so it reports on the first of its
        // seats — which is also the one the contest looks at.
        std::string occupantName; int expiryYear = 0, occupantAge = 0, reqRep = 0;
        std::string teamCountry;   // incumbent's country = the team's nationality proxy (natMatch)
        if (sd) {
            // the seat's bar is set by the series, the same way the decision week reads it
            const std::vector<market::MarketSeat> ms = market::SeatsForSeries(*sd);
            if (!ms.empty()) reqRep = ms[0].reqRep;
            const std::vector<career::Seat> seats = career::SeatsForClass(ws.series_id, s);
            for (size_t i = 0; i < seats.size(); i++) {
                if (seats[i].team_label != ws.team_label) continue;
                if (!seats[i].occupant_id.empty()) {
                    const career::PoolDriver* pd = career::DriverById(seats[i].occupant_id);
                    // generated drivers (gn_*) are NOT in the pool -> the raw id used to leak onto the screen
                    occupantName = career::DriverDisplayName(seats[i].occupant_id);
                    if (pd) teamCountry = pd->country;
                    if (pd && pd->birth_year > 0) occupantAge = career::DriverAge(pd->birth_year, year);
                    const std::string key = career::SeatKey(ws.series_id, (int)i);
                    expiryYear = career::ContractExpiryYear(key, seats[i].occupant_id,
                                                            career::SeatContractStartYear(s, key));
                }
                break;
            }
        }
        w.BeginObject();
        w.KS("seriesId", ws.series_id);
        w.KS("seriesDisplay", sd ? sd->display : ws.series_id);
        w.KI("level", sd ? sd->level : 0);
        w.KS("team", ws.team_label);
        w.KS("status", WindowSeatToken(ws.state));
        w.KB("applied", ws.applied != 0);
        // TESTED-THE-WATERS says the team talked to someone; this says it talked to YOU. Without
        // separating them, the screen can't tell "they've been asking around" from "they've been asking about you",
        // which is the difference between a press note and an actual offer.
        w.KB("soundedYou", ws.sounded != 0);
        w.KS("winnerName", ws.winner_name);
        w.KS("occupantName", occupantName);
        w.KI("expiryYear", expiryYear);
        // the FIGURE for the offered seat and whether you've already signed it. Both DERIVED: the salary is
        // recomputed (the sealed key stores identity, not numbers), and "signed" is simply having a
        // contract in that series. A persisted field would say the same thing and could drift out of sync.
        w.KI("perRace", ws.state == career::WSEAT_OFFERED
                          ? (int)career::WindowSeatPerRace(s, ws.series_id, ws.team_label, year) : 0);
        // (seat detail): the incumbent's AGE and the seat's REQUIREMENT. Both derived and
        // both cheap; without them the detail panel can't say whose seat it is or what it demands,
        // which is exactly what sets it apart from a plain list row.
        w.KI("occupantAge", occupantAge);
        w.KI("reqRep", reqRep);
        // your RATING, so the requirement can be rendered as ✓ or ✕ instead of a bare number.
        // It's deliberately per-seat, not in the header: the screen compares right there, with no data crossing needed.
        w.KI("yourRating", (int)s.prog.rating);
        // the RELATIONSHIP with that team (0..100). Derived from shared history -- former team, courting,
        // snubs, press marks -- via the SAME function that decides whether they test the waters with you. It's not decoration: it explains
        // why some teams come after you and others don't.
        w.KI("rel", (int)(market::TeamRelationship(s, ws.series_id, ws.team_label) * 100.0));
        // WHAT THIS SEAT IS LOOKING FOR, SPELLED OUT. The candidacy model already decided using
        // this, but it never left the engine: the detail panel could only show the reputation bar, so the
        // player had no way to know why one team wanted them and another didn't. The team's CHARACTER
        // and the factors it weighs most are emitted, each paired with what YOU bring -- which is what turns "they
        // care about money" into "they care about money, and you're at the 20th percentile".
        // Tokens, not text: the screen translates. And the calculation is THE SAME ONE that decides the contest, not a copy.
        w.KS("personality", career::TeamPersonalityToken(ws.team_label));
        { double mn = (double)s.prog.credits / career::bal::MK_PAYDRIVER_MONEY_REF;
          if (mn < 0.0) mn = 0.0; if (mn > 1.0) mn = 1.0;
          // only the ones that actually matter: all 8 integers would be noise and would hide exactly what sets
          // one team apart from another. With 4, it reads at a glance and the team's character comes through clearly.
          w.Key("asks"); w.BeginArray();
          for (const auto& a : career::SeatAsksFor(s, sd ? sd->class_name : std::string(), ws.team_label,
                                                   teamCountry, mn, year, /*topN=*/4)) {
              w.BeginObject(); w.KS("k", a.key); w.KI("w", a.weight); w.KI("you", a.you); w.EndObject();
          }
          w.EndArray(); }
        // THE SHORTLIST. It was already SEALED and persisted (SeatWatch.contenders): the only thing
        // missing was emitting it. The NAMES are given, which is what exists; per-candidate state
        // (pre-agreement / trial) and your spot on the list don't exist yet, so they aren't invented.
        w.Key("shortlist"); w.BeginArray();
        for (const auto& sw : s.seat_watches) {
            if (sw.series_id != ws.series_id || sw.team_label != ws.team_label) continue;
            for (const auto& c : sw.contenders) {
                w.BeginObject(); w.KS("id", c.id); w.KS("name", c.name); w.EndObject();
            }
            break;
        }
        w.EndArray();
        // WHICH CAR the seat is — in a multi-car class (Jessamy Lum364 has 6 of them), the
        // seat doesn't make sense without it. The overlay's provider resolves it via the same deterministic path
        // as the offer preview; with no provider or no resolution, the fields are omitted and the app renders nothing.
        if (g_paddockSeatCarFn) {
            std::string cn, cs; int nn = -1;
            if (g_paddockSeatCarFn(ws.series_id, ws.team_label, cn, cs, nn)) {
                w.KS("carName", cn); w.KS("carSlug", cs); w.KI("liveryNN", nn);
            }
        }
        w.KB("signed", career::ContractForSeries(s, ws.series_id) != nullptr);
        // 18/09 (Josh): how many days are left for an OFFERED, unsigned seat (1 = today is the last).
        if (ws.state == career::WSEAT_OFFERED && !career::ContractForSeries(s, ws.series_id)) {
            const int muere = career::WindowOfferDeathAbs(s, cal, year, doy, career::kStartYear);
            const int dias = muere > 0 ? muere - career::dateutil::AbsDayFromYearDoy(year, doy) : 0;
            if (dias > 0) w.KI("daysLeft", dias);
        }
        w.EndObject();
    }
    w.EndArray();

    // THE SERIES INDEX for the rail. Deliberately cheap: per series, how many seats are in play,
    // how many of those are yours, and how many contracts expire at year's end. It's what turns the rail into
    // something readable at a glance -- "where something's happening" -- instead of a list of names.
    {
        std::map<std::string, int> enJuego, tuyas;
        for (const auto& ws : s.window_seats) {
            enJuego[ws.series_id]++;
            if (ws.applied || ws.sounded) tuyas[ws.series_id]++;
        }
        w.Key("series"); w.BeginArray();
        for (const auto& sd : career::AllSeries()) {
            if (sd.kind == "standalone" || !sd.branch_of.empty()) continue;
            if (career::IsEventOnlySeries(sd)) continue;                  // 01/10: no seats, nothing to list
            if (!career::SeriesPlayable(sd)) continue;
            const std::vector<career::Seat> seats = career::SeatsForClass(sd.id, s);
            if (seats.empty()) continue;
            int vencen = 0;
            for (size_t i = 0; i < seats.size(); i++) {
                if (seats[i].occupant_id.empty()) continue;
                const std::string key = career::SeatKey(sd.id, (int)i);
                if (career::SeatContractExpires(s, key, seats[i].occupant_id, year)) vencen++;
            }
            const bool mia = career::ContractForSeries(s, sd.id) != nullptr;
            w.BeginObject();
            w.KS("id", sd.id);
            w.KS("display", sd.display);
            w.KI("level", sd.level);
            w.KS("discipline", career::DiscForSeries(&sd));
            w.KI("seats", (int)seats.size());
            w.KI("inPlay", enJuego.count(sd.id) ? enJuego[sd.id] : 0);
            w.KI("yours", tuyas.count(sd.id) ? tuyas[sd.id] : 0);
            w.KI("expiring", vencen);
            w.KB("mine", mia);          // "you race here": the rail has to tell you where YOU are
            w.EndObject();
        }
        w.EndArray();
    }

    // the world's DETAIL, for just the requested series: teams with their seats, who occupies them, at what
    // age and until when. With no seriesId nothing is computed (the rail doesn't need this to draw itself).
    const std::string pedida = params ? params->memberString("seriesId") : std::string();
    w.Key("world"); w.BeginArray();
    if (!pedida.empty()) {
        const career::SeriesDef* psd = career::SeriesById(pedida);
        if (psd) {
            const std::vector<career::Seat> seats = career::SeatsForClass(pedida, s);
            // grouped by TEAM, preserving appearance order: a class's seat order
            // matches its grid, and sorting it alphabetically would break the reading of who's ahead.
            std::vector<std::string> orden;
            std::map<std::string, std::vector<size_t>> porEquipo;
            for (size_t i = 0; i < seats.size(); i++) {
                if (seats[i].team_label.empty()) continue;
                if (!porEquipo.count(seats[i].team_label)) orden.push_back(seats[i].team_label);
                porEquipo[seats[i].team_label].push_back(i);
            }
            for (const auto& team : orden) {
                w.BeginObject();
                w.KS("team", team);
                // is this seat inside the live window? It's the union of the two views: the background world and
                // what's ALSO currently IN PLAY, flagged on top of it.
                const career::WindowSeat* ws = career::FindWindowSeat(s, pedida, team);
                w.KS("windowStatus", ws ? WindowSeatToken(ws->state) : "");
                w.KB("mine", career::ContractForSeries(s, pedida) != nullptr
                             && career::ContractForSeries(s, pedida)->team_id == team);
                w.Key("seats"); w.BeginArray();
                for (size_t idx : porEquipo[team]) {
                    const career::Seat& st = seats[idx];
                    std::string nom; int edad = 0, vence = 0;
                    if (!st.occupant_id.empty()) {
                        const career::PoolDriver* pd = career::DriverById(st.occupant_id);
                        nom = career::DriverDisplayName(st.occupant_id);   // same idea: covers generated drivers too
                        if (pd && pd->birth_year > 0) edad = career::DriverAge(pd->birth_year, year);
                        const std::string key = career::SeatKey(pedida, (int)idx);
                        vence = career::ContractExpiryYear(key, st.occupant_id,
                                                           career::SeatContractStartYear(s, key));
                    }
                    w.BeginObject();
                    w.KI("idx", (int)idx);
                    w.KS("occupantName", nom);
                    w.KI("occupantAge", edad);
                    w.KI("expiryYear", vence);
                    w.KB("open", st.open);
                    w.EndObject();
                }
                w.EndArray();
                w.EndObject();
            }
        }
    }
    w.EndArray();

    // MY CONTRACTS, the design's fourth sub-tab. It lives in query.paddock, not a
    // new query, because it's the SAME question as the rest of the screen ("where am I and where could I be"), and
    // splitting it into two queries would force the UI to stitch them back together just to render one thing.
    w.Key("contracts"); w.BeginArray();
    for (const auto& c : s.contracts) {
        if (!c.active) continue;
        const career::SeriesDef* sd = career::SeriesById(c.series_id);
        w.BeginObject();
        w.KS("seriesId", c.series_id);
        w.KS("seriesDisplay", sd ? sd->display : c.series_id);
        w.KS("team", c.team_id);
        w.KI("seasonsLeft", c.seasons_left);
        w.KI("perRace", (int)c.per_race);
        w.KI("objTargetPos", c.obj_target_pos);   // what the team demands of you (0 = no target)
        // the contract's YEARS, the first thing read off a card ("2025-2027 · 2 years left").
        // They'd always been in the save; nothing was emitting them, that's all.
        w.KI("fromYear", c.signed_year);
        w.KI("untilYear", c.signed_year + c.seasons_total - 1);
        w.KI("kind", c.kind);
        // your current POSITION in that championship. Without it, the target is a bare number: knowing they
        // want P5 says nothing if you don't know you're running P8. With both, the card can say ON TRACK or AT
        // RISK, which is the only reading that actually changes how you race the season.
        int posActual = 0;
        if (const career::SeasonState* ss = career::FindEnrollment(cal, c.series_id)) {
            const std::vector<career::StandingRow> tabla = career::FinalTable(ss->standings);
            const std::string yo = PlayerDisplayName(v);
            for (size_t k = 0; k < tabla.size(); k++)
                if (tabla[k].name == yo) { posActual = (int)k + 1; break; }
        }
        w.KI("currentPos", posActual);   // 0 = no table yet (you haven't raced that series this year)
        // YOUR TEAMMATE. The other seat carrying your same tag, and it only exists where the team
        // fields two cars. It's emitted with THEIR position in the same table as yours, because the "beat your
        // teammate" clause can't be read off a bare name: you need both numbers. "" = your team
        // runs a single car (the norm outside the big categories), and the card shouldn't render the row.
        std::string tmName, tmId; int tmPos = 0;
        { const int tmIdx = career::TeammateSeatIndex(s, c.series_id, c.team_id, c.seat_idx);
          if (tmIdx >= 0) {
              const std::vector<career::Seat> seats = career::SeatsForClass(c.series_id, s);
              if (tmIdx < (int)seats.size() && !seats[tmIdx].occupant_id.empty()) {
                  tmId = seats[tmIdx].occupant_id;
                  tmName = career::DriverDisplayName(tmId);   // same idea: your TEAMMATE could show up as gn_...
                  if (const career::SeasonState* ss2 = career::FindEnrollment(cal, c.series_id)) {
                      const std::vector<career::StandingRow> t2 = career::FinalTable(ss2->standings);
                      for (size_t k = 0; k < t2.size(); k++)
                          if (t2[k].name == tmName) { tmPos = (int)k + 1; break; }
                  }
              }
          } }
        w.KS("teammate", tmName);
        w.KI("teammatePos", tmPos);      // 0 = no standings, or not racing this championship
        // THE HEAD-TO-HEAD WITH YOUR TEAMMATE, RACE BY RACE. The table says who's ahead TODAY; this says
        // how many times you've beaten them, which is what actually gets talked about in the garage. And nothing new needs measuring:
        // AccumulateH2H has been logging your wins and losses against every named driver in
        // your class for a while now, so the teammate head-to-head was ALREADY sitting in save.h2h -- the only thing missing
        // was knowing which of those drivers is yours. Same car, a clean comparison: it's the only rival against
        // whom a bad result has no excuse, and until now the game never said so anywhere.
        { int gw = 0, gl = 0;
          if (!tmId.empty()) {
              const auto it = s.h2h.find(tmId);
              if (it != s.h2h.end()) { gw = it->second.w; gl = it->second.l; }
          }
          w.KI("teammateW", gw);
          w.KI("teammateL", gl); }
        // what it would cost to WALK AWAY from this contract RIGHT NOW. It's the same function that later charges
        // via BreakContract, so the warning can't lie. The UI needs it so breaking a contract is a decision
        // made with the price in plain view, not a surprise on the balance.
        w.KI("breakFee", (int)career::BreakContractFee(s, c.series_id));
        // the REPUTATION it costs, for the same reason as the amount: the warning has to say everything. The
        // EFFECTIVE hit is sent, not the constant, because BreakContract floors it at 0 -- with 4 REP left
        // you lose 4, not 10, and announcing 10 would be lying to exactly the player with the least room to spare.
        { const int golpe = -career::bal::REP_BROKEN_CONTRACT;   // the constant is negative
          w.KI("breakRep", s.prog.rep < golpe ? s.prog.rep : golpe); }
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    r.payload = w.Str();
    return r;
}

static PaddockApplyExec g_paddockApplyExec;
static PaddockSignExec  g_paddockSignExec;
void SetPaddockCmds(PaddockApplyExec apply, PaddockSignExec sign) {
    g_paddockApplyExec = std::move(apply); g_paddockSignExec = std::move(sign);
}
void SetPaddockSeatCarProvider(PaddockSeatCarFn fn) { g_paddockSeatCarFn = std::move(fn); }
HandlerResult CmdPaddockApply(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_paddockApplyExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string seriesId = params ? params->memberString("seriesId") : std::string();
    std::string team     = params ? params->memberString("team")     : std::string();
    if (seriesId.empty() || team.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing seriesId/team"; return r; }
    const bool applied = g_paddockApplyExec(seriesId, team);
    jsonw::Writer w; w.BeginObject(); w.KB("ok", applied); w.KB("applied", applied); w.EndObject();
    r.payload = w.Str();
    return r;
}

HandlerResult CmdPaddockSign(const jsonm::Value* params) {
    HandlerResult r;
    if (!g_paddockSignExec) { r.ok = false; r.code = "not_ready"; r.message = "career engine not initialized"; return r; }
    std::string seriesId = params ? params->memberString("seriesId") : std::string();
    std::string team     = params ? params->memberString("team")     : std::string();
    if (seriesId.empty() || team.empty()) { r.ok = false; r.code = "bad_params"; r.message = "missing seriesId/team"; return r; }
    std::string reason;
    const bool ok = g_paddockSignExec(seriesId, team, &reason);
    // the REASON travels even when the result is false: a bare "couldn't sign" leaves the player without
    // knowing whether they're short on money, already in that series, or their current team forbids it -- and all three get
    // fixed in a different way.
    jsonw::Writer w; w.BeginObject(); w.KB("ok", ok); w.KB("signed", ok); w.KS("reason", reason); w.EndObject();
    r.payload = w.Str();
    return r;
}

void RegisterCareerQueries(ApiCore& core) {
    core.Register("query.career_state", &QueryCareerState);
    core.Register("query.game_state",   &QueryGameState);
    core.Register("query.standings",    &QueryStandings);
    core.Register("query.inbox",        &QueryInbox);
    core.Register("query.profile",      &QueryProfile);
    core.Register("query.memories",     &QueryMemories);
    core.Register("query.calendar",     &QueryCalendar);
    core.Register("query.content",      &QueryContent);       // DLC products (Settings>Content)
    core.Register("query.events.content", &QueryEventsContent); // DLC: missing track/classes for each event
    core.Register("query.results",      &QueryResults);
    core.Register("query.times",        &QueryTimes);
    core.Register("query.tree",         &QueryTree);
    core.Register("query.settings",     &QuerySettings);
    core.Register("query.garage",       &QueryGarage);
    core.Register("cmd.garage.sell",    &CmdGarageSell);      // 0.7.0: sell a purchased car
    core.Register("query.market",       &QueryMarket);
    core.Register("query.hub",          &QueryHub);
    core.Register("query.identity",     &QueryIdentity);
    core.Register("query.negotiate",    &QueryNegotiate);
    core.Register("query.season",       &QuerySeason);
    core.Register("query.superlicenses", &QuerySuperlicenses);   // Profile > Super-licenses
    core.Register("query.prerace",      &QueryPrerace);
    core.Register("query.tryout",       &QueryTryout);
    core.Register("query.rollover",     &QueryRollover);
    core.Register("query.race_config",  &QueryRaceConfig);   // debug: last launch's config (SIMULATION-mode coverage)
    core.Register("query.cards",          &QueryCards);            // sponsor cards
    core.Register("cmd.cards.buy",        &CmdCardsBuy);
    core.Register("cmd.cards.reroll",     &CmdCardsReroll);
    core.Register("cmd.cards.shop_close", &CmdCardsShopClose);
    core.Register("cmd.cards.slot",       &CmdCardsSlot);
    core.Register("cmd.cards.bench",      &CmdCardsBench);
    core.Register("cmd.cards.upgrade",    &CmdCardsUpgrade);
    core.Register("cmd.cards.sell",       &CmdCardsSell);
    core.Register("cmd.cards.seen",       &CmdCardsSeen);     // tab badge
    core.Register("cmd.cards.arrange",    &CmdCardsArrange);  // atomic drag & drop
    core.Register("cmd.memories.ackgift", &CmdMemoriesAckGift);
    core.Register("cmd.settings.setMode", &CmdSettingsSetMode);
    core.Register("cmd.settings.setTrim", &CmdSettingsSetTrim);
    core.Register("cmd.settings.setAggr", &CmdSettingsSetAggr);   //
    core.Register("cmd.settings.setScale", &CmdSettingsSetScale);  //
    core.Register("cmd.settings.setFcy",  &CmdSettingsSetFcy);     // FCY/safety car
    core.Register("cmd.settings.setClassLock", &CmdSettingsSetClassLock);   // #DDA-lock
    core.Register("cmd.settings.setLockAll",   &CmdSettingsSetLockAll);     // #DDA-lock
    core.Register("cmd.settings.setWeather", &CmdSettingsSetWeather);   // weather mode
    core.Register("cmd.settings.setInherit", &CmdSettingsSetInherit);   // AUTO/Inherited per field
    core.Register("cmd.dbg.mem",           &CmdDbgMem);              // memory probe (diagnostics)
    core.Register("cmd.vr.panel",          &CmdVrPanel);             // VR: the app governs the headset panel
    core.Register("cmd.settings.setTelemetry", &CmdSettingsSetTelemetry);   // telemetry consent
    core.Register("cmd.settings.setSeed", &CmdSettingsSetSeed);   // Aug 2: the calibration race's level
    core.Register("cmd.settings.setCustomAi", &CmdSettingsSetCustomAi);   // hand-set fixed difficulty (DM_CUSTOM)
    core.Register("cmd.settings.setReward", &CmdSettingsSetReward);       // 0.7.0: career pace (reward_mode)
    core.Register("query.telemetry.race", &QueryTelemetryRace);   // last race's telemetry payload
    core.Register("cmd.results.feel",     &CmdResultsFeel);       // post-race feel survey
    core.Register("cmd.results.gapOffer", &CmdResultsGapOffer);   // difficulty offer for an outlier gap
    core.Register("cmd.content.toggle",   &CmdContentToggle);      // DLC product toggles
    core.Register("cmd.market.accept",    &CmdMarketAccept);
    core.Register("cmd.market.decline",   &CmdMarketDecline);
    core.Register("query.paddock",        &QueryPaddock);          // the transfer window
    core.Register("cmd.paddock.apply",    &CmdPaddockApply);       // apply for a seat
    core.Register("cmd.paddock.sign",     &CmdPaddockSign);        // sign an OFFERED seat
    core.Register("cmd.inbox.markRead", &CmdInboxMarkRead);   // inbox redesign: persists read state from the app
    core.Register("cmd.inbox.markAllRead", &CmdInboxMarkAllRead);   // 18/09: all read, a single save
    core.Register("cmd.inbox.answer",   &CmdInboxAnswer);     // inbox redesign: instant decisions
    core.Register("query.time.preview",   &QueryTimePreview);   // C1: { seriesId? } | { untilAbsDay? } | { scope? } | {}
    core.Register("cmd.time.advance",     &CmdTimeAdvance);     // C1: SAME params as the preview; executes
    core.Register("cmd.time.rules",       &CmdTimeRules);       // C1: { offers, myRaces, anyRace, mail, relation }
    core.Register("cmd.contract.break",   &CmdContractBreak);
    core.Register("cmd.contract.breakSeries", &CmdContractBreakSeries);   // params { seriesId } — "My Contracts"
    core.Register("cmd.identity.submit",  &CmdIdentitySubmit);
    core.Register("cmd.negotiate.send",        &CmdNegoSend);         // negotiation redesign: thread — open/counter-offer
    core.Register("cmd.negotiate.acceptTable", &CmdNegoAcceptTable);  // signs whatever's on the table (base or counter)
    core.Register("cmd.negotiate.sign",        &CmdNegoSign);         // signs the ACCEPTED_PENDING deal
    core.Register("cmd.negotiate.walk",        &CmdNegoWalk);         // walk away from the thread
    core.Register("cmd.season.ackBonus",  &CmdSeasonAckBonus);
    core.Register("cmd.calendar.launch",  &CmdCalendarLaunch);
    core.Register("cmd.calendar.launchCalibration", &CmdCalendarLaunchCalibration);
    core.Register("cmd.calendar.skipCalibration", &CmdCalendarSkipCalibration);   //
    core.Register("cmd.event.arm",        &CmdEventArm);   // arms a special-event exhibition race
    core.Register("cmd.debug.setState",   &CmdDebugSetState);
    core.Register("cmd.debug.simWorld",   &CmdDebugSimWorld);   // simulate the world forward to a date
    core.Register("cmd.debug.simRace",    &CmdDebugSimRace);    // SIMULATION mode (debug menu)
    core.Register("cmd.debug.lowRam",     &CmdDebugLowRam);     // Experimental Low RAM Mode (toggle in the Play window)
    core.Register("cmd.debug.contentLog", &CmdDebugContentLog); // debug menu switch (Ctrl+Shift+D)
    core.Register("cmd.app.closing",      &CmdAppClosing);      // clean app shutdown (quiesce)
    core.Register("cmd.log.note",         &CmdLogNote);         // breadcrumbs from the app into the support log
    core.Register("cmd.app.lang",         &CmdAppLang);         // app language -> the engine's TR()
    core.Register("query.profiles",       &QueryProfiles);        // multi-profile
    core.Register("cmd.profile.create",   &CmdProfileCreate);
    core.Register("cmd.profile.select",   &CmdProfileSelect);
    core.Register("cmd.profile.delete",   &CmdProfileDelete);
    core.Register("cmd.prerace.buy",      &CmdPreraceBuy);
    core.Register("cmd.prerace.launch",   &CmdPreraceLaunch);
    core.Register("cmd.tryout.arm",       &CmdTryoutArm);
    core.Register("cmd.tryout.start",     &CmdTryoutStart);
    core.Register("cmd.tryout.clear",     &CmdTryoutClear);
    core.Register("cmd.rollover.confirm", &CmdRolloverConfirm);
    core.Register("cmd.entitlements.set", &CmdEntitlementsSet);   // Patreon-app integration: tier plumbing
    core.Register("query.entitlements",   &QueryEntitlements);
    core.Register("cmd.skins.status",     &CmdSkinsStatus);       // "real skins": detected vs recommended
    core.Register("cmd.skins.installed",  &CmdSkinsInstalled);    // "real skins" installed slots + preview_rel
}

} // namespace capi
