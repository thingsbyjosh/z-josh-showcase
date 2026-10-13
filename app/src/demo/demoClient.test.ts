import { describe, it, expect, vi } from 'vitest';
import { createDemoClient } from './demoClient';
import type {
  HubPayload, CareerStatePayload, GameStatePayload, StandingsPayload, SeasonPayload, CalendarPayload, InboxPayload,
  TimeAdvancePayload, MarketPayload, NegotiatePayload, GaragePayload, CardsPayload, PreracePayload, RaceConfigPayload,
} from '../api/contracts';

describe('demo client race flow', () => {
  it('launch -> in session -> settled, hub and standings update', async () => {
    vi.useFakeTimers();
    const c = createDemoClient({ seed: 11, raceMs: 1000 });
    const hub0 = await c.query<HubPayload>('query.hub');
    expect((await c.cmd<{ ok: boolean; launched: boolean }>('cmd.calendar.launch', { seriesId: 'demo_gt', round: 2 })).launched).toBe(true);
    expect((await c.query<GameStatePayload>('query.game_state')).atMenu).toBe(true);   // the calendar only opens the pre-race
    const r = await c.cmd<{ ok: boolean }>('cmd.prerace.launch', {});
    expect(r.ok).toBe(true);
    expect((await c.query<GameStatePayload>('query.game_state')).atMenu).toBe(false);
    expect((await c.query<CareerStatePayload>('query.career_state')).flags.settlePending).toBe(true);
    await vi.advanceTimersByTimeAsync(1100);
    const gs = await c.query<GameStatePayload>('query.game_state');
    expect(gs.atMenu).toBe(true);
    const cs = await c.query<CareerStatePayload>('query.career_state');
    expect(cs.flags.resultSeq).toBe(1);
    expect(cs.flags.settlePending).toBe(false);
    const hub1 = await c.query<HubPayload>('query.hub');
    expect(hub1.recentResults!.length).toBe(hub0.recentResults!.length + 1);
    expect(hub1.stats.races).toBe(hub0.stats.races + 1);
    const st = await c.query<StandingsPayload>('query.standings');
    expect(st.rows.length).toBe(16);
    vi.useRealTimers();
  });

  it('the launch panel describes the race being simulated, not the fixture', async () => {
    const c = createDemoClient({ seed: 11, raceMs: 1000 });
    const pre = await c.query<PreracePayload>('query.prerace');
    await c.cmd('cmd.prerace.launch', {});
    const rc = await c.query<RaceConfigPayload>('query.race_config');
    expect(rc.trackName).toBe(pre.trackName);
    expect(rc.laps).toBe(pre.laps);
    expect(rc.opponents).toBe(pre.grid - 1);
    expect(rc.seriesName).toBe('GT Masters Demo');
  });

  it('launch twice -> one settle', async () => {
    vi.useFakeTimers();
    const c = createDemoClient({ seed: 11, raceMs: 1000 });
    await c.cmd('cmd.calendar.launch', { seriesId: 'demo_gt', round: 2 });
    await c.cmd('cmd.prerace.launch', {});
    const second = await c.cmd<{ ok: boolean }>('cmd.prerace.launch', {});
    expect(second.ok).toBe(false);
    await vi.advanceTimersByTimeAsync(1100);
    expect((await c.query<CareerStatePayload>('query.career_state')).flags.resultSeq).toBe(1);
    vi.useRealTimers();
  });

  it('time advance moves the cursor date', async () => {
    const c = createDemoClient({ seed: 2 });
    const before = (await c.query<HubPayload>('query.hub')).lifecycle.cursorDate;
    const res = await c.cmd<TimeAdvancePayload>('cmd.time.advance', {});
    expect(res.executed).toBe(true);
    expect((await c.query<HubPayload>('query.hub')).lifecycle.cursorDate).not.toBe(before);
  });

  it('every query answer is a fresh copy, so screens that memoise on identity refresh', async () => {
    const c = createDemoClient({ seed: 3, raceMs: 500 });
    const a = await c.query<InboxPayload>('query.inbox');
    const b = await c.query<InboxPayload>('query.inbox');
    expect(b.messages).not.toBe(a.messages);
    expect(b.messages).toEqual(a.messages);
  });

  it('the hub shows the seat the player holds, on the GT3 rung', async () => {
    const h = await createDemoClient({ seed: 3, raceMs: 500 }).query<HubPayload>('query.hub');
    expect(h.lifecycle.contractCount).toBe(1);
    expect(h.rating?.phase).toBe(5);
  });

  it('the seeded inbox carries no dead offer and every mail has demo text', async () => {
    const c = createDemoClient({ seed: 3, raceMs: 500 });
    const msgs = (await c.query<InboxPayload>('query.inbox')).messages;
    expect(msgs.filter((m) => m.kind === 'offer' && !m.offerId)).toEqual([]);
    const sp = msgs.find((m) => m.family === 'sponsor')!;
    expect(sp.subject).toBe('demo_mail_sponsor_subj');
    expect(sp.params).toMatchObject({ sponsor: 'Harbour Haulage Co.' });
  });

  it('season grid, calendar and inbox follow the state after a race', async () => {
    vi.useFakeTimers();
    const c = createDemoClient({ seed: 3, raceMs: 500 });
    const unread0 = (await c.query<HubPayload>('query.hub')).badges.unread;
    await c.cmd('cmd.prerace.launch', {});
    await vi.advanceTimersByTimeAsync(600);
    const se = await c.query<SeasonPayload>('query.season');
    expect(se.detail.roundIdx).toBe(2);
    expect(se.detail.standings.length).toBe(16);
    expect(se.detail.standings.every((r) => r.cells.filter((x) => x.ran).length === 2)).toBe(true);
    const cal = await c.query<CalendarPayload>('query.calendar');
    expect(cal.slots.slice(0, 2).every((x) => x.status === 'completed' || x.status === 'dnf')).toBe(true);
    expect(cal.slots[2].status).toBe('enrolled');
    expect(cal.slots[2].clickable).toBe(true);
    const inbox = await c.query<InboxPayload>('query.inbox');
    expect(inbox.messages[0].id).toBe('demo_r2');
    expect((await c.query<HubPayload>('query.hub')).badges.unread).toBe(unread0 + 1);
    await c.cmd('cmd.inbox.markAllRead', {});
    expect((await c.query<HubPayload>('query.hub')).badges.unread).toBe(0);
    vi.useRealTimers();
  });

  it('after the last round there is nothing to launch and no next race', async () => {
    vi.useFakeTimers();
    const c = createDemoClient({ seed: 4, raceMs: 100 });
    for (let i = 0; i < 7; i++) {
      expect((await c.cmd<{ ok: boolean }>('cmd.prerace.launch', {})).ok).toBe(true);
      await vi.advanceTimersByTimeAsync(150);
    }
    expect((await c.cmd<{ ok: boolean }>('cmd.prerace.launch', {})).ok).toBe(false);
    expect((await c.query<HubPayload>('query.hub')).nextRace).toBeNull();
    expect((await c.query<TimeAdvancePayload>('query.time.preview')).resolved).toBe(false);
    vi.useRealTimers();
  });
});

