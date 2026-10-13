// Startup: loadConfig -> wizard if missing/game not found, otherwise the shell.
import { useEffect, useMemo, useState } from 'react';
import { CareerApiClient } from './api/client';
import { createDemoClient } from './demo/demoClient';
import { seedDemoData } from './demo/demoEvents';
import type { ApiClientLike } from './api/contracts';
import { loadConfig, defaultConfig, type AppConfig } from './config/appConfig';
import { initI18n } from './i18n';
import { AppShell, GOTO_TAB_EVENT } from './shell/AppShell';
import { setSafetyNetClient } from './support/safetyNet';
import { supportLog } from './support/supportLog';
import { OnboardingWizard } from './onboarding/OnboardingWizard';
import { AssetsStaleNotice, assetsNoticeDismissed, dismissAssetsNotice } from './ui/AssetsStaleNotice';
import { getBridge, isTauriRuntime, managedDataRoot } from './shell/bridge';
import { VrOnScreenKeyboard } from './vr/VrOnScreenKeyboard';
import { VrMirrorTick } from './vr/VrMirrorTick';
import { launchMode, isVrLaunch } from './ui/prefs';

type Boot =
  | { phase: 'loading' }
  | { phase: 'wizard' }
  | { phase: 'shell'; config: AppConfig };

