// Settings: DIFFICULTY section, ported from screen_settings.cpp (source of truth). 5 cards
// per level (gauge + name + description) + the Adaptive trim row. 100% READ-ONLY (query.settings):
// selecting a card and ±trim are actions DEFERRED to a later phase (cmd.settings.*) -- the controls are shown here
// but don't mutate anything. The prototype's assists/AI/sim/interface sections don't exist in the engine: they aren't ported.
// Labels via t() (engine keys: settings_tab/diff_*). Each level's color comes in the payload.
import { useState, useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import type { ApiClientLike, SettingsPayload, SettingsDifficulty, SettingsDiffCard, SettingsInherit, ProfilesPayload } from '../api/contracts';
import { useQuery } from '../api/useQuery';
import { autoSwitchEnabled, setAutoSwitch,
  launchNoticeEnabled, setLaunchNoticeEnabled,   // the "hands off" notice while the race is being set up (dismissible from the notice itself)
  uiScale, setUiScale, UI_SCALES, UISCALE_EVENT,   // UI scale
  simRaceEnabled, setSimRace,   // simulation mode, now also in Settings (previously only on the connection screen)
  setTelemetryChoice } from '../ui/prefs';   // telemetry-sharing consent (local mirror of what the engine has saved)
import { LaunchModePicker } from '../ui/LaunchModePicker';   // the SAME selector as the Play window
import { Display, Mono, Kicker, Btn } from '../ui/components';
import { GamepadSection } from './settings/GamepadSection';   // its own section, with live diagnostics and a per-device profile
import { getBridge, managedDataRoot, BRAND_DIR, type Pre070BackupStatus } from '../shell/bridge';
import { supportLog } from '../support/supportLog';
import { loadConfig } from '../config/appConfig';
import { UpdatesSection } from './settings/UpdatesSection';
import { DifficultyLocksSheet } from './settings/DifficultyLocksSheet';

// VR settings (in-headset panel and confirmation) live in the PLAY window, next to the
// launch-mode selector, which is where the decision to enter VR is made. Settings draws the SAME component
// (ui/LaunchModePicker.tsx) instead of its old 4-chip copy, so both places say and do the same.
import { AccountSection } from './settings/AccountSection';
import { ContentSection } from './settings/ContentSection';
import { PerformanceSection } from './settings/PerformanceSection';   // cars-on-track cap
import { RaceSetupView } from './settings/RaceSetupView';   // subscreen: "Race configuration"
import { RegenerateAssetsDialog, type RegenStatus } from '../ui/RegenerateAssetsDialog';
import type { ProvisionProgress } from '../shell/bridge';

// 21/07: prog.reward_mode (0/1/2) -> pace token. CAREER_DIFFICULTIES goes in the same order as
// the engine's bal::REWARD_MULT, and is the vocabulary of profile.json and cmd.settings.setReward.
import { CAREER_DIFFICULTIES, type CareerDifficulty } from '../game/profileSlots';

const fmtSigned = (n: number): string => (n >= 0 ? `+${n}` : `${n}`);

function Center({ children }: { children: React.ReactNode }) {
  return <div className="col center grow" style={{ height: '100%' }}>{children}</div>;
}

function Meter({ meter, color, dim }: { meter: number; color: string; dim: boolean }) {
  const H = [16, 19, 22, 25, 28];
  return (
    <div className="row" style={{ alignItems: 'flex-end', gap: 3, height: 28, opacity: dim ? 0.55 : 1 }}>
      {H.map((h, i) => (
        <span key={i} style={{ width: 5, height: h, borderRadius: 2, background: i < meter ? color : 'var(--surface-3)' }} />
      ))}
    </div>
  );
}

function Card({ card, selected, onSelect, trim, ai, hover, onHoverIn, onHoverOut }: {
  card: SettingsDiffCard; selected: boolean; onSelect: () => void;
  trim?: React.ReactNode; ai?: { value: number; max: number };
  hover?: React.ReactNode; onHoverIn?: () => void; onHoverOut?: () => void;
}) {
  const { t } = useTranslation();
  return (
    <div role="button" tabIndex={0} onClick={selected ? undefined : onSelect}
      onKeyDown={(e) => !selected && e.key === 'Enter' && onSelect()}
      className="focusable" style={{
      display: 'flex', flexDirection: 'column', gap: 10, padding: '16px 16px 18px', borderRadius: 0,
      cursor: selected ? 'default' : 'pointer',
      background: selected ? `color-mix(in oklab, ${card.color} 12%, var(--surface))` : 'var(--surface)',
      border: selected ? `2px solid ${card.color}` : '1px solid var(--line)',
      boxShadow: selected ? `0 0 0 1px ${card.color}, 0 6px 24px -12px ${card.color}` : 'none',
    }}>
      <div className="row between" style={{ alignItems: 'flex-start' }}>
        <Meter meter={card.meter} color={card.color} dim={card.isAuto} />
        {selected && <span style={{ width: 9, height: 9, borderRadius: '50%', background: card.color }} />}
      </div>
      <div className="row gap8" style={{ alignItems: 'center' }}>
        <Display style={{ fontSize: 21, color: selected ? 'var(--text)' : 'var(--text-2)' }}>{t(card.nameKey)}</Display>
        {card.isAuto && <span style={{ fontSize: 9, fontWeight: 700, letterSpacing: '.08em', color: card.color, background: `color-mix(in oklab, ${card.color} 18%, transparent)`, borderRadius: 3, padding: '2px 5px' }}>AUTO</span>}
      </div>
      {/* current AI difficulty, summarized as a big number. Per-class breakdown aside, the number is still
          base+trim, i.e. YOUR LEVEL; the per-category breakdown lives in the hover. */}
      {ai != null && (
        <div className="row" style={{ alignItems: 'baseline', gap: 6, marginTop: 2, position: 'relative' }}
             onMouseEnter={onHoverIn} onMouseLeave={onHoverOut}>
          <span className="display" data-testid="dda-ai-number"
                style={{ fontSize: 46, lineHeight: 1, color: card.color }}>{ai.value}</span>
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>/ {ai.max}</Mono>
          {hover}
        </div>
      )}
      <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t(card.descKey)}</Mono>
      {trim /* manual adjustment of the Adaptive setting: inside its own card once selected */}
    </div>
  );
}

