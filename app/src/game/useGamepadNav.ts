// UI navigation with a GAMEPAD/WHEEL. A global hook (mounted once in AppShell) that polls the Gamepad
// API every frame and translates controller input into DOM FOCUS NAVIGATION: it doesn't depend on per-screen focus
// maps, it discovers focusable elements live and jumps to the closest one in the pressed direction (spatial
// navigation), like a TV remote. A = activate (click), B = back, bumpers = previous/next tab.
//
// Deliberate design: zero coupling with individual screens. Any visible <button>, [tabindex], input, or .focusable
// element participates automatically -> it works across the WHOLE app without touching each screen. "Back" (B) clicks
// a [data-gamepad-back] element if one exists, and otherwise fires a synthetic Escape (which overlays listen for).
import { useEffect, useRef } from 'react';
import { gamepadEnabled, type GamepadAction } from '../ui/prefs';
import { connectedPads, isInputActive, AxisRest } from './gamepads';
import { bindingsFor, GAMEPAD_ACTIONS } from './gamepadProfiles';
import { pickIndexPreferring, nearestIndex, outsideComfortBand, OPPOSITE, type Dir, type NavRect } from './spatialNav';

const AXIS_DEADZONE = 0.55;      // stick: threshold to count as a directional "press"
const REPEAT_MS = 140;           // auto-repeat while holding a direction
const POLL_HINT_EVENT = 'ams2:gamepad-nav';   // custom event so the UI can show the hint (optional)

// 27/09 (audit): with a modal open, focus escaped behind its veil (13 moves in
// Championships ended up on buttons covered by “Full season”). All the app's modals carry
// aria-modal="true": if any is visible, only what is INSIDE the last one (the topmost) counts.
function navRoot(): ParentNode {
  // 27/09 (Josh, rc.4: “the tutorial popups are not navigable”): with a driver.js balloon open, the
  // balloon IS the modal. Without this, between step and step the library briefly removes the `pointer-events: none`
  // from the rest of the page, focus went to a button behind the veil and A pressed it (reproduced on the
  // bench: A signed a contract with the tutorial in front).
  const tour = tourPopover();
  if (tour) return tour;
  const modals = Array.from(document.querySelectorAll<HTMLElement>('[aria-modal="true"]'))
    .filter((m) => { const r = m.getBoundingClientRect(); return r.width >= 1 && r.height >= 1; });
  return modals.length ? modals[modals.length - 1] : document;
}

// The tutorial balloon (driver.js) if a tour is running, or null.
function tourPopover(): HTMLElement | null {
  if (!document.body.classList.contains('driver-active')) return null;
  return document.querySelector<HTMLElement>('.driver-popover');
}

// VISIBLE focusable elements in DOM order. Excludes disabled, hidden, and tabindex=-1 elements.
function focusables(): HTMLElement[] {
  const sel = 'button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"]), .focusable';
  const all = Array.from(navRoot().querySelectorAll<HTMLElement>(sel));
  const seen = new Set<HTMLElement>();
  const out: HTMLElement[] = [];
  for (const el of all) {
    if (seen.has(el)) continue;
    seen.add(el);
    if (el.hasAttribute('disabled') || el.getAttribute('aria-disabled') === 'true') continue;
    // 27/09 (audit): tabindex=-1 removes ANY element from the traversal, a <button> too. The selector
    // only excluded it in the [tabindex] branch, and the “more tabs” arrows (button tabIndex={-1}) got in
    // anyway and created meaningless destinations and asymmetries on the Home screen.
    if (el.hasAttribute('tabindex') && el.tabIndex < 0) continue;
    // Tabs are deliberately EXCLUDED from directional navigation (user feedback: "in the
    // tabs it's not clear which one you're pointing at"). They're switched with their own keys (Q/E) and with the
    // controller bumpers, like in a game console: that way the arrows always move WITHIN the content, removing
    // the ambiguity between "the tab that's selected" and "the tab that has focus".
    // ONLY the shell's own tab bar is excluded, the one governed by Q/E and the bumpers. Any other strip
    // of tabs inside a screen remains reachable with the arrows: if it were excluded by plain
    // '.tab' class matching, those would become unreachable with nothing left to switch them.
    if (el.closest('[data-tour="shell.tabs"]')) continue;
    if (el.hasAttribute('data-nav-skip')) continue;                  // controls that aren't a focus target
    const r = el.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) continue;                       // display:none / collapsed
    // Elements outside the viewport are NOT discarded. That filter used to block scrolling down to content not yet visible
    // (user feedback: "I can't scroll down to tryouts on the calendar that I can't see"), which is exactly
    // what focus-based navigation on a scrolling screen is for: moveFocus() calls scrollIntoView and brings it into view.
    // What's actually hidden is already discarded above (zero size, visibility, opacity, pointer-events).
    const st = getComputedStyle(el);
    if (st.visibility === 'hidden' || st.opacity === '0' || st.pointerEvents === 'none') continue;
    out.push(el);
  }
  return out;
}

