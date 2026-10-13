// The clock used to judge an event's window. The window (starts_at/ends_at) is an absolute INSTANT in
// UTC, so the player's timezone never shifts it: the event opens at the same instant in Madrid and in Tokyo. What DOES
// (its clock is) a badly set PC clock, and that clock shouldn't have authority over something the backend publishes for
// everyone. On sync, /v1/events returns its own time (`now`); we store that time alongside the local time of the
// fetch, and the difference is this machine's clock skew.
//
// It's applied as a skew rather than "the server tells me which ones are live" on purpose: the mod is played OFFLINE and
// the engine reads the file hours or days after the last sync, so a frozen list would start lying the moment
// the window passed. With the skew, the countdown keeps running correctly against the real time.
// The ENGINE does exactly the same thing, reading serverNow/fetchedAt from the file (career_events.cpp).
let skewMs = 0;

// skew = server time - local time at the SAME instant (the fetch). Invalid ISO -> ignored.
export function setEventClockFromFetch(serverNowIso: string | undefined, fetchedAtIso: string | undefined): void {
  if (!serverNowIso || !fetchedAtIso) return;
  const srv = Date.parse(serverNowIso);
  const loc = Date.parse(fetchedAtIso);
  if (Number.isNaN(srv) || Number.isNaN(loc)) return;
  skewMs = srv - loc;
}

// effective time for judging event windows: local time corrected by the server skew.
export function eventNow(): number {
  return Date.now() + skewMs;
}

export function eventClockSkewMs(): number {
  return skewMs;
}

// tests only: resets to "no known skew".
export function resetEventClockForTest(): void {
  skewMs = 0;
}
