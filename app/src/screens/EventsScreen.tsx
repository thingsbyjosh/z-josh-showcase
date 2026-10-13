// Special events grid in the public window (— "See all" from the hub). Live list +
// preview ≤72h (eventIsListed); it does not draw distant teasers or finished ones. Order: live -> upcoming, then by
// ascending starts_at. Entering a LIVE+playable event delegates to onEnterEvent -> same flow as the carousel.
import { useEffect, useMemo, useState } from 'react';
import { logImageError } from '../support/supportLog';
import { useTranslation } from 'react-i18next';
import type { SpecialEvent } from '../events/eventsClient';
import { loadSpecialEvents } from '../events/eventsStore';
import {
  isPlayable, isTierLocked, eventTitle, eventDesc, eventClassColor, eventCredits,
  eventStatus, eventDaysLeft, eventStartsIn, eventWindowLabel, eventIsListed, type EventStatus,
} from '../events/eventHelpers';
import { eventBlock, useEventsContent, type EventBlock } from '../events/eventContent';
import type { ApiClientLike } from '../api/contracts';
import { Display, Mono, Kicker, Chip, AccentDot, LockIcon } from '../ui/components';
import { classDisplayName } from '../ui/classNames';
import { formatCreditsCr } from '../ui/format';
import { useTrackMapUrl } from '../ui/trackImages';
import { PATREON_URL, openCommunityUrl } from '../ui/community';

// hero gradients (mirrors EventPrerace): night or storm -> deep blue; otherwise warm brown.
const GRAD_NIGHT = 'linear-gradient(158deg,#101a2e 0%,#0a1120 46%,#06080e 100%)';
const GRAD_DAY = 'linear-gradient(158deg,#23180f 0%,#120d0a 52%,#08080a 100%)';
// presentation order by status: ongoing first, then upcoming, and finished last.
const STATUS_ORDER: Record<EventStatus, number> = { live: 0, upcoming: 1, ended: 2 };

