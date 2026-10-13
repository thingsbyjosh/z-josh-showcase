// SIMULATE THE WORLD UP TO A DATE (debug menu, Ctrl+Shift+D).
//
// Exists so nobody has to manually test what happens as time passes. Under the hood it runs SimulateWorld, which
// advances the clock through the SAME path the app itself uses, and runs whatever rounds come due before leaving them behind
// (skipped rounds count as an absence, and after three the contract breaks on its own).
//
// By default it does NOT touch the save: the engine works on a copy and returns the timeline. "Let's see what
// happens up to 2028" can't cost you your career. Applying it is a separate checkbox, and it's red on purpose.
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike } from '../api/contracts';
import { Kicker, Mono } from '../ui/components';

export interface SimWorldEvent {
  year: number; doy: number; kind: string;
  series: string; team: string; num: number; detail: string; money: number;
}
export interface SimWorldResult {
  ok: boolean; applied: boolean; days: number;
  fromYear: number; fromDoy: number; toYear: number; toDoy: number;
  races: number; windows: number; applications: number; signings: number;
  breaks: number; droughtDays: number; rollovers: number;
  credits: number; rep: number;
  timeline: SimWorldEvent[];
}

// day-of-year (1..366) from an ISO "YYYY-MM-DD" date. The engine reasons in (year, day-of-year), so the conversion
// lives here and not there: the picker speaks in dates because that's how people think, and the clock speaks in day-of-year because that's how it's
// measured.
function doyFromIso(iso: string): { year: number; doy: number } | null {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(iso);
  if (!m) return null;
  const y = Number(m[1]), mo = Number(m[2]), d = Number(m[3]);
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return null;
  const utc = Date.UTC(y, mo - 1, d), ini = Date.UTC(y, 0, 1);
  return { year: y, doy: Math.round((utc - ini) / 86400000) + 1 };
}

// the news items worth a line. Races are counted separately: over three years there are hundreds of them, and they'd bury
// exactly what you came here to look at, which is the market.
const RUIDO = new Set(['race', 'applied']);

