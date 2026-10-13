import type { InboxMessage, ResultsPayload, StandingsRow } from '../api/contracts';
import resultsFixture from '../../ui-fixtures/results.json';
import { maybeCreateOffer } from './demoMarket';
import { sponsorFx } from './demoEvents';
import { POINTS as WORLD_POINTS, type DemoRound, type DemoState } from './demoWorld';

export const POINTS = WORLD_POINTS;

export function nextRound(s: DemoState): DemoRound | null {
  return s.rounds.find((r) => !r.done) ?? null;
}

export function standingsRows(s: DemoState): StandingsRow[] {
  return s.drivers
    .map((d) => ({ d, p: s.points[d.id] ?? 0 }))
    .sort((a, b) => b.p - a.p || b.d.rating - a.d.rating || a.d.id.localeCompare(b.d.id))
    .map((x, i) => ({
      pos: i + 1, driverId: x.d.id, driverName: x.d.name, teamName: x.d.team, points: x.p, isPlayer: x.d.isPlayer,
    }));
}

/**
 * Mail copy for a settled race. Demo-owned keys (registered by demoI18n.ts) by outcome; the engine's
 * ms_* keys only on genuine milestones (first win, first podium, 3 non-podium results in a row).
 * Call AFTER the counters and `recent` were updated.
 */
export function reportKeys(s: DemoState, pos: number, finished: boolean): { subject: string; body: string } {
  const v = Math.floor(s.rng() * 3);
  const podium = finished && pos <= 3;
  let streak = 0;
  for (const r of s.recent) { if (r.finished && r.pos <= 3) break; streak++; }
  if (finished && pos === 1 && s.wins === 1) return { subject: 'ms_firstwin_subj', body: 'ms_firstwin_v' + v };
  if (podium && pos > 1 && s.podiums === 1) return { subject: 'ms_firstpodium_subj', body: 'ms_firstpodium_v' + v };
  if (!podium && streak === 3) return { subject: 'ms_coldstreak_subj', body: 'ms_coldstreak_v' + v };
  const cls = !finished ? 'dnf' : pos === 1 ? 'win' : pos <= 3 ? 'podium' : pos <= 10 ? 'points' : 'back';
  return { subject: 'demo_mail_race_subj', body: `demo_mail_race_${cls}_v${v}` };
}

function raceReport(s: DemoState, pos: number, finished: boolean, track: string, id: string, doy: number): InboxMessage {
  const k = reportKeys(s, pos, finished);
  return {
    id, week: Math.ceil(doy / 7), year: 2026,
    from: 'inbox_paddock', subject: k.subject, body: k.body,
    params: { player: 'Alex', track, pos: String(pos) },
    read: false, kind: 'manager', family: 'progress', you: false,
  };
}

const ELO_BANDS: [number, string, string][] = [
  [900, 'F', 'var(--danger)'], [1100, 'E', 'var(--danger)'], [1300, 'D', 'var(--text-2)'],
  [1500, 'C', 'var(--cyan)'], [1700, 'B', 'var(--gold)'], [Infinity, 'A', 'var(--green)'],
];
export function eloBand(elo: number): { band: string; color: string } {
  const b = ELO_BANDS.find((x) => elo < x[0])!;
  return { band: b[1], color: b[2] };
}
export const repColor = (rep: number): string => (rep >= 3000 ? 'var(--cyan)' : 'var(--text-2)');

/** 'clean' is a finished race: the demo models no incidents. */
function sponsorFires(trigger: string, pos: number, finished: boolean): boolean {
  switch (trigger) {
    case 'passive': return true;
    case 'podium': return finished && pos <= 3;
    case 'win': return finished && pos === 1;
    case 'top8': return finished && pos <= 8;
    case 'clean': return finished;
    default: return false;
  }
}

