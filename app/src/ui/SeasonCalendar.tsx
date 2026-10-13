// player feedback: "when I check a championship to sign up, the screen only says how many races it has.
// To find out WHICH ones, you have to leave for the races tab."
//
// The calendar for a championship you're not in yet. It's a different decision from the
// standings table: there you check how the season is going, here you check whether it fits you. That's why every round carries its duration --
// a 24-hour round is a different commitment than a sprint -- and its time of day, which decides whether you're driving at night.
//
// Used in the Championships ACCESS variant and in a Paddock seat's profile: it's the same question.
import { useTranslation } from 'react-i18next';
import type { SeasonRoundCol } from '../api/contracts';
import { Mono, Kicker } from './components';
import { formatRaceLength } from './format';

/** The round's duration exactly as the player will experience it: time or laps, never both. */
function lengthOf(r: SeasonRoundCol, laps: string): string {
  if (r.durationMin > 0) return formatRaceLength(r.durationMin);
  if (r.laps > 0) return `${r.laps} ${laps}`;
  return '—';
}

export function SeasonCalendar({ rounds, maxHeight = '34vh' }: { rounds: SeasonRoundCol[]; maxHeight?: string }) {
  const { t } = useTranslation();
  if (!rounds.length) return null;
  // Sessions: they're per-round in the data, but in practice a championship keeps them identical across all rounds. It's
  // summarized ONCE if they match, instead of repeating "60/15" thirteen times next to every race.
  const p0 = rounds[0].practiceMin, q0 = rounds[0].qualifyMin;
  const mismasSesiones = rounds.every((r) => r.practiceMin === p0 && r.qualifyMin === q0);

  return (
    <div className="col" style={{ gap: 6, minHeight: 0 }}>
      <div className="row between" style={{ alignItems: 'baseline', gap: 10 }}>
        <Kicker style={{ fontSize: 9.5 }}>{t('season_cal_title')}</Kicker>
        <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{t('season_cal_count', { n: rounds.length })}</Mono>
      </div>

      <div className="scroll col" data-testid="season-calendar" style={{ gap: 3, maxHeight, minHeight: 0, overflowY: 'auto' }}>
        {rounds.map((r) => (
          <div key={r.round} className="row between" data-testid={`season-cal-r${r.round}`}
               style={{ alignItems: 'baseline', gap: 10, padding: '4px 0', borderBottom: '1px solid color-mix(in oklab, var(--line) 45%, transparent)' }}>
            <span className="row gap6" style={{ alignItems: 'baseline', minWidth: 0 }}>
              <Mono style={{ fontSize: 9.5, color: 'var(--text-3)', flex: 'none', width: 26 }}>R{r.round}</Mono>
              <Mono style={{ fontSize: 11, color: 'var(--text-2)', minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{r.trackName}</Mono>
            </span>
            <span className="row gap8" style={{ alignItems: 'baseline', flex: 'none' }}>
              <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{r.date}</Mono>
              <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{String(r.hour).padStart(2, '0')}:00</Mono>
              <Mono style={{ fontSize: 10.5, color: 'var(--text-2)', minWidth: 46, textAlign: 'right' }}>{lengthOf(r, t('laps'))}</Mono>
            </span>
          </div>
        ))}
      </div>

      {mismasSesiones && (p0 > 0 || q0 > 0) && (
        <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>{t('season_cal_sessions', { p: p0, q: q0 })}</Mono>
      )}
    </div>
  );
}
