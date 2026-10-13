// Market/Driver market: ported from the engine's own market-drawing code (source of truth for data/
// ordering). Left: reputation (tier + progress + points ladder) + stats. Right: offers (direct offers seeking you
// first, then ones open by required rep). 100% READ-ONLY: accept/decline/negotiate/tryout are all engine commands; the payload
// already carries the state/gate those buttons need. Engine literal strings (title/expectation/lockReason)
// arrive as an {en,en}-style pair and are picked by locale. Chrome via engine keys (mkt_*/off_*/incl_*) + market.*.
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { ARCH_ACADEMY_PROMOTION, type ApiClientLike, type MarketPayload, type MarketOffer, type MarketReputation } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Kicker, Chip, Btn, LockIcon } from '../ui/components';
import { ExpiryChip } from '../ui/ExpiryChip';
import { classDisplayName } from '../ui/classNames';
import { accentText } from '../ui/colors';
import { formatCreditsCr } from '../ui/format';
import { rejectReasonKey } from '../ui/rejectReason';
import { useCarLiveries } from '../ui/vehImages';
import { Negotiate } from './Negotiate';
import { OfferDetail } from './OfferDetail';
import { supportLog, logImageError } from '../support/supportLog';

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}
const initials = (s: string): string => s.split(/[\s_]+/).map((w) => w[0]).slice(0, 2).join('').toUpperCase();

function RepPanel({ rep }: { rep: MarketReputation }) {
  const { t } = useTranslation();
  return (
    <div data-tour="market.rep" style={{ padding: '16px 16px 18px', borderRadius: 0, border: '1px solid var(--line)', background: `radial-gradient(circle at 50% 0%, color-mix(in oklab, ${rep.tierColor} 12%, var(--surface)), var(--surface))` }}>
      <Kicker style={{ color: 'var(--text-3)' }}>{t('mkt_reputation')}</Kicker>
      <Display style={{ fontSize: 26, fontStyle: 'italic', color: rep.tierColor }}>{t(`market.tier.${rep.tierKey}`)}</Display>
      <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{rep.score} REP</Mono>
      {rep.next && (
        <div style={{ marginTop: 10 }}>
          <div className="row between">
            <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('mkt_next')}: {t(`market.tier.${rep.next.tierKey}`)}</Mono>
            <Mono style={{ fontSize: 10, color: accentText(rep.tierColor) }}>{rep.pct}%</Mono>
          </div>
          <div style={{ height: 5, borderRadius: 3, background: 'var(--surface-3)', margin: '4px 0' }}>
            <div style={{ width: `${rep.pct}%`, height: '100%', borderRadius: 3, background: rep.tierColor }} />
          </div>
          <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{rep.next.gap} REP {t('mkt_to_for')} {t(`market.tier.${rep.next.tierKey}`)}</Mono>
        </div>
      )}
      <div className="row gap6" style={{ marginTop: 12, alignItems: 'center' }}>
        {rep.dots.map((d, i) => (
          <span key={i} style={{ width: d.filled ? 12 : 8, height: d.filled ? 12 : 8, borderRadius: '50%', background: d.filled ? d.color : 'var(--surface-3)', boxShadow: i === rep.tierIdx ? `0 0 8px ${d.color}` : 'none' }} />
        ))}
      </div>
    </div>
  );
}

// Offer-list ordering, refined per feedback -- 4 groups: 1) pending & available
// (by weeksLeft desc), 2) accepted, 3) declined, 4) locked ALWAYS at the bottom (even if its state is
// 1/2 -- held overrides). Stable tie-break by id. Does NOT touch the tab badge (that count already excludes
// held offers in the engine).
function offerRank(o: MarketOffer, playerRating: number): number {
  const repOk = o.direct || playerRating >= o.reqRep;
  if (o.held) return 4;                            // held (xp/gate lock): ALWAYS at the bottom, overrides the state
  if (o.state === 1) return 2;                    // accepted
  if (o.state === 2) return 3;                    // declined
  if (!repOk) return 4;                            // insufficient rep (pending, NOT signable) -> to the bottom with the held ones
  return 1;                                        // pending and signable, available
}

