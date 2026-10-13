// subscreen: "Race configuration" -- groups EVERYTHING that decides how each
// race you launch gets configured: per-field Auto/Inherited, the automatic realism multipliers
// and the safety car. Settings keeps a single entry point that opens this view (local state on the
// screen, no router); the settings.gameplay tutorial anchor lives on that entry point.
import { useTranslation } from 'react-i18next';
import type { SettingsInherit } from '../../api/contracts';
import { Display, Mono, Btn } from '../../ui/components';
import { WeatherPicker } from './WeatherPicker';

// AUTO/Inherited fields (same tokens as cmd.settings.setInherit / the engine's kInh* bits).
// WEATHER is deliberately not here. It used to have two controls for the same decision (this row and the
// mode in Settings > Weather), and the bit would silently win: with Inherited set, choosing Random or Selection
// did nothing. The control that survives is the mode, which also respects the authored weather of a historical round.
const INHERIT_FIELDS: { key: keyof SettingsInherit; labelKey: string; descKey: string }[] = [
  { key: 'hour',     labelKey: 'inherit.hour',     descKey: 'inherit.hourDesc' },
  { key: 'timeProg', labelKey: 'inherit.timeProg', descKey: 'inherit.timeProgDesc' },
  { key: 'start',    labelKey: 'inherit.start',    descKey: 'inherit.startDesc' },
  { key: 'pit',      labelKey: 'inherit.pit',      descKey: 'inherit.pitDesc' },
  { key: 'ai',       labelKey: 'inherit.ai',       descKey: 'inherit.aiDesc' },
  { key: 'sessions', labelKey: 'inherit.sessions', descKey: 'inherit.sessionsDesc' },
];

