import type {
  ApiClientLike, CareerStatePayload, InboxPayload, StandingsPayload,
  ProfilePayload, CalendarPayload, MemoriesPayload, ResultsPayload, TimesPayload, TreePayload, SettingsPayload,
  GaragePayload, CardsPayload, MarketPayload, HubPayload, IdentityPayload, NegotiatePayload, SeasonPayload, PreracePayload,
  TryoutPayload, RolloverPayload, RaceConfigPayload, SkinsStatus, InstalledSkins, TimeAdvancePayload,
} from './contracts';
// REAL fixtures dumped by the engine (career_test --dump-api-fixtures), grounded
// in the overlay. Regenerate after contract changes; the typecheck validates shape against contracts.ts.
import careerStateFixture from '../../ui-fixtures/career_state.json';
import standingsFixture from '../../ui-fixtures/standings.json';
import inboxFixture from '../../ui-fixtures/inbox.json';
import profileFixture from '../../ui-fixtures/profile.json';
// the profile has TWO payloads per the contract — the panel's (trimmed) version and the "view all" modal's
// (full:true, which adds worldRanking.all). These are two separate dumps from the engine, not one with edits.
import profileFullFixture from '../../ui-fixtures/profile.full.json';
import calendarFixture from '../../ui-fixtures/calendar.json';
import memoriesFixture from '../../ui-fixtures/memories.json';
import resultsFixture from '../../ui-fixtures/results.json';
import timesFixture from '../../ui-fixtures/times.json';
import treeFixture from '../../ui-fixtures/tree.json';
import settingsFixture from '../../ui-fixtures/settings.json';
import garageFixture from '../../ui-fixtures/garage.json';
import cardsFixture from '../../ui-fixtures/cards.json';
import marketFixture from '../../ui-fixtures/market.json';
import hubFixture from '../../ui-fixtures/hub.json';
import identityFixture from '../../ui-fixtures/identity.json';
import negotiateFixture from '../../ui-fixtures/negotiate.json';
import seasonFixture from '../../ui-fixtures/season.json';
import preraceFixture from '../../ui-fixtures/prerace.json';
import tryoutFixture from '../../ui-fixtures/tryout.json';
import rolloverFixture from '../../ui-fixtures/rollover.json';
import raceConfigFixture from '../../ui-fixtures/race_config.json';

const careerState: CareerStatePayload = careerStateFixture;
const standings: StandingsPayload = standingsFixture;
// inbox: params' shape varies per message (different keys per email) -> TS infers a structural
// union with optional `undefined` props that doesn't fit Record<string,string>; it's cast
// via unknown, same as the contracts below (the real shape is validated live by the engine).
const inbox = inboxFixture as unknown as InboxPayload;
// These 4 contracts use string-literal unions (discipline, state, tone); the JSON import
// widens those fields to string, so it's cast via unknown (the shapes were already validated during
// the original review; in production the engine generates them).
const profile = profileFixture as unknown as ProfilePayload;
const profileFull = profileFullFixture as unknown as ProfilePayload;
const calendar = calendarFixture as unknown as CalendarPayload;
const memories = memoriesFixture as unknown as MemoriesPayload;
const results = resultsFixture as unknown as ResultsPayload;
const times = timesFixture as unknown as TimesPayload;
const tree = treeFixture as unknown as TreePayload;
const settings = settingsFixture as unknown as SettingsPayload;
const garage = garageFixture as unknown as GaragePayload;
const cards = cardsFixture as unknown as CardsPayload;
const market = marketFixture as unknown as MarketPayload;
const hub = hubFixture as unknown as HubPayload;
const identity = identityFixture as unknown as IdentityPayload;
const negotiate = negotiateFixture as unknown as NegotiatePayload;
const season = seasonFixture as unknown as SeasonPayload;
const prerace = preraceFixture as unknown as PreracePayload;
const tryout = tryoutFixture as unknown as TryoutPayload;
const rollover = rolloverFixture as unknown as RolloverPayload;
const raceConfig: RaceConfigPayload = raceConfigFixture;

