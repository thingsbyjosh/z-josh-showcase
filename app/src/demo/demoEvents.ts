// Special events, plus the payloads for screens that would otherwise render empty (sponsor cards, paddock,
// superlicenses, content, profiles). All names are invented. Event windows are relative to `nowMs`.
import type { SpecialEvent } from '../events/eventsClient';
import { loadSpecialEvents, saveSpecialEvents } from '../events/eventsStore';
import type {
  CardDefView, CardsPayload, ContentPayload, EventsContentPayload, PaddockPayload, PaddockSeat, PreracePayload,
  ProfilesPayload, SuperlicensesPayload,
} from '../api/contracts';
import type { DemoState } from './demoWorld';

const DAY = 86400000;
const iso = (ms: number) => new Date(ms).toISOString();
export const DEMO_CLASS = 'gt3';
/** Non-empty data root: EventsScreen and AppShell skip loading special events when it is ''. */
export const DEMO_DATA_ROOT = 'demo-data';

interface EventSpec {
  id: string; from: number; to: number; track: string; trackName: string; laps: number; credits: number; rep: number;
  en: [string, string, string]; es: [string, string, string];   // title, desc, body
}
const SPECS: EventSpec[] = [
  { id: 'demo_ev_midnight', from: -2, to: 3, track: 'fairwater', trackName: 'Fairwater', laps: 10, credits: 40000, rep: 20,
    en: ['Midnight Sprint', 'A short night race under the floodlights.', 'Fairwater after dark: ten laps, lights on, and a purse for whoever keeps their nerve when the spray starts.'],
    es: ['Sprint de Medianoche', 'Una carrera nocturna corta bajo los focos.', 'Fairwater de noche: diez vueltas, con luces, y una bolsa para quien mantenga la sangre fría cuando empiece la lluvia.'] },
  { id: 'demo_ev_coastal', from: -2, to: 6, track: 'bellhaven', trackName: 'Bellhaven', laps: 12, credits: 55000, rep: 30,
    en: ['Coastal Classic', 'The seaside road course, one afternoon only.', 'Bellhaven hosts its yearly classic: fast sweepers off the sea wall and a long back straight where the slipstream decides everything.'],
    es: ['Clásica de la Costa', 'El circuito junto al mar, una sola tarde.', 'Bellhaven acoge su clásica anual: curvas rápidas junto al dique y una recta larga donde el rebufo lo decide todo.'] },
  { id: 'demo_ev_harvest', from: 4, to: 8, track: 'cindermoor', trackName: 'Cindermoor', laps: 8, credits: 30000, rep: 15,
    en: ['Harvest Cup', 'Autumn sprint on the moor.', 'Cindermoor in the golden hour: eight laps of cold tarmac and a field of local heroes.'],
    es: ['Copa de la Cosecha', 'Sprint otoñal en el páramo.', 'Cindermoor en la hora dorada: ocho vueltas de asfalto frío y una parrilla de héroes locales.'] },
  { id: 'demo_ev_winter', from: 10, to: 15, track: 'glasshouse', trackName: 'Glasshouse', laps: 15, credits: 80000, rep: 45,
    en: ['Winter Endurance Trial', 'The longest event of the season.', 'Glasshouse at the turn of the season: fifteen laps that reward patience, tyre care and a clean sheet.'],
    es: ['Prueba de Resistencia de Invierno', 'El evento más largo de la temporada.', 'Glasshouse a final de temporada: quince vueltas que premian la paciencia, el cuidado de las gomas y una carrera limpia.'] },
  { id: 'demo_ev_founders', from: -10, to: -3, track: 'holloway', trackName: 'Holloway', laps: 9, credits: 35000, rep: 20,
    en: ['Founders Trophy', 'Last week invitational, now closed.', 'Holloway hosted the invitational for the founding teams. The chequered flag has fallen; the results live on.'],
    es: ['Copa de los Fundadores', 'El invitacional de la semana pasada, ya cerrado.', 'Holloway acogió el invitacional de los equipos fundadores. La bandera a cuadros ya cayó; los resultados quedan.'] },
];

export function demoEvents(nowMs: number): SpecialEvent[] {
  return SPECS.map((e) => ({
    id: e.id,
    starts_at: iso(nowMs + e.from * DAY),
    ends_at: iso(nowMs + e.to * DAY),
    tier_min: 'free',
    teaser: { title_en: e.en[0], title_es: e.es[0], desc_en: e.en[1], desc_es: e.es[1], body_en: e.en[2], body_es: e.es[2] },
    payload: { classes: [DEMO_CLASS], track: e.track, laps: e.laps, rewards: { credits: e.credits, rep: e.rep } },
    rewards: { credits: e.credits, rep: e.rep },
  }));
}

