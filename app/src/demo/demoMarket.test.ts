import { describe, it, expect } from 'vitest';
import { buildWorld } from './demoWorld';
import { maybeCreateOffer, counter, acceptTable, sign, marketView, negotiateView, decline } from './demoMarket';

function withPodium(seed = 1) {
  const s = buildWorld(seed);
  s.recent.unshift({ pos: 2, field: 16, track: 'X', finished: true });
  return s;
}
describe('demo market', () => {
  it('a podium creates one offer and an inbox mail', () => {
    const s = withPodium(); const n = s.inbox.length;
    const o = maybeCreateOffer(s)!;
    expect(o.state).toBe('open');
    expect(maybeCreateOffer(s)).toBeNull();
    expect(s.inbox.length).toBe(n + 1);
    expect(s.inbox[0].offerId).toBe(o.id);
    expect(s.inbox[0].family).toBe('offer');
    expect(o.salary).toBe(Math.round(s.salary * 1.2));
    expect(o.bonus).toBe(900);
  });
  it('two top-5 results also trigger; one does not', () => {
    const s = buildWorld(1);
    s.recent.unshift({ pos: 5, field: 16, track: 'X', finished: true });
    expect(maybeCreateOffer(s)).toBeNull();
    s.recent.unshift({ pos: 4, field: 16, track: 'Y', finished: true });
    expect(maybeCreateOffer(s)).not.toBeNull();
  });
  it('ask within 15% of listed accepted, above it countered once then rejected', () => {
    const s = withPodium(); const o = maybeCreateOffer(s)!;
    const listed = o.salary;
    expect(counter(s, o.id, 90).state).toBe('countered');
    expect(o.salary).toBeGreaterThan(listed);
    expect(o.salary).toBeLessThan(Math.round(listed * 1.2));
    expect(counter(s, o.id, 90).state).toBe('rejected');
    const s2 = withPodium(2); const o2 = maybeCreateOffer(s2)!;
    const listed2 = o2.salary;
    expect(counter(s2, o2.id, 50).state).toBe('open');
    expect(o2.salary).toBe(listed2);
    expect(negotiateView(s2, o2.id).negState).toBe(2);
    const s3 = withPodium(3); const o3 = maybeCreateOffer(s3)!;
    expect(counter(s3, o3.id, 10).state).toBe('open');
  });
  it('every answer to a proposal lands in the inbox with the new salary', () => {
    const s = withPodium(); const o = maybeCreateOffer(s)!;
    counter(s, o.id, 90);
    expect(s.inbox[0].subject).toBe('demo_mail_counter_subj');
    expect(s.inbox[0].params).toMatchObject({ team: o.team, salary: String(o.salary) });
    acceptTable(s, o.id);
    expect(s.inbox[0].subject).toBe('demo_mail_agreed_subj');
    const s2 = withPodium(2); const o2 = maybeCreateOffer(s2)!;
    counter(s2, o2.id, 50);
    expect(s2.inbox[0].subject).toBe('demo_mail_agreed_subj');
  });

  it('closed offers are not negotiable', () => {
    const s = withPodium(); const o = maybeCreateOffer(s)!;
    expect(negotiateView(s, o.id).negotiable).toBe(true);
    decline(s, o.id);
    expect(negotiateView(s, o.id).negotiable).toBe(false);
  });
  it('signing changes team; signing again fails', () => {
    const s = withPodium(); const o = maybeCreateOffer(s)!;
    const money = s.money;
    expect(sign(s, o.id).ok).toBe(true);
    expect(s.team.name).toBe(o.team);
    expect(s.money).toBe(money + 900);
    expect(s.drivers.find((d) => d.isPlayer)!.team).toBe(o.team);
    expect(sign(s, o.id).ok).toBe(false);
  });
  it('signing a rejected offer fails', () => {
    const s = withPodium(); const o = maybeCreateOffer(s)!;
    counter(s, o.id, 90); counter(s, o.id, 90);
    expect(sign(s, o.id).ok).toBe(false);
  });
  it('views follow the state', () => {
    const s = withPodium(); const o = maybeCreateOffer(s)!;
    expect(marketView(s).offers.map((x) => x.id)).toEqual([o.id]);
    expect(negotiateView(s, o.id).negState).toBe(-1);
    expect(negotiateView(s, 'nope').found).toBe(false);
    counter(s, o.id, 90);
    expect(negotiateView(s, o.id).negState).toBe(1);
    decline(s, o.id);
    expect(marketView(s).offers[0].state).toBe(2);
  });
});

describe('demo market: mail ids and badge counts', () => {
  it('counter then acceptTable keeps every inbox id distinct', () => {
    const s = withPodium(); const o = maybeCreateOffer(s)!;
    counter(s, o.id, 90);
    expect(acceptTable(s, o.id).ok).toBe(true);
    const ids = s.inbox.map((m) => m.id);
    expect(new Set(ids).size).toBe(ids.length);
  });
  it('hub and market agree on the open offers after a counter', async () => {
    const { hubView } = await import('./demoViews');
    const s = withPodium(); const o = maybeCreateOffer(s)!;
    counter(s, o.id, 90);
    expect(o.state).toBe('countered');
    expect(hubView(s).badges.directOffers).toBe(marketView(s).seekingYouCount);
    expect(hubView(s).badges.directOffers).toBe(1);
  });
});
