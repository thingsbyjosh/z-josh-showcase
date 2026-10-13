// Times: lap/time log, ported from the native overlay's TIMES tab
// (screen_championships.cpp is the source of truth for WHAT is shown and in WHAT ORDER). Two levels:
// L1 = list of races run (clickable into the detail view if a grid exists); L2 = the full grid for
// that race. 100% READ-ONLY (query.times). Leader/gap/fastest-lap are computed here exactly like the native detail view.
// Labels and keys via t() (engine keys in the i18n bundle: times_*). No lap-by-lap drill-down yet.
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, TimesPayload, TimesRace, TimesGridRow } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono } from '../ui/components';
import { classDisplayName } from '../ui/classNames';
import { accentText } from '../ui/colors';
import { formatTime, formatGap, isDnf, dnfLabel } from '../ui/format';

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

// player result -> text + color (P1 gold / P<=3 cyan / DNF text3 / else plain text). Mirrors the native screen.
// P2 (F15): the retirement comes from `finished` and shows where you got out, “DNF (P24)”.
function resultOf(pos: number, field: number, finished?: boolean): { text: string; color: string } {
  if (isDnf(pos, finished)) return { text: dnfLabel(pos), color: 'var(--text-3)' };
  return { text: `P${pos} / ${field}`, color: pos === 1 ? 'var(--gold)' : pos <= 3 ? 'var(--cyan)' : 'var(--text)' };
}

const L1_COLS = '128px 1.4fr 1fr 96px 112px 112px 22px';   // Date | Track | Class | Result | Best | Total | chev
const L2_COLS = '56px 1.6fr 0.8fr 64px 116px 116px 116px'; // Pos | Driver | Class | Laps | Best | Time | Gap

function Cell({ children, align = 'left', color = 'var(--text-2)', size = 12, mono = true, bold = false }: {
  children: React.ReactNode; align?: 'left' | 'center'; color?: string; size?: number; mono?: boolean; bold?: boolean;
}) {
  return (
    <span style={{
      fontFamily: mono ? 'var(--font-mono, monospace)' : undefined, fontSize: size, color,
      fontWeight: bold ? 700 : 400, textAlign: align, justifySelf: align === 'center' ? 'center' : 'start',
      whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', minWidth: 0,
    }}>{children}</span>
  );
}

function HeaderRow({ cols, labels }: { cols: string; labels: { t: string; c?: boolean }[] }) {
  return (
    <div style={{ display: 'grid', gridTemplateColumns: cols, gap: 8, alignItems: 'center',
      padding: '8px 12px', background: 'var(--surface-2)', borderRadius: 4, borderBottom: '1px solid var(--line)' }}>
      {labels.map((l, i) => (
        <Mono key={i} style={{ fontSize: 10, letterSpacing: '.08em', color: 'var(--text-3)', textAlign: l.c ? 'center' : 'left' }}>{l.t}</Mono>
      ))}
    </div>
  );
}

