// Garage: ported from the engine's own garage-drawing code (source of truth for
// data/order). Owned cars + active contract strip + rating band + credits. Buying lives in
// pre-race; SELLING, here (0.7.0, thread “The ability to sell cars”): the car's km, what you get and the button with
// confirmation. The engine decides the price and the rejections (cmd.garage.sell). The car's real photo is not
// portable -> placeholder with the class shorthand. Labels via t() (engine keys + new garage.* keys).
import { useMemo, useState } from 'react';
import { logImageError } from '../support/supportLog';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, GaragePayload, GarageCar, GarageContract, GarageSellResult } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { useCarLiveries, useOverridePreviews } from '../ui/vehImages';
import { Display, Mono, Kicker, Chip, Btn } from '../ui/components';
import { BuyConfirm } from '../ui/BuyConfirm';
import { classDisplayName } from '../ui/classNames';
import { accentText } from '../ui/colors';
import VEH_SPECS from '../data/vehicleSpecs.json';
import VEH_SPECS_SUP from '../data/vehicleSpecsSupplemental.json';
import VEH_YEARS from '../data/vehicleYears.json';

// REAL specs keyed by vehHash — same sources and same precedence as the pre-race screen (EXTRACTED > curated):
// the car sheet shows actual data (power/torque/weight/balance/layout), not abstract bars.
type VehSpec = { hp?: number; tq?: number; kg?: number; bal?: string; lay?: string };
const SPECS: Record<string, VehSpec> = { ...(VEH_SPECS_SUP as Record<string, VehSpec>), ...(VEH_SPECS as Record<string, VehSpec>) };
// REAL year derived from the class era (the one the engine sends is a placeholder), same as the pre-race screen.
const YEARS = VEH_YEARS as Record<string, number>;

const fmtKr = (n: number): string => '₡ ' + Math.round(n).toLocaleString('en-US');
const fmtKm = (n: number): string => `${Math.round(n).toLocaleString('en-US')} km`;

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

// Fallback: only for cars the specs dataset doesn't cover (same rule as the pre-race screen).
function StatBar({ label, value, color }: { label: string; value: number; color: string }) {
  return (
    <div className="row gap8" style={{ alignItems: 'center' }}>
      <Mono style={{ fontSize: 9, width: 32, color: 'var(--text-3)' }}>{label}</Mono>
      <div style={{ flex: 1, height: 5, borderRadius: 3, background: 'var(--surface-3)' }}>
        <div style={{ width: `${Math.max(0, Math.min(100, value))}%`, height: '100%', borderRadius: 3, background: color }} />
      </div>
      <Mono style={{ fontSize: 10, width: 22, textAlign: 'right', color: 'var(--text-2)' }}>{value}</Mono>
    </div>
  );
}

// 0.7.0: sale row. Without the fields (older engine) it is not drawn. Blocked -> button off and the reason.
function SellRow({ car, onSell }: { car: GarageCar; onSell: () => void }) {
  const { t } = useTranslation();
  if (car.sellBlock === undefined) return null;
  const canSell = car.sellBlock === 'ok' && (car.sellPrice ?? 0) > 0;
  return (
    <div className="col" style={{ gap: 7, marginTop: 4, paddingTop: 10, borderTop: '1px solid var(--line)' }}>
      <div className="row between">
        <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('garage.sell.km')}</Mono>
        <Mono style={{ fontSize: 10, color: 'var(--text-2)' }}>{fmtKm(car.km ?? 0)}</Mono>
      </div>
      <div className="row between" style={{ alignItems: 'center', gap: 10 }}>
        <div className="col" style={{ gap: 2 }}>
          <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)' }}>{t('garage.sell.worth').toUpperCase()}</Mono>
          <Mono style={{ fontSize: 14, fontWeight: 700, color: canSell ? 'var(--gold)' : 'var(--text-3)' }}>
            {(car.sellPrice ?? 0) > 0 ? fmtKr(car.sellPrice ?? 0) : '—'}
          </Mono>
        </div>
        <Btn size="sm" variant="ghost" disabled={!canSell} onClick={onSell}>{t('garage.sell.button')}</Btn>
      </div>
      {!canSell && (
        <span style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.4 }}>{t(`garage.sell.block.${car.sellBlock}`)}</span>
      )}
    </div>
  );
}

