// Calibration intro: the FIRST thing a newly created profile sees, right after identity setup. No tabs or
// menus — just this. Instead of a dry "calibration race" label, your MANAGER speaks: an opportunity has come up
// in a class (Kingsmere Uma713) touring the UK, a turning point in your career. In secondary text: it's a
// calibration run, after which Adaptive difficulty becomes the default (changeable in Settings). Class/country are i18n keys
// (calib_class/calib_country) so they can be swapped easily later (the actual MECHANICS of the race live in the engine's calibration constants).
// The CTA launches the calibration race (cmd.calendar.launchCalibration) and enters the race flow (pre-race screen locked).
//
// THE PLAYER CHOOSES THIS RACE'S DIFFICULTY LEVEL. The calibration run always used seed 80, and that
// made it the worst-rated experience measured in the whole mod: of 162 real measurements from the first
// data batch, 98 got a vote and their average rating was 1.68 out of 5, the lowest in the entire corpus. It's also the very first
// race everyone who installs the mod plays. The slider spans AMS2's own real slider range (70..120) with
// 80 marked as recommended, and it's persisted via cmd.settings.setSeed (the engine only accepts it before
// calibration). The warning against raising it isn't just politeness: the measurement is your BEST lap against a
// fixed sample count, so raising the seed doesn't distort the reading by itself, but it puts you in more traffic and
// then your best lap gets worse, leaving the calibration reading you LOW for the rest of the season.
import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, SettingsPayload } from '../api/contracts';
import { Display, Mono, Kicker, Btn } from '../ui/components';
import { useScreenCrumb } from '../support/useScreenCrumb';

const SEED_MIN = 70, SEED_MAX = 120, SEED_RECO = 80;

export function Calibration({ client, onEnterPrerace, onSkip }: { client: ApiClientLike; onEnterPrerace: () => void; onSkip?: () => void }) {
  const { t } = useTranslation();
  useScreenCrumb(client, 'calibration');   // audit note, 07/20
  const [busy, setBusy] = useState(false);
  const [seed, setSeed] = useState(SEED_RECO);
  const sent = useRef(SEED_RECO);          // last value confirmed to the engine: avoids resending the same value
  const vars = { class: t('calib_class'), country: t('calib_country') };

  // If the player had already picked a level and comes back to this screen, it's restored. An older engine
  // build doesn't send an aiSeed: the recommended value is kept and the control still works (setSeed replies with an error but doesn't break).
  useEffect(() => {
    let alive = true;
    void client.query<SettingsPayload>('query.settings')
      .then((s) => {
        if (!alive || typeof s?.aiSeed !== 'number') return;
        const v = Math.min(SEED_MAX, Math.max(SEED_MIN, s.aiSeed));
        setSeed(v); sent.current = v;
      })
      .catch(() => { /* no adjustments: keeps the recommended value */ });
    return () => { alive = false; };
  }, [client]);

  // Sent on RELEASE, not on every pixel of the drag: the engine persists the save on every call.
  const commit = (v: number): Promise<unknown> => {
    if (v === sent.current) return Promise.resolve(null);
    sent.current = v;
    return client.cmd('cmd.settings.setSeed', { seed: v }).catch(() => null);
  };

  const start = () => {
    if (busy) return;
    setBusy(true);
    // The final commit ALWAYS happens before launch: if the player drags and hits the CTA without releasing
    // outside the control, the value shown and the value that actually runs must be the same.
    void commit(seed)
      .then(() => client.cmd<{ ok: boolean; launched: boolean }>('cmd.calendar.launchCalibration', {}))
      .then((r) => { if (r?.launched) onEnterPrerace(); else setBusy(false); })
      .catch(() => setBusy(false));
  };

  return (
    <div style={{ height: '100%', position: 'relative', display: 'flex', flexDirection: 'column' }}>
      <div className="app-bg" />
      <div className="col center grow" style={{ height: '100%', position: 'relative', zIndex: 1, padding: 40 }}>
        <div className="col" style={{ maxWidth: 580, gap: 18, textAlign: 'center', alignItems: 'center' }}>
          <Kicker style={{ color: 'var(--accent-text)', letterSpacing: 2 }}>{t('calib_kicker')}</Kicker>
          <Display style={{ fontSize: 42, lineHeight: 1.04 }}>{t('calib_epic_title')}</Display>
          <div style={{ fontSize: 16, lineHeight: 1.7, color: 'var(--text-2)', maxWidth: 540 }}>{t('calib_epic_body', vars)}</div>
          <Mono style={{ fontSize: 11.5, lineHeight: 1.65, color: 'var(--text-3)', maxWidth: 500 }}>{t('calib_epic_note')}</Mono>

          {/* Visual weight to match the CTA: same width, placed above it, with the number shown large. */}
          <div className="col" style={{ width: '100%', maxWidth: 420, gap: 8, marginTop: 10, alignItems: 'stretch' }}>
            <div className="row" style={{ justifyContent: 'space-between', alignItems: 'baseline', gap: 12 }}>
              <Kicker style={{ letterSpacing: 1.4 }}>{t('calib_seed_label')}</Kicker>
              <div className="row" style={{ gap: 8, alignItems: 'baseline' }}>
                <Display style={{ fontSize: 30, lineHeight: 1, color: seed > SEED_RECO ? 'var(--warn, var(--accent-text))' : 'var(--text-1)' }}>
                  {seed}
                </Display>
                {seed === SEED_RECO && (
                  <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('calib_seed_reco')}</Mono>
                )}
              </div>
            </div>
            <input
              type="range" min={SEED_MIN} max={SEED_MAX} step={1} value={seed}
              disabled={busy}
              aria-label={t('calib_seed_label')}
              onChange={(e) => setSeed(Number(e.currentTarget.value))}
              onPointerUp={() => void commit(seed)}
              onKeyUp={() => void commit(seed)}
              onBlur={() => void commit(seed)}
              style={{ width: '100%', accentColor: 'var(--accent)', cursor: busy ? 'default' : 'pointer' }}
            />
            <div className="row" style={{ justifyContent: 'space-between' }}>
              <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{SEED_MIN}</Mono>
              {/* marker for the recommended value, placed at its real position on the track */}
              <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', flex: 1, textAlign: 'left', paddingLeft: `calc(${((SEED_RECO - SEED_MIN) / (SEED_MAX - SEED_MIN)) * 100}% - 14px)` }}>
                {SEED_RECO}
              </Mono>
              <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{SEED_MAX}</Mono>
            </div>
            <Mono style={{ fontSize: 11, lineHeight: 1.6, color: 'var(--text-3)', textAlign: 'left' }}>{t('calib_seed_hint')}</Mono>
          </div>

          <Btn variant="primary" size="lg" style={{ minWidth: 240, marginTop: 4 }} disabled={busy} onClick={start}>
            {busy ? t('common.loading') : `${t('calib_epic_cta')} →`}
          </Btn>
          {/* DISCREET skip (small link under the CTA) — leaves you on the default seed without measuring. */}
          {onSkip && (
            <button type="button" disabled={busy} onClick={onSkip}
              style={{ background: 'none', border: 'none', cursor: 'pointer', padding: 4, marginTop: 2, fontSize: 11, color: 'var(--text-3)', textDecoration: 'underline', textUnderlineOffset: 3 }}>
              {t('calib_skip')}
            </button>
          )}
        </div>
      </div>
    </div>
  );
}
