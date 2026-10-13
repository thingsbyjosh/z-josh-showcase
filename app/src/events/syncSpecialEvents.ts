// Event sync orchestration: on EVERY app startup it downloads /v1/events (with the
// account's token if there is one) and overwrites the AUTHORITATIVE data root (config.paths.data, the one the engine reads).
// NO throttling: it checks on every startup (the payload is tiny; a freshly published event shows up on
// the next app open). Offline/HTTP error -> fetchEvents returns null and the previous file is KEPT (never
// overwritten) — "nothing online can break the game." Note the settled decision: the ENGINE loads this data ONCE per AMS2
// process (SeasonUiEnsureData) -> whatever gets downloaded here is only PLAYED on the game's NEXT startup, no hot reload.
import { loadConfig } from '../config/appConfig';
import { loadAccount } from '../account/accountStore';
import { fetchEvents } from './eventsClient';
import { saveSpecialEvents } from './eventsStore';

export type SyncResultKind = 'saved' | 'kept-offline' | 'no-config';

export async function syncSpecialEvents(nowMs: number = Date.now()): Promise<SyncResultKind> {
  const cfg = await loadConfig();
  const dataRoot = cfg?.paths.data;
  if (!dataRoot) return 'no-config';                 // wizard not completed yet: nothing to sync
  const acc = await loadAccount();
  const fetched = await fetchEvents(acc?.token ?? null);
  if (fetched === null) return 'kept-offline';       // network/HTTP failure: the previous file still stands (untouched)
  // the SERVER's time is stored alongside the local fetch time: their difference is this machine's clock skew, and with it
  // the backend decides event windows, not a badly set PC clock (see eventClock).
  await saveSpecialEvents(dataRoot, fetched.events, new Date(nowMs).toISOString(), fetched.serverNow);
  return 'saved';
}