function OfferCard({ o, playerRating, dataDir, onOpen, selected }: { o: MarketOffer; playerRating: number; dataDir?: string; onOpen: (id: string) => void; selected?: boolean }) {
  const { t, i18n } = useTranslation();
  const es = i18n.language === 'es';
  const title = es ? (o.title || o.titleEn) : (o.titleEn || o.title);
  const teamName = o.team === 'seat_privateer_entry' ? t(o.team) : o.team;
  const repOk = o.direct || playerRating >= o.reqRep;
  const blocked = o.held || !repOk;               // "not available": rep requirement met but xp/gate-locked (held), OR insufficient rep
  const declined = o.state === 2;
  const fundKey = o.contractKind === 2 ? 'mkt_fund_paydriver' : o.contractKind === 1 ? 'mkt_fund_customer' : 'mkt_fund_salaried';
  // thumbnail of the FIXED car+skin the team assigns (works/customer). The hook always runs
  // (Rules of Hooks); with an empty slug it returns url()='' -> falls back to the placeholder.
  // urlFor and not url: the NN comes raw from the engine and its numbering doesn't match the PNGs' (see the vehicle-image mapping).
  const { urlFor: teamCarUrl } = useCarLiveries(dataDir, o.fixesCar ? o.teamCarSlug : undefined);
  const teamCarImg = o.fixesCar && o.teamLiveryNN != null && o.teamLiveryNN >= 0 ? teamCarUrl(o.teamLiveryNN) : '';

  return (
    <div onClick={() => onOpen(o.id)} data-offer-card={o.id} data-selected={selected ? '1' : undefined} style={{
      borderRadius: 0, border: o.direct && o.state === 0 ? `2px solid ${o.teamColor}` : '1px solid var(--line)',
      // held: darker/duller than declined -- it must read clearly as "not available" (locked
      // due to insufficient rep/exp), distinct from the actionable ones. opacity 0.5 + low saturation + flatter background.
      background: blocked ? 'var(--surface-2)' : 'var(--surface)', opacity: (declined || blocked) ? 0.5 : 1,
      filter: blocked ? 'saturate(.7)' : undefined,
      padding: '12px 14px', display: 'flex', flexDirection: 'column', gap: 8, cursor: 'pointer',
    }}>
      <div className="row between" style={{ alignItems: 'center' }}>
        <span className="row gap8" style={{ alignItems: 'center', minWidth: 0 }}>
          <span className="mail-avatar" style={{ background: `color-mix(in oklab, ${o.teamColor} 28%, var(--surface-3))`, color: accentText(o.teamColor) }}>{initials(teamName)}</span>
          <span className="col" style={{ minWidth: 0 }}>
            <span style={{ fontWeight: 700, fontSize: 14 }}>{teamName}</span>
            <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{o.from}</Mono>
          </span>
        </span>
        {o.held ? <Chip kind="gold" style={{ fontSize: 9 }}>{t('mkt_held')}</Chip>
          : o.direct ? <Chip kind="accent" style={{ fontSize: 9 }}>★ {t('mkt_team_seeks')}</Chip>
          : <Chip kind="default" style={{ fontSize: 9 }}>{t('mkt_driver_wanted')}</Chip>}
      </div>

      <Display style={{ fontSize: 17, lineHeight: 1.05 }}>{title}</Display>

      {!declined && (
        <div className="row gap6" style={{ flexWrap: 'wrap', alignItems: 'center' }}>
          <Chip kind="default" style={{ fontSize: 9, color: accentText(o.contractKindColor) }}>{o.contractKindIcon} {t(`market.ckind.${o.contractKind}`)}</Chip>
          {o.requiresTryout && <Chip kind="default" style={{ fontSize: 9, color: 'var(--purple)' }}>◐ {t('off_requires_tryout')}</Chip>}
          {o.conflicts.length > 0 && <span title={t('market.conflictWith', { list: o.conflicts.join(', ') })}><Chip kind="default" style={{ fontSize: 9, color: 'var(--danger)' }}>⚠ {t('off_calendar_conflict')}</Chip></span>}
          {/* 18/09 (Josh): on the card only when it is tight (less than a week); the weeks go in the detail */}
          {o.state === 0 && !o.held && o.daysLeft != null && o.daysLeft < 7 && <ExpiryChip daysLeft={o.daysLeft} style={{ fontSize: 9 }} />}
          {o.archetype === ARCH_ACADEMY_PROMOTION
            ? <Chip kind="accent" style={{ fontSize: 9 }}>{t('market.affiliation.promotion')}</Chip>
            : o.parentTeam
              ? <Chip kind="default" style={{ fontSize: 9, color: 'var(--accent)' }}>{t('market.affiliation.canteraOf', { team: o.parentTeam })}</Chip>
              : null}
          {o.rel && o.rel.pathways.length > 0 && (
            <Chip kind="accent" style={{ fontSize: 9 }}>{t('market.rel.pathHint', { team: o.rel.pathways[0].team })}</Chip>
          )}
        </div>
      )}

      <div className="row gap8" style={{ flexWrap: 'wrap', alignItems: 'center' }}>
        <span className="row gap6" style={{ alignItems: 'center' }}>
          <span style={{ width: 6, height: 6, borderRadius: '50%', background: o.discColor }} />
          <Mono style={{ fontSize: 10.5, color: 'var(--text-2)' }}>{classDisplayName(o.className)}</Mono>
        </span>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t(`scope_${o.scope}`)}</Mono>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{o.commitment} {o.commitment === 1 ? t('market.raceSingular') : t('races_lc')}</Mono>
      </div>

      {/* team's car+skin (fixed). The player sees what they'll be driving BEFORE signing. */}
      {o.fixesCar && (
        <div className="row gap8" style={{ alignItems: 'center', padding: '6px 8px', borderRadius: 3, background: 'var(--surface-2)' }}>
          {teamCarImg
            ? <img src={teamCarImg} alt="" loading="lazy" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ width: 64, height: 36, objectFit: 'contain', borderRadius: 2, flex: 'none' }} />
            : <span style={{ width: 64, height: 36, borderRadius: 2, background: 'var(--surface-3)', flex: 'none' }} />}
          <span className="col" style={{ gap: 1, minWidth: 0 }}>
            <Mono style={{ fontSize: 9, letterSpacing: '.08em', color: 'var(--text-3)' }}>{t('mkt_team_car')}</Mono>
            <Mono style={{ fontSize: 11, color: 'var(--text-2)', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{o.teamCarName}</Mono>
          </span>
        </div>
      )}

      {!declined && (
        <div style={{ padding: '6px 8px', borderRadius: 3, background: 'var(--surface-2)' }}>
          <Mono style={{ fontSize: 9, letterSpacing: '.08em', color: 'var(--text-3)' }}>{t('off_expectation')}</Mono>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-2)', display: 'block' }}>{(es ? o.expectation : o.expectationEn) || (o.expectation || o.expectationEn) || t(o.expectationKey)}</Mono>
        </div>
      )}

      {/* rewards */}
      <div className="row gap16" style={{ flexWrap: 'wrap' }}>
        {o.signBonus > 0 && <span className="col"><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('mkt_sign_bonus')}</Mono><span style={{ color: 'var(--gold)', fontWeight: 700, fontSize: 13 }}>+{formatCreditsCr(o.signBonus)}</span></span>}
        {o.perRace > 0 && <span className="col"><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('mkt_per_race')}</Mono><span style={{ color: 'var(--green)', fontWeight: 700, fontSize: 13 }}>{formatCreditsCr(o.perRace)}</span></span>}
        <span className="col"><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('mkt_funding')}</Mono><Mono style={{ fontSize: 11 }}>{t(fundKey)}</Mono></span>
        {o.seatName && <span className="col"><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('mkt_seat')}</Mono><Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{o.seatName}</Mono></span>}
      </div>

      {/* Negotiation thread state -- when a thread is open, it overrides the "new"/held state
          below (an ongoing thread is no longer "new"; it's read first). */}
      {o.negState === 0 && <Chip kind="default" style={{ color: 'var(--cyan)' }}>{t('market.offer.negWaiting', { n: o.negRespondDays ?? 0 })}</Chip>}
      {o.negState === 1 && <Chip kind="default" style={{ color: 'var(--amber)' }}>{t('market.offer.negCountered')}</Chip>}
      {o.negState === 2 && <Chip kind="default" style={{ color: 'var(--green)' }}>{t('market.offer.negAccepted')}</Chip>}
      {o.negState === 3 && <Chip kind="default" style={{ color: 'var(--danger)' }}>{t('market.offer.negUltimatum')}</Chip>}

      {/* status / gate (per feedback: sign/negotiate/decline live ONLY in the detail view).
          held FIRST and always: a locked offer shows the locked chip even if its state is
          1/2 (accepted/declined) -- held overrides the decided state, same as in the sort order (offerRank). */}
      {o.held ? (
        <Chip kind="lock" style={{ fontSize: 10 }}><LockIcon size={10} /> {(es ? o.lockReason : o.lockReasonEn) || o.lockReason || o.lockReasonEn || t('market.offer.locked')}</Chip>
      ) : o.state === 1 ? (
        <Chip kind="green" style={{ fontSize: 10 }}>✓ {t('mkt_accepted')} · +{formatCreditsCr(o.signBonus)}</Chip>
      ) : declined ? (
        <Mono style={{ fontSize: 11, color: 'var(--text-3)', textAlign: 'center' }}>— {t('mkt_declined')} —</Mono>
      ) : !repOk ? (
        <div>
          <div className="row between"><Mono style={{ fontSize: 9, color: 'var(--amber)' }}>{t('mkt_req_rep')}</Mono><Mono style={{ fontSize: 10, color: 'var(--text-2)' }}>{o.reqRep} REP</Mono></div>
          <div style={{ height: 4, borderRadius: 3, background: 'var(--surface-3)', marginTop: 3 }}>
            <div style={{ width: `${Math.min(100, (playerRating / Math.max(1, o.reqRep)) * 100)}%`, height: '100%', borderRadius: 3, background: 'var(--amber)' }} />
          </div>
        </div>
      ) : null}

      {/* Per feedback: the card no longer signs directly -> an explicit affordance toward the offer-detail view (same navigation as clicking the body).
          Accent-colored CTA (btn-primary = var(--accent)) to invite the click into the detail view, not a faint ghost button.
          NOT shown on locked offers (held OR insufficient rep): you can't "enter" -- so no CTA inviting you to. */}
      {!blocked && <Btn variant="primary" size="sm" style={{ alignSelf: 'flex-end' }} onClick={() => onOpen(o.id)}>{t('market.offer.seeMore')}</Btn>}
    </div>
  );
}