export async function seedSpecialEvents(dataRoot: string, nowMs: number): Promise<void> {
  await saveSpecialEvents(dataRoot, demoEvents(nowMs), iso(nowMs), iso(nowMs));
}

/** Ends a raced event's window now, so the app stops listing it (an ended event is not shown). */
export async function closeSpecialEvent(dataRoot: string, id: string, nowMs: number): Promise<void> {
  const f = await loadSpecialEvents(dataRoot);
  if (!f) return;
  const events = f.events.map((e) => (e.id === id ? { ...e, ends_at: iso(nowMs - 1000) } : e));
  await saveSpecialEvents(dataRoot, events, f.fetchedAt, f.serverNow);
}

let seeding: Promise<string> | null = null;
/** Seeds the events file once per page load and resolves to the demo data root. The app boot awaits it,
 *  so the hub's first read of special_events.json finds the file. */
export function seedDemoData(nowMs = Date.now()): Promise<string> {
  return (seeding ??= seedSpecialEvents(DEMO_DATA_ROOT, nowMs).catch(() => {}).then(() => DEMO_DATA_ROOT));
}

/** A signed sponsor's effect on a race: its trigger, hook and value, or null for an unknown id. */
export function sponsorFx(id: string): { name: string; trigger: string; hook: string; v: number } | null {
  const d = Object.values(DEFS).find((x) => x.id === id);
  return d ? { name: d.name, trigger: d.trigger, hook: d.fx.hook, v: d.fx.v } : null;
}

export interface ArmedEvent { id: string; prize: number; track: string; trackSlug: string; laps: number }

/** Resolves an event id to what the race flow needs; null if unknown. */
export function armedEvent(id: string | undefined): ArmedEvent | null {
  const e = SPECS.find((x) => x.id === id);
  return e ? { id: e.id, prize: e.credits, track: e.trackName, trackSlug: e.track, laps: e.laps } : null;
}

export function eventsContentView(): EventsContentPayload {
  return {
    scanned: true,
    events: SPECS.map((e) => ({
      id: e.id, trackOk: true, trackProduct: '', trackName: e.trackName,
      classes: [{ id: DEMO_CLASS, display: 'GT3', ok: true, product: '' }],
    })),
  };
}

/** The pre-race payload retargeted to the armed event. */
export function eventPreraceView(p: PreracePayload, a: ArmedEvent): PreracePayload {
  return { ...p, trackName: a.track, raceName: a.track, laps: a.laps, raceLaps: a.laps, launchLaps: a.laps, reward: a.prize, clsEmpty: false };
}

// ---- screens that would otherwise be empty ----
const card = (id: string, name: string, en: string, es: string, rarity: number, trigger: string, hook: string, v: number, cost: number): CardDefView => ({
  id, name, tag: { en, es }, rarity, rarityKey: (['local', 'regional', 'national', 'international', 'elite'] as const)[rarity],
  life: { k: 'races', n: 8 }, trigger, fx: { hook, v }, clause: null, special: null,
  upgrade: { after: 4, cost: Math.round(cost * 0.6), levels: 2 }, cost,
});
const DEFS = {
  harbor: card('demo_harbor', 'Harbour Haulage Co.', 'Pays on every podium', 'Paga en cada podio', 1, 'podium', 'income.prize_mult', 1.15, 12000),
  lumen: card('demo_lumen', 'Lumora Fuels', 'Steady money, race after race', 'Dinero constante, carrera tras carrera', 0, 'passive', 'income.bonus', 600, 6000),
  quill: card('demo_quill', 'Quillfeather Watches', 'Bonus for a clean race', 'Prima por una carrera limpia', 2, 'clean', 'income.prize_mult', 1.25, 22000),
  mist: card('demo_mist', 'Mistfall Coffee', 'Pays when you score in the top eight', 'Paga si puntúas entre los ocho primeros', 0, 'top8', 'income.prize_mult', 1.1, 7000),
  ironbark: card('demo_ironbark', 'Ironbark Tools', 'Extra on a win', 'Extra por una victoria', 3, 'win', 'income.prize_mult', 1.4, 38000),
};
const signedCard = (def: CardDefView, slot: number, level: number) => ({
  def, level, slot, racesActive: 3, racesLeft: 5, usesLeft: 0, clauseMiss: 0, invested: def.cost, sellValue: Math.round(def.cost * 0.4),
  fxV: def.fx.v, upgradeNext: def.upgrade ? { toLevel: level + 1, cost: def.upgrade.cost, need: 4, have: 3, ready: false } : null,
});

/** Sponsor shop opened by a podium; `bought` holds the card ids signed in it. */
export interface DemoShop { raceId: string; bought: string[]; rerolls: number }
const SHOP_OFFERS = [DEFS.quill, DEFS.ironbark, DEFS.mist];
const SHOP_MAX_BUYS = 2;
const SHOP_REROLL_COST = 2000;
/** A reroll deals the same three sponsors in a new order: the demo has no wider pool. */
const shopOffers = (shop: DemoShop) => SHOP_OFFERS.map((_, i) => SHOP_OFFERS[(i + shop.rerolls) % SHOP_OFFERS.length]);