// The nearest scroller that clips an element (overflow auto/scroll), or null if only the window clips it.
function scrollParent(el: HTMLElement): HTMLElement | null {
  for (let p = el.parentElement; p; p = p.parentElement) {
    const o = getComputedStyle(p).overflowY;
    if (o === 'auto' || o === 'scroll') return p;
  }
  return null;
}

// Is it less than a screen away in the pressed direction? (distance between edges; overlap counts)
function withinReach(from: DOMRect, c: DOMRect, dir: Dir): boolean {
  const reach = dir === 'up' || dir === 'down' ? window.innerHeight : window.innerWidth;
  const d = dir === 'up' ? from.top - c.bottom : dir === 'down' ? c.top - from.bottom
          : dir === 'left' ? from.left - c.right : c.left - from.right;
  return d <= reach;
}

// Is it seen WHOLE? (to decide whether it needs to be brought in)
function fullyOnScreen(el: HTMLElement, r: DOMRect): boolean {
  const vw = window.innerWidth, vh = window.innerHeight;
  if (r.top < 0 || r.bottom > vh || r.left < 0 || r.right > vw) return false;
  const sp = scrollParent(el);
  if (!sp) return true;
  const s = sp.getBoundingClientRect();
  return r.top >= s.top && r.bottom <= s.bottom && r.left >= s.left && r.right <= s.right;
}

// The best candidate in direction `dir` from `from`. The arithmetic lives in spatialNav.ts, which is
// tests itself: here we only translate from DOM elements to rectangles. What is WITHIN REACH (less than
// a screen away in that direction) goes first (see pickIndexPreferring); the far-off only enters when there is
// nothing closer that way. It is measured from the ORIGIN and not “what is visible”: if the user has scrolled with
// the mouse away from the focus, “visible first” jumped to whatever was on screen instead of to the neighbor.
function pick(from: HTMLElement, dir: Dir, all: HTMLElement[]): HTMLElement | null {
  const others = all.filter((el) => el !== from);
  const fr = from.getBoundingClientRect();
  const rects = others.map((el) => el.getBoundingClientRect());
  const near = rects.map((r) => withinReach(fr, r, dir));
  const i = pickIndexPreferring(fr, dir, rects, near);
  return i >= 0 ? others[i] : null;
}

// 27/09 (audit): where the focus was the last time. If the element that had it disappears on repaint (a
// list that gets reordered, a card that closes), the next move landed on the FIRST focusable
// on the page, which in practice is the top corner: those were the jumps “to meaningless places”. Now it
// lands on the one closest to where it was. It is noted on every move and on every focusin (mouse).
let lastRect: NavRect | null = null;
export function rememberFocusRect(el: Element | null): void {
  if (!el || !(el instanceof HTMLElement)) return;
  const r = el.getBoundingClientRect();
  if (r.width < 1 && r.height < 1) return;
  lastRect = { left: r.left, right: r.right, top: r.top, bottom: r.bottom };
}

// 27/09 (audit): 251 asymmetric pairs in the baseline (A's “down” leads to B, B's “up” does not return to A).
// The geometry cannot always be symmetric, but the gesture of going and coming back does have to be: the
// last move is remembered and the opposite direction returns to the origin if it is still there.
let lastMove: { from: HTMLElement; to: HTMLElement; dir: Dir } | null = null;

function currentFocus(all: HTMLElement[]): HTMLElement | null {
  const act = document.activeElement as HTMLElement | null;
  if (act && all.includes(act)) return act;
  if (lastRect) {
    const i = nearestIndex(lastRect, all.map((el) => el.getBoundingClientRect()));
    if (i >= 0) return all[i];
  }
  return all[0] ?? null;
}

