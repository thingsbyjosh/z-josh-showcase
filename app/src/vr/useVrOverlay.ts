// This showcase does not include the SteamVR companion overlay (see the licence and README): the
// real useVrOverlay.ts starts/stops a native module, pushes visibility and dispatches the VR
// pointer, none of which is available without SteamVR hardware. This hook keeps the same exported
// shape and does nothing.
export function useVrOverlay(_client: unknown, _armed: boolean): void {
}
