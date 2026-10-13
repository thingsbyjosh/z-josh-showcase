// Pre-race / Vehicle selection: the last step before racing, ported from the engine's own vehicle-drawing code (source of
// truth). Reads the ARMED state (query.prerace, param carId) left behind by cmd.calendar.launch. Left: the
// car list (OWNED / MARKET) with real thumbnails. Right: hero (real photo by slug+livery) + specs + per-race costs
// + livery picker with real thumbnails + duration % + buttons. BUY -> cmd.prerace.buy; RENT/RACE ->
// cmd.prerace.launch (fires the engine's race-apply event = handoff to the real race) -> onFired. Photos are PNGs in
// <data>/vehimages (see ui/vehImages.ts); in the browser (mock mode) they fall back to a gradient placeholder.
import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, PreracePayload, PreraceCar } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Kicker, Btn, Chip, LockIcon } from '../ui/components';
import { classDisplayName } from '../ui/classNames';
import { SeriesLogo } from '../ui/SeriesLogo';
import { accentText } from '../ui/colors';
import { formatCreditsCr, formatRaceLength } from '../ui/format';
import { useCarLiveries, useOverridePreviews } from '../ui/vehImages';
import { WearNote } from '../ui/WearNote';   // wear/fuel consumption for the chosen duration %
import { LaunchNotice } from '../ui/LaunchNotice';   // "hands off" warning before assembling the race
import { BuyConfirm } from '../ui/BuyConfirm';   // buying a car doesn't happen on the first click
import { lastDurPct, setLastDurPct, launchNoticeEnabled } from '../ui/prefs';
import VEH_SPECS from '../data/vehicleSpecs.json';
import VEH_SPECS_SUP from '../data/vehicleSpecsSupplemental.json';
import VEH_YEARS from '../data/vehicleYears.json';
import { supportLog, logImageError } from '../support/supportLog';

// specs by vehHash. vehicleSpecs.json = EXTRACTED from the game (a generated CSV);
// vehicleSpecsSupplemental.json = curated (real) values for the cars the extraction didn't cover. EXTRACTED values win:
// if those cars ever get re-extracted, their values automatically overwrite the curated ones.
type VehSpec = { hp?: number; tq?: number; kg?: number; bal?: string; lay?: string };
const SPECS: Record<string, VehSpec> = { ...(VEH_SPECS_SUP as Record<string, VehSpec>), ...(VEH_SPECS as Record<string, VehSpec>) };
// REAL year by class era (the year the engine sends is a placeholder). vehHash -> year.
const YEARS = VEH_YEARS as Record<string, number>;
const carYear = (c?: { vehHash: number; year: number }) => (c ? (YEARS[String(c.vehHash)] ?? c.year) : 0);

const DUR_PCTS = [5, 10, 25, 50, 75, 100];
// Minimum race length: 3 laps (lap mode) or 8 min (time mode). The engine floors to this;
// here we disable any % button that would fall below it, so a shorter-than-minimum race can never be requested.
const DUR_MIN_LAPS = 3;
const DUR_MIN_MIN = 8;

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