// cmd.skins.status: fixture for the recommended-skins catalog. Copies the real shape of
// docs/skins/recommended_skins.json (packs with author/series/season_year + sample_vehicle/livery_id), plus the
// live detection (detected/detected_entries) the engine computes by scanning installed overrides.
// 6 resources; 2 detected (one pack, one standalone livery) to exercise the "INSTALLED" badge in dev and tests.
const skinsStatus: SkinsStatus = {
  ok: true,
  installed_overrides: 8,
  packs: [
    { slug: '2023-nimbus_masters_5890-nimbus_series_12532', title: '2023 NIMBUS_MASTERS_5890 Nimbus_Series_12532 Championship', url: 'https://www.lantern_series_8801/downloads/2023-nimbus_masters_5890-nimbus_series_12532.60001/',
      author: 'José M.', series: ['Onyx_Cup_4824', 'Pemberton_Zel121'], season_year: '2023',
      entries_total: 24, detected: true, detected_entries: 6, sample_vehicle: 'brackenridge_m4_gt3', sample_livery_id: 51 },
    { slug: 'lantern_trophy_5644-2026-gt3', title: 'Nimbus Trophy 4168 2026', url: 'https://www.lantern_series_8801/downloads/lantern_trophy_5644-2026-gt3.60002/',
      author: 'SimSkins', series: ['Onyx_Cup_4824'], season_year: '2026',
      entries_total: 12, detected: true, detected_entries: 2, sample_vehicle: 'copperline_quenby_vex495_vantage_gt3_evo', sample_livery_id: 51 },
    { slug: 'f1-1991-season', title: 'Beacon Trophy 2364 1991 Season Pack', url: 'https://www.lantern_series_8801/downloads/f1-1991-season.60003/',
      author: 'RetroLiveries', series: ['Formula_Retro_Gen3'], season_year: '1991',
      entries_total: 26, detected: false, detected_entries: 0, sample_vehicle: 'formula_retro_v12', sample_livery_id: 0 },
    { slug: 'stockcar-2024', title: 'Kingsmere Jun720', url: 'https://www.lantern_series_8801/downloads/stockcar-2024.60004/',
      author: 'BrasilSkins', series: ['StockV8_2024'], season_year: '2024',
      entries_total: 20, detected: false, detected_entries: 0, sample_vehicle: 'oldacre_cruze_stockcar_2024', sample_livery_id: 0 },
    { slug: 'pinecombe_motors-cup-heritage', title: 'Pinecombe Motors Cup #12 Heritage (single livery)', url: 'https://www.lantern_series_8801/downloads/pinecombe_motors-cup-heritage.60005/',
      author: 'PembertonTor793Fan', series: ['Pinecombe_Motors_Cup_992'], season_year: 'varios',
      entries_total: 1, detected: true, detected_entries: 1, sample_vehicle: 'pinecombe_motors_992_cup', sample_livery_id: 12 },
    { slug: 'gt1-2005-fiagt', title: 'ONYX CUP 1995 2005 Championship', url: 'https://www.lantern_series_8801/downloads/gt1-2005-fiagt.60006/',
      author: 'ClassicGT', series: ['CINDER_SERIES_4291'], season_year: '2005',
      entries_total: 18, detected: false, detected_entries: 0, sample_vehicle: 'ashbrook_motors_s7r', sample_livery_id: 0 },
  ],
};

// query.time.preview (C1, TimeAdvancePopup): to view it in `npm run fake` without an engine. A single day (today=doy1,
// stop=doy13) with three events that cross: a mail that passes, an offer that expires (does not stop either) and the
// player's own race that stops (gold). No no-shows and with the default rules (offers + my races active, the rest
// off). Review 0.7.0: no “new offer that passes” in January -- with the offers rule on it would stop,
// and with the market closed it does not even get there.
const timePreview: TimeAdvancePayload = {
  ok: true, resolved: true, executed: false,
  today: { absDay: 100, year: 2026, doy: 1, date: 'Jan 1', weekday: 'Thu' },
  stop: { absDay: 112, year: 2026, doy: 13, date: 'Jan 13', weekday: 'Tue', reason: 'race_mine' },
  days: 12, crossedYear: false,
  crossed: [
    { absDay: 104, year: 2026, doy: 5, date: 'Jan 5', weekday: 'Mon', kind: 'mail', label: 'time.ev.mail', params: { n: '2' }, seriesId: '', stops: false },
    { absDay: 107, year: 2026, doy: 8, date: 'Jan 8', weekday: 'Thu', kind: 'offer_expired', label: 'time.ev.offer_expired', params: { n: '1' }, seriesId: '', stops: false },
    { absDay: 112, year: 2026, doy: 13, date: 'Jan 13', weekday: 'Tue', kind: 'race_mine', label: 'time.ev.race_mine', params: { series: 'Valemont Ax229', track: 'Fairwater', round: '1' }, seriesId: 'harbor_trophy_6546', stops: true },
  ],
  noShows: [],
  rules: { offers: true, myRaces: true, anyRace: false, mail: false, relation: false },
};

// An entry can be either the direct payload or a params RESOLVER: the engine returns different payloads depending on
// what's requested (today only the profile, via full:true), and a mock that ignores params can't cover that.
type FixtureEntry = unknown | ((params?: object) => unknown);

