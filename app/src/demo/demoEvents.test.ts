import { describe, it, expect } from 'vitest';
import { demoEvents } from './demoEvents';
describe('demo events', () => {
  it('always has live, upcoming and closed events', () => {
    const now = Date.UTC(2026, 9, 13, 12);
    const ev = demoEvents(now);
    const live = ev.filter((e) => Date.parse(e.starts_at) <= now && now < Date.parse(e.ends_at));
    const up = ev.filter((e) => Date.parse(e.starts_at) > now);
    const closed = ev.filter((e) => Date.parse(e.ends_at) <= now);
    expect(live.length).toBeGreaterThanOrEqual(1);
    expect(up.length).toBeGreaterThanOrEqual(1);
    expect(closed.length).toBeGreaterThanOrEqual(1);
    for (const e of ev) { expect(e.payload?.classes.length).toBeGreaterThan(0); expect(e.teaser.title_es).toBeTruthy(); }
  });
});

describe('seeded store', () => {
  it('EventsScreen finds the events through the bridge store', async () => {
    const { seedSpecialEvents, DEMO_DATA_ROOT } = await import('./demoEvents');
    const { loadSpecialEvents } = await import('../events/eventsStore');
    await seedSpecialEvents(DEMO_DATA_ROOT, Date.now());
    expect(DEMO_DATA_ROOT).not.toBe('');   // the screens skip loading from an empty root
    const f = await loadSpecialEvents(DEMO_DATA_ROOT);
    expect(f?.events.length).toBe(5);
  });
});

describe('raced events and bought sponsors', () => {
  it('closing a raced event ends its window so the app stops listing it', async () => {
    const { seedSpecialEvents, closeSpecialEvent, DEMO_DATA_ROOT } = await import('./demoEvents');
    const { loadSpecialEvents } = await import('../events/eventsStore');
    const now = Date.now();
    await seedSpecialEvents(DEMO_DATA_ROOT, now);
    await closeSpecialEvent(DEMO_DATA_ROOT, 'demo_ev_midnight', now);
    const f = await loadSpecialEvents(DEMO_DATA_ROOT);
    expect(Date.parse(f!.events.find((e) => e.id === 'demo_ev_midnight')!.ends_at)).toBeLessThanOrEqual(now);
    expect(Date.parse(f!.events.find((e) => e.id === 'demo_ev_coastal')!.ends_at)).toBeGreaterThan(now);
  });
  it('a sponsor already signed cannot be bought again in a later shop', async () => {
    const { buyShopCard } = await import('./demoEvents');
    const s = { money: 1e6, sponsors: [] as string[] } as unknown as import('./demoWorld').DemoState;
    const first = { raceId: 'r2', bought: [], rerolls: 0 };
    const id = 'demo_quill';
    expect(buyShopCard(s, first, id)).toBe(true);
    const money = s.money;
    expect(buyShopCard(s, { raceId: 'r3', bought: [], rerolls: 0 }, id)).toBe(false);
    expect(s.money).toBe(money);
    expect(s.sponsors.filter((x) => x === id).length).toBe(1);
  });
});
