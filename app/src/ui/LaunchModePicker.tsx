// Game launch mode (Flat | VR) and, with VR selected, the headset options: runtime, panel,
// size/distance and the panel confirmation (key or wheel/controller button).
//
// lived duplicated. The Play window had the redesign (Flat | VR + options, 26/08) and Settings
// kept the old 4-chip copy, which did not even show the headset options. Now both screens draw
// THIS component, so they cannot be separated again.
//
// VR (feedback 24/07): without a selector in view, steam://run uses Steam's DEFAULT option, and a user with
// Steam-in-VR started in VR "by accident" (session 24/07), and vice versa. That is why it is glued to the Play button.
//
// VR redesign (Josh's decision 26/08, option A, refined): the row stays as Flat | VR, and when VR is selected the options block
// is ALWAYS shown (runtime + panel + size/distance + key), with no extra button: those who were bothered by
// the controls were the flat players, and they no longer see them. The PERSISTED mode does not change vocabulary
// ('flat'|'steamvr'|'oculusvr'|'dialog'): [VR] reactivates the last VR runtime used (from Flat it falls back to SteamVR).
//
// The panel settings are read by the DLL from config.json when the game STARTS, not hot (size and distance
// are also pushed live to the companion). The plumbing (config wins over localStorage, seeds if the
// key is missing) is in ui/vrPanel.ts. They are not conditioned on SteamVR being present: the panel is drawn by the DLL hooking
// LibOVR and whoever plays via Link goes on native Oculus without SteamVR anywhere.
import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { getBridge, type SteamLaunchMode } from '../shell/bridge';
import { launchMode, setLaunchMode } from './prefs';
import { Mono, Btn } from './components';
import { readAms2Gaze, writeAms2Gaze, readVrPanel, writeVrPanel, writeConfirmVk, writeConfirmButton, writeVrPanelGeometry, isAms2ConfirmKey, vkLabel,
         VR_PANEL_WIDTH_DEF, VR_PANEL_DIST_DEF } from './vrPanel';
import { PadPressWatcher, vrConfirmButtonFromPad, padDisplayName, xinputButtonName, type VrConfirmButton } from './vrConfirm';
import { connectedPads } from '../game/gamepads';
import { suspendGamepadNav } from '../game/useGamepadNav';

const CAPTURE_POLL_MS = 30;   // a pressed button lasts >50 ms: with 30 none escapes

