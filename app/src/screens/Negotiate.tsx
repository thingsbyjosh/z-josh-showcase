// Negotiate/Contract negotiation: ported from the engine's own negotiate-drawing code (source of truth). Reached
// from an offer card in the Market ("Negotiate"). Left: the team's offer (lowball) + 8 demands
// (gated by level) + salary<->coverage tradeoff (3 packages) + sponsor chip. Right: the negotiation thread,
// server-authoritative: the real evaluation lives in the engine -- the
// client does NOT replicate that oracle, it only cross-references its own spend (public catalogs) against the agent's own range
// (budgetLo/budgetHi) to give a QUALITATIVE estimate (never the exact margin), and renders whichever view
// fits the current negState (no thread / waiting / counteroffer / deal closed). Send -> cmd.negotiate.send,
// Accept the table -> cmd.negotiate.acceptTable (or cmd.market.accept if there's no thread yet), Sign ->
// cmd.negotiate.sign, Walk away -> cmd.negotiate.walk. Chrome via neg_* (engine keys + curated keys);
// demand/tier labels = {en,es} pairs picked by locale.
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, NegotiatePayload, NegoDemandRow } from '../api/contracts';
import { NEG_WAITING, NEG_COUNTERED, NEG_ACCEPTED_PENDING, NEG_ULTIMATUM, NEG_SIGNED } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Kicker, Btn } from '../ui/components';
import { ExpiryChip } from '../ui/ExpiryChip';
import { rejectReasonKey } from '../ui/rejectReason';
import { accentText } from '../ui/colors';
import { formatCreditsCr } from '../ui/format';
import { useScreenCrumb } from '../support/useScreenCrumb';

const initials = (s: string): string => s.split(/[\s_]+/).map((w) => w[0]).slice(0, 2).join('').toUpperCase();

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

// Agent's range (no oracle access): 3 qualitative bands + 3 patience levels. No exact numbers in the UI.
type Band = 'probable' | 'risky' | 'very';
const BAND_COLOR: Record<Band, string> = { probable: 'var(--green)', risky: 'var(--amber)', very: 'var(--danger)' };
const BAND_KEY: Record<Band, string> = { probable: 'neg_band_probable', risky: 'neg_band_risky', very: 'neg_band_very' };
const PAT_KEY = ['neg_pat_receptive', 'neg_pat_tense', 'neg_pat_edge'];
const PAT_COLOR = ['var(--green)', 'var(--amber)', 'var(--danger)'];

