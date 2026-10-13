// COMMUNITY call to action: join the Patreon, link your account if you're already subscribed, join the
// Discord. Lives on the hub and the Play window (the two places where the player is just standing there, looking).
// The links open in the system BROWSER; the link button fires the SAME OAuth loopback as
// Settings > Account (useAccount.connect), so there aren't two different paths to the same place.
import type { CSSProperties } from 'react';
import { useTranslation } from 'react-i18next';
import { Mono } from './components';
import { PATREON_URL, DISCORD_URL, openCommunityUrl } from './community';
import { useAccount } from '../account/useAccount';
import type { ModTier } from '../account/token';

const TIER_KEY: Record<ModTier, string> = {
  free: 'account.tierFree', supporter: 'account.tierSupporter', plus: 'account.tierPlus',
};

// Official marks, drawn as vector paths so there's no dependency on the network (CSP) or an icon pack.
function PatreonIcon({ size = 13 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true" focusable="false">
      <circle cx="15.2" cy="8.8" r="6.8" />
      <rect x="2" y="2" width="3.6" height="20" />
    </svg>
  );
}

function DiscordIcon({ size = 14 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="currentColor" aria-hidden="true" focusable="false">
      <path d="M19.3 5.4A16.7 16.7 0 0 0 15.2 4.1l-.2.4a15.5 15.5 0 0 1 3.6 1.5 13.6 13.6 0 0 0-11.3 0 15.5 15.5 0 0 1 3.7-1.5l-.3-.4a16.7 16.7 0 0 0-4.1 1.3C2.5 9.3 1.8 13.1 2.2 16.8a16.8 16.8 0 0 0 5.1 2.6l.9-1.5a11 11 0 0 1-1.7-.8l.4-.3a12 12 0 0 0 10.3 0l.4.3a11 11 0 0 1-1.7.8l.9 1.5a16.8 16.8 0 0 0 5.1-2.6c.4-4.3-.7-8-3.6-11.4ZM8.9 14.6c-1 0-1.8-.9-1.8-2s.8-2 1.8-2 1.8.9 1.8 2-.8 2-1.8 2Zm6.2 0c-1 0-1.8-.9-1.8-2s.8-2 1.8-2 1.8.9 1.8 2-.8 2-1.8 2Z" />
    </svg>
  );
}

/**
  * `compact` = a single tight row (the hub's column); without it, the row breathes more (the Play window).
 */
export function CommunityCta({ compact = false, style }: { compact?: boolean; style?: CSSProperties }) {
  const { t } = useTranslation();
  const a = useAccount();
  const linked = a.status === 'connected';
  const fs = compact ? 10.5 : 11.5;

  return (
    <div
      className="row"
      style={{ gap: 8, alignItems: 'center', flexWrap: 'wrap', justifyContent: compact ? 'flex-start' : 'center', ...style }}
      data-testid="community-cta"
    >
      <button
        className="btn btn-sm"
        onClick={() => void openCommunityUrl(PATREON_URL, 'Patreon')}
        style={{ gap: 7, fontSize: fs, color: 'var(--accent-ink)', background: 'var(--accent)', borderColor: 'var(--accent)' }}
      >
        <PatreonIcon /> {t('community.patreon')}
      </button>

      {/* "already subscribed?" -- a user who's already paying should NOT have to go hunt for Settings > Account.
          Once the account is linked, the button gives way to the status (tier) instead of inviting them to link again. */}
      {linked ? (
        <Mono style={{ fontSize: fs - 1, color: 'var(--green)' }}>
          ✓ {t('account.connected', { tier: t(TIER_KEY[a.tier]) })}
        </Mono>
      ) : (
        <button
          className="btn btn-sm btn-ghost"
          disabled={a.status === 'connecting'}
          onClick={() => void a.connect()}
          style={{ fontSize: fs }}
        >
          {a.status === 'connecting' ? `${t('account.connecting')}…` : t('community.alreadyMember')}
        </button>
      )}

      <button
        className="btn btn-sm btn-ghost"
        onClick={() => void openCommunityUrl(DISCORD_URL, 'Discord')}
        style={{ gap: 7, fontSize: fs }}
      >
        <DiscordIcon /> {t('community.discord')}
      </button>
    </div>
  );
}
