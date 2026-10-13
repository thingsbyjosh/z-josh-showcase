// Client for GET /v1/events: teasers always; the payload only if the Bearer token's tier
// reaches tier_min — gating happens SERVER-SIDE (a premium payload never reaches a machine without
// that tier; nothing is gated here). Fault-tolerant: any error -> null, and the caller keeps the
// previous file ("nothing online can break the game" rule).
import { BACKEND_URL } from '../account/backend';
import type { Locale } from '../i18n';

export interface SpecialEventTeaser {
  title_en: string;
  title_es: string;
  desc_en: string;
  desc_es: string;
  body_en?: string;                    // long-form narrative (EventPrerace)
  body_es?: string;
  image?: string;                      // base64 data: URI | remote url (deferred) | absent
  translations?: Partial<Record<Locale, { title?: string; desc?: string; body?: string }>>;
}

export interface SpecialEventPayload {
  classes: string[];                   // series_ids (multiclass); [] invalid
  track: string;                       // slug
  base_profile?: string;
  overrides?: Record<string, string>;  // rules (weather/time/pit/...), same as the engine
  laps?: number;                       // laps XOR duration_min
  duration_min?: number;
  rewards?: { credits?: number; rep?: number };
  discount_pct?: number;
}

export interface SpecialEvent {
  id: string;
  starts_at: string;                   // ISO UTC — the window is real time (two clocks)
  ends_at: string;
  tier_min: string;                    // 'free' | 'supporter' | 'plus' (a loose string: /v1 is additive)
  teaser: SpecialEventTeaser;
  payload?: SpecialEventPayload;       // absent = insufficient tier -> the app renders the paywall
  // rewards ALWAYS announced by the server (even with the tier-trimmed payload) — a locked
  // teaser can still advertise its prize pool. Absent on older files -> the UI falls back to the payload.
  rewards?: { credits?: number; rep?: number };
}

function validEvent(e: unknown): e is SpecialEvent {
  if (!e || typeof e !== 'object') return false;
  const v = e as Record<string, unknown>;
  return typeof v.id === 'string' && typeof v.starts_at === 'string'
    && typeof v.ends_at === 'string' && typeof v.tier_min === 'string'
    && typeof v.teaser === 'object' && v.teaser !== null;
}

// The response also carries the SERVER's time (`now`), which is what decides an event's window: it's propagated
// to the caller to store alongside the local fetch time (see eventClock). Absent/non-ISO -> undefined and the
// clock stays as it was (skew 0 = local clock, previous behavior).
export interface FetchedEvents {
  events: SpecialEvent[];
  serverNow?: string;
}

export async function fetchEvents(token: string | null): Promise<FetchedEvents | null> {
  try {
    const res = await fetch(`${BACKEND_URL}/v1/events`, {
      headers: token ? { authorization: `Bearer ${token}` } : {},
    });
    if (!res.ok) return null;
    const j = (await res.json()) as { events?: unknown; now?: unknown };
    if (!Array.isArray(j.events)) return null;
    const serverNow = typeof j.now === 'string' && !Number.isNaN(Date.parse(j.now)) ? j.now : undefined;
    return { events: j.events.filter(validEvent), serverNow };
  } catch {
    return null;
  }
}
