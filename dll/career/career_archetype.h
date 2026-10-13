#pragma once
#include "career_balance.h"
// Living World — a contract archetype layer OVER the existing 5 contract kinds. Pure:
// no ImGui, no game, no SHM, no SaveData. Constants in career_balance.h (career::bal::NEGO_ARCH_NEG_LEVEL).
// The 9-archetype -> preset mapping is fixed by the project's design spec
// (HISTORIC_EXHIBITION is OUT -> M2). Indices are STABLE + match NEGO_ARCH_NEG_LEVEL[10].
//
// NOTE (early skeleton): the resolver bodies in career_archetype.cpp are STUBS so the test build links;
// the real 9-row mapping + offer->archetype logic land in a later slice of this work.
namespace market { struct GenOffer; }   // forward decl — avoid pulling career_market_data.h/imgui into this pure header

namespace career {

// The 10 contract archetypes (0..9). Stable order = NEGO_ARCH_NEG_LEVEL[10] index order.
// HISTORIC_EXHIBITION dropped (-> M2 Memories). Internal-only this phase (no UI badge).
enum ContractArchetype {
    ARCH_SELF_FUNDED_ENTRY = 0,  // self-funded entry seat        (negLevel 0 / never trial)
    ARCH_CUSTOMER_SEAT     = 1,  // customer (self-funded) seat   (negLevel 1 / gray-zone trial)
    ARCH_OPEN_VACANCY      = 2,  // open advertised vacancy       (negLevel 1 / gray-zone trial)
    ARCH_DIRECT_OFFER      = 3,  // works seat, team approached you (negLevel 2 / trial unless recent result)
    ARCH_JUNIOR_PROGRAM    = 4,  // junior development program     (negLevel 0 / one-time entry trial)
    ARCH_FACTORY_PROGRAM   = 5,  // factory program                (negLevel 2 / one-time entry trial)
    ARCH_WILDCARD          = 6,  // one-race wildcard (race IS the test) (negLevel 0 / never)
    ARCH_RENEWAL           = 7,  // re-sign the current kind at rollover  (negLevel 2 / never)
    ARCH_BRIDGE_CONTRACT   = 8,  // short bridge contract          (negLevel 1 / gray-zone trial)
    ARCH_ACADEMY_PROMOTION = 9,  // promotion: the parent team bumps you from its academy up to the higher tier (a works seat)
};

// trialRule: 0=never, 1=gray-zone-set (trial only when a gray-zone trigger fires), 2=one-time-entry.
enum ArchetypeTrialRule { ARCH_TRIAL_NEVER = 0, ARCH_TRIAL_GRAYZONE = 1, ARCH_TRIAL_ONETIME = 2 };

// Resolved preset for an archetype: which economics kind it maps to (career::bal::CONTRACT_*), its
// negotiationLevel (0/1/2, the demand gate), and its trial rule. defaultSeasons seeds the
// contract term (1 = single-season, the safe default; RENEWAL/factory may shift it later).
struct ArchetypePreset {
    int economicsKind;   // career::bal::CONTRACT_WORKS/CUSTOMER/PAY_DRIVER/TRIAL/CONDITIONAL (0..4)
    int negotiationLevel;// 0=none 1=medium 2=high — the demand gate (== NEGO_ARCH_NEG_LEVEL[arch])
    int trialRule;       // ArchetypeTrialRule (0/1/2)
    int defaultSeasons;  // default contract term (seasons) the archetype seeds
};

// Pure lookup: the resolved preset for an archetype (clamped to 0..8). A later step fills the real 9-row table.
const ArchetypePreset& ArchetypeInfo(int arch);

// Pure resolver: which archetype an offer maps to, from the signals already set at offer generation
// (o.def.direct / o.def.contractKind / o.def.promote_to / o.want_t band) + player state. A later step fills the
// real mapping; this early stub returns ARCH_SELF_FUNDED_ENTRY (0).
int ArchetypeForOffer(const market::GenOffer& o);

} // namespace career
