// The two post-race surfaces on the Results screen.
// They're deliberately separate and don't mean the same thing:
//
//  · <FeelCard/> is SUBJECTIVE: a five-level question, optional and skippable, with optional reasons
//    underneath. Just marking the scale is enough; the reasons only refine the why. Its local effect is
//    deliberately weak (the engine only nudges the class difficulty by one point if you repeat an extreme rating).
//  · <GapOfferCard/> is OBJECTIVE: it only appears when the measurement loop detected an anomalous advantage and, out of caution,
//    decided not to trust it. It offers exactly the difficulty it would have set if it HAD trusted it. Declining costs
//    nothing: it keeps the damped path that was already applied.
//
// There's no real submit button: the answer is registered in the engine the moment it's marked, and telemetry fires
// on its own when leaving the screen (with consent). The "Send" button exists as a visible confirmation
// that it's already done, not as the gesture that does it.
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Panel, Kicker, Mono, Btn, Chip } from '../../ui/components';
import { FEEL_LEVELS, FEEL_REASONS } from '../../telemetry/raceFeedback';

// scale color: the extremes are the ones that "mean" something (they move the class difficulty).
const feelColor = (n: number): string =>
  n === 1 ? 'var(--cyan)' : n === 5 ? 'var(--danger)' : n === 3 ? 'var(--green)' : 'var(--text-2)';

export function FeelCard({ feel, telemetryOptIn, onAnswer }: {
  feel: number;                                             // 1..5 already answered (comes from the engine); 0 = not answered
  telemetryOptIn: boolean;
  onAnswer: (feel: number, reasons: string[]) => void;
}) {
  const { t } = useTranslation();
  const [picked, setPicked] = useState(feel > 0 ? feel : 0);
  const [reasons, setReasons] = useState<string[]>([]);
  const [ack, setAck] = useState(false);

  // marking the scale IS already the answer: it's sent to the engine on the spot (local effect + payload). Changing
  // your mind or marking reasons sends it again with whatever's there, so the last click is the one that counts.
  const pick = (n: number) => { setPicked(n); onAnswer(n, reasons); };
  const toggleReason = (r: string) => {
    const next = reasons.includes(r) ? reasons.filter((x) => x !== r) : [...reasons, r];
    setReasons(next);
    if (picked > 0) onAnswer(picked, next);
  };

  return (
    <Panel style={{ padding: '16px 20px' }} data-tour="results.feel">
      <Kicker style={{ display: 'block', marginBottom: 4 }}>{t('results.feel.title')}</Kicker>
      <Mono style={{ fontSize: 11.5, color: 'var(--text-3)', lineHeight: 1.5, display: 'block', marginBottom: 12 }}>
        {t('results.feel.question')}
      </Mono>

      <div className="row" role="group" aria-label={t('results.feel.question')} style={{ gap: 6, alignItems: 'stretch' }}>
        {FEEL_LEVELS.map((n) => {
          const on = picked === n;
          return (
            <button
              key={n}
              className="btn btn-sm"
              aria-pressed={on}
              onClick={() => pick(n)}
              style={{
                flex: 1, minWidth: 0, padding: '9px 6px', lineHeight: 1.25,
                background: on ? `color-mix(in oklab, ${feelColor(n)} 22%, transparent)` : 'var(--bg-2)',
                borderColor: on ? feelColor(n) : 'var(--line)',
                color: on ? feelColor(n) : 'var(--text-2)',
              }}
            >
              {/* the block fills the WHOLE button and centers its text: all five are stretched buttons
                  (flex:1), and with the slanted wedge shape, left-aligned text looks obviously off. The label
                  can now wrap onto two lines instead of overflowing the wedge (nowrap used to clip it). */}
              <span className="col" style={{ gap: 2, alignItems: 'center', width: '100%', minWidth: 0 }}>
                <span style={{ fontSize: 13, fontWeight: 700 }}>{n}</span>
                <span style={{ fontSize: 9.5, letterSpacing: '.02em', whiteSpace: 'normal', textAlign: 'center' }}>{t(`results.feel.level.${n}`)}</span>
              </span>
            </button>
          );
        })}
      </div>

      {/* Reasons: only appear AFTER marking the scale, and are optional. They cover both
          directions (a tightly packed grid explains both a "too easy" and a "too hard" rating). */}
      {picked > 0 && (
        <div className="col" style={{ gap: 8, marginTop: 14, paddingTop: 12, borderTop: '1px solid var(--line)' }}>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('results.feel.reasonsTitle')}</Mono>
          <div className="row" style={{ flexWrap: 'wrap', gap: 6 }}>
            {FEEL_REASONS.map((r) => {
              const on = reasons.includes(r);
              return (
                <button key={r} className="btn btn-sm" aria-pressed={on} onClick={() => toggleReason(r)}
                        style={{
                          padding: '5px 10px', fontSize: 11,
                          background: on ? 'color-mix(in oklab, var(--accent) 20%, transparent)' : 'var(--bg-2)',
                          borderColor: on ? 'var(--accent)' : 'var(--line)',
                          color: on ? 'var(--accent-text)' : 'var(--text-2)',
                        }}>
                  {t(`results.feel.reason.${r}`)}
                </button>
              );
            })}
          </div>
          <div className="row between" style={{ alignItems: 'center', gap: 12, marginTop: 2 }}>
            <Mono style={{ fontSize: 10.5, color: telemetryOptIn ? 'var(--text-3)' : 'var(--amber)', lineHeight: 1.5, flex: 1 }}>
              {telemetryOptIn ? t('results.feel.willSend') : t('results.feel.localOnly')}
            </Mono>
            {ack
              ? <Chip kind="green">{t('results.feel.saved')}</Chip>
              : <Btn variant="ghost" size="sm" onClick={() => setAck(true)}>{t('results.feel.send')}</Btn>}
          </div>
        </div>
      )}
    </Panel>
  );
}

