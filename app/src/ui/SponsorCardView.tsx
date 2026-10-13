import { useLayoutEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { useTranslation } from 'react-i18next';
import type { CardDefView, SignedCardView } from '../api/contracts';
import { fxText, fxExtraTexts, clauseText, lifeText, cardEffectDetail, TIER_COLORS } from './cardText';

// 0.7.0 (): effect card on mouse hover (or on focusing the card with keyboard or controller). It goes in a portal
// with fixed position so that no scrolling container clips it: to the right of the card if it fits, otherwise to
// the left, and always inside the window.
const TIP_W = 264;
const TIP_GAP = 10;

function CardEffectTip({ id, def, sc, anchor, tier }: {
  id: string; def: CardDefView; sc: SignedCardView | null; anchor: DOMRect; tier: string;
}) {
  const { i18n } = useTranslation();
  const ref = useRef<HTMLDivElement>(null);
  const [top, setTop] = useState(anchor.top);
  const d = cardEffectDetail(def, sc, i18n.language === 'es');
  const vw = window.innerWidth;
  const vh = window.innerHeight;
  const left = anchor.right + TIP_GAP + TIP_W <= vw - 8 ? anchor.right + TIP_GAP : Math.max(8, anchor.left - TIP_GAP - TIP_W);
  useLayoutEffect(() => {
    const h = ref.current?.offsetHeight ?? 0;
    setTop(Math.max(8, Math.min(anchor.top, vh - h - 8)));
  }, [anchor.top, vh]);
  return createPortal(
    <div ref={ref} id={id} className="scard-tip" role="tooltip" style={{ left, top, width: TIP_W, ['--tier' as string]: tier }}>
      <div className="scard-tip-when">{d.when}</div>
      <ul className="scard-tip-fx">
        {d.effects.map((e, i) => <li key={i} className={e.downside ? 'downside' : undefined}>{e.text}</li>)}
      </ul>
      <div className="scard-tip-row">{d.life}</div>
      {d.upgrade && <div className="scard-tip-row">{d.upgrade}</div>}
      {d.clause && <div className="scard-tip-row scard-tip-clause">{d.clause}</div>}
    </div>,
    document.body,
  );
}

export function SponsorCardView({ def, sc, size = 'md', level: levelProp, selected, dim, stamp, priceTag, deal, dealDelay, onClick,
  draggable, dragging, dropHint, onDragStart, onDragEnd, onDragOver, onDragLeave, onDrop, tip = true }: {
  def: CardDefView; sc?: SignedCardView | null; size?: 'lg' | 'md' | 'sm';
  level?: number;   // level to display when there's no SignedCardView (the results showcase)
  selected?: boolean; dim?: boolean; stamp?: string | null;
  priceTag?: React.ReactNode; deal?: boolean; dealDelay?: number; onClick?: () => void;
  // drag & drop for the sponsor portfolio -- visual states + HTML5 DnD handlers
  draggable?: boolean; dragging?: boolean; dropHint?: boolean;
  onDragStart?: (e: React.DragEvent) => void; onDragEnd?: () => void;
  onDragOver?: (e: React.DragEvent) => void; onDragLeave?: () => void; onDrop?: () => void;
  tip?: boolean;    // 0.7.0 (): effect card on mouse hover; false disables it
}) {
  const { t, i18n } = useTranslation();
  const es = i18n.language === 'es';
  const [anchor, setAnchor] = useState<DOMRect | null>(null);
  const showTip = (e: React.SyntheticEvent<HTMLDivElement>) => {
    if (tip && !dragging) setAnchor(e.currentTarget.getBoundingClientRect());
  };
  const hideTip = () => setAnchor(null);
  const tipId = `scard-tip-${def.id}`;
  const tier = TIER_COLORS[def.rarityKey];
  const level = sc ? sc.level : (levelProp ?? 1);
  const fx = fxText(def, sc ? sc.fxV : def.fx.v, es);
  const fxExtra = fxExtraTexts(def, es);   // 0-2 extra effects (the dual counterweight)
  const clause = clauseText(def, es);
  const maxLv = 1 + (def.upgrade?.levels ?? 0);
  const holo = def.rarityKey === 'elite' || def.rarityKey === 'international';
  const cls = ['scard', `scard-${size}`, onClick ? 'clickable' : '', selected ? 'selected' : '',
    dim ? 'dim' : '', deal ? 'scard-deal' : '', dragging ? 'dragging' : '', dropHint ? 'drop-hint' : ''].filter(Boolean).join(' ');
  return (
    <div className={cls} style={{ ['--tier' as string]: tier, animationDelay: deal ? `${dealDelay ?? 0}ms` : undefined }}
      onClick={onClick} tabIndex={onClick ? 0 : -1}
      onMouseEnter={showTip} onMouseLeave={hideTip} onFocus={showTip} onBlur={hideTip}
      aria-describedby={anchor ? tipId : undefined}
      draggable={draggable} onDragStart={(e) => { hideTip(); onDragStart?.(e); }} onDragEnd={onDragEnd}
      onDragOver={onDragOver} onDragLeave={onDragLeave} onDrop={onDrop}
      onKeyDown={onClick ? (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); onClick(); } } : undefined}>
      {holo && <div className="scard-holo" />}
      <div className="scard-top">
        <span className="scard-rarity">{fx.special ? '★ ' : ''}{t(`cards.rarityName.${def.rarityKey}`)}</span>
        <span className="scard-life">{def.life.k === 'perm' ? '∞ ' : '◷ '}{lifeText(def, sc ?? null, t, es).toUpperCase()}</span>
      </div>
      <div className="scard-brand">
        <span className="scard-glyph">{def.name[0]}</span>
        <div className="scard-name">{def.name}</div>
        <div className="scard-tag">{es ? def.tag.es : def.tag.en}</div>
      </div>
      <div className="scard-fx">
        <div className="trig">{fx.trig}</div>
        {fx.special
          ? <div className="mainspecial"><span style={{ color: 'var(--tier)' }}>★</span> {fx.sub}</div>
          : <div className="row" style={{ alignItems: 'baseline', gap: 7, flexWrap: 'wrap' }}>
              <span className="main">{fx.main}</span><span className="sub">{fx.sub}</span>
            </div>}
      </div>
      {fxExtra.length > 0 && (
        <div className="scard-fx-extra">
          {fxExtra.map((ex, i) => (
            <div key={i} className={'scard-fx-extra-line' + (ex.downside ? ' downside' : '')}>
              <span className="main">{ex.main}</span><span className="sub">{ex.sub}</span>
            </div>
          ))}
        </div>
      )}
      {clause ? <div className="scard-clause">⚠ {clause}</div> : <div style={{ height: 4 }} />}
      <div className="scard-foot">
        {maxLv > 1 ? (
          <span className="scard-pips" title={`${t('cards.level')} ${level}/${maxLv}`}>
            {Array.from({ length: maxLv }).map((_, i) => <i key={i} className={'pip' + (i < level ? ' on' : '')} />)}
          </span>
        ) : <span />}
        {priceTag ?? <span />}
      </div>
      {stamp && <div className="scard-stamp"><b>{stamp}</b></div>}
      {anchor && tip && !dragging && <CardEffectTip id={tipId} def={def} sc={sc ?? null} anchor={anchor} tier={tier} />}
    </div>
  );
}

