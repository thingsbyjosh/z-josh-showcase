// Rollover / End-of-season: review + contract decision, ported from screen_rollover.cpp (source of truth). Takeover
// (no nav). Retirements/promotions/moves columns (engine stub today) + your contract panel (summary of
// live renewal + 3 options) + insurance invoice + restart notice + Confirm. NOTE: advancing the year is
// AUTOMATIC (continuous clock); this screen is just REVIEW. "New objectives" -> Negotiate; Renew/Decline -> cmd.rollover.confirm.
// Chrome via the engine's ro_* keys; reasons/from-to = {en,es} pairs. Confirm is a stub in the engine (for parity).
import { useState, useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, RolloverPayload, TimeAdvancePayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Kicker, Btn, Chip } from '../ui/components';
import { accentText } from '../ui/colors';
import { formatCreditsCr } from '../ui/format';
import { useScreenCrumb } from '../support/useScreenCrumb';
import { TimeAdvancePopup } from '../components/TimeAdvancePopup';

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

// "Go to next event of this championship" CTA: the ONLY championship that can be resolved on the
// Rollover screen is the one under review for renewal (renewSeries = the REAL series_id;
// yourClass is just a display name for the class, not usable as a seriesId). Kept as a separate component
// (instead of one more hook in Rollover) so that its gating query.time.preview only fires
// ONCE, once there's an actual seriesId to resolve (it only mounts if renewSeries isn't empty;
// there is no "filler" fetch with empty params while the rollover loads). Review 0.7.0: with resolveOnly,
// which only asks whether there is a target; the whole forecast was seconds of game thread for a boolean.
// onAdvanced: the advance may have moved the clock (and even crossed another year), so on closing the popup
// the screen that opened it must be refreshed — the CTA relies on query.rollover, and until now it kept
// the renewal from before the jump.
function RolloverJumpCta({ client, seriesId, onAdvanced }: { client: ApiClientLike; seriesId: string; onAdvanced: () => void }) {
  const { t } = useTranslation();
  const jumpQ = useQuery<TimeAdvancePayload>(client, 'query.time.preview', { seriesId, resolveOnly: true });
  const [showJump, setShowJump] = useState(false);
  return (
    <>
      <div className="row gap8" style={{ marginTop: 12, alignItems: 'center' }}>
        <Btn variant="ghost" disabled={jumpQ.data?.resolved !== true} onClick={() => setShowJump(true)}>
          {t('time.rolloverCta')}
        </Btn>
        {jumpQ.data && !jumpQ.data.resolved && (
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>
            {t('time.unreachable')}
          </Mono>
        )}
      </div>
      {showJump && (
        <TimeAdvancePopup
          client={client}
          params={{ seriesId }}
          onCancel={() => setShowJump(false)}
          onAdvanced={() => { setShowJump(false); onAdvanced(); }}
        />
      )}
    </>
  );
}

const RENEW_COLOR: Record<string, string> = { better: 'var(--green)', same: 'var(--text-2)', worse: 'var(--danger)', none: 'var(--danger)' };
const RENEW_KEY: Record<string, string> = { better: 'ro_renew_better', same: 'ro_renew_same', worse: 'ro_renew_worse', none: 'ro_renew_none' };
const CHOICES = ['renew', 'newgoals', 'decline'] as const;

