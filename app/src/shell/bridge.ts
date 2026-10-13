// ShellBridge: the UI's single gateway into the OS (window, processes, tray).
// Project rule: the React UI NEVER imports Tauri APIs directly;
// that way the shell stays swappable and the UI runs in a plain browser during dev.

import type { VrPointerEvent } from '../vr/pointerDispatch';

export interface DirEntry {
  name: string;
  isDir: boolean;
  sizeBytes: number;
  modifiedMs: number;
}

export interface ProvisionProgress {
  phase: 'extract' | 'convert' | 'done' | 'error';
  pak: string;
  done: number;
  total: number;
  fail: number;
  message: string;
}

export interface SyncResult {
  ok: boolean;
  copied: number;
  skipped: boolean;
  message: string;
}

// steam://run/<id> ALWAYS launches the default option (flat). For VR
// Steam exposes steam://launch/<id>/vr (SteamVR) and /othervr (Oculus); the user picks the mode in Settings.
// 'dialog' opens Steam's own launch-option SELECTOR (steam://launch/<id>/dialog) -- the only
// way to pick the game's OpenXR mode, which motion-compensation rigs (OpenXR Motion Compensation) need.
export type SteamLaunchMode = 'flat' | 'steamvr' | 'oculusvr' | 'dialog';

export type InjectErrorCode =
  | 'ok' | 'policy_denied' | 'game_not_running' | 'bridge_unavailable'
  | 'dll_missing' | 'dll_invalid' | 'dll_identity_mismatch' | 'access_denied'
  | 'launch_prepare_failed' | 'launch_transfer_failed' | 'launch_setup_failed'
  | 'launch_step_failed' | 'launch_timed_out' | 'engine_load_failed'
  | 'internal_error' | 'unknown';

export interface InjectResult {
  ok: boolean;
  code: InjectErrorCode;
  message: string;
}

// The default way of loading the overlay, put in place before the game starts. It replaces
// the older path, which is now only used to attach to a game that's already running.
export type GameFolderCode =
  | 'ok' | 'removed' | 'nothing_to_remove'
  | 'game_path_missing' | 'overlay_resource_missing'
  | 'backup_failed' | 'copy_failed' | 'remove_failed';

export interface GameFolderResult {
  ok: boolean;
  code: GameFolderCode;
  message: string;
}

// T371: diagnostic-only read (does not touch disk) to know whether the overlay file is still in the
// game folder and whether it is ours, without going through prepareGameFolder/cleanGameFolder.
export interface GameFolderStatus {
  exists: boolean;
  isOurs: boolean;
}

export interface ProvisionResult {
  ok: boolean;
  cancelled: boolean;
  vehOk: number;
  vehFail: number;
  trkOk: number;
  trkFail: number;
  skipped: number;
  message: string;
}

// converting previews of installed skins (DDS->PNG). One slot = one detected override.
export interface OverridePreviewSlot { slug: string; livery_id: number; preview_rel: string }
export interface PreviewSyncResult { ok: boolean; converted: number; skipped: number; failed: number }

// installing skins from a file (zip/rar/7z) dropped onto the app.
export interface SkinLiveryInfo { id: number; name: string }
export interface SkinCarInfo { slug: string; liveries: SkinLiveryInfo[] }
export interface ArchiveInfo { ok: boolean; cars: SkinCarInfo[]; hasCustomAi: boolean; warnings: string[] }
// T349: installedFiles are paths relative to Overrides (e.g. "brackenridge_m4_gt3/brackenridge_m4_gt3.xml") — the app
// saves them to be able to uninstall THIS pack later without touching what another one has written.
export interface InstallResult { ok: boolean; installedCars: string[]; files: number; skippedAi: boolean; installedFiles: string[]; warnings: string[] }
export interface UninstallResult { ok: boolean; removed: number; missing: number; warnings: string[] }
// Tauri's native drop event (dragDropEnabled). 'over' fires continuously; 'drop' carries the final paths.
export interface FileDropEvent { kind: 'enter' | 'over' | 'drop' | 'leave'; paths: string[] }

export interface UpdateCheckResult { available: boolean; version?: string; notes?: string }

// the result of restoring the original CustomAIDrivers XML files.
export interface RestoreAiResult { restored: number; removed: number }

// Profile photo: `path` is only filled in on save() with ok=true (the final path of the saved PNG).
export interface AvatarResult { ok: boolean; path: string; message: string }

