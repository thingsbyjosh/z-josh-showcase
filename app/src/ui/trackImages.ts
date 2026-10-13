// Real track maps. The game's own DDS files (gui/trackmaps3d/<slug>.dds) are pre-converted to PNG
// (tools/conv_trackimg.py) and live under <data>/trackmaps/gui/trackmaps3d/<slug>.png. One file per circuit
// (not per-livery), so there's no need to list the folder: it's enough to build the path from the payload's trackSlug.
import { useEffect, useState } from 'react';
import { getBridge, joinPath, type ShellBridge } from '../shell/bridge';

export function trackMapPath(dataDir: string, slug: string): string {
  return joinPath(dataDir, 'trackmaps', 'gui', 'trackmaps3d', `${slug}.png`);
}

// Map URL builder (the webview's asset protocol). Returns '' if bridge/dataDir/slug is missing -> nothing is rendered.
export function useTrackMapUrl(dataDir: string | undefined): (slug: string | undefined) => string {
  const [bridge, setBridge] = useState<ShellBridge | null>(null);
  useEffect(() => {
    let live = true;
    void getBridge().then((b) => { if (live) setBridge(b); });
    return () => { live = false; };
  }, []);
  return (slug) => (bridge && dataDir && slug ? bridge.fs.assetUrl(trackMapPath(dataDir, slug)) : '');
}
