// consent is asked ONCE, the first time
// the Play button is seen. This is where it says exactly what gets sent, what doesn't, and what it's used for; from then on it's
// changed from Settings. Any answer (yes or no) closes the question for good, so
// anyone who says no is never asked again. Escape = "don't share" (an explicit answer, not a loophole).
import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Display, Mono, Btn } from './components';

export function TelemetryConsent({ onChoose }: { onChoose: (accept: boolean) => void }) {
  const { t } = useTranslation();
  useEffect(() => {
    const h = (e: KeyboardEvent) => { if (e.key === 'Escape') onChoose(false); };
    window.addEventListener('keydown', h);
    return () => window.removeEventListener('keydown', h);
  }, [onChoose]);

  const sent = ['class', 'config', 'result', 'answer'] as const;

  return (
    <div style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.82)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 60 }}>
      <div className="panel" role="dialog" aria-modal="true" aria-label={t('telemetry.title')}
           style={{ width: 560, maxHeight: '86vh', overflowY: 'auto', padding: '26px 28px', display: 'flex', flexDirection: 'column', gap: 14 }}>
        <div className="col" style={{ gap: 4 }}>
          <Display style={{ fontSize: 24 }}>{t('telemetry.title')}</Display>
          <Mono style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.6 }}>{t('telemetry.why')}</Mono>
        </div>

        <div className="col" style={{ gap: 6, padding: '12px 14px', background: 'var(--bg-2)', border: '1px solid var(--line)' }}>
          <Mono style={{ fontSize: 10, letterSpacing: '.1em', textTransform: 'uppercase', color: 'var(--text-3)' }}>{t('telemetry.sentTitle')}</Mono>
          {sent.map((k) => (
            <Mono key={k} style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.5 }}>· {t(`telemetry.sent.${k}`)}</Mono>
          ))}
        </div>

        <div className="col" style={{ gap: 6, padding: '12px 14px', background: 'var(--bg-2)', border: '1px solid color-mix(in oklab, var(--green) 35%, var(--line))' }}>
          <Mono style={{ fontSize: 10, letterSpacing: '.1em', textTransform: 'uppercase', color: 'var(--green)' }}>{t('telemetry.notSentTitle')}</Mono>
          <Mono style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.5 }}>{t('telemetry.notSent')}</Mono>
        </div>

        <Mono style={{ fontSize: 11, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('telemetry.changeLater')}</Mono>

        <div className="row gap8" style={{ marginTop: 4 }}>
          <Btn variant="primary" onClick={() => onChoose(true)} style={{ flex: 1 }}>{t('telemetry.accept')}</Btn>
          <Btn variant="ghost" autoFocus onClick={() => onChoose(false)} style={{ flex: 1 }}>{t('telemetry.decline')}</Btn>
        </div>
      </div>
    </div>
  );
}
