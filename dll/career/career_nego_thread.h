#pragma once
// career_nego_thread — the negotiation redesign: the negotiation thread's asynchronous
// state machine. PURE wherever possible (importance, personality, window, resolution); mutations
// onto SaveData live in the .cpp, split the same way as career_contract. No ImGui, no game.
#include <string>
#include <vector>
#include "career_save.h"
#include "career_negotiate.h"
#include "career_market_data.h"
#include "career_mail.h"        // MailParams (EmitNegoMail)
#include "career_dateutil.h"    // NegoAbsDay seam (AbsDayFromYearDoy)

namespace market { struct GenOffer; }   // forward decl: career_market.h (with ImGui/color) stays only in the .cpp

namespace career {

// ----
int NegoImportance(const market::OfferDef& o, int seriesLevel);
bool NegoNegotiable(const market::OfferDef& o);

// Per-team personality + agent window + patience ---
struct NegoPersonality {
    int delay_days;      // days until response (3..10)
    int patience_base;   // initial patience (40..69)
    int concession;      // concession-type index (0..2)
    int tone;            // communication tone (0=formal 1=warm 2=dry)
    int style;           // negotiation style (0=normal 1=take-it-or-leave-it 2=haggler)
};

NegoPersonality NegoPersonalityFor(const std::string& teamId, int seriesLevel);
void NegoEstimateBand(const std::string& offerId, int round, unsigned careerSeed, int trueBase, int& lo, int& hi);
int NegoPatienceStart(const NegoPersonality& p, double wantT);

enum NegoPatienceBand { NEGO_PAT_RECEPTIVE=0, NEGO_PAT_TENSE=1, NEGO_PAT_EDGE=2 };
int NegoPatienceBandOf(int patience, int patience_base);

// Pure resolution — NegoResolve + a greedy counteroffer ---
// State of the async thread (mirrors career_save.h::Negotiation::state, which persists it as an int).
enum NegoThreadState {
    NEGO_TH_WAITING = 0, NEGO_TH_COUNTERED = 1, NEGO_TH_ACCEPTED_PENDING = 2, NEGO_TH_ULTIMATUM = 3,
    NEGO_TH_SIGNED = 10, NEGO_TH_WITHDRAWN = 11, NEGO_TH_PLAYER_WALKED = 12,
    NEGO_TH_CLOSED_ROLLOVER = 13, NEGO_TH_CLOSED_ELSEWHERE = 14,
};
inline bool NegoThreadOpen(int st) { return st < 10; }

// The team's response type to a player proposal (mirrors NegoRoundLog::kind when who==1).
enum NegoReplyKind { NEGO_RE_ACCEPT = 0, NEGO_RE_COUNTER = 1, NEGO_RE_ULTIMATUM = 2, NEGO_RE_WITHDRAW = 3 };

// The world context at the moment of resolving (everything NegoResolve needs that does NOT live in Negotiation).
struct NegoWorldCtx {
    int competing = 0;         // other live offers/threads (leverage)
    double relNow = 0.5;       // TeamRelationship at resolve time
    double formNow = 0.7;      // CareerForm at resolve time (delta vs n.form_pct)
    int daysToRound1 = 999;    // preseason urgency
    unsigned careerSeed = 0;
    bool seatGone = false;     // the offer is no longer live on the board (seat filled/window closed)
};

// The team's response to the player's current proposal.
struct NegoReply {
    int kind = NEGO_RE_WITHDRAW;
    DealTerms counter;         // valid in COUNTER/ULTIMATUM (ULTIMATUM = the neutral base offer)
    int patience_after = 0;
    bool sweetener = false;    // the team improved out of desperation
};

// Trims `asked` until it closes `deficit` points of margin: first drops the sponsor if it offends,
// then removes the priciest demands (greedy, cost desc), and finally lowers the salary package.
DealTerms NegoCounterFrom(const DealTerms& asked, int deficit, int concession);

// Resolves the thread's current proposal against the world: accept/counter/ultimatum/withdraw. Pure
// (deterministic via MailHash; no rand()); it doesn't mutate `n` (mutations onto SaveData are the
// caller's responsibility).
NegoReply NegoResolve(const Negotiation& n, const NegoWorldCtx& ctx);

// Translation templates + mail emitter for the NEGOTIATION family ---
// GeneratedEmail::family values: 0=OFFER 1=PROGRESS/MILESTONE 2=sponsor cards 3=NEGOTIATION.
constexpr int NEGO_MAIL_FAMILY = 3;

// Emits a negotiation-thread mail (content-derived id: neg_id+"_"+beat+"_r"+round -> dedup).
// variant = the team personality's TONE (NegoPersonalityFor), NEVER a hash: the mail's voice
// has to match the personality already used to resolve the thread. Base team/series params +
// `extra` (e.g. deadline_w/reserve_w, which depend on the beat); from_name/from_role_key = the voice of the
// Team Principal of the offering team. outIds (optional) receives the generated id if there was no dedup.
// year: fixes "mail with an absurd date across the year boundary" — the calendar's year AT EMIT time (GeneratedEmail::year).
bool EmitNegoMail(SaveData& save, const Negotiation& n, const char* beat, int day, int year,
                  const MailParams& extra, std::vector<std::string>* outIds);

// Thread mutations (NegoStart/walk/closes) + snub on withdrawn ---

// Clock seam: EVERY absolute day in the negotiation passes through here, with the SAME convention
// RunWorldTick uses today for offer_first_seen (the real temporal model: the real abs_day from
// career_dateutil, epoch 2025-01-01) — mixing conventions would break the TTL. If the clock changes
// again, change ONLY this function alongside the offer_first_seen migration.
inline int NegoAbsDay(int year, int cursorDoy) { return dateutil::AbsDayFromYearDoy(year, cursorDoy); }

// Opens (or reopens after a counter) the thread: seals want/form/trial, patience, respond_day, and deadline.
// respond_day = min(absDay + personality.delay_days, deadline-1); if it doesn't fit, a one-off grace
// deadline = absDay + NEGO_TTL_GRACE_DAYS. Returns false if the offer isn't negotiable or there's
// an open thread on another offer in ACCEPTED_PENDING state (you don't negotiate with a live handshake).
bool NegoStart(SaveData& save, const market::OfferDef& offer, const DealTerms& terms,
               double wantT, int competing, double trialRatio, double formNow, int absDay, int year);
// The player's re-counter from COUNTERED: updates terms, round+1, back to WAITING.
// ULTIMATUM/other states -> false (take it or leave it). The patience cost is charged at RESOLVE time.
bool NegoReSend(SaveData& save, Negotiation& n, const DealTerms& terms, int absDay);
Negotiation* NegoFindByOffer(SaveData& save, const std::string& offerId);   // the OPEN thread for that offer (nullptr if none)
Negotiation* NegoFindById(SaveData& save, const std::string& negId);
// The player walking away: PLAYER_WALKED; with rounds already played -> a snub (inserted into declined_offers).
void NegoPlayerWalk(SaveData& save, Negotiation& n, int absDay);
// On signing ANY exclusive contract: closes every other open thread (CLOSED_ELSEWHERE + mail).
// Exception: an ACCEPTED_PENDING left abandoned like this IS a snub (you shook on it).
void NegoCloseElsewhere(SaveData& save, const std::string& signedOfferId, int absDay);
// Marks the thread signed (the real AcceptOffer is done by the caller; this only closes the state).
void NegoMarkSigned(SaveData& save, const std::string& negId);
void NegoAppendHistory(Negotiation& n, const NegoRoundLog& row);   // cap NEGO_HISTORY_MAX, drop oldest

// NegoTick — the negotiation thread's pulse, integrated into the world ---
struct CareerCalendar;   // forward decl: career_calendar.h (avoids dragging the calendar engine into this pure header)
struct WeekTickResult;   // forward decl: career_contract.h (same reason)

// The thread's pulse (a step within RunWorldTick, after regenerating the 'live' board + LapseExpiredOffers):
//  WAITING with respond_day <= today -> NegoResolve + transition + the beat's mail.
//  COUNTERED/ULTIMATUM: neglect drain (a week without responding) + a one-time reminder.
//  Candidate pressure (once, with competing>0): shortens the deadline and warns.
//  deadline expired -> WITHDRAWN + mail + save.withdrawn_offers (snub).
//  ACCEPTED_PENDING with reserve_day expired -> WITHDRAWN + expired-reservation mail + snub.
// The ids of emitted mails are appended to res.generated_mail_ids.
void NegoTick(SaveData& save, const CareerCalendar& cc, const std::vector<market::GenOffer>& live,
              int absToday, WeekTickResult& res);
// Mass close-out at rollover (one mail per open thread). Called by AdvanceWeek when crossing the year.
void NegoCloseAllOpen(SaveData& save, int absDay, WeekTickResult& res);
// Overload with no WeekTickResult: used by AdvanceDays, which has no 'res' in its signature (the year crossing lives
// outside the world pulse). The mail IS still persisted in save.emails (EmitNegoMail doesn't depend on outIds);
// only the immediate notice in generated_mail_ids for THIS one-off tick is lost — the thread is already
// CLOSED_ROLLOVER, so the next RunWorldTick doesn't re-announce it either (dedup by mail id): a silent
// mail, still visible in the player's inbox all the same.
void NegoCloseAllOpen(SaveData& save, int absDay);

} // namespace career
