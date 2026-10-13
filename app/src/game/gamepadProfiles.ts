// PER-DEVICE controller profiles. There used to be a single button configuration for everything, which
// doesn't work once you have more than one device plugged in at a time — which is normal at a
// sim racing rig: a wheel, a button box, and a desk gamepad. Each device keeps its own
// bindings and they all work AT THE SAME TIME; there's no need to pick which one is "active".
//
// A binding can be a button or an AXIS (see gamepads.ts): a wheel's D-pad doesn't arrive as a button.
//
// Resolution order: that specific device's profile -> the previous global configuration (so users don't lose
// what they already had saved) -> the Standard Gamepad's default mapping.
import {
  DEFAULT_GAMEPAD_BINDINGS, gamepadBindings,
  type GamepadAction, type GamepadBindings,
} from '../ui/prefs';
import { decodeInput, encodeInput, type PadInput } from './gamepads';

const PROFILES_KEY = 'ams2.gamepadProfiles';

export type GamepadProfiles = Record<string, Partial<Record<GamepadAction, string>>>;
export type ResolvedBindings = Record<GamepadAction, PadInput | null>;

export const GAMEPAD_ACTIONS = Object.keys(DEFAULT_GAMEPAD_BINDINGS) as GamepadAction[];

export function loadProfiles(): GamepadProfiles {
  try {
    const raw = localStorage.getItem(PROFILES_KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw) as unknown;
    if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) return {};
    return parsed as GamepadProfiles;
  } catch { return {}; }
}

function saveProfiles(p: GamepadProfiles): void {
  try { localStorage.setItem(PROFILES_KEY, JSON.stringify(p)); } catch { /* storage full or blocked */ }
}

// Effective controls for a device. A value that can't be decoded falls back to null instead of half-working:
// an action with no control does nothing, which is preferable to one pointing at an impossible control.
export function bindingsFor(deviceId: string): ResolvedBindings {
  const legacy: GamepadBindings = { ...DEFAULT_GAMEPAD_BINDINGS, ...gamepadBindings() };
  const own = loadProfiles()[deviceId] ?? {};
  const out = {} as ResolvedBindings;
  for (const a of GAMEPAD_ACTIONS) {
    out[a] = a in own ? decodeInput(own[a]) : decodeInput(legacy[a]);
  }
  return out;
}

export function setBindingFor(deviceId: string, action: GamepadAction, input: PadInput): void {
  const all = loadProfiles();
  all[deviceId] = { ...(all[deviceId] ?? {}), [action]: encodeInput(input) };
  saveProfiles(all);
}

// Clears that device's profile (falls back to inheriting the defaults), without touching the others.
export function resetProfileFor(deviceId: string): void {
  const all = loadProfiles();
  if (!(deviceId in all)) return;
  delete all[deviceId];
  saveProfiles(all);
}

export function hasProfile(deviceId: string): boolean {
  return deviceId in loadProfiles();
}
