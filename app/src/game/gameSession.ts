// Session-launch state machine: Play -> launch -> load -> connect.
// The reducer is PURE (deterministically testable); useGameSession wires up the side effects (timers, WS client).
import type { InjectErrorCode } from '../shell/bridge';

/** Startup steps. Order matters, which is why this is a pure, tested function. */
export type LaunchStep = 'close_game' | 'prepare_game_folder' | 'launch_steam' | 'await_process';

/**
 * Decides how the overlay gets loaded. The branch isn't about the session phase, it's about whether
 * we're the ones launching the game.
 *
 * The overlay file only takes effect if it's in place BEFORE
 * the process starts, because the game only looks for it while it is loading. It can't be added to a game
 * that's already running.
 *
 * The alternative for that case was a separate helper binary, dropped by the
 * owner's decision: it was one more moving part in the launch path, and
 * the product is simpler without it.
 * Now the game gets closed, the overlay gets put in place, and it reopens. The whole product no longer
 * ships that helper.
 *
 * Closing someone's game for them can cost them a race, so that path REQUIRES
 * explicit user confirmation before calling this.
 */
export function launchPlan(input: { gameAlreadyRunning: boolean; simGame?: boolean }): LaunchStep[] {
  // Against the AMS2 double (tools/zta_fakegame.cpp) there is nothing to launch: the plan exists to put the overlay
  // in place before AMS2 starts and to wait for its process, and the double is already listening.
  if (input.simGame) return [];
  const base: LaunchStep[] = ['prepare_game_folder', 'launch_steam', 'await_process'];
  return input.gameAlreadyRunning ? ['close_game', ...base] : base;
}

/**
 * Should restarting AMS2 be offered because the track grids have gone stale?
 *
 * AMS2 reads CustomAIDrivers AT STARTUP and doesn't hot-reload it, and the app runs INSIDE the
 * game, so a signing from the window doesn't reach the track until a restart. This used to be
 * "solved" by warning ALWAYS from the rollover screen, unconditionally — and warning always
 * is the same as not warning at all.
 *
 * Three conditions, and all three matter:
 *  - `stale` is reported by the ENGINE, comparing a fingerprint of the world against the one it sealed when writing
 *    the XML files; `undefined` is an older engine that can't answer, and there it stays silent.
 *  - dismissed by the user = stays quiet until the world changes again.
 *  - and NEVER during a launched race: offering to close the game right then is the worst possible
 *    moment, and it would cost them the race.
 */
export function shouldOfferGridRestart(input: {
  stale: boolean | undefined; dismissed: boolean; inRace: boolean;
}): boolean {
  return input.stale === true && !input.dismissed && !input.inRace;
}

export type SessionPhase =
  | 'idle'
  | 'launching'        // launchSteam was called
  | 'waiting_process'  // waiting for AMS2AVX.exe to appear
  | 'injecting'        // the engine is loading
  | 'connecting'       // WS open, waiting for api_info
  | 'connected'        // api_info with game_attached=true
  | 'safe_mode'        // connected but the build isn't supported (game_attached=false)
  | 'compat_blocked'   // unvalidated AMS2 version for the engine; blocked BEFORE launching
  // (reconnection flows): the process watchdog separates "reconnecting" from reality.
  | 'game_down'         // the AMS2 process is gone (died or was closed) -> Play/Reconnect relaunches
  | 'detected_unlinked' // AMS2 is running but without a WS for a while: manual startup without linking -> Link button
  | 'error';

export interface ApiInfo {
  api: number;
  build: string;   // 07/09: the engine's SEALED version, "<version>+<commit>[-dirty]" (or "dev" outside a build)
  game_attached: boolean;
  paths_ok: boolean;
  // Who hosts the engine. Only sent by the AMS2 double ("double"); the published DLL does not compile it, and its
  // absence is treated as the game.
  host?: string;
}

// Are we talking to the AMS2 double instead of the game? The ENGINE says so, not a setting you have to remember to
// remove. Internal instrument: outside development (vite build) it is a constant `false` and the bundle does not carry the double.
export function talkingToDouble(info?: ApiInfo): boolean {
  return import.meta.env.DEV && info?.host === 'double';
}

// 07/09 - IS THE APP RUNNING ITS OWN ENGINE? The engine inside AMS2 is whichever one was there when the game started:
// updating the app does not touch an AMS2 that is already up, and with the game open Windows locks the file, so
// the copy fails silently (see app/src-tauri/src/lib.rs). The two halves drift apart easily.
//
// Real cost: on 07/09 a user reported that adaptive difficulty was not persisting. It was not a bug -- they were running an
// engine older than the change that introduced that save line. It was found by recognising a log format
// stale, by sheer luck, because until now ALL the DLLs said "build 1 1" (/Brepro empties __DATE__).
// With the app-alive lock the price goes up: a mismatch no longer causes odd bugs, it leaves the mod INERT.
//
// Compares only the version, not the commit: "0.6.6+abc1234" and "0.6.6" are the same release. Returns the warning already
// worded, or null if there is nothing to say.
export function engineVersionMismatch(appVersion: string, engineBuild: string): string | null {
  const engine = (engineBuild || '').split('+')[0].trim();
  const app = (appVersion || '').trim();
  if (!engine || !app) return null;      // without data no verdict is invented
  if (engine === 'dev') return null;     // hand-compiled engine: normal in development, not a warning
  if (engine === app) return null;
  return `VERSION MISMATCH: app v${app} but engine v${engine} -- the DLL loaded in AMS2 is NOT from this app. ` +
         `Close AMS2 completely and relaunch from the app; if it persists, reinstall.`;
}

