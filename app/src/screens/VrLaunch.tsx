// Race-LAUNCH screen with the headset on. It's the in-app replacement for the two overlay notices
// the mod draws in flat mode, which never reach the headset in VR (they're drawn over the desktop swapchain):
// the opaque "Setting up..." cover and the "all set, hit Start" pill.
//
// The TEXT is the SAME as the engine's (loading_configuring / tip_dont_touch / start_hint*): the engine's i18n bundle
// is merged into the app's (see i18n/index.ts), so flat players and headset players read
// literally the same sentence, from a single source. Rewriting them here would have meant a second copy
// that drifts out of sync the first time either one gets edited.
import { useTranslation } from 'react-i18next';
import type { ApiClientLike } from '../api/contracts';
import type { VrRaceLaunch } from '../vr/useVrRacePanel';
import { Display, Kicker, Mono } from '../ui/components';
import { BrandLogo } from '../ui/BrandLogo';
import { RaceConfigPanel } from '../ui/RaceConfigPanel';

export function VrLaunch({ client, launch, onBack }: { client: ApiClientLike; launch: VrRaceLaunch; onBack: () => void }) {
  const { t } = useTranslation();
  // same priority order as the monitor pill in the native overlay. Shared Memory being off wins
  // over "some value didn't land": that race won't be recorded at all, which outranks a mere config detail.
  const shmOff = launch.shmOff;
  const warn = !shmOff && launch.failN > 0;
  const alert = shmOff || warn;

  return (
    <div className="col center grow" style={{ height: '100%', gap: 16, padding: 30, textAlign: 'center', overflowY: 'auto' }}>
      <BrandLogo height={48} style={{ marginBottom: 6 }} />

      {launch.stage === 'configuring' && (
        <>
          <Display style={{ fontSize: 30, color: 'var(--accent)' }}>{t('loading_configuring')}</Display>
          <div className="spinner" aria-hidden="true" />
          <Mono style={{ fontSize: 13, color: 'var(--text-2)', maxWidth: 460 }}>{t('tip_dont_touch')}</Mono>
          {/* The SAME thing the flat-mode cover lists: what the race is being set up with. */}
          <RaceConfigPanel client={client} />
        </>
      )}

      {launch.stage === 'ready' && (
        <>
          <Display style={{ fontSize: 34, color: alert ? 'var(--amber)' : 'var(--accent)' }}>
            {t(shmOff ? 'start_hint_shm_kicker' : warn ? 'start_hint_warn_kicker' : 'start_hint_kicker')}
          </Display>
          <Mono style={{ fontSize: 14, color: 'var(--text)', maxWidth: 480 }}>
            {t(shmOff ? 'start_hint_shm' : warn ? 'start_hint_warn' : 'start_hint')}
          </Mono>
          {shmOff && <Mono style={{ fontSize: 12.5, color: 'var(--amber)', maxWidth: 480 }}>{t('start_hint_shm_fix')}</Mono>}
          {warn && launch.failGroups && (
            <Mono style={{ fontSize: 12.5, color: 'var(--amber)', maxWidth: 480 }}>{launch.failGroups}</Mono>
          )}
        </>
      )}

      {launch.stage === 'waiting' && (
        <>
          <Display style={{ fontSize: 30, color: 'var(--accent)' }}>{t('race.launchedTitle')}</Display>
          {/* VR-specific copy: in flat mode the notice says "switch to the game", and with a headset on there's nowhere to switch to. */}
          <Mono style={{ fontSize: 13, color: 'var(--text-2)', maxWidth: 460 }}>{t('race.launchedBodyVr')}</Mono>
          <Kicker style={{ color: 'var(--text-3)' }}>{t('race.waitingResult')}</Kicker>
          <button className="btn btn-ghost" data-gamepad-back onClick={onBack}>{t('common.back')}</button>
        </>
      )}
    </div>
  );
}
