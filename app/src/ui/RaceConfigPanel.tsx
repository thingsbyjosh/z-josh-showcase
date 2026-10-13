// RaceConfigPanel -- WHAT the race would have been configured with (a DEBUG tool).
// Reads query.race_config: the snapshot the engine takes inside its apply-event step, after resolving Auto/Inherited
// and BEFORE writing anything to the game. Its home is covering SIMULATION MODE: there's no game there
// to look at, so without this panel the only trace of the configuration was the log ([event] APPLY / params).
// READING RULE: the engine sends sentinels, not decoration. A field the mod does NOT write arrives at its sentinel
// value (<0, or 0 for the "0 = don't force" fields), and here it's shown as "Not forced" -- or "Inherited" if it's a Settings decision
// (`inherited`). Both cases leave the player's own Custom Race config alone, but for different reasons, and
// that's why they're distinguished: one means the event doesn't set it, the other means you asked for it not to be touched.
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, RaceConfigPayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { weatherKeyForCode } from '../events/eventHelpers';
import { Kicker, Mono } from './components';

// One row of the panel. `muted` = the value isn't a configured number but a state (Inherited / Not forced).
function Row({ label, value, muted }: { label: string; value: string; muted?: boolean }) {
  return (
    <div className="row between" style={{ gap: 18, padding: '5px 0', borderBottom: '1px solid var(--line)' }}>
      <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', letterSpacing: '0.06em', textTransform: 'uppercase', flex: 'none' }}>{label}</Mono>
      <Mono style={{ fontSize: 11.5, color: muted ? 'var(--text-3)' : 'var(--text)', textAlign: 'right', minWidth: 0 }}>{value}</Mono>
    </div>
  );
}

