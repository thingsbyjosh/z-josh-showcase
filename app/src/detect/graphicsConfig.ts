// This showcase does not include local-install detection (see the licence and README): the real
// graphicsConfig.ts reads/writes AMS2's own graphicsconfigdx11.xml on disk, which has no meaning
// without a local game install in this repository's scope. This stub keeps the same exported
// shape; reading always reports "unknown" and writing is a no-op passthrough.
export function readWindowedMode(_xmlText: string): number | null {
  return null;
}

export function setWindowedMode(xmlText: string, _value: number): string {
  return xmlText;
}
