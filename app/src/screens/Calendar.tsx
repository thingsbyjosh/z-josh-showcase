// Calendar / master calendar: temporal spine of real series, ported from the design
// (screen_calendar.jsx), bound to the read-only contract (query.calendar). ORDER and
// DATA come from the overlay (screen_calendar.cpp): one card per series ROUND, grouped by
// date (day-of-year), with status resolved by the engine. DESIGN owns the VISUALS. Status labels
// and format are engine i18n keys, resolved via t() (fallback = the key). Write
// actions (enroll/launch, time-skip) travel through cmd.* and are never handled here.
// The two time advances (next AVAILABLE event / next ENROLLED event) ALWAYS go through
// TimeAdvancePopup (the popup IS the review: feed of what is crossed + no-shows + stop rules);
// no button runs cmd.time.advance in a single click.
import { eraSkin } from '../ui/eraSkin';
import { useEffect, useMemo, useRef, useState } from 'react';
import { logImageError, supportLog } from '../support/supportLog';
import { useTranslation } from 'react-i18next';
import type {
  ApiClientLike,
  CalendarPayload,
  CalendarSlot,
  CalendarDiscipline,
  CalendarSlotStatus,
  TimeAdvancePayload,
} from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Btn, Chip, LockIcon, inputStyle } from '../ui/components';
import { dismissedResults, dismissResult } from '../ui/dismissedResults';
import { DISC_META, DISC_ORDER } from '../ui/disciplines';
import { useTrackMapUrl } from '../ui/trackImages';
import { useCarLiveries } from '../ui/vehImages';
import { classDisplayName } from '../ui/classNames';
import { SeriesLogo, seriesLogoSrc } from '../ui/SeriesLogo';
import { TimeAdvancePopup } from '../components/TimeAdvancePopup';
import { monthNames as mesesTraducidos } from '../i18n';

// Task C.3 (time advance): TS MIRROR of dll/career/career_dateutil.h (epoch abs_day=0 == 2025-01-01,
// same leap years). Used ONLY to translate the date the player picked (date picker +
// "jump to next year" button) to an untilAbsDay: TimeAdvancePopup resolves the REAL target with
// query.time.preview and executes with those SAME params; the advance itself is NEVER recomputed on the client.
const CAL_EPOCH_YEAR = 2025;
function isLeapYear(y: number): boolean {
  return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
}
function daysInYear(y: number): number {
  return isLeapYear(y) ? 366 : 365;
}
function daysInMonth(y: number, m: number): number {
  const d = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  if (m === 2 && isLeapYear(y)) return 29;
  return d[(m - 1 + 12) % 12];
}
function daysBeforeYear(year: number): number {
  let total = 0;
  if (year >= CAL_EPOCH_YEAR) { for (let y = CAL_EPOCH_YEAR; y < year; y++) total += daysInYear(y); }
  else { for (let y = year; y < CAL_EPOCH_YEAR; y++) total -= daysInYear(y); }
  return total;
}
function doyFromYmd(y: number, m: number, d: number): number {
  let doy = d;
  for (let mm = 1; mm < m; mm++) doy += daysInMonth(y, mm);
  return doy;
}
function absDayFromYmd(y: number, m: number, d: number): number {
  return daysBeforeYear(y) + (doyFromYmd(y, m, d) - 1);
}
function absDayFromYearDoy(year: number, doy: number): number {
  return daysBeforeYear(year) + (doy - 1);
}
// 18/09 (Josh's decision) — ON THE LAST DAY OF THE YEAR NOTHING IS ASKED.
//
// The clock ALWAYS brakes on 31 December: it is a hard stop of the engine, not a player rule. So
// when you are already there, “advance” can only mean one thing — cross into the new year — and opening a
// dialog that asks where you want to stop is noise: there is nowhere to stop, the destination is tomorrow.
// The dialog is skipped, ONE day is advanced and that triggers the winter report.
//
// The rest of the year the dialog stays as it is, including the jump-to-next-year button: there
// there may be stops along the way and your rules still apply.
function esFinDeAno(year: number, doy: number): boolean {
  return doy >= daysInYear(year);
}

// threshold (px) to consider the list scroll "near the end" -> the next-year button appears
const NEAR_END_PX = 600;

// Date picker (fix): 3 <select> day/month/year instead of a native <input type="date"> — native
// controls don't inherit the design (light background + system font in a dark theme) and the
// "dropdown" they open is the browser's own calendar, which can't be styled. Same pattern already
// used for the birth-date field (day/month/year + shared inputStyle).
// The YEAR dropdown is clamped at render time to [today's year .. +8] (matches the engine's
// max-years-forward limit — it never looks for a race more than 8 years ahead) and never offers
// past years (the clock never runs backward).
const JUMP_MAX_YEARS_FORWARD = 8;

// Disciplines present + color and bilingual label (color/label belong to DESIGN; the engine
// hardcodes these labels outside the translation table).

// Resolved status -> chip i18n key + color (colors are owned by DESIGN).
// color = badge fill (color-mix); textColor = 10px label (accent-text for AA contrast; otherwise matches color).
const STATUS_META: Record<CalendarSlotStatus, { key: string; color: string; textColor: string }> = {
  completed: { key: 'completed',      color: 'var(--green)',  textColor: 'var(--green)' },
  dnf:       { key: 'champ_dnf',      color: 'var(--text-3)', textColor: 'var(--text-3)' },
  locked:    { key: 'locked',         color: 'var(--text-3)', textColor: 'var(--text-3)' },
  traveled:  { key: 'champ_traveled', color: 'var(--text-3)', textColor: 'var(--text-3)' },
  // hub closed grid without a contract -- the seat comes from the market (hint in the chip's title)
  invite:    { key: 'champ_by_invite', color: 'var(--text-3)', textColor: 'var(--text-3)' },
  clash:     { key: 'champ_clash',    color: 'var(--amber)',  textColor: 'var(--amber)' },
  enrolled:  { key: 'champ_enrolled', color: 'var(--accent)', textColor: 'var(--accent-text)' },
  past:      { key: 'champ_past',     color: 'var(--text-3)', textColor: 'var(--text-3)' },
  upcoming:  { key: 'champ_upcoming', color: 'var(--text-2)', textColor: 'var(--text-2)' },
};

// 142000 -> "Cr 142.000" (dot-separated thousands; mirrors the overlay's currency formatter).
function fmtCredits(n: number): string {
  const neg = n < 0;
  const s = Math.abs(n).toString();
  let out = '';
  for (let i = 0; i < s.length; i++) {
    if (i > 0 && (s.length - i) % 3 === 0) out += '.';
    out += s[i];
  }
  return 'Cr ' + (neg ? '-' : '') + out;
}

// day-of-year -> "Feb 15", the SAME format (month day) the calendar slots already use, so a day
// injected by a market event reads the same as one with races. Mirrors the engine's date-label formatter.
const MON_ABBR = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
function dateFromDoy(year: number, doy: number): string {
  if (doy < 1) return '';
  let d = doy;
  for (let m = 1; m <= 12; m++) {
    const len = daysInMonth(year, m);
    if (d <= len) return `${MON_ABBR[m - 1]} ${d}`;
    d -= len;
  }
  return '';
}

// "Feb 14" -> { month: "Feb", day: "14" } for the day column.
function splitDate(date: string): { month: string; day: string } {
  const sp = date.indexOf(' ');
  if (sp < 0) return { month: '', day: date };
  return { month: date.slice(0, sp), day: date.slice(sp + 1) };
}

// D1545023970431213599: a single day -> "Feb 15"; a range -> "Feb 15–17". Same format as the cards,
// so the clash warning reads with the SAME dates as the rest of the calendar.
function fmtDoyRange(year: number, lo: number, hi: number): string {
  return lo === hi ? dateFromDoy(year, lo) : `${dateFromDoy(year, lo)}–${dateFromDoy(year, hi)}`;
}

// D1545023970431213599: a clash, with the range of days (winLoDoy/winHiDoy resolved by the engine) that causes it.
interface ClashInfo { name: string; lo: number; hi: number }

// (duration nuance): a race's time window, mirrors the engine's calendar module (same parameters).
// Used for the client-side conflict warning; the engine is the source of truth for DNFs. If you change
// these values, change them in the engine too (AVG_LAP_MIN / RACE_GAP_BUFFER_MIN / DEFAULT_RACE_MIN).
const AVG_LAP_MIN = 2.0, RACE_GAP_BUFFER_MIN = 30, DEFAULT_RACE_MIN = 30;
function estRaceMinutes(s: CalendarSlot): number {
  if (s.durationMin > 0) return s.durationMin;
  if (s.laps > 0) return Math.ceil(s.laps * AVG_LAP_MIN);
  return DEFAULT_RACE_MIN;
}
function raceWindowsOverlap(a: CalendarSlot, b: CalendarSlot): boolean {
  const as = a.hour * 60, ae = as + estRaceMinutes(a) + RACE_GAP_BUFFER_MIN;
  const bs = b.hour * 60, be = bs + estRaceMinutes(b) + RACE_GAP_BUFFER_MIN;
  return as < be && bs < ae;
}

