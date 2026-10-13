// Home/Hub: rich driver profile screen, ported from the engine's own hub-drawing code (source of truth). Left: the driver
// hero (identity + reputation tier + bar + trophy case + fanbase). Right: NEXT race +
// progress + session launcher (Play/status). Data via query.hub; the launcher via useGameSession. Labels
// via t() (engine keys + market.tier.* for the tier name). Advance week / break contract live here too.
import { useEffect, useMemo, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, CalendarPayload, HubPayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Panel, Btn, Display, Kicker, Mono, Chip } from '../ui/components';
import { accentText } from '../ui/colors';
import { isDnf, dnfLabel } from '../ui/format';
import type { UseGameSession } from '../game/useGameSession';
import { PHASE_LABEL_KEY } from '../game/gameSession';
import { getBridge, type ShellBridge } from '../shell/bridge';
import { loadSpecialEvents } from '../events/eventsStore';
import type { SpecialEvent } from '../events/eventsClient';
import { isPlayable, eventTitle, eventDesc, eventImageSrc, eventStatus, eventStartsIn, eventIsListed } from '../events/eventHelpers';
import { eventBlock, useEventsContent } from '../events/eventContent';
import { SupporterWall } from '../ui/SupporterWall';
import { CommunityCta } from '../ui/CommunityCta';
import { TimeAdvancePopup } from '../components/TimeAdvancePopup';
import { PlayerAvatar } from '../ui/PlayerAvatar';

const IDLE_PHASES = new Set(['idle', 'error']);
const fmtFollowers = (n: number): string =>
  n >= 1e6 ? `${(n / 1e6).toFixed(1)}M` : n >= 1e4 ? `${Math.floor(n / 1000)}K` : n >= 1000 ? `${(n / 1000).toFixed(1)}K` : String(n);

