// Shell/app preferences (client-side, localStorage). The app<->game focus auto-switch can be turned off to
// switch between them MANUALLY (window controls). Default: ON.
import { getBridge, type SteamLaunchMode } from '../shell/bridge';

const KEY = 'ams2.autoSwitchFocus';

export function autoSwitchEnabled(): boolean {
  try { return localStorage.getItem(KEY) !== '0'; } catch { return true; }
}

export function setAutoSwitch(on: boolean): void {
  try { localStorage.setItem(KEY, on ? '1' : '0'); } catch { /* without localStorage: doesn't persist */ }
}

// "HANDS OFF" notice before the race is set up (ui/LaunchNotice). Shown by default, and anyone who already
// knows it can turn it off from the notice itself; it comes back from Settings. It's the only window in the mod where
// a player's click can sneak into the game's menus and undo the configuration that was just written.
const LAUNCH_NOTICE_KEY = 'ams2.launchNotice';

export function launchNoticeEnabled(): boolean {
  try { return localStorage.getItem(LAUNCH_NOTICE_KEY) !== '0'; } catch { return true; }
}

export function setLaunchNoticeEnabled(on: boolean): void {
  try { localStorage.setItem(LAUNCH_NOTICE_KEY, on ? '1' : '0'); } catch { /* without localStorage: doesn't persist */ }
}

// WINDOW MODE (a recurring report: "I can't stop it maximizing after an alt-tab"). The focus auto-switch
// brings focus back to the app at four moments (end of race, abandonment, a tryout verdict, the game returning to
// its menu) and until now it ALWAYS did so in fullscreen: the ❑ button only turned fullscreen off for that one
// instant, and nobody remembered the choice, so the next race would undo it. Now the button pins this
// preference and the auto-switch respects it. Default: fullscreen (business as usual, nobody notices a change).
export type WindowMode = 'fullscreen' | 'windowed';
const WINMODE_KEY = 'ams2.windowMode';
// The button lives in two places (the topbar and the connection screen), and the auto-switch changes the mode from outside
// both of them: this event stops the icon from getting stuck showing a state that's no longer current.
export const WINMODE_EVENT = 'zta:winmode';

export function windowMode(): WindowMode {
  try { return localStorage.getItem(WINMODE_KEY) === 'windowed' ? 'windowed' : 'fullscreen'; } catch { return 'fullscreen'; }
}

export function setWindowMode(mode: WindowMode): void {
  try { localStorage.setItem(WINMODE_KEY, mode); } catch { /* without localStorage: doesn't persist */ }
}

// how the Play button launches the game. steam://run always opens Steam's
// default option (flat); for VR it has to be requested explicitly (steam://launch/<id>/vr | /othervr).
// If the game is already running, the app just attaches and this setting doesn't apply. Default: flat (the usual behavior).
const LAUNCH_KEY = 'ams2.launchMode';

export function launchMode(): SteamLaunchMode {
  try {
    const v = localStorage.getItem(LAUNCH_KEY);
    return v === 'steamvr' || v === 'oculusvr' || v === 'dialog' ? v : 'flat';
  } catch { return 'flat'; }
}

export function setLaunchMode(mode: SteamLaunchMode): void {
  try { localStorage.setItem(LAUNCH_KEY, mode); } catch { /* without localStorage: doesn't persist */ }
}

// is the chosen mode a VR launch MANAGED by the app (in-headset panels, keyboard, mirror)?
// 'dialog' (choosing from Steam's own selector) is deliberately left OUT: the user could pick any
// option there -- including the game's OpenXR mode, which motion-compensation rigs need -- and the app has no
// way to know which one; the VR panels only arm themselves for the explicit modes. Focus
// (shouldTakeoverDesktop) is decided by the in-headset panel PREFERENCE across every non-flat mode, dialog
// included -- panel OFF means the fully-flat flow.
export function isVrLaunch(mode: SteamLaunchMode): boolean {
  return mode === 'steamvr' || mode === 'oculusvr';
}

// toggle for the in-headset overlay panel. Only has an effect if SteamVR is also present
// (the Rust module stays dormant without it). Default ON: anyone not using VR never notices it.
const VR_OVERLAY_KEY = 'ams2.vrOverlay';

export function vrOverlayEnabled(): boolean {
  try { return localStorage.getItem(VR_OVERLAY_KEY) !== '0'; } catch { return true; }
}

export function setVrOverlayEnabled(on: boolean): void {
  try { localStorage.setItem(VR_OVERLAY_KEY, on ? '1' : '0'); } catch { /* without localStorage: doesn't persist */ }
}

