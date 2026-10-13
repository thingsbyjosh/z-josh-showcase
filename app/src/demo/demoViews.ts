// State -> contract payloads for the showcase demo. Every builder starts from the emitted fixture
// (so fields the demo does not model keep realistic values) and overwrites the state-driven ones.
import type {
  CareerStatePayload, CalendarPayload, GaragePayload, HubPayload, InboxPayload, PreracePayload, ProfilePayload, SeasonPayload,
  StandingsPayload, TimeAdvancePayload, TimeDayEvent,
} from '../api/contracts';
import hubFx from '../../ui-fixtures/hub.json';
import careerFx from '../../ui-fixtures/career_state.json';
import calendarFx from '../../ui-fixtures/calendar.json';
import seasonFx from '../../ui-fixtures/season.json';
import profileFx from '../../ui-fixtures/profile.json';
import profileFullFx from '../../ui-fixtures/profile.full.json';
import preraceFx from '../../ui-fixtures/prerace.json';
import garageFx from '../../ui-fixtures/garage.json';
import timeFx from '../../ui-fixtures/time_preview.json';
import { POINTS, type DemoState } from './demoWorld';
import { eloBand, nextRound, standingsRows } from './demoEngine';

const clone = <T,>(x: unknown): T => JSON.parse(JSON.stringify(x)) as T;

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const ABS_OFFSET = 364;   // absDay = doy + offset; only has to be monotonic
function dayInfo(doy: number): { date: string; weekday: string } {
  const d = new Date(Date.UTC(2026, 0, doy));
  return { date: `${MONTHS[d.getUTCMonth()]} ${d.getUTCDate()}`, weekday: WEEKDAYS[d.getUTCDay()] };
}
const timeDay = (doy: number) => ({ absDay: doy + ABS_OFFSET, year: 2026, doy, ...dayInfo(doy) });
const unreadCount = (s: DemoState) => s.inbox.filter((m) => !m.read).length;
const openOffers = (s: DemoState) => s.offers.filter((o) => o.state === 'open' || o.state === 'countered').length;

export function hubView(s: DemoState): HubPayload {
  const h = clone<HubPayload>(hubFx);
  const n = nextRound(s);
  const di = dayInfo(s.today);
  h.stats = { wins: s.wins, podiums: s.podiums, races: s.races };
  h.nextRace = n ? { series: s.series.name, track: n.track, date: dayInfo(n.doy).date, round: n.round, doy: n.doy, weekday: dayInfo(n.doy).weekday } : null;
  h.recentResults = s.recent.map((r) => ({ ...r }));
  // GT3 sits on the International rung of the ladder (tree.level.5)
  if (h.rating) h.rating = { ...h.rating, elo: s.elo, phase: 5 };
  h.reputation = { ...h.reputation, score: s.rep };
  h.lifecycle = { ...h.lifecycle, cursorDate: di.date, weekday: di.weekday, contractTeams: [s.team.name], contractActive: true, contractCount: 1, contractSeries: s.series.name };
  h.badges = { ...h.badges, unread: unreadCount(s), directOffers: openOffers(s) };
  return h;
}

export function careerStateView(s: DemoState): CareerStatePayload {
  const c = clone<CareerStatePayload>(careerFx);
  c.profile = { ...c.profile, money: s.money, eloFame: s.elo, xp: s.xp, week: Math.ceil(s.today / 7), year: 2026, dateDoy: s.today };
  c.contract = { seriesId: s.series.id, seriesName: s.series.name, teamName: s.team.name, raceSeat: true };
  c.flags = { ...c.flags, inSeason: nextRound(s) !== null, pendingResults: s.settlePending, resultSeq: s.resultSeq, settlePending: s.settlePending, simRace: true };
  if (c.badges) c.badges = { ...c.badges, inbox: unreadCount(s) };
  return c;
}

export function standingsView(s: DemoState): StandingsPayload {
  return { seriesId: s.series.id, seriesName: s.series.name, rows: standingsRows(s) };
}

export function calendarView(s: DemoState): CalendarPayload {
  const c = clone<CalendarPayload>(calendarFx);
  const tpl = c.slots[0];
  const next = nextRound(s);
  const di = dayInfo(s.today);
  c.slots = s.rounds.map((r) => {
    const status = r.done ? (r.result?.finished === false ? 'dnf' : 'completed') : r === next ? 'enrolled' : 'upcoming';
    const d = dayInfo(r.doy);
    return {
      ...tpl,
      id: `${s.series.id}#${r.round}`, seriesId: s.series.id, seriesName: s.series.name, className: s.series.className,
      trackName: r.track, trackSlug: r.trackSlug, trackHash: 2000 + r.round, doy: r.doy, date: d.date, weekday: d.weekday,
      winLoDoy: r.doy - 1, winHiDoy: r.doy, round: r.round, laps: r.laps, prize: r.prize, grid: s.drivers.length,
      gridEffective: s.drivers.length, status, clickable: r === next, enrolled: true,
    } as CalendarPayload['slots'][number];
  });
  c.today = { doy: s.today, date: di.date, year: 2026, weekday: di.weekday };
  c.weeksToNextEvent = next ? Math.max(0, Math.ceil((next.doy - s.today) / 7)) : 0;
  c.commitments = { enrolled: s.rounds.filter((r) => !r.done).length, clashes: 0 };
  return c;
}