export function DebugSimWorld({ client, onClose }: { client: ApiClientLike; onClose: () => void }) {
  const { t } = useTranslation();
  const [hasta, setHasta] = useState('');
  const [aplicar, setAplicar] = useState(false);
  // the player's on-track level is CHOSEN, not inferred: what's being tested is the CONSEQUENCES of
  // performing at that level (what the world does if you dominate, scrap, or sink), not modeling how anyone actually drives.
  const [perf, setPerf] = useState(2);
  const [corriendo, setCorriendo] = useState(false);
  const [error, setError] = useState('');
  const [res, setRes] = useState<SimWorldResult | null>(null);

  const destino = doyFromIso(hasta);

  const correr = async () => {
    if (!destino || corriendo) return;
    setCorriendo(true); setError(''); setRes(null);
    try {
      const r = await client.cmd<SimWorldResult>('cmd.debug.simWorld', {
        toYear: destino.year, toDoy: destino.doy, apply: aplicar, perf,
      });
      setRes(r);
    } catch (e) {
      setError(String(e));
    } finally {
      setCorriendo(false);
    }
  };

  const hitos = (res?.timeline ?? []).filter((e) => !RUIDO.has(e.kind));

  return (
    <div onClick={onClose} data-debug-simworld="1"
         style={{ position: 'fixed', inset: 0, background: 'rgba(6,7,10,.6)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 62 }}>
      <div className="panel" onClick={(e) => e.stopPropagation()}
           style={{ width: 560, maxHeight: '82vh', padding: '18px 20px', display: 'flex', flexDirection: 'column', gap: 12 }}>
        <Kicker style={{ color: 'var(--amber)' }}>{t('debug.sim.title', { defaultValue: 'Simulate the world' })}</Kicker>
        <Mono style={{ fontSize: 10, color: 'var(--text-3)', lineHeight: 1.5 }}>
          {t('debug.sim.desc', { defaultValue: 'Runs the real engine: races are simulated, but time passes exactly as it would in game.' })}
        </Mono>

        <div className="row" style={{ gap: 10, alignItems: 'center', flexWrap: 'wrap' }}>
          <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('debug.sim.until', { defaultValue: 'UNTIL' })}</Mono>
          <input type="date" value={hasta} onChange={(e) => setHasta(e.target.value)}
                 data-debug-simworld-date="1"
                 style={{ background: 'var(--bg-2)', color: 'var(--text)', border: '1px solid var(--line)', padding: '5px 8px', fontFamily: 'var(--font-mono)', fontSize: 12 }} />
          <select value={perf} onChange={(e) => setPerf(Number(e.target.value))}
                  data-debug-simworld-perf="1"
                  style={{ background: 'var(--bg-2)', color: 'var(--text)', border: '1px solid var(--line)', padding: '5px 8px', fontFamily: 'var(--font-mono)', fontSize: 11 }}>
            <option value={0}>{t('debug.sim.perf0', { defaultValue: 'You win everything' })}</option>
            <option value={1}>{t('debug.sim.perf1', { defaultValue: 'You fight at the front' })}</option>
            <option value={2}>{t('debug.sim.perf2', { defaultValue: 'You run midfield' })}</option>
            <option value={3}>{t('debug.sim.perf3', { defaultValue: 'You struggle at the back' })}</option>
          </select>
          <label className="row" style={{ gap: 6, alignItems: 'center', cursor: 'pointer' }}>
            <input type="checkbox" checked={aplicar} onChange={(e) => setAplicar(e.target.checked)} data-debug-simworld-apply="1" />
            <Mono style={{ fontSize: 10, color: aplicar ? 'var(--danger)' : 'var(--text-3)' }}>
              {t('debug.sim.apply', { defaultValue: 'APPLY TO SAVE' })}
            </Mono>
          </label>
          <button className="btn btn-sm btn-primary" disabled={!destino || corriendo}
                  data-debug-simworld-run="1" onClick={() => void correr()}
                  style={{ marginLeft: 'auto' }}>
            {corriendo ? t('debug.sim.running', { defaultValue: 'Running…' }) : t('debug.sim.run', { defaultValue: 'Simulate' })}
          </button>
        </div>

        {aplicar && (
          <Mono style={{ fontSize: 10, color: 'var(--danger)' }}>
            {t('debug.sim.applyWarn', { defaultValue: 'This advances your actual career. Without it, nothing is touched.' })}
          </Mono>
        )}
        {!!error && <Mono style={{ fontSize: 10, color: 'var(--danger)' }}>{error}</Mono>}

        {res && (
          <div className="col" style={{ gap: 10, minHeight: 0 }}>
            <div className="row" style={{ gap: 14, flexWrap: 'wrap' }}>
              {([
                ['debug.sim.races', res.races, 'races'],
                ['debug.sim.windows', res.windows, 'windows'],
                ['debug.sim.signings', res.signings, 'signings'],
                ['debug.sim.breaks', res.breaks, 'breaks'],
                ['debug.sim.drought', res.droughtDays, 'drought'],
              ] as [string, number, string][]).map(([k, v, id]) => (
                <span key={id} className="col" style={{ gap: 1 }} data-simworld-stat={id}>
                  <span style={{ fontSize: 18, lineHeight: 1 }}>{v}</span>
                  <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)' }}>{t(k, { defaultValue: id.toUpperCase() })}</Mono>
                </span>
              ))}
              <span className="col" style={{ gap: 1, marginLeft: 'auto', alignItems: 'flex-end' }}>
                <span className="mono" style={{ fontSize: 12 }}>{res.toYear}/{String(res.toDoy).padStart(3, '0')}</span>
                <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)' }}>
                  {t('debug.sim.landed', { defaultValue: 'LANDED ON' })}
                </Mono>
              </span>
            </div>

            {/* the timeline WITHOUT the races: over three years there are hundreds of them, and they'd bury the market, which is
                what you actually came here to look at. The counter above already says how many there were. */}
            <div className="col" style={{ gap: 2, overflowY: 'auto', maxHeight: '40vh', borderTop: '1px solid var(--line)', paddingTop: 8 }}>
              {hitos.length === 0 && (
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
                  {t('debug.sim.nothing', { defaultValue: 'Nothing happened in the market over that span.' })}
                </Mono>
              )}
              {hitos.map((e, i) => (
                <span key={i} className="row" style={{ gap: 8, fontSize: 11, alignItems: 'baseline' }} data-simworld-event={e.kind}>
                  <span className="mono" style={{ color: 'var(--text-3)', minWidth: 62 }}>{e.year}/{String(e.doy).padStart(3, '0')}</span>
                  <span className="mono" style={{ minWidth: 74, color: 'var(--accent-text)' }}>{e.kind}</span>
                  <span style={{ color: 'var(--text-2)', minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                    {e.team || e.series}
                  </span>
                  {!!e.detail && <span className="mono" style={{ color: 'var(--text-3)', marginLeft: 'auto' }}>{e.detail}</span>}
                </span>
              ))}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
