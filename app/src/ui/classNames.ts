// class display names -- the engine emits raw catalog tokens (e.g. "ivory_cup")
// and the frontend used to render them as-is. This converts them into a presentable name ("F-Retro Gen 1") without
// depending on data: known acronyms/brands via a lookup table, everything else title-cased word by word.
const WORD_FIX: Record<string, string> = {
  gt1: 'GT1', gt2: 'GT2', gt3: 'GT3', gt4: 'GT4', garrowby_hex394: 'GARROWBY_HEX394', merrowdown_myr373: 'MERROWDOWN_MYR373', drift_championship_5521: 'DRIFT_CHAMPIONSHIP_5521',
  pemberton_zel121: 'Pemberton_Zel121', lmp1: 'LMP1', lantern_masters_7489: 'LANTERN_MASTERS_7489', lmp3: 'LMP3', dpi: 'DPi', pinnacle_challenge_5480: 'PINNACLE_CHALLENGE_5480',
  tsi: 'TSI', tcr: 'TCR', keystone_cup_6259: 'KEYSTONE_CUP_6259', srx: 'SRX',
  us: 'US', eu: 'EU', uk: 'UK', br: 'BR',
  v8: 'V8', v10: 'V10', v12: 'V12', f1: 'F1',
};

export function classDisplayName(raw: string | undefined | null): string {
  if (!raw) return '';
  return raw
    .split('_')
    .map((seg) =>
      seg
        .split('-')
        .map((w) => {
          const lw = w.toLowerCase();
          if (WORD_FIX[lw]) return WORD_FIX[lw];
          const gen = lw.match(/^gen(\d+)$/); // gen1 -> Gen 1
          if (gen) return `Gen ${gen[1]}`;
          return w.charAt(0).toUpperCase() + w.slice(1);
        })
        .join('-'),
    )
    .join(' ');
}