export function cardsView(s: DemoState, shop: DemoShop | null = null, signedIds: string[] = []): CardsPayload {
  const extra = signedIds.map((id) => SHOP_OFFERS.find((d) => d.id === id)).filter((d): d is CardDefView => !!d);
  return {
    signed: [signedCard(DEFS.harbor, 0, 1), signedCard(DEFS.lumen, 1, 1), ...extra.map((d) => signedCard(d, -1, 1))],
    shop: shop && {
      raceId: shop.raceId, rerolls: shop.rerolls, bought: shop.bought.length, maxBuys: SHOP_MAX_BUYS, offerCount: SHOP_OFFERS.length,
      rerollCost: SHOP_REROLL_COST, offers: shopOffers(shop), boughtIds: shop.bought,
    },
    slotsUnlocked: 3, phase: 1, money: s.money,
  };
}

/** Signs a shop card: pays its cost; at most SHOP_MAX_BUYS per shop and only cards on offer. */
export function buyShopCard(s: DemoState, shop: DemoShop | null, id: string): boolean {
  const def = SHOP_OFFERS.find((d) => d.id === id);
  if (!shop || !def || shop.bought.includes(id) || s.sponsors.includes(id) || shop.bought.length >= SHOP_MAX_BUYS || s.money < def.cost) return false;
  s.money -= def.cost;
  shop.bought.push(id);
  s.sponsors.push(id);
  return true;
}

export function paddockView(s: DemoState): PaddockPayload {
  const teams = [...new Set(s.drivers.map((d) => d.team))];
  const level = 3;
  const seats: PaddockSeat[] = teams.map((team, i) => {
    const occ = s.drivers.find((d) => d.team === team && !d.isPlayer);
    return {
      seriesId: s.series.id, seriesDisplay: s.series.name, level, team,
      status: i === 0 ? 'renewed' : i === 3 ? 'free' : 'unknown', applied: false, soundedYou: false,
      winnerName: '', occupantName: i === 3 ? '' : (occ?.name ?? ''), expiryYear: i === 3 ? 0 : 2026 + (i % 2), perRace: 0,
      signed: i === 0, occupantAge: 24 + i * 2,
    };
  });
  return {
    phase: 'season', year: 2026, doy: s.today, targetYear: 2027, windowYear: 0, daysToWindow: Math.max(1, 300 - s.today), daysToDecision: 0,
    seats,
    contracts: [{
      seriesId: s.series.id, seriesDisplay: s.series.name, team: s.team.name, seasonsLeft: 1, perRace: s.salary, objTargetPos: 6,
      currentPos: 6, teammate: s.drivers.find((d) => d.team === s.team.name && !d.isPlayer)?.name, teammatePos: 12, breakFee: s.salary * 4,
    }],
    series: [{ id: s.series.id, display: s.series.name, level, discipline: 'gt', seats: teams.length, inPlay: 0, yours: 1, expiring: 2, mine: true }],
    world: [],
  };
}

export function superlicensesView(): SuperlicensesPayload {
  const row = (champId: string, champDisplay: string, unlocked: boolean) => ({
    champId, champDisplay, unlocked, qualDisplay: 'Qualifier Cup', qualHasCal: true, qualNextDate: 'Nov 12', gateTop: 5,
    qualLevel: 6, hasFeeder: true, feederClass: 'GT3', feederTop: 3, feederHasContext: true, feederContextPos: 6, feederContextSeries: 'GT Masters Demo',
  });
  return { licenses: [row('demo_open_wheel', 'Apex Open Wheel Series', false), row('demo_endurance', 'Meridian Endurance Cup', true)] };
}

export function contentView(): ContentPayload {
  return {
    scanned: true,
    products: [
      { id: 'demo_coastal_pack', display: 'Coastal Circuits Pack', kind: 'tracks', detected: true, enabled: true, override: 'auto', owned: 'owned' },
      { id: 'demo_classic_pack', display: 'Classic Racers Pack', kind: 'cars', detected: true, enabled: true, override: 'auto', owned: 'owned' },
    ],
  };
}

export function profilesView(): ProfilesPayload {
  return { active: 'alex-bravo', profiles: [{ slug: 'alex-bravo', name: 'Alex Bravo', mode: 'career' }] };
}

/** Redeals the shop for SHOP_REROLL_COST. */
export function rerollShop(s: DemoState, shop: DemoShop | null): boolean {
  if (!shop || s.money < SHOP_REROLL_COST) return false;
  s.money -= SHOP_REROLL_COST;
  shop.rerolls++;
  return true;
}
