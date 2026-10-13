// This showcase does not include local-install detection (see the licence and README): the real
// steam.ts parses Steam's own libraryfolders.vdf to locate a local AMS2 install, which has no
// meaning without a local game install in this repository's scope. This stub keeps the same
// exported shape; every lookup reports "not found".
export interface SteamLibrary { path: string; appIds: string[] }

export function joinWin(...parts: string[]): string {
  return parts.join('\\');
}

export function parseLibraryFolders(_vdfText: string): SteamLibrary[] {
  return [];
}

export function findGameInstall(_vdfText: string, _appId: number): string | null {
  return null;
}

export function libraryFoldersVdfPath(steamRoot: string): string {
  return joinWin(steamRoot, 'steamapps', 'libraryfolders.vdf');
}
