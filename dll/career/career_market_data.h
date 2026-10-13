#pragma once
// career_market_data.h — data for the DRIVER MARKET + MAIL.
// SAMPLE data (like the rest of the overlay): offers (open/direct), reputation tiers, and the
// mail inbox. Reputation = the ENGINE's own (career::prog.rep / RepDisplayBand); the Market only reads it.
// Future: this data will come from the season-JSON (teams->seats->reqRep). For now, a static catalog.
#include "imgui.h"
#include <string>
#include <vector>

namespace market {

// Reputation: COSMETIC display bands over the engine's REP (idx = career::RepDisplayBand) ----
struct RepTier { std::string key, es, en; int min; ImU32 color; };
extern std::vector<RepTier> REP_TIERS;

struct RepInfo { int score; int idx; int pct; const RepTier* tier; const RepTier* next; };
// Builds the Market's reputation info from the ENGINE's REP (career::prog.rep).
// idx = career::RepDisplayBand(rep); score = rep; pct = progress to the next band. The ONLY source of REP.
RepInfo RepInfoFor(int rep);

// Market offers ----
// direct=true  -> the team is coming after YOU ("THEY WANT YOU"): set above, only visible if rep>=reqRep.
// direct=false -> "DRIVER WANTED" (you apply): always visible, sorted by reqRep.
// scope: "season" | "championship" | "special" | "program" (labeled via translation).
// clsId: id from SAMPLE_CLASSES (disc dot + class name). seatName: the car the team fields.
struct OfferDef {
  std::string id; bool direct;
  std::string team, from; ImU32 color;
  std::string clsId, scope; int reqRep; std::string seatName;
  long signBonus, perRace; int commitment;
  std::string title, title_en, pitch, pitch_en;
  // Contract type + expectation + "why" bars + tryout/conflict + promotion clause.
  int contractKind = 0;                 // career::bal::CONTRACT_* (0 works default; back-compat)
  // Narrative archetype OVER the economics kind (career::ContractArchetype 0..8). Resolved at
  // offer gen via career::ArchetypeForOffer; copied onto the Contract at sign. Default 0 = ARCH_SELF_FUNDED_ENTRY.
  int archetype = 0;
  int whyRating = 0, whyRel = 0;        // 0..100 display bars (q(R) and relationship)
  int whyForm = 0;                      // 0..100 display bar (CareerForm; SAME source as the soft-score form term)
  std::string expect, expect_en;        // team expectation text ("Win or out")
  bool requiresTryout = false;          // tryout gate
  std::string promote_to;               // conditional: target series_id ("" = none)
  // signability soft-mark. true = the player can SIGN (default; fee-waived/affordable).
  // It NEVER drops the offer — only the UI Accept button reads it (DORMANT at money_norm=0; no pay seats today).
  bool canAfford = true;
  // TRANSIENT renewal-quality classification, set ONLY at rollover by
  // BuildRenewalOffer (where perf is known): 0=better / 1=same / 2=worse; -1 = not a renewal (default). NOT
  // persisted (renewal save line stays 11 fields — back-compat) since the rollover screen reads it in-session
  // right after generation; the bridge degrades to a commitment-based classification after a save round-trip.
  int renewQuality = -1;
  // The pay-driver/Latifi BUY-IN the player pays up front to take an above-pace seat (you bring
  // budget). Computed in OfferTerms (MK_PAYDRIVER_BUYIN[level-1]*(1-q(R))) ONLY for CONTRACT_PAY_DRIVER seats;
  // 0 for every other type (fee-waived). AcceptOffer copies it onto Contract.buy_in and DEBITS it at signing
  // (solvency-gated). Placed AMONG the trailing defaulted fields (after renewQuality) so the positional aggregate
  // inits in career_market_data.cpp OFFERS stay valid; additive default 0 -> byte-identical for all non-pay offers.
  long buy_in = 0;
  // true = this is an ELO-DISPLACEMENT offer (the player outpaces the team's
  // weakest in-reach backmarker -> "take his seat"). The Market renders a distinct `seat_displacement` label;
  // the live accept-time invocation of career::DisplaceIncumbent (the move-out) keys off it. Additive trailing
  // default false -> byte-identical for every non-displacement offer + the positional OFFERS inits stay valid.
  bool displacement = false;
};
extern std::vector<OfferDef> OFFERS;

// Mail ----
struct EmailChoice {
  std::string id, label, label_en; int kind;     // 0=primary 1=ghost 2=design
  long credits; std::string reply, reply_en;
  ImU32 sw0, sw1, sw2;                            // swatch (only for kind=design)
};
struct EmailDef {
  std::string id, from, org; int tag;             // tag: 0=sponsor 1=team 2=official
  int day; int type;                              // type: 0=info 1=choice
  std::string subject, subject_en;
  std::vector<std::string> body, body_en;
  bool design = false;                            // livery-design mail (3 swatches)
  std::vector<EmailChoice> choices;
  // synthesized from a direct offer (CTA button -> Market)
  bool offer = false; std::string ctaLabel, ctaLabel_en;
};
extern std::vector<EmailDef> EMAILS;

ImU32 TagColor(int tag);                           // 0/1/2 -> tag color

// SHARED decision state (Market <-> Mail). Demo: in memory, no real save.
int  OfferDecision(const std::string& id);          // 0=undecided 1=accepted/applied 2=rejected
void SetOfferDecision(const std::string& id, int d);

} // namespace market
