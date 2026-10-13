// requested by players: a cap on cars ON TRACK.
//
// Lives in config.json rather than localStorage for the same reason as the VR panel (see ui/vrPanel.ts): it's
// applied by the DLL, not the shell. And it's applied in TWO places -- when configuring the race inside the game, and when
// writing the AI XML files before launch, which runs in a SEPARATE process. Both read this same key.
//
// It's written by MUTATING the object loadConfig returns, never by building a new one: the file carries
// keys that only the engine reads, and they survive because saveConfig serializes that same parsed object.
import { loadConfig, saveConfig } from '../config/appConfig';

/** 0 = no cap. The engine ignores any value < 2 (a grid of one isn't a race). */
export const GRID_CAP_OFF = 0;
/** The steps offered. 32 is the number a player reported as their comfortable ceiling. */
export const GRID_CAP_CHOICES = [GRID_CAP_OFF, 32, 24, 20, 16] as const;

export function isValidGridCap(n: number): boolean {
  return n === GRID_CAP_OFF || (Number.isInteger(n) && n >= 2);
}

export async function readGridCap(): Promise<number> {
  try {
    const c = await loadConfig();
    const n = c?.max_grid;
    return typeof n === 'number' && isValidGridCap(n) ? n : GRID_CAP_OFF;
  } catch {
    return GRID_CAP_OFF;
  }
}

/** Applies the next time the game LAUNCHES: the engine reads the key at startup and while pre-generating. */
export async function writeGridCap(n: number): Promise<void> {
  if (!isValidGridCap(n)) return;
  const c = await loadConfig();
  if (!c) return;
  c.max_grid = n;
  await saveConfig(c);
}
