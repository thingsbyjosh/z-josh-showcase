// the app's config.json — the ONLY source of truth shared between the app and the engine.
// Lives at %LOCALAPPDATA%\CareerEngineDemo\config.json (after a rebrand; it used to be \CareerEngineDemoLegacy — loadConfig falls back to the
// legacy path if the machine hasn't migrated yet); the engine reads the same file (career_overlay.cpp / paths.cpp).
import { getBridge, joinPath, BRAND_DIR, LEGACY_BRAND_DIR } from '../shell/bridge';
import { detectLocale, type Locale } from '../i18n';

export interface AppConfig {
  schemaVersion: 1;
  api_enabled: boolean;
  api_port: number;
  api_token: string;
  // Task 6: ten languages, not just en/es. The engine only understands `lang` (0/1, see cmd.app.lang);
  // this field is what the APP persists and rereads.
  locale: Locale;
  // `dll` (optional): absolute path to career_overlay.dll for cold start. In dev it points
  // at the live dev build output, to avoid re-copying it on every compile;
  // if absent, the launcher falls back to the bundled resource (resources/career_overlay.dll) in production.
  paths: { data: string; userdata: string; gameInstall: string; gameUserDir: string; dll?: string };
  // Confirm key for gaze-pointing in VR (no controller trigger). Windows virtual-key
  // code, read by the engine at startup (career_overlay.cpp -> vrmirror::SetConfirmKey). Absent = M (0x4D).
  vr_confirm_vk?: number;
  // the confirmation can also be a wheel or controller BUTTON ("joy:<vid>:<pid>:<n>"
  // or "xinput:<n>", see ui/vrConfirm.ts). If set and the device shows up in a race, it overrides the key; if
  // the device is not there, the DLL and the host fall back to the key. The name is only for display.
  vr_confirm_btn?: string;
  vr_confirm_btn_name?: string;
  // The app panel INSIDE the headset, drawn by the engine itself (a quad inside the headset). Toggled by the
  // Settings switch. Absent = on (the engine's default since a recent update); doesn't depend on the launch
  // mode, only on whether the game loaded LibOVR. Takes effect the next time the game launches.
  vr_mirror_panel?: boolean;
  // 23/09: AMS2 GAZE click (staring at a game menu option presses it). Absent = OFF.
  // The app applies it when launching in VR by writing PointerTriggerTime into the game's VR XMLs (lib.rs,
  // apply_ams2_gaze): the game only reads it at startup.
  ams2_gaze_click?: boolean;
  // headset panel geometry chosen by the player (meters). Today it's consumed by the
  // OpenVR companion (the app pushes it live via vr_set_panel_geometry); it's persisted HERE rather than in localStorage so
  // a future version of the engine's own panel can read the same keys. Absent = 2.8 m at 1.5 m.
  vr_panel_width_m?: number;
  vr_panel_dist_m?: number;
  // cap on cars ON TRACK (0/absent = no cap). Read by the engine along TWO paths: at startup
  // inside the game (configures the race) and in ZtaPregenAllAi (writes the AI XML files before launch,
  // in a separate process). If only one of them read it, the XML and the race's grid would disagree on the number.
  max_grid?: number;
}

// CAUTION when touching loadConfig/saveConfig: the file also carries keys that only the engine reads, which aren't
// part of this interface (nav_instrument, nav_static_anchor, nav_park_cursor, vr_pointer_guard...). They survive because
// loadConfig just CASTS the parsed object and saveConfig serializes that SAME object: unknown
// properties travel through intact. If someone "cleans this up" by building a new object with only the
// typed fields, engine settings will silently get switched off (the VR panel, among others).

export function configFilePath(localAppData: string): string {
  return joinPath(localAppData, BRAND_DIR, 'config.json');
}

export function randomToken(): string {
  const bytes = new Uint8Array(16);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
}

export function defaultConfig(): AppConfig {
  return {
    schemaVersion: 1,
    api_enabled: true,
    api_port: 47123,
    api_token: randomToken(),
    // The system language, not plain English: this is the ONLY place where a new player
    // gets their language, and until today everyone got English. An already-saved config does not go through here,
    // so a player's choice is never overwritten.
    locale: detectLocale(),
    paths: { data: '', userdata: '', gameInstall: '', gameUserDir: '' },
  };
}

// After a rebrand: a config written BEFORE the folder rename still has \CareerEngineDemoLegacy\ paths, which after the
// physical migration no longer exist. They get rewritten to the new brand. Returns true if anything changed.
function rebrandLegacyPaths(cfg: AppConfig): boolean {
  let changed = false;
  const marker = `\\${LEGACY_BRAND_DIR}`;
  for (const k of Object.keys(cfg.paths) as (keyof AppConfig['paths'])[]) {
    const v = cfg.paths[k];
    if (typeof v === 'string' && v.includes(marker)) {
      cfg.paths[k] = v.split(marker).join(`\\${BRAND_DIR}`);
      changed = true;
    }
  }
  return changed;
}

// Loads the config; null if it doesn't exist or doesn't validate (forces the first-run wizard).
// Search order: branded location -> legacy (an unmigrated machine). If it was read from the branded
// location (the physical migration already happened) and legacy paths remain inside it, they get rewritten and saved.
export async function loadConfig(): Promise<AppConfig | null> {
  const b = await getBridge();
  const lad = await b.paths.localAppData();
  let path = configFilePath(lad);
  let atBrand = true;
  if (!(await b.fs.exists(path))) {
    path = joinPath(lad, LEGACY_BRAND_DIR, 'config.json');
    atBrand = false;
    if (!(await b.fs.exists(path))) return null;
  }
  try {
    const c = JSON.parse(await b.fs.readTextFile(path));
    if (!c || c.schemaVersion !== 1 || typeof c.paths !== 'object' || c.paths === null) return null;
    const cfg = c as AppConfig;
    if (atBrand && rebrandLegacyPaths(cfg)) await saveConfig(cfg);
    return cfg;
  } catch {
    return null;
  }
}

// Atomic write; creates %LOCALAPPDATA%\CareerEngineDemo if missing.
export async function saveConfig(cfg: AppConfig): Promise<void> {
  const b = await getBridge();
  const lad = await b.paths.localAppData();
  await b.fs.makeDir(joinPath(lad, BRAND_DIR));
  await b.fs.writeTextFileAtomic(configFilePath(lad), JSON.stringify(cfg, null, 2));
}

// 01/10 (player report after 0.7.0): the language chosen in the top bar was not saved.
// `pickLocale` only changed i18next, and the only place that wrote `locale` was the first-run
// wizard, so on reopening the app the language of that day came back. With two languages almost nobody noticed;
// with twelve, the first one who chose theirs lost it the next day. The whole config is written (it is the
// same object the app keeps in memory) and the choice survives a restart, just like the VR ones.
export async function saveLocale(cfg: AppConfig, locale: Locale): Promise<void> {
  cfg.locale = locale;
  await saveConfig(cfg);
}
