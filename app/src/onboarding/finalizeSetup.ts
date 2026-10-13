// Wizard completion: builds the AppConfig from state and persists it; legacy save import.
import { getBridge, managedDataRoot, BRAND_DIR } from '../shell/bridge';
import { saveConfig, defaultConfig, type AppConfig } from '../config/appConfig';
import type { WizardState } from './useWizard';

export type CareerMode = 'new' | 'import' | 'explore';

export async function finalizeSetup(w: WizardState, _mode: CareerMode): Promise<AppConfig> {
  const b = await getBridge();
  const cfg = defaultConfig();
  cfg.locale = w.locale;
  cfg.paths = {
    data: managedDataRoot(await b.paths.localAppData()),
    userdata: b.paths.join(await b.paths.localAppData(), BRAND_DIR),
    gameInstall: w.gameInstall.path,
    gameUserDir: w.gameUserDir.path,
  };
  await saveConfig(cfg);
  return cfg;
}

// Copies a chosen save to userdata/saves/career.sav (the engine validates the version on load).
export async function importSave(srcPath: string): Promise<{ ok: boolean; message: string }> {
  const b = await getBridge();
  if (!(await b.fs.exists(srcPath))) {
    return { ok: false, message: 'firstrun.importInvalid' };
  }
  const dst = b.paths.join(await b.paths.localAppData(), BRAND_DIR, 'saves', 'career.sav');
  try {
    await b.fs.copyFile(srcPath, dst);
    return { ok: true, message: '' };
  } catch (e) {
    return { ok: false, message: String(e) };
  }
}
