// Settings > Account: Patreon link status + connect/disconnect.
import { useTranslation } from 'react-i18next';
import { useAccount } from '../../account/useAccount';
import type { ModTier } from '../../account/token';
import { Btn, Display, Mono } from '../../ui/components';

const TIER_KEY: Record<ModTier, string> = { free: 'account.tierFree', supporter: 'account.tierSupporter', plus: 'account.tierPlus' };

export function AccountSection() {
  const { t } = useTranslation();
  const a = useAccount();

  const line =
    a.status === 'connecting' ? t('account.connecting')
    : a.status === 'connected' ? t('account.connected', { tier: t(TIER_KEY[a.tier]) })
    : a.status === 'expired' ? t('account.expired')
    : a.status === 'error' ? t('account.error', { msg: a.error ?? '' })
    : t('account.notConnected');

  return (
    <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
      <div className="col" style={{ gap: 2 }}>
        <Display style={{ fontSize: 19 }}>{t('account.title')}</Display>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('account.sub')}</Mono>
      </div>
      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <Mono style={{ fontSize: 10.5, color: a.status === 'error' ? 'var(--danger)' : 'var(--text-2)' }}>{line}</Mono>
        {a.status === 'connected'
          ? <Btn variant="ghost" size="sm" onClick={() => void a.disconnect()} style={{ flexShrink: 0 }}>{t('account.disconnect')}</Btn>
          : a.status === 'connecting'
          ? (
            <div className="row" style={{ gap: 8, flexShrink: 0 }}>
              <Btn variant="primary" size="sm" disabled style={{ flexShrink: 0 }}>{t('account.connect')}</Btn>
              <Btn variant="ghost" size="sm" onClick={() => void a.cancel()} style={{ flexShrink: 0 }}>{t('account.cancel')}</Btn>
            </div>
          )
          : <Btn variant="primary" size="sm" onClick={() => void a.connect()} style={{ flexShrink: 0 }}>{t('account.connect')}</Btn>}
      </div>
    </div>
  );
}
