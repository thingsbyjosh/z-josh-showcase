// Results: post-race settlement, ported from the engine's own ImGui overlay (the results-drawing code,
// which is the source of truth for WHAT is shown and in WHAT ORDER), with the visual design of a separate mockup.
// A 100% READ-ONLY screen: live data arrives via query.results; navigation (Continue -> Hub)
// is owned by the shell, not this screen (same convention as the Inbox). Static labels and engine
// keys (money lines) are resolved via t() (bilingual via the translation table: a missing key falls back to itself).
import { useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, ResultsPayload, HubPayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { Panel, Chip, Kicker, Display, Mono, AccentDot, BoltIcon } from '../ui/components';
import { classDisplayName } from '../ui/classNames';
import { accentText } from '../ui/colors';
import { formatTime, formatGap, isDnf, dnfLabel } from '../ui/format';
import { SponsorCardView } from '../ui/SponsorCardView';
import { useScreenCrumb } from '../support/useScreenCrumb';
import { FeelCard, GapOfferCard } from './results/PostRaceFeedback';
import { sendRaceTelemetry } from '../telemetry/raceFeedback';
import { machineHash } from '../account/accountStore';

// credits -> "12,500 credits". Replaces the overlay's manual currency formatter with toLocaleString (per the design rule).
const fmtKr = (n: number): string => '₡ ' + Math.round(n).toLocaleString('en-US');

// delta chip, top-right of each progression card (▲ +n green / ▼ n red / ± gray).
function Delta({ d, suffix, money }: { d: number; suffix?: string; money?: boolean }) {
  const color = d > 0 ? 'var(--green)' : d < 0 ? 'var(--danger)' : 'var(--text-3)';
  const arrow = d > 0 ? '▲ +' : d < 0 ? '▼ ' : '± ';
  const val = money ? fmtKr(Math.abs(d)) : String(Math.abs(d));
  return <Mono style={{ fontSize: 11, color, fontWeight: 700 }}>{arrow}{val}{suffix ?? ''}</Mono>;
}

// card in the 2x2 progression grid: icon+label, delta chip, before -> AFTER (large).
function ProgCard({ icon, label, color, before, after, delta, suffix, money }: {
  icon: string; label: string; color: string;
  before: number; after: number; delta: number; suffix?: string; money?: boolean;
}) {
  const fmt = (x: number): string => (money ? fmtKr(x) : String(x));
  return (
    <Panel style={{ padding: '12px 14px', background: 'var(--bg-2)', display: 'flex', flexDirection: 'column', gap: 8 }}>
      <div className="row between" style={{ alignItems: 'center' }}>
        <span className="row gap8" style={{ alignItems: 'center' }}>
          <span style={{ color: accentText(color), fontSize: 13 }}>{icon}</span>
          <Mono style={{ fontSize: 10, letterSpacing: '.1em', textTransform: 'uppercase', color: 'var(--text-3)' }}>{label}</Mono>
        </span>
        <Delta d={delta} suffix={suffix} money={money} />
      </div>
      <div className="row gap8" style={{ alignItems: 'baseline' }}>
        <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{fmt(before)}</Mono>
        <span style={{ color: 'var(--text-3)', fontSize: 11 }}>→</span>
        <Display style={{ fontSize: 22, fontStyle: 'italic', color }}>{fmt(after)}</Display>
      </div>
    </Panel>
  );
}

export function Results({ client, onDone }: { client: ApiClientLike; onDone?: () => void }) {
  const { t } = useTranslation();
  useScreenCrumb(client, 'results');   // audit note, 07/20
  const q = useQuery<ResultsPayload>(client, 'query.results');
  const hq = useQuery<HubPayload>(client, 'query.hub');   // phase/XP for the progress bar toward the next tier
  // Telemetry is sent ON ITS OWN, with consent, with no button triggering it. TWICE:
  // on ENTRY, so the race is safely recorded as soon as it exists (if the app closes or the save is abandoned
  // without returning to the hub, it's already been sent), and on EXIT, which is when it's known whether the form was answered. The
  // backend merges both into a single row. If the network call fails, the race goes into a retry queue.
  const clientRef = useRef(client);
  clientRef.current = client;
  useEffect(() => {
    void sendRaceTelemetry(clientRef.current, machineHash);
    return () => { void sendRaceTelemetry(clientRef.current, machineHash); };
  }, []);

  // The answer goes to the engine the moment it's marked (applies the local effect and stays around for the
  // payload). Fire-and-refresh like the rest of the app: the engine is the source of truth.
  const answerFeel = (feel: number, reasons: string[]) => {
    void client.cmd('cmd.results.feel', { feel, reasons }).catch(() => {});
  };
  const decideOffer = (accept: boolean) => {
    void client.cmd('cmd.results.gapOffer', { accept }).then(() => q.refetch()).catch(() => {});
  };

  if (q.loading) return <div className="col center grow" style={{ height: '100%' }}><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></div>;
  if (q.error || !q.data) return <div className="col center grow" style={{ height: '100%' }}><Mono style={{ color: 'var(--text-3)' }}>{t('results.noResult')}</Mono></div>;

  const r = q.data;
  // P2 (F15): the retirement comes from `finished`; `pos` is the position you retired in (“DNF (P24)”).
  const dnf = isDnf(r.pos, r.finished);
  const podium = !dnf && r.pos <= 3;
  const posColor = dnf ? 'var(--text-3)' : r.pos === 1 ? 'var(--gold)' : podium ? 'var(--cyan)' : 'var(--text)';

  const { timing, money, progression: prog, championship: champ } = r;
  const rating = hq.data?.rating;   // ladder level + XP toward the next tier (post-race)
  // The "next tier" bar as a reusable block — it goes in the RIGHT column if there's a championship
  // (making use of its space), or in the left one if it's a single-column layout. Reuses query.hub (phase + XP after the race).
  const tierPanel = rating && rating.xpThreshold > 0 ? (
    <Panel style={{ padding: '12px 20px' }}>
      <div className="row between" style={{ alignItems: 'baseline', marginBottom: 7 }}>
        <Kicker>{t('results.tierProgress', { defaultValue: 'Next tier' })}</Kicker>
        <Mono style={{ fontSize: 11, color: 'var(--text-2)' }}>{rating.xpToward} / {rating.xpThreshold} XP</Mono>
      </div>
      <div className="row between" style={{ alignItems: 'baseline', marginBottom: 5 }}>
        <Mono style={{ fontSize: 12, color: 'var(--text-2)' }}>{t(`tree.level.${rating.phase}`)}</Mono>
        {rating.phase < 7
          ? <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>→ {t(`tree.level.${rating.phase + 1}`)}</Mono>
          : <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('home.tierMax', { defaultValue: 'Max tier' })}</Mono>}
      </div>
      <div style={{ height: 6, borderRadius: 3, background: 'var(--surface-3)' }}>
        <div style={{ width: `${Math.max(0, Math.min(100, (rating.xpToward / rating.xpThreshold) * 100))}%`, height: '100%', borderRadius: 3, background: 'var(--cyan)' }} />
      </div>
    </Panel>
  ) : null;
  // The two post-race surfaces (the anomalous-gap offer + the feel question) go
  // TOGETHER, at the top of the RIGHT column when there's a championship: they're the only two things on this screen that
  // ask something of you and that expire along with it (standings and tier can still be checked later in Season and Profile).
  // They also lighten the left column, which already carries times, money, sponsors, and the 2x2 grid.
  // With no championship there's no right column: the screen is single-column and they stay in the flow.
  const feedbackPanels = (
    <>
      {r.feedback?.offer && (
        <GapOfferCard gapPct={r.feedback.gapPct} current={r.feedback.curDifficulty}
                      offered={r.feedback.offerDifficulty} held={r.feedback.held} onDecide={decideOffer} />
      )}
      {r.feedback?.ask && (
        <FeelCard feel={r.feedback.feel} telemetryOptIn={r.feedback.telemetryOptIn} onAnswer={answerFeel} />
      )}
    </>
  );
  // the FINAL gap to your class leader AT THE FINISH (not a pace metric): if you WON (pos===1) the line is HIDDEN;
  // if you got lapped -> "+N L"; otherwise -> the time gap to the leader at the finish line; DNF / no data -> "—".
  const isLeader = !dnf && r.pos === 1;
  const gapLeaderText = dnf ? '—'
    : timing.gapLeaderLaps > 0 ? `+${timing.gapLeaderLaps} ${t('times_lap_abbr')}`
    : timing.gapLeaderS > 0 ? formatGap(timing.gapLeaderS)
    : '—';
  const rankColor = champ == null ? 'var(--text)' : champ.playerRank === 1 ? 'var(--gold)' : champ.playerRank <= 3 ? 'var(--cyan)' : 'var(--text)';

  return (
    <div className="screen scroll" style={{ height: '100%' }}>
      <div className="col" style={{ minHeight: '100%', padding: '34px 40px 40px', alignItems: 'center', gap: 22 }}>
        {onDone && <button className="btn btn-sm btn-ghost" style={{ alignSelf: 'flex-start' }} onClick={onDone}>← {t('common.back')}</button>}
        <Kicker style={{ color: 'var(--accent-text)' }}>{t('results.title')} · {r.trackName}</Kicker>

        {/* hero row: giant position number + car/class/track identity */}
        <div className="row gap24" style={{ alignItems: 'center', flexWrap: 'wrap', justifyContent: 'center' }} data-tour="results.hero">
          <div className="col center">
            <Display style={{ fontSize: 110, lineHeight: 0.8, fontStyle: 'italic', color: posColor }}>{dnf ? t('results.dnf') : 'P' + r.pos}</Display>
            <Mono style={{ fontSize: 12, color: 'var(--text-3)', letterSpacing: '.2em', marginTop: 4, whiteSpace: 'nowrap' }}>
              {dnf ? (r.pos > 0 ? `${dnfLabel(r.pos, t('results.dnf'))} · ${t('results.grid')} ${r.field}` : t('results.dnf'))
                : `${t('results.position')} · ${t('results.grid')} ${r.field}`}
            </Mono>
            {/* Multiclass: the hero's pos/field are already CLASS-relative (from the engine); the overall standing is just for context */}
            {!dnf && r.multiclass && r.overallPos != null && r.overallPos > 0 && r.overallField != null && (
              <Mono style={{ fontSize: 11, color: 'var(--text-3)', marginTop: 2, whiteSpace: 'nowrap' }}>
                {t('results.overall', { pos: r.overallPos, field: r.overallField })}
              </Mono>
            )}
          </div>
          <div className="col" style={{ gap: 6, alignItems: 'flex-start' }}>
            <span className="row gap8" style={{ alignItems: 'center' }}>
              <AccentDot color={r.classColor} size={11} />
              <Mono style={{ fontSize: 11, color: accentText(r.classColor), letterSpacing: '.1em' }}>{classDisplayName(r.className).toUpperCase()}</Mono>
            </span>
            <Display style={{ fontSize: 26, whiteSpace: 'nowrap' }}>{r.carName}</Display>
            <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{r.trackName}</Mono>
          </div>
        </div>

        {/* content: 1 or 2 columns depending on whether there's a championship */}
        <div style={{ display: 'grid', gridTemplateColumns: champ ? '1fr 1fr' : '1fr', gap: 18, width: '100%', maxWidth: champ ? 920 : 560, alignItems: 'start' }}>

          {/* LEFT: times + money + progression */}
          <div className="col gap16">
            {/* times */}
            <Panel style={{ padding: '16px 20px' }}>
              <Kicker style={{ display: 'block', marginBottom: 12 }}>{t('results.timing')}</Kicker>
              <div className="row between" style={{ marginBottom: 8 }}>
                <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('results.totalTime')}</Mono>
                <Mono style={{ fontSize: 14, color: 'var(--text)', fontWeight: 700 }}>{formatTime(timing.totalTimeS, false)}</Mono>
              </div>
              <div className="row between" style={{ marginBottom: 8 }}>
                <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('results.bestLap')}</Mono>
                <Mono style={{ fontSize: 14, color: 'var(--purple)', fontWeight: 700 }}><BoltIcon size={12} /> {formatTime(timing.bestLapS, true)}</Mono>
              </div>
              {!isLeader && (
                <div className="row between">
                  <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{t('results.gapLeader')}</Mono>
                  <Mono style={{ fontSize: 14, color: 'var(--text-2)', fontWeight: 700 }}>{gapLeaderText}</Mono>
                </div>
              )}
            </Panel>

            {/* money: the visible lines sum to the net (an invariant of this panel) */}
            <Panel style={{ padding: '16px 20px' }}>
              <Kicker style={{ display: 'block', marginBottom: 12 }}>{t('results.money')}</Kicker>
              <div className="col gap8">
                {money.lines.map((ln, i) => {
                  const neg = ln.amount < 0;
                  // 19/09 (tester: a sponsor's cut came out in blue). The tone says WHAT the line is about, not whether it
                  // adds or subtracts: a multiplier card that takes money away was still painted cyan. What subtracts, red.
                  const color = neg ? 'var(--danger)' : ln.tone === 'gold' ? 'var(--gold)' : ln.tone === 'cyan' ? 'var(--cyan)' : 'var(--danger)';
                  return (
                    <div key={i} className="row between" style={{ fontSize: 14 }}>
                      <span style={{ color: 'var(--text-2)' }}>{ln.label ?? (ln.key ? t(ln.key) : '')}</span>
                      <span style={{ color, fontWeight: 700 }}>{neg ? '− ' : ''}{fmtKr(Math.abs(ln.amount))}</span>
                    </div>
                  );
                })}
                <div style={{ height: 1, background: 'var(--line)', margin: '4px 0' }} />
                <div className="row between" style={{ fontSize: 16 }}>
                  <span style={{ color: 'var(--text)', fontWeight: 700 }}>{t('results.net')}</span>
                  <span style={{ color: money.net >= 0 ? 'var(--green)' : 'var(--danger)', fontWeight: 700, fontSize: 20 }}>
                    {money.net >= 0 ? '+ ' : '− '}{fmtKr(Math.abs(money.net))}
                  </span>
                </div>
              </div>
            </Panel>

            {/* Sponsor showcase (per feedback): the actual CARD when it TRIGGERS — fired this
                race (with its effect visible on the card panel), expired, or lost (dimmed with a
                stamp). Idle active cards are NOT shown: only events. Falls back to the old chips if
                the engine doesn't emit cardShowcase yet. */}
            {(() => {
              const events = (r.cardShowcase ?? []).filter((s) => s.kind !== 'idle');
              if (events.length > 0) return (
                <Panel style={{ padding: '14px 20px' }}>
                  <Kicker style={{ marginBottom: 10 }}>{t('cards.results.sponsors')}</Kicker>
                  <div className="row" style={{ gap: 12, flexWrap: 'wrap', alignItems: 'flex-start' }}>
                    {events.map((s, i) => (
                      <SponsorCardView key={`${s.def.id}:${i}`} def={s.def} size="sm" level={s.level}
                        deal dealDelay={i * 90}
                        dim={s.kind !== 'fired'}
                        stamp={s.kind === 'expired' ? t('cards.results.expired').toUpperCase()
                             : s.kind === 'lost' ? t('cards.results.lost').toUpperCase() : null} />
                    ))}
                  </div>
                </Panel>
              );
              if (r.cardChips && r.cardChips.length > 0) return (
                <Panel style={{ padding: '14px 20px' }}>
                  <Kicker style={{ marginBottom: 10 }}>{t('cards.results.sponsors')}</Kicker>
                  <div className="row gap8" style={{ flexWrap: 'wrap' }}>
                    {r.cardChips.map((c, i) => (
                      <Chip key={i} kind={c.kind === 'fired' ? 'green' : c.kind === 'lost' ? 'accent' : 'lock'}>
                        <span aria-hidden="true">{c.kind === 'fired' ? '✓' : c.kind === 'idle' ? '·' : '✕'}</span>
                        <span>{c.text}</span>
                      </Chip>
                    ))}
                  </div>
                </Panel>
              );
              return null;
            })()}

            {/* 2x2 progression: ELO / REP / EXP / Balance */}
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }} data-tour="results.progress">
              <ProgCard icon="◆" label={`${t('results.elo')} ${prog.elo.band}`} color={prog.elo.color} before={prog.elo.before} after={prog.elo.after} delta={prog.elo.delta} />
              <ProgCard icon="★" label={t('results.rep')} color={prog.rep.color} before={prog.rep.before} after={prog.rep.after} delta={prog.rep.delta} suffix={' ' + t('common.rep')} />
              <ProgCard icon="✦" label={t('results.exp')} color="var(--cyan)" before={prog.exp.before} after={prog.exp.after} delta={prog.exp.delta} />
              <ProgCard icon="₡" label={t('results.balance')} color="var(--gold)" before={prog.balance.before} after={prog.balance.after} delta={prog.balance.delta} money />
            </div>

            {/* With no championship, the screen is a single column, so the question about
the (optional, skippable) sensation and the “next tier” bar stay here. With a championship,
both move to the RIGHT. */}
            {!champ && feedbackPanels}
            {!champ && tierPanel}
          </div>

          {/* RIGHT (championship only): whatever needs attention up top, then the standings and
the tier bar underneath it (fills the gap). */}
          {champ ? (
            <div className="col gap16">
            {feedbackPanels}
            <Panel style={{ overflow: 'hidden', alignSelf: 'stretch' }}>
              <div className="set-head row between" style={{ alignItems: 'center' }}>
                <span style={{ color: 'var(--accent-text)', fontSize: 15 }}>{champ.seriesName}</span>
                {champ.pointsGained > 0 && <Chip kind="green" style={{ fontSize: 9 }}>+{champ.pointsGained} {t('results.pts')}</Chip>}
              </div>
              <div style={{ padding: '16px 18px' }}>
                <div className="row between" style={{ alignItems: 'center', marginBottom: 14 }}>
                  <Mono style={{ fontSize: 10, letterSpacing: '.1em', color: 'var(--text-3)', textTransform: 'uppercase' }}>{t('results.position')}</Mono>
                  <Display as="h1" style={{ fontSize: 30, fontStyle: 'italic', color: rankColor }}>{champ.playerRank}º</Display>
                </div>
                <div className="col" style={{ gap: 2 }}>
                  {champ.rows.map((d, i) => (
                    <div key={d.name} className="row between" style={{ padding: '6px 8px', borderRadius: 3, background: d.isPlayer ? 'color-mix(in oklab, var(--accent) 14%, transparent)' : 'transparent' }}>
                      <span className="row" style={{ gap: 10, alignItems: 'center', minWidth: 0 }}>
                        <Mono style={{ fontSize: 11, color: i === 0 ? 'var(--gold)' : 'var(--text-3)', width: 16, flex: 'none' }}>{i + 1}</Mono>
                        <span style={{ fontSize: 13, fontWeight: d.isPlayer ? 700 : 500, color: d.isPlayer ? 'var(--accent-text)' : 'var(--text)', whiteSpace: 'nowrap' }}>{d.isPlayer ? '▸ ' : ''}{d.name}</span>
                      </span>
                      <Mono style={{ fontSize: 12, color: 'var(--text-2)', flex: 'none' }}>{d.points}</Mono>
                    </div>
                  ))}
                </div>
              </div>
            </Panel>
            {tierPanel}
            </div>
          ) : null}
        </div>

        {!champ && <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('results.single')}</Mono>}
      </div>
    </div>
  );
}