export function RaceConfigPanel({ client }: { client: ApiClientLike }) {
  const { t } = useTranslation();
  const q = useQuery<RaceConfigPayload>(client, 'query.race_config');
  const c = q.data;
  // not_ready (nothing has launched yet this session) or an old engine without the query -> the panel doesn't exist:
  // coverage stays exactly as it always was. Never a gap saying "error" in the middle of a launch.
  if (!c) return null;

  const inheritedTxt = t('inherit.inherited');
  const notForced = t('raceCfg.notForced');
  const rows: { label: string; value: string; muted?: boolean }[] = [];
  const add = (label: string, value: string, muted?: boolean) => rows.push({ label, value, muted });
  // a concrete value, or whichever state applies: Inherited (a Settings decision) takes priority over Not forced (the event doesn't set it).
  const orState = (inherited: boolean, value: string | null) =>
    inherited ? { v: inheritedTxt, muted: true } : value ? { v: value, muted: false } : { v: notForced, muted: true };

  const evento = [c.seriesName, c.trackName].filter(Boolean).join(' · ');
  if (evento) add(t('raceCfg.event'), evento);
  else if (c.raceId) add(t('raceCfg.event'), c.raceId);

  add(t('raceCfg.format'),
      c.durationMin > 0 ? `${c.durationMin} min` : c.laps > 0 ? `${c.laps} ${t('raceCfg.laps')}` : notForced,
      c.durationMin <= 0 && c.laps <= 0);

  add(t('raceCfg.rivals'),
      c.opponents > 0 ? `${c.opponents}${c.numClasses > 1 ? ` · ${c.numClasses} ${t('raceCfg.classes')}` : ''}` : notForced,
      c.opponents <= 0);

  {   // AI: skill/aggression/mistakes/wet-weather, each with its own sentinel
    const parts: string[] = [];
    if (c.ai.skill > 0) parts.push(`${t('raceCfg.aiSkill')} ${c.ai.skill}`);
    if (c.ai.aggression > 0) parts.push(`${t('raceCfg.aiAggr')} ${c.ai.aggression}`);
    // 19/09: the errors sentinel is -1; 0 is "off" and the engine writes it (qualifier-gate)
    if (c.ai.mistakes >= 0) parts.push(`${t('raceCfg.aiMistakes')} ${c.ai.mistakes}`);
    if (c.ai.wetSkill >= 0) parts.push(`${t('raceCfg.aiWet')} ${c.ai.wetSkill}`);
    const s = orState(c.inherited.ai, parts.length ? parts.join(' · ') : null);
    add(t('inherit.ai'), s.v, s.muted);
  }

  {   // weather: the 4 states shown by NAME (never the raw number); consecutive identical states collapse into one
    const names: string[] = [];
    let prev = -1;
    for (const w of c.weather) {
      if (w < 0 || w === prev) continue;
      const k = weatherKeyForCode(w);
      names.push(k ? t(k) : String(w));
      prev = w;
    }
    const slots = c.weatherSlots > 0 ? ` (${c.weatherSlots})` : '';
    const s = orState(c.inherited.weather, names.length ? names.join(' → ') + slots : null);
    add(t('inherit.weather'), s.v, s.muted);
  }

  {
    const s = orState(c.inherited.hour, c.startHour >= 0 ? `${String(c.startHour).padStart(2, '0')}:00` : null);
    add(t('inherit.hour'), s.v, s.muted);
  }

  {   // time progression: index 1..4 = real time / x2 / x5 / x10
    const TP = ['', t('raceCfg.timeReal'), 'x2', 'x5', 'x10'];
    const s = orState(c.inherited.timeProg, c.timeProg >= 1 && c.timeProg <= 4 ? TP[c.timeProg] : null);
    add(t('inherit.timeProg'), s.v, s.muted);
  }

  {
    const s = orState(c.inherited.start,
                      c.startType === 1 ? t('raceCfg.startRolling') : c.startType === 0 ? t('raceCfg.startStanding') : null);
    add(t('inherit.start'), s.v, s.muted);
  }

  {   // pit stops: mandatory stop + minimums + refueling + speed limit
    const parts: string[] = [];
    if (c.pit.mandatory >= 0) parts.push(c.pit.mandatory === 1 ? t('raceCfg.pitMandatory') : t('raceCfg.pitFree'));
    if (c.pit.minTyres > 0) parts.push(`${c.pit.minTyres} ${t('raceCfg.tyres')}`);
    if (c.pit.refuelling >= 0) parts.push(c.pit.refuelling === 1 ? t('raceCfg.refuelYes') : t('raceCfg.refuelNo'));
    if (c.pit.speedLimit >= 0) parts.push(`${c.pit.speedLimit} km/h`);
    const s = orState(c.inherited.pit, parts.length ? parts.join(' · ') : null);
    add(t('inherit.pit'), s.v, s.muted);
  }

  // FCY: 1/0 = an explicit Settings decision (forced); -1 = Inherited (same visual family as the inherit fields).
  add(t('raceCfg.fcy'), c.fcy === 1 ? t('raceCfg.on') : c.fcy === 0 ? t('raceCfg.off') : inheritedTxt, c.fcy < 0);

  {   // wear/fuel consumption: a GLOBAL game option scaled to race length, not part of
      // the handoff. Two states per axis: scaling turned off in Settings (your own global
      // setting wins) or a multiplier ALWAYS applied once the toggle is armed (x1 for the reference-length race).
    const wear = (axis: { scale: boolean; mult: number }) =>
      !axis.scale ? { v: t('raceCfg.wearOff'), muted: true }
      : { v: `x${Math.max(1, axis.mult)}`, muted: false };
    const ty = wear(c.wear.tyre); add(t('gpscale.tyre'), ty.v, ty.muted);
    const fu = wear(c.wear.fuel); add(t('gpscale.fuel'), fu.v, fu.muted);
  }

  {   // practice / qualifying: the ACTUAL minutes that get pinned (the engine already resolved the defaults).
      // on/off is just another sentinel (1 = turn it on, 0 = turn it off, -1 = leave it alone), and the mod
      // does NOT turn practice on: it only sets its duration. Announcing the bare minutes used to promise a session that
      // the game leaves off if that's how the player has it set, so now every session is listed with what will actually happen.
    const sess = (on: number, min: number) =>
      on === 0 ? t('raceCfg.sessOff') : `${min}'${on === 1 ? '' : t('raceCfg.sessIfOn')}`;
    const s = orState(c.inherited.sessions,
                      `${sess(c.sessions.practiceOn, c.sessions.practiceMin)} / ${sess(c.sessions.qualifyOn, c.sessions.qualifyMin)}`);
    add(t('inherit.sessions'), s.v, s.muted);
  }

  if (c.date.day > 0 && c.date.month > 0) {
    // the year is -1 ("don't force") when the class's era doesn't yield a valid year: the mod sets day/month but leaves the
    // game's own year alone (only pinned when dateYear>0). No year -> shows dd/mm only.
    const dm = `${String(c.date.day).padStart(2, '0')}/${String(c.date.month).padStart(2, '0')}`;
    add(t('raceCfg.date'), c.date.year > 0 ? `${dm}/${c.date.year}` : dm);
  }

  if (c.vehHash) add(t('raceCfg.car'), `#${c.vehHash}${c.livery >= 0 ? ` · ${t('raceCfg.livery')} ${c.livery}` : ''}`);
  if (c.trial) add(t('raceCfg.trial'), `Top ${c.trialTopN}`);

  return (
    <div className="col" style={{ gap: 8, width: '100%', maxWidth: 460, textAlign: 'left',
                                  border: '1px solid var(--line)', background: 'var(--surface)', padding: '14px 16px' }}>
      <div className="row between" style={{ alignItems: 'baseline', gap: 12 }}>
        <Kicker>{t('raceCfg.title')}</Kicker>
        {c.sim && <Mono style={{ fontSize: 9.5, color: 'var(--amber)', letterSpacing: '0.08em' }}>{t('raceCfg.notApplied')}</Mono>}
      </div>
      <div className="col">
        {rows.map((r) => <Row key={r.label} label={r.label} value={r.value} muted={r.muted} />)}
      </div>
    </div>
  );
}
