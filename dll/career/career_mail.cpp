#include "career_mail.h"
#include "career_save.h"     // Full SaveData (career_mail.h only forward-declares it)
#include "career_series.h"   // AllSeries + IsEraMemorySeries (the memories-mail gate)
#include "career_balance.h"  // MK_MAIL_SUBTYPE_COURTING
#include "career_market.h"   // offer expiry: market::MarketPhaseFor/MARKET_PADDOCK (is the market closed?)
#include "career_calendar.h" // offer expiry: career::kStartYear (year 1's compressed cycle)
#include <ctime>             // event invitation: derive the year from ev.ends_at when there's no calendar
#include "career_events.h"   // Full SpecialEventDef (career_mail.h only forward-declares it)
#include "strings.h"
#include <cstdio>

namespace career {

static bool mailReserved(char c) { return c=='%' || c=='|' || c==';' || c=='=' || c=='\r' || c=='\n'; }

std::string MailParamsEncode(const MailParams& p) {
    std::string out;
    auto esc = [&](const std::string& s) {
        for (char c : s) {
            if (mailReserved(c)) { char b[4]; snprintf(b, sizeof(b), "%%%02X", (unsigned char)c); out += b; }
            else out += c;
        }
    };
    for (size_t i = 0; i < p.size(); i++) {
        if (i) out += ';';
        esc(p[i].first); out += '='; esc(p[i].second);
    }
    return out;
}

static std::string mailUnesc(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {                                          // %XX
            auto hex = [](char c)->int { if (c>='0'&&c<='9') return c-'0'; if (c>='A'&&c<='F') return c-'A'+10; if (c>='a'&&c<='f') return c-'a'+10; return -1; };
            int h = hex(s[i+1]), l = hex(s[i+2]);
            if (h >= 0 && l >= 0) { out += (char)(h*16 + l); i += 2; continue; }
        }
        out += s[i];
    }
    return out;
}

MailParams MailParamsDecode(const std::string& s) {
    MailParams out;
    std::string k, v; bool inVal = false;
    auto flush = [&]() { if (!k.empty()) out.push_back({ mailUnesc(k), mailUnesc(v) }); k.clear(); v.clear(); inVal = false; };
    for (char c : s) {
        if (c == ';') flush();
        else if (c == '=' && !inVal) inVal = true;
        else (inVal ? v : k) += c;
    }
    flush();
    return out;
}

std::string MailInterpolate(const std::string& text, const MailParams& p) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '{') {
            size_t close = text.find('}', i);
            if (close != std::string::npos) {
                std::string key = text.substr(i + 1, close - i - 1);
                std::string val;                      // no param -> gets removed (never crashes)
                for (const auto& kv : p) if (kv.first == key) { val = kv.second; break; }
                out += val; i = close; continue;
            }
        }
        out += text[i];
    }
    return out;
}

unsigned MailHash(const std::string& id) {           // FNV-1a 32-bit
    unsigned h = 2166136261u;
    for (unsigned char c : id) { h ^= c; h *= 16777619u; }
    return h;
}

int MailVariantCount(const std::string& payload_key) {
    int n = 0;
    for (int i = 0; i < 8; i++) {
        char buf[160]; snprintf(buf, sizeof(buf), "%s_v%d", payload_key.c_str(), i);
        if (!TRHas(buf)) break;
        n++;
    }
    return n;
}

std::string MailBodyKey(const std::string& payload_key, int variant) {
    int n = MailVariantCount(payload_key);
    if (n <= 0) return payload_key;                   // no variants -> the bare key (old saves)
    if (variant < 0) variant = 0;
    if (variant >= n) variant = n - 1;                // clamp (for a save with variant >= count)
    char buf[160]; snprintf(buf, sizeof(buf), "%s_v%d", payload_key.c_str(), variant);
    return buf;
}

std::string MailSubjectKey(const std::string& payload_key, int variant) {
    std::string sk = payload_key + "_subj";
    if (TRHas(sk.c_str())) return sk;
    return MailBodyKey(payload_key, variant);
}

