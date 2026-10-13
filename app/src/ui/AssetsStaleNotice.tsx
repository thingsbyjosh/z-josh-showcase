// 12/09/2026 — stale-assets warning.
//
// Car photos are NOT read from the game: they come from its DDS files, are converted to PNG and live in the data
// root. When AMS2 adds a car (e.g. the Stonewood Racing 9595 of 1.6.9.95), the new one is left WITHOUT a
// photo until the user reruns the provisioning — and until today nothing told them: the full
// generation only runs in the first-time onboarding.
//
// It is compared by BUILD VERSION, not by folder inventory. A "missing folder = missing photo"
// would give FALSE POSITIVES: there are slugs that legitimately have no folder (the livery packs). And the
// cost of a false warning is not zero — it sends the user to a ~9.5 GB extraction.
//
// It does not block: it is a dismissible notice. Whoever dismisses it does not see it again for THAT build (the
// dismissed version is remembered, not a boolean: if the game updates again, it warns again).
import { useTranslation } from 'react-i18next';
import { Display, Mono, Btn } from './components';

const DISMISS_KEY = 'zta.assetsStale.dismissedFor';

export function assetsNoticeDismissed(current: string): boolean {
  try { return localStorage.getItem(DISMISS_KEY) === current; } catch { return false; }
}
export function dismissAssetsNotice(current: string): void {
  try { localStorage.setItem(DISMISS_KEY, current); } catch { /* no storage: it will warn again */ }
}

export function AssetsStaleNotice(props: {
  oldVersion: string;
  newVersion: string;
  onSettings: () => void;
  onDismiss: () => void;
}) {
  const { t } = useTranslation();
  return (
    <div className="panel col" role="status"
         style={{ position: 'fixed', right: 16, bottom: 16, width: 420, maxWidth: 'calc(100vw - 32px)', zIndex: 60, gap: 10,
                  padding: '18px 20px', border: '1px solid color-mix(in oklab, var(--amber) 45%, var(--line))' }}>
      <Display style={{ fontSize: 17, color: 'var(--amber)' }}>{t('assetsStale.title')}</Display>
      <Mono style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.55 }}>
        {t('assetsStale.body', { old: props.oldVersion, new: props.newVersion })}
      </Mono>
      <Mono style={{ fontSize: 11, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('assetsStale.cost')}</Mono>
      <div className="row" style={{ gap: 8, justifyContent: 'flex-end' }}>
        <Btn variant="ghost" size="sm" onClick={props.onDismiss}>{t('assetsStale.dismiss')}</Btn>
        <Btn size="sm" onClick={props.onSettings}>{t('assetsStale.action')}</Btn>
      </div>
    </div>
  );
}
