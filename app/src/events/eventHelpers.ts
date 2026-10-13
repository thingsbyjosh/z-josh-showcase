// Pure helpers over SpecialEvent (the new schema, dated 15 Jul). No network.
// The default time for judging windows is eventNow() (local clock corrected by the server skew,
// see eventClock), not Date.now(): the backend publishes the window and it must hold the same on every machine.
import type { SpecialEvent } from './eventsClient';
import { trackMapPath } from '../ui/trackImages';
import { eventNow } from './eventClock';
import { normalizeLocale } from '../i18n';

// Same literal as catalog/events.ts EVENT_PREVIEW_SEC (app has no catalog import path today).
const EVENT_PREVIEW_SEC = 3 * 86400;

// true if the event carries a playable payload (sufficient tier + at least 1 class).
// Defensive Array.isArray: an event with an old/malformed schema (payload with no classes) must NOT crash the hub.
export function isPlayable(ev: SpecialEvent): boolean {
  return !!ev.payload && Array.isArray(ev.payload.classes) && ev.payload.classes.length > 0;
}

// true if the event is missing its payload BECAUSE the server trimmed it by tier (a supporter-only event and
// this machine isn't one). Different from plain "not playable": a free event with no payload is a MALFORMED teaser,
// and you can't offer "become a supporter" for that one (it wouldn't fix anything). The UI uses this to route a
// click on a locked event to the support page.
export function isTierLocked(ev: SpecialEvent): boolean {
  return !ev.payload && ev.tier_min !== 'free';
}

// event prize pool (credits): playable payload > teaser announcement (the server also serves
// it with the tier-trimmed payload, so the locked card doesn't advertise an empty prize).
export function eventCredits(ev: SpecialEvent): number {
  return ev.payload?.rewards?.credits ?? ev.rewards?.credits ?? 0;
}

function localizedEventText(ev: SpecialEvent, field: 'title' | 'desc' | 'body', lang: number | string): string {
  const locale = typeof lang === 'number' ? (lang === 1 ? 'es' : 'en') : normalizeLocale(lang);
  const text = (code: string): string => {
    const value = code === 'en' || code === 'es'
      ? ev.teaser[`${field}_${code}`]
      : ev.teaser.translations?.[code as keyof NonNullable<typeof ev.teaser.translations>]?.[field];
    return typeof value === 'string' && value.trim() ? value : '';
  };
  return text(locale) || (locale === 'pt-BR' ? text('pt') : '') || text('en') || text('es');
}

export function eventTitle(ev: SpecialEvent, lang: number | string): string {
  return localizedEventText(ev, 'title', lang);
}

export function eventDesc(ev: SpecialEvent, lang: number | string): string {
  return localizedEventText(ev, 'desc', lang);
}

// long-form narrative (EventPrerace); "" if the event has no body.
export function eventBody(ev: SpecialEvent, lang: number | string): string {
  return localizedEventText(ev, 'body', lang);
}

export interface EventCondition {
  key: string;
  value: string;
}

// CANONICAL catalog of AMS2 weather values (mapped from the game's own catalog, dated 17 Jul). The engine only exposes the raw number
// in overrides.weather; the event generator can emit a CSV of up to 4 slots ("0,8,17") -> here we
// resolve the 1st slot to an i18n key under events.*. Unknown value/CSV = undefined (the UI falls back to the raw number).
//   0 clear, 1 partly cloudy, 2 medium cloud, 3 mostly cloudy, 4 overcast
//   5 drizzle, 6 rain, 7 storm, 8 thunderstorm
//   12 hazy, 13 hazy with rain, 14 dense fog, 15 dense fog with rain, 16 haze, 17 random
// (9/10/11 unmapped). WET conditions (relevant for the fragile-class crash warning): 5,6,7,8,13,15, and 17 (uncertain).
const WEATHER_I18N_KEY: Record<string, string> = {
  '0': 'events.wx_clear', '1': 'events.wx_partly', '2': 'events.wx_medium', '3': 'events.wx_mostly',
  '4': 'events.wx_overcast', '5': 'events.wx_drizzle', '6': 'events.wx_rain', '7': 'events.wx_storm',
  '8': 'events.wx_thunder', '12': 'events.wx_hazy', '13': 'events.wx_mistrain', '14': 'events.wx_densefog',
  '15': 'events.wx_densefograin', '16': 'events.wx_mist', '17': 'events.wx_random',
};
// wet weather (rain/storm/haze-with-rain/random). Single source of truth for the app's chips/tint.
const WET_WEATHER = new Set(['5', '6', '7', '8', '13', '15', '17']);
// 1st slot of a weather override (a single "8" or a CSV "0,8,17"); '' if empty/absent.
export function weatherFirstSlot(raw: string | undefined): string {
  return raw !== undefined ? (raw.split(',')[0]?.trim() ?? '') : '';
}
export function weatherI18nKey(raw: string | undefined): string | undefined {
  return weatherKeyForCode(weatherFirstSlot(raw));
}
// Same, but for a STANDALONE code (query.race_config emits all 4 states as numbers, not as a CSV);
// same table, to avoid duplicating the catalog. undefined = unmapped code (the UI falls back to the raw number).
export function weatherKeyForCode(code: number | string): string | undefined {
  return WEATHER_I18N_KEY[String(code)];
}
// true if ANY slot of the override is wet weather (for storm tint / warnings).
export function weatherIsWet(raw: string | undefined): boolean {
  return raw !== undefined && raw.split(',').some((v) => WET_WEATHER.has(v.trim()));
}