std::string MailFmtCredits(long v) {
    bool neg = v < 0; unsigned long a = neg ? (unsigned long)(-v) : (unsigned long)v;
    char raw[32]; snprintf(raw, sizeof(raw), "%lu", a);
    std::string digits(raw), grouped;
    int cnt = 0;
    for (int i = (int)digits.size() - 1; i >= 0; i--) {
        grouped.insert(grouped.begin(), digits[i]);
        if (++cnt == 3 && i > 0) { grouped.insert(grouped.begin(), '.'); cnt = 0; }
    }
    return (neg ? std::string("-\xE2\x82\xA1 ") : std::string("\xE2\x82\xA1 ")) + grouped;
}

std::string MailFirstName(const std::string& full) {
    size_t sp = full.find(' ');
    return sp == std::string::npos ? full : full.substr(0, sp);
}

const char* MailManagerName() { return "Atenea Montero"; }
const char* MailPressName() { return "Dario Quesada"; }

void MailNarrativeOnSettle(SaveData& save, int pos, int field, bool dnf, bool clean, const std::string& track) {
    MailNarrative& n = save.mailnarr;
    bool podium = !dnf && pos >= 1 && pos <= 3;
    bool bad    = dnf || (field > 0 && pos * 3 > 2 * field);   // DNF or bottom third of the grid
    if (podium) {
        n.consec_podiums++; n.consec_bottom = 0; n.last_notable_track = track;
        n.season_podiums++; if (pos == 1) n.season_wins++;
    } else if (bad) {
        n.consec_bottom++; n.consec_podiums = 0;
    } else {
        n.consec_podiums = 0; n.consec_bottom = 0;             // midfield: resets both
    }
    // Clean-race streak — increases with a clean, finished race, breaks with an
    // incident or a DNF. Independent of podium/bottom-third (a clean race in P10 DOES count).
    if (!dnf && clean) n.consec_clean++; else n.consec_clean = 0;
}

void EmitOnboardIntro(SaveData& save, const std::string& playerFullName, int year, int day) {
    for (const auto& e : save.emails) if (e.id == "ob_intro") return;      // dedup (job_lost's own vocabulary)
    GeneratedEmail ge;
    ge.id = "ob_intro"; ge.tag = 1; ge.day = day; ge.year = year; ge.family = 1; ge.subtype = 6;
    ge.payload_key = "ob_intro";
    int vc = MailVariantCount("ob_intro");
    ge.variant = vc > 0 ? (int)(MailHash(ge.id + playerFullName) % (unsigned)vc) : 0;  // varies by race/name
    MailParams p; std::string pf = MailFirstName(playerFullName);
    if (!pf.empty()) p.push_back({"player", pf});
    ge.params = MailParamsEncode(p);
    ge.from_name = MailManagerName(); ge.from_role_key = "inbox_role_manager";
    save.emails.push_back(ge);
    save.mailnarr.onboard_stage = 1;
    save.mailnarr.season_start_credits = save.prog.credits;
}

void EmitMemoryIntro(SaveData& save, const std::string& playerFullName, int year, int day) {
    for (const auto& e : save.emails) if (e.id == "mem_intro") return;      // dedup (once per race)
    // GATE: once the player already owns a car from some period series (Memories become accessible via purchase/gift).
    bool anyEraOwned = false;
    for (const auto& sd : AllSeries())
        if (IsEraMemorySeries(sd) && OwnsCarOfClass(save, sd.class_name)) { anyEraOwned = true; break; }
    if (!anyEraOwned) return;
    GeneratedEmail ge;
    ge.id = "mem_intro"; ge.tag = 1; ge.day = day; ge.year = year; ge.family = 1; ge.subtype = 6;
    ge.payload_key = "mem_intro";
    int vc = MailVariantCount("mem_intro");
    ge.variant = vc > 0 ? (int)(MailHash(ge.id + playerFullName) % (unsigned)vc) : 0;
    MailParams p; std::string pf = MailFirstName(playerFullName);
    if (!pf.empty()) p.push_back({"player", pf});
    ge.params = MailParamsEncode(p);
    ge.from_name = MailManagerName(); ge.from_role_key = "inbox_role_manager";
    save.emails.push_back(ge);
}

