import type { InboxMessage, ResultsPayload } from '../api/contracts';
import careerStateFixture from '../../ui-fixtures/career_state.json';
import inboxFixture from '../../ui-fixtures/inbox.json';
import garageFixture from '../../ui-fixtures/garage.json';
import { mulberry32 } from './rng';

export interface DemoDriver { id: string; name: string; team: string; teamColor: string; rating: number; isPlayer: boolean }
export interface DemoRound { round: number; track: string; trackSlug: string; doy: number; laps: number; prize: number;
  done: boolean; result?: { pos: number; finished: boolean; points: number };
  order?: string[];                  // driver ids by finishing position (finishers first, then DNFs)
  dnf?: string[];                    // driver ids that retired
}
export interface DemoOffer { id: string; team: string; teamColor: string; car: string; salary: number; bonus: number;
  state: 'open' | 'countered' | 'signed' | 'rejected'; counterUsed: boolean;
  listed?: number;                   // salary as first listed (counters scale from it)
  agreed?: boolean;                  // terms accepted by the team, ready to sign
  tablePct?: number;                 // salary pct on the table (player proposal or team counter)
  history?: { day: number; who: number; kind: number; demandsMask: number; salaryPct: number }[];
}
export interface DemoState {
  seed: number; rng: () => number;
  today: number;                     // day of year, 2026
  salary: number;                    // current per-race salary
  money: number; rep: number; elo: number; xp: number;
  wins: number; podiums: number; races: number;
  series: { id: string; name: string; className: string; classColor: string };
  team: { name: string; color: string; car: string };
  drivers: DemoDriver[];             // includes the player (isPlayer)
  points: Record<string, number>;    // driverId -> season points
  rounds: DemoRound[];
  recent: { pos: number; field: number; track: string; finished: boolean }[];   // newest first, max 5
  inbox: InboxMessage[];
  offers: DemoOffer[];
  resultSeq: number; settlePending: boolean;
  lastResult: ResultsPayload | null;
  rewardMult: number;
  sponsors: string[];                // signed sponsor card ids (starting ones plus shop buys)
  mailSeq: number;                   // unique suffix for generated reply mails
}

/** Points table (defined here so demoEngine can re-export it without an import cycle). */
export const POINTS = [25, 18, 15, 12, 10, 8, 6, 4, 2, 1];

const TEAMS: { name: string; color: string }[] = [
  { name: 'Team Zenith', color: '#3fb6ff' },
  { name: 'Team Apex', color: '#ff8a3d' },
  { name: 'Falcon Racing', color: '#e5484d' },
  { name: 'Halcyon Racing', color: '#46c28a' },
  { name: 'Northgate Motorsport', color: '#a78bfa' },
  { name: 'Kestrel Works', color: '#f5c542' },
];

// 15 rivals; six of them are clearly ahead of the player (62), so P5-P8 on average.
const RIVALS: { name: string; team: number; rating: number }[] = [
  { name: 'L. Andersen', team: 1, rating: 80 },
  { name: 'M. Valdoro', team: 2, rating: 77 },
  { name: 'K. Tanaka', team: 3, rating: 74 },
  { name: 'P. Silva', team: 4, rating: 71 },
  { name: 'D. Okafor', team: 5, rating: 68 },
  { name: 'T. Lindqvist', team: 1, rating: 64 },
  { name: 'R. Moreau', team: 2, rating: 60 },
  { name: 'S. Novak', team: 3, rating: 58 },
  { name: 'J. Duarte', team: 4, rating: 56 },
  { name: 'E. Haddad', team: 5, rating: 54 },
  { name: 'C. Brandt', team: 0, rating: 52 },
  { name: 'A. Petrov', team: 1, rating: 50 },
  { name: 'N. Olabide', team: 2, rating: 48 },
  { name: 'F. Kowal', team: 3, rating: 46 },
  { name: 'H. Sato', team: 4, rating: 45 },
];

// Circuit names already present in the emitted fixtures (calendar/memories); slugs are lowercase kebab.
const TRACKS = ['Fairwater', 'Bellhaven', 'Cindermoor', 'Drummond Park', 'Ellerby', 'Glasshouse', 'Holloway', 'Ilminster'];
export const slugify = (n: string): string => n.toLowerCase().replace(/\s+/g, '-');

export function buildWorld(seed: number): DemoState {
  const rng = mulberry32(seed);
  const carName = (garageFixture as { cars: { name: string }[] }).cars[0]?.name ?? 'Halcyon GT3 R';
  const drivers: DemoDriver[] = [
    { id: 'player', name: 'Alex Bravo', team: TEAMS[0].name, teamColor: TEAMS[0].color, rating: 62, isPlayer: true },
    ...RIVALS.map((r, i) => ({
      id: 'd' + (i + 1), name: r.name, team: TEAMS[r.team].name, teamColor: TEAMS[r.team].color,
      rating: r.rating, isPlayer: false,
    })),
  ];
  const rounds: DemoRound[] = TRACKS.map((track, i) => ({
    round: i + 1, track, trackSlug: slugify(track), doy: 40 + i * 14, laps: 12 + (i % 3) * 2,
    prize: 20000 + i * 2000, done: false,
  }));

  const points: Record<string, number> = {};
  drivers.forEach((d) => { points[d.id] = 0; });
  // Round 1 is history: order by noisy score, then slot the player into P6.
  const order = drivers
    .map((d) => ({ d, s: d.rating + (rng() - 0.5) * 30 }))
    .sort((a, b) => b.s - a.s)
    .map((x) => x.d);
  const pi = order.findIndex((d) => d.isPlayer);
  const [pl] = order.splice(pi, 1);
  order.splice(5, 0, pl);
  order.forEach((d, i) => { points[d.id] += POINTS[i] ?? 0; });
  rounds[0].done = true;
  rounds[0].result = { pos: 6, finished: true, points: POINTS[5] };
  rounds[0].order = order.map((d) => d.id);
  rounds[0].dnf = [];

  const money = (careerStateFixture as { profile: { money: number } }).profile.money;
  const messages = (inboxFixture as unknown as { messages: InboxMessage[] }).messages;
  return {
    seed, rng,
    today: rounds[1].doy - 2,
    salary: 3000,
    money, rep: 3120, elo: 1235, xp: 5400,
    wins: 0, podiums: 0, races: 1,
    series: { id: 'demo_gt', name: 'GT Masters Demo', className: 'GT3', classColor: '#3fb6ff' },
    team: { name: TEAMS[0].name, color: TEAMS[0].color, car: carName },
    drivers, points, rounds,
    recent: [{ pos: 6, field: drivers.length, track: rounds[0].track, finished: true }],
    // The fixture's offer has no backing offer in this world and its sponsor
    // mail points at a key with no text: offers come from the demo market and
    // the sponsor mail is rewritten against the sponsor the player starts with.
    inbox: messages
      .filter((m) => m.kind !== 'offer')
      .map((m) => (m.family === 'sponsor'
        ? { ...m, subject: 'demo_mail_sponsor_subj', body: 'demo_mail_sponsor_body', params: { sponsor: 'Harbour Haulage Co.', player: 'Alex' } }
        : { ...m })),
    offers: [],
    resultSeq: 0, settlePending: false, lastResult: null, rewardMult: 1,
    sponsors: ['demo_harbor', 'demo_lumen'], mailSeq: 0,
  };
}