export function GapOfferCard({ gapPct, current, offered, held, onDecide }: {
  gapPct: number;              // fraction of lap time (+ = the player is lapping faster than the reference)
  current: number;
  offered: number;
  // The settlement HELD BACK the sample and applied nothing, so not answering leaves the race with nothing to
  // show. It's only stated in that case: with `held` false, the step WAS applied, and saying so would be a lie.
  held?: boolean;
  onDecide: (accept: boolean) => void;
}) {
  const { t } = useTranslation();
  const [done, setDone] = useState<null | boolean>(null);
  const ahead = gapPct > 0;
  const pct = Math.abs(gapPct * 100).toFixed(1);
  const decide = (accept: boolean) => { setDone(accept); onDecide(accept); };

  if (done !== null) {
    return (
      <Panel style={{ padding: '14px 20px' }}>
        <Mono style={{ fontSize: 12, color: 'var(--text-2)' }}>
          {done ? t('results.gapOffer.applied', { value: offered }) : t('results.gapOffer.kept', { value: current })}
        </Mono>
      </Panel>
    );
  }

  return (
    <Panel style={{ padding: '16px 20px', borderColor: 'color-mix(in oklab, var(--amber) 45%, var(--line))' }} data-tour="results.gapOffer">
      <Kicker style={{ display: 'block', marginBottom: 6, color: 'var(--amber)' }}>{t('results.gapOffer.title')}</Kicker>
      <Mono style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.6, display: 'block' }}>
        {ahead ? t('results.gapOffer.ahead', { pct }) : t('results.gapOffer.behind', { pct })}
      </Mono>
      {held && (
        <Mono style={{ fontSize: 11.5, color: 'var(--amber)', lineHeight: 1.5, display: 'block', marginTop: 6 }}
              data-gapoffer-held="1">
          {t('results.gapOffer.heldNote')}
        </Mono>
      )}
      <div className="row gap8" style={{ alignItems: 'baseline', marginTop: 10, marginBottom: 12 }}>
        <Mono style={{ fontSize: 12, color: 'var(--text-3)' }}>{current}</Mono>
        <span style={{ color: 'var(--text-3)', fontSize: 11 }}>→</span>
        <span className="display" style={{ fontSize: 24, fontStyle: 'italic', color: 'var(--amber)' }}>{offered}</span>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('results.gapOffer.aiLevel')}</Mono>
      </div>
      <div className="row gap8">
        <Btn variant="primary" size="sm" onClick={() => decide(true)}>{t('results.gapOffer.accept', { value: offered })}</Btn>
        <Btn variant="ghost" size="sm" onClick={() => decide(false)}>{t('results.gapOffer.oneOff')}</Btn>
      </div>
    </Panel>
  );
}