// birthday greeting. Id "bday_<year>" -> exactly one per clock year (dedup),
// even if AdvanceDays crosses the date in several jumps or the tick repeats. Deterministic, no RNG.
void EmitBirthday(SaveData& save, const std::string& playerFullName, int year, int day) {
    if (save.player.birth_year == 0) return;                       // profile has no birth date
    char id[32]; snprintf(id, sizeof(id), "bday_%d", year);
    for (const auto& e : save.emails) if (e.id == id) return;      // already greeted this year
    GeneratedEmail ge;
    ge.id = id; ge.tag = 1; ge.day = day; ge.year = year; ge.family = 1; ge.subtype = 6;
    ge.payload_key = "mail_bday";
    int vc = MailVariantCount("mail_bday");
    ge.variant = vc > 0 ? (int)(MailHash(ge.id + playerFullName) % (unsigned)vc) : 0;   // varies by year/race
    MailParams p; std::string pf = MailFirstName(playerFullName);
    if (!pf.empty()) p.push_back({"player", pf});
    int age = year - save.player.birth_year;
    if (age > 0) p.push_back({"age", std::to_string(age)});
    ge.params = MailParamsEncode(p);
    ge.from_name = MailManagerName(); ge.from_role_key = "inbox_role_manager";
    save.emails.push_back(ge);
}

// Inbox redesign ----
const char* MailFamilyToken(int family) {
    switch (family) {
        case 0: return "offer";  case 1: return "progress"; case 2: return "sponsor";
        case 3: return "nego";   case 4: return "intel";    case 5: return "rival";
        case 6: return "press";  case 7: return "invite";   case 8: return "teamops";
    }
    return "info";
}
// see career_mail.h. The expiry the mail system wasn't checking.
int MailOfferExpiryReason(const GeneratedEmail& e, const SaveData& save, int year, int doy) {
    if (e.family != 0) return MAIL_OFFER_LIVE;                        // only the OFFER family expires this way
    if (e.subtype == bal::MK_MAIL_SUBTYPE_COURTING) return MAIL_OFFER_LIVE;  // the courting offer doesn't get signed
    // A REAL decision (1 accept / 2 reject) has its own outcome and isn't an expiry. But `answered`
    // with choice 0 it's NOT a decision: it's the sync closing an offer that vanished from the market (see
    // MAIL_OFFER_GONE). That case still gets evaluated below, or the mail would end up saying nothing at all.
    if (e.answered && e.choice != 0) return MAIL_OFFER_LIVE;
    if (e.payload_key.empty()) return MAIL_OFFER_LIVE;                // legacy, with no offer behind it
    if (save.declined_offers.count(e.payload_key)) return MAIL_OFFER_LIVE;   // declining is NOT expiring
    // A RENEWAL doesn't live in the market: it's signed on top of the current contract (the early one, "#early") or
    // it arrives in January after the rollover, with the market CLOSED. Applying the expiry logic below to it would remove the
    // button at the one moment it can actually be used. Its lifecycle is closed by answered/CloseStaleRenewalMails.
    if (e.id.rfind("renew_", 0) == 0) return MAIL_OFFER_LIVE;
    if (e.payload_key.find("#early") != std::string::npos) return MAIL_OFFER_LIVE;
    // 1-3: the three the engine already wrote to the save when the clock ticked.
    if (save.lapsed_offers.count(e.payload_key))    return MAIL_OFFER_LAPSED;         // the deadline ran out
    if (save.expired_offers.count(e.payload_key))   return MAIL_OFFER_SEASON_STARTED; // their season started
    if (save.withdrawn_offers.count(e.payload_key)) return MAIL_OFFER_WITHDRAWN;      // the team withdrew it
    // 4: its window already closed — the mail is from a season BEFORE the clock's current one. The YEAR is compared, and
    // not the phase: an offer doesn't die because it's February today, it dies because its window passed. (The first
    // version of this checked the phase and marked any offer pending during the eight months of
    // closed market as expired, freshly-arrived ones included.) `doy` no longer factors in here; it's kept in the signature
    // because it's part of the clock all these gates receive.
    (void)doy;
    if (e.year > 0 && year > 0 && e.year < year) return MAIL_OFFER_WINDOW_OVER;
    // 5: closed by the sync without you ever deciding. It's the last resort on purpose: the reasons
    // above explain what happened BETTER, and the rollover clears its sets every year (declined/expired/lapsed/
    // withdrawn/offer_first_seen), so a mail that survives the year change loses its specific reason
    // and only this is left. Without this case, that mail would stay silent forever.
    if (e.answered && e.choice == 0) return MAIL_OFFER_GONE;
    return MAIL_OFFER_LIVE;
}
const char* MailOfferExpiryToken(int reason) {
    switch (reason) {
        case MAIL_OFFER_LAPSED:         return "lapsed";
        case MAIL_OFFER_SEASON_STARTED: return "season_started";
        case MAIL_OFFER_WITHDRAWN:      return "withdrawn";
        case MAIL_OFFER_WINDOW_OVER:    return "window_over";
        case MAIL_OFFER_GONE:           return "gone";
        default:                        return nullptr;   // alive: nothing to report
    }
}
bool MailOfferExpired(const GeneratedEmail& e, const SaveData& save, int year, int doy) {
    return MailOfferExpiryReason(e, save, year, doy) != MAIL_OFFER_LIVE;
}

