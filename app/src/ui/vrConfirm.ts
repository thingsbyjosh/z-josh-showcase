// / — the VR panel's confirm key can also be a wheel, button-box
// or controller BUTTON. Josh (26/08): “the capture does not detect my wheel, as it does with other keys in the
// in-game options”.
//
// Why the previous approach was not enough: the capture listened to `keydown`, and a wheel button is not a Windows
// virtual key. Neither the capture saw it nor could GetAsyncKeyState (what the DLL and the SteamVR host watch
// during the race) watch it.
//
// The button is saved in config.json as text, so that the DLL (C++) and the host (Rust) read it the same:
//   · "joy:<vid>:<pid>:<n>" -> a DirectInput device (wheel, button box) identified by its
//     USB vendor and product, button n counting from 0. In a race it is read with joyGetPosEx (winmm), which gives
//     32 buttons: the same ones the browser's Gamepad API exposes.
//   · "xinput:<n>"          -> an Xbox-compatible controller; n is the index of the Gamepad API's STANDARD
//     mapping (0 = A). In a race it is read with XInputGetState, in any of the four slots.
// The DLL and the host validate the text on their own; what they do not understand they ignore and stay with the key.

export type VrConfirmButton =
  | { kind: 'joy'; vid: number; pid: number; button: number }
  | { kind: 'xinput'; button: number };

// joyGetPosEx returns the buttons in a 32-bit mask; beyond that they cannot be read in a race.
export const JOY_MAX_BUTTONS = 32;
// Standard mapping: 0..15 are buttons that XInput exposes (16 is the center button, which XInput does NOT give).
export const XINPUT_BUTTONS = 16;

// Controllers that the browser presents with standard mapping but that are NOT XInput (read via RawInput and the
// browser reorders their buttons): in a race there is no way to know which real button the index we saw is.
const NON_XINPUT_STANDARD_VENDORS = [0x054c /* Sony */, 0x057e /* Nintendo */];

const hex4 = (n: number): string => n.toString(16).padStart(4, '0');

export function encodeVrConfirmButton(b: VrConfirmButton): string {
  return b.kind === 'joy' ? `joy:${hex4(b.vid)}:${hex4(b.pid)}:${b.button}` : `xinput:${b.button}`;
}

export function decodeVrConfirmButton(raw: unknown): VrConfirmButton | null {
  if (typeof raw !== 'string') return null;
  const j = /^joy:([0-9a-f]{4}):([0-9a-f]{4}):(\d{1,2})$/i.exec(raw);
  if (j) {
    const button = Number(j[3]);
    if (button >= JOY_MAX_BUTTONS) return null;
    return { kind: 'joy', vid: parseInt(j[1], 16), pid: parseInt(j[2], 16), button };
  }
  const x = /^xinput:(\d{1,2})$/.exec(raw);
  if (x) {
    const button = Number(x[1]);
    return button < XINPUT_BUTTONS ? { kind: 'xinput', button } : null;
  }
  return null;
}

// Chromium puts the vendor and product at the end of the id: "FANATEC Wheel (Vendor: 0eb7 Product: 0020)".
export function padVendorProduct(id: string): { vid: number; pid: number } | null {
  const m = /Vendor:\s*([0-9a-f]{4})\s+Product:\s*([0-9a-f]{4})/i.exec(id);
  return m ? { vid: parseInt(m[1], 16), pid: parseInt(m[2], 16) } : null;
}

// Name to display: the id without the technical parenthesis at the end.
export function padDisplayName(id: string): string {
  return id.replace(/\s*\([^()]*(?:Vendor|XInput|STANDARD GAMEPAD)[^()]*\)\s*$/i, '').trim() || id;
}

// Translates "button `index` of device `pad`" into something the DLL and the host can watch in a race. null if that
// device cannot be read from the game (the screen explains it instead of saving something that is not going to work).
export function vrConfirmButtonFromPad(pad: { id: string; mapping: string }, index: number): VrConfirmButton | null {
  if (!Number.isInteger(index) || index < 0) return null;
  const vp = padVendorProduct(pad.id);
  if (pad.mapping === 'standard') {
    if (vp && NON_XINPUT_STANDARD_VENDORS.includes(vp.vid)) return null;
    return index < XINPUT_BUTTONS ? { kind: 'xinput', button: index } : null;
  }
  if (!vp || index >= JOY_MAX_BUTTONS) return null;
  return { kind: 'joy', vid: vp.vid, pid: vp.pid, button: index };
}

// Name of an Xbox controller's button in the standard mapping (the one the player sees printed on the controller).
const XINPUT_NAMES = ['A', 'B', 'X', 'Y', 'LB', 'RB', 'LT', 'RT', 'View', 'Menu', 'LS', 'RS', '↑', '↓', '←', '→'];
export function xinputButtonName(index: number): string {
  return XINPUT_NAMES[index] ?? String(index);
}

// Capture: the FIRST button pressed AFTER capture starts. What was already pressed at the
// start does not count (the button the capture was activated with, a pedal someone has mapped as a button):
// if it counted, the capture would close by itself right after opening.
export interface PadLike { index: number; id: string; mapping: string; buttons: ReadonlyArray<{ pressed: boolean; value: number }> }
export interface PadPress { pad: PadLike; index: number }

const down = (b: { pressed: boolean; value: number } | undefined): boolean => !!b && (b.pressed || b.value > 0.5);

export class PadPressWatcher {
  private held = new Set<string>();

  // Returns the first NEW press (rising edge) of any device, or null.
  next(pads: ReadonlyArray<PadLike>): PadPress | null {
    let found: PadPress | null = null;
    const now = new Set<string>();
    for (const pad of pads) {
      for (let i = 0; i < pad.buttons.length; i++) {
        if (!down(pad.buttons[i])) continue;
        const key = `${pad.index}|${pad.id}|${i}`;
        now.add(key);
        if (!found && !this.held.has(key)) found = { pad, index: i };
      }
    }
    this.held = now;
    return found;
  }

  // Takes as "already pressed" whatever there is right now, returning nothing.
  prime(pads: ReadonlyArray<PadLike>): void { this.held.clear(); this.next(pads); }
}
