// Inbox: master-detail view ported from the design mockup (an inbox screen), bound to OUR OWN
// contract type (InboxPayload). from/subject/body are engine i18n keys (bilingual via the translation table): they're
// resolved with t(); until the engine's string table is exported, t() returns the key itself as a fallback.
import { useEffect, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, InboxPayload, InboxMessage } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Chip, Btn } from '../ui/components';
import { ExpiryChip } from '../ui/ExpiryChip';
import { interpEngine } from '../i18n';
import { Negotiate } from './Negotiate';
import type { SpecialEvent } from '../events/eventsClient';

const KIND_COLOR: Record<string, string> = {
  offer: 'var(--accent)',
  result: 'var(--green)',
  manager: 'var(--green)', // family 1 (Atenea): used to be "result", same visual continuity
  info: 'var(--cyan)',
};

// kind labels (small text): AA-legible accent; everything else matches KIND_COLOR.
const KIND_TEXT_COLOR: Record<string, string> = { ...KIND_COLOR, offer: 'var(--accent-text)' };

function initials(s: string): string {
  return s.split(/[\s_]+/).map((w) => w[0]).slice(0, 2).join('').toUpperCase();
}

// Synthesizes a MINIMAL SpecialEvent from a substitute-drive email's params (family 'invite',
// subtype sub, payload_key "invite_sub" — team/series/track/fee, see the engine's substitute-drive weekly-draw logic)
// so it can reuse EventPrerace/cmd.event.arm without duplicating its UI. The engine routes the arm by the "subride_"
// id PREFIX (in its event-command routing) to a dedicated sub-ride arm path — it arms directly from the saved sub-ride state and
// IGNORES carClassId on that branch (it isn't a registered special event); classes/track/starts_at/ends_at only
// feed EventPrerace's hero BEFORE arming (query.prerace overwrites the real track/car/reward once it
// arrives). The window is deliberately wide (as covered by the test suite): the REAL window is decided by the engine's own
// readiness check, not by this synthetic object's eventStatus.
function synthSubRideEvent(m: InboxMessage): SpecialEvent {
  const p = m.params ?? {};
  const series = p.series ?? '';
  const team = p.team ?? '';
  const now = Date.now();
  return {
    id: m.id,
    starts_at: new Date(now - 86400000).toISOString(),
    ends_at: new Date(now + 30 * 86400000).toISOString(),
    tier_min: 'free',
    teaser: {
      title_en: series ? `Substitution \xB7 ${series}` : 'Substitution drive',
      title_es: series ? `Sustituci\xF3n \xB7 ${series}` : 'Carrera de sustituci\xF3n',
      desc_en: team, desc_es: team,
    },
    payload: {
      classes: [series || m.id],
      track: p.track ?? '',
      rewards: { credits: 0 },
    },
  };
}

