// First-run wizard state: steps, language, detected paths, per-step gating.
import { useCallback, useState } from 'react';
import i18n, { type Locale } from '../i18n';

export type WizardStep = 'welcome' | 'detect' | 'display' | 'assets' | 'career';
export type RootStatus = 'unknown' | 'validating' | 'found_auto' | 'found_manual' | 'not_found';
export type AssetsStatus = 'idle' | 'running' | 'done' | 'error';

export interface WizardState {
  step: WizardStep;
  locale: Locale;
  gameInstall: { path: string; status: RootStatus };
  gameUserDir: { path: string; status: RootStatus };
  windowedMode: number | null;
  gameRunning: boolean;
  assetsStatus: AssetsStatus;
}

export const WIZARD_ORDER: WizardStep[] = ['welcome', 'detect', 'display', 'assets', 'career'];

export interface UseWizard {
  state: WizardState;
  patch: (p: Partial<WizardState>) => void;
  setLocale: (l: Locale) => void;
  setAssetsStatus: (s: AssetsStatus) => void;
  canAdvance: () => boolean;
  next: () => void;
  back: () => void;
}

function isFound(st: RootStatus): boolean {
  return st === 'found_auto' || st === 'found_manual';
}

export function useWizard(initial?: Partial<WizardState>): UseWizard {
  const [state, setState] = useState<WizardState>({
    step: 'welcome',
    locale: 'en',
    gameInstall: { path: '', status: 'unknown' },
    gameUserDir: { path: '', status: 'unknown' },
    windowedMode: null,
    gameRunning: false,
    assetsStatus: 'idle',
    ...initial,
  });

  const patch = useCallback((p: Partial<WizardState>) => setState((s) => ({ ...s, ...p })), []);

  const setLocale = useCallback((l: Locale) => {
    void i18n.changeLanguage(l);
    setState((s) => ({ ...s, locale: l }));
  }, []);

  const setAssetsStatus = useCallback((s: AssetsStatus) => {
    setState((st) => ({ ...st, assetsStatus: s }));
  }, []);

  const canAdvance = useCallback((): boolean => {
    switch (state.step) {
      case 'detect':
        return isFound(state.gameInstall.status) && isFound(state.gameUserDir.status);
      case 'display':
        return state.windowedMode === 2;
      case 'assets':
        return state.assetsStatus !== 'running';
      default:
        return true;
    }
  }, [state]);

  const next = useCallback(
    () =>
      setState((s) => {
        const i = WIZARD_ORDER.indexOf(s.step);
        return i < WIZARD_ORDER.length - 1 ? { ...s, step: WIZARD_ORDER[i + 1] } : s;
      }),
    [],
  );

  const back = useCallback(
    () =>
      setState((s) => {
        const i = WIZARD_ORDER.indexOf(s.step);
        return i > 0 ? { ...s, step: WIZARD_ORDER[i - 1] } : s;
      }),
    [],
  );

  return { state, patch, setLocale, setAssetsStatus, canAdvance, next, back };
}
