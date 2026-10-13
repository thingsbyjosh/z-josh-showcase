import type { CardDefView, SignedCardView, CardRarityKey } from '../api/contracts';
import { formatCreditsCr } from './format';
import i18n from 'i18next';

export const TIER_COLORS: Record<CardRarityKey, string> = {
  local: 'var(--steel)', regional: 'var(--green)', national: 'var(--cyan)',
  international: 'var(--amber)', elite: 'var(--accent)',
};

export interface CardFxText { trig: string; main: string; sub: string; special: boolean; downside: boolean; }

// hook->text mapping shared between the MAIN effect (fxText) and the EXTRA
// effects (fxExtraTexts) -- same hook vocabulary, same presentation. `downside` flags an effect that
// penalizes you (mult <1, bonus/fee <0) so the UI can render it differently (SponsorCardView).
function hookText(hook: string, v: number, def: CardDefView, es: boolean):
  { main: string; sub: string; special: boolean; downside: boolean } {
  const t = i18n.t.bind(i18n);
  if (hook === 'special' && def.special) return { main: '★', sub: es ? def.special.es : def.special.en, special: true, downside: false };
  if (hook === 'income.prize_mult') return { main: `×${+v.toFixed(2)}`, sub: t('cards.hookSub.income.prize_mult'), special: false, downside: v < 1 };
  if (hook === 'income.bonus') {
    if (v < 0) return { main: `−${formatCreditsCr(-v)}`, sub: t('cards.hookSub.fee'), special: false, downside: true };
    return { main: `+${formatCreditsCr(v)}`, sub: t('cards.hookSub.income.bonus'), special: false, downside: false };
  }
  if (hook === 'xp.mult') return { main: `×${+v.toFixed(2)}`, sub: 'XP', special: false, downside: v < 1 };
  if (hook === 'rep.mult') return { main: `×${+v.toFixed(2)}`, sub: 'REP', special: false, downside: v < 1 };
  if (hook.startsWith('cost.')) return { main: `−${Math.round(v * 100)}%`, sub: t(`cards.hookSub.${hook}`), special: false, downside: false };
  return { main: '?', sub: hook, special: false, downside: false };
}

export function fxText(def: CardDefView, fxV: number, es: boolean): CardFxText {
  const t = i18n.t.bind(i18n);
  const trig = t(`cards.trig.${def.trigger}`);
  const v = fxV || def.fx.v;
  return { trig, ...hookText(def.fx.hook, v, def, es) };
}

// text for the EXTRA effects (0-2) of a dual risk/reward card. Same
// trigger as the main effect (the card fires AS ONE UNIT) -- repeated on each entry for the caller's
// convenience (same shape as fxText).
export function fxExtraTexts(def: CardDefView, es: boolean): CardFxText[] {
  const t = i18n.t.bind(i18n);
  const trig = t(`cards.trig.${def.trigger}`);
  return (def.fxExtra ?? []).map(e => ({ trig, ...hookText(e.hook, e.v, def, es) }));
}

// 0.7.0 (): the card's effect explained in words, for the shop's and the wallet's hover. The card's
// face summarizes it in figures (×1.3, −10%); here goes when it is fulfilled, what each effect does, how long it
// lasts, what the next upgrade gives and what the clause costs.
export interface CardEffectDetail {
  when: string;
  effects: { text: string; downside: boolean }[];
  life: string;
  upgrade: string | null;
  clause: string | null;
}

function effectSentence(hook: string, v: number, def: CardDefView, es: boolean): { text: string; downside: boolean } {
  const t = i18n.t.bind(i18n);
  const pct = (x: number) => Math.round(Math.abs(x) * 100);
  if (hook === 'special' && def.special) return { text: es ? def.special.es : def.special.en, downside: false };
  if (hook === 'income.prize_mult') {
    return v >= 1 ? { text: t('cards.tip.prizeUp', { v: +v.toFixed(2) }), downside: false }
                  : { text: t('cards.tip.prizeDown', { pct: pct(v) }), downside: true };
  }
  if (hook === 'income.bonus') {
    return v >= 0 ? { text: t('cards.tip.bonus', { v: formatCreditsCr(v) }), downside: false }
                  : { text: t('cards.tip.fee', { v: formatCreditsCr(-v) }), downside: true };
  }
  if (hook === 'xp.mult' || hook === 'rep.mult') {
    const up = v >= 1;
    const key = `cards.tip.${hook === 'xp.mult' ? 'xp' : 'rep'}${up ? 'Up' : 'Down'}`;
    return { text: t(key, { pct: pct(up ? v - 1 : 1 - v) }), downside: !up };
  }
  if (hook.startsWith('cost.')) {
    return { text: t('cards.tip.costOff', { pct: pct(v), what: t(`cards.tip.costWhat.${hook}`) }), downside: false };
  }
  return { text: hook, downside: false };
}

export function cardEffectDetail(def: CardDefView, sc: SignedCardView | null, es: boolean): CardEffectDetail {
  const t = i18n.t.bind(i18n);
  const level = sc ? sc.level : 1;
  const mainV = sc ? (sc.fxV || def.fx.v) : def.fx.v;
  const effects = [effectSentence(def.fx.hook, mainV, def, es),
    ...(def.fxExtra ?? []).map(e => effectSentence(e.hook, e.v, def, es))];
  const life = def.life.k === 'perm' ? t('cards.tip.lifePerm')
    : t(def.life.k === 'races' ? 'cards.tip.lifeRaces' : 'cards.tip.lifeUses', { n: def.life.n });
  let upgrade: string | null = null;
  const up = def.upgrade;
  if (up && up.levels > 0 && level < 1 + up.levels) {
    const nextV = level + 1 >= 3 ? up.v3 : up.v2;
    if (nextV != null && nextV > 0) {
      const face = hookText(def.fx.hook, nextV, def, es);
      upgrade = t('cards.tip.upgrade', {
        need: up.after * level, cost: formatCreditsCr(up.cost * level), effect: `${face.main} ${face.sub}`,
      });
    }
  }
  let clause: string | null = null;
  if (def.clause?.kind === 'dnf_fine') clause = t('cards.tip.clauseDnf', { v: formatCreditsCr(def.clause.fine) });
  else if (def.clause?.kind === 'notop10_lose') clause = t('cards.tip.clauseTop10', { n: def.clause.races });
  return { when: t(`cards.tip.when.${def.trigger}`), effects, life, upgrade, clause };
}

export function clauseText(def: CardDefView, _es: boolean): string | null {
  if (!def.clause) return null;
  const t = i18n.t.bind(i18n);
  if (def.clause.kind === 'dnf_fine') return t('cards.clause.dnf_fine', { v: formatCreditsCr(def.clause.fine) });
  return t('cards.clause.notop10_lose', { n: def.clause.races });
}

export function lifeText(def: CardDefView, sc: SignedCardView | null,
                         t: (k: string) => string, _es: boolean): string {
  if (def.life.k === 'perm') return t('cards.permanent');
  if (def.life.k === 'races') { const left = sc ? sc.racesLeft : def.life.n; return `${left} ${t('cards.races')}`; }
  const left = sc ? sc.usesLeft : def.life.n;
  return `${left} ${t(left === 1 ? 'cards.use' : 'cards.uses')}`;
}
