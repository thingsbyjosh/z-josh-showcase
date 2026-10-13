// TimeAdvancePopup (C1, spec + docs/design/paddock_v2/screen_time_popup.jsx): the ONLY time-advance
// dialog. Asks for query.time.preview with the caller's params ({seriesId}|{untilAbsDay}|{scope}|{}), draws
// the day strip (by weeks if > 14), the feed of what is crossed (gold STOP / grey passes, no-shows in
// red), "Stop here" on each event (= untilAbsDay), ⚙ with the five rules (cmd.time.rules + refetch) and a
// single "▶▶ Advance" that sends cmd.time.advance with the SAME params as the last preview. Replaces the old
// jump dialog: a single confirm, because the popup IS the review. Never horizontal scroll.
//
// Day/week strip (controller's decision, 03/09): runs from TODAY to the STOP inclusive, so with
// days <= 14 days+1 day cells are drawn (today is cell 0, in accent) and with days > 14 they are grouped into
// ceil((days+1)/7) week cells — never both strips at once. Review 0.7.0 (finding 6): past
// 26 weeks they are grouped by MONTH (one cell per month the stretch touches, with its short name): with ~25 cells or more
// of weeks each “W12” no longer fit and the strip spilled out of the panel.
import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, TimeAdvanceParams, TimeAdvancePayload, TimeStopRules } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Btn, Kicker } from '../ui/components';

// Day of the MONTH for the strip (not the day of the year): the engine sends today and the stop, and the strip's days in
// between are counted by the app. With the raw doy it showed "95 96 97" —which is not a date to anyone— and on crossing
// the year, "365 366". Full Gregorian (leap = /4 except /100 except /400) and with year wraparound, so
// a long jump keeps telling the truth when crossing December.
const RULE_KEYS: Array<keyof TimeStopRules> = ['offers', 'myRaces', 'anyRace', 'mail', 'relation'];