// readable pairs of track/weather/time/format/classes from the payload; [] if there's no payload.
export function eventConditions(ev: SpecialEvent): EventCondition[] {
  const p = ev.payload;
  if (!p) return [];
  const out: EventCondition[] = [];
  out.push({ key: 'track', value: p.track });
  if (p.overrides?.weather !== undefined) out.push({ key: 'weather', value: p.overrides.weather });
  if (p.overrides?.hour !== undefined) out.push({ key: 'hour', value: p.overrides.hour });
  if (p.laps !== undefined) out.push({ key: 'format', value: `${p.laps} laps` });
  else if (p.duration_min !== undefined) out.push({ key: 'format', value: `${p.duration_min} min` });
  out.push({ key: 'classes', value: `${Array.isArray(p.classes) ? p.classes.length : 0}` });
  return out;
}

// STABLE identity color per class (EventPrerace: hero chips, tabs, card corner). The engine only
// exposes the discColor of the ACTIVE class (p.discColor); to color EVERY class in a multiclass event — including ones
// not yet armed — you need a color per series_id. Hue is deterministic from the string: same id -> same color
// always, different per class. Purely cosmetic identity; never a substitute for real engine data.
export function eventClassColor(seriesId: string): string {
  let h = 0;
  for (let i = 0; i < seriesId.length; i++) h = (Math.imul(h, 31) + seriesId.charCodeAt(i)) >>> 0;
  return `hsl(${h % 360} 68% 58%)`;
}

// event cover image: direct base64 | circuit-map placeholder (a remote url is deferred -> placeholder).
export function eventImageSrc(ev: SpecialEvent, assetUrl: (p: string) => string, dataDir: string): string {
  const img = ev.teaser.image;
  if (img && img.startsWith('data:')) return img;                 // direct base64 (CSP allows data:)
  const slug = ev.payload?.track ?? '';                           // placeholder = circuit map
  return slug ? assetUrl(trackMapPath(dataDir, slug)) : '';       // a remote url is deferred -> placeholder
}

// the event's WINDOW state in real time (two clocks): before it starts -> 'upcoming';
// once ends_at is reached (inclusive) -> 'ended'; in between -> 'live'. now is injectable for tests.
export type EventStatus = 'upcoming' | 'live' | 'ended';
export function eventStatus(ev: SpecialEvent, now: number = eventNow()): EventStatus {
  if (now < Date.parse(ev.starts_at)) return 'upcoming';
  if (now >= Date.parse(ev.ends_at)) return 'ended';
  return 'live';
}

// Cache safety net for the public 72h preview window: false if ended or more than 72h before starts_at.
// Backend listPublishedEvents already cuts the same way; this hides stale far-future rows in special_events.json.
export function eventIsListed(ev: SpecialEvent, now: number = eventNow()): boolean {
  if (now >= Date.parse(ev.ends_at)) return false;
  if (Date.parse(ev.starts_at) > now + EVENT_PREVIEW_SEC * 1000) return false;
  return true;
}

// days left until it ENDS (rounded up; never negative). 0 = ends today / already ended.
export function eventDaysLeft(ev: SpecialEvent, now: number = eventNow()): number {
  return Math.max(0, Math.ceil((Date.parse(ev.ends_at) - now) / 86400000));
}

// days left until it STARTS (rounded up; never negative). 0 = starts today / already started.
export function eventDaysTo(ev: SpecialEvent, now: number = eventNow()): number {
  return Math.max(0, Math.ceil((Date.parse(ev.starts_at) - now) / 86400000));
}

// hours left until it STARTS (rounded up; never negative).
export function eventHoursTo(ev: SpecialEvent, now: number = eventNow()): number {
  return Math.max(0, Math.ceil((Date.parse(ev.starts_at) - now) / 3600000));
}

// countdown to the START in the HONEST unit: hours below 24h, days above it.
// eventDaysTo rounds up, so on its own it would say "starts in 1 day" for an event opening in two hours.
export function eventStartsIn(ev: SpecialEvent, now: number = eventNow()): { key: string; count: number } {
  const h = eventHoursTo(ev, now);
  return h <= 24 ? { key: 'events.cardStartsH', count: h } : { key: 'events.cardStarts', count: eventDaysTo(ev, now) };
}

// compact window label "MM-DD -> MM-DD" (the same `shortWin` used by EventPrerace: .slice(5,10) of the ISO). The
// UI language doesn't change the date's numeric format (two clocks = real date, not localized); _lang is kept in
// the signature for consistency with the rest of the i18n helpers, and for future localization of the separator.
export function eventWindowLabel(ev: SpecialEvent, _lang: number | string): string {
  return `${ev.starts_at.slice(5, 10)} → ${ev.ends_at.slice(5, 10)}`;
}