export function Negotiate({ client, offerId, onBack, onDone }: { client: ApiClientLike; offerId: string; onBack: () => void; onDone: () => void }) {
  const { t, i18n } = useTranslation();
  const es = i18n.language === 'es';
  useScreenCrumb(client, 'negotiation', offerId);   // audit note, 07/20
  const q = useQuery<NegotiatePayload>(client, 'query.negotiate', { offerId });
  const p = q.data;

  const [demandsOn, setDemandsOn] = useState<boolean[]>(() => Array(8).fill(false));
  const [salaryPct, setSalaryPct] = useState<number>(50);
  // The selection is keyed by cardId (not by tierRank) -- two cards can share the same
  // rarity tier, and the engine needs to know EXACTLY which card is being offered (terms.sponsorCardId). '' = "None offered".
  const [sponsorCardId, setSponsorCardId] = useState<string>('');
  const [sent, setSent] = useState(false);
  // distinguishes failure (ok:false or a caught error) from success -- before, both used to show "Sent".
  // besides the failure itself, it stores the engine's rejection CODE ('' = failure with no code/a transport error)
  // so the specific reason (already have a contract / exclusive / buy-in / window) shows instead of a silent banner.
  const [error, setError] = useState<string | false>(false);
  const [busy, setBusy] = useState(false);

  if (q.loading) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  // D1548667906764837026: the offer no longer exists for the engine (expired/withdrawn/the window closed) --
  // before, this returned to the Market ALONE without warning (a one-click onBack), and "I pressed Negotiate and nothing happened" is
  // exactly the symptom they reported. Same reason (mkt_reject_expired) and same panel shape as the rest of the
  // rejections on this screen (errorPanel below); Back remains explicit, not automatic.
  if (q.error || !p || !p.found) {
    return (
      <Center>
        <div className="col center" style={{ padding: '14px', borderRadius: 0, background: 'color-mix(in oklab, var(--danger) 12%, var(--surface))', border: '1px solid var(--danger)', gap: 8 }}>
          <Mono style={{ fontSize: 14, fontWeight: 700, color: 'var(--danger)' }}>{t('mkt_reject_expired')}</Mono>
          <Btn variant="ghost" size="sm" onClick={onBack}>{t('neg_back_market')} →</Btn>
        </div>
      </Center>
    );
  }

  // Defensive guard: the normal Market path never reaches here with negotiable=false --
  // this only covers a direct/future payload with no negotiation CTA.
  if (p.negotiable === false) {
    return (
      <Center>
        <Mono style={{ color: 'var(--text-3)', marginBottom: 14 }}>{t('neg_not_negotiable')}</Mono>
        <Btn variant="ghost" size="sm" onClick={onBack}>{t('neg_back_market')} →</Btn>
      </Center>
    );
  }

  const demands = p.demands ?? [];
  const salaryPkgs = p.salaryPackages ?? [];
  const chips = p.sponsorChips ?? [];
  const negLevel = p.negotiationLevel ?? 0;
  const teamColor = p.teamColor ?? 'var(--accent)';
  const negId = p.negId ?? '';
  const st = p.negState ?? -1;

  // own spend (public catalogs) — the ONLY thing the client computes; no oracle, no exact margin in the UI.
  const demandCost = demands.reduce((s, d) => s + (demandsOn[d.demandId] ? d.cost : 0), 0);
  const salaryCost = salaryPkgs.find((pk) => pk.pct === salaryPct)?.cost ?? 0;
  const selChip = chips.find((c) => c.cardId === sponsorCardId);
  const sponsorRank = selChip?.tierRank ?? -1;
  const sponsorDelta = selChip ? selChip.delta : 0;
  const spend = demandCost + salaryCost - sponsorDelta;
  const lo = p.budgetLo ?? 0, hi = p.budgetHi ?? 0;
  const band: Band = spend <= lo ? 'probable' : spend <= hi ? 'risky' : 'very';
  const bandColor = BAND_COLOR[band];
  const patIdx = p.patienceBand ?? 0;
  const enabledCount = demandsOn.filter(Boolean).length;

  const toggleDemand = (id: number) => {
    setDemandsOn((prev) => {
      const on = prev[id];
      if (!on && enabledCount >= negLevel) return prev;   // gate: no more demands than the level allows
      const next = [...prev]; next[id] = !on; return next;
    });
  };
  const pickSponsor = (chip: { tierRank: number; cardId: string }) =>
    setSponsorCardId((cur) => (cur === chip.cardId || chip.tierRank < 0 ? '' : chip.cardId));

  // Actions: the thread is server-authoritative (evaluated in the engine). The client only dispatches and shows
  // a transient ack -- the Market refreshes on return (onDone), which is where the real state lives.
  const doCmd = (method: string, params: object) => {
    if (busy) return;
    setBusy(true);
    setError(false);
    // The engine returns {ok:boolean} -- ok:false (a valid rejection, e.g. the thread is no longer
    // COUNTERED) and a caught transport error both land on the same error banner; ONLY a truthy ok is a real "Sent".
    // A rejection now carries a code (already/exclusive/afford/window/refused/closed) and it's stored so
    // the specific reason can be shown; transport errors and old payloads with no code fall back to the generic one ('unknown' is truthy
    // on purpose: '' would break the `if (error)` check that selects the panel).
    void client.cmd<{ ok?: boolean; code?: string }>(method, params)
      .then((res) => { if (res && res.ok === false) { setError(res.code || 'unknown'); return; } setSent(true); })
      .catch(() => setError('unknown'))
      .finally(() => setBusy(false));
  };
  const sendProposal = () => doCmd('cmd.negotiate.send', { offerId, terms: { demands: demandsOn, salaryPct, sponsorTierRank: sponsorRank, sponsorCardId } });
  const acceptTable = () => (st >= 0 ? doCmd('cmd.negotiate.acceptTable', { negId }) : doCmd('cmd.market.accept', { offerId }));
  const signDeal = () => doCmd('cmd.negotiate.sign', { negId });
  const walk = () => (st >= 0 ? doCmd('cmd.negotiate.walk', { negId }) : onBack());

  const sign = (n: number): string => (n >= 0 ? `+${n}` : `${n}`);

  // Diff of the counteroffer (COUNTERED/ULTIMATUM): compares p.table against your last who===0 row in p.history.
  const lastMine = [...(p.history ?? [])].reverse().find((h) => h.who === 0);
  const askedMask = lastMine?.demandsMask ?? 0;
  const removed: NegoDemandRow[] = demands.filter((d) => (askedMask >> d.demandId) & 1 && !(p.table?.demands?.[d.demandId]));
  const lowered = (lastMine?.salaryPct ?? 50) > (p.table?.salaryPct ?? 50);

  // Agent range + patience (composer, no oracle): reused for both no-thread and COUNTERED (re-counter) states.
  const agentEstimate = (
    <div className="col" style={{ gap: 10, margin: '4px 0 14px' }}>
      <Kicker>{t('neg_agent_estimate')}</Kicker>
      <div className="row gap8" style={{ flexWrap: 'wrap' }}>
        <span style={{ fontSize: 12, fontWeight: 700, color: bandColor, background: `color-mix(in oklab, ${bandColor} 14%, transparent)`, border: `1px solid color-mix(in oklab, ${bandColor} 42%, transparent)`, borderRadius: 5, padding: '4px 12px' }}>{t(BAND_KEY[band])}</span>
        <span style={{ fontSize: 12, fontWeight: 700, color: PAT_COLOR[patIdx], background: `color-mix(in oklab, ${PAT_COLOR[patIdx]} 14%, transparent)`, border: `1px solid color-mix(in oklab, ${PAT_COLOR[patIdx]} 42%, transparent)`, borderRadius: 5, padding: '4px 12px' }}>{t(PAT_KEY[patIdx])}</span>
      </div>
    </div>
  );

  // Post-action: a generic transient ack (the real thread refreshes on returning to Market via onDone).
  const sentPanel = (
    <div className="col grow" style={{ justifyContent: 'flex-end' }}>
      <div className="col center" style={{ padding: '14px', borderRadius: 0, background: 'color-mix(in oklab, var(--green) 12%, var(--surface))', border: '1px solid var(--green)', gap: 8 }}>
        <Mono style={{ fontSize: 14, fontWeight: 700, color: 'var(--green)' }}>✓ {t('neg_sent_ack')}</Mono>
        <Btn variant="ghost" size="sm" onClick={onDone}>{t('neg_back_market')} →</Btn>
      </div>
    </div>
  );

  // Failure banner -- same shape as the sent panel but in red, with a Back button (no fake ack).
  // With an engine code it shows the specific REASON (already have a contract / exclusive / buy-in / window); without a code
  // ('unknown': a transport error or an old payload) rejectReasonKey falls back to the usual generic message.
  const errorPanel = (
    <div className="col grow" style={{ justifyContent: 'flex-end' }}>
      <div className="col center" style={{ padding: '14px', borderRadius: 0, background: 'color-mix(in oklab, var(--danger) 12%, var(--surface))', border: '1px solid var(--danger)', gap: 8 }}>
        <Mono style={{ fontSize: 14, fontWeight: 700, color: 'var(--danger)' }}>{t(rejectReasonKey(error === 'unknown' ? undefined : (error || undefined)))}</Mono>
        <Btn variant="ghost" size="sm" onClick={onBack}>{t('neg_back_market')} →</Btn>
      </div>
    </div>
  );

  let rightPanel: React.ReactNode;
  if (error) {
    rightPanel = errorPanel;
  } else if (sent) {
    rightPanel = sentPanel;
  } else if (st >= NEG_SIGNED) {
    // closed (>=10): a DEFENSIVE fallback -- the normal path only delivers open threads (closures arrive via the inbox).
    rightPanel = (
      <div className="col grow" style={{ justifyContent: 'flex-end', gap: 8 }}>
        <Mono style={{ fontSize: 13, color: 'var(--text-3)' }}>{t('neg_closed_generic')}</Mono>
        <Btn variant="ghost" size="sm" onClick={onBack}>{t('neg_back_market')} →</Btn>
      </div>
    );
  } else if (st === NEG_WAITING) {
    rightPanel = (
      <div className="col grow" style={{ gap: 12 }}>
        <div className="col" style={{ padding: '14px 16px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
          <Mono style={{ fontSize: 13, fontWeight: 700 }}>{t('neg_waiting', { n: p.respondInDays ?? 0 })}</Mono>
        </div>
        {(p.history ?? []).length > 0 && (
          <div className="col" style={{ gap: 4 }}>
            <Kicker>{t('neg_your_demands')}</Kicker>
            {(p.history ?? []).map((h, i) => (
              <div key={i} className="row between"><Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{h.who === 0 ? t('common.you') : p.teamName}</Mono><Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{h.salaryPct}%</Mono></div>
            ))}
          </div>
        )}
        <div className="col grow" style={{ justifyContent: 'flex-end' }}>
          <Btn variant="ghost" size="sm" onClick={onBack}>{t('neg_back_market')} →</Btn>
        </div>
      </div>
    );
  } else if (st === NEG_ACCEPTED_PENDING) {
    rightPanel = (
      <div className="col grow" style={{ gap: 12 }}>
        <div className="col center" style={{ padding: '18px 16px', borderRadius: 0, background: 'color-mix(in oklab, var(--green) 10%, var(--surface))', border: '1px solid var(--green)', gap: 6 }}>
          <Mono style={{ fontSize: 15, fontWeight: 700, color: 'var(--green)' }}>{t('neg_deal_agreed')}</Mono>
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('neg_reserve_note', { n: p.deadlineWeeks ?? 0 })}</Mono>
        </div>
        <div className="col grow" style={{ justifyContent: 'flex-end', gap: 8 }}>
          <Btn variant="primary" size="lg" disabled={busy} onClick={signDeal}>{t('neg_sign')} →</Btn>
          <Btn variant="ghost" size="sm" disabled={busy} onClick={walk}>{t('neg_walk')}</Btn>
        </div>
      </div>
    );
  } else if (st === NEG_COUNTERED || st === NEG_ULTIMATUM) {
    const ultimatum = st === NEG_ULTIMATUM;
    const sponsorTier = chips.find((c) => c.tierRank === (p.table?.sponsorTierRank ?? -1));
    rightPanel = (
      <div className="col grow" style={{ minHeight: 0 }}>
        <Kicker>{t(p.tableIsCounter ? 'neg_counter_title' : 'neg_table_title')}</Kicker>
        <div className="col" style={{ gap: 5, padding: '10px 0', borderTop: '1px solid var(--line)', borderBottom: '1px solid var(--line)', margin: '10px 0' }}>
          <div className="row between"><Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('neg_salary')}</Mono><Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{p.table?.salaryPct ?? 50}%</Mono></div>
          {sponsorTier && sponsorTier.tierRank >= 0 && (
            <div className="row between"><Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('neg_offer_sponsor')}</Mono><Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{sponsorTier.name}</Mono></div>
          )}
          {removed.length > 0 && (
            <div className="col" style={{ gap: 2, marginTop: 4 }}>
              <Mono style={{ fontSize: 11, color: 'var(--danger)', fontWeight: 700 }}>{t('neg_diff_removed')}</Mono>
              {removed.map((d) => <Mono key={d.demandId} style={{ fontSize: 11, color: 'var(--text-3)' }}>· {es ? d.es : d.en}</Mono>)}
            </div>
          )}
          {lowered && <Mono style={{ fontSize: 11, color: 'var(--danger)' }}>{t('neg_diff_lowered')}</Mono>}
        </div>
        {!ultimatum && agentEstimate}
        <div className="col grow" style={{ justifyContent: 'flex-end', gap: 8 }}>
          {ultimatum ? (
            <>
              <Btn variant="primary" size="lg" disabled={busy} onClick={acceptTable}>{t('neg_accept_table')} →</Btn>
              <Btn variant="ghost" size="sm" disabled={busy} onClick={walk}>{t('neg_walk')}</Btn>
            </>
          ) : (
            <>
              <Btn variant="primary" size="lg" disabled={busy} onClick={sendProposal}>{t('neg_send')} →</Btn>
              <div className="row gap8">
                <Btn variant="ghost" size="sm" style={{ flex: '1 1 60%' }} disabled={busy} onClick={acceptTable}>{t('neg_accept_table')}</Btn>
                <Btn variant="ghost" size="sm" style={{ flex: 1 }} disabled={busy} onClick={walk}>{t('neg_walk')}</Btn>
              </div>
            </>
          )}
        </div>
      </div>
    );
  } else {
    // no thread (negState undefined/-1): composer active, table = the team's base offer (already visible above).
    rightPanel = (
      <div className="col grow" style={{ minHeight: 0 }}>
        {agentEstimate}
        <div className="col grow" style={{ justifyContent: 'flex-end', gap: 8 }}>
          <Btn variant="primary" size="lg" disabled={busy} onClick={sendProposal}>{t('neg_send')} →</Btn>
          <div className="row gap8">
            <Btn variant="ghost" size="sm" style={{ flex: '1 1 60%' }} disabled={busy} onClick={acceptTable}>{t('neg_accept_table')}</Btn>
            <Btn variant="ghost" size="sm" style={{ flex: 1 }} disabled={busy} onClick={walk}>{t('neg_walk')}</Btn>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      {/* Header */}
      <div className="row between" style={{ padding: '16px 30px 14px', alignItems: 'center', borderBottom: '1px solid var(--line)' }}>
        <div className="row gap12" style={{ alignItems: 'center', minWidth: 0 }}>
          <button className="btn btn-sm btn-ghost" onClick={onBack}>← {t('neg_back_market')}</button>
          <span className="mail-avatar" style={{ width: 44, height: 44, fontSize: 17, background: `color-mix(in oklab, ${teamColor} 26%, var(--surface-3))`, color: accentText(teamColor) }}>{initials(p.teamName ?? '?')}</span>
          <div className="col" style={{ minWidth: 0 }}>
            <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)' }}>{t('neg_with')}</Mono>
            <Display style={{ fontSize: 24 }}>{p.teamName}</Display>
          </div>
          {/* 18/09 (Josh): the offer's deadline in view while you negotiate; only when it is tight */}
          {p.deadlineDays != null && p.deadlineDays < 7 && <ExpiryChip daysLeft={p.deadlineDays} />}
        </div>
        <div className="row gap24">
          <div className="col" style={{ alignItems: 'flex-end' }}><span className="display" style={{ fontSize: 22, color: 'var(--cyan)' }}>{p.rating}</span><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('neg_your_rating')}</Mono></div>
          <div className="col" style={{ alignItems: 'flex-end' }}><span className="display" style={{ fontSize: 22, color: (p.relationship ?? 0) > 60 ? 'var(--green)' : 'var(--amber)' }}>{p.relationship}</span><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('neg_relationship')}</Mono></div>
        </div>
      </div>

      <div style={{ flex: 1, display: 'grid', gridTemplateColumns: '1fr 380px', gap: 0, minHeight: 0 }}>
        {/* Left column (kept as-is: base offer + demands + salary + sponsor) */}
        <div className="scroll col" style={{ gap: 16, minHeight: 0, padding: '18px 24px 24px' }}>
          {/* Team's offer */}
          <div style={{ padding: '14px 16px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
            <div className="row between" style={{ alignItems: 'center', marginBottom: 10 }}>
              <Kicker>{t('neg_team_offer')}</Kicker>
              <Mono style={{ fontSize: 9, color: 'var(--text-3)', background: 'var(--surface-3)', borderRadius: 3, padding: '2px 6px' }}>{t('neg_starting_point')}</Mono>
            </div>
            <div className="row gap24" style={{ flexWrap: 'wrap' }}>
              <div className="col"><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('neg_bonus')}</Mono><span style={{ color: 'var(--gold)', fontWeight: 700, fontSize: 14 }}>{formatCreditsCr(p.startBonus ?? 0)}</span></div>
              <div className="col"><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('neg_per_race')}</Mono><span style={{ fontWeight: 700, fontSize: 14 }}>{formatCreditsCr(p.startPerRace ?? 0)}</span></div>
              <div className="col"><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('neg_duration')}</Mono><Mono style={{ fontSize: 13 }}>{p.startDurationSeasons} {t('common.season').toLowerCase()}</Mono></div>
              <div className="col"><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('neg_protection')}</Mono><Mono style={{ fontSize: 13, color: 'var(--danger)' }}>{t('neg_none')}</Mono></div>
            </div>
          </div>

          {/* Demands */}
          <div>
            <Kicker style={{ marginBottom: 8, display: 'block' }}>{t('neg_your_demands')}</Kicker>
            <div className="col" style={{ gap: 6 }}>
              {demands.map((d) => {
                const on = demandsOn[d.demandId];
                const disabled = !on && enabledCount >= negLevel;
                const refunds = d.cost < 0;
                return (
                  <button key={d.demandId} onClick={() => toggleDemand(d.demandId)} disabled={disabled}
                    className="focusable" style={{
                      display: 'flex', alignItems: 'flex-start', gap: 12, textAlign: 'left', padding: '10px 12px', borderRadius: 3,
                      background: on ? 'color-mix(in oklab, var(--accent) 8%, var(--surface))' : 'var(--surface)',
                      border: on ? '1px solid var(--accent)' : '1px solid var(--line)',
                      opacity: disabled ? 0.4 : 1, cursor: disabled ? 'not-allowed' : 'pointer',
                    }}>
                    <span style={{ width: 18, height: 18, marginTop: 1, flex: 'none', borderRadius: 4, border: on ? '1px solid var(--accent)' : '1px solid var(--line)', background: on ? 'var(--accent)' : 'transparent', color: '#fff', fontSize: 12, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>{on ? '✓' : ''}</span>
                    <span className="col" style={{ flex: 1, gap: 2 }}>
                      <span style={{ fontWeight: 600, fontSize: 13 }}>{es ? d.es : d.en}</span>
                      <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{es ? d.descEs : d.descEn}</Mono>
                    </span>
                    <span style={{ fontSize: 11, fontWeight: 700, color: refunds ? 'var(--green)' : 'var(--accent-text)' }}>{refunds ? `+${-d.cost}` : `−${d.cost}`}%</span>
                  </button>
                );
              })}
            </div>
          </div>

          {/* Salary <-> coverage */}
          <div style={{ padding: '12px 14px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
            <div className="row between" style={{ alignItems: 'center', marginBottom: 8 }}>
              <Kicker>{t('neg_salary_cover')}</Kicker>
              <span style={{ fontSize: 11, fontWeight: 700, color: salaryCost > 0 ? 'var(--accent-text)' : salaryCost < 0 ? 'var(--green)' : 'var(--text-3)' }}>{sign(-salaryCost)}%</span>
            </div>
            <div className="row gap8">
              {salaryPkgs.map((pk, i) => {
                const label = i === 0 ? t('neg_pkg_more_cover') : i === 1 ? t('neg_pkg_neutral') : t('neg_pkg_more_salary');
                const sel = salaryPct === pk.pct;
                const c = i === 0 ? 'var(--cyan)' : i === 2 ? 'var(--accent)' : 'var(--text-2)';
                const cText = i === 0 ? 'var(--cyan)' : i === 2 ? 'var(--accent-text)' : 'var(--text-2)';   // small text: AA-legible accent color
                return (
                  <button key={pk.pct} onClick={() => setSalaryPct(pk.pct)} className="focusable" style={{
                    flex: 1, padding: '8px 4px', borderRadius: 3, fontSize: 11.5, cursor: 'pointer',
                    background: sel ? `color-mix(in oklab, ${c} 16%, var(--surface))` : 'var(--surface)',
                    color: sel ? cText : 'var(--text-2)', border: sel ? `1px solid ${c}` : '1px solid var(--line)',
                  }}>{label}</button>
                );
              })}
            </div>
            <div className="row between" style={{ marginTop: 6 }}>
              <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{100 - salaryPct}% {t('neg_cover')}</Mono>
              <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{salaryPct}% {t('neg_salary')}</Mono>
            </div>
          </div>

          {/* Sponsor chip */}
          <div style={{ padding: '12px 14px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
            <div className="row between" style={{ alignItems: 'center', marginBottom: 4 }}>
              <Kicker>{t('neg_offer_sponsor')}</Kicker>
              {sponsorRank >= 0 && <span style={{ fontSize: 11, fontWeight: 700, color: sponsorDelta < 0 ? 'var(--danger)' : 'var(--green)' }}>{sign(sponsorDelta)}%</span>}
            </div>
            <Mono style={{ fontSize: 10, color: 'var(--text-3)', display: 'block', marginBottom: 8 }}>{t('neg_offer_sponsor_sub')}</Mono>
            <div className="row gap6" style={{ flexWrap: 'wrap' }}>
              {chips.map((c) => {
                const sel = sponsorCardId === c.cardId;
                const isNone = c.tierRank < 0;
                return (
                  <button key={c.cardId || 'none'} onClick={() => pickSponsor(c)} className="focusable" style={{
                    padding: '6px 10px', borderRadius: 3, fontSize: 11, cursor: 'pointer',
                    background: sel ? `color-mix(in oklab, ${isNone ? 'var(--text-3)' : c.color} 18%, var(--surface))` : 'var(--surface)',
                    color: sel ? (isNone ? 'var(--text)' : accentText(c.color)) : 'var(--text-2)',
                    border: sel ? `1px solid ${isNone ? 'var(--text-3)' : c.color}` : '1px solid var(--line)',
                  }}>{isNone ? t('neg_none_offered') : `${c.name} · ${es ? c.tierEs : c.tierEn}`}</button>
                );
              })}
            </div>
            {selChip && sponsorDelta < 0 && <Mono style={{ fontSize: 10, color: 'var(--danger)', display: 'block', marginTop: 8 }}>⚠ {t('neg_offends')}</Mono>}
          </div>
        </div>

        {/* Right panel: negotiation thread (varies by negState) */}
        <div className="col" style={{ minHeight: 0, padding: '18px 22px', borderLeft: '1px solid var(--line)', background: 'color-mix(in oklab, var(--surface) 60%, transparent)' }}>
          {rightPanel}
        </div>
      </div>
    </div>
  );
}
