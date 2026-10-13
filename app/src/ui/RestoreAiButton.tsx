// moved from Settings to the PLAY screen: restore the ORIGINAL XML files for
// the user's CustomAIDrivers. The engine backs them up before writing its own, and
// this undoes it: backups restored, and the mod-generated XML files deleted.
//
// Lives here and not in Settings because it's the "I want my files back" decision, and the only
// moment it can be made is with the game closed, which is exactly where this
// screen lives. Two steps (click, then confirm) because it overwrites files.
//
// Restores UNTIL the next launch: the Play button's pre-generation writes the mod's
// grids right back. That's stated in the copy for this text, which goes in the tooltip.
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { getBridge } from '../shell/bridge';
import { loadConfig } from '../config/appConfig';
import { supportLog } from '../support/supportLog';
import { Mono } from './components';

export function RestoreAiButton({ disabled, style }: { disabled?: boolean; style?: React.CSSProperties }) {
  const { t } = useTranslation();
  const [step, setStep] = useState<'idle' | 'confirm' | 'busy' | 'done' | 'error'>('idle');
  const [line, setLine] = useState('');
  const run = async () => {
    setStep('busy'); setLine('');
    try {
      const b = await getBridge();
      const cfg = await loadConfig();
      if (!cfg || !cfg.paths.gameInstall) { setStep('error'); setLine(t('gamefiles.noConfig')); return; }
      // With the game running its files are in use, and on top of that the engine would rewrite them on the fly.
      // The guard errs on caution: if it can't tell whether the game is running, nothing gets touched.
      // It used to be `.catch(() => false)`, which would restore with the game still running whenever the check failed.
      let running: boolean;
      try {
        running = await b.game.isRunning('AMS2AVX.exe');
      } catch (e) {
        setStep('error'); setLine(String(e)); return;
      }
      if (running) { setStep('error'); setLine(t('gamefiles.gameRunning')); return; }
      const r = await b.game.restoreCustomAi(cfg.paths.gameInstall, cfg.paths.data);
      setLine(t('gamefiles.done', { restored: r.restored, removed: r.removed }));
      setStep('done');
      supportLog(null, `customai: restore originals (restored=${r.restored} removed=${r.removed})`);   //
    } catch (e) {
      setLine(String(e)); setStep('error');
      supportLog(null, `customai: restore originals FAILED (${String(e)})`);
    }
  };
  return (
    <div className="row" style={{ gap: 6, alignItems: 'center', ...style }}>
      {step === 'confirm' ? (
        <>
          {/* The `disabled` flag ALSO goes here: the confirm step can stay open while the
               user launches the game, and what governs it is the CURRENT state, not the one from when they pressed the
               first button. Without this, confirming while the game is starting up sneaks past the
               isRunning guard (which still returns false) and overwrites the files while the game is half-open.
               The tooltip too: it explains that files get overwritten, and it used to vanish at exactly the moment
               of deciding. (No need here for the other button's `|| step === 'busy'`: with `busy` this
               branch doesn't even render.) */}
          <button className="btn btn-sm btn-ghost" title={t('gamefiles.desc')} disabled={disabled}
                  style={{ fontSize: 10, color: 'var(--amber)' }}
                  onClick={() => void run()}>
            {t('gamefiles.confirm')}
          </button>
          <button className="btn btn-sm btn-ghost" style={{ fontSize: 10, color: 'var(--text-3)' }}
                  onClick={() => { setStep('idle'); setLine(''); }}>
            {t('common.cancel')}
          </button>
        </>
      ) : (
        // Confirming again clears `line`: otherwise the previous attempt's error stays
        // stuck next to "Confirm restore" and reads as if it belonged to this attempt.
        <button className="btn btn-sm btn-ghost" title={t('gamefiles.desc')} disabled={disabled || step === 'busy'}
                style={{ fontSize: 10, color: 'var(--text-3)' }} onClick={() => { setStep('confirm'); setLine(''); }}>
          {t('gamefiles.button')}
        </button>
      )}
      {line && (
        <Mono style={{ fontSize: 10, maxWidth: 340, color: step === 'error' ? 'var(--danger)' : 'var(--text-2)' }}>
          {line}
        </Mono>
      )}
    </div>
  );
}