export function simulateRace(s: DemoState, opts?: { eventPrize?: number; eventTrack?: string; forcePos?: number }): ResultsPayload | null {
  const isEvent = opts?.eventPrize !== undefined;
  const round = isEvent ? null : nextRound(s);
  if (!isEvent && !round) return null;

  const scored = s.drivers.map((d) => {
    const score = d.rating + (s.rng() - 0.5) * 30;
    const dnf = s.rng() < 0.05;
    return { d, score, dnf };
  });
  const ranked = [
    ...scored.filter((x) => !x.dnf).sort((a, b) => b.score - a.score),
    ...scored.filter((x) => x.dnf).sort((a, b) => b.score - a.score),
  ];
  if (opts?.forcePos) {   // test-only: put the player at an exact finishing position
    const at = ranked.findIndex((x) => x.d.isPlayer);
    const [me] = ranked.splice(at, 1);
    me.dnf = false;
    ranked.splice(Math.min(Math.max(opts.forcePos, 1), ranked.length + 1) - 1, 0, me);
  }
  const field = ranked.length;
  const idx = ranked.findIndex((x) => x.d.isPlayer);
  const pos = idx + 1;
  const finished = !ranked[idx].dnf;
  const track = isEvent ? (opts?.eventTrack ?? 'Fairwater') : round!.track;
  const doy = round ? round.doy : s.today;
  const prize = isEvent ? opts!.eventPrize! : round!.prize;

  let gained = 0;
  if (round) {
    ranked.forEach((x, i) => { if (!x.dnf) s.points[x.d.id] = (s.points[x.d.id] ?? 0) + (POINTS[i] ?? 0); });
    gained = finished ? (POINTS[idx] ?? 0) : 0;
    round.done = true;
    round.result = { pos, finished, points: gained };
    round.order = ranked.map((x) => x.d.id);
    round.dnf = ranked.filter((x) => x.dnf).map((x) => x.d.id);
  }

  const scale = Math.max(0.2, 1 - (pos - 1) * 0.06);
  const cash = Math.round(prize * scale * s.rewardMult);
  const repBefore = s.rep, eloBefore = s.elo, xpBefore = s.xp, moneyBefore = s.money;
  const repD = Math.max(0, 6 - pos);
  const eloD = Math.round((field / 2 - pos) * 2);
  const xpD = finished ? 100 + (field - pos) * 10 : 40;
  // Money promised by the demo's own mails: the contract salary per championship round, and each signed
  // sponsor whose trigger fired (prize multiplier on the prize, or a flat bonus), as separate lines.
  const lines: ResultsPayload['money']['lines'] = [{ key: 'results.prize', amount: cash, tone: 'gold' }];
  if (round && s.salary > 0) lines.push({ key: 'results.salary', amount: s.salary, tone: 'cyan' });
  for (const id of s.sponsors) {
    const fx = sponsorFx(id);
    if (!fx || !sponsorFires(fx.trigger, pos, finished)) continue;
    const amount = fx.hook === 'income.prize_mult' ? Math.round(cash * (fx.v - 1)) : fx.hook === 'income.bonus' ? fx.v : 0;
    if (amount > 0) lines.push({ label: fx.name, amount, tone: 'gold' });
  }
  const net = lines.reduce((a, l) => a + l.amount, 0);
  s.money += net; s.rep += repD; s.elo += eloD; s.xp += xpD;
  s.races++;
  if (finished && pos === 1) s.wins++;
  if (finished && pos <= 3) s.podiums++;
  s.recent.unshift({ pos, field, track, finished });
  s.recent.length = Math.min(s.recent.length, 5);
  s.today = doy;
  s.resultSeq++;
  s.settlePending = false;
  s.inbox.unshift(raceReport(s, pos, finished, track, 'demo_r' + (round ? round.round : 'e' + s.resultSeq), doy));

  maybeCreateOffer(s);

  const fx = JSON.parse(JSON.stringify(resultsFixture)) as ResultsPayload;
  const rows = standingsRows(s);
  const laps = round ? round.laps : 12;
  const bestLap = 82 + (round ? round.round : 3) * 2.5 + s.rng() * 3;
  const lapped = finished && pos > field - 3 && s.rng() < 0.5 ? 1 : 0;
  const r3 = (n: number) => Math.round(n * 1000) / 1000;
  const timing = {
    totalTimeS: finished ? r3(laps * bestLap * (1.03 + s.rng() * 0.02)) : -1,
    bestLapS: r3(bestLap),
    gapS: r3(s.rng() * 1.5),
    gapLeaderS: finished && pos > 1 && !lapped ? r3((pos - 1) * 1.3 + s.rng() * 2) : -1,
    gapLeaderLaps: lapped,
  };
  const result: ResultsPayload = {
    ...fx,
    pos, finished, field, multiclass: false, overallPos: 0, overallField: 0,
    className: s.series.className, classColor: s.series.classColor, carName: s.team.car, trackName: track,
    timing,
    money: { lines, net },
    progression: {
      elo: { before: eloBefore, after: s.elo, delta: eloD, ...eloBand(s.elo) },
      rep: { before: repBefore, after: s.rep, delta: repD, color: repColor(s.rep) },
      exp: { before: xpBefore, after: s.xp, delta: xpD },
      balance: { before: moneyBefore, after: s.money, delta: net },
    },
    championship: round ? {
      seriesName: s.series.name, pointsGained: gained,
      playerRank: rows.find((r) => r.isPlayer)!.pos,
      rows: rows.slice(0, 5).map((r) => ({ name: r.driverName, points: r.points, isPlayer: r.isPlayer })),
    } : null,
    cardChips: [],
    cardShowcase: [],
    feedback: { ask: true, feel: 0, telemetryOptIn: false, offer: false, gapPct: 0, offerDifficulty: 0, curDifficulty: 92, held: false },
  };
  s.lastResult = result;
  return result;
}
