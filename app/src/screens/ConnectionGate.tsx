// Connection gate: with AMS2 not attached, the app shows ONLY this — a big Play button (never connected) or
// Reconnect (was connected and dropped / the game closed), no tabs or menus. safe_mode (live WebSocket,
// unsupported build) does NOT reach this screen: it goes to the shell with actions disabled, as before.
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { UseGameSession } from '../game/useGameSession';
import { PHASE_LABEL_KEY } from '../game/gameSession';
import { getBridge } from '../shell/bridge';
import { toggleWindowMode, useWindowMode } from '../shell/windowMode';
import { Mono, Kicker, Btn } from '../ui/components';
import { BrandLogo } from '../ui/BrandLogo';
import { CommunityCta } from '../ui/CommunityCta';
import { SUPPORTED_AMS2 } from '../game/compat';
import { useUpdater } from '../updater/useUpdater';
import { simRaceEnabled, setSimRace, lowRamMode, setLowRamMode,
         telemetryChoice, setTelemetryChoice } from '../ui/prefs';
import { TelemetryConsent } from '../ui/TelemetryConsent';
import { LaunchModePicker } from '../ui/LaunchModePicker';   // the same selector as Settings
import { readProfileSlots, activateProfile, createProfile, deleteProfile,
         CAREER_DIFFICULTIES, type ProfileSlots, type CareerDifficulty } from '../game/profileSlots';
import { RestoreAiButton } from '../ui/RestoreAiButton';

// Race slots got bigger (150x72 -> 186x96). This is the primary action on this screen, and the empty
// slot also has to accommodate the 3 reward modes at creation time.
const SLOT_W = 186;
const SLOT_H = 96;


