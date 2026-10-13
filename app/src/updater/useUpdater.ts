// This showcase does not include auto-updating (see the licence and README): the real
// useUpdater.ts drives Tauri's updater against this project's own release feed, which is not
// part of this repository. This hook keeps the same exported shape and reports "no update
// available"; check()/install() are no-ops.
export type UpdaterStatus =
  'idle' | 'checking' | 'none' | 'available' | 'downloading' | 'restarting' | 'error';

export interface UseUpdater {
  status: UpdaterStatus;
  version: string | null;
  notes: string | null;
  progressPct: number | null;
  error: string | null;
  check(): Promise<void>;
  install(): Promise<void>;
}

export function useUpdater(): UseUpdater {
  return {
    status: 'idle',
    version: null,
    notes: null,
    progressPct: null,
    error: null,
    check: async () => {},
    install: async () => {},
  };
}
