// buying a car was IRREVERSIBLE and happened on the first click -- and the buy button sits
// right next to the rent button (same row, same size), so a mis-click cost the price of a car
// instead of a rental. This dialog steps in at all three purchase points (two in pre-race,
// one in the event pre-race screen).
//
// It speaks the same language as the rest of the app's dialogs for what has no way back:
// a modal panel, role="dialog" + aria-modal, a Display-styled title, the consequence spelled out IN WORDS (not a bare
// "are you sure?") and the [confirm][cancel] pair. Cancel carries data-gamepad-back so a controller can back out with B.
import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Display, Mono, Btn } from './components';
import { formatCreditsCr } from './format';

// 0.7.0 (car selling): the same dialog serves to SELL from the garage (mode="sell"). The title,
// the button and the sign of the balance change (money comes in), and `note` explains where the price comes from. Selling is not
// undoable either: the car leaves the garage and buying it back costs the full price.
export function BuyConfirm({
  carName,
  price,
  credits,
  busy = false,
  mode = 'buy',
  note,
  onConfirm,
  onCancel,
}: {
  carName: string;
  price: number;      // what is REALLY paid (the caller applies the event discount if there is one); when selling, what is collected
  credits: number;
  busy?: boolean;
  mode?: 'buy' | 'sell';
  note?: string;
  onConfirm: () => void;
  onCancel: () => void;
}) {
  const { t } = useTranslation();
  const selling = mode === 'sell';
  const title = selling ? t('sellConfirm.title') : t('buyConfirm.title');

  // Escape closes it: in a dialog that's stepped in front of a button the player actually meant to press, backing out has
  // to be as cheap as possible. Clicking outside also cancels.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape' && !busy) onCancel(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [busy, onCancel]);

  return (
    <div onClick={() => { if (!busy) onCancel(); }}
      style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.82)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 60 }}>
      <div className="panel" role="dialog" aria-modal="true" aria-label={title}
        onClick={(e) => e.stopPropagation()}
        style={{ width: '92vw', maxWidth: 400, padding: '26px 28px', display: 'flex', flexDirection: 'column', gap: 14 }}>
        <Display style={{ fontSize: 22 }}>{title}</Display>

        <div className="col" style={{ gap: 4 }}>
          <Display style={{ fontSize: 17 }}>{carName}</Display>
          <Mono style={{ fontSize: 15, fontWeight: 700, color: 'var(--gold)' }}>{selling ? '+' : ''}{formatCreditsCr(price)}</Mono>
          {note && <span style={{ fontSize: 13, color: 'var(--text-2)', lineHeight: 1.45 }}>{note}</span>}
        </div>

        {/* What you actually want to know before parting with the money: what you'll have left. */}
        <div className="row between" style={{ alignItems: 'baseline', gap: 10 }}>
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('buyConfirm.balance')}</Mono>
          <span data-testid="buy-confirm-balance">
            <Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>
              {formatCreditsCr(credits)} → {formatCreditsCr(selling ? credits + price : credits - price)}
            </Mono>
          </span>
        </div>

        <div className="row gap8" style={{ marginTop: 4 }}>
          <Btn variant="primary" disabled={busy} onClick={onConfirm} style={{ flex: 1 }}>{selling ? t('garage.sell.button') : t('buy')}</Btn>
          <Btn variant="ghost" disabled={busy} data-gamepad-back onClick={onCancel} style={{ flex: 1 }}>{t('common.cancel')}</Btn>
        </div>
      </div>
    </div>
  );
}
