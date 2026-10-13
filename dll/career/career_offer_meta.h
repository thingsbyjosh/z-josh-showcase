#pragma once
#include <string>
#include <vector>
#include "imgui.h"
// career_offer_meta.h — a DISPLAY table for the contract type, for the Market's cards.
// (color + icon + label per type). The offer's own data — contract type (contractKind),
// expectation (expect/expect_en), "why they want you" (whyRating/whyRel), and the tryout (requiresTryout) —
// already live in market::OfferDef, populated by the contracts engine (career_market.h / ContractKindForSeat
// + OfferTerms + TryoutRequired). This table only translates the type index (career::bal::CONTRACT_*) into its
// visual presentation; it mirrors the prototype's CONTRACT_TYPES display (data_living.jsx).
namespace market {

// index = career::bal::CONTRACT_* : 0=works 1=customer 2=pay_driver 3=trial 4=conditional
struct ContractType { std::string es, en; ImU32 color; const char* icon; };
const ContractType& ContractTypeInfo(int kind);   // 0..4 (clamp)

// DISPLAY tables for the NEGOTIATION screen (the MATH lives in career_negotiate.{h,cpp}) ----
// Labels for the 8 demands (an extended mirror of data_living.jsx's NEGOTIATION_DEMANDS). The real %
// cost comes from career::bal::NEGO_DEMAND_COST[demandId]; only the text lives here. demandId = career::NegoDemandId (0..7).
struct NegDemandView { int demandId; std::string es, en, desc_es, desc_en; };
const std::vector<NegDemandView>& NegDemands();

// Display for a sponsor tier (rank 0..4 local..elite): label + color. The % DELTA comes from
// career::NegoSponsorDelta(rank). Mirrors the prototype's SPONSOR_TIERS.
struct SponsorTierView { std::string es, en; ImU32 color; };
const SponsorTierView& SponsorTierDisplay(int rank);   // 0..4 (clamp)

// Player sponsors (SAMPLE; replaced by the signed ones later). Stable id, tierRank 0..4.
struct MySponsorView { std::string id, name; int tierRank; };
const std::vector<MySponsorView>& MySponsors();

} // namespace market
