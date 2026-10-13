import { useEffect, useRef, useState } from 'react';
import { getBridge } from './bridge';

// subscribes to the window's NATIVE file drop (dragDropEnabled). `onDrop` receives
// only the paths that match `exts` (zip/rar/7z by default). Returns `hovering` to highlight the drop zone.
// In the browser/mock it's a no-op (hovering always false). The drop is GLOBAL to the window; only mount the hook where
// there's an actual drop zone (e.g. the Liveries screen) so it only listens while that screen is alive.
export function useFileDrop(onDrop: (paths: string[]) => void, exts: string[] = ['zip', 'rar', '7z']): boolean {
  const [hovering, setHovering] = useState(false);
  const onDropRef = useRef(onDrop);
  onDropRef.current = onDrop;

  useEffect(() => {
    let live = true;
    let un: (() => void) | undefined;
    void getBridge()
      .then((b) => b.onFileDrop((e) => {
        if (!live) return;
        if (e.kind === 'enter' || e.kind === 'over') setHovering(true);
        else if (e.kind === 'leave') setHovering(false);
        else if (e.kind === 'drop') {
          setHovering(false);
          const matched = e.paths.filter((p) => exts.some((x) => p.toLowerCase().endsWith('.' + x)));
          if (matched.length) onDropRef.current(matched);
        }
      }))
      .then((u) => { if (live) un = u; else u(); })
      .catch(() => {});
    return () => { live = false; if (un) un(); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return hovering;
}
