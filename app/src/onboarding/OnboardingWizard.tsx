// First-run wizard: 5 steps (welcome+language / detection / display / assets / career).
// Styled per the design system; functionality and ordering take priority (project rule).
import { useCallback, useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { getBridge, managedDataRoot } from '../shell/bridge';
import { Panel, Btn, Chip, Kicker, Display, Mono, StepDots } from '../ui/components';
import { BrandLogo } from '../ui/BrandLogo';
import { useWizard, WIZARD_ORDER, type RootStatus } from './useWizard';
import { LocalePicker } from '../shell/LocalePicker';
import {
  detectGameInstall,
  detectGameUserDir,
  validateGameInstall,
  validateGameUserDir,
} from './detectRoots';
import { readDisplayMode, fixBorderless } from './fixBorderless';
import { finalizeSetup, importSave, type CareerMode } from './finalizeSetup';
import type { AppConfig } from '../config/appConfig';

export interface OnboardingWizardProps {
  onComplete: (cfg: AppConfig, mode: CareerMode) => void;
}

function StatusPill({ status, t }: { status: RootStatus; t: (k: string) => string }) {
  if (status === 'found_auto' || status === 'found_manual') return <Chip kind="green">✓ {t('firstrun.found')}</Chip>;
  if (status === 'validating') return <Chip>{t('firstrun.detecting')}</Chip>;
  if (status === 'not_found') return <Chip kind="lock">✕ {t('firstrun.notFound')}</Chip>;
  return <Chip>—</Chip>;
}

function AssetsStep({ gameInstall, status, onStatus }: {
  gameInstall: string;
  status: 'idle' | 'running' | 'done' | 'error';
  onStatus: (s: 'idle' | 'running' | 'done' | 'error') => void;
}) {
  const { t } = useTranslation();
  const [line, setLine] = useState('');
  const [pct, setPct] = useState(0);
  const [errMsg, setErrMsg] = useState('');

  const start = async () => {
    const b = await getBridge();
    onStatus('running');
    setErrMsg('');
    const root = managedDataRoot(await b.paths.localAppData());
    const un = await b.provision.onProgress((p) => {
      if (p.phase === 'extract') { setLine(t('firstrun.assetsExtract', { pak: p.pak, n: p.done })); setPct(5); }
      if (p.phase === 'convert') {
        setLine(t('firstrun.assetsConvert', { pak: p.pak, done: p.done, total: p.total }));
        if (p.total > 0) setPct(10 + Math.round((p.done / p.total) * 90));
      }
    });
    try {
      await b.provision.syncStatic(root);
      const r = await b.provision.run(gameInstall, root);
      if (r.cancelled) { onStatus('idle'); setLine(''); setPct(0); }
      else {
        setLine(t('firstrun.assetsDone', { ok: r.vehOk + r.trkOk, fail: r.vehFail + r.trkFail, skipped: r.skipped }));
        setPct(100); onStatus('done');
      }
    } catch (e) {
      const m = String(e);
      // dotnet_missing: the .exe DOES start, and it's its .NET host that aborts, so it doesn't arrive as
      // 'spawn:'. Without this branch the user saw the host's raw dump (paths and architectures).
      // pak_tool_corrupt: the extractor's .exe is damaged and its host aborts before running
      // anything. The raw dump ("Bundle header version compatibility check failed") tells the
      // player nothing useful; the only actionable advice is reinstall and check the antivirus.
      setErrMsg(m.startsWith('spawn:') || m === 'dotnet_missing' ? t('firstrun.assetsDotnet')
        : m === 'disk_space' ? t('firstrun.assetsDisk')
        : m.includes('pak_tool_corrupt') ? t('firstrun.assetsToolCorrupt')
        : t('firstrun.assetsError', { msg: m }));
      onStatus('error');
    } finally { un(); }
  };

  return (
    <div className="col gap16">
      <Display style={{ fontSize: 34 }}>{t('firstrun.assetsTitle')}</Display>
      <p style={{ color: 'var(--text-2)', lineHeight: 1.5, fontSize: 15 }}>{t('firstrun.assetsBody')}</p>
      {(status === 'idle' || status === 'error') && (
        <div className="row gap12">
          <Btn variant="primary" onClick={() => void start()}>
            {status === 'error' ? t('firstrun.assetsRetry') : t('firstrun.assetsStart')}
          </Btn>
        </div>
      )}
      {status === 'running' && (
        <div className="col gap8">
          <div className="bar" style={{ height: 6 }}><i style={{ width: `${pct}%` }} /></div>
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{line || t('firstrun.assetsRunning')}</Mono>
          <div className="row"><Btn variant="ghost" size="sm" onClick={() => void getBridge().then((b) => b.provision.cancel())}>{t('firstrun.assetsCancel')}</Btn></div>
        </div>
      )}
      {status === 'done' && <Mono style={{ fontSize: 12, color: 'var(--green)' }}>{line}</Mono>}
      {status === 'error' && <Mono style={{ fontSize: 12, color: 'var(--danger)' }}>{errMsg}</Mono>}
    </div>
  );
}

export function OnboardingWizard({ onComplete }: OnboardingWizardProps) {
  const { t } = useTranslation();
  const { state, patch, setLocale, setAssetsStatus, canAdvance, next, back } = useWizard();
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');

  // Step 2: automatic detection on entry.
  useEffect(() => {
    if (state.step !== 'detect') return;
    let alive = true;
    (async () => {
      patch({ gameInstall: { path: '', status: 'validating' }, gameUserDir: { path: '', status: 'validating' } });
      const [gi, gu] = await Promise.all([detectGameInstall(), detectGameUserDir()]);
      if (!alive) return;
      patch({ gameInstall: gi, gameUserDir: gu });
    })();
    return () => { alive = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [state.step]);

  // Step 3: read the window mode on entry.
  useEffect(() => {
    if (state.step !== 'display' || !state.gameUserDir.path) return;
    let alive = true;
    (async () => {
      const b = await getBridge();
      const running = await b.game.isRunning('AMS2AVX.exe');
      const mode = await readDisplayMode(state.gameUserDir.path);
      if (alive) patch({ windowedMode: mode, gameRunning: running });
    })();
    return () => { alive = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [state.step, state.gameUserDir.path]);

  const pickInstall = useCallback(async () => {
    const b = await getBridge();
    const p = await b.dialog.pickFolder(t('firstrun.detectInstall'));
    if (!p) return;
    patch({ gameInstall: { path: p, status: (await validateGameInstall(p)) ? 'found_manual' : 'not_found' } });
  }, [patch, t]);

  const pickUserDir = useCallback(async () => {
    const b = await getBridge();
    const p = await b.dialog.pickFolder(t('firstrun.detectUserDir'));
    if (!p) return;
    patch({ gameUserDir: { path: p, status: (await validateGameUserDir(p)) ? 'found_manual' : 'not_found' } });
  }, [patch, t]);

  const doFix = useCallback(async () => {
    setBusy(true); setErr('');
    const r = await fixBorderless(state.gameUserDir.path);
    if (r.ok) patch({ windowedMode: 2 });
    else setErr(r.message.startsWith('firstrun.') ? t(r.message) : r.message);
    setBusy(false);
  }, [state.gameUserDir.path, patch, t]);

  const recheck = useCallback(async () => {
    const b = await getBridge();
    const running = await b.game.isRunning('AMS2AVX.exe');
    patch({ windowedMode: await readDisplayMode(state.gameUserDir.path), gameRunning: running });
  }, [state.gameUserDir.path, patch]);

  const finish = useCallback(async (mode: CareerMode, savePath?: string) => {
    setBusy(true); setErr('');
    try {
      if (mode === 'import' && savePath) {
        const r = await importSave(savePath);
        if (!r.ok) { setErr(t(r.message)); setBusy(false); return; }
      }
      const cfg = await finalizeSetup(state, mode);
      onComplete(cfg, mode);
    } catch (e) {
      setErr(`${t('firstrun.writeError')}: ${String(e)}`);
      setBusy(false);
    }
  }, [state, onComplete, t]);

  const pickAndImport = useCallback(async () => {
    const b = await getBridge();
    const p = await b.dialog.pickFolder(t('firstrun.importSave'));
    if (p) void finish('import', b.paths.join(p, 'career.sav'));
  }, [finish, t]);

  const stepIdx = WIZARD_ORDER.indexOf(state.step);

  return (
    <div className="screen" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div className="app-bg" />
      <div className="row between" style={{ padding: '22px 34px 8px', alignItems: 'center', zIndex: 1 }}>
        <div className="row gap12" style={{ alignItems: 'center' }}>
          <div className="mark" />
          <BrandLogo height={24} />
          <Kicker style={{ marginLeft: 2 }}>{t('firstrun.title')}</Kicker>
        </div>
        <div className="row gap12" style={{ alignItems: 'center' }}>
          <Mono style={{ fontSize: 11, color: 'var(--text-3)' }}>{t('firstrun.step', { n: stepIdx + 1, total: WIZARD_ORDER.length })}</Mono>
          <StepDots total={WIZARD_ORDER.length} current={stepIdx} />
        </div>
      </div>

      <div className="col center grow" style={{ padding: 34, zIndex: 1, justifyContent: 'center' }}>
        <Panel style={{ width: '100%', maxWidth: 720, padding: '30px 34px' }}>
          {/* STEP 1 -- welcome + language */}
          {state.step === 'welcome' && (
            <div className="col gap16">
              <Display style={{ fontSize: 34 }}>{t('firstrun.welcomeTitle')}</Display>
              <p style={{ color: 'var(--text-2)', lineHeight: 1.5, fontSize: 15 }}>{t('firstrun.welcomeBody')}</p>
              <div className="col gap8" style={{ marginTop: 8 }}>
                <Kicker>{t('firstrun.language')}</Kicker>
                {/* There were TEN buttons wrapping across the panel — the first screen a new
                    player sees, and it looked like a keyboard. With two languages a couple of buttons was
                    fine; with ten, this is a list. */}
                <LocalePicker value={state.locale}
                              onPick={setLocale}
                              label={t('firstrun.language')} />
              </div>
            </div>
          )}

          {/* STEP 2 -- detection */}
          {state.step === 'detect' && (
            <div className="col gap16">
              <Display style={{ fontSize: 28 }}>{t('firstrun.detectTitle')}</Display>
              {([
                ['detectInstall', state.gameInstall, pickInstall, 'needAms2Exe'],
                ['detectUserDir', state.gameUserDir, pickUserDir, 'needGraphicsXml'],
              ] as const).map(([labelKey, root, pick, errKey]) => (
                <div key={labelKey} className="row between" style={{ alignItems: 'center', gap: 14 }}>
                  <div className="col" style={{ gap: 3, minWidth: 0, flex: 1 }}>
                    <span style={{ fontWeight: 600, fontSize: 14 }}>{t(`firstrun.${labelKey}`)}</span>
                    <Mono style={{ fontSize: 11, color: 'var(--text-3)', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                      {root.path || (root.status === 'not_found' ? t(`firstrun.${errKey}`) : '—')}
                    </Mono>
                  </div>
                  <div className="row gap8" style={{ alignItems: 'center', flex: 'none' }}>
                    <StatusPill status={root.status} t={t} />
                    <Btn size="sm" variant="ghost" onClick={pick}>{t('firstrun.chooseFolder')}</Btn>
                  </div>
                </div>
              ))}
            </div>
          )}

          {/* STEP 3 -- display */}
          {state.step === 'display' && (
            <div className="col gap16">
              <Display style={{ fontSize: 28 }}>{t('firstrun.displayTitle')}</Display>
              <p style={{ color: 'var(--text-2)', lineHeight: 1.5, fontSize: 14 }}>{t('firstrun.displayWhy')}</p>
              {state.windowedMode === 2 ? (
                <Chip kind="green" style={{ alignSelf: 'flex-start' }}>✓ {t('firstrun.displayOk')}</Chip>
              ) : (
                <div className="col gap12">
                  <Chip kind="lock" style={{ alignSelf: 'flex-start' }}>{t('firstrun.displayNeedFix')}</Chip>
                  {state.gameRunning && <Mono style={{ fontSize: 12, color: 'var(--amber)' }}>⚠ {t('firstrun.gameRunning')}</Mono>}
                  <div className="row gap12">
                    <Btn variant="primary" disabled={busy || state.gameRunning} onClick={doFix}>{t('firstrun.fixForMe')}</Btn>
                    <Btn variant="ghost" onClick={recheck}>{t('firstrun.recheck')}</Btn>
                  </div>
                </div>
              )}
            </div>
          )}

          {/* STEP 4 -- assets */}
          {state.step === 'assets' && (
            <AssetsStep
              gameInstall={state.gameInstall.path}
              status={state.assetsStatus}
              onStatus={setAssetsStatus}
            />
          )}

          {/* STEP 5 -- career */}
          {state.step === 'career' && (
            <div className="col gap16">
              <Display style={{ fontSize: 28 }}>{t('firstrun.careerTitle')}</Display>
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 12 }}>
                {([
                  ['newCareer', 'newCareerDesc', () => finish('new')],
                  ['importSave', 'importSaveDesc', pickAndImport],
                  ['explore', 'exploreDesc', () => finish('explore')],
                ] as const).map(([titleKey, descKey, action]) => (
                  <button key={titleKey} className="panel" disabled={busy}
                    onClick={action}
                    style={{ padding: '18px 16px', textAlign: 'left', cursor: 'pointer', display: 'flex', flexDirection: 'column', gap: 8 }}>
                    <span className="display" style={{ fontSize: 18 }}>{t(`firstrun.${titleKey}`)}</span>
                    <Mono style={{ fontSize: 11, color: 'var(--text-3)', lineHeight: 1.4 }}>{t(`firstrun.${descKey}`)}</Mono>
                  </button>
                ))}
              </div>
            </div>
          )}

          {err && <Mono style={{ fontSize: 12, color: 'var(--danger)', marginTop: 14, display: 'block' }}>{err}</Mono>}
        </Panel>
      </div>

      {/* Back/Continue bar (step 5 advances through the cards) */}
      <div className="row between" style={{ padding: '12px 34px 26px', zIndex: 1 }}>
        <Btn variant="ghost" disabled={stepIdx === 0 || (state.step === 'assets' && state.assetsStatus === 'running')} onClick={back}>← {t('common.back')}</Btn>
        {state.step !== 'career' && (
          <Btn variant="primary" disabled={!canAdvance()} onClick={next}>{t('common.continue')} →</Btn>
        )}
      </div>
    </div>
  );
}
