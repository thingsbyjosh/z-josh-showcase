// Tutorial trigger. The hook lives in AppShell (the single trigger point): it receives
// the active tour (or null if a gate/state vetoes it), and if it hasn't been seen, launches driver.js after a
// short delay so the screen can mount its queries. Seeing it halfway counts as seen (navigating
// away mid-tour destroys it and marks it as seen); the nonce forces re-evaluation after a replay from Settings.
import { useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { driver, type Driver, type DriveStep } from 'driver.js';
import { TOURS, type TourKey } from './steps';
import { tourSeen, markTourSeen } from './seen';
import { focusForGamepad } from '../game/useGamepadNav';

export interface TourCtx {
  tab: TourKey;
  raceFlow: 'prerace' | 'launched' | 'results' | 'shop' | null;
  rolloverOpen: boolean;
  eventsListOpen: boolean;
  tryout: boolean;
}

// Mirrors AppShell's render order: rollover and raceFlow cover the tab; launched, the events
// list, and a tryout verdict don't carry a tour.
export function activeTourFor(c: TourCtx): TourKey | null {
  if (c.rolloverOpen) return 'rollover';
  if (c.raceFlow === 'prerace') return 'prerace';
  if (c.raceFlow === 'results') return c.tryout ? null : 'results';
  if (c.raceFlow === 'shop') return 'shop';
  if (c.raceFlow === 'launched') return null;
  if (c.eventsListOpen) return null;
  return c.tab;
}

const MOUNT_DELAY_MS = 400;   // margin so the screen can paint its data before anchors are measured

type TFn = ReturnType<typeof useTranslation>['t'];

// steps of a tour chain resolved to the current language (filters out those pointing at an anchor absent from the
// DOM: conditional element without data -> the step is skipped, it does not break).
function buildSteps(chain: TourKey[], t: TFn): DriveStep[] {
  const steps: DriveStep[] = [];
  for (const k of chain) {
    for (const s of TOURS[k]) {
      if (s.anchor && !document.querySelector(`[data-tour="${s.anchor}"]`)) continue;
      steps.push({
        ...(s.anchor ? { element: `[data-tour="${s.anchor}"]` } : {}),
        popover: { title: t(s.titleKey), description: t(s.bodyKey) },
      });
    }
  }
  return steps;
}

// `ready` (T147 point 5): Settings and Rollover request their OWN query and draw no anchor while
// it loads -- with that slow query, the fixed margin below was not enough and the tour fired lame (steps
// filtered out by absent anchor, not because the element was really conditional). The shell passes `ready`
// as false until that screen has settled its own query; the hook arms nothing until it
// turns true, so it never gets to open a half-painted tour.
export function useTutorial(active: TourKey | null, nonce = 0, ready = true): void {
  const { t, i18n } = useTranslation();
  // ref to the ACTIVE driver and to the chain it was built with, to be able to retranslate an already open balloon
  // (point 2 below) without going through destroy() -- destroy() ALWAYS marks the chain as seen.
  const driverRef = useRef<Driver | null>(null);
  const chainRef = useRef<TourKey[]>([]);

  useEffect(() => {
    if (!active || !ready || tourSeen(active)) return;
    // First run: the topbar tour chains into the hub's tour in a single pass.
    const chain: TourKey[] = active === 'hub' && !tourSeen('shell') ? ['shell', 'hub'] : [active];
    const timer = setTimeout(() => {
      const steps = buildSteps(chain, t);
      if (steps.length === 0) { chain.forEach(markTourSeen); return; }
      chainRef.current = chain;
      const d = driver({
        steps,
        showProgress: steps.length > 1,
        progressText: '{{current}} / {{total}}',
        nextBtnText: t('tour.next'),
        prevBtnText: t('tour.prev'),
        doneBtnText: t('tour.done'),
        stagePadding: 6,
        overlayOpacity: 0.62,
        // 27/09: each balloon is born with focus on “Next”. driver.js left it on the ✕, so the controller's first A
        // closed the tutorial instead of advancing it; and with navigation bounded to the balloon (see
        // useGamepadNav.navRoot) the focus has to be inside from the start.
        onPopoverRender: (p) => { requestAnimationFrame(() => focusForGamepad(p.nextButton)); },
        onDestroyed: () => { chain.forEach(markTourSeen); driverRef.current = null; chainRef.current = []; },
      });
      driverRef.current = d;
      d.drive();
    }, MOUNT_DELAY_MS);
    return () => {
      clearTimeout(timer);
      // navigating away mid-tour closes it; onDestroyed marks the chain as seen (halfway counts as seen)
      if (driverRef.current?.isActive()) driverRef.current.destroy();
    };
  }, [active, nonce, ready]);   // eslint-disable-line react-hooks/exhaustive-deps

  // T147 (2): an OPEN tour stayed in the startup language if you changed language midway -- the
  // balloon did not listen to i18n.language (on purpose: if it were in the deps of the effect above, every language change
  // would destroy the tour in progress and mark it seen). Instead of restarting, it RETRANSLATES in
  // place: same steps, same active index, new text.
  useEffect(() => {
    const d = driverRef.current;
    if (!d || !d.isActive() || chainRef.current.length === 0) return;
    const idx = d.getActiveIndex() ?? 0;
    const steps = buildSteps(chainRef.current, t);
    if (steps.length === 0) return;
    d.setSteps(steps);
    // manual merge onto the LIVE config (not setConfig with a partial object: driver.js merges it onto
    // its DEFAULTS, not onto the current config, and in one stroke would lose steps/onDestroyed/overlayOpacity).
    d.setConfig({ ...d.getConfig(), nextBtnText: t('tour.next'), prevBtnText: t('tour.prev'), doneBtnText: t('tour.done') });
    d.drive(Math.min(idx, steps.length - 1));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [i18n.language]);
}
