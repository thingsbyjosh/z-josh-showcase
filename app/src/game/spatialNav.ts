// Geometry for directional focus navigation (gamepad D-pad and keyboard arrows). Lives separately from
// useGamepadNav so it can be tested on its own: it's pure rectangle arithmetic, and it's the part that's
// caused the most trouble ("the jumps don't seem logical", "I can't reach this button").
export type Dir = 'up' | 'down' | 'left' | 'right';
export interface NavRect { left: number; right: number; top: number; bottom: number }

// Drifting off-axis costs more than moving away in the pressed direction: that keeps you in the same row or
// column instead of jumping diagonally to whatever's closest.
const CROSS_WEIGHT = 4;

// Gap between two intervals; 0 if they overlap (i.e., they're aligned on that axis).
function gap(a0: number, a1: number, b0: number, b1: number): number {
  if (b1 < a0) return a0 - b1;
  if (b0 > a1) return b0 - a1;
  return 0;
}

const mid = (lo: number, hi: number): number => (lo + hi) / 2;

// Distance in the requested direction and misalignment on the cross axis, measured between EDGES. Measuring
// between centers made two buttons in the same row look misaligned as soon as one was
// wider than the other.
function edgeMetrics(a: NavRect, b: NavRect, dir: Dir): { primary: number; cross: number } {
  if (dir === 'up' || dir === 'down') {
    return { primary: dir === 'up' ? a.top - b.bottom : b.top - a.bottom, cross: gap(a.left, a.right, b.left, b.right) };
  }
  return { primary: dir === 'left' ? a.left - b.right : b.left - a.right, cross: gap(a.top, a.bottom, b.top, b.bottom) };
}

// Same thing but between CENTERS, for the second pass (see below).
function centreMetrics(a: NavRect, b: NavRect, dir: Dir): { primary: number; cross: number } {
  const dx = mid(b.left, b.right) - mid(a.left, a.right);
  const dy = mid(b.top, b.bottom) - mid(a.top, a.bottom);
  if (dir === 'up') return { primary: -dy, cross: Math.abs(dx) };
  if (dir === 'down') return { primary: dy, cross: Math.abs(dx) };
  if (dir === 'left') return { primary: -dx, cross: Math.abs(dy) };
  return { primary: dx, cross: Math.abs(dy) };
}

// Index of the best candidate in direction `dir`, or -1 if there's none.
//
// TWO PASSES, and the second one exists because of a real case: the calendar's "jump to next year" button
// floats absolutely positioned over the list, so it OVERLAPS the rows. The first pass requires a clean
// edge separation (which is what gives predictable jumps between grid elements), and because of that it
// was rejecting that button: there was no way to reach it with a controller. If that pass finds nothing, the second
// pass accepts overlapping candidates as long as their center is ahead in that direction.
//
// Order matters: as long as something is cleanly separated, that wins. The permissive pass only kicks in when
// the alternative is not being able to move at all.
export function pickIndex(from: NavRect, dir: Dir, candidates: NavRect[]): number {
  for (const pass of ['edges', 'centres'] as const) {
    let best = -1;
    let bestScore = Infinity;
    for (let i = 0; i < candidates.length; i++) {
      const c = candidates[i];
      const m = pass === 'edges' ? edgeMetrics(from, c, dir) : centreMetrics(from, c, dir);
      if (m.primary < (pass === 'edges' ? -1 : 1)) continue;   // behind (or on top, in the 1st pass)
      const score = Math.max(m.primary, 0) + m.cross * CROSS_WEIGHT;
      if (score < bestScore) { bestScore = score; best = i; }
    }
    if (best >= 0) return best;
  }
  return -1;
}

// 27/09 (navigation audit, rc.4 baseline): the four measured “long jumps” were the same case. From
// a Settings difficulty card, “down” jumped 1,271 px to a VR launcher button that was
// vertically aligned, passing over a “−” visible at 720 px but slightly offset. The scoring
// rewards alignment so much that an aligned off-screen candidate beats a nearby one in view.
// Rule: as long as there is something WITHIN REACH (less than a screen away) ahead in that direction, go to that;
// the far-off only enters when nothing is left closer that way (and then the scroll brings it). `preferred`
// says, per candidate, whether it is within reach; who decides it (and with what measure) is the caller.
export function pickIndexPreferring(from: NavRect, dir: Dir, candidates: NavRect[], preferred: boolean[]): number {
  const idx: number[] = [];
  const rects: NavRect[] = [];
  for (let i = 0; i < candidates.length; i++) if (preferred[i]) { idx.push(i); rects.push(candidates[i]); }
  if (rects.length) {
    const j = pickIndex(from, dir, rects);
    if (j >= 0) return idx[j];
  }
  return pickIndex(from, dir, candidates);
}

// The candidate whose center is closest to a point (to land again where the focus was when
// the element that had it has disappeared on repaint). -1 with no candidates.
export function nearestIndex(to: NavRect, candidates: NavRect[]): number {
  const cx = mid(to.left, to.right), cy = mid(to.top, to.bottom);
  let best = -1, bestD = Infinity;
  for (let i = 0; i < candidates.length; i++) {
    const c = candidates[i];
    const dx = mid(c.left, c.right) - cx, dy = mid(c.top, c.bottom) - cy;
    const d = dx * dx + dy * dy;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

export const OPPOSITE: Record<Dir, Dir> = { up: 'down', down: 'up', left: 'right', right: 'left' };

// A TV's comfort band: focus stays between 10% and 72% of the visible height. Outside
// it (or cut off by a scroller) it is recentered, so that what surrounds the focused element is seen too.
// Josh (rc.4): “it scrolls halfway: it lands on the button but doesn't show everything”. With `nearest` the scroll
// stopped as soon as the button peeked out, and what went below (the card, the open tab) stayed outside.
export function outsideComfortBand(r: NavRect, viewportHeight: number): boolean {
  return r.top < viewportHeight * 0.10 || r.bottom > viewportHeight * 0.72;
}
