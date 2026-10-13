// The app's in-headset panel, and the key that acts as a click when pointing by gaze.
//
// Both settings live together here because they're the same feature, and because the plumbing isn't obvious:
// the panel is drawn by the DLL, so its toggle can NOT stay in localStorage like the rest of the
// shell's preferences. It has to end up in config.json, which is what the DLL reads when the game starts.
//
// And it has to be written by mutating the object loadConfig returns, never by building a new one: the file
// carries keys only the engine reads (nav_static_anchor, vr_pointer_guard...), and they survive because loadConfig
// casts the parsed result and saveConfig serializes that same object. See the warning in appConfig.ts.
import { loadConfig, saveConfig } from '../config/appConfig';
import { vrOverlayEnabled, setVrOverlayEnabled } from './prefs';
import { decodeVrConfirmButton, encodeVrConfirmButton, type VrConfirmButton } from './vrConfirm';

export const VK_M = 0x4D;   // by default: the only one we know does NOT collide with the game
// Keys that ALSO confirm or cancel in the game: picking one of these fires both actions at once.
// They aren't blocked (it's the user's own binding and their call), but a warning is shown next to it.
const AMS2_CONFIRM_VKS = [0x0D, 0x20, 0x1B, 0x08];   // Enter, Space, Esc, Backspace
export function isAms2ConfirmKey(vk: number): boolean { return AMS2_CONFIRM_VKS.includes(vk); }

// Readable name for a virtual key code. No giant lookup table: letters and digits cover 99% of the
// cases, and everything else is shown in hex so it's at least identifiable and reportable.
export function vkLabel(vk: number): string {
  if (vk >= 0x41 && vk <= 0x5A) return String.fromCharCode(vk);            // A-Z
  if (vk >= 0x30 && vk <= 0x39) return String.fromCharCode(vk);            // 0-9
  if (vk >= 0x70 && vk <= 0x87) return 'F' + (vk - 0x6F);                  // F1-F24
  const named: Record<number, string> = {
    0x0D: 'Enter', 0x20: 'Espacio', 0x1B: 'Esc', 0x08: 'Retroceso', 0x09: 'Tab',
    0x2D: 'Insert', 0x2E: 'Supr', 0x24: 'Inicio', 0x23: 'Fin', 0x21: 'PgUp', 0x22: 'PgDn',
  };
  return named[vk] ?? ('0x' + vk.toString(16).toUpperCase());
}

// default companion-panel geometry -- the SAME values as the Rust host (
// PANEL_WIDTH_M/PANEL_DIST_M), so the UI and the headset agree before anything is touched.
export const VR_PANEL_WIDTH_DEF = 2.8;
export const VR_PANEL_DIST_DEF = 1.5;
const geomOk = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v) && v > 0.1 && v < 20;

// confirmBtn: wheel/controller button that acts as the click; null = the confirmVk key rules.
export interface VrPanelSettings {
  panel: boolean; confirmVk: number; confirmBtn: VrConfirmButton | null; confirmBtnName: string; widthM: number; distM: number;
}

const fallback = (): VrPanelSettings => ({
  panel: vrOverlayEnabled(), confirmVk: VK_M, confirmBtn: null, confirmBtnName: '', widthM: VR_PANEL_WIDTH_DEF, distM: VR_PANEL_DIST_DEF,
});

// Reads both settings. config.json WINS over localStorage, because that's what the DLL actually reads.
// If the panel key doesn't exist yet (a config written before this feature shipped) it gets SEEDED with whatever the user
// already had, so the toggle and the engine agree from the very first launch.
export async function readVrPanel(): Promise<VrPanelSettings> {
  try {
    const c = await loadConfig();
    if (!c) return fallback();
    const confirmVk = c.vr_confirm_vk ?? VK_M;
    // a text that is not understood is treated as "no button": the DLL and the host do the same with it
    const confirmBtn = decodeVrConfirmButton(c.vr_confirm_btn);
    const confirmBtnName = confirmBtn && typeof c.vr_confirm_btn_name === 'string' ? c.vr_confirm_btn_name : '';
    // panel geometry; a missing or nonsensical value falls back to the default (the same clamp the host uses).
    const widthM = geomOk(c.vr_panel_width_m) ? c.vr_panel_width_m : VR_PANEL_WIDTH_DEF;
    const distM  = geomOk(c.vr_panel_dist_m)  ? c.vr_panel_dist_m  : VR_PANEL_DIST_DEF;
    if (typeof c.vr_mirror_panel === 'boolean') {
      setVrOverlayEnabled(c.vr_mirror_panel);
      return { panel: c.vr_mirror_panel, confirmVk, confirmBtn, confirmBtnName, widthM, distM };
    }
    const seed = vrOverlayEnabled();
    c.vr_mirror_panel = seed;
    await saveConfig(c);
    return { panel: seed, confirmVk, confirmBtn, confirmBtnName, widthM, distM };
  } catch {
    return fallback();
  }
}

// 23/09: AMS2 gaze click. Absent = off. Applied by the app on LAUNCH in VR (lib.rs), so the
// change holds for the next game start, not hot.
export async function readAms2Gaze(): Promise<boolean> {
  try { return (await loadConfig())?.ams2_gaze_click === true; } catch { return false; }
}
export async function writeAms2Gaze(on: boolean): Promise<void> {
  const c = await loadConfig();
  if (!c) return;
  c.ams2_gaze_click = on;
  await saveConfig(c);
}

// persists the panel geometry (config.json, see appConfig), and the reason it lives here and not in
// localStorage: the DLL will be able to read these same keys once the engine's own panel gets this knob too. The
// LIVE application to the companion is done by the caller (bridge.vr.setPanelGeometry) -- this only saves.
export async function writeVrPanelGeometry(widthM: number, distM: number): Promise<void> {
  const c = await loadConfig();
  if (!c) return;
  if (geomOk(widthM)) c.vr_panel_width_m = widthM;
  if (geomOk(distM)) c.vr_panel_dist_m = distM;
  await saveConfig(c);
}

// Turns the panel on/off. Drives BOTH implementations: the one the engine draws (config.json, the
// in-headset panel) and the app's own OpenVR one (localStorage, Rust module). On any given machine only one can be
// alive, and the user shouldn't have to know which one applies to them, so a single switch governs both.
export async function writeVrPanel(on: boolean): Promise<void> {
  setVrOverlayEnabled(on);
  const c = await loadConfig();
  if (!c) return;
  c.vr_mirror_panel = on;
  await saveConfig(c);
}

// Click key for gaze pointing. The DLL reads it at startup, so it applies the next time the game
// LAUNCHES, not live. Outside the valid range the DLL just falls back to M.
// choosing a KEY removes the button there was: the confirmation is just one, and leaving the saved button
// would make the newly chosen key never take effect.
export async function writeConfirmVk(vk: number): Promise<void> {
  const c = await loadConfig();
  if (!c) return;
  c.vr_confirm_vk = vk;
  delete c.vr_confirm_btn;
  delete c.vr_confirm_btn_name;
  await saveConfig(c);
}

// wheel/controller button as the click. The key (vr_confirm_vk) stays saved on purpose: it is the one
// the DLL and the host go back to if that device is not connected at launch.
export async function writeConfirmButton(btn: VrConfirmButton, name: string): Promise<void> {
  const c = await loadConfig();
  if (!c) return;
  c.vr_confirm_btn = encodeVrConfirmButton(btn);
  c.vr_confirm_btn_name = name;
  await saveConfig(c);
}
