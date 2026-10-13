// CardShop — a post-race step in the flow: 3-4 cards, max 1 purchase (meta-cards can raise that),
// rerolls cost increasingly more, anything not picked is lost on exit. The engine re-validates everything.
import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, CardsPayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Chip, Btn } from '../ui/components';
import { SponsorCardView } from '../ui/SponsorCardView';
import { formatCreditsCr } from '../ui/format';
import { useScreenCrumb } from '../support/useScreenCrumb';

// 19/09: `doneLabel` when closing does NOT return to the races (entered by leaving Results through another
// tab, see leaveResults in AppShell): the button cannot promise “Back to races” and lead to the garage.
export function CardShop({ client, onDone, doneLabel }: { client: ApiClientLike; onDone: () => void; doneLabel?: string }) {
  const { t } = useTranslation();
  useScreenCrumb(client, 'card shop');   // audit note, 07/20
  const q = useQuery<CardsPayload>(client, 'query.cards');
  const shop = q.data?.shop ?? null;

  useEffect(() => {                       // no pending shop -> the step is skipped automatically
    if (q.data && !q.data.shop) onDone();
  }, [q.data, onDone]);

  if (q.loading || !q.data) return <Mono style={{ padding: 30, color: 'var(--text-3)' }}>{t('common.loading')}</Mono>;
  if (!shop) return null;

  const run = (method: string, params?: object) =>
    void client.cmd(method, params).then(() => q.refetch()).catch(() => q.refetch());
  const close = () => void client.cmd('cmd.cards.shop_close').then(onDone).catch(onDone);

  const buysLeft = Math.max(0, shop.maxBuys - shop.bought);
  const canReroll = q.data.money >= shop.rerollCost;
  const subKey = shop.maxBuys > 1 ? 'cards.shopSubPlural' : 'cards.shopSub';

  return (
    <div className="screen" style={{ position: 'absolute', inset: 0, display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      <div className="row between" style={{ padding: '22px 40px 0', alignItems: 'flex-end', flexWrap: 'wrap', gap: 14 }}>
        <div>
          <Mono style={{ fontSize: 11, letterSpacing: '.14em', color: 'var(--accent-text)' }}>
            {t('cards.shopKicker').toUpperCase()} · {shop.raceId.toUpperCase()}
          </Mono>
          <Display as="h1" style={{ fontSize: 40, marginTop: 4 }}>{t('cards.shopTitle')}</Display>
          <Mono style={{ fontSize: 11.5, color: 'var(--text-3)', marginTop: 5 }}>{t(subKey, { n: shop.maxBuys })}</Mono>
        </div>
        <div className="row gap12" style={{ alignItems: 'center' }}>
          <span className="credits-pill" style={{ fontSize: 15 }}>{formatCreditsCr(q.data.money)}</span>
          <Chip kind={buysLeft > 0 ? 'green' : 'lock'}>
            {buysLeft}/{shop.maxBuys} {t(shop.maxBuys === 1 ? 'cards.buysOne' : 'cards.buys').toUpperCase()}
          </Chip>
        </div>
      </div>

      <div className="scroll" style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '10px 40px' }} data-tour="shop.cards">
        <div className="row" style={{ gap: 22, flexWrap: 'wrap', justifyContent: 'center', alignItems: 'flex-start' }}>
          {shop.offers.map((def, i) => {
            const isBought = shop.boughtIds.includes(def.id);
            const canBuy = !isBought && buysLeft > 0 && q.data!.money >= def.cost;
            const reason = isBought ? null : buysLeft <= 0 ? t('cards.maxBought')
              : q.data!.money < def.cost ? t('cards.noFunds') : null;
            return (
              <div key={`${shop.rerolls}:${def.id}`} className="col" style={{ gap: 12, alignItems: 'stretch' }}>
                <SponsorCardView def={def} size="lg" deal dealDelay={i * 90}
                  dim={!isBought && buysLeft <= 0}
                  stamp={isBought ? t('cards.bought').toUpperCase() : null}
                  priceTag={<span className="scard-price">{formatCreditsCr(def.cost)}</span>} />
                <Btn variant={canBuy ? 'primary' : 'default'} disabled={!canBuy}
                  onClick={() => run('cmd.cards.buy', { id: def.id })} style={{ justifyContent: 'center' }}>
                  {isBought ? `✓ ${t('cards.bought')}` : reason ?? `${t('cards.buy')} · ${formatCreditsCr(def.cost)}`}
                </Btn>
              </div>
            );
          })}
        </div>
      </div>

      <div className="row between" style={{ padding: '0 40px 26px', alignItems: 'center', flexWrap: 'wrap', gap: 14 }} data-tour="shop.actions">
        <Btn disabled={!canReroll} onClick={() => run('cmd.cards.reroll')}>
          ↻ {t('cards.reroll')} · {shop.rerollCost === 0 ? t('cards.free').toUpperCase() : formatCreditsCr(shop.rerollCost)}
        </Btn>
        <Btn variant="primary" size="lg" onClick={close}>{doneLabel ?? t('cards.toCalendar')} →</Btn>
      </div>
    </div>
  );
}