const FIXTURES: Record<string, FixtureEntry> = {
  'query.career_state': careerState,
  'query.standings': standings,
  'query.inbox': inbox,
  // without full, only the trimmed version travels; with full:true, the complete table. Same as the engine.
  'query.profile': (params?: object) => ((params as { full?: boolean } | undefined)?.full ? profileFull : profile),
  'query.calendar': calendar,
  'query.memories': memories,
  'query.results': results,
  'query.times': times,
  'query.tree': tree,
  'query.settings': settings,
  'query.garage': garage,
  'query.cards': cards,
  'query.market': market,
  'query.hub': hub,
  'query.identity': identity,
  'query.negotiate': negotiate,
  'query.season': season,
  'query.prerace': prerace,
  'query.tryout': tryout,
  'query.rollover': rollover,
  'query.race_config': raceConfig,   // debug: config of the last launch (used by SIMULATION mode's coverage)
  'query.time.preview': timePreview,   // C1: TimeAdvancePopup in `npm run fake` with no engine behind it
  'cmd.skins.status': skinsStatus,   // skins are read via query() (the engine dispatches by name)
  // installed skin slots (consistent with the detected:true packs in skinsStatus above).
  'cmd.skins.installed': {
    ok: true,
    slots: [
      { slug: 'brackenridge_m4_gt3', livery_id: 51, name: 'Paul Miller Racing #1', preview_rel: 'NIMBUS_MASTERS_5890\\preview_1.dds' },
      { slug: 'copperline_quenby_vex495_vantage_gt3_evo', livery_id: 51, name: 'AMR #27', preview_rel: 'LANTERN_TROPHY_5644\\preview_27.dds' },
      { slug: 'pinecombe_motors_992_cup', livery_id: 12, name: 'Pinecombe Motors Cup #12', preview_rel: 'preview_12.dds' },
    ],
  } as InstalledSkins,
  'query.api_info': { api: 1, build: 'mock', game_attached: false },
  'query.game_state': { gameState: 1, sessionState: 0, atMenu: true, shmMapped: true,
                        launchPhase: 0, cfgFailN: 0, cfgFailGroups: '' },   // browser: reports "at menu" so polling doesn't hang
};

/** Client for browser dev and screen tests: serves fixtures, no socket. */
export function createMockApiClient(overrides: Record<string, unknown> = {}): ApiClientLike {
  const data: Record<string, FixtureEntry> = { ...FIXTURES, ...overrides };
  return {
    status: 'connected',
    async query<T>(method: string, params?: object): Promise<T> {
      if (!(method in data)) throw new Error(`bad_method: ${method}`);
      const e = data[method];
      // a test override can still be a plain value: it's only invoked if it's a resolver
      return (typeof e === 'function' ? (e as (p?: object) => unknown)(params) : e) as T;
    },
    // Write commands are still NOT generally supported (the mock serves fixtures, it doesn't simulate the engine), but
    // the difficulty locks ARE: without them the "see more" panel is inert in the local dev server — you click and
    // nothing happens, because the command fired but the screen just re-rendered the same static fixture. The in-memory
    // copy of the fixture gets mutated instead, and that's what the next query returns, so the design can actually be reviewed.
    async cmd<T>(method: string, params?: object): Promise<T> {
      const settings = data['query.settings'] as { difficulty?: { lockAll?: boolean; classes?: { classId: string; locked?: boolean; seeded?: boolean }[] } } | undefined;
      const diff = settings?.difficulty;
      if (method === 'cmd.settings.setClassLock' && diff?.classes) {
        const p = (params ?? {}) as { classId?: string; locked?: boolean };
        const row = diff.classes.find((c) => c.classId === p.classId);
        // same rule as the engine: a SEEDED category can't be locked on its own (its number is
        // recalculated from the overall value on every query, so the lock wouldn't freeze anything).
        if (!row || row.seeded) return { ok: false } as T;
        row.locked = !!p.locked;
        return { ok: true } as T;
      }
      if (method === 'cmd.settings.setLockAll' && diff) {
        diff.lockAll = !!((params ?? {}) as { locked?: boolean }).locked;
        return { ok: true } as T;
      }
      // 0.7.0: race pace. Same vocabulary and same multipliers as the engine (bal::REWARD_MULT).
      // The object is replaced (not mutated): the imported fixture is shared between tests.
      if (method === 'cmd.settings.setReward' && settings && 'reward' in settings) {
        const tokens = ['easy', 'normal', 'hard'];
        const mode = tokens.indexOf(String(((params ?? {}) as { mode?: string }).mode));
        if (mode < 0) throw new Error('bad_params: unknown reward mode');
        const mult = [1.25, 1, 0.75][mode];
        data['query.settings'] = { ...settings, reward: { mode, mult } } as FixtureEntry;
        return { ok: true, mode: tokens[mode], mult } as T;
      }
      throw new Error(`mock: cmd no soportado (${method})`);
    },
    on() {
      return () => {};
    },
  };
}