// Brings the element into view IF needed: clipped by the window or by its scroller, or outside the comfort
// band (see outsideComfortBand). Centered, so that what surrounds it is seen too; and no scrolling
// when it is already well placed, to avoid wobbling.
function reveal(el: HTMLElement): void {
  const r = el.getBoundingClientRect();
  if (fullyOnScreen(el, r) && !outsideComfortBand(r, window.innerHeight)) return;
  el.scrollIntoView({ block: 'center', inline: 'nearest' });
}

// Visible focus indicator (fix: "it wasn't marking focus"). All the design system's focus styling goes through
// focus-visible, which is a browser HEURISTIC: it activates on Tab, but NOT when focus is
// moved by script -- which is exactly what this hook does. Result: focus was actually moving, but it wasn't
// visible, so it looked like navigation wasn't working. The .is-focus class already existed in the design
// exactly for this; it just needed someone to apply it.
const FOCUS_CLASS = 'is-focus';
function markFocus(el: HTMLElement | null): void {
  document.querySelectorAll('.' + FOCUS_CLASS).forEach((n) => n.classList.remove(FOCUS_CLASS));
  if (el) el.classList.add(FOCUS_CLASS);
}

// Put focus on an element AS THE CONTROLLER WOULD (focus + visible mark + remember where). For whoever
// opens something new and wants the next A to land where it should (the tutorial's “Next”).
export function focusForGamepad(el: HTMLElement | null): void {
  if (!el) return;
  el.focus();
  if (gamepadEnabled()) markFocus(el);
  rememberFocusRect(el);
}

function moveFocus(dir: Dir) {
  const all = focusables();
  if (!all.length) return;
  const cur = currentFocus(all);
  if (!cur) return;
  if (document.activeElement !== cur) {   // there was no valid focus -> land (near where it was, or on the 1st)
    cur.focus(); markFocus(cur); reveal(cur); rememberFocusRect(cur); lastMove = null; return;
  }
  // Go and come back: the direction opposite to the last move undoes that move if the origin is still there.
  const back = lastMove && lastMove.to === cur && OPPOSITE[lastMove.dir] === dir && all.includes(lastMove.from)
    ? lastMove.from : null;
  const next = back ?? pick(cur, dir, all);
  if (next) {
    next.focus();
    markFocus(next);
    reveal(next);
    rememberFocusRect(next);
    lastMove = { from: cur, to: next, dir };
    window.dispatchEvent(new CustomEvent(POLL_HINT_EVENT));
  }
}

function activate() {
  const el = document.activeElement as HTMLElement | null;
  if (!el) return;
  // text inputs: no synthetic "click"; focus already lets you type. For everything else, a synthetic click.
  const tag = el.tagName.toLowerCase();
  if (tag === 'input' || tag === 'textarea' || tag === 'select') return;
  el.click();
}

function goBack() {
  // With the tutorial in front, B closes the balloon (not the “back” of the screen underneath).
  const tour = tourPopover();
  if (tour) { tour.querySelector<HTMLElement>('.driver-popover-close-btn')?.click(); return; }
  const back = document.querySelector<HTMLElement>('[data-gamepad-back]');
  if (back) { back.click(); return; }
  // no back button marked -> synthetic Escape (overlays/modals listen for it)
  const ev = new KeyboardEvent('keydown', { key: 'Escape', bubbles: true });
  (document.activeElement ?? document.body).dispatchEvent(ev);
}

// while a screen is CAPTURING a button (the VR panel's confirm key), the controller
// does not navigate. If it did, pressing the button you want to assign could also "activate" the focused element
// (typically the capture button itself, which would close it) or fire a synthetic Escape. While suspended
// pressed buttons are still tracked, but without acting: so the button that closed the capture, still pressed
// on resume, does not count as a new press. Returns the function that lifts it (idempotent).
let navSuspended = 0;
export function suspendGamepadNav(): () => void {
  navSuspended++;
  let released = false;
  return () => { if (!released) { released = true; navSuspended--; } };
}
export function gamepadNavSuspended(): boolean { return navSuspended > 0; }

