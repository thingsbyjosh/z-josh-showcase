// Side scroll arrows (requested by a user). Designed mainly for the VR panel:
// there you point with a laser and there's no mouse wheel or scrollbar to drag, so without this a long
// screen is simply unreachable with the headset's controller. On desktop they stay out of the way: they only appear
// when there's something to scroll, and each arrow turns off once it reaches its end.
//
// Deliberately generic: instead of every screen mounting its own control, it looks live for the
// largest visible '.scroll' container, the pattern the screens already use. That way it works across the whole app
// without touching them one by one, the same way focus navigation does.
import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';

const POLL_MS = 250;      // content changes on navigation/load; polling is simpler than observing everything
const EDGE_SLACK = 4;     // pixels of slack so a rounding error doesn't leave an arrow lit up

interface RailState { el: HTMLElement | null; canUp: boolean; canDown: boolean }

// The largest container with REAL scroll among the visible ones. Biggest by area wins: it's the one taking up the
// screen, as opposed to small lists that might also have their own scroll.
//
// Walked from the content area, and only a few levels deep: scrolling containers live near the top of the
// tree, and scanning the whole app would mean resolving computed style on thousands of nodes every few hundred ms.
// Filtering by the '.scroll' class doesn't work either: several screens set up their scrolling with inline styles.
const MAX_DEPTH = 5;

function scrollsVertically(el: HTMLElement): boolean {
  if (el.scrollHeight <= el.clientHeight + EDGE_SLACK) return false;
  const oy = getComputedStyle(el).overflowY;
  return oy === 'auto' || oy === 'scroll' || oy === 'overlay';
}

function findScroller(): HTMLElement | null {
  const root = document.querySelector<HTMLElement>('[role="main"]') ?? document.body;
  if (!root) return null;
  let best: HTMLElement | null = null;
  let bestArea = 0;
  let level: HTMLElement[] = [root];
  for (let depth = 0; depth <= MAX_DEPTH && level.length; depth++) {
    const next: HTMLElement[] = [];
    for (const el of level) {
      const r = el.getBoundingClientRect();
      if (r.width < 1 || r.height < 1) continue;                  // a hidden branch: skip it and its children
      if (scrollsVertically(el)) {
        const area = r.width * r.height;
        if (area > bestArea) { bestArea = area; best = el; }
      }
      for (const child of Array.from(el.children)) next.push(child as HTMLElement);
    }
    level = next;
  }
  return best;
}

export function ScrollRail() {
  const { t } = useTranslation();
  const [rail, setRail] = useState<RailState>({ el: null, canUp: false, canDown: false });

  // 31/08: the arrows float FIXED over the right edge, so they eat the 48px of any
  // content flush with that edge -- seen with the championship table's POINTS column, which ends up
  // under the down button. Instead of patching screen by screen, the rail publishes its width in
  // `--rail-gutter` (0 when nothing is painted) and whoever has content flush with the edge reserves that gap.
  // Second round: moving the CONTENT with padding isn't enough -- the scrollbar paints itself on the BOX's
  //   edge, so it stayed just as much under the buttons. The gap has to NARROW the scroller
  //   (margin), and then its bar travels with it. The rail stays flush with the edge, which is where it belongs.
  const RAIL_RIGHT = 8;
  const RAIL_GUTTER = '56px';   // right:8 + a 40px button + 8px of breathing room
  useEffect(() => {
    const sync = () => {
      const el = findScroller();
      document.documentElement.style.setProperty('--rail-gutter', el ? RAIL_GUTTER : '0px');
      setRail((prev) => {
        const canUp = !!el && el.scrollTop > EDGE_SLACK;
        const canDown = !!el && el.scrollTop + el.clientHeight < el.scrollHeight - EDGE_SLACK;
        if (prev.el === el && prev.canUp === canUp && prev.canDown === canDown) return prev;   // without repainting
        return { el, canUp, canDown };
      });
    };
    sync();
    const iv = setInterval(sync, POLL_MS);
    window.addEventListener('resize', sync);
    return () => {
      clearInterval(iv); window.removeEventListener('resize', sync);
      document.documentElement.style.setProperty('--rail-gutter', '0px');
    };
  }, []);

  if (!rail.el) return null;

  // A short "page" instead of the whole screen: keeps context between jumps, which is what's
  // expected of a page-down and helps you not get lost when you can't drag the scrollbar.
  const step = (dir: 1 | -1) => {
    const el = rail.el;
    if (!el) return;
    el.scrollBy({ top: dir * el.clientHeight * 0.8, behavior: 'smooth' });
  };

  const arrow = (dir: 1 | -1, enabled: boolean, label: string, glyph: string) => (
    <button
      type="button"
      data-nav-skip                    /* not a focus target: the keyboard arrows already scroll on their own */
      aria-label={label}
      disabled={!enabled}
      onClick={() => step(dir)}
      style={{
        width: 40, height: 40, display: 'grid', placeItems: 'center',
        background: 'var(--surface-2)', border: '1px solid var(--line-2)', color: 'var(--text-2)',
        cursor: enabled ? 'pointer' : 'default', opacity: enabled ? 1 : 0.25,
        font: '600 15px/1 var(--font-mono)',
      }}
    >{glyph}</button>
  );

  return (
    // pointerEvents none on the container and auto on the buttons: the column must not steal clicks from the
    // content underneath it.
    <div
      aria-hidden={false}
      style={{
        position: 'fixed', right: RAIL_RIGHT, top: '50%', transform: 'translateY(-50%)',
        display: 'flex', flexDirection: 'column', gap: 6, zIndex: 40, pointerEvents: 'none',
      }}
    >
      <div style={{ pointerEvents: 'auto', display: 'flex', flexDirection: 'column', gap: 6 }}>
        {arrow(-1, rail.canUp, t('nav.scrollUp'), '▲')}
        {arrow(1, rail.canDown, t('nav.scrollDown'), '▼')}
      </div>
    </div>
  );
}
