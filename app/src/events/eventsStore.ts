// Persistence for special events: <config.paths.data>\special_events.json — the AUTHORITATIVE
// data root that the ENGINE reads (dll/career/paths.cpp reads config.json -> paths.data). Don't use
// managedDataRoot here: a dev config (data=docs) must get the file where the engine actually looks for it.
// File shape (settled decision): { schemaVersion: 1, fetchedAt: "<ISO>", events: [/v1/events as-is] },
// plus serverNow: the time the backend returned on THAT fetch. The pair (serverNow, fetchedAt) is this
// machine's clock skew, and with it the event window is decided by the server both in the app (eventClock) and in the ENGINE
// (career_events.cpp reads the same two fields). Optional field: an older file without it = skew 0.
import { getBridge, joinPath } from '../shell/bridge';
import type { SpecialEvent } from './eventsClient';
import { setEventClockFromFetch } from './eventClock';

export interface SpecialEventsFile {
  schemaVersion: 1;
  fetchedAt: string;                   // local ISO of the last successful fetch
  serverNow?: string;                  // backend's ISO at that same instant (absent in older files)
  events: SpecialEvent[];
}

export function specialEventsPath(dataRoot: string): string {
  return joinPath(dataRoot, 'special_events.json');
}

export async function loadSpecialEvents(dataRoot: string): Promise<SpecialEventsFile | null> {
  const b = await getBridge();
  const path = specialEventsPath(dataRoot);
  if (!(await b.fs.exists(path))) return null;
  try {
    const f = JSON.parse(await b.fs.readTextFile(path)) as SpecialEventsFile;
    if (!f || f.schemaVersion !== 1 || !Array.isArray(f.events)) return null;
    setEventClockFromFetch(f.serverNow, f.fetchedAt);   // the cached file carries the skew from the last sync
    return f;
  } catch {
    return null;
  }
}

export async function saveSpecialEvents(dataRoot: string, events: SpecialEvent[], fetchedAtIso: string,
                                        serverNowIso?: string): Promise<void> {
  const b = await getBridge();
  await b.fs.makeDir(dataRoot);
  const f: SpecialEventsFile = { schemaVersion: 1, fetchedAt: fetchedAtIso, serverNow: serverNowIso, events };
  await b.fs.writeTextFileAtomic(specialEventsPath(dataRoot), JSON.stringify(f, null, 2));
  setEventClockFromFetch(serverNowIso, fetchedAtIso);   // fresh sync: the skew is already correct, no need to re-read the file
}
