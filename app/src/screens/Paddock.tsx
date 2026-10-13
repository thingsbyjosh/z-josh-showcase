// Paddock (silly season): THE SIGNING WINDOW.
//
// The year has three phases and the Paddock is a different thing in each: during the SEASON it shows who drives
// where and until when (a planning tool, no seats actually in play); during the WINDOW seats open up and
// you can apply; during the DECISION WEEK everything resolves at once.
//
// This screen does NOT INVENT DATA. What the engine doesn't produce, this screen doesn't show: the three-state
// (agreed/tryout/pending) breakdown, requirements spelled out explicitly, and the team's own voice are later
// milestones. Showing a placeholder there would be narrating a silly season that doesn't exist yet.
import { useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, PaddockContract, PaddockPayload, PaddockSeat, PaddockSeatStatus } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Btn } from '../ui/components';
import { ExpiryChip } from '../ui/ExpiryChip';
import { Market } from './Market';
import { PaddockSeatSheet } from './PaddockSeatSheet';
import { PaddockWorld } from './PaddockWorld';

// Exact mirror of the engine's window-seat-state enum. Color is NOT decorative: it separates what's still in play
// (amber/accent) from what no longer is (gray), which is the only reading that matters at a glance.
const STATUS_META: Record<PaddockSeatStatus, { key: string; color: string }> = {
  free:     { key: 'pd_status_free',     color: 'var(--text-3)' },
  sounded:  { key: 'pd_status_sounded',  color: 'var(--cyan)' },
  disputed: { key: 'pd_status_disputed', color: 'var(--amber)' },
  offered:  { key: 'pd_status_offered',  color: 'var(--accent)' },
  rejected: { key: 'pd_status_rejected', color: 'var(--danger)' },
  renewed:  { key: 'pd_status_renewed',  color: 'var(--text-3)' },
  unknown:  { key: 'pd_status_unknown',  color: 'var(--text-3)' },
};

const PHASE_META = {
  season:   { key: 'pd_phase_season',   color: 'var(--text-3)' },
  window:   { key: 'pd_phase_window',   color: 'var(--cyan)' },
  decision: { key: 'pd_phase_decision', color: 'var(--gold)' },
} as const;

// can I apply to this seat? Mirrors the engine's own apply-to-seat check: seats you're already contesting,
// renewed ones, and already-resolved ones are excluded. It's replicated here so as not to offer a button the engine would reject —
// a button that does nothing teaches the player worse than a button that isn't there at all.
function canApply(s: PaddockSeat): boolean {
  return !s.applied && (s.status === 'free' || s.status === 'sounded');
}

// The design mockup's four sub-tabs. They aren't decoration: they separate "what you can
// sign" (which is the same old Market, moved in here) from "the world and its seats". The world
// view says it outright in the mockup: "opportunities live in their own tab".
// `news` is left out until that feed has real engine data behind it — showing it empty would be worse than not showing it.
export type PaddockSub = 'opps' | 'world' | 'contracts';