export function CardSlotBox({ n, locked, phaseLabel, target, onClick, dragOver, onDragOver, onDragLeave, onDrop }: {
  n: number; locked?: boolean; phaseLabel?: string; target?: boolean; onClick?: () => void;
  // drag & drop target (an empty slot)
  dragOver?: boolean; onDragOver?: (e: React.DragEvent) => void; onDragLeave?: () => void; onDrop?: () => void;
}) {
  const { t } = useTranslation();
  return (
    <div className={['cslot', locked ? 'locked' : '', target ? 'target' : '', dragOver ? 'drag-over' : ''].filter(Boolean).join(' ')}
      onClick={!locked ? onClick : undefined} tabIndex={!locked && onClick ? 0 : -1}
      onDragOver={!locked ? onDragOver : undefined} onDragLeave={onDragLeave} onDrop={!locked ? onDrop : undefined}>
      {locked ? (
        <div className="col center" style={{ gap: 7 }}>
          <span style={{ fontSize: 19, color: 'var(--text-3)' }}>🔒</span>
          <span className="mono" style={{ fontSize: 9.5, letterSpacing: '.14em', color: 'var(--text-3)' }}>{t('cards.locked').toUpperCase()}</span>
          <span className="mono" style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{phaseLabel}</span>
        </div>
      ) : (
        <div className="col center" style={{ gap: 7 }}>
          <span style={{ fontSize: 26, fontStyle: 'italic', color: 'var(--text-3)', opacity: .7 }}>{n}</span>
          <span className="mono" style={{ fontSize: 9.5, letterSpacing: '.14em', color: target ? 'var(--accent-text)' : 'var(--text-3)' }}>{t('cards.slotFree').toUpperCase()}</span>
        </div>
      )}
    </div>
  );
}