// App preferences (shell, not the engine): the app<->game focus switch is persisted in localStorage (prefs.ts).
// Users asked for the toggle to live IN SETTINGS: with AUTO the app steps aside when a race launches and comes back when it ends;
// with MANUAL the user manages focus themselves with the top bar's window controls.
function PrefsSection({ onReplayTutorial, client }: { onReplayTutorial?: () => void; client?: ApiClientLike }) {
  const { t } = useTranslation();
  const [autoOn, setAutoOn] = useState(autoSwitchEnabled());
  const toggle = () => { const v = !autoOn; setAutoSwitch(v); setAutoOn(v); };
  // simulation mode, now ALSO here -- previously it only lived on the connection screen, and once the
  // app was connected there was no way to reach it. The preference is the SAME one (prefs.ts); the command is pushed
  // on the fly because AppShell only syncs it on (re)connect -- without this, the change wouldn't apply until reopening.
  const [simOn, setSimOn] = useState(simRaceEnabled());
  const toggleSim = (v: boolean) => {
    if (v === simOn) return;
    setSimRace(v); setSimOn(v);
    void client?.cmd('cmd.debug.simRace', { enabled: v }).catch(() => {});
  };
  // The pre-race notice is dismissed from its own checkbox ("don't show again"). This is the only place
  // where it can be turned back on, which is what makes it a checkbox and not a one-way door.
  const [noticeOn, setNoticeOn] = useState(launchNoticeEnabled());
  const toggleNotice = (v: boolean) => { if (v !== noticeOn) { setLaunchNoticeEnabled(v); setNoticeOn(v); } };
  // UI scale (requested by several users running the app on a distant monitor). setUiScale already applies it.
  const [scale, setScale] = useState(uiScale());
  const pickScale = (pct: number) => { if (pct !== scale) { setUiScale(pct); setScale(pct); } };
  // The Ctrl +/-/0 shortcuts change the scale without going through these buttons: this syncs the highlighted step.
  useEffect(() => {
    const h = () => setScale(uiScale());
    window.addEventListener(UISCALE_EVENT, h);
    return () => window.removeEventListener(UISCALE_EVENT, h);
  }, []);
  return (
    <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
      <div className="col" style={{ gap: 2 }}>
        <Display style={{ fontSize: 19 }}>{t('prefs.title')}</Display>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('prefs.sub')}</Mono>
      </div>
      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <span className="display" style={{ fontSize: 15 }}>{t('prefs.autoSwitch')}</span>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('prefs.autoSwitchDesc')}</Mono>
        </div>
        {/* AUTO/MANUAL segmented switch */}
        <div className="row" role="group" aria-label={t('prefs.autoSwitch')} style={{ flexShrink: 0 }}>
          <Btn variant={autoOn ? 'primary' : 'ghost'} size="sm" onClick={() => !autoOn && toggle()} aria-pressed={autoOn}>{t('prefs.auto')}</Btn>
          <Btn variant={!autoOn ? 'primary' : 'ghost'} size="sm" onClick={() => autoOn && toggle()} aria-pressed={!autoOn}>{t('prefs.manual')}</Btn>
        </div>
      </div>
      {/* Pre-race notice ("don't touch anything while we set up the race"). Same segmented pattern. */}
      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <span className="display" style={{ fontSize: 15 }}>{t('prefs.launchNotice')}</span>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('prefs.launchNoticeDesc')}</Mono>
        </div>
        <div className="row" role="group" aria-label={t('prefs.launchNotice')} style={{ flexShrink: 0 }}>
          <Btn variant={noticeOn ? 'primary' : 'ghost'} size="sm" onClick={() => toggleNotice(true)} aria-pressed={noticeOn}>{t('prefs.on')}</Btn>
          <Btn variant={!noticeOn ? 'primary' : 'ghost'} size="sm" onClick={() => toggleNotice(false)} aria-pressed={!noticeOn}>{t('prefs.off')}</Btn>
        </div>
      </div>
      {/* VR: game launch mode. The same component as the Play window (Flat | VR and, with
          VR, runtime + panel + confirmation), with Settings' long text on top. The two VR options are not
          merged into a single button: 'steamvr' launches in OpenVR and 'oculusvr' in native Oculus, and the same headset falls
          on one side or the other depending on how it is connected; only the user knows which transport they use. */}
      <div className="col" style={{ gap: 10, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <span className="display" style={{ fontSize: 15 }}>{t('prefs.launchMode')}</span>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('prefs.launchModeDesc')}</Mono>
        </div>
        <LaunchModePicker align="start" />
      </div>
      {/* UI scale. Webview zoom (the window does NOT resize: what grows is what's
          drawn inside it). Applies instantly and is remembered across sessions. */}
      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <span className="display" style={{ fontSize: 15 }}>{t('prefs.uiScale')}</span>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('prefs.uiScaleDesc')}</Mono>
        </div>
        <div className="row" role="group" aria-label={t('prefs.uiScale')} style={{ flexShrink: 0 }}>
          {UI_SCALES.map((pct) => (
            <Btn key={pct} variant={scale === pct ? 'primary' : 'ghost'} size="sm"
              onClick={() => pickScale(pct)} aria-pressed={scale === pct}>{pct}%</Btn>
          ))}
        </div>
      </div>
      {/* simulation mode (debug). The toggle on the connection screen is still there; this is the
          same one, reachable once the app is connected. Amber when active: the results are fabricated. */}
      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: simOn ? '1px solid var(--amber)' : '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <span className="display" style={{ fontSize: 15, color: simOn ? 'var(--amber)' : undefined }}>{t('prefs.simRace')}</span>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('prefs.simRaceDesc')}</Mono>
        </div>
        <div className="row" role="group" aria-label={t('prefs.simRace')} style={{ flexShrink: 0 }}>
          <Btn variant={simOn ? 'primary' : 'ghost'} size="sm" onClick={() => toggleSim(true)} aria-pressed={simOn}>{t('prefs.on')}</Btn>
          <Btn variant={!simOn ? 'primary' : 'ghost'} size="sm" onClick={() => toggleSim(false)} aria-pressed={!simOn}>{t('prefs.off')}</Btn>
        </div>
      </div>
      {/* replay the guided tutorial -- only if the shell wires it up (onReplayTutorial) */}
      {onReplayTutorial && (
        <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
          <div className="col" style={{ gap: 4, maxWidth: 560 }}>
            <span className="display" style={{ fontSize: 15 }}>{t('tour.replayTitle')}</span>
            <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('tour.replayDesc')}</Mono>
          </div>
          <Btn variant="ghost" size="sm" data-tour="settings.replay" style={{ flexShrink: 0 }} onClick={onReplayTutorial}>{t('tour.replay')}</Btn>
        </div>
      )}
    </div>
  );
}