// Compact "at a glance" card: fills the right column with useful info (contract/offers/inbox/
// seasons) instead of a single empty pane. accent = highlights when something needs attention (>0 / has a contract).
function Glance({ label, value, accent }: { label: string; value: React.ReactNode; accent?: boolean }) {
  return (
    <div className="col" style={{ gap: 3, padding: '10px 12px', border: '1px solid var(--line)', background: 'var(--surface)', minWidth: 0 }}>
      <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)', textTransform: 'uppercase', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{label}</Mono>
      <span className="display" style={{ fontSize: 20, lineHeight: 1.05, color: accent ? 'var(--accent)' : 'var(--text)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{value}</span>
    </div>
  );
}

// "Recent results" row: position badge (P#/DNF, color-coded) + track + grid size.
function ResultRow({ pos, field, track, finished }: { pos: number; field: number; track: string; finished?: boolean }) {
  // P2 (F15): the retirement comes from `finished`; on retiring AMS2 classifies you anyway, so `pos` is where you got out.
  const dnf = isDnf(pos, finished);
  const color = dnf ? 'var(--danger)' : pos === 1 ? 'var(--gold)' : pos <= 3 ? 'var(--accent-text)' : 'var(--text-2)';
  const bg = dnf ? 'color-mix(in oklab, var(--danger) 14%, transparent)' : pos === 1 ? 'color-mix(in oklab, var(--gold) 14%, transparent)' : 'var(--surface-2)';
  const bd = dnf ? 'color-mix(in oklab, var(--danger) 35%, transparent)' : pos === 1 ? 'color-mix(in oklab, var(--gold) 35%, transparent)' : 'var(--line)';
  return (
    <div className="row" style={{ alignItems: 'center', gap: 10 }}>
      <span className="mono" style={{ minWidth: 42, textAlign: 'center', padding: '4px 6px', borderRadius: 4, fontSize: 12, fontWeight: 700, color, background: bg, border: `1px solid ${bd}` }}>{dnf ? dnfLabel(pos) : `P${pos}`}</span>
      <Mono style={{ fontSize: 12, color: 'var(--text-2)', flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{track || '—'}</Mono>
      {!dnf && field > 0 && <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>/{field}</Mono>}
    </div>
  );
}

// Career record: read as one integrated band (not repeated big-number tiles + labels).
function Trophy({ n, label, color, first }: { n: number; label: string; color: string; first?: boolean }) {
  return (
    <div className="row" style={{ flex: 1, alignItems: 'baseline', gap: 9, padding: '12px 16px', borderLeft: first ? 'none' : '1px solid var(--line)', minWidth: 0 }}>
      <span className="display" style={{ fontSize: 30, color }}>{n}</span>
      <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)', textTransform: 'uppercase', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{label}</Mono>
    </div>
  );
}

export function Home({ client, session, onRollover, dataDir, onEnterEvent, onViewAllEvents }: { client: ApiClientLike; session: UseGameSession; onRollover?: () => void; dataDir?: string; onEnterEvent?: (ev: SpecialEvent) => void; onViewAllEvents?: () => void }) {
  const { t, i18n } = useTranslation();
  const q = useQuery<HubPayload>(client, 'query.hub');
  // "upcoming races" (plural) with dates — the next few launchable calendar slots,
  // not just the first one. query.calendar already carries doy/date/status per slot; here it's just filtered and sorted.
  const calq = useQuery<CalendarPayload>(client, 'query.calendar');
  const nextRaces = useMemo(() => {
    const cal = calq.data;
    if (!cal || cal.today.doy <= 0) return [];
    return cal.slots
      .filter((s) => s.doy >= cal.today.doy && !s.isMemory && (s.enrolled || s.isAvailable))
      .sort((a, b) => a.doy - b.doy || (b.enrolled ? 1 : 0) - (a.enrolled ? 1 : 0))
      .slice(0, 3);
  }, [calq.data]);
  // Season-end detection — when the continuous clock's year ADVANCES (after advancing a week), trigger
  // the Rollover (season review) screen. The engine rolls the year automatically when crossing Dec 31 inside the advance-week call.
  const prevYear = useRef<number | null>(null);
  const hubYear = q.data?.lifecycle?.year;
  useEffect(() => {
    if (hubYear == null) return;
    if (prevYear.current != null && hubYear > prevYear.current) onRollover?.();
    prevYear.current = hubYear;
  }, [hubYear, onRollover]);

  // Hub carousel: live + preview ≤72h from special_events.json (eventIsListed).
  const [events, setEvents] = useState<SpecialEvent[]>([]);
  const [totalEvents, setTotalEvents] = useState(0);   // same listings (live + ≤72h preview) -> count for "See all"
  useEffect(() => {
    if (!dataDir) return;
    let alive = true;
    void loadSpecialEvents(dataDir)
      .then((f) => {
        if (!alive || !f) return;
        const listed = f.events.filter((e) => eventIsListed(e));
        setTotalEvents(listed.length);
        setEvents(listed);
      })
      .catch(() => {});
    return () => { alive = false; };
  }, [dataDir]);
  // OS bridge call (once) to resolve the active event's cover image.
  const [shellBridge, setShellBridge] = useState<ShellBridge | null>(null);
  useEffect(() => {
    let alive = true;
    void getBridge().then((b) => { if (alive) setShellBridge(b); });
    return () => { alive = false; };
  }, []);
  const [slide, setSlide] = useState(0);
  // automatic rotation (~6s); stops with 0-1 events (nothing to rotate).
  useEffect(() => {
    if (events.length <= 1) return;
    const id = window.setInterval(() => setSlide((s) => (s + 1) % events.length), 6000);
    return () => window.clearInterval(id);
  }, [events.length]);
  const activeIdx = events.length > 0 ? slide % events.length : 0;

  const { phase } = session.state;
  const connected = phase === 'connected' || client.status === 'connected';
  const canPlay = IDLE_PHASES.has(phase) && !connected;
  const h = q.data;

  // Lifecycle bar (bottom of the hub screen). Only shown with a season in progress (enrollmentCount>0).
  // Fire-and-refetch: the engine is the source of truth; .catch(refetch) tolerates the mock (command throws) by showing the real state.
  // fix (same defect as the calendar): this button moved the clock DIRECTLY, without warning — the player
  // could cause cascading no-shows without seeing it coming. Now it opens the SAME TimeAdvancePopup that the
  // calendar uses (scope:"available"); the advance itself is never executed with a single click.
  // C1: it is no longer disabled with weeksToNextEvent === 0 either. The clock does not depend on there being a race
  // available: there is always a stop ahead (at the very least, the end of the year), and the popup shows it.
  const [showAdvanceJump, setShowAdvanceJump] = useState(false);
  const lc = h?.lifecycle;
  // BREAK CONTRACT no longer lives in the hub. That button used to break ALL contracts at
  // once, and its total against the Paddock's per-contract price (2000 vs 500) looked like a bug even with a warning.
  // Terminating a contract now lives ONLY in Paddock -> My Contracts, one contract at a time with its price shown upfront. The
  // lifecycle card is left for advancing time (which depends on being enrolled).
  const showAdvance = !!lc && lc.enrollmentCount > 0;

  const rep = h?.reputation;
  const tierColor = rep?.tierColor ?? 'var(--accent)';
  const name = h ? `${h.identity.first} ${h.identity.last}`.trim() : t('common.player');

  // active slide of the events carousel: title/blurb/image resolved here (playable -> clickable; locked -> paywall).
  // The carousel lists everything that hasn't ended, so it also shows events that haven't opened yet: entering
  // one of those used to leave the pre-race screen with no cars (the engine only registers events with a LIVE window), so
  // entry now requires an open window, and the upcoming slide shows its own countdown.
  const contentFor = useEventsContent(client);   // DLC: content missing for each event
  const activeEvent = events[activeIdx];
  const activePlayable = activeEvent ? isPlayable(activeEvent) : false;
  const activeStatus = activeEvent ? eventStatus(activeEvent) : 'ended';
  const activeLive = activeStatus === 'live';
  // DLC: if the player is missing the event's track (or all of its classes), the arm can't succeed:
  // the slide says so and blocks entry, instead of letting the player hit a failed load with no explanation.
  const activeBlock = eventBlock(contentFor.rowFor(activeEvent?.id ?? ''));
  const canEnterEvent = activePlayable && activeLive && !activeBlock.blocked;
  const activeStartsIn = activeEvent && activeStatus === 'upcoming' ? eventStartsIn(activeEvent) : null;
  const activeImg = activeEvent && shellBridge && dataDir ? eventImageSrc(activeEvent, shellBridge.fs.assetUrl, dataDir) : '';

  return (
    <div className="screen" style={{ height: '100%', display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 18, padding: '20px 30px', minHeight: 0 }}>
      {/* LEFT (compact, NO scroll): condensed driver card + at-a-glance summary + session + lifecycle */}
      <div className="col" style={{ gap: 12, minHeight: 0 }}>
        {/* Driver card: identity + fame + integrated session LAUNCHER (no separate single-button panel) */}
        <Panel data-tour="hub.card" style={{ padding: '18px 22px', position: 'relative', overflow: 'hidden', flexShrink: 0, background: rep ? `radial-gradient(circle at 92% -20%, color-mix(in oklab, ${tierColor} 16%, var(--surface)), var(--surface))` : undefined }}>
          <div className="row gap12" style={{ alignItems: 'center' }}>
            {/* Profile photo: if the player set one (Profile > change photo), it replaces the initials. */}
            <PlayerAvatar
              initials={(h?.identity.first?.[0] ?? '?') + (h?.identity.last?.[0] ?? '')}
              size={50} fontSize={21}
              style={{ background: `color-mix(in oklab, ${tierColor} 20%, var(--surface-3))`, color: 'var(--text)' }}
            />
            <div className="col" style={{ minWidth: 0, gap: 5 }}>
              <Display style={{ fontSize: 26, lineHeight: 1 }}>
                {h ? <>{h.identity.first.toUpperCase()} <span style={{ color: 'var(--accent)' }}>{h.identity.last.toUpperCase()}</span></> : name}
              </Display>
              <span className="row gap4" style={{ flexWrap: 'wrap', alignItems: 'center' }}>
                {/* Ladder LEVEL (1-7) next to the name — the tier you're at, so you can tell at a
                    glance which series (tier N) you're eligible for. Different from the FAME tier, which is the reputation axis. */}
                {h?.rating && (
                  <span style={{ fontSize: 10, color: 'var(--text-2)', background: 'var(--surface-3)', border: '1px solid var(--line-2)', borderRadius: 4, padding: '2px 8px', whiteSpace: 'nowrap' }}>
                    {t('home.tierN', { n: h.rating.phase, defaultValue: 'Tier {{n}}' })} · {t(`tree.level.${h.rating.phase}`)}
                  </span>
                )}
                {rep && <span style={{ fontSize: 10, color: accentText(tierColor), background: `color-mix(in oklab, ${tierColor} 20%, transparent)`, borderRadius: 4, padding: '2px 8px', whiteSpace: 'nowrap' }}>★ {t(`market.tier.${rep.tierKey}`)}</span>}
              </span>
            </div>
          </div>

          <div className="row" style={{ marginTop: 14, border: '1px solid var(--line)', background: 'var(--bg-2)' }}>
            <Trophy first n={h?.stats.wins ?? 0} label={t('wins')} color="var(--gold)" />
            <Trophy n={h?.stats.podiums ?? 0} label={t('podiums')} color="var(--text)" />
            <Trophy n={h?.stats.races ?? 0} label={t('races')} color="var(--accent)" />
          </div>
          {rep && (
            <div className="row between" style={{ alignItems: 'center', marginTop: 12, paddingTop: 10, borderTop: '1px solid var(--line)' }}>
              <span className="row gap8" style={{ alignItems: 'center' }}>
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('fanbase')}</Mono>
                {/* 6 segments = 6 REP bands (Unknown..Legend): only fills completely at Legend */}
                <span className="row gap4">{[0, 1, 2, 3, 4, 5].map((i) => <span key={i} style={{ width: 12, height: 6, borderRadius: 1, background: i <= rep.tierIdx ? 'var(--accent)' : 'var(--surface-3)' }} />)}</span>
              </span>
              <Mono style={{ fontSize: 15, fontWeight: 700 }}>{fmtFollowers(rep.followers)} {t('followers')}</Mono>
            </div>
          )}
        </Panel>

        {/* Performance: unified rating (ELO) + last-race delta + phase progression (XP) */}
        <Panel style={{ padding: '14px 20px', flexShrink: 0 }}>
          <div className="row between" style={{ alignItems: 'baseline' }}>
            <Kicker>{t('home.rating')}</Kicker>
            {(() => { const d = h?.rating?.delta ?? 0; const c = d > 0 ? 'var(--green)' : d < 0 ? 'var(--danger)' : 'var(--text-3)'; return <Mono style={{ fontSize: 12, fontWeight: 700, color: c }}>{d > 0 ? `▲ +${d}` : d < 0 ? `▼ ${d}` : '—'}</Mono>; })()}
          </div>
          <div className="row gap8" style={{ alignItems: 'baseline', marginTop: 2 }}>
            <span className="display" style={{ fontSize: 36, lineHeight: 1 }}>{h?.rating?.elo ?? '—'}</span>
            <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>ELO</Mono>
          </div>
          {/* progress toward the NEXT tier — names the levels (Regional -> National) so it reads as
              "how much further to level up", not a cryptic bare number. At the top tier (Works) there's no next one. */}
          {h?.rating && h.rating.xpThreshold > 0 && (
            <div style={{ marginTop: 10 }}>
              <div className="row between">
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
                  {t(`tree.level.${h.rating.phase}`)}{h.rating.phase < 7 ? <> <span style={{ color: 'var(--text-2)' }}>→ {t(`tree.level.${h.rating.phase + 1}`)}</span></> : ` · ${t('home.tierMax', { defaultValue: 'MAX' })}`}
                </Mono>
                <Mono style={{ fontSize: 10, color: 'var(--text-2)' }}>{h.rating.xpToward} / {h.rating.xpThreshold} XP</Mono>
              </div>
              <div style={{ height: 6, borderRadius: 3, background: 'var(--surface-3)', marginTop: 5 }}><div style={{ width: `${Math.max(0, Math.min(100, (h.rating.xpToward / h.rating.xpThreshold) * 100))}%`, height: '100%', borderRadius: 3, background: 'var(--accent)' }} /></div>
            </div>
          )}
        </Panel>

        {/* At-a-glance summary: number of contracts · offers seeking you · unread mail · active seasons */}
        <div data-tour="hub.glance" style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10, flexShrink: 0 }}>
          <Glance label={t('home.contracts')} value={(h?.lifecycle.contractCount ?? 0) > 0 ? (h?.lifecycle.contractCount ?? 0) : t('home.privateer')} accent={(h?.lifecycle.contractCount ?? 0) > 0} />
          <Glance label={t('home.seekingYou')} value={h?.badges.directOffers ?? 0} accent={(h?.badges.directOffers ?? 0) > 0} />
          <Glance label={t('home.unread')} value={h?.badges.unread ?? 0} accent={(h?.badges.unread ?? 0) > 0} />
          <Glance label={t('home.seasons')} value={h?.lifecycle.enrollmentCount ?? 0} />
        </div>

        {/* Session: connection status with AMS2 + launch / reconnect (its own section, ~rating-sized) */}
        <Panel style={{ padding: '14px 20px', flexShrink: 0, display: 'flex', flexDirection: 'column', gap: 10 }}>
          <div className="row between" style={{ alignItems: 'center' }}>
            <Kicker>{t('home.session')}</Kicker>
            <Chip kind={connected ? 'green' : 'default'}>{connected ? t('session.connected') : t('conn.disconnected')}</Chip>
          </div>
          {canPlay ? (
            <Btn variant="primary" onClick={() => session.play()} style={{ alignSelf: 'flex-start' }}>{t('session.play')} →</Btn>
          ) : connected ? (
            <Mono style={{ fontSize: 11.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('home.gameLive')}</Mono>
          ) : (
            <Chip kind="accent">{t('session.' + (PHASE_LABEL_KEY[phase] ?? 'connecting'))}</Chip>
          )}
          {phase === 'safe_mode' && <Mono style={{ fontSize: 11, color: 'var(--amber)' }}>{t('session.safeMode')}</Mono>}
          {canPlay && <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('home.playToStart')}</Mono>}
        </Panel>

        {/* Recent results — SHRUNK: shares space with the session block (still fills whatever room is left) */}
        <Panel style={{ padding: '14px 20px', flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column', gap: 10 }}>
          <Kicker>{t('home.recentResults')}</Kicker>
          {(h?.recentResults?.length ?? 0) > 0 ? (
            /* 17/09 (Josh) — the list OVERFLOWED BELOW THE BOTTOM of the panel. The box had
               `flex:1; minHeight:0` but nothing containing the content, so six rows taller than
               the available space came out painted outside the panel. `.scroll` is the house class: it clips
               inside and lets you scroll if they do not fit, instead of spilling over what is below. */
            <div className="col scroll" style={{ gap: 8, flex: 1, minHeight: 0 }}>
              {h!.recentResults!.slice(0, 6).map((rr, i) => <ResultRow key={i} pos={rr.pos} field={rr.field} track={rr.track} finished={rr.finished} />)}
            </div>
          ) : (
            <div className="col center" style={{ flex: 1 }}><Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('home.noResults')}</Mono></div>
          )}
        </Panel>

        {/* Lifecycle: advance week + break contract */}
        {showAdvance && (
          <Panel style={{ padding: '14px 16px', display: 'flex', flexDirection: 'column', gap: 10, flexShrink: 0 }}>
            <div className="row between" style={{ alignItems: 'flex-end' }}>
              <div className="col" style={{ gap: 2 }}>
                <Btn variant="ghost" size="sm" onClick={() => setShowAdvanceJump(true)}>
                  {t('time.cta')} →
                </Btn>
              </div>
            </div>
          </Panel>
        )}
      </div>

      {/* RIGHT (NO scroll, no gaps): EVENTS = half the height + NEXT RACE = the other half. Both flex:1
          -> they split the column height 50/50 and fill it exactly, with no overflow. */}
      <div className="col" style={{ gap: 12, minHeight: 0 }}>
        <Panel data-tour="hub.events" style={{ flex: 1, minHeight: 0, padding: 0, position: 'relative', overflow: 'hidden', borderColor: 'color-mix(in oklab, var(--gold) 32%, var(--line))', display: 'flex', flexDirection: 'column', background: 'radial-gradient(circle at 50% 26%, color-mix(in oklab, var(--gold) 11%, var(--surface)), var(--surface))' }}>
          <div className="row between" style={{ position: 'absolute', top: 0, left: 0, right: 0, alignItems: 'center', padding: '13px 15px', zIndex: 2 }}>
            <Kicker style={{ color: 'var(--gold)' }}>{t('home.specialEvents')}</Kicker>
            {onViewAllEvents && totalEvents > 0 ? (
              // "See all (N)": N = ALL events (incl. finished); opens the full grid (events screen).
              <button className="btn btn-sm btn-ghost" onClick={onViewAllEvents}
                style={{ color: 'var(--gold)', borderColor: 'color-mix(in oklab, var(--gold) 40%, transparent)', background: 'rgba(0,0,0,.3)' }}>
                {t('events.viewAll', { defaultValue: 'View all' })} ({totalEvents}) →
              </button>
            ) : events.length === 0 ? (
              <span className="mono" style={{ fontSize: 9, letterSpacing: '.12em', color: 'var(--gold)', background: 'color-mix(in oklab, var(--gold) 16%, transparent)', padding: '3px 7px', borderRadius: 3 }}>{t('home.soon')}</span>
            ) : null}
          </div>
          {/* prev/next arrows: only with >1 event in the carousel (the LIVE ones Home loads). stopPropagation prevents
              triggering the active slide's "enter event" click. zIndex set ABOVE the supporter paywall:
              with a locked event active, the paywall used to cover the arrows and any click opened Patreon. */}
          {events.length > 1 && (['prev', 'next'] as const).map((dir) => (
            <button key={dir} aria-label={dir}
              onClick={(e) => { e.stopPropagation(); setSlide((s) => (s + (dir === 'next' ? 1 : events.length - 1)) % events.length); }}
              style={{
                position: 'absolute', top: '50%', left: dir === 'prev' ? 10 : undefined, right: dir === 'next' ? 10 : undefined,
                transform: 'translateY(-50%)', zIndex: 6, width: 32, height: 44, display: 'grid', placeItems: 'center', cursor: 'pointer',
                background: 'rgba(6,7,10,.55)', border: '1px solid color-mix(in oklab, var(--gold) 35%, var(--line))',
                color: 'var(--gold)', fontSize: 16, fontFamily: 'var(--font-display)',
              }}>
              {dir === 'prev' ? '‹' : '›'}
            </button>
          ))}
          {!activeEvent ? (
            // no live events (or still loading) -> "COMING SOON" placeholder (the original fallback)
            <div style={{ flex: 1, display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', gap: 10, padding: '24px 26px', textAlign: 'center' }}>
              <span className="display" style={{ fontSize: 42, lineHeight: 1, color: 'color-mix(in oklab, var(--gold) 45%, var(--text-3))', opacity: 0.5 }}>◆</span>
              <Display style={{ fontSize: 21, lineHeight: 1.1 }}>{t('home.specialEvents')}</Display>
              <Mono style={{ fontSize: 11.5, color: 'var(--text-3)', maxWidth: 300, lineHeight: 1.6 }}>{t('home.specialEventsSoon')}</Mono>
            </div>
          ) : (
            // active slide: playable -> clickable (enters the event); locked -> supporter paywall on top (NOT clickable)
            <div
              className={canEnterEvent ? 'focusable' : undefined}
              role={canEnterEvent ? 'button' : undefined}
              tabIndex={canEnterEvent ? 0 : undefined}
              onClick={canEnterEvent ? () => onEnterEvent?.(activeEvent) : undefined}
              onKeyDown={canEnterEvent ? (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); onEnterEvent?.(activeEvent); } } : undefined}
              style={{
                flex: 1, minHeight: 0, position: 'relative', display: 'flex', flexDirection: 'column',
                justifyContent: 'flex-end', gap: 6, padding: '24px 26px', textAlign: 'center',
                cursor: canEnterEvent ? 'pointer' : 'default',
                backgroundImage: activeImg ? `linear-gradient(180deg, rgba(10,9,6,.1), rgba(10,9,6,.82)), url(${activeImg})` : undefined,
                backgroundSize: 'cover', backgroundPosition: 'center',
                filter: activeStatus === 'upcoming' ? 'saturate(.45)' : undefined,
                opacity: activeStatus === 'upcoming' ? 0.72 : 1,
              }}
            >
              <Display style={{ fontSize: 21, lineHeight: 1.1 }}>{eventTitle(activeEvent, i18n.language)}</Display>
              <Mono style={{ fontSize: 11.5, color: 'var(--text-2)', maxWidth: 320, lineHeight: 1.6, alignSelf: 'center' }}>{eventDesc(activeEvent, i18n.language)}</Mono>
              {activeBlock.blocked
                ? (
                  <Mono style={{ fontSize: 9.5, letterSpacing: '.1em', color: 'var(--text-3)', marginTop: 2 }}>
                    {activeBlock.product ? t('events.needsDlc', { product: activeBlock.product.toUpperCase() }) : t('events.needsContent')}
                  </Mono>
                )
                : !activePlayable
                ? <SupporterWall tierMin={activeEvent.tier_min} />
                : activeStartsIn && (
                  <div className="col" style={{ gap: 4, alignItems: 'center', marginTop: 2 }}>
                    <Mono style={{ fontSize: 9.5, letterSpacing: '.1em', color: 'var(--text-3)' }}>
                      {t('events.notYetAvailable', { defaultValue: 'NOT YET AVAILABLE' })}
                    </Mono>
                    <Mono style={{ fontSize: 9.5, letterSpacing: '.1em', color: 'var(--cyan)' }}>
                      {t(activeStartsIn.key, { count: activeStartsIn.count })}
                    </Mono>
                  </div>
                )}
            </div>
          )}
          {/* carousel dots: one per event (or 3 decorative ones if there are none yet); clicking jumps to that slide */}
          <div className="row" style={{ justifyContent: 'center', gap: 6, padding: '10px 0 14px', position: 'relative', zIndex: 2 }}>
            {events.length > 0
              ? events.map((_, i) => (
                  <span
                    key={i}
                    role="button"
                    tabIndex={0}
                    onClick={() => setSlide(i)}
                    style={{ width: i === activeIdx ? 16 : 6, height: 6, borderRadius: 3, background: i === activeIdx ? 'var(--gold)' : 'var(--surface-3)', cursor: 'pointer' }}
                  />
                ))
              : [0, 1, 2].map((i) => <span key={i} style={{ width: i === 0 ? 16 : 6, height: 6, borderRadius: 3, background: i === 0 ? 'var(--gold)' : 'var(--surface-3)' }} />)}
          </div>
        </Panel>

        {/* Upcoming races — the next few launchable calendar slots with their dates.
            The first one gets "next up" visual weight; the rest render as compact rows. */}
        <Panel data-tour="hub.next" style={{ padding: '18px 22px', flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column', justifyContent: 'center' }}>
          <div className="row between" style={{ alignItems: 'center', marginBottom: 10 }}>
            <Kicker style={{ color: 'var(--accent-text)' }}>{t('home.nextRaces')}</Kicker>
            {h?.lifecycle && <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{h.lifecycle.weekday ? `${h.lifecycle.weekday} ` : ''}{h.lifecycle.cursorDate} · {h.lifecycle.year}</Mono>}
          </div>
          {nextRaces.length > 0 ? (
            <div className="col" style={{ gap: 10, minHeight: 0 }}>
              {/* separate the class (series) from the circuit — they used to sit 4px apart and read as glued together. */}
              <div className="col gap4">
                <span className="row gap6" style={{ alignItems: 'center' }}><span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--accent)' }} /><Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{nextRaces[0].seriesName} · {t('round')} {nextRaces[0].round}</Mono></span>
                <Display style={{ fontSize: 24, marginTop: 5 }}>{nextRaces[0].trackName}</Display>
                <Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{nextRaces[0].date}</Mono>
              </div>
              {nextRaces.slice(1).map((s) => (
                <div key={s.id} className="row" style={{ alignItems: 'center', gap: 10, paddingTop: 8, borderTop: '1px solid var(--line)' }}>
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)', minWidth: 46 }}>{s.date}</Mono>
                  <Mono style={{ fontSize: 11, color: 'var(--text-2)', flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{s.seriesName} · {s.trackName}</Mono>
                  {s.enrolled && <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--accent)', flex: 'none' }} />}
                </div>
              ))}
            </div>
          ) : h?.nextRace ? (
            <div className="col gap4">
              <span className="row gap6" style={{ alignItems: 'center' }}><span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--accent)' }} /><Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{h.nextRace.series} · {t('round')} {h.nextRace.round}</Mono></span>
              <Display style={{ fontSize: 26 }}>{h.nextRace.track}</Display>
              <Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{h.nextRace.date}</Mono>
            </div>
          ) : (
            <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('home.noCareer')}</Mono>
          )}
        </Panel>

        {/* COMMUNITY: Patreon (+ link your account if you're already a supporter) and Discord. Below UPCOMING RACES:
            closes out the right column without stealing height from the two panels above (flexShrink 0, a single row). */}
        <CommunityCta compact style={{ flexShrink: 0, justifyContent: 'center' }} />
      </div>

      {/* same time-advance popup that the calendar uses (scope:"available"); no advance
          time button runs cmd.time.advance in a single click: the popup IS the review.
          On return BOTH queries must be refreshed: the hub (clock, next race) and the calendar, which
          is where "UPCOMING RACES" on this same screen comes from and it went stale after advancing.
          The year crossing is opened HERE, with the payload's `crossedYear`: the hubYear watcher (above) stays
          in place as a net —it covers a crossing that arrives by another route—, but it depends on the refetch already bringing the
          year bumped, so it is not enough as the only trigger. */}
      {showAdvanceJump && (
        <TimeAdvancePopup
          client={client}
          params={{ scope: 'available' }}
          onCancel={() => setShowAdvanceJump(false)}
          onAdvanced={(p) => { setShowAdvanceJump(false); q.refetch(); calq.refetch(); if (p.crossedYear) onRollover?.(); }}
        />
      )}
    </div>
  );
}
