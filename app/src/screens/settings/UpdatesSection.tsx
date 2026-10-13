// Settings > Updates: installed version + check/install update.
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { getBridge } from '../../shell/bridge';
import { useUpdater } from '../../updater/useUpdater';
import { ReleaseNotesDigest } from '../../updater/ReleaseNotesDigest';
import { Btn, Display, Mono } from '../../ui/components';

export function UpdatesSection() {
  const { t } = useTranslation();
  const u = useUpdater();
  const [current, setCurrent] = useState('');
  useEffect(() => { void getBridge().then((b) => b.app.version()).then(setCurrent); }, []);

  const line =
    u.status === 'checking' ? t('updates.checking')
    : u.status === 'none' ? t('updates.none')
    : u.status === 'available' ? t('updates.available', { v: u.version ?? '' })
    : u.status === 'downloading' ? (u.progressPct === null ? t('updates.downloading') : t('updates.downloadingPct', { pct: u.progressPct }))
    : u.status === 'restarting' ? t('updates.restarting')
    : u.status === 'error' ? t('updates.error', { msg: u.error ?? '' })
    : '';

  return (
    <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
      <div className="col" style={{ gap: 2 }}>
        <Display style={{ fontSize: 19 }}>{t('updates.title')}</Display>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('updates.sub')}</Mono>
      </div>
      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('updates.current', { v: current })}</Mono>
          {line && <Mono style={{ fontSize: 10.5, color: u.status === 'error' ? 'var(--danger)' : 'var(--text-2)' }}>{line}</Mono>}
        </div>
        {u.status === 'available'
          ? <Btn variant="primary" size="sm" onClick={() => void u.install()} style={{ flexShrink: 0 }}>{t('updates.installNow')}</Btn>
          : <Btn variant="ghost" size="sm" disabled={u.status === 'checking' || u.status === 'downloading' || u.status === 'restarting'} onClick={() => void u.check()} style={{ flexShrink: 0 }}>{t('updates.check')}</Btn>}
      </div>
      {/* the same summary as the startup notice, for anyone who comes looking for the update by hand from here. */}
      {u.status === 'available' && (
        <div style={{ padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
          <ReleaseNotesDigest notes={u.notes} maxHeight="30vh" />
        </div>
      )}
    </div>
  );
}
