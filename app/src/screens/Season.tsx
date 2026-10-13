// Season / Championships: master-detail view ported from screen_season.cpp (source of truth). REPLACES the
// Standings scaffold on the CHAMPIONSHIPS tab. Left rail with 3 sections (my championships / available /
// locked) + a right-hand detail panel that changes by kind (1=round-by-round standings grid, 2=access/super-license,
// 3=info) + a season-close BONUS modal. Selecting a card re-queries query.season {seriesId}.
// GO TO NEXT RACE jumps to the Calendar tab. Chrome via the engine's keys (champ_*/sec_*/season_*/gate_*).
import { useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, SeasonPayload, SeasonRailItem, SeasonStandRow } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Display, Mono, Kicker, Btn, Chip, LockIcon } from '../ui/components';
import { accentText } from '../ui/colors';
import { formatCreditsCr } from '../ui/format';
import { SeasonCalendar } from '../ui/SeasonCalendar';   // what you're racing, not just how you get in
import { SeriesLogo } from '../ui/SeriesLogo';

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

const SECTION_KEY = ['champ_my', 'sec_available', 'sec_locked'];

// The engine's keys use printf-style placeholders (%d/%s), not i18next's {{}}: positional substitution, in order.
const fmt = (s: string, ...vals: (string | number)[]): string => { let i = 0; return s.replace(/%[ds]/g, () => String(vals[i++] ?? '')); };

function RailCard({ it, onSelect }: { it: SeasonRailItem; onSelect: (id: string) => void }) {
  const { t } = useTranslation();
  return (
    <button onClick={() => onSelect(it.seriesId)} className="focusable" style={{
      display: 'flex', flexDirection: 'column', gap: 4, textAlign: 'left', padding: '10px 12px', borderRadius: 3, cursor: 'pointer',
      background: it.selected ? `color-mix(in oklab, ${it.discColor} 12%, var(--surface))` : 'var(--surface)',
      border: it.selected ? `1px solid ${it.discColor}` : '1px solid var(--line)',
      opacity: it.section === 2 ? 0.7 : 1,
    }}>
      <div className="row between" style={{ alignItems: 'center' }}>
        <span style={{ fontWeight: 700, fontSize: 13 }}>{it.display}</span>
        {it.section === 2 && <span style={{ fontSize: 11 }}>{it.gateLocked ? <LockIcon size={11} /> : `${t('phase')} ${it.level}`}</span>}
      </div>
      <span className="row gap6" style={{ alignItems: 'center' }}>
        <span style={{ width: 7, height: 7, borderRadius: '50%', background: it.discColor, flex: 'none' }} />
        <Mono style={{ fontSize: 9.5, color: accentText(it.discColor) }}>{it.classShort}</Mono>
      </span>
      {it.section === 0 && (
        <div className="row between" style={{ alignItems: 'flex-end', marginTop: 4 }}>
          <span className="row gap4" style={{ alignItems: 'baseline' }}>
            <span className="display" style={{ fontSize: 22, color: it.discColor }}>P{it.playerPos}</span>
            <Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{it.myPoints} {t('champ_pts')}</Mono>
          </span>
          <Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{it.done ? t('season_done') : `R${it.roundIdx}/${it.totalRounds}`}</Mono>
        </div>
      )}
    </button>
  );
}

function Cell({ cell }: { cell: { pos: number; points: number; dnf: boolean; ran: boolean } }) {
  if (!cell.ran) return <span style={{ color: 'var(--text-3)', fontSize: 11 }}>–</span>;
  if (cell.dnf) return <span style={{ color: 'var(--danger)', fontSize: 10, fontWeight: 700 }}>DNF</span>;
  const c = cell.pos === 1 ? 'var(--gold)' : cell.pos <= 3 ? 'var(--cyan)' : 'var(--text-2)';
  return (
    <span className="col" style={{ alignItems: 'center', lineHeight: 1 }}>
      <span style={{ color: c, fontWeight: 700, fontSize: 12 }}>{cell.pos}</span>
      <Mono style={{ fontSize: 8, color: 'var(--text-3)' }}>{cell.points}</Mono>
    </span>
  );
}

