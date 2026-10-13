// Contracts for the showcase demo: offers after good results, one counter-offer, signing.
import type { InboxMessage, MarketOffer, MarketPayload, NegotiatePayload } from '../api/contracts';
import marketFx from '../../ui-fixtures/market.json';
import negotiateFx from '../../ui-fixtures/negotiate.json';
import type { DemoOffer, DemoState } from './demoWorld';

const clone = <T,>(x: unknown): T => JSON.parse(JSON.stringify(x)) as T;
const BONUS = 900;
const NEG_PREFIX = 'neg_';

// Invented cars per team; unknown teams get the default GT3.
const CAR_BY_TEAM: Record<string, string> = {
  'Team Zenith': 'Halcyon GT3 R',
  'Team Apex': 'Vanta GT3 S',
  'Falcon Racing': 'Corvane GT3',
  'Halcyon Racing': 'Halcyon GT3 R Evo',
  'Northgate Motorsport': 'Northgate NG3',
  'Kestrel Works': 'Kestrel K3 GT',
};
const carOf = (team: string) => CAR_BY_TEAM[team] ?? 'Halcyon GT3 R';

const isOpenish = (o: DemoOffer) => o.state === 'open' || o.state === 'countered';
const find = (s: DemoState, id: string) => s.offers.find((o) => o.id === id);
export const negIdOf = (offerId: string) => NEG_PREFIX + offerId;
export function offerByNegId(s: DemoState, negId: string | undefined): DemoOffer | undefined {
  return negId && negId.startsWith(NEG_PREFIX) ? find(s, negId.slice(NEG_PREFIX.length)) : undefined;
}
// The app sends salaryPct as a salary-vs-cover split (10 more cover / 50 neutral / 90 more salary):
// 50 is the listed salary, each side moves it by up to ±20 %.
const salaryFor = (listed: number, pct: number) => Math.round(listed * (1 + (pct - 50) / 200));
/** The team accepts outright any ask at most this much above the listed salary. */
const ACCEPT_OVER = 0.15;

/** The best rival team (highest average rating) other than the player's current one, or null. */
function betterTeam(s: DemoState): { name: string; color: string } | null {
  const agg = new Map<string, { sum: number; n: number; color: string }>();
  for (const d of s.drivers) {
    if (d.isPlayer || d.team === s.team.name) continue;
    const a = agg.get(d.team) ?? { sum: 0, n: 0, color: d.teamColor };
    a.sum += d.rating; a.n++;
    agg.set(d.team, a);
  }
  let best: { name: string; color: string; avg: number } | null = null;
  for (const [name, a] of agg) {
    const avg = a.sum / a.n;
    if (!best || avg > best.avg) best = { name, color: a.color, avg };
  }
  return best ? { name: best.name, color: best.color } : null;
}

export function maybeCreateOffer(s: DemoState): DemoOffer | null {
  const [r0, r1] = s.recent;
  if (!r0) return null;
  const podium = r0.finished && r0.pos <= 3;
  const twoTop5 = r1 !== undefined && r0.finished && r0.pos <= 5 && r1.finished && r1.pos <= 5;
  if (!podium && !twoTop5) return null;
  if (s.offers.some(isOpenish)) return null;
  const t = betterTeam(s);
  if (!t) return null;
  const salary = Math.round(s.salary * 1.2);
  const o: DemoOffer = {
    id: 'offer_' + (s.offers.length + 1), team: t.name, teamColor: t.color, car: carOf(t.name),
    salary, bonus: BONUS, state: 'open', counterUsed: false, listed: salary, agreed: false, history: [],
  };
  s.offers.push(o);
  const msg: InboxMessage = {
    id: 'demo_' + o.id, week: Math.ceil(s.today / 7), year: 2026,
    from: 'mkt_offer_inbox', subject: 'demo_mail_offer_subj', body: 'demo_mail_offer_body',
    params: { team: o.team, car: o.car, salary: String(salary), bonus: String(BONUS) },
    read: false, kind: 'offer', family: 'offer', you: false, offerId: o.id, negId: negIdOf(o.id),
    actions: [
      { choice: 0, label: 'demo_offer_accept', kind: 'primary' },
      { choice: 1, label: 'demo_offer_decline', kind: 'ghost' },
    ],
  };
  s.inbox.unshift(msg);
  return o;
}

function settleMail(s: DemoState, offerId: string, resolvedKey: string) {
  for (const m of s.inbox) {
    if (m.offerId === offerId) { delete m.actions; delete m.offerId; m.resolvedKey = resolvedKey; m.read = true; }
  }
}

/** The team's answer to a proposal lands in the inbox, as it would in the game. */
function replyMail(s: DemoState, o: DemoOffer, subject: string, body: string) {
  s.inbox.unshift({
    id: `demo_reply_${o.id}_${++s.mailSeq}`, week: Math.ceil(s.today / 7), year: 2026,
    from: 'mkt_offer_inbox', subject, body,
    params: { team: o.team, car: o.car, salary: String(o.salary) },
    read: false, kind: 'manager', family: 'offer', you: false,
  });
}