export function LaunchModePicker({ align = 'center' }: { align?: 'center' | 'start' }) {
  const { t } = useTranslation();
  const [launch, setLaunch] = useState<SteamLaunchMode>(launchMode());
  const pickLaunch = (m: SteamLaunchMode) => { if (m !== launch) { setLaunchMode(m); setLaunch(m); } };
  const lastVr = useRef<SteamLaunchMode>(launchMode() !== 'flat' ? launchMode() : 'steamvr');
  const pickRuntime = (m: SteamLaunchMode) => { lastVr.current = m; pickLaunch(m); };

  const [vrPanel, setVrPanel] = useState(true);
  const [confirmVk, setConfirmVk] = useState<number | null>(null);
  // wheel/controller button that acts as the click; null = the key.
  const [confirmBtn, setConfirmBtn] = useState<{ btn: VrConfirmButton; name: string } | null>(null);
  const [capturing, setCapturing] = useState(false);
  // name of the device whose button was pressed but which the game cannot read (it is explained and capture continues)
  const [unreadablePad, setUnreadablePad] = useState('');
  // Size/distance of the companion panel (meters). Persisted to config.json (writeVrPanelGeometry)
  // and applied LIVE to the host (bridge.vr.setPanelGeometry): with the headset on, you can see it move instantly.
  const [panelW, setPanelW] = useState(VR_PANEL_WIDTH_DEF);
  const [panelD, setPanelD] = useState(VR_PANEL_DIST_DEF);
  useEffect(() => {
    let alive = true;
    void readVrPanel().then((v) => {
      if (!alive) return;
      setVrPanel(v.panel); setConfirmVk(v.confirmVk); setPanelW(v.widthM); setPanelD(v.distM);
      setConfirmBtn(v.confirmBtn ? { btn: v.confirmBtn, name: v.confirmBtnName } : null);
    });
    return () => { alive = false; };
  }, []);
  // 23/09: AMS2 gaze click (off by default). Applied on launch: the app writes it into the game's
  // VR XMLs right before opening it.
  const [ams2Gaze, setAms2Gaze] = useState(false);
  const gazeTouched = useRef(false);   // if the player has already pressed, the initial read (which arrives late) does not overwrite it
  useEffect(() => {
    let alive = true;
    void readAms2Gaze().then((v) => { if (alive && !gazeTouched.current) setAms2Gaze(v); });
    return () => { alive = false; };
  }, []);
  const toggleAms2Gaze = (on: boolean) => {
    gazeTouched.current = true;
    if (on === ams2Gaze) return;
    setAms2Gaze(on);
    void writeAms2Gaze(on).catch(() => { /* no writable config: it stays off, the default */ });
  };
  const toggleVrPanel = (on: boolean) => {
    if (on === vrPanel) return;
    setVrPanel(on);
    void writeVrPanel(on).catch(() => { /* no writable config: the engine's default wins */ });
  };
  const applyGeom = (w: number, d: number) => {
    setPanelW(w); setPanelD(d);
    void writeVrPanelGeometry(w, d).catch(() => { /* no writable config: lives only for this session */ });
    void getBridge().then((b) => b.vr.setPanelGeometry(w, d)).catch(() => { /* companion off: pushed at startup */ });
  };
  const round2 = (v: number) => Math.round(v * 100) / 100;

  // the capture listens to the KEYBOARD and the DEVICES at once. Before only `keydown`, and a wheel button is not
  // a key: the capture never saw it. While it lasts, the controller does not navigate the app (suspendGamepadNav),
  // or the button you want to assign could also press whatever has the focus.
  useEffect(() => {
    if (!capturing) return;
    const resumeNav = suspendGamepadNav();
    const onKey = (e: KeyboardEvent) => {
      // (the synthetic Escape of the controller's "back" cannot get here: with navigation suspended it is not emitted)
      e.preventDefault(); e.stopPropagation();
      if (e.key === 'Escape') { setCapturing(false); return; }   // cancel without changing anything
      const vk = e.keyCode;                 // on Windows this matches the virtual key code
      if (!vk || vk < 0x08 || vk > 0xFE) return;
      setCapturing(false); setUnreadablePad('');
      setConfirmVk(vk); setConfirmBtn(null);
      void writeConfirmVk(vk).catch(() => { /* the displayed value reverts on reload */ });
    };
    window.addEventListener('keydown', onKey, true);
    // What was already pressed at the start (the button the capture was opened with) does not count.
    const watcher = new PadPressWatcher();
    watcher.prime(connectedPads());
    const iv = window.setInterval(() => {
      const hit = watcher.next(connectedPads());
      if (!hit) return;
      const name = padDisplayName(hit.pad.id);
      const btn = vrConfirmButtonFromPad(hit.pad, hit.index);
      if (!btn) { setUnreadablePad(name); return; }
      setCapturing(false); setUnreadablePad('');
      setConfirmBtn({ btn, name });
      void writeConfirmButton(btn, name).catch(() => { /* the displayed value reverts on reload */ });
    }, CAPTURE_POLL_MS);
    return () => { window.removeEventListener('keydown', onKey, true); window.clearInterval(iv); resumeNav(); };
  }, [capturing]);

  const confirmLabel = (): string => {
    if (confirmBtn) {
      const { btn, name } = confirmBtn;
      const which = btn.kind === 'xinput' ? xinputButtonName(btn.button) : `${t('gamepad.button')} ${btn.button}`;
      return name ? `${name} · ${which}` : which;
    }
    return confirmVk === null ? '…' : vkLabel(confirmVk);
  };

  return (
    <div className="col" style={{ gap: 8, alignItems: align === 'center' ? 'center' : 'flex-start' }}>
      <div className="row" role="group" aria-label={t('prefs.launchMode')} style={{ gap: 4, alignItems: 'center' }}>
        <Mono style={{ fontSize: 10, color: 'var(--text-3)', marginRight: 4 }}>{t('prefs.launchMode')}</Mono>
        <Btn variant={launch === 'flat' ? 'primary' : 'ghost'} size="sm"
             onClick={() => pickLaunch('flat')} aria-pressed={launch === 'flat'}>{t('prefs.launchFlat')}</Btn>
        <Btn variant={launch !== 'flat' ? 'primary' : 'ghost'} size="sm"
             onClick={() => pickLaunch(lastVr.current)} aria-pressed={launch !== 'flat'}>{t('prefs.launchVr')}</Btn>
      </div>
      {/* VR options: runtime and confirmation apply on LAUNCH (the DLL reads them at startup); size and
          distance go LIVE to the companion. 'dialog' () still arms no app panels. */}
      {launch !== 'flat' && (
        <div className="col" style={{ gap: 8, alignItems: 'center', marginTop: 2, padding: '10px 14px',
                                      border: '1px solid var(--line)', background: 'var(--surface)' }}>
          <div className="col" role="group" aria-label={t('prefs.vrRuntime')} style={{ gap: 4 }}>
            <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('prefs.vrRuntime')}</Mono>
            {([['steamvr', t('prefs.launchSteamVr'), t('prefs.vrRuntimeSteamHint'), true],
               ['oculusvr', t('prefs.launchOculus'), t('prefs.vrRuntimeOculusHint'), true],
               ['dialog', t('prefs.launchDialog'), t('prefs.vrRuntimeDialogHint'), false]] as [SteamLaunchMode, string, string, boolean][]).map(([mode, lbl, hint, exp]) => (
              <div key={mode} className="row" style={{ gap: 8, alignItems: 'center' }}>
                <Btn variant={launch === mode ? 'primary' : 'ghost'} size="sm" style={{ minWidth: 126 }}
                     onClick={() => pickRuntime(mode)} aria-pressed={launch === mode}>{lbl}</Btn>
                <Mono style={{ fontSize: 9.5, color: 'var(--text-3)' }}>
                  {hint}{exp ? ` · ${t('prefs.experimental')}` : ''}
                </Mono>
              </div>
            ))}
          </div>
          <div className="row" style={{ gap: 16, alignItems: 'center', flexWrap: 'wrap', justifyContent: 'center' }}>
            <div className="row" role="group" aria-label={t('prefs.vrOverlay')} style={{ gap: 4, alignItems: 'center' }}>
              <Mono style={{ fontSize: 10, color: 'var(--text-3)', marginRight: 4 }}>{t('prefs.vrOverlay')}</Mono>
              <Btn variant={vrPanel ? 'primary' : 'ghost'} size="sm"
                   onClick={() => toggleVrPanel(true)} aria-pressed={vrPanel}>{t('prefs.on')}</Btn>
              <Btn variant={!vrPanel ? 'primary' : 'ghost'} size="sm"
                   onClick={() => toggleVrPanel(false)} aria-pressed={!vrPanel}>{t('prefs.off')}</Btn>
            </div>
            {/* The confirmation only draws with the panel on: it is the click INSIDE the panel. */}
            {vrPanel && (
              <div className="row" style={{ gap: 6, alignItems: 'center' }}>
                <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('prefs.vrConfirmKey')}</Mono>
                <span data-testid="vr-confirm-label" title={confirmLabel()}
                      style={{ minWidth: 46, maxWidth: 240, textAlign: 'center', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                  <Mono style={{ fontSize: 11 }}>{confirmLabel()}</Mono>
                </span>
                <Btn variant={capturing ? 'primary' : 'ghost'} size="sm"
                     onClick={() => { setUnreadablePad(''); setCapturing((v) => !v); }} aria-pressed={capturing}>
                  {capturing ? t('prefs.vrConfirmPressAny') : t('prefs.vrConfirmKeyChange')}
                </Btn>
              </div>
            )}
            {/* panel size/distance. With the headset on, changes are visible instantly
                (the host applies them live), so you can fine-tune while watching the result. */}
            {vrPanel && (
              <>
                <div className="row" role="group" aria-label={t('prefs.vrPanelSize')} style={{ gap: 4, alignItems: 'center' }}>
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('prefs.vrPanelSize')}</Mono>
                  <Btn variant="ghost" size="sm" aria-label="−" disabled={panelW <= 1.21}
                       onClick={() => applyGeom(round2(panelW - 0.2), panelD)}>−</Btn>
                  <Mono style={{ fontSize: 11, minWidth: 44, textAlign: 'center' }}>{panelW.toFixed(1)} m</Mono>
                  <Btn variant="ghost" size="sm" aria-label="+" disabled={panelW >= 4.99}
                       onClick={() => applyGeom(round2(panelW + 0.2), panelD)}>+</Btn>
                </div>
                <div className="row" role="group" aria-label={t('prefs.vrPanelDist')} style={{ gap: 4, alignItems: 'center' }}>
                  <Mono style={{ fontSize: 10, color: 'var(--text-3)' }}>{t('prefs.vrPanelDist')}</Mono>
                  <Btn variant="ghost" size="sm" aria-label="−" disabled={panelD <= 0.76}
                       onClick={() => applyGeom(panelW, round2(panelD - 0.25))}>−</Btn>
                  <Mono style={{ fontSize: 11, minWidth: 50, textAlign: 'center' }}>{panelD.toFixed(2)} m</Mono>
                  <Btn variant="ghost" size="sm" aria-label="+" disabled={panelD >= 3.99}
                       onClick={() => applyGeom(panelW, round2(panelD + 0.25))}>+</Btn>
                </div>
              </>
            )}
          </div>
          {/* 23/09: AMS2 gaze click. Separate from the panel: it applies to the GAME's menus, panel or not.
              The text changes with the state because the two positions have a different consequence. */}
          <div className="col" style={{ gap: 4, alignItems: 'center' }}>
            <div className="row" role="group" aria-label={t('prefs.vrAms2Gaze')} style={{ gap: 4, alignItems: 'center' }}>
              <Mono style={{ fontSize: 10, color: 'var(--text-3)', marginRight: 4 }}>{t('prefs.vrAms2Gaze')}</Mono>
              <Btn variant={ams2Gaze ? 'primary' : 'ghost'} size="sm"
                   onClick={() => toggleAms2Gaze(true)} aria-pressed={ams2Gaze}>{t('prefs.on')}</Btn>
              <Btn variant={!ams2Gaze ? 'primary' : 'ghost'} size="sm"
                   onClick={() => toggleAms2Gaze(false)} aria-pressed={!ams2Gaze}>{t('prefs.off')}</Btn>
            </div>
            <span data-testid="ams2-gaze-hint" style={{ textAlign: 'center', maxWidth: 520 }}>
              <Mono style={{ fontSize: 10, color: ams2Gaze ? 'var(--amber)' : 'var(--text-3)' }}>
                {ams2Gaze ? t('prefs.vrAms2GazeOnHint') : t('prefs.vrAms2GazeOffHint')}
              </Mono>
            </span>
          </div>
          {/* the device whose button was pressed cannot be read from the game: it says so, and keeps waiting. */}
          {vrPanel && capturing && unreadablePad && (
            <Mono style={{ fontSize: 10, color: 'var(--amber)', textAlign: 'center', maxWidth: 520 }}>
              {t('prefs.vrConfirmPadUnreadable', { name: unreadablePad })}
            </Mono>
          )}
          {/* With a chosen button, the key stays as a fallback: if that device is not there at launch, it clicks. */}
          {vrPanel && !capturing && confirmBtn && confirmVk !== null && (
            <Mono style={{ fontSize: 10, color: 'var(--text-3)', textAlign: 'center', maxWidth: 520 }}>
              {t('prefs.vrConfirmButtonFallback', { key: vkLabel(confirmVk) })}
            </Mono>
          )}
          {/* The warning only shows when the chosen key ACTUALLY conflicts. This already happened with
              SPACE: it confirmed in AMS2 and in the panel at once and the user ended up in Options. */}
          {vrPanel && !confirmBtn && confirmVk !== null && isAms2ConfirmKey(confirmVk) && (
            <Mono style={{ fontSize: 10, color: 'var(--danger)', textAlign: 'center', maxWidth: 520 }}>
              {t('prefs.vrConfirmKeyClash', { key: vkLabel(confirmVk) })}
            </Mono>
          )}
        </div>
      )}
    </div>
  );
}