export interface AssetsStatus {
  /** AMS2 build with which the assets were generated ('' if never stamped). */
  provisionedVersion: string;
  /** AMS2 build installed now ('' if it could not be read). */
  currentVersion: string;
  /** true = the game changed since they were generated -> suggest regenerating. */
  stale: boolean;
}

// Review batch 0 (#ea77c402): real state of the one-time backup of the races prior to the
// 0.7.0 migration (pre070_backup.rs). 'none' covers both "installation prior to 0.7.0, not yet
// started with it" and "retries in progress, not yet at a final state" — in both cases
// Settings must show nothing.
export interface Pre070BackupStatus {
  state: 'done' | 'failed' | 'none';
  /** number of profiles with a full copy. Only meaningful if state === 'done'. */
  profiles: number;
  /** Full path to backups\pre-0.7.0. Only meaningful if state === 'done'. */
  backupDir: string;
}

export interface ShellBridge {
  window: {
    /** Management mode: fullscreen (borderless) covering the monitor + focus. NO always-on-top (so the user
     *  can always step out to see the game). Idempotent (includes unminimize+show). Fullscreen avoids clipping against the taskbar. */
    enterTakeover(): Promise<void>;
    /** Window mode: exits fullscreen and restores the normal window (borders + previous size). */
    exitTakeover(): Promise<void>;
    hideToTray(): Promise<void>;
    isTakeover(): boolean;
    /** Minimize to the taskbar (manual button + auto-switch while driving). */
    minimize(): Promise<void>;
    /** Close the app. */
    close(): Promise<void>;
    /** Bring the app to the front RIGHT NOW (unminimize + show + focus). */
    focus(): Promise<void>;
    /** (requested by a user with a triple-screen Nvidia Surround setup plus a fourth monitor above): the app
     *  used to ALWAYS open on the left monitor and had to be dragged every time. Restores position/size/maximized
     *  state from the last session and installs auto-save going forward (move/resize/close). Idempotent.
     *  Validates the geometry against the monitors that are actually PRESENT: if the monitor it was on is gone (or it would land
     *  off-screen), nothing is touched and the OS decides. Takeover mode doesn't count as geometry. */
    restoreGeometry(): Promise<void>;
    /** UI scale. NATIVE webview zoom (the same thing as the browser's Ctrl+=): the
     *  content is drawn bigger and the CSS viewport shrinks by the same proportion, so the UI still
     *  fits EXACTLY in the window, which never resizes. Do NOT use CSS `zoom` on the root: that enlarges the
     *  canvas (#root measures 100vw/100vh) and pushes it outside the window, which `overflow:hidden` then CLIPS
     *  (a real bug: at 125% Settings' own scale buttons ended up off-screen and there was no way back).
     *  factor 1 = 100%. */
    setZoom(factor: number): Promise<void>;
  };
  /** File I/O: the UI never touches the filesystem directly; everything goes through here. */
  fs: {
    readTextFile(path: string): Promise<string>;
    /** Atomic write: writes to a temp file and renames it (avoids half-written files). */
    writeTextFileAtomic(path: string, content: string): Promise<void>;
    exists(path: string): Promise<boolean>;
    copyFile(src: string, dst: string): Promise<void>;
    /** Creates the directory and its parents (mkdir -p). Doesn't fail if it already exists. */
    makeDir(path: string): Promise<void>;
    /** recursively deletes a directory. GUARD: only inside a `profiles/` folder (used for deleting
     *  profiles from the Play window). Idempotent (fine if it doesn't exist). */
    removeDir(path: string): Promise<void>;
    listDir(path: string): Promise<DirEntry[]>;
    /** Converts an ABSOLUTE local path into a URL the webview can load (Tauri's asset protocol).
     *  In the browser (mock) it returns '' (no local file access) -> the UI falls back to a placeholder. */
    assetUrl(path: string): string;
  };
  /** OS root folders. documents() ALWAYS goes through the known-folder API (never a literal absolute path). */
  paths: {
    localAppData(): Promise<string>;   // %LOCALAPPDATA%
    documents(): Promise<string>;      // FOLDERID_Documents (can be redirected, e.g. to a different drive)
    resourceDir(): Promise<string>;    // the app's install resources (the data/ root)
    join(...parts: string[]): string;  // synchronous join with '\'
  };
  /** Native dialogs: folder picker for the wizard. null if the user cancels. */
  dialog: {
    pickFolder(title: string): Promise<string | null>;
  };
  /** Profile photo (thread "Custom Avatar picture"): choose an image from disk and save it — already
   *  cropped to a square by the front end (canvas) — in the app's data folder. NEVER in the
   *  game's userdata: it is the app's identity, not the career's. */
  avatar: {
    /** Native image picker (png/jpg/jpeg/webp/bmp). null = the user cancelled. */
    pick(title: string): Promise<string | null>;
    /** `pngBase64` WITHOUT the "data:image/...;base64," prefix. Atomic write. */
    save(localAppData: string, pngBase64: string): Promise<AvatarResult>;
    /** Deletes the saved photo, if there is one. Idempotent. */
    remove(localAppData: string): Promise<AvatarResult>;
  };
  /** Game bridge: detect the process, launch via Steam, and start the narrow-purpose game bridge. */
  game: {
    isRunning(processName: string): Promise<boolean>;      // the game's executable name
    launchSteam(appId: number, mode?: SteamLaunchMode): Promise<void>;   // no mode = 'flat' (steam://run)
    inject(): Promise<InjectResult>;
    /** Puts the overlay in the game's own folder BEFORE launching, so the
     *  game loads it on its own at startup. A pre-existing, unrelated file of the same name
     *  gets moved aside to a backup extension, never overwritten. */
    prepareGameFolder(): Promise<GameFolderResult>;
    /** Removes it, and only if it's ours. Restores the backup if there was one. */
    cleanGameFolder(): Promise<GameFolderResult>;
    /** T371: diagnostic-only read — is the overlay file still in the game folder, and is it
     *  ours? Does not touch disk. Used right before launching and on declaring detected_unlinked,
     *  to distinguish "removed since" from "it is there but did not load". */
    checkGameFolder(): Promise<GameFolderStatus>;
    /** File version of the game's executable. null = couldn't be read (fail-open). */
    exeFileVersion(path: string): Promise<string | null>;
    /** closes the game process (Stop-Process -Force). Best-effort. */
    closeProcess(processName: string): Promise<void>;
    /** restores the ORIGINAL CustomAIDrivers XML files (undoes the engine's own backup
     *  files and deletes the XML files the mod generated where the user had no original). Game must be closed. */
    restoreCustomAi(gameInstall: string, dataRoot: string): Promise<RestoreAiResult>;
  };
  /** First-run provisioning: static-content sync + extraction/conversion of the game's assets. */
  provision: {
    syncStatic(dataRoot: string): Promise<SyncResult>;
    run(gameInstall: string, dataRoot: string): Promise<ProvisionResult>;   // rejects with a Rust error string
    cancel(): Promise<boolean>;
    onProgress(cb: (p: ProvisionProgress) => void): Promise<() => void>;   // returns an unsubscribe function
    /** converts installed skins' preview images to PNG (background, idempotent). */
    syncOverridePreviews(gameInstall: string, dataRoot: string, slots: OverridePreviewSlot[]): Promise<PreviewSyncResult>;
    /** Were the assets generated with ANOTHER AMS2 build? (12/09) If the game adds cars, the new one shows up
     *  without a photo until regenerated. Compares by VERSION, not by folder inventory: there are slugs that
     *  legitimately have no folder and would give a false positive. */
    assetsStatus(gameInstall: string, dataRoot: string): Promise<AssetsStatus>;
  };
  /** installing skins from a file (zip/rar/7z). */
  skins: {
    /** Previews which cars/liveries the file contains (does NOT write to the game). */
    inspectArchive(archive: string): Promise<ArchiveInfo>;
    /** Installs ONLY the Overrides tree into the game install (path-safe, no CustomAIDrivers). */
    installArchive(archive: string, gameInstall: string): Promise<InstallResult>;
    /** T349: deletes exactly `files` (relative to Overrides, from the manifest saved at install time) and
     *  cleans up any car folders left empty. Does NOT restore anything: install_at never kept a copy
     *  of what it overwrote, so the car goes back to the game's factory livery. */
    uninstallFiles(gameInstall: string, files: string[]): Promise<UninstallResult>;
  };
  /** Native file drop onto the window. Returns unsubscribe. Mock: no-op. */
  onFileDrop(cb: (e: FileDropEvent) => void): Promise<() => void>;
  /** in-headset overlay panel (SteamVR). Everything is a no-op if SteamVR isn't present. */
  vr: {
    isAvailable(): Promise<boolean>;                                   // SteamVR present
    start(): Promise<void>;                                            // starts the overlay module
    stop(): Promise<void>;
    setOverlayVisible(visible: boolean): Promise<void>;               // expand / collapse
    onPointer(cb: (e: VrPointerEvent) => void): Promise<() => void>;   // controller laser or mouse
    onOverrideToggle(cb: () => void): Promise<() => void>;             // manual show/hide trigger
    showKeyboard(): Promise<void>;                                     // requests the VR system's on-screen keyboard
    setConfirmKey(vk: number): Promise<void>;                          // click key for gaze pointing
    setConfirmButton(spec: string): Promise<void>;                     // wheel/controller button ("joy:.."/"xinput:.."; '' = key only)
    onKeyboardText(cb: (text: string) => void): Promise<() => void>;  // confirmed text from the VR keyboard
    onHostBlocked(cb: (reason: string) => void): Promise<() => void>; // the VR host can NOT start (e.g. an OpenComposite runtime, which would abort the process) -- reason goes to the support log
    setPanelGeometry(widthM: number, distM: number): Promise<void>;   // panel size/distance (meters), applied LIVE by the host
  };
  env: { isTauri: boolean };
  /** Installed app version (tauri.conf.json). Mock: '0.0.0-dev'. */
  app: { version(): Promise<string> };
  /** Auto-update: checks against a signed latest.json; install downloads+installs; relaunch restarts. */
  updater: {
    check(): Promise<UpdateCheckResult>;
    /** Downloads and installs the pending update from the last check(). onProgress: bytes downloaded / total (if known). */
    downloadAndInstall(onProgress?: (downloaded: number, total?: number) => void): Promise<void>;
    relaunch(): Promise<void>;
  };
  /** Opens a URL in the system browser (Patreon OAuth); openPath opens a local FOLDER in the
   *  file explorer, creating it if it's missing. */
  opener: { openUrl(url: string): Promise<void>; openPath(path: string): Promise<void> };
  /** support-log fallback -- appends a single [app] line directly to the one shared file
   *  (for operations without the engine: game closed, skins, assets, account). Use it via supportLog(), not directly. */
  support: {
    log(line: string): Promise<void>;
    /** Review batch 0 (#ea77c402): REAL state of the one-time backup prior to 0.7.0 (reads
     *  the marks written by pre070_backup.rs), so that Settings stops showing the "a copy was
     *  saved" notice unconditionally. 'none' = nothing to show yet (retries in progress, or
     *  a startup prior to the first one of 0.7.0). */
    pre070BackupStatus(): Promise<Pre070BackupStatus>;
  };
  /** Loopback OAuth: listen binds and returns a port; waitCode waits for the callback with the code. */
  oauth: {
    listen(): Promise<number>;
    waitCode(timeoutSecs: number): Promise<string>;
    /** Cancels an in-flight connect (releases the listener). */
    cancel(): Promise<void>;
  };
  /** Machine identity for the refresh anti-abuse heuristic. null = not available. */
  account: { machineId(): Promise<string | null> };
}

