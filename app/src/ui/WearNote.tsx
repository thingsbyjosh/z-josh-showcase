// requested by a player: "being able to see the wear and fuel consumption the app is about to apply while picking the
// duration, to plan setup and stops from the start". The mod scales those two GLOBAL game options
// inversely to race length (a 25%-length race burns tires at x4 so strategy
// still matters), and until now it was only visible AFTER launch. This puts it right next to the duration selector.
//
// The multipliers are NOT computed here: they arrive via query.prerace (wearByPct), computed by the engine using the
// same functions it later uses to write them. If the engine doesn't send them (old build), nothing gets drawn.
import { useTranslation } from 'react-i18next';
import type { PreracePayload } from '../api/contracts';
import { Mono } from './components';

export function WearNote({ p, pct }: { p: PreracePayload; pct: number }) {
  const { t } = useTranslation();
  const w = p.wearByPct?.find((o) => o.pct === pct);
  if (!w) return null;
  // Two states per axis: scaling off in Settings · the multiplier about to be written.
  // (31/08): the full-distance race used to say "Your setting", and that's false -- with scaling ON the
  // scale always takes over, and at its starting point it writes Authentic, which is x1. Saying "your setting"
  // suggested we were respecting the player's global value there. The game offers off · x1 · x2 · x3 · x4 · x5,
  // and this shows one of those, never anything else.
  const axis = (mult: number, on: boolean | undefined) =>
    on === false ? t('raceCfg.wearOff') : `x${mult < 1 ? 1 : mult}`;
  return (
    <Mono style={{ display: 'block', marginTop: 7, fontSize: 10, color: 'var(--text-3)', letterSpacing: '0.04em' }}>
      {t('gpscale.tyre')} {axis(w.tyre, p.wearScaleTyre)} · {t('gpscale.fuel')} {axis(w.fuel, p.wearScaleFuel)}
    </Mono>
  );
}
