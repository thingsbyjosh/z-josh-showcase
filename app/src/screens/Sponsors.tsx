// Sponsors -- the sponsor-card portfolio: a row of slots (2-6 per phase) +
// bench (frozen clock) + a management rail. 100% engine-driven: every command -> refetch.
import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, CardsPayload, SignedCardView } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Kicker, Chip, Btn } from '../ui/components';
import { SponsorCardView, CardSlotBox } from '../ui/SponsorCardView';
import { TIER_COLORS, clauseText, lifeText } from '../ui/cardText';
import { formatCreditsCr } from '../ui/format';

const MAX_SLOTS = 6;
// the phase in which slot i unlocks (mirrors the engine's bal::CARD_SLOTS_BY_PHASE {2,2,3,4,4,5,6})
const SLOT_UNLOCK_PHASE = [1, 1, 3, 4, 6, 7];

function InfoRow({ k, v, vColor }: { k: string; v: string; vColor?: string }) {
  return (
    <div className="row between" style={{ padding: '8px 0', borderBottom: '1px solid var(--line)' }}>
      <Mono style={{ fontSize: 10, letterSpacing: '.1em', textTransform: 'uppercase', color: 'var(--text-3)' }}>{k}</Mono>
      <Mono style={{ fontSize: 12, fontWeight: 700, color: vColor ?? 'var(--text)' }}>{v}</Mono>
    </div>
  );
}