function Section({ first, title, sub, children }: { first?: boolean; title: string; sub?: string; children: React.ReactNode }) {
  return (
    <div className="col" style={first ? { gap: 12 } : { gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
      <div className="col" style={{ gap: 2 }}>
        <Display style={{ fontSize: 19 }}>{title}</Display>
        {sub && <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{sub}</Mono>}
      </div>
      {children}
    </div>
  );
}

function Row({ label, desc, children }: { label: string; desc: string; children: React.ReactNode }) {
  return (
    <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
      <div className="col" style={{ gap: 4, maxWidth: 560 }}>
        <span className="display" style={{ fontSize: 15 }}>{label}</span>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{desc}</Mono>
      </div>
      <div className="row" role="group" aria-label={label} style={{ flexShrink: 0, gap: 2 }}>{children}</div>
    </div>
  );
}

export function RaceSetupView({ gp, setScale, fcyMode, setFcy, weather, setWeather, inherit, setInherit, onBack }: {
  gp: { tyre: boolean; fuel: boolean };
  setScale: (tyre: boolean, fuel: boolean) => void;
  fcyMode: number;                       // 0=No, 1=Yes, 2=Inherited (same philosophy as inherit)
  setFcy: (mode: number) => void;
  weather?: { mode: number; weights: Record<string, number>; defaults: Record<string, number> };
  setWeather: (mode: number, weights?: Record<string, number>) => void;
  inherit?: SettingsInherit;             // absent = old DLL -> the Auto/Inherited block isn't shown
  setInherit: (field: keyof SettingsInherit, value: boolean) => void;
  onBack: () => void;
}) {
  const { t } = useTranslation();
  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div style={{ padding: '18px 30px 8px' }}>
        <Btn variant="ghost" size="sm" onClick={onBack} style={{ marginBottom: 8 }}>← {t('settings_tab')}</Btn>
        <Display as="h1" style={{ fontSize: 30 }}>{t('raceSetup.title')}</Display>
        <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('raceSetup.sub')}</Mono>
      </div>
      <div className="scroll" style={{ flex: 1, padding: '12px 30px 24px', minHeight: 0 }}>
        {inherit && (
          <Section first title={t('inherit.title')} sub={t('inherit.sub')}>
            {INHERIT_FIELDS.map(({ key, labelKey, descKey }) => {
              const inh = inherit[key];
              return (
                <Row key={key} label={t(labelKey)} desc={t(descKey)}>
                  <Btn variant={!inh ? 'primary' : 'ghost'} size="sm" aria-pressed={!inh} onClick={() => inh && setInherit(key, false)}>{t('inherit.auto')}</Btn>
                  <Btn variant={inh ? 'primary' : 'ghost'} size="sm" aria-pressed={inh} onClick={() => !inh && setInherit(key, true)}>{t('inherit.inherited')}</Btn>
                </Row>
              );
            })}
            <Mono style={{ fontSize: 10, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('inherit.note')}</Mono>
          </Section>
        )}
        {/* automatic realism multipliers (wear / fuel consumption) by race length */}
        <Section first={!inherit} title={t('gpscale.title')} sub={t('gpscale.sub')}>
          {([['tyre', gp.tyre, 'gpscale.tyre', 'gpscale.tyreDesc'], ['fuel', gp.fuel, 'gpscale.fuel', 'gpscale.fuelDesc']] as const).map(([key, on, lk, dk]) => (
            <Row key={key} label={t(lk)} desc={t(dk)}>
              <Btn variant={on ? 'primary' : 'ghost'} size="sm" aria-pressed={on} onClick={() => !on && setScale(key === 'tyre' ? true : gp.tyre, key === 'fuel' ? true : gp.fuel)}>{t('gpscale.on')}</Btn>
              {/* (31/08): here "No" doesn't exist -- wear can't be turned off from the mod. The two
                  states are scale by distance (On) or touch nothing, which is this screen's INHERITED:
                  your Custom Race's global setting takes over. Same vocabulary as the rows above. */}
              <Btn variant={!on ? 'primary' : 'ghost'} size="sm" aria-pressed={!on} onClick={() => on && setScale(key === 'tyre' ? false : gp.tyre, key === 'fuel' ? false : gp.fuel)}>{t('inherit.inherited')}</Btn>
            </Row>
          ))}
          {/* FCY/safety car -- tri-state Yes/No/Inherited, like the rest of the screen.
              Inherited = the mod doesn't write the rule and whatever you already have in your Custom Race wins (the old binary
              "No" used to read as inherited without actually being so). */}
          <Row label={t('fcy.label')} desc={t('fcy.desc')}>
            {([[1, 'gpscale.on'], [0, 'gpscale.off'], [2, 'inherit.inherited']] as const).map(([m, lk]) => (
              <Btn key={m} variant={fcyMode === m ? 'primary' : 'ghost'} size="sm" aria-pressed={fcyMode === m}
                   onClick={() => fcyMode !== m && setFcy(m)}>{t(lk)}</Btn>
            ))}
          </Row>
        </Section>
        {/* Race weather. Three modes; only the third one reads the selection. */}
        {weather && (
          <Section title={t('wx.title')} sub={t('wx.sub')}>
            <Row label={t('wx.mode')} desc={t('wx.modeDesc')}>
              {([0, 1, 2] as const).map((m) => (
                <Btn key={m} variant={weather.mode === m ? 'primary' : 'ghost'} size="sm"
                     aria-pressed={weather.mode === m}
                     onClick={() => weather.mode !== m && setWeather(m)}>{t(`wx.mode${m}`)}</Btn>
              ))}
            </Row>
            {weather.mode === 2 && (
              <div className="col" style={{ gap: 10, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
                <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('wx.pickDesc')}</Mono>
                <WeatherPicker weights={weather.weights} defaults={weather.defaults}
                               onChange={(next) => setWeather(2, next)} />
              </div>
            )}
            <Mono style={{ fontSize: 10, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('wx.note')}</Mono>
          </Section>
        )}
      </div>
    </div>
  );
}