// T371: why the watchdog declares detected_unlinked. 'missing' = the overlay file is no longer in the
// game folder (something removed it between it being put in place and AMS2 loading it).
// 'foreign' = there is a file of that name but it is not ours (another mod). 'present' = it is ours and
// is still there, but the engine did not start (WS/api off). 'unknown' = could not be checked (bridge down).
export type UnlinkedReason = 'missing' | 'foreign' | 'present' | 'unknown';

export interface SessionState {
  phase: SessionPhase;
  message: string;
  apiInfo?: ApiInfo;
  detected?: string | null;   // FileVersion of AMS2AVX.exe when phase === 'compat_blocked'
  injectCode?: InjectErrorCode;
  unlinkedReason?: UnlinkedReason;   // only with phase === 'detected_unlinked'
}

export type SessionEvent =
  | { t: 'play' }
  | { t: 'awaiting' }                                   // launchSteam resolved -> now waiting for the process
  | { t: 'process_up' }
  | { t: 'process_timeout' }
  | { t: 'injected'; ok: boolean; code: InjectErrorCode; msg: string }
  | { t: 'api_info'; info: ApiInfo }
  | { t: 'compat_blocked'; detected: string | null }
  | { t: 'ws_drop' }
  | { t: 'proc_dead' }            // watchdog: the AMS2 process disappeared (no WS)
  // watchdog: AMS2 is running but the WS does not connect (no DLL). reason: see UnlinkedReason.
  | { t: 'proc_alive_unlinked'; reason?: UnlinkedReason }
  | { t: 'reset' };

export const INITIAL_SESSION: SessionState = { phase: 'idle', message: '' };

// phase -> i18n key (session.*) for the progress text. The reducer carries NO presentation text
// (bilingual rule: nothing hardcoded); the views translate per phase using this map. `message` is kept
// ONLY for the bridge's raw technical detail (a failed step), which is shown verbatim.
export const PHASE_LABEL_KEY: Record<string, string> = {
  launching: 'launching', waiting_process: 'waiting', injecting: 'injecting',
  connecting: 'connecting', safe_mode: 'safeMode', connected: 'connected',
};

export function reduceSession(s: SessionState, ev: SessionEvent): SessionState {
  switch (ev.t) {
    case 'reset':
      return { ...INITIAL_SESSION };

    case 'play':
      return s.phase === 'idle' || s.phase === 'error' || s.phase === 'compat_blocked'
          || s.phase === 'game_down' || s.phase === 'detected_unlinked'   // relaunch / link
        ? { phase: 'launching', message: '' }
        : s;

    case 'compat_blocked':
      return s.phase === 'launching'
        ? { phase: 'compat_blocked', message: '', detected: ev.detected }
        : s;

    case 'awaiting':
      return s.phase === 'launching'
        ? { phase: 'waiting_process', message: '' }
        : s;

    case 'process_up':
      return s.phase === 'launching' || s.phase === 'waiting_process'
        ? { phase: 'injecting', message: '' }
        : s;

    case 'process_timeout':
      return s.phase === 'launching' || s.phase === 'waiting_process'
        ? { phase: 'error', message: '' }
        : s;

    case 'injected':
      if (s.phase !== 'injecting') return s;
      return ev.ok
        ? { phase: 'connecting', message: '' }
        : { phase: 'error', message: ev.msg, injectCode: ev.code };

    case 'api_info':
      // api_info is ground truth: it only arrives over a live WS, so it reflects the real state
      // from ANY phase. This includes "warm attach": the app opens with AMS2 already running and the engine already up,
      // the client connects and api_info arrives as 'idle' -> we jump straight to connected (we don't leave the cold-start
      // Play button lying). It used to be ignored outside connecting/connected/safe_mode.
      return ev.info.game_attached
        ? { phase: 'connected', message: '', apiInfo: ev.info }
        : {
            phase: 'safe_mode',
            message: '',
            apiInfo: ev.info,
          };

    case 'ws_drop':
      return s.phase === 'connected' || s.phase === 'safe_mode' || s.phase === 'connecting'
        ? { phase: 'connecting', message: '' }
        : s;

    // the process watchdog (useGameSession) splits the endless "Reconnecting..." into two realities.
    // proc_dead: AMS2 isn't running anymore -> reconnecting against a dead port goes nowhere; the gate offers to
    // relaunch. proc_alive_unlinked: AMS2 is running but the WS has gone a while without connecting -> a manual startup
    // with no engine attached; the gate offers to Link (play() skips the launch and links directly). api_info remains
    // ground truth: if the WS connects from any of these phases, connected wins.
    case 'proc_dead':
      return s.phase === 'connecting' || s.phase === 'detected_unlinked'
        ? { phase: 'game_down', message: '' }
        : s;

    case 'proc_alive_unlinked':
      return s.phase === 'connecting' || s.phase === 'game_down'
        ? { phase: 'detected_unlinked', message: '', unlinkedReason: ev.reason ?? 'unknown' }
        : s;

    default:
      return s;
  }
}
