import type { ShellBridge, DirEntry, ProvisionProgress, Pre070BackupStatus } from './bridge';
import { joinPath } from './bridge';

/** In-memory implementation for the browser (dev) and tests. */
export interface MockBridge extends ShellBridge {
  hiddenToTray: boolean;
  /** Seeds a file into the in-memory filesystem (for tests). */
  __seed(path: string, content: string): void;
  /** Dumps the in-memory filesystem (for assertions). */
  __files(): Map<string, string>;
  /** Marks whether "the game" is running (for game.isRunning tests). */
  __setRunning(running: boolean): void;
  /** How many times launchSteam was called (for assertions). */
  __launches(): number;
  /** Path that dialog.pickFolder will return (null by default = cancel). */
  __setPickedFolder(path: string | null): void;
  /** Path that avatar.pick will return (null by default = cancel). */
  __setPickedAvatar(path: string | null): void;
  /** Seeds the pending update that updater.check will return (null = no update available). */
  __setUpdate(u: { version: string; notes?: string } | null): void;
  /** File version that game.exeFileVersion will return (null = couldn't be read). */
  __setExeFileVersion(v: string | null): void;
  /** Code that oauth.waitCode will return (null by default = rejects with a timeout). */
  __setOauthCode(code: string | null): void;
  /** machineId that account.machineId will return (null by default). */
  __setMachineId(v: string | null): void;
  /** Log of URLs opened via opener.openUrl. */
  __openedUrls: string[];
  /** lines written by support.log (the support-log fallback). */
  __supportLines: string[];
  /** State that support.pre070BackupStatus() will return ('none' by default: nothing to show in Settings). */
  __setPre070BackupStatus(status: Pre070BackupStatus): void;
  /** Whether oauth.cancel() was called since the last listen() (for assertions). */
  __wasOauthCancelled(): boolean;
}