function App() {
  const [boot, setBoot] = useState<Boot>({ phase: 'loading' });
  // 12/09: assets generated with ANOTHER AMS2 build -> new cars would show up without a photo.
  const [assetsStale, setAssetsStale] = useState<{ old: string; now: string } | null>(null);
  const isMock = useMemo(() => new URLSearchParams(window.location.search).has('mock') || !isTauriRuntime(), []);

  useEffect(() => {
    void (async () => {
      // restore the monitor/position from the last session BEFORE anything else in startup, so
      // the window doesn't visibly appear in one spot and jump to another. Doesn't block boot if it fails.
      try { const b = await getBridge(); await b.window.restoreGeometry(); supportLog(null, 'boot: window geometry restored'); }
      catch (e) {
        // 12/09: the failure is no longer swallowed whole. This catch was mute and this stretch —getBridge() +
        // restoreGeometry()— is exactly the startup gap that had NOT A SINGLE line: between the
        // process starting and the 'config loaded' below there is room for an app hung forever on an await,
        // painting the empty div of boot.phase==='loading' (that is, a black screen). With the line above
        // and the 'config loaded' one we can tell in which stretch it got stuck.
        supportLog(null, `boot: window geometry NOT restored (${String(e)})`);
      }
      const cfg = await loadConfig();
      // the loaded CONFIG, with values, goes to the support log — "what settings does the app see" is the
      // first question in any diagnosis. One line per path, with whether it actually exists
      // (never the token). If there's no config -> wizard, and that gets logged too.
      if (cfg) {
        supportLog(null, `config loaded: locale=${cfg.locale} api_enabled=${cfg.api_enabled} api_port=${cfg.api_port} dllOverride=${cfg.paths.dll ? 'yes' : 'no'}`);
        try {
          const b = await getBridge();
          const entries: Array<[string, string | undefined]> = [
            ['data', cfg.paths.data], ['userdata', cfg.paths.userdata],
            ['gameInstall', cfg.paths.gameInstall], ['dll', cfg.paths.dll],
          ];
          for (const [k, v] of entries) {
            if (!v) { supportLog(null, `config path ${k} = (empty -> engine fallback)`); continue; }
            const ex = await b.fs.exists(v).catch(() => false);
            supportLog(null, `config path ${k} = ${v} (${ex ? 'ok' : 'MISSING'})`);
          }
        } catch { /* no bridge (web/tests): paths can't be probed */ }
      } else {
        supportLog(null, 'config missing or invalid -> first-run wizard');
      }
      if (cfg && !isMock) {
        // static-asset sync ONLY targets the managed data root (never a dev config pointing at docs)
        try {
          const b = await getBridge();
          const managed = managedDataRoot(await b.paths.localAppData());
          if (cfg.paths.data.replace(/[\\/]+$/, '').toLowerCase() === managed.toLowerCase()) {
            await b.provision.syncStatic(managed);
          }
        } catch { /* a failed sync doesn't block startup */ }
        // Stale-assets warning: if the game changed build since they were generated, the cars
        // that the update brings show up without a photo. FAIL-CLOSED on the Rust side: without a stamp or without a version it does NOT
        // warn (a false warning sends the user to a ~9.5 GB extraction).
        try {
          const b = await getBridge();
          if (cfg.paths.gameInstall) {
            const st = await b.provision.assetsStatus(cfg.paths.gameInstall, cfg.paths.data);
            supportLog(null, `assets: provisioned=${st.provisionedVersion || '(sin sello)'} current=${st.currentVersion || '(ilegible)'} stale=${st.stale}`);
            if (st.stale && !assetsNoticeDismissed(st.currentVersion))
              setAssetsStale({ old: st.provisionedVersion, now: st.currentVersion });
          }
        } catch { /* the warning never blocks startup */ }
      }
      if (cfg) {
        initI18n(cfg.locale);
        // A valid config with gameInstall resolved is enough to enter the shell (the shell handles
        // disconnection gracefully). We don't gate on a live fs.exists check: it was brittle (it forced the
        // wizard even when the game existed), and the config was already written by the wizard or setup.
        const gameOk = !!cfg.paths.gameInstall;
        setBoot(gameOk || isMock ? { phase: 'shell', config: cfg } : { phase: 'wizard' });
      } else {
        setBoot(isMock ? { phase: 'shell', config: { ...defaultConfig(), paths: { ...defaultConfig().paths, data: await seedDemoData() } } } : { phase: 'wizard' });
      }
    })();
  }, [isMock]);

  const client = useMemo<ApiClientLike | null>(() => {
    if (boot.phase !== 'shell') return null;
    if (isMock) return createDemoClient();
    const c = new CareerApiClient(`ws://127.0.0.1:${boot.config.api_port}`, { token: boot.config.api_token });
    // tracks cmd.* transport failures to the support log (falls back to the Rust-side log if the WS is down)
    c.onIncident = (line) => supportLog(c, line);
    c.connect();
    return c;
  }, [boot, isMock]);
  // the safety net uses the live client (leaves a breadcrumb via cmd.log.note if the engine is reachable)
  useEffect(() => { setSafetyNetClient(() => client); }, [client]);

  // The headset's on-screen keyboard mounts HERE, at the root, not inside the shell. It used to be inside, which is why it
  // never showed up: AppShell passes through five gates before reaching its final JSX (driver card, profile
  // editor, calibration, end of season...), and exactly two of those gates are the screen where you type the
  // name. So the keyboard was missing on exactly the screens that have text fields.
  // Mounted here it covers everything: the startup wizard, the gates, and the shell.
  const vrOverlays = (
    <>
      {/* isVrLaunch, not !== 'flat' — 'dialog' mode (the Steam picker) doesn't arm the VR panels. */}
      <VrOnScreenKeyboard enabled={isVrLaunch(launchMode())} />
      {/* Keeps the headset panel's image alive. This is NOT decorative: without it, capture stops delivering
          new frames and the headset sees a stale image. See VrMirrorTick. */}
      <VrMirrorTick enabled={isVrLaunch(launchMode())} />
    </>
  );

  if (boot.phase === 'loading') {
    return <div className="col center" style={{ height: '100%' }} />;
  }
  if (boot.phase === 'wizard') {
    return (
      <>
        <OnboardingWizard
          onComplete={(cfg) => { initI18n(cfg.locale); setBoot({ phase: 'shell', config: cfg }); }}
        />
        {vrOverlays}
      </>
    );
  }
  const assetsNotice = assetsStale ? (
    <AssetsStaleNotice
      oldVersion={assetsStale.old}
      newVersion={assetsStale.now}
      onSettings={() => { dismissAssetsNotice(assetsStale.now); setAssetsStale(null); window.dispatchEvent(new CustomEvent(GOTO_TAB_EVENT, { detail: 'settings' })); }}
      onDismiss={() => { dismissAssetsNotice(assetsStale.now); setAssetsStale(null); }}
    />
  ) : null;
  return client ? <><AppShell client={client} config={boot.config} />{vrOverlays}{assetsNotice}</> : null;
}

export default App;