// L1: list of races ----
function TimesList({ races, onOpen }: { races: TimesRace[]; onOpen: (i: number) => void }) {
  const { t } = useTranslation();
  const anyGrid = races.some((r) => r.hasGrid);
  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div style={{ padding: '18px 30px 10px' }}>
        <Display as="h1" style={{ fontSize: 30 }}>{t('times_title')}</Display>
        <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{anyGrid ? t('times_open') : t('times_sub')}</Mono>
      </div>
      <div className="scroll" style={{ flex: 1, padding: '0 30px 24px', minHeight: 0 }} data-tour="times.list">
        <HeaderRow cols={L1_COLS} labels={[
          { t: t('times_date') }, { t: t('times_track') }, { t: t('times_class') },
          { t: t('times_result'), c: true }, { t: t('times_best'), c: true }, { t: t('times_total'), c: true }, { t: '' },
        ]} />
        {races.map((r, i) => {
          const res = resultOf(r.pos, r.field, r.finished);
          return (
            <div key={i} role={r.hasGrid ? 'button' : undefined} tabIndex={r.hasGrid ? 0 : undefined}
              onClick={() => r.hasGrid && onOpen(i)}
              onKeyDown={(e) => r.hasGrid && e.key === 'Enter' && onOpen(i)}
              style={{ display: 'grid', gridTemplateColumns: L1_COLS, gap: 8, alignItems: 'center',
                padding: '10px 12px', borderBottom: '1px solid var(--line)',
                cursor: r.hasGrid ? 'pointer' : 'default' }}
              className={r.hasGrid ? 'focusable' : undefined}>
              <Cell size={11}>{r.date}{r.year > 0 ? ` · ${r.year}` : ''}</Cell>
              <Cell mono={false} size={14} color="var(--text)" bold>{r.track || '—'}</Cell>
              <span className="row gap6" style={{ alignItems: 'center', minWidth: 0 }}>
                <span style={{ width: 7, height: 7, borderRadius: '50%', background: r.classColor, flex: 'none' }} />
                <Cell size={11} color={accentText(r.classColor)}>{classDisplayName(r.className) || '—'}</Cell>
              </span>
              <Cell align="center" bold size={13} color={res.color}>{res.text}</Cell>
              <Cell align="center" bold size={13} color="var(--purple)">{formatTime(r.bestLapS, true)}</Cell>
              <Cell align="center" bold size={13}>{formatTime(r.totalTimeS, false)}</Cell>
              <Cell align="center" color={r.hasGrid ? 'var(--accent)' : 'var(--text-3)'}>{r.hasGrid ? '›' : ''}</Cell>
            </div>
          );
        })}
      </div>
    </div>
  );
}

