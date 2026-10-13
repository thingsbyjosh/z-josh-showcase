// The app's window mode: fullscreen (takeover) or windowed, as a user PREFERENCE.
//
// The problem this fixes: the focus auto-switch brings the app to the front at four moments -- end of race,
// abandonment, a tryout verdict, and "the game is back at its menu" -- and all four used to call enterTakeover()
// unconditionally. The ❑ button only turned off fullscreen for that one instant (an in-memory boolean on the bridge), so the
// user's choice got lost on the next race or the next alt-tab. There was no way to pin it down.
//
// The contract now: whatever the user last chose with the button WINS. If they left the app windowed, it comes back
// windowed -- in the exact spot and size they left it in -- and if they left it maximized, it comes back maximized.
import { getBridge, type ShellBridge } from './bridge';
import { windowMode, setWindowMode, WINMODE_EVENT, type WindowMode } from '../ui/prefs';
import { useEffect, useState } from 'react';

function announce(mode: WindowMode): void {
  try { window.dispatchEvent(new CustomEvent<WindowMode>(WINMODE_EVENT, { detail: mode })); } catch { /* no DOM */ }
}

// Puts the window into a specific mode and brings it to front. In window mode it's UN-MINIMIZED first (coming back from
// a race the app is minimized, and positioning a minimized window doesn't always show up once it's restored), and
// afterward exitTakeover returns it to its saved geometry: position, size, and maximized if that's how it was left.
export async function applyWindowMode(b: ShellBridge, mode: WindowMode): Promise<void> {
  if (mode === 'fullscreen') {
    await b.window.enterTakeover();
  } else {
    await b.window.focus();
    await b.window.exitTakeover();
  }
  announce(mode);
}

// What the auto-switch calls: "bring the app to the front" while respecting what the user chose.
export function surfaceApp(b: ShellBridge): Promise<void> {
  return applyWindowMode(b, windowMode());
}

// The ❑/⛶ button. Toggles based on the window's REAL state (not the saved one) so pressing it always
// always does something visible, and SAVES the result as a preference: that's what the auto-switch will respect.
export async function toggleWindowMode(b?: ShellBridge): Promise<WindowMode> {
  const bridge = b ?? (await getBridge());
  const next: WindowMode = bridge.window.isTakeover() ? 'windowed' : 'fullscreen';
  setWindowMode(next);
  await applyWindowMode(bridge, next);
  return next;
}

// Current mode, for rendering the right icon. Starts from the preference and corrects itself on every real change.
export function useWindowMode(): WindowMode {
  const [mode, setMode] = useState<WindowMode>(() => windowMode());
  useEffect(() => {
    const on = (e: Event) => setMode((e as CustomEvent<WindowMode>).detail);
    window.addEventListener(WINMODE_EVENT, on);
    return () => window.removeEventListener(WINMODE_EVENT, on);
  }, []);
  return mode;
}