describe('demo client contracts', () => {
  async function raceToPodium() {
    vi.useFakeTimers();
    const c = createDemoClient({ seed: 5, raceMs: 500, forcePos: 2 });
    await c.cmd('cmd.calendar.launch', { seriesId: 'demo_gt', round: 2 });
    await c.cmd('cmd.prerace.launch', {});
    await vi.advanceTimersByTimeAsync(600);
    vi.useRealTimers();
    return c;
  }
  it('podium -> offer in market and inbox -> negotiate -> sign changes team everywhere', async () => {
    const c = await raceToPodium();
    const m = await c.query<MarketPayload>('query.market');
    expect(m.offers.length).toBe(1);
    const id = m.offers[0].id;
    const inbox = await c.query<InboxPayload>('query.inbox');
    expect(inbox.messages[0].offerId).toBe(id);
    expect((await c.query<NegotiatePayload>('query.negotiate', { offerId: id })).found).toBe(true);
    expect((await c.cmd<{ ok: boolean }>('cmd.negotiate.send', { offerId: id, terms: { salaryPct: 90 } })).ok).toBe(true);
    const neg2 = await c.query<NegotiatePayload>('query.negotiate', { offerId: id });
    expect(neg2.negState).toBe(1);
    expect((await c.cmd<{ ok: boolean }>('cmd.negotiate.acceptTable', { negId: neg2.negId })).ok).toBe(true);
    expect((await c.cmd<{ ok: boolean }>('cmd.negotiate.sign', { negId: neg2.negId })).ok).toBe(true);
    const garage = await c.query<GaragePayload>('query.garage');
    expect(garage.cars[0].name).toBe(m.offers[0].seatName);
    expect((await c.query<CareerStatePayload>('query.career_state')).contract!.teamName).toBe(m.offers[0].team);
    expect((await c.query<HubPayload>('query.hub')).lifecycle.contractTeams).toEqual([m.offers[0].team]);
    expect((await c.query<MarketPayload>('query.market')).contract!.teamName).toBe(m.offers[0].team);
    expect((await c.cmd<{ ok: boolean }>('cmd.negotiate.sign', { negId: neg2.negId })).ok).toBe(false);
  });
  it('inbox decline rejects the offer', async () => {
    const c = await raceToPodium();
    const msg = (await c.query<InboxPayload>('query.inbox')).messages[0];
    await c.cmd('cmd.inbox.answer', { id: msg.id, choice: 1 });
    expect((await c.query<MarketPayload>('query.market')).offers[0].state).toBe(2);
  });
  it('cmd.market.accept signs the listed terms', async () => {
    const c = await raceToPodium();
    const o = (await c.query<MarketPayload>('query.market')).offers[0];
    expect((await c.cmd<{ ok: boolean }>('cmd.market.accept', { offerId: o.id })).ok).toBe(true);
    expect((await c.query<GaragePayload>('query.garage')).contract!.teamCar).toBe(o.seatName);
  });

  it('the sponsor shop stays shut until a podium, then sells and closes', async () => {
    expect((await createDemoClient({ seed: 5 }).query<CardsPayload>('query.cards')).shop).toBeNull();
    const c = await raceToPodium();
    const before = await c.query<CardsPayload>('query.cards');
    expect(before.shop!.raceId).toBe('demo_gt#2');
    const def = before.shop!.offers[0];
    expect((await c.cmd<{ ok: boolean }>('cmd.cards.buy', { id: def.id })).ok).toBe(true);
    expect((await c.cmd<{ ok: boolean }>('cmd.cards.buy', { id: def.id })).ok).toBe(false);
    const after = await c.query<CardsPayload>('query.cards');
    expect(after.money).toBe(before.money - def.cost);
    expect(after.shop!.boughtIds).toEqual([def.id]);
    expect(after.signed.map((x) => x.def.id)).toContain(def.id);
    expect((await c.cmd<{ ok: boolean }>('cmd.cards.reroll')).ok).toBe(true);
    expect((await c.query<CardsPayload>('query.cards')).shop!.offers[0].id).not.toBe(def.id);
    await c.cmd('cmd.cards.shop_close');
    expect((await c.query<CardsPayload>('query.cards')).shop).toBeNull();
  });
  it('every sponsor card uses an effect hook the card text knows', async () => {
    const known = /^(income\.(prize_mult|bonus)|xp\.mult|rep\.mult|cost\..+|special)$/;
    const c = await raceToPodium();
    const cards = await c.query<CardsPayload>('query.cards');
    for (const d of [...cards.signed.map((x) => x.def), ...cards.shop!.offers]) expect(d.fx.hook).toMatch(known);
  });

  it('unsupported cmd answers ok:false instead of throwing', async () => {
    const c = createDemoClient({ seed: 1 });
    await expect(c.cmd('cmd.garage.sell', { carId: 'x' })).resolves.toMatchObject({ ok: false, demo: true });
  });
  it('previously missing queries now answer', async () => {
    const c = createDemoClient({ seed: 1 });
    for (const m of ['query.events.content', 'query.paddock', 'query.superlicenses', 'query.content', 'query.cards'])
      await expect(c.query(m)).resolves.toBeTruthy();
  });
  it('an armed event pays its prize without consuming a championship round', async () => {
    vi.useFakeTimers();
    const c = createDemoClient({ seed: 3, raceMs: 500 });
    const before = await c.query<CalendarPayload>('query.calendar');
    const arm = await c.cmd<{ armed: boolean }>('cmd.event.arm', { eventId: 'demo_ev_midnight', carClassId: 'gt3' });
    expect(arm.armed).toBe(true);
    expect((await c.cmd<{ ok: boolean }>('cmd.prerace.launch', {})).ok).toBe(true);
    await vi.advanceTimersByTimeAsync(600);
    const after = await c.query<CalendarPayload>('query.calendar');
    expect(after.slots.filter((x) => x.status === 'completed').length).toBe(before.slots.filter((x) => x.status === 'completed').length);
    expect((await c.query<CareerStatePayload>('query.career_state')).flags.resultSeq).toBe(1);
    vi.useRealTimers();
  });
  it('a stale armed event does not hijack the next championship race', async () => {
    vi.useFakeTimers();
    const c = createDemoClient({ seed: 4, raceMs: 500 });
    const money0 = (await c.query<CareerStatePayload>('query.career_state')).profile.money;
    await c.cmd('cmd.event.arm', { eventId: 'demo_ev_midnight', carClassId: 'gt3' });
    await c.query('query.hub');   // player backed out to the hub
    expect((await c.cmd<{ ok: boolean }>('cmd.prerace.launch', {})).ok).toBe(true);
    await vi.advanceTimersByTimeAsync(600);
    const cal = await c.query<CalendarPayload>('query.calendar');
    expect(cal.slots.filter((x) => x.status === 'completed' || x.status === 'dnf').length).toBe(2);
    const res = await c.query<{ trackName: string }>('query.results');
    expect(res.trackName).not.toBe('Fairwater');
    expect(money0).toBeGreaterThan(0);
    vi.useRealTimers();
  });
});

describe('demo client: special events are raced once', () => {
  it('a raced event cannot be armed again', async () => {
    vi.useFakeTimers();
    const c = createDemoClient({ seed: 3, raceMs: 500 });
    expect((await c.cmd<{ armed: boolean }>('cmd.event.arm', { eventId: 'demo_ev_midnight', carClassId: 'gt3' })).armed).toBe(true);
    await c.cmd('cmd.prerace.launch', {});
    await vi.advanceTimersByTimeAsync(600);
    expect((await c.cmd<{ armed: boolean }>('cmd.event.arm', { eventId: 'demo_ev_midnight', carClassId: 'gt3' })).armed).toBe(false);
    expect((await c.cmd<{ armed: boolean }>('cmd.event.arm', { eventId: 'demo_ev_coastal', carClassId: 'gt3' })).armed).toBe(true);
    vi.useRealTimers();
  });
});