// Card for a single event in the grid. Defined at module level (not nested in the screen) so the map
// hook doesn't remount on every parent render. Only a LIVE+playable event is clickable (enters the arm flow); a live-but-locked
// event would show the paywall in the hub carousel — here, since it's a season teaser, it's left non-clickable with its own chip.
function EventCard({ ev, lang, now, trackMapUrl, onEnter, block, trackName }: {
  ev: SpecialEvent; lang: string; now: number; trackMapUrl: (slug: string | undefined) => string; onEnter: (ev: SpecialEvent) => void;
  block: EventBlock;   // DLC: event content this player doesn't have
  trackName: string;   // 23/09: the track name (the payload key was what was being shown)
}) {
  const { t } = useTranslation();
  const status = eventStatus(ev, now);
  const playable = isPlayable(ev);
  // DLC: without the track (or none of its classes), the arm can't succeed, so the card
  // doesn't let you in: it states what's missing. This is different from the supporter paywall, which does lead somewhere (Patreon).
  const clickable = status === 'live' && playable && !block.blocked;
  // a LIVE event you can't run because you're not a supporter: the card is no longer a dead end,
  // clicking it opens Patreon (same destination as the paywall in the hub carousel).
  const locked = status === 'live' && isTierLocked(ev);
  const actionable = (clickable || locked) && !block.blocked;
  const activate = () => { if (clickable) onEnter(ev); else void openCommunityUrl(PATREON_URL, 'Patreon (evento bloqueado)'); };

  const payload = ev.payload;
  const classes = payload?.classes ?? [];
  const multi = classes.length > 1;
  const weatherRaw = payload?.overrides?.weather;
  const hourRaw = payload?.overrides?.hour;
  const night = hourRaw !== undefined && parseInt(hourRaw, 10) >= 19;
  const storm = weatherRaw === '8';
  const heroColor = classes[0] ? eventClassColor(classes[0]) : 'var(--accent)';
  const mapUrl = trackMapUrl(payload?.track);
  // teaser cover: server-side events ship full base64 PHOTOS — rendered full-bleed (cover)
  // across the whole visual area, same as the hub carousel (fitting them bottom-right as if they were a
  // car render distorted them). Without an image, the card falls back to the circuit map + gradient.
  const coverImg = ev.teaser.image && ev.teaser.image.startsWith('data:') ? ev.teaser.image : '';
  const credits = eventCredits(ev);   // also in locked teasers (event-level rewards)
  const laps = payload?.laps ?? 0;
  const durationMin = payload?.duration_min ?? 0;
  const distance = laps > 0 ? `${laps} ${t('laps')}` : durationMin > 0 ? `${durationMin} ${t('dur_min')}` : '';

  // status chip (top corner): live (gold), finished (padlock), upcoming (grey + countdown).
  // The upcoming countdown switches to hours under a day: in days it used to round up, so an event
  // opening in two hours was announced as "STARTS IN 1 DAY".
  const startsIn = eventStartsIn(ev, now);
  const statusChip =
    status === 'live' ? <Chip kind="gold" style={{ fontSize: 9 }}>{t('events.cardLive', { count: eventDaysLeft(ev, now) })}</Chip>
    : status === 'ended' ? <Chip kind="lock" style={{ fontSize: 9 }}><LockIcon size={8} /> {t('events.cardEnded', { defaultValue: 'FINISHED' })}</Chip>
    : (
      <span className="row gap6" style={{ alignItems: 'center', flexWrap: 'wrap' }}>
        <span className="mono" style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)', background: 'color-mix(in oklab, var(--text-3) 14%, transparent)', border: '1px solid color-mix(in oklab, var(--text-3) 35%, transparent)', padding: '3px 7px', whiteSpace: 'nowrap' }}>
          {t('events.notYetAvailable', { defaultValue: 'NOT YET AVAILABLE' })}
        </span>
        <span className="mono" style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--cyan)', background: 'color-mix(in oklab, var(--cyan) 16%, transparent)', border: '1px solid color-mix(in oklab, var(--cyan) 45%, transparent)', padding: '3px 7px', whiteSpace: 'nowrap' }}>
          {t(startsIn.key, { count: startsIn.count })}
        </span>
      </span>
    );

  // root is clickable/focusable only when appropriate (same pattern as the Home carousel: locked/finished are NOT clickable).
  // plain div.panel (not the Panel component) because this needs handlers + a single gamepad focus target.
  return (
    <div
      className={`panel${actionable ? ' focusable' : ''}`}
      role={actionable ? 'button' : undefined}
      tabIndex={actionable ? 0 : undefined}
      onClick={actionable ? activate : undefined}
      onKeyDown={actionable ? (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); activate(); } } : undefined}
      style={{
        position: 'relative', overflow: 'hidden', padding: 0, display: 'flex', flexDirection: 'column',
        cursor: actionable ? 'pointer' : 'default',
        border: status === 'live' ? '1px solid color-mix(in oklab, var(--gold) 55%, var(--line))' : '1px solid var(--line)',
        filter: status === 'ended' || status === 'upcoming' ? 'saturate(.35)' : undefined,
        opacity: status === 'ended' ? 0.62 : status === 'upcoming' ? 0.72 : 1,
      }}
    >
      {/* VISUAL */}
      <div style={{ position: 'relative', height: 150, flex: 'none', overflow: 'hidden', background: night || storm ? GRAD_NIGHT : GRAD_DAY }}>
          {coverImg ? (
            <img src={coverImg} alt="" draggable={false} onError={(e) => logImageError(null, e.currentTarget.src)} style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', objectFit: 'cover' }} />
          ) : (
            <>
              <div style={{ position: 'absolute', inset: 0, background: `radial-gradient(70% 90% at 80% 8%, color-mix(in oklab, ${heroColor} 20%, transparent), transparent 62%)` }} />
              {mapUrl && <img src={mapUrl} alt="" draggable={false} onError={(e) => logImageError(null, e.currentTarget.src)} style={{ position: 'absolute', right: -18, top: -22, height: 190, opacity: 0.15, transform: 'rotate(5deg)' }} />}
              {storm && <div style={{ position: 'absolute', inset: 0, background: 'repeating-linear-gradient(118deg, transparent 0 7px, rgba(150,185,235,.06) 7px 8px)' }} />}
            </>
          )}
          <div style={{ position: 'absolute', inset: 0, background: 'linear-gradient(to top, var(--surface) 3%, transparent 52%)' }} />
          {/* top overlay: status + window */}
          <div className="row gap8" style={{ position: 'absolute', top: 10, left: 12, right: 12, alignItems: 'center', flexWrap: 'wrap' }}>
            {statusChip}
            <Mono style={{ fontSize: 10, color: 'var(--text-2)', letterSpacing: '.1em' }}>{eventWindowLabel(ev, lang)}</Mono>
          </div>
          {/* title bottom-left */}
          <Display style={{ position: 'absolute', left: 14, bottom: 10, right: 14, fontSize: 22, fontStyle: 'italic', lineHeight: 0.95, textShadow: '0 2px 12px rgba(0,0,0,.85)' }}>
            {eventTitle(ev, lang)}
          </Display>
        </div>

        {/* BODY */}
        <div className="col" style={{ gap: 10, padding: '12px 14px 14px', flex: 1, minHeight: 0 }}>
          {!!eventDesc(ev, lang) && (
            <Mono style={{ fontSize: 10.5, color: 'var(--text-2)', lineHeight: 1.55, display: '-webkit-box', WebkitLineClamp: 2, WebkitBoxOrient: 'vertical', overflow: 'hidden' }}>
              {eventDesc(ev, lang)}
            </Mono>
          )}
          {classes.length > 0 && (
            <div className="row gap6" style={{ flexWrap: 'wrap', alignItems: 'center' }}>
              {classes.map((cls) => {
                const col = eventClassColor(cls);
                return (
                  <span key={cls} className="row gap6" style={{ alignItems: 'center', padding: '2px 8px', background: 'var(--surface)', border: `1px solid color-mix(in oklab, ${col} 50%, transparent)` }}>
                    <AccentDot color={col} size={7} />
                    <Mono style={{ fontSize: 9.5, letterSpacing: '.08em', color: col }}>{classDisplayName(cls)}</Mono>
                  </span>
                );
              })}
              {multi && <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)' }}>{t('events.multiclass', { defaultValue: 'MULTICLASS' })}</Mono>}
            </div>
          )}
          {/* final row: track + distance | prize + ENTER */}
          <div className="row between" style={{ alignItems: 'baseline', marginTop: 'auto', paddingTop: 10, borderTop: '1px solid var(--line)', gap: 8 }}>
            <Mono style={{ fontSize: 10, color: 'var(--text-3)', minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
              {trackName}{distance ? ` · ${distance}` : ''}
            </Mono>
            <span className="row gap8" style={{ alignItems: 'baseline', flex: 'none' }}>
              {credits > 0 && <Mono style={{ fontSize: 12, fontWeight: 700, color: 'var(--gold)' }}>{formatCreditsCr(credits)}</Mono>}
              {clickable && <Mono style={{ fontSize: 10, fontWeight: 700, color: 'var(--accent-text)', letterSpacing: '.08em' }}>{t('events.enter', { defaultValue: 'ENTER' })} →</Mono>}
              {/* DLC: takes priority over the supporter paywall — sending someone to Patreon is pointless if what's missing is a Steam pack */}
              {status === 'live' && block.blocked && (
                <Mono style={{ fontSize: 10, fontWeight: 700, color: 'var(--text-3)', letterSpacing: '.08em' }}>
                  {block.product ? t('events.needsDlc', { product: block.product.toUpperCase() }) : t('events.needsContent')}
                </Mono>
              )}
              {locked && !block.blocked && <Mono style={{ fontSize: 10, fontWeight: 700, color: 'var(--accent-text)', letterSpacing: '.08em', textDecoration: 'underline' }}>{t('community.patreon').toUpperCase()}</Mono>}
            </span>
          </div>
        </div>
      </div>
  );
}