function CarCard({ car, dataDir, onSell }: { car: GarageCar; dataDir?: string; onSell: () => void }) {
  const { t } = useTranslation();
  // Real car photo (same pipeline as the pre-race screen): NN by the owned livery's ordinal, falls back to the first one.
  const { nns, url } = useCarLiveries(dataDir, car.slug);
  const ov = useOverridePreviews(dataDir);   // "real skins": installed skin first, falls back to vanilla
  const nn = nns.length ? (car.livery >= 0 && car.livery < nns.length ? nns[car.livery] : nns[0]) : null;
  const thumb = nn != null ? (ov.url(car.slug, nn) || url(nn)) : '';
  const year = YEARS[String(car.vehHash)] ?? car.year;
  const sp = SPECS[String(car.vehHash)];
  const rows: Array<[string, string]> = [];
  if (sp) {
    if (sp.hp) rows.push(['spec_hp', `${sp.hp} ${t('unit_power')}`]);
    if (sp.tq) rows.push(['spec_torque', `${sp.tq} Nm`]);
    if (sp.kg) rows.push(['spec_weight', `${sp.kg} kg`]);
    if (sp.bal) rows.push(['spec_balance', sp.bal]);
    if (sp.lay) rows.push(['spec_layout', sp.lay]);
  }
  return (
    <div style={{ borderRadius: 0, overflow: 'hidden', border: `1px solid color-mix(in oklab, ${car.classColor} 35%, var(--line))`, background: 'var(--surface)' }}>
      <div style={{ position: 'relative', height: 132, background: `radial-gradient(circle at 50% 30%, color-mix(in oklab, ${car.classColor} 14%, var(--surface-2)), var(--surface-2))`, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        {thumb
          ? <img src={thumb} alt={car.name} loading="lazy" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', objectFit: 'cover' }} />
          : <span className="display" style={{ fontSize: 40, color: `color-mix(in oklab, ${car.classColor} 55%, var(--text-3))`, opacity: 0.55 }}>{car.classShort || '—'}</span>}
        <span className="row gap6" style={{ position: 'absolute', top: 8, left: 10, alignItems: 'center' }}>
          <span style={{ width: 7, height: 7, borderRadius: '50%', background: car.classColor }} />
          <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: accentText(car.classColor) }}>{(classDisplayName(car.className) || '—').toUpperCase()}</Mono>
        </span>
        <Chip kind="green" style={{ position: 'absolute', top: 8, right: 10, fontSize: 9 }}>◈ {t('owned_tag')}</Chip>
      </div>
      <div className="col" style={{ padding: '12px 14px 14px', gap: 8 }}>
        <Display style={{ fontSize: 19, lineHeight: 1.05 }}>{car.name}</Display>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{year} · {car.classShort} · <span style={{ color: 'var(--accent-text)' }}>◆ {t('collectible')}</span></Mono>
        {rows.length > 0 ? (
          <div className="col" style={{ gap: 4, marginTop: 2 }}>
            {rows.map(([k, val]) => (
              <div key={k} className="row between">
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t(k)}</Mono>
                <Mono style={{ fontSize: 10, color: 'var(--text-2)' }}>{val}</Mono>
              </div>
            ))}
          </div>
        ) : (
          <div className="col" style={{ gap: 5, marginTop: 2 }}>
            <StatBar label={t('stat_pwr')} value={car.stats.pwr} color="var(--accent)" />
            <StatBar label={t('stat_grp')} value={car.stats.grp} color="var(--cyan)" />
            <StatBar label={t('stat_bal')} value={car.stats.bal} color="var(--green)" />
          </div>
        )}
        <SellRow car={car} onSell={onSell} />
      </div>
    </div>
  );
}

function ContractStrip({ c }: { c: GarageContract }) {
  const { t } = useTranslation();
  const seasonN = Math.max(1, Math.min(c.seasonsTotal || 1, c.seasonsTotal - c.seasonsLeft + 1));
  const money = c.selfFunded ? t('con_money_self') : `+${fmtKr(c.perRace)}/${t('garage.contract.perRace')}`;
  const cov: [boolean, string][] = [
    [c.coverage.seatEntry, t('garage.cov.seat')], [c.coverage.consumables, t('garage.cov.consumables')],
    [c.coverage.damage, t('garage.cov.repairs')], [c.coverage.travel, t('garage.cov.travel')],
  ];
  const covered = cov.filter(([b]) => b).map(([, s]) => s);
  const pays = cov.filter(([b]) => !b).map(([, s]) => s);

  const Cell = ({ k, v }: { k: string; v: string }) => (
    <div className="col" style={{ gap: 2, minWidth: 0 }}>
      <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)' }}>{k}</Mono>
      <span style={{ fontSize: 13, fontWeight: 600, color: 'var(--text)' }}>{v}</span>
    </div>
  );

  return (
    <div style={{ margin: '0 30px 14px', padding: '14px 18px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
      <div className="row between" style={{ marginBottom: 12 }}>
        <Kicker style={{ color: 'var(--accent-text)' }}>{t('con_active')}</Kicker>
        <span style={{ fontWeight: 700 }}>{c.series}</span>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 16 }}>
        <Cell k={t('con_duration')} v={`${t('garage.contract.seasonPrefix')} ${seasonN}/${c.seasonsTotal || 1}`} />
        <Cell k={t('con_money')} v={money} />
        <Cell k={t('con_role')} v={c.roleLead ? t('con_role_lead') : t('con_role_team')} />
        {/* SEASON target (since 0.4.0): the team demands "finish Top N" (assigned at signing; season rollover judges
            renewal/release against it). With no season target -> the trial ratio, as before. */}
        <Cell k={t('con_objective')} v={c.objTargetPos ? `Top ${c.objTargetPos}` : `${c.objDone}/${c.objTotal} ${t('garage.contract.objMet')}`} />
      </div>
      <div className="row gap12" style={{ marginTop: 12, flexWrap: 'wrap', alignItems: 'center' }}>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('garage.contract.typeWord')} · {t(`ctype_${c.typeLabel}`)}</Mono>
        <Mono style={{ fontSize: 10.5, color: 'var(--green)' }}>{t('con_covers')} {covered.length === 4 ? t('con_covers_all') : covered.join(', ') || '—'}</Mono>
        {pays.length > 0 && <Mono style={{ fontSize: 10.5, color: 'var(--amber)' }}>{t('con_youpay')} {pays.length === 4 ? t('con_youpay_all') : pays.join(', ')}</Mono>}
        {c.fixesCar && <Chip kind="default" style={{ fontSize: 9 }}>◈ {t('car_fixed_by_team')} {c.teamCar}</Chip>}
        {/* Silly season (since 0.4.0): the team put an early renewal offer on the table -> it's in the inbox */}
        {c.renewOnTable && <Chip kind="green" style={{ fontSize: 9 }}>✉ {t('con_renew_table')}</Chip>}
      </div>
    </div>
  );
}

