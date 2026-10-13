import { describe, it, expect } from 'vitest';
import { buildWorld } from './demoWorld';
import { simulateRace, nextRound, standingsRows, reportKeys } from './demoEngine';

describe('demo engine', () => {
  it('starts with round 1 done and one recent result', () => {
    const s = buildWorld(20261013);
    expect(s.rounds[0].done).toBe(true);
    expect(s.recent.length).toBe(1);
    expect(nextRound(s)?.round).toBe(2);
  });
  it('simulating a race settles the next round and moves standings', () => {
    const s = buildWorld(20261013);
    const before = standingsRows(s).map((r) => r.points).reduce((a, b) => a + b, 0);
    const res = simulateRace(s)!;
    expect(res.field).toBe(16);
    expect(s.rounds[1].done).toBe(true);
    expect(s.resultSeq).toBe(1);
    expect(s.recent[0].track).toBe(s.rounds[1].track);
    const after = standingsRows(s).map((r) => r.points).reduce((a, b) => a + b, 0);
    expect(after).toBeGreaterThan(before);
  });
  it('stores the per-round finishing order for every driver', () => {
    const s = buildWorld(5);
    expect(s.rounds[0].order!.length).toBe(16);
    expect(s.rounds[0].order!.indexOf('player')).toBe(5);
    simulateRace(s);
    const r = s.rounds[1];
    expect(new Set(r.order).size).toBe(16);
    expect(r.order!.indexOf('player') + 1).toBe(r.result!.pos);
    for (const id of r.dnf!) expect(r.order!.indexOf(id)).toBeGreaterThanOrEqual(16 - r.dnf!.length);
  });
  it('is deterministic for the same seed', () => {
    const a = buildWorld(7); const b = buildWorld(7);
    expect(simulateRace(a)!.pos).toBe(simulateRace(b)!.pos);
  });
  it('season complete: no next round, simulate returns null', () => {
    const s = buildWorld(1);
    while (nextRound(s)) simulateRace(s);
    expect(simulateRace(s)).toBeNull();
    expect(s.rounds.every((r) => r.done)).toBe(true);
  });
  it('standings are sorted and positions are 1..N', () => {
    const s = buildWorld(3); simulateRace(s);
    const rows = standingsRows(s);
    expect(rows.map((r) => r.pos)).toEqual(rows.map((_, i) => i + 1));
    for (let i = 1; i < rows.length; i++) expect(rows[i - 1].points).toBeGreaterThanOrEqual(rows[i].points);
  });
  it('race report lands in the inbox', () => {
    const s = buildWorld(5); const n = s.inbox.length; simulateRace(s);
    expect(s.inbox.length).toBe(n + 1 + s.offers.length);
    expect(s.inbox[0].read).toBe(false);
  });
  it('is deterministic over the whole standings and result', () => {
    const a = buildWorld(11); const b = buildWorld(11);
    simulateRace(a); simulateRace(a); simulateRace(b); simulateRace(b);
    expect(standingsRows(a)).toEqual(standingsRows(b));
    expect(a.lastResult).toEqual(b.lastResult);
  });
  it('DNF path: ranks last, scores no points, keeps a retirement report', () => {
    let hit = false;
    for (let seed = 1; seed < 400 && !hit; seed++) {
      const s = buildWorld(seed);
      while (nextRound(s)) {
        const res = simulateRace(s)!;
        if (res.finished === false) {
          hit = true;
          expect(s.rounds.find((r) => r.result && !r.result.finished)!.result!.points).toBe(0);
          expect(s.recent[0].finished).toBe(false);
          expect(s.inbox[0].body).toMatch(/dnf_v\d$|coldstreak/);
          expect(res.timing.totalTimeS).toBeLessThan(0);
          break;
        }
      }
    }
    expect(hit).toBe(true);
  });
  it('event race pays out but does not touch rounds or points', () => {
    const s = buildWorld(9);
    const pts = JSON.stringify(s.points); const done = s.rounds.filter((r) => r.done).length;
    const money = s.money; const races = s.races;
    const res = simulateRace(s, { eventPrize: 30000, eventTrack: 'Ellerby' })!;
    expect(JSON.stringify(s.points)).toBe(pts);
    expect(s.rounds.filter((r) => r.done).length).toBe(done);
    expect(s.money).toBeGreaterThan(money);
    expect(s.races).toBe(races + 1);
    expect(s.recent[0].track).toBe('Ellerby');
    expect(res.championship).toBeNull();
  });
  it('mail keys: P5 never yields first-win/coldstreak, second podium never reuses ms_firstpodium', () => {
    const s = buildWorld(2);
    expect(reportKeys(s, 5, true).body).toMatch(/^demo_mail_race_points_v[0-2]$/);
    s.wins = 2; s.podiums = 2; s.recent.unshift({ pos: 2, field: 16, track: 'x', finished: true });
    expect(reportKeys(s, 1, true).body).toMatch(/^demo_mail_race_win_v/);
    s.podiums = 3;
    expect(reportKeys(s, 3, true).body).toMatch(/^demo_mail_race_podium_v/);
    const f = buildWorld(2); f.podiums = 1; f.recent.unshift({ pos: 3, field: 16, track: 'x', finished: true });
    expect(reportKeys(f, 3, true).body).toMatch(/^ms_firstpodium_v/);
  });
  it('coldstreak fires only after 3 consecutive non-podium results', () => {
    const s = buildWorld(2); // recent: [P6]
    expect(reportKeys(s, 8, true).subject).toBe('demo_mail_race_subj');
    s.recent.unshift({ pos: 7, field: 16, track: 'x', finished: true }, { pos: 9, field: 16, track: 'y', finished: true });
    expect(reportKeys(s, 9, true).subject).toBe('ms_coldstreak_subj');
  });
  it('result progression is recomputed per race', () => {
    const s = buildWorld(4); const a = simulateRace(s)!; const b = simulateRace(s)!;
    expect(b.progression.elo.before).toBe(a.progression.elo.after);
    expect(b.timing.bestLapS).not.toBe(a.timing.bestLapS);
    expect(b.progression.elo.band).toMatch(/^[A-F]$/);
  });
});

