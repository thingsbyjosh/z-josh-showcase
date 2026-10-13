// -- profile slots in the Play window. Reads/writes the SAME files as the engine
// (multi-profile: userdata\saves\profiles\<slug>\profile.json + saves\active_profile.txt) BEFORE
// connecting (there's no WS yet): picking a slot = writing active_profile.txt (the engine resolves the slug
// on injection); an empty slot creates the folder + profile.json {mode:'career'} -> a fresh save -> the Identity
// gate fires the new-career flow. Switching profiles while already connected is still handled by Settings
// (cmd.profile.select hot-reloads the state); this is only for cold start.
import { getBridge, joinPath } from '../shell/bridge';

// 21/07: `difficulty` = the race's REWARD mode (easy/normal/hard). It is chosen AT CREATION, before
// connecting, so it lives in profile.json (the engine seeds it into the save on load). 0.7.0: Settings changes it
// with cmd.settings.setReward, and it is the ENGINE that then rewrites profile.json and the save.
// It scales what you EARN — money, reputation, and XP; the on-track AI is a separate axis (the adaptive difficulty system) untouched by it.
export type CareerDifficulty = 'easy' | 'normal' | 'hard';
export const CAREER_DIFFICULTIES: CareerDifficulty[] = ['easy', 'normal', 'hard'];

export interface ProfileSlot { slug: string; name: string; mode: string; difficulty: CareerDifficulty }
export interface ProfileSlots { active: string; profiles: ProfileSlot[] }

const savesDir = (userdata: string) => joinPath(userdata, 'saves');
const profilesRoot = (userdata: string) => joinPath(userdata, 'saves', 'profiles');
const activeTxt = (userdata: string) => joinPath(savesDir(userdata), 'active_profile.txt');

// Replicates the engine's legacy migration but BY COPYING (the bridge doesn't expose move/delete): if profiles\ doesn't
// exist yet, it copies the flat career*.sav* files into profiles\default\ and writes its profile.json. Once the engine
// finds profiles\ already created, it does NOT re-migrate; the flat files are left orphaned and harmless (their content
// already lives in default).
// "Default" exists ONLY for that migration. A 100% fresh install leaves the root EMPTY -> the gate
// shows 3 empty slots and the FIRST career is born as an auto-named save ("Save N", with no naming prompt).
// the gate does NOT let you play without a profile — PLAY is disabled until a career is selected, so
// the engine's fallback to its old 'default' slug now only serves as a legacy safety net.
export async function ensureProfilesRoot(userdata: string): Promise<void> {
  const b = await getBridge();
  const root = profilesRoot(userdata);
  if (await b.fs.exists(root)) return;
  await b.fs.makeDir(root);
  const def = joinPath(root, 'default');
  let migrated = false;
  try {
    for (const e of await b.fs.listDir(savesDir(userdata))) {
      if (e.isDir) continue;
      if (!/^career.*\.sav/i.test(e.name)) continue;
      if (!migrated) { await b.fs.makeDir(def); migrated = true; }
      await b.fs.copyFile(joinPath(savesDir(userdata), e.name), joinPath(def, e.name));
    }
  } catch { /* saves\ may not exist yet (fresh install) */ }
  if (migrated)
    await b.fs.writeTextFileAtomic(joinPath(def, 'profile.json'), JSON.stringify({ name: 'Default', mode: 'career' }) + '\n');
}

export async function readProfileSlots(userdata: string): Promise<ProfileSlots> {
  const b = await getBridge();
  const root = profilesRoot(userdata);
  const out: ProfileSlots = { active: '', profiles: [] };
  if (!(await b.fs.exists(root))) {
    // unmigrated legacy install: shows "Default" if the flat save exists (the engine will migrate it on injection)
    if (await b.fs.exists(joinPath(savesDir(userdata), 'career.sav')))
      out.profiles.push({ slug: 'default', name: 'Default', mode: 'career', difficulty: 'normal' });
    out.active = out.profiles.length ? 'default' : '';
    return out;
  }
  for (const e of await b.fs.listDir(root)) {
    if (!e.isDir) continue;
    let name = e.name, mode = 'career';
    let difficulty: CareerDifficulty = 'normal';   // a profile from before this change -> Normal (the engine does the same)
    try {
      const pj = JSON.parse(await b.fs.readTextFile(joinPath(root, e.name, 'profile.json')));
      if (typeof pj?.name === 'string' && pj.name) name = pj.name;
      if (typeof pj?.mode === 'string' && pj.mode) mode = pj.mode;
      if (CAREER_DIFFICULTIES.includes(pj?.difficulty)) difficulty = pj.difficulty as CareerDifficulty;
    } catch { /* profile.json missing/corrupt -> defaults derived from the folder name */ }
    out.profiles.push({ slug: e.name, name, mode, difficulty });
  }
  out.profiles.sort((a, z) => (a.slug === 'default' ? -1 : z.slug === 'default' ? 1 : a.slug.localeCompare(z.slug)));
  try { if (await b.fs.exists(activeTxt(userdata))) out.active = (await b.fs.readTextFile(activeTxt(userdata))).trim(); } catch { /* none active */ }
  if (!out.profiles.some((p) => p.slug === out.active)) out.active = out.profiles.length ? out.profiles[0].slug : '';
  return out;
}

export async function activateProfile(userdata: string, slug: string): Promise<void> {
  const b = await getBridge();
  await ensureProfilesRoot(userdata);
  await b.fs.writeTextFileAtomic(activeTxt(userdata), slug);
}

// deletes a profile (folder) from the Play window (pre-connection, via bridge.removeDir). If it was the
// ACTIVE profile, reassigns active to whichever one remains first (or none). The caller must NOT allow deleting the
// last profile (at least one must always remain). Idempotent if the folder is already gone.
export async function deleteProfile(userdata: string, slug: string): Promise<void> {
  const b = await getBridge();
  await ensureProfilesRoot(userdata);
  await b.fs.removeDir(joinPath(profilesRoot(userdata), slug));
  try {
    const cur = (await b.fs.exists(activeTxt(userdata))) ? (await b.fs.readTextFile(activeTxt(userdata))).trim() : '';
    if (cur === slug) {
      const left = await readProfileSlots(userdata);   // re-reads with the deletion already applied
      await b.fs.writeTextFileAtomic(activeTxt(userdata), left.profiles[0]?.slug ?? '');
    }
  } catch { /* best-effort: with no active profile written, readProfileSlots reassigns one on read */ }
}

// Creates a fresh CAREER profile (an empty gate slot) and makes it active. Returns the created slug.
// 21/07: `difficulty` is written to profile.json as the save's choice (afterwards only the engine rewrites it,
// from Settings, preserving the other fields).
export async function createProfile(userdata: string, name: string, difficulty: CareerDifficulty = 'normal'): Promise<string> {
  const b = await getBridge();
  await ensureProfilesRoot(userdata);
  const root = profilesRoot(userdata);
  const base = name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '') || 'career';
  let slug = base;
  for (let i = 2; await b.fs.exists(joinPath(root, slug)); i++) slug = `${base}-${i}`;
  await b.fs.makeDir(joinPath(root, slug));
  await b.fs.writeTextFileAtomic(joinPath(root, slug, 'profile.json'), JSON.stringify({ name, mode: 'career', difficulty }) + '\n');
  await b.fs.writeTextFileAtomic(activeTxt(userdata), slug);
  return slug;
}