// SIMULATION MODE (debug): races do NOT launch the game; the engine fabricates a result
// (biased toward a win/podium, with a chance of failure) and settles it through the real settle path. Chosen on the Play gate
// (before connecting), and the shell syncs it to the engine on connect (cmd.debug.simRace). Default: OFF.
const SIM_KEY = 'ams2.simRace';

export function simRaceEnabled(): boolean {
  try { return localStorage.getItem(SIM_KEY) === '1'; } catch { return false; }
}

export function setSimRace(on: boolean): void {
  try { localStorage.setItem(SIM_KEY, on ? '1' : '0'); } catch { /* without localStorage: doesn't persist */ }
}

// Experimental Low RAM Mode -- a switch on the Play window. With it ON, the engine does NOT sweep the entire heap (~10GB)
// during race startup: on machines with less memory bandwidth (DDR4), that sweep, which
// runs on every frame while the game is setting up Custom Race, freezes the game for 90-215s and spikes RAM to ~10GB. The
// diagnostics always go to the log; this switch only changes the behavior. The shell syncs it to the engine on
// connect (cmd.debug.lowRam). Default: OFF (the usual behavior; it stays experimental until validated on more
// affected machines).
const LOWRAM_KEY = 'ams2.lowRamMode';

export function lowRamMode(): boolean {
  try { return localStorage.getItem(LOWRAM_KEY) === '1'; } catch { return false; }
}

export function setLowRamMode(on: boolean): void {
  try { localStorage.setItem(LOWRAM_KEY, on ? '1' : '0'); } catch { /* without localStorage: doesn't persist */ }
}

// Debug menu switch (Ctrl+Shift+D). The shell syncs it to the engine on connect (cmd.debug.contentLog).
// Default: OFF.
const CONTENT_LOG_KEY = 'ams2.contentLog';

export function contentLogVerbose(): boolean {
  try { return localStorage.getItem(CONTENT_LOG_KEY) === '1'; } catch { return false; }
}

export function setContentLogVerbose(on: boolean): void {
  try { localStorage.setItem(CONTENT_LOG_KEY, on ? '1' : '0'); } catch { /* without localStorage: doesn't persist */ }
}

// consent for sharing race data. Asked ONCE, on the Play window,
// which is the app's first screen and where there's STILL NO ENGINE to ask (same situation as simulation
// mode and Low RAM above): that's why the answer lives locally and the shell syncs it to the
// engine on every connection (cmd.settings.setTelemetry). Three states, and the third one matters: null = it hasn't
// been asked yet, and it's the only one that triggers the dialog. Saying NO is remembered exactly like saying yes.
const TELEMETRY_KEY = 'ams2.telemetryOptIn';

export function telemetryChoice(): boolean | null {
  try {
    const v = localStorage.getItem(TELEMETRY_KEY);
    return v === '1' ? true : v === '0' ? false : null;
  } catch { return null; }
}

export function setTelemetryChoice(on: boolean): void {
  try { localStorage.setItem(TELEMETRY_KEY, on ? '1' : '0'); } catch { /* without localStorage: it'll just ask again */ }
}

// The last duration (%) the player chose in a FREE race (not locked by a contract/calibration). It's
// re-preselected on the next race (Prerace); if it isn't available (falls under the 3-lap / 8-minute
// minimum) the shortest available option is used instead. Default 100.
const DUR_KEY = 'ams2.lastDurPct';

export function lastDurPct(): number {
  try { const v = parseInt(localStorage.getItem(DUR_KEY) || '', 10); return Number.isFinite(v) ? v : 100; } catch { return 100; }
}

export function setLastDurPct(pct: number): void {
  try { localStorage.setItem(DUR_KEY, String(pct)); } catch { /* without localStorage: doesn't persist */ }
}

// (requested by several users): UI SCALE. A lot of people
// plays with the app on a distant or angled monitor (triple setups plus a fourth monitor above), and the current text
// ends up too small for them. It scales the WHOLE interface, not just the font: the design is in the prototype's raw pixels, and
// enlarging only the text would break the dense screens (calendar, market).
//
// The mechanism is the webview's NATIVE ZOOM, the same as the browser's Ctrl+=: the content is drawn
// bigger and the CSS viewport shrinks by the same proportion, so the window does NOT resize and the UI
// still fits entirely (the dense areas scroll, which is exactly what .scroll is there for).
//
// The first version used CSS `zoom` on <html> and it was WRONG: #root measures 100vw/100vh, so the zoom
// enlarged the canvas 25% past the window, and `body { overflow:hidden }` clipped whatever spilled over --
// the interface grew, not the content inside it. At 125%, Settings' own scale buttons would
// end up off-screen, with no way back short of wiping localStorage.
// Persisted in localStorage; applied on launch and whenever it's changed.
const UISCALE_KEY = 'ams2.uiScale';
export const UI_SCALES = [90, 100, 110, 125, 150] as const;   // the percentages offered in Settings
// Settings renders the active step with its own state, but the keyboard shortcuts (Ctrl +/-/0) change the scale
// from outside that component: this event stops a value that's no longer current from staying highlighted.
export const UISCALE_EVENT = 'zta:uiscale';