export function Inbox({ client, onGoMarket, onGoGarage, onOpenEvent, onDriveSub }: {
  client: ApiClientLike; onGoMarket?: () => void; onGoGarage?: () => void;
  onOpenEvent?: (eventId: string) => void; onDriveSub?: (ev: SpecialEvent) => void;
}) {
  const { t } = useTranslation();
  const q = useQuery<InboxPayload>(client, 'query.inbox');
  // Inbox redesign: negotiation is PLAYED right here — an embedded composer inside the tab screen,
  // same pattern as the Market screen (negOffer). onDone triggers a refresh: the thread/offer will have changed.
  const [negOffer, setNegOffer] = useState<string | null>(null);
  // the engine emits in descending-index order = the real cross-year timeline; do NOT re-sort client-side
  // — year is constant (always the current calendar year) and week~day-of-year would break the order at
  // season rollover (the new season's preview, week 0, would end up buried under the previous one).
  const messages = useMemo(() => q.data?.messages ?? [], [q.data]);

  // Negotiation thread: the engine emits persisted messages in descending-index order and then
  // APPENDS the synthetic ones (you:true) at the end WITHOUT sorting them against the rest (a deliberate control tweak
  // from review feedback). Inside each thread (grouped by negId), messages ARE re-sorted client-side by
  // (year, week) ascending — Array#sort is stable, so ties keep the payload's original order.
  const byYearWeekAsc = (a: InboxMessage, b: InboxMessage) => (a.year - b.year) || (a.week - b.week);
  const threads = useMemo(() => {
    const byNeg = new Map<string, InboxMessage[]>();
    for (const m of messages) if (m.negId) {
      const arr = byNeg.get(m.negId) ?? []; arr.push(m); byNeg.set(m.negId, arr);
    }
    for (const arr of byNeg.values()) arr.sort(byYearWeekAsc);   // ascending: [0] = oldest, [len-1] = most recent
    return byNeg;
  }, [messages]);
  // list: one entry per thread. The list position respects the first time a negId appears in the
  // payload, but the CONTENT shown (subject/read state) is the thread's MOST RECENT message
  // by (year, week) — not the first one in the array, which can be an old persisted message if the newer
  // synthetic "you" message ended up at the end. Messages without a negId: current behavior unchanged.
  const listItems = useMemo(() => {
    const seen = new Set<string>();
    return messages
      .filter((m) => {
        if (!m.negId) return true;
        if (seen.has(m.negId)) return false;
        seen.add(m.negId); return true;
      })
      .map((m) => {
        if (!m.negId) return m;
        const thread = threads.get(m.negId)!;
        return thread[thread.length - 1];
      });
  }, [messages, threads]);

  const [selId, setSelId] = useState<string | null>(null);
  // Inbox redesign: mark as read on OPEN (persists in the engine -> real dot/badge/pruning). Optimistic:
  // readIds clears the dot instantly; the engine confirms on the next query.inbox. For negotiation threads it
  // marks the WHOLE thread (the synthetic "you" messages already come as read:true and the filter skips them).
  const [readIds, setReadIds] = useState<Set<string>>(new Set());
  const isRead = (m: InboxMessage) => m.read || readIds.has(m.id);
  // 18/09: what the engine already confirms as read comes off the optimistic set. Otherwise a mail that the engine marks
  // UNREAD again (the eve of its offer expiring) would stay switched off here for as long as the screen lives.
  useEffect(() => {
    setReadIds((prev) => {
      let cambia = false; const n = new Set(prev);
      for (const m of messages) if (m.read && n.delete(m.id)) cambia = true;
      return cambia ? n : prev;
    });
  }, [messages]);
  const sel: InboxMessage | undefined = listItems.find((m) => m.id === selId) ?? listItems[0];

  useEffect(() => {
    if (!sel) return;
    const targets = (sel.negId ? threads.get(sel.negId) ?? [sel] : [sel]).filter((m) => !m.read && !readIds.has(m.id));
    if (!targets.length) return;
    setReadIds((prev) => { const n = new Set(prev); for (const m of targets) n.add(m.id); return n; });
    for (const m of targets) void client.cmd('cmd.inbox.markRead', { id: m.id }).catch(() => {});
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sel?.id, q.data]);

  const unread = messages.filter((m) => !isRead(m)).length;
  // 18/09 (Josh): everything read in one go. Optimistic, like opening a mail: the dots go out now and the
  // engine confirms it in the next query.inbox. A single command, not one per mail (each would save).
  const marcarTodoLeido = () => {
    setReadIds((prev) => { const n = new Set(prev); for (const m of messages) if (!m.read) n.add(m.id); return n; });
    void client.cmd('cmd.inbox.markAllRead', {}).then(() => q.refetch()).catch(() => q.refetch());
  };

  if (q.loading) return <div className="col center grow" style={{ height: '100%' }}><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></div>;
  if (q.error) return <div className="col center grow" style={{ height: '100%' }}><Mono style={{ color: 'var(--text-3)' }}>{t('home.noCareer')}</Mono></div>;

  if (negOffer) return <Negotiate client={client} offerId={negOffer} onBack={() => setNegOffer(null)} onDone={() => { setNegOffer(null); q.refetch(); }} />;

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div className="row between" style={{ padding: '18px 30px 12px', alignItems: 'flex-end' }}>
        <div className="row gap16" style={{ alignItems: 'center' }}>
          <Display as="h1" style={{ fontSize: 30 }}>{t('nav.inbox')}</Display>
          {unread > 0 && <Chip kind="accent">{unread} {t('common.unread')}</Chip>}
        </div>
        {unread > 0 && (
          <Btn variant="ghost" size="sm" data-testid="inbox-mark-all" onClick={marcarTodoLeido}>{t('inbox.markAllRead')}</Btn>
        )}
      </div>

      <div style={{ flex: 1, display: 'grid', gridTemplateColumns: '370px 1fr', minHeight: 0 }}>
        {/* list */}
        <div className="scroll" style={{ borderRight: '1px solid var(--line)' }} data-tour="inbox.list">
          {listItems.map((e) => {
            const color = KIND_COLOR[e.kind] ?? 'var(--text-3)';
            const active = sel?.id === e.id;
            const from = e.fromName ?? t(e.from);
            const threadCount = e.negId ? threads.get(e.negId)!.length : 0;
            return (
              <div key={e.id} role="button" tabIndex={0}
                onClick={() => setSelId(e.id)}
                onKeyDown={(ev) => ev.key === 'Enter' && setSelId(e.id)}
                className={'mail-item focusable' + (active ? ' active' : '') + (!isRead(e) ? ' unread' : '')}
                style={{ outlineOffset: -2 }}>
                <span className="mail-avatar" style={{ background: `color-mix(in oklab, ${color} 28%, var(--surface-3))`, color }}>{initials(from)}</span>
                <div className="col" style={{ flex: 1, minWidth: 0, gap: 3 }}>
                  <div className="row between">
                    <span style={{ fontWeight: 600, fontSize: 13.5, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{from}</span>
                    <Mono style={{ fontSize: 9.5, color: 'var(--text-3)', flex: 'none', marginLeft: 8 }}>{e.year} · S{e.week}</Mono>
                  </div>
                  <span className="mail-subj" style={{ fontSize: 13, color: 'var(--text-2)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                    {interpEngine(t(e.subject), e.params)}
                    {threadCount > 1 && <Mono style={{ fontSize: 11, color: 'var(--text-3)', marginLeft: 6 }}>({threadCount})</Mono>}
                  </span>
                  <div className="row gap8" style={{ alignItems: 'center' }}>
                    <Mono style={{ fontSize: 9, letterSpacing: '.1em', textTransform: 'uppercase', color: KIND_TEXT_COLOR[e.kind] ?? 'var(--text-3)' }}>{e.kind}</Mono>
                    {/* 18/09 (Josh): on the eve of expiry, the row says so (and the engine moves it to the top) */}
                    {e.expiresInDays === 1 && <ExpiryChip daysLeft={1} style={{ fontSize: 8.5 }} />}
                  </div>
                </div>
                {!isRead(e) && <span className="mail-dot" style={{ background: 'var(--accent)' }} />}
              </div>
            );
          })}
        </div>

        {/* reading pane */}
        {sel && (
          <div className="scroll" key={sel.id}>
            <div style={{ maxWidth: 720, margin: '0 auto', padding: '28px 40px 40px' }}>
              <div className="row gap12" style={{ marginBottom: 6 }}>
                <Mono style={{ fontSize: 10, letterSpacing: '.14em', textTransform: 'uppercase', color: KIND_TEXT_COLOR[sel.kind] ?? 'var(--text-3)' }}>{sel.kind}</Mono>
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>· {sel.year} · {t('common.season')} S{sel.week}</Mono>
              </div>
              <Display as="h1" style={{ fontSize: 30, lineHeight: 1.02, marginBottom: 16 }}>{interpEngine(t(sel.subject), sel.params)}</Display>
              <div className="row gap12" style={{ paddingBottom: 18, borderBottom: '1px solid var(--line)', marginBottom: 20, alignItems: 'center' }}>
                <span className="mail-avatar" style={{ width: 44, height: 44, fontSize: 18, background: `color-mix(in oklab, ${KIND_COLOR[sel.kind] ?? 'var(--text-3)'} 28%, var(--surface-3))`, color: KIND_COLOR[sel.kind] ?? 'var(--text-3)' }}>{initials(sel.fromName ?? t(sel.from))}</span>
                <div className="col">
                  <span style={{ fontWeight: 700, fontSize: 15 }}>{sel.fromName ?? t(sel.from)}</span>
                  {sel.fromRole && <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t(sel.fromRole)}</Mono>}
                </div>
              </div>
              {sel.negId ? (
                // negotiation thread: bubbles in ascending order (threads already come sorted by
                // (year, week), no reverse needed) + a CTA that opens the embedded composer.
                <div className="col" style={{ gap: 12 }}>
                  {threads.get(sel.negId)!.map((m) => (
                    <div key={m.id} className="col" style={{ gap: 4, padding: '12px 14px', background: m.you ? 'var(--surface-3)' : 'var(--surface-2)', border: '1px solid var(--line)' }}>
                      <div className="row between" style={{ alignItems: 'center' }}>
                        <span style={{ fontWeight: 700, fontSize: 12.5 }}>{m.you ? t('inbox_you') : (m.fromName ?? t(m.from))}</span>
                        <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{m.year} · S{m.week}</Mono>
                      </div>
                      <p style={{ fontSize: 14.5, lineHeight: 1.55, color: 'var(--text-2)', whiteSpace: 'pre-line', margin: 0 }}>{interpEngine(t(m.body), m.params)}</p>
                    </div>
                  ))}
                  {/* The offerId is emitted by the engine from the live negotiation (the real neg_id carries
                      year+absDay and can't be derived by slicing). No offerId (thread purged) -> no CTA. */}
                  {sel.offerId && (
                    <Btn variant="primary" size="sm" style={{ alignSelf: 'flex-start', marginTop: 4 }}
                      onClick={() => setNegOffer(sel.offerId!)}>
                      {t('inbox_open_negotiation')} →
                    </Btn>
                  )}
                </div>
              ) : (
                <>
                  <p style={{ fontSize: 15.5, lineHeight: 1.62, color: 'var(--text-2)', whiteSpace: 'pre-line' }}>{interpEngine(t(sel.body), sel.params)}</p>
                  {/* 18/09: with the engine's days, the common deadline mark (last day / N days left); otherwise,
                      the usual weeks (invitations and older mails) */}
                  {typeof sel.expiresInDays === 'number' && sel.expiresInDays < 7 ? (
                    <div style={{ marginTop: 14 }}><ExpiryChip daysLeft={sel.expiresInDays} /></div>
                  ) : typeof sel.deadlineWeeks === 'number' && (
                    <Chip kind="accent" style={{ marginTop: 14 }}>{t('inbox.deadline', { n: sel.deadlineWeeks })}</Chip>
                  )}
                  {/* An offer no longer on the table says so RIGHT WHERE the button used to be. The engine stops
                      sending actions/offerId once it expires; without this line the email would go silent and
                      look broken, which is exactly what got reported the other way around (a button that did nothing). */}
                  {sel.expired && (
                    <div style={{ marginTop: 22 }}>
                      <Chip kind="lock">
                        {t(sel.expiredReason === 'withdrawn' ? 'inbox.offerWithdrawn' : 'inbox.offerExpired')}
                      </Chip>
                      <p style={{ marginTop: 10, fontSize: 13.5, lineHeight: 1.5, color: 'var(--text-3)' }}>
                        {t(`inbox.offerExpiredWhy.${sel.expiredReason ?? 'lapsed'}`)}
                      </p>
                    </div>
                  )}
                  {((sel.actions?.length ?? 0) > 0 || sel.offerId) && (
                    // Inbox redesign: a GENERIC action bar — renders whatever the engine sends
                    // (sign/decline, invitations, press…). answer -> refetch: the engine re-serves
                    // the email already resolved (no actions, with resolvedKey). Negotiate (offerId) is added
                    // in front of the engine's own actions -- it opens the embedded composer, with no refetch of its own.
                    <div className="row gap12" style={{ marginTop: 22 }}>
                      {sel.offerId && (
                        <Btn size="sm" variant="ghost" onClick={() => setNegOffer(sel.offerId!)}>
                          {t('inbox_act_negotiate')} →
                        </Btn>
                      )}
                      {sel.actions?.map((a) => (
                        <Btn key={a.choice} size="sm"
                          variant={a.kind === 'primary' ? 'primary' : a.kind === 'ghost' ? 'ghost' : 'default'}
                          style={a.kind === 'danger' ? { color: 'var(--accent-text)' } : undefined}
                          onClick={() => void client.cmd('cmd.inbox.answer', { id: sel.id, choice: a.choice }).then(() => q.refetch()).catch(() => {})}>
                          {t(a.label)}
                        </Btn>
                      ))}
                    </div>
                  )}
                  {sel.nav === 'market' && onGoMarket && (
                    <Btn size="sm" variant="ghost" style={{ marginTop: 14 }} onClick={onGoMarket}>
                      {t('inbox.viewMarket')} →
                    </Btn>
                  )}
                  {/* Treasury alert (family TEAMOPS) -> CTA to the garage, same
                      pattern as the courtship email -> Market above. */}
                  {sel.nav === 'garage' && onGoGarage && (
                    <Btn size="sm" variant="ghost" style={{ marginTop: 14 }} onClick={onGoGarage}>
                      {t('inbox.viewGarage')} →
                    </Btn>
                  )}
                  {/* Special EVENT invitation -- opens EventPrerace with the real event
                      (the shell looks it up by id in the list already loaded for the hub). */}
                  {sel.eventId && onOpenEvent && (
                    <Btn size="sm" variant="primary" style={{ marginTop: 14 }} onClick={() => onOpenEvent(sel.eventId!)}>
                      {t('inbox.openEvent')} →
                    </Btn>
                  )}
                  {/* Substitute-drive ACCEPTED + inside its arming window (the engine's subReady) -- synthesizes
                      the exhibition event from the email's params and enters EventPrerace the same way. */}
                  {sel.subReady && onDriveSub && (
                    <Btn size="sm" variant="primary" style={{ marginTop: 14 }} onClick={() => onDriveSub(synthSubRideEvent(sel))}>
                      {t('inbox.driveSub')} →
                    </Btn>
                  )}
                  {sel.resolvedKey && (
                    <p style={{ marginTop: 22, paddingTop: 14, borderTop: '1px solid var(--line)', fontSize: 13.5, color: 'var(--text-3)' }}>
                      {interpEngine(t(sel.resolvedKey), sel.params)}
                    </p>
                  )}
                </>
              )}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
