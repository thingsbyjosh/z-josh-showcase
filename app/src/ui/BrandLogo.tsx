// Career Engine brand logo — the single consumer of the assets in app/src/assets/brand.
// tone 'white' for a dark background (the whole app); 'color' (black+red) reserved for light backgrounds.
import type { CSSProperties } from 'react';
import { useTranslation } from 'react-i18next';
import wordmarkColor from '../assets/brand/wordmark.svg';
import wordmarkWhite from '../assets/brand/wordmark-white.svg';
import markColor from '../assets/brand/mark.svg';
import markWhite from '../assets/brand/mark-white.svg';

const SRC = {
  wordmark: { color: wordmarkColor, white: wordmarkWhite },
  mark: { color: markColor, white: markWhite },
} as const;

export function BrandLogo({ variant = 'wordmark', tone = 'white', height = 26, style }: {
  variant?: 'wordmark' | 'mark';
  tone?: 'color' | 'white';
  height?: number;
  style?: CSSProperties;
}) {
  const { t } = useTranslation();
  return (
    <img src={SRC[variant][tone]} alt={t('app.brand')} draggable={false}
      style={{ height, width: 'auto', display: 'block', ...style }} />
  );
}
