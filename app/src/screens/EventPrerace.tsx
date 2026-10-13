// BESPOKE pre-race screen for special events. Editorial redesign (new design system, 2026-07-15):
//  LEFT — a LIVE hero (day/night gradient + giant, dim circuit map + render of the chosen car/skin +
//  subtle rain if stormy) with title, window, and "ends in N days"; a LARGE data band (circuit/date/
//  format/prize); conditions shown as SVG glyphs (weather/time/grid/distance) and a long narrative.
//  RIGHT — class tabs (multiclass) → car list with real renders + micro-bars for power/grip/balance → skin
//  picker (real thumbnails; the hero reflects the choice) → actions.
// Mechanism (unchanged): the event is ARMED as a standalone race via cmd.event.arm{eventId,carClassId} on
// mount and on every tab change; only once armed:true does query.prerace refresh and the list render. The economics are
// resolved by the ENGINE (query.prerace/cmd.prerace.buy/launch) — nothing is recalculated here: the event discount is APPLIED
// in the engine to rent/purchase (rentCost, canBuy, outlays already come discounted); the PER-CARD price (car.price)
// arrives at full price, so the "before/after" strikethrough uses the SAME formula as the engine (round(price*(100-d)/100)).
import { useEffect, useState, type CSSProperties } from 'react';
import { logImageError } from '../support/supportLog';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, PreracePayload, PreraceCar } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import type { SpecialEvent } from '../events/eventsClient';
import { eventTitle, eventDesc, eventBody, weatherI18nKey, weatherFirstSlot, weatherIsWet, eventClassColor, eventStatus, eventStartsIn, eventCredits } from '../events/eventHelpers';
import { Display, Mono, Kicker, Chip, Btn, Panel, AccentDot, LockIcon } from '../ui/components';
import { classDisplayName } from '../ui/classNames';
import { formatCreditsCr } from '../ui/format';
import { BuyConfirm } from '../ui/BuyConfirm';   // buying a car doesn't happen on the first click
import { useCarLiveries, useOverridePreviews } from '../ui/vehImages';
import { useTrackMapUrl } from '../ui/trackImages';
import { eventBlock, useEventsContent } from '../events/eventContent';
import { lastDurPct, setLastDurPct, launchNoticeEnabled } from '../ui/prefs';
import { WearNote } from '../ui/WearNote';   // wear/fuel consumption for the chosen duration %
import { LaunchNotice } from '../ui/LaunchNotice';   // "hands off" warning before assembling the race
import { useScreenCrumb } from '../support/useScreenCrumb';

// duration % selector (same as the regular pre-race screen). DIFFERENCE for events: the reward (credits+REP) is DIVIDED
// by the chosen % -> running a shorter version pays proportionally less. The engine applies this at settlement.
const DUR_PCTS = [5, 10, 25, 50, 75, 100];
const DUR_MIN_LAPS = 3;   // minimum race: 3 laps / 8 min; below that the button is disabled (same as the regular pre-race screen)
const DUR_MIN_MIN = 8;

// condition glyphs (thin line, no emoji — a design-system rule). They inherit color via currentColor.
function WxIcon({ kind, size = 22 }: { kind: string; size?: number }) {
  const s: CSSProperties = { width: size, height: size, flex: 'none' };
  const P = (ds: string[]) => (
    <svg viewBox="0 0 24 24" style={s} fill="none" stroke="currentColor" strokeWidth={1.6} strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      {ds.map((d, i) => <path key={i} d={d} />)}
    </svg>
  );
  switch (kind) {
    case 'storm': return P(['M17 17a4 4 0 0 0 0-8 5.5 5.5 0 0 0-10.6 1.6A3.5 3.5 0 0 0 7 17.5h1.2', 'M12 13l-2.2 4h3l-1.8 3.6']);
    case 'sun': return P(['M12 17a5 5 0 1 0 0-10 5 5 0 0 0 0 10z', 'M12 2v2.2M12 19.8V22M2 12h2.2M19.8 12H22M4.9 4.9l1.6 1.6M17.5 17.5l1.6 1.6M19.1 4.9l-1.6 1.6M6.5 17.5l-1.6 1.6']);
    case 'fog': return P(['M4 10h13M6 13.5h14M4 17h12']);
    case 'dice': return P(['M4 4h16v16H4z', 'M9 9h.01M15 15h.01M15 9h.01M9 15h.01M12 12h.01']);
    case 'moon': return P(['M20 13.5A8 8 0 0 1 10.5 4 6.8 6.8 0 1 0 20 13.5z']);
    case 'clock': return P(['M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18z', 'M12 7v5l3.2 1.8']);
    case 'flagd': return P(['M5 3v18', 'M5 4h13l-2.6 3.5L18 11H5']);
    case 'lock': return P(['M6 11h12v9H6z', 'M8.5 11V8a3.5 3.5 0 0 1 7 0v3']);
    default: return null;
  }
}
// weather glyph (canonical catalog, see eventHelpers.WEATHER_I18N_KEY): cloudy->sun, rain/storm->storm,
// dry fog->fog, wet-with-fog->storm, random->dice. Resolved from the 1st slot of the override.
const WX_GLYPH: Record<string, string> = {
  '0': 'sun', '1': 'sun', '2': 'sun', '3': 'sun', '4': 'sun',
  '5': 'storm', '6': 'storm', '7': 'storm', '8': 'storm',
  '12': 'fog', '13': 'storm', '14': 'fog', '15': 'storm', '16': 'fog', '17': 'dice',
};

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