// 17/09 — the calendar sheet needs to label EVERY day of the path, and the engine only sends today's.
// Full Gregorian (leap = /4 except /100 except /400) with year wraparound, so a long advance
// keeps telling the truth when crossing December.
const DIAS_MES = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
const bisiesto = (y: number) => (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0;
function fechaDe(year: number, doy: number): { d: number; m: number; y: number } {
  let y = year;
  let n = doy;
  for (let g = 0; g < 64; g++) {
    const largo = bisiesto(y) ? 366 : 365;
    if (n <= largo) break;
    n -= largo; y++;
  }
  for (let m = 0; m < 12; m++) {
    const largo = DIAS_MES[m] + (m === 1 && bisiesto(y) ? 1 : 0);
    if (n <= largo) return { d: n, m, y };
    n -= largo;
  }
  return { d: 1, m: 0, y };
}

// Sheet pace. It starts slow so the day can be READ, and accelerates: a half-year jump at 120 ms per
// sheet would be three quarters of a minute watching a calendar. Past two weeks it groups, so what
// changes is how far each sheet advances, not the feeling that time is running.
const MS_HOJA = 120;
/** Days in `year` (365/366): the doy of 31 December. */
function fechaDoyTope(year: number): number {
  return (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0 ? 366 : 365;
}
function saltoDeHoja(diaActual: number, total: number): number {
  if (total <= 21) return 1;
  if (diaActual < 14) return 1;               // the first two weeks, day by day
  if (total <= 90) return 3;
  if (total <= 400) return 7;
  return 30;
}

export function TimeAdvancePopup({ client, params, onCancel, onAdvanced }: {
  client: ApiClientLike; params?: TimeAdvanceParams; onCancel: () => void; onAdvanced: (p: TimeAdvancePayload) => void;
}) {
  const { t, i18n } = useTranslation();
  const i18nLang = i18n.language || 'en';
  // No `setEffParams`: the target is no longer chosen from inside the dialog. What comes from the caller
  //  rules (a date for “go to”, or nothing — which the engine reads as “until the first stop”).
  const [effParams] = useState<TimeAdvanceParams>(params ?? {});
  // 18/09 — TWO QUERIES, AND THE FACE ONLY WHEN NEEDED.
  //
  // Before there was only one: the FULL forecast, which simulates from today to the first stop. In a
  // real save that is ~1.6 s, and it was requested when opening the dialog **and again every time a
  // checkbox was ticked** — with the checkboxes disabled in the meantime. Ticking a rule felt sticky, and
  // rightly so: the entire world was being recomputed to learn something that is not needed until you press
  // advance. The rules only change WHERE it stops.
  //
  //  · `qBase` (`resolveOnly`) costs **31 ms** and brings what the dialog needs to draw itself: today's
  //    day, the five rules and whether there is a reachable target. With this it opens instantly and the checkboxes
  //    respond instantly.
  //  · `qLarga` is the real simulation, and only contributes two things: **how many days** lie ahead (to
  //    know when to stop launching sheets) and the warning about **races you are going to miss**. It runs behind,
  //    blocking nothing. If it has not arrived yet when you press advance, the sheets start falling anyway: the
  //    world has not moved yet, so there is nothing to correct — it lands as soon as it arrives.
  const [effBase] = useState<TimeAdvanceParams>({ ...(params ?? {}), resolveOnly: true });
  const q = useQuery<TimeAdvancePayload>(client, 'query.time.preview', effBase);
  const qLarga = useQuery<TimeAdvancePayload>(client, 'query.time.preview', effParams);
  const p = q.data;
  /** Days until the stop, or `null` while the long simulation has not come back. */
  const dias: number | null = qLarga.data ? qLarga.data.days : null;
  // 18/09 (Josh) — “IF YOU'RE NOT CALCULATING ANYTHING, HOW DO YOU KNOW I'M GOING TO MISS THIS?”. The list of
  // races you skip comes from the count that runs behind up to the first stop, and in “advance”
  // it assumed you would not stop earlier — exactly what the dialog doesn't know, because you can cut the sheet
  // whenever you want. It also counted the future, which on 17/09 it was decided not to show when advancing.
  // So it only appears when YOU choose the destination (“Go to date”, jumping to next year, going to a series):
  // there you have decided where you are going and knowing what you skip along the way helps you decide. Plain advance, nothing.
  const conDestino = !!params && (params.untilAbsDay != null || params.seriesId != null || params.scope != null);
  const noShows = conDestino && qLarga.data ? qLarga.data.noShows : [];
  const [stage, setStage] = useState<'idle' | 'busy' | 'error'>('idle');
  // 17/09 (Josh's decision) — TIME IS SEEN PASSING, AND STOPPING STOPS FOR REAL.
  //
  // Measured against the engine before writing this: advancing ONE day costs 40 ms, so the temptation was to
  // call the engine once per day and animate the result. On 17/09 that **was not possible**: checked with two
  // identical sandboxes, twenty one-day calls did NOT leave the world the same as one twenty-day call, because the
  // world pulse ran once a week COUNTED FROM EACH CALL — 5 extra offers expired and
  // the window opened another day. Since 18/09 the pulse is daily and that no longer happens: measured on the double,
  // twenty one-day calls and one twenty-day call leave the save identical BYTE FOR BYTE, and likewise with
  // two hundred and sixty days crossing the February decision and the opening of the September window. Even
  // so ONE call is still made: it adopts what was already forecast instead of simulating again, and saves the
  // save once instead of once per day.
  //
  // What is done: the sheets fall ahead and the world clock does NOT move yet. When the player
  // presses STOP —or when the count reaches the stop by itself— ONE single call is made to the engine up to that exact
  // day. They see time pass, can cut it wherever they want, and the world ends up as if it had jumped there
  // in one go, which is exactly what it is.
  const [dia, setDia] = useState<number | null>(null);   // days shown; null = the count has not started
  const diaRef = useRef(0);
  const pararRef = useRef(false);
  // Review 0.7.0 (finding 1): a cmd.time.advance failure does not say the clock is still where it was (the engine
  // may have finished advancing after the app got tired of waiting). Retrying on the old forecast
  // is not allowed: a new one is requested, which starts from the REAL state, and Advance waits for it to arrive. `staleSince` is the
  // query version at the moment of the failure; until a new one arrives, the on-screen forecast is not valid.
  const [staleSince, setStaleSince] = useState<number | null>(null);
  const stale = staleSince !== null && q.version === staleSince;
  // the LIVE version of the query, for the .then/.catch: the one from the render in which it was pressed may already be old
  const versionRef = useRef(q.version);
  versionRef.current = q.version;
  // Review 0.7.0 (finding 5): the rules came from p.rules, which does not change until the new forecast arrives:
  // two consecutive checkboxes sent the second with the first still unticked and undid it. Now the checkboxes
  // show and start from the LOCAL (optimistic) rules and stay still while a cmd.time.rules is in progress
  // or its forecast has not arrived. If the command fails, whatever the re-requested forecast says goes.
  const [localRules, setLocalRules] = useState<TimeStopRules | null>(null);
  const [rulesInFlight, setRulesInFlight] = useState(false);
  // What blocks the checkbox is ONLY the command (≤30 ms), not the simulation. Before it also waited for
  // the new forecast to arrive — a second and a half with the five checkboxes frozen.
  const rulesBusy = rulesInFlight;
  const shownRules: TimeStopRules | null = localRules ?? (p ? p.rules : null);

  const setRule = (k: keyof TimeStopRules, v: boolean) => {
    if (!p || !shownRules || rulesBusy) return;
    const rules: TimeStopRules = { ...shownRules, [k]: v };
    setLocalRules(rules);          // the local one goes until the dialog closes: the engine already has this
    setRulesInFlight(true);
    void client.cmd('cmd.time.rules', rules)
      // The long simulation is requested again because the rules change where it stops — but BEHIND, without
      // blocking: the only thing it updates is the missed-races warning and the day count.
      .then(() => { setStage('idle'); setRulesInFlight(false); qLarga.refetch(); })
      .catch(() => { setStage('error'); setLocalRules(null); setRulesInFlight(false); q.refetch(); qLarga.refetch(); });
  };
  // The REAL jump, a single call. `hasta` null = up to wherever the caller said (and if it said nothing, the
  // engine reads it as “until the first stop”); with a number, the exact day on which the player cut it.
  const saltar = (hasta: number | null) => {
    setStage('busy');
    const params: TimeAdvanceParams = hasta == null ? effParams : { untilAbsDay: hasta };
    void client.cmd<TimeAdvancePayload>('cmd.time.advance', params).then((res) => onAdvanced(res)).catch(() => {
      setStage('error');
      setDia(null);
      setStaleSince(versionRef.current);
      q.refetch();
    });
  };

  // Starts the sheet count. Does not touch the world: only watches the days pass that the engine already said there are
  // ahead (`p.days`), without ever showing where they lead.
  const advance = () => {
    if (!p || !p.resolved || stage === 'busy' || stale || rulesBusy || dia !== null) return;
    // It does not wait for the long simulation: the sheets start falling and it lands when it arrives. The world
    // does not move until the final jump, so running the animation ahead commits nothing.
    if (dias !== null && dias <= 0) { saltar(null); return; }
    diaRef.current = 0; pararRef.current = false;
    setDia(0);
  };

  const parar = () => {
    if (!p || stage === 'busy') return;
    pararRef.current = true;
    // the day the player is SEEING is how far the world goes
    saltar(p.today.absDay + diaRef.current);
  };

  // days from today to 31 December of this year: the engine's hard stop, which the blind sheet does not cross
  const finDeAnoDias = p ? fechaDoyTope(p.today.year) - p.today.doy : 0;
  useEffect(() => {
    if (dia === null || !p || stage === 'busy') return;
    const t = setTimeout(() => {
      if (pararRef.current) return;
      // 18/09 — WHILE IT IS NOT KNOWN HOW MANY DAYS THERE ARE, THE SHEET GOES ONE BY ONE, ALWAYS. Before that only held
      // for the first two weeks (`saltoDeHoja` with an invented total of 9999), counting on the forecast
      // arriving in those 1.7 s. With the world moving every day the forecast for a year takes ~12 s, and past
      // those two weeks the sheet jumped thirty at a time: it got years ahead of the real stop and
      // then came back all at once. A “stop” pressed there pointed at a date the world was never going to reach.
      // Day by day the sheet goes at 120 ms per day and the forecast at ~36: the forecast ALWAYS arrives before the
      // sheet reaches the stop. And in case a slow machine reversed that, the blind sheet does not go past 31
      // December either, which is the engine's hard stop: there it waits to know the total.
      if (dias === null) {
        if (diaRef.current >= finDeAnoDias) return;   // not one day beyond where the clock surely brakes
        diaRef.current += 1; setDia(diaRef.current);
        return;
      }
      const paso = saltoDeHoja(diaRef.current, dias);
      if (diaRef.current >= dias) { saltar(null); return; }   // the count overshot: land now, without going back
      const siguiente = Math.min(dias, diaRef.current + paso);
      diaRef.current = siguiente;
      setDia(siguiente);
      if (siguiente >= dias) saltar(null);    // it reached the stop by itself: the real jump
    }, MS_HOJA);
    return () => clearTimeout(t);
  }, [dia, p, dias, stage, finDeAnoDias]);
  return (
    <div onClick={onCancel} style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.82)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 60 }}>
      <div className="panel" role="dialog" aria-modal="true" aria-label={t('time.title')} onClick={(e) => e.stopPropagation()}
        style={{ width: '92vw', maxWidth: 580, maxHeight: '88vh', overflowY: 'auto', overflowX: 'hidden', padding: '22px 24px', display: 'flex', flexDirection: 'column', gap: 12 }}>
        <div className="row between" style={{ alignItems: 'baseline' }}>
          <Display style={{ fontSize: 24 }}>{t('time.title')}</Display>
          {/* 17/09 (Josh's decision) — YOU ARE NOT TOLD WHERE YOU ARE GOING. Before, the header announced “8 Jan → 15 Feb”
              and below came the day strip and the list of everything you were about to cross: the player read their own
              future before living it. Now only TODAY is seen; the clock runs and stops at the first thing you have
              ticked below. The engine already worked like this —`untilAbsDay = 0` is its documented sentinel for
              “until the first stop”—, so this removes screen, not mechanism. */}
          {p && <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{p.today.weekday} {p.today.date} {p.today.year}</Mono>}
        </div>
        {q.loading && !p && <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('common.loading')}</Mono>}
        {q.error && <><Mono style={{ fontSize: 13, color: 'var(--danger)' }}>{t('time.failed')}</Mono><Btn variant="ghost" onClick={onCancel}>{t('common.cancel')}</Btn></>}
        {p && !p.resolved && !q.error && (<><Mono style={{ fontSize: 13 }}>{t('time.unreachable')}</Mono><Btn variant="ghost" onClick={onCancel}>{t('common.cancel')}</Btn></>)}
        {/* mutually exclusive: with old `data` in hand and the last query FAILED, both the failure notice
            and a stale feed that no longer describes the clock were painted at once. The error wins. */}
        {p && p.resolved && !q.error && (
          <>
            {noShows.length > 0 && (
              <div className="col" style={{ gap: 4, padding: '10px 12px', border: '1px solid color-mix(in oklab, var(--danger) 45%, transparent)' }}>
                <Mono style={{ fontSize: 12, fontWeight: 700, color: 'var(--danger)' }}>{t('time.noShowTitle')}</Mono>
                <Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{t('time.noShowBody')}</Mono>
                {noShows.map((ns) => <Mono key={`${ns.seriesId}-${ns.round}`} style={{ fontSize: 11 }}>{ns.seriesName} · {ns.trackName} · {ns.weekday} {ns.date}</Mono>)}
              </div>
            )}
            {/* The SHEET. While the count runs, the calendar takes the place of the rules: it is the only thing
                to look at. Each day change remounts the sheet (the `key`), and the CSS lets it fall
                spinning — the one underneath already has the next day written, so it reads like tearing off a
                sheet from a wall calendar. */}
            {dia !== null && (() => {
              const f = fechaDe(p.today.year, p.today.doy + dia);
              const mes = new Intl.DateTimeFormat(i18nLang, { month: 'long', timeZone: 'UTC' })
                .format(Date.UTC(f.y, f.m, 1));
              return (
                <div className="col" style={{ alignItems: 'center', gap: 10, padding: '10px 0 2px' }}>
                  <div className="hoja-pila">
                    <div className="hoja" key={dia} data-testid="time-hoja">
                      <div className="hoja-mes">{mes}</div>
                      <div className="hoja-dia">{f.d}</div>
                      <div className="hoja-anyo">{f.y}</div>
                    </div>
                  </div>
                  <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('time.running')}</Mono>
                </div>
              );
            })()}
            {dia === null && (
              <div className="panel" style={{ padding: '12px 14px', background: 'var(--bg-2)' }}>
                <Kicker>{t('time.rulesTitle')}</Kicker>
                <div className="col" style={{ gap: 6, marginTop: 8 }}>
                  {RULE_KEYS.map((k) => (
                    <label key={k} className="row" style={{ gap: 10, alignItems: 'center', fontSize: 12.5, cursor: 'pointer' }}>
                      <input type="checkbox" checked={(shownRules ?? p.rules)[k]} disabled={stage === 'busy' || rulesBusy} onChange={(e) => setRule(k, e.target.checked)} />
                      <span>{t(`time.rules.${k}`)}</span>
                    </label>
                  ))}
                </div>
                <Mono style={{ fontSize: 9.5, color: 'var(--text-3)', display: 'block', marginTop: 8 }}>{t('time.rulesNote')}</Mono>
              </div>
            )}
            {stage === 'error' && <Mono style={{ fontSize: 12, color: 'var(--danger)' }}>{t('time.failed')}</Mono>}
            <div className="row between" style={{ marginTop: 4, alignItems: 'center' }}>
              {/* While it runs, something has to MOVE: before, the dialog sat still and there was no way
                  to tell whether the engine was working or had hung. */}
              <span />
              {dia !== null ? (
                /* A single button while it runs, and a big one: stopping is the only decision left. The world
                   ends EXACTLY on the day being seen, because that is how far it jumps. */
                <Btn variant="primary" size="sm" data-testid="time-parar" disabled={stage === 'busy'} onClick={parar}>
                  {t('time.stop')}
                </Btn>
              ) : (
                <div className="row" style={{ gap: 8 }}>
                  <Btn variant="ghost" size="sm" disabled={stage === 'busy'} onClick={onCancel}>{t('common.cancel')}</Btn>
                  <Btn variant="primary" size="sm" disabled={stage === 'busy' || stale || rulesBusy} onClick={advance}>{t('time.advance')}</Btn>
                </div>
              )}
            </div>
          </>
        )}
      </div>
    </div>
  );
}