std::vector<MailAction> MailActionsFor(const GeneratedEmail& e, const SaveData& save, int year, int doy) {
    // A mail that's already been answered never offers buttons again (a guard shared by ALL families).
    if (e.answered) return {};
    if (e.family == 0) {
        // The OFFER family offers instant actions. Courting (subtype 10), legacy with no
        // payload_key, rejected in the Market, or with a contract already signed in that series -> nothing.
        if (e.subtype == bal::MK_MAIL_SUBTYPE_COURTING) return {};
        if (e.payload_key.empty()) return {};
        if (save.declined_offers.count(e.payload_key)) return {};
        // ...and an offer that's no longer on the table doesn't offer buttons either. Without this the mail kept
        // showing "Accept" indefinitely on expired offers: clicking it did nothing (the executor returns
        // not_answerable and MarketUiAccept rejects it outside the window) and the player had no way to know why.
        if (MailOfferExpired(e, save, year, doy)) return {};
        std::string sid = (e.payload_key.rfind("of_", 0) == 0) ? e.payload_key.substr(3) : e.payload_key;
        // 0.4.0 rival offers: "of_<serie>#rv<i>" -> signing ANY contract in the series switches off the
        // actions on ALL its offers (renewal + rival bids); same cutoff as seriesIdOfOffer.
        // THE "#early" EXCEPTION (silly season): the EARLY renewal is an offer ON TOP OF the current contract
        // (extending it), so the active contract can't switch it off — its lifecycle is closed by `answered` (on signing/
        // declining) or CloseStaleRenewalMails (at rollover).
        size_t h = sid.find('#');
        bool early = false;
        if (h != std::string::npos) {
            early = sid.compare(h + 1, 5, "early") == 0;
            sid = sid.substr(0, h);
        }
        if (!early && ContractForSeries(save, sid)) return {};
        // tryout: no direct signing (the trial run governs); it can still be declined.
        bool tryout = false;
        for (const auto& kv : MailParamsDecode(e.params)) if (kv.first == "tryout" && kv.second == "1") tryout = true;
        std::vector<MailAction> acts;
        if (!tryout) acts.push_back({1, "inbox_act_accept", "primary"});
        acts.push_back({2, "inbox_act_decline", "danger"});
        return acts;
    }
    // The PRESS family (interviews, subtype 0..3) offers 3 response TONES. The
    // RANKING subtype (a preseason piece, with no question to answer) falls into the "everything else -> {}" case below.
    if (e.family == 6 && e.subtype <= 3) {
        return {{1, "press_tone_humble", "primary"}, {2, "press_tone_spicy", "danger"}, {3, "press_tone_safe", "ghost"}};
    }
    // The INVITE family (7) offers 2 decisions per subtype. sub (0) reuses the same accept/decline labels
    // (same verb, the mail is from the main team offering the seat). event (1) ONLY offers
    // Ignore — "accept" is never a mail button here: the CTA that sets up the event opens
    // EventPrerace (picking class/car needs that screen, not an instant decision on this channel).
    if (e.family == 7) {
        if (e.subtype == 0) return {{1, "inbox_act_accept", "primary"}, {2, "inbox_act_decline", "danger"}};
        if (e.subtype == 1) return {{2, "inbox_act_ignore", "ghost"}};
        return {};
    }
    return {};
}
const char* MailResolvedKey(const GeneratedEmail& e) {
    if (e.family == 0 && e.subtype != bal::MK_MAIL_SUBTYPE_COURTING && e.answered) {
        if (e.choice == 1) return "inbox_res_offer_accepted";
        if (e.choice == 2) return "inbox_res_offer_declined";
    }
    // Interview answered — closes out the published piece based on the chosen tone.
    if (e.family == 6 && e.subtype <= 3 && e.answered) {
        if (e.choice == 1) return "press_res_humble";
        if (e.choice == 2) return "press_res_spicy";
        if (e.choice == 3) return "press_res_safe";
    }
    // Outcome of an answered invitation — sub accept/decline; event only has Ignored (choice 2).
    // choice 0 (closed by expiry, the expirations sync) falls to the nullptr case below, same idiom as OFFER/PRESS.
    if (e.family == 7 && e.answered) {
        if (e.subtype == 0) {
            if (e.choice == 1) return "invite_sub_res_accept";
            if (e.choice == 2) return "invite_sub_res_decline";
        } else if (e.subtype == 1 && e.choice == 2) {
            return "invite_event_res_ignored";
        }
    }
    return nullptr;   // resolved by sync (choice 0), and every other family: no outcome
}
bool MailActionablePending(const GeneratedEmail& e, const SaveData& save) {
    if (!e.read) return true;                        // unread -> pending, never pruned
    // (phantom badge): a READ event INVITATION (family 7 subtype 1) stops counting as a
    // pending decision — there's nothing to accept in the mail (racing happens from the hub/EventPrerace), and
    // its only button is "Ignore", so counting it until it expired was inflating the badge with notices already seen
    // (4 of the 9 "pending" in the real case; two from events ALREADY run). The button is still there (MailActionsFor)
    // and expiry/claim is still closed out by the invite sync with choice 0 on the next tick.
    if (e.family == 7 && e.subtype == 1) return false;
    if (e.family == 0 && !e.answered) return true;   // unanswered OFFER -> actionable (legacy guard)
    if (!e.answered && !MailActionsFor(e, save).empty()) return true;   // unanswered decision
    return false;
}
bool MailMarkRead(SaveData& save, const std::string& id) {
    for (auto& e : save.emails) if (e.id == id) { if (e.read) return false; e.read = 1; return true; }
    return false;
}
int MailMarkAllRead(SaveData& save) {
    int n = 0;
    for (auto& e : save.emails) if (!e.read) { e.read = 1; n++; }
    return n;
}
int MailAnswerCheck(const SaveData& save, const std::string& id, int choice, int year, int doy) {
    for (const auto& e : save.emails) {
        if (e.id != id) continue;
        if (e.answered) return 2;
        // the clock is threaded all the way here so validation and what gets rendered are THE SAME gate: if the
        // mail no longer shows buttons due to expiry, answering it has to return not_answerable, not slip through.
        std::vector<MailAction> acts = MailActionsFor(e, save, year, doy);
        if (acts.empty()) return 3;
        for (const auto& a : acts) if (a.choice == choice) return 0;
        return 4;
    }
    return 1;
}
bool MailAnswerCommit(SaveData& save, const std::string& id, int choice) {
    for (auto& e : save.emails) if (e.id == id) { e.answered = 1; e.choice = choice; return true; }
    return false;
}
int MailAnswer(SaveData& save, const std::string& id, int choice) {
    int rc = MailAnswerCheck(save, id, choice);
    if (rc != 0) return rc;
    MailAnswerCommit(save, id, choice);
    return 0;
}
int MailPendingDecisions(const SaveData& save) {
    int n = 0;
    for (const auto& e : save.emails) if (MailActionablePending(e, save)) n++;
    return n;
}