export function uiScale(): number {
  try {
    const v = parseInt(localStorage.getItem(UISCALE_KEY) || '', 10);
    if (!Number.isFinite(v)) return 100;
    return v < 75 ? 75 : v > 200 ? 200 : v;    // clamp: a nonsense value in storage must never leave the app unusable
  } catch { return 100; }
}

export function setUiScale(pct: number): void {
  try { localStorage.setItem(UISCALE_KEY, String(pct)); } catch { /* without localStorage: doesn't persist */ }
  void applyUiScale(pct);
  try { window.dispatchEvent(new CustomEvent(UISCALE_EVENT, { detail: pct })); } catch { /* no DOM */ }
}

// Applies the scale. With no argument it uses the persisted one (the startup call). Returns a promise so it can be
// awaited in tests; the real callers (startup / Settings) don't need to await it.
export function applyUiScale(pct?: number): Promise<void> {
  const v = pct ?? uiScale();
  // A holdover from the broken version: if CSS `zoom` was ever left on the root, ALWAYS strip it. Without this, anyone
  // who left the app at 125% would find it just as clipped after updating, unable to even reach Settings.
  try { document.documentElement.style.removeProperty('zoom'); } catch { /* no DOM: nothing to clean up */ }
  return getBridge()
    .then((b) => b.window.setZoom(v / 100).catch((e) => {
      // Missing the capability, or the webview doesn't support zoom: the app just stays at 100% and doesn't break,
      // but the failure can NOT be silent (otherwise it's "the setting does nothing" with no lead to start looking from).
      void b.support.log(`[uiscale] setZoom(${v}%) FAILED (${String(e)})`).catch(() => { /* not even a log entry */ });
    }))
    .catch(() => { /* no bridge (node tests): nothing to apply */ });
}

// UI navigation with a GAMEPAD/WHEEL (Gamepad API). Can be enabled, with configurable button bindings, all
// in localStorage (client-side). Actions = directional focus (D-pad/stick) + activate (A) + back (B) + previous/next
// tab (bumpers). The indices are the "Standard Gamepad" ones (Xbox: 0=A,1=B,4=LB,5=RB,12..15 D-pad).
export type GamepadAction = 'up' | 'down' | 'left' | 'right' | 'activate' | 'back' | 'prevTab' | 'nextTab';
export type GamepadBindings = Record<GamepadAction, number>;

// Default mapping (Standard Gamepad). Directional focus also responds to the left stick (axes 0/1),
// which is NOT rebindable (it's analog); the buttons below ARE.
export const DEFAULT_GAMEPAD_BINDINGS: GamepadBindings = {
  up: 12, down: 13, left: 14, right: 15,   // D-pad
  activate: 0,   // A
  back: 1,       // B
  prevTab: 4,    // LB
  nextTab: 5,    // RB
};

const GP_ON_KEY = 'ams2.gamepadNav';
const GP_BIND_KEY = 'ams2.gamepadBindings';

export function gamepadEnabled(): boolean {
  try { return localStorage.getItem(GP_ON_KEY) === '1'; } catch { return false; }   // default OFF (opt-in)
}
export function setGamepadEnabled(on: boolean): void {
  try { localStorage.setItem(GP_ON_KEY, on ? '1' : '0'); } catch { /* doesn't persist */ }
}

export function gamepadBindings(): GamepadBindings {
  try {
    const raw = localStorage.getItem(GP_BIND_KEY);
    if (!raw) return { ...DEFAULT_GAMEPAD_BINDINGS };
    const parsed = JSON.parse(raw) as Partial<GamepadBindings>;
    // merges on top of the default -> tolerates incomplete bindings / new keys added later
    const out = { ...DEFAULT_GAMEPAD_BINDINGS };
    for (const k of Object.keys(out) as GamepadAction[]) {
      const v = parsed[k];
      if (typeof v === 'number' && v >= 0) out[k] = v;
    }
    return out;
  } catch { return { ...DEFAULT_GAMEPAD_BINDINGS }; }
}
export function setGamepadBinding(action: GamepadAction, button: number): void {
  try {
    const cur = gamepadBindings();
    cur[action] = button;
    localStorage.setItem(GP_BIND_KEY, JSON.stringify(cur));
  } catch { /* doesn't persist */ }
}
export function resetGamepadBindings(): void {
  try { localStorage.removeItem(GP_BIND_KEY); } catch { /* doesn't persist */ }
}
