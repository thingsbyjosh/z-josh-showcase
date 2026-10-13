// Tutorial tours already seen (client-side, localStorage). Same pattern as ui/prefs.ts: access
// wrapped in try/catch (without localStorage it just doesn't persist, never breaks) and corrupt = empty.
const KEY = 'ams2.tutorialSeen';

export function seenTours(): string[] {
  try {
    const raw = localStorage.getItem(KEY);
    const arr: unknown = raw ? JSON.parse(raw) : [];
    return Array.isArray(arr) ? arr.filter((x): x is string => typeof x === 'string') : [];
  } catch { return []; }
}

export function tourSeen(k: string): boolean {
  return seenTours().includes(k);
}

export function markTourSeen(k: string): void {
  try {
    const cur = seenTours();
    if (!cur.includes(k)) localStorage.setItem(KEY, JSON.stringify([...cur, k]));
  } catch { /* without localStorage: doesn't persist */ }
}

export function resetTutorial(): void {
  try { localStorage.removeItem(KEY); } catch { /* without localStorage */ }
}
