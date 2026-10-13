// This showcase does not include telemetry persistence (see the licence and README): the real
// queue.ts keeps a disk-backed retry queue for telemetry the app could not send (no network,
// backend down, laptop asleep), which is not part of this repository. This stub keeps the same
// exported shape; every function is a safe no-op and the queue is always empty.
export const QUEUE_MAX_ENTRIES = 20;
export const QUEUE_MAX_TRIES = 10;
export const QUEUE_MAX_AGE_DAYS = 14;

export interface QueuedRace {
  key: string;
  queuedAt: string;
  tries: number;
  race: Record<string, unknown>;
}

export async function loadQueue(): Promise<QueuedRace[]> {
  return [];
}

export async function saveQueue(_q: QueuedRace[]): Promise<void> {
}

export async function clearQueue(): Promise<void> {
}

export async function enqueueRace(
  _key: string, _race: Record<string, unknown>, _nowIso: string,
): Promise<void> {
}

export interface FlushResult { sent: number; kept: number; dropped: number }

export async function flushTelemetryQueue(
  _post: (race: never, sender: string) => Promise<boolean>,  // any post function fits
  _sender: () => Promise<string>,
  _allowed: boolean,
  _nowIso: string,
): Promise<FlushResult> {
  return { sent: 0, kept: 0, dropped: 0 };
}
