// Season results already dismissed with the ✕ (client-side, localStorage). Same pattern as
// tutorial/seen.ts and ui/prefs.ts: access wrapped in try/catch (without localStorage it just doesn't persist, never
// breaks) and corrupt content = an empty list.
//
// Why this persists: the dismissal used to live in the component's useState, so switching tabs and coming back
// (or reloading the app) remounted the component and the dismissed cards would reappear.
const KEY = 'ams2.dismissedSeasonResults';

export function dismissedResults(): string[] {
  try {
    const raw = localStorage.getItem(KEY);
    const arr: unknown = raw ? JSON.parse(raw) : [];
    return Array.isArray(arr) ? arr.filter((x): x is string => typeof x === 'string') : [];
  } catch { return []; }
}

export function dismissResult(k: string): void {
  try {
    const cur = dismissedResults();
    if (!cur.includes(k)) localStorage.setItem(KEY, JSON.stringify([...cur, k]));
  } catch { /* without localStorage: doesn't persist */ }
}
