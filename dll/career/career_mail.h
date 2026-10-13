#pragma once
// Living mail: shared utilities for the mail channel — k=v
// percent-escaped params (the save's 'email' line is pipe-delimited), {key} interpolation over
// TR templates, deterministic variant selection (FNV-1a, NO rand), and credits formatting.
// Consumers: career_contract.cpp (generator), career_calendar.cpp (rollover), career_api_queries.cpp
// (QueryInbox), screen_inbox.cpp (ImGui rendering), and career_overlay.cpp (ticks + profile creation).
#include <string>
#include <vector>
#include <utility>

namespace career {

struct SaveData;   // fwd (career_mail.h doesn't include career_save.h; the .cpp does)
struct GeneratedEmail;  // fwd (career_mail.h doesn't include career_save.h; the .cpp does)
struct SpecialEventDef; // fwd (career_mail.h doesn't include career_events.h; the .cpp does) — EmitEventInviteMail
struct SeatWatch;       // fwd (same) — EmitSeatWatchMail

using MailParams = std::vector<std::pair<std::string, std::string>>;

std::string MailParamsEncode(const MailParams& p);   // "k=v;k2=v2", with % | ; = \r \n escaped as %XX
MailParams  MailParamsDecode(const std::string& s);  // tolerant: empty/malformed input -> empty/partial list

// Replaces {key}; a placeholder with no matching param gets REMOVED (release behavior — it never crashes).
std::string MailInterpolate(const std::string& text, const MailParams& p);

unsigned MailHash(const std::string& id);            // FNV-1a 32-bit (deterministic per-id variant)

// number of "_v0".."_v7" variants present in the TR table (0 = no variants -> the bare key).
int MailVariantCount(const std::string& payload_key);
// body key: "<key>_v<variant>" if it exists (clamped to count-1); if there are no variants, "<key>".
std::string MailBodyKey(const std::string& payload_key, int variant);
// subject key: "<key>_subj" if it exists; otherwise the body key (legacy subject==body behavior).
std::string MailSubjectKey(const std::string& payload_key, int variant);

std::string MailFmtCredits(long v);                  // "12.500 credits" (thousands separated with '.'; negative "-800 credits")
std::string MailFirstName(const std::string& full);  // first token ("Josh Bermudez" -> "Josh")
const char* MailManagerName();                       // "Atenea Montero" (the single source of truth for her canonical name)
const char* MailPressName();                          // "Dario Quesada" (the single source of truth for the journalist, PRESS family)

// Updates streaks and season counters after settling a race. bad = DNF or bottom third
// (pos*3 > 2*field). A midfield result resets BOTH streaks. Call BEFORE GenerateCareerEmails
// in the post-Settle tick. clean = no incident (a real settle signal, independent of dnf);
// consec_clean increases with !dnf && clean, and cuts to 0 on an incident or a DNF.
void MailNarrativeOnSettle(SaveData& save, int pos, int field, bool dnf, bool clean, const std::string& track);

// Living mail: the opening arc — Atenea introduces herself on day 0 of the career (ProfileOpCreate,
// "career" mode; the sandbox does NOT emit it). Emits "ob_intro" (family 1, subtype 6, sender Atenea) and arms
// mailnarr.onboard_stage=1 + season_start_credits=save.prog.credits (a snapshot for the season-end recap).
// Dedup the same way as the rest of the channel (same idiom as job_lost in career_calendar.cpp): a 2nd call doesn't
// duplicate it. Builds the GeneratedEmail directly (doesn't depend on career_market_data.h / EmailDef).
// year: fixes "mail with an absurd date across the year boundary" — the calendar's year AT EMIT time (GeneratedEmail::year).
void EmitOnboardIntro(SaveData& save, const std::string& playerFullName, int year, int day);

// Atenea introduces MEMORIES the first time a period series unlocks
// (an IsEraMemorySeries exists with level <= frontier_phase). Emits "mem_intro" (sender Atenea) ONCE
// (dedup by id). Internal GATE: if no period series is unlocked yet, it does nothing. Call it in
// the post-Settle tick (the phase may have advanced). Deterministic; no RNG.
void EmitMemoryIntro(SaveData& save, const std::string& playerFullName, int year, int day);

// Atenea's greeting on the player's BIRTHDAY. Triggered by AdvanceDays when it crosses
// save.player's (month, day) on the continuous clock. Emits "bday_<year>" (dedup by id: one per year), family 1
// (progress), params {player}+{age}. With no birth date in the save (birth_year/month/day 0), it does nothing.
void EmitBirthday(SaveData& save, const std::string& playerFullName, int year, int day);

// Inbox redesign: read/answer/prune mechanics ----
// An instant action a mail offers on its panel (a button). label_key/kind pass straight through to
// query.inbox's contract (actions[]); choice is the integer the app returns via cmd.inbox.answer.
struct MailAction { int choice; const char* label_key; const char* kind; };   // kind: "primary"|"danger"|"ghost"

const char* MailFamilyToken(int family);                    // stable token for the contract ("offer"|"progress"|…)
// A mail's instant actions. A live OFFER offers accept/decline (needs the save to check
// declined_offers/contracts). The table lives here so answer (validation) and query.inbox (serialization)
// share ONE source.
// `year`/`doy` = the CLOCK, optional. Needed for the last of the expiry checks (whether the market is
// CLOSED), because that one can't be read from the save. 0 = don't evaluate it — this way old callers and tests
// with no clock on hand still work, and the other two expiry checks (which do live in the save) still get
// applied the same way. See MailOfferExpired.
std::vector<MailAction> MailActionsFor(const GeneratedEmail& e, const SaveData& save,
                                       int year = 0, int doy = 0);

// Is this offer STILL on the table? Reported on Aug 9: the mail kept showing the accept button long
// after the offer had stopped being valid, so clicking it did nothing, with no explanation why. The
// engine ALREADY knew it had expired through three different paths, and the mail wasn't checking any of them:
//   1. `lapsed_offers`   — its 4-week window (bal::MK_OFFER_TTL_DAYS) ran out, sealed by LapseExpiredOffers.
//   2. `expired_offers`  — its season already started (you let it pass), sealed by the AdvanceWeek tick.
//   3. `withdrawn_offers`— the team withdrew it.
//   4. ...and the MARKET CLOSED. This one isn't in the save: the window runs from September 1 to December 21,
//      so for the other eight months of the year the button was promising a signature that MarketUiAccept was going to reject
//      anyway, due to its own scheduling lock. It needs the clock, and by far it's the longest stretch.
// Declining is NOT expiring (declining has its own outcome), and a RENEWAL never expires due to the market:
// it doesn't live there — it's signed on top of the current contract, and the final one arrives in January, with the market closed.
bool MailOfferExpired(const GeneratedEmail& e, const SaveData& save, int year, int doy);

// ...and WHY it expired, because it isn't all the same. "Your window ran out" is on you, and "the market is
// closed" is the calendar's doing — and that last one, on top of that, is worth waiting for: the market reopens in September.
// A gap where a button used to be that just says "expired" leaves the player not knowing whether to wait, whether they missed
// something, or whether the game is broken. Stable token for query.inbox's contract; 0 = the offer is still alive.
enum MailOfferExpiry {
    MAIL_OFFER_LIVE = 0,
    MAIL_OFFER_LAPSED = 1,          // the response window ran out (4-week TTL)
    MAIL_OFFER_SEASON_STARTED = 2,  // its season already started: you let it pass
    MAIL_OFFER_WITHDRAWN = 3,       // the team withdrew it (not on you or the clock)
    // the offer is from a WINDOW THAT'S ALREADY CLOSED (its year is earlier than the clock's). Watch the criterion: the
    // first version checked the market's PHASE ("it's closed today"), and that marked EVERY offer
    // pending during the eight months the market stays closed as expired — including one that had just arrived.
    // The player spotted it instantly: "now everything shows as expired". What kills an offer isn't that today
    // it can't be signed, it's that its window already passed, and that's known from the mail's YEAR, not the month.
    MAIL_OFFER_WINDOW_OVER = 4,
    // closed WITH NO decision: the sync (career_contract.cpp:1353) marks answered=1 and leaves choice at 0 when the
    // offer vanishes from the live market. That's not "answered", it's "it's over", and it was the case that left the
    // mail COMPLETELY SILENT: no buttons (because of answered), no outcome (MailResolvedKey only maps
    // choice 1/2), and no expiry notice (the answered guard was discarding it). Reported on Aug 9 with a
    // real Greywick Motors mail. It goes LAST: if the save kept a specific reason, that one wins.
    MAIL_OFFER_GONE = 5,
};
int MailOfferExpiryReason(const GeneratedEmail& e, const SaveData& save, int year, int doy);
const char* MailOfferExpiryToken(int reason);   // the contract's token ("lapsed"|…); nullptr if it's still alive
// TR key for the outcome paragraph of an answered mail (nullptr = nothing to show; varies by family).
const char* MailResolvedKey(const GeneratedEmail& e);
// pending? (unread, or an unanswered OFFER/decision). Single source of truth: pruning (career_contract) and
// the badge (overlay). Replaces the static mailActionablePending in career_contract.cpp.
bool MailActionablePending(const GeneratedEmail& e, const SaveData& save);
bool MailMarkRead(SaveData& save, const std::string& id);   // true if it changed (the caller persists it)
// 18/09: marks ALL the mails as read and returns how many changed (0 = nothing to save; the caller
// persists ONCE). It exists because marking one by one from the app would save the game once per mail.
int  MailMarkAllRead(SaveData& save);
// (cmd.inbox.answer): MailAnswer split into Check (pure validation) + Commit (write)
// so the caller can interleave the EFFECT (accepting/declining in the market) between the two — the effect
// mutates the state validation reads (active contract / declined_offers), so validating AFTER the
// effect always returned not_answerable on the success path.
// Validation ONLY, const, no side effects. Same codes/guard order as MailAnswer (found ->
// answered -> answerable -> choice): 0 ok / 1 not_found / 2 already_answered / 3 not_answerable / 4 bad_choice.
// year/doy: the clock, optional, with the same criterion as MailActionsFor (0 = don't evaluate the
// market closing). It lives here so validating and rendering use THE SAME gate and can't disagree.
int MailAnswerCheck(const SaveData& save, const std::string& id, int choice, int year = 0, int doy = 0);
// Marks answered=1 + choice on the mail. Validates NOTHING (doesn't even re-check MailActionsFor):
// call it ONLY after a MailAnswerCheck==0, with the effect ALREADY applied. false only if the id doesn't exist.
bool MailAnswerCommit(SaveData& save, const std::string& id, int choice);
// Convenience = Check immediately followed by Commit (no effect in between). The existing tests
// still use this form; cmd.inbox.answer in career_overlay.cpp uses Check/Commit separately.
// 0 ok / 1 not_found / 2 already_answered / 3 not_answerable / 4 bad_choice (fixed guard order).
int MailAnswer(SaveData& save, const std::string& id, int choice);
int MailPendingDecisions(const SaveData& save);             // badge: unread + pending decisions

// Effect of the chosen TONE in a press interview (family PRESS, subtype 0..3). The overlay
// (cmd.inbox.answer) calls it as a SIBLING branch to the family==0 effect, BEFORE MailAnswerCommit (same idiom:
// effect first, commit after). choice 1=humble 2=spicy 3=institutional (outside that range, no-op).
// Mutates save.prog.rep (floor 0) + save.rel_marks/save.beef depending on the tone; NEVER touches save.prog.rating (the ELO
// sporting rating is zero-sum against the real world, indifferent to what you say in a press conference). Returns 0.
int ApplyPressAnswer(SaveData& save, const GeneratedEmail& mail, int choice);

// Is there ALREADY an unanswered press interview? (family 6, subtype 0..3 = the interviews with
// a tone; the RANKING subtype is excluded, it's not a question to answer). Hard rule from the redesign: at most ONE
// pending interview at a time — EVERY interview emitter (settle + rollover) checks it before
// emit; if one is already pending, the new one is dropped, not deferred. PURE (only reads save.emails).
bool PressInterviewPending(const SaveData& save);

// Inbox redesign: INVITE family (7) — EVENT invitations (subtype 1) and
// one-off SUBSTITUTE invitations (subtype 0) ----

// Amended: a PUSH invitation for a special event already loaded in the registry (career_events.h,
// SetSpecialEventRegistry — lives only in the overlay, so the CALLER iterates the registry and calls this per
// event. id "evinv_<eventId>" -> stable dedup (one invitation per event, never repeats). family 7,
// subtype 1, payload_key "invite_event", tag 2 (OFFICIAL voice: "Career Engine Events" is not your manager or a
// team). title = the title already localized AT EMIT time (GetLang(): 0=en/1=es over SpecialEventDef.title_en/es) —
// it's EXTERNAL data from the backend (special_events.json), not a translation key, so it's resolved once here and
// persisted as a param (not re-localized on read, unlike the rest of the channel). ADDITIVE params
// event_id/ends_at (the template doesn't use them — MailInterpolate ignores unmatched keys): they're consumed
// by query.inbox (eventId) and the expiry sync (ends_at, real clock-driven expiry). Deterministic (variant
// picked by hash of id+player, no RNG, same idiom as EmitBirthday).
// year: fixes the "mail with an absurd date across a year boundary" bug — the calendar year AT EMIT time (GeneratedEmail::year);
// 0 = unknown (e.g. boot before the calendar is loaded) -> the API emitter falls back to cal.year.
void EmitEventInviteMail(SaveData& save, const SpecialEventDef& ev, int year, int day, const std::string& playerFullName);

// — the team ANSWERS you. The trial used to close silently: the panel just vanished, so
// you'd spend the whole period never finding out whether you got the seat or lost it. A
// process that never tells you how it ended isn't a process. Dedup by (seat, year): one mail per contest.
// `won` picks the template and subject; with no sealed rival the variant that omits one is used (never invented).
void EmitSeatWatchMail(SaveData& save, const SeatWatch& w, const std::string& seriesDisplay, bool won,
                       int year, int day, const std::string& playerFullName);

// Which variant applies: v0 is the ONLY one that doesn't name the rival, so with no sealed rival it's mandatory. With a rival
// it's split among the others. Kept separate from EmitSeatWatchMail because MailVariantCount needs the templates
// loaded (the test harness doesn't have them), and this rule can stand on its own.
int SeatWatchMailVariant(int variantCount, bool hasRival, const std::string& mailId);

// Effect of answering a SUBSTITUTE invitation (family 7, subtype 0). The overlay
// (cmd.inbox.answer) calls it as a SIBLING branch of ApplyPressAnswer, BEFORE MailAnswerCommit. The mail does NOT
// carry the absday in its params (only team/series/track/fee) — the match is verified by rebuilding the exact id
// that SubRideWeeklyDraw emitted ("subride_"+series_id+"_"+absday) from save.sub_ride and comparing it against
// mail.id: if it does NOT match (sub_ride already empty/resolved another way, or an old mail from a substitution already
// closed) it's a defensive no-op — state is NEVER overwritten blind. choice 1=accept (state 1->2, the
// guest race becomes ARMABLE); choice 2=decline (sub_ride resets to {} = state 0, releasing the "one
// substitution at a time" gate). Returns 0.
int ApplySubRideAnswer(SaveData& save, const GeneratedEmail& mail, int choice);

// PURE settlement of the SUBSTITUTE race — called by the standalone branch of the settle step (overlay) with
// the real finishing position/grid of the race just run. Returns the FEE to credit ONCE (save.sub_ride.fee,
// frozen when offered; 0 if there is NO ACCEPTED substitution, i.e. state != 2). Records the team's WARMTH
// signal covered (a rel_mark toward save.sub_ride.team_label, same scale/idiom as ApplyPressAnswer)
// when you finish in the FRONT HALF (pos*2 <= field; pos<=0 = DNF -> no warmth). The fee does NOT depend on the result
// (the commitment is to race, not to win). Consumes the substitution (save.sub_ride = {}) -> a second settle returns 0 (no
// double-paying). SELF-CONTAINED: the team_label travels inside sub_ride itself, so it's testable without the overlay.
long SubRideSettle(SaveData& save, int pos, int field);

} // namespace career
