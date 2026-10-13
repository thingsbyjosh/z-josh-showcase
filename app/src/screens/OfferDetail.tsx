// Market offer-detail screen (per player feedback): a sub-screen reached by clicking an
// OfferCard's body. Same underlying data as the card (the full MarketOffer, no re-fetch by id) but with
// room to explain WHY the team wants you (whyTheyWantKey: the dominant signal among the "why" bars +
// archetype) and an "Includes" breakdown (what the team covers vs. what you pay). Scaffolding copied from the Negotiate screen
// (back button + Display/Mono header); chips and action buttons copied from OfferCard, to avoid
// diverging from that card's own logic.
import { useTranslation } from 'react-i18next';
import { ARCH_ACADEMY_PROMOTION, whyTheyWantKey, type MarketOffer } from '../api/contracts';
import { Display, Mono, Kicker, Chip, Panel, Btn, LockIcon } from '../ui/components';
import { ExpiryChip } from '../ui/ExpiryChip';
import { accentText } from '../ui/colors';
import { formatCreditsCr } from '../ui/format';

const initials = (s: string): string => s.split(/[\s_]+/).map((w) => w[0]).slice(0, 2).join('').toUpperCase();

export function OfferDetail({ o, playerRating, onBack, onAccept, onDecline, onNegotiate, onTryout }: {
  o: MarketOffer;
  playerRating: number;
  onBack: () => void;
  onAccept: (id: string) => void;
  onDecline: (id: string) => void;
  onNegotiate: (id: string) => void;
  onTryout: (id: string) => void;
}) {
  const { t, i18n } = useTranslation();
  const es = i18n.language === 'es';
  const title = es ? (o.title || o.titleEn) : (o.titleEn || o.title);
  const teamName = o.team === 'seat_privateer_entry' ? t(o.team) : o.team;
  const declined = o.state === 2;
  const repOk = o.direct || playerRating >= o.reqRep;   // same gate as OfferCard
  // countdown -- only for actionable offers (not decided, not held); a missing
  // weeksLeft (old fixtures/offers) -> nothing is rendered.
  // 18/09: with the engine's daysLeft the count drops to days in the last week and says “Last day” on the last (ExpiryChip).
  const showsCountdown = (o.daysLeft != null || o.weeksLeft != null) && o.state === 0 && !o.held;

  const includeRows = ([
    ['seatEntry', o.includes.seatEntry],
    ['consumables', o.includes.consumables],
    ['damage', o.includes.damage],
    ['travel', o.includes.travel],
  ] as const);

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      {/* Header (copied from the Negotiate screen) */}
      <div className="row between" style={{ padding: '16px 30px 14px', alignItems: 'center', borderBottom: '1px solid var(--line)' }}>
        <div className="row gap12" style={{ alignItems: 'center', minWidth: 0 }}>
          <button className="btn btn-sm btn-ghost" onClick={onBack}>← {t('neg_back_market')}</button>
          <span className="mail-avatar" style={{ width: 44, height: 44, fontSize: 17, background: `color-mix(in oklab, ${o.teamColor} 26%, var(--surface-3))`, color: accentText(o.teamColor) }}>{initials(teamName)}</span>
          <div className="col" style={{ minWidth: 0 }}>
            <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)' }}>{teamName}</Mono>
            <Display style={{ fontSize: 24 }}>{title}</Display>
          </div>
        </div>
        {showsCountdown && <ExpiryChip daysLeft={o.daysLeft} weeksLeft={o.weeksLeft} />}
      </div>

      <div className="scroll col" style={{ gap: 16, minHeight: 0, padding: '18px 30px 24px', maxWidth: 640 }}>
        {/* Chips (copied from OfferCard) */}
        {!declined && (
          <div className="row gap6" style={{ flexWrap: 'wrap', alignItems: 'center' }}>
            <Chip kind="default" style={{ fontSize: 9, color: accentText(o.contractKindColor) }}>{o.contractKindIcon} {t(`market.ckind.${o.contractKind}`)}</Chip>
            {o.requiresTryout && <Chip kind="default" style={{ fontSize: 9, color: 'var(--purple)' }}>◐ {t('off_requires_tryout')}</Chip>}
            {o.conflicts.length > 0 && <span title={t('market.conflictWith', { list: o.conflicts.join(', ') })}><Chip kind="default" style={{ fontSize: 9, color: 'var(--danger)' }}>⚠ {t('off_calendar_conflict')}</Chip></span>}
            {o.archetype === ARCH_ACADEMY_PROMOTION
              ? <Chip kind="accent" style={{ fontSize: 9 }}>{t('market.affiliation.promotion')}</Chip>
              : o.parentTeam
                ? <Chip kind="default" style={{ fontSize: 9, color: 'var(--accent)' }}>{t('market.affiliation.canteraOf', { team: o.parentTeam })}</Chip>
                : null}
          </div>
        )}

        {/* Why they want you (per feedback): the dominant signal, NOT a generic one */}
        <Panel style={{ padding: '14px 16px' }}>
          <Kicker style={{ marginBottom: 8, display: 'block' }}>{t('off_why_you')}</Kicker>
          <Mono style={{ fontSize: 13, color: 'var(--text)', lineHeight: 1.6, display: 'block' }}>{t(whyTheyWantKey(o))}</Mono>
          {/* the seat is CONTESTED. If there was someone else in the running, this is where they're named: the seat was
              theirs if you don't get there first. With no rival (or no contest) nothing is shown — no adversary is invented. */}
          {!!o.rivalName && (
            <div style={{ marginTop: 12, paddingTop: 12, borderTop: '1px solid var(--line)' }}>
              <Mono style={{ fontSize: 9, letterSpacing: '.08em', color: 'var(--text-3)', display: 'block' }}>{t('off_contested')}</Mono>
              <Mono style={{ fontSize: 12, color: 'var(--text-2)', display: 'block', marginTop: 2 }}>{t('off_contested_with', { name: o.rivalName })}</Mono>
            </div>
          )}
          <div style={{ marginTop: 12, paddingTop: 12, borderTop: '1px solid var(--line)' }}>
            <Mono style={{ fontSize: 9, letterSpacing: '.08em', color: 'var(--text-3)', display: 'block' }}>{t('off_expectation')}</Mono>
            <Mono style={{ fontSize: 12, color: 'var(--text-2)', display: 'block', marginTop: 2 }}>{t(o.expectationKey)}</Mono>
          </div>
        </Panel>

        {/* Relationships: the offering team's team<->team graph -- promotion path + who's warming up/cooling off */}
        {o.rel && (o.rel.pathways.length > 0 || o.rel.allies.length > 0 || o.rel.rivals.length > 0) && (
          <Panel style={{ padding: '14px 16px' }}>
            <Kicker style={{ marginBottom: 8, display: 'block' }}>{t('market.rel.title')}</Kicker>
            {o.rel.pathways.length > 0 && (
              <Mono style={{ fontSize: 13, color: 'var(--text)', lineHeight: 1.6, display: 'block' }}>
                {t('market.rel.pathwaysTo', { list: o.rel.pathways.map((p) => p.team).join(' · ') })}
              </Mono>
            )}
            {o.rel.allies.length > 0 && (
              <Mono style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.6, display: 'block', marginTop: 6 }}>
                {t('market.rel.warms', { list: o.rel.allies.map((a) => a.team).join(', ') })}
              </Mono>
            )}
            {o.rel.rivals.length > 0 && (
              <Mono style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.6, display: 'block', marginTop: 2 }}>
                {t('market.rel.cools', { list: o.rel.rivals.map((r) => r.team).join(', ') })}
              </Mono>
            )}
          </Panel>
        )}

        {/* 01/10 ([tester], Discord): the offer that arrives by MAIL is opened here, and here it did not say which car
            comes with the seat; the Market card does (Market.tsx, item 5). Same data, same label. Without
            thumbnail: this screen does not receive dataDir and the name is what the player was asking about. */}
        {o.fixesCar && o.teamCarName && (
          <Panel style={{ padding: '12px 16px' }}>
            <Kicker style={{ marginBottom: 4, display: 'block' }}>{t('mkt_team_car')}</Kicker>
            <Mono style={{ fontSize: 13, color: 'var(--text-1)', display: 'block' }}>{o.teamCarName}</Mono>
          </Panel>
        )}

        {/* Includes (per feedback): payment breakdown -- what the team covers vs. what you pay */}
        <Panel style={{ padding: '14px 16px' }}>
          <Kicker style={{ marginBottom: 4, display: 'block' }}>{t('market.include.title')}</Kicker>
          <Mono style={{ fontSize: 11, color: 'var(--text-2)', display: 'block', marginBottom: 10 }}>{o.contractKindIcon} {t(`market.ckind.${o.contractKind}`)}</Mono>
          <div className="col" style={{ gap: 6 }}>
            {includeRows.map(([key, covered]) => (
              <div key={key} className="row between" style={{ alignItems: 'center', padding: '6px 10px', borderRadius: 3, background: 'var(--surface-2)' }}>
                <Mono style={{ fontSize: 12, color: 'var(--text-2)' }}>{t(`market.include.${key}`)}</Mono>
                <span style={{ fontSize: 11, fontWeight: 700, color: covered ? 'var(--green)' : 'var(--text-3)' }}>
                  {covered ? '✓' : '✗'} {covered ? t('market.include.coveredBy') : t('market.include.paidByYou')}
                </span>
              </div>
            ))}
          </div>
          <div className="col" style={{ gap: 4, marginTop: 10, paddingTop: 10, borderTop: '1px solid var(--line)' }}>
            {o.buyIn > 0 && (
              <div className="row between"><Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('market.include.buyIn')}</Mono><Mono style={{ fontSize: 11, fontWeight: 700, color: 'var(--danger)' }}>−{formatCreditsCr(o.buyIn)}</Mono></div>
            )}
            {o.perRace > 0 && (
              <div className="row between"><Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('market.include.perRace')}</Mono><Mono style={{ fontSize: 11, fontWeight: 700, color: 'var(--green)' }}>+{formatCreditsCr(o.perRace)}</Mono></div>
            )}
            {o.signBonus > 0 && (
              <div className="row between"><Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('market.include.signBonus')}</Mono><Mono style={{ fontSize: 11, fontWeight: 700, color: 'var(--gold)' }}>+{formatCreditsCr(o.signBonus)}</Mono></div>
            )}
          </div>
        </Panel>

        {/* Actions -- same logic as OfferCard: already-decided / waiting state ->
            informational only; otherwise, the three action buttons. */}
        {o.state === 1 ? (
          <Chip kind="green" style={{ fontSize: 10, alignSelf: 'flex-start' }}>✓ {t('mkt_accepted')} · +{formatCreditsCr(o.signBonus)}</Chip>
        ) : declined ? (
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>— {t('mkt_declined')} —</Mono>
        ) : o.held ? (
          <Chip kind="gold" style={{ fontSize: 10, alignSelf: 'flex-start' }}><LockIcon size={10} /> {(es ? o.lockReason : o.lockReasonEn) || o.lockReason || o.lockReasonEn}</Chip>
        ) : !repOk ? (
          <div>
            <div className="row between"><Mono style={{ fontSize: 10, color: 'var(--amber)' }}>{t('mkt_req_rep')}</Mono><Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{o.reqRep} REP</Mono></div>
            <div style={{ height: 5, borderRadius: 3, background: 'var(--surface-3)', marginTop: 4 }}>
              <div style={{ width: `${Math.min(100, (playerRating / Math.max(1, o.reqRep)) * 100)}%`, height: '100%', borderRadius: 3, background: 'var(--amber)' }} />
            </div>
          </div>
        ) : o.negotiable && !o.requiresTryout ? (
          // negotiable overrides "sign directly" -- the gate is negotiable,
          // not direct (the old ghost "Negotiate" button used to hang off direct). With an open thread (negState>=0)
          // the CTA enters the existing thread instead of reopening the composer.
          <div className="row gap8">
            <Btn variant="primary" size="lg" style={{ flex: 1 }} onClick={() => onNegotiate(o.id)}>
              {(o.negState ?? -1) >= 0 ? t('off_view_negotiation') : t('off_negotiate')}
            </Btn>
            <Btn variant="ghost" size="lg" onClick={() => onDecline(o.id)}>{t('mkt_decline')}</Btn>
          </div>
        ) : (
          <>
            <div className="row gap8">
              <Btn variant="primary" size="lg" style={{ flex: 1 }} disabled={!o.canAfford} onClick={() => (o.requiresTryout ? onTryout(o.id) : onAccept(o.id))}>
                {o.requiresTryout ? `◐ ${t('off_requires_tryout')}` : o.direct ? t('mkt_accept') : t('mkt_apply')}
              </Btn>
              <Btn variant="ghost" size="lg" onClick={() => onDecline(o.id)}>{t('mkt_decline')}</Btn>
            </div>
            {/* not negotiable (and no pending tryout): an explicit note explaining why there's no Negotiate button */}
            {!o.negotiable && !o.requiresTryout && (
              <Mono style={{ fontSize: 10, color: 'var(--text-3)', marginTop: 6 }}>{t('off_no_negotiation')}</Mono>
            )}
          </>
        )}
      </div>
    </div>
  );
}
