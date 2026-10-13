// AI difficulty by phase (passed to the game via a dedicated settings slot): skill / aggression / mistakes.
//
// Jul 25 — extracted from career_data.cpp. It used to live among the UI layer's sample data, and once a
// test covering it existed (test_handoff_mistakes), the engine test harness wouldn't link: career_data.cpp isn't in
// build_career_test.ps1 and can't be, because it drags in the whole UI layer (CareerOwned*, Col::Disc...).
// The function is PURE (only its arguments plus a local table), so its home is its own translation unit, linked into
// both builds: the overlay picks up dll/career/*.cpp by glob, and the harness lists it explicitly.
// Declared in career_state.h (global namespace, the same way screen_calendar and career_overlay use it).
#include "career_state.h"
#include "career_balance.h"   // AI_AGGR_MEDIUM / AggrSliderKeepingPersonality

void HandoffAiForPhase(int phase, int& skill, int& aggr, int& mistakes) {
  int p = phase < 1 ? 1 : (phase > 7 ? 7 : phase);
  skill = 78 + p * 4; if (skill > 108) skill = 108;   // 82..106 (AI skill 70..120)
  // higher default aggression (used to be 40/60/80: the AI drove in a procession at low phases).
  // The user can set this in Settings (prog.ai_aggression); this is only the AUTO-by-phase default.
  // At the top tier, AUTO is capped at Medium. Above Medium, the global slider OVERRIDES the per-driver aggression that
  // writes the XML (Maximum pins it at 1.00 for everyone, High compresses it to 0.6-1.0), so scaling by
  // phase at 60/80/100 was wiping out exactly the character the factor model had just given each rival. What actually
  // scales with phase is still SKILL, the real difficulty lever; aggression is now set by
  // each driver individually. Only AUTO is capped: an explicit override from Settings and the special-case gate (GATE_AI_AGGR,
  // where the goal IS a uniformly maxed-out AI) deliberately override this.
  aggr = career::bal::AggrSliderKeepingPersonality(p <= 2 ? 60 : (p == 3 ? 80 : 100));
  // Mistakes are set HIGH at the bottom, decreasing further up the ladder (testers wanted it this way: feels more realistic).
  //
  // Jul 28 — ENUM MEASURED, no more guessing. The user read the labels straight from the game itself, and there are SEVEN,
  // indices 0..6 (career::bal::kMist*):
  //     0 off · 1 x0.5 · 2 standard · 3 x2 · 4 x3 · 5 x4 · 6 x5
  // The hypothesis that used to be here (0=Off..7=x5, with an x1.5 in the middle) was WRONG on both counts: there's no
  // x1.5, and the max is 6, not 7. The old table was { 7,7,7,6,6,5,5,4 }, meaning phases 1 and 2
  // were writing a 7 that was OUT OF RANGE, and nobody knows what the game did with it (clamp to x5? ignore the
  // write and keep the player's last Custom Race setting?). Now every value is valid, which
  // removes that uncertainty.
  //
  // It also raises the FLOOR, which is what was asked for ("the AI has to make mistakes and fail"): the elite tier goes from x2 to x4.
  // The top stays at the max. It's still monotonically non-increasing: more mistakes in the lower categories.
  static const int kMistakesByPhase[8] = {
      career::bal::kMistX5,   // idx0 unused (p is clamped to 1..7)
      career::bal::kMistX5,   // phase 1  rookie
      career::bal::kMistX5,   // phase 2
      career::bal::kMistX5,   // phase 3
      career::bal::kMistX5,   // phase 4
      career::bal::kMistX4,   // phase 5
      career::bal::kMistX4,   // phase 6
      career::bal::kMistX4,   // phase 7  elite (used to be x2)
  };
  mistakes = kMistakesByPhase[p];      // p is already clamped to 1..7 above
}