/** Windows-style join shared across implementations (avoids double slashes). */
export function joinPath(...parts: string[]): string {
  return parts
    .map((p, i) => (i === 0 ? p.replace(/[\\/]+$/, '') : p.replace(/^[\\/]+|[\\/]+$/g, '')))
    .filter((p) => p.length > 0)
    .join('\\');
}

// Rebrand: the brand's data folder (%LOCALAPPDATA%\CareerEngineDemo; previously \CareerEngineDemoLegacy).
// The physical migration (renaming the folder) is done by the Rust side at startup (migrate_legacy_data_root)
// and the DLL's boot sequence; this code only builds paths against the new name.
export const BRAND_DIR = 'CareerEngineDemo';
export const LEGACY_BRAND_DIR = 'CareerEngineDemoLegacy';

// Data root MANAGED by the app (the install): %LOCALAPPDATA%\CareerEngineDemo\data.
// The static-content sync ONLY writes here; a dev config (data=docs) is never touched.
export function managedDataRoot(localAppData: string): string {
  return joinPath(localAppData, BRAND_DIR, 'data');
}

export function isTauriRuntime(): boolean {
  return typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;
}

let overrideBridge: ShellBridge | null = null;

/** Tests only: inject a concrete implementation. */
export function setBridge(b: ShellBridge | null): void {
  overrideBridge = b;
}

let cached: Promise<ShellBridge> | null = null;

export async function getBridge(): Promise<ShellBridge> {
  if (overrideBridge) return overrideBridge;
  // One pending promise for every early caller: two concurrent first calls must share one bridge.
  cached ??= isTauriRuntime()
    ? import('./tauriBridge').then((m) => m.tauriBridge)
    : import('./mockBridge').then((m) => m.createMockBridge());
  return cached;
}