// 27/09 (Josh, rc.4): the controller navigated the app WHILE DRIVING. Chromium's Gamepad API keeps
// delivering readings with the window unfocused, and WebView2 starts with the flags that keep it from sleeping
// when covered or minimized (needed for the headset panel), so the per-frame polling stayed alive with the app
// minimized behind AMS2 and every wheel button moved the focus or pressed something. Rule: the controller only navigates
// with the window FOCUSED (`document.hasFocus()`), unless the app says it is being viewed by another route
// (`allowUnfocused`: the headset panel captures the window without it having system focus). Pure so it can be
// tested; and while the gate is closed what is pressed is still tracked, as in suspension, so that
// the button that was already pressed on regaining focus does not count as a new press.
export function gamepadNavGateOpen(hasFocus: boolean, allowUnfocused: boolean): boolean {
  return hasFocus || allowUnfocused;
}

// AppShell passes the tab callbacks (bumpers). Without them, those buttons do nothing.
export interface GamepadNavHandlers {
  onPrevTab?: () => void;
  onNextTab?: () => void;
  /** When the controller may navigate WITHOUT the window having focus (headset panel). Without it: never. */
  allowUnfocused?: () => boolean;
}

export function useGamepadNav(handlers: GamepadNavHandlers) {
  const handlersRef = useRef(handlers);
  handlersRef.current = handlers;

  // Keyboard: arrow keys move focus with the SAME spatial navigation as the gamepad. ----
  // It's not a "mode" and there's no device to pick: keyboard, gamepad, and wheel are all live at once, and you use
  // whichever is at hand, the way any desktop app works.
  // ONLY the arrow keys are touched, deliberately: moving focus in 2D is exactly what the browser doesn't
  // know how to do. Enter (activate) and Escape (close) already work natively, and duplicating them here would cause
  // double activation -- and in Escape's case, recursion with the synthetic Escape that goBack() fires.
  // This runs without the gamepadEnabled() toggle: the keyboard doesn't have the risks that toggle exists for
  // (a wheel with pedals at rest can send axes to full deflection; a keyboard can't).
  useEffect(() => {
    const isEditable = (el: Element | null): boolean => {
      if (!el) return false;
      const tag = el.tagName.toLowerCase();
      if (tag === 'input' || tag === 'textarea' || tag === 'select') return true;
      return (el as HTMLElement).isContentEditable === true;
    };
    const onKey = (e: KeyboardEvent) => {
      if (!e.isTrusted) return;                                  // ignores synthetic events the app itself dispatches
      if (e.ctrlKey || e.altKey || e.metaKey) return;            // system/app shortcuts: not navigation
      if (isEditable(document.activeElement)) return;            // inside a field, the arrow keys move the cursor
      if (tourPopover()) return;                                 // the tutorial already uses ← → to move between steps; Tab/Enter for the rest
      const dir: Dir | null =
        e.key === 'ArrowUp' ? 'up' : e.key === 'ArrowDown' ? 'down' :
        e.key === 'ArrowLeft' ? 'left' : e.key === 'ArrowRight' ? 'right' : null;
      if (dir) {
        e.preventDefault();                                      // otherwise the page would also scroll
        moveFocus(dir);
        return;
      }
      // Tabs get their own keys instead of the arrows: that way it's clear at a glance what moves what
      // (arrows = within the content, Q/E = switch tabs), same as the bumpers on the
      // gamepad. Q/E rather than W/E because that's the conventional pair for cycling in a game; W is usually move-forward.
      const k = e.key.toLowerCase();
      if (k === 'q') { e.preventDefault(); handlersRef.current.onPrevTab?.(); }
      else if (k === 'e') { e.preventDefault(); handlersRef.current.onNextTab?.(); }
    };
    // Using the mouse clears the focus indicator, like any app does: the outline is for knowing
    // where you are when navigating blind, and it's redundant once the pointer's in play.
    const onMouse = () => markFocus(null);
    // Where the focus ends up when someone else moves it (a click, a Tab): to land nearby if that element goes away.
    const onFocusIn = (e: FocusEvent) => rememberFocusRect(e.target as Element | null);
    window.addEventListener('keydown', onKey);
    window.addEventListener('mousedown', onMouse);
    window.addEventListener('focusin', onFocusIn);
    return () => {
      window.removeEventListener('keydown', onKey);
      window.removeEventListener('mousedown', onMouse);
      window.removeEventListener('focusin', onFocusIn);
    };
  }, []);

  useEffect(() => {
    // Only runs if the Gamepad API exists. enable is re-read every frame (cheap) so the Settings toggle
    // takes effect without remounting. Rising-edge detection per button, plus auto-repeat on directions.
    if (typeof navigator === 'undefined' || !navigator.getGamepads) return;
    let raf = 0;
    // Key "padIndex:action": ALL controllers are read, so the action alone no longer identifies
    // anything (the wheel's "confirm" and the gamepad's are different controls).
    const prevPressed = new Map<string, boolean>();   // pad:action -> was active on the previous frame
    const nextRepeat = new Map<string, number>();      // dir -> timestamp of the next allowed repeat
    let axisLatched: Dir | null = null;                // stick: prevents continuous repeat while held
    const rest = new AxisRest();                       // real rest position of each axis (see gamepads.ts, rule 3)

    const DIR_ACTIONS: Partial<Record<GamepadAction, Dir>> =
      { up: 'up', down: 'down', left: 'left', right: 'right' };

    const loop = () => {
      raf = requestAnimationFrame(loop);
      if (!gamepadEnabled()) { prevPressed.clear(); axisLatched = null; return; }
      // ALL connected controllers, not just the first: with a wheel plugged in, the first one is the
      // wheel, and the gamepad stayed invisible no matter how hard it was pressed (see gamepads.ts, rule 1).
      const pads = connectedPads();
      if (!pads.length) return;
      const now = performance.now();
      // (capture) and 27/09 (unfocused window): what is pressed is tracked, but no action is taken.
      const gateOpen = gamepadNavGateOpen(document.hasFocus(), handlersRef.current.allowUnfocused?.() ?? false);
      const suspended = navSuspended > 0 || !gateOpen;

      // ASSIGNED CONTROLS (rising edge; directions with auto-repeat) ---
      // Iterated by ACTION rather than by button: that way a binding can be an AXIS, which is how a wheel's
      // D-pad arrives. Iterating by button, that D-pad was simply invisible.
      for (const pad of pads) {
        // DEVICE-specific bindings, not global ones: button 0 on a wheel and button 0 on a gamepad don't necessarily
        // mean the same thing, and both can be connected at once (see gamepadProfiles.ts).
        const b = bindingsFor(pad.id);
        for (const action of GAMEPAD_ACTIONS) {
          const active = isInputActive(pad, b[action]);
          const pk = `${pad.index}:${action}`;
          const was = prevPressed.get(pk) ?? false;
          prevPressed.set(pk, active);
          const dir = DIR_ACTIONS[action];
          if (dir) {
            if (active) {
              const key = 'btn:' + dir;
              const due = nextRepeat.get(key) ?? 0;
              if (suspended) nextRepeat.set(key, now + REPEAT_MS);
              else if (!was || now >= due) { moveFocus(dir); nextRepeat.set(key, now + REPEAT_MS); }
            }
            continue;
          }
          if (active && !was && !suspended) {   // non-directional actions: only on the rising edge
            if (action === 'activate') activate();
            else if (action === 'back') goBack();
            else if (action === 'prevTab') handlersRef.current.onPrevTab?.();
            else if (action === 'nextTab') handlersRef.current.onNextTab?.();
          }
        }
      }

      // AXES 0/1 of any controller: directional focus, latched so it doesn't repeat in a cascade ---
      // Deflection is measured against each axis's REST position, not against 0: with pedals resting at ±1,
      // assuming 0 would fire focus in a loop the moment the wheel was plugged in.
      let stickDir: Dir | null = null;
      for (const pad of pads) {
        const ax = rest.relative(pad, 0), ay = rest.relative(pad, 1);
        if (Math.abs(ay) > Math.abs(ax)) {
          if (ay < -AXIS_DEADZONE) stickDir = 'up'; else if (ay > AXIS_DEADZONE) stickDir = 'down';
        } else {
          if (ax < -AXIS_DEADZONE) stickDir = 'left'; else if (ax > AXIS_DEADZONE) stickDir = 'right';
        }
        if (stickDir) break;   // whichever device speaks first wins; two devices are never combined at once
      }
      if (stickDir) {
        const key = 'ax:' + stickDir;
        const due = nextRepeat.get(key) ?? 0;
        if (suspended) { axisLatched = stickDir; nextRepeat.set(key, now + REPEAT_MS); }
        else if (axisLatched !== stickDir || now >= due) {
          moveFocus(stickDir); axisLatched = stickDir; nextRepeat.set(key, now + REPEAT_MS);
        }
      } else {
        axisLatched = null;
      }
    };

    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, []);
}
