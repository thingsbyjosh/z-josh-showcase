// Real vehicle photos. The game's DDS files are pre-converted to PNG (tools/conv_vehimg.py) and live under
// <data>/vehimages/gui/vehicleimages/vehicleimages_<slug>/<slug>_livery_NN.png (the same paths.data root the
// native overlay uses for the DDS files). The webview can't read DDS -> the PNGs are loaded through Tauri's asset protocol (bridge.fs.assetUrl).
// The ascending NN order matches the engine's own ordinals (liveryNN[]/ownedLiveryOrdinal), so the selector lines up.
import { useEffect, useState } from 'react';
import { getBridge, joinPath, type ShellBridge } from '../shell/bridge';

const VEH_REL = ['vehimages', 'gui', 'vehicleimages'];

// Some slugs from the CustomLiveries/override world do NOT match the slug of the photos (vehimages) that ship with the
// game. An override-slug -> image-slug alias so the card shows the real car. (A modern F1 car installs
// as `formula_ultimate_2022` but its vanilla photos live under `formula_ultimate_2024`.) The alias affects both the
// FOLDER name and the PNG PREFIX (`vehicleimages_<s>/<s>_livery_NN.png`).
const VEHIMG_ALIAS: Record<string, string> = {
  formula_ultimate_2022: 'formula_ultimate_2024',
};
function imgSlug(slug: string): string { return VEHIMG_ALIAS[slug] ?? slug; }

function carDir(dataDir: string, slug: string): string {
  return joinPath(dataDir, ...VEH_REL, `vehicleimages_${imgSlug(slug)}`);
}

export function liveryPath(dataDir: string, slug: string, nn: number): string {
  const s = imgSlug(slug);
  return joinPath(dataDir, ...VEH_REL, `vehicleimages_${s}`, `${s}_livery_${nn}.png`);
}

// NUMBERING OFFSET (measured): the engine's catalog numbers liveries starting at 50, while the PNGs
// extracted from the game start at 51 -- of 519 installed cars, 518 have NO `_livery_50.png` at all. Any
// screen that passes the engine's RAW NN straight through as a filename ends up with no photo, and 50 is exactly the
// most common value the seat resolver hands out. The Garage and pre-race screens never noticed because
// they index this same list instead of naming the file directly. This is the presentation layer's safety net: it asks for the
// exact number, tries the known offset, and as a last resort shows the right car with a different livery --
// an imperfect photo communicates more than a blank hole. The offset IN THE DATA itself is a separate problem.
export function pickLiveryNN(nns: number[], wanted: number): number | null {
  if (!nns.length || wanted < 0) return null;
  if (nns.includes(wanted)) return wanted;
  if (nns.includes(wanted + 1)) return wanted + 1;
  return nns[0];
}

// Lists a car's livery NNs (PNG) in ascending order + a URL builder the webview can load.
// empty dataDir/slug, the browser (mock), or a missing folder -> nns=[] and url()='' (the UI falls back to a placeholder).
export function useCarLiveries(dataDir: string | undefined, slug: string | undefined): { nns: number[]; url: (nn: number) => string; urlFor: (nn: number) => string } {
  const [bridge, setBridge] = useState<ShellBridge | null>(null);
  const [nns, setNns] = useState<number[]>([]);

  useEffect(() => {
    let live = true;
    void getBridge().then((b) => { if (live) setBridge(b); });
    return () => { live = false; };
  }, []);

  useEffect(() => {
    let live = true;
    if (!bridge || !dataDir || !slug) { setNns([]); return; }
    void bridge.fs.listDir(carDir(dataDir, slug)).then((entries) => {
      if (!live) return;
      const re = /_livery_(\d+)\.png$/i;
      const out = entries
        .filter((e) => !e.isDir)
        .map((e) => { const m = e.name.match(re); return m ? parseInt(m[1], 10) : -1; })
        .filter((n) => n >= 0)
        .sort((a, b) => a - b);
      setNns(out);
    }).catch(() => { if (live) setNns([]); });
    return () => { live = false; };
  }, [bridge, dataDir, slug]);

  const url = (nn: number): string => (bridge && dataDir && slug ? bridge.fs.assetUrl(liveryPath(dataDir, slug, nn)) : '');
  // urlFor: like url(), but resolving the NN against the REAL files on disk (see pickLiveryNN). This is the one
  // screens that receive the engine's raw number should use; url() is for callers that already index nns.
  const urlFor = (nn: number): string => { const p = pickLiveryNN(nns, nn); return p == null ? '' : url(p); };
  return { nns, url, urlFor };
}

// previews of INSTALLED skins. The Rust command sync_override_previews converts the
// override's preview image to PNG at <data>\vehimages\overrides\<slug>_livery_<NN>.png. This hook lists that
// folder and exposes a resolver: url(slug, nn) returns the real skin if it exists, '' otherwise (the UI falls back to
// the vanilla one). Re-listed whenever dataDir changes and on remount (the shell's refreshNonce after the background sync).
const OVR_REL = ['vehimages', 'overrides'];
function overridesDir(dataDir: string): string { return joinPath(dataDir, ...OVR_REL); }
export function overridePreviewPath(dataDir: string, slug: string, nn: number): string {
  return joinPath(overridesDir(dataDir), `${slug}_livery_${nn}.png`);
}
export function useOverridePreviews(dataDir: string | undefined): { has: (slug: string, nn: number) => boolean; url: (slug: string, nn: number) => string } {
  const [bridge, setBridge] = useState<ShellBridge | null>(null);
  const [names, setNames] = useState<Set<string>>(() => new Set());

  useEffect(() => {
    let live = true;
    void getBridge().then((b) => { if (live) setBridge(b); });
    return () => { live = false; };
  }, []);

  useEffect(() => {
    let live = true;
    if (!bridge || !dataDir) { setNames(new Set()); return; }
    void bridge.fs.listDir(overridesDir(dataDir)).then((entries) => {
      if (!live) return;
      const set = new Set<string>();
      for (const e of entries) if (!e.isDir && /\.png$/i.test(e.name)) set.add(e.name.toLowerCase());
      setNames(set);
    }).catch(() => { if (live) setNames(new Set()); });   // missing folder (no skins) -> empty
    return () => { live = false; };
  }, [bridge, dataDir]);

  const has = (slug: string, nn: number): boolean => names.has(`${slug}_livery_${nn}.png`.toLowerCase());
  const url = (slug: string, nn: number): string =>
    (bridge && dataDir && has(slug, nn)) ? bridge.fs.assetUrl(overridePreviewPath(dataDir, slug, nn)) : '';
  return { has, url };
}
