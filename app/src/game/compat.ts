// AMS2 compatibility gate: an AMS2 build is identified by the
// PE FileVersion of AMS2AVX.exe (per a June 2026 runbook). Running the engine against an unvalidated build
// can crash the game -> play() bails out before launching.
// Deliberately FAIL-OPEN: if the version can't be read (null), we do NOT block (an intentional escape hatch).
import compatData from './ams2-compat.json';

export const SUPPORTED_AMS2: string[] = compatData.supported;

export function normalizeFileVersion(v: string): string {
  return v.trim().replace(/,/g, '.');
}

export function isAms2Supported(fileVersion: string | null, supported: string[] = SUPPORTED_AMS2): boolean {
  if (fileVersion === null) return true;
  const norm = normalizeFileVersion(fileVersion);
  return supported.some((s) => normalizeFileVersion(s) === norm);
}

// 12/09 — the AMS2 version to the log ALWAYS, not only when the gate blocks. The fail-open above
// is deliberate, but it has a price we paid in a tester's triage (six Play attempts, zero
// block lines): an UNREADABLE FileVersion passes the gate silently and leaves EXACTLY the same log
// as a validated build. One of the two cases is reassuring and the other is not, so they cannot look
// the same. Here the three states are separated without touching what the gate decides.
export function ams2BuildLine(fileVersion: string | null, supported: string[] = SUPPORTED_AMS2): string {
  if (fileVersion === null) return 'AMS2 build UNREADABLE -> fail-open (the gate cannot judge)';
  const veredicto = isAms2Supported(fileVersion, supported) ? 'validated' : 'NOT validated';
  return `AMS2 build ${normalizeFileVersion(fileVersion)} (${veredicto})`;
}
