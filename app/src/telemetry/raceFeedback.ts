// This showcase does not include telemetry sending (see the licence and README): the real
// raceFeedback.ts composes the post-race feedback payload and sends it to this project's own
// telemetry backend, whose schema is not part of this repository. This stub keeps the same
// exported shape; sending always reports "declined" and nothing ever leaves the machine.
export const FEEL_REASONS = [
  'pack', 'spread', 'robotic', 'strategy', 'racecraft', 'starts', 'wet', 'mycar',
] as const;
export type FeelReason = (typeof FEEL_REASONS)[number];

export const FEEL_LEVELS = [1, 2, 3, 4, 5] as const;

export interface TelemetryRacePayload {
  schema: number;
  optIn: boolean;
  classId: string;
  raceId?: string;
  year?: number;
  doy?: number;
  config?: { sim?: boolean } | null;
  [k: string]: unknown;
}

export function setModVersion(_v: string): void {
}

export const raceKey = (p: TelemetryRacePayload): string =>
  `${p.raceId ?? ''}|${p.classId}|${p.year ?? 0}|${p.doy ?? 0}`;

export async function fetchRaceTelemetry(_client: unknown): Promise<TelemetryRacePayload | null> {
  return null;
}

export async function postRaceTelemetry(
  _payload: TelemetryRacePayload, _sender: string,
): Promise<boolean> {
  return false;
}

export async function sendRaceTelemetry(
  _client: unknown,
  _sender: () => Promise<string>,
  _nowIso: string = new Date().toISOString(),
): Promise<'sent' | 'queued' | 'declined' | 'simulated' | 'duplicate' | 'nothing'> {
  return 'nothing';
}

export function __resetSentRaces(): void {
}
