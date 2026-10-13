// Time formatting shared by the Results/Times screens. Mirrors the native overlay's fmtT/fmtGap
// (screen_results.cpp / screen_championships.cpp): seconds -> time; <=0 => "--".

// withMs includes .mmm (for laps); >1h uses H:MM:SS; otherwise M:SS(.mmm).
export function formatTime(secs: number, withMs: boolean): string {
  if (secs <= 0) return '—';
  const totalMs = Math.round(secs * 1000);
  const ms = totalMs % 1000;
  const s = Math.floor(totalMs / 1000) % 60;
  const m = Math.floor(totalMs / 60000) % 60;
  const h = Math.floor(totalMs / 3600000);
  const pad = (n: number, w = 2): string => String(n).padStart(w, '0');
  if (h > 0) return `${h}:${pad(m)}:${pad(s)}`;
  if (withMs) return `${m}:${pad(s)}.${pad(ms, 3)}`;
  return `${m}:${pad(s)}`;
}

// credits formatted like "Cr 28.000" (thousands with a DOT), mirroring the native overlay's fmtCrK (sponsors/calendar). Different
// from Home/Results/topbar's currency-symbol format (toLocaleString en-US) -- this one uses the Cr symbol and a dot separator.
export function formatCreditsCr(n: number): string {
  return 'Cr ' + Math.round(n).toLocaleString('de-DE');
}

// time gap to the leader -> "+S.mmm" / "+M:SS.mmm"; <=0 => "--". Mirrors fmtGap.
export function formatGap(gapSecs: number): string {
  if (gapSecs <= 0) return '—';
  const totalMs = Math.round(gapSecs * 1000);
  const ms = totalMs % 1000;
  const s = Math.floor(totalMs / 1000);
  const pad = (n: number, w = 2): string => String(n).padStart(w, '0');
  if (s >= 60) return `+${Math.floor(s / 60)}:${pad(s % 60)}.${pad(ms, 3)}`;
  return `+${s}.${pad(ms, 3)}`;
}

// P2 (F15): retirement. `finished === false` is what the engine sends; `pos <= 0` is the old convention, which
// nobody wrote but may arrive from an old DLL. With the position known both things are shown, “DNF (P24)”:
// retiring while leading is not the same as retiring last.
export function isDnf(pos: number, finished?: boolean): boolean {
  return finished === false || pos <= 0;
}
export function dnfLabel(pos: number, dnf = 'DNF'): string {
  return pos > 0 ? `${dnf} (P${pos})` : dnf;
}

// race duration in the unit people actually think in. "90 min" reads as a small
// number; "1h 30m" is an hour and a half and you can see it coming -- which is exactly the surprise a player reported after
// picking 25% of a historical-era race. Under an hour it stays in minutes, and the "m" is the same
// compact abbreviation the pre-race screen already used in its header (no translation needed).
export function formatRaceLength(minutes: number): string {
  const m = Math.max(0, Math.round(minutes));
  if (m < 60) return `${m}m`;
  const h = Math.floor(m / 60);
  const rest = m % 60;
  return rest === 0 ? `${h}h` : `${h}h ${rest}m`;
}