// Effect of the chosen tone in a press interview. params get decoded ONCE (the mail
// carries them exactly as the sender left them: {team} from the contract active at question time, {rival_id}/{rival_team}
// if the question is about a specific rival). REP uses the SAME vocabulary as the rest of the engine (rep +=; floor
// 0), and it's NEVER logged through EconLog (it isn't money). The sporting rating is NEVER touched: it's the zero-sum scale
// against the real world, and a press conference doesn't win or lose races.
int ApplyPressAnswer(SaveData& save, const GeneratedEmail& mail, int choice) {
    MailParams p = MailParamsDecode(mail.params);
    std::string team, rivalId, rivalTeam;
    for (const auto& kv : p) {
        if (kv.first == "team") team = kv.second;
        else if (kv.first == "rival_id") rivalId = kv.second;
        else if (kv.first == "rival_team") rivalTeam = kv.second;
    }
    if (choice == 1) {                                          // humble
        save.prog.rep += bal::MK_PRESS_REP_HUMBLE;
        if (save.prog.rep < 0) save.prog.rep = 0;
        if (!team.empty())
            save.rel_marks.push_back({team, save.current_world_year, bal::MK_PRESS_MARK_X1000});
    } else if (choice == 2) {                                    // spicy
        save.prog.rep += bal::MK_PRESS_REP_SPICY;
        if (save.prog.rep < 0) save.prog.rep = 0;
        if (!rivalId.empty()) {
            Beef& bf = save.beef[rivalId];
            bf.level = (bf.level < 5) ? bf.level + 1 : 5;        // cap of 5 (same idiom as the on-track rivalry)
            bf.last_year = save.current_world_year;
        }
        if (!rivalTeam.empty())
            save.rel_marks.push_back({rivalTeam, save.current_world_year, -bal::MK_PRESS_MARK_X1000});
    } else if (choice == 3) {                                    // institutional (textbook response)
        save.prog.rep += bal::MK_PRESS_REP_SAFE;
        if (save.prog.rep < 0) save.prog.rep = 0;
    }
    return 0;
}