export function Garage({ client, dataDir }: { client: ApiClientLike; dataDir?: string }) {
  const { t } = useTranslation();
  const q = useQuery<GaragePayload>(client, 'query.garage');
  const cars = useMemo(() => q.data?.cars ?? [], [q.data]);
  const classCount = useMemo(() => new Set(cars.map((c) => c.classId)).size, [cars]);
  // 0.7.0: sale with confirmation. The result is shown above (sold or why not) and the garage is reread.
  const [selling, setSelling] = useState<GarageCar | null>(null);
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState<{ ok: boolean; text: string } | null>(null);
  const confirmSell = () => {
    if (!selling || busy) return;
    const car = selling;
    setBusy(true);
    void client.cmd<GarageSellResult>('cmd.garage.sell', { carId: car.id, livery: car.livery })
      .then((r) => {
        setNotice(r && r.ok
          ? { ok: true, text: t('garage.sell.done', { car: car.name, v: fmtKr(r.paid) }) }
          : { ok: false, text: t('garage.sell.failed', { why: t(`garage.sell.block.${r?.reason ?? 'not_owned'}`) }) });
      })
      .catch(() => setNotice({ ok: false, text: t('garage.sell.failed', { why: t('garage.sell.block.not_owned') }) }))
      .finally(() => { setBusy(false); setSelling(null); q.refetch(); });
  };

  if (q.loading) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || !q.data) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('need_car')}</Mono></Center>;

  const { credits, tier, contract } = q.data;

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div className="row between" style={{ padding: '16px 30px 12px', alignItems: 'flex-end' }}>
        <div className="col" style={{ gap: 3 }}>
          <Display as="h1" style={{ fontSize: 30 }}>{t('garage_title')}</Display>
          <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{cars.length} {t('owned_lc')} · {classCount} {t('classes_lc')}</Mono>
        </div>
        <div className="row gap12" style={{ alignItems: 'center' }}>
          <span className="row gap6" style={{ alignItems: 'center' }}>
            <span style={{ width: 7, height: 7, borderRadius: '50%', background: tier.color }} />
            <Mono style={{ fontSize: 10, color: accentText(tier.color), textTransform: 'uppercase' }}>{t(`garage.tier.${tier.token}`)}</Mono>
          </span>
          <span className="credits-pill">{fmtKr(credits)}</span>
        </div>
      </div>

      {contract && <ContractStrip c={contract} />}

      {notice && (
        <div role="status" className="row between" style={{ margin: '0 30px 12px', padding: '10px 14px', gap: 12, alignItems: 'center',
          border: `1px solid color-mix(in oklab, ${notice.ok ? 'var(--green)' : 'var(--danger)'} 45%, var(--line))`,
          background: `color-mix(in oklab, ${notice.ok ? 'var(--green)' : 'var(--danger)'} 8%, var(--surface))` }}>
          <span style={{ fontSize: 13, color: 'var(--text)' }}>{notice.text}</span>
          <Btn size="sm" variant="ghost" onClick={() => setNotice(null)}>{t('common.dismiss')}</Btn>
        </div>
      )}

      <div className="scroll" style={{ flex: 1, padding: '4px 30px 24px', minHeight: 0 }} data-tour="garage.grid">
        {cars.length === 0 ? (
          <Center><Mono style={{ color: 'var(--text-3)' }}>{t('need_car')}</Mono></Center>
        ) : (
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(290px, 1fr))', gap: 16 }}>
            {cars.map((c) => (
              <CarCard key={`${c.id}:${c.livery}`} car={c} dataDir={dataDir}
                onSell={() => { setNotice(null); setSelling(c); }} />
            ))}
          </div>
        )}
      </div>

      {selling && (
        <BuyConfirm mode="sell" carName={selling.name} price={selling.sellPrice ?? 0} credits={credits} busy={busy}
          note={(selling.km ?? 0) > 0 ? t('garage.sell.note', { km: fmtKm(selling.km ?? 0) }) : t('garage.sell.noteNew')}
          onConfirm={confirmSell} onCancel={() => { if (!busy) setSelling(null); }} />
      )}
    </div>
  );
}
