// Tryout: trial-as-race, ported from screen_tryout.cpp (source of truth). Armed from the Market
// (an offer with requiresTryout -> cmd.tryout.arm). 3 mutually exclusive phases: BRIEF (the challenge + what's at stake +
// "Start tryout" -> cmd.tryout.start fires the actual race), RUNNING (polling query.tryout while you race
// in the game), RESULT (a BINARY verdict, pass=pos<=topN: PASS -> Negotiate the offer; soft FAIL -> go back). The verdict
// comes from the REAL settled result (no random roll). Chrome via the engine's ty_* keys; target/standard/stakes = {en,es} pairs.
import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, TryoutPayload, GameStatePayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { autoSwitchEnabled, launchMode, vrOverlayEnabled } from '../ui/prefs';
import { getBridge } from '../shell/bridge';
import { surfaceApp } from '../shell/windowMode';
import { shouldTakeoverDesktop } from '../game/useGameSession';
import { Display, Mono, Kicker, Btn, Chip } from '../ui/components';
import { classDisplayName } from '../ui/classNames';
import { useScreenCrumb } from '../support/useScreenCrumb';

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

export function Tryout({ client, offerId, onBack, onNegotiate }: { client: ApiClientLike; offerId: string; onBack: () => void; onNegotiate: (offerId: string) => void }) {
  const { t, i18n } = useTranslation();
  const es = i18n.language === 'es';
  useScreenCrumb(client, 'tryout', offerId);   // audit note, 07/20
  const q = useQuery<TryoutPayload>(client, 'query.tryout');
  const p = q.data;
  const [started, setStarted] = useState(false);
  const [busy, setBusy] = useState(false);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  // RUNNING: poll query.tryout until there's a result (a real race, can take minutes)
  useEffect(() => {
    if (started && p?.armed && !p?.hasResult) {
      pollRef.current = setInterval(() => q.refetch(), 2500);
      return () => { if (pollRef.current) clearInterval(pollRef.current); };
    }
  }, [started, p?.armed, p?.hasResult]);

  // a tryout behaves like a normal RACE -- when it starts, the app steps aside (game in front, ready for
  // driving); once the verdict is in, the app comes back to front when the game returns to its menu. Respects the AUTO/MANUAL focus setting.
  useEffect(() => {
    if (!started || !shouldTakeoverDesktop(launchMode(), autoSwitchEnabled(), vrOverlayEnabled())) return;
    if (!p?.hasResult) { void getBridge().then((b) => b.window.minimize().catch(() => {})); return; }
    let cancel = false;
    void (async () => {
      const b = await getBridge();
      for (let i = 0; i < 150 && !cancel; i++) {
        try { const gs = await client.query<GameStatePayload>('query.game_state'); if (gs.atMenu) break; } catch { /* retries */ }
        await new Promise((r) => setTimeout(r, 2000));
      }
      if (!cancel) await surfaceApp(b).catch(() => {});
    })();
    return () => { cancel = true; };
  }, [started, p?.hasResult, client]);

  const clear = (then?: () => void) => { void client.cmd('cmd.tryout.clear', {}).then(() => then?.()).catch(() => then?.()); };
  const start = () => { if (busy) return; setBusy(true); void client.cmd('cmd.tryout.start', {}).then(() => { setStarted(true); }).catch(() => {}).finally(() => setBusy(false)); };

  if (q.loading && !p) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || !p || !p.armed) { return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>; }

  const header = (
    <div className="row between" style={{ padding: '14px 26px', alignItems: 'center', borderBottom: '1px solid var(--line)' }}>
      <div className="row gap12" style={{ alignItems: 'center' }}>
        <button className="btn btn-sm btn-ghost" onClick={() => clear(onBack)}>← {t('ty_market')}</button>
        <div className="col"><Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('ty_for')}</Mono><Display style={{ fontSize: 22 }}>{p.team}</Display></div>
      </div>
      <Chip kind="accent" style={{ fontSize: 9 }}>{t('ty_title')}</Chip>
    </div>
  );

  // RESULT
  if (p.hasResult) {
    return (
      <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
        {header}
        <div className="col center grow" style={{ gap: 16, padding: 30, textAlign: 'center' }}>
          <Display style={{ fontSize: 40, color: p.pass ? 'var(--green)' : 'var(--danger)' }}>{p.pass ? t('ty_passed') : t('ty_failed')}</Display>
          <Mono style={{ fontSize: 14, color: 'var(--text-2)' }}>{t('ty_result')}: P{p.pos} / {p.field} · Top {p.resultTopN}</Mono>
          {p.pass ? (
            <>
              <Mono style={{ fontSize: 12, color: 'var(--green)' }}>{t('ty_offeropens')}</Mono>
              <div className="row gap8">
                <Btn variant="primary" size="lg" onClick={() => clear(() => onNegotiate(offerId))}>{t('off_negotiate')} →</Btn>
                <Btn variant="ghost" size="lg" onClick={() => clear(onBack)}>{t('ty_back')}</Btn>
              </div>
            </>
          ) : (
            <>
              <Mono style={{ fontSize: 12, color: 'var(--text-3)', maxWidth: 380 }}>{t('ty_fail_soft')}</Mono>
              <Btn variant="ghost" size="lg" onClick={() => clear(onBack)}>{t('ty_back')}</Btn>
            </>
          )}
        </div>
      </div>
    );
  }

  // RUNNING
  if (started) {
    return (
      <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
        {header}
        <div className="col center grow" style={{ gap: 14, padding: 30, textAlign: 'center' }}>
          <Display style={{ fontSize: 28, color: 'var(--accent)' }}>{t('ty_evaluating')}</Display>
          <Mono style={{ fontSize: 13, color: 'var(--text-2)', maxWidth: 420 }}>{t('race.launchedBody')}</Mono>
        </div>
      </div>
    );
  }

  // BRIEF
  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      {header}
      <div className="col center grow" style={{ gap: 0, padding: 24 }}>
        <div className="row" style={{ width: 'min(760px, 100%)', gap: 0, border: '1px solid var(--line)', borderRadius: 12, background: 'var(--surface)', overflow: 'hidden' }}>
          {/* The challenge */}
          <div className="col" style={{ flex: 1, padding: '20px 22px', gap: 12, borderRight: '1px solid var(--line)' }}>
            <Kicker>{t('ty_challenge')}</Kicker>
            <div className="col" style={{ gap: 2 }}>
              <Display style={{ fontSize: 20 }}>{p.trackName}</Display>
              <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{classDisplayName(p.className)}</Mono>
            </div>
            <div style={{ padding: '10px 14px', borderRadius: 0, background: 'var(--surface-2)' }}>
              <Display style={{ fontSize: 18, color: 'var(--accent-text)' }}>{es ? p.targetEs : p.targetEn}</Display>
            </div>
            <Mono style={{ fontSize: 12, color: 'var(--amber)' }}>⚠ {es ? p.standardEs : p.standardEn}</Mono>
          </div>
          {/* What's at stake */}
          <div className="col" style={{ flex: 1, padding: '20px 22px', gap: 10 }}>
            <Kicker>{t('ty_stake')}</Kicker>
            {p.stakes.map((s, i) => (
              <div key={i} className="row gap8" style={{ alignItems: 'center' }}>
                <span style={{ width: 6, height: 6, borderRadius: '50%', background: s.color, flex: 'none' }} />
                <Mono style={{ fontSize: 12, color: 'var(--text-2)' }}>{es ? s.es : s.en}</Mono>
              </div>
            ))}
          </div>
        </div>
        <Btn variant="primary" size="lg" style={{ marginTop: 24 }} disabled={busy} onClick={start}>{t('ty_start')} →</Btn>
      </div>
    </div>
  );
}
