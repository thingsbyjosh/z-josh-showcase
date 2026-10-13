// Wizard step 3: read/fix the window mode (borderless=2) with a .bak backup.
import { getBridge } from '../shell/bridge';
import { readWindowedMode, setWindowedMode } from '../detect/graphicsConfig';

const XML = 'graphicsconfigdx11.xml';

export async function readDisplayMode(gameUserDir: string): Promise<number | null> {
  const b = await getBridge();
  const path = b.paths.join(gameUserDir, XML);
  if (!(await b.fs.exists(path))) return null;
  return readWindowedMode(await b.fs.readTextFile(path));
}

// Writes Windowed=2 with a .bak backup. Refuses if the game is running (that would block the edit).
export async function fixBorderless(gameUserDir: string): Promise<{ ok: boolean; message: string }> {
  const b = await getBridge();
  if (await b.game.isRunning('AMS2AVX.exe')) {
    return { ok: false, message: 'firstrun.gameRunning' };
  }
  const path = b.paths.join(gameUserDir, XML);
  try {
    const orig = await b.fs.readTextFile(path);
    await b.fs.copyFile(path, `${path}.bak`);
    await b.fs.writeTextFileAtomic(path, setWindowedMode(orig, 2));
    return { ok: true, message: '' };
  } catch (e) {
    return { ok: false, message: String(e) };
  }
}