export function Paddock({
  client, dataDir, initialSub, onTryout, initialNegotiate, onNegotiateConsumed,
}: {
  client: ApiClientLike;
  dataDir?: string;
  initialSub?: PaddockSub;
  onTryout?: (offerId: string) => void;
  initialNegotiate?: string | null;
  onNegotiateConsumed?: () => void;
}) {
  const { t } = useTranslation();
  const q = useQuery<PaddockPayload>(client, 'query.paddock');
  const [busy, setBusy] = useState<string>('');
  const [fallo, setFallo] = useState<{ key: string; reason: string } | null>(null);
  // starts on OPPORTUNITIES: that's the actionable part. Whoever opens the Paddock almost always comes to see what's
  // being offered; the world view gets looked at after that.
  const [sub, setSub] = useState<PaddockSub>(initialSub ?? 'opps');
  // the seat open in the sliding panel. The KEY is stored, not the object: after applying or signing,
  // the query refreshes and the old object would be stuck frozen -- the panel would show the stale state.
  const [sheetKey, setSheetKey] = useState<string | null>(null);
  // the series whose termination is asking for confirmation. Terminating a contract is irreversible and costs money and
  // reputation, so it's a TWO-step action: the first button doesn't break anything, it just puts the price up front.
  const [confirmarRuptura, setConfirmarRuptura] = useState<string | null>(null);

  const data = q.data;

  // grouped by SERIES: the paddock reads by category, not as a flat list of teams. Order: by
  // descending level (the most ambitious ones on top, which is what you look at first), then by name.
  const grupos = useMemo(() => {
    const map = new Map<string, { seriesId: string; display: string; level: number; seats: PaddockSeat[] }>();
    for (const s of data?.seats ?? []) {
      const g = map.get(s.seriesId);
      if (g) g.seats.push(s);
      else map.set(s.seriesId, { seriesId: s.seriesId, display: s.seriesDisplay, level: s.level, seats: [s] });
    }
    return [...map.values()].sort((a, b) => b.level - a.level || a.display.localeCompare(b.display));
  }, [data]);

  // the two decision-week columns. They're always computed (they're cheap) and only rendered during their
  // own phase: the verdict is still there afterward, but folded back into the tree, which is where it lives the rest of the year.
  const ofrecidas = useMemo(() => (data?.seats ?? []).filter((s) => s.status === 'offered'), [data]);
  const descartadas = useMemo(() => (data?.seats ?? []).filter((s) => s.status === 'rejected'), [data]);

  const apply = async (s: PaddockSeat) => {
    const key = s.seriesId + '|' + s.team;
    setBusy(key);
    try {
      await client.cmd('cmd.paddock.apply', { seriesId: s.seriesId, team: s.team });
      q.refetch();
    } finally {
      setBusy('');
    }
  };

  const sign = async (s: PaddockSeat) => {
    const key = s.seriesId + '|' + s.team;
    setBusy(key);
    try {
      const r = await client.cmd<{ ok: boolean; reason: string }>('cmd.paddock.sign', { seriesId: s.seriesId, team: s.team });
      // the REASON is shown. A bare "Couldn't sign" leaves the player not knowing whether they're short on money,
      // already have that series, or their current team forbids it — and each of the three is fixed differently.
      setFallo(r && !r.ok ? { key, reason: r.reason || 'refused' } : null);
      q.refetch();
    } finally {
      setBusy('');
    }
  };

  // terminate the contract for ONE series. The engine charges the penalty and the reputation hit inside its own
  // break-contract function, which is the SAME function the amount shown on the card comes from, so what gets
  // warned about and what actually gets charged can never drift apart.
  const rescindir = async (c: PaddockContract) => {
    const key = 'break|' + c.seriesId;
    setBusy(key);
    try {
      const r = await client.cmd<{ ok: boolean }>('cmd.contract.breakSeries', { seriesId: c.seriesId });
      // ok:false = the engine no longer had that contract (auto-terminated for a no-show, or season close).
      // A termination that didn't happen is NOT announced as if it did: it's stated, and the refetch leaves the screen showing the real state.
      setFallo(r && !r.ok ? { key, reason: 'not_found' } : null);
      setConfirmarRuptura(null);
      q.refetch();
    } finally {
      setBusy('');
    }
  };

  if (q.loading && !data)
    return (
      <div className="col center grow" style={{ height: '100%' }}>
        <Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono>
      </div>
    );
  if (!data)
    return (
      <div className="col center grow" style={{ height: '100%' }}>
        <Mono style={{ color: 'var(--text-3)' }}>{q.error ?? t('common.loading')}</Mono>
      </div>
    );

  const phase = PHASE_META[data.phase] ?? PHASE_META.season;
  // the countdown that matters depends on the phase: during the season, when it OPENS; once the window is open,
  // when it gets DECIDED. Showing both at once is noise; showing the wrong one is misleading.
  const cuenta = data.phase === 'season' ? data.daysToWindow : data.daysToDecision;
  const cuentaKey = data.phase === 'season' ? 'pd_days_to_window' : 'pd_days_to_decision';

  return (
    // position:relative: confines the seat panel (position:absolute) to THIS screen, below the
    // top bar. Without it, it would climb up to the shell's container, which also scrolls.
    <div className="col grow" style={{ height: '100%', minHeight: 0, gap: 16, padding: '20px 30px 0', position: 'relative' }}>
      <div className="row" style={{ alignItems: 'flex-end', gap: 16, flexWrap: 'wrap' }}>
        <Display style={{ fontSize: 34, lineHeight: 1 }}>{t('pd_title', { defaultValue: 'PADDOCK' })}</Display>
        <span
          className="mono"
          data-paddock-phase={data.phase}
          style={{
            fontSize: 10, letterSpacing: '.14em', color: phase.color, whiteSpace: 'nowrap',
            border: `1px solid color-mix(in oklab, ${phase.color} 45%, var(--line-2))`,
            background: `color-mix(in oklab, ${phase.color} 14%, transparent)`,
            borderRadius: 3, padding: '4px 10px',
          }}
        >
          {t(phase.key)}
        </span>
        <div className="grow" />
        {/* the season being signed for. In year 1's compressed cycle it's THIS year, not next year,
            and without saying so the screen would mislabel half of all new saves. */}
        <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>
          {t('pd_signing_for', { year: data.targetYear, defaultValue: 'Signing for {{year}}' })}
          {cuenta > 0 && ` · ${t(cuentaKey, { days: cuenta, defaultValue: '{{days}} days' })}`}
        </Mono>
      </div>

      {/* THE DROUGHT. The window passed and you ended up with nothing. It's said out loud and at the very
          top, because until now this state looked like an empty tab, and an empty screen doesn't read
          as "you missed out", it reads as "this is broken". And above all: it's NOT game over. You rent a drive,
          race on your own dime, and get looked at again in September, so the copy says that before anything else. */}
      {data.drought && (
        <div className="panel col" data-paddock-drought="1"
             style={{ gap: 6, padding: '13px 17px', border: '1px solid color-mix(in oklab, var(--danger) 32%, var(--line))' }}>
          <span className="mono" style={{ fontSize: 10, letterSpacing: '.14em', color: 'var(--danger)' }}>
            {t('pd_drought_title', { defaultValue: 'NO SEAT FOR NEXT SEASON' })}
          </span>
          <span style={{ fontSize: 12.5, color: 'var(--text-2)', lineHeight: 1.5, maxWidth: '62ch' }}>
            {t('pd_drought_body', {
              defaultValue: 'The window closed and nothing came through. You will rent a car and race the season at your own cost — no salary, worse machinery. Finish well and they will be looking again in September.',
            })}
          </span>
        </div>
      )}

      {/* the design mockup's sub-tabs. Decision week is NOT one of them: it's an overlay screen that takes
          over when it's time, because during that week there's nothing else to do. */}
      {data.phase !== 'decision' && (
        <div className="row gap8" style={{ alignItems: 'center', borderBottom: '1px solid var(--line-2)', paddingBottom: 8 }}>
          {(['opps', 'world', 'contracts'] as PaddockSub[]).map((k) => (
            <button
              key={k}
              className="focusable"
              data-paddock-sub={k}
              data-active={sub === k ? '1' : undefined}
              onClick={() => setSub(k)}
              style={{
                background: 'none', border: 'none', cursor: 'pointer', padding: '4px 2px',
                fontSize: 11, letterSpacing: '.12em', fontFamily: 'var(--font-mono)',
                color: sub === k ? 'var(--text)' : 'var(--text-3)',
                borderBottom: sub === k ? '2px solid var(--accent)' : '2px solid transparent',
              }}
            >
              {t(`pd_sub_${k}`)}
            </button>
          ))}
        </div>
      )}

      {/* OFFERS. The Market doesn't disappear: it moves in here, whole, with the same props
          (tryout, negotiation). The mockup removes 'market' from the tab bar and has the Paddock
          handle its route — having two places to look at contracts was the actual bug. */}
      {data.phase !== 'decision' && sub === 'opps' && (
        <div className="col grow" style={{ minHeight: 0 }} data-paddock-opps="1">
          <Market
            client={client}
            dataDir={dataDir ?? ''}
            onTryout={onTryout ?? (() => {})}
            initialNegotiate={initialNegotiate ?? null}
            onNegotiateConsumed={onNegotiateConsumed ?? (() => {})}
          />
        </div>
      )}

      {/* MY CONTRACTS: where you're committed and what's demanded of you. It's the other half of the question
          the Paddock answers — without it you'd be looking at other seats without knowing what you can count on. */}
      {data.phase !== 'decision' && sub === 'contracts' && (
        <div className="col grow" style={{ minHeight: 0, overflowY: 'auto', gap: 8, paddingBottom: 24 }} data-paddock-contracts="1">
          {(data.contracts ?? []).length === 0 && (
            <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>
              {t('pd_no_contracts', { defaultValue: 'You are not under contract with anyone.' })}
            </Mono>
          )}
          {(data.contracts ?? []).map((c) => {
            // ON TRACK / AT RISK: the card's entire reading. It can only be stated when there's both
            // a target AND a standings table; without both, nothing is asserted — which is different from saying things are going fine.
            const juzgable = c.objTargetPos > 0 && (c.currentPos ?? 0) > 0;
            const cumple = juzgable && (c.currentPos as number) <= c.objTargetPos;
            const col = !juzgable ? 'var(--text-3)' : cumple ? 'var(--green)' : 'var(--danger)';
            // Are you ahead of your teammate? It's the comparison the team looks at first,
            // because it's the only one with the same car underneath both of you. It's only stated once both positions are known.
            const duelo = (c.currentPos ?? 0) > 0 && (c.teammatePos ?? 0) > 0;
            const ganasElDuelo = duelo && (c.currentPos as number) < (c.teammatePos as number);
            return (
            <div key={c.seriesId} className="panel col" data-paddock-contract={c.seriesId} style={{ padding: 0, overflow: 'hidden' }}>
              <div className="row between" style={{ alignItems: 'center', padding: '14px 18px', borderBottom: '1px solid var(--line)' }}>
                <div className="col" style={{ gap: 2, minWidth: 0 }}>
                  <Display style={{ fontSize: 19, lineHeight: 1 }}>{c.team}</Display>
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{c.seriesDisplay}</Mono>
                </div>
                {/* the YEARS shown large: it's the first thing anyone looks at in a contract */}
                {(c.fromYear ?? 0) > 0 && (
                  <div className="col" style={{ alignItems: 'flex-end', gap: 3, flex: 'none' }}>
                    <Display style={{ fontSize: 20, color: c.seasonsLeft <= 1 ? 'var(--gold)' : 'var(--text)' }}>
                      {c.fromYear}–{c.untilYear}
                    </Display>
                    <Mono style={{ fontSize: 9.5, color: c.seasonsLeft <= 1 ? 'var(--gold)' : 'var(--text-3)' }}>
                      {t('pd_seasons_left', { n: c.seasonsLeft, defaultValue: '{{n}} season(s) left' })}
                    </Mono>
                  </div>
                )}
              </div>
              <div style={{ padding: '14px 18px', display: 'grid', gridTemplateColumns: '1fr 168px', gap: 18, alignItems: 'center' }}>
                <div className="col" style={{ gap: 6, minWidth: 0 }}>
                  <span className="mono" style={{ fontSize: 9, letterSpacing: '.14em', color: 'var(--text-3)' }}>
                    {t('pd_obj_title', { defaultValue: 'AGREED OBJECTIVE' })}
                  </span>
                  <span style={{ fontSize: 14.5, fontWeight: 600 }}>
                    {c.objTargetPos > 0
                      ? t('pd_obj_full', { pos: c.objTargetPos, defaultValue: 'Finish P{{pos}} or better' })
                      : t('pd_obj_none', { defaultValue: 'No objective agreed' })}
                  </span>
                  {c.perRace > 0 && (
                    <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
                      {t('pd_per_race', { n: c.perRace.toLocaleString('en-US'), defaultValue: '₡ {{n}} per race' })}
                    </Mono>
                  )}
                </div>
                {juzgable && (
                  <div className="col" style={{ gap: 5, alignItems: 'flex-end' }} data-paddock-obj={cumple ? 'ok' : 'risk'}>
                    <span className="row gap8" style={{ alignItems: 'baseline' }}>
                      <Display style={{ fontSize: 30, color: col, lineHeight: 1 }}>P{c.currentPos}</Display>
                      <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>/ {t('pd_asks_pos', { pos: c.objTargetPos, defaultValue: 'asks P{{pos}}' })}</Mono>
                    </span>
                    <span className="mono" style={{ fontSize: 9.5, letterSpacing: '.08em', padding: '2px 8px', color: col, border: `1px solid color-mix(in oklab, ${col} 40%, transparent)` }}>
                      {cumple ? t('pd_on_target', { defaultValue: 'ON TARGET' }) : t('pd_at_risk', { defaultValue: 'AT RISK' })}
                    </span>
                  </div>
                )}
              </div>
              {/* the TEAMMATE and what it costs to leave. They sit together at the bottom because they're two sides
                  of the same question: who you're measured against inside your own team, and what you pay to exit.
                  The teammate row only exists where the team runs two cars; the price row, only if there's
                  something to break.
                  And now also the EXIT. The row is ALWAYS rendered: before it only appeared if there was a teammate or
                  something to pay, and a card with neither would be left with no way out. */}
              <div className="row" style={{ gap: 14, alignItems: 'center', flexWrap: 'wrap', padding: '9px 18px 11px', borderTop: '1px solid var(--line)' }}>
                  {!!c.teammate && (
                    <span className="row gap8" style={{ alignItems: 'baseline', minWidth: 0 }} data-paddock-teammate={c.teammate}>
                      <span className="mono" style={{ fontSize: 9.5, letterSpacing: '.1em', color: 'var(--text-3)' }}>
                        {t('pd_teammate', { defaultValue: 'TEAMMATE' })}
                      </span>
                      <span style={{ fontSize: 12.5 }}>{c.teammate}</span>
                      {duelo && (
                        <span className="mono" style={{ fontSize: 10.5, color: ganasElDuelo ? 'var(--green)' : 'var(--danger)' }}>
                          {ganasElDuelo
                            ? t('pd_ahead_of_mate', { pos: c.teammatePos, defaultValue: 'you lead, P{{pos}}' })
                            : t('pd_behind_mate', { pos: c.teammatePos, defaultValue: 'ahead of you, P{{pos}}' })}
                        </span>
                      )}
                      {/* the race-by-race HEAD-TO-HEAD. The table above says who's ahead TODAY; this says
                          how many times you've beaten them, which is what actually gets talked about in the garage. Same car, so
                          it's the one rival a bad result has no excuse against. This data had already been tracked
                          in the head-to-head record for a while: what was missing was knowing which one of them is yours. */}
                      {(() => {
                        const gw = c.teammateW ?? 0, gl = c.teammateL ?? 0;
                        if (gw + gl === 0) return null;   // no head-to-head to tell if your paths never crossed
                        return (
                          <span className="mono" data-paddock-mate-duel={`${gw}-${gl}`}
                            style={{ fontSize: 10.5, color: gw >= gl ? 'var(--green)' : 'var(--danger)' }}>
                            {t('pd_mate_duel', { w: gw, l: gl, defaultValue: '{{w}}-{{l}} against him' })}
                          </span>
                        );
                      })()}
                    </span>
                  )}
                  {(c.breakFee ?? 0) > 0 && (
                    <span className="mono" style={{ fontSize: 10, color: 'var(--text-3)', marginLeft: 'auto' }} data-paddock-breakfee={c.breakFee}>
                      {t('pd_break_fee', { n: (c.breakFee as number).toLocaleString('en-US'), defaultValue: 'Walking out costs ₡ {{n}}' })}
                    </span>
                  )}
                  {/* THE EXIT. The price is already visible above; this button doesn't break anything by itself —
                      it opens the confirmation, which is where the cost is stated and that there's no going back. */}
                  {confirmarRuptura === c.seriesId ? (
                    <div className="row" data-paddock-break-confirm={c.seriesId}
                         style={{ gap: 10, alignItems: 'center', flexWrap: 'wrap',
                                  marginLeft: (c.breakFee ?? 0) > 0 ? undefined : 'auto' }}>
                      <span style={{ fontSize: 12, color: 'var(--danger)', maxWidth: '46ch', lineHeight: 1.45 }}>
                        {/* the warning promises exactly what the engine DOES: the amount, that the team will remember it in their
                            future offers (a relationship mark -> team relationship), and your standing with sponsors (internal REP
                            -> tier). The REP number itself isn't quoted: that scale isn't shown on any
                            screen, and promising "10 reputation" against an invisible number is exactly
                            the kind of warning that reads as a lie. ELO isn't touched, so it isn't mentioned. */}
                        {t('pd_break_confirm', {
                            n: (c.breakFee ?? 0).toLocaleString('en-US'), team: c.team,
                            defaultValue: "Pay ₡ {{n}}. {{team}} won't forgive you, and your image with sponsors takes the hit. You lose the seat and there is no going back." })}
                      </span>
                      <Btn size="sm" disabled={busy !== ''} onClick={() => void rescindir(c)}
                           style={{ borderColor: 'var(--danger)', color: 'var(--danger)' }}>
                        {t('pd_break_do', { defaultValue: 'Terminate' })}
                      </Btn>
                      <Btn size="sm" variant="ghost" disabled={busy !== ''} onClick={() => setConfirmarRuptura(null)}>
                        {t('common.cancel')}
                      </Btn>
                    </div>
                  ) : (
                    <Btn size="sm" variant="ghost" data-paddock-break-open={c.seriesId}
                         style={{ marginLeft: (c.breakFee ?? 0) > 0 ? undefined : 'auto' }}
                         onClick={() => { setFallo(null); setConfirmarRuptura(c.seriesId); }}>
                      {t('pd_break_open', { defaultValue: 'Terminate contract' })}
                    </Btn>
                  )}
                  {/* the engine no longer had that contract: it's stated, instead of announcing a termination that never happened */}
                  {fallo?.key === 'break|' + c.seriesId && (
                    <span className="mono" data-paddock-reason={fallo.reason} style={{ fontSize: 11, color: 'var(--danger)' }}>
                      {t('pd_break_gone', { defaultValue: 'That contract was already gone.' })}
                    </span>
                  )}
                </div>
            </div>
            );
          })}
        </div>
      )}

      {/* DECISION WEEK has its own face. It isn't just another list: it's the moment everything
          resolves at once, and the two columns need to read at a glance — what you can
          still sign and what got away from you, naming exactly who took it. */}
      {data.phase === 'decision' && (
        <div className="row" style={{ gap: 20, alignItems: 'flex-start', flexWrap: 'wrap' }} data-paddock-decision="1">
          <div className="col grow" style={{ gap: 8, minWidth: 300 }}>
            <Mono style={{ fontSize: 10, letterSpacing: '.14em', color: 'var(--accent-text)' }}>
              {t('pd_offered_title', { defaultValue: 'ON THE TABLE' })}
            </Mono>
            {/* It's NOT "pick one". Holding multiple contracts is deliberate: you can sign everything you're offered,
                and the real limits are the calendar, your money, and exclusivity clauses — not some invented cap. That's why
                each one has its own button and none of them disappear when you sign another. */}
            <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>
              {t('pd_offered_hint', { defaultValue: 'You may sign more than one — your calendar, your money and exclusivity are the limits.' })}
            </Mono>
            {ofrecidas.length === 0 && (
              <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('pd_none_offered', { defaultValue: 'Nothing offered to you this time.' })}</Mono>
            )}
            {ofrecidas.map((s) => {
              const key = s.seriesId + '|' + s.team;
              return (
                <div key={key} className="panel col" data-paddock-offered={key} style={{ gap: 6, padding: '12px 14px', borderColor: 'color-mix(in oklab, var(--accent) 45%, var(--line))' }}>
                  <div className="row gap8" style={{ alignItems: 'baseline' }}>
                    <span style={{ fontWeight: 700, fontSize: 15 }}>{s.team}</span>
                    <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{s.seriesDisplay}</Mono>
                    <div className="grow" />
                    {/* 18/09 (Josh): how much time is left on the offered unsigned seat */}
                    {!s.signed && s.daysLeft != null && <ExpiryChip daysLeft={s.daysLeft} />}
                  </div>
                  <div className="row gap8" style={{ alignItems: 'center' }}>
                    {s.perRace > 0 && (
                      <Mono style={{ fontSize: 12, color: 'var(--text-2)' }}>
                        {t('pd_per_race', { n: s.perRace.toLocaleString('en-US'), defaultValue: '₡ {{n}} per race' })}
                      </Mono>
                    )}
                    <div className="grow" />
                    {s.signed ? (
                      <span className="mono" data-paddock-signed="1" style={{ fontSize: 10, color: 'var(--accent-text)' }}>{t('pd_signed', { defaultValue: 'SIGNED' })}</span>
                    ) : (
                      <Btn variant="primary" size="sm" disabled={busy === key} onClick={() => void sign(s)}>
                        {t('pd_sign', { defaultValue: 'Sign' })}
                      </Btn>
                    )}
                  </div>
                  {fallo?.key === key && (
                    <span className="mono" data-paddock-reason={fallo.reason} style={{ fontSize: 11, color: 'var(--danger)' }}>
                      {t(`pd_reason_${fallo.reason}`, { defaultValue: t('pd_reason_refused', { defaultValue: 'Could not sign.' }) })}
                    </span>
                  )}
                </div>
              );
            })}
          </div>

          <div className="col grow" style={{ gap: 8, minWidth: 300 }}>
            <Mono style={{ fontSize: 10, letterSpacing: '.14em', color: 'var(--danger)' }}>
              {t('pd_rejected_title', { defaultValue: 'GONE' })}
            </Mono>
            {descartadas.length === 0 && (
              <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('pd_none_rejected', { defaultValue: 'Nothing slipped away.' })}</Mono>
            )}
            {descartadas.map((s) => (
              <div key={s.seriesId + '|' + s.team} className="panel row" data-paddock-rejected={s.seriesId + '|' + s.team} style={{ gap: 10, padding: '10px 14px', alignItems: 'baseline', opacity: 0.8 }}>
                <span style={{ fontWeight: 700, fontSize: 14 }}>{s.team}</span>
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{s.seriesDisplay}</Mono>
                <div className="grow" />
                {/* the name of whoever took it is half of what this screen is showing: without it,
                    a "missed" seat is just a no with no story, and with it, it's a rival. */}
                {s.winnerName && (
                  <Mono style={{ fontSize: 11, color: 'var(--danger)' }}>
                    {t('pd_taken_by', { name: s.winnerName, defaultValue: 'taken by {{name}}' })}
                  </Mono>
                )}
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Player report (Windows scaled to 125%): "in the PADDOCK menu the rows are cut off". It wasn't
          a font or DPI issue: it was a SCROLLER NESTED INSIDE ANOTHER. This container was scrolling and, inside it, PaddockWorld
          was set to `grow` (flex:1 1 0) with its two columns set to overflow:auto. With flex-basis 0 and minHeight 0,
          its height was "whatever's left over" after rendering the seats in play — at 100% scale there was room to spare and it went unnoticed, and at
          125% (fewer CSS pixels of height) there was nothing left over: the columns ended up with near-zero height, text
          poked out above each row's border, and the internal scrollbars got painted on top of the rows.
          Now: this container does NOT scroll (it's the flex parent), the seats-in-play list gets its own scroll with
          a 40vh cap (so the world section is NEVER left with zero height), and PaddockWorld receives a REAL height. See also
          PaddockWorld's gridTemplateRows: without it, the grid's implicit row sized itself to its content. */}
      {data.phase !== 'decision' && sub === 'world' && (
      <div className="col grow" style={{ minHeight: 0, gap: 18, paddingBottom: 24 }} data-paddock-worldtab="1">
        {grupos.length === 0 && (
          // "there's no window yet" is NOT the same as "there are no seats", and getting this wrong frustrates the player:
          // during the season there's nothing to look at BECAUSE it isn't time yet, and that's information, not a bug.
          <Mono style={{ color: 'var(--text-3)', fontSize: 12 }}>
            {data.phase === 'season'
              ? t('pd_empty_season', { defaultValue: 'No seats in play — the window opens later in the year.' })
              : t('pd_empty', { defaultValue: 'No seats in play right now.' })}
          </Mono>
        )}

        {/* WHAT'S IN PLAY: natural height, but with its own scroll and a 40vh cap. The cap is what
            guarantees the world section (below) is ALWAYS left height to work with, at any Windows scale. */}
        <div className="col scroll" data-paddock-inplay="1"
             style={{ flex: '0 0 auto', maxHeight: '40vh', gap: 18, minHeight: 0, scrollbarGutter: 'stable' }}>
        {grupos.length > 0 && (
          <Mono style={{ fontSize: 10, letterSpacing: '.14em', color: 'var(--cyan)' }}>
            {t('pd_in_play_title', { defaultValue: 'IN PLAY' })}
          </Mono>
        )}
        {grupos.map((g) => (
          <div key={g.seriesId} className="col" style={{ gap: 8 }} data-paddock-series={g.seriesId}>
            <div className="row gap8" style={{ alignItems: 'baseline' }}>
              <Display style={{ fontSize: 19, lineHeight: 1 }}>{g.display.toUpperCase()}</Display>
              <Mono style={{ fontSize: 9, letterSpacing: '.12em', color: 'var(--text-3)' }}>
                {t('pd_tier', { n: g.level, defaultValue: 'TIER {{n}}' })}
              </Mono>
            </div>

            {g.seats.map((s) => {
              const sm = STATUS_META[s.status] ?? STATUS_META.unknown;
              const key = s.seriesId + '|' + s.team;
              return (
                <div
                  key={key}
                  className="panel row focusable"
                  data-paddock-seat={key}
                  role="button"
                  tabIndex={0}
                  onClick={() => setSheetKey(key)}
                  onKeyDown={(e) => { if (e.key === 'Enter') setSheetKey(key); }}
                  style={{ alignItems: 'center', gap: 12, padding: '10px 14px', flexWrap: 'wrap', cursor: 'pointer' }}
                >
                  <span style={{ fontWeight: 700, fontSize: 15, minWidth: 0 }}>{s.team}</span>

                  {/* "they reached out to you" is the actual news, which is why it comes BEFORE the status: TALKED-TO alone only says
                      the team spoke with someone. That they spoke to YOU is what changes your week. */}
                  {s.soundedYou && (
                    <span className="mono" data-paddock-sounded="1" style={{ fontSize: 9, color: 'var(--cyan)', background: 'color-mix(in oklab, var(--cyan) 22%, transparent)', borderRadius: 3, padding: '2px 6px' }}>
                      {t('pd_sounded_you', { defaultValue: 'THEY ASKED ABOUT YOU' })}
                    </span>
                  )}
                  {s.applied && (
                    <span className="mono" data-paddock-applied="1" style={{ fontSize: 9, color: 'var(--accent-text)', background: 'color-mix(in oklab, var(--accent) 20%, transparent)', borderRadius: 3, padding: '2px 6px' }}>
                      {t('pd_applied', { defaultValue: 'YOU APPLIED' })}
                    </span>
                  )}

                  <div className="grow" />

                  {/* the current occupant and UNTIL WHEN. This is what turns the paddock into a real tool: you know what's
                      opening up and when, so you can target a seat two seasons ahead of time. */}
                  {s.occupantName && (
                    <Mono style={{ fontSize: 11, color: 'var(--text-3)', whiteSpace: 'nowrap' }}>
                      {s.occupantName}
                      {s.expiryYear > 0 && ` · ${t('pd_until', { year: s.expiryYear, defaultValue: 'until {{year}}' })}`}
                    </Mono>
                  )}
                  {s.winnerName && (
                    <Mono style={{ fontSize: 11, color: 'var(--danger)', whiteSpace: 'nowrap' }}>
                      {t('pd_taken_by', { name: s.winnerName, defaultValue: 'taken by {{name}}' })}
                    </Mono>
                  )}

                  <span
                    className="mono"
                    data-paddock-status={s.status}
                    style={{ fontSize: 10, color: sm.color, background: `color-mix(in oklab, ${sm.color} 20%, transparent)`, borderRadius: 3, padding: '3px 8px', whiteSpace: 'nowrap' }}
                  >
                    {t(sm.key)}
                  </span>

                  {canApply(s) && (
                    <Btn size="sm" disabled={busy === key} onClick={(e) => { e.stopPropagation(); void apply(s); }}>
                      {t('pd_apply', { defaultValue: 'Apply' })}
                    </Btn>
                  )}
                </div>
              );
            })}
          </div>
        ))}
        </div>

        {/* THE WORLD, below what's in play. The design mockup literally labels this view
            "your offers on top · series -> teams": your own stuff first, then the whole paddock for
            planning. Without this part, outside the window this tab had nothing to show. */}
        <PaddockWorld client={client} rows={data.series ?? []} year={data.year} />
      </div>
      )}

      {/* the panel is resolved by KEY against live data: so, after applying, what you see inside is
          the new state, not a snapshot from when you opened it. */}
      {sheetKey && (() => {
        const sel = (data.seats ?? []).find((x) => x.seriesId + '|' + x.team === sheetKey);
        if (!sel) return null;
        return (
          <PaddockSeatSheet
            client={client}
            seat={sel}
            busy={busy === sheetKey}
            dataDir={dataDir}
            onClose={() => setSheetKey(null)}
            onApply={(x) => void apply(x)}
            onSign={(x) => void sign(x)}
          />
        );
      })()}
    </div>
  );
}