// L2: full grid for one race ----
function TimesDetail({ race, onBack }: { race: TimesRace; onBack: () => void }) {
  const { t } = useTranslation();
  const grid = race.grid;
  // P2 (F15): AMS2's classification counts whoever retires as classified (RETIRED is not `dnf` in the row), so
  // your row comes out as DNF from the race's `finished`, not from the grid's.
  const rowDnf = (g: TimesGridRow): boolean => g.dnf || (g.isPlayer && race.finished === false);

  // leader for the fastest lap (purple) and the FINAL gap (winner = P1; total = sum of laps). Same as
  // the native detail view: the winner's leaderTotal, falling back to whoever has the most laps and lowest total.
  let leaderBest = -1, leaderTotal = -1, leaderLaps = 0;
  for (const g of grid) {
    if (g.bestLapS > 0 && (leaderBest < 0 || g.bestLapS < leaderBest)) leaderBest = g.bestLapS;
    if (!rowDnf(g) && g.pos === 1 && g.totalTimeS > 0) { leaderTotal = g.totalTimeS; leaderLaps = g.laps; }
  }
  if (leaderTotal <= 0) for (const g of grid) {
    if (rowDnf(g) || g.totalTimeS <= 0) continue;
    if (g.laps > leaderLaps || (g.laps === leaderLaps && (leaderTotal <= 0 || g.totalTimeS < leaderTotal))) {
      leaderTotal = g.totalTimeS; leaderLaps = g.laps;
    }
  }
  const gapOf = (g: TimesGridRow): { text: string; color: string } => {
    const isLeader = !rowDnf(g) && leaderTotal > 0 && g.pos === 1;
    if (rowDnf(g) || g.totalTimeS <= 0 || leaderTotal <= 0 || isLeader) return { text: '—', color: 'var(--text-3)' };
    if (leaderLaps > 0 && g.laps < leaderLaps) {
      // if the row wasn't a final result (settling captures the moment the player finishes, with half
      // the grid still finishing its last lap), that driver finishes the lap in the real world: with a
      // deficit of d laps they've actually been lapped d-1 times, not d. A deficit of 1 while unfinished = same lap -> no reliable gap ("--").
      const deficit = leaderLaps - g.laps;
      const lapsDown = g.finished === true ? deficit : deficit - 1;
      if (lapsDown >= 1) return { text: `+${lapsDown} ${t('times_lap_abbr')}`, color: 'var(--text-3)' };
      return { text: '—', color: 'var(--text-3)' };
    }
    return { text: formatGap(g.totalTimeS - leaderTotal), color: 'var(--text-2)' };
  };
  const res = resultOf(race.pos, race.field, race.finished);

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div style={{ padding: '16px 30px 10px', borderBottom: '1px solid var(--line)' }}>
        <div className="row between" style={{ alignItems: 'flex-start', gap: 16 }}>
          <div className="col" style={{ minWidth: 0, gap: 4 }}>
            <span className="row gap8" style={{ alignItems: 'center' }}>
              <span style={{ width: 8, height: 8, borderRadius: '50%', background: race.classColor, flex: 'none' }} />
              <Mono style={{ fontSize: 10, letterSpacing: '.12em', color: accentText(race.classColor), textTransform: 'uppercase' }}>{classDisplayName(race.className) || '—'}</Mono>
            </span>
            <Display style={{ fontSize: 26 }}>{race.track || t('times_title')}</Display>
            <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>
              {race.date}{race.year > 0 ? ` · ${race.year}` : ''} · <span style={{ color: res.color }}>{res.text}</span>
              {race.totalTimeS > 0 ? ` · ${t('times_total')} ${formatTime(race.totalTimeS, false)}` : ''}
            </Mono>
          </div>
          <button className="btn btn-sm btn-ghost" onClick={onBack}>{t('times_back')}</button>
        </div>
      </div>

      {grid.length === 0 ? (
        <Center><Mono style={{ color: 'var(--text-2)' }}>{t('times_grid_empty')}</Mono></Center>
      ) : (
        <div className="scroll" style={{ flex: 1, padding: '12px 30px 8px', minHeight: 0 }}>
          <HeaderRow cols={L2_COLS} labels={[
            { t: t('times_pos'), c: true }, { t: t('times_driver') }, { t: t('times_class') },
            { t: t('times_laps'), c: true }, { t: t('times_best'), c: true }, { t: t('times_total'), c: true }, { t: t('times_gap'), c: true },
          ]} />
          {grid.map((g, i) => {
            const fastest = g.bestLapS > 0 && leaderBest > 0 && g.bestLapS <= leaderBest + 0.0005;
            const gap = gapOf(g);
            const dnf = rowDnf(g);
            const pc = dnf ? 'var(--text-3)' : g.pos === 1 ? 'var(--gold)' : g.pos <= 3 ? 'var(--cyan)' : 'var(--text)';
            return (
              <div key={i} style={{ display: 'grid', gridTemplateColumns: L2_COLS, gap: 8, alignItems: 'center',
                padding: '9px 12px', borderBottom: '1px solid var(--line)',
                background: g.isPlayer ? 'color-mix(in oklab, var(--accent) 13%, transparent)' : undefined }}>
                <Cell align="center" bold={!dnf} size={dnf ? 11 : 15} color={pc}>{dnf ? 'DNF' : g.pos}</Cell>
                <Cell mono={false} size={14} bold color={g.isPlayer ? 'var(--accent-text)' : 'var(--text)'}>{g.name || '—'}</Cell>
                <span className="row gap6" style={{ alignItems: 'center', minWidth: 0 }}>
                  <span style={{ width: 6, height: 6, borderRadius: '50%', background: g.classColor, flex: 'none' }} />
                  <Cell size={10}>{classDisplayName(g.className) || '—'}</Cell>
                </span>
                <Cell align="center">{g.laps}</Cell>
                <Cell align="center" bold size={13} color={fastest ? 'var(--purple)' : 'var(--text)'}>{formatTime(g.bestLapS, true)}</Cell>
                <Cell align="center" bold size={12} color={g.totalTimeS > 0 ? 'var(--text-2)' : 'var(--text-3)'}>{formatTime(g.totalTimeS, false)}</Cell>
                <Cell align="center" color={gap.color}>{gap.text}</Cell>
              </div>
            );
          })}
          <Mono style={{ fontSize: 10, color: 'var(--text-3)', padding: '10px 2px' }}>{t('times_note')}</Mono>
        </div>
      )}
    </div>
  );
}

export function Times({ client }: { client: ApiClientLike }) {
  const { t } = useTranslation();
  const q = useQuery<TimesPayload>(client, 'query.times');
  const [sel, setSel] = useState<number | null>(null);
  const races = q.data?.races ?? [];

  if (q.loading) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || races.length === 0) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('times_empty')}</Mono></Center>;

  const selRace = sel !== null && sel < races.length ? races[sel] : null;
  return selRace
    ? <TimesDetail race={selRace} onBack={() => setSel(null)} />
    : <TimesList races={races} onOpen={setSel} />;
}
