// Startup update modal: Install now / Later. Reusable from Settings.
import { useTranslation } from 'react-i18next';
import { Btn, Kicker, Mono } from '../ui/components';
import type { UseUpdater } from '../updater/useUpdater';
import { ReleaseNotesDigest } from '../updater/ReleaseNotesDigest';

export function UpdateModal({ u, onDismiss }: { u: UseUpdater; onDismiss: () => void }) {
  const { t } = useTranslation();
  const busy = u.status === 'downloading' || u.status === 'restarting';
  return (
    <div style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.6)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 70 }}>
      <div className="panel" style={{ width: 420, maxHeight: '80vh', padding: '18px 20px', display: 'flex', flexDirection: 'column', gap: 12 }}>
        <Kicker style={{ color: 'var(--amber)' }}>{t('updates.modalTitle')}</Kicker>
        <Mono style={{ fontSize: 12, color: 'var(--text-2)' }}>
          {u.status === 'restarting' ? t('updates.restarting')
            : u.status === 'downloading' ? (u.progressPct === null ? t('updates.downloading') : t('updates.downloadingPct', { pct: u.progressPct }))
            : u.status === 'error' ? t('updates.error', { msg: u.error ?? '' })
            : t('updates.available', { v: u.version ?? '' })}
        </Mono>
        {/* what the version about to be installed brings. Only with a pending update: while it's downloading or
            restarting, what matters is the progress. */}
        {u.status === 'available' && <ReleaseNotesDigest notes={u.notes} />}
        <div className="row" style={{ gap: 8, justifyContent: 'flex-end' }}>
          {!busy && <Btn variant="ghost" size="sm" onClick={onDismiss}>{t('updates.later')}</Btn>}
          {u.status === 'available' && (
            <Btn variant="primary" size="sm" onClick={() => void u.install()}>{t('updates.installNow')}</Btn>
          )}
        </div>
      </div>
    </div>
  );
}
