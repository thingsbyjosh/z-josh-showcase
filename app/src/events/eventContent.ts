// What the player is missing to be able to run an event (the engine's query.events.content).
//
// An event isn't a calendar round: it doesn't substitute its circuit or relax its class, because an event IS its
// circuit and its car. Before this, a player without the DLC saw the card like any other, entered, and arming it
// sent the game a track hash it doesn't have, or a grid with no cars. Now the card says so upfront.
//
// Fail-open across three layers: no live catalogs scanned, no event in the payload, or no engine connected —
// none of these assert that anything's missing, and the card behaves as before.
import { useQuery } from '../api/useQuery';
import type { ApiClientLike, EventsContentPayload, EventContentRow } from '../api/contracts';

export interface EventBlock {
  blocked: boolean;
  product: string;   // display name of the product to buy ("" if the engine doesn't know which one)
}

const OPEN: EventBlock = { blocked: false, product: '' };

// Locks if the circuit is missing, or if NONE of the event's classes have any cars installed. If only
// some are missing, the player can still race with another one: that doesn't lock the card (and the engine leaves the
// missing class out of the grid).
export function eventBlock(row: EventContentRow | undefined): EventBlock {
  if (!row) return OPEN;
  if (!row.trackOk) return { blocked: true, product: row.trackProduct };
  const cls = row.classes ?? [];
  if (cls.length > 0 && cls.every((c) => !c.ok)) {
    const named = cls.find((c) => c.product);
    return { blocked: true, product: named ? named.product : '' };
  }
  return OPEN;
}

// 23/09 (Tester Hub): the track name of an event. The event payload brings the KEY (`ashhurst_park`,
// `coldgate_park`) and the cards showed it as is. The name is resolved by the engine (query.events.content,
// trackName); without an engine, the key is presented readable — never with underscores.
export function eventTrackLabel(row: EventContentRow | undefined, slug: string | undefined): string {
  if (row && row.trackName) return row.trackName;
  const s = (slug ?? '').trim();
  if (!s) return '—';
  return s.split(/[_\s]+/).filter(Boolean).map((w) => w.charAt(0).toUpperCase() + w.slice(1)).join(' ');
}

export interface EventsContentLookup {
  // the event row ONLY if the live catalogs are scanned (fail-open of the DLC block)
  rowFor: (eventId: string) => EventContentRow | undefined;
  // its track name: the engine's even if there is no scan (resolving it does not depend on what is installed)
  trackNameFor: (eventId: string, slug: string | undefined) => string;
}

// A single query for the whole screen: the lookup by event id and its track name.
export function useEventsContent(client: ApiClientLike): EventsContentLookup {
  const { data } = useQuery<EventsContentPayload>(client, 'query.events.content');
  // `events` may be missing in a half-built payload (mocks, old engine): nothing to look up, not a crash
  const any = (eventId: string) => data?.events?.find((e) => e.id === eventId);
  return {
    rowFor: (eventId: string) => (data && data.scanned ? any(eventId) : undefined),
    trackNameFor: (eventId: string, slug: string | undefined) => eventTrackLabel(any(eventId), slug),
  };
}
