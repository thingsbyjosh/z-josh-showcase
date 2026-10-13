// Championship mark. Series with their own art (0.7.0 port docs) show it; the rest
// keep looking as they have until now, with their name in the headline typeface.
//
// The art is drawn by tools/series_logos/build_assets.py and lives in app/public/series/<key>-{h,m}.svg,
// served by path like the flags. Two montages: 'h' for headers (horizontal, with descriptor)
// and 'm' for rows (thumbnail, legible at 28 px high; in the F1 seasons the two are the same
// drawing because the F1 logo is already compact at source).
import type { CSSProperties } from 'react';
import { SERIES_LOGO_ART, SERIES_LOGO_NAME } from './seriesLogoArt.gen';

export type SeriesLogoVariant = 'h' | 'm';

/** Path of a series' art, or undefined if that series has no drawn mark. */
export function seriesLogoSrc(seriesId: string | undefined, variant: SeriesLogoVariant = 'h'): string | undefined {
  const key = seriesId ? SERIES_LOGO_ART[seriesId] : undefined;
  return key ? `${import.meta.env.BASE_URL}series/${key}-${variant}.svg` : undefined;
}

/** Name of the championship that the art of that series is labelled with, if it has one. */
export function seriesLogoName(seriesId: string | undefined): string | undefined {
  const key = seriesId ? SERIES_LOGO_ART[seriesId] : undefined;
  return key ? SERIES_LOGO_NAME[key] : undefined;
}

export function SeriesLogo({ id, name, height, variant = 'h', fontSize, style, textStyle, artOnly }: {
  id: string | undefined;
  /** Series name. If missing, the one labelling the art is used. */
  name?: string;
  /** Logo height in px. The width is set by the drawing's proportion. */
  height: number;
  variant?: SeriesLogoVariant;
  /** Size of the name when the series has no art. By default, proportional to the height. */
  fontSize?: number;
  style?: CSSProperties;
  textStyle?: CSSProperties;
  /** Draw nothing when the series has no art, instead of falling back to the name. */
  artOnly?: boolean;
}) {
  const src = seriesLogoSrc(id, variant);
  const rotulo = name ?? seriesLogoName(id) ?? '';
  if (!src) {
    if (artOnly || !rotulo) return null;
    return (
      <div className="display" style={{ fontSize: fontSize ?? Math.round(height * 0.82), lineHeight: 1, ...style, ...textStyle }}>
        {rotulo.toUpperCase()}
      </div>
    );
  }
  // `alt` carries the name: whoever cannot see the drawing (screen reader, image that fails to load) reads the series.
  return (
    // alignSelf: the logo aligns with the text of its column, it is neither stretched nor centered for being
    // the only child with its own width inside a flex box.
    <img src={src} alt={rotulo} title={rotulo} draggable={false}
      style={{ height, width: 'auto', maxWidth: '100%', display: 'block', alignSelf: 'flex-start', ...style }} />
  );
}
