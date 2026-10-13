// Controller/wheel reading, shared by navigation (useGamepadNav) and the Settings section.
//
// Rules the original code broke, which explain why it "doesn't detect inputs":
//
//  1. You can't just grab "the first connected gamepad." With a wheel plugged in, that first one is usually
//     the wheel, and the desk gamepad stays invisible no matter how hard you press it. Read ALL of them.
//  2. A control isn't always a button. A wheel's D-pad (and many non-standard controllers) arrives
//     as an AXIS with discrete values, not as buttons: looking only at `buttons` detects the four
//     face buttons and nothing else, which is exactly what happened with the Fanatec. That's why a binding here
//     is either "button N" or "axis N at value X."
//  3. An axis's rest position is NOT 0. Many pedals rest at ±1, and a loose D-pad can report
//     3.28. Assuming 0 makes focus jump around on its own without touching anything.
//
// And a fourth issue that isn't ours but the browser's: Chromium won't reveal a gamepad until a
// button is pressed with the window focused. Before that, getGamepads() doesn't list it, so an indicator that just
// polls will say "disconnected" forever even though the controller works fine.

export interface PadInfo { index: number; id: string; buttons: number; axes: number; mapping: string }

// One specific control on a controller. Axes store the VALUE at which they're considered active, not a sign:
// a D-pad reports a different discrete value per direction (up, up-right, right...), so
// a simple +/- could only tell two of the eight apart.
export type PadInput =
  | { kind: 'button'; index: number }
  | { kind: 'axis'; index: number; value: number };

// Tolerance when comparing an axis value. Loose enough to absorb potentiometer noise, but well
// below the jump between two adjacent D-pad positions (~0.29), so they don't get confused.
const AXIS_MATCH_TOLERANCE = 0.12;
// How far an axis has to move from ITS rest position to count as "just moved" during setup.
const AXIS_CAPTURE_DEFLECTION = 0.5;

export function connectedPads(): Gamepad[] {
  if (typeof navigator === 'undefined' || !navigator.getGamepads) return [];
  return Array.from(navigator.getGamepads()).filter((p): p is Gamepad => !!p && p.connected);
}

export function padInfo(pad: Gamepad): PadInfo {
  return { index: pad.index, id: pad.id, buttons: pad.buttons.length, axes: pad.axes.length, mapping: pad.mapping || '—' };
}

export function isPressed(b: GamepadButton): boolean {
  return b.pressed || b.value > 0.5;
}

// Serialization. Stored as text ("b3", "a9:-0.71") so both forms fit. Plain
// numeric values are still read as a button index: that's how it used to be stored, and it isn't lost.
export function encodeInput(i: PadInput): string {
  return i.kind === 'button' ? `b${i.index}` : `a${i.index}:${i.value.toFixed(3)}`;
}

export function decodeInput(raw: unknown): PadInput | null {
  if (typeof raw === 'number' && Number.isInteger(raw) && raw >= 0) return { kind: 'button', index: raw };
  if (typeof raw !== 'string') return null;
  const b = /^b(\d+)$/.exec(raw);
  if (b) return { kind: 'button', index: Number(b[1]) };
  const a = /^a(\d+):(-?\d+(?:\.\d+)?)$/.exec(raw);
  if (a) {
    const value = Number(a[2]);
    if (Number.isFinite(value)) return { kind: 'axis', index: Number(a[1]), value };
  }
  return null;
}

export function isInputActive(pad: Gamepad, input: PadInput | null): boolean {
  if (!input) return false;
  if (input.kind === 'button') return !!pad.buttons[input.index] && isPressed(pad.buttons[input.index]);
  const raw = pad.axes[input.index];
  if (raw === undefined) return false;
  return Math.abs(raw - input.value) <= AXIS_MATCH_TOLERANCE;
}

// Capturing a control during setup. Watches ALL controllers, not just one: the user presses the button
// they want and we figure out which device it was. Requiring them to pick the right device first
// is exactly what left "press a button..." hanging forever.
export interface PadInputCapture { padIndex: number; padId: string; input: PadInput }

// The first ACTIVE control of EACH device (those at rest do not appear). `rest` gives each axis's rest position
// (axes only count if they have moved clearly away from it, otherwise a loose D-pad or a pedal at rest
// would be auto-assigned).
//
// 27/09: per device, and not "the first of all". With the single-result version, a wheel with a
// fixed button (or a pedal out of its rest position) covered the whole device: the capture only accepted the chosen device,
// and that one never got to be seen because the wheel came first in the list. Each device is looked at on its own.
export function activeInputs(pads: Gamepad[], rest: AxisRest): PadInputCapture[] {
  const out: PadInputCapture[] = [];
  for (const pad of pads) {
    // The deflection of EVERY axis is computed BEFORE deciding anything: rest.relative() calibrates the rest position
    // on its first reading, so any early return would leave the axes that come later
    // uncalibrated, and they'd never be detected again (the first time they moved, that moved
    // value would become their new "rest").
    const deflection = pad.axes.map((_, a) => Math.abs(rest.relative(pad, a)));
    const b = pad.buttons.findIndex(isPressed);
    if (b >= 0) { out.push({ padIndex: pad.index, padId: pad.id, input: { kind: 'button', index: b } }); continue; }
    const a = deflection.findIndex((d) => d >= AXIS_CAPTURE_DEFLECTION);
    if (a >= 0) out.push({ padIndex: pad.index, padId: pad.id, input: { kind: 'axis', index: a, value: pad.axes[a] as number } });
  }
  return out;
}

// The first of all ("what are you pressing" indicator). Calibrates all devices even if the first already says something.
export function activeInput(pads: Gamepad[], rest: AxisRest): PadInputCapture | null {
  return activeInputs(pads, rest)[0] ?? null;
}

// 27/09 — the "release first" capture. Josh (rc.4): “you hit it and they keep changing on you”. It was entered into
// “press a button…” with the device's own A button, and on the next read A was still held, so
// A was saved as the assignment: every attempt to configure something left it set to the accept button. The
// gate admits nothing until the device being configured has passed through rest (nothing pressed, axes
// still); from then on, the first control that is activated is the right one. It only looks at THAT device: a stuck button
// on another device must not leave the capture hanging.
export class CaptureGate {
  private armed = false;
  feed(active: PadInput | null): PadInput | null {
    if (!this.armed) { if (!active) this.armed = true; return null; }
    return active;
  }
  isArmed(): boolean { return this.armed; }
}

export function inputLabel(input: PadInput | null, t: (k: string) => string): string {
  if (!input) return '—';
  return input.kind === 'button'
    ? `${t('gamepad.button')} ${input.index}`
    : `${t('gamepad.axis')} ${input.index} (${input.value.toFixed(2)})`;
}

// Axis rest positions (rule 3). The first reading of each axis is taken as its rest position: if the user
// has a pedal pressed right when the app opens, that axis stays uncalibrated until it reconnects. That's a lesser
// evil than the alternative (assuming 0 as rest makes focus loop uncontrollably with any wheel).
export class AxisRest {
  private rest = new Map<string, number>();

  relative(pad: Gamepad, axis: number): number {
    const raw = pad.axes[axis] ?? 0;
    const key = `${pad.index}:${pad.id}:${axis}`;
    const known = this.rest.get(key);
    if (known === undefined) { this.rest.set(key, raw); return 0; }
    return raw - known;
  }

  forget(): void { this.rest.clear(); }
}
