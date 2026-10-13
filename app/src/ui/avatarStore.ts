// Profile photo (Discord thread "Custom Avatar picture"): the player chooses an image from their disk and the
// app saves it CROPPED to a square in its data folder (next to config.json — it is the identity of the
// APP, not of the career, so it does not live in the game's userdata). The cropping/scaling is done by a
// webview <canvas>: it decodes any image format the browser understands (jpg/png/
// webp/bmp…) without the Rust side needing a user-image decoder.
import { getBridge, joinPath, BRAND_DIR } from '../shell/bridge';

// Side of the final PNG. Enough for the largest hexagon/avatar in the UI without weighing too much.
export const AVATAR_SIZE = 512;

export function avatarFilePath(localAppData: string): string {
  return joinPath(localAppData, BRAND_DIR, 'avatar.png');
}

// The largest CENTERED square rectangle that fits in a w×h image — "cover" crop: the
// excess of the long sides is lost, bands are never added. PURE so it can be tested without touching a real <canvas>.
export function squareCropRect(width: number, height: number): { sx: number; sy: number; s: number } {
  const s = Math.max(1, Math.min(width, height) | 0);
  return { sx: Math.round((width - s) / 2), sy: Math.round((height - s) / 2), s };
}

// 30/09 (Josh: “Tainted canvases may not be exported”): the image arrives through the assets protocol, which
// is ANOTHER origin than the window. Without requesting it in CORS mode the browser marks the canvas as tainted as
// soon as it is drawn and `toDataURL` throws: the photo could NEVER be set. The protocol already answers with
// Access-Control-Allow-Origin for the window, so it is enough to request it this way BEFORE giving it the src.
export function loadImage(src: string): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.crossOrigin = 'anonymous';
    img.onload = () => resolve(img);
    img.onerror = () => reject(new Error('could not decode the picked image'));
    img.src = src;
  });
}

// Opens the native picker, crops to a square + scales to AVATAR_SIZE and saves it. null = the user
// cancelled the picker (not an error). `dialogTitle` already comes translated from the caller (same pattern
// as dialog.pickFolder in the rest of the app).
export async function pickAndSaveAvatar(localAppData: string, dialogTitle: string): Promise<string | null> {
  const b = await getBridge();
  const picked = await b.avatar.pick(dialogTitle);
  if (!picked) return null;
  const img = await loadImage(b.fs.assetUrl(picked));
  const w = img.naturalWidth || img.width;
  const h = img.naturalHeight || img.height;
  const { sx, sy, s } = squareCropRect(w, h);
  const canvas = document.createElement('canvas');
  canvas.width = AVATAR_SIZE;
  canvas.height = AVATAR_SIZE;
  const ctx = canvas.getContext('2d');
  if (!ctx) throw new Error('canvas 2D unavailable');
  ctx.drawImage(img, sx, sy, s, s, 0, 0, AVATAR_SIZE, AVATAR_SIZE);
  const dataUrl = canvas.toDataURL('image/png');
  const base64 = dataUrl.slice(dataUrl.indexOf(',') + 1);
  const r = await b.avatar.save(localAppData, base64);
  if (!r.ok) throw new Error(r.message || 'could not save the picture');
  return r.path;
}

export async function removeAvatar(localAppData: string): Promise<void> {
  const b = await getBridge();
  const r = await b.avatar.remove(localAppData);
  if (!r.ok) throw new Error(r.message || 'could not remove the picture');
}
