// app breadcrumb to the single support log (%LOCALAPPDATA%\Career Engine\logs\zta_support.log).
// With the engine connected it goes through cmd.log.note (the DLL writes the line: a single writer); without the engine --
// game closed, app startup, skins/assets/account -- it falls back to a direct append via the Rust side (bridge.support).
// ALWAYS best-effort: the log must never break or delay a user's action.
import { getBridge } from '../shell/bridge';
import type { ApiClientLike } from '../api/contracts';

export function supportLog(client: ApiClientLike | null, text: string): void {
  const line = text.length > 300 ? `${text.slice(0, 300)}...` : text;   // 300 -- long paths + a diagnostic suffix
  if (client && client.status === 'connected') {
    void client.cmd('cmd.log.note', { text: line }).catch(() => fallback(line));
    return;
  }
  void fallback(line);
}

async function fallback(line: string): Promise<void> {
  try {
    const b = await getBridge();
    await b.support.log(line);
  } catch {
    // best-effort: without a bridge (or without permissions) the breadcrumb is simply lost
  }
}

// BROKEN IMAGE breadcrumb (the "textures/photos disappear" case): records the EXACT src that failed,
// once per src (a broken list would otherwise fire dozens of times per render), capped per session.
// Wire it into the onError of every content <img> (cars/tracks/packs), not the bundle's own assets.
const seenImgErrors = new Set<string>();
export function logImageError(client: ApiClientLike | null, src: string | null | undefined): void {
  const s = src || '(empty src)';
  if (seenImgErrors.has(s) || seenImgErrors.size >= 40) return;
  seenImgErrors.add(s);
  supportLog(client, `image FAILED to load: ${s}`);
}

/** Tests only: resets the broken-image dedup. */
export function __resetImageErrorLog(): void { seenImgErrors.clear(); }
