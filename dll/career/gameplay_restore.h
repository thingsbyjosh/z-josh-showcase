#pragma once

namespace career {
// Player finish/retirement is not the end of the weekend. Keep the override
// through practice, qualifying, race, pits and results; restore on front-end return.
struct GameplayRestoreGate {
  bool enteredSession = false;
  bool Update(bool pending, bool frontEnd, bool inSession) {
    if (!pending) { enteredSession = false; return false; }
    if (!frontEnd && inSession) enteredSession = true;
    // Initial Custom Race must not undo an override that was just applied.
    // Do not consume the latch here: a failed memory write must be retried.
    return enteredSession && frontEnd && !inSession;
  }
};
}
