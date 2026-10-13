// Colors arriving from the ENGINE in payloads (tierColor/classColor/...) can be 'var(--accent)'.
// In small text that particular red doesn't reach AA contrast on a dark background; this shim bumps it up to
// accent-text ONLY in text sinks -- fills keep using the payload's raw color.
export function accentText(color: string): string {
  return color === 'var(--accent)' ? 'var(--accent-text)' : color;
}
