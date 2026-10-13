// Can the Results screen be shown yet?
//
// Finishing the race and EXITING the race are two separate steps in Automobilista 2: crossing the line drops the
// game into its own end-of-session screen, and you still have to exit to get back to the main
// menu. The app used to jump to Results as soon as the engine settled the race, i.e. at the finish line, and that's where people
// forgot the step they still had left: they'd see their result, consider themselves done, and leave AMS2 stuck in its
// session (so the next race would start from a strange state).
//
// The headset panel already followed this rule: wait for the main menu (GAME_FRONT_END) and nothing else. This brings
// flat-screen mode into line with it, which is where it was missing.
//
// Deliberately PURE: the tricky part isn't reading a boolean, it's the two emergency exits — without them
// the result would stay hostage forever, which is worse than showing it too early.
export interface ResultsGateInput {
  resultReady: boolean;   // the engine already settled this race (resultSeq ahead of the baseline)
  atFrontEnd: boolean;    // query.game_state.atMenu = gameState 1 = AMS2's main menu
  simRace: boolean;       // simulation: there's no game to exit from
  gameGone: boolean;      // the game stopped responding (closed or crashed) -> there's no race to exit
}

export function shouldShowResults(i: ResultsGateInput): boolean {
  if (!i.resultReady) return false;
  // Simulation: the race isn't actually driven, so there's no pending step in the game.
  if (i.simRace) return true;
  // The game is simply gone (force-closed, or crashed at the end, which has happened): waiting for a main
  // menu that will never arrive would bury the result and leave the player not knowing what happened to their race.
  if (i.gameGone) return true;
  return i.atFrontEnd;
}

// Consecutive query.game_state probes with no response before declaring the game gone. At 3s per probe, that's ~9s:
// enough to not mistake a momentary stutter for a closed game, and short enough that nobody's left staring at
// a screen asking them to exit a race that no longer exists.
export const GAME_GONE_TICKS = 3;

// Do we call the race ABANDONED?
//
// Leaving a race for the menu without finishing it settles nothing, so resultSeq never rises and the app got
// stuck on "waiting for result" forever. Hence this gate: the race started, the game has spent two
// polls at the menu and no result has arrived -> you got out, we go back to the hub with no Results.
//
// The problem ([tester], 05/09) is that "no result has arrived" does NOT mean "none will arrive". Finishing (or
// retiring) and LEAVING the session are two separate steps, and in between the engine HOLDS the snapshot unsettled
// until the race really ends. Measured in their logs: 17, 17, 26, 10 and 9 seconds of hold - against the
// 6 s this gate took to fire. Result: a false abandon, back to the calendar, and the real result
// landing later on a screen nobody was watching any more (the event WAS recorded, which is exactly what made it
// so baffling). Multiclass was not the cause, it only made it frequent: its holds are the long ones,
// but a single-class race from the same batch held for 10.1 s and would have failed just the same.
//
// settlePending is the signal that was missing: as long as the engine says it has a settlement on hold, we wait.
// With a cap, because this gate's real safety net is still not leaving anyone staring at a dead screen: if the
// settlement does not land within ABANDON_SETTLE_GRACE_TICKS, we go back to the hub anyway.
export interface AbandonGateInput {
  sawRaceStart: boolean;    // the race did start (it left the menu at least once)
  atMenuStreak: number;     // consecutive polls seeing AMS2's main menu
  resultAdvanced: boolean;  // resultSeq has passed the baseline -> there is a result, this is not an abandon
  settlePending: boolean;   // the engine has a settlement ON HOLD (query.career_state.flags.settlePending)
}

// Polls at the menu before calling the race abandoned. At 3 s per poll, 6 s.
export const ABANDON_MENU_TICKS = 2;
// Wait cap for when the engine says it is settling. At 3 s per poll, 2 minutes: far more than the
// longest hold measured (26 s) and still an ending, so a stuck engine cannot hang the hub.
export const ABANDON_SETTLE_GRACE_TICKS = 40;

export function shouldGiveUpAsAbandoned(i: AbandonGateInput): boolean {
  if (!i.sawRaceStart) return false;      // never left the menu: there is no race to get out of
  if (i.resultAdvanced) return false;     // there is a result -> Results, not retirement
  if (i.atMenuStreak < ABANDON_MENU_TICKS) return false;
  // A settlement on hold = the result is on its way. Wait, but not forever.
  if (i.settlePending && i.atMenuStreak < ABANDON_SETTLE_GRACE_TICKS) return false;
  return true;
}