export function CarTile({ c, dataDir, sel, locked, onSelect }: { c: PreraceCar; dataDir?: string; sel: boolean; locked: boolean; onSelect: () => void }) {
  const { t } = useTranslation();
  const { nns, url } = useCarLiveries(dataDir, c.slug);
  const ov = useOverridePreviews(dataDir);   // "real skins": installed skin first, falls back to vanilla
  const thumb = nns.length ? (ov.url(c.slug, nns[0]) || url(nns[0])) : '';
  return (
    <button onClick={locked && !sel ? undefined : onSelect} className="focusable" style={{
      display: 'flex', flexDirection: 'column', gap: 4, textAlign: 'left', padding: '10px 12px', borderRadius: 3,
      cursor: locked && !sel ? 'not-allowed' : 'pointer', opacity: locked && !sel ? 0.4 : 1,
      background: sel ? `color-mix(in oklab, ${c.discColor} 12%, var(--surface))` : 'var(--surface)',
      border: sel ? `1px solid ${c.discColor}` : '1px solid var(--line)',
    }}>
      {thumb && (
        <div style={{ height: 42, margin: '-2px 0 2px', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <img src={thumb} alt="" loading="lazy" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain' }} />
        </div>
      )}
      <div className="row between" style={{ alignItems: 'center' }}>
        <span style={{ fontWeight: 700, fontSize: 13 }}>{c.name}</span>
        <Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{carYear(c) || ''}</Mono>
      </div>
      <div className="row between" style={{ alignItems: 'center' }}>
        <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{c.classShort}</Mono>
        {sel && locked ? <Mono style={{ fontSize: 9, color: 'var(--gold)' }}><LockIcon size={9} /> {t('locked')}</Mono>
          : c.owned ? <Mono style={{ fontSize: 9, color: 'var(--green)' }}>{t('owned_tag')}</Mono>
          : c.factory ? <Mono style={{ fontSize: 9, color: 'var(--gold)' }}>{t('by_contract')}</Mono>
          : <span style={{ fontSize: 11, fontWeight: 700, color: 'var(--gold)' }}>{formatCreditsCr(c.price)}</span>}
      </div>
    </button>
  );
}

function Bar({ label, val }: { label: string; val: number }) {
  return (
    <div className="col" style={{ gap: 3, flex: 1 }}>
      <div className="row between"><Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{label}</Mono><Mono style={{ fontSize: 9, color: 'var(--text-2)' }}>{val}</Mono></div>
      <div style={{ height: 4, borderRadius: 2, background: 'var(--surface-3)' }}><div style={{ width: `${Math.min(100, val)}%`, height: '100%', borderRadius: 2, background: 'var(--accent)' }} /></div>
    </div>
  );
}

export function Prerace({ client, dataDir, onBack, onFired }: { client: ApiClientLike; dataDir?: string; onBack: () => void; onFired: () => void }) {
  const { t } = useTranslation();
  const [carId, setCarId] = useState<string | undefined>(undefined);
  const [liveryOrd, setLiveryOrd] = useState<number>(-1);   // 1 = no explicit choice made (opt-in)
  const [durPct, setDurPct] = useState<number>(() => lastDurPct());
  const [busy, setBusy] = useState(false);
  // Clicking Rent/Run doesn't launch instantly: it shows the "hands off" warning (ui/LaunchNotice),
  // which closes itself after a few seconds and is what actually releases the race. It happens BEFORE the command because as soon as
  // the race is armed the app steps aside on its own, so a warning shown afterward would never be read by anyone, and that's exactly
  // when a stray player click can leak into the game's own menus. Placed up here with the rest of the hooks, since the
  // screen has loading early-returns further down.
  const [pendingAction, setPendingAction] = useState<'own' | 'rent' | null>(null);
  // the livery pending CONFIRMATION before buying (null = no dialog). The ordinal itself is stored,
  // not a boolean, because in Memories it's exactly what decides WHICH unit gets bought.
  const [pendingBuy, setPendingBuy] = useState<number | null>(null);
  // With the warning turned off ("don't show again" checked), the click launches directly, as before it existed.
  const requestLaunch = (action: 'own' | 'rent') => (launchNoticeEnabled() ? setPendingAction(action) : fire(action));
  const q = useQuery<PreracePayload>(client, 'query.prerace', carId ? { carId } : undefined);
  const p = q.data;

  // Photos of the SELECTED car (hero + picker). The hook goes BEFORE the early-returns (Rules of Hooks);
  // with an undefined slug it returns url()='' (placeholder). The on-disk NN ordering matches the engine's own ordinals.
  const selSlug = p?.cars.find((c) => c.id === p?.selCarId)?.slug;
  const detail = useCarLiveries(dataDir, selSlug);
  const ov = useOverridePreviews(dataDir);   // "real skins": installed skin first (hero + thumbnails), falls back to vanilla

  // on load: if duration is locked, force the preset
  useEffect(() => { if (p?.lockDuration) setDurPct(p.presetDurPct); }, [p?.lockDuration, p?.presetDurPct]);

  // Remember duration between races. Once the race is known (not locked), pre-select the last
  // value the player chose; if it's not available (falls under the minimum), use the shortest one available. Done once per
  // race (keyed by identity: track+class+name, stable across a car change) so it never overwrites a later click.
  const preselKey = useRef<string>('');
  useEffect(() => {
    if (!p || p.lockDuration) return;
    const key = `${p.trackHash}|${p.className}|${p.raceName}`;
    if (preselKey.current === key) return;
    preselKey.current = key;
    const isT = p.durationMin > 0;
    const below = (pct: number) => pct < 100 && (isT ? Math.floor(p.durationMin * pct / 100) < DUR_MIN_MIN : Math.round(p.laps * pct / 100) < DUR_MIN_LAPS);
    const avail = DUR_PCTS.filter((pct) => pct === 100 || !below(pct));   // ascending -> avail[0] = the shortest one available
    const remembered = lastDurPct();
    setDurPct(avail.includes(remembered) ? remembered : avail[0]);
  }, [p?.trackHash, p?.className, p?.raceName, p?.durationMin, p?.laps, p?.lockDuration]);

  // Breadcrumb "opens pre-race" + a PERSISTENT car-fix diagnostic log (previously a console.log -> invisible on
  // the end user's machine). Logged once per race identity. lockCar=0/presetVeh=0 -> the engine did NOT set the contract's
  // car (an engine-side bug); lockCar=1 + presetVeh!=0 but no car rendered -> an app-side bug.
  const crumbKey = useRef<string>('');
  useEffect(() => {
    if (!p) return;
    const key = `${p.trackHash}|${p.className}|${p.raceName}`;
    if (crumbKey.current === key) return;
    crumbKey.current = key;
    supportLog(client, `open prerace: ${p.raceName || p.className} | lockCar=${p.lockCar ? 1 : 0} selCar=${p.selCarId ?? ''} presetVeh=${p.presetVehHash ?? 0} lockSkin=${p.lockSkin ? 1 : 0} reason=${p.reasonKey ?? ''}`);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [p?.trackHash, p?.className, p?.raceName]);

  if (q.loading && !p) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || !p) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (p.clsEmpty) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('choose_car_first')}</Mono></Center>;

  const selectCar = (id: string) => { setCarId(id); setLiveryOrd(-1); };
  const effDur = p.lockDuration ? p.presetDurPct : durPct;
  // When the skin is PRESET (contract/championship), the picker is NOT removed — it's shown DIMMED, with
  // the fixed skin highlighted. It's only hidden for a "pure" factory drive with no fixed skin (nothing to choose or show).
  const liveryLocked = p.lockSkin;
  const showLivery = p.visual && p.liveryCount > 1 && (liveryLocked || !p.selFactory);
  // A tryout reuses the "factory" path to be free, but it's NOT a salaried seat -> we don't show
  // the "factory salary" (you don't get paid for an audition); we show "free tryout" instead.
  const isTryout = p.reasonKey === 'lock_tryout';
  const isTime = p.durationMin > 0;

  // "Married" (privateer_pick) — the marriage's car+skin as the DEFAULT, but still changeable; diverging
  // warns about the divorce cost (REP + credits) right next to the launch button.
  const married = p.divorceRep > 0 && p.presetVehHash !== 0 && p.presetLivery >= 0;
  const ownedLiveries = p.ownedLiveries ?? [];
  // Reported by a player (Aug 9): "I bought the Brackenridge Gry837 Coldley Motors, ran a couple of privateer races,
  // closed with 'close both', and the next morning the car was gone and it asked me to buy it again." The car NEVER
  // left the save: what got lost was recognition, because the screen was AUTO-PRESELECTING a skin
  // that wasn't the one purchased. The 1:1 rule says "just Run" is only valid if you own the unit being SHOWN
  // (car+livery are a 1:1 unit), so as soon as the default pointed at a different livery, your own car showed up as
  // BUY/RENT — without the player ever touching the strip. And the default was drifting on its own: when racing as a
  // privateer, the marriage latch fixes (car, skin) for the season, and `defaultOrd` was honoring it WITHOUT
  // checking that unit was actually yours. Renting someone else's skin just once was enough for your own
  // purchased car to stop being recognized the next session.
  // The marriage mechanic is still honored — it's still its own mechanic, divorce included — but only when the married unit is actually yours;
  // otherwise, the default falls back to the one you OWN. The 1:1 rule stays intact: manually navigating to another skin still
  // means BUY/RENT for that unit.
  const marriedUnitOwned = ownedLiveries.length === 0
    || ownedLiveries.includes(p.presetLivery) || ownedLiveries.includes(-1);
  const defaultOrd = married && p.selVehHash === p.presetVehHash && marriedUnitOwned ? p.presetLivery
    : p.ownedLiveryOrdinal >= 0 ? p.ownedLiveryOrdinal : 0;
  // Livery ordinal shown in the hero: preset fixed by contract/calibration > user's own choice >
  // default (the marriage's skin, or the saved skin for an owned car). liveryNN[ordinal] = the on-disk PNG's NN.
  const heroOrd = p.lockSkin && p.presetLivery >= 0 ? p.presetLivery
    : liveryOrd >= 0 ? liveryOrd
    : defaultOrd;
  const heroNN = p.liveryNN[heroOrd] ?? p.liveryNN[0];
  const heroImg = heroNN != null ? (ov.url(selSlug ?? '', heroNN) || detail.url(heroNN)) : '';
  const divorcing = married && !p.lockSkin && (p.selVehHash !== p.presetVehHash || heroOrd !== p.presetLivery);
  // the suffix states the MONEY (real and visible); the internal divorce REP hit isn't quoted as a number — no
  // screen shows it, and promising "-3 REP" against an invisible number is the same lying-warning problem the
  // contract-termination confirm dialog had. divorceRep is still the signal for "married" (>0), it just stops being rendered.
  const divorceSuffix = divorcing ? ` · −${formatCreditsCr(p.divorceCr)}` : '';
  // Memories 1:1 (car+skin units): every (car, livery) pair is a collectible unit. The PRESELECTED one is the
  // one you own (the owned livery, via defaultOrd = ownedLiveryOrdinal); navigating to an unowned skin turns RUN
  // into BUY (that livery is a DIFFERENT unit). ownedLiveries = the car's owned ordinals (declared above,
  // next to defaultOrd, which now needs it); memUnitOwned = whether the shown skin (heroOrd) is yours.
  // 1:1 (car+skin) — a rule change: every (car, livery) pair is a DISTINCT unit. You own the shown unit
  // if its ordinal (heroOrd) is in ownedLiveries. Navigating to an UNOWNED skin of the same car treats it as an ENTIRELY
  // different car -> BUY/RENT that unit (at its own price) + a divorce on switching, not just "Run". This replaces the old
  // per-car p.selIsOwned in the button gate and in the cost calculation. Compat: if the payload doesn't carry the list
  // (fixture/legacy), it falls back to per-car ownership; a generic livery (-1: a car bought with no specific skin,
  // a pre-1:1 purchase) counts as owned on any skin. A market car -> empty list + selIsOwned=false -> false.
  const unitOwned = ownedLiveries.length === 0
    ? p.selIsOwned
    : (ownedLiveries.includes(heroOrd) || ownedLiveries.includes(-1));
  const memUnitOwned = !!p.isMemory && unitOwned;
  // a factory car or price 0 (provided: qualifiers, contract seats) is not bought — the
  // engine already rejects it (canBuy/PreraceBuy); the button is not even offered here (avoids the greyed-out "BUY · Cr 0").
  const selCarInfo = p.cars.find((c) => c.id === p.selCarId);
  const buyable = !!selCarInfo && !selCarInfo.factory && (selCarInfo.price ?? 0) > 0;

  // YOU PAY, net out-of-pocket — mirrors the engine's own settlement (out-of-pocket = entry + costs −
  // coverage, with coverage clamped to entry+costs, >=0) + travel. Before, the panel showed "Covered -X" but
  // YOU PAY didn't actually subtract coverAmt, and on top of that it used the RENTAL cost for a contracted seat (a car provided by the
  // team, launched as 'own') -> it came out huge even when the team covered almost everything. Scenario:
  //  owned or contract-provided (unitOwned || lockCar, launched as 'own'): costs = the displayed run-costs
  //    (p.cost.total, scaled by laps, the SAME figure the settlement deducts).
  //  rental (else): costs = the rental fee (arrive-and-drive). With no contract, coverAmt=0.
  const raceOwnScenario = unitOwned || p.lockCar;
  const netOwn  = Math.max(0, p.cost.total + p.entryFee + p.inscDue - p.coverAmt) + p.travel;
  const netRent = Math.max(0, p.rentCost   + p.entryFee + p.inscDue - p.coverAmt) + p.travel;
  const youPay  = raceOwnScenario ? netOwn : netRent;
  // The breakdown rows ARE the scenario that YOU PAY is charging for. When renting, it lists the rental fee
  // (all-inclusive), not the owned car's consumables you're not paying for — the real-world bug report: the breakdown
  // summed to 5,051 while the total said 8,350, because it mixed "owned" components into a "rental" total.
  // Now the rows and the total agree in both scenarios.
  const costRows: Array<[string, number]> = raceOwnScenario
    ? [['cost_tyres', p.cost.tyres], ['cost_fuel', p.cost.fuel], ['cost_fluids', p.cost.fluids], ['cost_insurance', p.cost.insurance]]
    : [['cost_rent', p.rentCost]];
  costRows.push(['entry_fee', p.entryFee], ['inscription', p.inscDue], ['cost_travel', p.travel]);

  const fire = (action: 'own' | 'rent') => {
    if (busy) return;
    setBusy(true);
    if (!p.lockDuration) setLastDurPct(durPct);   // remembers the player's FREE choice (not the locked preset)
    // the livery that gets SENT is the one being SHOWN (heroOrd), not the player's raw click. With liveryOrd,
    // it used to send -1 until a thumbnail was clicked, even when the hero was already showing skin 1 marked as
    // active: the mod launched with no specific livery and the game picked one at random. Also, with no ordinal the
    // privateer latch never engaged (it requires veh+ord >= 0), so neither privpick nor livery_ord got written on
    // entry -> on the following round, the car you'd already raced didn't come back preselected. Buying already used heroOrd.
    void client.cmd('cmd.prerace.launch', { carId: p.selCarId, action, livery: heroOrd, durationPct: effDur })
      .then(() => onFired()).catch(() => setBusy(false));
  };
  // Memories 1:1: BUYING an unowned livery buys THAT unit (car+skin) -> it becomes runnable. By default
  // it buys the shown skin (heroOrd); every other purchase uses the player's own choice (liveryOrd).
  const buy = (liv: number = liveryOrd) => {
    if (busy) return;
    setBusy(true);
    void client.cmd('cmd.prerace.buy', { carId: p.selCarId, livery: liv }).then(() => q.refetch()).catch(() => q.refetch()).finally(() => setBusy(false));
  };

  const owned = p.cars.filter((c) => c.owned);
  const market = p.cars.filter((c) => !c.owned);
  // "you need" ALWAYS with an amount: the TOTAL to pay (you-pay), same as the overlay's "You need Cr <total>".
  // It used to show the DEFICIT and hide it when it came out to 0 -> leaving the orphaned word "you need" with no number.
  const needLabel = (total: number) => `${t('need_credits')} · ${formatCreditsCr(total)}`;
  // the EFFECTIVE duration — the one that actually gets run at the chosen %. The header used to show
  // `p.durationMin`, which is the REFERENCE value (the 100% one): picking 25% still announced the full race,
  // and the number that actually applied lived at 11px next to the label. A player signed up for an hour and a half in a
  // Ashmere Motors race thinking it was 15 minutes. A single calculation now feeds both the header and the large block.
  const effLen = Math.round((isTime ? p.durationMin : p.laps) * effDur / 100);
  const durReadout = isTime ? formatRaceLength(effLen) : `${effLen} ${t('laps')}`;

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      {/* Header */}
      <div className="row between" style={{ padding: '14px 26px', alignItems: 'center', borderBottom: '1px solid var(--line)' }}>
        <div className="row gap12" style={{ alignItems: 'center', minWidth: 0 }}>
          <button className="btn btn-sm btn-ghost" data-gamepad-back onClick={onBack}>← {t('calendar')}</button>
          {/* The championship mark, only if that series has art: the header is already tight and it is not
              worth adding another line of text on the series that do not have it yet. */}
          <SeriesLogo id={p.seriesId} variant="h" height={30} artOnly style={{ flex: 'none' }} />
          <div className="col" style={{ minWidth: 0 }}>
            <span className="row gap6" style={{ alignItems: 'center' }}>
              <span style={{ width: 7, height: 7, borderRadius: '50%', background: p.discColor }} />
              <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{classDisplayName(p.className).toUpperCase()}</Mono>
              {p.reasonKey && <Chip kind="gold" style={{ fontSize: 8 }}><LockIcon size={8} /> {t(p.reasonKey)}</Chip>}
            </span>
            <Display style={{ fontSize: 22 }}>{p.raceName}</Display>
            <Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{p.trackName} · <span data-testid="prerace-head-len">{durReadout}</span> · {t('grid')} {p.grid}</Mono>
          </div>
        </div>
        <div className="col" style={{ alignItems: 'flex-end', gap: 4 }}>
          <span style={{ color: 'var(--gold)', fontWeight: 700, fontSize: 16 }}>{formatCreditsCr(p.reward)}</span>
          {p.ratingTierLabel && <Chip kind="default" style={{ fontSize: 9, color: accentText(p.ratingTierColor) }}>{p.ratingTierLabel}</Chip>}
        </div>
      </div>

      <div style={{ flex: 1, display: 'grid', gridTemplateColumns: '360px 1fr', gap: 0, minHeight: 0 }}>
        {/* Car list */}
        <div className="scroll col" style={{ gap: 8, minHeight: 0, padding: '14px 18px' }} data-tour="prerace.cars">
          {owned.length > 0 && <Kicker style={{ color: 'var(--green)' }}>{t('veh_owned')} ({owned.length})</Kicker>}
          {owned.map((c) => <CarTile key={c.id} c={c} dataDir={dataDir} sel={c.id === p.selCarId} locked={p.lockCar} onSelect={() => selectCar(c.id)} />)}
          {market.length > 0 && <Kicker style={{ marginTop: owned.length ? 8 : 0 }}>{t('veh_market')} ({market.length})</Kicker>}
          {/* Memories (1:1 units): market cars are SELECTABLE so they can be BOUGHT (any of them, not
              just the first one). The button already distinguishes by unit (owned car+skin = RUN; otherwise = BUY), so
              selecting an unowned one never "runs" it. The only real lock left is lockCar (contract/calibration). */}
          {market.map((c) => <CarTile key={c.id} c={c} dataDir={dataDir} sel={c.id === p.selCarId} locked={p.lockCar} onSelect={() => selectCar(c.id)} />)}
        </div>

        {/* Detail of the selected car */}
        <div className="col" style={{ minHeight: 0, padding: '14px 22px', borderLeft: '1px solid var(--line)', gap: 14, overflow: 'auto' }}>
          {/* hero: full-width banner. The renders are panoramic (8:3) -> the box uses a taller
              height so the photo fills the horizontal space instead of floating in a central strip. A bottom scrim
              keeps the text readable over the image. No image -> just the gradient shows. */}
          <div style={{ width: '100%', height: 260, borderRadius: 0, background: `linear-gradient(135deg, color-mix(in oklab, ${p.discColor} 22%, var(--surface-2)), var(--surface-2))`, border: '1px solid var(--line)', display: 'flex', alignItems: 'flex-end', padding: 16, position: 'relative', overflow: 'hidden' }}>
            <div style={{ position: 'absolute', top: 0, left: 0, width: '100%', height: 3, background: p.discColor, zIndex: 3 }} />
            {heroImg && (
              <img src={heroImg} alt="" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', objectFit: 'contain', objectPosition: 'center', padding: '10px 6px 22px' }} />
            )}
            {/* scrim to separate the text from the photo */}
            <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: '46%', background: 'linear-gradient(to top, color-mix(in oklab, var(--surface-2) 88%, transparent), transparent)', zIndex: 2 }} />
            <div className="col" style={{ position: 'relative', zIndex: 3, textShadow: '0 1px 8px rgba(0,0,0,.8)' }}>
              <Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{classDisplayName(p.className).toUpperCase()}</Mono>
              <Display style={{ fontSize: 24 }}>{p.cars.find((c) => c.id === p.selCarId)?.name ?? ''}</Display>
            </div>
            {unitOwned && <Chip kind="green" style={{ position: 'absolute', top: 10, right: 10, fontSize: 8, zIndex: 3 }}>{t('owned_tag')}</Chip>}
          </div>

          <div className="row gap12">
            {/* spec */}
            <div style={{ flex: '1 1 60%', padding: '12px 14px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
              <Kicker>{t('spec')}</Kicker>
              {(() => {
                const c = p.cars.find((x) => x.id === p.selCarId);
                // REAL specs by vehHash (extracted from the game). If the car is in the dataset, they're shown
                // as actual data (power/torque/weight/balance/layout); if not, it falls back to derived power/grip/balance bars.
                const sp = c ? SPECS[String(c.vehHash)] : undefined;
                const rows: Array<[string, string]> = [];
                if (sp) {
                  if (sp.hp) rows.push(['spec_hp', `${sp.hp} ${t('unit_power')}`]);
                  if (sp.tq) rows.push(['spec_torque', `${sp.tq} Nm`]);
                  if (sp.kg) rows.push(['spec_weight', `${sp.kg} kg`]);
                  if (sp.bal) rows.push(['spec_balance', sp.bal]);
                  if (sp.lay) rows.push(['spec_layout', sp.lay]);
                }
                if (rows.length > 0) {
                  return (
                    <div className="col" style={{ marginTop: 8, gap: 4 }}>
                      {!!carYear(c) && <div className="row between"><Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('spec_year')}</Mono><Mono style={{ fontSize: 10, color: 'var(--text-2)' }}>{carYear(c)}</Mono></div>}
                      {rows.map(([k, val]) => (
                        <div key={k} className="row between"><Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t(k)}</Mono><Mono style={{ fontSize: 10, color: 'var(--text-2)' }}>{val}</Mono></div>
                      ))}
                    </div>
                  );
                }
                return c ? <div className="col gap8" style={{ marginTop: 8 }}><Bar label="PWR" val={c.pwr} /><Bar label="GRP" val={c.grp} /><Bar label="BAL" val={c.bal} /></div> : null;
              })()}
            </div>
            {/* per-race costs */}
            <div style={{ flex: '1 1 38%', padding: '12px 14px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
              <Kicker>{t('costs_per_race')}</Kicker>
              {isTryout ? (
                <div className="col" style={{ marginTop: 8 }}>
                  <span style={{ color: 'var(--green)', fontWeight: 700, fontSize: 13 }}>{t('tryout_free')}</span>
                  <Mono style={{ fontSize: 9.5, color: 'var(--text-3)', marginTop: 4 }}>{t('tryout_free_note')}</Mono>
                </div>
              ) : p.selFactory ? (
                <div className="col" style={{ marginTop: 8 }}>
                  <span style={{ color: 'var(--green)', fontWeight: 700, fontSize: 15 }}>+{formatCreditsCr(p.factorySalary)}</span>
                  <Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{t('salary_per_race')}</Mono>
                  <Mono style={{ fontSize: 9.5, color: 'var(--text-3)', marginTop: 4 }}>{t('factory_salary_note')}</Mono>
                </div>
              ) : p.freeEntry ? (
                <Mono style={{ fontSize: 12, color: 'var(--green)', display: 'block', marginTop: 8 }}>{t('free_measurement')}</Mono>
              ) : (
                <div className="col" style={{ marginTop: 8, gap: 3 }}>
                  {costRows.filter(([, v]) => v > 0).map(([k, v]) => (
                    <div key={k} className="row between"><Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t(k)}</Mono><Mono style={{ fontSize: 10, color: 'var(--text-2)' }}>{formatCreditsCr(v)}</Mono></div>
                  ))}
                  {p.coverAmt > 0 && <div className="row between"><Mono style={{ fontSize: 10, color: 'var(--green)' }}>{t('covered_by_team')}</Mono><Mono style={{ fontSize: 10, color: 'var(--green)' }}>−{formatCreditsCr(p.coverAmt)}</Mono></div>}
                  <div className="row between" style={{ marginTop: 4, paddingTop: 4, borderTop: '1px solid var(--line)' }}><Mono style={{ fontSize: 10, fontWeight: 700 }}>{t('you_pay')}</Mono><Mono style={{ fontSize: 11, fontWeight: 700, color: 'var(--accent-text)' }}>{formatCreditsCr(youPay)}</Mono></div>
                </div>
              )}
            </div>
          </div>

          {/* livery picker: real thumbnails (falls back to a color swatch if there's no image) */}
          {showLivery && (
            <div style={{ padding: '10px 14px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
              <div className="row between">
                <Kicker>{t('livery_sel')}</Kicker>
                <Mono style={{ fontSize: 10, color: liveryLocked ? 'var(--gold)' : 'var(--text-3)' }}>
                  {liveryLocked && <LockIcon size={9} />} {(liveryLocked ? heroOrd + 1 : liveryOrd < 0 ? (p.ownedLiveryOrdinal >= 0 ? p.ownedLiveryOrdinal + 1 : 1) : liveryOrd + 1)}/{p.liveryCount}
                </Mono>
              </div>
              <div className="row gap6" style={{ flexWrap: 'wrap', marginTop: 8 }}>
                {p.liveryNN.slice(0, 24).map((nn, i) => {
                  // locked -> the fixed one highlighted, the rest DIMMED and unclickable (not removed).
                  // unlocked: the default is the marriage's skin (defaultOrd), if there is one.
                  const active = liveryLocked ? i === heroOrd : (liveryOrd < 0 ? i === defaultOrd : liveryOrd === i);
                  const u = ov.url(selSlug ?? '', nn) || detail.url(nn);
                  return (
                    <button key={i} onClick={liveryLocked ? undefined : () => setLiveryOrd(i)} className="focusable" title={`#${nn}`} style={{
                      width: 104, height: 60, borderRadius: 4, padding: 0, overflow: 'hidden',
                      cursor: liveryLocked ? 'not-allowed' : 'pointer',
                      opacity: liveryLocked && !active ? 0.35 : 1,
                      background: u ? 'var(--surface-2)' : `hsl(${(nn * 47) % 360} 45% 40%)`,
                      border: active ? '2px solid var(--accent)' : '1px solid var(--line)',
                    }}>
                      {u && <img src={u} alt="" loading="lazy" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ width: '100%', height: '100%', objectFit: 'cover' }} />}
                    </button>
                  );
                })}
              </div>
            </div>
          )}

          {/* duration % selector */}
          <div data-tour="prerace.duration">
            <Kicker>{t('duration_sel')}</Kicker>
            <div className="row gap6" style={{ marginTop: 8 }}>
              {DUR_PCTS.map((pct) => {
                const sel = effDur === pct;
                // a % that would fall below the minimum (3 laps / 8 min) -> button disabled. 100% is never disabled.
                const scaled = isTime ? Math.floor(p.durationMin * pct / 100) : Math.round(p.laps * pct / 100);
                const belowMin = pct < 100 && (isTime ? scaled < DUR_MIN_MIN : scaled < DUR_MIN_LAPS);
                const disabled = p.lockDuration || belowMin;
                return <button key={pct} disabled={disabled} onClick={() => setDurPct(pct)} className="focusable"
                  title={belowMin ? (isTime ? `< ${DUR_MIN_MIN} min` : `< ${DUR_MIN_LAPS} ${t('laps')}`) : undefined}
                  style={{ flex: 1, padding: '7px 0', borderRadius: 3, fontSize: 12, cursor: disabled ? 'not-allowed' : 'pointer', opacity: disabled && !sel ? 0.4 : 1, background: sel ? 'var(--accent)' : 'var(--surface)', color: sel ? '#fff' : 'var(--text-2)', border: sel ? '1px solid var(--accent)' : '1px solid var(--line)' }}>{pct}%</button>;
              })}
            </div>
            {/* the number shown LARGE, same pattern as the event screen's data band (kicker + a large display value
                + a small mono subtext). Before, this was an 11px mono value shown in passing, and people ended up entering races triple
                the length they thought they were signing up for. */}
            <div className="col" style={{ gap: 3, marginTop: 10 }}>
              <span data-testid="prerace-duration-big"><Display style={{ fontSize: 26, lineHeight: 1 }}>{durReadout}</Display></span>
              <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{effDur}%</Mono>
            </div>
            {/* wear/fuel consumption that will apply with the chosen duration (for planning pit stops and fuel load) */}
            <WearNote p={p} pct={effDur} />
          </div>

          {/* actions */}
          <div style={{ marginTop: 'auto', paddingTop: 4 }} data-tour="prerace.actions">
            {p.selFactory ? (
              <Btn variant="primary" size="lg" style={{ width: '100%' }} disabled={!p.seatOk || busy} onClick={() => requestLaunch('own')}>{p.seatOk ? `${t('race_with')} →${divorceSuffix}` : t('factory_need_rep')}</Btn>
            ) : p.isMemory ? (
              // Memories 1:1: an owned unit (shown car+skin) -> RUN; a different skin -> BUY that unit.
              memUnitOwned ? (
                <Btn variant="primary" size="lg" style={{ width: '100%' }} disabled={!p.canRaceOwn || busy} onClick={() => requestLaunch('own')}>{p.canRaceOwn ? `${t('race_with')} →` : needLabel(netOwn)}</Btn>
              ) : (
                <Btn variant="primary" size="lg" style={{ width: '100%' }} disabled={!p.canBuy || busy} onClick={() => setPendingBuy(heroOrd)}>{t('buy')} · {formatCreditsCr(p.cars.find((c) => c.id === p.selCarId)?.price ?? 0)}</Btn>
              )
            ) : (unitOwned || p.lockCar) ? (
              <Btn variant="primary" size="lg" style={{ width: '100%' }} disabled={!p.canRaceOwn || busy} onClick={() => requestLaunch('own')}>{p.canRaceOwn ? `${t('race_with')} →${divorceSuffix}` : needLabel(netOwn)}</Btn>
            ) : (
              <div className="row gap8">
                {buyable && (
                  <Btn variant="ghost" size="lg" style={{ flex: 1 }} disabled={!p.canBuy || busy} onClick={() => setPendingBuy(heroOrd)}>{t('buy')} · {formatCreditsCr(selCarInfo?.price ?? 0)}</Btn>
                )}
                {/* Memories: era series = buy-to-own, no renting */}
                {!p.isMemory && (
                  <Btn variant="primary" size="lg" style={{ flex: 1 }} disabled={!p.canRent || busy} onClick={() => requestLaunch('rent')}>{p.canRent ? `${t('rent')} · ${formatCreditsCr(p.rentCost)}${divorceSuffix}` : needLabel(netRent)}</Btn>
                )}
              </div>
            )}
            {/* DIVORCE warning — you're racing under a different identity while married to a (car, skin) pair.
                It promises what's real: money at settlement + standing with sponsors (the internal REP drops, but its
                number isn't shown on any screen and isn't quoted). There's no team standing you up: the privateer's
                marriage is to your own colors, not to a team — which is why there are no relationship marks here. */}
            {divorcing && (
              <Mono style={{ display: 'block', textAlign: 'center', fontSize: 10, color: 'var(--amber)', marginTop: 7 }}>
                {t('divorce_warn', { cr: formatCreditsCr(p.divorceCr) })}
              </Mono>
            )}
            {/* estimated TOTAL cost of the race, below the buttons (the button's price tag is only the
                rental/purchase cost; this is everything: entry + signup fee + car + travel). Free races (tryout/calibration/factory)
                don't apply here. Per feedback: the NUMBER stands out (it's the important data), the label stays small. */}
            {!isTryout && !p.selFactory && !p.freeEntry && (
              <div className="row" style={{ justifyContent: 'center', alignItems: 'baseline', gap: 6, marginTop: 7 }}>
                <Mono style={{ fontSize: 15, fontWeight: 700, color: 'var(--text-2)' }}>
                  {formatCreditsCr(youPay)}
                </Mono>
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('race_total_cost_label')}</Mono>
              </div>
            )}
          </div>
        </div>
      </div>

      {pendingAction && (
        <LaunchNotice onDone={() => { const a = pendingAction; setPendingAction(null); fire(a); }} />
      )}
      {/* buying is irreversible and the button sits right next to renting. One more step before spending. */}
      {pendingBuy != null && (
        <BuyConfirm
          carName={p.cars.find((c) => c.id === p.selCarId)?.name ?? ''}
          price={p.cars.find((c) => c.id === p.selCarId)?.price ?? 0}
          credits={p.credits}
          busy={busy}
          onCancel={() => setPendingBuy(null)}
          onConfirm={() => { const liv = pendingBuy; setPendingBuy(null); buy(liv); }}
        />
      )}
    </div>
  );
}
