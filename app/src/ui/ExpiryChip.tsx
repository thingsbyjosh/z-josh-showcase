// 18/09 (Josh): “a flag on each contract that expires in a day or so”. ONE piece for the deadline of an
// offer on all the screens that show it (Market, its detail, the mail, the negotiation and the seats of the
// Paddock), so they do not say different things. The engine sends the days left (TTL, negotiation or market close,
// whichever comes first; 1 = today is the last day to respond):
//   · 1        -> “Last day”, in red;
//   · 2..6     -> “N days left”;
//   · 7 or more  -> the usual weeks.
// Without days (old engine or fixture) it falls back to the weeks that already came, and with nothing it draws nothing.
import type { CSSProperties } from 'react';
import { useTranslation } from 'react-i18next';
import { Chip } from './components';

export type ExpiryTone = 'last' | 'days' | 'weeks';

export function expiryTone(daysLeft?: number): ExpiryTone {
  if (daysLeft != null && daysLeft <= 1) return 'last';
  if (daysLeft != null && daysLeft < 7) return 'days';
  return 'weeks';
}

export function ExpiryChip({ daysLeft, weeksLeft, style }: { daysLeft?: number; weeksLeft?: number; style?: CSSProperties }) {
  const { t } = useTranslation();
  if (daysLeft == null && weeksLeft == null) return null;
  const tone = expiryTone(daysLeft);
  const base: CSSProperties = { fontSize: 10, ...style };
  if (tone === 'last') {
    return <span data-expiry="last"><Chip kind="default" style={{ ...base, color: 'var(--danger)' }}>{t('market.offer.lastDay')}</Chip></span>;
  }
  if (tone === 'days') {
    return <span data-expiry="days"><Chip kind="default" style={{ ...base, color: 'var(--amber)' }}>{t('market.offer.daysLeft', { count: daysLeft })}</Chip></span>;
  }
  const semanas = daysLeft != null ? Math.ceil(daysLeft / 7) : (weeksLeft as number);
  // a week or less already reads as urgent (what the offer detail did before there were days)
  const urgente = semanas <= 1 ? { color: 'var(--danger)' } : {};
  return <span data-expiry="weeks"><Chip kind="default" style={{ ...base, ...urgente }}>{t('market.offer.expiresIn', { n: semanas })}</Chip></span>;
}