// multi-profile manager -- list (active one highlighted, Sandbox badge), switch/delete, and create
// with name + mode (Career | Sandbox: everything unlocked + 10M credits). After creating/switching, onSwitched triggers the
// shell's global refresh (same pattern as the debug save presets): re-query career_state/identity + remount.
function ProfilesSection({ client, onSwitched }: { client: ApiClientLike; onSwitched?: () => void }) {
  const { t } = useTranslation();
  const q = useQuery<ProfilesPayload>(client, 'query.profiles');
  const [name, setName] = useState('');
  const [mode, setMode] = useState<'career' | 'sandbox'>('career');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const [needsRestart, setNeedsRestart] = useState(() => sessionStorage.getItem('cantera:profileRestart') === '1');

  const run = async (cmd: string, params: Record<string, unknown>, switched: boolean) => {
    setBusy(true); setErr('');
    try {
      await client.cmd(cmd, params);
      await q.refetch();
      if (switched) sessionStorage.setItem('cantera:profileRestart', '1');   // survives the remount that onSwitched triggers
      if (switched) onSwitched?.();
    } catch (e) {
      setErr(String(e));
      void q.refetch();
    } finally { setBusy(false); }
  };
  const create = () => { if (!name.trim()) return; void run('cmd.profile.create', { name: name.trim(), mode }, true).then(() => setName('')); };
  const select = (slug: string) => void run('cmd.profile.select', { slug }, true);
  // hub (tester, 29/08): deleting a profile was ONE irreversible click -- weeks of progress to a misclick.
  // Two inline steps: the first click ARMS the button (red, "delete forever?") and only the second
  // one deletes; it disarms itself after 4s, or when another profile's is armed.
  const [confirmDel, setConfirmDel] = useState<string | null>(null);
  const remove = (slug: string) => {
    if (confirmDel !== slug) {
      setConfirmDel(slug);
      window.setTimeout(() => setConfirmDel((c) => (c === slug ? null : c)), 4000);
      return;
    }
    setConfirmDel(null);
    void run('cmd.profile.delete', { slug }, false);
  };

  const rows = q.data?.profiles ?? [];
  const active = q.data?.active ?? '';
  const MAX_PROFILES = 3;                       // 3 slots (same as the Play window); delete one to make room
  const atLimit = rows.length >= MAX_PROFILES;
  return (
    <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
      <div className="col" style={{ gap: 2 }}>
        <Display style={{ fontSize: 19 }}>{t('profiles.title')}</Display>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('profiles.sub')}</Mono>
      </div>
      {needsRestart && (
        <div style={{ padding: '12px 16px', border: '1px solid color-mix(in oklab, var(--amber) 45%, transparent)', background: 'color-mix(in oklab, var(--amber) 8%, var(--surface))', display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 12 }}>
          <div className="col" style={{ gap: 2, minWidth: 0 }}>
            <Mono style={{ fontSize: 12, fontWeight: 700, color: 'var(--amber)' }}>⚠ {t('profiles.restartTitle')}</Mono>
            <Mono style={{ fontSize: 10.5, color: 'var(--text-2)' }}>{t('profiles.restartWarn')}</Mono>
          </div>
          <Btn variant="ghost" size="sm" style={{ flexShrink: 0 }} onClick={() => { sessionStorage.removeItem('cantera:profileRestart'); setNeedsRestart(false); }}>{t('profiles.restartDismiss')}</Btn>
        </div>
      )}
      <div className="col" style={{ gap: 2 }}>
        {rows.map((p) => {
          const isActive = p.slug === active;
          return (
            <div key={p.slug} className="row between" style={{ alignItems: 'center', gap: 16, padding: '12px 16px', background: 'var(--surface)', border: isActive ? '1px solid var(--accent)' : '1px solid var(--line)' }}>
              <div className="row" style={{ alignItems: 'center', gap: 10, minWidth: 0 }}>
                {isActive && <span style={{ width: 8, height: 8, borderRadius: '50%', background: 'var(--accent)', flexShrink: 0 }} />}
                <span className="display" style={{ fontSize: 15, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{p.name}</span>
                {p.mode === 'sandbox' && (
                  <span style={{ fontSize: 9, fontWeight: 700, letterSpacing: '.08em', color: 'var(--amber)', background: 'color-mix(in oklab, var(--amber) 16%, transparent)', borderRadius: 3, padding: '2px 5px', flexShrink: 0 }}>
                    {t('profiles.sandboxBadge')}
                  </span>
                )}
                {isActive && <Mono style={{ fontSize: 9.5, color: 'var(--text-3)', flexShrink: 0 }}>{t('profiles.active')}</Mono>}
              </div>
              <div className="row" style={{ gap: 6, flexShrink: 0 }}>
                {!isActive && <Btn variant="ghost" size="sm" disabled={busy} onClick={() => select(p.slug)}>{t('profiles.switch')}</Btn>}
                {!isActive && (
                  <Btn variant="ghost" size="sm" disabled={busy} onClick={() => remove(p.slug)}
                       style={confirmDel === p.slug ? { color: 'var(--danger)', borderColor: 'var(--danger)', fontWeight: 700 } : undefined}>
                    {confirmDel === p.slug ? t('profiles.deleteConfirm') : t('profiles.delete')}
                  </Btn>
                )}
              </div>
            </div>
          );
        })}
      </div>
      <div className="row between" style={{ alignItems: 'center', gap: 16, padding: '14px 16px', background: 'var(--surface)', border: '1px dashed var(--line)' }}>
        <div className="col" style={{ gap: 8, flex: 1, minWidth: 0 }}>
          <div className="row" style={{ gap: 8, alignItems: 'center' }}>
            <input value={name} onChange={(e) => setName(e.target.value)} placeholder={t('profiles.namePh')}
              onKeyDown={(e) => e.key === 'Enter' && create()}
              style={{ flex: 1, minWidth: 0, background: 'var(--surface-2, var(--surface))', border: '1px solid var(--line)', color: 'var(--text)', padding: '8px 10px', fontSize: 13, fontFamily: 'inherit' }} />
            <div className="row" role="group" aria-label={t('profiles.mode')} style={{ flexShrink: 0 }}>
              <Btn variant={mode === 'career' ? 'primary' : 'ghost'} size="sm" aria-pressed={mode === 'career'} onClick={() => setMode('career')}>{t('profiles.modeCareer')}</Btn>
              <Btn variant={mode === 'sandbox' ? 'primary' : 'ghost'} size="sm" aria-pressed={mode === 'sandbox'} onClick={() => setMode('sandbox')}>{t('profiles.modeSandbox')}</Btn>
            </div>
            <Btn variant="primary" size="sm" disabled={busy || !name.trim() || atLimit} onClick={create} style={{ flexShrink: 0 }}>{t('profiles.create')}</Btn>
          </div>
          <Mono style={{ fontSize: 10, color: atLimit ? 'var(--amber)' : 'var(--text-3)', lineHeight: 1.5 }}>
            {atLimit
              ? t('profiles.limitReached', { n: MAX_PROFILES, defaultValue: 'Max {{n}} profiles — delete one to add another.' })
              : mode === 'sandbox' ? t('profiles.sandboxDesc') : t('profiles.careerDesc')}
          </Mono>
          {err && <Mono style={{ fontSize: 10.5, color: 'var(--danger)' }}>{err}</Mono>}
        </div>
      </div>
    </div>
  );
}


// Provisioning: re-runs the sync + the full asset generation (photos/maps) on demand.
// Only ever operates on the MANAGED data root (never on a dev config pointing at docs); if it detects a
// custom folder, it warns and touches nothing (same guard as the automatic startup sync in App.tsx).
function AssetsSection() {
  const { t } = useTranslation();
  const [status, setStatus] = useState<'idle' | RegenStatus | 'dev'>('idle');
  const [progress, setProgress] = useState<ProvisionProgress | null>(null);
  const [summary, setSummary] = useState('');
  const [open, setOpen] = useState(false);
  const run = async () => {
    const b = await getBridge();
    const cfg = await loadConfig();
    if (!cfg) return;
    const managed = managedDataRoot(await b.paths.localAppData());
    if (cfg.paths.data.replace(/[\/]+$/, '').toLowerCase() !== managed.toLowerCase()) {
      setStatus('dev'); setSummary(t('assets.devNote')); return;
    }
    setStatus('running'); setProgress(null); setSummary(''); setOpen(true);
    supportLog(null, 'assets: regeneration started from Settings');   // operates on the filesystem, doesn't go through the engine
    // All the phases, not just the conversion: the extraction (the slow part) used to show nothing.
    const un = await b.provision.onProgress((p) => setProgress(p));
    try {
      await b.provision.syncStatic(managed);
      const r = await b.provision.run(cfg.paths.gameInstall, managed);
      if (r.cancelled) {
        setSummary(t('assets.cancelledSummary')); setStatus('cancelled');
        supportLog(null, 'assets: regeneration CANCELLED by user');
      } else {
        setSummary(t('assets.done', { ok: r.vehOk + r.trkOk, fail: r.vehFail + r.trkFail, skipped: r.skipped }));
        setStatus('done');
        supportLog(null, `assets: regeneration OK (${r.vehOk + r.trkOk} new, ${r.vehFail + r.trkFail} failed)`);   //
      }
    } catch (e) {
      // Same classification as the first-run wizard: regenerating from here used to fail with the
      // raw .NET host crash dump, without ever telling the user they're missing the runtime.
      const m = String(e);
      setSummary(m === 'dotnet_missing' || m.startsWith('spawn:') ? t('firstrun.assetsDotnet')
        : m.includes('pak_tool_corrupt') ? t('firstrun.assetsToolCorrupt')   //
        : t('assets.error', { msg: m }));
      setStatus('error');
      supportLog(null, `assets: regeneration FAILED (${m})`);   //
    } finally { un(); }
  };
  const cancel = async () => {
    setStatus('cancelling');
    supportLog(null, 'assets: cancel requested');
    try { await (await getBridge()).provision.cancel(); } catch { /* run() would finish anyway */ }
  };
  const busy = status === 'running' || status === 'cancelling';
  return (
    <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
      <div className="col" style={{ gap: 2 }}>
        <Display style={{ fontSize: 19 }}>{t('assets.title')}</Display>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('assets.sub')}</Mono>
      </div>
      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('assets.desc')}</Mono>
          {summary && !busy && <Mono style={{ fontSize: 10.5, color: status === 'error' ? 'var(--danger)' : 'var(--text-2)' }}>{summary}</Mono>}
        </div>
        <Btn variant="ghost" size="sm" disabled={busy} onClick={() => void run()} style={{ flexShrink: 0 }}>{t('assets.button')}</Btn>
      </div>
      {open && status !== 'dev' && status !== 'idle' && (
        <RegenerateAssetsDialog status={status} progress={progress} summary={summary}
          onCancel={() => void cancel()} onClose={() => setOpen(false)} />
      )}
    </div>
  );
}