export function Market({ client, dataDir, onTryout, initialNegotiate, onNegotiateConsumed }: { client: ApiClientLike; dataDir?: string; onTryout?: (offerId: string) => void; initialNegotiate?: string | null; onNegotiateConsumed?: () => void }) {
  const { t } = useTranslation();
  const q = useQuery<MarketPayload>(client, 'query.market');
  const [negOffer, setNegOffer] = useState<string | null>(null);
  const [detailOffer, setDetailOffer] = useState<string | null>(null);
  // code from the last rejection of cmd.market.accept (null = no rejection to show)
  const [rejectCode, setRejectCode] = useState<string | null>(null);

  // after PASSING a tryout (normal race flow), the shell asks to open Negotiate here -> consumed once.
  useEffect(() => { if (initialNegotiate) { setNegOffer(initialNegotiate); onNegotiateConsumed?.(); } }, [initialNegotiate]); // eslint-disable-line react-hooks/exhaustive-deps

  // audit note, 07/20: a breadcrumb trail on opening an offer's detail view (a step before signing/negotiating; no command of its own).
  // switching offers clears the rejection warning (it belonged to the previous offer).
  useEffect(() => { setRejectCode(null); if (detailOffer) supportLog(client, `open offer detail: ${detailOffer}`); }, [detailOffer]); // eslint-disable-line react-hooks/exhaustive-deps

  // accept/decline an offer -> a command + refetch (the engine re-validates affordability/conflicts). Negotiate = the
  // Negotiate sub-screen. A tryout is no longer a local sub-screen: it's delegated to the shell (the normal race flow
  // through the pre-race screen); the verdict shows up in Results. Accept is gated on canAfford.
  // the engine now reports WHY it won't sign ({ok:false, code}), and it's shown here — before, a rejection was a
  // silent double refetch and the offer simply "did nothing". The warning lives right next to the detail view (where the
  // button that was pressed is) and clears on the next action or when switching offers.
  const accept = (id: string) => {
    setRejectCode(null);
    void client.cmd<{ ok?: boolean; code?: string }>('cmd.market.accept', { offerId: id })
      .then((r) => { if (r && r.ok === false) setRejectCode(r.code || 'refused'); q.refetch(); })
      .catch(() => q.refetch());
  };
  const decline = (id: string) => { setRejectCode(null); void client.cmd('cmd.market.decline', { offerId: id }).then(() => q.refetch()).catch(() => q.refetch()); };
  const startTryout = (id: string) => onTryout?.(id);

  // Flow sub-screen (reached from an offer's CTA). onDone/onBack both refresh the market.
  if (negOffer) return <Negotiate client={client} offerId={negOffer} onBack={() => setNegOffer(null)} onDone={() => { setNegOffer(null); q.refetch(); }} />;

  if (q.loading) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || !q.data) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;

  const { reputation, stats, contract, seekingYouCount, openSeatsCount, playerRating, offers } = q.data;
  const watching = q.data.watching ?? [];   // absent in old payloads/fixtures
  // market schedule: absent in old payloads -> treated as open, matching the previous behavior
  const marketOpen = q.data.marketOpen !== false;
  const daysToWindow = q.data.daysToWindow ?? 0;

  // pending & available (weeksLeft desc, fresh=4..about to expire=1; absent ->
  // 99 = very new) -> accepted -> declined -> locked at the bottom (always last). Stable tie-break
  // by id. The original array (offers) still serves id lookups (detailOffer); only the
  // rendering uses the sorted order.
  const orderedOffers = [...offers].sort((a, b) => {
    const ra = offerRank(a, playerRating), rb = offerRank(b, playerRating);
    if (ra !== rb) return ra - rb;
    if (ra === 1) {
      const wa = a.weeksLeft ?? 99, wb = b.weeksLeft ?? 99;
      if (wa !== wb) return wb - wa;
    }
    return a.id.localeCompare(b.id);
  });

  // the detail view no longer replaces the screen: it renders ALONGSIDE the list (master-detail, like
  // the design mockup's opportunities view). Losing the list when opening an offer forced you to go back to compare two,
  // which is exactly what you do during a signing window.
  const seleccionada = detailOffer ? offers.find((x) => x.id === detailOffer) ?? null : null;

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div className="row between" style={{ padding: '16px 30px 10px', alignItems: 'flex-end' }}>
        <div className="col" style={{ gap: 3 }}>
          <Display as="h1" style={{ fontSize: 30 }}>{t('mkt_title')}</Display>
          <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('mkt_sub')}</Mono>
          {contract && (
            <Mono style={{ fontSize: 10.5, color: 'var(--accent-text)' }}>
              {t('market.underContract')} {contract.count === 1
                ? `${contract.teamName} · ${contract.seriesName} · ${t(['mkt_kind_works', 'mkt_kind_customer', 'mkt_kind_paydriver'][contract.kind ?? 0])}`
                : `${contract.count} ${t('mkt_contracts_n')} · ${(contract.seriesNames ?? []).join(' · ')}`}
            </Mono>
          )}
        </div>
        <div className="row gap8">
          {seekingYouCount > 0 && <Chip kind="accent" style={{ fontSize: 10 }}>★ {seekingYouCount} {t('mkt_seek_you')}</Chip>}
          <Chip kind="default" style={{ fontSize: 10 }}>{openSeatsCount} {t('mkt_open_seats')}</Chip>
        </div>
      </div>

      {/* The opportunities list takes up HALF the width, not a 340px column. An offer carries
          team, series, deadline and reason, and at 340px all of that breaks into three lines and has to be read
          twice; this is the screen where the season gets decided, so how well it scans matters as much as the
          detail view. The 340px floor keeps it as it was on narrow windows instead of squeezing both panes. */}
      <div style={{ flex: 1, display: 'grid', gridTemplateColumns: 'minmax(340px,1fr) minmax(0,1fr)', gap: 16, minHeight: 0, padding: '0 30px 20px' }}>
        {/* ---- LIST (master) ---- */}
        <div className="scroll col" style={{ gap: 10, minHeight: 0 }} data-tour="market.offers">
          {/* The design mockup's note, pinned above the list: applying doesn't commit you to anything and everything
              gets resolved during the decision week. Without saying so, players hold back applications out of
              fear of commitment, which is exactly the opposite of what the system wants. */}
          <Mono style={{ fontSize: 10, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('mkt_apply_free')}</Mono>
          {watching.length > 0 && (
            <div className="col" style={{ gap: 8, padding: '12px 14px', border: '1px solid var(--line)', background: 'var(--surface)' }}>
              <Kicker>{t('mkt_watching')}</Kicker>
              {watching.map((w) => (
                <div key={`${w.seriesId}|${w.teamName}`} className="col" style={{ gap: 2 }}>
                  <Mono style={{ fontSize: 11.5, color: 'var(--text)', minWidth: 0 }}>
                    {w.teamName === 'seat_privateer_entry' ? t(w.teamName) : w.teamName} · {w.seriesName}
                  </Mono>
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
                    {w.rivalName ? t('mkt_watching_vs', { name: w.rivalName, n: w.racesLeft }) : t('mkt_watching_solo', { n: w.racesLeft })}
                  </Mono>
                </div>
              ))}
            </div>
          )}
          {offers.length === 0 ? (
            /* EMPTY BECAUSE OF SCHEDULE ≠ EMPTY BECAUSE OF NO OPENINGS, and they can't be told the same way. Outside the
               signing window there are no offers because the market is CLOSED, and saying "seats
               open up when drivers retire" there would be misleading: it's not that there are none, it's that it isn't time yet. The
               countdown turns it into a wait with a date attached — the same lesson learned from the
               off-season tab, where an empty screen used to read as "this is broken". */
            <div className="col center" style={{ padding: '34px 24px', gap: 8, textAlign: 'center' }}>
              {!marketOpen ? (
                <>
                  <Mono style={{ color: 'var(--text-2)', fontSize: 13 }}>{t('mkt_closed_title')}</Mono>
                  {daysToWindow > 0 && (
                    <Chip kind="default" style={{ fontSize: 10 }}>{t('mkt_closed_days', { n: daysToWindow })}</Chip>
                  )}
                  <Mono style={{ color: 'var(--text-3)', fontSize: 11, maxWidth: 420, lineHeight: 1.6 }}>{t('mkt_closed_why')}</Mono>
                </>
              ) : (
                <>
                  <Mono style={{ color: 'var(--text-2)', fontSize: 13 }}>{t('mkt_open_seats')}: 0</Mono>
                  <Mono style={{ color: 'var(--text-3)', fontSize: 11, maxWidth: 420, lineHeight: 1.6 }}>{t('mkt_empty_why')}</Mono>
                </>
              )}
            </div>
          ) : (
            <>
              {/* TWO LABELED GROUPS, per the mockup: first the ones seeking YOU (direct offers) and
                  then the open ones. A single list doesn't distinguish "they want you" from "you can try", and
                  that's the only hierarchy that actually matters when scanning the market. */}
              {orderedOffers.filter((o) => o.direct).length > 0 && (
                <Kicker style={{ color: 'var(--accent-text)' }}>★ {t('mkt_group_want_you')} ({orderedOffers.filter((o) => o.direct).length})</Kicker>
              )}
              {orderedOffers.filter((o) => o.direct).map((o) => (
                <OfferCard key={o.id} o={o} playerRating={playerRating} dataDir={dataDir} onOpen={setDetailOffer} selected={o.id === detailOffer} />
              ))}
              {orderedOffers.filter((o) => !o.direct).length > 0 && (
                <Kicker style={{ marginTop: 6 }}>{t('mkt_group_open')} ({orderedOffers.filter((o) => !o.direct).length})</Kicker>
              )}
              {orderedOffers.filter((o) => !o.direct).map((o) => (
                <OfferCard key={o.id} o={o} playerRating={playerRating} dataDir={dataDir} onOpen={setDetailOffer} selected={o.id === detailOffer} />
              ))}
            </>
          )}
        </div>

        {/* ---- DETAIL (slave) ---- */}
        <div className="scroll col" style={{ gap: 14, minHeight: 0 }} data-market-detail={seleccionada ? seleccionada.id : ''}>
          {/* the rejection reason, right next to the button that was pressed (same shape as the error panel in
              Negotiate). Before, a rejected accept was invisible: a refetch and silence. */}
          {rejectCode && (
            <div className="col" style={{ padding: '12px 14px', borderRadius: 0, background: 'color-mix(in oklab, var(--danger) 12%, var(--surface))', border: '1px solid var(--danger)' }}>
              <Mono style={{ fontSize: 12, fontWeight: 700, color: 'var(--danger)' }}>{t(rejectReasonKey(rejectCode))}</Mono>
            </div>
          )}
          {seleccionada ? (
            <OfferDetail o={seleccionada} playerRating={playerRating} onBack={() => setDetailOffer(null)} onAccept={accept} onDecline={decline} onNegotiate={setNegOffer} onTryout={startTryout} />
          ) : (
            <>
          <RepPanel rep={reputation} />
          <div style={{ padding: '14px 16px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
            <div className="row gap16">
              {([['wins', stats.wins, 'var(--accent)'], ['podiums', stats.podiums, 'var(--text)'], ['races', stats.races, 'var(--text)']] as const).map(([k, val, c]) => (
                <div key={k} className="col" style={{ gap: 2 }}>
                  <span className="display" style={{ fontSize: 22, color: c }}>{val}</span>
                  <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)', textTransform: 'uppercase' }}>{t(k)}</Mono>
                </div>
              ))}
            </div>
            <Mono style={{ fontSize: 10, color: 'var(--text-3)', display: 'block', marginTop: 10 }}>{t('mkt_rep_note')}</Mono>
          </div>
            </>
          )}
        </div>

      </div>
    </div>
  );
}
