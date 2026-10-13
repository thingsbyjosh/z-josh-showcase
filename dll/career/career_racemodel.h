#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "career_engine.h"  // DnfKind

namespace career {

struct RaceEntrant {
    std::string id;
    std::string name;
    bool   is_player   = false;
    double skill       = 0.85;   // race_skill 0..1 (AI: GridDriver.race_skill; player: RatingQuality(rating))
    double consistency = 0.85;
    double aggression  = 0.60;
    double reliability = 0.95;
    double wet_skill   = 0.72;
};

struct RaceModelConfig {
    double base_sigma                 = 0.05;  // pace spread with consistency=1
    double consistency_sigma          = 0.10;  // extra spread scaled by (1-consistency)
    double wet_sigma_mult             = 1.8;   // rain widens the spread
    double wet_skill_weight           = 0.30;  // in rain, weight toward wet_skill
    double mech_dnf_per_unreliability = 0.18;  // P(mechanical DNF) = this * (1-reliability)
    double incident_dnf_per_aggression= 0.06;  // P(incident)    = this * aggression
    double player_offset              = 0.0;   // "player performance" knob (+ = faster)
};

// order: entrant indices in classified order (0=winner). Finishers first by pace, DNFs after.
// dnf_of: indexed by entrant (not by order). player_pos: the player's classified 1..N.
struct RaceOutcome {
    std::vector<int>     order;
    std::vector<DnfKind> dnf_of;
    int player_pos = 0;
    int field = 0;
};

RaceOutcome SimulateRace(const std::vector<RaceEntrant>& entrants, int weather,
                         std::uint64_t seed, const RaceModelConfig& cfg = RaceModelConfig());

} // namespace career