// (support): "View log" button — opens %LOCALAPPDATA%\Career Engine\logs, the ONLY folder with the
// two files to share when reporting a problem (zta_support.log + zta_engine.log; per user feedback).
// The Rust side creates it if it doesn't exist, so the button works even before the engine has written anything.
function SupportSection() {
  const { t } = useTranslation();
  const [err, setErr] = useState('');
  // Review batch 0 (#ea77c402): before, this note was shown ALWAYS, without checking whether the copy had actually been
  // made. Now Rust is asked (pre070_backup.rs is the one that knows the real state: success mark,
  // final-failure mark after the retry cap, or neither yet). Single read on
  // mount: the state does not change while Settings is open (the copy only runs once, at startup).
  const [backupStatus, setBackupStatus] = useState<Pre070BackupStatus | null>(null);
  useEffect(() => {
    let alive = true;
    void (async () => {
      try {
        const b = await getBridge();
        const s = await b.support.pre070BackupStatus();
        if (alive) setBackupStatus(s);
      } catch { /* best-effort: without a state read, nothing is shown (same criterion as 'none') */ }
    })();
    return () => { alive = false; };
  }, []);
  const open = async () => {
    setErr('');
    try {
      const b = await getBridge();
      await b.opener.openPath(b.paths.join(await b.paths.localAppData(), BRAND_DIR, 'logs'));
    } catch (e) { setErr(String(e)); }
  };
  return (
    <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
      <div className="col" style={{ gap: 2 }}>
        <Display style={{ fontSize: 19 }}>{t('support.title')}</Display>
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('support.sub')}</Mono>
      </div>
      <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
        <div className="col" style={{ gap: 4, maxWidth: 560 }}>
          <span className="display" style={{ fontSize: 15 }}>{t('support.logTitle')}</span>
          <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('support.logDesc')}</Mono>
          {err && <Mono style={{ fontSize: 10.5, color: 'var(--danger)' }}>{err}</Mono>}
        </div>
        <Btn variant="ghost" size="sm" onClick={() => void open()} style={{ flexShrink: 0 }}>{t('support.viewLog')}</Btn>
      </div>
      {/* Batch 0d (0.7.0): informational ONLY — the copy is made by pre070_backup.rs at startup, no button here.
          Review batch 0: conditioned on the real state, no longer shown unconditionally. */}
      {backupStatus?.state === 'done' && (
        <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>
          {t('support.pre070BackupNote', { path: backupStatus.backupDir })}
        </Mono>
      )}
      {backupStatus?.state === 'failed' && (
        <Mono style={{ fontSize: 10.5, color: 'var(--danger)', lineHeight: 1.5 }}>
          {t('support.pre070BackupFailedNote')}
        </Mono>
      )}
    </div>
  );
}