function StandingsGrid({ p, onGoToCalendar }: { p: SeasonPayload; onGoToCalendar: () => void }) {
  const { t } = useTranslation();
  const d = p.detail;
  const cols = d.rounds;
  // 42px wasn't even enough for "Millvale". The table already scrolls horizontally, so widening the column
  // only makes a championship with many rounds start scrolling sooner -- in exchange for knowing WHERE each
  // round was raced without leaving the screen.
  const cellW = 68;   // 31/08: goes up with the header font (R#, date and circuit no longer render at 7.5px)
  return (
    <div className="col" style={{ gap: 12, minHeight: 0 }}>
      <div className="row between" style={{ alignItems: 'flex-end' }}>
        <div className="col" style={{ gap: 2 }}>
          <SeriesLogo id={p.selectedSeriesId} name={d.seriesDisplay} height={34} fontSize={24} />
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('season_year')} {d.seasonYear} · {t('season_round')} {d.roundIdx}/{d.totalRounds}</Mono>
        </div>
        <div className="row gap8" style={{ alignItems: 'center' }}>
          {d.pointsScale.length > 0 && <Chip kind="default" style={{ fontSize: 9 }}>{d.pointsScale.slice(0, 6).join('·')}…</Chip>}
          <Btn variant="ghost" size="sm" onClick={onGoToCalendar}>{t('see_class_races')} →</Btn>
        </div>
      </div>

      {/* 31/08: the POINTS column and the scrollbar were sitting BELOW the rail's arrows
          (fixed over the right edge). With padding only the content moved over: the bar paints itself on
          the BOX's edge and didn't move. With margin the whole scroller narrows, so the fixed column
          (which measures its own `right: 0` against that edge) and the bar move over together. The rail
          itself publishes its width in `--rail-gutter`, which is 0 when there are no arrows to paint. */}
      <div className="scroll" style={{ minHeight: 0, overflow: 'auto', marginRight: 'var(--rail-gutter, 0px)' }}>
        <div style={{ display: 'inline-block', minWidth: '100%' }}>
          {/* header */}
          <div className="row" style={{ position: 'sticky', top: 0, background: 'var(--surface-2)', borderBottom: '1px solid var(--line)', padding: '7px 0' }}>
            <span style={{ width: 30, flex: 'none' }} />
            <span style={{ flex: 1, minWidth: 140, fontSize: 10.5, letterSpacing: '.08em', color: 'var(--text-3)', textTransform: 'uppercase' }}>{t('driver_hdr')}</span>
            {cols.map((r) => (
              // the track name was ALREADY in the payload (SeasonRoundCol.trackName) and the header was throwing it away:
              // finding out where each round was held meant leaving for the races tab. Long names
              // ("Stonewood Ring Blackworth 1971") get truncated with an ellipsis, with the full name in the tooltip.
              <span key={r.round} className="col" data-testid={`season-round-${r.round}`} title={r.trackName}
                    style={{ width: cellW, flex: 'none', alignItems: 'center', lineHeight: 1.25, padding: '0 4px', minWidth: 0 }}>
                <Mono style={{ fontSize: 10.5, color: 'var(--text-2)' }}>R{r.round}</Mono>
                <Mono style={{ fontSize: 9, color: 'var(--text-3)' }}>{r.date}</Mono>
                <Mono style={{ fontSize: 9, color: 'var(--text-3)', maxWidth: '100%', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{r.trackName}</Mono>
              </span>
            ))}
            {/* tester suggestion (hub): the POINTS column stays stuck to the right. It used to travel to
                the end of the content, so in a long championship the horizontal scroll would send it out of view
                and the scrollbar would ride on top of it. The total is the first thing you look at: it doesn't scroll. */}
            {/* 31/08: the number was stuck to its column's edge. Its own paddingRight: the text
                breathes on the inside, and the rail's gap still belongs to the scroller. */}
            <span style={{ width: 64, flex: 'none', textAlign: 'right', fontSize: 10.5, color: 'var(--text-3)', textTransform: 'uppercase', letterSpacing: '.08em',
                           position: 'sticky', right: 0, background: 'var(--surface-2)', paddingLeft: 10, paddingRight: 6, borderLeft: '1px solid var(--line)', zIndex: 2 }}>{t('champ_pts')}</span>
          </div>
          {/* rows */}
          {p.detail.standings.map((row: SeasonStandRow) => (
            <div key={row.rank} className="row" style={{ alignItems: 'center', padding: '8px 0', borderBottom: '1px solid color-mix(in oklab, var(--line) 50%, transparent)', background: row.isPlayer ? 'color-mix(in oklab, var(--accent) 8%, transparent)' : 'transparent' }}>
              <span style={{ width: 30, flex: 'none', textAlign: 'center', fontSize: 12.5, color: 'var(--text-3)' }}>{row.rank}</span>
              <span className="row gap6" style={{ flex: 1, minWidth: 140, alignItems: 'center' }}>
                <span style={{ fontWeight: row.isPlayer ? 700 : 500, fontSize: 14, color: row.isPlayer ? 'var(--accent-text)' : 'var(--text)' }}>{row.name}</span>
                {row.isPlayer && <Chip kind="accent" style={{ fontSize: 9 }}>{t('pos_yours')}</Chip>}
              </span>
              {cols.map((rc) => {
                const cell = row.cells.find((c) => c.round === rc.round) ?? { round: rc.round, pos: 0, points: 0, dnf: false, ran: false };
                return <span key={rc.round} style={{ width: cellW, flex: 'none', display: 'flex', justifyContent: 'center' }}><Cell cell={cell} /></span>;
              })}
              {/* hub fixed to the right like its header; the background keeps it opaque over the rounds that
                  scroll underneath it (the player's row keeps its own tint). */}
              <span style={{ width: 64, flex: 'none', textAlign: 'right', fontWeight: 700, fontSize: 15,
                             position: 'sticky', right: 0, paddingLeft: 10, paddingRight: 6, borderLeft: '1px solid var(--line)', zIndex: 1,
                             background: row.isPlayer ? 'color-mix(in oklab, var(--accent) 8%, var(--surface-2))' : 'var(--surface-2)' }}>{row.total}</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}

function AccessPanel({ p }: { p: SeasonPayload }) {
  const { t } = useTranslation();
  const d = p.detail;
  return (
    <div className="col" style={{ gap: 14 }}>
      <SeriesLogo id={p.selectedSeriesId} name={d.seriesDisplay} height={34} fontSize={24} />
      <div style={{ padding: '16px 18px', borderRadius: 0, border: '1px solid var(--line)', background: 'var(--surface)' }}>
        <Kicker style={{ color: 'var(--purple)' }}>{t('access_title')}</Kicker>
        {d.qualHasCal ? (
          <Mono style={{ fontSize: 12, color: 'var(--text-2)', display: 'block', marginTop: 8 }}>{t('via_qualifier')}: {d.qualDisplay} · {d.qualFirstDate}</Mono>
        ) : (
          <Mono style={{ fontSize: 12, color: 'var(--text-3)', display: 'block', marginTop: 8 }}>{t('gate_pending')}</Mono>
        )}
        {d.hasFeeder && (
          <div style={{ marginTop: 12, paddingTop: 12, borderTop: '1px solid var(--line)' }}>
            <Mono style={{ fontSize: 11, color: 'var(--text-2)', display: 'block' }}>{t('via_season')}: {t('gate_win_note')}</Mono>
            <Mono style={{ fontSize: 11, color: 'var(--text-3)', display: 'block', marginTop: 4 }}>{fmt(t('gate_season_fmt'), d.feederTop, d.feederClass)}</Mono>
            {d.feederHasContext && <Mono style={{ fontSize: 11, color: 'var(--accent-text)', display: 'block', marginTop: 4 }}>{fmt(t('gate_context_fmt'), d.feederContextPos, d.feederContextSeries)}</Mono>}
          </div>
        )}
      </div>
      {/* until now this screen only said HOW you get in. What you'd actually race was the other half of the
          decision, and it forced you out to the races tab just to see it. */}
      <SeasonCalendar rounds={d.rounds} />
    </div>
  );
}

export function Season({ client, onGoToCalendar }: { client: ApiClientLike; onGoToCalendar: () => void }) {
  const { t } = useTranslation();
  const [sel, setSel] = useState<string | undefined>(undefined);
  const q = useQuery<SeasonPayload>(client, 'query.season', sel ? { seriesId: sel } : undefined);
  // Tester feedback (Aug 28, hub, video "Interface resets"): picking a championship changes the query params and
  // useQuery empties data (subject change) -- the WHOLE screen fell back to "loading", the rail unmounted, and
  // its scroll reset to zero on every click. This query's subject is mixed (an invariant rail + variable
  // detail), so the screen keeps the last good payload and swaps it in when the new one arrives:
  // the rail never unmounts, and the detail does stale-while-revalidate, like Paddock or the calendar.
  const lastGood = useRef<SeasonPayload | null>(null);
  if (q.data) lastGood.current = q.data;
  const p = q.data ?? lastGood.current;

  const ackBonus = () => { void client.cmd('cmd.season.ackBonus', {}).then(() => q.refetch()).catch(() => q.refetch()); };

  if (q.loading && !p) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || !p) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;

  const sections = [0, 1, 2].map((s) => ({ s, items: p.rail.filter((it) => it.section === s) }));

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column', position: 'relative' }}>
      <div style={{ padding: '16px 30px 10px' }}>
        <Display as="h1" style={{ fontSize: 30 }}>{t('champs_tab')}</Display>
        <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('champ_my_sub')}</Mono>
      </div>

      <div style={{ flex: 1, display: 'grid', gridTemplateColumns: '300px 1fr', gap: 16, minHeight: 0, padding: '0 30px 20px' }}>
        {/* Rail */}
        <div className="scroll col" style={{ gap: 14, minHeight: 0 }} data-tour="champ.rail">
          {sections.map(({ s, items }) => (
            <div key={s} className="col" style={{ gap: 8 }}>
              <Kicker style={{ color: s === 0 ? 'var(--accent-text)' : 'var(--text-3)' }}>{t(SECTION_KEY[s])}</Kicker>
              {items.length === 0 && s === 0
                ? <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('champ_none_enrolled')}</Mono>
                : items.map((it) => <RailCard key={it.seriesId} it={it} onSelect={setSel} />)}
            </div>
          ))}
        </div>

        {/* Detail */}
        <div className="col" style={{ minHeight: 0, padding: '4px 0' }} data-tour="champ.detail">
          {p.detail.kind === 1 ? <StandingsGrid p={p} onGoToCalendar={onGoToCalendar} />
            : p.detail.kind === 2 ? <AccessPanel p={p} />
            : p.detail.kind === 3 ? (
              <div className="col" style={{ gap: 10 }}>
                <SeriesLogo id={p.selectedSeriesId} name={p.detail.seriesDisplay} height={34} fontSize={24} />
                <SeasonCalendar rounds={p.detail.rounds} />   {/* */}
                <Btn variant="ghost" size="sm" style={{ alignSelf: 'flex-start' }} onClick={onGoToCalendar}>{t('see_class_races')} →</Btn>
              </div>
            ) : (
              <Center><Mono style={{ color: 'var(--text-3)' }}>{t('champ_my_sub')}</Mono></Center>
            )}
        </div>
      </div>

      {/* End-of-season BONUS modal */}
      {p.bonusShown && (
        <div style={{ position: 'absolute', inset: 0, background: 'rgba(6,7,10,.62)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 40 }}>
          {/* 27/09 (audit): aria-modal, like the rest of the modals. Without the mark, the controller navigated behind the veil. */}
          <div className="panel" role="dialog" aria-modal="true" aria-label={t('season_done')}
               style={{ width: 460, padding: '26px 28px', border: '1px solid var(--gold)', display: 'flex', flexDirection: 'column', gap: 14 }}>
            <Display style={{ fontSize: 24, color: 'var(--gold)' }}>{t('season_done')}</Display>
            <Mono style={{ fontSize: 16, color: 'var(--text)' }}>{t('season_final_pos')} P{p.bonusPlayerPos} / {p.bonusField}</Mono>
            <Mono style={{ fontSize: 15, color: 'var(--green)', fontWeight: 700 }}>+{formatCreditsCr(p.bonusMoney)}  ·  +{p.bonusXp} XP  ·  +{p.bonusRep} REP</Mono>
            <Btn variant="primary" style={{ alignSelf: 'flex-end', marginTop: 4 }} onClick={ackBonus}>{t('season_back_select')} →</Btn>
          </div>
        </div>
      )}
    </div>
  );
}
