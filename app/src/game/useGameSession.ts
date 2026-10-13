// This showcase does not include the game launcher (see the licence and README): the real
// useGameSession.ts starts AMS2 through Steam, brings the engine up with it and watches the game
// process, none of which exists in a browser, where this app only ever runs on demo data. This
// module keeps the same exported shape: the session stays idle, and Play reports that there is
// no game to start here.
import { useCallback, useState } from 'react';
import type { SessionState } from './gameSession';

export interface UseGameSession {
  state: SessionState;
  play: (opts?: { force?: boolean }) => void;
  everConnected: boolean;
}

const NO_GAME_IN_A_BROWSER = 'There is no game to start here: this showcase runs in a browser, on demo data.';

// Whether the app may bring itself to the front: never with the auto-switch off, always in flat
// mode, and in the VR modes only when the headset panel is off. Same rule as the desktop app.
export function shouldTakeoverDesktop(mode: string, autoOn: boolean, vrPanelOn: boolean): boolean {
  if (!autoOn) return false;
  if (mode === 'flat') return true;
  return !vrPanelOn;
}

export async function closeAppGracefully(client: { close?: () => void }): Promise<void> {
  try { client.close?.(); } catch { /* already closed */ }
}

export async function pushEntitlements(_client: unknown): Promise<void> {
}

export function useGameSession(_cfg: unknown, _client: unknown): UseGameSession {
  const [state, setState] = useState<SessionState>({ phase: 'idle', message: '' });
  const play = useCallback(() => {
    setState({ phase: 'error', message: NO_GAME_IN_A_BROWSER });
  }, []);
  return { state, play, everConnected: false };
}