// One-interview-at-a-time rule. subtype <= 3 = interviews (0 firstwin / 1 titlefight /
// 2 titlewon / 3 joblost); the RANKING subtype (10) does NOT count (it's not a question). !answered = still pending.
bool PressInterviewPending(const SaveData& save) {
    for (const auto& e : save.emails)
        if (e.family == 6 && e.subtype <= 3 && !e.answered) return true;
    return false;
}

// PUSH announcement for a special event — see the full contract in the .h file. The
// event registry only lives in the overlay (career_events.cpp, static g_events), so the CALLER (career_overlay.cpp,
// after SetSpecialEventRegistry) iterates the registry and calls this per event; this function is PURE (no globals
// from the game, no registry) so the test harness can exercise it directly with a synthetic SpecialEventDef.
// v0 is the ONLY one that doesn't name the rival. With no rival locked in, it's forced: filling the gap with an
// invented name, or leaving the placeholder empty, would be worse than not mentioning it at all.
int SeatWatchMailVariant(int variantCount, bool hasRival, const std::string& mailId) {
    if (!hasRival || variantCount <= 1) return 0;
    return 1 + (int)(MailHash(mailId) % (unsigned)(variantCount - 1));
}

void EmitSeatWatchMail(SaveData& save, const SeatWatch& w, const std::string& seriesDisplay, bool won,
                       int year, int day, const std::string& playerFullName) {
    if (w.series_id.empty() || w.team_label.empty()) return;
    const std::string id = "sw_" + std::string(won ? "win_" : "lose_") + w.series_id + "_" + w.team_label
                         + "_" + std::to_string(year);
    for (const auto& e : save.emails) if (e.id == id) return;       // dedup: one dispute, one mail
    const std::string key = won ? "seatwatch_won" : "seatwatch_lost";
    GeneratedEmail ge;
    ge.id = id; ge.tag = 1; ge.day = day; ge.year = year;
    ge.family = 1;                                                   // PROGRESS: it's a news item, not an offer
    ge.subtype = won ? 13 : 14;
    ge.payload_key = key;
    ge.variant = SeatWatchMailVariant(MailVariantCount(key), !w.rival_name.empty(), id);
    MailParams p;
    std::string pf = MailFirstName(playerFullName);
    if (!pf.empty()) p.push_back({"player", pf});
    p.push_back({"team", w.team_label});
    p.push_back({"series", seriesDisplay.empty() ? w.series_id : seriesDisplay});
    if (!w.rival_name.empty()) p.push_back({"rival", w.rival_name});
    ge.params = MailParamsEncode(p);
    ge.from_name = w.team_label; ge.from_role_key = "inbox_role_teamprincipal";
    save.emails.push_back(ge);
}