export function Sponsors({ client, onOpenShop }: { client: ApiClientLike; onOpenShop?: () => void }) {
  const { t, i18n } = useTranslation();
  const es = i18n.language === 'es';
  const q = useQuery<CardsPayload>(client, 'query.cards');
  const [selId, setSelId] = useState<string | null>(null);
  const [confirmSell, setConfirmSell] = useState(false);
  // drag & drop of cards between active slots and the bench (ported from the app/design prototype)
  const [dragId, setDragId] = useState<string | null>(null);
  const [dragOver, setDragOver] = useState<number | 'bench' | null>(null);
  useEffect(() => { setConfirmSell(false); }, [selId]);
  // visiting the tab marks the portfolio as SEEN (turns off the topbar badge)
  useEffect(() => { void client.cmd('cmd.cards.seen', {}).catch(() => {}); }, [client]);

  const run = (method: string, params?: object) =>
    void client.cmd(method, params).then(() => q.refetch()).catch(() => q.refetch());

  if (q.loading || !q.data) return <Mono style={{ padding: 30, color: 'var(--text-3)' }}>{t('common.loading')}</Mono>;
  const { signed, shop, slotsUnlocked } = q.data;
  const bySlot = new Map<number, SignedCardView>();
  signed.forEach(s => { if (s.slot >= 0) bySlot.set(s.slot, s); });
  const bench = signed.filter(s => s.slot < 0);
  const sel = signed.find(s => s.def.id === selId) ?? null;
  const maxLv = sel ? 1 + (sel.def.upgrade?.levels ?? 0) : 1;
  const freeSlots: number[] = [];
  for (let i = 0; i < slotsUnlocked; i++) if (!bySlot.has(i)) freeSlots.push(i);

  const doActivate = (slot?: number) => {
    if (!sel) return;
    const target = slot ?? freeSlots[0];
    if (target == null) return;
    run('cmd.cards.slot', { id: sel.def.id, slot: target });
  };
  const doSell = () => {
    if (!sel) return;
    if (!confirmSell) { setConfirmSell(true); return; }
    run('cmd.cards.sell', { id: sel.def.id });
    setSelId(null);
  };

  // drag & drop --------
  const dragEnd = () => { setDragId(null); setDragOver(null); };
  const dragProps = (sc: SignedCardView) => ({
    draggable: true,
    dragging: dragId === sc.def.id,
    onDragStart: (e: React.DragEvent) => {
      e.dataTransfer.setData('text/plain', sc.def.id);
      e.dataTransfer.effectAllowed = 'move';
      setDragId(sc.def.id); setSelId(sc.def.id);
    },
    onDragEnd: dragEnd,
  });
  const overProps = (key: number | 'bench') => ({
    onDragOver: (e: React.DragEvent) => {
      if (!dragId) return;
      e.preventDefault(); e.dataTransfer.dropEffect = 'move';
      if (dragOver !== key) setDragOver(key);
    },
    onDragLeave: () => { if (dragOver === key) setDragOver(null); },
  });

  // dropping on slot i: move / swap / insert in the middle (cmd.cards.arrange is atomic in the engine)
  const dropOnSlot = (i: number) => {
    const X = signed.find(x => x.def.id === dragId);
    if (!X || i >= slotsUnlocked) return dragEnd();
    const occupant = bySlot.get(i);
    if (!occupant) { run('cmd.cards.slot', { id: X.def.id, slot: i }); return dragEnd(); }
    if (occupant.def.id === X.def.id) return dragEnd();
    if (X.slot >= 0) {
      // reordering within the active slots: insert at the target position, shifting the rest
      const actives = [...bySlot.keys()].sort((a, b) => a - b).map(k => bySlot.get(k)!);
      const slotIdxs = actives.map(c => c.slot);
      const order = actives.filter(c => c.def.id !== X.def.id);
      const targetPos = order.findIndex(c => c.def.id === occupant.def.id);
      const insertAt = X.slot < i ? targetPos + 1 : targetPos;
      order.splice(insertAt, 0, X);
      run('cmd.cards.arrange', { moves: order.map((c, k) => ({ id: c.def.id, slot: slotIdxs[k] })) });
    } else if (freeSlots.length) {
      // from the bench when there's a gap: insert at i, shifting toward the nearest gap
      const moves = [{ id: X.def.id, slot: i }];
      const holeUp = freeSlots.find(f => f >= i);
      if (holeUp != null) {
        for (let k = holeUp; k > i; k--) { const c = bySlot.get(k - 1); if (c) moves.push({ id: c.def.id, slot: k }); }
      } else {
        const holeDown = freeSlots.filter(f => f < i).pop()!;
        for (let k = holeDown; k < i; k++) { const c = bySlot.get(k + 1); if (c) moves.push({ id: c.def.id, slot: k }); }
      }
      run('cmd.cards.arrange', { moves });
    } else {
      // row full: it's a swap -- the occupant drops to the bench
      run('cmd.cards.arrange', { moves: [{ id: X.def.id, slot: i }, { id: occupant.def.id, slot: -1 }] });
    }
    dragEnd();
  };
  const dropOnBench = () => {
    const X = signed.find(x => x.def.id === dragId);
    if (X && X.slot >= 0) run('cmd.cards.bench', { id: X.def.id });
    dragEnd();
  };

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      {/* header */}
      <div className="row between" style={{ padding: '18px 30px 12px', alignItems: 'flex-end', gap: 16, flexWrap: 'wrap' }}>
        <div>
          <Display as="h1" style={{ fontSize: 30 }}>{t('sp_title')}</Display>
          <Mono style={{ fontSize: 12, color: 'var(--text-3)', marginTop: 3 }}>{t('cards.walletSub')}</Mono>
        </div>
        <div className="row gap8" style={{ alignItems: 'center', flexWrap: 'wrap' }}>
          {shop && <Btn variant="primary" size="sm" onClick={onOpenShop}>▚ {t('cards.shopPending')} — {t('cards.open')}</Btn>}
          <Chip kind="accent">{bySlot.size}/{slotsUnlocked} {t('cards.slots').toUpperCase()}</Chip>
        </div>
      </div>

      <div style={{ flex: 1, minHeight: 0, display: 'grid', gridTemplateColumns: '1fr 330px' }}>
        {/* LEFT: slots + bench */}
        <div className="scroll" style={{ padding: '6px 26px 34px 30px' }}>
          <div className="row between" style={{ margin: '4px 0 10px', alignItems: 'baseline' }}>
            <Kicker style={{ color: 'var(--accent-text)' }}>{t('cards.active')}</Kicker>
            <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('cards.activeNote')}</Mono>
          </div>
          <div className="row" style={{ gap: 14, flexWrap: 'wrap', alignItems: 'flex-start' }} data-tour="sponsors.slots">
            {Array.from({ length: MAX_SLOTS }).map((_, i) => {
              if (i >= slotsUnlocked)
                return <CardSlotBox key={i} n={i + 1} locked phaseLabel={`${t('cards.phase')} ${SLOT_UNLOCK_PHASE[i]}`} />;
              const sc = bySlot.get(i);
              if (!sc) return <CardSlotBox key={i} n={i + 1} target={!!sel && sel.slot < 0 && !dragId}
                dragOver={dragOver === i}
                {...overProps(i)} onDrop={() => dropOnSlot(i)}
                onClick={sel && sel.slot < 0 ? () => doActivate(i) : undefined} />;
              return <SponsorCardView key={sc.def.id} def={sc.def} sc={sc}
                selected={selId === sc.def.id}
                {...dragProps(sc)} {...overProps(i)} onDrop={() => dropOnSlot(i)}
                dropHint={dragOver === i && dragId !== sc.def.id}
                onClick={() => setSelId(selId === sc.def.id ? null : sc.def.id)} />;
            })}
          </div>

          <div className="row between" style={{ margin: '28px 0 10px', alignItems: 'baseline' }}>
            <Kicker>{t('cards.bench')}</Kicker>
            <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{bench.length ? `${bench.length} · ` : ''}{t('cards.benchNote')}</Mono>
          </div>
          {signed.length === 0 ? (
            <div className="panel" style={{ padding: '34px 30px' }}>
              <Display style={{ fontSize: 21, color: 'var(--text-2)' }}>{t('cards.walletEmpty')}</Display>
              <p style={{ fontSize: 13, color: 'var(--text-3)', maxWidth: 460, lineHeight: 1.5, marginTop: 8 }}>{t('cards.walletEmptySub')}</p>
            </div>
          ) : (
            <div
              className={'benchzone' + (dragOver === 'bench' && dragId && (signed.find(x => x.def.id === dragId)?.slot ?? -1) >= 0 ? ' drag-over' : '')}
              {...overProps('bench')} onDrop={dropOnBench}
            >
              {bench.length === 0 ? (
                <Mono style={{ fontSize: 11.5, color: 'var(--text-3)', display: 'block', padding: '18px 2px' }}>{t('cards.benchEmpty')}</Mono>
              ) : (
                <div className="row" style={{ gap: 14, flexWrap: 'wrap', alignItems: 'flex-start' }}>
                  {bench.map(sc => (
                    <SponsorCardView key={sc.def.id} def={sc.def} sc={sc}
                      selected={selId === sc.def.id}
                      {...dragProps(sc)}
                      onClick={() => setSelId(selId === sc.def.id ? null : sc.def.id)} />
                  ))}
                </div>
              )}
            </div>
          )}
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', display: 'block', marginTop: 24 }}>{t('cards.swapNote')}</Mono>
        </div>

        {/* RIGHT: management rail */}
        <div className="scroll" style={{ borderLeft: '1px solid var(--line)', background: 'rgba(0,0,0,.22)' }} data-tour="sponsors.manage">
          {!sel ? (
            <div className="col" style={{ gap: 14, padding: '18px 16px' }}>
              <Kicker>{t('cards.detailIdle')}</Kicker>
              <span style={{ fontSize: 13, color: 'var(--text-2)', lineHeight: 1.5 }}>{t('cards.detailIdleSub')}</span>
            </div>
          ) : (
            <div className="col" style={{ gap: 12, padding: '16px 16px 20px' }}>
              <div className="row center"><SponsorCardView def={sel.def} sc={sel} /></div>
              <div className="col gap8">
                {sel.slot >= 0
                  ? <Btn size="sm" onClick={() => run('cmd.cards.bench', { id: sel.def.id })}>{t('cards.toBench')} ↓</Btn>
                  : <Btn size="sm" variant="primary" disabled={!freeSlots.length} onClick={() => doActivate()}>
                      {freeSlots.length ? `${t('cards.activate')} → SLOT ${freeSlots[0] + 1}` : t('cards.noFreeSlot')}
                    </Btn>}
                {sel.upgradeNext && (
                  <Btn size="sm" disabled={!sel.upgradeNext.ready || q.data.money < sel.upgradeNext.cost}
                    onClick={() => run('cmd.cards.upgrade', { id: sel.def.id })}>
                    {t('cards.upgrade')} → N{sel.upgradeNext.toLevel} · {formatCreditsCr(sel.upgradeNext.cost)}
                  </Btn>
                )}
                <Btn size="sm" variant="ghost" onClick={doSell}
                  style={confirmSell ? { boxShadow: 'inset 0 0 0 1px var(--danger)', color: 'var(--danger)' } : undefined}>
                  {confirmSell ? t('cards.confirm') : `${t('cards.sell')} · ${formatCreditsCr(sel.sellValue)}`}
                </Btn>
              </div>

              {/* card face */}
              <div className="col" style={{ gap: 0, borderTop: '1px solid var(--line)', marginTop: 2 }}>
                <InfoRow k={t('cards.level')} v={maxLv > 1 ? `${sel.level}/${maxLv}` : '—'} />
                <InfoRow k={t('cards.life')} v={lifeText(sel.def, sel, t, es)} />
                <InfoRow k={t('cards.rarity')} v={t(`cards.rarityName.${sel.def.rarityKey}`)} vColor={TIER_COLORS[sel.def.rarityKey]} />
                <InfoRow k={t('cards.sellFor')} v={formatCreditsCr(sel.sellValue)} vColor="var(--gold)" />
              </div>

              {/* upgrade progress */}
              {sel.upgradeNext && (
                <div className="col gap8" style={{ background: 'var(--bg-2)', border: '1px solid var(--line)', padding: '10px 12px' }}>
                  <div className="row between">
                    <Mono style={{ fontSize: 9.5, letterSpacing: '.12em', color: 'var(--text-3)' }}>
                      {t('cards.upgrade').toUpperCase()} N{sel.upgradeNext.toLevel}
                    </Mono>
                    <Mono style={{ fontSize: 11, color: sel.upgradeNext.ready ? 'var(--green)' : 'var(--text-2)' }}>
                      {sel.upgradeNext.have}/{sel.upgradeNext.need} {t('cards.upgradeProgress')}
                    </Mono>
                  </div>
                  <div className="bar" style={{ height: 4 }}>
                    <i style={{
                      width: `${sel.upgradeNext.need > 0 ? Math.min(100, (sel.upgradeNext.have / sel.upgradeNext.need) * 100) : 0}%`,
                      background: sel.upgradeNext.ready ? 'var(--green)' : 'var(--cyan)',
                    }} />
                  </div>
                </div>
              )}

              {/* clause */}
              {sel.def.clause && (
                <div className="col gap8" style={{ borderLeft: '3px solid color-mix(in oklab, var(--danger) 55%, transparent)', padding: '2px 0 2px 10px' }}>
                  <Mono style={{ fontSize: 9.5, letterSpacing: '.12em', color: 'var(--text-3)' }}>{t('cards.clauseLbl').toUpperCase()}</Mono>
                  <span style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.45 }}>{clauseText(sel.def, es)}</span>
                </div>
              )}

              {/* special */}
              {sel.def.special && (
                <div className="col gap8" style={{ borderLeft: `3px solid ${TIER_COLORS[sel.def.rarityKey]}`, padding: '2px 0 2px 10px' }}>
                  <Mono style={{ fontSize: 9.5, letterSpacing: '.12em', color: 'var(--text-3)' }}>★ {t('cards.specialLbl').toUpperCase()}</Mono>
                  <span style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.45 }}>{es ? sel.def.special.es : sel.def.special.en}</span>
                </div>
              )}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
