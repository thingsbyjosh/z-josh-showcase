// This showcase does not include the in-headset VR pointer (see the licence and README): the real
// useVrPointer.ts translates controller-ray pointer events from a live engine into
// synthetic DOM clicks, which is not part of this repository. This hook keeps the same exported
// shape and does nothing.
export function useVrPointer(_client: unknown): void {
}
