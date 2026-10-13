// Showcase demo client. Not part of the desktop app: the export copies this file into the public tree
// and points App.tsx at it. It wraps the fixture mock and turns it into a small living career.
import type { ApiClientLike, GameStatePayload, RaceConfigPayload } from '../api/contracts';
import { createMockApiClient } from '../api/mockClient';
import { setSimRace } from '../ui/prefs';
import i18n, { initI18n, detectLocale } from '../i18n';
import { buildWorld } from './demoWorld';
import { nextRound, simulateRace } from './demoEngine';
import { acceptTable, counter, decline, marketView, negotiateView, offerByNegId, sign } from './demoMarket';
import { registerDemoI18n } from './demoI18n';
import {
  armedEvent, buyShopCard, closeSpecialEvent, DEMO_DATA_ROOT, cardsView, rerollShop, type DemoShop, contentView, eventPreraceView, eventsContentView, paddockView, profilesView, seedDemoData,
  superlicensesView, type ArmedEvent,
} from './demoEvents';
import {
  calendarView, careerStateView, garageView, hubView, inboxView, preraceView, profileView, seasonView, standingsView, timePreview,
} from './demoViews';

export interface DemoClientOpts { seed?: number; raceMs?: number; forcePos?: number /* tests only */ }

export function createDemoClient(opts: DemoClientOpts = {}): ApiClientLike {
  // addResourceBundle only exists once i18next is initialised; the app normally did it before building the client.
  if (!i18n.isInitialized) initI18n(detectLocale());
  registerDemoI18n();
  setSimRace(true);
  const raceMs = opts.raceMs ?? 6000;
  const s = buildWorld(opts.seed ?? 20261013);
  const mock = createMockApiClient();
  let running = false;
  let armed: ArmedEvent | null = null;   // special event armed from EventPrerace; the next launch runs it
  let shop: DemoShop | null = null;      // sponsor shop opened by a championship podium
  const racedEvents = new Set<string>(); // special events already run: each can be raced once
  const signedCards: string[] = [];      // cards signed in the shop, shown after the starting ones
  let launched: Partial<RaceConfigPayload> | null = null;   // the race being simulated, laid over the race_config fixture
  // The events screen reads the (in-memory) bridge store; seed it without blocking client creation.
  void seedDemoData();

  const gameState = (): GameStatePayload => ({
    gameState: running ? 2 : 1, sessionState: running ? 5 : 0, atMenu: !running, shmMapped: true,
    launchPhase: 0, cfgFailN: 0, cfgFailGroups: '',
  });

  const queries: Record<string, (p?: object) => unknown> = {
    'query.hub': () => { armed = null; return hubView(s); },   // leaving EventPrerace lands on a tab; drop the stale arm
    'query.career_state': () => careerStateView(s),
    'query.standings': () => standingsView(s),
    'query.calendar': () => { armed = null; return calendarView(s); },
    'query.season': () => seasonView(s),
    'query.profile': (p) => profileView(s, !!(p as { full?: boolean } | undefined)?.full),
    'query.prerace': () => (armed ? eventPreraceView(preraceView(s), armed) : preraceView(s)),
    'query.inbox': () => inboxView(s),
    'query.results': () => s.lastResult,   // null before the first race -> falls back to the fixture
    'query.time.preview': () => timePreview(s),
    'query.game_state': () => gameState(),
    'query.market': () => marketView(s),
    'query.negotiate': (p) => negotiateView(s, (p as { offerId?: string } | undefined)?.offerId ?? ''),
    'query.garage': () => garageView(s),
    'query.events.content': () => eventsContentView(),
    'query.cards': () => cardsView(s, shop, signedCards),
    'query.paddock': () => paddockView(s),
    'query.superlicenses': () => superlicensesView(),
    'query.content': () => contentView(),
    'query.profiles': () => profilesView(),
  };

  function launch(): { ok: boolean; launched?: boolean } {
    const ev = armed;
    if (running || (!ev && !nextRound(s))) return { ok: false };
    const n = nextRound(s);
    const day = new Date(Date.UTC(2026, 0, ev || !n ? s.today : n.doy));
    const raceCfg: Partial<RaceConfigPayload> = launched = {
      sim: true, raceId: ev ? ev.id : `${s.series.id}#${n!.round}`, seriesName: ev ? '' : s.series.name,
      trackName: ev ? ev.track : n!.track, laps: ev ? ev.laps : n!.laps, durationMin: 0, opponents: s.drivers.length - 1,
      date: { day: day.getUTCDate(), month: day.getUTCMonth() + 1, year: 2026 },
    };
    if (ev) {
      racedEvents.add(ev.id);
      void closeSpecialEvent(DEMO_DATA_ROOT, ev.id, Date.now()).catch(() => {});   // off the list and the hub
    }
    running = true;
    s.settlePending = true;
    armed = null;
    setTimeout(() => {
      // an event race leaves its prize and a palmares entry but consumes no championship round
      simulateRace(s, { ...(opts.forcePos ? { forcePos: opts.forcePos } : {}), ...(ev ? { eventPrize: ev.prize, eventTrack: ev.track } : {}) });
      if (!ev && (s.recent[0]?.pos ?? 99) <= 3) shop = { raceId: raceCfg.raceId ?? '', bought: [], rerolls: 0 };
      running = false;
    }, raceMs);
    return { ok: true, launched: true };
  }

  const markRead = (id: string | undefined) => { for (const m of s.inbox) if (m.id === id) m.read = true; };

  return {
    status: 'connected',
    async query<T>(method: string, params?: object): Promise<T> {
      const q = queries[method];
      if (q) {
        const v = q(params);
        // A fresh copy per answer, as the engine's JSON would be: views hand out live state
        // (s.inbox, s.offers…) and screens memoise on identity, so a shared reference never refreshes.
        if (v !== null && v !== undefined) return structuredClone(v) as T;
      }
      if (method === 'query.race_config' && launched)
        return { ...(await mock.query<RaceConfigPayload>(method, params)), ...launched } as T;
      return mock.query<T>(method, params);
    },
    async cmd<T>(method: string, params?: object): Promise<T> {
      switch (method) {
        case 'cmd.calendar.launch': {
          // as in the app: the calendar only opens the pre-race screen; cmd.prerace.launch runs the race
          armed = null;
          const ok = !running && nextRound(s) !== null;
          return { ok, launched: ok } as T;
        }
        case 'cmd.prerace.launch':
          return launch() as T;
        case 'cmd.event.arm': {
          const a0 = armedEvent((params as { eventId?: string } | undefined)?.eventId);
          const a = a0 && !racedEvents.has(a0.id) ? a0 : null;   // a raced event cannot be entered again
          armed = a;
          return (a ? { ok: true, armed: true } : { ok: false, armed: false }) as T;
        }
        case 'cmd.time.advance': {
          const p = timePreview(s);
          if (p.resolved) s.today = p.stop.doy;
          return { ...p, executed: p.resolved } as T;
        }
        case 'cmd.inbox.answer': {
          const p = params as { id?: string; choice?: number } | undefined;
          const m = s.inbox.find((x) => x.id === p?.id);
          markRead(p?.id);
          if (m?.offerId) return { ok: (p?.choice === 0 ? sign(s, m.offerId) : decline(s, m.offerId)).ok } as T;
          return { ok: true } as T;
        }
        case 'cmd.market.accept': {
          const r = sign(s, (params as { offerId?: string } | undefined)?.offerId ?? '');
          return (r.ok ? r : { ok: false, code: 'closed' }) as T;
        }
        case 'cmd.market.decline':
          return decline(s, (params as { offerId?: string } | undefined)?.offerId ?? '') as T;
        case 'cmd.negotiate.send': {
          const p = params as { offerId?: string; terms?: { salaryPct?: number } } | undefined;
          const r = counter(s, p?.offerId ?? '', p?.terms?.salaryPct ?? 50);
          return (r.ok ? { ok: true } : { ok: false, code: 'closed' }) as T;
        }
        case 'cmd.negotiate.acceptTable': {
          const o = offerByNegId(s, (params as { negId?: string } | undefined)?.negId);
          return (o && acceptTable(s, o.id).ok ? { ok: true } : { ok: false, code: 'closed' }) as T;
        }
        case 'cmd.negotiate.sign': {
          const o = offerByNegId(s, (params as { negId?: string } | undefined)?.negId);
          return (o && sign(s, o.id).ok ? { ok: true } : { ok: false, code: 'closed' }) as T;
        }
        case 'cmd.negotiate.walk': {
          const o = offerByNegId(s, (params as { negId?: string } | undefined)?.negId);
          return (o ? decline(s, o.id) : { ok: false }) as T;
        }
        case 'cmd.cards.buy': {
          const id = (params as { id?: string } | undefined)?.id ?? '';
          const ok = buyShopCard(s, shop, id);
          if (ok) signedCards.push(id);
          return { ok } as T;
        }
        case 'cmd.cards.reroll':
          return { ok: rerollShop(s, shop) } as T;
        case 'cmd.cards.shop_close':
          shop = null;
          return { ok: true } as T;
        case 'cmd.cards.seen':
          return { ok: true } as T;
        case 'cmd.inbox.markRead':
          markRead((params as { id?: string } | undefined)?.id);
          return { ok: true } as T;
        case 'cmd.inbox.markAllRead':
          for (const m of s.inbox) m.read = true;
          return { ok: true } as T;
        case 'cmd.debug.simRace':
        case 'cmd.debug.lowRam':
        case 'cmd.debug.contentLog':
          return { ok: true } as T;
        default:
          try {
            return await mock.cmd<T>(method, params);
          } catch {
            return { ok: false, demo: true } as T;   // not simulated in the showcase
          }
      }
    },
    on() {
      // The shell polls query.career_state / query.game_state; nothing is pushed.
      return () => {};
    },
  };
}