// event car card: real render (first livery) + name/year/price (before/after strikethrough if discounted)
// + power/grip/balance micro-bars + class corner mark (green if owned). Defined at module level (not nested in the screen) so
// the useCarLiveries hook doesn't remount on every parent render. Discounted price = same formula as the engine.
function EvCarTile({ c, dataDir, sel, classColor, disc, locked = false, onSelect }: {
  c: PreraceCar; dataDir?: string; sel: boolean; classColor: string; disc: number; locked?: boolean; onSelect: () => void;
}) {
  const { t } = useTranslation();
  const { nns, url } = useCarLiveries(dataDir, c.slug);
  const ov = useOverridePreviews(dataDir);   // "real skins": installed skin first, falls back to vanilla
  const thumb = nns.length ? (ov.url(c.slug, nns[0]) || url(nns[0])) : '';
  const discounted = disc > 0 ? Math.round(c.price * (100 - disc) / 100) : c.price;
  return (
    <button onClick={locked ? undefined : onSelect} className="focusable" style={{
      position: 'relative', display: 'flex', alignItems: 'center', textAlign: 'left', padding: 0, overflow: 'hidden',
      flex: 'none', cursor: locked ? 'not-allowed' : 'pointer', opacity: locked ? 0.45 : 1,
      background: sel ? 'color-mix(in oklab, var(--accent) 8%, var(--surface))' : 'var(--surface)',
      border: sel ? '1px solid var(--accent)' : '1px solid var(--line)',
      boxShadow: sel ? '0 0 0 1px var(--accent)' : 'none',
    }}>
      <span style={{ position: 'absolute', left: 0, top: 0, width: 14, height: 14, clipPath: 'polygon(0 0,100% 0,0 100%)', background: c.owned ? 'var(--green)' : classColor, zIndex: 1 }} />
      <div style={{ width: 116, flex: 'none', height: 60, padding: '6px 4px 6px 12px', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        {thumb ? <img src={thumb} alt="" loading="lazy" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ maxWidth: '100%', maxHeight: 50, objectFit: 'contain', filter: 'drop-shadow(0 4px 7px rgba(0,0,0,.5))' }} />
          : <Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{c.classShort}</Mono>}
      </div>
      <div style={{ flex: 1, padding: '10px 10px 10px 4px', minWidth: 0 }}>
        <div className="display" style={{ fontSize: 15, lineHeight: 1, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{c.name}</div>
        <div className="row gap8" style={{ alignItems: 'center', marginTop: 5 }}>
          <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{c.year || ''}</Mono>
          {locked ? <Mono style={{ fontSize: 9, color: 'var(--gold)' }}><LockIcon size={9} /> {t('locked')}</Mono>
            : c.owned ? <Mono style={{ fontSize: 9, color: 'var(--green)' }}>{t('owned_tag')}</Mono>
            : c.factory ? <Mono style={{ fontSize: 9, color: 'var(--gold)' }}>{t('by_contract')}</Mono>
            : disc > 0 ? (
              <span className="mono" style={{ fontSize: 11 }}>
                <s style={{ color: 'var(--text-3)', marginRight: 5 }}>{formatCreditsCr(c.price)}</s>
                <span style={{ color: 'var(--gold)' }}>{formatCreditsCr(discounted)}</span>
              </span>
            ) : <span className="mono" style={{ fontSize: 11, color: 'var(--gold)' }}>{formatCreditsCr(c.price)}</span>}
        </div>
      </div>
      <div className="col" style={{ gap: 3, paddingRight: 14, flex: 'none' }}>
        {([['pwr', 'var(--accent)'], ['grp', 'var(--cyan)'], ['bal', 'var(--green)']] as const).map(([k, col]) => (
          <span key={k} style={{ width: 34, height: 3, background: 'var(--surface-3)', display: 'block' }}>
            <span style={{ width: `${Math.min(100, c[k])}%`, height: '100%', background: col, display: 'block' }} />
          </span>
        ))}
      </div>
    </button>
  );
}

export function EventPrerace({ client, dataDir, event, onFired, onBack }: {
  client: ApiClientLike; dataDir: string; event: SpecialEvent; onFired: () => void; onBack: () => void;
}) {
  const { t, i18n } = useTranslation();
  useScreenCrumb(client, 'event prerace', event.id);   // audit note, 07/20
  const payload = event.payload;
  const classes = payload?.classes ?? [];
  const multi = classes.length > 1;

  const [classIdx, setClassIdx] = useState(0);
  const [armed, setArmed] = useState(false);
  const [armFail, setArmFail] = useState(false);   // the engine rejected the arm (class/track not resolvable) -> it's stated
  const status = eventStatus(event);               // window in REAL time: outside it the engine doesn't have the event
  const [selCarId, setSelCarId] = useState<string | undefined>(undefined);
  const [liveryOrd, setLiveryOrd] = useState<number>(-1);   // 1 = no explicit choice made (opt-in), same as the regular pre-race screen
  const [busy, setBusy] = useState(false);
  // Same as the regular pre-race screen: clicking Rent/Run shows the "hands off" warning, which closes itself
  // and is what actually releases the race. Grouped with the rest of the hooks, since loading early-returns follow below.
  const [pendingAction, setPendingAction] = useState<'own' | 'rent' | null>(null);
  // With the warning turned off ("don't show again" checked), the click launches directly, as before it existed.
  const requestLaunch = (action: 'own' | 'rent') => (launchNoticeEnabled() ? setPendingAction(action) : fire(action));
  const [durPct, setDurPct] = useState<number>(() => lastDurPct());   // chosen duration % (divides the reward)

  const q = useQuery<PreracePayload>(client, 'query.prerace', selCarId ? { carId: selCarId } : undefined);
  const p = q.data;
  const activeClass = classes[classIdx];

  // photos of the SELECTED car (hero + livery picker). Placed before the `ready` guard (Rules of Hooks); slug
  // undefined -> url()='' (placeholder). The on-disk NN ordering matches the engine's own ordinals.
  const selSlug = p?.cars?.find((c) => c.id === p?.selCarId)?.slug;
  const carLiveries = useCarLiveries(dataDir, selSlug);
  const ov = useOverridePreviews(dataDir);   // "real skins": installed skin first (hero + thumbnails), falls back to vanilla
  const trackMapUrl = useTrackMapUrl(dataDir);
  // DLC: what the player is missing for THIS event. Besides the event-level lock (track, or no class
  // available at all), it also checks the ACTIVE tab's class: other classes may be playable while this one isn't.
  const content = useEventsContent(client);
  const contentRow = content.rowFor(event.id);
  const contentBlock = (() => {
    const base = eventBlock(contentRow);
    if (base.blocked) return base;
    const sel = contentRow?.classes.find((c) => c.id === classes[classIdx]);
    return sel && !sel.ok ? { blocked: true, product: sel.product } : base;
  })();

  // arms the event for the ACTIVE class: on mount and on every classIdx change. Only once armed:true does the
  // pre-race data refresh — until then the guard keeps showing loading. (identical to the previous flow.)
  // The engine ONLY registers events with a live window (the loader filters by real time), so outside the
  // window the arm call returns armed:false: without an armFail state the screen would sit in "loading" forever, looking like
  // the event had no cars. Outside the window it doesn't even try; inside it, a real failure is shown as a failure.
  useEffect(() => {
    let alive = true;
    setArmed(false);
    setArmFail(false);
    const carClassId = classes[classIdx];
    if (!carClassId || status !== 'live') return;
    void client.cmd<{ ok: boolean; armed: boolean }>('cmd.event.arm', { eventId: event.id, carClassId })
      .then((r) => {
        if (!alive) return;
        if (r && r.armed) { setArmed(true); q.refetch(); } else setArmFail(true);
      })
      .catch(() => { if (alive) setArmFail(true); });
    return () => { alive = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [classIdx, event.id, status]);

  // duration: if the event locks it, use the preset; otherwise remember the last free choice and discard any % that
  // would fall below the minimum (3 laps / 8 min) on (re)arm. Identical to the regular pre-race screen.
  useEffect(() => { if (p?.lockDuration) setDurPct(p.presetDurPct); }, [p?.lockDuration, p?.presetDurPct]);
  useEffect(() => {
    if (!p || p.lockDuration) return;
    const isT = p.durationMin > 0;
    const below = (pct: number) => pct < 100 && (isT ? Math.floor(p.durationMin * pct / 100) < DUR_MIN_MIN : Math.round(p.laps * pct / 100) < DUR_MIN_LAPS);
    const avail = DUR_PCTS.filter((pct) => pct === 100 || !below(pct));
    const remembered = lastDurPct();
    setDurPct(avail.includes(remembered) ? remembered : avail[0]);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [p?.trackHash, p?.className, p?.raceName, p?.durationMin, p?.laps, p?.lockDuration]);

  const selectClass = (i: number) => { setClassIdx(i); setSelCarId(undefined); setLiveryOrd(-1); };
  const selectCar = (id: string) => { setSelCarId(id); setLiveryOrd(-1); };

  // fire/buy identical to the regular pre-race screen. Duration IS selectable (durPct); for events it DIVIDES the reward (the engine
  // applies it at settlement). If the event locks duration, use the preset. The livery is passed along (liveryOrd; -1 = engine default).
  const fire = (action: 'own' | 'rent') => {
    if (!p || busy) return;
    setBusy(true);
    const dp = p.lockDuration ? p.presetDurPct : durPct;
    if (!p.lockDuration) setLastDurPct(durPct);   // remembers the FREE choice (not the locked preset)
    void client.cmd('cmd.prerace.launch', { carId: p.selCarId, action, livery: liveryOrd, durationPct: dp })
      .then(() => onFired()).catch(() => setBusy(false));
  };
  const [pendingBuy, setPendingBuy] = useState(false);   //
  const buy = () => {
    if (!p || busy) return;
    setBusy(true);
    void client.cmd('cmd.prerace.buy', { carId: p.selCarId, livery: liveryOrd }).then(() => q.refetch()).catch(() => q.refetch()).finally(() => setBusy(false));
  };
  const needLabel = (total: number) => `${t('need_credits')} · ${formatCreditsCr(total)}`;

  // event facts (with fallback to event.payload so the hero/band render during the arm, before `p` exists) ----
  const disc = payload?.discount_pct ?? 0;
  const weatherRaw = payload?.overrides?.weather;
  const hourRaw = payload?.overrides?.hour;
  const night = hourRaw !== undefined && parseInt(hourRaw, 10) >= 19;
  const storm = weatherIsWet(weatherRaw);
  // 23/09: while being armed, the engine's name (not the raw payload key)
  const trackName = p?.trackName || content.trackNameFor(event.id, payload?.track);
  const grid = p?.grid ?? 0;
  // The prize purse comes from the EVENT, never the regular pre-race screen: the engine sets pr.reward=0 when arming an event
  // (in the event-arming code) because the payout isn't taken from the position-based prize ladder but paid as a
  // flat bonus at settlement (from ev->reward_credits, which comes from this SAME rewards.credits value).
  // Reading it from p with `??` — which only falls back to the alternate on null/undefined — let that 0 win, and the band
  // would announce "Cr 0" for a 40,000-credit event. eventCredits = the same source the hub and the grid use.
  const reward = eventCredits(event);
  const laps = p?.laps ?? payload?.laps ?? 0;
  const durationMin = p?.durationMin ?? payload?.duration_min ?? 0;
  const isTime = durationMin > 0;
  const effDur = p?.lockDuration ? (p.presetDurPct ?? durPct) : durPct;   // effective duration % (locked preset or free choice)
  const scaledReward = Math.round(reward * effDur / 100);                  // events: the reward is DIVIDED by the duration
  // 19/09 (tester: finished P9, collected ₡51,104 and on winning afterwards collected nothing with the band promising ₡250,000).
  // The engine now pays the DIFFERENCE with what was already collected in this event; here it says how much you have so that the
  // band's prize does not look like it is added in full again.
  const eventPaid = p?.eventPaid ?? 0;
  const effLen = Math.round((isTime ? durationMin : laps) * effDur / 100); // effective laps/minutes actually run
  const durReadout = isTime ? `${effLen} ${t('dur_min')}` : `${effLen} ${t('laps')}`;
  const effPrice = (price: number) => (disc > 0 ? Math.round(price * (100 - disc) / 100) : price);

  const title = eventTitle(event, i18n.language);
  const desc = eventDesc(event, i18n.language);
  const body = eventBody(event, i18n.language);
  const windowLabel = t('events.window', { from: event.starts_at.slice(0, 10), to: event.ends_at.slice(0, 10), defaultValue: '{{from}} → {{to}}' });
  const shortWin = `${event.starts_at.slice(5, 10)} → ${event.ends_at.slice(5, 10)}`;   // MM-DD -> MM-DD (fits in the band)
  const eventYear = event.starts_at.slice(0, 4);
  const daysLeft = Math.max(0, Math.ceil((new Date(event.ends_at).getTime() - Date.now()) / 86400000));

  const heroClassColor = activeClass ? eventClassColor(activeClass) : 'var(--accent)';
  const wxGlyph = WX_GLYPH[weatherFirstSlot(weatherRaw)] ?? 'sun';
  const wxKey = weatherI18nKey(weatherRaw);
  const wxLabel = wxKey ? t(wxKey, { defaultValue: weatherRaw ?? '—' }) : (weatherRaw ?? '—');
  const mapUrl = trackMapUrl(payload?.track);

  // HERO livery ordinal: locked preset > user's choice > default (owned car or the first one).
  const liveryLocked = p?.lockSkin ?? false;
  const defaultLiveryOrd = (p?.ownedLiveryOrdinal ?? -1) >= 0 ? (p as PreracePayload).ownedLiveryOrdinal : 0;
  const heroLiveryOrd = liveryLocked && (p?.presetLivery ?? -1) >= 0 ? (p as PreracePayload).presetLivery
    : liveryOrd >= 0 ? liveryOrd : defaultLiveryOrd;
  const heroNN = p?.liveryNN ? (p.liveryNN[heroLiveryOrd] ?? p.liveryNN[0]) : undefined;   // an empty-class payload may not include liveryNN
  const heroImg = heroNN != null ? (ov.url(selSlug ?? '', heroNN) || carLiveries.url(heroNN)) : '';

  // conditions shown LARGE with glyphs (feedback: readable weather, never the raw "8" code).
  const conds = ([
    { glyph: wxGlyph, k: t('events.cond_weather', { defaultValue: 'Weather' }), v: wxLabel, tint: storm ? 'var(--cyan)' : undefined },
    hourRaw !== undefined && { glyph: night ? 'moon' : 'clock', k: t('events.cond_startTime', { defaultValue: 'Start time' }), v: hourRaw + (night ? ` · ${t('events.night', { defaultValue: 'Night' })}` : '') },
    { glyph: 'flagd', k: t('events.cond_grid', { defaultValue: 'Grid' }), v: grid > 0 ? `${grid}${multi ? ` · ${classes.length} ${t('events.classesWord', { defaultValue: 'classes' })}` : ` ${t('events.carsWord', { defaultValue: 'cars' })}`}` : '—' },
    { glyph: 'flagd', k: t('events.cond_distance', { defaultValue: 'Distance' }), v: `${effLen} ${isTime ? t('dur_min') : t('laps')}${effDur < 100 ? ` · ${effDur}%` : ''}` },
  ].filter(Boolean) as Array<{ glyph: string; k: string; v: string; tint?: string }>);

  // data band shown LARGE (feedback: "make the track, laps/time and date bigger").
  const band = [
    { k: t('events.stat_track', { defaultValue: 'Track' }), v: trackName, sub: multi ? `${classes.length} ${t('events.classesWord', { defaultValue: 'classes' })}` : classDisplayName(activeClass) },
    { k: t('events.stat_date', { defaultValue: 'Event date' }), v: shortWin, sub: eventYear },
    { k: isTime ? t('events.stat_duration', { defaultValue: 'Duration' }) : t('events.stat_laps', { defaultValue: 'Laps' }), v: String(effLen), sub: isTime ? t('dur_min') : t('laps') },
    { k: t('events.stat_prize', { defaultValue: 'Prize' }), v: formatCreditsCr(scaledReward), sub: effDur < 100 ? `${effDur}% · ${t('events.prizeWinner', { defaultValue: 'winner' })}` : t('events.prizeWinner', { defaultValue: 'winner' }), gold: true },
  ];

  // LEFT (editorial hero): doesn't depend on pre-race state -> computed once and reused in both the loading
  // guard and the final render (same layout, no duplicated JSX). During the arm, p is undefined and the hero's
  // car doesn't show yet (just gradient + map); once armed, the car/skin render appears.
  const leftEditorial = (
    <div className="col scroll" style={{ minHeight: 0, borderRight: '1px solid var(--line)' }}>
      {/* live HERO */}
      <div style={{
        position: 'relative', height: 300, flex: 'none', overflow: 'hidden',
        // event sky via palette variables (--sky-*): cold blue night or dirty amber sunset
        background: night
          ? 'linear-gradient(158deg,var(--sky-night-1) 0%,var(--sky-night-2) 46%,#06080e 100%)'
          : 'linear-gradient(158deg,var(--sky-dusk-1) 0%,var(--sky-dusk-2) 52%,#08080a 100%)',
        filter: status === 'upcoming' ? 'saturate(.45)' : undefined,
        opacity: status === 'upcoming' ? 0.78 : 1,
      }}>
        <div style={{ position: 'absolute', inset: 0, background: `radial-gradient(70% 90% at 78% 10%, color-mix(in oklab, ${heroClassColor} 16%, transparent), transparent 65%)` }} />
        {mapUrl && <img src={mapUrl} alt="" draggable={false} onError={(e) => logImageError(null, e.currentTarget.src)} style={{ position: 'absolute', right: -30, top: -40, height: 360, opacity: 0.14, transform: 'rotate(6deg)' }} />}
        {storm && <div style={{ position: 'absolute', inset: 0, background: 'repeating-linear-gradient(118deg, transparent 0 7px, var(--sky-rain) 7px 8px)' }} />}
        {heroImg && (
          <div style={{ position: 'absolute', right: 18, bottom: 2, width: '60%', pointerEvents: 'none' }}>
            <img src={heroImg} alt="" onError={(e) => logImageError(null, e.currentTarget.src)} style={{ width: '100%', height: 168, objectFit: 'contain', objectPosition: 'bottom right', filter: 'drop-shadow(0 16px 24px rgba(0,0,0,.65))' }} />
          </div>
        )}
        <div style={{ position: 'absolute', inset: 0, background: 'linear-gradient(to top, var(--bg) 4%, transparent 55%)' }} />
        <button className="btn btn-sm btn-ghost" data-gamepad-back onClick={onBack} style={{ position: 'absolute', top: 14, left: 16, zIndex: 3 }}>← {t('common.back')}</button>
        <div className="col" style={{ position: 'absolute', left: 24, right: 24, bottom: 14, gap: 8, zIndex: 2 }}>
          <div className="row gap8" style={{ alignItems: 'center', flexWrap: 'wrap' }}>
            <Chip kind="gold">{t('events.tag', { defaultValue: 'SPECIAL EVENT' })}</Chip>
            {status === 'upcoming' && (
              <Chip kind="lock" style={{ fontSize: 9 }}>{t('events.notYetAvailable', { defaultValue: 'NOT YET AVAILABLE' })}</Chip>
            )}
            <Mono style={{ fontSize: 11, color: 'var(--text-2)', letterSpacing: '.12em' }}>{windowLabel}</Mono>
            <Chip kind={daysLeft <= 2 ? 'accent' : 'lock'} style={{ fontSize: 9 }}>{t('events.endsIn', { count: daysLeft })}</Chip>
          </div>
          <Display as="h1" style={{ fontSize: 40, fontStyle: 'italic', lineHeight: 0.92, textShadow: '0 2px 14px rgba(0,0,0,.85)' }}>{title}</Display>
          <div className="row gap8" style={{ alignItems: 'center', flexWrap: 'wrap' }}>
            {classes.map((k) => (
              <span key={k} className="row gap8" style={{ alignItems: 'center', padding: '3px 9px', background: 'rgba(0,0,0,.45)', border: `1px solid color-mix(in oklab, ${eventClassColor(k)} 55%, transparent)` }}>
                <AccentDot color={eventClassColor(k)} size={8} />
                <Mono style={{ fontSize: 10, letterSpacing: '.12em', color: eventClassColor(k) }}>{classDisplayName(k)}</Mono>
              </span>
            ))}
            {!!desc && <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{desc}</Mono>}
          </div>
        </div>
      </div>

      {/* data band */}
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', flex: 'none', borderTop: '1px solid var(--line)', borderBottom: '1px solid var(--line)' }}>
        {band.map((s, i) => (
          <div key={s.k} className="col" style={{ gap: 5, padding: '15px 18px', borderRight: i < 3 ? '1px solid var(--line)' : 'none', minWidth: 0 }}>
            <Kicker style={{ fontSize: 9.5 }}>{s.k}</Kicker>
            <Display style={{ fontSize: 20, lineHeight: 1, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', color: s.gold ? 'var(--gold)' : 'var(--text)' }}>{s.v}</Display>
            <Mono style={{ fontSize: 10, color: 'var(--text-3)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{s.sub}</Mono>
          </div>
        ))}
      </div>
      {eventPaid > 0 && (
        <div style={{ flex: 'none', padding: '9px 18px', borderBottom: '1px solid var(--line)' }}>
          <Mono style={{ fontSize: 11, color: 'var(--gold)' }}>{t('events.alreadyEarned', { amount: formatCreditsCr(eventPaid) })}</Mono>
        </div>
      )}

      <div className="col" style={{ gap: 18, padding: '20px 26px 26px' }}>
        {/* conditions with glyphs */}
        <div>
          <div className="row between" style={{ marginBottom: 10, alignItems: 'center' }}>
            <Kicker>{t('events.conditions', { defaultValue: 'CONDITIONS' })}</Kicker>
            {disc > 0 && <Chip kind="gold" style={{ fontSize: 9 }}>{t('events.discountChip', { pct: disc, defaultValue: 'BUY & RENT −{{pct}}% THIS EVENT' })}</Chip>}
          </div>
          <div style={{ display: 'grid', gridTemplateColumns: `repeat(${conds.length}, 1fr)`, gap: 8 }}>
            {conds.map((cd, i) => (
              <Panel key={i} style={{ padding: '13px 14px', display: 'flex', flexDirection: 'column', gap: 8, background: 'var(--bg-2)' }}>
                <span style={{ color: cd.tint ?? 'var(--text-2)' }}><WxIcon kind={cd.glyph} size={22} /></span>
                <div className="col" style={{ gap: 3 }}>
                  <Display style={{ fontSize: 15, lineHeight: 1.05 }}>{cd.v}</Display>
                  <Mono style={{ fontSize: 9.5, letterSpacing: '.1em', color: 'var(--text-3)' }}>{cd.k.toUpperCase()}</Mono>
                </div>
              </Panel>
            ))}
          </div>
        </div>

        {/* long narrative */}
        {!!body && <div style={{ whiteSpace: 'pre-line', fontSize: 14.5, lineHeight: 1.65, color: 'var(--text-2)', maxWidth: 640 }}>{body}</div>}
      </div>
    </div>
  );

  // right-side header: "your entry" + your credits (once armed). Class tabs: presentable name + identity
  // dot; the full name (classDisplayName) is the button's accessible name.
  const header = (
    <div className="row between" style={{ alignItems: 'baseline', flex: 'none' }}>
      <Display style={{ fontSize: 20 }}>{t('events.yourEntry', { defaultValue: 'YOUR ENTRY' })}</Display>
      {p && <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{formatCreditsCr(p.credits)}</Mono>}
    </div>
  );

  const classTabs = multi ? (
    <div className="row" style={{ gap: 6, flex: 'none' }}>
      {classes.map((cls, i) => {
        const on = i === classIdx;
        const col = eventClassColor(cls);
        return (
          <button key={cls} onClick={() => selectClass(i)} className="focusable" style={{
            flex: 1, padding: '9px 11px', cursor: 'pointer', textAlign: 'left',
            background: on ? `color-mix(in oklab, ${col} 14%, var(--surface))` : 'var(--surface)',
            border: on ? `1px solid ${col}` : '1px solid var(--line)',
            boxShadow: on ? `inset 0 3px 0 ${col}` : 'none',
          }}>
            <span className="row gap8" style={{ alignItems: 'center' }}>
              <AccentDot color={col} size={9} />
              <span className="display" style={{ fontSize: 15, color: on ? 'var(--text)' : 'var(--text-2)' }}>{classDisplayName(cls)}</span>
            </span>
          </button>
        );
      })}
    </div>
  ) : activeClass ? (
    <div className="row gap8" style={{ alignItems: 'center', flex: 'none' }}>
      <AccentDot color={heroClassColor} size={9} />
      <Mono style={{ fontSize: 11, letterSpacing: '.12em', color: heroClassColor }}>{classDisplayName(activeClass).toUpperCase()}</Mono>
      <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>· {t('events.singleClass', { defaultValue: 'single event class' })}</Mono>
    </div>
  ) : null;

  // gated by PRESENCE (armed + data), not by q.loading: during a refetch (picking a car / after a purchase) useQuery
  // keeps `p` around, so the list/buttons don't flicker. Switching CLASS does re-show loading (the arm resets armed=false).
  const ready = armed && !!p;
  if (!ready || !p) {
    // why there are no cars: outside the window / arm rejected / still loading. Never a perpetual "loading".
    const startsIn = status === 'upcoming' ? eventStartsIn(event) : null;
    const notice =
      status === 'upcoming' ? { title: t('events.notLiveTitle'), body: t('events.notLiveBody', { when: t(startsIn!.key, { count: startsIn!.count }).toLowerCase() }) }
      : status === 'ended' ? { title: t('events.endedTitle'), body: t('events.endedBody') }
      // DLC: the arm is also cancelled when the player is missing the track or class for THIS event. Just saying
      // "couldn't set up the race" there would be half a lie: it states what content is missing and, if the engine knows, exactly which.
      : armFail && contentBlock.blocked
        ? { title: contentBlock.product ? t('events.needsDlcTitle', { product: contentBlock.product }) : t('events.needsContentTitle'),
            body: contentBlock.product ? t('events.needsDlcBody', { product: contentBlock.product }) : t('events.needsContentBody') }
      : armFail ? { title: t('events.armFailTitle'), body: t('events.armFailBody') }
      : null;
    return (
      <div className="screen" style={{ height: '100%', display: 'grid', gridTemplateColumns: '1.08fr 1fr', minHeight: 0 }}>
        {leftEditorial}
        <div className="col" style={{ minHeight: 0, padding: '18px 26px 20px', gap: 12 }}>
          {header}
          {classTabs}
          {notice ? (
            <Center>
              <div className="col" style={{ gap: 8, maxWidth: 340, textAlign: 'center', alignItems: 'center' }}>
                <Display style={{ fontSize: 17, lineHeight: 1.15 }}>{notice.title}</Display>
                <Mono style={{ fontSize: 11, color: 'var(--text-3)', lineHeight: 1.6 }}>{notice.body}</Mono>
                <Btn variant="ghost" size="sm" onClick={onBack} style={{ marginTop: 4 }}>← {t('common.back')}</Btn>
              </div>
            </Center>
          ) : (
            <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>
          )}
        </div>
      </div>
    );
  }

  // p.cars can be missing in an empty-class payload (the window between setArmed(true) and the class refetch, or a genuinely empty class):
  // guard with ?? [] before filtering. Without this, p.cars.filter throws and crashes the screen (a black-screen regression).
  const cars = p.cars ?? [];
  const owned = cars.filter((c) => c.owned);
  const market = cars.filter((c) => !c.owned);
  const isTryout = p.reasonKey === 'lock_tryout';

  const selCar = cars.find((c) => c.id === p.selCarId);
  const buyFull = selCar?.price ?? 0;
  const buyDisc = effPrice(buyFull);

  const showLivery = !!p.selCarId && p.visual && p.liveryCount > 1;

  return (
    <div className="screen" style={{ height: '100%', display: 'grid', gridTemplateColumns: '1.08fr 1fr', minHeight: 0 }}>
      {leftEditorial}
      <div className="col" style={{ minHeight: 0, padding: '18px 26px 20px', gap: 12 }}>
        {header}
        {classTabs}

        {p.clsEmpty ? (
          <Center><Mono style={{ color: 'var(--text-3)' }}>{t('choose_car_first')}</Mono></Center>
        ) : (
          <>
            {/* T147 (3): same anchor as Prerace.tsx -- the 'prerace' tour is SHARED between the normal pre-race
                and the special-events one (activeTourFor only looks at raceFlow==='prerace', it does not distinguish
                which of the two screens is mounted), so without this data-tour the step was silently filtered out
                and an event's tour was left lame (only the intro, without cars/duration/actions). */}
            <div className="scroll col" data-tour="prerace.cars" style={{ gap: 8, flex: 1, minHeight: 0, paddingRight: 2 }}>
              {owned.length > 0 && <Kicker style={{ color: 'var(--green)' }}>{t('veh_owned')} ({owned.length})</Kicker>}
              {owned.map((c) => <EvCarTile key={c.id} c={c} dataDir={dataDir} sel={c.id === p.selCarId} classColor={heroClassColor} disc={disc} onSelect={() => selectCar(c.id)} />)}
              {market.length > 0 && <Kicker style={{ marginTop: owned.length ? 6 : 0 }}>{t('veh_market')} ({market.length})</Kicker>}
              {/* Memories: buy-to-own -> market cars (not owned) are VISIBLE but LOCKED
                  (not selectable); you can only race what you already own. */}
              {market.map((c) => <EvCarTile key={c.id} c={c} dataDir={dataDir} sel={c.id === p.selCarId} classColor={heroClassColor} disc={disc} locked={!!p.isMemory} onSelect={p.isMemory ? () => {} : () => selectCar(c.id)} />)}
            </div>

            {/* livery picker: real thumbnails (falls back to a swatch if there's no image). The hero reflects the choice. */}
            {showLivery && (
              <Panel style={{ flex: 'none', padding: '11px 14px', background: 'var(--bg-2)' }}>
                <div className="row between">
                  <Kicker>{t('livery_sel')}</Kicker>
                  <Mono style={{ fontSize: 10, color: liveryLocked ? 'var(--gold)' : 'var(--text-3)' }}>
                    {liveryLocked && <LockIcon size={9} />} {heroLiveryOrd + 1}/{p.liveryCount}
                  </Mono>
                </div>
                <div className="row gap6" style={{ flexWrap: 'wrap', marginTop: 8 }}>
                  {p.liveryNN.slice(0, 24).map((nn, i) => {
                    const active = i === heroLiveryOrd;
                    const u = ov.url(selSlug ?? '', nn) || carLiveries.url(nn);
                    return (
                      <button key={i} onClick={liveryLocked ? undefined : () => setLiveryOrd(i)} className="focusable" title={`#${nn}`} style={{
                        width: 72, height: 42, padding: 0, overflow: 'hidden',
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
              </Panel>
            )}

            {/* duration % selector: in EVENTS it divides the reward proportionally. Locked if the event fixes the format. */}
            <Panel data-tour="prerace.duration" style={{ flex: 'none', padding: '11px 14px', background: 'var(--bg-2)' }}>
              <div className="row between">
                <Kicker>{t('duration_sel')}</Kicker>
                <Mono style={{ fontSize: 10, color: effDur < 100 ? 'var(--gold)' : 'var(--text-3)' }}>{durReadout} · {t('events.rewardShare', { defaultValue: 'reward' })} {effDur}%</Mono>
              </div>
              <div className="row gap6" style={{ marginTop: 8 }}>
                {DUR_PCTS.map((pct) => {
                  const sel = effDur === pct;
                  const scaled = isTime ? Math.floor(durationMin * pct / 100) : Math.round(laps * pct / 100);
                  const belowMin = pct < 100 && (isTime ? scaled < DUR_MIN_MIN : scaled < DUR_MIN_LAPS);
                  const disabled = p.lockDuration || belowMin;
                  return <button key={pct} disabled={disabled} onClick={() => setDurPct(pct)} className="focusable"
                    title={belowMin ? (isTime ? `< ${DUR_MIN_MIN} min` : `< ${DUR_MIN_LAPS} ${t('laps')}`) : undefined}
                    style={{ flex: 1, padding: '7px 0', borderRadius: 3, fontSize: 12, cursor: disabled ? 'not-allowed' : 'pointer', opacity: disabled && !sel ? 0.4 : 1, background: sel ? 'var(--accent)' : 'var(--surface)', color: sel ? '#fff' : 'var(--text-2)', border: sel ? '1px solid var(--accent)' : '1px solid var(--line)' }}>{pct}%</button>;
                })}
              </div>
              {/* wear/fuel consumption that will apply with the chosen duration (same warning as the regular pre-race screen) */}
              <WearNote p={p} pct={effDur} />
            </Panel>

            {/* actions: same engine economy logic as the regular pre-race screen (factory / owned / buy+rent) */}
            <div data-tour="prerace.actions" style={{ flex: 'none' }}>
              {p.selFactory ? (
                <Btn variant="primary" size="lg" style={{ width: '100%' }} disabled={!p.seatOk || busy} onClick={() => requestLaunch('own')}>
                  {p.seatOk ? `${t('race_with')} →` : t('factory_need_rep')}
                </Btn>
              ) : (p.selIsOwned || p.lockCar) ? (
                <Btn variant="primary" size="lg" style={{ width: '100%' }} disabled={!p.canRaceOwn || busy} onClick={() => requestLaunch('own')}>
                  {p.canRaceOwn ? `${t('race_with')} →` : needLabel(p.outlayOwn + p.travel)}
                </Btn>
              ) : (
                <div className="row gap8">
                  <Btn variant="ghost" size="lg" style={{ flex: 1 }} disabled={!p.canBuy || busy} onClick={() => setPendingBuy(true)}>
                    {t('buy')} · {disc > 0 ? (
                      <><s style={{ color: 'var(--text-3)', margin: '0 5px 0 4px', fontWeight: 400 }}>{formatCreditsCr(buyFull)}</s><span style={{ color: 'var(--gold)' }}>{formatCreditsCr(buyDisc)}</span></>
                    ) : formatCreditsCr(buyFull)}
                  </Btn>
                  {/* Memories: era series = buy-to-own, no renting */}
                  {!p.isMemory && (
                    <Btn variant="primary" size="lg" style={{ flex: 1.2 }} disabled={!p.canRent || busy} onClick={() => requestLaunch('rent')}>
                      {p.canRent ? `${t('rent')} · ${formatCreditsCr(p.rentCost)}` : needLabel(p.outlayRent + p.travel)}
                    </Btn>
                  )}
                </div>
              )}
              <div className="row between" style={{ marginTop: 8, alignItems: 'baseline' }}>
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('events.durationScalesReward', { defaultValue: 'Shorter duration → proportionally smaller reward' })}</Mono>
                {!isTryout && !p.selFactory && !p.freeEntry && (
                  <span className="row" style={{ alignItems: 'baseline', gap: 6 }}>
                    <Mono style={{ fontSize: 14, fontWeight: 700, color: 'var(--text-2)' }}>{formatCreditsCr(p.selIsOwned ? p.totalOwn : p.totalRent)}</Mono>
                    <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('race_total_cost_label')}</Mono>
                  </span>
                )}
              </div>
            </div>
          </>
        )}
      </div>

      {pendingAction && (
        <LaunchNotice onDone={() => { const a = pendingAction; setPendingAction(null); fire(a); }} />
      )}
      {/* buying is irreversible and the button sits right next to renting. The price confirmed is the
          event-DISCOUNTED one (buyDisc), which is what the engine will actually charge. */}
      {pendingBuy && p && (
        <BuyConfirm
          carName={selCar?.name ?? ''}
          price={buyDisc}
          credits={p.credits}
          busy={busy}
          onCancel={() => setPendingBuy(false)}
          onConfirm={() => { setPendingBuy(false); buy(); }}
        />
      )}
    </div>
  );
}
