// Wizard path detection: AMS2 install location (Steam VDF) + user data folder.
// Pure over shellBridge; the parsers (steam.ts) do the format-specific work.
import { getBridge } from '../shell/bridge';
import { findGameInstall, libraryFoldersVdfPath } from '../detect/steam';

export const AMS2_APPID = 1066890;

export interface DetectResult {
  path: string;
  status: 'found_auto' | 'not_found';
}

const STEAM_ROOTS = ['C:\\Program Files (x86)\\Steam', 'C:\\Program Files\\Steam'];

export async function detectGameInstall(): Promise<DetectResult> {
  const b = await getBridge();
  for (const root of STEAM_ROOTS) {
    const vdf = libraryFoldersVdfPath(root);
    if (!(await b.fs.exists(vdf))) continue;
    try {
      const install = findGameInstall(await b.fs.readTextFile(vdf), AMS2_APPID);
      if (install && (await b.fs.exists(b.paths.join(install, 'AMS2AVX.exe')))) {
        return { path: install, status: 'found_auto' };
      }
    } catch {
      /* next candidate */
    }
  }
  return { path: '', status: 'not_found' };
}

export async function detectGameUserDir(): Promise<DetectResult> {
  const b = await getBridge();
  const dir = b.paths.join(await b.paths.documents(), 'Automobilista 2');
  if (await b.fs.exists(b.paths.join(dir, 'graphicsconfigdx11.xml'))) {
    return { path: dir, status: 'found_auto' };
  }
  return { path: '', status: 'not_found' };
}

export async function validateGameInstall(path: string): Promise<boolean> {
  const b = await getBridge();
  return b.fs.exists(b.paths.join(path, 'AMS2AVX.exe'));
}

export async function validateGameUserDir(path: string): Promise<boolean> {
  const b = await getBridge();
  return b.fs.exists(b.paths.join(path, 'graphicsconfigdx11.xml'));
}