// DIFFICULTY section (cards + trim/fixed; per-category hover breakdown): its own component
// (used to live inline inside Settings) so it can be mounted on its own in tests. First review pass: it does NOT make its
// own query.settings call -- it receives `diff` and `onChanged` (the refetch) from the parent, which already has the payload loaded.
// Before this change it ran its own useQuery, which duplicated the network request and decoupled its loading gate
// from the rest of Settings (aggression, weather...): a staggered loading window that didn't exist in the
// inline version. With props, it's back to being ONE query and ONE gate, just like before the extraction.
export function DifficultySection({ client, diff, onChanged }: {
  client: ApiClientLike; diff: SettingsDifficulty; onChanged: () => void;
}) {
  const { t } = useTranslation();

  // Actions -> cmd.* in the engine + parent refresh. Fire-and-refresh (the engine is the source of truth); a silent
  // .catch (if the command fails, the refetch shows the real state anyway). Disabling the button during submission
  // isn't necessary: the engine validates and persists; the refetch corrects any discrepancy.
  const setMode = (mode: string) => { void client.cmd('cmd.settings.setMode', { mode }).then(onChanged).catch(onChanged); };
  const setTrim = (trim: number) => { void client.cmd('cmd.settings.setTrim', { trim }).then(onChanged).catch(onChanged); };
  // Manually FIXED difficulty. The number is edited LOCALLY and sent with a delay: dragging the slider
  // fires one event per point, and every send writes to the engine's save. The engine is still the source of truth --
  // customEdit only exists while you're actively dragging, and turns itself off once the value comes back. This IS
  // this section's own state (not the parent's): nothing else in Settings edits fixed difficulty.
  const customAi = diff.customAi;
  const [customEdit, setCustomEdit] = useState<number | null>(null);
  const customVal = customEdit ?? customAi ?? 0;   // what gets RENDERED: an in-progress edit wins over the engine
  const customTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  useEffect(() => { if (customEdit != null && customAi === customEdit) setCustomEdit(null); }, [customAi, customEdit]);
  useEffect(() => () => { if (customTimer.current) clearTimeout(customTimer.current); }, []);
  const setCustomAi = (raw: number) => {
    const lo = diff.aiMin, hi = diff.aiMax;
    const value = Math.min(hi, Math.max(lo, Math.round(raw)));
    setCustomEdit(value);
    if (customTimer.current) clearTimeout(customTimer.current);
    customTimer.current = setTimeout(() => {
      customTimer.current = null;
      void client.cmd('cmd.settings.setCustomAi', { value }).then(onChanged).catch(onChanged);
    }, 250);
  };
  // hovering the Adaptive setting's big number reveals the per-category breakdown.
  // Also its own state: nothing else in Settings needs to know whether this hover is open.
  const [ddaHover, setDdaHover] = useState(false);
  // The DDA-lock panel, where you actually CHANGE things (the hover above is read-only). Its own state:
  // no other section in Settings opens this panel.
  const [locksOpen, setLocksOpen] = useState(false);
  const setClassLock = (classId: string, locked: boolean) => {
    void client.cmd('cmd.settings.setClassLock', { classId, locked }).then(onChanged).catch(onChanged);
  };
  const setLockAll = (locked: boolean) => {
    void client.cmd('cmd.settings.setLockAll', { locked }).then(onChanged).catch(onChanged);
  };

  return (
    // auto-fit instead of 5 fixed columns: with the fixed-difficulty card that's SIX now, and at 1280px
    // wide, six rigid columns leave no room for the big number. This way they spread across two rows when
    // they don't fit, instead of getting squeezed.
    <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(186px, 1fr))', gap: 12, alignItems: 'start' }} data-tour="settings.difficulty">
      {diff.cards.map((c) => {
        const sel = diff.mode === c.mode;
        const isCustom = c.mode === 'custom';
        // the Adaptive setting's manual adjustment sits INSIDE its own card once selected
        const trim = c.isAuto && sel ? (
          <div className="col" style={{ gap: 4, marginTop: 6, paddingTop: 10, borderTop: '1px solid var(--line)' }} onClick={(e) => e.stopPropagation()}>
            <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)', textTransform: 'uppercase' }}>{t('diff_trim')}</Mono>
            <div className="row between" style={{ alignItems: 'center' }}>
              <Btn variant="ghost" size="sm" disabled={diff.trim <= diff.trimMin} onClick={() => setTrim(diff.trim - 1)}>−</Btn>
              <span className="display" style={{ fontSize: 22, color: 'var(--purple)', minWidth: 40, textAlign: 'center' }}>{fmtSigned(diff.trim)}</span>
              <Btn variant="ghost" size="sm" disabled={diff.trim >= diff.trimMax} onClick={() => setTrim(diff.trim + 1)}>+</Btn>
            </div>
            {/* the door into the DDA-lock panel. Only for adaptive mode: in the
                fixed modes and manually-fixed difficulty there's nothing to lock. */}
            <Btn variant="ghost" size="sm" onClick={() => setLocksOpen(true)}>{t('diff_locks_open')}</Btn>
          </div>
        ) : undefined;
        // the FIXED card carries the number inside it, editable: that's the only thing that defines it. The slider
        // covers the whole range (70..120 is 50 points, which at ±1 per click would be 50 clicks) and the arrows
        // fine-tune one at a time. stopPropagation on the container: touching the control isn't re-selecting,
        // and without it, dragging on an already-active card wouldn't do anything odd, but on another card it would switch it.
        const custom = isCustom && sel && customAi != null ? (
          <div className="col" style={{ gap: 6, marginTop: 6, paddingTop: 10, borderTop: '1px solid var(--line)' }}
               onClick={(e) => e.stopPropagation()} onKeyDown={(e) => e.stopPropagation()}>
            <Mono style={{ fontSize: 9, letterSpacing: '.1em', color: 'var(--text-3)', textTransform: 'uppercase' }}>{t('diff_custom_pick')}</Mono>
            <div className="row between" style={{ alignItems: 'center', gap: 6 }}>
              <Btn variant="ghost" size="sm" aria-label={t('diff_custom_less')} disabled={customVal <= diff.aiMin} onClick={() => setCustomAi(customVal - 1)}>−</Btn>
              <input type="range" className="focusable" min={diff.aiMin} max={diff.aiMax} step={1} value={customVal}
                     aria-label={t('diff_custom_pick')}
                     onChange={(e) => setCustomAi(Number(e.currentTarget.value))}
                     style={{ flex: 1, minWidth: 0, accentColor: c.color, cursor: 'pointer' }} />
              <Btn variant="ghost" size="sm" aria-label={t('diff_custom_more')} disabled={customVal >= diff.aiMax} onClick={() => setCustomAi(customVal + 1)}>+</Btn>
            </div>
          </div>
        ) : undefined;
        // on the selected Adaptive card, the big current AI-difficulty number.
        // the fixed card shows its own in the same format: it's the number that's actually going to be raced, just as real.
        const ai = c.isAuto && sel ? { value: diff.aiEffective, max: diff.aiMax }
                 : isCustom && customAi != null ? { value: customVal, max: diff.aiMax }
                 : undefined;
        // the per-category breakdown. Same hover pattern as Calendar.tsx
        // (onMouseEnter/onMouseLeave + position:relative on the container); no new tooltip
        // component needed. Only on the adaptive card, and only if the engine sends rows: an older engine won't send
        // `classes`, and a freshly-started save sends it empty -- in both cases there's nothing to show.
        const clases = diff.classes ?? [];
        const hover = c.isAuto && sel && ddaHover && clases.length > 0 ? (
          <div style={{
            position: 'absolute', top: '100%', left: 0, zIndex: 20, marginTop: 6, minWidth: 300,
            background: 'var(--surface-2)', border: '1px solid var(--line)', padding: '10px 12px',
            boxShadow: '0 10px 30px -12px rgba(0,0,0,.7)',
          }}>
            <Mono style={{ fontSize: 9, letterSpacing: '.14em', color: 'var(--text-3)' }}>
              {t('diff_by_class')}
            </Mono>
            <div className="col" style={{ gap: 4, marginTop: 8 }}>
              {clases.map((k) => (
                <div key={k.classId} data-testid={`dda-class-row-${k.classId}`} className="row"
                     style={{ alignItems: 'baseline', gap: 10 }}>
                  <span style={{ fontSize: 12.5, color: 'var(--text-2)', minWidth: 0, whiteSpace: 'nowrap',
                                 overflow: 'hidden', textOverflow: 'ellipsis' }}>{k.classId}</span>
                  <div className="grow" />
                  <span className="display" style={{ fontSize: 15, color: c.color }}>{k.difficulty}</span>
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)', minWidth: 28, textAlign: 'right' }}>
                    {fmtSigned(k.delta)}
                  </Mono>
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)', minWidth: 42, textAlign: 'right' }}>
                    {k.seeded ? t('diff_by_class_seeded') : t('diff_races_short', { n: k.races })}
                  </Mono>
                </div>
              ))}
            </div>
          </div>
        ) : undefined;
        return <Card key={c.mode} card={c} selected={sel} onSelect={() => setMode(c.mode)}
                     trim={trim ?? custom} ai={ai} hover={hover}
                     onHoverIn={() => setDdaHover(true)} onHoverOut={() => setDdaHover(false)} />;
      })}
      {locksOpen && (
        <DifficultyLocksSheet
          classes={diff.classes ?? []}
          lockAll={!!diff.lockAll}
          level={diff.aiEffective}
          trim={diff.trim}
          onClose={() => setLocksOpen(false)}
          onToggleClass={setClassLock}
          onToggleAll={setLockAll} />
      )}
    </div>
  );
}