export function EventsScreen({ client, dataDir, onBack, onEnterEvent }: {
  client: ApiClientLike; dataDir?: string; onBack: () => void; onEnterEvent: (ev: SpecialEvent) => void;
}) {
  const { t, i18n } = useTranslation();
  const lang = i18n.language;
  const now = Date.now();
  const trackMapUrl = useTrackMapUrl(dataDir);
  const contentFor = useEventsContent(client);   // DLC: a single query for the whole grid

  // Only events within the public window (live + preview ≤72h). eventIsListed is a safety net
  // against old caches with distant teasers; finished ones are no longer listed here.
  const [events, setEvents] = useState<SpecialEvent[]>([]);
  useEffect(() => {
    if (!dataDir) return;
    let alive = true;
    void loadSpecialEvents(dataDir).then((f) => {
      if (alive && f) setEvents(f.events.filter((e) => eventIsListed(e)));
    }).catch(() => {});
    return () => { alive = false; };
  }, [dataDir]);

  // Escape returns to the hub (keyboard-operable, no mouse needed).
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onBack(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onBack]);

  const sorted = useMemo(() => {
    return [...events].sort((a, b) => {
      const d = STATUS_ORDER[eventStatus(a, now)] - STATUS_ORDER[eventStatus(b, now)];
      return d !== 0 ? d : Date.parse(a.starts_at) - Date.parse(b.starts_at);
    });
    // `now` is fixed at mount time (a snapshot of the season); it's not a reactive dependency.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [events]);
  const liveCount = useMemo(() => events.filter((e) => eventStatus(e, now) === 'live').length, [events, now]);

  return (
    <div className="screen col" style={{ height: '100%', minHeight: 0, padding: '20px 30px', gap: 16 }}>
      {/* header */}
      <div className="row between" style={{ alignItems: 'flex-start', flex: 'none' }}>
        <div className="col" style={{ gap: 5 }}>
          <Kicker style={{ color: 'var(--gold)' }}>{t('events.allKicker', { defaultValue: 'SEASON · LIMITED TIME' })}</Kicker>
          <Display as="h1" style={{ fontSize: 34, fontStyle: 'italic', lineHeight: 0.95 }}>{t('events.allTitle', { defaultValue: 'Special events' })}</Display>
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>
            {t('events.allSubtitle', { live: liveCount, total: events.length, defaultValue: '{{live}} live · {{total}} this season · windows in real time' })}
          </Mono>
        </div>
        <button className="btn btn-sm btn-ghost" data-gamepad-back onClick={onBack} style={{ flex: 'none' }}>← {t('nav.hub', { defaultValue: 'Hub' })}</button>
      </div>

      {/* grid */}
      {sorted.length > 0 ? (
        <div className="scroll" style={{ flex: 1, minHeight: 0 }}>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(360px, 1fr))', gap: 14, alignItems: 'start' }}>
            {sorted.map((ev) => (
              <EventCard key={ev.id} ev={ev} lang={lang} now={now} trackMapUrl={trackMapUrl} onEnter={onEnterEvent} block={eventBlock(contentFor.rowFor(ev.id))}
                         trackName={contentFor.trackNameFor(ev.id, ev.payload?.track)} />
            ))}
          </div>
        </div>
      ) : (
        <div className="col center grow" style={{ gap: 10, textAlign: 'center' }}>
          <span className="display" style={{ fontSize: 42, lineHeight: 1, color: 'color-mix(in oklab, var(--gold) 45%, var(--text-3))', opacity: 0.5 }}>◆</span>
          <Display style={{ fontSize: 21 }}>{t('events.allTitle', { defaultValue: 'Special events' })}</Display>
          <Mono style={{ fontSize: 11.5, color: 'var(--text-3)', maxWidth: 300, lineHeight: 1.6 }}>{t('home.specialEventsSoon')}</Mono>
        </div>
      )}
    </div>
  );
}