function normalize(p: string): string {
  return p.replace(/\//g, '\\').replace(/\\+/g, '\\').replace(/\\+$/, '');
}
function dirName(p: string): string {
  const n = normalize(p);
  const i = n.lastIndexOf('\\');
  return i > 0 ? n.slice(0, i) : n;
}
function baseName(p: string): string {
  const n = normalize(p);
  const i = n.lastIndexOf('\\');
  return i >= 0 ? n.slice(i + 1) : n;
}

export function createMockBridge(): MockBridge {
  let takeover = false;
  let running = false;
  let launches = 0;
  let pickedFolder: string | null = null;
  let pickedAvatar: string | null = null;
  let mockUpdate: { version: string; notes?: string } | null = null;
  let mockExeVersion: string | null = null;
  let mockOauthCode: string | null = null;
  let mockMachineId: string | null = null;
  let oauthCancelled = false;
  const files = new Map<string, string>(); // normalized path -> content
  const dirs = new Set<string>();           // directories created
  const openedUrls: string[] = [];
  const supportLines: string[] = [];   //
  let mockPre070BackupStatus: Pre070BackupStatus = { state: 'none', profiles: 0, backupDir: '' };
  const listeners: ((p: ProvisionProgress) => void)[] = [];
  const mockProgress: ProvisionProgress[] = [
    { phase: 'extract', pak: 'GUIVEHICLEIMAGES', done: 100, total: 0, fail: 0, message: '' },
    { phase: 'convert', pak: 'vehimages', done: 2, total: 2, fail: 0, message: '' },
    { phase: 'done', pak: '', done: 3, total: 3, fail: 0, message: '' },
  ];

  const mock: MockBridge = {
    hiddenToTray: false,
    env: { isTauri: false },

    __seed(path, content) {
      files.set(normalize(path), content);
      dirs.add(dirName(path));
    },
    __files: () => files,
    __setRunning(r) { running = r; },
    __launches: () => launches,
    __setPickedFolder(p) { pickedFolder = p; },
    __setPickedAvatar(p) { pickedAvatar = p; },
    __setUpdate(u) { mockUpdate = u; },
    __setExeFileVersion(v) { mockExeVersion = v; },
    __setOauthCode(c) { mockOauthCode = c; },
    __setMachineId(v) { mockMachineId = v; },
    __openedUrls: openedUrls,
    __supportLines: supportLines,
    __setPre070BackupStatus(status) { mockPre070BackupStatus = status; },
    __wasOauthCancelled: () => oauthCancelled,

    app: { version: async () => '0.0.0-dev' },
    updater: {
      check: async () => (mockUpdate ? { available: true, ...mockUpdate } : { available: false }),
      downloadAndInstall: async (onProgress) => {
        if (!mockUpdate) throw new Error('no update pending');
        onProgress?.(1024, 4096); onProgress?.(4096, 4096);
      },
      relaunch: async () => {},
    },

    window: {
      isTakeover: () => takeover,
      async enterTakeover() { takeover = true; },
      async exitTakeover() { takeover = false; },
      async hideToTray() { mock.hiddenToTray = true; },
      // minimizing does NOT change the mode (the app steps aside to let you drive, then comes back as it was): the real
      // bridge doesn't do it either, and removing this here was exactly what hid the window-mode regression.
      async minimize() { },
      async close() { /* browser: no-op */ },
      async focus() { /* browser: no-op */ },
      async restoreGeometry() { /* without a native window there's no geometry to remember */ },
      // in the browser there's no webview to ask for zoom -- and there's no need, because the browser's own
      // Ctrl +/- already does exactly this. Don't emulate it with CSS `zoom`: it clips the UI (see ShellBridge.window.setZoom).
      async setZoom() { /* browser: no-op */ },
    },

    fs: {
      async readTextFile(path) {
        const k = normalize(path);
        if (!files.has(k)) throw new Error(`ENOENT: ${path}`);
        return files.get(k)!;
      },
      async writeTextFileAtomic(path, content) {
        files.set(normalize(path), content);
        dirs.add(dirName(path));
      },
      async exists(path) {
        const k = normalize(path);
        return files.has(k) || dirs.has(k);
      },
      async copyFile(src, dst) {
        const s = normalize(src);
        if (!files.has(s)) throw new Error(`ENOENT: ${src}`);
        files.set(normalize(dst), files.get(s)!);
        dirs.add(dirName(dst));
      },
      async makeDir(path) {
        dirs.add(normalize(path));
      },
      async removeDir(path) {   // deletes everything under the prefix in memory (mock of a recursive delete)
        const base = normalize(path);
        const pref = base + '\\';
        for (const k of [...files.keys()]) if (k === base || k.startsWith(pref)) files.delete(k);
        for (const d of [...dirs]) if (d === base || d.startsWith(pref)) dirs.delete(d);
      },
      async listDir(path) {
        const base = normalize(path);
        const out: DirEntry[] = [];
        for (const [k, v] of files) {
          if (dirName(k) === base) {
            out.push({ name: baseName(k), isDir: false, sizeBytes: v.length, modifiedMs: 0 });
          }
        }
        // direct subdirectories (the real bridge does return these; profileSlots lists profile folders)
        for (const d of dirs) {
          if (d !== base && dirName(d) === base) {
            out.push({ name: baseName(d), isDir: true, sizeBytes: 0, modifiedMs: 0 });
          }
        }
        return out;
      },
      assetUrl() { return ''; },   // browser: no access to local files -> the UI falls back to a placeholder
    },

    paths: {
      async localAppData() { return 'C:\\Users\\Dev\\AppData\\Local'; },
      // Reflects the real machine (Documents redirected to another drive) instead of assuming the default path.
      async documents() { return 'G:\\Documents'; },
      async resourceDir() { return 'C:\\Program Files\\AMS2 Career\\resources'; },
      join: (...parts) => joinPath(...parts),
    },

    dialog: {
      async pickFolder() { return pickedFolder; },
    },

    avatar: {
      async pick() { return pickedAvatar; },
      async save(localAppData, pngBase64) {
        const path = joinPath(localAppData, 'Career Engine', 'avatar.png');
        files.set(normalize(path), pngBase64);
        dirs.add(dirName(path));
        return { ok: true, path, message: '' };
      },
      async remove(localAppData) {
        const path = normalize(joinPath(localAppData, 'Career Engine', 'avatar.png'));
        const had = files.delete(path);
        return { ok: true, path: '', message: had ? 'removed' : 'nothing to remove' };
      },
    },

    game: {
      async isRunning() { return running; },
      async launchSteam() { launches++; },
      async inject() { return { ok: true, code: 'ok', message: 'mock bridge' }; },
      // In the browser there's no game folder to touch, but the default
      // Play path runs through here: returning ok keeps the mock mode usable.
      async prepareGameFolder() { return { ok: true, code: 'ok', message: 'mock bridge' } as const; },
      async cleanGameFolder() { return { ok: true, code: 'nothing_to_remove', message: 'mock bridge' } as const; },
      async checkGameFolder() { return { exists: true, isOurs: true }; },
      async exeFileVersion() { return mockExeVersion; },
      async closeProcess() { running = false; },   // "closes" the mock game
      async restoreCustomAi() { return { restored: 0, removed: 0 }; },   // no-op in the browser
    },

    provision: {
      async syncStatic() { return { ok: true, copied: 0, skipped: true, message: 'mock' }; },
      async assetsStatus() { return { provisionedVersion: '', currentVersion: '', stale: false }; },
      async run() {
        for (const p of mockProgress) listeners.forEach((cb) => cb(p));
        return { ok: true, cancelled: false, vehOk: 2, vehFail: 0, trkOk: 1, trkFail: 0, skipped: 0, message: '' };
      },
      async cancel() { return false; },
      async onProgress(cb) { listeners.push(cb); return () => { const i = listeners.indexOf(cb); if (i >= 0) listeners.splice(i, 1); }; },
      async syncOverridePreviews() { return { ok: true, converted: 0, skipped: 0, failed: 0 }; },
    },

    skins: {
      async inspectArchive() { return { ok: false, cars: [], hasCustomAi: false, warnings: ['mock: sin extractor'] }; },
      async installArchive() { return { ok: false, installedCars: [], files: 0, skippedAi: false, installedFiles: [], warnings: ['mock: sin extractor'] }; },
      async uninstallFiles(_gameInstall, files) { return { ok: true, removed: files.length, missing: 0, warnings: [] }; },
    },
    async onFileDrop() { return () => {}; },
    vr: {
      async isAvailable() { return false; },   // no SteamVR in the browser/tests
      async start() {},
      async stop() {},
      async setOverlayVisible() {},
      async onPointer() { return () => {}; },
      async onOverrideToggle() { return () => {}; },
      async showKeyboard() {},
      async setConfirmKey() {},
      async setConfirmButton() {},
      async onKeyboardText() { return () => {}; },
      async onHostBlocked() { return () => {}; },
      async setPanelGeometry() {},
    },
    opener: {
      openUrl: async (url: string) => { openedUrls.push(url); },
      openPath: async (path: string) => { openedUrls.push(path); },   // same log as the URLs
    },
    support: {
      log: async (line: string) => { supportLines.push(line); },   //
      pre070BackupStatus: async () => mockPre070BackupStatus,   // review batch 0 (#ea77c402)
    },
    oauth: {
      listen: async () => { oauthCancelled = false; return 4242; },
      waitCode: async () => {
        if (mockOauthCode === null) throw new Error('timed out waiting for the Patreon callback');
        return mockOauthCode;
      },
      cancel: async () => { oauthCancelled = true; },
    },
    account: { machineId: async () => mockMachineId },
  };
  return mock;
}
