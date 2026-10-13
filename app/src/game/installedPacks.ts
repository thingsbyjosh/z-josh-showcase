// T349 — Registry of livery packs installed by dragging in a file (Liveries.tsx, ).
// The engine knows NOTHING about this: cmd.skins.status detects against a curated catalog (known packs with
// url/author/season), not against loose packs the user installs by hand. Without this registry there was no
// way to know which files to delete on uninstall without risking touching those of ANOTHER pack (or the game's
// own). It lives in dataDir (the app's userdata), NEVER in the game install: it is our metadata, not something
// the game needs to read.
import { getBridge, joinPath, type InstallResult } from '../shell/bridge';

export interface InstalledPack {
  id: string;
  label: string;         // name of the installed file (no path), to identify it in the UI
  installedAt: number;   // Date.now() epoch ms
  cars: string[];        // slugs of the cars it brought (InstallResult.installedCars)
  files: string[];       // paths relative to Overrides (InstallResult.installedFiles) — what gets deleted on uninstall
}

const registryPath = (dataDir: string) => joinPath(dataDir, 'installed_skin_packs.json');

export async function readInstalledPacks(dataDir: string): Promise<InstalledPack[]> {
  const b = await getBridge();
  const p = registryPath(dataDir);
  if (!(await b.fs.exists(p))) return [];
  try {
    const parsed = JSON.parse(await b.fs.readTextFile(p));
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];   // corrupt file: treated as empty, never breaks the Liveries screen
  }
}

async function writeInstalledPacks(dataDir: string, packs: InstalledPack[]): Promise<void> {
  const b = await getBridge();
  await b.fs.writeTextFileAtomic(registryPath(dataDir), JSON.stringify(packs, null, 2) + '\n');
}

// Called right after an installArchive() with ok=true. archiveLabel = file name (to
// show in the list; the full path does not matter for identifying the pack to the user).
export async function recordInstalledPack(
  dataDir: string,
  archiveLabel: string,
  result: Pick<InstallResult, 'installedCars' | 'installedFiles'>,
): Promise<InstalledPack> {
  const entry: InstalledPack = {
    id: crypto.randomUUID(),
    label: archiveLabel,
    installedAt: Date.now(),
    cars: result.installedCars,
    files: result.installedFiles,
  };
  const packs = await readInstalledPacks(dataDir);
  packs.push(entry);
  await writeInstalledPacks(dataDir, packs);
  return entry;
}

// Removes ONLY the app manifest entry; deleting the files from the game install is
// bridge.skins.uninstallFiles(), which is called separately (see Liveries.tsx).
export async function forgetInstalledPack(dataDir: string, id: string): Promise<void> {
  const packs = await readInstalledPacks(dataDir);
  await writeInstalledPacks(dataDir, packs.filter((p) => p.id !== id));
}
