// Settings > Performance (requested by players). Just one thing for now: how many cars
// show up on track at most.
//
// It's a MACHINE setting, not a race setting: it exists because your PC struggles to push forty cars, and that doesn't
// change depending on which save you open. That's why it lives in config.json and not in the save.
//
// It applies the next time you LAUNCH the game, not live: the engine reads the key at startup, and also
// when pre-generating the AI XML files (which happens right before launch, in a separate process).
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Display, Mono, Btn } from '../../ui/components';
import { GRID_CAP_CHOICES, GRID_CAP_OFF, readGridCap, writeGridCap } from '../../ui/gridCap';

export function PerformanceSection() {
  const { t } = useTranslation();
  const [cap, setCap] = useState<number | null>(null);   // null = still loading
  // tester suggestion (Aug 28): the "applies on launch" note in 9.5px text was easy to miss, and the cap seemed
  // to "not work". After CHANGING the value, the note goes up in tone (amber) and explicitly says that if AMS2
  // is open you need to restart it. At rest it stays the same small text as always.
  const [dirty, setDirty] = useState(false);

  useEffect(() => { void readGridCap().then(setCap); }, []);

  const pick = (n: number) => {
    if (n === cap) return;
    setCap(n); setDirty(true);       // optimistic: the control responds to the click, the write follows behind
    void writeGridCap(n).catch(() => { void readGridCap().then(setCap); });
  };

  return (
    <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
      <div className="col" style={{ gap: 2 }}>
        <Display style={{ fontSize: 19 }}>{t('perf.title')}</Display>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('perf.sub')}</Mono>
      </div>

      <div className="col" style={{ gap: 8, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 2 }}>
          <Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{t('perf.gridCap')}</Mono>
          <Mono style={{ fontSize: 10, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('perf.gridCapDesc')}</Mono>
        </div>
        <div className="row gap6" role="group" aria-label={t('perf.gridCap')}>
          {GRID_CAP_CHOICES.map((n) => (
            <Btn key={n} size="sm" variant={cap === n ? 'primary' : 'ghost'} disabled={cap === null}
                 aria-pressed={cap === n} data-testid={`grid-cap-${n}`} onClick={() => pick(n)}>
              {n === GRID_CAP_OFF ? t('perf.gridCapOff') : String(n)}
            </Btn>
          ))}
        </div>
        {/* It doesn't apply live, and saying so up front heads off the "this does nothing" reports: the engine reads this at
            game startup and when writing the AI XML files, both before the race actually starts. */}
        {dirty ? (
          <Mono data-testid="grid-cap-restart-note" style={{ fontSize: 11, color: 'var(--amber)', lineHeight: 1.5 }}>{t('perf.gridCapChanged')}</Mono>
        ) : (
          <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{t('perf.gridCapWhen')}</Mono>
        )}
      </div>
    </div>
  );
}