export function counter(s: DemoState, offerId: string, salaryPct: number): { ok: boolean; state: DemoOffer['state'] } {
  const o = find(s, offerId);
  if (!o) return { ok: false, state: 'rejected' };
  if (!isOpenish(o)) return { ok: false, state: o.state };
  const day = s.today;
  const hist = [...(o.history ?? []), { day, who: 0, kind: 0, demandsMask: 0, salaryPct }];
  o.history = hist;
  const listed = o.listed ?? o.salary;
  const asked = salaryFor(listed, salaryPct);
  if (asked <= listed * (1 + ACCEPT_OVER)) {
    o.state = 'open'; o.agreed = true; o.tablePct = salaryPct; o.salary = asked;
    replyMail(s, o, 'demo_mail_agreed_subj', 'demo_mail_agreed_body');
  } else if (!o.counterUsed) {
    // Meet halfway between the listed salary and the ask: never below what was first offered.
    const mid = Math.round((salaryPct + 50) / 2);
    o.counterUsed = true; o.state = 'countered'; o.agreed = false; o.tablePct = mid; o.salary = salaryFor(listed, mid);
    hist.push({ day, who: 1, kind: 1, demandsMask: 0, salaryPct: mid });
    replyMail(s, o, 'demo_mail_counter_subj', 'demo_mail_counter_body');
  } else {
    o.state = 'rejected'; o.agreed = false;
    settleMail(s, o.id, 'demo_mail_offer_declined');
  }
  return { ok: true, state: o.state };
}

/** Takes the team's counter-offer on the table. */
export function acceptTable(s: DemoState, offerId: string): { ok: boolean } {
  const o = find(s, offerId);
  if (!o || o.state !== 'countered') return { ok: false };
  o.state = 'open'; o.agreed = true;
  replyMail(s, o, 'demo_mail_agreed_subj', 'demo_mail_agreed_body');
  return { ok: true };
}

export function decline(s: DemoState, offerId: string): { ok: boolean } {
  const o = find(s, offerId);
  if (!o || !isOpenish(o)) return { ok: false };
  o.state = 'rejected';
  settleMail(s, o.id, 'demo_mail_offer_declined');
  return { ok: true };
}

export function sign(s: DemoState, offerId: string): { ok: boolean } {
  const o = find(s, offerId);
  if (!o || !isOpenish(o)) return { ok: false };
  o.state = 'signed';
  for (const x of s.offers) {
    if (x !== o && isOpenish(x)) { x.state = 'rejected'; settleMail(s, x.id, 'demo_mail_offer_declined'); }
  }
  s.team = { name: o.team, color: o.teamColor, car: o.car };
  s.salary = o.salary;
  s.money += o.bonus;
  const me = s.drivers.find((d) => d.isPlayer);
  if (me) { me.team = o.team; me.teamColor = o.teamColor; }
  settleMail(s, o.id, 'demo_mail_offer_signed');
  s.inbox.unshift({
    id: 'demo_signed_' + o.id, week: Math.ceil(s.today / 7), year: 2026,
    from: 'inbox_paddock', subject: 'demo_mail_signed_subj', body: 'demo_mail_signed_body',
    params: { team: o.team, car: o.car, salary: String(o.salary), bonus: String(o.bonus) },
    read: false, kind: 'manager', family: 'progress', you: false,
  });
  return { ok: true };
}

const negState = (o: DemoOffer): number =>
  o.state === 'signed' ? 10 : o.state === 'rejected' ? 11 : o.state === 'countered' ? 1 : o.agreed ? 2 : -1;

export function marketView(s: DemoState): MarketPayload {
  const m = clone<MarketPayload>(marketFx);
  const tpl = m.offers.find((o) => o.category === 'direct') ?? m.offers[0];
  m.offers = s.offers.map((o): MarketOffer => ({
    ...clone<MarketOffer>(tpl),
    id: o.id, category: 'direct', direct: true, state: o.state === 'signed' ? 1 : o.state === 'rejected' ? 2 : 0,
    team: o.team, teamColor: o.teamColor, title: `${o.team} seat`, titleEn: `${o.team} seat`,
    signBonus: o.bonus, perRace: o.salary, seatName: o.car, fixesCar: true, teamCarName: o.car,
    negotiable: isOpenish(o), negState: negState(o), canAfford: true, weeksLeft: 4, daysLeft: 28,
  }));
  m.stats = { wins: s.wins, podiums: s.podiums, races: s.races };
  m.reputation = { ...m.reputation, score: s.rep };
  m.contract = { count: 1, teamName: s.team.name, seriesName: s.series.name, kind: 0 };
  m.seekingYouCount = s.offers.filter(isOpenish).length;
  m.openSeatsCount = 0;
  m.watching = [];
  return m;
}

export function negotiateView(s: DemoState, offerId: string): NegotiatePayload {
  const n = clone<NegotiatePayload>(negotiateFx);
  const o = find(s, offerId);
  if (!o) return { ...n, found: false, offerId };
  return {
    ...n, found: true, offerId: o.id, teamName: o.team, teamColor: o.teamColor,
    startBonus: o.bonus, startPerRace: o.salary, startDurationSeasons: 1,
    negotiable: isOpenish(o),
    credits: s.money,
    negId: negIdOf(o.id), negState: negState(o), negRound: o.history?.filter((h) => h.who === 0).length ?? 0,
    tableIsCounter: o.state === 'countered',
    table: { demands: Array(8).fill(false), salaryPct: o.tablePct ?? 50, sponsorTierRank: -1 },
    history: (o.history ?? []).map((h) => ({ ...h })),
    respondInDays: 0, deadlineWeeks: 4, deadlineDays: 28,
  };
}