void EmitEventInviteMail(SaveData& save, const SpecialEventDef& ev, int year, int day, const std::string& playerFullName) {
    std::string id = "evinv_" + ev.id;
    for (const auto& e : save.emails) if (e.id == id) return;      // dedup (one invitation per event, never repeats)
    // the title is ALREADY localized to the ACTIVE language (GetLang) at EMIT time — external backend data, not a TR key. If the
    // active language comes back empty in the data (incomplete file), it falls back to the other language rather than leaving it blank.
    std::string title = (GetLang() == 1) ? ev.title_es : ev.title_en;
    if (title.empty()) title = (GetLang() == 1) ? ev.title_en : ev.title_es;
    // a mail's date can't AGE ON ITS OWN. On the boot path the calendar isn't loaded yet, and
    // the caller knowingly passes year=0 (career_overlay.cpp), counting on the API falling back to "the
    // current year". But that fallback gets re-evaluated EVERY TIME the inbox is served: an invitation emitted in 2026
    // reads as 2027 in 2027 and 2028 in 2028. Reported on Aug 9 with a mail from a July 2026 event that
    // showed up as 2027. The calendar isn't needed to know the year: the event carries its own real date
    // (ends_at, UTC epoch), the same one that governs its expiry. It's only used as a fallback — a valid
    // year from the caller always wins.
    if (year <= 0 && ev.ends_at > 0) {
        time_t t = (time_t)ev.ends_at; struct tm g{};
        if (gmtime_s(&g, &t) == 0) year = g.tm_year + 1900;
    }
    GeneratedEmail ge;
    ge.id = id; ge.tag = 2; ge.day = day; ge.year = year; ge.family = 7; ge.subtype = 1;
    ge.payload_key = "invite_event";
    int vc = MailVariantCount("invite_event");
    ge.variant = vc > 0 ? (int)(MailHash(id + playerFullName) % (unsigned)vc) : 0;
    MailParams p;
    std::string pf = MailFirstName(playerFullName);
    if (!pf.empty()) p.push_back({"player", pf});
    if (!title.empty()) p.push_back({"title", title});
    p.push_back({"reward", MailFmtCredits(ev.reward_credits)});
    // ADDITIVE fields (the template doesn't use them, MailInterpolate ignores keys with no match in the text): event_id is
    // consumed by query.inbox (the "eventId" field); ends_at (the real UTC epoch) is consumed by the expirations sync and the
    // deadlineWeeks calculation — an event invitation's expiry is the SAME real window as the event itself,
    // not a made-up mail TTL.
    p.push_back({"event_id", ev.id});
    p.push_back({"ends_at", std::to_string(ev.ends_at)});
    ge.params = MailParamsEncode(p);
    ge.from_name = "Career Engine Events"; ge.from_role_key = "inbox_role_organizer";
    save.emails.push_back(ge);
}

// See the full contract in the .h file. PURE except for mutating save.sub_ride (no I/O, no registry).
int ApplySubRideAnswer(SaveData& save, const GeneratedEmail& mail, int choice) {
    if (save.sub_ride.state != 1) return 0;   // no ACTIVE offer to resolve (already accepted/declined/expired) -> no-op
    std::string expectedId = "subride_" + save.sub_ride.series_id + "_" + std::to_string(save.sub_ride.event_absday);
    if (mail.id != expectedId) return 0;      // this mail isn't the active substitution invitation -> defensive no-op
    if (choice == 1) save.sub_ride.state = 2;             // accept -> ARMED (sets up the guest-drive race)
    else if (choice == 2) save.sub_ride = SubRide{};       // decline -> releases the "one substitution at a time" gate
    return 0;
}

// See the full contract in the .h file. PURE except for mutating save (rel_marks + sub_ride); no I/O.
long SubRideSettle(SaveData& save, int pos, int field) {
    if (save.sub_ride.state != 2) return 0;               // only an ACCEPTED substitution pays out (never one merely offered/empty)
    long fee = save.sub_ride.fee;                          // frozen at offer time; doesn't depend on the outcome
    // warmth toward the team you're standing in for: finishing in the front half leaves a good impression (rel_mark +MK_PRESS_MARK,
    // same scale/annual decay as press does in ApplyPressAnswer). pos<=0 = DNF -> the pos>0 guard denies it.
    if (pos > 0 && pos * 2 <= field && !save.sub_ride.team_label.empty())
        save.rel_marks.push_back({ save.sub_ride.team_label, save.current_world_year, bal::MK_PRESS_MARK_X1000 });
    save.sub_ride = SubRide{};                             // consumed -> a 2nd settle returns 0 (no double payout)
    return fee;
}

} // namespace career