type Avail = 'all' | 'open' | 'locked' | 'enrolled';   // + enrolled-series filter
type Era = 'all' | 'modern' | 'historic';

// Filter persists PER GAME SESSION: module-level store that survives unmount when navigating
// between tabs (not an app restart). So coming back to the Calendar keeps the filter set.
const sessionFilter: { avail: Avail; era: Era; discs: CalendarDiscipline[] } = { avail: 'all', era: 'all', discs: [] };

// Single-select segmented button (STATUS / ERA).
function Seg({ active, onClick, label, color }: { active: boolean; onClick: () => void; label: string; color?: string }) {
  return (
    <button
      onClick={onClick}
      className="cal-seg focusable"
      style={{
        borderColor: active ? (color ?? 'var(--accent)') : 'var(--line-2)',
        background: active ? (color ?? 'var(--accent)') : 'var(--surface-2)',
        color: active ? (color ? '#15140f' : 'var(--accent-ink)') : 'var(--text-2)',
      }}
    >
      {label}
    </button>
  );
}

// Thumbnail of a car from the field (on hover). The hook lives here (child component) so it isn't
// called inside a loop in renderCard. Empty slug/dataDir or missing PNG -> placeholder box.
function FieldThumb({ dataDir, slug, name }: { dataDir?: string; slug: string; name: string }) {
  const { nns, url } = useCarLiveries(dataDir, slug);
  const src = nns.length ? url(nns[0]) : '';
  return (
    <div title={name} style={{ width: 72, height: 40, borderRadius: 4, overflow: 'hidden', background: 'var(--surface-3)', border: '1px solid var(--line-2)', flexShrink: 0 }}>
      {src && <img src={src} alt={name} loading="lazy" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ width: '100%', height: '100%', objectFit: 'cover' }} />}
    </div>
  );
}

