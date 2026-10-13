// This showcase does not include the VR mirror keep-alive (see the licence and README): on real
// hardware, the headset's two mirror panels capture this window by screen-capture, which only
// delivers a new frame when the window repaints -- the real component forces a tiny, harmless
// repaint on a timer so a still screen never shows a stale frame inside the headset. This stub keeps
// the same exported name and prop shape and renders nothing.
export function VrMirrorTick(_props: { enabled: boolean }) {
  return null;
}