describe('demo engine: promised income', () => {
  const sum = (r: { money: { lines: { amount: number }[]; net: number } }) => r.money.lines.reduce((a, l) => a + l.amount, 0);
  it('pays the contract salary per championship round, and net/balance equal the sum of lines', () => {
    const s = buildWorld(4); s.sponsors = []; s.salary = 3300;
    const m0 = s.money;
    const r = simulateRace(s, { forcePos: 12 })!;
    const sal = r.money.lines.find((l) => l.key === 'results.salary');
    expect(sal?.amount).toBe(3300);
    expect(r.money.net).toBe(sum(r));
    expect(s.money - m0).toBe(r.money.net);
    expect(r.progression.balance.delta).toBe(r.money.net);
    expect(r.progression.balance.after).toBe(s.money);
  });
  it('no salary line on a special event', () => {
    const s = buildWorld(4);
    const r = simulateRace(s, { eventPrize: 40000, eventTrack: 'Fairwater', forcePos: 5 })!;
    expect(r.money.lines.some((l) => l.key === 'results.salary')).toBe(false);
  });
  it('sponsor triggers fire only when met, each as its own line', () => {
    const lineOf = (pos: number, ids: string[]) => {
      const s = buildWorld(4); s.sponsors = ids;
      const r = simulateRace(s, { forcePos: pos })!;
      expect(r.money.net).toBe(sum(r));
      return r.money.lines.filter((l) => l.label);
    };
    const prizeOf = (pos: number) => { const s = buildWorld(4); s.sponsors = []; return simulateRace(s, { forcePos: pos })!.money.lines[0].amount; };
    expect(lineOf(2, ['demo_harbor'])[0].amount).toBe(Math.round(prizeOf(2) * 0.15));   // podium, income.prize_mult
    expect(lineOf(6, ['demo_harbor'])).toHaveLength(0);
    expect(lineOf(1, ['demo_ironbark'])).toHaveLength(1);                               // win
    expect(lineOf(2, ['demo_ironbark'])).toHaveLength(0);
    expect(lineOf(8, ['demo_mist'])).toHaveLength(1);                                   // top8
    expect(lineOf(9, ['demo_mist'])).toHaveLength(0);
    expect(lineOf(12, ['demo_quill'])).toHaveLength(1);                                 // clean
    expect(lineOf(12, ['demo_lumen'])[0].amount).toBe(600);                             // passive, income.bonus
  });
  it('a sponsor bought in the shop applies on the next race', async () => {
    const { buyShopCard } = await import('./demoEvents');
    const s = buildWorld(4); s.money = 100000;
    const shop = { raceId: 'x', bought: [], rerolls: 0 };
    expect(buyShopCard(s, shop, 'demo_ironbark')).toBe(true);
    expect(s.sponsors).toContain('demo_ironbark');
    const r = simulateRace(s, { forcePos: 1 })!;
    expect(r.money.lines.some((l) => l.label === 'Ironbark Tools')).toBe(true);
  });
});
