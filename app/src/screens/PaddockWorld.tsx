// PaddockWorld — "The paddock": the world, as a series rail + detail view.
//
// Shape taken from the design mockup (its `world` branch): on the left, SERIES grouped by
// DISCIPLINE with their counters, on the right the chosen series' TEAMS as COLLAPSIBLE rows that open
// to show their seats. Before, this used to be a flat list of the seats currently in play, which isn't the world: it's
// just the window. The difference matters because the paddock gets looked at ALL YEAR for planning, and outside the
// window there are no in-play seats to show.
//
// The rail renders from the index (cheap); a series' detail is fetched separately. Walking the seats
// of all ~115 series on every refresh just to render one would mean paying for the whole world to look at a single corner.
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, PaddockPayload, PaddockSeriesRow, PaddockWorldTeam } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Mono } from '../ui/components';
import { DISC_META, DISC_ORDER } from '../ui/disciplines';
import { SeriesLogo } from '../ui/SeriesLogo';
import type { CalendarDiscipline } from '../api/contracts';

function discOf(token: string): CalendarDiscipline {
  return (DISC_ORDER as readonly string[]).includes(token) ? (token as CalendarDiscipline) : DISC_ORDER[0];
}

export function PaddockWorld({ client, rows, year }: { client: ApiClientLike; rows: PaddockSeriesRow[]; year: number }) {
  const { t, i18n } = useTranslation();
  // starts on the series you RACE in; if you don't race in any, the one with the most activity. Always opening on
  // the first alphabetically would leave the player staring at a category that has nothing to do with them.
  const inicial = rows.find((r) => r.mine) ?? [...rows].sort((a, b) => b.inPlay - a.inPlay || b.expiring - a.expiring)[0];
  const [sel, setSel] = useState<string>(inicial?.id ?? '');
  const [abierto, setAbierto] = useState<string | null>(null);

  const detalle = useQuery<PaddockPayload>(client, 'query.paddock', sel ? { seriesId: sel } : undefined);
  const crudos: PaddockWorldTeam[] = detalle.data?.world ?? [];
  // teams with something to show go FIRST. In grid order, the one row you actually care about
  // used to end up buried among a dozen others. The final tie-break is the original order, so within each group
  // the grid still reads as-is.
  const peso = (eq: PaddockWorldTeam): number => {
    if (eq.mine) return 0;                                        // where you race
    if (eq.windowStatus === 'offered') return 1;                  // you've been offered it: the most actionable state
    if (eq.windowStatus === 'sounded' || eq.windowStatus === 'disputed') return 2;
    if (eq.windowStatus !== '') return 3;                         // in play, but already resolved or renewed
    if (eq.seats.some((st) => st.expiryYear > 0 && st.expiryYear <= year)) return 4;   // expires this year
    return 5;
  };
  const equipos = crudos
    .map((eq, i) => ({ eq, i }))
    .sort((a, b) => peso(a.eq) - peso(b.eq) || a.i - b.i)
    .map((x) => x.eq);
  const serie = rows.find((r) => r.id === sel);

  // grouped by discipline, in the same order the rest of the app already uses
  const porDisc = DISC_ORDER.map((d) => ({ d, list: rows.filter((r) => discOf(r.discipline) === d) })).filter((g) => g.list.length);

  return (
    // `gridTemplateRows` is half the fix for "the rows are cut off" at 125% scale. Without
    // declaring it, the grid's implicit row defaults to `auto` = sized to its CONTENT, so the two
    // columns kept growing to their natural height (the rail holds ~115 series) and overflowed their bounded box:
    // each column's `overflow-y:auto` never kicked in, and it was the ancestor that clipped it, cutting the
    // row in half. With `minmax(0,1fr)` the row gets the grid's REAL height and each column scrolls
    // on its own. `flex: 1 1 0` + minHeight 0 states this explicitly instead of inheriting it from a shared `grow` class.
    <div style={{ flex: '1 1 0', minHeight: 240, display: 'grid',
                  gridTemplateColumns: '236px minmax(0,1fr)', gridTemplateRows: 'minmax(0,1fr)', gap: 12 }}>
      {/* ---- SERIES RAIL ---- */}
      {/* `scrollbar-gutter: stable` reserves the scrollbar's space: without it the bar gets painted ON TOP of the series
          name (the other half of what shows up in the bug screenshot). */}
      <div className="col scroll" style={{ gap: 4, minHeight: 0, paddingRight: 6, scrollbarGutter: 'stable' }} data-paddock-rail="1">
        {porDisc.map(({ d, list }) => (
          <div key={d} className="col" style={{ gap: 4 }}>
            <span className="mono" style={{ fontSize: 8.5, letterSpacing: '.14em', color: DISC_META[d].color, margin: '8px 2px 2px', opacity: 0.85 }}>
              {(i18n.language === 'es' ? DISC_META[d].es : DISC_META[d].en).toUpperCase()}
            </span>
            {list.map((r) => {
              const on = r.id === sel;
              return (
                <button
                  key={r.id}
                  className="focusable"
                  data-paddock-serie={r.id}
                  data-active={on ? '1' : undefined}
                  onClick={() => { setSel(r.id); setAbierto(null); }}
                  style={{
                    cursor: 'pointer', textAlign: 'left', padding: '9px 12px', display: 'flex', flexDirection: 'column', gap: 4,
                    background: on ? 'color-mix(in oklab, var(--accent) 10%, var(--surface))' : 'var(--surface)',
                    border: `1px solid ${on ? 'var(--accent)' : 'var(--line)'}`,
                    borderLeft: `3px solid ${DISC_META[d].color}`,
                  }}
                >
                  <span className="row between" style={{ alignItems: 'center', gap: 8 }}>
                    <span style={{ fontSize: 13, fontWeight: 700, color: on ? 'var(--text)' : 'var(--text-2)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{r.display}</span>
                    <span className="row" style={{ gap: 4, flex: 'none', alignItems: 'center' }}>
                      {/* yours first and in the accent color: it's the only thing in the rail that demands your attention */}
                      {r.yours > 0 && <span className="mono" title={t('pd_c_yours', { n: r.yours, defaultValue: '{{n}} seats you are in' })} style={{ fontSize: 9, color: 'var(--accent-ink)', background: 'var(--accent)', padding: '1px 5px', fontWeight: 700 }}>★ {r.yours}</span>}
                      {r.inPlay - r.yours > 0 && <span className="mono" title={t('pd_c_inplay', { n: r.inPlay - r.yours, defaultValue: '{{n}} seats in play' })} style={{ fontSize: 9, color: 'var(--bg)', background: 'var(--cyan)', padding: '1px 5px', fontWeight: 700 }}>{r.inPlay - r.yours}</span>}
                      {/* YELLOW isn't open opportunities: it's contracts EXPIRING this year, i.e.
                          the seats that will turn into opportunities once the window opens. It's
                          shown whenever there are any, even with the window already open -- knowing there are
                          three more about to fall is exactly what makes you wait instead of signing the first offer. */}
                      {r.expiring > 0 && <span className="mono" title={t('pd_c_expiring', { n: r.expiring, defaultValue: '{{n}} contracts expire this year' })} style={{ fontSize: 9, color: 'var(--gold)', border: '1px solid color-mix(in oklab, var(--gold) 45%, transparent)', padding: '0 5px' }}>{r.expiring}</span>}
                    </span>
                  </span>
                  <span className="mono" style={{ fontSize: 9, color: 'var(--text-3)' }}>
                    {t('pd_n_seats', { n: r.seats, defaultValue: '{{n}} seats' })}
                    {r.mine ? ` · ${t('pd_you_race_here', { defaultValue: 'you race here' })}` : ''}
                  </span>
                </button>
              );
            })}
          </div>
        ))}
      </div>

      {/* ---- TEAMS IN THE SELECTED SERIES ---- */}
      <div className="col" style={{ gap: 8, minHeight: 0, overflowY: 'auto', paddingRight: 4 }} data-paddock-world="1">
        {serie && (
          <div className="row between" style={{ alignItems: 'baseline', marginBottom: 2 }}>
            <SeriesLogo id={serie.id} name={serie.display} height={28} fontSize={20} />
            <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
              {t('pd_tier', { n: serie.level, defaultValue: 'TIER {{n}}' })} · {t('pd_n_seats', { n: serie.seats, defaultValue: '{{n}} seats' })}
              {serie.expiring > 0 && <span style={{ color: 'var(--gold)' }}> · {t('pd_n_expire', { n: serie.expiring, defaultValue: '{{n}} expire this year' })}</span>}
              {serie.inPlay > 0 && <span style={{ color: 'var(--cyan)' }}> · {t('pd_n_in_play', { n: serie.inPlay, defaultValue: '{{n}} in play' })}</span>}
            </Mono>
          </div>
        )}
        {equipos.length === 0 && (
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('common.loading')}</Mono>
        )}
        {equipos.map((eq) => {
          const open = abierto === eq.team;
          const enJuego = eq.windowStatus !== '';
          return (
            <div key={eq.team} className="panel" data-paddock-team={eq.team} style={{
              // flex:none — the team list is a flex column with overflow-y:auto, but a flex child
              // SHRINKS by default: while shrinking there's never any overflow, so the container never got to
              // scroll, and the cards got squashed instead (with overflow:hidden here, clipping their layout).
              padding: 0, overflow: 'hidden', flex: 'none',
              borderColor: eq.mine ? 'color-mix(in oklab, var(--accent) 45%, var(--line))'
                : enJuego ? 'color-mix(in oklab, var(--cyan) 35%, var(--line))' : 'var(--line)',
            }}>
              <div
                role="button"
                tabIndex={0}
                className="focusable"
                onClick={() => setAbierto(open ? null : eq.team)}
                onKeyDown={(e) => { if (e.key === 'Enter') setAbierto(open ? null : eq.team); }}
                style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '11px 14px', cursor: 'pointer', background: 'var(--surface)' }}
              >
                <span style={{ width: 4, height: 30, background: eq.mine ? 'var(--accent)' : enJuego ? 'var(--cyan)' : 'var(--line-2)', flex: 'none' }} />
                <span style={{ fontSize: 14.5, fontWeight: 700, minWidth: 0, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{eq.team}</span>
                {eq.mine && <span className="mono" style={{ fontSize: 8.5, color: 'var(--accent-ink)', background: 'var(--accent)', padding: '1px 6px', flex: 'none' }}>{t('pd_your_team', { defaultValue: 'YOUR TEAM' })}</span>}
                {enJuego && <span className="mono" style={{ fontSize: 9, color: 'var(--cyan)', flex: 'none' }}>{t(`pd_status_${eq.windowStatus}`)}</span>}
                <div className="grow" />
                <Mono style={{ fontSize: 10, color: 'var(--text-3)', flex: 'none' }}>{open ? '▾' : '▸'}</Mono>
              </div>
              {open && (
                <div className="col" data-paddock-team-open={eq.team} style={{ padding: '4px 14px 12px', flex: 'none' }}>
                  {eq.seats.map((st) => (
                    /* flex:none — in a flex column, children SHRINK by default (flex-shrink:1), and this
                       list lives inside a container with bounded height. While shrinking, each row's
                       text overflowed its box and lines overlapped each other, with separators
                       landing in the wrong place. With its natural height, the container scrolls instead, which is correct. */
                    <div key={st.idx} className="row" style={{ gap: 10, alignItems: 'baseline', padding: '6px 0', borderTop: '1px solid var(--line)', flex: 'none' }}>
                      <span style={{ fontSize: 12.5, color: st.occupantName ? 'var(--text-2)' : 'var(--text-3)' }}>
                        {st.occupantName || t('pd_seat_open', { defaultValue: 'open seat' })}
                        {st.occupantAge > 0 && ` (${st.occupantAge})`}
                      </span>
                      <div className="grow" />
                      {/* the "until when" is what makes the paddock an actual tool: you know what's opening up
                          and in which year, so you can target it seasons in advance. */}
                      {st.expiryYear > 0 && (
                        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>
                          {t('pd_until', { year: st.expiryYear, defaultValue: 'until {{year}}' })}
                        </Mono>
                      )}
                    </div>
                  ))}
                </div>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
}
