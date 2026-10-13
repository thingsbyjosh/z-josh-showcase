// Supporter paywall: a hatched veil (.lockhatch from the design system) +
// an "Available for <tier>" label over a locked card. ABSOLUTE overlay: the parent must be
// position:relative. An analogous widget existed in the old overlay UI; this is its React port (first wiring).
// The paywall is CLICKABLE: clicking an event you don't have access to because you're not a supporter takes you to the Patreon page
// (previously it just blocked you and left you with no idea where to go). Since it covers a card that might be
// clickable on its own, the click stops right here (stopPropagation): it never reaches the locked event.
import { useTranslation } from 'react-i18next';
import { Mono } from './components';
import { PATREON_URL, openCommunityUrl } from './community';

export function SupporterWall({ tierMin }: { tierMin: string }) {
  const { t } = useTranslation();
  const tierLabel = tierMin === 'plus'
    ? t('events.tierPlus', { defaultValue: 'Supporter Plus' })
    : t('events.tierSupporter', { defaultValue: 'Supporter' });
  const go = (e: { stopPropagation(): void; preventDefault(): void }) => {
    e.stopPropagation();
    e.preventDefault();
    void openCommunityUrl(PATREON_URL, 'Patreon (muro de evento)');
  };
  return (
    <div
      className="lockhatch"
      role="button"
      tabIndex={0}
      title={t('community.wallCta')}
      onClick={go}
      onKeyDown={(e) => { if (e.key === 'Enter' || e.key === ' ') go(e); }}
      style={{
        position: 'absolute', inset: 0, zIndex: 5,
        display: 'flex', alignItems: 'center', justifyContent: 'center',
        background: 'color-mix(in oklab, var(--bg) 55%, transparent)',
        cursor: 'pointer',
      }}
    >
      <div className="col center" style={{ gap: 4, textAlign: 'center', padding: '0 12px' }}>
        <Mono style={{ fontSize: 10, letterSpacing: '.14em', color: 'var(--accent-text)' }}>
          {t('events.wallKicker', { defaultValue: 'SUPPORTER CONTENT' })}
        </Mono>
        <Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>
          {t('events.availableFor', { tier: tierLabel, defaultValue: 'Available for {{tier}}' })}
        </Mono>
        <Mono style={{ fontSize: 10.5, color: 'var(--accent-text)', textDecoration: 'underline' }}>
          {t('community.wallCta')}
        </Mono>
      </div>
    </div>
  );
}