// onRollover: the clock advance can CROSS the year from any of the four popups on this screen
// (date, series, "jump to year N+1", scope). The crossing comes in the payload (`crossedYear`) and the
// end-of-season review must be opened right here: the Hub watcher (which detects it by seeing hub.lifecycle.year rise)
// only finds out if the player returns to the Hub, so crossing the year from the Calendar skipped the Rollover.
export function Calendar({ client, dataDir, onEnterPrerace, onRollover }: { client: ApiClientLike; dataDir?: string; onEnterPrerace?: () => void; onRollover?: () => void }) {
  const { t, i18n } = useTranslation();
  const q = useQuery<CalendarPayload>(client, 'query.calendar');
  const trackUrl = useTrackMapUrl(dataDir);   // Real circuit map keyed by trackSlug (PNG served from data/)
  const scrollRef = useRef<HTMLDivElement>(null);

  // Launching a race from a launchable slot -> cmd.calendar.launch arms the state (SampleState) and,
  // if launched, the app navigates to Prerace (onEnterPrerace). launched=false = insufficient balance -> stays on the calendar.
  const launch = (seriesId: string, round: number) => {
    void client.cmd<{ ok: boolean; launched: boolean }>('cmd.calendar.launch', { seriesId, round })
      // hub a launched:false with NO trace was invisible (14 series "do nothing"); the engine should no
      // longer reject anything clickable, so if it happens, log it to zta_support instead of dying silently.
      .then((r) => { if (r?.launched) onEnterPrerace?.(); else supportLog(client, `calendar.launch REFUSED: ${seriesId} R${round} (clickable card but the engine said no)`); })
      .catch(() => { /* mock/offline: no effect */ });
  };
  // Onboarding: launches the CALIBRATION race (adaptive difficulty). Same path as a normal slot: arms
  // SampleState and, if launched, enters Prerace (car/track/duration locked). Without this, a new profile
  // got stuck (no calibration -> no calendar or market). The "Start calibration" button had no onClick handler.
  const startCalibration = () => {
    void client.cmd<{ ok: boolean; launched: boolean }>('cmd.calendar.launchCalibration', {})
      .then((r) => { if (r?.launched) onEnterPrerace?.(); })
      .catch(() => { /* mock/offline: no effect */ });
  };
  const [avail, setAvail] = useState<Avail>(sessionFilter.avail);
  const [era, setEra] = useState<Era>(sessionFilter.era);
  const [discs, setDiscs] = useState<CalendarDiscipline[]>(sessionFilter.discs);
  // Tester suggestion (hub 0.6.5): the "Requires superlicense" chip jumps to the round of the qualifier that
  // unlocks it. It clears the filters first (the qualifier might be filtered out), lets the re-render
  // paint its card, and then scrolls to it with a gold flash so the eye finds it.
  const [flashSlot, setFlashSlot] = useState<string | null>(null);
  const jumpToQualifier = (qualId: string) => {
    setAvail('all'); setEra('all'); setDiscs([]);
    setFlashSlot(qualId);
    window.setTimeout(() => {
      document.querySelector(`[data-slot-series="${qualId}"]`)?.scrollIntoView({ behavior: 'smooth', block: 'center' });
    }, 60);
    window.setTimeout(() => setFlashSlot(null), 2600);
  };
  // Dismissal is PERSISTED (localStorage): it used to be component-only state, so returning to the
  // tab remounted it and dismissed cards reappeared.
  const [dismissed, setDismissed] = useState<string[]>(() => dismissedResults());
  // Season result from which "go to next event" is requested for THAT championship.
  const [resultJumpSeries, setResultJumpSeries] = useState<string | null>(null);
  // D1545023970431213599: each clash carries its own range of days (lo/hi, doy resolved by the engine) so
  // the warning can explain WHY they overlap, not just WITH WHOM.
  const [pending, setPending] = useState<{ seriesId: string; round: number; year: number; ownLo: number; ownHi: number; clashes: ClashInfo[] } | null>(null);   // conflict confirmation
  const [hoverId, setHoverId] = useState<string | null>(null);   // round with active hover (field thumbnails)
  // The thumbnail overlay waits ~500ms before showing (just passing the mouse over on the way to
  // another card no longer triggers it). The timer is cancelled on mouse-leave or unmount.
  const hoverTimer = useRef<number | null>(null);
  useEffect(() => () => { if (hoverTimer.current != null) window.clearTimeout(hoverTimer.current); }, []);
  const hoverEnter = (id: string) => {
    if (hoverTimer.current != null) window.clearTimeout(hoverTimer.current);
    hoverTimer.current = window.setTimeout(() => { hoverTimer.current = null; setHoverId(id); }, 500);
  };
  const hoverLeave = (id: string) => {
    if (hoverTimer.current != null) { window.clearTimeout(hoverTimer.current); hoverTimer.current = null; }
    setHoverId((cur) => (cur === id ? null : cur));
  };
  // Special-event teasers are NO LONGER rendered here — the calendar is the series agenda;
  // events live in the hub (carousel) and in their own grid (EventsScreen). The fixed strip of
  // locked cards used to sit at the top of the calendar as if it were just another entry.
  // syncs the filter to the session store so it persists across navigation
  useEffect(() => { sessionFilter.avail = avail; sessionFilter.era = era; sessionFilter.discs = discs; }, [avail, era, discs]);
  // Escape closes the conflict-confirmation modal (keyboard-operable, no mouse needed)
  useEffect(() => {
    if (!pending) return;
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') setPending(null); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [pending]);

  const allSlots = useMemo(() => q.data?.slots ?? [], [q.data]);

  // conflict warning. Series already COMMITTED (enrolled) that day that would clash with 'slot'.
  // Travel + DURATION nuance (client-side mirror of the engine's slot-conflict check): at the SAME
  // circuit you can run both ONLY if their time windows (start..start+duration+buffer) don't overlap —
  // a 66-lap race leaves no room for another one that day; at a different circuit the same day DOES clash (no time to travel).
  // Memories (per player feedback): an ERA round is free time — it doesn't occupy the weekend, so it
  // neither triggers the warning nor suffers one (same cutoff the engine's slot-conflict check uses, which already exempts era series). Without
  // this mirror, the app used to warn about a clash — and a DNF — that the engine no longer enforces.
  // Bug reported ("Why can't I do the Jun 30 race?"): this used to compare `o.doy !== slot.doy` -> it warned
  // only for a same-day clash, while the engine DNFs on overlap of the occupied WINDOW (the series'
  // footprint: a top-formula weekend spans Fri-Sun, an endurance round spans the whole week). Result: rounds the engine silently killed,
  // with no warning dialog. Now the window comes RESOLVED from the engine (window-start/window-end) so
  // the mirror can't diverge from the real footprint; without those fields (older engine) it falls back to the single-day check as before.
  const occupiedDays = (s: CalendarSlot): [number, number] =>
    (s.winLoDoy != null && s.winHiDoy != null) ? [s.winLoDoy, s.winHiDoy] : [s.doy, s.doy];
  // D1545023970431213599 (Josh's note): the warning said WHAT clashed but not WHY — now each clash carries
  // the range of days resolved by the engine (winLoDoy/winHiDoy), so it is seen at a glance that the two
  // windows overlap instead of having to trust the warning's word.
  const clashDetails = (slot: CalendarSlot): ClashInfo[] => {
    if (slot.historic) return [];
    const [slo, shi] = occupiedDays(slot);
    const out: ClashInfo[] = []; const seen = new Set<string>();
    for (const o of allSlots) {
      if (o.seriesId === slot.seriesId || !o.enrolled || o.historic) continue;
      if (o.status === 'completed' || o.status === 'dnf') continue;
      if (o.year !== slot.year) continue;                                     // the year matters: day-of-year alone isn't a unique day key
      const [olo, ohi] = occupiedDays(o);
      if (!(olo <= shi && slo <= ohi)) continue;                              // no day overlap -> no clash
      if (o.doy === slot.doy && o.trackHash === slot.trackHash && !raceWindowsOverlap(o, slot)) continue;  // same day+circuit, non-overlapping windows: both fit
      if (!seen.has(o.seriesName)) { seen.add(o.seriesName); out.push({ name: o.seriesName, lo: olo, hi: ohi }); }
    }
    return out;
  };
  const tryLaunch = (slot: CalendarSlot) => {
    const clashes = clashDetails(slot);
    if (clashes.length) {
      const [ownLo, ownHi] = occupiedDays(slot);
      setPending({ seriesId: slot.seriesId, round: slot.round, year: slot.year, ownLo, ownHi, clashes });
    } else launch(slot.seriesId, slot.round);
  };

  // Time-skip: projected queue for NEXT year (backend: first 6 rounds, always
  // clickable:false/isAvailable:false). Sorted by day-of-year -> [0] is the round that anchors the
  // "jump to next year" button and supplies the year for its label (never hardcoded).
  const nextYearRounds = useMemo(() => {
    const ty = q.data?.today.year ?? 0;
    return allSlots.filter((s) => s.year > ty).slice().sort((a, b) => a.doy - b.doy);
  }, [allSlots, q.data]);
  const firstNextYearRound = nextYearRounds[0] ?? null;

  // date picker (item 1) + "jump to next year" button (item 3): both open TimeAdvancePopup with
  // an untilAbsDay already computed on the client; the popup asks for its own preview with THAT param and executes with the
  // same one (never recomputed here).
  // Per feedback: picking a date NEVER opens the dialog by itself (it used to fire on every onChange).
  // is kept in local state and only the explicit "Go to date" button computes the untilAbsDay and opens the popup;
  // that button stays disabled until a valid date is set.
  const [dateJumpAbsDay, setDateJumpAbsDay] = useState<number | null>(null);
  const [showYearJump, setShowYearJump] = useState(false);
  const [pickedDay, setPickedDay] = useState(0);
  const [pickedMonth, setPickedMonth] = useState(0);
  const [pickedYear, setPickedYear] = useState(0);
  const pickedDateAbsDay = useMemo(() => {
    if (!pickedDay || !pickedMonth || !pickedYear) return null;
    return absDayFromYmd(pickedYear, pickedMonth, pickedDay);
  }, [pickedDay, pickedMonth, pickedYear]);
  const goToPickedDate = () => {
    if (pickedDateAbsDay != null) setDateJumpAbsDay(pickedDateAbsDay);
  };

  // Two distinct semantics: "Next available" resolves the next event you can enter
  // (scope:"available", replacing the old ADVANCE TIME/NEXT EVENT buttons that did the SAME thing); "Next
  // enrolled" resolves the next event you're ALREADY enrolled in (scope:"enrolled"). Both always go
  // by TimeAdvancePopup (feed + no-shows + rules); none runs cmd.time.advance in a single click.
  // 'advance' = advance WITHOUT a destination: the engine reads it as “until the first stop” by your rules.
  const [jumpScope, setJumpScope] = useState<'available' | 'enrolled' | 'advance' | null>(null);
  const [showDatePicker, setShowDatePicker] = useState(false);

  // "Jump to next year" button: appears when scrolling near/reaching the end of the list (if the
  // whole list already fits without scrolling, you're already "at the end" -> it shows immediately).
  const [nearEnd, setNearEnd] = useState(false);
  useEffect(() => {
    const el = scrollRef.current;
    if (!el) return;
    const check = () => setNearEnd(el.scrollHeight - el.scrollTop - el.clientHeight <= NEAR_END_PX);
    check();
    el.addEventListener('scroll', check);
    return () => el.removeEventListener('scroll', check);
  }, [q.data]);

  // disciplines present in the calendar (so we don't offer empty filters)
  const presentDiscs = useMemo(() => {
    const set = new Set(allSlots.map((s) => s.discipline));
    return DISC_ORDER.filter((d) => set.has(d));
  }, [allSlots]);

  // client-side filter over the read-only slots (STATUS / ERA / DISCIPLINE)
  const filtered = useMemo(
    () =>
      allSlots.filter((s) => {
        if (avail === 'open' && !s.isAvailable) return false;
        if (avail === 'locked' && !s.isLocked) return false;
        if (avail === 'enrolled' && !s.enrolled) return false;   //
        if (era === 'modern' && s.historic) return false;
        if (era === 'historic' && !s.historic) return false;
        if (discs.length && !discs.includes(s.discipline)) return false;
        return true;
      }),
    [allSlots, avail, era, discs],
  );

  // Group by day and sort ascending. day-of-year alone is NOT a unique day key — the backend appends
  // next year's queue to the SAME array (day-of-year restarts at 1), so the group key and ordering
  // ALWAYS use (year, day-of-year); grouping by day-of-year alone would merge a queued day with a
  // current-year day landing on the same day-of-year.
  const days = useMemo(() => {
    const map = new Map<string, { year: number; doy: number; list: CalendarSlot[]; markKey?: string; markKind?: 'window' | 'decision' }>();
    for (const s of filtered) {
      const key = `${s.year}:${s.doy}`;
      const g = map.get(key);
      if (g) g.list.push(s);
      else map.set(key, { year: s.year, doy: s.doy, list: [s] });
    }
    // THE DAYS OF THE DECISION WEEK. The calendar only renders days that have a race, so
    // the seven days of the signing window weren't showing up blank — they DIDN'T EXIST as a row. A stretch
    // that isn't drawn reads as "nothing happens here", and this is the week that decides your season.
    //
    // Only the decision week is expanded DAY BY DAY. The window lasts three and a half months:
    // expanding all of it would seed a hundred filler rows and bury the calendar, so only its first day
    // gets marked and that's it. What the agenda needs to show is what DEMANDS your presence, not everything that's simply open.
    const cy = q.data?.today.year ?? 0;
    for (const m of q.data?.marketMarks ?? []) {
      const span = m.kind === 'decision' ? Math.max(1, m.days) : 1;
      for (let k = 0; k < span; k++) {
        const doy = m.doy + k;
        const key = `${cy}:${doy}`;
        const g = map.get(key);
        // a day that ALREADY has a race keeps its cards; the marker is just noted on top
        if (g) { g.markKey = m.dayLabelKey; g.markKind = m.kind; }
        else map.set(key, { year: cy, doy, list: [], markKey: m.dayLabelKey, markKind: m.kind });
      }
    }
    // T169: within the same day, AVAILABLE races go first (Array.sort is stable -- the
    // relative order between races with the same availability state does not change, only the actionable is put first).
    for (const g of map.values()) g.list.sort((a, b) => (a.isAvailable === b.isAvailable ? 0 : a.isAvailable ? -1 : 1));
    return [...map.values()].sort((a, b) => a.year - b.year || a.doy - b.doy);
  }, [filtered, q.data]);

  // jumps to the first block on/after TODAY (fallback: the first one); reused by the TODAY pill as a button
  const scrollToToday = () => {
    if (!scrollRef.current) return;
    const el =
      scrollRef.current.querySelector<HTMLElement>('[data-today="1"]') ??
      scrollRef.current.querySelector<HTMLElement>('[data-day]');
    el?.scrollIntoView?.({ block: 'start' });
  };
  // the calendar "follows you": on load, it jumps to TODAY
  useEffect(() => { scrollToToday(); }, [q.data]);

  if (q.loading)
    return (
      <div className="col center grow" style={{ height: '100%' }}>
        <Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono>
      </div>
    );
  if (q.error)
    return (
      <div className="col center grow" style={{ height: '100%' }}>
        <Mono style={{ color: 'var(--text-3)' }}>{t('home.noCareer')}</Mono>
      </div>
    );
  const data = q.data;
  if (!data) return null;

  const es = (i18n.language ?? '').toLowerCase().startsWith('es');

  // ONBOARDING GATE: until calibrated, only the calibration race is available.
  if (!data.calibrated) {
    const cal = data.calibration;
    return (
      <div className="screen col center grow" style={{ height: '100%', gap: 18, padding: 34 }}>
        <div className="col center gap8">
          <Display as="h1" style={{ fontSize: 30 }}>{t('calib_title', { defaultValue: 'Calibration race' })}</Display>
          <Mono style={{ fontSize: 12, color: 'var(--text-3)', textAlign: 'center', maxWidth: 460 }}>
            {t('calib_sub', { defaultValue: 'A measurement race sets your baseline difficulty.' })}
          </Mono>
        </div>
        <div className="panel" style={{ padding: '18px 24px', maxWidth: 520, borderColor: 'color-mix(in oklab, var(--accent) 45%, var(--line))' }}>
          <div className="display" style={{ fontSize: 18 }}>
            {cal ? `${cal.carName} — ${cal.trackName}` : t('calib_title', { defaultValue: 'Calibration race' })}
          </div>
        </div>
        <Btn variant="primary" onClick={startCalibration}>{t('calib_cta', { defaultValue: 'Start calibration' })} →</Btn>
      </div>
    );
  }

  const todayDoy = data.today.doy;
  const todayYear = data.today.year;
  // Date picker: translated months (same array as the identity screen) + years clamped to [todayYear..+8]
  // (JUMP_MAX_YEARS_FORWARD, mirrors the engine's own max-years-forward limit) — never past, never beyond.
  const monthNames = mesesTraducidos(t);
  const jumpYears = Array.from({ length: JUMP_MAX_YEARS_FORWARD + 1 }, (_, i) => todayYear + i);
  // The clock NEVER runs backward (the advance-to-date call only moves forward with a positive delta),
  // so the picker must not offer past dates: month and day are capped when the chosen year is the
  // current one. The validation below (isPastPick) also covers transient combinations while changing an already-picked dropdown.
  // No useMemo on purpose: this point in the render is AFTER an early return, so a hook here
  // would break the Rules of Hooks. It's trivial arithmetic (a loop over <=12 months and a short array).
  const todayAbsDay = absDayFromYearDoy(todayYear, todayDoy);
  // On the last day of the year, the advance buttons do not open a dialog: they cross. A single call,
  // a single day, and the `crossedYear` that comes back is what opens the winter report.
  const finDeAno = esFinDeAno(todayYear, todayDoy);
  const cruzarElAno = () => {
    void client.cmd<TimeAdvancePayload>('cmd.time.advance', { untilAbsDay: todayAbsDay + 1 })
      .then((p) => { q.refetch(); if (p.crossedYear) onRollover?.(); })
      .catch(() => { q.refetch(); });
  };
  let todayMonth = 1, todayDayOfMonth = todayDoy;
  while (todayMonth < 12 && todayDayOfMonth > daysInMonth(todayYear, todayMonth)) {
    todayDayOfMonth -= daysInMonth(todayYear, todayMonth); todayMonth++;
  }
  const jumpMonths = monthNames
    .map((name, i) => ({ name, num: i + 1 }))
    .filter((mo) => pickedYear !== todayYear || mo.num >= todayMonth);
  const jumpDaysFirst = (pickedYear === todayYear && pickedMonth === todayMonth) ? todayDayOfMonth + 1 : 1;
  const jumpDaysInMonth = daysInMonth(pickedYear || todayYear, pickedMonth || 1);
  const jumpDays = Array.from(
    { length: Math.max(0, jumpDaysInMonth - jumpDaysFirst + 1) },
    (_, i) => jumpDaysFirst + i,
  );
  // You can only jump FORWARD: today itself isn't valid either (it wouldn't move the clock).
  const isPastPick = pickedDateAbsDay != null && pickedDateAbsDay <= todayAbsDay;
  const { enrolled, clashes } = data.commitments;
  // "Future" also compares by year — a low day-of-year from next year's queue is NOT "before today".
  const firstFutureIdx = days.findIndex((d) => d.year > todayYear || (d.year === todayYear && d.doy >= todayDoy));
  const filtersActive = avail !== 'all' || era !== 'all' || discs.length > 0;
  const clearFilters = () => {
    setAvail('all');
    setEra('all');
    setDiscs([]);
  };
  const toggleDisc = (d: CalendarDiscipline) =>
    setDiscs((arr) => (arr.includes(d) ? arr.filter((x) => x !== d) : [...arr, d]));

  const visibleResults = data.seasonResults
    .filter((r) => !dismissed.includes(r.seriesId + '|' + r.year))
    .slice(0, 3);

  // length: endurance races by TIME ("2h"/"45min"/"1h30"); everything else by laps ("N laps").
  const fmtLen = (s: CalendarSlot): string => {
    if (s.durationMin > 0) {
      const h = Math.floor(s.durationMin / 60);
      const m = s.durationMin % 60;
      if (m === 0) return `${h}h`;
      if (h === 0) return `${m}min`;
      return `${h}h${String(m).padStart(2, '0')}`;
    }
    return `${s.laps} ${t('laps', { defaultValue: 'laps' })}`;
  };

  const renderCard = (s: CalendarSlot) => {
    const meta = DISC_META[s.discipline];
    const sm = STATUS_META[s.status];
    const aud = Math.max(0, Math.min(100, s.audience ?? 0));   // event popularity (subtle bar in the header)
    const barColor = s.conflict ? 'var(--amber)' : meta.color;
    const futureSeason = s.year > todayYear;   // projected round from next year's queue
    // id (`${seriesId}#${round}`) can repeat between the current year and next year's queue
    // (same series, same round number) — disambiguated with the year for the React key and hover state.
    const cardKey = `${s.id}|${s.year}`;
    const blocked = s.status === 'locked' || s.status === 'dnf' || s.status === 'traveled' || futureSeason;
    const dimmed = s.status === 'completed' ? 0.7 : s.status === 'dnf' ? 0.6 : s.status === 'traveled' ? 0.6 : futureSeason ? 0.55 : 1;
    const tImg = trackUrl(s.trackSlug);   // circuit map ('' if there's no dataDir/slug/bridge)
    const flashed = flashSlot !== null && s.seriesId === flashSlot;   // jump from the superlicense chip
    // 04/10 (Josh): a period round wears the skin of its decade, the same as its card in Memories: it is
    // told apart at a glance from today's races and says which season it is from.
    const skin = s.historic ? eraSkin(s.eraYear) : '';
    return (
      <div
        key={cardKey}
        data-slot-series={s.seriesId}
        className={'panel' + (skin ? ` era-skin ${skin}` : '') + (blocked ? ' lockhatch' : '') + (s.clickable ? ' focusable' : '')}
        role={s.clickable ? 'button' : undefined}
        tabIndex={s.clickable ? 0 : undefined}
        onClick={s.clickable ? () => tryLaunch(s) : undefined}
        onKeyDown={s.clickable ? (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); tryLaunch(s); } } : undefined}
        onMouseEnter={() => hoverEnter(cardKey)}
        onMouseLeave={() => hoverLeave(cardKey)}
        style={{
          flex: '1 1 340px',
          maxWidth: 520,
          position: 'relative',
          overflow: 'hidden',
          display: 'flex',
          opacity: dimmed,
          cursor: s.clickable ? 'pointer' : 'default',
          borderColor: flashed ? 'var(--gold)' : s.clickable ? `color-mix(in oklab, ${barColor} 45%, var(--line))` : 'var(--line)',
          boxShadow: flashed ? '0 0 0 2px color-mix(in oklab, var(--gold) 55%, transparent)' : undefined,
          transition: 'box-shadow .3s, border-color .3s',
        }}
      >
        {/* discipline corner accent (bigger if enrolled): the only colored border mark; the data itself is already in the dot+class */}
        <div style={{ position: 'absolute', left: 0, top: 0, width: s.enrolled ? 26 : 20, height: s.enrolled ? 26 : 20, clipPath: 'polygon(0 0, 100% 0, 0 100%)', background: barColor, opacity: s.clickable ? 1 : 0.4, zIndex: 1 }} />
        <div style={{ flex: 1, padding: '13px 16px', display: 'flex', flexDirection: 'column', gap: 8, minWidth: 0 }}>
          {/* row 1: R## · dot · CLASS (+ badges) · status chip
              with a long class name plus several badges (MULTICLASS/SPECIAL/enrolled/era) the header didn't
              fit on one line and overflowed the card. Now the left-hand group WRAPS (badges drop to
              a 2nd line and the card grows: same-day cards already stretch via alignItems:stretch) and the chip
              stays anchored top-right. */}
          <div className="row between" style={{ alignItems: 'flex-start', gap: 8 }}>
            <span className="row gap8" style={{ alignItems: 'center', minWidth: 0, flexWrap: 'wrap', rowGap: 6 }}>
              <span className="mono" style={{ fontSize: 10, letterSpacing: '.12em', color: 'var(--text-3)', flex: 'none' }}>R{String(s.round).padStart(2, '0')}</span>
              <span style={{ width: 8, height: 8, borderRadius: '50%', background: meta.color, flex: 'none' }} />
              <span className="mono era-class" style={{ fontSize: 10, letterSpacing: '.12em', color: meta.color, whiteSpace: 'nowrap', minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis' }}>{classDisplayName(s.className).toUpperCase()}</span>
              {/* event popularity — SUBTLE bar to the right of the class, gray→yellow→red gradient
                  (filled up to the value: low-popularity events = short and grayish; top events = long and red). No number shown. */}
              {aud > 0 && (
                <span
                  title={`${t('cal_hype', { defaultValue: 'Hype' })} · ${aud}`}
                  aria-label={`${t('cal_hype', { defaultValue: 'Hype' })} ${aud}`}
                  style={{ display: 'inline-block', width: 46, height: 4, borderRadius: 2, background: 'var(--surface-3)', overflow: 'hidden', flex: 'none' }}
                >
                  <span style={{ display: 'block', height: '100%', width: `${aud}%`, background: 'linear-gradient(90deg, var(--text-3), var(--amber), var(--danger))', backgroundSize: '46px 100%', backgroundRepeat: 'no-repeat' }} />
                </span>
              )}
              {/* "enrolled" indicator — series you're ALREADY racing (active enrollment). Shown when
                  the status chip is NOT already "enrolled" (e.g. completed/future rounds), so you can spot
                  your series at a glance without duplicating the chip on the right. */}
              {s.enrolled && s.status !== 'enrolled' && (
                <span className="mono" style={{ fontSize: 9, color: 'var(--accent-text)', background: 'color-mix(in oklab, var(--accent) 20%, transparent)', borderRadius: 3, padding: '2px 6px', flex: 'none' }}>{t('champ_enrolled')}</span>
              )}
              {s.multiclass && (
                <span className="mono" style={{ fontSize: 9, color: 'var(--cyan)', background: 'color-mix(in oklab, var(--cyan) 22%, transparent)', borderRadius: 3, padding: '2px 6px', flex: 'none' }}>{t('cal_multiclass', { defaultValue: 'MULTICLASS' })}</span>
              )}
              {/* tester suggestion (hub 29/08): a Superlicense Test is recognizable at a glance in the list --
                  gold lock-and-key chip, the same visual language as the gate it unlocks */}
              {s.isQualifier && (
                <span className="mono" title={t('cal_qualifier_hint', { defaultValue: 'Win this one-off test to earn the superlicense it guards' })} style={{ fontSize: 9, color: 'var(--gold)', background: 'color-mix(in oklab, var(--gold) 22%, transparent)', borderRadius: 3, padding: '2px 6px', flex: 'none' }}>{t('cal_qualifier', { defaultValue: 'SUPERLICENSE TEST' })}</span>
              )}
              {/* projected round from next year's queue — clearly NOT playable yet */}
              {futureSeason && (
                <span className="mono" style={{ fontSize: 9, color: 'var(--text-3)', background: 'color-mix(in oklab, var(--text-3) 20%, transparent)', borderRadius: 3, padding: '2px 6px', flex: 'none' }}>{t('cal_next_season', { defaultValue: 'NEXT SEASON' })}</span>
              )}
              {/* era rounds (pre-2000): do NOT consume the continuous clock — informational chip only, the card
                  still follows the normal clickable/locked rules (no longer depends on an isMemory flag). */}
              {s.historic && (
                <span
                  className="mono"
                  title={t('cal_free_time', { defaultValue: 'Free time · no clock' })}
                  style={{ fontSize: 9, color: 'var(--purple)', background: 'color-mix(in oklab, var(--purple) 22%, transparent)', borderRadius: 3, padding: '2px 6px', flex: 'none' }}
                >{t('cal_free_time', { defaultValue: 'Free time · no clock' })}</span>
              )}
              {s.special && (
                <span className="mono" style={{ fontSize: 9, color: 'var(--gold)', background: 'color-mix(in oklab, var(--gold) 22%, transparent)', borderRadius: 3, padding: '2px 6px', flex: 'none' }}>{t('events.badge', { defaultValue: 'SPECIAL' })}</span>
              )}
            </span>
            <span className="mono" title={s.status === 'invite' ? t('champ_by_invite_hint') : undefined} style={{ fontSize: 10, flex: 'none', color: sm.textColor, background: `color-mix(in oklab, ${sm.color} 22%, transparent)`, borderRadius: 3, padding: '3px 7px', whiteSpace: 'nowrap' }}>
              {s.conflict ? '⚠ ' : ''}{t(sm.key)}
            </span>
          </div>
          {/* row 2: the championship mark. The 'm' montage is drawn for this height; series
              without their own art keep showing their name as a headline, which is how it looked until 0.7.0.
              17/09 (Josh) — the logo COMPLEMENTS the name, it does not replace it. A drawing identifies at a
              glance but does not always SAY which one it is: three series share the Fenwick Wyn558 mark and the F1s of
              different eras look alike on purpose. With art, the logo is the headline and the name goes below
              as secondary; without art, the name remains the headline and nothing is repeated. */}
          {/* 17/09 (Josh) — ALL CARDS MEASURE THE SAME. The block ALWAYS reserves the same height,
              logo or not: with art, logo on top and name below; without art, the name alone, centered in
              the same space. If the height depended on the content, half the grid would end up misaligned depending
              which series fall on that month — and that is far more noticeable than the gap of a card with no mark. */}
          <div className="col" style={{ gap: 2, minWidth: 0, height: 42, justifyContent: 'center' }}>
            {/* 04/10: with an era skin the headline is the NAME in the typography of its decade; the logos are
                drawn for the app's dark background (white and gold) and would not be readable on cream paper. */}
            {skin
              ? <span className="display era-name" style={{ fontSize: 21 }}>{s.seriesName}</span>
              : <SeriesLogo id={s.seriesId} name={s.seriesName} variant="m" height={26} fontSize={21} />}
            {!skin && seriesLogoSrc(s.seriesId, 'm') && (
              <span className="mono" style={{ fontSize: 10.5, letterSpacing: '.06em', textTransform: 'uppercase',
                                              color: 'var(--text-3)', whiteSpace: 'nowrap', overflow: 'hidden',
                                              textOverflow: 'ellipsis' }}>
                {/* In UPPERCASE in the text itself and not only via CSS: it is how it is seen, and this way the name
                    on screen is the one in the document — otherwise the same label shows up in two
                    different boxes depending on who reads it (a person sees it the same; a screen reader, a
                    search engine or a test does not). */}
                {s.seriesName.toUpperCase()}
              </span>
            )}
          </div>
          {/* row 3: region + track · format · length · grid */}
          {/* 31/08: this row used to fit on ONE line with right-side truncation, so as soon as one more
              chunk came in (the substitution badge) what disappeared was the DURATION and the grid --
              which you need to read before entering. Truncating the circuit doesn't work either: it's
              the round's identity. Now it overflows DOWNWARD, and every card reserves the second
              line's space (minHeight), so the grid stays aligned whether one wraps or not. */}
          <div className="row gap8" style={{ color: 'var(--text-2)', fontSize: 13, lineHeight: 1.3, minWidth: 0,
                                             flexWrap: 'wrap', rowGap: 2, alignContent: 'flex-start', minHeight: 36 }}>
            <span style={{ flex: 'none' }}>{s.region} {s.trackName}</span>
            {/* track substituted for lack of DLC.
                31/08: the hover said "Replaces X · Requires DLC · Y", which reads like a LOCK --
                a player thought the round was blocked when it had actually already moved circuit. Now
                the badge carries the word, and the hover tells the story in one sentence. */}
            {s.dlcSubbed && (
              <span
                className="mono"
                title={t('dlc_sub_note', {
                  orig: s.origTrackName ?? '',
                  product: s.dlcProduct || t('gate_dlc', { defaultValue: 'Requires DLC' }),
                  defaultValue: 'Runs here instead of {{orig}} — that circuit belongs to {{product}}, which is not on your Steam account.',
                })}
                style={{ flex: 'none', fontSize: 10, color: 'var(--text-3)', cursor: 'help', letterSpacing: '.04em' }}
              >⇄ {t('dlc_sub_badge', { defaultValue: 'swapped' })}</span>
            )}
            {/* A format was already visible on screen; what wasn't visible was its CONSEQUENCE. A player won their round,
                got 13 points instead of 25 and reported it as a bug, because nothing on screen said that
                round scored at half value. The engine decides this (a half-points flag), it can't be deduced from `format`: series
                that are sprint-format start to finish don't apply the cut, so the warning would be a lie. */}
            <span className="mono" style={{ color: s.halfPoints ? 'var(--amber)' : 'var(--text-3)', flex: 'none' }}>
              · {t(s.format)}{s.halfPoints ? ` ${t('half_points', { defaultValue: '(½ points)' })}` : ''}
            </span>
            <span className="mono" style={{ color: 'var(--text-3)', flex: 'none' }}>· {fmtLen(s)}</span>
            <span className="mono" style={{ color: 'var(--text-3)', flex: 'none' }}>· {t('grid', { defaultValue: 'Grid' })} {s.grid}</span>
          </div>
          {/* row 4: PRIZE (gold) + enter / memory / super-license */}
          <div className="row between" style={{ alignItems: 'center', marginTop: 2 }}>
            <span className="chip chip-gold">{t('prize_label', { defaultValue: 'PRIZE' })} {fmtCredits(s.prize)}</span>
            {/* "Enter" button made more EYE-CATCHING — filled accent pill (not plain text) as the card's CTA. */}
            {s.clickable && (
              <span className="display" style={{ fontSize: 12.5, letterSpacing: '.04em', color: 'var(--accent-ink)', background: 'var(--accent)', borderRadius: 4, padding: '5px 12px', boxShadow: '0 1px 0 rgba(0,0,0,.35)', whiteSpace: 'nowrap' }}>{t('enter', { defaultValue: 'Enter' }).toUpperCase()} →</span>
            )}
            {/* The super-license used to be gray text that was easy to miss — now it uses the same visual language as
                the pre-race lock chip (gold + padlock), so the lock reads the same
                everywhere. HOW to earn it lives in the Championships > Access panel. */}
            {s.gateLocked && (s.gateQual ? (
              // tester suggestion: clickable -- takes you to the test's round in this same calendar
              <span
                role="button" tabIndex={0} className="focusable"
                title={t('gate_supl_go', { defaultValue: 'Click to jump to the superlicense test' })}
                onClick={(e) => { e.stopPropagation(); jumpToQualifier(s.gateQual!); }}
                onKeyDown={(e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); e.stopPropagation(); jumpToQualifier(s.gateQual!); } }}
                style={{ cursor: 'pointer', flex: 'none', display: 'inline-flex' }}
              >
                <Chip kind="gold" style={{ fontSize: 8.5 }}><LockIcon size={8} /> {t('gate_supl', { defaultValue: 'Requires superlicense' })}</Chip>
              </span>
            ) : (
              <Chip kind="gold" style={{ fontSize: 8.5 }}><LockIcon size={8} /> {t('gate_supl', { defaultValue: 'Requires superlicense' })}</Chip>
            ))}
            {/* track not installed -> DLC reason (with the pack name if known) */}
            {s.dlcLocked && (
              <span className="mono" style={{ fontSize: 10.5, color: 'var(--amber)' }}>
                {t('gate_dlc', { defaultValue: 'Requires DLC' })}{(s.dlcProduct || s.dlcPack) ? ` · ${s.dlcProduct || s.dlcPack}` : ''}
              </span>
            )}
          </div>
        </div>
        {/* circuit map (thumbnail, right side); onError hides it if no PNG exists for that slug */}
        {tImg && (
          <div style={{ flex: 'none', width: 78, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 8, borderLeft: '1px solid var(--line)' }}>
            <img src={tImg} alt="" loading="lazy" onError={(e) => { logImageError(null, e.currentTarget.src); e.currentTarget.style.display = 'none'; }} style={{ maxWidth: '100%', maxHeight: 62, objectFit: 'contain', opacity: 0.92 }} />
          </div>
        )}
        {/* HOVER -> strip of thumbnails for the field's cars (bottom overlay; pointerEvents:none so it
            doesn't block clicks on the card). Each thumbnail resolves its PNG via useCarLiveries in FieldThumb. */}
        {hoverId === cardKey && (s.field?.length ?? 0) > 0 && (
          <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, zIndex: 4, display: 'flex', gap: 5, padding: '14px 12px 9px', background: 'linear-gradient(to top, var(--surface) 60%, color-mix(in oklab, var(--surface) 55%, transparent) 88%, transparent)', pointerEvents: 'none', flexWrap: 'nowrap', overflow: 'hidden' }}>
            {s.field!.slice(0, 4).map((f, i) => <FieldThumb key={i} dataDir={dataDir} slug={f.slug} name={f.name} />)}
          </div>
        )}
      </div>
    );
  };

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column', position: 'relative' }}>
      {/* HEADER: title + subtitle + commitments | TODAY pill | time controls */}
      <div className="row between" style={{ padding: '18px 30px 12px', alignItems: 'flex-start', gap: 16 }}>
        <div className="col" style={{ gap: 2, minWidth: 0 }}>
          <Display as="h1" style={{ fontSize: 30, whiteSpace: 'nowrap' }}>{t('champ_master_title', { defaultValue: 'Real series · Calendar' })}</Display>
          <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>
            {t('cal_clock_sub', { defaultValue: 'Continuous clock · manage your schedule' })} · {t('common.phase')} {data.phase}
          </Mono>
          <div className="row gap8" style={{ marginTop: 4 }}>
            <Mono style={{ fontSize: 10, letterSpacing: '.08em', color: 'var(--accent-text)' }}>{t('cal_commitments', { defaultValue: 'YOUR COMMITMENTS' })}</Mono>
            <Mono style={{ fontSize: 10, color: clashes > 0 ? 'var(--amber)' : 'var(--text-2)' }}>
              {t('cal_enrolled_n_fmt', { enrolled, clashes, defaultValue: '{{enrolled}} enrolled · {{clashes}} clashes' })}
            </Mono>
          </div>
        </div>

        {data.today.doy > 0 && (
          <button
            type="button"
            className="row gap8"
            onClick={scrollToToday}
            title={t('cal_today_jump', { defaultValue: 'Jump to today in the calendar' })}
            style={{ alignItems: 'center', alignSelf: 'center', flex: 'none', border: 'none', cursor: 'pointer', borderRadius: 5, padding: '6px 14px', background: 'color-mix(in oklab, var(--accent) 18%, transparent)' }}
          >
            <span className="mono" style={{ fontSize: 11, letterSpacing: '.1em', color: 'var(--accent-text)' }}>{t('cal_today', { defaultValue: 'TODAY' })}</span>
            <span className="display" style={{ fontSize: 20 }}>{data.today.weekday ? `${data.today.weekday} ` : ''}{data.today.date} {data.today.year}</span>
          </button>
        )}

        {/* 17/09 (Josh's decision) — TWO ACTIONS, AND NOTHING ELSE: “advance time” and “go to date”.
            Before there were four controls here: three loose date dropdowns taking up the header, their
            go button, and two jump buttons (“next available” and “next enrolled”). Those two
            ARE REDUNDANT since the clock stops by rules: they are literally two of the five checkboxes of the
            dialog (“any race I could enter” and “one from a championship I am
            enrolled in”), so having them here was offering the same thing twice by two different routes.
            And the date is chosen INSIDE its own dialog, which is where it is decided, instead of living
            unfolded in the header no matter what. */}
        <div className="row gap8" style={{ flex: 'none', alignItems: 'center' }}>
          <Btn size="sm" onClick={() => setShowDatePicker(true)}>
            {t('cal_jump_date_go', { defaultValue: 'GO TO DATE' })}
          </Btn>
          <Btn variant="primary" size="sm" data-tour="calendar.advance"
            onClick={() => { if (finDeAno) cruzarElAno(); else setJumpScope('advance'); }}>
            {t('time.title')}
          </Btn>
        </div>
      </div>

      {/* FILTER BAR */}
      <div className="cal-filters" data-tour="calendar.filters">
        <div className="cal-fgroup">
          <span className="cal-flabel">{t('cal_f_state', { defaultValue: 'Status' })}</span>
          <div className="cal-seg-row">
            <Seg active={avail === 'all'} onClick={() => setAvail('all')} label={t('cal_f_all', { defaultValue: 'All' })} />
            <Seg active={avail === 'open'} onClick={() => setAvail('open')} label={t('cal_f_avail', { defaultValue: 'Available' })} color="#19d27c" />
            <Seg active={avail === 'enrolled'} onClick={() => setAvail('enrolled')} label={t('cal_f_enrolled', { defaultValue: 'Enrolled' })} />
            <Seg active={avail === 'locked'} onClick={() => setAvail('locked')} label={t('cal_f_locked', { defaultValue: 'Locked' })} />
          </div>
        </div>
        <div className="cal-fdiv" />
        <div className="cal-fgroup">
          <span className="cal-flabel">{t('cal_f_era', { defaultValue: 'Era' })}</span>
          <div className="cal-seg-row">
            <Seg active={era === 'all'} onClick={() => setEra('all')} label={t('cal_f_all', { defaultValue: 'All' })} />
            <Seg active={era === 'modern'} onClick={() => setEra('modern')} label={t('cal_f_current', { defaultValue: 'Current' })} color="#5ec8ff" />
            <Seg active={era === 'historic'} onClick={() => setEra('historic')} label={t('cal_f_historic', { defaultValue: 'Historic' })} color="#c9a24b" />
          </div>
        </div>
        <div className="cal-fdiv" />
        <div className="cal-fgroup" style={{ flex: 1, minWidth: 0 }}>
          <span className="cal-flabel">{t('cal_f_disc', { defaultValue: 'Discipline' })}</span>
          <div className="cal-seg-row" style={{ flexWrap: 'wrap' }}>
            {presentDiscs.map((d) => {
              const m = DISC_META[d];
              const on = discs.includes(d);
              return (
                <button
                  key={d}
                  onClick={() => toggleDisc(d)}
                  className="cal-disc focusable"
                  style={{ borderColor: on ? m.color : 'var(--line-2)', background: on ? `color-mix(in oklab, ${m.color} 18%, transparent)` : 'var(--surface-2)', color: on ? m.color : 'var(--text-2)' }}
                >
                  <span style={{ width: 9, height: 9, borderRadius: '50%', background: m.color, flex: 'none' }} />
                  {es ? m.es : m.en}
                </button>
              );
            })}
          </div>
        </div>
        {filtersActive && (
          <Btn size="sm" variant="ghost" onClick={clearFilters} style={{ flex: 'none', alignSelf: 'flex-end' }}>✕ {t('cal_f_clear', { defaultValue: 'Clear' })}</Btn>
        )}
      </div>

      {/* the event-teaser strip was removed — events live in the hub and in the events screen. */}

      {/* LIST BY DAY */}
      <div className="scroll" ref={scrollRef} style={{ flex: 1, padding: '6px 30px 30px' }} data-tour="calendar.list">
        {!days.length && (
          <div className="col center" style={{ padding: '60px 20px', gap: 12 }}>
            <span className="display" style={{ fontSize: 22, color: 'var(--text-3)' }}>
              {filtersActive ? t('cal_f_none', { defaultValue: 'No races match these filters' }) : t('champ_no_dates', { defaultValue: 'No dated races yet' })}
            </span>
            {filtersActive && <Btn size="sm" onClick={clearFilters}>{t('cal_f_clear', { defaultValue: 'Clear' })}</Btn>}
          </div>
        )}
        {days.map((d, idx) => {
          // A market-event day has no races to pull the date from: it's derived from the day-of-year using the SAME
          // "Feb 15" format the slots use, so the column reads consistently across the whole list.
          const { month, day } = splitDate(d.list.length ? d.list[0].date : dateFromDoy(d.year, d.doy));
          const past = d.year < todayYear || (d.year === todayYear && todayDoy > 0 && d.doy < todayDoy);
          const allDone = d.list.length > 0 && d.list.every((s) => s.status === 'completed' || s.status === 'dnf');
          const isToday = idx === firstFutureIdx;
          // Clear separator between seasons — next year's queue starts right after it.
          const yearChanged = idx > 0 && d.year !== days[idx - 1].year;
          return (
            <div key={`${d.year}-${d.doy}`} data-day={`${d.year}-${d.doy}`} data-today={isToday ? '1' : undefined}>
              {yearChanged && (
                <div className="row" style={{ alignItems: 'center', gap: 10, margin: '20px 0 16px' }}>
                  <div style={{ flex: 1, height: 1, background: 'var(--line-2)' }} />
                  <span
                    className="mono"
                    style={{
                      fontSize: 10, letterSpacing: '.14em', color: 'var(--purple)', flex: 'none', whiteSpace: 'nowrap',
                      border: '1px solid color-mix(in oklab, var(--purple) 45%, var(--line-2))',
                      background: 'color-mix(in oklab, var(--purple) 14%, transparent)', padding: '3px 12px', borderRadius: 3,
                    }}
                  >
                    {t('cal_season_year_fmt', { year: d.year, defaultValue: 'SEASON {{year}}' })}
                  </span>
                  <div style={{ flex: 1, height: 1, background: 'var(--line-2)' }} />
                </div>
              )}
              {isToday && (
                <div className="row" style={{ alignItems: 'center', gap: 8, margin: '2px 0 14px' }}>
                  <span className="mono" style={{ fontSize: 9, letterSpacing: '.12em', color: 'var(--bg)', background: 'var(--accent)', padding: '2px 6px', borderRadius: 3 }}>{t('cal_today', { defaultValue: 'TODAY' })}</span>
                  <div style={{ flex: 1, height: 2, background: 'var(--accent)' }} />
                </div>
              )}
              {/* each day is a bounded BAND — top separator + status rail + date cell
                  with a background fill, so days don't blend into each other (before: just margin and a low-contrast line). */}
              <div style={{
                display: 'flex', gap: 16, padding: '12px 0 6px', marginBottom: 4, alignItems: 'stretch',
                borderTop: isToday ? 'none' : '1px solid var(--line-2)',
              }}>
                <div style={{ flex: 'none', width: 66, textAlign: 'right', paddingTop: 6, paddingRight: 10, borderRadius: 4, background: allDone ? 'color-mix(in oklab, var(--surface-2) 60%, transparent)' : 'transparent' }}>
                  <div className="display" style={{ fontSize: 34, fontStyle: 'italic', color: past || allDone ? 'var(--text-3)' : 'var(--text)' }}>{day}</div>
                  <div className="mono" style={{ fontSize: 9, letterSpacing: '.12em', color: 'var(--text-3)' }}>{month.toUpperCase()}</div>
                  {d.list.length > 1 && <div className="mono" style={{ fontSize: 9, color: 'var(--accent-text)', marginTop: 6, lineHeight: 1.2 }}>{t('one_per_day', { defaultValue: 'One race per day' })}</div>}
                </div>
                <div style={{ flex: 'none', width: 3, borderRadius: 2, background: isToday ? 'var(--accent)' : past || allDone ? 'var(--line)' : 'var(--line-2)' }} />
                <div className="row wrap" style={{ gap: 14, flex: 1, alignItems: 'center' }}>
                  {d.list.map((s) => renderCard(s))}
                  {/* the market-day label. It goes AFTER the cards so a day that also has a race
                      still reads primarily by its race; on days with nothing else, it's the only
                      row, turning a blank gap into a named date. */}
                  {d.markKey && (
                    <span
                      className="mono"
                      data-market-mark={d.markKind}
                      style={{
                        fontSize: 10, letterSpacing: '.14em', whiteSpace: 'nowrap',
                        color: d.markKind === 'decision' ? 'var(--gold)' : 'var(--text-3)',
                        border: `1px solid color-mix(in oklab, ${d.markKind === 'decision' ? 'var(--gold)' : 'var(--text-3)'} 40%, var(--line-2))`,
                        background: `color-mix(in oklab, ${d.markKind === 'decision' ? 'var(--gold)' : 'var(--text-3)'} 12%, transparent)`,
                        borderRadius: 3, padding: '4px 10px',
                        opacity: past ? 0.55 : 1,
                      }}
                    >
                      {t(d.markKey)}
                    </span>
                  )}
                </div>
              </div>
            </div>
          );
        })}
      </div>

      {/* SEASON-RESULT STACK (bottom-left, dismissible, max 3) */}
      {visibleResults.length > 0 && (
        <div className="col" style={{ position: 'absolute', left: 30, bottom: 24, gap: 10, zIndex: 20, width: 360 }}>
          {visibleResults.map((r) => {
            const gold = r.finalPos >= 1 && r.finalPos <= 3;
            const ac = gold ? 'var(--gold)' : 'var(--accent)';
            const acText = gold ? 'var(--gold)' : 'var(--accent-text)';   // small text: AA-legible accent color
            const key = r.seriesId + '|' + r.year;
            return (
              <div key={key} className="panel" style={{ padding: '14px 18px 16px', position: 'relative', overflow: 'hidden', borderColor: `color-mix(in oklab, ${ac} 55%, var(--line))` }}>
                <div className="row between" style={{ alignItems: 'flex-start' }}>
                  <Mono style={{ fontSize: 10, letterSpacing: '.14em', color: acText }}>{t('cal_result_title', { defaultValue: 'SEASON RESULT' })}</Mono>
                  <button
                    className="focusable"
                    aria-label={t('cal_result_dismiss', { defaultValue: 'Dismiss' })}
                    onClick={() => { setDismissed((a) => [...a, key]); dismissResult(key); }}
                    style={{ background: 'none', border: 'none', color: 'var(--text-3)', cursor: 'pointer', fontSize: 14, lineHeight: 1, padding: 0 }}
                  >
                    ✕
                  </button>
                </div>
                <div className="display" style={{ fontSize: 17, marginTop: 6 }}>{r.seriesName.toUpperCase()} · {r.year}</div>
                <div className="row gap12" style={{ marginTop: 6, flexWrap: 'wrap' }}>
                  <Mono style={{ fontSize: 12, color: gold ? 'var(--gold)' : 'var(--text-2)' }}>
                    {t('cal_your_finish', { defaultValue: 'Your finish' })}: {r.finalPos > 0 ? t('cal_pos_fmt', { pos: r.finalPos, defaultValue: 'P{{pos}}' }) : 'DNF'}
                  </Mono>
                  {r.champion && (
                    <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('cal_champion', { defaultValue: 'Champion' })}: {r.champion}</Mono>
                  )}
                </div>
                {/* Shortcut: continue with THAT championship without hunting for it in the list. The season just ended,
                    so the destination is usually the opener of the next one; the dialog shows the real date. */}
                <Btn
                  size="sm"
                  onClick={() => setResultJumpSeries(r.seriesId)}
                  style={{ marginTop: 12, width: '100%' }}
                >
                  {t('cal_result_next_event', { defaultValue: 'GO TO NEXT EVENT' })}
                </Btn>
              </div>
            );
          })}
        </div>
      )}

      {/* conflict confirmation — running this one forfeits the one you already have that day (an intent warning) */}
      {pending && (
        <div onClick={() => setPending(null)} style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.82)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 50 }}>
          <div className="panel" role="dialog" aria-modal="true" aria-label={t('cal_clash_title', { defaultValue: 'Schedule conflict' })} onClick={(e) => e.stopPropagation()} style={{ width: 460, padding: '26px 28px', border: '1px solid color-mix(in oklab, var(--amber) 55%, transparent)', display: 'flex', flexDirection: 'column', gap: 14 }}>
            <Display style={{ fontSize: 24, color: 'var(--amber)' }}>{t('cal_clash_title', { defaultValue: 'Schedule conflict' })}</Display>
            {/* D1545023970431213599 (Josh's note): it is not enough to say WHOM it clashes with -- BOTH day
                ranges are shown (yours and each clash's) so it is seen at a glance that they overlap, instead
                of having to trust the warning's word. */}
            <Mono style={{ fontSize: 13, color: 'var(--text)', lineHeight: 1.5 }}>
              {t('cal_clash_own_range', { range: fmtDoyRange(pending.year, pending.ownLo, pending.ownHi), defaultValue: 'This round runs {{range}}, and overlaps:' })}
            </Mono>
            <div className="col" style={{ gap: 4 }}>
              {pending.clashes.map((c) => (
                <div key={c.name} className="row between" style={{ gap: 12 }}>
                  <Mono style={{ fontSize: 12.5, color: 'var(--text-2)' }}>{c.name}</Mono>
                  <Mono style={{ fontSize: 12.5, color: 'var(--amber)', flex: 'none' }}>{fmtDoyRange(pending.year, c.lo, c.hi)}</Mono>
                </div>
              ))}
            </div>
            <Mono style={{ fontSize: 13, color: 'var(--text)', lineHeight: 1.5 }}>
              {t('cal_clash_why', { defaultValue: 'The dates overlap, so racing this will mark the other race as a DNF. Continue?' })}
            </Mono>
            <div className="row gap8" style={{ marginTop: 4 }}>
              <Btn variant="primary" onClick={() => { const p = pending; setPending(null); launch(p.seriesId, p.round); }} style={{ flex: 1 }}>{t('cal_clash_confirm', { defaultValue: 'Race anyway' })}</Btn>
              <Btn variant="ghost" autoFocus onClick={() => setPending(null)} style={{ flex: 1 }}>{t('cancel', { defaultValue: 'Cancel' })}</Btn>
            </div>
          </div>
        </div>
      )}

      {/* "Jump to next year" button — appears when scrolling near/reaching the end of the list;
          it points at the absDay of the FIRST round in the projected queue. The label's year comes from the data. */}
      {firstNextYearRound && nearEnd && (
        <Btn
          variant="primary"
          onClick={() => { if (finDeAno) cruzarElAno(); else setShowYearJump(true); }}
          style={{ position: 'absolute', right: 30, bottom: 24, zIndex: 20 }}
        >
          {t('cal_jump_year_fmt', { year: firstNextYearRound.year, defaultValue: 'Jump to year {{year}}' })} →
        </Btn>
      )}

    {showDatePicker && (
        /* The selector lives HERE and not in the header: you choose the day and nothing happens until you press go,
           which is when the untilAbsDay is computed and the clock dialog opens with that date as the cap. */
        <div onClick={() => setShowDatePicker(false)}
             style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.82)', display: 'flex',
                      alignItems: 'center', justifyContent: 'center', zIndex: 60 }}>
          <div className="panel" role="dialog" aria-modal="true"
               aria-label={t('cal_jump_date_label', { defaultValue: 'Jump to a date' })}
               onClick={(e) => e.stopPropagation()}
               style={{ width: '92vw', maxWidth: 420, padding: '22px 24px', display: 'flex',
                        flexDirection: 'column', gap: 14 }}>
            <Display style={{ fontSize: 22 }}>{t('cal_jump_date_label', { defaultValue: 'Jump to a date' })}</Display>
            <div className="row gap8" style={{ alignItems: 'center', flexWrap: 'wrap' }}>
            <select
              aria-label={t('identity.day', { defaultValue: 'Day' })}
              value={pickedDay}
              onChange={(e) => setPickedDay(Number(e.target.value))}
              style={{ ...inputStyle, width: 'auto', flex: '0 0 58px', padding: '6px 8px', fontSize: 12, colorScheme: 'dark' }}
            >
              <option value={0}>{t('identity.day', { defaultValue: 'Day' })}</option>
              {jumpDays.map((n) => <option key={n} value={n}>{n}</option>)}
            </select>
            <select
              aria-label={t('identity.month', { defaultValue: 'Month' })}
              value={pickedMonth}
              onChange={(e) => setPickedMonth(Number(e.target.value))}
              style={{ ...inputStyle, width: 'auto', flex: '0 0 78px', padding: '6px 8px', fontSize: 12, colorScheme: 'dark' }}
            >
              <option value={0}>{t('identity.month', { defaultValue: 'Month' })}</option>
              {jumpMonths.map((mo) => <option key={mo.num} value={mo.num}>{mo.name}</option>)}
            </select>
            <select
              aria-label={t('identity.year', { defaultValue: 'Year' })}
              value={pickedYear}
              onChange={(e) => setPickedYear(Number(e.target.value))}
              style={{ ...inputStyle, width: 'auto', flex: '0 0 78px', padding: '6px 8px', fontSize: 12, colorScheme: 'dark' }}
            >
              <option value={0}>{t('identity.year', { defaultValue: 'Year' })}</option>
              {jumpYears.map((y) => <option key={y} value={y}>{y}</option>)}
            </select>
            </div>
            {isPastPick && (
              <Mono style={{ fontSize: 10.5, color: 'var(--amber)' }}>
                {t('cal_jump_date_past', { defaultValue: 'Pick a date in the future, not one that already passed.' })}
              </Mono>
            )}
            <div className="row" style={{ gap: 8, justifyContent: 'flex-end', marginTop: 2 }}>
              <Btn variant="ghost" size="sm" onClick={() => setShowDatePicker(false)}>{t('common.cancel')}</Btn>
              <Btn variant="primary" size="sm" disabled={pickedDateAbsDay == null || isPastPick}
                   onClick={() => { setShowDatePicker(false); goToPickedDate(); }}>
                {/* “GO”, not “GO TO DATE”: the dialog title already says what you came for, and repeating the same
                    label left two buttons with the SAME name — the header one and the one that confirms. */}
                {t('cal_jump_date_confirm', { defaultValue: 'GO' })}
              </Btn>
            </div>
          </div>
        </div>
      )}
      {dateJumpAbsDay != null && (
        <TimeAdvancePopup
          client={client}
          params={{ untilAbsDay: dateJumpAbsDay }}
          onCancel={() => setDateJumpAbsDay(null)}
          onAdvanced={(p) => { setDateJumpAbsDay(null); q.refetch(); if (p.crossedYear) onRollover?.(); }}
        />
      )}
      {resultJumpSeries && (
        <TimeAdvancePopup
          client={client}
          params={{ seriesId: resultJumpSeries }}
          onCancel={() => setResultJumpSeries(null)}
          onAdvanced={(p) => { setResultJumpSeries(null); q.refetch(); if (p.crossedYear) onRollover?.(); }}
        />
      )}
      {showYearJump && firstNextYearRound && (
        <TimeAdvancePopup
          client={client}
          params={{ untilAbsDay: absDayFromYearDoy(firstNextYearRound.year, firstNextYearRound.doy) }}
          onCancel={() => setShowYearJump(false)}
          onAdvanced={(p) => { setShowYearJump(false); q.refetch(); if (p.crossedYear) onRollover?.(); }}
        />
      )}
      {/* 17/09 — “Advance time” goes WITHOUT a destination: `{}` is the engine's sentinel for “until the first
          stop” according to the player's rules. The 'available'/'enrolled' scopes stay in the type because
          other screens use them, but this one no longer offers them: they were two of the five rules through another door. */}
      {jumpScope && (
        <TimeAdvancePopup
          client={client}
          params={jumpScope === 'advance' ? {} : { scope: jumpScope }}
          onCancel={() => setJumpScope(null)}
          onAdvanced={(p) => { setJumpScope(null); q.refetch(); if (p.crossedYear) onRollover?.(); }}
        />
      )}
    </div>
  );
}