export function Rollover({ client, onDone, onNegotiate, onDataSettled }: { client: ApiClientLike; onDone: () => void; onNegotiate: (offerId: string) => void; onDataSettled?: () => void }) {
  const { t, i18n } = useTranslation();
  const es = i18n.language === 'es';
  useScreenCrumb(client, 'season rollover');   // audit note, 07/20
  const q = useQuery<RolloverPayload>(client, 'query.rollover');
  const p = q.data;
  const [choice, setChoice] = useState<number>(-1);   // 1=none / 0=renew / 1=newgoals / 2=decline
  // T147 (5): same reason as in Settings -- this takeover's tour waited a FIXED margin before measuring
  // its anchors (data-tour="rollover.cols"/".contract"/".confirm"), and with a slow query.rollover it fired
  // lame. Notifies the shell ONCE when this query settles.
  const settledRef = useRef(false);
  useEffect(() => {
    if (!q.loading && !settledRef.current) { settledRef.current = true; onDataSettled?.(); }
  }, [q.loading]);   // eslint-disable-line react-hooks/exhaustive-deps

  if (q.loading && !p) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || !p) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;

  // 18/09 (Josh: “a screen where I have to tick your contract no matter what, when I don't have one”) —
  // there is only something to DECIDE if a renewal is on the table. With a contract and no renewal only the
  // contract is shown, and that's it; with neither contract nor renewal, not even the block. In those two cases you confirm without choosing.
  const hayContrato = p.hasRenewal || !!p.yourTeam;
  const hayQueElegir = p.hasRenewal;
  const confirm = () => {
    if (!hayQueElegir) { onDone(); return; }
    if (choice < 0) return;
    if (choice === 1 && p.renewOfferId) { onNegotiate(p.renewOfferId); return; }   // New objectives -> Negotiate
    void client.cmd('cmd.rollover.confirm', { choice: CHOICES[choice] }).then(() => onDone()).catch(() => onDone());
  };
  const renewColor = RENEW_COLOR[p.renewState] ?? 'var(--danger)';

  // 17/09: `total` is how many people the winter really moved; the list below is just the sample the
  // engine sends. It is shown next to the title because it IS the news: “a hundred-odd promotions”, not “eight”.
  const Col = ({ title, color, total, children }: { title: string; color: string; total?: number; children: React.ReactNode }) => (
    <div className="col" style={{ flex: 1, borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)', overflow: 'hidden', minWidth: 0 }}>
      <div className="row between" style={{ padding: '8px 14px', borderBottom: `2px solid ${color}`, alignItems: 'baseline' }}>
        <Kicker style={{ color }}>{title}</Kicker>
        {total ? <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{total}</Mono> : null}
      </div>
      <div className="scroll col" style={{ gap: 6, padding: '10px 14px', maxHeight: 220 }}>{children}</div>
    </div>
  );

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'auto' }}>
      <div className="row between" style={{ padding: '18px 30px 8px', alignItems: 'flex-start' }}>
        <div className="col" style={{ gap: 2 }}>
          <Display as="h1" style={{ fontSize: 30 }}>{t('ro_title')}</Display>
          <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('ro_sub')}</Mono>
        </div>
        <Chip kind="accent" style={{ fontSize: 10 }}>{t('ro_season')} → {t('ro_season')} {p.nextYear}</Chip>
      </div>

      {/* 3 columns */}
      <div className="row gap16" style={{ padding: '10px 30px', alignItems: 'stretch' }} data-tour="rollover.cols">
        <Col title={t('ro_retirements')} color="#ff4d4d" total={p.retiresTotal}>
          {p.retirements.length === 0 && <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>—</Mono>}
          {p.retirements.map((r, i) => (
            <div key={i} className="row between" style={{ alignItems: 'center' }}>
              <div className="col" style={{ minWidth: 0 }}><span style={{ fontWeight: 700, fontSize: 13 }}>{r.name}</span><Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{/* 18/09: age only if known. `DriverAge` returns -1 when there is no birth year, and the template painted it as is: “-1 yrs”. The engine no longer sends the sentinel (it travels 0), but this is the last gate and costs nothing. */}{[r.team, r.age > 0 ? `${r.age} ${t('ro_years')}` : ''].filter(Boolean).join(' · ')}</Mono></div>
              <Chip kind="default" style={{ fontSize: 8 }}>{es ? r.reasonEs : r.reasonEn}</Chip>
            </div>
          ))}
          {p.retiresTotal > p.retirements.length && (
            <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>+{p.retiresTotal - p.retirements.length}</Mono>
          )}
        </Col>
        <Col title={t('ro_promotions')} color="#19d27c" total={p.promosTotal}>
          {p.promotions.length === 0 && <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>—</Mono>}
          {p.promotions.map((r, i) => (
            <div key={i} className="row between" style={{ alignItems: 'center' }}>
              <span style={{ fontWeight: 600, fontSize: 13 }}>{r.name}</span>
              <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{es ? r.fromEs : r.fromEn} → {es ? r.toEs : r.toEn}</Mono>
            </div>
          ))}
          {p.promosTotal > p.promotions.length && (
            <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>+{p.promosTotal - p.promotions.length}</Mono>
          )}
        </Col>
        <Col title={t('ro_market')} color="#5ec8ff" total={p.movesTotal}>
          {p.marketMoves.length === 0 && <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>—</Mono>}
          {p.marketMoves.map((r, i) => (
            <div key={i} className="col" style={{ gap: 1 }}>
              <span style={{ fontWeight: 600, fontSize: 13, color: accentText(r.color) }}>{r.driver}</span>
              <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{r.from ? `${r.from} → ${r.to}` : `→ ${r.to}`}</Mono>
            </div>
          ))}
          {(p.movesTotal ?? 0) > p.marketMoves.length && (
            <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>+{(p.movesTotal ?? 0) - p.marketMoves.length}</Mono>
          )}
        </Col>
      </div>

      {/* Your contract */}
      {hayContrato && (
      <div style={{ margin: '12px 30px', padding: '16px 18px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }} data-tour="rollover.contract">
        <div className="row between" style={{ alignItems: 'center' }}>
          <Kicker style={{ color: 'var(--accent-text)' }}>{t('ro_your_contract')}</Kicker>
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{p.yourTeam} · {p.yourClass}</Mono>
        </div>
        <div className="row between" style={{ alignItems: 'center', marginTop: 8 }}>
          <span style={{ fontSize: 14, fontWeight: 600, color: renewColor }}>{t(RENEW_KEY[p.renewState] ?? 'ro_renew_none')}</span>
          {p.hasRenewal && p.renewPerRace > 0 && <span style={{ color: 'var(--gold)', fontWeight: 700, fontSize: 13 }}>+{formatCreditsCr(p.renewPerRace)}/{t('market.raceSingular')}</span>}
        </div>
        {hayQueElegir && (
        <div className="row gap8" style={{ marginTop: 12 }}>
          {CHOICES.map((c, i) => {
            const disabled = i === 0 && !p.hasRenewal;
            const sel = choice === i;
            const label = i === 0 ? t('ro_renew') : i === 1 ? t('ro_newgoals') : t('ro_decline');
            return (
              <button key={c} disabled={disabled} onClick={() => setChoice(i)} className="focusable" style={{
                flex: 1, padding: '10px 0', borderRadius: 3, fontSize: 12.5, cursor: disabled ? 'not-allowed' : 'pointer', opacity: disabled ? 0.4 : 1,
                background: sel ? 'var(--accent)' : (i === 0 ? 'color-mix(in oklab, var(--accent) 12%, var(--surface))' : 'var(--surface)'),
                color: sel ? '#fff' : 'var(--text-2)', border: sel ? '1px solid var(--accent)' : '1px solid var(--line)',
              }}>{sel ? '✓ ' : ''}{label}</button>
            );
          })}
        </div>
        )}

        {p.renewSeries && <RolloverJumpCta client={client} seriesId={p.renewSeries} onAdvanced={() => { void q.refetch(); }} />}
      </div>
      )}

      {/* Insurance invoice (conditional) */}
      {p.insurancePremium > 0 && (
        <div style={{ margin: '0 30px 12px', padding: '12px 16px', borderRadius: 0, border: '1px solid color-mix(in oklab, var(--danger) 45%, transparent)', background: 'color-mix(in oklab, var(--danger) 8%, var(--surface))' }}>
          <div className="row between" style={{ alignItems: 'center' }}>
            <div className="col"><Mono style={{ fontSize: 11, fontWeight: 700, color: 'var(--danger)' }}>✉ {t('ro_insurance_title')}</Mono><Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('ro_insurance_body')}</Mono></div>
            <span style={{ color: 'var(--danger)', fontWeight: 700, fontSize: 14 }}>−{formatCreditsCr(p.insurancePremium)}</span>
          </div>
        </div>
      )}

      {/* Confirm. The "restart the game" notice was REMOVED from here: it used to fire ALWAYS, unconditionally,
          and warning always is the same as never warning -- it fired even on a rollover where not a single seat moved.
          Now the shell's own bar handles it, and only shows up when the engine says the on-disk grids
          no longer match the world state (`gridsStale`), and it offers to restart for you. */}
      <div style={{ margin: '0 30px 20px', padding: '14px 18px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)', display: 'flex', justifyContent: 'flex-end', alignItems: 'center', gap: 16 }} data-tour="rollover.confirm">
        <Btn variant="primary" size="lg" style={{ flex: 'none' }} disabled={hayQueElegir && choice < 0} onClick={confirm}>{t('ro_confirm')} →</Btn>
      </div>
    </div>
  );
}