export function Settings({ client, onEditProfile, onProfileSwitched, onReplayTutorial, onDataSettled }: { client: ApiClientLike; onEditProfile?: () => void; onProfileSwitched?: () => void; onReplayTutorial?: () => void; onDataSettled?: () => void }) {
  const { t } = useTranslation();
  const q = useQuery<SettingsPayload>(client, 'query.settings');
  // T147 (5): this screen's tour waited a FIXED margin (MOUNT_DELAY_MS) before measuring its anchors;
  // with a slow query.settings, the tour fired before data-tour="settings.difficulty"/
  // ".gameplay"/".replay" existed, and those steps were silently filtered out (lame tour). Notifies the shell ONCE
  // (not on every later refetch) when this query settles, so it waits for THAT instead of a fixed clock.
  const settledRef = useRef(false);
  useEffect(() => {
    if (!q.loading && !settledRef.current) { settledRef.current = true; onDataSettled?.(); }
  }, [q.loading]);   // eslint-disable-line react-hooks/exhaustive-deps
  const diff = q.data?.difficulty;
  // AI aggression (0 = auto by phase; 40/60/80/100 = fixed for the whole race)
  const aggr = q.data?.aggression?.value ?? 0;
  const setAggr = (v: number) => { void client.cmd('cmd.settings.setAggr', { aggr: v }).then(() => q.refetch()).catch(() => q.refetch()); };
  // toggles for Gameplay's automatic multiplier scaling.
  const gp = q.data?.gameplayScaling ?? { tyre: true, fuel: true };
  const setScale = (tyre: boolean, fuel: boolean) => { void client.cmd('cmd.settings.setScale', { tyre, fuel }).then(() => q.refetch()).catch(() => q.refetch()); };
  // FCY/safety car -- tri-state 0=No/1=Yes/2=Inherited (applied at launch).
  // Old DLL without fcyMode: derived from the bool (no Inherited available). The command also sends `on` so
  // an older DLL still understands the setting (Inherited degrades to Yes, its default).
  const fcyMode = q.data?.fcyMode ?? ((q.data?.fcyOn ?? true) ? 1 : 0);
  const setFcy = (mode: number) => { void client.cmd('cmd.settings.setFcy', { mode, on: mode !== 0 }).then(() => q.refetch()).catch(() => q.refetch()); };
  // Weather mode + selection. Weights absent = only the mode changes and the saved selection
  // stays as it was (switching modes can't wipe out a selection that took effort to set up).
  const weather = q.data?.weather;
  const setWeather = (mode: number, weights?: Record<string, number>) => {
    void client.cmd('cmd.settings.setWeather', weights ? { mode, weights } : { mode })
      .then(() => q.refetch()).catch(() => q.refetch());
  };
  // 21/07: the save's pace (reward mode). Chosen when creating the career and, since 0.7.0 (thread
  // “Progression / career speed”), changed here. The engine rewrites profile.json and the save; if it could not
  // (ok:false or error) a notice is shown and the refetch leaves marked the mode that actually remained.
  const reward = q.data?.reward;
  const [rewardFailed, setRewardFailed] = useState(false);
  const setReward = (mode: CareerDifficulty) => {
    setRewardFailed(false);
    void client.cmd<{ ok?: boolean }>('cmd.settings.setReward', { mode })
      .then((res) => { if (res?.ok === false) setRewardFailed(true); })
      .catch(() => setRewardFailed(true))
      .finally(() => { void q.refetch(); });
  };
  // per-field AUTO/Inherited (only if the engine sends it; older DLL -> block hidden).
  const inherit = q.data?.inherit;
  const setInherit = (field: keyof SettingsInherit, value: boolean) => {
    void client.cmd('cmd.settings.setInherit', { field, inherit: value }).then(() => q.refetch()).catch(() => q.refetch());
  };
  // subscreen: "Race configuration" (Auto/Inherited + multipliers + safety car).
  // Local screen state (no router); the hook runs BEFORE the loading early-returns.
  const [raceSetup, setRaceSetup] = useState(false);
  // sharing race telemetry (consent was requested in front of the Play button).
  // It's written to BOTH places: the engine (which is what marks the payload as shareable) and the local
  // preference, which is what the shell syncs on every connection. Without the second one, turning it off here would only last until
  // the next reconnection, which would push the old value back up.
  const telemetryOptIn = q.data?.telemetryOptIn;
  const setTelemetry = (on: boolean) => {
    setTelemetryChoice(on);
    void client.cmd('cmd.settings.setTelemetry', { on }).then(() => q.refetch()).catch(() => q.refetch());
  };

  if (q.loading) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;
  if (q.error || !diff) return <Center><Mono style={{ color: 'var(--text-3)' }}>{t('common.loading')}</Mono></Center>;

  if (raceSetup)
    return <RaceSetupView gp={gp} setScale={setScale} fcyMode={fcyMode} setFcy={setFcy} weather={weather} setWeather={setWeather}
                          inherit={inherit} setInherit={setInherit} onBack={() => setRaceSetup(false)} />;

  // position:relative: this is what confines the locks panel (position:absolute) to THIS screen, below
  // the top bar, as the design requires. Without it, it would climb all the way up to the shell's own container.
  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column', position: 'relative' }}>
      <div style={{ padding: '18px 30px 8px' }}>
        <Display as="h1" style={{ fontSize: 30 }}>{t('settings_tab')}</Display>
        <Kicker style={{ color: 'var(--accent-text)' }}>{t('diff_kicker')}</Kicker>
        <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('diff_sub')}</Mono>
        {/* the real AI difficulty number (base + trim) no longer goes here as text --
            it's shown BIG inside the Adaptive card itself (see Card `ai`). */}
      </div>
      <div className="scroll" style={{ flex: 1, padding: '12px 30px 24px', minHeight: 0 }}>
        <DifficultySection client={client} diff={diff} onChanged={q.refetch} />
        {/* AI aggression -- Auto (scales with your phase) or a fixed override for the whole race. */}
        <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
          <div className="col" style={{ gap: 2 }}>
            <Display style={{ fontSize: 19 }}>{t('aggr.title')}</Display>
            <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('aggr.sub')}</Mono>
          </div>
          <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
            <div className="col" style={{ gap: 4, maxWidth: 560 }}>
              <span className="display" style={{ fontSize: 15 }}>{t('aggr.label')}</span>
              <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('aggr.desc')}</Mono>
            </div>
            <div className="row" role="group" aria-label={t('aggr.label')} style={{ flexShrink: 0, gap: 2 }}>
              {([[0, 'aggr.auto'], [40, 'aggr.low'], [60, 'aggr.medium'], [80, 'aggr.high'], [100, 'aggr.max']] as const).map(([v, k]) => (
                <Btn key={v} variant={aggr === v ? 'primary' : 'ghost'} size="sm" aria-pressed={aggr === v} onClick={() => aggr !== v && setAggr(v)}>{t(k)}</Btn>
              ))}
            </div>
          </div>
        </div>
        {/* entry point into the "Race configuration" subscreen (Auto/Inherited + multipliers
            + safety car). The settings.gameplay tutorial anchor lives here (the "Realism" step still
            lands correctly even though the toggles moved inside). */}
        <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }} data-tour="settings.gameplay">
          <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
            <div className="col" style={{ gap: 4, maxWidth: 560 }}>
              <span className="display" style={{ fontSize: 15 }}>{t('raceSetup.title')}</span>
              <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('raceSetup.entryDesc')}</Mono>
            </div>
            <Btn variant="ghost" size="sm" data-tour="settings.raceSetupOpen" style={{ flexShrink: 0 }} onClick={() => setRaceSetup(true)}>{t('raceSetup.open')} →</Btn>
          </div>
          {/* 21/07: career PACE (reward mode). Chosen when creating the save; since 0.7.0 also
              changed here, with the same three modes and the same texts as the create-career card.
              Absent (older DLL) -> not drawn. */}
          {reward && (
            <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
              <div className="col" style={{ gap: 4, maxWidth: 560 }}>
                <span className="display" style={{ fontSize: 15 }}>{t('play.pickPace')}</span>
                <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('play.paceNoteChangeable')}</Mono>
                {rewardFailed && (
                  <Mono style={{ fontSize: 10.5, color: 'var(--danger)', lineHeight: 1.5 }}>{t('play.paceSaveFailed')}</Mono>
                )}
              </div>
              <div className="row" role="group" aria-label={t('play.pickPace')} style={{ flexShrink: 0, gap: 2, alignItems: 'flex-start' }}>
                {CAREER_DIFFICULTIES.map((d, i) => {
                  const on = reward.mode === i;
                  return (
                    <div key={d} className="col" style={{ gap: 3, alignItems: 'center' }}>
                      <Btn variant={on ? 'primary' : 'ghost'} size="sm" aria-pressed={on}
                           title={`${t(`play.pace.${d}`)} — ${t(`play.paceEffect.${d}`)}`}
                           onClick={() => !on && setReward(d)}>{t(`play.pace.${d}`)}</Btn>
                      <Mono style={{ fontSize: 9, color: d === 'normal' ? 'var(--text-3)' : 'var(--accent-text)' }}>{t(`play.paceShort.${d}`)}</Mono>
                    </div>
                  );
                })}
              </div>
            </div>
          )}
        </div>
        {/* sharing race telemetry. Consent was asked once, in front of the
            Play button; this is where you can change your mind. Absent (older DLL) -> the block isn't shown. */}
        {telemetryOptIn !== undefined && (
          <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
            <div className="col" style={{ gap: 2 }}>
              <Display style={{ fontSize: 19 }}>{t('telemetry.title')}</Display>
              <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('telemetry.why')}</Mono>
            </div>
            <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
              <div className="col" style={{ gap: 4, maxWidth: 560 }}>
                <span className="display" style={{ fontSize: 15 }}>{t('telemetry.settingsLabel')}</span>
                <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('telemetry.settingsDesc')}</Mono>
              </div>
              <div className="row" role="group" aria-label={t('telemetry.settingsLabel')} style={{ flexShrink: 0, gap: 2 }}>
                <Btn variant={telemetryOptIn ? 'primary' : 'ghost'} size="sm" aria-pressed={telemetryOptIn}
                     onClick={() => !telemetryOptIn && setTelemetry(true)}>{t('telemetry.on')}</Btn>
                <Btn variant={!telemetryOptIn ? 'primary' : 'ghost'} size="sm" aria-pressed={!telemetryOptIn}
                     onClick={() => telemetryOptIn && setTelemetry(false)}>{t('telemetry.off')}</Btn>
              </div>
            </div>
          </div>
        )}
        <PrefsSection onReplayTutorial={onReplayTutorial} client={client} />
        {/* access to the driver-profile EDITOR (name/sex/nationality/date). The original sign-up only
            shows on a fresh start; here it can be REVIEWED/EDITED later (a non-destructive submit mid-career). */}
        {onEditProfile && (
          <div className="col" style={{ gap: 12, marginTop: 22, paddingTop: 22, borderTop: '1px solid var(--line)' }}>
            <div className="col" style={{ gap: 2 }}>
              <Display style={{ fontSize: 19 }}>{t('profile_edit.title')}</Display>
              <Mono style={{ fontSize: 10.5, color: 'var(--text-3)' }}>{t('profile_edit.sub')}</Mono>
            </div>
            <div className="row between" style={{ alignItems: 'center', gap: 20, padding: '14px 16px', background: 'var(--surface)', border: '1px solid var(--line)' }}>
              <div className="col" style={{ gap: 4, maxWidth: 560 }}>
                <span className="display" style={{ fontSize: 15 }}>{t('profile_edit.driver')}</span>
                <Mono style={{ fontSize: 10.5, color: 'var(--text-3)', lineHeight: 1.5 }}>{t('profile_edit.desc')}</Mono>
              </div>
              <Btn variant="ghost" size="sm" onClick={onEditProfile} style={{ flexShrink: 0 }}>{t('profile_edit.button')}</Btn>
            </div>
          </div>
        )}
        <ProfilesSection client={client} onSwitched={onProfileSwitched} />
        <ContentSection client={client} />   {/* Content and DLC (auto-detection + toggles) */}
        <GamepadSection />
        <PerformanceSection />   {/* how many cars show up on track at most */}
        <AssetsSection />
        <SupportSection />
        <UpdatesSection />
        <AccountSection />
      </div>
    </div>
  );
}