export function seasonView(s: DemoState): SeasonPayload {
  const p = clone<SeasonPayload>(seasonFx);
  const rows = standingsRows(s);
  const me = rows.find((r) => r.isPlayer)!;
  const doneCount = s.rounds.filter((r) => r.done).length;
  p.selectedSeriesId = s.series.id;
  p.rail[0] = {
    ...p.rail[0], seriesId: s.series.id, display: s.series.name, classShort: s.series.className, className: s.series.className,
    discColor: s.series.classColor, selected: true, playerPos: me.pos, myPoints: me.points, roundIdx: doneCount,
    totalRounds: s.rounds.length, field: s.drivers.length, done: doneCount === s.rounds.length,
  };
  p.detail = {
    ...p.detail, kind: 1, seriesDisplay: s.series.name, roundIdx: doneCount, totalRounds: s.rounds.length, seasonYear: 2026,
    myPos: me.pos, myPts: me.points, pointsScale: [...POINTS],
    rounds: s.rounds.map((r) => ({ round: r.round, trackName: r.track, date: dayInfo(r.doy).date, doy: r.doy, hour: 0, laps: r.laps, durationMin: 0, practiceMin: 0, qualifyMin: 0 })),
    standings: rows.map((row) => ({
      rank: row.pos, name: row.driverName, isPlayer: row.isPlayer, total: row.points,
      cells: s.rounds.map((r) => {
        const idx = r.order ? r.order.indexOf(row.driverId) : -1;
        if (!r.done || idx < 0) return { round: r.round, pos: 0, points: 0, dnf: false, ran: false };
        const dnf = !!r.dnf?.includes(row.driverId);
        return { round: r.round, pos: idx + 1, points: dnf ? 0 : POINTS[idx] ?? 0, dnf, ran: true };
      }),
    })),
    nextRaceDoy: nextRound(s)?.doy ?? 0,
  };
  p.playerName = me.driverName;
  p.bonusShown = false;
  return p;
}

export function profileView(s: DemoState, full: boolean): ProfilePayload {
  const p = clone<ProfilePayload>(full ? profileFullFx : profileFx);
  const b = eloBand(s.elo);
  const last = s.lastResult?.progression.elo.delta ?? 0;
  p.rating = { ...p.rating, score: s.elo, delta: last, band: { ...p.rating.band, letter: b.band, color: b.color } };
  p.recentForm = s.recent.map((r) => ({ pos: r.pos, finished: r.finished }));
  p.palmares = { ...p.palmares, wins: s.wins, podiums: s.podiums, races: s.races, xp: s.xp };
  const wr = p.worldRanking as unknown as Record<string, { isPlayer?: boolean; rating: number; bandLetter: string; bandColor: string }[] | undefined> | undefined;
  if (wr) {
    for (const key of ['top', 'neighborhood', 'all']) {
      for (const row of wr[key] ?? []) {
        if (row.isPlayer) { row.rating = s.elo; row.bandLetter = b.band; row.bandColor = b.color; }
      }
    }
  }
  return p;
}

export function preraceView(s: DemoState): PreracePayload {
  const p = clone<PreracePayload>(preraceFx);
  const n = nextRound(s);
  p.credits = s.money;
  p.seriesId = s.series.id;
  p.className = s.series.className;
  p.discColor = s.series.classColor;
  if (p.cars[0]) p.cars[0] = { ...p.cars[0], name: s.team.car };
  if (!n) { p.clsEmpty = true; return p; }
  p.clsEmpty = false;
  p.raceName = `R${String(n.round).padStart(2, '0')} ${dayInfo(n.doy).date} ${n.track}`;
  p.trackName = n.track;
  p.laps = n.laps; p.raceLaps = n.laps; p.launchLaps = n.laps;
  p.grid = s.drivers.length;
  p.reward = n.prize;
  return p;
}

export function inboxView(s: DemoState): InboxPayload {
  return { messages: s.inbox };
}

/** Where "advance time" stops: the player's next race day. */
export function timePreview(s: DemoState): TimeAdvancePayload {
  const t = clone<TimeAdvancePayload>(timeFx);
  const n = nextRound(s);
  const today = timeDay(s.today);
  t.today = today;
  t.executed = false;
  if (!n || n.doy <= s.today) {
    t.resolved = false;
    t.stop = { ...today, reason: '' }; t.days = 0; t.crossed = []; t.crossedYear = false; t.noShows = [];
    return t;
  }
  const crossed: TimeDayEvent[] = [];
  const mails = s.inbox.filter((m) => m.year === 2026 && m.week * 7 > s.today && m.week * 7 <= n.doy);
  if (mails.length) {
    const d = Math.min(n.doy, Math.max(s.today + 1, mails[0].week * 7));
    crossed.push({ ...timeDay(d), kind: 'mail', label: 'time.ev.mail', params: { n: String(mails.length) }, seriesId: '', stops: false });
  }
  crossed.push({ ...timeDay(n.doy), kind: 'race_mine', label: 'time.ev.race_mine', params: { series: s.series.name, track: n.track, round: String(n.round) }, seriesId: s.series.id, stops: true });
  t.resolved = true;
  t.stop = { ...timeDay(n.doy), reason: 'race_mine' };
  t.days = n.doy - s.today; t.crossedYear = false; t.crossed = crossed; t.noShows = [];
  return t;
}

export function garageView(s: DemoState): GaragePayload {
  const g = clone<GaragePayload>(garageFx);
  g.credits = s.money;
  if (g.cars[0]) g.cars[0] = { ...g.cars[0], name: s.team.car };
  if (g.contract) {
    g.contract = { ...g.contract, series: s.series.name, perRace: s.salary, fixesCar: true, teamCar: s.team.car, roleLead: false };
  }
  return g;
}