export function ConnectionGate({ session, everConnected, userdata }: { session: UseGameSession; everConnected: boolean; userdata?: string }) {
  const { t } = useTranslation();
  const winMode = useWindowMode();   // the window/fullscreen button reflects its current action
  const phase = session.state.phase;
  const launching = phase === 'launching' || phase === 'waiting_process' || phase === 'injecting';
  const reconnecting = phase === 'connecting';
  const isError = phase === 'error';
  const injectCode = session.state.injectCode;
  const bridgeFailure = isError && !!injectCode;
  const safeRecoveryCodes = ['bridge_unavailable', 'dll_missing', 'dll_invalid', 'dll_identity_mismatch'];
  const showSafeRecovery = bridgeFailure && safeRecoveryCodes.includes(injectCode);
  // watchdog phases — game closed (relaunch) / game started manually without attaching (link).
  const gameDown = phase === 'game_down';
  const unlinked = phase === 'detected_unlinked';
  // T371: why it is unlinked — the message distinguishes "the file is not there" from "it is there but did not load".
  const unlinkedKey = session.state.unlinkedReason === 'missing' ? 'conn.unlinkedMissing'
    : session.state.unlinkedReason === 'foreign' ? 'conn.unlinkedForeign'
    : 'conn.unlinked';
  // Disabled only while launching (or connecting COLD). On reconnect, if it was already
  // connected before, it's kept clickable so it can RELAUNCH the game if it closed (the play action relaunches
  // when the process isn't running). game_down/unlinked states are always clickable: those are exactly the states where the user's action is the only way forward.
  const disabled = launching || (reconnecting && !everConnected);
  // With the game already open, the overlay can't be loaded (the game only picks it up at process
  // startup), and the fallback path was removed from the product. The only way out is to restart AMS2,
  // which can cost a race to anyone currently playing, so this is a
  // TWO-step action: the button asks for confirmation before closing anything.
  const [askRestart, setAskRestart] = useState(false);
  const label = unlinked
    ? (askRestart ? t('session.restartConfirmBtn') : t('session.restartGame'))
    : everConnected ? t('session.reconnect') : t('session.play');
  const progress = (launching || reconnecting) ? t('session.' + (PHASE_LABEL_KEY[phase] ?? 'connecting')) : '';
  const compatBlocked = phase === 'compat_blocked';
  const upd = useUpdater();
  // Version footer: the Play screen shows the installed version and the result of the automatic
  // check ("you're up to date" / "X.Y.Z is available") — requested during end-to-end testing feedback.
  const [appVersion, setAppVersion] = useState('');
  // SIMULATION MODE (debug). LOCAL preference chosen here (before connecting: there's no
  // engine to talk to yet); the shell syncs it to the engine on connect (cmd.debug.simRace).
  const [simOn, setSimOn] = useState(simRaceEnabled());
  // Experimental Low RAM Mode: toggle next to the version footer. With it ON, the engine skips scanning the
  // entire heap when launching a race (fixes the freeze + RAM spike on DDR4 machines). The shell syncs it to the engine on connect.
  const [lowRam, setLowRam] = useState(lowRamMode());
  // 3 PROFILE slots on cold start. The engine's profile files are read/written directly
  // (there's no WebSocket connection yet); on reconnect (everConnected) they're hidden — the engine already loaded one
  // and switching it live is a Settings concern (cmd.profile.select).
  const [slots, setSlots] = useState<ProfileSlots | null>(null);
  const showSlots = !!userdata && !everConnected;
  const refreshSlots = () => {
    if (userdata) void readProfileSlots(userdata).then(setSlots).catch(() => setSlots(null));
  };
  useEffect(() => {
    void getBridge().then((b) => b.app.version()).then(setAppVersion);
    void upd.check();
    if (showSlots) refreshSlots();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  const pickSlot = (slug: string) => {
    if (!userdata) return;
    void activateProfile(userdata, slug).then(refreshSlots).catch(() => {});
  };
  // Auto-named saves ("Save N", first free N): creating one doesn't ask for a name — one click on the
  // empty slot creates it and selects it; PLAY always launches the active slot. With no profiles there's no standalone
  // PLAY: the big button is CREATE SAVE AND PLAY (creates "Save 1" and launches in the same gesture), so it never falls back to the engine's own 'default' profile.
  const [creating, setCreating] = useState(false);
  // Creating a save now goes through CHOOSING A MODE. Pressing "+" opens the selector (only then do the 3
  // of the creation act draw nothing). 0.7.0: the choice is no longer irrevocable, it is changed later in Settings.
  // pickMode = index of the slot whose selector is open; null = none.
  const [pickMode, setPickMode] = useState<number | null>(null);
  const autoName = (s: ProfileSlots): string => {
    for (let n = 1; ; n++) {
      const nm = t('play.autoName', { n });
      if (!s.profiles.some((p) => p.name === nm)) return nm;
    }
  };
  const createSlot = (difficulty: CareerDifficulty) => {
    if (!userdata || !slots || creating) return;
    setCreating(true);
    setPickMode(null);
    void createProfile(userdata, autoName(slots), difficulty)
      .then(refreshSlots)
      .catch(() => {})
      .finally(() => setCreating(false));
  };
  const activeName = slots?.profiles.find((p) => p.slug === slots.active)?.name ?? '';
  // PLAY now requires a chosen save. With no active profile there's nothing to launch, and falling back
  // to the engine's 'default' slug is exactly the bug that creates phantom saves. On RECONNECT (the engine already loaded a profile), the
  // slots aren't rendered and this gate doesn't apply.
  const noCareer = showSlots && !slots?.profiles.some((p) => p.slug === slots.active);
  // deleting a profile from the start screen (X + confirmation). delSlug = the slot with confirmation open.
  const [delSlug, setDelSlug] = useState<string | null>(null);
  // Telemetry consent. There's NO engine here yet (same situation as the simulation mode
  // and the Low RAM setting above), so the answer goes to local preferences and the shell syncs it to the engine
  // as soon as it connects. null = never asked before -> the dialog shows.
  const [telemetry, setTelemetryState] = useState<boolean | null>(telemetryChoice());
  const askTelemetry = telemetry === null;
  const answerTelemetry = (on: boolean) => { setTelemetryChoice(on); setTelemetryState(on); };
  const deleteSlot = (slug: string) => {
    if (!userdata) return;
    setDelSlug(null);
    void deleteProfile(userdata, slug).then(refreshSlots).catch(() => {});
  };
  const updLine = upd.status === 'checking' ? t('updates.checking')
    : upd.status === 'none' ? t('updates.none')
    : upd.status === 'available' ? t('updates.available', { v: upd.version ?? '' })
    : upd.status === 'downloading' ? (upd.progressPct === null ? t('updates.downloading') : t('updates.downloadingPct', { pct: upd.progressPct }))
    : upd.status === 'restarting' ? t('updates.restarting')
    : upd.status === 'error' ? t('updates.error', { msg: upd.error ?? '' })
    : '';

  return (
    <div style={{ height: '100%', position: 'relative', display: 'flex', flexDirection: 'column' }}>
      {/* Consent is asked ONCE, here, since this is where
          the Play button lives and the first thing you see when opening the app. It's answered before touching anything else.
          Either answer (yes or no) closes the question for good; you can change your mind in Settings. */}
      {askTelemetry && <TelemetryConsent onChoose={answerTelemetry} />}
      <div className="app-bg" />
      {/* window controls: no title bar, so the user never feels trapped */}
      <div className="row" style={{ position: 'absolute', top: 10, right: 12, gap: 2, zIndex: 2 }}>
        <button className="btn btn-sm btn-ghost" title={t('common.minimize')} onClick={() => void getBridge().then((b) => b.window.minimize())}>—</button>
        <button className="btn btn-sm btn-ghost" title={t(winMode === 'fullscreen' ? 'common.windowed' : 'common.fullscreen')} onClick={() => void toggleWindowMode()}>{winMode === 'fullscreen' ? '❐' : '⛶'}</button>
        <button className="btn btn-sm btn-ghost" title={t('common.close')} onClick={() => void getBridge().then((b) => b.window.close())}>✕</button>
      </div>

      <div className="col center grow" style={{ height: '100%', gap: 18, padding: 40, textAlign: 'center', position: 'relative', zIndex: 1 }}>
        <div className="col" style={{ gap: 10, alignItems: 'center' }}>
          <BrandLogo height={72} />
          <Kicker>{t('app.tagline')}</Kicker>
        </div>
        {bridgeFailure ? (
          <div className="panel col" style={{ gap: 9, padding: '16px 18px', maxWidth: 560, textAlign: 'left' }}>
            <Kicker style={{ color: 'var(--amber)' }}>{t('bridgeError.title')}</Kicker>
            <Mono style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.55 }}>
              {t(`bridgeError.codes.${injectCode}`)}
            </Mono>
            {showSafeRecovery && (
              <ol style={{ margin: 0, paddingLeft: 18, color: 'var(--text-2)', fontSize: 12, lineHeight: 1.55 }}>
                <li>{t('bridgeError.protectionHistory')}</li>
                <li>{t('bridgeError.verifyDownload')}</li>
                <li>{t('bridgeError.reportFalsePositive')}</li>
              </ol>
            )}
            <Mono style={{ fontSize: 10, color: 'var(--text-3)', overflowWrap: 'anywhere' }}>
              {session.state.message}
            </Mono>
          </div>
        ) : (
          <Mono style={{ fontSize: 13, color: 'var(--text-2)', maxWidth: 460 }}>
          {isError ? (session.state.message || t('session.launchTimeout'))
            : gameDown ? t('conn.gameDown')
            : unlinked ? t(unlinkedKey)
            : everConnected ? t('conn.dropped') : t('conn.disconnected')}
          </Mono>
        )}
        {compatBlocked ? (
          <div className="panel col" style={{ gap: 12, padding: '18px 20px', maxWidth: 540, textAlign: 'left' }}>
            <Kicker style={{ color: 'var(--amber)' }}>{t('compat.title')}</Kicker>
            <Mono style={{ fontSize: 12, color: 'var(--text-2)', lineHeight: 1.6 }}>
              {t('compat.body', {
                detected: session.state.detected ?? t('compat.unknown'),
                supported: SUPPORTED_AMS2.join(', '),
              })}
            </Mono>
            {updLine && (
              <Mono style={{ fontSize: 11, color: upd.status === 'error' ? 'var(--danger)' : 'var(--text-3)' }}>
                {updLine}
              </Mono>
            )}
            <div className="row" style={{ gap: 8 }}>
              {upd.status === 'available'
                ? <Btn variant="primary" size="sm" onClick={() => void upd.install()}>{t('updates.installNow')}</Btn>
                : <Btn variant="primary" size="sm" disabled={upd.status === 'checking' || upd.status === 'downloading'} onClick={() => void upd.check()}>{t('compat.checkUpdate')}</Btn>}
              <Btn variant="ghost" size="sm" onClick={() => session.play({ force: true })}>{t('compat.injectAnyway')}</Btn>
            </div>
          </div>
        ) : (
          <>
            {/* 3 profile slots — the active one is highlighted; an empty slot creates an auto-named save */}
            {showSlots && slots && (
              <div className="col" style={{ gap: 8, alignItems: 'center' }}>
                <Kicker style={{ fontSize: 10 }}>{t('play.chooseProfile')}</Kicker>
                <div className="row" style={{ gap: 12, alignItems: 'stretch' }}>
                  {[0, 1, 2].map((i) => {
                    const p = slots.profiles[i];
                    const active = p && p.slug === slots.active;
                    // Empty slot -> "+ NEW CAREER" (bigger, since it's the primary action on this
                    // screen). Pressing it TURNS the slot into the 3 modes: picking one creates the career.
                    if (!p) return (
                      <div key={i} style={{ width: SLOT_W, height: SLOT_H }}>
                        {pickMode === i ? (
                          <div className="col" style={{ width: '100%', height: '100%', gap: 5, padding: '8px 8px 7px', borderRadius: 4,
                                                        border: '1.5px solid var(--accent)', background: 'var(--surface)' }}>
                            <Mono style={{ fontSize: 8.5, letterSpacing: '.12em', color: 'var(--accent-text)' }}>{t('play.pickPace')}</Mono>
                            <div className="row" style={{ gap: 5, flex: 1 }}>
                              {CAREER_DIFFICULTIES.map((d) => (
                                <button key={d} className="col center" disabled={creating} onClick={() => createSlot(d)}
                                  title={`${t(`play.pace.${d}`)} — ${t(`play.paceEffect.${d}`)}`}
                                  style={{ flex: 1, gap: 3, padding: '4px 2px', cursor: 'pointer', borderRadius: 3,
                                           border: `1px solid ${d === 'normal' ? 'var(--accent)' : 'var(--line-2)'}`,
                                           background: d === 'normal' ? 'color-mix(in oklab, var(--accent) 12%, var(--surface-2))' : 'var(--surface-2)' }}>
                                  <Mono style={{ fontSize: 9, fontWeight: 700, letterSpacing: '.06em', color: 'var(--text)' }}>{t(`play.pace.${d}`).toUpperCase()}</Mono>
                                  <Mono style={{ fontSize: 8, color: d === 'normal' ? 'var(--text-3)' : 'var(--accent-text)' }}>{t(`play.paceShort.${d}`)}</Mono>
                                </button>
                              ))}
                            </div>
                            <button onClick={() => setPickMode(null)} style={{ background: 'none', border: 'none', padding: 0, cursor: 'pointer' }}>
                              <Mono style={{ fontSize: 8, color: 'var(--text-3)' }}>{t('common.cancel')}</Mono>
                            </button>
                          </div>
                        ) : (
                          <button className="col center" disabled={disabled || creating} onClick={() => setPickMode(i)}
                            style={{ width: '100%', height: '100%', gap: 6, cursor: 'pointer', background: 'transparent',
                                     border: '1.5px dashed var(--line-2)', borderRadius: 4, color: 'var(--text-3)' }}>
                            <span style={{ fontSize: 26, lineHeight: 1 }}>+</span>
                            <Mono style={{ fontSize: 10.5, letterSpacing: '.1em' }}>{t('play.newCareer').toUpperCase()}</Mono>
                          </button>
                        )}
                      </div>
                    );
                    return (
                      <div key={i} style={{ position: 'relative', width: SLOT_W, height: SLOT_H }}>
                        <button className="col" disabled={disabled || creating} onClick={() => pickSlot(p.slug)}
                        style={{ width: '100%', height: '100%', gap: 4, padding: '12px 14px', cursor: 'pointer', alignItems: 'flex-start',
                                 textAlign: 'left', borderRadius: 4, justifyContent: 'center',
                                 border: `1px solid ${active ? 'var(--accent)' : 'var(--line)'}`,
                                 background: active ? 'color-mix(in oklab, var(--accent) 10%, var(--surface))' : 'var(--surface)' }}>
                        <span style={{ fontSize: 15, fontWeight: 700, color: 'var(--text)', maxWidth: '100%', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{p.name}</span>
                        <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: active ? 'var(--accent-text)' : 'var(--text-3)' }}>
                          {active ? t('play.active').toUpperCase() : p.mode === 'sandbox' ? t('play.sandbox').toUpperCase() : ' '}
                        </Mono>
                        {/* 21/07: the race mode, visible on the card (read from profile.json, which Settings
                            rewrites when changed since 0.7.0). Normal is not drawn: it is the default, it would be noise. */}
                        {p.difficulty !== 'normal' && (
                          <Mono style={{ fontSize: 8.5, letterSpacing: '.1em', color: 'var(--text-3)', border: '1px solid var(--line-2)', borderRadius: 3, padding: '1px 5px' }}>
                            {t(`play.pace.${p.difficulty}`).toUpperCase()}
                          </Mono>
                        )}
                        </button>
                        {/* X to delete (corner); stopPropagation so clicking it doesn't also select the slot. */}
                        {!disabled && delSlug !== p.slug && (
                          <button title={t('profiles.delete')} aria-label={t('profiles.delete')} onClick={(e) => { e.stopPropagation(); setDelSlug(p.slug); }}
                            style={{ position: 'absolute', top: 3, right: 3, width: 18, height: 18, lineHeight: '15px', textAlign: 'center', padding: 0, borderRadius: 3, cursor: 'pointer', fontSize: 11, color: 'var(--text-3)', background: 'var(--surface-3)', border: '1px solid var(--line-2)' }}>✕</button>
                        )}
                        {/* in-place confirmation over the slot */}
                        {delSlug === p.slug && (
                          <div className="col center" style={{ position: 'absolute', inset: 0, gap: 6, borderRadius: 4, zIndex: 2, background: 'color-mix(in oklab, var(--danger) 14%, var(--surface))', border: '1px solid color-mix(in oklab, var(--danger) 55%, transparent)' }}>
                            <Mono style={{ fontSize: 9, color: 'var(--danger)', textAlign: 'center', padding: '0 6px' }}>{t('play.deleteConfirm', { defaultValue: 'Delete this profile?' })}</Mono>
                            <div className="row" style={{ gap: 6 }}>
                              <button onClick={() => deleteSlot(p.slug)} style={{ fontSize: 10, fontWeight: 700, cursor: 'pointer', padding: '3px 9px', borderRadius: 3, color: 'var(--accent-ink)', background: 'var(--danger)', border: 'none' }}>{t('profiles.delete')}</button>
                              <button onClick={() => setDelSlug(null)} style={{ fontSize: 10, cursor: 'pointer', padding: '3px 9px', borderRadius: 3, color: 'var(--text-2)', background: 'var(--surface-3)', border: '1px solid var(--line-2)' }}>{t('common.cancel')}</button>
                            </div>
                          </div>
                        )}
                      </div>
                    );
                  })}
                </div>
                {/* Player confusion was exactly this — mistaking it for AI difficulty.
                    It's spelled out clearly, and only while you're choosing: this mode scales money/REP/XP, AI difficulty is separate. */}
                {pickMode !== null && (
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)', maxWidth: 3 * SLOT_W, lineHeight: 1.5 }}>
                    {t('play.paceNoteChangeable')}
                  </Mono>
                )}
              </div>
            )}
            {/* PLAY launches the ACTIVE slot and is DISABLED until a career has been chosen
                (it used to blindly create one in the same gesture). Below it, the name of the one about to launch or the
                reason it can't. */}
            <Btn variant="primary" size="lg" style={{ minWidth: 240 }} disabled={disabled || creating || noCareer}
                 onClick={() => {
                   // First click while the game is open: just warns. The second click actually closes AMS2.
                   if (unlinked && !askRestart) { setAskRestart(true); return; }
                   setAskRestart(false);
                   session.play();
                 }}>
              {creating ? '…' : disabled ? `${progress}…` : label}
            </Btn>
            {unlinked && askRestart && (
              <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', maxWidth: 360, lineHeight: 1.5 }}>
                {t('session.restartWhy')}
              </Mono>
            )}
            {/* Launch mode + headset options: the SAME component as Settings. */}
            <LaunchModePicker />
            {showSlots && !noCareer && !!activeName && !disabled && (
              <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('play.activeCareer', { name: activeName })}</Mono>
            )}
            {noCareer && !disabled && !creating && (
              <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('play.needCareer')}</Mono>
            )}
            {reconnecting && everConnected && <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{progress}…</Mono>}
            {!launching && !reconnecting && !isError && <Mono style={{ fontSize: 11, color: 'var(--text-3)', maxWidth: 440 }}>{t('conn.hint')}</Mono>}
            {/* COMMUNITY: Patreon (+ link your account if you're already a supporter) and Discord. This is where the player
                is standing before entering, and where the account matters (premium events require it). */}
            <CommunityCta style={{ marginTop: 6 }} />
          </>
        )}
      </div>

      {/* version + updater on the left, and to its RIGHT the Low RAM toggle (experimental), same row,
          discreet (bottom-left). Experimental Low RAM Mode: opt-in for machines that freeze when starting
          a race; amber when active. Synced to the engine on (re)connect. */}
      <div className="row" style={{ position: 'absolute', bottom: 10, left: 12, gap: 10, zIndex: 2, alignItems: 'center' }}>
        {(appVersion || updLine) && (
          <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>
            {appVersion ? `v${appVersion}` : ''}
            {appVersion && updLine ? ' · ' : ''}
            {updLine}
          </Mono>
        )}
        {/* no padding override: .btn-sm already ships the asymmetric padding (8/20/8/16) that compensates for the button's
            diagonal clip-path; trimming it left the text hanging outside the top-left corner. */}
        <button className="btn btn-sm btn-ghost" role="switch" aria-checked={lowRam} title={t('lowram.hint')}
                style={{ fontSize: 10, color: lowRam ? 'var(--amber)' : 'var(--text-3)' }}
                onClick={() => { const v = !lowRam; setLowRamMode(v); setLowRam(v); }}>
          {t('lowram.label')}: {lowRam ? t('lowram.on') : t('lowram.off')}
        </button>
        {/* Restore the user's custom AI drivers. Here, not in Settings: this is the screen where the
            game is closed, the only moment this operation makes sense. */}
        <RestoreAiButton disabled={disabled} />
      </div>

      {/* SIMULATION MODE (debug), discreet bottom-right (mirrors the version footer). Amber when
          active, so it's never forgotten that results will be fabricated. */}
      <button className="btn btn-sm btn-ghost" aria-pressed={simOn} title={t('simrace.hint')}
              style={{ position: 'absolute', bottom: 6, right: 12, zIndex: 2, fontSize: 10,
                       color: simOn ? 'var(--amber)' : 'var(--text-3)' }}
              onClick={() => { const v = !simOn; setSimRace(v); setSimOn(v); }}>
        {simOn ? t('simrace.on') : t('simrace.off')}
      </button>
    </div>
  );
}
