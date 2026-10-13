// 13/09/2026 — progress window for "Regenerate assets".
//
// Why it exists: regenerating extracts ~9.5 GB of the game's car photos and then converts them, and before,
// Settings only painted one line of text DURING THE CONVERSION. The extraction, which is by far the
// slowest part (several minutes), showed nothing: grey button and silence. A player reported it just like that: "nothing
// tells you how long it takes or how much is left".
//
// There are four steps in a row and the backend already sends progress for all of them (provision.rs):
//   1 extract car photos · 2 convert them · 3 extract circuit maps · 4 convert them
// The EXTRACTION only knows how many files it has done, not how many are left (PCarsTools does not say beforehand), so
// there the counter is shown and a warning that it is the slow part. No invented bar is drawn: a bar that
// gets stuck at 90% for three minutes is worse than an honest counter. The CONVERSION does bring a total and
// gets a real bar.
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Display, Mono, Btn } from './components';
import type { ProvisionProgress } from '../shell/bridge';

export type RegenStatus = 'running' | 'cancelling' | 'done' | 'cancelled' | 'error';

export function stepOf(p: ProvisionProgress | null): 1 | 2 | 3 | 4 {
  if (!p) return 1;
  if (p.phase === 'extract') return p.pak === 'GUITRACKMAPS' ? 3 : 1;
  if (p.phase === 'convert') return p.pak === 'trackmaps' ? 4 : 2;
  return 4;
}

const STEP_KEY: Record<1 | 2 | 3 | 4, string> = {
  1: 'assets.stepExtractVeh', 2: 'assets.stepConvertVeh', 3: 'assets.stepExtractTrk', 4: 'assets.stepConvertTrk',
};

function fmtElapsed(s: number): string {
  const m = Math.floor(s / 60);
  return `${m}:${String(s % 60).padStart(2, '0')}`;
}

export function RegenerateAssetsDialog(props: {
  status: RegenStatus;
  progress: ProvisionProgress | null;
  summary: string;
  onCancel: () => void;
  onClose: () => void;
}) {
  const { t, i18n } = useTranslation();
  const [elapsed, setElapsed] = useState(0);
  const active = props.status === 'running' || props.status === 'cancelling';

  useEffect(() => {
    if (!active) return;
    const iv = setInterval(() => setElapsed((n) => n + 1), 1000);
    return () => clearInterval(iv);
  }, [active]);

  const p = props.progress;
  const step = stepOf(p);
  const nf = (n: number) => n.toLocaleString(i18n.language);
  const extracting = p?.phase === 'extract' || (!p && active);
  const pct = p && p.phase === 'convert' && p.total > 0 ? Math.min(100, Math.round((p.done / p.total) * 100)) : 0;

  return (
    <div style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.82)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 80 }}>
      <div className="panel col" role="alertdialog" aria-modal="true" aria-label={t('assets.dlgTitle')}
           style={{ width: 520, padding: '26px 28px', gap: 16, border: '1px solid var(--line)' }}>
        <Display style={{ fontSize: 22 }}>{t('assets.dlgTitle')}</Display>

        {active && (
          <>
            <div className="col" style={{ gap: 6 }}>
              <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('assets.step', { n: step, total: 4 })}</Mono>
              <Mono style={{ fontSize: 13, color: 'var(--text)' }}>{t(STEP_KEY[step])}</Mono>
            </div>

            {extracting ? (
              <div className="col" style={{ gap: 6 }}>
                <Mono style={{ fontSize: 13, color: 'var(--text-2)' }}>{t('assets.extracted', { n: nf(p?.done ?? 0) })}</Mono>
                <Mono style={{ fontSize: 11, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('assets.slowHint')}</Mono>
              </div>
            ) : (
              <div className="col" style={{ gap: 6 }}>
                <div style={{ height: 6, background: 'var(--bg-2)', border: '1px solid var(--line)' }}>
                  <div style={{ height: '100%', width: `${pct}%`, background: 'var(--amber)', transition: 'width .3s' }} />
                </div>
                <Mono style={{ fontSize: 12, color: 'var(--text-2)' }}>
                  {t('assets.converted', { done: nf(p?.done ?? 0), total: nf(p?.total ?? 0) })}  ·  {pct}%
                </Mono>
              </div>
            )}

            <div className="row between" style={{ alignItems: 'center' }}>
              <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('assets.elapsed', { t: fmtElapsed(elapsed) })}</Mono>
              <Btn variant="ghost" size="sm" disabled={props.status === 'cancelling'} onClick={props.onCancel}>
                {props.status === 'cancelling' ? t('assets.cancelling') : t('assets.cancel')}
              </Btn>
            </div>
          </>
        )}

        {!active && (
          <>
            <Mono style={{ fontSize: 12, lineHeight: 1.5, color: props.status === 'error' ? 'var(--danger)' : 'var(--text-2)' }}>
              {props.summary}
            </Mono>
            <div className="row" style={{ justifyContent: 'flex-end' }}>
              <Btn size="sm" onClick={props.onClose}>{t('assets.close')}</Btn>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
